#!/usr/bin/env python3
#
# Check that the debug scripts decode what littlefs writes, and reject what
# littlefs rejects.
#
# Images come from the test runner (test.py -d), corrupt images are made
# from them by editing a commit and resealing its checksum, so only the
# rule under test can reject them.
#
# Example:
# ./scripts/test_dbg.py -R runners/test_runner
#
# Copyright (c) 2026, The littlefs authors.
# SPDX-License-Identifier: BSD-3-Clause
#

# prevent local imports
if __name__ == "__main__":
    __import__('sys').path.pop(0)

import importlib.util
import os
import struct
import subprocess
import sys
import tempfile

try:
    import crc32c as crc32c_lib
except ModuleNotFoundError:
    crc32c_lib = None


# images to write, test case followed by any defines
IMAGES = [
    ['test_files_image'],
    ['test_dirs_image'],
    ['test_attrs_image'],
    # a bshrub with a btree inner node
    ['test_fwrite_simple', '-DSIZE=262144', '-DSYNC=1'],
    # an mroot chain
    ['test_mtree_extend', '-DF_FLAGS=0', '-DM_FLAGS=0', '-DREVNOISE=0'],
]

# scripts with their own copy of the rbyd decoder
DECODERS = [
    'dbgrbyd.py',
    'dbgbtree.py',
    'dbgmtree.py',
    'dbglfs3.py',
    'dbgbmap.py',
    'dbgbmapsvg.py',
]

TAG_MAGIC       = 0x0131
TAG_VERSION     = 0x0134
TAG_DID         = 0x0420
TAG_MROOT       = 0x0431
TAG_MTREE       = 0x043c
TAG_SHRUB       = 0x1000
TAG_ALT         = 0x4000
TAG_CKSUM       = 0x3000
TAG_PHASE       = 0x0003
TAG_PERTURB     = 0x0004
TAG_NOTE        = 0x3100
TAG_GCKSUMDELTA = 0x3300

CRC32C_ODDZERO = 0xfca42daf


def crc32c(data, crc=0):
    if crc32c_lib is not None:
        return crc32c_lib.crc32c(data, crc)
    else:
        crc ^= 0xffffffff
        for b in data:
            crc ^= b
            for j in range(8):
                crc = (crc >> 1) ^ ((crc & 1) * 0x82f63b78)
        return 0xffffffff ^ crc

def pmul(a, b):
    r = 0
    while b:
        if b & 1:
            r ^= a
        a <<= 1
        b >>= 1
    return r

def crc32cmul(a, b):
    r = pmul(a, b)
    for _ in range(31):
        r = (r >> 1) ^ ((r & 1) * 0x82f63b78)
    return r

def crc32ccube(a):
    return crc32cmul(crc32cmul(a, a), a)

def parity(x):
    return bin(x).count('1') & 1

def fromle32(data, j=0):
    return struct.unpack('<I', bytes(data[j:j+4]).ljust(4, b'\0'))[0]

def tole32(word):
    return struct.pack('<I', word)

def toleb128(word):
    data = bytearray()
    while True:
        b = word & 0x7f
        word >>= 7
        if word:
            data.append(b | 0x80)
        else:
            data.append(b)
            return bytes(data)

# a leb128 padded to n bytes, which lfs3_fromleb128 accepts
def toleb128_(word, n):
    return bytes([(word >> 7*i) & 0x7f | (0x80 if i < n-1 else 0)
            for i in range(n)])

# lenient leb128, used to step over crafted encodings
def fromleb128(data, j=0):
    word = 0
    d = 0
    while j+d < len(data):
        b = data[j+d]
        word |= (b & 0x7f) << 7*d
        if not b & 0x80:
            return word, d+1
        d += 1
    return word, d

# strict leb128, as lfs3_fromleb128
def fromleb128_(data, j=0):
    word = 0
    for d in range(min(5, len(data)-j)):
        b = data[j+d]
        word = (word | (b & 0x7f) << 7*d) & 0xffffffff
        if not b & 0x80:
            return (word if word >> 7*d == b else None), d+1
    return None, 0

def mktag(tag, weight, size, *,
        wdata=None,
        sdata=None):
    return (struct.pack('>H', tag)
            + (wdata if wdata is not None else toleb128(weight))
            + (sdata if sdata is not None else toleb128(size)))


# walk an rbyd's log the way lfs3_rbyd_fetch_ does, returning the commits
# whose cksum checks out
class Commit:
    def __init__(self, start, tags, trunk, eoff, cksum):
        self.start = start
        # (off, tag, weight, size, d)
        self.tags = tags
        self.trunk = trunk
        self.eoff = eoff
        self.cksum = cksum

def walk(data, block):
    commits = []
    rev = fromle32(data, 0)
    cksum__ = crc32c(data[0:4])
    cksum_ = cksum__
    perturb = False
    trunk_ = 0
    trunk__ = 0
    start = 4
    tags = []
    j = 4
    while j <= len(data)-4:
        tag = struct.unpack('>H', bytes(data[j:j+2]))[0]
        v, tag = tag >> 15, tag & 0x7fff
        w, d1 = fromleb128_(data[j:j+11], 2)
        if w is None or w > 0x7fffffff:
            break
        size, d2 = fromleb128_(data[j:j+11], 2+d1)
        if size is None or size > 0x0fffffff:
            break
        d = 2+d1+d2
        if v != parity(cksum__):
            break
        cksum__ ^= 0x80 if v else 0
        cksum__ = crc32c(data[j:j+d], cksum__)
        alt = tag & TAG_ALT
        if not alt and j+d+size > len(data):
            break
        tags.append((j, tag, w, size, d))

        if not alt:
            if (tag & 0xff00) != TAG_CKSUM:
                cksum__ = crc32c(data[j+d:j+d+size], cksum__)
            else:
                if size < 4 or (tag & TAG_PHASE) != (block & 0x3):
                    break
                if fromle32(data, j+d) != cksum__:
                    break
                commits.append(Commit(start, tags, trunk_, j+d+size,
                        cksum_))
                tags = []
                start = j+d+size
                perturb = bool(tag & TAG_PERTURB)
                cksum__ = cksum_ ^ (CRC32C_ODDZERO if perturb else 0)

        if (tag & 0xf000) != TAG_CKSUM:
            if not trunk__:
                trunk__ = j
            if not alt:
                if not tag & TAG_SHRUB:
                    trunk_ = trunk__
                trunk__ = 0
            cksum_ = cksum__ ^ (CRC32C_ODDZERO if perturb else 0)

        j += d + (0 if alt else size)

    return rev, commits

# rewrite valid bits and cksums up to end, following each tag's own sizes,
# so an edit that keeps the log parseable stays checksum-valid
def reseal(data, block, end):
    cksum__ = crc32c(data[0:4])
    cksum_ = cksum__
    perturb = False
    j = 4
    while j < end:
        tag = struct.unpack('>H', bytes(data[j:j+2]))[0] & 0x7fff
        _, d1 = fromleb128(data, j+2)
        size, d2 = fromleb128(data, j+2+d1)
        d = 2+d1+d2
        v = parity(cksum__)
        data[j] = (data[j] & 0x7f) | (v << 7)
        cksum__ ^= 0x80 if v else 0
        cksum__ = crc32c(data[j:j+d], cksum__)
        alt = tag & TAG_ALT

        if not alt and (tag & 0xff00) == TAG_CKSUM:
            data[j+d:j+d+4] = tole32(cksum__)
            perturb = bool(tag & TAG_PERTURB)
            cksum__ = cksum_ ^ (CRC32C_ODDZERO if perturb else 0)
            j += d + size
            continue

        if not alt:
            cksum__ = crc32c(data[j+d:j+d+size], cksum__)
        if (tag & 0xf000) != TAG_CKSUM:
            cksum_ = cksum__ ^ (CRC32C_ODDZERO if perturb else 0)
        j += d + (0 if alt else size)

# build an rbyd from raw tags, each commit gets a cksum tag
def mkrbyd(block, commits, *,
        block_size=512,
        rev=1):
    data = bytearray(tole32(rev))
    for commit in commits:
        for raw in commit:
            data += raw
        data += mktag(TAG_CKSUM | (block & 0x3), 0, 4) + tole32(0)
    reseal(data, block, len(data))
    return bytes(data.ljust(block_size, b'\xff'))


# a littlefs image as a list of blocks
class Image:
    def __init__(self, path, block_size):
        with open(path, 'rb') as f:
            data = f.read()
        self.block_size = block_size
        self.blocks = [bytearray(data[i:i+block_size])
                for i in range(0, len(data), block_size)]

    def write(self, path):
        with open(path, 'wb') as f:
            for b in self.blocks:
                f.write(b)

    def copy(self):
        image = Image.__new__(Image)
        image.block_size = self.block_size
        image.blocks = [bytearray(b) for b in self.blocks]
        return image

    # the active mroot anchor block, by revision count
    def mroot(self):
        revs = []
        for block in [0, 1]:
            rev, commits = walk(self.blocks[block], block)
            revs.append((rev, commits))
        if not revs[1][1] or (revs[0][1]
                and (revs[1][0] - revs[0][0]) & 0x80000000):
            return 0, revs[0][1]
        else:
            return 1, revs[1][1]

    # the active block of an mdir, by revision count
    def active(self, blocks):
        best = None
        for block in blocks:
            rev, commits = walk(self.blocks[block], block)
            if commits and (best is None
                    or (rev != best[0]
                        and not (rev - best[0]) & 0x80000000)):
                best = (rev, block, commits)
        return best[1], best[2]

    # the mroot chain as (block, commits) of each mdir's active block,
    # None if there is an mtree
    def chain(self):
        chain = []
        blocks = (0, 1)
        while len(chain) < 8:
            block, commits = self.active(blocks)
            chain.append((block, commits))
            if self.find(block, TAG_MTREE) is not None:
                return None
            mptr = self.find(block, TAG_MROOT)
            if mptr is None:
                return chain
            a, d = fromleb128(mptr, 0)
            b, _ = fromleb128(mptr, d)
            blocks = (a, b)
        return None

    # reseal an mdir in a chain, fixing the first gcksumdelta found in
    # the last commits of the chain so the gcksum still checks out,
    # gcksumdeltas are not part of the cksums they fix
    def resealchain(self, chain, block, end):
        cksum = dict(chain)[block][-1].cksum
        reseal(self.blocks[block], block, end)
        _, commits = walk(self.blocks[block], block)
        cksum_ = commits[-1].cksum
        total = 0
        for _, commits in chain:
            total ^= commits[-1].cksum
        total_ = total ^ cksum ^ cksum_
        for block_, _ in sorted(chain, key=lambda c: c[0] != block):
            data = self.blocks[block_]
            _, commits = walk(data, block_)
            for off, tag, _, size, d in commits[-1].tags:
                if tag == TAG_GCKSUMDELTA:
                    delta = fromle32(data, off+d)
                    delta ^= crc32ccube(total) ^ crc32ccube(total_)
                    data[off+d:off+d+4] = tole32(delta)
                    reseal(data, block_, commits[-1].eoff)
                    return

    # the payload of the newest tag matching tag in a block's valid log
    def find(self, block, tag):
        _, commits = walk(self.blocks[block], block)
        found = None
        for commit in commits:
            for off, tag_, _, size, d in commit.tags:
                if tag_ == tag:
                    found = bytes(self.blocks[block][off+d:off+d+size])
        return found

    # reseal a block, fixing the last commit's gcksumdelta so the gcksum
    # still checks out, this assumes the block is the only mdir
    def reseal(self, block, end, cksum):
        data = self.blocks[block]
        reseal(data, block, end)
        _, commits = walk(data, block)
        last = commits[-1]
        cksum_ = last.cksum
        for off, tag, _, size, d in last.tags:
            if tag == TAG_GCKSUMDELTA:
                delta = fromle32(data, off+d)
                delta ^= crc32ccube(cksum) ^ crc32ccube(cksum_)
                data[off+d:off+d+4] = tole32(delta)
        reseal(data, block, end)


class Checker:
    def __init__(self, scripts, dir, block_size, verbose=False):
        self.scripts = scripts
        self.dir = dir
        self.block_size = block_size
        self.verbose = verbose
        self.passed = 0
        self.failed = 0
        self.modules = {}

    def check(self, name, ok, detail=''):
        if ok:
            self.passed += 1
            if self.verbose:
                print('pass  %s' % name)
        else:
            self.failed += 1
            print('FAIL  %s%s' % (name, ': %s' % detail if detail else ''))

    def run(self, script, *args):
        r = subprocess.run(
                [sys.executable, os.path.join(self.scripts, script),
                    *map(str, args)],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True)
        return r.returncode, r.stdout, r.stderr

    def module(self, script):
        if script not in self.modules:
            spec = importlib.util.spec_from_file_location(
                    'dbg_' + script[:-3],
                    os.path.join(self.scripts, script))
            mod = importlib.util.module_from_spec(spec)
            # the tag reprs parse their module's source
            sys.modules[spec.name] = mod
            spec.loader.exec_module(mod)
            self.modules[script] = mod
        return self.modules[script]

    # every script must decode an image littlefs wrote without errors
    def ck_good(self, name, path):
        bs = '-b%d' % self.block_size
        for args in [
                ['dbglfs3.py', bs, '--ckdata', '-q', path],
                ['dbglfs3.py', bs, '-e', '-a', '-r0', '--structs',
                    '--attrs', '--config', '--gstate', path],
                ['dbgmtree.py', bs, '-e', '-q', path],
                ['dbgrbyd.py', bs, '-e', '-q', path, '0', '1'],
                ['dbgbmap.py', bs, '-e', path],
                ['dbgbmapsvg.py', bs, '-e', '-o',
                    os.path.join(self.dir, 'out.svg'), path]]:
            rc, _, err = self.run(*args)
            self.check('%s: %s' % (name, ' '.join(a for a in args
                        if a not in [bs, path] and not a.endswith('.svg'))),
                    rc == 0,
                    'rc %d %s' % (rc, err.strip().splitlines()[-1:]))

        # and the mtree on its own, if there is one
        image = Image(path, self.block_size)
        block, _ = image.mroot()
        mtree = image.find(block, TAG_MTREE)
        if mtree is not None:
            d = 0
            _, d_ = fromleb128(mtree, d); d += d_
            b, d_ = fromleb128(mtree, d); d += d_
            t, d_ = fromleb128(mtree, d); d += d_
            rc, _, err = self.run('dbgbtree.py', bs, '-e', '-q', path,
                    '0x%x.%x' % (b, t))
            self.check('%s: dbgbtree.py -e' % name,
                    rc == 0,
                    'rc %d %s' % (rc, err.strip().splitlines()[-1:]))

    # a commit littlefs rejects must look like a commit that isn't there
    def ck_same(self, name, x, y):
        bs = '-b%d' % self.block_size
        px = os.path.join(self.dir, 'x.disk')
        py = os.path.join(self.dir, 'y.disk')
        x.write(px)
        y.write(py)
        for args in [
                ['dbgrbyd.py', bs, '0', '1'],
                ['dbglfs3.py', bs, '-a', '--files', '--config',
                    '--gstate']]:
            rx, ox, ex = self.run(*args[:2], px, *args[2:])
            ry, oy, ey = self.run(*args[:2], py, *args[2:])
            self.check('%s: %s' % (name, args[0]),
                    (rx, ox) == (ry, oy),
                    'shows %r, expected %r' % (
                        (ox.splitlines()[:1] or ex.strip().splitlines()[-1:]),
                        oy.splitlines()[:1]))

    # a commit the driver's fetch keeps must be kept by every decoder,
    # with the same trunk, eoff and cksum
    def ck_kept(self, name, x, block):
        _, commits = walk(x.blocks[block], block)
        expected = (block, commits[-1].trunk, commits[-1].eoff,
                commits[-1].cksum)
        px = os.path.join(self.dir, 'x.disk')
        x.write(px)
        with open(px, 'rb') as f:
            for script in DECODERS:
                mod = self.module(script)
                rbyd = mod.Rbyd.fetch(mod.Bd(f, self.block_size), (0, 1))
                found = (rbyd.block, rbyd.trunk, rbyd.eoff, rbyd.cksum)
                self.check('%s: %s' % (name, script),
                        found == expected,
                        'fetched 0x%x.%x eoff 0x%x cksum %08x, '
                            'expected 0x%x.%x eoff 0x%x cksum %08x' % (
                            *found, *expected))

    # crafted variants of the mroot's last commit
    def ck_commits(self, name, path):
        image = Image(path, self.block_size)
        block, commits = image.mroot()
        if len(commits) < 2:
            return
        prev, last = commits[-2], commits[-1]

        # without the last commit
        y = image.copy()
        y.blocks[block][prev.eoff:last.eoff] = (
                b'\xff' * (last.eoff-prev.eoff))

        def edit(off, f, end=last.eoff):
            x = image.copy()
            data = x.blocks[block]
            data[off] = f(data[off])
            reseal(data, block, end)
            return x

        alts = [t for t in last.tags if t[1] & TAG_ALT]
        leaves = [t for t in last.tags
                if not t[1] & TAG_ALT and (t[1] & 0xf000) != TAG_CKSUM]
        notes = [t for t in last.tags
                if not t[1] & TAG_ALT and t[1] == TAG_GCKSUMDELTA]
        cksums = [t for t in last.tags
                if not t[1] & TAG_ALT and (t[1] & 0xff00) == TAG_CKSUM]
        # bit 7 of a tag is reserved, but only commits refuse it, a fetch
        # keeps a commit holding one, see lfs3_rbyd_fetch_
        for what, tags in [
                ('an alt', alts[:1]),
                ('a leaf', leaves[-1:]),
                ('a gcksumdelta', notes[-1:])]:
            if tags:
                self.ck_kept('%s: bit 7 in %s' % (name, what),
                        edit(tags[0][0]+1, lambda b: b | 0x80), block)

        # a gcksumdelta with bit 7 is an unknown cksum tag, ignored, so
        # the gcksum no longer checks out unless the delta was zero
        if notes:
            off, _, _, _, d = notes[-1]
            delta = fromle32(image.blocks[block], off+d)
            x = edit(off+1, lambda b: b | 0x80)
            px = os.path.join(self.dir, 'x.disk')
            x.write(px)
            for args in [
                    ['dbglfs3.py', '-e'],
                    ['dbgbmap.py', '-e'],
                    ['dbgbmapsvg.py', '-e', '-o',
                        os.path.join(self.dir, 'out.svg')]]:
                rc, out, _ = self.run(args[0], '-b%d' % self.block_size,
                        *args[1:], px)
                # dbglfs3 marks a gcksum mismatch with ?
                head = (out.splitlines() or [''])[0]
                ok = rc == (2 if delta else 0)
                if args[0] == 'dbglfs3.py':
                    ok = (ok and head.startswith('littlefs ')
                            and head.endswith('?') == bool(delta))
                self.check('%s: bit 7 in a gcksumdelta: %s' % (
                            name, ' '.join(a for a in args
                                if not a.endswith('.svg'))),
                        ok,
                        'rc %d %r, expected %s' % (rc, head,
                            'a gcksum mismatch' if delta else 'no error'))

        # cksum phase must match the block, see lfs3_rbyd_fetch_
        off, _, _, size, d = cksums[-1]
        self.ck_same('%s: cksum phase' % name,
                edit(off+1, lambda b: (b & ~0x3) | ((block+1) & 0x3)), y)

        # cksums must be at least 4 bytes, littlefs pads the size's
        # leb128, so this keeps the tag's length
        _, d1 = fromleb128(image.blocks[block], off+2)
        self.ck_same('%s: cksum size 3' % name,
                edit(off+2+d1, lambda b: (b & 0x80) | 3, end=off+d+3), y)

        # appended commits with tags the driver can't read
        for what, raws in [
                ('weight > 31 bits', [mktag(TAG_NOTE, 0, 0,
                    wdata=b'\x80\x80\x80\x80\x08')]),
                ('alt jump > 28 bits', [mktag(TAG_ALT | TAG_DID, 0, 0,
                        sdata=b'\x80\x80\x80\x80\x01'),
                    mktag(0, 0, 0)]),
                ('overlong leb128', [mktag(TAG_NOTE, 0, 0,
                    wdata=b'\x80\x80\x80\x80\x80\x00')]),
                ('overflowed leb128', [mktag(TAG_NOTE, 0, 0,
                    wdata=b'\x80\x80\x80\x80\x10')])]:
            x = image.copy()
            data = x.blocks[block]
            commit = b''.join(raws) + mktag(
                    TAG_CKSUM | (block & 0x3), 0, 4) + tole32(0)
            data[last.eoff:last.eoff+len(commit)] = commit
            reseal(data, block, last.eoff+len(commit))
            self.ck_same('%s: appended %s' % (name, what), x, image)

        # magic must match exactly, redund bits included, the gcksum
        # fixup needs the mroot to be the only mdir
        if (image.find(block, TAG_MROOT) is not None
                or image.find(block, TAG_MTREE) is not None):
            return
        x = image.copy()
        data = x.blocks[block]
        for commit in commits:
            for off, tag, _, _, _ in commit.tags:
                if tag == TAG_MAGIC:
                    data[off+1] = (data[off+1] & ~0x3) | 0x2
        x.reseal(block, last.eoff, last.cksum)
        px = os.path.join(self.dir, 'x.disk')
        x.write(px)
        rc, _, _ = self.run('dbglfs3.py', '-b%d' % self.block_size,
                '-e', '-q', px)
        self.check('%s: magic tag 0x%04x' % (name, TAG_MAGIC & ~0x3 | 0x2),
                rc == 2,
                'rc %d, expected 2' % rc)

    # an image littlefs refuses to mount must be flagged by -e
    def ck_refused(self, name, x, args):
        px = os.path.join(self.dir, 'x.disk')
        x.write(px)
        for args_ in args:
            rc, _, err = self.run(args_[0], '-b%d' % self.block_size,
                    *args_[1:], px)
            self.check('%s: %s' % (name, ' '.join(a for a in args_
                        if not a.endswith('.svg'))),
                    rc == 2,
                    'rc %d, expected 2 %s' % (
                        rc, err.strip().splitlines()[-1:]))

    # crafted variants of the mroot chain's config and mptrs
    def ck_chain(self, name, path):
        image = Image(path, self.block_size)
        chain = image.chain()
        if chain is None:
            return
        refused = [
                ['dbglfs3.py', '-e', '-q'],
                ['dbgbmap.py', '-e'],
                ['dbgbmapsvg.py', '-e', '-o',
                    os.path.join(self.dir, 'out.svg')]]

        # versions lfs3_mountmroot refuses
        block, commits = chain[-1]
        for version in [b'\x01\x00', b'\x00\x01', b'\xff\x00']:
            x = image.copy()
            data = x.blocks[block]
            for commit in commits:
                for off, tag, _, size, d in commit.tags:
                    if tag == TAG_VERSION and size >= 2:
                        data[off+d:off+d+2] = version
            x.resealchain(chain, block, commits[-1].eoff)
            self.ck_refused('%s: version v%d.%d' % (
                        name, version[0], version[1]),
                    x, refused)

        # an mptr with one block, lfs3_data_readmptr reads two
        if len(chain) < 2:
            return
        block, commits = chain[0]
        child, _ = chain[1]
        x = image.copy()
        data = x.blocks[block]
        last = None
        for commit in commits:
            for off, tag, _, size, d in commit.tags:
                if tag == TAG_MROOT:
                    last = (off, size, d)
        off, size, d = last
        data[off+d:off+d+size] = toleb128_(child, size)
        x.resealchain(chain, block, commits[-1].eoff)
        self.ck_refused('%s: mptr of one block' % name, x,
                refused + [['dbgmtree.py', '-e', '-q']])

    # a btree node whose commit's cksum fails must be corrupt, even if
    # the cksum up to the trunk matches
    def ck_torn(self, name, path, tag, args):
        image = Image(path, self.block_size)
        block, _ = image.mroot()
        data = image.find(block, tag)
        if data is None:
            return
        d = 0
        if tag == TAG_MTREE:
            _, d_ = fromleb128(data, d); d += d_
        b, d_ = fromleb128(data, d); d += d_
        t, d_ = fromleb128(data, d); d += d_
        _, commits = walk(image.blocks[b], b)
        for commit in commits:
            if commit.start <= t < commit.eoff:
                off, _, _, _, d = commit.tags[-1]
                image.blocks[b][off+d] ^= 0x01
                break
        else:
            self.check('%s: torn node 0x%x.%x' % (name, b, t), False,
                    'no commit')
            return
        px = os.path.join(self.dir, 'x.disk')
        image.write(px)
        for args_ in args:
            rc, _, _ = self.run(args_[0], '-b%d' % self.block_size,
                    *args_[1:], px)
            self.check('%s: torn node 0x%x.%x: %s' % (
                        name, b, t, ' '.join(a for a in args_
                            if not a.endswith('.svg'))),
                    rc == 2,
                    'rc %d, expected 2' % rc)

    # decoder internals, against crafted rbyds
    def ck_decoders(self):
        block = 2
        did = mktag(TAG_DID, 1, 1) + b'\x00'
        for script in DECODERS:
            mod = self.module(script)

            # fetching at a trunk must check that commit's cksum
            data = bytearray(mkrbyd(block, [[did], [did]]))
            _, commits = walk(data, block)
            a, b = commits
            data[b.eoff-4] ^= 0x01
            rbyd = mod.Rbyd._fetch(bytes(data), block, b.trunk)
            self.check('%s: fetch at a torn trunk' % script,
                    rbyd.trunk == a.trunk and rbyd.eoff == a.eoff,
                    'trunk 0x%x eoff 0x%x, expected 0x%x 0x%x' % (
                        rbyd.trunk, rbyd.eoff, a.trunk, a.eoff))

            # a shrub null tag is not an entry
            data = mkrbyd(block, [[mktag(TAG_SHRUB, 1, 0), did]])
            shrub = mod.Rbyd._fetch(data, block, 4)
            rid, rattr = shrub.lookupnext(0)
            self.check('%s: shrub null tag' % script,
                    rid is None,
                    'found rid %s tag 0x%04x' % (
                        rid, rattr.tag if rattr else 0))

            # struct tags match exactly
            t = mod.Tag.find(0x040c, default=None)
            self.check('%s: tag 0x040c' % script,
                    t is None or t.name != 'TAG_BLOCK',
                    'decodes as %s' % (t.name if t else None))

            # mptrs are two leb128s, anything shorter is corrupt
            if hasattr(mod, 'frommdir'):
                blocks, _ = mod.frommdir(b'\x02\x03\x04')
                self.check('%s: mptr of three blocks' % script,
                        blocks == (2, 3),
                        'decodes as %r' % (blocks,))
                for data in [b'\x02', b'\x82\x00', b'', b'\x02\x83']:
                    blocks, _ = mod.frommdir(data)
                    self.check('%s: mptr %s' % (script, data.hex()),
                            blocks is None,
                            'decodes as %r' % (blocks,))

            # version is two bytes, missing bytes are zero
            if hasattr(mod, 'Config'):
                class Rattr:
                    def __init__(self, data):
                        self.data = data
                for data, version in [
                        (b'\x00\x00', (0, 0)),
                        (b'\x00\x80', (0, 128)),
                        (b'\x81\x01', (129, 1)),
                        (b'\x00', (0, 0))]:
                    v = mod.Config.Version(None, TAG_VERSION, Rattr(data))
                    self.check('%s: version %s' % (script, data.hex()),
                            (v.major, v.minor) == version,
                            'decodes as v%s.%s' % (v.major, v.minor))

        mod = self.module('dbgtag.py')
        t = mod.Tag.find(0x040c, default=None)
        self.check('dbgtag.py: tag 0x040c',
                t is None or t.name != 'TAG_BLOCK',
                'decodes as %s' % (t.name if t else None))


def main(runners=None, *,
        scripts=None,
        dir=None,
        block_size=4096,
        verbose=False):
    here = os.path.dirname(os.path.abspath(__file__))
    scripts = os.path.abspath(scripts or here)
    runners = runners or ['runners/test_runner']

    with tempfile.TemporaryDirectory() as tmp:
        dir = dir or tmp
        os.makedirs(dir, exist_ok=True)
        ck = Checker(scripts, dir, block_size, verbose)

        ck.ck_decoders()

        for i, runner in enumerate(runners):
            for case in IMAGES:
                name = '%s%s' % (case[0], '@%d' % i if i else '')
                path = os.path.join(dir, '%s-%d.disk' % (case[0], i))
                r = subprocess.run(
                        [sys.executable, os.path.join(here, 'test.py'),
                            '-R', runner, '-Pnone', '-d', path, *case],
                        stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT,
                        text=True)
                if r.returncode != 0 or not os.path.exists(path):
                    ck.check('%s: write image' % name, False,
                            r.stdout.strip().splitlines()[-1:])
                    continue

                ck.ck_good(name, path)
                ck.ck_commits(name, path)
                ck.ck_chain(name, path)
                ck.ck_torn(name, path, TAG_MTREE, [
                        ['dbglfs3.py', '--ckmeta', '-q'],
                        ['dbgmtree.py', '-e', '-q'],
                        ['dbgbmap.py', '-e'],
                        ['dbgbmapsvg.py', '-e', '-o',
                            os.path.join(dir, 'out.svg')]])

    print('dbg checks: %d/%d passed' % (ck.passed, ck.passed+ck.failed))
    return 1 if ck.failed else 0


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(
            description="Check the debug scripts against littlefs.",
            allow_abbrev=False)
    parser.add_argument(
            '-R', '--runner',
            dest='runners',
            action='append',
            help="Test runner that writes the images, may be given more "
                "than once, for example once with LFS3_YES_GBMAP. Defaults "
                "to runners/test_runner.")
    parser.add_argument(
            '-S', '--scripts',
            help="Directory of the scripts to check. Defaults to the "
                "directory of this script.")
    parser.add_argument(
            '-d', '--dir',
            help="Directory for images. Defaults to a temporary directory.")
    parser.add_argument(
            '-b', '--block-size',
            type=lambda x: int(x, 0),
            default=4096,
            help="Block size of the runner's images. Defaults to 4096.")
    parser.add_argument(
            '-v', '--verbose',
            action='store_true',
            help="Show passing checks too.")
    sys.exit(main(**{k: v
            for k, v in vars(parser.parse_intermixed_args()).items()
            if v is not None}))
