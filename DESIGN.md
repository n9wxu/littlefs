## The design of littlefs

A little fail-safe filesystem designed for microcontrollers.

```
   | | |     .---._____
  .-----.   |          |
--|o    |---| littlefs |
--|     |---|    v3    |
  '-----'   '----------'
   | | |
```

littlefs was originally built as an experiment to learn about filesystem design
in the context of microcontrollers. The question was: How would you build a
filesystem that is resilient to power-loss and flash wear without using
unbounded memory?

littlefs v2 answered that question well enough to end up in a lot of devices.
It also taught us where the answer was incomplete. littlefs v3 is more or less
a rewrite of the filesystem from the ground up: the goals are the same, the
on-disk data structures are almost entirely new, and the on-disk format is not
compatible with v2.

This document covers the high-level design of littlefs v3, how it differs from
v2 and from other filesystems, and the design decisions that got us here. For
the low-level details covering every bit on disk, check out
[SPEC.md](SPEC.md). The design of v2 is still described in the DESIGN.md on
the [v2 branch][v2-design].

A note on status: v3 is still in alpha. Images are marked with disk version
v0.0 to say they are experimental, and a released v3 will refuse to mount
them. Where this document describes something that is planned rather than
implemented, it says so.

## The problem

The embedded systems littlefs targets are usually 32-bit microcontrollers with
around 32 KiB of RAM and 512 KiB of ROM. These are often paired with SPI NOR
flash chips with about 4 MiB of flash storage, and more and more often with
SPI NAND, SD cards and eMMC, where storage is measured in hundreds of MiB and
blocks can be 128 KiB or larger. These devices are too small for Linux and
most existing filesystems, requiring code written specifically with size in
mind.

Flash itself is an interesting piece of technology with its own quirks and
nuance. Unlike other forms of storage, writing to flash requires two
operations: erasing and programming. Programming (setting bits to 0) is
relatively cheap and can be very granular. Erasing however (setting bits to 1),
requires an expensive and destructive operation which gives flash its name.
[Wikipedia][wikipedia-flash] has more information on how exactly flash works.

To make the situation more annoying, it's very common for these embedded
systems to lose power at any time. Usually, microcontroller code is simple and
reactive, with no concept of a shutdown routine. This presents a big challenge
for persistent storage, where an unlucky power loss can corrupt the storage and
leave a device unrecoverable.

This leaves us with the same three major requirements v2 started with:

1. **Power-loss resilience** - On these systems, power can be lost at any time.
   If a power loss corrupts any persistent data structures, this can cause the
   device to become unrecoverable. An embedded filesystem must be designed to
   recover from a power loss during any write operation.

1. **Wear leveling** - Writing to flash is destructive. If a filesystem
   repeatedly writes to the same block, eventually that block will wear out.
   Filesystems that don't take wear into account can easily burn through blocks
   used to store frequently updated metadata and cause a device's early death.

1. **Bounded RAM/ROM** - These systems also have very limited amounts of
   memory. All RAM usage is bounded: it does not grow as the filesystem
   changes in size or number of files. And, since some users can't use a heap
   at all, littlefs must be able to run with every buffer provided statically
   (`LFS3_NO_MALLOC`).

Experience with v2 added three more:

4. **Scaling with block size** - Everything littlefs does must stay tractable
   as blocks grow from 4 KiB (NOR) to 128 KiB-1 MiB (NAND). An operation that
   is _O(b&sup2;)_ in the block size is a performance problem on NOR and a
   usability problem on NAND.

5. **Small, frequent syncs** - The single most common use of littlefs is
   appending small records to a file and syncing so they survive a power
   loss. The cost of that sync, in erases and in latency, matters more than
   almost anything else.

6. **Error detection** - Flash wears out and bits rot. littlefs should be able
   to tell when what it reads back is not what it wrote, for metadata and for
   data, and should not silently serve an older version of the filesystem.

## Existing designs?

The v2 DESIGN.md surveys the existing designs in some detail. The short
version:

1. **Block-based filesystems** such as [FAT] and [ext2] update blocks in
   place. They are small and fast, but not power-loss resilient, and the fixed
   binding of data to storage location prevents any wear leveling.

2. **Logging filesystems** such as [JFFS], [YAFFS] and [SPIFFS] append every
   change to a log that spans the whole disk. They are power-loss resilient
   and level wear perfectly, but garbage collecting a log costs either
   _O(n&sup2;)_ runtime or _O(n)_ RAM.

3. **Journaling filesystems** such as [ext4] and [NTFS] add a bounded log to a
   block-based filesystem. They recover from power loss, but are effectively
   two filesystems in one (code size), and still bind data to location (no
   wear leveling).

4. **Copy-on-write (COW) filesystems** such as [btrfs] and [ZFS] never update
   in place, instead copying and replacing any block they change. They are
   power-loss resilient and free to move data, but every update propagates up
   to the root, and that concentrates writes (and wear) on the blocks near
   the root.

littlefs v2's answer was to combine the last two. At the sub-block level,
small two-block logs (metadata pairs) provide atomic updates. At the
super-block level, a copy-on-bounded-writes (CObW) tree of blocks only copies
a node after _n_ writes to it, which divides the upward propagation of wear by
_n_ at every level.

v3 keeps this idea and pushes it further. In v2 the logs were flat and had to
be scanned, and the COW structures were append-only skip-lists. In v3 the logs
are themselves balanced trees, and the COW structures are B-trees whose nodes
are those same logs:

```
                anchor 0x{0,1}
               .--------.
              .| mroot  |      config, global state, mtree root
              ||        |
              |'--------'
              '----|---'
                   v
              .--------.
              | mtree  |       B-tree of metadata logs, keyed by mid
              |  node  |
              '--------'
             .-'      '-.
            v            v
       .--------.    .--------.
      .|  mdir  |   .|  mdir  |   metadata logs: names, attributes,
      ||        |   ||        |   small files and B-tree roots
      |'--------'   |'--------'
      '---|----'    '---|----'
          v             v
      .--------.    .--------.
      | B-tree |    |  data  |    file B-tree nodes (also logs)
      |  node  |    |        |    and raw data blocks
      '--------'    '--------'
       .-'   '-.
      v         v
 .--------. .--------.
 |  data  | |  data  |
 '--------' '--------'
```

Before we get into how these pieces work, it's worth looking at why v2's
pieces had to change.

## What v2 learned the hard way

v2's design holds up well for small blocks and small filesystems. The trouble
is that each of its data structures has a cost that grows with something
users care about, and users kept finding those costs.

1. **Metadata compaction is _O(n&sup2;)_.** v2's metadata pairs are flat logs.
   To find the most recent version of an entry, v2 scans the log, and to
   compact a log it checks every entry against every newer entry. With 4 KiB
   blocks this is fine. With 128 KiB NAND blocks a single compaction can take
   seconds, sometimes minutes. As the maintainer summarized the two big
   bottlenecks in v2:

   > Metadata compaction scales _O(n&sup2;)_ with the block size.
   > `metadata_max` exists as a hack to limit this runaway performance, at a
   > cost of wasting most of the space in metadata blocks. ([#1079])

2. **CTZ skip-lists are only fast at appending.** v2 stores files in
   backwards linked skip-lists, with the pointers embedded in the data blocks.
   Appending is _O(1)_, but modifying a block in the middle requires copying
   every block after it: a random write is _O(n)_ in the size of the file
   ([#852]).

3. **A sync freezes the tail of the file.** When a file is synced, its last,
   partially filled block becomes part of the committed file. v2 doesn't
   track whether the rest of that block is still erased (and with a program
   size above one byte, the sync had to pad it anyway), so the next write
   copies the whole block to a new one. For a log that is synced after every
   record, that is one block erase and copy per sync, no matter how small the
   record is ([#344], [#374], [#581], [#905], [#1012]). Back in 2019, the
   maintainer sketched the eventual fix:

   > Thinking generally, we could also repurpose the inline file mechanism used
   > to store small files to also store data written to the end of files.
   > ([#344])

   The worked example near the end of this document measures this cost: on
   NOR flash, logging one 22-byte row per second with a sync after each row,
   v2 erases ~63 blocks per minute.

4. **Inline files are limited by RAM.** v2 stores small files directly in
   their directory's metadata pair, but an inline file has to fit in the file
   cache, and it is rewritten in full every time it is synced.

5. **The threaded linked-list is fragile.** v2 threads a linked-list through
   every metadata pair so the filesystem can be traversed in constant RAM.
   Keeping that list consistent across power loss needs orphans,
   half-orphans, a deorphaning scan and a lot of care:

   > I'm not sure how much users are aware of it, but the previous threaded
   > file tree was a real _pain-in-the-ass_ with the amount of bugs it caused.
   > Turns out having a fully-connected graph in a CoBW filesystem is a really
   > bad idea. ([#1111])

   It also means every directory needs at least one metadata pair of its own.

6. **Free space is rediscovered on every mount.** The lookahead allocator
   finds free blocks by traversing the whole filesystem, and nothing about
   free blocks survives a power cycle. That rules out pre-erasing blocks:
   without a record of which blocks were erased, a device that loses power
   often could keep re-erasing blocks and wear them out ([#1111]).

7. **Only metadata is checksummed, and only locally.** Each metadata commit
   has a CRC, but data blocks have none, and nothing ties the metadata pairs
   together. A pair can fall back to an older valid commit and every pair
   still checks out.

8. **The sync model is ill-defined.** What another open handle sees after a
   sync, and what exists on disk after a power loss in the middle of creating
   a file (a zero-sized file, [#908]), were never pinned down.

None of these are fixed by tuning. They come from the data structures, so v3
replaces the data structures. The rest of this document walks through the
replacements, roughly bottom-up.

## Rbyds: logs that are also trees

Metadata pairs were the backbone of v2, and logs are still the backbone of v3.
Logs are what let littlefs make atomic updates without a journal: append the
change, append a checksum, and the change exists if and only if the checksum
landed. The problem in v2 was never the log. It was that a flat log has to be
read from start to end to answer any question about it.

What we want is a log that we can search like a tree. It turns out we can have
one.

The idea comes from Daniel Beer's [Dhara FTL][dhara]. Dhara stores its sector
map as a [log-encoded radix tree][dhara-tree]: every time it writes a page to
its journal, it also writes the path from the root of the map to that page,
with "alt pointers" to the parts of the tree it didn't change. The newest page
is always the root of the newest version of the tree, and older pages are
reached through the alt pointers. Nothing is ever rewritten, and a lookup only
touches one path.

littlefs v3 builds its metadata out of the same trick, with two changes. The
tree is a balanced binary tree ([red-black][rb-tree]) instead of a radix tree,
so it works with variable-length keys and doesn't need a fixed number of alt
pointers per entry. And every subtree carries a weight, which makes it a
[counted][cb-tree] (or [order-statistic][os-tree]) tree. We call the result a
red-black-yellow Dhara tree, or rbyd. (Yellow is what you get when two reds
collide. We'll get to that.)

---

An rbyd lives in a single block. Everything in it is a tag: a small header
(type, weight, size) followed by some data. The tags are sorted by a key made
of a record id (rid) and the tag's type. Most tags are leaves that hold
metadata, but some are alt pointers, which hold:

1. A color, red or black.
2. A direction, either "less than or equal" or "greater than".
3. A key to compare against.
4. A weight, the number of rids in the subtree it points to.
5. A jump: how far _back_ in the block the subtree it points to starts.

Every tag appended to the log is written as a trunk: a run of alt pointers
ending in the new leaf. The most recent trunk is the root of the whole tree.
To look something up,
we start at the last trunk and read forward. At each alt pointer we compare our
key against the alt's key. If the alt says our key is on its side, we follow
the jump back into an older part of the log. Otherwise we fall through to the
next tag in the current trunk. Either way we eventually arrive at a leaf.

Here's a small rbyd after four commits, appending the tags a, b, c and d:

```
on disk, appended left to right:

.-----.-----.-----------.-----------------.-----------------------.------
| rev |  a  |  <=a   b  |  <=a   <=b   c  |  <=b        <=c     d  |
'-----'-----'-----------'-----------------'-----------------------'------
                                           ^
                                           trunk of the last commit

the tree seen from the last trunk:

      <=b ---------------.      each alt either falls through (down)
       |                 |      or jumps back into the log (right)
      <=c -----.        <=a ---.
       |       |         |     |
       d       c         b     a
```

The first alt of the last trunk, `<=b`, jumps back into the trunk written by
the second commit, which is itself a complete tree over a and b. The second
alt, `<=c`, jumps back to the leaf c written by the third commit. The alts
written by commits 2 and 3 that nothing points to any more are garbage, and
will be cleaned up when the block is compacted.

Because the tree is balanced, the path from the last trunk to any leaf is
_O(log n)_ tags long, where _n_ is the number of tags in the tree. Lookups
read _O(log n)_ tags, and appends write _O(log n)_ tags.

---

How do we keep the tree balanced when we can't rewrite anything?

A red-black tree is a binary encoding of a 2-3-4 tree: a black edge separates
2-3-4 nodes, and red edges join the binary pieces of one 2-3-4 node together.
Balancing a red-black tree comes down to two operations, color flips and
rotations, which split 4-nodes and keep every leaf at the same black height.

To append a tag, littlefs walks the current trunk top-down, exactly as if it
were looking up the new tag's key, and writes out a new trunk as it goes. Alts
the path doesn't take are copied into the new trunk as-is, still pointing back
into the old log. Alts the path does take are recolored, flipped, or split as
needed. When two red alts end up in a row, we have a 4-node that is too big;
we call that edge yellow and split it on the way down. The new leaf goes at the
end of the trunk.

Since tags can't be changed once they're written, the append keeps the last
three alts of the new trunk in a small FIFO in RAM (`lfs3_rbyd_p_push` and
friends in `lfs3.c`). Recoloring and reordering only ever touch those three
alts, and an alt is only written to disk once it falls out of the FIFO, by
which time its color and target are settled. So an append needs a fixed amount
of RAM, regardless of how big the tree is.

Some edges become unreachable along the way. Unreachable red alts can simply
be dropped. Unreachable black alts can't, since removing a black edge would
change the black height of one side of the tree, so these get written with a
jump of zero, which marks them as never taken. In a debug build
(`LFS3_DBGRBYDBALANCE`) littlefs checks that every leaf has the same black
height, and that the tree's height is at most twice its black height plus
one (plus two after a range removal).

---

Weights are what make rbyds useful for more than one thing.

Every leaf has a weight, and every alt pointer records the total weight of the
subtree it jumps to. While walking down the tree, we can add up the weights we
skip over to find the rid of the leaf we land on. This means rids are never
stored anywhere. Inserting an entry with weight _w_ shifts the rid of every
entry after it by _w_, and nothing after it has to be rewritten.

littlefs uses this in three different ways:

1. In metadata logs, every file or directory entry has weight 1, so its rid
   is its position in the log's sorted order.

2. In file B-trees, every entry's weight is the number of bytes it covers, so
   its rid is a byte offset into the file. Inserting or removing bytes anywhere
   in a file shifts the offsets of everything after it for free. This is what
   makes sparse files and `lfs3_file_fruncate`, truncating from the front,
   cheap.

3. In the global block map, every entry's weight is the length of a run of
   blocks, so a whole range of free blocks costs one entry.

---

A commit can append any number of tags, followed by a checksum. Like v2, only
the commit as a whole has a checksum, which lets littlefs update any number
of unrelated tags in the same log atomically. The details of how commits are
checksummed, and how littlefs knows where the next commit can go, are in the
section on checksums below.

Before we can use an rbyd we need to find its last valid commit. With no other
information, we have to fetch it: read the block from the start, checking each
commit's checksum, and remember where the last good trunk was. Fetching is
_O(b)_ in the block size. But if we already know where a trunk is and what the
checksum of the log up to that trunk should be, we can skip the fetch and walk
the tree directly from that trunk. This is how B-tree pointers work in v3, and
it's why only metadata logs, not B-tree nodes, pay for fetching.

---

Eventually the block fills up and we need to compact.

Compaction in v2 checked every entry against every newer entry to find which
entries were still live, which is where its _O(n&sup2;)_ comes from. In an
rbyd we don't need to do that: the last trunk already knows which tags are
live. We read every live tag in order with _n_ lookups, write each tag into
the new block as a single-tag trunk, and then build a perfectly balanced tree
on top of them, one layer at a time, by connecting pairs of trunks with new
two-alt trunks until one trunk is left:

```
before compaction, block full of commits and garbage alts:

.-----.--------.--------.--------.-- ... --.--------.
| rev | commit | commit | commit |         | commit |
'-----'--------'--------'--------'-- ... --'--------'

after compaction, into another block:

.-----.---.---.---.---.------.------.------.-----------
| rev | a | b | c | d | a-b  | c-d  | ab-cd|  (erased)
'-----'---'---'---'---'------'------'------'-----------
       '-----.-----'  '----- layers of a perfectly -----'
          leaves          balanced tree, built upwards
```

Compaction costs _O(n log n)_ reads and _O(n)_ writes, down from
_O(n&sup2;)_. As the maintainer put it:

> This improvement may sound minor on paper, but it's a difference measured in
> seconds, sometimes even minutes, on devices with extremely large blocks.
> ([#1111])

The compacted tree isn't the most compact encoding possible. Each inner node
costs two alts and a null terminator, so each tag carries up to _3t+4_ bytes
of overhead, where _t_ is the worst-case size of an encoded tag. A tree that
used exactly one alt per tag (_2t_) is possible, but needs RAM proportional to
the tree size to build, so we don't. littlefs uses this bound to estimate how
big a log will be after compaction without actually compacting it. With 4 KiB
blocks and the default 31-bit file limit, _t_ works out to 5 bytes for
metadata entries (at most 19 bytes of overhead per tag) and 9 bytes for B-tree
entries (at most 31 bytes per tag).

---

To sum up, for a log of _n_ tags in a block of _b_ bytes:

| operation            | v2 metadata pair | v3 rbyd                   |
|----------------------|------------------|---------------------------|
| fetch                | _O(b)_           | _O(b)_, skipped with a known trunk |
| lookup (fetched)     | _O(n)_           | _O(log n)_                |
| append a tag         | _O(1)_           | _O(log n)_                |
| compact              | _O(n&sup2;)_     | _O(n log n)_              |

The price is a few bytes of alt pointers per append, and a lot more code.
The write side of rbyds, the balancing in particular, is the most complicated
code in littlefs. The read side is much simpler, which matters for read-only
builds.

## Metadata pairs, again

An rbyd is a log in one block, and a log in one block can't be compacted in
place: we'd have to erase the only copy of the data we're compacting. So, just
like v2, littlefs stores metadata in pairs of blocks. v3 calls them metadata
directories, or mdirs, though an mdir is no longer tied to a single
directory.

Most of v2's reasoning still applies, so we'll only go over it briefly:

1. Each block starts with a 32-bit revision count. The block with the more
   recent revision count, compared with
   [sequence arithmetic][wikipedia-sna], is the active one. If it has no valid
   commit, littlefs falls back to the other block.

2. New commits are appended to the active block.

3. When the active block fills up, littlefs erases the other block, writes an
   incremented revision count, and compacts the live tags into it. The
   compacted block only becomes the active block once its first commit's
   checksum lands, so a power loss during compaction leaves the old block in
   charge.

4. If the compacted mdir would be more than half full, littlefs splits it into
   two new mdirs instead. The same argument as in v2 applies: a log compacted
   to more than 50% full gets compacted again too soon, and splitting at 50%
   bounds the amortized cost of compaction to about 2x.

v3 adds one rule: an mdir whose last entry is removed is dropped from the
filesystem. mdirs are never merged, but empty ones don't linger.

---

The revision count got a bit more crowded in v3. The only thing littlefs
strictly requires of it is that the newer block has the newer count:

> note! the only strict requirement for revision counts is that the most
> recent block has the most recent revision count, all drivers must be ok with
> _simple_ 32-bit counters! (`lfs3.c`)

This driver packs a few optional things into those 32 bits:

```
vvvvrrrr rrrrrrnn nnnnnnnn pddddddd
'-.''----.----''----.----' ^'--.--'
  '------|----------|------|---|---- 4-bit relocation count
         '----------|------|---|---- recycle counter
                    '------|---|---- pseudorandom noise (optional)
                           '---|---- perturb bit (optional)
                               '---- debug bits: 'h' anchor, 'm' mdir,
                                                 'b' B-tree node
```

The recycle counter drives wear leveling. Every compaction erases one block of
the pair. After `block_recycles` erases per block (rounded down to a power of
two), littlefs relocates the mdir to a new pair of blocks instead of
compacting it in place. The counter counts modulo an odd number, so that the
two blocks of a pair take turns triggering the relocation; with an even
modulus, only one of them ever would.

When an mdir is relocated, littlefs only erases one of the two new blocks. The
other block keeps whatever was on it, and the new mdir's revision count is
chosen so that it is always newer than any stale revision count on that block,
by bumping the 4-bit relocation count. The stale block gets erased the first
time the new mdir is compacted, which would have happened anyway. The perturb
bit comes up again with pre-erased blocks. The noise bits, optional and off by
default, mix in bits of the gcksum, which makes accidental checksum
collisions (say, from a filesystem bug) less likely.

## The mtree

In v2, each directory is a linked-list of metadata pairs, and a second
linked-list is threaded through every metadata pair in the filesystem so it
can be traversed in constant RAM. We've already seen that this was the source
of a lot of bugs. v3 rips both lists out and replaces them with a single
B-tree of mdirs: the mtree.

The first step is to stop tying mdirs to directories. v3 has one global
namespace. Every file and directory is keyed by its parent's directory id
(did) followed by its name, and all entries in the filesystem are kept in that
order:

```
(did 0, "")          <- bookmark of the root directory
(did 0, "config")    file
(did 0, "logs")      directory, with did 0x2a
(did 0, "www")       directory, with did 0x71
(did 0x2a, "")       <- bookmark of /logs
(did 0x2a, "a.bin")  file
(did 0x2a, "b.bin")  file
(did 0x71, "")       <- bookmark of /www
(did 0x71, "app.js") file
```

Each directory owns a bookmark entry, an empty name that sorts before its
children. A directory is then just a contiguous range of this keyspace,
starting at its bookmark. Small directories can share an mdir, and big
directories can span many mdirs; there is no longer a minimum of one metadata
pair per directory.

Directory ids are picked by hashing: the parent's did xor the CRC-32C of the
name, truncated to a mask that grows with the number of mdirs, with linear
probing on a collision. The did doesn't need to be reproducible, so the hash
doesn't need to be good, but hashing keeps dids spread out, and truncating
keeps them short in their [leb128][leb128] encoding.

---

Every entry also has a metadata id, or mid, which says where it is:

```
mid = (index of its mdir << mbits) | (its rid in that mdir)
```

`mbits` is `log2(block_size) - 3`, room for `block_size/8` entries per mdir.
That's the most entries a perfectly packed mdir could ever hold, assuming 8
bytes per entry (a minimal 4-byte tag plus one 4-byte alt). littlefs's own
compaction never gets close to that, but the mid space is sized for a
theoretical perfect implementation so that a future driver can pack mdirs
tighter without changing the format. With 4 KiB blocks, each mdir gets 512
mids.

The mtree is a B-tree, built from rbyds the same way as file B-trees (next
section), whose leaves are pointers to mdirs, each with a weight of
`2^mbits`. The mdir holding a mid is found by a weighted lookup, and adding a
new mdir shifts the mids of every later mdir, for free, thanks to rbyd
weights. Every leaf but the first also carries a name, the first name in its
mdir at the time the mdir was split off, so the mtree can be searched by name
as well as by mid. (Names in the mtree can go stale as entries are removed,
which is fine: a stale name still separates the two mdirs correctly.)

```
             .----------------------------------------.
             | mtree root                             |
             | mdir 0 ptr | (0x2a,"b.bin") mdir 1 ptr |
             '----------------------------------------'
                  .-'                          '-.
                 v                                v
         .---------------------. .----------------------.
        .| mdir 0              |.| mdir 1               |
        || mids 0-511          ||| mids 512-1023        |
        ||                     |||                      |
        || /  bookmark         ||| /logs/b.bin          |
        || /config             ||| /www  bookmark       |
        || /logs               ||| /www/app.js          |
        || /www                |||                      |
        || /logs  bookmark     |||                      |
        || /logs/a.bin         |||                      |
        |'---------------------'|'----------------------'
        '---------------------' '----------------------'
```

Looking up a path is then, for each path component, a name lookup in the
mtree (_O(log_b m)_ B-tree nodes for _m_ mdirs), a fetch of one mdir
(_O(b)_), and a binary search over the names in that mdir (_O(log&sup2; n)_,
since each probe is an _O(log n)_ rbyd lookup). In v2 the same lookup was a
linear walk through every metadata pair of the directory.

A small filesystem doesn't need an mtree at all. When everything fits, files
live directly in the mroot, the root metadata log that also holds the
filesystem's configuration. The mtree is only created the first time the
mroot has to split.

---

Removing the threaded linked-list has a cost too: traversing the filesystem
is no longer a matter of following one list. A traversal has to track which
B-tree it's in and where it is in that B-tree. In a read-only build, this is
where most of v3's extra code goes. What we get in exchange is that every
multi-block change is a tree update: write the new blocks, then commit one
pointer to them. Orphaned metadata pairs and half-orphans are gone. (v3 still
has one kind of leftover to clean up after a power loss, files created but
never synced, which we'll get to.)

## The mroot chain

The mroot has to be somewhere littlefs can find without any other
information. Like v2's superblock, it starts at blocks 0 and 1, the anchor.

But blocks 0 and 1 can't move, and the mroot is the most frequently written
metadata log in the filesystem: every mtree update commits to it. So, as with
v2's superblock expansion, when the anchor wears out its turn (its recycle
counter says it's time to relocate), littlefs doesn't move it. Instead it
compacts the anchor in place down to just the magic string and a pointer to a
new mroot somewhere else, which takes over the configuration, the global state
and the mtree root. From then on the anchor only changes when that mroot is
relocated, which is `block_recycles` times rarer. If that mroot wears out too,
the chain grows another link.

```
 anchor 0x{0,1}       mroot 0x{7,8}        mroot 0x{2c,2d}
.--------------.     .--------------.     .----------------.
| "littlefs"   |     | "littlefs"   |     | "littlefs"     |
| mroot -------|---->| mroot -------|---->| config         |
'--------------'     '--------------'     | global state   |
                                          | mtree root ----|---> ...
                                          '----------------'
```

This is CObW in its purest form: each link in the chain divides the wear on
the link before it by `block_recycles`. Every mroot in the chain carries the
magic string, only the last one carries the configuration, and mount follows
the chain with [Brent's algorithm][brent] to detect cycles.

---

Some operations change more than one block: a split writes two new mdirs and
an mtree update, a relocation writes a new mdir and updates its pointer. The
rule that makes these power-loss safe is the same everywhere in v3:

1. Write the new blocks. Nothing points to them yet, so a power loss leaves
   them as garbage that the allocator will reclaim.
2. `sync` the block device, so the new blocks are durable before anything
   refers to them.
3. Commit the pointer to them, in a single commit, to the metadata log that
   owns it: the mroot for the mtree's root, the previous link of the mroot
   chain for a relocated mroot, a file's mdir for its tree.
4. `sync` again.

A reference is never durable without the blocks it points to, even on block
devices that can reorder writes between syncs.

## Global state

Commits are atomic within one mdir, but some operations need to change more
than one mdir. v2 solved this with global state, and v3 keeps the mechanism
unchanged, so we'll only summarize it.

Global state (gstate) is a small amount of state that can be updated from any
mdir. It is stored as deltas, one per mdir, and the actual value is the xor of
every delta in the filesystem:

```
             .--------.
            .| mroot  |
            || gdelta |  = 0x12
            || =0x12  |
            |'--------'
            '----|---'
             .-'    '-.
            v          v
      .--------.   .--------.
     .| mdir   |  .| mdir   |
     || gdelta |  || gdelta |      gstate = 0x12 ^ 0x40 ^ 0x09
     || =0x40  |  || =0x09  |             = 0x5b
     |'--------'  |'--------'
     '--------'   '--------'
```

To change the gstate, a commit to any mdir xors the change into that mdir's
delta. littlefs keeps the current gstate in RAM, so the deltas only need to
be read at mount. When an mdir is split, dropped or relocated, its delta
isn't lost: it's folded into the delta of whichever mdir the atomic commit
lands in (the parent mroot, for example). Only one mdir's delta changes per
atomic commit.

Deltas can't easily be cleaned up, so gstate must stay small. v3 has three
pieces of it:

1. **grm**, the global remove queue, covered next.
2. **gbmap**, the root of the optional on-disk block map, covered in the
   section on block allocation.
3. **gcksum**, the global checksum, covered in its own section.

---

v2 used gstate to record at most one in-progress move. v3 generalizes this
into grm: a queue of up to two mids that are pending removal. An entry whose
mid is in the grm is treated as if it doesn't exist. littlefs removes it
right away, in a second commit, or, if power is lost first, the next time the
filesystem is written to.

This is enough to make every multi-mdir namespace operation atomic:

1. **mkdir** takes two commits, because a new directory's bookmark and its
   name live at different places in the keyspace, and so possibly in
   different mdirs. The first commit creates the bookmark and pushes the
   bookmark's own mid onto the grm, so a power loss removes it again. The
   second commit writes the name and did into the parent, and pops the grm in
   the same commit.

2. **rename** writes the new name, moves all of the source's tags to the
   destination, and pushes the source's mid onto the grm, all in one commit to
   the destination mdir. After that commit, the file exists at its new name
   only, even if power is lost before the source is actually removed.
   Renaming a directory over an empty directory also needs to remove the
   replaced directory's bookmark, which is where the second slot gets used.

3. **rmdir** removes the directory's name and pushes its bookmark's mid onto
   the grm in one commit, then removes the bookmark.

The same queue is used to clean up files that were created but never synced.
More on those in the section on the sync model.

## Files

v2 stores files in CTZ skip-lists: backwards linked-lists of data blocks,
where block _n_ holds ctz(_n_)+1 pointers to earlier blocks. They are, in the
maintainer's words, "humorously overoptimized" for one thing, sequential
appends, which cost _O(1)_. Everything else about them has a problem:

1. The pointers live in the data blocks. Changing any block means changing
   every block after it, so a random write rewrites the rest of the file.
2. Appending always happens at the tail block, and after a sync v2 can't
   tell whether the rest of the tail block is still erased, so the next write
   copies it.
3. Files small enough (smaller than the file cache and, by default, an eighth
   of a block) don't use a skip-list at all. They are stored inline in the
   metadata log instead, as a single blob that has to fit in RAM and is
   rewritten whenever it changes.

v3 replaces all three representations with one: a B-tree whose leaves are
either pieces of data stored in the tree itself, or pointers to slices of
data blocks.

---

v2's DESIGN.md considered and rejected B-trees, because they can't be
traversed with constant RAM, and because updating B-tree nodes copy-on-write
means copying whole blocks. Two things change that in v3.

First, the nodes are rbyds. A B-tree node is a log, so updating a node is an
append of _O(log n)_ bytes, not a copy of the node. The parent needs to point
at the new version of the child, which is another append, and so on up to the
root:

```
before:                              after writing to leaf B:

      .------.                            .------.------.
      | root |                            | root | root'|
      '------'                            '------'------'
      .-'  '-.                             .-'        '-.
     v        v                           v              v
 .------.  .------.                   .------.  .------.------.
 |  A   |  |  B   |                   |  A   |  |  B   |  B'  |
 '------'  '------'                   '------'  '------'------'
                                         old versions stay valid;
                                         new versions are appends
```

Old versions of a node stay valid, because a pointer to a node records the
block, the offset of the trunk it points to, and the checksum of the log up
to that trunk. Appending to the log can't change any of these. The on-disk
tree, an open file's unsynced copy of the tree, and another handle's snapshot
can all share nodes. Before one of them appends to a node, littlefs marks
every other copy of that node in RAM as stale, and a stale copy re-checks the
node's erased state on disk (its ecksum, described later) before appending.
So two snapshots can never both append to the same tail: the second one finds
the tail already programmed, and copies the node instead.

A node can't be compacted in place, since its older versions may still be in
use. When a node fills up, littlefs compacts it into a newly allocated block,
splitting it in two if the result would be more than half full, and merging it
with a sibling if it is at most a quarter full and both together would fit in
half a block. So B-tree nodes are copy-on-bounded-writes, where the bound is
however many appends fit in a block. The maintainer:

> For extra cool points, littlefs's B-trees use rbyds for the inner nodes,
> which makes CoW updates much cheaper than traditional array-packed B-tree
> nodes when large blocks are involved ( _O(n)_ → _O(log n)_ ). ([#1111])

Second, the B-trees are counted. Every entry in a file's B-tree has a weight
equal to the number of bytes it covers, and every branch has the total weight
of its subtree, so a position in a file is just a byte offset. littlefs calls
the offset of the last byte of an entry its bid. To traverse a B-tree, we only
need to remember the bid we're at, plus a copy of the current leaf to avoid
looking it up again for every entry. When we reach the end of a leaf, we look
up the next bid from the root. That's _O(log_b n)_ per leaf and constant RAM.

---

A B-tree root needs a block of its own, and most files on a microcontroller
are nowhere near a block in size. Giving every small file its own block would
bring back the 3-blocks-for-4-bytes problem v2 solved with inline files.

v3 solves it by storing the root of a small B-tree inside the metadata log
itself. An rbyd block can hold more than one tree: the mdir's own tree, and
any number of secondary trees whose tags are marked as belonging to a shrub.
A file whose tree lives in its mdir this way is a B-shrub: its mdir entry
records the offset of the shrub's trunk instead of a block pointer. Commits to
the shrub are appended to the mdir log like any other mdir commit, and when
the mdir is compacted, the shrubs it contains are compacted along with it.

```
mdir 0x{4,5}
.------------------------------------------------.
| rev | ... | "notes.txt" REG | bshrub -----.     |
|     | ... shrub: <=a  a  b  <=b ... c  <--'     |
|     | ...                                       |
'------------------------------------------------'
      the file's tree is a secondary tree in the
      same log as the mdir's own entries
```

Shrubs are bounded by `shrub_size`, at most a quarter of a block. When a
shrub's estimated size passes half of `shrub_size`, or a commit would push it
past `shrub_size`, littlefs evicts the root into a block of its own. The
estimate counts every version of the shrub that open handles are holding, so
that an mdir can always be compacted. A B-shrub doesn't have to be small,
only its root does: a large file's tree can have its root inlined in the mdir
and its other nodes in blocks.

This replaces v2's inline files, without their limits. The shrub lives on
disk, not in the file cache, and appending to a small file adds to its tree
instead of rewriting the whole file:

> In v3, B-trees can have their root inlined in the file's mdir, giving us
> what I've been calling a "B-shrub". This, combined with the above inlined
> leaves, gives us a much more efficient inlined file representation, with
> better code reuse to boot. ([#1111])

---

The leaves of a file's tree come in three kinds:

1. **Fragments** hold up to `fragment_size` bytes of data directly in the
   tree node, like v2's inline data.

2. **Block pointers** point to a slice of a data block: the block, an offset
   and a size, plus a checksum. The checksum covers the block from its first
   byte up to a length, cksize, which may be longer than the slice. This lets
   several pointers share one block, and lets the checksum be extended as
   data is appended, without reading the block back.

3. **Holes** are fragments that store fewer bytes than their weight. The
   missing bytes read as zeros and take no space, so sparse files come for
   free.

Data blocks hold only data. There are no pointers or headers in them, which
also means any contiguous run of data on disk can be described by a single
block pointer:

```
file "log.bin", 8300 bytes

  bids:  0.........4095  4096........8191  8192....8291  8292...8299
        .--------------..---------------..------------..-----------.
        |    block     ||     block     ||  fragment  ||   hole    |
        '------|-------''------|--------''------------''-----------'
               |               |          100 bytes in   8 zeros,
               v               v          the tree       no storage
        .------------.  .------------.
        | data block |  | data block |
        | 0x1a       |  | 0x3c       |
        '------------'  '------------'
```

---

So when does data go in a fragment, and when does it go in a block?

This is the hard part. The maintainer again:

> B-trees, on their own, don't add that much code. They are a relatively
> poetic data-structure. But _deciding_ how to write to a B-tree, efficiently,
> with an unknown write pattern, is surprisingly tricky. ([#1111])

Fragments are cheap to write, since a small write only adds a small commit
to the tree, but they are expensive to store: they live in metadata logs,
which are compacted at half full and come in pairs. Blocks are cheap to store
but expensive to write, since a block holding a few bytes still costs a whole
block, and changing a few bytes in the middle of a block would mean rewriting
it.

v3's answer, which the maintainer calls lazy crystallization, is to write
fragments first and pack them into blocks only when enough of them have
accumulated:

1. A write that can't go anywhere better becomes one or more fragments of at
   most `fragment_size` bytes, merged with neighboring fragments where
   possible.

2. When a write, together with the fragments on either side of it, spans at
   least `crystal_thresh` bytes, littlefs crystallizes: it allocates a block
   and copies the write and its neighboring fragments into it, then replaces
   them in the tree with a single block pointer.

3. If a file's last block still has erased space, a write that continues where
   the block left off resumes crystallizing into the same block, without an
   erase. This only needs `min(prog_size, crystal_thresh)` bytes, usually
   much less than it took to start the block.

Setting `crystal_thresh` above the block size turns crystallization off, and
every write becomes fragments. In the maintainer's benchmarks this
"fragmented" mode improves random writes a lot, at the cost of ~4x storage
overhead and poor sequential writes ([#1114]).

---

Crystallization is also where v3 fixes the sync-padding problem that made v2
so expensive for logs ([#862]).

A block is always crystallized up to a prog boundary (unless
`crystal_thresh` is set below `prog_size`, a corner case for devices with very
large prog sizes). When a sync, or anything else, forces littlefs to write out
data that doesn't end on a prog boundary, the unaligned tail isn't programmed
into the block with padding.
It's left in the tree as a fragment. The block's pointer covers only the
aligned part, and since the rest of the block is still erased, littlefs
remembers that it can keep appending to it:

```
after a sync at an unaligned offset:

 data block 0x3c
.------------------------------------------------------------------.
| data | data | data |  (erased)                                    |
'------------------------------------------------------------------'
 '---- aligned to prog_size ----'
                          + fragment in the tree: the unaligned tail

after the next write, the block resumes where it left off:

.------------------------------------------------------------------.
| data | data | data | tail+new data |  (erased)                    |
'------------------------------------------------------------------'
```

The old, synced version of the file still points at the shorter slice, whose
checksum only covers the part it knows about, so appending to the block
doesn't disturb it.

Only a sync, a flush or a close has to write the tail out. When an append
fills the file cache in between, littlefs writes the cache up to the block's
last prog boundary and keeps the tail cached: more appends before the sync
would only replace a fragment written now, and each fragment costs a commit
to the file's tree padded to `prog_size`. Likewise, when an append merges
with the file's last fragment, removing that fragment and adding the longer
one is a single commit, so an error part way can't leave the handle's tree
half updated.

There is one catch. Whether a data block still has erased space is only known
in RAM, while the file is open; it isn't stored on disk. After a file is
closed and reopened, or the filesystem remounted, the next append starts as
fragments, and the next crystallization either copies the partial block into a
new one or starts a new block. A log that stays open pays for its erase once
per block. A log that is reopened for every record pays more.

---

Two more consequences of counted trees are worth mentioning:

1. `lfs3_file_truncate` and the new `lfs3_file_fruncate`, which truncates from
   the front of a file, are both just weight changes in the tree. A bounded
   log or FIFO is:

   ```c
   lfs3_file_write(&lfs, &file, entry, entry_size) => entry_size;
   lfs3_file_fruncate(&lfs, &file, log_size) => 0;
   ```

   This isn't free, though. In the maintainer's benchmarks, write+fruncate
   costs two commits where rotating between two files with `lfs3_rename`
   costs one, and rotating logs came out faster ([#1114]).

2. Seeking past the end of a file and writing creates a hole, not a run of
   zeros on disk.

---

Here's how the file operations compare, for a file of _n_ bytes in blocks of
_b_ bytes:

| operation                  | v2 (CTZ skip-list)         | v3 (B-tree)                           |
|----------------------------|----------------------------|---------------------------------------|
| read at an offset          | _O(log(n/b))_ block reads  | _O(log_b n)_ node lookups             |
| append, no sync            | _O(1)_                     | _O(1)_ data, plus tree commits per block |
| overwrite in the middle    | copy everything after it   | the written range, plus _O(log_b n)_ node appends |
| sync after a small append  | copy the tail block        | two small commits, no erase until a log fills |
| small file                 | inline, must fit in RAM    | B-shrub in the mdir                   |
| truncate from the front    | not possible               | weight change                         |

Pure sequential writes are the one place v2 still wins. The CTZ skip-list
really is very good at them, and nothing in v3 is quite as cheap as writing a
block of data and a couple of pointers.

## The sync model

POSIX leaves a lot unsaid about what happens when a file is open more than
once, and almost everything unsaid about what's on disk after a power loss.
littlefs has never had a strictly POSIX API, which puts us in a position to
pin these things down:

> One interesting thing about littlefs, it doesn't have a strictly POSIX API.
> This puts us in a relatively unique position, where we can explore tweaks to
> the POSIX API that may make it easer to write powerloss-safe applications.
> ([#1111])

v3's sync model comes down to five rules ([#960], [#1111]):

1. Open file handles are snapshots of the on-disk state. Writes to a file are
   copy-on-write, with no effect on the on-disk state or on any other handle.

2. Syncing or closing an in-sync file atomically updates the on-disk state
   and every other in-sync handle to the same file.

3. A file can be desynced, either explicitly with `lfs3_file_desync`, or
   because an operation on it failed. Desynced files don't receive updates
   from other handles' syncs, and closing a desynced file does nothing to the
   on-disk state.

4. Calling `lfs3_file_sync` on a desynced file atomically updates the on-disk
   state and every in-sync handle, and marks the file as in-sync again.

5. Calling `lfs3_file_resync` throws away a file's unsynced changes and marks
   it in-sync. It's equivalent to closing and reopening the file.

---

The data structures make these rules easy to keep. A file's contents are
reachable from exactly one place on disk: the struct tag in its mdir entry,
which holds either the shrub's trunk or the B-tree's root pointer, or nothing
at all for an empty file. Everything a write or a flush puts on disk (data
blocks, B-tree node appends, shrub commits in the mdir log) is unreachable
until that tag changes.

A sync is then:

1. Flush the file cache and finish any crystallization in progress.
2. `sync` the block device, so the data is durable.
3. Commit, in one mdir commit, the new struct tag, any changed custom
   attributes and, for a new file, its name.
4. `sync` the block device again.

If power is lost before step 3 lands, the file is exactly what it was at its
last sync. After it, the file is exactly what was synced. Every sync of every
file is atomic, and the commit in step 3 is small: some tags, a few alt
pointers and a checksum. The maintainer, on what sync costs in v3:

> Sync still has a cost, it needs to write-out any cached data and append a
> metadata commit, but it should be ~2 progs (pages) instead of ~1 block.
> ([#1114])

Here's what survives a power loss:

| state when power is lost                  | after remount                         |
|-------------------------------------------|---------------------------------------|
| `lfs3_file_sync` returned, or `lfs3_file_close` of an in-sync file | contents, size and attributes as synced |
| written or flushed, not synced            | the previous synced version           |
| opened with `LFS3_O_TRUNC`, not synced    | the old contents                      |
| created, never synced                     | nothing: the file doesn't exist       |
| written with `LFS3_O_SYNC`                | durable once the write returned        |
| `lfs3_set` returned                       | contents as set                       |

`LFS3_O_FLUSH` writes data out on every write, but doesn't make it durable;
only a sync does that. Note also that a read on a writable handle may need to
flush cached data first, so reads can write and can fail with write errors.

---

Snapshots also define what other handles see. Each handle owns its own copy
of the tree root, the leaf it's working on and its cache. When one handle
syncs, littlefs copies the new root, leaf and cache contents into every other
in-sync handle to the same file, which discards anything they had written but
not synced: the last sync wins. Desynced handles are left alone, though the
allocator keeps their private blocks alive while they're open.

The same machinery gives littlefs a well-defined answer to errors. Since v3
was a rewrite anyway, it reverts in-RAM filesystem state to the last
known-good state when an operation fails. File data is the one exception:

> Reverting file data correctly turned out to roughly double the cost of
> files. And now that you can manual revert with `lfs3_file_resync`, I figured
> this cost just isn't worth it. So file _data_ remains undefined after an
> error. ([#1111])

Instead, a file that hits an error is desynced, and its owner decides whether
to retry the sync or resync to get back to the on-disk version. One
consequence is worth knowing about: because closing a desynced file is a
no-op, `lfs3_file_close` returns success even if an earlier write failed. The
error is reported by the write.

## Stickynotes

There's one more awkward state: a file that has been created, but not yet
synced. In v2, opening a file with `LFS_O_CREAT` immediately wrote an empty
file, so a power loss left a zero-sized file behind ([#908]).

v3 adds a new file type for this state, the stickynote. Creating a file
commits a stickynote with the file's name, which reserves its place in the
namespace so other handles and `LFS3_O_EXCL` see a consistent picture. The
first sync turns the stickynote into a regular file in the same commit that
writes its contents. Until then:

1. A stickynote is visible only while a handle that is in-sync with it is
   open. Otherwise lookups treat it as if it doesn't exist.
2. If its last handle is closed without syncing, littlefs pushes it onto the
   grm, or, if the grm is full, marks the filesystem as needing cleanup.
3. If power is lost, the stickynote is left behind invisible, and the next
   time the filesystem is written to (or when `lfs3_fs_mkconsistent` or gc
   runs) littlefs scans the mtree and removes it.

Removing or renaming over a file that is still open reuses the same idea in
reverse. The entry is turned back into a stickynote and the open handles
become zombies: their syncs quietly do nothing, and the stickynote goes away
with the last handle.

Small files can skip the stickynote. `lfs3_set` writes a new file's name and
contents in a single commit when the contents fit in a fragment, and a sync of
a small file that fits entirely in its cache writes the whole file as one
fragment in the same commit as its struct tag.

## Checksums

v2 checksummed metadata commits and nothing else. v3 checksums everything,
and tries to do it without spending much code or RAM.

v3 also switches from v2's CRC-32 to [CRC-32C][wikipedia-crc] (polynomial
0x1edc6f41, initial value and final xor 0xffffffff), and leans on two of its
algebraic properties:

1. CRC-32C's polynomial has an even number of terms, so it's divisible by
   x+1. Since its initial value and final xor also have even parity, the
   parity of a CRC-32C is the parity of the data it covers.

2. CRC-32Cs are elements of a ring, so they can be multiplied, and there is an
   odd-parity "zero" (0xfca42daf) that flips a checksum's parity without
   changing anything else about it.

We'll see where both of these come in.

---

Like v2, a commit is checksummed as a whole. littlefs keeps a running
checksum of the log, starting from the revision count and covering every tag
and its data, and the commit ends with a checksum tag holding that checksum.
A commit counts if and only if its checksum is correct. Fetching a log stops
at the first commit that doesn't check out, and the commit before it is the
last valid one.

The top bit of every tag is its valid bit, which is not itself covered by the
checksum. littlefs sets it to the parity of the running checksum at that
point, which by property 1 is the parity of every bit before it (not counting
the valid bits themselves). This means
that the valid bit of each tag is a parity bit for the tag before it. Fetching
a log uses the valid bits to tell real tags from erased or half-written flash
cheaply, and with `LFS3_M_CKMETAPARITY`, littlefs also checks every tag it
reads against the next tag's valid bit, a 1-bit check that costs one extra
byte per read instead of a recomputed checksum.

The checksum tag also carries the low two bits of the block's address, called
its phase. A commit read from the wrong block, as happens with a
misconfigured geometry or an image shifted by a block or two, is rejected
even if its bytes are otherwise intact.

---

The interesting problem is knowing where the next commit can go.

The obvious answer is "after the last valid commit", and the obvious way to
check that the space there is still erased is to check that it reads as
0xff. Neither works:

1. littlefs doesn't assume what erased flash looks like. Erased flash can read
   as 0x00, and an encrypted block device can make it look like random data.

2. A program interrupted by power loss can leave bits partially programmed.
   These can read as erased, and some can read differently every time they
   are read, a problem known as metastability ([#671]). Programming over them
   again may not produce what we programmed.

littlefs has a conservative model of flash: it never programs anything unless
it is sure no program has been attempted there since the last erase. v2.1
added a forward checksum for this, and v3 keeps the idea under the name
ecksum. Every commit that doesn't end the block is followed by an ecksum tag,
which holds the checksum of the next program's worth of bytes as they were
when the commit was written, which is to say erased. When littlefs fetches the
log, it checksums those bytes again. If they still match, nothing has been
programmed there, and it's safe to append. If they don't, littlefs assumes the
worst, and the next write to the log compacts it instead of appending.

A program's worth is not `prog_size` bytes. The next commit's first program
is a flush of the program cache, up to `pcache_size` bytes, and a program cut
short by power loss can leave its first prog unit erased while changing bytes
after it. So the ecksum covers `pcache_size` bytes, at least 11 (the largest
tag littlefs programs on its own), rounded up to `prog_size` and stopping at
the end of the block. The one assumption left about the hardware is that an
interrupted program changes nothing outside the bytes it was given.

This costs reads: every fetch and every commit reads up to `pcache_size`
bytes of erased flash instead of `prog_size`. On the flight log of the
worked example, with `prog_size` 1 and a 1 KiB program cache, that's about
100 KiB more a minute and 3 KiB more at mount, a few milliseconds a minute
on a fast SPI bus. An ecksum narrower than the mount's, from an image
written with a smaller program cache, isn't trusted, so the first commit to
each such log compacts it.

That leaves one more case. Erased flash can never pass as a valid commit,
since a commit needs a checksum to match, but its first byte could pass the
first test a fetch applies to a tag, the valid bit. littlefs wants the valid
bit alone to say where a log ends. So when a commit is finished, littlefs
reads the first erased byte after it. If its top bit is what the next tag's
valid bit would be, littlefs sets a perturb bit in the checksum tag, which
tells the next commit to invert all its valid bits. Inverting the valid bits
is done by xoring the odd-parity zero from property 2 into the running
checksum, so the next commit's checksums stay consistent. Erased flash,
whatever its erase value, never looks like the start of a valid commit.

```
end of a commit in the middle of a block:

... last tag |ecksum   |cksum               |padding   |erased ...
             |next     |crc of the log,     |to the    |
             |program's|perturb bit,        |next prog |
             |bytes    |phase bits          |boundary  |
'-------- checksummed --------------------'  never      '-- covered by
                                             programmed     the ecksum
```

Padding up to the next prog boundary is never programmed. So for any write
that doesn't end on a prog boundary, littlefs pays for the rest of the prog
unit in space, but not in programming time. This padding is why the program
size matters so much to v3's metadata costs, which we'll come back to in the
worked example.

---

littlefs also needs a checksum it can refer to from elsewhere. The checksum
that ends a commit includes things that have nothing to do with the tree, like
the ecksum and the perturb bit, so v3 defines the canonical checksum of a log
as the checksum up to the end of its last trunk. It covers the tree and only
the tree.

With canonical checksums, every pointer in v3 carries a checksum of what it
points to:

1. A file's B-tree root pointer, in its mdir entry, records the root node's
   block, trunk, and canonical checksum.
2. Every branch in a B-tree node records the same for its child.
3. Every block pointer records the checksum of its data block, up to cksize.

So every byte littlefs writes is covered by a checksum, and every one of
those checksums is covered by a checksum in its parent, up to the mdirs. This
is the same construction as a [Merkle tree][merkle-tree]. The mdirs are the
top of the tree, and the next section ties them together.

---

As the maintainer points out, computing checksums while writing is almost
free, since the data is passing through anyway. The question is when to check
them:

> Funny thing about checksums. It's incredibly cheap to calculate checksums
> when writing, as we're already processing that data anyways. The hard part
> is, when do you check the checksums? ([#1111])

By default, littlefs checks the checksums of metadata logs every time it
fetches one, because it has to. Following a B-tree pointer skips the fetch,
and reading data doesn't read the whole block, so B-tree nodes and data blocks
are not checked on every read. Instead, littlefs lets the application choose
when to pay:

| when                                 | how                                           |
|--------------------------------------|-----------------------------------------------|
| at mount                             | `LFS3_M_CKMETA`, `LFS3_M_CKDATA`              |
| when opening a file                  | `LFS3_O_CKMETA`, `LFS3_O_CKDATA`              |
| on demand, whole filesystem          | `lfs3_fs_ck`                                  |
| on demand, one file                  | `lfs3_file_ck`                                |
| incrementally, in a traversal        | `LFS3_T_CKMETA`, `LFS3_T_CKDATA`              |
| incrementally, during gc             | `LFS3_GC_CKMETA`, `LFS3_GC_CKDATA`            |
| after every prog, by reading it back | `LFS3_M_CKPROGS` (`LFS3_CKPROGS`)             |
| the first time a node or block is used | `LFS3_M_CKFETCHES` (`LFS3_CKFETCHES`)       |
| every tag read                       | `LFS3_M_CKMETAPARITY` (`LFS3_CKMETAPARITY`)   |
| every data read                      | `LFS3_M_CKDATACKSUMS` (`LFS3_CKDATACKSUMS`)   |

CKMETA checks metadata (mdirs and B-tree nodes), CKDATA checks metadata and
data. The last four are compile-time options, so builds that don't use them
don't pay for their code. Their runtime costs differ a lot: CKPROGS doubles
the bus traffic of every write, and CKDATACKSUMS re-reads the whole
checksummed prefix of a block on every read, up to a block per read.

All of this is detection, not correction. A failed check returns
`LFS3_ERR_CORRUPT`. Error correction within a block fits better at the block
device level (see [ramcrc32bd] and [ramrsbd] for examples), and redundancy
across blocks is planned.

## Global checksums

Checksumming every log has a blind spot.

Suppose the most recent commit to an mdir is lost: its last prog unit rots,
or a metastable bit left by a power loss reads differently today, or a bad
block starts returning stale data. The next fetch finds that the last commit
doesn't check out and falls back to the commit before it, which is perfectly
valid. Every log in the filesystem still checks out, but the filesystem as a
whole is now in a state that never existed: one mdir is from the past, and the
rest are from the present. In v2, this is silent.

To catch this we need a checksum over all of the logs. The naive version, a
checksum of the whole filesystem stored in one place, would have to be
rewritten on every commit to every log, which is exactly the kind of hot spot
littlefs is designed to avoid. So v3 stores it as global state, as a delta in
every mdir.

The global checksum, gcksum, is the xor of the canonical checksums of every
mdir in the filesystem (the mroot chain and every mdir in the mtree). Every
commit that changes an mdir's checksum also updates that mdir's delta, so that
at all times:

```
xor of every mdir's gcksumdelta = gcksum^3
```

where the cube is computed in the CRC-32C ring. Mount computes both sides and
fails with `LFS3_ERR_CORRUPT` if they don't match.

Why the cube? Consider what a commit to mdir _i_ does to its delta, when its
checksum changes by _c_ and the rest of the filesystem's gcksum is _g_
(addition in the ring is xor):

```
with a linear function t:

  d_i = t(g + c) - t(g) = t(g) + t(c) - t(g) = t(c)

with the cube:

  d_i = (g + c)^3 - g^3 = g^2c + gc^2 + c^3
```

With any linear function, the state of the rest of the filesystem cancels out,
and each mdir's delta only vouches for its own checksum. An mdir rolled back
to an older commit brings its older delta with it, and the two still agree.
With the cube, each delta depends on the state of the whole filesystem at the
time of the commit. Rolling back one mdir leaves its delta disagreeing with
every commit made since. (The cube also maps the checksum one-to-one, and the
code comments note that computing a delta loses at most 3 bits of
information.)

---

So what does the gcksum catch?

1. At mount, the loss or rollback of commits to one mdir, if any other mdir
   has been committed to since.
2. After mount, a traversal with CKMETA recomputes the xor of every mdir's
   checksum and compares it to the gcksum in RAM, which catches any change to
   any mdir, including the most recent commit.

And what can't it catch?

1. Losing the most recent commits to the filesystem, if they all went to the
   same mdir. What's left is a state the filesystem really was in, and that's
   indistinguishable from a power loss just before those commits, which
   littlefs is designed to survive by rolling back.
2. Replacing the whole filesystem with an older, consistent copy of itself.
3. Changes after mount, until something checks.
4. Collisions. It's a 32-bit checksum.

The first two can't be caught from inside the filesystem at all. For these,
`lfs3_fs_cksum` returns the current gcksum, which an application can store
somewhere else (internal flash, a secure element, a server) after a sync and
compare after the next mount:

> With gcksums, and a traditional Merkle-tree-esque B-tree construction, v3 now
> provides a filesystem-wide self-validating checksum via `lfs3_fs_cksum`. This
> checksum can be stored external to the filesystem to provide protection
> against last-commit rollback issues, metastability, or just for that extra
> peace of mind. ([#1111])

Since the B-tree roots and their checksums live in the mdirs, the gcksum
covers file data and B-tree nodes too, through the chain of checksums
described above. It is order-sensitive, so two filesystems with the same
contents can have different gcksums.

Currently a gcksum mismatch at mount is fatal: the mount fails, even
read-only, and there is no degraded mode. The gcksum costs 4 bytes in each
commit that carries a delta, and a few ring multiplications per commit.

## The block allocator

v3's default allocator is v2's. littlefs doesn't keep a free list. The
filesystem itself is the record of which blocks are in use, and any block
the filesystem doesn't reference is free. Freeing a block is a no-op; it
simply stops being referenced. To find free blocks, littlefs traverses the
filesystem and marks the blocks it finds in a small bitmap, the lookahead
buffer, covering a window of `8*lookahead_size` blocks. Allocation takes
blocks from the window, and when the window is used up, littlefs moves it
forward and traverses again.

The rule that keeps this safe is also v2's: allocator checkpoints. An
operation checkpoints the allocator when every block that is in use is
reachable from something the traversal will visit. In v3 that means more than
the committed filesystem: every open file's unsynced tree and cached leaf, and
any data in the middle of being written, have to be visited too, since
snapshots keep blocks alive that nothing on disk points to. Between two
checkpoints, a block is allocated at most once, and a block allocated since
the last checkpoint is never handed out again. If the allocator goes all the
way around the disk without a checkpoint, it gives up with `LFS3_ERR_NOSPC`.

Without a gbmap, the window starts at a pseudorandom offset at mount, the
gcksum modulo the block count, for the same reason v2 used the xor of its
checksums: so that a device that loses power often doesn't always allocate
near the start of the disk.

The lookahead allocator's weakness is the traversal. Every time the window
runs out, and on the first allocation after every mount, littlefs reads the
whole filesystem. On NOR flash with 4 KiB blocks this is usually tolerable. On
an SD card with 512-byte blocks and tens of megabytes of data, it can take
seconds ([#1079]).

---

v3 adds an optional alternative: the global block map, or gbmap. It's
enabled at format with `LFS3_F_GBMAP`, or later with `lfs3_fs_mkgbmap`, and
compiled in with `LFS3_GBMAP`.

The gbmap is a B-tree, built from rbyds like everything else, whose weight is
the number of blocks on the disk. Each entry describes a run of blocks in one
state, and its weight is the length of the run, so a run of a thousand free
blocks costs one entry:

| state  | meaning                                                         |
|--------|-----------------------------------------------------------------|
| free   | not in use; erase before using                                  |
| in use | referenced by the filesystem, or not known to be free           |
| erased | not in use and already erased; carries an ecksum of the erased state |
| bad    | must never be erased or programmed, see [Bad blocks](#bad-blocks) |

The gbmap's root, and the window into it described below, are stored in
gstate, so they're updated atomically with whatever commit allocates blocks.

The maintainer describes three purposes:

> v3 includes support for the global block-map (gbmap), an optional auxiliary
> tree stored in gstate that can track additional metadata about free blocks.
> This enables several features: (1) faster block allocation on large disks,
> (2) pre-erased block tracking, and (3) bad-block tracking. ([#1111])

The gbmap only has to be right about free blocks. When a block is freed,
nothing updates the gbmap; the block stays "in use" until the next time the
gbmap is rebuilt. This keeps freeing a no-op, as in v2, and avoids the classic
problem with on-disk free maps: one missed update loses a block forever, and
one wrong update corrupts data.

---

The trick that makes the gbmap work is the known window. The gbmap stores a
window, the next block to allocate, and a count of how many blocks from there
on its entries can be trusted:

```
blocks:   .---.---.---.---.---.---.---.---.---.---.---.---.---.---.
gbmap:    |    in use     |  free |erased |in use |   free    | ...
          '---'---'---'---'---'---'---'---'---'---'---'---'---'---'
                  ^ window
                  |<--------------- known ------------->|
                  allocate from here, moving right
```

Allocation takes the next free or erased block inside the window, moves the
window past it, and shrinks the known count. The new window is part of the
gbmap's gstate, so it's committed atomically with the mdir commit that makes
the new block referenced. Entries outside the window are not trusted. So if
power is lost after a block is allocated, but before the commit that uses it,
the block is still inside the persisted window, marked free (and so erased
before it's used) or erased (and so checked against its ecksum before it's
used).

When the known count falls below `lookgbmap_thresh`, littlefs rebuilds the
gbmap. It copies the current gbmap, marks every in-use and erased block
outside the known window as free (keeping bad blocks bad), traverses the
filesystem marking every block it finds as in use, and adopts the result with
the whole disk as the new known window. The rebuilt gbmap isn't committed
right away. It rides along with the next mdir commit, and if power is lost
first, littlefs just rebuilds it again.

Building a tree that tracks free blocks, using blocks allocated from that same
tree, is a bit of a catch-22:

> Implementing the gbmap was a particularly difficult challenge due to
> catch-22 issues. How do you allocate blocks for the gbmap... from the
> gbmap... without recursion? ([#1111])

The answer is that the new gbmap's nodes are allocated from the old gbmap,
after the checkpoint. They end up behind the window, outside the new known
range, so they can't be handed out again. The next rebuild finds them in use
by the traversal, which visits the gbmap's own nodes.

So the gbmap doesn't get rid of traversals; a rebuild is a full traversal. What
it does is make each traversal count for the whole disk instead of
`8*lookahead_size` blocks, without the RAM, and remember the result across
power cycles. In the maintainer's words, this amortizes block allocation to
~_O(log_b n)_. In his benchmarks on NOR and NAND flash, the gbmap made little
difference, since large blocks keep allocation cheap there; small-block
devices like SD and eMMC are where it should help ([#1114]).

The gbmap adds code on top of the lookahead allocator, which it still needs as
a fallback and to bootstrap itself. It's tracked by a write-compat flag, so a
driver built without gbmap support can still mount a filesystem that has one,
read-only.

---

### Pre-erasing

Erasing is usually the most expensive thing a flash chip does, and on NOR
flash it's the main source of latency in writes: tens of milliseconds for a 4
KiB sector, hundreds for larger ones. The obvious fix is to erase free blocks
ahead of time, when nothing else is going on. v2 couldn't, because it had no
way to remember which blocks were erased across a power cycle, and getting
that wrong means either programming a block that isn't fully erased, or
wearing blocks out by erasing them again and again.

With the gbmap, v3 can remember. Incremental gc (below) erases free blocks
inside the known window, checksums the first program's worth of bytes, as
wide as an rbyd's ecksum, and records the block as erased in the gbmap,
along with that checksum. Later,
when the allocator picks an erased block, it checks the ecksum again. If it
matches, the block is used without an erase. If it doesn't, something has
been programmed there since, and the block is skipped entirely until the next
gbmap rebuild marks it free again.

The hard part is making sure that a block that has been even partially
programmed never matches its ecksum. Programs can be interrupted by power loss
at any point, and the program we were interrupted in the middle of could, in
principle, have written bytes that happen to match the erased state. v3 deals
with this in two ways, depending on what the block is used for:

1. Metadata logs and B-tree nodes always start with a revision count. With
   `LFS3_M_REVPERTURB`, the top bit of the first byte of the revision count is
   set to the inverse of whatever that bit currently reads on disk. So the
   very first program to a pre-erased block always changes its first prog
   unit, and an interrupted write can never leave a block that still matches
   its ecksum.

2. Data blocks can contain anything, so the same trick doesn't work. Instead,
   before writing data into a pre-erased block, littlefs commits the gbmap
   with the window moved past that block. After a power loss, the block is
   outside the trusted window, and its "erased" entry is no longer believed.

The maintainer:

> littlefs has a very conservative model of flash, and avoids progging unless
> it is sure a prog has not been attempted. We also make no assumptions about
> erase value, so can't just check for 0xffs. But with a delicate dance of
> perturb bits in mdir revision counts, and a cheaply updatable known window,
> the system works now. ([#1111])

This relies on one assumption about the hardware: a program interrupted by
power loss changes nothing outside the bytes it was given. The ecksum covers
the whole of the first program into the block, so a torn program that leaves
its first prog unit erased still fails the check. A block pre-erased under a
narrower ecksum, with a smaller `pcache_size`, isn't trusted, and is erased
again when it is allocated.

Pre-erasing needs `LFS3_PREERASE`, `LFS3_GBMAP` and `LFS3_REVPERTURB` at
compile time, `LFS3_M_REVPERTURB` at mount, and a `gc_preerase_count`, the
number of erased blocks gc tries to keep ready ahead of the window.

There's one erase pre-erasing can't remove. Compacting an mdir erases the
other block of its pair, which is never a free block, so mdir compactions
always erase on the critical path. The worked example below shows that this
is what's left once pre-erasing has done its job.

---

### Incremental gc

v3 collects all of littlefs's janitorial work into one place, `lfs3_fs_gc`,
behind the `LFS3_GC` compile-time option. Each call does up to `gc_steps`
steps, each roughly one block of work, of whichever kinds of work
`gc_flags` asks for:

1. **mkconsistent**: finish pending grm removes, and remove stickynotes left
   behind by a power loss.
2. **lookahead**: repopulate the lookahead buffer or rebuild the gbmap.
3. **preerase**: erase free blocks ahead of the allocator.
4. **compact**: compact metadata logs more than `gc_compact_thresh` full
   (by default 7/8 of a block), before a write has to.
5. **ckmeta** and **ckdata**: check checksums.

Internally, gc is just a traversal object, the same one behind the traversal
API, stored in `lfs3_t`. It's opt-in because it makes `lfs3_t` bigger. The
same work can be done to completion with `lfs3_fs_ck` or at mount with the
matching `LFS3_M_*` flags, or driven step by step with
`lfs3_trv_open`/`lfs3_trv_read` and the matching `LFS3_T_*` flags.

None of this work is new. Lookahead scans, compactions and erases all happen
anyway. Incremental gc lets an application move them out of the operations
it cares about and into time it doesn't.

## Wear leveling

v3 levels wear the same way v2 does, with two mechanisms: recovering from bad
blocks when they show up, and spreading wear evenly over the blocks that
change, so that no block wears out much earlier than the rest. As in v2, this
is dynamic wear leveling only. Blocks holding data that never changes are
never moved to give their unused erase cycles to the rest.

How wear spreads depends on what's in the block:

1. **mdirs** are compacted in place, alternating between their two blocks,
   until their recycle counter runs out. After `block_recycles` erases per
   block, the mdir moves to a new pair of blocks. `block_recycles=-1` turns
   this off, and `block_recycles=0` moves the mdir on every compaction, which
   is usually counterproductive.

2. **The mroot anchor** can't move, so the mroot chain divides its wear by
   `block_recycles` per link, as described above.

3. **B-tree nodes and data blocks** are copy-on-write. Every time one is
   compacted or rewritten, it moves to a newly allocated block.

4. **Allocation** moves linearly through the disk and wraps around, so new
   blocks are spread evenly over every free block. Without a gbmap, the
   starting point after a mount is pseudorandom (the gcksum modulo the block
   count). With a gbmap, the allocator resumes exactly where it left off,
   since the window is stored on disk, so even frequent power loss doesn't
   bias where blocks get allocated.

As in v2, the result is a best-effort statistical distribution of wear, with
the same useful property: you can extend the life of a device by giving
littlefs more storage. One thing that did change: v2's revision counts
doubled as a rough count of erases, while v3's recycle counter restarts every
time an mdir is relocated.

## Bad blocks

Flash wears out. Eventually a block fails to erase, or fails to hold what was
programmed into it. Here's what v3 does about it.

The block device reports a bad block by returning `LFS3_ERR_CORRUPT` from
`prog` or `erase`. littlefs then treats the block the way v2 did: as a reason
to relocate.

1. If an erase fails during allocation, the allocator skips the block and
   tries the next one.
2. If a prog fails while writing an mdir, a B-tree node or a data block,
   littlefs allocates a new block and writes it again. Every write in v3 is
   either copy-on-write or a compaction into a new block, so there is always
   a copy of the data to start from.
3. A prog that silently doesn't stick, or sticks wrong, isn't caught at write
   time unless `LFS3_M_CKPROGS` is enabled, in which case littlefs reads every
   prog back and treats a mismatch as a failed prog. Without it, the next
   checksum check catches it later.
4. When there is nowhere left to relocate to, writes fail with
   `LFS3_ERR_NOSPC`. An mdir that can't be relocated is compacted in place
   anyway ("overrecycled"), trading wear leveling for a little more life.

A failed read of the block being copied is not a bad destination. littlefs
keeps the two apart, and returns `LFS3_ERR_CORRUPT` instead of relocating
again, so an unreadable source can't make it allocate and erase new blocks
until the disk is exhausted.

Read errors are different. There's no copy of the data in RAM to rewrite, so
littlefs can only detect them. littlefs itself doesn't do error correction,
though a block device can, and littlefs honors any error the block device
reports.

### Remembering bad blocks

Bad-block tracking is one of the two items the maintainer lists as blocking
the release of v3, alongside this document:

> Bad-block tracking - This should be a relatively easy addition to the gbmap,
> and would be significantly valuable by making bd-level error-correction
> practical. ([#1114])

> This should be easy to add at this stage, though there are a few unanswered
> questions around the API and how to handle bad blocks detected in rdonly
> contexts. ([#1111])

With the gbmap (`LFS3_GBMAP`), littlefs remembers bad blocks. There is no
separate option: without a gbmap there is nowhere to keep the marks, and
littlefs relocates as described above, retrying a bad block each time the
allocator comes back around to it.

**On disk**, a bad block is a range in the gbmap's bad state, with the run
length as its weight and no payload. Unlike free and erased ranges, bad ranges
are trusted everywhere, not only inside the known window: allocation from the
lookahead buffer marks them as in use before adopting a window, pre-erasing
skips them, and rebuilding the gbmap keeps them, even for a block it finds
still referenced.

**In RAM**, a block that fails an erase, a prog or a `LFS3_M_CKPROGS`
read-back joins a small queue of runs, `LFS3_BADQ_SIZE` (4), and the
allocator skips queued blocks. If more runs go bad at once than the queue
holds, the smallest is forgotten, which is safe because marks are advisory: a
forgotten block fails again the next time it's tried, and is queued then.

**Marking** happens at an allocator checkpoint, as a gc step, or in
`lfs3_fs_mkbad`, when every block is accounted for. littlefs traverses the
filesystem, and writes the queued blocks that nothing references into the
in-RAM gbmap, which the next mdir commit persists. A mark is never written
for a block the committed filesystem references, so it can never hide data.
A bad block still in use, usually a data block that's released by the next
sync, or an mdir block that's released when the mdir next compacts, is
checked again after 1, 2, 4, ... commits, and whenever the gbmap is rebuilt.
Power loss can lose a mark, which only costs another failed erase or prog.

**Read-only** builds and mounts never write a mark.

**Format** writes the gbmap's root to the first block from 2 on that erases
and programs, and marks any it skipped as bad. Blocks 0 and 1 can't be
marked, since the anchor can't move; they stay a hardware requirement. A NAND
factory bad-block table can be honored by a block device that refuses to
erase those blocks, which keeps format off them, and by `lfs3_fs_mkbad` right
after format, before anything else is written.

**The API** is three calls: `lfs3_fs_mkbad` marks a block known to be bad,
refusing blocks in use with `LFS3_ERR_BUSY`; `lfs3_fs_mkgood` clears a mark,
say after a bench test; and `lfs3_fs_nextbad` lists bad blocks, including
those still queued. `LFS3_I_BADBLOCKS` says some are queued but not yet on
disk, and `lfs3_fs_usage` counts bad blocks as used. `lfs3_fs_rmgbmap` drops
every mark, and `lfs3_fs_mkgbmap` starts without any.

**No compat flag** guards the bad state. Drivers that understand the gbmap
already treat a bad range as in use when allocating from it, and an older
driver that allocates a marked block from a traversal treats it as it would an
unmarked bad block: one failed erase or prog, then relocation. A flag would
instead stop every such driver from writing the filesystem at all.

### Suspect blocks

A read that fails, with `LFS3_ERR_CORRUPT` from the block device or a
checksum that doesn't match, doesn't prove the block is bad. The supply may
have been low, or a bit left metastable by a power loss may read differently
next time. So littlefs doesn't mark these blocks bad. It lists them as
suspect, in RAM, up to `LFS3_SUSPECTS_SIZE` (8), forgetting the oldest, and
`lfs3_fs_nextsuspect` returns them, so an application can see where its flash
is going wrong and decide what to do. Finding suspects never writes, so
read-only mounts find them too. Keeping them across mounts would need a new
gbmap state and a compat flag, so it's left to the application.

### Repairing checks

With the gbmap, a check can repair what it finds, not just report it. Data
blocks and B-tree nodes have a checksum where they're referenced, so with
`ck_retries` a check that fails is read again, up to that many times, before
the check gives up with `LFS3_ERR_CORRUPT`. How hard to try is the
application's choice, since only it knows whether its supply can sag.

On a writable filesystem, `lfs3_fs_ck`, `lfs3_fs_gc` and mount-time checks
then move the contents of a suspect block to a new block, copying exactly
the bytes of a read that passed: a data block's copy is checksummed as it's
copied, and a B-tree node's copy is fetched and its checksum compared, so a
bad read is never given a fresh checksum. Commits record the low two bits of
their block, so a B-tree node is copied to a block with the same low bits.
Once the new location is committed and nothing references the old block,
littlefs tests it: erase, program a pattern, read it back. If that works the
block was probably a weak write, and it's free again; if not, or if it needs
moving twice in one mount, it's marked bad.

Some things aren't moved. mdirs and mtree nodes have no checksum in a
parent, their commits are only covered globally by the gcksum, so there's
nothing to check a copy against. Blocks of open files are left until the
files close. A block that can't be moved now, because the disk is full or
the copy never checked out, is left for the next check.

`ck_passes` makes `lfs3_fs_ck` and mount-time checks read everything more
than once, which catches more bits that read differently each time.

### What v3 still doesn't handle

1. **Blocks 0 and 1 must work.** The anchor can't move. If one of its blocks
   goes bad, the filesystem keeps working until littlefs next has to rewrite
   that block, and from then on writes fail with `LFS3_ERR_NOSPC`. Format
   also needs blocks 0 and 1 to be good.

2. **An unreadable metadata block stops allocation.** Repopulating the
   lookahead buffer or rebuilding the gbmap traverses the whole filesystem,
   so a metadata block that can't be read makes every write that needs a
   traversal fail.

3. **Without the gbmap, bad blocks aren't remembered.** A block that failed is
   retried when the allocator comes back around to it, and after every
   mount.

## Costs

### RAM

All of littlefs's RAM is either in the structs the application provides or in
buffers whose sizes come from the configuration. None of it grows with the
size of the filesystem or the number of files:

1. `lfs3_t`, plus three buffers: the read cache (`rcache_size`), the program
   cache (`pcache_size`) and the lookahead buffer (`lookahead_size`). v3
   sizes the caches independently, where v2 used one `cache_size` for all of
   them.
2. Per open file, an `lfs3_file_t` plus its file cache (`fcache_size`, which
   can be set per file).
3. Per open directory or traversal, an `lfs3_dir_t` or `lfs3_trv_t`.

Every buffer can be provided statically, and with `LFS3_NO_MALLOC` littlefs
never calls malloc. The data structures are designed around this: an rbyd
append needs a three-alt FIFO no matter how big the tree is, a B-tree
traversal needs a position and one cached leaf, and every commit is built from
a bounded list of attributes.

Measured on v3-alpha, for a Cortex-M0+ (`arm-none-eabi-gcc` 13.2,
`-mthumb -Os`), the structs are:

| struct          | v2.11 | v3 default | v3 gbmap | v3 gbmap+gc+preerase | v3 read-only |
|-----------------|------:|-----------:|---------:|---------------------:|-------------:|
| `lfs_t`/`lfs3_t` | 128 B | 188 B      | 296 B    | 436 B                | 120 B        |
| file            | 84 B  | 136 B      | 136 B    | 136 B                | 104 B        |
| directory       | 52 B  | 48 B       | 48 B     | 48 B                 | 44 B         |

A traversal, `lfs3_trv_t`, is 108 bytes in the default build. The gbmap's
in-RAM state and the gc traversal are what make `lfs3_t` grow in the larger
configurations.

### Code and stack

v3 is a little less little than v2. Measured the same way, with asserts and
logging compiled out:

| build      | v2.11    | v3        |       |
|------------|---------:|----------:|------:|
| default    | 15752 B  | 32548 B   | 2.07x |
| read-only  | 5792 B   | 9976 B    | 1.72x |
| + gbmap    |          | 35536 B   | +9.2% |
| + gbmap, gc, pre-erase |  | 36276 B | +11.5% |

The maintainer's own measurements, from a different build, show the same
ratio and also cover stack: 1440 bytes for v2 and 2136 bytes for v3 by
February 2026 ([#1111], [#1114]). He attributes most of the growth to three
things:

1. **Runtime error recovery.** Reverting in-RAM state after a failed
   operation means tracking both the before and after state of everything an
   operation touches.
2. **B-tree flexibility.** The B-trees themselves are small. Deciding how to
   write to them, crystallization, is not.
3. **Traversal inversion.** v2's traversal was a callback, with its state on
   the stack. v3's is an incremental state machine (which is what makes
   incremental gc possible), so all of that state is explicit, and it has to
   cope with the filesystem changing in the middle of a traversal.

For a sense of scale, the maintainer measured a NAND logging benchmark with
littlefs v3 at 35776 bytes of code and 3672 bytes of RAM, v2 at 16880 and
2880, SPIFFS at 25201 and 3588, and Yaffs2 at 39383 and 87252 ([#1114]).

### Erases, programs and time

For a filesystem with _m_ mdirs, metadata logs of _n_ entries, blocks of _b_
bytes, and files of _N_ bytes:

| operation                     | cost                                                     |
|-------------------------------|----------------------------------------------------------|
| mount                         | fetch and checksum every mdir: _O(m b)_ reads            |
| first write after mount       | a scan for leftover stickynotes, _O(m b)_ reads, unless mount or gc did it |
| look up a path component      | _O(log_b m)_ mtree, one _O(b)_ fetch, _O(log&sup2; n)_ name search |
| metadata commit               | _O(log n)_ bytes per tag, plus a trailer, rounded up to `prog_size` |
| metadata compaction           | _O(n log n)_ reads, at most _b/2_ bytes of programs, one erase |
| mdir relocation               | one erase, every `block_recycles` compactions per block |
| read a file at an offset      | _O(log_b N)_ node lookups                                |
| sync after an append          | the new data, plus two small mdir commits (a B-shrub) or a commit per B-tree level |
| data block                    | one erase per block of data written                      |
| B-tree node                   | one erase each time a node fills and is relocated         |
| allocation, lookahead         | a full traversal every `8*lookahead_size` blocks         |
| allocation, gbmap             | _O(log_b n)_ per block, a full traversal per rebuild     |

Erases of data blocks and B-tree nodes can be moved out of the way by
pre-erasing. Erases from mdir compactions can't, but can be done early by
gc's compaction step.

## Worked example: a flight log

The workload littlefs sees most is a log: small records appended to a file,
synced so they survive a power loss. Here's one measured end to end.

The setup:

1. A simulated W25Q128JV SPI NOR flash: 4 KiB sectors, 256-byte pages,
   0.4 ms to program a page and 45 ms to erase a sector (typical datasheet
   figures). littlefs uses an 8 MiB partition, 2048 blocks of 4 KiB, through
   the bench runner's block device.
2. Seven other files already on the filesystem, about 77 KiB, as a device's
   configuration and web assets would be.
3. A flight log: 22-byte rows, one `lfs3_file_write` every 200 ms with the rows
   due by then, and one `lfs3_file_sync` every second, for 10 simulated
   minutes. The log is read back and checked after a remount.
4. v2.11.3 with 1 KiB caches, a 16-byte lookahead and `block_cycles=500`. v3
   with 1 KiB caches, a 16-byte lookahead, `block_recycles=512`,
   `shrub_size=1024`, `fragment_size=256` and `crystal_thresh=256`. With the
   gbmap, `lookgbmap_thresh` is 512 blocks, and pre-erasing runs gc to
   completion "on the pad", before the log is opened.

The v3 runs are `bench_wlog_fresh` in `benches/bench_wlog.toml`, which
`make bench` runs; REQUIREMENTS.md, Appendix B.1, has every permutation,
with and without the gbmap and pre-erasing, and how the v2 figures were
measured.

At 1 row per second:

| configuration                          | erases/min | longest call |
|----------------------------------------|-----------:|-------------:|
| v2.11.3, `prog_size=256`               | 62.7       | 96.6 ms      |
| v2.11.3, `prog_size=16`                | 58.7       | 96.6 ms      |
| v2.11.3, `prog_size=1`                 | 58.7       | 96.6 ms      |
| v3, `prog_size=256`                    | 10.6       | 184.6 ms     |
| v3, `prog_size=16`                     | 5.7        | 93.9 ms      |
| v3, `prog_size=1`                      | 3.5        | 184.6 ms     |
| v3, `prog_size=1`, gbmap, pre-erased   | 2.9        | 50.2 ms      |

At 50 rows per second:

| configuration                          | erases/min | longest call |
|----------------------------------------|-----------:|-------------:|
| v2.11.3, `prog_size=256`               | 80.7       | 96.6 ms      |
| v2.11.3, `prog_size=16`                | 76.7       | 96.6 ms      |
| v2.11.3, `prog_size=1`                 | 76.7       | 96.6 ms      |
| v3, `prog_size=256`                    | 48.5       | 185.9 ms     |
| v3, `prog_size=16`                     | 27.8       | 145.3 ms     |
| v3, `prog_size=1`                      | 23.1       | 184.6 ms     |
| v3, `prog_size=1`, gbmap, pre-erased   | 3.4        | 52.0 ms      |

Every run read back every row intact.

---

v2 first. At 1 row per second, 577 of v2's 600 writes erased a block: each
write after a sync copied the file's tail block, frozen by the sync, into a
new block. That's the sync-padding problem from the start of this document,
one block erase and copy per sync. Setting `prog_size=1` barely helps (58.7
erases per minute), because v2 copies a partially filled tail block whenever
it extends a file, whatever the program size.

v3 doesn't copy the tail. To see where its erases come from, we instrumented
a copy of the driver to attribute every erase and every programmed byte to
the kind of block it went to:

| per minute                 | mdir erases | mdir bytes | B-tree erases | B-tree bytes | data erases | data bytes |
|----------------------------|------------:|-----------:|--------------:|-------------:|------------:|-----------:|
| 1 row/s, `prog_size=256`   | 6.5         | 25805      | 3.7           | 14131        | 0.4         | 1306       |
| 1 row/s, `prog_size=16`    | 5.2         | 19958      | 0.1           | 8            | 0.4         | 1320       |
| 1 row/s, `prog_size=1`     | 3.0         | 11022      | 0.1           | 7            | 0.4         | 1320       |
| 1 row/s, pre-erased        | 2.9         | 11742      | 0             | 6            | 0           | 1320       |
| 50 rows/s, `prog_size=256` | 14.4        | 58470      | 17.9          | 66381        | 16.2        | 65997      |
| 50 rows/s, `prog_size=16`  | 5.3         | 20117      | 6.3           | 22696        | 16.2        | 66000      |
| 50 rows/s, `prog_size=1`   | 3.2         | 11799      | 3.7           | 12727        | 16.2        | 66000      |
| 50 rows/s, pre-erased      | 3.4         | 13751      | 0             | 12622        | 0           | 66000      |

Bytes programmed to blocks that were replaced within the same call, about
2% of the total at `prog_size=256` and less otherwise, are left out. The
data columns are just the log itself: 22 or 1100 bytes a second, and one
erase per 4 KiB of it. Everything else is metadata.

With `prog_size=1`, at 1 row per second, each sync appends the new row to the
end of the log's current data block, without an erase, and makes two small
mdir commits: the shrub commit that grafts the longer block pointer into the
file's tree, and the commit that updates the file's struct tag. Together they
come to about 180 bytes. The log's tree stays a B-shrub holding a handful of
block pointers, so no B-tree node is touched. The mdir fills up and is
compacted, one erase, about every 20 syncs.

With `prog_size=256`, two things go wrong at once:

1. Every commit is rounded up to a prog boundary. The mdir took about 430
   bytes per sync instead of 180, and was compacted about every 9 syncs
   instead of every 20.

2. Data can only be appended to the data block in 256-byte units, so each
   second's 22 bytes go into a fragment in the tree instead, rewritten at
   every sync until 256 bytes have accumulated. With fragments in it, the
   tree outgrew its shrub and moved its root into a block of its own (we
   checked: it became a B-tree after three and a half minutes, and stays a
   B-shrub with `prog_size=1`). Each sync then also appends a padded commit
   to that node, about 360 bytes, and every time the node fills it is
   relocated, an erase.

At 50 rows per second the same effects compound. Within a few minutes the
log needs B-tree leaves at any `prog_size`, under a root kept in the mdir.
With `prog_size=256`, each second's 1100 bytes reach the data block in
256-byte units. A write that fills the file cache flushes only up to the
block's last prog boundary and keeps the rest cached, since more appends
will follow before the sync. The sync writes what's left as a fragment and
grafts it, with the longer block pointer, into the tree, replacing the
previous fragment and appending past it in one commit. That's about three
commits a second to the leaf, each padded to 256 bytes, and as many to the
root in the mdir: the leaf is relocated 17.9 times a minute and the mdir
compacted 14.4 times, 48.5 erases a minute against v2's 80.7. With
`prog_size=1`, the data block takes every byte, the pointer is grafted once
a second, and metadata costs 6.9 erases per minute on top of the 16.2 the
data needs.

Pre-erasing removes the data and B-tree erases from the log's path entirely:
gc on the pad erased all 2023 free blocks, which took 93 simulated seconds.
What remains, 2.9 and 3.4 erases per minute, is mdir compaction, the one erase
pre-erasing can't remove. The longest call drops from 185 ms, four erases in
one sync, to 50 ms, one erase and some programs. Pre-erasing moves erases
rather than adding them, as long as erased blocks are used while the gbmap
still trusts them.

---

So `prog_size` matters a lot more to v3 than it did to v2. v2's sync cost was
dominated by a block copy that didn't depend on the program size. v3 gets rid
of the block copy, and what's left are small commits whose size is rounded up
to `prog_size`. Going from `prog_size=1` to 16 to 256, the log's erases go
from 3.5 to 5.7 to 10.6 a minute at 1 row per second, and from 23.1 to 27.8
to 48.5 at 50, while v2's stay between 59 and 81.

On NOR flash, the page size is usually not the program size. A page is the
most a single program command can write; most SPI NOR flash, the W25Q128JV
included, can program anything from one byte up to a page, and littlefs never
programs the same byte twice between erases. `prog_size` should be the
smallest program the device can actually do. The maintainer's benchmarks came
to the same conclusion:

> On NOR, with its small 256B prog-only hardware buffer, it seems the smaller
> the better. ([#1114])

Devices that really do need large programs, NAND with per-page ECC or NOR with
on-die ECC, have to use their real program size. For them the
`prog_size=256` rows above are what to expect: a sixth of v2's erases for
slow logs, and three fifths for fast ones. REQUIREMENTS.md, Appendix B.5,
describes the changes to how littlefs appends that got fast logs there.

## Compatibility

v3 breaks disk compatibility with v2, deliberately. Supporting both
formats in one driver would cost about as much code as including both drivers,
and a lot of development time ([#1111]). v2 isn't going anywhere, and the
maintainer has said a v2-to-v3 migration function is unlikely: in his words,
"a high-effort, high-risk, low-value function" ([#1114]). Application-level
migration (copying the files over) works for any pair of filesystems.

What v3 tries to do instead is make sure there's no need for a v4. A few
lessons from v2 are built into the format:

1. **Compat flags.** Besides a major and minor version, the superblock carries
   three sets of feature flags, borrowed from Linux filesystems: rcompat
   flags, which a driver must understand to read the filesystem; wcompat
   flags, which it must understand to write to it; and ocompat flags, which it
   can ignore. Features can be added and removed independently, and a driver
   that doesn't understand a write-only feature can still mount read-only. The
   optional gbmap is tracked this way.

2. **Variable-length encodings.** Most integers on disk are
   [leb128][leb128]-encoded, so the format itself puts no practical limit on
   block counts or file sizes. The driver is limited to 31 bits for now, but
   16-bit and 64-bit drivers are possible.

3. **Tagged, variable-sized data.** Most structures are tags whose data can
   grow new fields later, and the tag encoding has a bit reserved for
   extending the tag space itself.

4. **Data blocks hold only data.** With no pointers in data blocks, a
   filesystem's data can be described by v3's metadata wherever it already
   is, which the maintainer points out makes v3 a sort of "universal
   migrator" ([#1114]).

Custom attributes keep their single-byte types, but the upper half of the
type space is now set aside for future standard attributes, which is much
easier to do while the format is changing anyway. [SPEC.md](SPEC.md) has the
details of every one of these.

## What's next

v3 is not finished. Here's where the remaining pieces stand, according to the
maintainer ([#1111], [#1114]):

1. **Bad-block tracking** in the gbmap, described above, and one of the two
   things blocking release. Blocks that fail reads are listed as suspect in
   RAM only; keeping them across mounts would be a format addition.

2. **This document and SPEC.md.** The other blocker, followed by a period of
   review, since "it would be foolish to commit to a disk format without at
   least these two documents".

3. **Metadata redundancy.** Every mdir already has a second block that sits
   unused between compactions, and metadata is usually much smaller than data,
   so the plan is to store redundant metadata as plain copies.

4. **Data redundancy and deduplication.** Error correction across data blocks,
   RAID-6 style, needs a map from blocks to parity groups. If that map is
   keyed by block checksum, the same map deduplicates blocks for free.

5. **16-bit and 64-bit variants.** Undecided, but worth a look at their
   impact on the disk format before it's frozen.

Items 3 and 4 are "high risk for the disk format, they're also low-value", and
probably won't block the release. Everything else on the original list of
stretch goals (`lfs3_migrate`, API reworks for the configuration, block device
and attributes, alternative write strategies, hole-punching and range
operations, copy-on-write file copies, reserved blocks to prevent CoW
lockups, integrated ECC, RAID) is expected to come, if at all, after the
release. Alternative checksums, compression, symlinks, filesystem shrinking
and high-level caches are out of scope.

The current plan is a v3-beta, with a stable disk format and an API that can
still change, so that users who want the new format can use it while the API
settles.

## Terms

v3 brings a lot of new vocabulary. In rough order of appearance:

| term        | meaning                                                                |
|-------------|------------------------------------------------------------------------|
| rbyd        | red-black-yellow Dhara tree: a log in one block that is also a balanced, counted tree |
| tag         | a header (type, weight, size) and its data; everything in an rbyd is a tag |
| alt pointer | a tag that branches the tree: color, direction, key, weight, jump back |
| trunk       | the run of alt pointers and the leaf written by one append            |
| rid         | the position of an entry in an rbyd, derived from weights              |
| weight      | how many rids, bytes or blocks an entry or subtree covers              |
| commit      | one or more tags and a checksum, the unit of atomicity                 |
| ecksum      | a checksum of the erased space after a commit, or of a pre-erased block |
| perturb     | inverting the next commit's valid bits so erased flash never looks valid |
| canonical checksum | the checksum of a log up to its last trunk                      |
| mdir        | a metadata log in a pair of blocks                                     |
| mroot       | the mdir holding the configuration, global state and mtree root        |
| anchor      | blocks 0 and 1, the start of the mroot chain                           |
| mtree       | the B-tree of mdirs                                                    |
| mid         | an entry's position in the filesystem: its mdir and its rid in that mdir |
| did         | a directory's id, the first half of every name in it                   |
| bookmark    | the empty-named entry that marks the start of a directory              |
| bid         | a position in a B-tree; in a file, the offset of an entry's last byte  |
| B-shrub     | a B-tree whose root is stored inside an mdir                           |
| fragment    | file data stored directly in a file's tree                             |
| block pointer | a leaf pointing to a slice of a data block, with its checksum        |
| crystallize | pack fragments and new data into a data block                          |
| gstate      | global state, stored as xor-deltas spread across mdirs                 |
| grm         | the global remove queue                                                |
| gcksum      | the global checksum, the xor of every mdir's checksum                  |
| gbmap       | the optional on-disk global block map                                  |
| known window | the range of the gbmap that can be trusted                            |
| stickynote  | a file that has been created but not yet synced                       |
| zombie      | an open handle whose file has been removed                             |

## Conclusion

And that's littlefs v3, thanks for reading!


[v2-design]: https://github.com/littlefs-project/littlefs/blob/v2/DESIGN.md

[wikipedia-flash]: https://en.wikipedia.org/wiki/Flash_memory
[wikipedia-sna]: https://en.wikipedia.org/wiki/Serial_number_arithmetic
[wikipedia-crc]: https://en.wikipedia.org/wiki/Cyclic_redundancy_check

[fat]: https://en.wikipedia.org/wiki/Design_of_the_FAT_file_system
[ext2]: http://e2fsprogs.sourceforge.net/ext2intro.html
[jffs]: https://www.sourceware.org/jffs2/jffs2-html
[yaffs]: https://yaffs.net/documents/how-yaffs-works
[spiffs]: https://github.com/pellepl/spiffs/blob/master/docs/TECH_SPEC
[ext4]: https://ext4.wiki.kernel.org/index.php/Ext4_Design
[ntfs]: https://en.wikipedia.org/wiki/NTFS
[btrfs]: https://btrfs.wiki.kernel.org/index.php/Btrfs_design
[zfs]: https://en.wikipedia.org/wiki/ZFS

[dhara]: https://github.com/dlbeer/dhara
[dhara-tree]: https://github.com/dlbeer/dhara/blob/master/map_internals.txt
[rb-tree]: https://en.wikipedia.org/wiki/Red%E2%80%93black_tree
[cb-tree]: https://www.chiark.greenend.org.uk/~sgtatham/algorithms/cbtree.html
[os-tree]: https://en.wikipedia.org/wiki/Order_statistic_tree
[leb128]: https://en.wikipedia.org/wiki/LEB128
[merkle-tree]: https://en.wikipedia.org/wiki/Merkle_tree
[brent]: https://en.wikipedia.org/wiki/Cycle_detection#Brent's_algorithm

[ramcrc32bd]: https://github.com/littlefs-project/ramcrc32bd
[ramrsbd]: https://github.com/littlefs-project/ramrsbd

[#344]: https://github.com/littlefs-project/littlefs/issues/344
[#374]: https://github.com/littlefs-project/littlefs/issues/374
[#581]: https://github.com/littlefs-project/littlefs/issues/581
[#671]: https://github.com/littlefs-project/littlefs/issues/671
[#852]: https://github.com/littlefs-project/littlefs/issues/852
[#862]: https://github.com/littlefs-project/littlefs/issues/862
[#905]: https://github.com/littlefs-project/littlefs/issues/905
[#908]: https://github.com/littlefs-project/littlefs/issues/908
[#960]: https://github.com/littlefs-project/littlefs/pull/960
[#1012]: https://github.com/littlefs-project/littlefs/issues/1012
[#1079]: https://github.com/littlefs-project/littlefs/issues/1079
[#1111]: https://github.com/littlefs-project/littlefs/pull/1111
[#1114]: https://github.com/littlefs-project/littlefs/issues/1114
