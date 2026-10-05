## littlefs technical specification

This is the technical specification of the little filesystem v3 with
on-disk version v0.0, the experimental version written by the v3-alpha
driver. This document covers the technical details of how littlefs is
stored on disk for introspection and tooling. This document assumes you are
familiar with the design of littlefs, for more info on how littlefs works
check out [DESIGN.md](DESIGN.md).

```
   | | |     .---._____
  .-----.   |          |
--|o    |---| littlefs |
--|     |---|    v3    |
  '-----'   '----------'
   | | |
```

**NOTE: THIS ON-DISK FORMAT IS NOT YET FROZEN.**

The driver writes and accepts the on-disk version v0.0 (`LFS3_DISK_VERSION`
is `0x00000000`). v0.0 marks the format as experimental: it may still change
in incompatible ways without a version bump, so two v0.0 images written by
different alpha revisions of the driver are not guaranteed to be compatible.
When the format is frozen, the release driver will use a non-zero version
and will reject v0.0 images.

Places where the code marks the format as unstable, unimplemented, or
reserved for planned features are called out in the text, and collected in
[Open points](#open-points) at the end.

## Some quick notes

- littlefs is a block-based filesystem. The disk is divided into an array of
  evenly sized blocks that are used as the logical unit of storage.

- littlefs v3 stores almost all integers as variable-length [leb128]s. Block
  addresses, weights, and sizes are limited to 31 bits, and offsets inside a
  block to 28 bits. There is no special null block address.

- Integers that are not leb128s are stored in little-endian, except for the
  16-bit tag at the start of every metadata entry, which is stored in
  big-endian (see [Tags](#tags)).

- All checksums are CRC-32C (Castagnoli), see [Checksums](#crc-32c).

- In addition to the logical block size (which usually matches the erase
  block size), littlefs also uses a program block size and read block size.
  These determine the alignment of block device operations, but don't need
  to be consistent for portability. Only the block size and block count are
  recorded on disk.

- Metadata is stored in logs of tagged entries. Every log is a
  self-balancing binary tree laid out in append-only order, which we call a
  red-black-yellow Dhara tree, or rbyd. Rbyds are the building block of
  everything else: metadata pairs, B-trees, the metadata tree, and the
  on-disk block map.

## Block device model

littlefs assumes a very conservative model of storage:

1. The disk is an array of `block_count` blocks of `block_size` bytes each.
   Blocks are addressed by their index, starting at 0.

2. A block must be erased before it can be programmed. littlefs makes no
   assumption about the value of erased bytes. An erased block may read as
   `0xff`s, `0x00`s, or anything else, and the erased value may differ
   between blocks or between erases.

3. A block can be programmed in units of `prog_size` bytes, at
   `prog_size`-aligned offsets, and each byte is programmed at most once
   between erases. littlefs never relies on being able to program a byte
   twice.

4. Reads happen in units of `read_size` bytes at `read_size`-aligned
   offsets.

5. If power is lost during a program or erase, the affected region may be
   left in any state. littlefs does not assume that a program happens in
   order, so a partially programmed unit may have any subset of its bits
   changed.

littlefs does not store `prog_size` or `read_size` on disk. Logs pad their
commits to the program size in use when they were written, and erased-state
checksums record the number of bytes they cover, so a disk can be read with a
different `prog_size`/`read_size` as long as the block device supports it.

Only blocks 0 and 1 have a fixed purpose, they hold the
[mroot anchor](#the-mroot-anchor-and-the-mroot-chain). Every other block is
allocated dynamically, though format puts the initial
[gbmap](#the-global-block-map-gbmap), if there is one, in block 2.

## Primitive encodings

### le32

A 32-bit little-endian integer:

```
.---+---+---+---.  total: 4 bytes
|     le32      |
'---+---+---+---'
```

le32s are used for checksums, revision counts, and the compat flags.

### leb128

Most integers are stored as unsigned [leb128]s: 7 bits per byte, least
significant group first, with the top bit of each byte set if more bytes
follow:

```
.---+- -+- -+- -+- -.  total: <=5 bytes
|      leb128       |
'---+- -+- -+- -+- -'

 0x00000000 => 00
 0x0000007f => 7f
 0x00000080 => 80 01
 0x00001000 => 80 20
 0x7fffffff => ff ff ff ff 07
```

All leb128s in littlefs reserve the sign bit, so a leb128 holds at most 31
bits (`0x7fffffff`) and at most 5 bytes. A leb128 that does not terminate
within 5 bytes, or that decodes to more than 31 bits, is corrupt.

The driver always writes the shortest encoding, but accepts non-canonical
(over-long) encodings when reading, such as `80 00` for 0. Writers must not
depend on this. The one place where an over-long encoding is required is
the size field of the [`CKSUM`](#0x3000-lfs3_tag_cksum) tag.

### lleb128

A "little leb128" is a leb128 limited to 28 bits (`0x0fffffff`), so that it
fits in 4 bytes. lleb128s are used for offsets and sizes inside a block,
which limits `block_size` to 2^28 bytes:

```
.---+- -+- -+- -.  total: <=4 bytes
|    lleb128    |
'---+- -+- -+- -'
```

On disk an lleb128 is just a leb128. A reader decodes it as a leb128 and
must treat a value above `0x0fffffff` as corrupt.

### CRC-32C

All checksums in littlefs are CRC-32C (Castagnoli), with the polynomial
`0x1edc6f41` (`0x82f63b78` bit-reversed), a reflected input and output, an
initial value of `0xffffffff`, and a final xor of `0xffffffff`. This is the
standard CRC-32C as used by iSCSI and ext4, so `crc32c("123456789")` is
`0xe3069283`.

littlefs computes checksums incrementally, `crc32c(crc, data)` continues the
checksum `crc` over `data`, and `crc32c(0, data)` is the standard CRC-32C of
`data`:

```c
uint32_t crc32c(uint32_t crc, const uint8_t *data, size_t size) {
    crc ^= 0xffffffff;
    for (size_t i = 0; i < size; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0x82f63b78 : 0);
        }
    }
    return crc ^ 0xffffffff;
}
```

The CRC-32C polynomial has an even number of terms, so it is divisible by
`x+1`, and a CRC-32C preserves parity: the parity (xor of all bits) of a
running checksum equals the parity of the initial checksum xored with the
parity of every byte fed into it. littlefs uses this to derive its
[valid bits](#valid-bits-and-parity) from the running checksum.

Two constants come up:

- `0xfca42daf`, the "odd-parity zero". It is the polynomial
  `P(x)/(x+1)` in the reflected representation. Multiplying it by `x`
  modulo `P(x)` gives itself back, so xoring it into a running CRC-32C at
  any point has the same effect as xoring it into the final result: it flips
  the checksum's parity without otherwise changing its position in the
  CRC-32C ring. littlefs uses it to [perturb](#perturbation) commits.

- `0xef2f4c10` is the CRC-32C of 16 bytes of `0xff`, which shows up in the
  examples below as the erased-state checksum of NOR flash with a 16-byte
  program size.

### CRC-32C multiplication

The [global checksum](#gcksum) uses multiplication in the CRC-32C ring, that
is, the carry-less product of two checksums modulo the CRC-32C polynomial.
Because checksums are stored bit-reversed, the 63-bit carry-less product is
shifted left by one before reduction:

```c
uint32_t crc32c_mul(uint32_t a, uint32_t b) {
    // carry-less multiply
    uint64_t r = 0;
    for (int i = 0; i < 32; i++) {
        if (b & (1 << i)) {
            r ^= (uint64_t)a << i;
        }
    }
    // reflected inputs give a 63-bit result, so shift by 1, then reduce
    r <<= 1;
    for (int i = 0; i < 32; i++) {
        r = (r >> 1) ^ ((r & 1) ? 0x82f63b78 : 0);
    }
    return (uint32_t)r;
}

uint32_t crc32c_cube(uint32_t a) {
    return crc32c_mul(crc32c_mul(a, a), a);
}
```

For example, `crc32c_cube(0x9c558f15)` is `0x1228c36f`.

### Parity

`parity(x)` is the xor of all bits of the 32-bit word `x`, 1 if `x` has an
odd number of set bits and 0 otherwise.

[leb128]: https://en.wikipedia.org/wiki/LEB128

## Tags

In littlefs, tags describe every type of metadata. And this means _every_
type of metadata: file names, file data, directory ids, config, global
state, the pointers between B-tree nodes, even the inner nodes of the rbyd
trees themselves and the checksums that end each commit.

Each tag is a 16-bit tag followed by two leb128s, a weight and a size:

```
.---+---+---+- -+- -+- -+- -+---+- -+- -+- -.  tag:    1 be16    2 bytes
|  tag  | weight            | size          |  weight: 1 leb128  <=5 bytes
'---+---+---+- -+- -+- -+- -+---+- -+- -+- -'  size:   1 leb128  <=4 bytes
                                               total:            <=11 bytes
```

The 16-bit tag is stored in big-endian, the only big-endian value in
littlefs. This puts the tag's valid bit in the first bit of the first byte,
where it can be checked before anything else is parsed.

Most tags are followed by `size` bytes of data. The exception is alt
pointers, which have no data and reuse the size field as a jump offset.

Tag fields:

1. **Tag (16-bits)** - Type of the tag, broken down further below.

2. **Weight (leb128, <=31-bits)** - The number of ids (rids) the tag
   occupies, or for alt pointers the number of ids in the subtree they
   point to. What an id means depends on the tree: a file in a metadata
   pair, a byte in a file, a block in the block map, etc. See
   [Weights and rids](#weights-and-rids).

3. **Size (leb128, <=28-bits)** - Size of the attached data in bytes. For
   alt pointers this is the jump, see [Alt pointers](#alt-pointers).

A tag must fit in its block: a reader treats a tag that starts less than 4
bytes before the end of the block, or whose data extends past the end of
the block, as corrupt. The smallest tag is 4 bytes, a 16-bit tag and two
single-byte leb128s.

The 16-bit tag itself contains some densely packed information, and there
are two layouts, one for normal tags and one for alt pointers:

```
normal tags:

[----            16             ----]
[1|1| 2 |-- 4 --|1|------ 7 ------]
 ^ ^  ^     ^    ^        ^- subtype
 | |  |     |    '---------- reserved (0)
 | |  |     '--------------- suptype
 | |  '--------------------- mode
 | '------------------------ alt bit (0)
 '-------------------------- valid bit

alt pointers:

[----            16             ----]
[1|1|1|1|---------- 12 ----------]
 ^ ^ ^ ^           ^- key
 | | | '------------- direction (0 = le, 1 = gt)
 | | '--------------- color (0 = black, 1 = red)
 | '----------------- alt bit (1)
 '------------------- valid bit
```

Tag fields:

1. **Valid bit (1-bit)** - Used to detect the end of the log and
   incomplete commits, see [Valid bits and parity](#valid-bits-and-parity).
   The valid bit is not part of the tag's type. Readers clear it after
   checking it, and it is excluded from checksums.

2. **Alt bit (1-bit)** - Set for [alt pointers](#alt-pointers), the inner
   nodes of an rbyd tree. Everything below describes normal tags unless it
   says otherwise.

3. **Mode (2-bits)** - Splits normal tags into four classes:

   | mode | tags            | meaning                                      |
   |------|-----------------|----------------------------------------------|
   | `00` | `0x0000-0x0fff` | normal tags, part of an rbyd's main tree     |
   | `01` | `0x1000-0x1fff` | shrub tags, part of a [shrub](#shrubs)        |
   | `11` | `0x3000-0x3fff` | checksum tags, outside of any tree           |
   | `10` | `0x2000-0x2fff` | reserved, never written                      |

4. **Suptype (4-bits)** - The broad type of the tag: config, gstate, name,
   struct, attribute, etc.

5. **Reserved (1-bit)** - Bit 7 is reserved for a future extension of the
   subtype (the plan is to turn it into a leb128 continuation bit). Writers
   must write 0. The driver does not check it on read.

6. **Subtype (7-bits)** - The specific type within the suptype.

Normal and shrub tags are sorted in an rbyd by their **key**, which is the
lower 12 bits of the tag, the suptype and subtype (`tag & 0x0fff`). The mode
bits are not part of the key, so a shrub tag `0x1404` has the same key,
`0x404`, as the normal tag `0x0404`. The tag `0x0000`, and any tag with key
0 (`0x1000`), is the null tag, it never names real data.

Several tags, `MAGIC`, `GBMAPDELTA`, and most struct tags, reserve their two
lowest bits for redundancy. In v0.0 these bits are fixed by the tag values
listed below, and the driver matches them exactly, see
[Open points](#open-points).

### Weights and rids

Each rbyd is a sorted map from (rid, key) to data. The rids are not stored
anywhere. Instead each tag has a weight, and an rbyd with total weight `W`
holds the rids `0` to `W-1`. A tag with weight `w` occupies `w` rids, and
its rid is the last of them. So an entry covering a range of ids is keyed by
its last id, which is what makes range lookups work: looking up any id in
the range finds the entry.

Multiple tags can share a rid, such as a file's name, its data, and its
custom attributes. Only one tag per rid carries the rid's weight, the tag
with the smallest key, the others have weight 0.

Tags can also have rid -1. These are tags that sort before rid 0, have
weight 0, and hold the rbyd's own attributes, such as the config in the
mroot or global-state deltas in a metadata pair.

A tag's weight field records the weight the tag had when it was written.
Because later commits can change the weight of an existing rid without
rewriting its tags, a reader must compute the weight of a rid from the tree
(see [Looking up tags](#looking-up-tags)), not from the weight field of the
tag it finds. The weight fields of the most recent trunk, on the other hand,
always add up to the rbyd's total weight.

### Alt pointers

Alt pointers ("alts") are the inner nodes of an rbyd's binary tree. Each alt
splits the (rid, key) space of its subtree in two, and points backwards in
the log to one side, the other side continuing with the next tag in the
log:

```
        tag
[--      16      --][- leb128 -][- leb128 -]
[1|1|1|1|-- 12 ---][- weight  -][-  jump  -]
 ^ ^ ^ ^    ^           ^           ^- jump, distance back to the target
 | | | |    |           '------------- weight of the target subtree
 | | | |    '------------------------- key
 | | | '------------------------------ direction (le/gt)
 | | '-------------------------------- color (black/red)
 | '---------------------------------- alt bit
 '------------------------------------ valid bit
```

Alt fields:

1. **Color (1-bit)** - Red (1) or black (0). The color is only used by
   writers to keep the tree balanced, as in a red-black tree. Readers ignore
   it.

2. **Direction (1-bit)** - Whether the alt points to the lower (`le`, 0) or
   upper (`gt`, 1) side of its split.

3. **Key (12-bits)** - The tag key of the split. Together with the weight,
   this defines the (rid, key) split point.

4. **Weight (leb128)** - The number of rids in the subtree the alt points
   to, the side of the split that is jumped to.

5. **Jump (leb128)** - The distance in bytes from the start of this alt
   back to the tag it points to. Alts always point backwards, to an earlier
   tag in the same block. A jump of 0 marks an unreachable alt. Writers
   sometimes emit these to keep the tree balanced. They are always black,
   and a reader must never follow one.

Following the driver's `lfs3_tag_follow`, an `le` alt with weight `w` and key
`k`, in a subtree that holds rids `[lower, upper)`, points to every
(rid, key) with:

```
rid < lower+w-1, or rid == lower+w-1 and key <= k
```

and a `gt` alt with weight `w` and key `k` points to every (rid, key) with:

```
rid > upper-w-1, or rid == upper-w-1 and key > k
```

## Rbyds

Red-black-yellow Dhara trees (rbyds) form the backbone of littlefs. Every
metadata block in littlefs, whether it belongs to a metadata pair, a B-tree
node, or the on-disk block map, is an rbyd.

An rbyd is an append-only log of tags in a single block. Each block
behaves as a log containing a variable number of commits, and commits can
be appended to the log without an erase. Unlike v2's metadata logs, the
tags in an rbyd also form a self-balancing binary tree: each commit appends
a new path from the root of the tree, its "trunk", made out of alt pointers
that point back into older parts of the log. This keeps lookups
logarithmic in the number of tags, instead of linear in the size of the
log.

The high-level layout of an rbyd block is fairly simple:

```
  .---------------------------------------.
.-|  revision count   |      trunks       |  \
| |-------------------+                   |  |
| |                                       |  |
| |                                       |  +-- 1st commit
| |         +---------+-------------------|  |
| |         | ecksum  |   cksum + padding |  /
| |---------+---------+-------------------|
| |                trunks                 |  \
| |                                       |  |
| |                                       |  +-- 2nd commit
| |    +---------+------------------------|  |
| |    | ecksum  |    cksum + padding     |  /
| |----+---------+------------------------|
| |           erased storage              |  more commits
| |                                       |       |
| |                                       |       v
| |                                       |
| '---------------------------------------'
'---------------------------------------'
```

Each rbyd block contains a 32-bit revision count followed by a number of
commits. Each commit contains some number of trunks, followed by some
checksum tags, and ends with a [`CKSUM`](#0x3000-lfs3_tag_cksum) tag
containing a 32-bit CRC-32C. The `CKSUM` tag's data is padded so the next
commit starts at a `prog_size`-aligned offset.

Rbyd block fields:

1. **Revision count (32-bits, le32)** - Lets a metadata pair tell its two
   blocks apart, see [Revision counts](#revision-counts).

2. **Commits** - One or more commits. A block without a valid commit
   containing at least one trunk is not a valid rbyd.

### Trunks

A trunk is a sequence of zero or more alt pointers terminated by exactly
one non-alt tag, the trunk's leaf:

```
trunk = alt* leaf
leaf  = any normal or shrub tag (mode 00 or 01), including null tags
```

Each trunk is a path from the root of the tree to a leaf, and is a complete
snapshot of the tree as of that trunk. The offset of the trunk's first tag
(the first alt, or the leaf if there are no alts) is the trunk's address,
and the sum of the weight fields of the trunk's tags, alts and leaf, is the
weight of the tree.

A commit can contain any number of trunks, and writers usually append one
trunk per tag they add. Only the last trunk whose leaf is a normal tag
matters for the rbyd itself. Earlier trunks remain valid snapshots, and are
referenced by alt pointers in later trunks, by B-tree branches (which store
a trunk address, see [B-trees](#b-trees)), and, for shrub trunks, by the
[`BSHRUB`](#0x0428-lfs3_tag_bshrub) tags that own them.

Trunks whose leaf is a shrub tag belong to a shrub, a secondary tree living
in the same block, see [Shrubs](#shrubs). They never become the rbyd's
trunk.

The null tag `0x0000` (or `0x1000` in a shrub) is a valid leaf. Writers use
it to terminate a trunk that leads nowhere, when tags are removed or when
building a tree during compaction. Lookups never return a null tag.

### Revision counts

The revision count is a le32 at offset 0 of every rbyd. The only strict
requirement on it is that, in a [metadata pair](#metadata-pairs), the most
recently written block has the more recent revision count, compared with
sequence arithmetic, `(int32_t)(a - b) > 0`, to allow overflow. The one
exception is a [settled copy](#settled-copies), which keeps the revision
count of the block it copies.

Readers must not depend on any other structure in the revision count. With
that said, the driver crams a few optional features into it:

```
[----            32             ----]
[- 4 -|-- r --|-- n --|1|--- 7 ---]
   ^      ^       ^    ^      ^- low-effort debug bits
   |      |       |    '-------- perturb bit (if revperturb)
   |      |       '------------- pseudorandom noise (if revnoise)
   |      '--------------------- recycle counter (recycle-bits wide)
   '---------------------------- relocation revision
```

1. **Relocation revision (4-bits)** - Incremented each time an mdir is
   allocated a new block. A newly allocated block's other half may still hold
   a stale but valid rbyd, so a new mdir reads the old revision count and
   increments these bits, which guarantees the new block compares as more
   recent.

2. **Recycle counter** - Counts erases for wear-leveling,
   `nlog2(2*(block_recycles+1)+1)-1` bits wide (at most 20). The driver
   relocates a metadata pair when this counter overflows.

3. **Noise** - With `LFS3_M_REVNOISE`, the bits between the recycle
   counter and bit 8 are xored with the global checksum. This reduces the
   chance of checksum collisions caused by filesystem bugs.

4. **Perturb bit (bit 7)** - With `LFS3_M_REVPERTURB`, bit 7 is set to the
   inverse of bit 7 of the block's erased state. This guarantees that the
   first program of a reused block changes its first byte, which
   invalidates any [erased-state checksum](#the-global-block-map-gbmap)
   recorded for the block. Pre-erased blocks are only reused when this is
   enabled.

5. **Debug bits (7-bits)** - ASCII hints for debugging: `'h'` (`0x68`) in
   the format-time mroot anchor (whose revision counts start as
   `0xf0216968` and `0x00216968`, "hi!"), `'m'` (`0x6d`) in other metadata
   pairs, and `'b'` (`0x62`) in B-tree nodes.

B-tree nodes are never paired, so their revision counts have no meaning.

### Valid bits and parity

Every tag has a valid bit, used to find the end of the log and to detect
partially written commits. A tag is valid if its valid bit equals the
parity of the running checksum before it (see [Checksums](#checksums)):

```
valid bit == parity(running checksum before the tag)
```

Since CRC-32C preserves parity, the parity of the running checksum is the
parity of every byte it covers: the revision count, the trunks of earlier
commits, and the tags and data so far in this commit, inverted if the
previous commit was [perturbed](#perturbation). In other words, each valid
bit is a parity bit for the tags and data that precede it in its commit.
Valid bits are excluded from checksums, readers clear them before
checksumming a tag.

This gives littlefs a cheap way to detect where the log ends. A reader
parsing the log stops at the first tag whose valid bit is wrong, and
everything from that tag on is either erased storage or an incomplete
commit.

Parity only applies to tags inside a commit. The tag after a commit's
`CKSUM` tag, which starts after the `CKSUM` tag's padding, belongs to the
next commit, and the next commit's running checksum restarts from the
canonical checksum (see below). So the byte after a commit is either the
next commit's first valid bit, `parity(canonical checksum) ^ perturb`, or
erased state that a writer may have arranged to look invalid by
[perturbing](#perturbation) the commit. It is not a parity bit for the
`CKSUM` tag, which is protected by its own checksum. Checksum tags before
the `CKSUM` tag, such as `ECKSUM`, are different: the valid bit of the tag
after them does cover them, like any other tag in the commit, even though
they are not part of the canonical checksum.

The driver can optionally use valid bits as a cheap check of individual
tags when reading, without checksumming the whole log
(`LFS3_M_CKMETAPARITY`): the parity of a tag and its data must match the
valid bit of the tag that follows it. This works for every alt and leaf,
since every commit ends with at least a `CKSUM` tag after its trunks, but
not for the `CKSUM` tag itself.

### Checksums

Each rbyd keeps two checksums: a running checksum that covers everything,
and a canonical checksum that only covers the tree.

The **running checksum** starts as the CRC-32C of the 4-byte revision count,
`crc32c(0, rev)`. Each tag and its data are then added in log order, with
the tag's valid bit cleared:

```
running = crc32c(running, tag bytes with bit 7 of the first byte cleared)
running = crc32c(running, data)     // except for CKSUM tags and alts
```

The **canonical checksum** is the value of the running checksum after the
last trunk tag, alt or leaf (any tag whose mode is not `11`), with any
[perturbation](#perturbation) removed. In practice, since writers put all
checksum tags at the end of a commit, the canonical checksum is the CRC-32C
of the revision count followed by every alt and leaf, and their data, in
log order, with valid bits cleared, skipping checksum tags.

The canonical checksum is what identifies an rbyd's contents: it is stored
in B-tree [branches](#0x0400-lfs3_tag_branch), and it is what metadata
pairs contribute to the [global checksum](#gcksum). It does not depend on
erased state, padding, or how a writer split its tags into commits.

At the end of each commit, the [`CKSUM`](#0x3000-lfs3_tag_cksum) tag holds
the running checksum as of the end of the `CKSUM` tag's own tag and leb128s,
not including its data. So a commit's checksum covers every tag in the
commit, the checksum tags included, plus the canonical checksum of
everything before it.

After a valid `CKSUM` tag, the running checksum restarts from the canonical
checksum:

```
running = canonical ^ (perturb ? 0xfca42daf : 0)
```

where `perturb` is the `CKSUM` tag's perturb bit. Checksum tags (and the
padding) from earlier commits are never part of later checksums.

### Perturbation

The valid bit alone is not enough to know if the next commit has been
written. The space after the last commit is usually erased, and since
littlefs makes no assumptions about the erased value, an erased byte may
look like a valid tag.

To prevent this, the `CKSUM` tag includes a perturb bit. When a writer
finishes a commit and the first byte of the following erased storage would
pass the valid-bit check, it sets the perturb bit. A set perturb bit xors
the next commit's running checksum with the odd-parity zero `0xfca42daf`,
which inverts the parity, and so inverts every valid bit in the next
commit. Erased storage that looked valid now looks invalid, and an actual
next commit is written with inverted valid bits.

Because `0xfca42daf` keeps its position in the CRC-32C ring, xoring it into
the start of the running checksum is the same as xoring it into the
checksum at the end. Writers usually compute the next commit's checksum
without the perturbation and xor `0xfca42daf` into the stored value.

The perturbation never reaches the canonical checksum.

### Erased-state checksums

Valid bits catch most interrupted commits, but not all of them. Since a
program may change any subset of bits when power is lost, a commit may have
been attempted and left the first valid bit untouched. Appending to such a
block would program bytes twice.

To ensure we only ever program erased bytes, each commit can contain an
erased-state checksum, an [`ECKSUM`](#0x3200-lfs3_tag_ecksum) tag with a
CRC-32C of the first `cksize` bytes after the commit, taken when the block
was erased. `cksize` is the writer's `prog_size`.

A writer may only append to an rbyd if its last commit has an `ECKSUM`,
and:

1. The end of the commit, `eoff`, is aligned to the current `prog_size`.

2. `eoff + cksize < block_size`.

3. The first byte after the commit does not pass the valid-bit check, that
   is, `(byte >> 7) ^ perturb != parity(canonical checksum)`.

4. The CRC-32C of the `cksize` bytes at `eoff` matches the `ECKSUM`.

If any of these fail, we must assume a commit was attempted but failed due
to power-loss, and the rbyd must be compacted into a new block before it
can be written again. Readers that don't write can ignore erased-state
checksums entirely.

Writers only write an `ECKSUM` when there is room for another commit.
Commits that fill their block, ending exactly at `block_size`, omit it.

Note the writer emits an `ECKSUM` whenever the next commit would start
before `block_size`, but the reader requires `eoff + cksize < block_size`.
A commit that ends exactly one program unit before the end of the block
gets an `ECKSUM` that a later fetch will reject. This is harmless, it only
wastes the last program unit of the block.

### Fetching an rbyd

Finding the current state of an rbyd, "fetching" it, means scanning the
log from the start, checking valid bits and checksums, and keeping the last
trunk of the last valid commit:

```
running   = crc32c(0, block[0:4])
canonical = running
perturb   = false
off       = 4
pending   = (none)     // trunk/weight/gcksumdelta/ecksum of current commit

while off < block_size:
    parse tag, weight, size at off, stop if it doesn't fit
    stop if tag.valid != parity(running)
    running = crc32c(running, tag bytes with the valid bit cleared)
    stop if the tag is not an alt and its data doesn't fit

    if the tag is not an alt:
        if (tag & 0xff00) != 0x3000:      // not a CKSUM tag
            running = crc32c(running, data)
            remember ECKSUM/GCKSUMDELTA data in pending
        else:                             // a CKSUM tag
            stop if size < 4
            stop if (tag & 0x3) != (block & 0x3)
            stop if le32(data) != running
            commit pending: the rbyd's trunk, weight, checksum
                (= canonical), gcksumdelta, ecksum, eoff = off+tag+size
            perturb = tag & 0x4
            running = canonical ^ (perturb ? 0xfca42daf : 0)

    if (tag & 0xf000) != 0x3000:          // a trunk tag, alt or leaf
        add the tag's weight to the current trunk
        if the tag is a leaf:
            if it is not a shrub tag:
                pending trunk/weight = current trunk/weight
            start a new trunk
        canonical = running ^ (perturb ? 0xfca42daf : 0)

    off += tag + (size if not an alt)

the rbyd is corrupt if no commit was found
```

A few notes:

- The phase check, `(tag & 0x3) == (block & 0x3)`, catches a block that
  was copied or mounted at the wrong address. See
  [`CKSUM`](#0x3000-lfs3_tag_cksum).

- The rbyd's state is only updated at a valid `CKSUM` tag, so the trunks of
  an incomplete commit are ignored.

- `GCKSUMDELTA` and `ECKSUM` tags only count for the commit they are in.
  The rbyd's gcksumdelta and ecksum are the ones in its last commit, or
  none.

- Checksum tags other than `CKSUM`, `ECKSUM` and `GCKSUMDELTA`, including
  [`NOTE`](#0x3100-lfs3_tag_note), are checksummed and otherwise ignored.

When the expected trunk and checksum are already known, as for B-tree nodes
reached through a branch, a reader can skip fetching and walk the tree
directly from the trunk. The driver does this unless `LFS3_M_CKFETCHES` is
set. To validate such a node, a reader fetches the block until the commit
that contains the trunk, and checks that the canonical checksum at the end
of that commit matches the branch's checksum.

### Looking up tags

Lookups walk the most recent trunk from its first tag, keeping track of the
range of rids `[lower, upper)` that the current subtree holds, starting
from `[0, weight)`:

```
lookupnext(rid, key):
    key = max(key, 1)
    if rid >= weight: not found
    branch = trunk, lower = 0, upper = weight

    loop:
        read the tag at branch
        if alt:
            if follow(alt, w, lower, upper, rid, key):
                // take the alt, flipping it to describe the side we are
                // jumping to
                alt.direction = !alt.direction
                w = (upper - lower) - w
                next = branch - jump
            else:
                next = branch + tag size

            if alt.direction == gt: upper -= w
            else:                   lower += w
            branch = next

        else:
            // found a leaf
            if key(leaf) == 0: not found
            rid' = upper - 1, weight' = upper - lower
            if (rid', key(leaf)) < (rid, key): not found
            return rid', key(leaf), weight', data
```

`follow` is the rule from [Alt pointers](#alt-pointers). This finds the
first tag with (rid', key) >= (rid, key), so a lookup for an exact tag
checks the result. Rid -1 tags are found like any other, with
`lower = upper = 0` at their leaves.

The weight of a rid is the weight' found by looking up its smallest key,
`lookupnext(rid, 0)`. Other tags at the same rid have a weight' of 0.

#### Example: a small rbyd

In the examples in this document, hexdump offsets are relative to the start
of the block, and decoded tags show the valid bit as `v0`/`v1`, followed by
the tag with its valid bit cleared.

Here is the on-disk block map (gbmap) root of a freshly formatted 4 KiB x
32 filesystem, block 2. It maps blocks 0-2 as in-use and blocks 3-31 as
free:

```
00000000: 62 00 00 00 84 41 03 00 44 41 03 04 84 40 1d 00  b....A..DA...@..
00000010: b2 00 00 05 10 10 4c 2f ef b0 06 00 90 80 80 00  ......L/........
00000020: c5 3c 50 b6 ff ff ff ff ff ff ff ff ff ff ff ff  .<P.............
```

```
off   bytes                    tag
0000: 62 00 00 00              revision count 0x00000062 ('b')
0004: 84 41 03 00              v1 0x0441 BMINUSE w3 size 0
0008: 44 41 03 04              v0 0x4041 alt b le 0x441 w3 jump 4 => 0x0004
000c: 84 40 1d 00              v1 0x0440 BMFREE w29 size 0
0010: b2 00 00 05              v1 0x3200 ECKSUM w0 size 5
0014:   10 10 4c 2f ef           cksize 16, cksum 0xef2f4c10
0019: b0 06 00 90 80 80 00     v1 0x3006 CKSUM phase 2, perturb, w0 size 16
0020:   c5 3c 50 b6 ff ... ff    cksum 0xb6503cc5, 12 bytes of padding
```

There are two trunks. The first, at `0x0004`, is just the `BMINUSE` leaf.
The second, at `0x0008`, is an alt and the `BMFREE` leaf, and is the rbyd's
trunk. Its weight is 3 + 29 = 32. The canonical checksum, the CRC-32C of
bytes `0x00-0x0f` with valid bits cleared, is `0x10b0b710`.

Looking up rid 1 at `0x0008`, `rid < 0+3-1`, so we take the alt. Flipping
it gives a `gt` alt with weight 32-3 = 29, so `upper` becomes 3, and we land
on the `BMINUSE` leaf at `0x0004` with rid `upper-1 = 2` and weight
`upper-lower = 3`: blocks 0 to 2 are in use.

Looking up rid 10, we don't take the alt, `lower` becomes 3, and we land
on the `BMFREE` leaf at `0x000c` with rid 31 and weight 29: blocks 3 to 31
are free.

The commit ends at `0x0030`, a multiple of the 16-byte `prog_size`. The
first byte after it, `0xff`, has its top bit set, which happens to equal
the parity of the canonical checksum, so the writer set the perturb bit,
and a commit appended here would start with its valid bits inverted.

### Shrubs

A shrub is a second, smaller rbyd tree living inside another rbyd's block,
written in the same log as the main tree. littlefs uses shrubs to store
small files, and the roots of small file B-trees, directly in their
metadata pair, see [Files](#files).

Shrub trees are made of the same tags as main trees, except that their
leaves have the shrub mode bits set (`0x1000`). Shrub alts are normal alts,
since the alt bit already determines the tag's layout. Because their leaves
are shrub tags, fetch never mistakes a shrub trunk for the main trunk.
Otherwise shrub tags are normal trunk tags, and are part of the block's
canonical checksum.

A shrub has no revision count or commits of its own. It is identified by
the offset of its trunk in the block and its weight, which are stored in
the [`BSHRUB`](#0x0428-lfs3_tag_bshrub) tag that owns it. Lookups in a
shrub work exactly like lookups in the main tree, with the shrub's trunk and
weight, and return the key without the shrub bits. A shrub trunk and the
`BSHRUB` tag that points to it are always written in the same commit.

When a metadata pair is compacted, its shrubs are copied into the new block
and their `BSHRUB` tags are updated with the new trunk offsets.

### Example: a complete commit

Here is the first commit in block 1 of a freshly formatted 4 KiB x 32
filesystem, with a 16-byte `prog_size`. This is the mroot anchor, see
[The mroot anchor and the mroot chain](#the-mroot-anchor-and-the-mroot-chain):

```
00000000: 68 69 21 00 81 31 00 08 6c 69 74 74 6c 65 66 73  hi!..1..littlefs
00000010: c1 31 00 0c 01 34 00 02 00 00 e1 31 00 16 41 34  .1...4.....1..A4
00000020: 00 0a 81 35 00 04 90 0c 01 00 61 31 00 26 e1 34  ...5......a1.&.4
00000030: 00 1a 41 35 00 10 81 36 00 04 00 00 04 01 c1 34  ..A5...6.......4
00000040: 00 14 61 35 00 20 41 36 00 10 81 38 00 03 ff 1f  ..a5. A6...8....
00000050: 1f c1 34 00 27 61 35 00 33 e1 36 00 23 c1 38 00  ..4.'a5.3.6.#.8.
00000060: 13 81 39 00 02 ff 01 61 34 00 3d c1 36 00 16 61  ..9....a4.=.6..a
00000070: 38 00 25 c1 39 00 12 81 3a 00 05 ff ff ff ff 07  8.%.9...:.......
00000080: e1 34 00 56 c1 36 00 2f 61 38 00 3e e1 39 00 2b  .4.V.6./a8.>.9.+
00000090: 41 3a 00 19 83 04 01 01 00 33 00 00 04 6f c3 28  A:.......3...o.(
000000a0: 12 b2 00 00 05 10 10 4c 2f ef b0 01 00 8f 80 80  .......L/.......
000000b0: 00 fa 42 c3 92 ff ff ff ff ff ff ff ff ff ff ff  ..B.............
000000c0: ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff  ................
```

```
off   bytes                    tag
0000: 68 69 21 00              revision count 0x00216968 ("hi!")
0004: 81 31 00 08              v1 MAGIC rid -1 w0 size 8
0008:   6c 69 74 74 6c 65 66 73  "littlefs"
0010: c1 31 00 0c              v1 alt b le 0x131 w0 jump 12 => 0x0004
0014: 01 34 00 02              v0 VERSION w0 size 2
0018:   00 00                    v0.0
001a: e1 31 00 16              v1 alt r le 0x131 w0 jump 22 => 0x0004
001e: 41 34 00 0a              v0 alt b le 0x134 w0 jump 10 => 0x0014
0022: 81 35 00 04              v1 RCOMPAT w0 size 4
0026:   90 0c 01 00              0x00010c90
 ... more trunks: WCOMPAT, GEOMETRY, NAMELIMIT, FILELIMIT ...
0080: e1 34 00 56              v1 alt r le 0x134 w0 jump 86 => 0x002a
0084: c1 36 00 2f              v1 alt b le 0x136 w0 jump 47 => 0x0055
0088: 61 38 00 3e              v0 alt r le 0x138 w0 jump 62 => 0x004a
008c: e1 39 00 2b              v1 alt r le 0x139 w0 jump 43 => 0x0061
0090: 41 3a 00 19              v0 alt b le 0x13a w0 jump 25 => 0x0077
0094: 83 04 01 01              v1 BOOKMARK rid 0 w1 size 1
0098:   00                       did 0, the root directory
0099: 33 00 00 04              v0 GCKSUMDELTA w0 size 4
009d:   6f c3 28 12              0x1228c36f
00a1: b2 00 00 05              v1 ECKSUM w0 size 5
00a5:   10 10 4c 2f ef           cksize 16, cksum 0xef2f4c10
00aa: b0 01 00 8f 80 80 00     v1 CKSUM phase 1 w0 size 15
00b1:   fa 42 c3 92              cksum 0x92c342fa
00b5:   ff ff ff ff ff ff ff ff ff ff ff
                                 11 bytes of padding, contents unspecified
```

The rbyd's trunk is the last trunk, at `0x0080`, five alts and the
`BOOKMARK` leaf, with weight 1. The canonical checksum, the CRC-32C of bytes
`0x00-0x98` with valid bits cleared, is `0x9c558f15`, and the
`GCKSUMDELTA` is its cube, `0x1228c36f`.

The first valid bit, at `0x0004`, is 1, the parity of
`crc32c(0, 68 69 21 00)` = `0x15b8cf4e`. The `CKSUM` tag's size, 15, is
encoded as a 4-byte leb128 (`8f 80 80 00`) and covers the 4-byte checksum
and 11 bytes of padding that bring the next commit to `0x00c0`. The erased
`0xff` at `0x00c0` has its top bit set, while the next commit's first valid
bit would be `parity(0x9c558f15)` = 0, so no perturbation was needed.

Looking up the `NAMELIMIT` tag, (rid -1, `0x139`), from the trunk:

```
0080: alt r le 0x134 w0   [0,1)  not taken      => 0x0084  [0,1)
0084: alt b le 0x136 w0   [0,1)  not taken      => 0x0088  [0,1)
0088: alt r le 0x138 w0   [0,1)  not taken      => 0x008c  [0,1)
008c: alt r le 0x139 w0   [0,1)  taken (gt w1)  => 0x0061  [0,0)
0061: NAMELIMIT, rid 0-1 = -1, weight 0
```

## Metadata pairs

Metadata pairs (mdirs) hold the filesystem's metadata: file names, small
files and the roots of larger ones, directories, custom attributes, and
global-state deltas.

As their name suggests, a metadata pair is stored in two blocks, with one
block providing a backup during erase cycles in case power is lost. These
two blocks are not necessarily sequential and may be anywhere on disk, so a
pointer to a metadata pair, an mptr, is stored as two block addresses:

```
.---+- -+- -+- -+- -.  blocks: 2 leb128s  <=2x5 bytes
| block x 2         |  total:             <=10 bytes
+                   +
|                   |
'---+- -+- -+- -+- -'
```

The order of the blocks in an mptr doesn't matter. Two mptrs refer to the
same metadata pair if they contain the same two blocks.

Each block of a metadata pair is an [rbyd](#rbyds). At any time one of
them, the active block, holds the current metadata. New commits are
appended to the active block until it fills up, at which point the
metadata is compacted into the other block, which then becomes the active
block.

To find the active block, a reader:

1. Reads the revision counts of both blocks. A block whose revision count
   can't be read is treated as older.

2. Fetches the block with the more recent revision count, compared with
   sequence arithmetic, first. If the revision counts are equal, the pair
   holds a [settled copy](#settled-copies), and the reader fetches both
   and picks one as described there.

3. If that block has no valid commit, fetches the other block instead.
   A writer must not append to that older block: the newer block is an
   interrupted compaction, and an interrupted program can read as whole
   at a later fetch. The next commit compacts.

4. If neither block has a valid commit, the metadata pair is corrupt.

The inactive block may contain anything: an older version of the metadata,
an interrupted compaction, or garbage. When a new metadata pair is
allocated, its second block is not even erased. This is why the writer
derives each new revision count from the other block's revision count, see
[Revision counts](#revision-counts).

### Settled copies

A program interrupted by a power loss can leave bits that read 0 on one
read and 1 on the next until the block is erased. The commit being written
can then pass its checksum at one fetch and fail it at the next, and any
commit appended after it is lost with it. To avoid building on such a
commit, the driver settles a metadata pair at mount: it copies the active
block into the other block, commit by commit, checking each commit's
checksum on the bytes it copies, and stops before the first commit that
fails or that reads differently between fetches. The copy:

- keeps the source's revision count, so the global checksum is unchanged,
- copies every tag byte for byte, re-emits each `CKSUM` with the copy's
  phase and the source's perturb bit and with its settled field cleared,
  and recomputes the checksums over the copied bytes,
- ends its last commit with an `ECKSUM` of its own in place of the
  source's, and a `CKSUM` whose settled field
  ([`CKSUM`](#0x3000-lfs3_tag_cksum) bits 3-4) holds a generation from 1
  to 3: the successor of the source's (1 after 0 or 3, 2 after 1, 3 after
  2).

A block's generation is the settled field of the one settled commit in its
log, 0 if it has none. A copy that a power loss interrupted has none,
unless its last commit landed. A reader that finds equal revision counts
fetches both blocks and takes, in order:

1. the only block with a valid commit,
2. the block with a generation, when the other has none,
3. the block whose generation is the successor of the other's,
4. the block with the longer log.

A writer compacts a pair whose last commit is settled before appending to
it, unless compacting needs more than half a block, then it appends as to
any log; the settled commit stays the block's generation. A writer never
appends to a block without a generation in a pair with equal revision
counts: an interrupted settle can leave a copy whose last commit reads as
whole at one fetch and torn at the next, and the next commit must compact
past both.

A metadata pair whose last commit is settled has not been written since it
was settled. After a mount finds a settled copy, the driver sets the
`SETTLED` [wcompat flag](#wcompat-flags) the next time it compacts the
mroot, so drivers that don't apply these rules don't write the filesystem.

### Metadata pair contents

In a metadata pair's rbyd, each rid from 0 up is one entry, a file, a
directory, a bookmark, or a stickynote. Each entry has weight 1, carried by
its [name tag](#directories), the entry's smallest tag. The rbyd's weight
is the number of entries. Other tags at the same rid, such as the entry's
struct tag and custom attributes, have weight 0:

```
rid -1:  gstate deltas (+ config, mroot/mtree pointers, root attrs if mroot)
rid 0:   name tag (w1), [struct tag], [custom attrs] ...
rid 1:   name tag (w1), [struct tag], [custom attrs] ...
...
```

Entries are sorted by (did, name), see [Directories](#directories), and
this order continues across metadata pairs, so the entries of the whole
filesystem form one sorted sequence.

Rid -1 holds tags that belong to the metadata pair itself: the
[global-state deltas](#global-state) in every metadata pair, and in mroots,
the magic string, the config, the mroot/mtree pointer, and the root
directory's custom attributes.

The metadata pair's [`GCKSUMDELTA`](#0x3300-lfs3_tag_gcksumdelta) is not in
the tree at all. It is a checksum tag at the end of the last commit, see
[gcksum](#gcksum).

### Mids

Every entry in the filesystem has a metadata id, or mid, made of the index
of its metadata pair in the [mtree](#the-metadata-tree-mtree) and its rid
in that metadata pair:

```
mid = (mdir index << mbits) | rid
```

`mbits` is a fixed function of the block size, with each metadata pair
reserving room for up to `block_size/8` entries:

```
mbits = nlog2(block_size) - 3
```

where `nlog2(x)` is the smallest `n` with `2^n >= x`. So with 4 KiB blocks,
`mbits` is 9 and each metadata pair spans 512 mids, and with 512-byte
blocks, `mbits` is 6 and each metadata pair spans 64 mids.

Mids are used by [global removes](#grm), and in the mtree, where each
metadata pair occupies a weight of `2^mbits`. If there is no mtree, the
mroot is the only metadata pair, and mids are simply its rids.

## The mroot anchor and the mroot chain

The mroot (metadata root) is the metadata pair that holds the filesystem's
config. littlefs finds it starting from the only fixed location on disk,
the mroot anchor, which is always the metadata pair in blocks 0 and 1:

```
 .--------.  .--------.  .--------.  .--------.  .--------.
.| magic  |->| magic  |->| magic  |->| magic  |->| magic  |
|| mroot  | || mroot  | || mroot  | || mroot  | || config |
||        | ||        | ||        | ||        | || mtree  |
|'--------' |'--------' |'--------' |'--------' |'--------'
'--------'  '--------'  '--------'  '--------'  '--------'
  0x{0,1}
\----------------+----------------/ \----+---/
           mroot chain              active mroot
```

An mroot may have an [`MROOT`](#0x0431-lfs3_tag_mroot) tag at rid -1. If
it has one, it is a link in the mroot chain, and the `MROOT` tag's mptr
points to the next mroot. The last mroot, the one without an
`MROOT` tag, is the active mroot. It holds:

- The [config](#config), which only the active mroot's copy counts for.
- The [`MTREE`](#0x043c-lfs3_tag_mtree) pointer to the mtree, or, if there
  is no `MTREE`, the filesystem's entries inlined at rids 0 and up.
- Any custom attributes attached to the root directory.

Every mroot, including the anchor and the rest of the chain, must contain
the magic string, a [`MAGIC`](#0x0131-lfs3_tag_magic) tag with
`"littlefs"`. An mroot without it is corrupt. Mroots also take part in
[global state](#global-state) like every other metadata pair.

At rid -1 of an mroot, the first tag in the struct range (`0x0400-0x04ff`)
must be `MROOT`, `MTREE`, or absent. The driver treats any other struct
tag there as corruption.

The filesystem starts with only the mroot anchor, which is also the active
mroot. When an mroot needs to be relocated (when it wears out under
`block_recycles`, or hits a bad block), the writer moves it to a new
metadata pair and updates the `MROOT` tag in its parent, which is itself a
commit to the parent. The anchor can never move, so when the anchor itself
would need to be relocated, the chain grows instead: the anchor's contents
move to a new mroot, and the anchor is rewritten with just the magic string
and an `MROOT` tag pointing to it (plus any gstate deltas it is later
given). Each mroot in the chain is written far less often than the one
after it, which, as in v2, prolongs the life of blocks 0 and 1
exponentially.

Readers following the chain must protect themselves against cycles. The
driver uses Brent's algorithm.

Since the anchor's first tag is always its `MAGIC` tag, at rid -1 with the
lowest key, the magic string `"littlefs"` usually ends up at offset 8 of
blocks 0 and 1, right after the 4-byte revision count and the 4-byte tag.
The driver tests for this, but a block that was being written when power
was lost may not have it, so tools should not rely on it.

#### Example: an mroot chain

Here is the anchor of a 512 B x 256 filesystem whose mroot has been
relocated once. Block 0 has revision count `0x10216968`, which is more
recent than block 1's format-time `0x00216968`:

```
00000000: 68 69 21 10 01 31 00 08 6c 69 74 74 6c 65 66 73  hi!..1..littlefs
00000010: 41 31 00 0c 84 31 00 04 b1 01 b0 01 e1 31 00 18  A1...1.......1..
00000020: d2 30 00 0c 02 30 00 01 41 33 00 00 04 60 59 1d  .0...0..A3...`Y.
00000030: ed b2 00 00 05 10 10 4c 2f ef b0 00 00 8f 80 80  .......L/.......
00000040: 00 29 ad 8b 56 ff ff ff ff ff ff ff ff ff ff ff  .)..V...........
```

```
off   bytes                    tag
0000: 68 69 21 10              revision count 0x10216968
0004: 01 31 00 08              v0 MAGIC w0 size 8
0008:   6c 69 74 74 6c 65 66 73  "littlefs"
0010: 41 31 00 0c              v0 alt b le 0x131 w0 jump 12 => 0x0004
0014: 84 31 00 04              v1 MROOT w0 size 4
0018:   b1 01 b0 01              mptr 0x{b1,b0}
001c: e1 31 00 18              v1 alt r le 0x131 w0 jump 24 => 0x0004
0020: d2 30 00 0c              v1 alt b gt 0x230 w0 jump 12 => 0x0014
0024: 02 30 00 01              v0 GRMDELTA w0 size 1
0028:   41                       delta 0x41
0029: 33 00 00 04              v0 GCKSUMDELTA w0 size 4
002d:   60 59 1d ed              0xed1d5960
0031: b2 00 00 05              v1 ECKSUM w0 size 5
0035:   10 10 4c 2f ef           cksize 16, cksum 0xef2f4c10
003a: b0 00 00 8f 80 80 00     v1 CKSUM phase 0 w0 size 15
0041:   29 ad 8b 56 ff ... ff    cksum 0x568bad29, 11 bytes of padding
```

The anchor's `MROOT` tag points to the metadata pair 0x{b1,b0}, whose most
recent block, 0xb1, has revision count `0x0000006d` (`'m'`), no `MROOT`
tag, and so is the active mroot. Its `MTREE` tag is
`84 3c 00 09 | 80 01 af 01 50 93 54 6a 10`: an mtree of weight 128 (two
metadata pairs of 64 mids), rooted at block 0xaf, trunk 0x50, with
checksum `0x106a5493`.

The anchor's `GRMDELTA`, `0x41`, is cancelled out by an identical delta in
another metadata pair, see [Global state](#global-state).

## The metadata tree (mtree)

Once the entries don't fit in the mroot, littlefs moves them into a
[B-tree](#b-trees) of metadata pairs, the mtree. The active mroot points to
the root of the mtree with an [`MTREE`](#0x043c-lfs3_tag_mtree) tag, and
keeps only rid -1 tags of its own.

The mtree is a B-tree whose leaf entries each have weight `2^mbits` and
describe one metadata pair:

- An [`MDIR`](#0x0435-lfs3_tag_mdir) tag with the metadata pair's mptr.
- Except for the first metadata pair, an [`MNAME`](#0x0330-lfs3_tag_mname)
  tag with a name (did and name) that sorts no later than the metadata
  pair's first entry, and after every entry of the metadata pairs before
  it. `MNAME` is the entry's smallest tag, so it carries the weight.

```
                          .----------------.
            mtree root -> | branch (w192)  |
                          | bname, branch  |
                          | bname, branch  |
                          '--|----------|--'
                 .-----------'          '-----------.
                 v                                  v
        .----------------.                 .----------------.
        | mdir (w64)     |                 | mname, mdir    |
        | mname, mdir    |                 | mname, mdir    | ...
        | mname, mdir    |                 | mname, mdir    |
        '-|-------|------'                 '----------------'
          v       v
       .------. .------.
       | mdir | | mdir |
       '------' '------'
```

Mids map onto the mtree directly: the metadata pair holding mid `m` is the
mtree entry that contains bid `m` (each entry being keyed by its last bid,
`(index << mbits) + 2^mbits - 1`), and the entry's rid in that metadata
pair is `m & (2^mbits - 1)`. The mtree's weight is the number of metadata
pairs times `2^mbits`.

Names in the mtree are used to find the metadata pair that may contain a
given name with a binary search. They are only lower bounds: since they are
copied when metadata pairs split and not updated when entries are removed,
an `MNAME` may name an entry that no longer exists.

If the active mroot has no `MTREE` tag, there is no mtree, and the mroot
itself is the only metadata pair, with mids equal to its rids.

#### Example: an mtree

Here is part of a two-level mtree in a 512 B x 256 filesystem with 15
metadata pairs. The active mroot (the anchor) has the `MTREE` tag:

```
0146: 84 3c 00 0a              v1 MTREE rid -1 w0 size 10
014a:   c0 07                    weight 960 (15 x 64)
014c:   97 01                    block 0x97
014e:   fc 02                    trunk 0x17c
0150:   f1 10 17 1e              cksum 0x1e1710f1
```

The root node, block 0x97, holds four branches. The first has no name. The
second is named after its subtree's first entry, `"f08"` in the root
directory (did 0):

```
0004: 84 00 c0 01 06           v1 BRANCH rid 191 w192 size 6
0009:   78                       block 0x78
000a:   5b                       trunk 0x5b
000b:   4b 54 4d eb              cksum 0xeb4d544b
000f: 83 00 c0 01 04           v1 BNAME rid 383 w192 size 4
0014:   00 66 30 38              did 0, "f08"
0018: 04 00 00 07              v0 BRANCH rid 383 w0 size 7
001c:   87 01                    block 0x87
001e:   67                       trunk 0x67
001f:   7e d5 9f fe              cksum 0xfe9fd57e
```

The leaf at block 0x78, trunk 0x5b, holds the first three metadata pairs,
mids 0-63, 64-127, and 128-191:

```
0004: 84 35 40 02              v1 MDIR rid 63 w64 size 2
0008:   69 68                    mptr 0x{69,68}
000a: 83 30 40 04              v1 MNAME rid 127 w64 size 4
000e:   00 66 30 32              did 0, "f02"
0012: 04 35 00 02              v0 MDIR rid 127 w0 size 2
0016:   6d 6c                    mptr 0x{6d,6c}
```

So the file `"f02"` is mid 64, rid 0 of the metadata pair 0x{6d,6c}.

## B-trees

littlefs uses B-trees for files, for the mtree, and for the on-disk block
map. All three share the same structure: a B-tree is a tree of rbyds, each
node being a single block, and each node's rbyd holding either the node's
children or the tree's entries.

Unlike metadata pairs, B-tree nodes are not paired. They are updated
copy-on-write, by writing a new node to a new block, or by appending a new
commit to the node's existing block if it still has erased space. Since
appending never changes existing trunks, the old trunk, and so any parent
still pointing to it, stays valid.

A pointer to a B-tree node, a branch, is the node's block, the address of
the trunk to use, and the canonical checksum of the node as of the commit
containing that trunk:

```
.---+- -+- -+- -+- -.  block: 1 leb128  <=5 bytes
| block             |  trunk: 1 leb128  <=4 bytes
+---+- -+- -+- -+- -'  cksum: 1 le32    4 bytes
| trunk         |      total:           <=13 bytes
+---+- -+- -+- -+
|     cksum     |
'---+---+---+---'
```

The node's weight is not part of the branch, it comes from whatever points
to the node: the weight of the parent's rid for inner nodes, and the
weight stored with the root pointer for roots. The root pointers are:

- [`BTREE`](#0x042c-lfs3_tag_btree) for file B-trees and
  [`MTREE`](#0x043c-lfs3_tag_mtree) for the mtree, both a leb128 weight
  followed by a branch.
- [`BSHRUB`](#0x0428-lfs3_tag_bshrub) for file B-trees whose root is a
  [shrub](#shrubs) in the file's metadata pair.
- The gbmap's [gstate](#gbmap-gstate), a branch whose weight is implied to
  be the filesystem's block count.

Since every branch contains its child's checksum, and every metadata pair
is part of the [global checksum](#gcksum), the whole filesystem forms a
Merkle tree.

Each rid in an inner node is one child, with:

- A [`BRANCH`](#0x0400-lfs3_tag_branch) tag pointing to the child. The rid's
  weight is the total weight of the child.
- In named trees (the mtree), optionally a
  [`BNAME`](#0x0300-lfs3_tag_bname) tag with a copy of the first name in
  the child's subtree, at the time the child was split off. When present,
  `BNAME` is the rid's smallest tag, so it carries the weight. The first
  child of a node usually has no name.

Each rid in a leaf node is one entry of the tree, whose tags depend on the
tree. A leaf is told apart from a branch by its smallest tag: a rid whose
smallest tag is `BRANCH` or `BNAME` is a child pointer, anything else is an
entry.

To look up the entry containing bid `b` in a B-tree with root weight `W`
(`0 <= b < W`):

```
node = root, base = 0
loop:
    rid, tag, weight, data = lookupnext(node, b - base, 0)
    if tag == BNAME:
        data = lookup(node, rid, BRANCH)   // keep BNAME's weight
        tag = BRANCH
    if tag == BRANCH:
        base = base + rid - (weight-1)
        node = the rbyd at data's block/trunk, with weight `weight`
    else:
        return the entry at bid base+rid, its weight, its tags
```

Entries are keyed by their last bid, so an entry with bid `b` and weight `w`
covers bids `b-w+1` to `b`.

To check a node, a reader [fetches](#fetching-an-rbyd) its block up to the
commit containing the branch's trunk, and compares the trunk, the weight,
and the canonical checksum with the branch. The driver only does this
eagerly with `LFS3_M_CKFETCHES`, during mount for the mtree's inner nodes,
and when checking metadata.

As with the mtree's names, `BNAME`s are lower bounds that may be out of
date. Readers can use them to binary-search a named B-tree, but an entry's
real name is in the entry.

An empty B-tree is represented by the absence of a root pointer. Writers
never write a `BTREE` or `BSHRUB` with weight 0, but readers must accept
one as an empty tree.

#### Example: a file B-tree

Here is a 51300-byte file in a 512 B x 256 filesystem, stored in a
3-level B-tree. Its metadata pair has:

```
00b6: 83 01 01 04              v1 REG rid 0 w1 size 4
00ba:   1e 62 69 67              did 0x1e, "big"
00cc: 84 2c 00 0a              v1 BTREE rid 0 w0 size 10
00d0:   e4 90 03                 weight 51300
00d3:   66                       block 0x66
00d4:   8b 03                    trunk 0x18b
00d6:   39 ef 0b 31              cksum 0x310bef39
```

The root node, block 0x66, has five children. The first one covers bytes
0 to 10239:

```
0004: 84 00 80 50 07           v1 BRANCH rid 10239 w10240 size 7
0009:   ea 01                    block 0xea
000b:   54                       trunk 0x54
000c:   a9 42 51 66              cksum 0x665142a9
```

and block 0xea, fetched up to trunk 0x54, has a weight of 10240 and a
canonical checksum of `0x665142a9`, as expected.

## Files

A regular file is an entry in a metadata pair with a
[`REG`](#0x0301-lfs3_tag_reg) name tag and, unless the file is empty, a
struct tag describing its data:

| struct    | file data                                                    |
|-----------|--------------------------------------------------------------|
| none      | the file is empty                                            |
| `BSHRUB`  | a B-tree whose root is a shrub in the metadata pair          |
| `BTREE`   | a B-tree whose root is a separate block                      |

With either struct, the file's data is a B-tree over the file's bytes,
where each byte of the file is one bid. The tree's weight is the file's
size, so the size of a file is simply the first leb128 in its struct
tag.

A `BSHRUB` stores the weight and the trunk of a [shrub](#shrubs) in the
same block as the metadata pair's active block:

```
.---+- -+- -+- -+- -.  weight: 1 leb128  <=5 bytes
| weight            |  trunk:  1 leb128  <=4 bytes
+---+- -+- -+- -+- -'  total:            <=9 bytes
| trunk         |
'---+- -+- -+- -'
```

The shrub is the B-tree's root node, holding the file's entries directly.
This is how small files are inlined in their metadata pair. Readers can
treat a shrub like any other B-tree root, branches included, but the
driver converts a bshrub into a `BTREE` before its root would need to
split, so in practice shrub roots are leaves. A `BSHRUB` has no checksum of
its own: the shrub is part of the metadata pair's rbyd and covered by its
checksums.

A `BTREE` stores the weight and a [branch](#b-trees) to the root node.

Each entry in a file's B-tree covers a range of bytes, with the entry's
weight being the number of bytes, and is one of two leaf tags (with the
shrub bits set when the entry lives in a shrub):

1. A [`DATA`](#0x0404-lfs3_tag_data) tag, a fragment, with the bytes
   inline in the tag. A fragment's size may be smaller than its weight. The
   bytes past the end of the data, up to the weight, read as zeros. A
   fragment with size 0 is a hole, so sparse files cost nothing for the
   holes.

2. A [`BLOCK`](#0x0408-lfs3_tag_block) tag, a block pointer (bptr), pointing
   to data in a data block:

   ```
   .---+- -+- -+- -.      size:   1 leb128  <=4 bytes
   | size          |      block:  1 leb128  <=5 bytes
   +---+- -+- -+- -+- -.  off:    1 leb128  <=4 bytes
   | block             |  cksize: 1 leb128  <=4 bytes
   +---+- -+- -+- -+- -'  cksum:  1 le32    4 bytes
   | off           |      total:            <=21 bytes
   +---+- -+- -+- -+
   | cksize        |
   +---+- -+- -+- -+
   |     cksum     |
   '---+---+---+---'
   ```

   The entry's bytes are the `min(size, weight)` bytes at offset `off` in
   `block`, followed by zeros up to the weight. `cksum` is the CRC-32C of
   the first `cksize` bytes of the block, `crc32c(0, block[0:cksize])`,
   which is not necessarily the same range as the data.

Data blocks contain nothing but file data. They have no revision count,
tags, or other metadata, so littlefs can't tell a data block's contents
apart from garbage without the bptr's checksum.

A few things follow from this encoding:

- The checksum covers a prefix of the block, not the referenced range.
  This lets a writer append more data into the erased end of a partially
  written block later and write a new bptr with a larger `cksize`, without
  invalidating older bptrs into the same block.

- Several bptrs can point into the same block. When a write lands in the
  middle of a bptr's range, the driver may "carve" it into a left and a
  right bptr into the same block, with adjusted `off` and `size`, or copy
  the remaining bytes into fragments.

- How data is split between fragments and blocks is up to the writer. The
  driver keeps small writes as fragments of at most `fragment_size` bytes,
  and "crystallizes" fragments into blocks once enough of them, at least
  `crystal_thresh` bytes, accumulate. Readers don't need to care.

The driver does not check `off + size <= cksize <= block_size` when
reading a bptr. Readers should treat a bptr that violates this as corrupt.

The rcompat flags reserve two more representations, presumably a single
fragment (`BMOSS`) or a single bptr (`BSPROUT`) stored directly as the
file's struct tag. Neither is implemented, the driver never sets these
flags, and it doesn't handle a `DATA` or `BLOCK` struct tag in a file entry
(it would read such a file as empty).

#### Example: an inlined file

Here is the 13-byte file `"hello"` in the root directory (did 0), stored as
a shrub in the mroot of a 4 KiB x 32 filesystem. The commit that created it
contains:

```
000000d0: 56 43 04 01 3d 03 01 01 06 00 68 65 6c 6c 6f 94  VC..=.....hello.
000000e0: 04 0d 0d 48 65 6c 6c 6f 20 57 6f 72 6c 64 21 0a  ...Hello World!.
000000f0: c1 36 00 30 c1 39 00 6c e1 3a 00 81 01 e3 04 01  .6.0.9.l.:......
00000100: 69 c3 01 01 2c 84 28 00 03 0d df 01 b3 00 00 04  i...,.(.........
```

```
off   bytes                    tag
00d5: 03 01 01 06              v0 REG rid 2 w1 size 6
00d9:   00 68 65 6c 6c 6f        did 0, "hello"
00df: 94 04 0d 0d              v1 shrub DATA (0x1404) w13 size 13
00e3:   48 65 6c 6c 6f 20 57     "Hello World!\n"
        6f 72 6c 64 21 0a
00f0: c1 36 00 30              v1 alt b le 0x136 w0 jump 48 => 0x00c0
 ... three more alts ...
0101: c3 01 01 2c              v1 alt b le 0x301 w1 jump 44 => 0x00d5
0105: 84 28 00 03              v1 BSHRUB rid 2 w0 size 3
0109:   0d                       weight 13
010a:   df 01                    trunk 0x0df
```

The shrub's trunk, at `0x00df`, is a single shrub `DATA` leaf, so the
shrub has one fragment covering bytes 0 to 12.

#### Example: fragments in a B-tree

Here is a 600-byte file written in three synced 200-byte writes, in the
same filesystem. It outgrew its shrub, so its struct is a `BTREE`:

```
04c9: 04 2c 00 09              v0 BTREE rid 5 w0 size 9
04cd:   d8 04                    weight 600
04cf:   15                       block 0x15
04d0:   c0 03                    trunk 0x1c0
04d2:   c5 fb 5a a3              cksum 0xa35afbc5
```

Its only node, block 0x15, holds three fragments, two from the commit
that created the B-tree, and one appended later in a second commit:

```
0000: 62 00 00 00              revision count 0x00000062 ('b')
0004: 84 04 c8 01 c8 01        v1 DATA w200 size 200, "aaa..."
00d2: c4 04 c8 01 ce 01        v1 alt b le 0x404 w200 jump 206 => 0x0004
00d8: 04 04 c8 01 c8 01        v0 DATA w200 size 200, "bbb..."
01a6: 32 00 00 05              v0 ECKSUM w0 size 5
01af: 30 01 00 8a 80 80 00     v0 CKSUM phase 1 w0 size 10
01c0: 64 04 c8 01 bc 03        v0 alt r le 0x404 w200 jump 444 => 0x0004
01c6: c4 04 c8 01 ee 01        v1 alt b le 0x404 w200 jump 238 => 0x00d8
01cc: 84 04 c8 01 c8 01        v1 DATA w200 size 200, "ccc..."
029a: b2 00 00 05              v1 ECKSUM w0 size 5
02a3: b0 05 00 96 80 80 00     v1 CKSUM phase 1, perturb, w0 size 22
```

The `BTREE`'s trunk, `0x01c0`, is in the second commit, whose canonical
checksum is `0xa35afbc5`.

#### Example: block pointers

Here is a 32868-byte file in the same filesystem, eight full blocks and a
100-byte fragment, with its root in block 0x1b. The first entry covers
bytes 0 to 4095:

```
0004: 84 08 80 20 0a           v1 BLOCK w4096 size 10
0009:   80 20                    size 4096
000b:   16                       block 0x16
000c:   00                       off 0
000d:   80 20                    cksize 4096
000f:   32 fe 71 9c              cksum 0x9c71fe32
```

and the CRC-32C of block 0x16 is indeed `0x9c71fe32`. The last entry
covers bytes 32768 to 32867 with a fragment:

```
018c: 84 04 64 64              v1 DATA w100 size 100
0190:   00 01 02 03 ...
```

#### Example: a hole

Here is a sparse file, created by seeking to offset 100000 and writing
`"end"`, so it has 100003 bytes, all but three of them a hole:

```
083e: 04 28 00 05              v0 BSHRUB w0 size 5
0842:   a3 8d 06                 weight 100003
0845:   a6 0f                    trunk 0x7a6

07a0: 14 04 a0 8d 06 00        v0 shrub DATA w100000 size 0
07a6: c4 04 a0 8d 06 06        v1 alt b le 0x404 w100000 jump 6 => 0x07a0
07ac: 14 04 03 03              v0 shrub DATA w3 size 3
07b0:   65 6e 64                 "end"
```

The shrub's trunk, at `0x07a6`, is an alt pointing back to the first
fragment, followed by the second fragment. The first fragment has size 0
and weight 100000, so bytes 0 to 99999 read as zeros.

## Directories

littlefs v3 has no per-directory data structures. All entries of all
directories live in the one sorted sequence of entries formed by the
mtree's metadata pairs, and directories are just ranges in that sequence.

Each directory has a directory id, or did, a 31-bit number unique in the
filesystem. The root directory's did is always 0. Other dids are arbitrary,
the driver derives them from a hash of the directory's name to keep them
small and avoid collisions, but readers must not assume any structure.

Every entry starts with a name tag, whose data is the did of the directory
containing the entry followed by the entry's name:

```
.---+- -+- -+- -+- -.  did:  1 leb128  <=5 bytes
| did               |  name: variable length
+---+- -+- -+- -+- -'
| name              |
'---+- -+- -+- -+- -'
```

The name is a sequence of bytes, not null-terminated, whose length is
whatever remains of the tag's data. Names can't contain `'/'` or `'\0'`,
and are at most [`NAMELIMIT`](#0x0139-lfs3_tag_namelimit) bytes long.

Entries are sorted by (did, name): first by did, as a number, then by name,
byte by byte as unsigned bytes, with a name that is a prefix of another
sorting first. So all entries of a directory are contiguous, sorted by
name, and directories are sorted by did.

The name tag's subtype gives the entry's type:

| tag      | type         | struct      | meaning                            |
|----------|--------------|-------------|------------------------------------|
| `0x0301` | `REG`        | file data   | a regular file, see [Files](#files)|
| `0x0302` | `DIR`        | `DID`       | a directory                        |
| `0x0303` | `STICKYNOTE` | none        | a file created but not yet synced  |
| `0x0304` | `BOOKMARK`   | none        | the start of a directory           |

A directory takes two entries:

1. A [`DIR`](#0x0302-lfs3_tag_dir) entry in its parent, named with the
   parent's did and the directory's name, with a
   [`DID`](#0x0420-lfs3_tag_did) struct tag holding the directory's own did.

2. A [`BOOKMARK`](#0x0304-lfs3_tag_bookmark) entry, named with the
   directory's own did and an empty name. Since the empty name sorts first,
   the bookmark is the first entry with the directory's did, and marks where
   the directory's entries start.

To list a directory, a reader finds the directory's bookmark, then reads
entries until it finds one with a different did. The root directory has no
`DIR` entry, only a bookmark with did 0, which is always the very first
entry, mid 0.

Bookmarks make empty directories representable, and give writers a stable
place to insert a directory's first entry.

Stickynotes are files that have been created but not synced yet, which
keeps them from showing up as empty files if power is lost (a long-standing
v2 issue). The driver treats a stickynote as existing only while a file
handle has it open. Any other stickynote is an orphan: readers must ignore
it, and the driver removes orphaned stickynotes before its next write. When
a stickynote is synced, its name tag becomes a `REG`.

The driver maps name tags it doesn't know, any type other than these four
in `0x0301-0x0304`, to an unknown type (`LFS3_TYPE_UNKNOWN`), which it
reports but otherwise leaves alone. A comment in the driver suggests that
future entry types should come with a wcompat flag.

#### Example: a directory

Here are the entries of a filesystem with a file `"hello"` in the root
directory, and files `"big"` and `"frag"` in a directory `"dir"`, all in
the mroot:

```
rid  off   bytes                         tag
 0:  0094: 83 04 01 01 | 00              BOOKMARK w1, did 0, "" (root)
 1:  023c: 03 02 01 04 | 00 64 69 72     DIR w1, did 0, "dir"
     0260: 04 20 00 02 | 9e 01           DID w0, did 0x9e
 2:  00d5: 03 01 01 06 | 00 68 65 6c 6c 6f
                                         REG w1, did 0, "hello"
 3:  01cd: 83 04 01 02 | 9e 01           BOOKMARK w1, did 0x9e, "" ("dir")
 4:  0680: 03 01 01 05 | 9e 01 62 69 67  REG w1, did 0x9e, "big"
 5:  0339: 83 01 01 06 | 9e 01 66 72 61 67
                                         REG w1, did 0x9e, "frag"
```

The offsets show the order the tags were written in, the rids show the
sorted order. `"hello"` was created first, then `"dir"`, whose `DIR` entry
sorts before `"hello"`, while its bookmark, with did 0x9e, sorts after
everything in the root directory.

## Custom attributes

littlefs has a concept of "custom attributes". These are small attributes
attached to files and directories that can be used to store things like
timestamps, hashes, permissions, etc.

Each custom attribute is identified by an 8-bit type and stored in its own
tag at the rid of the entry it belongs to. Attributes of the root directory,
which has no entry, are stored at rid -1 of the active mroot. The tag
depends on the type:

```
tag = 0x0600 | ((type & 0x80) << 1) | (type & 0x7f)
```

So types `0x00-0x7f` are stored as [`UATTR`](#0x06xx-lfs3_tag_uattr) tags
`0x0600-0x067f`, and types `0x80-0xff` as
[`SATTR`](#0x07xx-lfs3_tag_sattr) tags `0x0700-0x077f`. The type's top bit
moves to bit 8 because bit 7 of every tag is reserved.

The data is the attribute's value, any number of bytes. Note the driver
does not enforce a limit, but an attribute must fit in a metadata
commit (see [Open points](#open-points)).

The attribute types are divided into ranges:

- `0x00-0x7f` - Free for user attributes (uattrs).
- `0x80-0xff` - Reserved for standard attributes (sattrs).

The v3-alpha announcement further splits the reserved range into
`0x80-0xbf` for standard attributes and `0xc0-0xff` for system attributes,
while the header only says `0x80-0xff` "may be assigned a standard
attribute". No standard attributes are defined yet, and a portable driver
should work with any attributes missing.

For example, a uattr of type `0x61` with value `"attr"` on the file at rid
2, and a uattr of type `0x7f` with value `"root"` on the root directory:

```
0145: 86 61 00 04 | 61 74 74 72      v1 UATTR 0x61 rid 2 w0 size 4, "attr"
0188: 86 7f 00 04 | 72 6f 6f 74      v1 UATTR 0x7f rid -1 w0 size 4, "root"
```

## Global state

littlefs has a concept of "global state". This is a small set of state that
can be updated by a commit to _any_ metadata pair in the filesystem.

The way this works is that the global state is stored as a set of deltas
distributed across the filesystem such that the global state can be found
by the xor-sum of these deltas:

```
 .--------.  .--------.  .--------.  .--------.  .--------.
.| mroot  |->| mroot  |  | mdir   |  | mdir   |  | mdir   |
|| anchor | || gdelta |  | gdelta |  |        |  | gdelta |
||        | || 0x23   |  | 0xff   |  |        |  | 0xce   |
|'--------' |'----|---'  '----|---'  '--------'  '----|---'
'--------'  '-----|--'   '----|---'  '--------'  '-----|--'
                  v           v                        v
     0x00 ----> xor ------> xor ---------------------> xor --> gstate = 0x12
```

Every metadata pair takes part: the mroot anchor, the rest of the mroot
chain, and every metadata pair in the mtree. B-tree nodes don't. The deltas
of a metadata pair are the gdelta tags at rid -1 of its active block, plus
the `GCKSUMDELTA` of its last commit.

Each piece of global state has a fixed size, and a delta tag may be shorter
than that. Deltas are xored byte by byte, as if padded with zeros, and a
delta longer than the global state is truncated. A missing delta is zero, so
a filesystem with no deltas has all-zero global state.

When committing to a metadata pair, a writer folds any change to the global
state into that metadata pair's delta, `delta' = delta ^ old ^ new`, and
removes the delta tag if it becomes all zeros. Since each commit is atomic,
so are changes to the global state.

There are currently three pieces of global state.

### gcksum

The global checksum (gcksum) is the xor of the canonical checksums of every
metadata pair in the filesystem (their active blocks). Since metadata pairs
contain branches with the checksums of every B-tree node, and bptrs with the
checksums of every data block, the gcksum is a checksum of the whole
filesystem.

The gcksum's deltas are the [`GCKSUMDELTA`](#0x3300-lfs3_tag_gcksumdelta)
tags, which are not stored in the tree like other gdeltas, but as checksum
tags at the end of each commit, where they don't affect the canonical
checksum. A metadata pair's delta is the `GCKSUMDELTA` in its last valid
commit, or zero if that commit doesn't have one. The xor of all deltas
must equal the cube of the gcksum in the CRC-32C ring:

```
crc32c_cube(xor of every canonical checksum) == xor of every GCKSUMDELTA
```

Storing the cube, and not the gcksum itself, is important. With a linear
function, each metadata pair's delta would only depend on that metadata
pair's own checksum, and a stale metadata pair (rolled back by a lost
write, say) would carry a matching delta with it. Cubing makes each delta
depend on the rest of the filesystem.

The driver checks this on mount and fails to mount if it doesn't match.
`lfs3_fs_cksum` returns the gcksum, which can be stored outside the
filesystem to detect rollback of the last commit.

Writers must keep the gcksum up to date when the
[`GCKSUM`](#wcompat-flags) wcompat flag is set, which the driver always
sets.

For example, a freshly formatted filesystem has only one metadata pair, the
mroot anchor, so its gcksum is the anchor's canonical checksum,
`0x9c558f15`, and the anchor's `GCKSUMDELTA` is its cube, `0x1228c36f`.

### grm

The global remove (grm) state is used to remove entries atomically with
changes in other metadata pairs, such as when creating, removing, or
renaming a directory. It holds up to two mids whose entries are pending
removal:

```
.- -+- -+- -+- -+- -.  mids:  2 leb128s  <=2x5 bytes
' mids              '  total:            <=10 bytes
+                   +
'                   '
'- -+- -+- -+- -+- -'
```

The grm is 10 bytes. The mids are leb128s, and the list ends at the second
mid, at a mid of 0, or at the end of the data. Mid 0 is always the root's
bookmark, which is never removed, so 0 can mark an empty slot. Writers trim
trailing zeros.

If a mid is in the grm, its entry has logically been removed: readers must
treat it as if it doesn't exist. Writers must finish the removal, removing
the entry and clearing the grm, before making any other changes to the
filesystem.

The driver uses the grm to:

- Create directories: it first commits the bookmark along with a grm for
  the bookmark itself, then commits the `DIR` entry while clearing the grm.
  If power is lost in between, the orphaned bookmark is removed.
- Remove directories: it removes the `DIR` entry while adding a grm for the
  bookmark.
- Rename entries: it creates the new entry while adding a grm for the old
  one, and, when the rename replaces an empty directory, a second grm for
  that directory's bookmark. This is why the grm holds two mids.

For example, the first commit of `mkdir("dir")` in the filesystem above
added the bookmark at rid 2 along with a grm delta of `02`, mid 2. The next
commit added the `DIR` entry at rid 1, moving the bookmark to rid 3, and
removed the grm.

In the [mroot chain example](#example-an-mroot-chain), the anchor has a grm
delta of `0x41` (mid 65), and another metadata pair has the same delta,
`82 30 00 01 | 41`, so they cancel out and the grm is empty.

The grm uses the [`GRM`](#rcompat-flags) rcompat flag, since a reader that
doesn't understand it would show removed entries.

### gbmap gstate

The [gbmap](#the-global-block-map-gbmap)'s root and allocation window are
stored in the global state, with [`GBMAPDELTA`](#0x0234-lfs3_tag_gbmapdelta)
deltas:

```
.---+- -+- -+- -+- -.  window: 1 leb128  <=5 bytes
| window            |  known:  1 leb128  <=5 bytes
+---+- -+- -+- -+- -+  block:  1 leb128  <=5 bytes
| known             |  trunk:  1 leb128  <=4 bytes
+---+- -+- -+- -+- -+  cksum:  1 le32    4 bytes
| block             |  total:            23 bytes
+---+- -+- -+- -+- -'
| trunk         |
+---+- -+- -+- -+
|     cksum     |
'---+---+---+---'
```

The gbmap state is 23 bytes, and writers trim trailing zeros. `block`,
`trunk` and `cksum` are a [branch](#b-trees) to the gbmap's root, whose
weight is the filesystem's block count.

The gbmap state is only meaningful if the [`GBMAP`](#wcompat-flags) wcompat
flag is set. The driver collects the deltas regardless, so the state
survives if the gbmap is disabled and re-enabled.

## The global block map (gbmap)

The gbmap is an optional on-disk map of the state of every block, used to
speed up block allocation, track pre-erased blocks, and, in the future,
bad blocks. Without it, the driver finds free blocks by traversing the
filesystem into a RAM bitmap, the lookahead buffer, as in v2.

The gbmap is a [B-tree](#b-trees) whose bids are block addresses. Each
entry is a range of blocks in the same state, with the entry's weight being
the length of the range, and the entry's tag the state:

| tag      | name       | data             | state                          |
|----------|------------|------------------|--------------------------------|
| `0x0440` | `BMFREE`   | none             | free, must be erased before use |
| `0x0441` | `BMINUSE`  | none             | in use, or assumed in use      |
| `0x0442` | `BMERASED` | optional ecksum  | free and already erased        |
| `0x0443` | `BMBAD`    | none             | bad, reserved                  |

The two low bits of these tags are the state: bit 0 means in use, bit 1
means erased, and in use + erased means bad. `BMBAD` is reserved for
bad-block tracking and never written by the driver, which treats it like
`BMINUSE`.

A `BMERASED` range may carry an erased-state checksum, the same encoding as
an [`ECKSUM`](#0x3200-lfs3_tag_ecksum), the CRC-32C of the first `cksize`
bytes of each block taken right after erasing it. An empty `BMERASED`
payload means the erased state is unknown. A writer may program a
`BMERASED` block without erasing it only if the block's first `cksize`
bytes still match the checksum, and the writer perturbs revision counts
(`LFS3_M_REVPERTURB`, see [Revision counts](#revision-counts)), which
guarantees that writing to the block invalidates the checksum.

The driver only allocates `BMERASED` blocks when built with pre-erase
support (`LFS3_PREERASE`) and mounted with `LFS3_M_REVPERTURB`. It then
uses a block whose checksum still matches without erasing it, skips a block
whose checksum no longer matches, and erases a block without a checksum.
Otherwise it treats `BMERASED` like `BMINUSE` until the gbmap is
repopulated.

Not every entry in the gbmap is current. The gbmap's gstate includes a
window: only blocks from `window` to `window+known-1`, modulo the block
count, have trusted states. The allocator hands out blocks starting at
`window`, advancing `window` and decreasing `known` as it goes, and records
the new window in the gstate with its next commit, without having to
update the gbmap itself. Blocks outside the window may have been allocated
since the gbmap was written and must not be allocated from it.

Blocks that become free are not marked free right away. The driver
repopulates the gbmap from time to time by traversing the filesystem, at
which point the window covers the whole disk again.

When a filesystem is formatted with a gbmap, block 2 holds the initial
gbmap root, with blocks 0-2 in use and everything else free, `window` is 3,
and `known` is the block count.

The gbmap only matters to writers. A reader can ignore it, but a writer
that doesn't understand it would allocate blocks without updating it,
which is why it is a [wcompat](#wcompat-flags) feature.

#### Example: a gbmap

Here is the gbmap of a 4 KiB x 32 filesystem with 13 blocks in use and
four pre-erased blocks. The mroot's `GBMAPDELTA`:

```
07c3: 02 34 00 09              v0 GBMAPDELTA w0 size 9
07c7:   0d                       window 13
07c8:   20                       known 32
07c9:   02                       block 0x2
07ca:   e7 0e                    trunk 0x767
07cc:   1e 92 c9 91              cksum 0x91c9921e
```

and the gbmap's root, block 2 at trunk 0x767, holds three ranges:

```
05ba: 04 41 0d 00              BMINUSE  blocks 0-12   (rid 12, w13)
075e: 04 42 04 05              BMERASED blocks 13-16  (rid 16, w4)
0762:   10 10 4c 2f ef           cksize 16, cksum 0xef2f4c10
0770: 04 40 0f 00              BMFREE   blocks 17-31  (rid 31, w15)
```

Blocks 13 to 16 were erased to `0xff`s, and `0xef2f4c10` is the CRC-32C of
16 bytes of `0xff`.

## Config

The filesystem's format-time configuration is stored in config tags (the
`0x01xx` suptype) at rid -1 of the active mroot:

| tag      | name        | data                                    | default      |
|----------|-------------|-----------------------------------------|--------------|
| `0x0131` | `MAGIC`     | `"littlefs"`                            | required     |
| `0x0134` | `VERSION`   | u8 major, u8 minor                      | v0.0         |
| `0x0135` | `RCOMPAT`   | rcompat flags, le, any length           | 0            |
| `0x0136` | `WCOMPAT`   | wcompat flags, le, any length           | 0            |
| `0x0137` | `OCOMPAT`   | ocompat flags, le, any length           | 0            |
| `0x0138` | `GEOMETRY`  | lleb128 block_size-1, leb128 block_count-1 | required  |
| `0x0139` | `NAMELIMIT` | leb128 name limit                       | 255          |
| `0x013a` | `FILELIMIT` | leb128 file limit                       | `0x7fffffff` |

Only the active mroot's config counts. The other mroots in the chain only
need `MAGIC`. The driver writes every config tag except `OCOMPAT` at format
time, and treats a missing tag as its default.

The tags are described in detail under [Tag reference](#tag-reference).

### Compat flags

As well as a version, littlefs v3 has three sets of compat flags, which
allow features to be added and removed independently of each other:

- **rcompat flags** - Must be understood to read the filesystem (ext4's
  `incompat` flags).
- **wcompat flags** - Must be understood to write to the filesystem (ext4's
  `ro_compat` flags).
- **ocompat flags** - No understanding necessary (ext4's `compat` flags).

Note "understanding" a flag does not necessarily mean supporting the
feature. A driver can understand a flag well enough to know it can't read a
filesystem that uses it.

Each set of flags is stored as a little-endian bitfield of any length. A
reader treats missing bytes as zeros. The driver reads the first 32 bits,
and if any later byte is non-zero, sets an internal overflow bit
(`0x80000000`), which makes the flags mismatch like any other unknown flag.
The driver writes rcompat and wcompat flags as 4-byte le32s, and never
writes ocompat flags.

#### rcompat flags

| flag         | name          | meaning                              | v0.0      |
|--------------|---------------|--------------------------------------|-----------|
| `0x00000001` | `NONSTANDARD` | Non-standard filesystem format       |           |
| `0x00000004` | `WRONLY`      | Reading is disallowed                |           |
| `0x00000010` | `MMOSS`       | May use an inlined mdir              | always    |
| `0x00000020` | `MSPROUT`     | May use an mdir pointer              | reserved  |
| `0x00000040` | `MSHRUB`      | May use an inlined mtree             | reserved  |
| `0x00000080` | `MTREE`       | May use an mtree                     | always    |
| `0x00000100` | `BMOSS`       | Files may use inlined data           | reserved  |
| `0x00000200` | `BSPROUT`     | Files may use block pointers         | reserved  |
| `0x00000400` | `BSHRUB`      | Files may use inlined btrees         | always    |
| `0x00000800` | `BTREE`       | Files may use btrees                 | always    |
| `0x00010000` | `GRM`         | Global-remove in use                 | always    |

The "moss/sprout/shrub/tree" flags describe the representations that the
mtree and files may use, from smallest to largest. The reserved ones are
only defined by their names so far, so their descriptions here are
inferred:

- moss: inlined in the parent. For metadata, the entries live in the mroot
  (no mtree). For files, presumably a single fragment as the struct tag.
- sprout: a single pointer. For metadata, presumably an `MDIR` tag in the
  mroot pointing to the only metadata pair. For files, presumably a single
  `BLOCK` struct tag.
- shrub: a B-tree whose root is a shrub in the parent. For metadata,
  presumably an mtree whose root is a shrub in the mroot.
- tree: a B-tree whose root is in its own block.

In v0.0, the driver only implements `MMOSS`, `MTREE`, `BSHRUB`, and
`BTREE`, and always sets these flags along with `GRM`, so its rcompat flags
are always `0x00010c90`. `MSPROUT`, `MSHRUB`, `BMOSS`, and `BSPROUT` are
reserved and not implemented.

#### wcompat flags

| flag         | name          | meaning                              | v0.0      |
|--------------|---------------|--------------------------------------|-----------|
| `0x00000001` | `NONSTANDARD` | Non-standard filesystem format       |           |
| `0x00000002` | `RDONLY`      | Writing is disallowed                |           |
| `0x00040000` | `GCKSUM`      | Global-checksum in use               | always    |
| `0x00080000` | `GBMAP`       | Global on-disk block-map in use      | optional  |
| `0x00100000` | `SETTLED`     | Pairs may hold settled copies        | optional  |
| `0x01000000` | `DIR`         | Directory files in use               | always    |

So a freshly formatted filesystem's wcompat flags are `0x01040000`, or
`0x010c0000` with a gbmap. The driver adds `SETTLED` when it compacts the
mroot after finding a [settled copy](#settled-copies), and never removes
it.

#### ocompat flags

| flag         | name          | meaning                              |
|--------------|---------------|--------------------------------------|
| `0x00000001` | `NONSTANDARD` | Non-standard filesystem format       |

`NONSTANDARD`, `WRONLY`, and `RDONLY` are not used by the driver. They let
other tools mark a filesystem as non-standard, write-only, or read-only on
disk.

#### Example: config

Here is the config of a freshly formatted 4 KiB x 32 filesystem, from the
[complete commit example](#example-a-complete-commit):

```
0004: 81 31 00 08 | 6c 69 74 74 6c 65 66 73   MAGIC     "littlefs"
0014: 01 34 00 02 | 00 00                     VERSION   v0.0
0022: 81 35 00 04 | 90 0c 01 00               RCOMPAT   0x00010c90
0036: 81 36 00 04 | 00 00 04 01               WCOMPAT   0x01040000
004a: 81 38 00 03 | ff 1f 1f                  GEOMETRY  4095+1 x 31+1
0061: 81 39 00 02 | ff 01                     NAMELIMIT 255
0077: 81 3a 00 05 | ff ff ff ff 07            FILELIMIT 0x7fffffff
```

## Compatibility

This section collects the rules a driver must follow to stay compatible
with other drivers, and notes where the v0.0 driver differs.

1. **Version** - A driver must refuse to mount a filesystem whose major
   version differs from its own, or whose minor version is newer than its
   own. Minor versions are for backwards-compatible additions. The driver's
   version is v0.0, and a filesystem without a `VERSION` tag is treated as
   v0.0. v0.0 is experimental: a released v3 driver will refuse v0.0
   filesystems, and two v0.0 filesystems from different alpha revisions of
   the driver may not be compatible.

2. **rcompat flags** - A driver must refuse to mount a filesystem with any
   rcompat flag it doesn't understand. The v0.0 driver is stricter: it
   requires the rcompat flags to equal its own set, `0x00010c90`, exactly,
   so it also refuses a filesystem that is missing any of its flags.

3. **wcompat flags** - A driver must refuse to write to a filesystem with
   any wcompat flag it doesn't understand, but may mount it read-only. The
   v0.0 driver requires the wcompat flags to equal its own set exactly,
   except for `GBMAP`, which it accepts either way when built with gbmap
   support, and `SETTLED`, which it accepts either way. On a mismatch it refuses to mount read-write, but can still
   mount read-only.

4. **ocompat flags** - May be ignored. The v0.0 driver never reads them.

5. **Config** - A driver must refuse to mount a filesystem whose
   active mroot contains a config tag (`0x0100-0x01ff` at rid -1) it doesn't
   understand. The v0.0 driver only looks for unknown config tags at
   `0x013b` and above, so unknown config tags in `0x0100-0x0130`, `0x0132`,
   and `0x0133` are silently ignored. This is a known gap, see
   [Open points](#open-points).

   A driver must also refuse a filesystem whose block size differs from its
   configured block size, whose block count is larger than its configured
   block count, or whose name or file limits are larger than it supports.

6. **New structures** - New representations of files or metadata come
   with an rcompat flag, which keeps old drivers from misreading them. The
   v0.0 driver ignores struct tags it doesn't expect, so without the flag,
   a file using an unknown struct would look empty.

7. **Unknown entry types** - Entries whose name tags are in
   `0x0300-0x03ff` but are not one of the known types are reported as an
   unknown type. The driver suggests future
   entry types should come with a wcompat flag.

8. **Unknown global state** - gdelta tags (`0x0200-0x02ff`) other than
   `GRMDELTA` and `GBMAPDELTA` are ignored. New global state that affects
   reading or writing must come with an rcompat or wcompat flag, as `GRM`
   and `GBMAP` do.

9. **Unknown checksum tags** - Checksum tags (`0x3000-0x3fff`) other than
   `CKSUM`, `ECKSUM`, and `GCKSUMDELTA`, such as `NOTE`, are included in
   the commit checksum and otherwise ignored. A reader that ignores the
   settled field of `CKSUM` reads every block correctly, but must not
   write a filesystem that holds settled copies, which the `SETTLED`
   wcompat flag says once the driver has compacted the mroot.

10. **Entry state tags** - Tags in `0x0500-0x05ff`, such as `DIRTY`, record
    state of an entry that a driver may ignore. A driver that ignores
    them keeps them on compaction (rule 13) and drops them with their
    entry; a stale `DIRTY` mark costs a later driver one settle and is then
    removed.

11. **Custom attributes** - Attributes are opaque, and a driver must keep
    working when any attribute is missing.

12. **Reserved bits** - Writers must write bit 7 of every tag as 0, must
    not write the `10` tag mode, and must write struct tags with the exact
    values listed here, including their low "redundancy" bits. The v0.0
    driver does not check bit 7 when reading, and treats a struct tag with
    different low bits as a different, unknown tag.

13. **Preserving unknown tags** - When the driver compacts an rbyd, it
    copies every tag in the tree, in order, including tags it doesn't
    understand. Attributes and other tags that the driver doesn't interpret
    survive compaction.

## Tag reference

What follows is an exhaustive list of the tags littlefs v3 writes to disk,
with their numeric values from `enum lfs3_tag` in `lfs3.h`.

Each entry also gives the tag's bit pattern, in the notation used by
`scripts/dbgtag.py`: `v` is the valid bit, `-` and `0` are zero bits, `1`
is a one bit, `+` is a reserved bit, `r` are redundancy bits, and other
letters are fields described in the entry.

Some quick reference:

```
tag      name          rid    weight      data
0x0000   NULL          any    any         none
0x0131   MAGIC         -1     0           "littlefs"
0x0134   VERSION       -1     0           u8 major, u8 minor
0x0135   RCOMPAT       -1     0           le flags
0x0136   WCOMPAT       -1     0           le flags
0x0137   OCOMPAT       -1     0           le flags
0x0138   GEOMETRY      -1     0           lleb128 bsize-1, leb128 bcount-1
0x0139   NAMELIMIT     -1     0           leb128
0x013a   FILELIMIT     -1     0           leb128
0x0230   GRMDELTA      -1     0           <=10 bytes
0x0234   GBMAPDELTA    -1     0           <=23 bytes
0x0300   BNAME         >=0    child       leb128 did, name
0x0301   REG           >=0    1           leb128 did, name
0x0302   DIR           >=0    1           leb128 did, name
0x0303   STICKYNOTE    >=0    1           leb128 did, name
0x0304   BOOKMARK      >=0    1           leb128 did
0x0330   MNAME         >=0    2^mbits     leb128 did, name
0x0400   BRANCH        >=0    child       branch (block, trunk, cksum)
0x0404   DATA          >=0    bytes       bytes
0x0408   BLOCK         >=0    bytes       bptr
0x0420   DID           >=0    0           leb128 did
0x0428   BSHRUB        >=0    0           leb128 weight, lleb128 trunk
0x042c   BTREE         >=0    0           leb128 weight, branch
0x0431   MROOT         -1     0           mptr
0x0435   MDIR          >=0    2^mbits     mptr
0x043c   MTREE         -1     0           leb128 weight, branch
0x0440   BMFREE        >=0    blocks      none
0x0441   BMINUSE       >=0    blocks      none
0x0442   BMERASED      >=0    blocks      optional ecksum
0x0443   BMBAD         >=0    blocks      none
0x0500   DIRTY         >=0    0           none
0x06xx   UATTR         any    0           bytes
0x07xx   SATTR         any    0           bytes
0x1xxx   SHRUB         -      -           shrub variant of a normal tag
0x4xxx   ALT           -      subtree     none, size is the jump
0x3000   CKSUM         -      0           le32 cksum, padding
0x3100   NOTE          -      0           bytes
0x3200   ECKSUM        -      0           lleb128 cksize, le32 cksum
0x3300   GCKSUMDELTA   -      0           le32
```

---
#### `0x0000` LFS3_TAG_NULL

bits: `v--- ---- +--- ----`

The null tag. Key 0 is never used for real data, and lookups never return
it. Writers use null tags, with size 0, to terminate trunks that don't
lead to real data, such as when building a tree during compaction or
after removing tags. In a shrub, the null tag is `0x1000`.

---
#### `0x01xx` LFS3_TAG_CONFIG

bits: `v--- ---1 +ttt tttt`

Config tags hold the filesystem's format-time configuration, at rid -1 of
the active mroot, see [Config](#config). A driver must refuse to mount a
filesystem with config tags it doesn't understand.

---
#### `0x0131` LFS3_TAG_MAGIC

bits: `v--- ---1 +-11 --rr`

The magic string, `"littlefs"`, which identifies a littlefs mroot. Every
mroot, including the anchor and the rest of the mroot chain, must have one
at rid -1, and a reader must treat an mroot without it, or with a different
string, as corrupt.

```
  tag     weight  size                  data
[- 16 -][leb128][leb128][---             64             ---]
    ^      ^       ^                     ^- magic string ("littlefs")
    |      |       '- size (8)
    |      '--------- weight (0)
    '---------------- tag (0x0131)
```

Since `MAGIC` is the smallest tag at rid -1, a compacted mroot starts with
it, so the magic string usually sits at offset 8 of blocks 0 and 1, see
[The mroot anchor and the mroot chain](#the-mroot-anchor-and-the-mroot-chain).

---
#### `0x0134` LFS3_TAG_VERSION

bits: `v--- ---1 +-11 -1--`

The on-disk version of the filesystem.

```
  tag     weight  size       data
[- 16 -][leb128][leb128][- 8 -|- 8 -]
    ^      ^       ^       ^     ^- minor version
    |      |       |       '------- major version
    |      |       '- size (2)
    |      '--------- weight (0)
    '---------------- tag (0x0134)
```

Version fields:

1. **Major version (8-bits)** - Incremented on backwards-incompatible
   changes.

2. **Minor version (8-bits)** - Incremented on backwards-compatible
   feature additions.

This specification describes version v0.0 (`00 00`), the experimental
version. Missing bytes read as zero, so a missing `VERSION` tag means v0.0.
Note the header's `LFS3_DISK_VERSION` macros allow 16-bit major and minor
versions, but only 8 bits of each are stored.

---
#### `0x0135` LFS3_TAG_RCOMPAT

bits: `v--- ---1 +-11 -1-1`

Flags that must be understood to read the filesystem, see
[rcompat flags](#rcompat-flags).

```
  tag     weight  size          data
[- 16 -][leb128][leb128][---  variable length  ---]
    ^      ^       ^                ^- little-endian flags
    |      |       '- size
    |      '--------- weight (0)
    '---------------- tag (0x0135)
```

---
#### `0x0136` LFS3_TAG_WCOMPAT

bits: `v--- ---1 +-11 -11-`

Flags that must be understood to write to the filesystem, see
[wcompat flags](#wcompat-flags). Same layout as `RCOMPAT`.

---
#### `0x0137` LFS3_TAG_OCOMPAT

bits: `v--- ---1 +-11 -111`

Flags that don't need to be understood, see
[ocompat flags](#ocompat-flags). Same layout as `RCOMPAT`. Never written by
the v0.0 driver.

---
#### `0x0138` LFS3_TAG_GEOMETRY

bits: `v--- ---1 +-11 1---`

The filesystem's geometry. Both values are stored minus one, so the full
range of each fits.

```
  tag     weight  size          data
[- 16 -][leb128][leb128][- lleb128 -][- leb128 -]
    ^      ^       ^          ^            ^- block count - 1
    |      |       |          '-------------- block size - 1
    |      |       '- size
    |      '--------- weight (0)
    '---------------- tag (0x0138)
```

Geometry fields:

1. **Block size - 1 (lleb128, <=28-bits)** - Size of a block in bytes,
   minus one. A driver must use the same block size.

2. **Block count - 1 (leb128, <=31-bits)** - Number of blocks in the
   filesystem, minus one. The filesystem may be smaller than the device,
   and may later be grown.

A driver must refuse to mount a filesystem without a `GEOMETRY` tag.

---
#### `0x0139` LFS3_TAG_NAMELIMIT

bits: `v--- ---1 +-11 1--1`

The maximum length of a file name, in bytes, as a leb128 (written as an
lleb128). Defaults to 255 if missing. A driver must refuse to mount a
filesystem whose name limit is larger than it supports.

---
#### `0x013a` LFS3_TAG_FILELIMIT

bits: `v--- ---1 +-11 1-1-`

The maximum size of a file, in bytes, as a leb128. Defaults to
`0x7fffffff` if missing. A driver must refuse to mount a filesystem whose
file limit is larger than it supports.

---
#### `0x02xx` LFS3_TAG_GDELTA

bits: `v--- --1- +ttt tttt`

Global-state deltas, at rid -1 of any metadata pair, see
[Global state](#global-state).

---
#### `0x0230` LFS3_TAG_GRMDELTA

bits: `v--- --1- +-11 --++`

A delta of the [grm](#grm), up to 10 bytes. The two low bits are reserved.

```
  tag     weight  size          data
[- 16 -][leb128][leb128][- leb128 -][- leb128 -]
    ^      ^       ^          ^            ^- second mid (delta)
    |      |       |          '-------------- first mid (delta)
    |      |       '- size (<=10)
    |      '--------- weight (0)
    '---------------- tag (0x0230)
```

Note the delta is xored byte-wise into the grm before decoding, so a single
delta is not necessarily a valid pair of leb128s on its own.

---
#### `0x0234` LFS3_TAG_GBMAPDELTA

bits: `v--- --1- +-11 -1rr`

A delta of the [gbmap gstate](#gbmap-gstate), up to 23 bytes: window,
known, and a branch to the gbmap's root.

```
  tag     weight  size                data
[- 16 -][leb128][leb128][leb128][leb128][leb128][lleb128][-- 32 --]
    ^      ^       ^       ^       ^       ^       ^         ^- cksum
    |      |       |       |       |       |       '----------- trunk
    |      |       |       |       |       '------------------- block
    |      |       |       |       '--------------------------- known
    |      |       |       '----------------------------------- window
    |      |       '- size (<=23)
    |      '--------- weight (0)
    '---------------- tag (0x0234)
```

---
#### `0x03xx` LFS3_TAG_NAME

bits: `v--- --11 +ttt tttt`

Name tags. In metadata pairs, each entry's name tag is its smallest tag,
carries the entry's weight of 1, and gives the entry's type. In the mtree
and other named B-trees, name tags carry names used for binary search.

All name tags share the same data layout:

```
  tag     weight  size          data
[- 16 -][leb128][leb128][- leb128 -][---  variable length  ---]
    ^      ^       ^          ^                  ^- name
    |      |       |          '-------------------- did
    |      |       '- size
    |      '--------- weight
    '---------------- tag (0x03xx)
```

Name fields:

1. **Did (leb128)** - The id of the directory containing the entry.

2. **Name** - The entry's name, the rest of the tag's data. Not
   null-terminated, and empty for bookmarks.

---
#### `0x0300` LFS3_TAG_BNAME

bits: `v--- --11 +--- ----`

A branch name in an inner node of a named B-tree (the mtree), at the same
rid as the [`BRANCH`](#0x0400-lfs3_tag_branch) it names. Holds a copy of
the first name in the branch's subtree as of when the subtree was split
off, a lower bound used for binary search. Its weight is the subtree's
weight.

---
#### `0x0301` LFS3_TAG_REG

bits: `v--- --11 +--- ---1`

A regular file. Its struct tag, if any, is a
[`BSHRUB`](#0x0428-lfs3_tag_bshrub) or [`BTREE`](#0x042c-lfs3_tag_btree)
with the file's data, see [Files](#files).

---
#### `0x0302` LFS3_TAG_DIR

bits: `v--- --11 +--- --1-`

A directory. Its struct tag is a [`DID`](#0x0420-lfs3_tag_did) with the
directory's own did, see [Directories](#directories).

---
#### `0x0303` LFS3_TAG_STICKYNOTE

bits: `v--- --11 +--- --11`

A file that has been created but not synced. Stickynotes are only
considered to exist while a file handle has them open, and readers must
otherwise ignore them, see [Directories](#directories).

---
#### `0x0304` LFS3_TAG_BOOKMARK

bits: `v--- --11 +--- -1--`

The start of a directory's entries. Its did is the directory's own did,
and its name is empty, so its data is only the did. The root directory's
bookmark, did 0, is always mid 0.

---
#### `0x0330` LFS3_TAG_MNAME

bits: `v--- --11 +-11 ----`

The name of a metadata pair in an mtree leaf, at the same rid as its
[`MDIR`](#0x0435-lfs3_tag_mdir). A lower bound on the names in the metadata
pair, used for binary search. Its weight is `2^mbits`. The first metadata
pair in the mtree has no `MNAME`.

---
#### `0x04xx` LFS3_TAG_STRUCT

bits: `v--- -1-- +ttt tttt`

Struct tags describe on-disk data structures. Each entry, and rid -1 of
each mroot, has at most one struct tag, and a reader looks it up as the
first tag at the rid in `0x0400-0x04ff`. Writing a new struct tag replaces
the old one.

---
#### `0x0400` LFS3_TAG_BRANCH

bits: `v--- -1-- +--- --rr`

A pointer to a child node in an inner node of a B-tree. The rid's weight
is the child's weight.

```
  tag     weight  size          data
[- 16 -][leb128][leb128][- leb128 -][- lleb128 -][-- 32 --]
    ^      ^       ^          ^            ^           ^- cksum
    |      |       |          |            '------------- trunk
    |      |       |          '-------------------------- block
    |      |       '- size (<=13)
    |      '--------- weight (child's weight, unless named)
    '---------------- tag (0x0400)
```

Branch fields:

1. **Block (leb128)** - The block containing the child node.

2. **Trunk (lleb128)** - The offset of the child's trunk in the block.

3. **Cksum (32-bits)** - The child's canonical checksum as of the commit
   containing the trunk.

---
#### `0x0404` LFS3_TAG_DATA

bits: `v--- -1-- +--- -1rr`

A fragment of a file, with the data inline. Its weight is the number of
bytes it covers. Its size may be smaller than its weight, in which case the
rest reads as zeros, and a size of 0 is a hole.

```
  tag     weight  size          data
[- 16 -][leb128][leb128][---  variable length  ---]
    ^      ^       ^                ^- file data
    |      |       '- size (<= weight)
    |      '--------- weight (bytes covered)
    '---------------- tag (0x0404, 0x1404 in a shrub)
```

---
#### `0x0408` LFS3_TAG_BLOCK

bits: `v--- -1-- +--- 1err`

A block pointer (bptr), pointing to file data in a data block. Its weight
is the number of bytes it covers.

```
  tag     weight  size              data
[- 16 -][leb128][leb128][lleb128][leb128][lleb128][lleb128][-- 32 --]
    ^      ^       ^        ^       ^       ^        ^         ^- cksum
    |      |       |        |       |       |        '----------- cksize
    |      |       |        |       |       '-------------------- off
    |      |       |        |       '---------------------------- block
    |      |       |        '------------------------------------ size
    |      |       '- size (<=21)
    |      '--------- weight (bytes covered)
    '---------------- tag (0x0408, 0x1408 in a shrub)
```

Bptr fields:

1. **Size (lleb128)** - Number of bytes of data in the block. Only the
   first `min(size, weight)` bytes are used, the rest of the weight reads
   as zeros.

2. **Block (leb128)** - The data block.

3. **Off (lleb128)** - Offset of the data in the block.

4. **Cksize (lleb128)** - Number of bytes at the start of the block
   covered by the checksum.

5. **Cksum (32-bits)** - CRC-32C of the first `cksize` bytes of the block.

`scripts/dbgtag.py` marks bit 2 of `BLOCK` as an `e` field. The driver
doesn't define it, writes it as 0, and would not recognize a `BLOCK` tag
with it set.

---
#### `0x0420` LFS3_TAG_DID

bits: `v--- -1-- +-1- ----`

The struct tag of a [`DIR`](#0x0302-lfs3_tag_dir) entry, holding the
directory's own did as a leb128.

---
#### `0x0428` LFS3_TAG_BSHRUB

bits: `v--- -1-- +-1- 1-rr`

The struct tag of a file whose B-tree root is a [shrub](#shrubs) in the
same block as the tag.

```
  tag     weight  size          data
[- 16 -][leb128][leb128][- leb128 -][- lleb128 -]
    ^      ^       ^          ^            ^- trunk
    |      |       |          '-------------- weight (file size)
    |      |       '- size (<=9)
    |      '--------- weight (0)
    '---------------- tag (0x0428)
```

Bshrub fields:

1. **Weight (leb128)** - The shrub's weight, which is the file's size.

2. **Trunk (lleb128)** - Offset of the shrub's trunk in the metadata pair's
   active block. Never 0.

---
#### `0x042c` LFS3_TAG_BTREE

bits: `v--- -1-- +-1- 11rr`

The struct tag of a file whose B-tree root is in its own block.

```
  tag     weight  size          data
[- 16 -][leb128][leb128][- leb128 -][- leb128 -][- lleb128 -][-- 32 --]
    ^      ^       ^          ^           ^            ^           ^- cksum
    |      |       |          |           |            '------------- trunk
    |      |       |          |           '-------------------------- block
    |      |       |          '-------------------------------------- weight
    |      |       '- size (<=18)
    |      '--------- weight (0)
    '---------------- tag (0x042c)
```

The weight is the file's size, followed by a [branch](#b-trees) to the
root.

---
#### `0x0431` LFS3_TAG_MROOT

bits: `v--- -1-- +-11 --rr`

A pointer to the next mroot in the
[mroot chain](#the-mroot-anchor-and-the-mroot-chain), at rid -1 of an
mroot.

```
  tag     weight  size          data
[- 16 -][leb128][leb128][- leb128 -][- leb128 -]
    ^      ^       ^          ^            ^- second block
    |      |       |          '-------------- first block
    |      |       '- size (<=10)
    |      '--------- weight (0)
    '---------------- tag (0x0431)
```

The two low bits are reserved for redundancy, `scripts/dbgtag.py` calls
them `rr`, and are `01` in v0.0. The driver matches the whole tag value and
reads exactly two blocks, see [Open points](#open-points).

---
#### `0x0435` LFS3_TAG_MDIR

bits: `v--- -1-- +-11 -1rr`

A pointer to a metadata pair, in an mtree leaf. The rid's weight is
`2^mbits`. Same layout as [`MROOT`](#0x0431-lfs3_tag_mroot).

At rid -1 of the mroot, an `MDIR` would presumably be the reserved
`MSPROUT` representation, which the driver doesn't implement, and treats as
corruption.

---
#### `0x043c` LFS3_TAG_MTREE

bits: `v--- -1-- +-11 11rr`

A pointer to the root of the [mtree](#the-metadata-tree-mtree), at rid -1
of the active mroot. Same layout as [`BTREE`](#0x042c-lfs3_tag_btree): the
mtree's weight, the number of metadata pairs times `2^mbits`, followed by a
branch to its root.

---
#### `0x0440` LFS3_TAG_BMFREE

bits: `v--- -1-- +1-- ----`

A range of free blocks in the [gbmap](#the-global-block-map-gbmap). Its
weight is the number of blocks, and it has no data. `0x0440-0x0443` are
collectively the `BMRANGE` tags, whose two low bits are the range's state:

```
bits: v--- -1-- +1-- ++ei
                       ^^- in use
                       '-- erased
```

Bits 2 and 3 are reserved. In use and erased together means bad.

---
#### `0x0441` LFS3_TAG_BMINUSE

bits: `v--- -1-- +1-- ---1`

A range of blocks that are in use, or that the driver can't prove are free.

---
#### `0x0442` LFS3_TAG_BMERASED

bits: `v--- -1-- +1-- --1-`

A range of free blocks that have already been erased, with an optional
erased-state checksum:

```
  tag     weight  size          data
[- 16 -][leb128][leb128][- lleb128 -][-- 32 --]
    ^      ^       ^           ^           ^- cksum
    |      |       |           '------------- cksize
    |      |       '- size (0 or <=8)
    |      '--------- weight (blocks)
    '---------------- tag (0x0442)
```

The checksum applies to each block in the range: the CRC-32C of its first
`cksize` bytes after erasing. With size 0, the erased state is unknown.

---
#### `0x0443` LFS3_TAG_BMBAD

bits: `v--- -1-- +1-- --11`

A range of bad blocks. Reserved for planned bad-block tracking: the driver
never writes it, and treats it as in use.

---
#### `0x0500` LFS3_TAG_DIRTY

bits: `v--- -1-1 +--- ----`

Marks a regular file whose write session is open. The first metadata
commit of a session on an existing file (a shrub commit or a sync), or the
sync that creates a new file, adds it at the file's rid, with weight 0 and
no data, and the commit that ends the session (`lfs3_file_close`) removes
it. Until that first sync a new file is a `STICKYNOTE`, which marks it the
same way. A mark or a stickynote found at mount belongs to a session a
power loss interrupted: the driver [settles](#settled-copies) the file's
metadata pair and the pairs on the path to it from the mroot anchor before
it appends to any of them, and its first `lfs3_fs_mkconsistent` removes
the stale mark. `lfs3_set` commits a file in a single commit and never
marks it.

Ignorable: a driver that doesn't know it keeps it on compaction, and needs
no compat flag.

---
#### `0x06xx` LFS3_TAG_UATTR

bits: `v--- -11- +aaa aaaa`

A user attribute, type `0x00-0x7f`, see
[Custom attributes](#custom-attributes). `LFS3_TAG_ATTR` is also `0x0600`,
the base of both attribute ranges.

```
  tag     weight  size          data
[- 16 -][leb128][leb128][---  variable length  ---]
    ^      ^       ^                ^- attribute data
    |      |       '- size
    |      '--------- weight (0)
    '---------------- tag (0x0600 | type)
```

---
#### `0x07xx` LFS3_TAG_SATTR

bits: `v--- -111 +aaa aaaa`

A standard attribute, type `0x80-0xff`, stored as `0x0700 | (type & 0x7f)`.
Same layout as `UATTR`. No standard attributes are defined yet.

---
#### `0x1xxx` LFS3_TAG_SHRUB

bits: `v--1 kkkk +kkk kkkk`

The shrub mode bits. A normal tag with `0x1000` set is the same tag in a
[shrub](#shrubs), a secondary tree in the same block. In v0.0, shrubs
contain shrub `DATA` (`0x1404`), shrub `BLOCK` (`0x1408`), and shrub null
(`0x1000`) tags.

---
#### `0x4xxx` LFS3_TAG_ALT

bits: `v1cd kkkk +kkk kkkk`

An [alt pointer](#alt-pointers), an inner node of an rbyd tree.

```
  tag     weight  size
[- 16 -][leb128][leb128]
    ^      ^       ^- jump, bytes back from this tag
    |      '--------- weight of the subtree jumped to
    '---------------- 0x4000 | color | direction | key
```

- `LFS3_TAG_B` (`0x0000`) / `LFS3_TAG_R` (`0x2000`) - black or red.
- `LFS3_TAG_LE` (`0x0000`) / `LFS3_TAG_GT` (`0x1000`) - points to the
  lower or upper side of the split.
- Key (12-bits) - the split's tag key. Since keys come from normal tags,
  whose bit 7 is reserved, bit 7 of an alt's key is also always 0, and
  `scripts/dbgtag.py` marks it as reserved.

---
#### `0x3000` LFS3_TAG_CKSUM

bits: `v-11 ---- +++s spqq`

Marks the end of a commit, and provides a checksum for the commit.

```
  tag     weight  size              data
[- 16 -][leb128][- 4-byte leb128 -][-- 32 --][---   padding   ---]
    ^      ^            ^               ^              ^- padding
    |      |            |               '---------------- cksum
    |      |            '- size (4 + padding)
    |      '-------------- weight (0)
    '--------------------- 0x3000 | settled | perturb | phase
```

Cksum fields:

1. **Phase (2-bits)** - The two low bits of the block's address. A reader
   must treat a `CKSUM` tag whose phase doesn't match the block it's in as
   invalid. This catches blocks that were copied, or read at the wrong
   address, which could otherwise look valid.

2. **Perturb (1-bit)** - If set, the next commit's running checksum is
   xored with `0xfca42daf`, inverting its valid bits, see
   [Perturbation](#perturbation).

3. **Settled (2-bits, bits 3-4)** - 0, or the generation, 1 to 3, of a
   [settled copy](#settled-copies) whose last commit this was. A settle
   clears these bits in the commits it copies before its last, so a block
   holds at most one settled commit.

4. **Cksum (32-bits)** - The running checksum of the commit up to and
   including the `CKSUM` tag's own tag, weight, and size, with the valid bit
   cleared. A size smaller than 4 is invalid.

5. **Padding** - Padding to the next `prog_size`-aligned offset, where the
   next commit starts. No guarantees are made about its contents.

The size's leb128 encoding depends on the padding, which depends on the
size's encoding, so writers break the cycle by always writing the size as
a fully-expanded 4-byte leb128. Readers accept any encoding.

The other bits are reserved, writers must write 0, and readers ignore them.
Writers always write a weight of 0.

---
#### `0x3100` LFS3_TAG_NOTE

bits: `v-11 ---1 ++++ ++++`

A checksum tag with arbitrary data, included in the commit's checksum and
otherwise ignored. Reserved, never written by the driver.

---
#### `0x3200` LFS3_TAG_ECKSUM

bits: `v-11 --1- ++++ ++++`

An [erased-state checksum](#erased-state-checksums), describing the erased
storage after the commit.

```
  tag     weight  size          data
[- 16 -][leb128][leb128][- lleb128 -][-- 32 --]
    ^      ^       ^           ^           ^- cksum
    |      |       |           '------------- cksize
    |      |       '- size (<=8)
    |      '--------- weight (0)
    '---------------- tag (0x3200)
```

Ecksum fields:

1. **Cksize (lleb128)** - Number of bytes covered, the writer's
   `prog_size`.

2. **Cksum (32-bits)** - CRC-32C of the `cksize` bytes following the
   commit, when erased.

Only an `ECKSUM` in the last commit of an rbyd counts.

---
#### `0x3300` LFS3_TAG_GCKSUMDELTA

bits: `v-11 --11 ++++ ++++`

A metadata pair's [gcksum](#gcksum) delta, as a le32. Only the
`GCKSUMDELTA` in the last commit of a metadata pair counts, and a last
commit without one means a delta of zero.

```
  tag     weight  size      data
[- 16 -][leb128][leb128][-- 32 --]
    ^      ^       ^         ^- gcksum delta
    |      |       '- size (4)
    |      '--------- weight (0)
    '---------------- tag (0x3300)
```

---
#### In-device only tags

`lfs3.h` also defines tags that are only used in RAM and must never appear
on disk:

- `0x013b` `LFS3_tag_UNKNOWNCONFIG` - the start of the unknown config
  range, used by the mount check.
- `0x0305` `LFS3_tag_ORPHAN`, `0x0306` `LFS3_tag_TRV`, `0x0307`
  `LFS3_tag_UNKNOWN` - name types for orphaned entries, traversals, and
  unknown entries.
- `0x0000-0x0005` `LFS3_tag_INTERNAL`, `RATTRS`, `SHRUBCOMMIT`, `GRMPUSH`,
  `MOVE`, `ATTRS` - internal commit operations.
- `0x8000` `LFS3_tag_RM`, `0x4000` `LFS3_tag_GROW`, and `0x1000-0x3000`
  `LFS3_tag_MASK2/MASK8/MASK12` - modifiers on pending commits.

## Open points

The v3 on-disk format is still being worked on, and this specification
describes the v0.0 format as implemented by the v3-alpha driver. The
following are places where the format is unfinished, marked as reserved or
TODO in the code, or where the driver and this document leave a question
open.

1. **The version is not frozen.** `LFS3_DISK_VERSION` is `0x00000000`, and
   every v3-alpha image claims v0.0, so incompatible alpha revisions can't
   be told apart on disk. The maintainer has said the format may still
   change before release, and that the released driver will reject v0.0.

2. **Planned features.** Bad-block tracking (`BMBAD` is reserved but never
   written), metadata redundancy, data redundancy and deduplication, and
   16-bit and 64-bit variants are planned or being considered. Bad-block
   tracking is the one the maintainer lists as a release blocker. Any of
   these may change the format.

3. **Redundancy bits.** The two low bits of `MAGIC`, `GBMAPDELTA`, and
   most struct tags are reserved for redundancy (`lfs3_tag_redund` exists in
   the driver but is unused).
   `MAGIC`, `MROOT`, and `MDIR` are written with `01`, everything else with
   `00`. The driver matches these tags exactly and reads exactly two blocks
   from an mptr, while the debug scripts ignore the redundancy bits and read
   any number of blocks. How readers should treat other values is not
   defined yet.

4. **Reserved representations.** The `MSPROUT`, `MSHRUB`, `BMOSS`, and
   `BSPROUT` rcompat flags name representations that are not implemented,
   and their exact encodings are not defined.

5. **Reserved tags and bits.** The `NOTE` tag is reserved and never
   written. The `10` tag mode is unused. Bit 7 of every tag is reserved for
   extending the subtype. `scripts/dbgtag.py` marks bit 2 of `BLOCK` as an
   `e` field that the driver doesn't define.

6. **Unknown config tags.** The driver only rejects unknown config tags at
   `0x013b` and above. Unknown config tags at `0x0100-0x0130`, `0x0132`,
   and `0x0133` are silently accepted, which breaks rule 5 of
   [Compatibility](#compatibility).

7. **rcompat matching.** The compat flags are described as "must
   understand", but the driver requires its rcompat flags, and its wcompat
   flags apart from `GBMAP`, to match exactly, so a filesystem that doesn't
   set the flag of a feature the driver supports (`MTREE`, say) is still
   rejected. It's not clear yet whether a driver may accept a subset of its
   flags.

8. **Version width.** The version macros allow 16-bit major and minor
   versions, but `VERSION` stores 8 bits of each. `scripts/dbglfs3.py`
   decodes `VERSION` as two leb128s instead of two bytes. The two agree for
   versions below 128.

9. **Custom attribute ranges.** `lfs3.h` reserves types `0x80-0xff` for
   standard attributes, while the v3-alpha announcement reserves
   `0x80-0xbf` and encourages `0xc0-0xff` for system attributes. No
   standard attributes are defined.

10. **Limits.** The format puts no limit on names or attribute sizes beyond
    `NAMELIMIT`, but an entry and its attributes must fit in a metadata
    pair after compaction. The driver doesn't enforce this, and very large
    names or attributes, relative to the block size, can trip an assertion
    in the driver.

11. **Unchecked bptrs.** The driver doesn't check that a bptr's
    `off + size <= cksize <= block_size` when reading it.

12. **Erased-state boundary.** Writers emit an `ECKSUM` whenever another
    commit could start before the end of the block, but readers require
    `eoff + cksize < block_size`, so a commit ending exactly one program
    unit before the end of the block gets an `ECKSUM` that is rejected
    later. This is harmless, but one of the two should probably change.

13. **gbmap at format.** Format always places the initial gbmap root in
    block 2, with a TODO about trying other blocks if block 2 is bad.

14. **Empty `BMERASED`.** An empty `BMERASED` payload is defined as
    "erased, checksum unknown", but the driver never writes one.
