# 2 — Files and the data path (littlefs v3-alpha, `v3-work` = b10efaa)

Scope: file B-trees, B-shrubs, leaves (fragments/blocks), crystallization, write/read
path, fcache/pcache, flush vs sync, the sync model (snapshots, broadcast,
desync/resync, stickynotes, zombies), open flags, file_limit, kv API, file attrs
as they interact with sync.

Evidence tags used throughout:

- **[DOC]** documented intent (lfs3.h docs, code comments, commit messages)
- **[IMPL]** implemented behaviour read from lfs3.c
- **[TEST]** exercised by a test case (suite:case)
- **[MEAS]** measured by me with throw-away programs built from lfs3.c + bd/lfs3_emubd.c in
  the session scratchpad (nothing in the repo was modified). Programs: `meas.c` (op counts per
  sync), `img.c` (image for dbglfs3.py), `flim.c` (file_limit), `rdsync.c` (RDONLY sync),
  `frbad.c` (fruncate + bad block), `fmax.c` (2 GiB sparse file).
- **[?]** uncertain / inferred

All line numbers are lfs3.c unless prefixed `h:` (lfs3.h), `t:` (tests/…), `s:` (scripts/…).

Glossary (v3 terms the docs must define):

| term | meaning |
|---|---|
| bid | B-tree id = byte offset of the **last** byte of an entry (entry covers `[bid-(weight-1), bid]`), 5338-5419 |
| weight | number of file bytes an entry (or subtree) covers; root weight == file size |
| leaf / entry | one rid in a leaf rbyd: a `DATA` tag (fragment or hole) or a `BLOCK` tag (bptr) |
| fragment | inline data stored in the leaf rbyd itself (`TAG_DATA`), `size <= fragment_size` |
| bptr | block pointer (`TAG_BLOCK`): block, off, size, cksize, cksum |
| hole | entry whose `size < weight`; bytes past `size` read as zeros; pure hole = `DATA` with size 0 |
| crystal / crystallize | packing fragments + new data into a freshly erased data block (13368-13663) |
| graft | inserting a crystallized bptr or fragments into the tree (13097-13365) |
| shrub | an rbyd trunk living *inside an mdir block* (tags carry the 0x1000 SHRUB bit) |
| bshrub | a file B-tree whose root is a shrub (`TAG_BSHRUB`); btree = root is its own block (`TAG_BTREE`) |
| stickynote | name tag `0x0303` reserving a mid for a created-but-never-synced file |
| zombie | open handle whose file was removed/replaced (`LFS3_o_ZOMBIE`) |
| snapshot | a file handle's in-RAM view (bshrub/btree + leaf + cache), independent of disk until sync |

---------------------------------------------------------------------------------------------

## 1. Features

### 1.1 Handle state (h:1204-1226, h:146-154)

`lfs3_file_t = { lfs3_bshrub_t b (handle + btree root b.r + staged shrub b_), cfg, pos,
cache{pos,size,buffer}, leaf{pos,weight,bptr} }`.

Internal flags (h:147-154): `o_WRSET=3` (lfs3_set only), `o_TYPE`, `o_ZOMBIE 0x08000000`,
`o_UNCREAT 0x04000000` (not on disk yet), `o_UNSYNC 0x02000000` (RAM != disk),
`o_UNCRYST 0x01000000` (leaf block partially crystallized), `o_UNGRAFT 0x00800000` (leaf not
in tree), `o_UNFLUSH 0x00400000` (cache holds unwritten data).
Invariant chain asserted in code: UNFLUSH/UNCRYST/UNGRAFT ⇒ UNSYNC (13346, 13673, 14262,
14312-14319); UNCREAT ⇒ UNSYNC (14312).

`lfs3_file_size_` = max(cache.pos+cache.size, leaf.pos+leaf.weight, b.r.weight) (12498-12504).

### 1.2 Open / close (h:1505-1541; 12590-12885)

`lfs3_file_open(lfs3, file, path, flags)` = `opencfg` with zeroed cfg (12826). Not available
with `LFS3_NO_MALLOC` (h:1511) — then `opencfg` with `fcache_buffer` is required.

| flag | value | semantics [IMPL] |
|---|---|---|
| O_RDONLY/WRONLY/RDWR | 0/1/2 | mode 3 is asserted forbidden (12788); writable mode asserted on RDONLY mount (12804) |
| O_CREAT | 0x04 | create if absent: commits a STICKYNOTE name (12725-12739) unless small `lfs3_set` (12707-12724) |
| O_EXCL | 0x08 | `LFS3_ERR_EXIST` if entry exists — including another handle's visible stickynote (12670) |
| O_TRUNC | 0x10 | only marks UNSYNC and skips fetching the tree (12522-12533, 12693-12695); old data stays on disk until sync → atomic replace |
| O_APPEND | 0x20 | each write goes to `lfs3_file_size_()` (14133-14135) |
| O_FLUSH | 0x40 | `lfs3_file_flush` after every write (14226-14231) |
| O_SYNC | 0x80 | `lfs3_file_sync` after every write (14234-14239); **not** after truncate/fruncate [TEST t:test_fsync.toml rwtf… "note LFS3_O_SYNC does _not_ sync truncates"] |
| O_DESYNC | 0x00100000 | open desynced: no broadcast received, close does not sync (h:142) |
| O_CKMETA/CKDATA | 0x1000/0x2000 | run `lfs3_file_ck` at open; CORRUPT fails the open (12761-12772) |
| M_FLUSH/M_SYNC (mount) | 0x40/0x80 | OR'd into every open (12604-12607) |

Errors from open [IMPL]: NOENT (missing, or parent missing, or orphan w/o CREAT), EXIST,
ISDIR (tag DIR, 12677), NOTSUP (unknown name type, 12681), NOTDIR (trailing slash on create,
12649), NAMETOOLONG (> name_limit, 12655), NOMEM (fcache malloc, 12621), CORRUPT (ck flags),
plus anything from `lfs3_fs_mkconsistent` which every writable open runs first (12594-12600).
Asserts: already open (12786), unknown flags (12790), CREAT/EXCL/TRUNC with RDONLY (12807-12809).

Open of an existing **stickynote** (another in-sync handle created it): succeeds as
UNCREAT|UNSYNC with an empty view (12688-12690) [TEST test_stickynotes_uncreat_sync_rw
"via open … read => 0"]. Open with CREAT of an **orphan** reuses the orphan's mid without a new
commit (12661-12663).

Close (12871-12885): if not RDONLY and not DESYNC → `lfs3_file_sync`; always releases the
handle (12833-12866) even on error. If the handle was UNCREAT and no other handle holds the
mid, the stickynote is queued for removal: push onto the in-RAM grm queue (≤2 entries) or set
`LFS3_I_MKCONSISTENT` (12846-12864) [DOC h:1530-1541 "Readonly and desynchronized files do
not touch disk and will always return 0"].

### 1.3 Data representation (per file)

- Zero-size file: **no** struct tag in the mdir (sync removes it, 14390-14392).
- Otherwise the mdir entry carries exactly one of `BSHRUB` (tree root inlined in the mdir)
  or `BTREE` (root rbyd in its own block) (14389-14409, 6678-6722).
- Leaves are `DATA` (fragment or hole) or `BLOCK` (bptr). Reader asserts nothing else can
  appear in a file tree (12899-12900; 2432-2434 `LFS3_UNREACHABLE`).
- An entry may have `size < weight` (trailing hole) [MEAS img.c: sparse writes produced
  `data w600 100`] and on read `size > weight` is tolerated and sliced to weight (2436-2438)
  for future compression [TEST test_fwrite_bigger_than_expected_fragments/_blocks].
- rcompat advertises `MMOSS|MTREE|BSHRUB|BTREE|GRM` (15432-15439); `BMOSS` (file struct =
  inline data) and `BSPROUT` (file struct = single bptr) are declared (h:934-935) but never
  written, and mount requires an exact rcompat match (15610-15619) → such images are
  `NOTSUP`.
- Limits: block ≤ 31 bits, bptr size/off/cksize ≤ 28 bits (2309-2316); tree weight ≤ 31 bits
  (5254-5255, 6475-6476); file ≤ `file_limit` ≤ `LFS3_FILE_MAX` = 2^31-1 (h:69-74)
  [MEAS fmax.c: sparse file ending exactly at 2147483647 writes, remounts, reads and
  fruncates correctly].

### 1.4 Write path (14107-14247 → 13696-14100 → 13380-13663 / 13097-13335)

`lfs3_file_write(lfs3, file, buf, size)` → bytes written or error.

1. `size==0` returns 0 with no side effects (14117-14119) [DOC comment].
2. FBIG if `size > file_limit - file->pos` (14123-14126) — see bug B2 (uses `pos`, not the
   append position).
3. Marks UNSYNC; picks `pos` (APPEND → current size).
4. **fcache** (per-file cache, size `fcache_size` or per-file `cfg->fcache_size`,
   12491-12496):
   - write ≥ fcache_size and cache clean → bypass: `lfs3_file_flush_` straight from the user
     buffer, then copy the tail of the write into the cache as a clean read cache
     (14146-14170);
   - else copy into the cache if the cache is clean or the write is contiguous within the
     cache window (14182-14211) → UNFLUSH;
   - else flush the cache first (14213-14219).
5. O_FLUSH / O_SYNC post-actions; any error ⇒ `LFS3_O_DESYNC` set (14243-14246).

`lfs3_file_flush_(pos, buf, size)` decides fragments vs blocks:

- `crystal_thresh > block_size` ⇒ fragments only (13703-13705).
- **Resume** (13715-13756): if the leaf is a bptr with in-RAM `ISERASED`, `pos` lies in
  `[block_end, block_start+block_size)`, `pos-block_end < max(crystal_thresh,1)` and at least
  `min(prog_size, crystal_thresh)` bytes are available, keep programming the *same* block
  after its current `cksize` (no erase).
- Otherwise graft any pending leaf, then estimate the crystal: extend left/right over
  adjacent *fragments* (not holes, not blocks) within `crystal_thresh` (13776-13847). If the
  crystal is `< crystal_thresh` → fragment path; else crystallize a new block, aligning the
  block start to the left neighbour's physical block start (13907-13945; "use the actual
  block start … avoids excessive recrystallizations when fruncating", commit c33182b).
- Fragment path (13971-14099): chunks of ≤ `fragment_size`, coalesced with overlapping or
  adjacent left/right fragments (≤ 3 datas), grafted one commit per chunk. The file's leaf is
  discarded if it is a fragment or overlaps the write (13989-13993), but kept if it is a
  non-overlapped block so its erased state survives.

`lfs3_file_crystallize_(block_pos, crystal_min, crystal_max, pos, buf, size)` (13380-13663):
- Before writing into a resumed block, **claims** (clears ISERASED on) every other handle's
  leaf that points to the same block (13415-13423).
- Fills `[pos_, crystal_limit)` from, in priority order: the caller's buffer, the file's
  leaf, the tree (copies fragments / old block data), zeros for holes (13437-13581). Stops
  early at pure holes or data that would overrun the block (13514-13536).
- **Prog alignment trick** (13597-13612): drops the unaligned tail
  `(pos_-block_pos) % prog_size` from the pcache (if `< max(crystal_thresh,1)`) so the block
  ends prog-aligned; that tail is later written as a fragment. The new bptr gets
  `cksize = pos_-block_pos`, `cksum = crc32c(block[0:cksize])` and `ISERASED` iff cksize is
  prog-aligned (13630-13638). Leaf marked UNGRAFT.
- Bad prog (`LFS3_ERR_CORRUPT` from prog/flush) ⇒ `relocate:` allocate a new block and
  rewrite from `block_pos` (13644-13662). **Broken when `block_pos` wraps, see B1.**

`lfs3_file_graft_(pos, weight, delta, datas, count)` (13097-13335): replaces
`[pos, pos+weight)` with `weight+delta` bytes of new data (fragments via `FROM_CAT`, or one
bptr via `FROM_BPTR`, or a hole). Carves left/right neighbours (GROW to shrink, or re-emit
as fragment when `≤ fragment_size && < max(crystal_thresh,1)`, else as a sliced bptr);
spans over several entries are committed one entry at a time (13213-13232). `pos == 0 &&
weight ≥ tree && delta == -weight` drops the whole tree (13106-13112). In-flight datas are
published in `lfs3->graft`/`graft_count` so the allocator masks them (13130-13131,
11190-11195).

Tree commit: `lfs3_file_commit` → `lfs3_bshrub_commit` (6919-6985) → `lfs3_btree_commit_`
(5582-6146) walks leaf→root; at a shrub root it returns `LFS3_ERR_EXIST` and the commit is
appended to the mdir as `LFS3_tag_SHRUBCOMMIT` (6819-6914, 8219-8238). Shrub → btree
eviction when the recomputed estimate > `shrub_size/2` or estimate+commit > `shrub_size`
(6851-6876) → `lfs3_btree_commitroot_` compacts the shrub into a new block (6151-6185).

### 1.5 Read path (12978-13073, 12914-12974)

- Asserts not WRONLY and `pos+size ≤ 0x7fffffff` (12983; see B14).
- Priority: dirty/clean fcache > leaf > tree > zeros. Reads ≥ fcache_size bypass the cache
  (13020-13032); smaller reads refill the cache when it is clean (13035-13045).
- If the leaf is UNCRYST/UNGRAFT or the cache is dirty and a tree lookup is needed, the read
  **flushes first** (13048-13058) → reads on a writable handle can program, allocate and
  fail with write errors; that also sets DESYNC [DOC h:1553-1560].
- Holes and past-end regions below size read as zeros (12947-12957, 13061-13066); reads at
  or past EOF return 0.
- With `LFS3_CKDATACKSUMS`, every bptr read checks `crc32c(block[0:cksize])` → read
  amplification up to one block per read (761-900).

### 1.6 Seek / tell / rewind / size (14665-14713)

- SET/CUR/END; result `> file_limit` → `LFS3_ERR_INVAL` (14684-14686). Negative results wrap
  above file_limit → INVAL [TEST test_fwrite_seek_negative]. Invalid `whence` →
  `LFS3_UNREACHABLE` (assert). "TODO check for out-of-range?" (14669).
- Seeking past EOF is allowed up to file_limit; a later write creates a hole
  [TEST test_fwrite_holes, test_fwrite_w_seek/rw_seek].
- `lfs3_file_size` returns the handle's view (incl. unsynced data); `lfs3_stat` returns the
  on-disk size = first leb128 of the struct (12035-12053).
- `lfs3_file_rewind` declared `int` (h:1640) but defined `lfs3_soff_t` (14700) — B9.

### 1.7 Truncate / fruncate (14716-14914)

`lfs3_file_truncate(size_)` sets the size from the end; growing creates a zero hole
[DOC h:1610-1618]. Does not move `pos` [TEST test_fwrite_truncate_pos].
`lfs3_file_fruncate(size_)` sets the size by cutting/growing at the **front**; grows insert a
leading hole; `pos` moves so it stays at the same offset relative to EOF, clamped at 0
(14902-14906) [DOC h:1620-1628, TEST test_fwrite_fruncate_pos]. Intended for logs
[DOC 14849-14851, commit c33182b].

Both: no-op if unchanged; `FBIG` if `size_ > file_limit` (+DESYNC); mark UNSYNC; graft
pending leaf; checkpoint allocator; one `graft_` with NULL data; then patch leaf/cache in
place. Truncate drops the erased state if the cut is inside the written part of the block
(14769-14776); fruncate keeps it (the block tail is untouched, `off` grows). Neither honours
O_SYNC/O_FLUSH (intentional per tests). Errors from the initial graft/ckpoint return
without DESYNC (B8).

### 1.8 Flush vs sync (14250-14608)

- `lfs3_file_flush`: writes the dirty cache, finishes crystallization, grafts the leaf. The
  tree changes are written (as SHRUBCOMMITs in the mdir or btree node commits) but **no
  struct tag** is updated → nothing becomes visible or durable [DOC h:1553-1560].
- `lfs3_file_sync`: zombie → return 0 (14558-14560). Small-file fast path when the whole
  file is in the cache and `≤ shrub_size`, `≤ fragment_size`, `< max(crystal_thresh,1)`
  (14568-14575): drop the leaf and build a fresh single-DATA shrub from the cache in the
  same mdir commit (14349-14374) — the whole small file is rewritten each sync [MEAS]. Else
  flush. Then `lfs3_file_sync_` (14305-14550): one mdir commit containing, as needed, the
  stickynote→REG conversion or an explicit name (lfs3_set), the SHRUBCOMMIT, the struct tag
  (RM / BSHRUB from staged `b_` / BTREE) and changed attrs (`tag_ATTRS`). A `lfs3_bd_sync`
  precedes the commit whenever data blocks may be involved (14376-14383); the mdir commit
  ends with another `lfs3_bd_sync` (9341-9347).
- Success clears DESYNC (14600); failure sets it (14604-14606).

**Cost of a small append + sync [MEAS meas.c]** — block 4096, crystal 256, fragment 256, shrub
1024, fcache 64, emubd, counted prog calls / progged bytes / erases / cfg->sync calls:

| scenario (16-byte append then sync) | prog_size 16 | prog_size 1 | prog_size 256 |
|---|---|---|---|
| tiny file inline (≤ fcache) | 3 progs, 176-224 B (≈ file size + ~130 B; whole file rewritten), 0 erases, 4 syncs | 167-215 B | – |
| bshrub file, same session, block tail erased | 1 data prog of the aligned 16 B into the **existing** block (no erase) + 2 mdir commits (SHRUBCOMMIT at flush, struct at sync) ≈ 464-480 B, 7 syncs; mdir log compaction every ~8 syncs (1 erase, ~800 B) | 4 progs, ~206 B, 4 syncs | no data prog until 256 B pending; each sync = 2-3 metadata commits padded to 256 B ≈ 768 B, 0 erases (periodic log/rbyd compaction) |
| btree file, same session | 16 B data + ~320 B leaf/root rbyd commits + ~96 B mdir ≈ 432 B, 0 erases, 2 syncs | | |
| after close+reopen (erased state lost) | appends become/extend a tail fragment (rewritten each sync, 224→304 B) until ≥ crystal_thresh; then crystallization **copies the partial last block** into a new block (1 erase + 1376 B) and later appends resume in place | | |
| after remount, 300-byte append (≥ crystal_thresh) | new block started at the append point (1 erase + 304 B), old partial block left as is (no copy, internal fragmentation) | | |

"Sync-padding problem" answer: v3 never re-programs a partially programmed data prog unit.
Data blocks are cut to a prog boundary and the tail lives as a metadata fragment; the in-RAM
bptr keeps `ISERASED + cksize/cksum` so later appends continue in the same block while the
previously synced bptr stays valid (its cksum covers only `[0, cksize)`). Costs that remain:
(a) every metadata commit is still padded to prog_size (dominant with large prog sizes);
(b) erased state of data blocks is **not persisted** (`lfs3_data_readbptr` never sets
ISERASED, 2352-2390; `lfs3_file_init` discards the leaf, 12510-12518), so after reopen/remount
the first crystallization either copies the partial block or starts a new one;
(c) if `crystal_thresh < prog_size` (allowed since 0698c49 for SD/eMMC) the tail is not cut
and the block is not marked erased (13608-13612, 13634-13637) → the classic padding cost
returns by design.

### 1.9 Sync model (snapshots, broadcast, desync/resync)

- Each handle is a snapshot: it owns its tree root, leaf and cache; nothing it does is
  visible to other handles or disk until its sync.
- **Broadcast** (14469-14541): after a successful sync every other REG handle with the same
  mid gets `UNCREAT` cleared; in-sync handles get the syncer's tree root, leaf (incl.
  ISERASED) and cache contents (tail if their cache is smaller) and their writable-by-others
  attrs; flags UNSYNC/UNFLUSH/UNCRYST/UNGRAFT cleared. Their own unsynced writes are
  **discarded** (last sync wins). Desynced handles are only marked UNSYNC so the allocator
  keeps their private blocks alive (10000-10020) [TEST test_fsync_* 50 cases].
- `lfs3_file_desync` sets DESYNC (14610-14620). `lfs3_file_resync`: zombie → NOENT; if
  UNSYNC, drop tree/cache/leaf and refetch from disk without re-truncating (14622-14661);
  clears DESYNC.
- Any error in write/flush/sync/truncate(FBIG)/fruncate(FBIG) sets DESYNC [DOC h:1568-1569].
  An explicit successful `lfs3_file_sync` re-syncs and commits whatever the handle holds
  (h:1571-1572) — see B5.
- `LFS3_O_DESYNC` at open = explicit-commit mode: only `lfs3_file_sync` writes; close
  discards [TEST test_stickynotes_undesync_*, test_fsync_desync_wrrd].
- Stickynote visibility (7925-7950): a STICKYNOTE is visible (type `LFS3_TYPE_STICKYNOTE`,
  size 0) iff some open handle holds the mid without ZOMBIE/DESYNC; otherwise it is an
  `ORPHAN` and every lookup reports NOENT (12316-12319, 12066, 12102). Pending grm mids are
  also ORPHANs.
- Zombies: `lfs3_remove`/rename-over of an open file rewrites the entry as a stickynote and
  marks handles `UNCREAT|ZOMBIE|UNSYNC` (11762-11800); `lfs3_file_sync` is then a silent no-op,
  resync → NOENT [TEST test_stickynotes_zombie_*].
- Cleanup: `lfs3_mdir_mkconsistent` removes stickynotes not held by any handle (16540-16585),
  run by `lfs3_fs_mkconsistent` (first writable op after mount, 12594) or gc.

### 1.10 Power-loss guarantees

| state at power loss | after remount [IMPL/TEST] |
|---|---|
| returned from `lfs3_file_sync`/`close` (non-desync) | file content, size and written attrs exactly as of that sync (single atomic mdir commit, data `bd_sync`ed first) [TEST test_powerloss_spam_f_pl_fuzz: every surviving file has size SIZE and a valid checksum; test_files_pl_fuzz; test_relocations_spam_f_pl_fuzz; test_grow_incr_spam_f_pl_fuzz] |
| written/flushed but not synced (incl. O_FLUSH) | previous synced content; new data blocks are unreferenced (reclaimed by the traversal-based allocator); unsynced shrub tags in the mdir are unreachable [MEAS img.c: flushed 5000-byte file shows as bare stickynote] |
| created, never synced | invisible (orphan), removed by mkconsistent [TEST test_stickynotes_uncreat_pl, _uncreat_many_pl, _undesync_pl, _undesync_many_pl] |
| `lfs3_set` of a new small file | created atomically in one commit (no stickynote, 12707-12724); larger/new → stickynote then sync (atomic from the user's view) [not PL-tested] |
| O_TRUNC + partial rewrite | old content [TEST test_powerloss_spam_f_pl_fuzz uses O_TRUNC] |
| O_SYNC write returned | durable (sync ran), truncate/fruncate not |

Tested emulated power-loss models: ATOMIC (default everywhere), SOMEBITS, MOSTBITS, OOO
(out-of-order bd writes, checks the `bd_sync` ordering) — the last three only in
test_powerloss.toml.

### 1.11 File-attached attrs vs sync (h:761-807; 12535-12578, 14412-14453, 8326-…)

- `lfs3_file_cfg.attrs`: readable attrs are loaded at open (UNCREAT → size = NOATTR) and kept
  current by broadcasts and by `lfs3_setattr`/`lfs3_removeattr` on in-sync handles
  (12391-12412, 12442-12462). Writable attrs are committed in the same mdir commit as the
  file sync: always when the file is UNSYNC, otherwise only non-LAZY attrs that differ from
  disk (14417-14447); the commit layer re-compares each attr and skips equal ones (8337-8356).
- `LFS3_A_LAZY` = write only when the file itself changed [TEST test_attrs_fattr_lazy].
- Attr access mode is independent of file mode → a RDONLY file can write attrs (B3).

### 1.12 Key-value API (14990-15072)

- `lfs3_get(path, buf, size)` → bytes read (≤ size) or error; `lfs3_size(path)` → size.
  Both open RDONLY with a dummy cache (`fcache_buffer=(uint8_t*)1, fcache_size=0`,
  14997-15001, "TODO is this the best way") so all reads bypass the cache. Uncreated
  (visible) stickynote → empty file, not NOENT.
- `lfs3_set(path, buf, size)` → 0 or error. Opens with `o_WRSET|O_CREAT|O_TRUNC` using the
  caller's buffer as the file cache (15057-15064). New + small (`≤ shrub_size`,
  `≤ fragment_size`, `< crystal_thresh`) → one commit creating REG+data; otherwise normal
  sync in close. Broadcasts to open in-sync handles [TEST test_kv_interop_sync].

### 1.13 `lfs3_file_ck(flags)` (14918-14986)

Only `CK_CKMETA|CK_CKDATA`. Traverses the handle's tree; CKMETA/CKDATA re-fetch every branch
rbyd with checksum; CKDATA also checks every bptr's `crc32c(block[0:cksize])` and the
ungrafted leaf. Returns `LFS3_ERR_CORRUPT` on mismatch [TEST test_ck_file_ckmeta/ckdata_easy/
_hard]. Inline fragments are protected only by their rbyd checksum.

### 1.14 file_limit (h:660-665; 15237-15242, 15724-15748)

cfg 0 → `LFS3_FILE_MAX`; must be ≤ `LFS3_FILE_MAX` (assert); written to the superblock at
format; mount fails NOTSUP if the on-disk limit > configured, then adopts the on-disk value.
Enforced by write (14123), seek (14684), truncate/fruncate (14729, 14819). Not enforced for
O_APPEND writes after a seek/rewind (B2).

---------------------------------------------------------------------------------------------

## 2. On-disk format (this area)

### 2.1 Tag framing (reference; owned by the rbyd area)

`be16 tag | leb128 weight (≤5 B) | leb128 size (≤4 B)` then `size` payload bytes
(h:957-962; 1462-1507). Bit 15 of the tag = "valid" bit = parity(running cksum) XOR perturb,
excluded from the checksum (1478-1482, 1446-1448). Bit 7 reserved (1469). Tags written into a
shrub carry `0x1000` (4361-4368). Top two bits of leb128 fields limited: weights 31 bits,
sizes 28 bits.

### 2.2 File struct tags in the mdir entry (rid of the file)

| tag | value | payload | notes |
|---|---|---|---|
| `BSHRUB` | 0x0428 (`+-1- 1-rr`) | `leb128 weight` · `leb128 trunk` (≤ 9 B, h:1057-1064) | trunk is a byte offset of a shrub trunk **inside the same mdir block**; no cksum (the mdir commit covers it); weight = file size (6469-6524) |
| `BTREE` | 0x042c (`+-1- 11rr`) | `leb128 weight` · `leb128 block` · `leb128 trunk` · `le32 cksum` (≤ 18 B, h:1044-1055) | cksum = rbyd checksum at that trunk, verified on fetch (5247-5327) |
| none | – | – | zero-size file |

"size is always the first field" (12046) — `lfs3_stat` reads only the first leb128.
Zero-weight BSHRUB/BTREE are never written but must be readable [TEST
test_files_zero_bnull/_bshrub/_btree]. A zero shrub trunk trips an assert (6519).

### 2.3 Leaf tags (inside a shrub or a btree leaf rbyd)

| tag | value (in shrub) | rbyd weight | payload |
|---|---|---|---|
| `DATA` (fragment/hole) | 0x0404 (0x1404) | bytes covered | raw bytes, `size ≤ weight` (`size==0` ⇒ hole) |
| `BLOCK` (bptr) | 0x0408 (0x1408) | bytes covered | `leb128 size` · `leb128 block` · `leb128 off` · `leb128 cksize` · `le32 cksum` (≤ 21 B, h:1029-1042, 2305-2390) |

bptr semantics: file bytes `[bid-weight+1, …]` map to `block[off, off+min(size,weight))`;
`cksum = crc32c(block[0:cksize])` (CRC-32C Castagnoli, init/xorout 0xffffffff, s:dbglfs3.py
332-341; 2455-2477); `off+size ≤ cksize ≤ block_size` expected but **not validated on
decode** (B13). `s:dbglfs3.py` legend marks BLOCK as `+--- 1err`: the `e` bit (0x0004) is
unused by the C code [? possibly reserved for persisted erased-state]; the `rr` bits of
struct tags are "redund" bits (1055-1057) [owned by other area].

### 2.4 Inner nodes of file btrees

`BRANCH` 0x0400 (`+--- --rr`), rbyd weight = subtree weight, payload `leb128 block ·
leb128 trunk · le32 cksum` (≤ 13 B, h:1018-1027, 5145-5201). File trees are unnamed: no
`BNAME` 0x0300 is emitted because split names are only emitted when the first tag of the
right sibling is a NAME (6043-6048).

Node policy (5736-5859): on overflow compact; split if compacted estimate
`> block_size/2`; try merging with a sibling if `≤ block_size/4` and the sum
`< block_size/2`; degenerate roots collapse (5697-5702, 6110-6118).

### 2.5 Worked example [MEAS img.c + `scripts/dbglfs3.py -b4096 --structs -x`]

prog_size 16. `shrubblocks` = 2·4096+1000 bytes → `bshrub 0x{1,0}.d8` with entries
`0-4095 shrubblock 0x3.0 w4096 4096`, `4096-8191 shrubblock 0x4.0`, `8192-9183 shrubblock
0x5.0 w992 992`, `9184-9191 shrubdata w8 8` (the 8-byte unaligned tail became a fragment).
First entry bytes: `14 08 80 20 0a | 80 20 03 00 80 20 14 7f c6 15` = tag 0x1408
(SHRUB|BLOCK), weight 4096, size 10 | size 4096, block 3, off 0, cksize 4096, cksum
0x15c67f14. Btree root branch `84 00 c8 33 07 | 07 a2 0a 8f 43 bc 0d` = tag 0x8400 (v=1,
BRANCH), weight 6600, size 7 | block 7, trunk 0x522, cksum. Leaf fragment `84 04 d8 04 64`
= DATA, weight 600, size 100 (100 bytes + 500-byte hole). `sparse` (10 bytes at 100000) =
`0-99999 shrubdata w100000 0` + `100000-100009 shrubdata w10 10`.

### 2.6 Things only in RAM (must not be specified as on-disk)

`ISERASED` bit in `cksize` (2213), `LFS3_DATA_ONDISK/ISBPTR` bits in `size` (1513-1514),
`LFS3_RBYD_ISSHRUB` in trunk (2583, 6522), `GRAFT_ISBPTR` (13085), shrub estimate kept in
`eoff` (6853-6855, 6896-6911).

---------------------------------------------------------------------------------------------

## 3. Invariants and power-loss argument

I1. **Size = weight.** The root weight equals the file size; the entries' weights tile
`[0,size)` with no gaps; weight changes are always balanced within one `graft_` call and the
tree size "must never overflow, even temporarily" (13100-13101). A graft spanning several
entries is split into commits, so the in-RAM tree size can temporarily be *smaller* (B5).

I2. **Copy-on-write data with append-only reuse.** A data block referenced by any on-disk or
in-RAM bptr is only programmed at offsets ≥ its current `cksize`, only when the programming
handle holds ISERASED, and only after clearing ISERASED in every other handle
(13398-13423). Old references stay valid because their cksum covers only `[0, cksize)`.
ISERASED is only set on a prog-aligned cksize (13634).

I3. **Nothing on disk points at unwritten data.** Order: data progs → pcache flush
(13615) → `bd_sync` (14379) → single mdir commit with checksum → `bd_sync` (9344). The only
reachability root for a file is the struct tag in its mdir entry, rewritten only by
`lfs3_file_sync_`. Shrub commits during flush and btree node commits are unreachable until
that tag changes (6882-6890, 8219-8238). ⇒ after power loss a file is exactly its last
synced version (atomicity per file per sync).

I4. **Allocator never hands out live blocks.** Blocks are only allocated after
`lfs3_alloc_ckpoint` (13349, 13676, 13710, 13998, 14744, 14834); between checkpoints nothing
is reallocated (2178-2184). The lookahead scan traverses the mtree plus every UNSYNC handle's
tree and its ungrafted leaf (9989-10023) and masks the in-flight graft datas (11190-11195).
Desynced snapshots get UNSYNC on the first foreign sync (14479-14481), zombies get it at
remove, so their private blocks are protected while open.

I5. **Shrub size is bounded.** shrub_size ≤ block_size/4 and fragment_size ≤ block_size/4
(15149-15154). Per mid, the sum of all open handles' shrubs is estimated
(6731-6774) and evicted to a btree when it exceeds shrub_size/2 or would exceed shrub_size
with the commit (6864-6876); mdir estimates include opened unsynced shrubs (8517-8540) so
mdir compaction/split accounts for them; compaction copies both referenced shrubs and those
of open handles (8605-8676, 8304-8323).

I6. **One commit fits.** "bshrubs are always converted to btrees first, which can't fail
(shrub < 1/2 block + commit < 1/2 block)" (6932-6937); `graft_` uses ≤ 10 rattrs (13135,
13220), `sync_` ≤ 14 (14308, 14459).

I7. **Stickynote lifecycle.** A mid created by a writable open is either REG (after first
sync), a visible stickynote (held by a non-desync non-zombie handle), or an orphan removed
by the grm queue or mkconsistent. Readers never see orphans (7925-7950).

I8. **Relocation on bad prog preserves content** by rewriting the whole crystal from
`block_pos` out of buffer + leaf + tree (13644-13662, commit 664d99d). **Violated when
`block_pos` wraps (leaf.pos < bptr off after fruncate) — B1.**

I9. **Errors never touch disk state.** Every write-side error leaves disk at the last synced
state; the handle is marked DESYNC so close does not commit it. The handle's RAM view may be
partially updated (B5).

---------------------------------------------------------------------------------------------

## 4. Configuration knobs

| knob | where | valid / default | effect in this area | tests |
|---|---|---|---|---|
| `fcache_size` | h:547-550; per file `lfs3_file_cfg.fcache_size/buffer` h:789-797 | any; not validated; 0 w/o buffer → `malloc(0)` (NULL on some libcs → NOMEM) | write coalescing, read cache, bypass threshold, small-file sync limit | default 16 (runners/test_defines.h:17), 64 in files tests |
| `shrub_size` | h:670-677 | 0..block_size/4 (asserted 15151); 0 disables shrubs | max inline tree, eviction at /2, small-file limit | default BLOCK_SIZE/4; 0 in test_powerloss, test_fwrite litmus, test_ck, test_trvs |
| `fragment_size` | h:679-684 | ≤ block_size/4 (asserted 15153); **0 not rejected but hangs** (B12, t:test_fwrite.toml:4 "TODO should fragment_size accept 0?") | max inline leaf | default min(BLOCK_SIZE/16,512); 1/16/64 in test_fwrite; BLOCK_SIZE/8 in test_ck/test_trvs |
| `crystal_thresh` | h:693-702 | any; 0 ≈ 1; -1 or > block_size → fragments only (13703); < prog_size allowed (disables erased-state reuse); < fragment_size discouraged (commit 8cc91ff) | when fragments become blocks | default BLOCK_SIZE/16 (was /8, commit 0ea11c1); 512 and -1 in test_fwrite; **0 and < prog_size never tested** |
| `prog_size` | h:509-513 | block_size multiple | alignment of crystals, ISERASED, metadata padding | default 1; 16 in test_fwrite; 64 in *_compaction tests; 256 never |
| `pcache_size`/`rcache_size` | h:535-545 | multiples of prog/read size | pcache is manipulated directly by the tail-cut hack (13608-13612) | |
| `file_limit` | h:660-665 | 0 → LFS3_FILE_MAX; ≤ LFS3_FILE_MAX | FBIG/INVAL | only default in file tests; custom in test_mount/test_grow |
| `LFS3_FILE_MAX` | h:69-74 | ≤ 2^31-1 | compile-time cap | test_fwrite_fbig |
| `LFS3_M_FLUSH/M_SYNC` | h:246-247 | mount flags | implies O_FLUSH/O_SYNC | test_fsync SYNC/FLUSH=3 |
| `LFS3_CKDATACKSUMS` | compile + M_ flag | | verify bptr cksum on every read | test_ck_* |
| `LFS3_CKFETCHES` | compile + M_ flag | | verify bptr/branch on fetch (2441-2449, 5215-5226) | test_ck_* |
| `LFS3_BLEAFCACHE` | compile | | caches last btree leaf rbyd (h:1160-1168) | not referenced by any test, the Makefile or .github/workflows/test.yml (compiles cleanly with clang -fsyntax-only) |
| `LFS3_RDONLY`, `LFS3_NO_MALLOC` | compile | | drop write path / `lfs3_file_open` | |
| `LFS3_DBGBTREECOMMITS` | compile | | **does not compile** (B6) | |

Header TODOs about defaults: h:667-668 "TODO these are pretty low-level details, should we
have reasonable defaults? need to benchmark." (shrub_size, fragment_size, crystal_thresh
all default to 0 when the cfg is zeroed, i.e. shrubs disabled, fragment_size 0 → B12,
crystal_thresh 0 → always crystallize); h:686-688 "crystal_thresh=0 really just means
crystal_thresh=1 … block_size/16 or block_size/8 is probably a better default";
h:690-691 "should probably just assert if crystal_thresh < fragment_size, or if
crystal_thresh < prog_size". Note a zero-initialised `lfs3_cfg` therefore gets a
non-working fragment_size — the requirements doc should demand explicit defaults or
validation.

---------------------------------------------------------------------------------------------

## 5. Test coverage map

Suites (cases): test_btree 50 (all `in=lfs3.c`, none reentrant), test_files 31
(4 reentrant), test_fwrite 41 (0 reentrant), test_fsync 50 (0 reentrant),
test_stickynotes 67 (4 reentrant), test_powerloss 4 (all reentrant), test_kv 18
(0 reentrant). Reentrant cases run under the "linear" powerloss runner (power cut after
every bd op). Bad-block/error injection comes from test_badblocks, test_exhaustion,
test_ck (fuzzers stop at the first error and close/desync).

| feature | tests | power loss | bad blocks / errors |
|---|---|---|---|
| open flags, errors | files_create/_noent/_excl/_trunc/_file_not_dir/_dir_not_file/_root_not_file/_noent_not_file; stickynotes_uncreat_excl/_orphan_excl | files_many (CREAT only) | – |
| O_APPEND | fwrite_incr(_litmus_*), fsync_sync_wwrr_append, desync/resync *_append | – | – |
| small/inline files, shrub, bptr, btree shapes | files_more, files_many, fwrite_simple(_litmus_*), fwrite_incr_litmus_*, files_mv_split(_backwards) | powerloss_spam_file_many/f_pl_fuzz (SHRUB_SIZE BLOCK/4 and 0) | badblocks_every_btree_many, *_spam_fwrite_fuzz |
| B-tree algorithms (push/update/pop/split/drop/merge/find/traverse) | test_btree_* (50) | none | badblocks_every_btree_many |
| fragments vs crystallization, prog alignment | fwrite suite over FRAGMENT_SIZE{1,16,64} × PROG_SIZE{1,16} × CRYSTAL 512; *_litmus_fragments/_blocks; bigger_than_expected_* | none | badblocks spam_fwrite (prog_size 1 only) |
| random overwrite, holes, reverse writes, compaction | fwrite_overwrite, _holes, _reversed, _freversed, _overwrite_compaction, _hole_compaction, _clip_cache/_leaf/_hole, _fuzz_aligned/_unaligned, _rwtf_fuzz | none | ck_spam_fwrite_fuzz, badblocks *_spam_fwrite_fuzz, exhaustion_spam_fwrite_fuzz |
| seek (SET/CUR/END, negative, past EOF) | fwrite_r_seek/_w_seek/_rw_seek/_seek_negative | none | – |
| truncate | fwrite_truncate(_truncate/_pos/_litmus_zero/_litmus_fragment/_fbig), fsync_rwtf*_sparse_fuzz | **none** | badblocks/ck/exhaustion spam_fwrite (truncate only) |
| fruncate | fwrite_fruncate(_fruncate/_pos/_litmus_*/_fbig), fwrite_freversed(_litmus_*), fwrite_rwtf_fuzz, fsync_rwtf*_sparse_fuzz | **none** | **none** |
| FBIG / file_limit | fwrite_fbig, _truncate_fbig, _fruncate_fbig (LFS3_FILE_MAX only) | – | – |
| flush vs sync, O_FLUSH/O_SYNC, M_FLUSH/M_SYNC | fsync_sync_o_*, wrrr, wwww, wwrr, rwrw (+fuzz), fwrite_* SYNC=[0,1] | only via state-file sync in pl fuzzers | – |
| multi-handle broadcast | fsync_sync_wrr/_wwrr(_zero/_noop/_append), rrrr, wrrr, wwww, wwrr, rwrw(+_sparse) | none | – |
| desync / O_DESYNC / resync | fsync_desync_*, drrr, wddd, rwdrwd, resync_*, yrrr, wyyy, rwdy*/rwtfdy* sparse fuzz; kv_interop_desync/_resync; attrs_fattr_desync_no_receive/_resync_receive | stickynotes_undesync_pl | ck/badblocks/exhaustion *_uz(d)_fuzz |
| stickynotes, orphans, zombies, cleanup | test_stickynotes (67): uncreat_*, undesync_*, orphan_*, zombie_*, zombify_*, fileonzombie_*, cleanup_*, file_mv_*, uz/uzd_fuzz | uncreat_pl, uncreat_many_pl, undesync_pl, undesync_many_pl | badblocks/ck/exhaustion/relocations *_uz(d)_fuzz |
| sync after power loss (atomic file replace) | – | powerloss_spam_f_pl_fuzz / fd_pl_fuzz (ATOMIC/SOMEBITS/MOSTBITS/OOO), files_pl_fuzz, relocations_spam_f(d)_pl_fuzz, grow_incr_spam_f(d)_pl_fuzz | – |
| file attrs + sync | attrs_fattr_* (26) incl. _lazy, _broadcast, _setattr_broadcast, _wronly_no_receive, _rdonly_no_broadcast, _zombie_* | attrs_fattr_pl_fuzz_fuzz | – |
| kv API | kv_set(_trunc/_noent/_update/_zero/_null), kv_remove, kv_many(_big), kv_fuzz(_big), kv_interop_* | **none** | – |
| lfs3_file_ck / O_CK* | ck_file_ckmeta/ckdata_easy/_hard | – | ck_spam_* |
| traversal/allocator protection of open files | trvs_clobber_files(_opened), trvs_mutation_* (fwrite, bsprout/btree/bshrub, uncreat, close, rm, mv, mroot/mtree split/extend/relocate), trvs_mkconsistent_* | – | – |
| NOSPC during file write | alloc_nospc_files (expects close → 0 after write NOSPC) | – | exhaustion_* |

Untested or weakly tested (explicit list):

1. Any **power-loss** test of: truncate, fruncate, random overwrite/seek writes, append that
   resumes crystallization after a sync, desync/resync, O_SYNC/O_FLUSH, `lfs3_set`
   (kv suite has no reentrant case), multiple handles syncing. PL file coverage is whole-file
   `O_TRUNC`+write+close plus an 8-byte in-place state-file rewrite+sync; test_files_many
   limits PL runs to `SIZE*N ≤ 1 block` (t:test_files.toml:750-752).
2. **Bad block appearing after data was written** (block good at first prog, bad at resumed
   prog). All badblock fuzzers mark blocks bad before the test; wear only changes on erase,
   so the resume-relocation path in `lfs3_file_crystallize_` is effectively untested — B1
   survived because of this. No fruncate under any error injection.
3. `prog_size` > 64, especially prog_size == block_size (SD/eMMC) and `crystal_thresh <
   prog_size` (the case 0698c49 added); `crystal_thresh = 0`; `fragment_size = 0`;
   `fragment_size = block_size/4`; `shrub_size` values other than BLOCK/4 and 0; fcache_size
   0 / very large; per-file `fcache_size` override outside kv.
4. Custom `file_limit` smaller than FILE_MAX in write/seek/truncate; O_APPEND + FBIG (B2);
   successful writes ending at the limit (only my [MEAS]).
5. Sync/attr writes through RDONLY handles and on `LFS3_M_RDONLY` mounts (B3).
6. Continuing to use a handle after a write error and then calling `lfs3_file_sync` (B5).
7. Erased-state behaviour across close/reopen/remount (performance only; no litmus test
   asserts block reuse or copy counts), and op-count/cost regression tests for sync.
8. `LFS3_BLEAFCACHE` and `LFS3_DBGBTREECOMMITS` builds.
9. Two handles both holding ISERASED for the same block and alternately appending (claim
   logic 13415-13423) — only indirectly via fsync fuzzers with SIZE up to 4 blocks.
10. Very large sparse files near 2^31 (reads/writes only exercised by fbig failure path).

---------------------------------------------------------------------------------------------

## 6. TODO / FIXME / FUTURE in this area

lfs3.h: 667-668 (knob defaults), 686-688 (crystal_thresh=0), 690-691 (assert
crystal_thresh ≥ fragment_size/prog_size).

lfs3.c:
- 5403, 5511, 6302 — "TODO how many of these should be conditional?" (btree lookup outputs)
- 5458 — "TODO should lfs3_btree_lookupnext/lfs3_btree_parent be deduplicated?"
- 6902-6905 — "TODO bit of a hack … make sure estimate is not clobbered on redundant shrub
  sync" (shrub estimate stored in `b_.eoff`)
- 8246 — "TODO can this be deduplicated with lfs3_mdir_compact___" (MOVE copies shrubs)
- 8449, 8727 — "TODO do we need to include commit overhead here?" (mdir estimate margins
  that bound shrub growth)
- 10026 — "TODO is this correct?" (mtree traversal mid advance, which is also where unsynced
  file trees are enumerated for the allocator)
- 13514 — "(FUTURE) better leverage erased-state in sparse files"
- 14669 — "TODO check for out-of-range?" (seek)
- 14998 — "TODO is this the best way to do this?" (kv dummy cache buffer)
- 5549-5580 — design warning: non-bid-0 name updates must go via splits (named btrees; not
  used by files).

Tests: t:test_fwrite.toml:4 "TODO should fragment_size accept 0?"; :833 "TODO this is too slow
right now"; :3760, :3951 "TODO is setting PROG_SIZE here reasonable?"; t:test_files.toml:751,
:1434, :2405 "TODO can this be increased after optimizing file writes?" (PL size limits);
:775 etc. "TODO remove this eventually?"; t:test_btree.toml:2450 "TODO rm".
Stale define: t:test_fwrite.toml:12 `FRAGMENT_THRESH = [-1]` (no such cfg field any more).

Unimplemented / reserved: rcompat `BMOSS`, `BSPROUT` (h:934-935) never produced or read;
persisted data-block erased state (13514, BLOCK tag `e` bit in s:dbglfs3.py:64 [?]);
"compression" (weight < size) only tolerated on read (2436-2437).

---------------------------------------------------------------------------------------------

## 7. Suspected bugs and risks (nothing fixed)

**B1 — CONFIRMED, high: fruncate + bad prog during resumed append loses data and corrupts the
file.** After `lfs3_file_fruncate` the leaf keeps its physical block alignment, so
`block_pos = leaf.pos - bptr.off` wraps below 0 (unsigned). In `lfs3_file_crystallize_` the
`relocate:` path sets `pos_ = block_pos` (13656-13658) and the fill loop
`while (pos_ < crystal_limit)` (13437) never runs, so nothing is copied; `flush_` then
advances by `max(leaf.pos+size, pos) - pos` ≈ 2^32 and reports success. Reachable from the
resume paths (13738, 13875) and `lfs3_file_crystallize` (13682). Repro [MEAS frbad.c]:
prog 16, write 2000 B, sync, fruncate to 1900, sync, `lfs3_emubd_mkbad(data block)`
(PROGERROR), append 64 B → write returns 64; debug build then asserts
`!lfs3_o_isuncryst` at 13690 during flush; with `LFS3_NO_ASSERT` flush and sync return 0,
`lfs3_file_size` = -100 and after close `lfs3_get` returns `LFS3_ERR_CORRUPT` (persistent).
Same scenario without fruncate relocates correctly (copies the block to a new one), and
fruncate without the bad block works — the bug is specific to wrapped `block_pos` +
relocation. Introduced by the intent of c33182b (physical block alignment for logs).

**B2 — CONFIRMED, medium: O_APPEND bypasses file_limit / gives spurious FBIG.**
The FBIG check `size > file_limit - file->pos` (14123) runs before `pos` is replaced by the
file size for O_APPEND (14133). [MEAS flim.c, file_limit 1000]: write 900, rewind,
append 500 → returns 500, size 1400. Seek to 1000 on an empty O_APPEND file then write 10
→ FBIG although the write would land at 0. With the default limit this lets a file exceed
2^31-1 (31-bit weight asserts in encoders, 5255/6476). Untested.

**B3 — CONFIRMED, medium: `lfs3_file_sync` writes through RDONLY handles, even on a
RDONLY mount.** No RDONLY guard in `lfs3_file_sync` (14553-14608) and attr modes are not
checked against the file mode at open (12810-12816). [MEAS rdsync.c]: (a) RDONLY handle,
desync, another handle writes "NEW" and syncs, `lfs3_file_sync(rdonly)` → 0 and disk
reverts to "old-contents"; (b) RDONLY file with an `LFS3_A_RDWR` attr → sync writes the attr;
(c) same on `LFS3_M_RDONLY` mount → one prog issued to the device. Untested.

**B4 — CONFIRMED (documented behaviour, usability risk).** Any write error including the
argument check FBIG sets DESYNC (14124-14125, 14243-14246); close of a desynced handle
returns 0 without syncing (12876-12879). [MEAS flim.c]: new file, write 900 (ok),
write 200 → FBIG, close → 0, file does not exist (NOENT). Tested only as "file unchanged"
(test_fwrite_fbig, test_alloc_nospc_files expect `close => 0`). Requirements must state that
close cannot report data loss after an earlier error.

**B5 — SUSPECTED, medium: partially applied writes can be persisted.** A graft spanning
several entries commits each carve separately (13213-13232), temporarily shrinking the
in-RAM tree; an error in a later commit leaves the handle's tree shifted. The handle is
DESYNC, but the API explicitly allows `lfs3_file_sync` to re-sync it (h:1571-1572), which
would commit the inconsistent view. Not reproduced; fuzzers stop at the first error.

**B6 — CONFIRMED, low: `LFS3_DBGBTREECOMMITS` does not compile.** 6969-6983 use
`bshrub->b.weight/blocks/cksum` (members of `.r`) — 4 errors with `clang -fsyntax-only
-DLFS3_DBGBTREECOMMITS`.

**B7 — CONFIRMED warning, low: branch encoded into an 8-byte buffer.** `FROM_BRANCH`
passes `ctx.u.ecksum.buf` (LFS3_ECKSUM_DSIZE = 8) to `lfs3_data_frombranch(…, uint8_t
buffer[static 13])` (3505-3511); clang warns "array argument is too small". Benign today only
because the union is larger and `branch.buf` sits at the same offset; UB by the C standard
and fragile under sanitizers / layout changes.

**B8 — low: inconsistent DESYNC on truncate/fruncate errors.** Errors from
`lfs3_file_graft`/`lfs3_alloc_ckpoint` return directly (14738-14747, 14828-14837) instead of
`goto failed`, contrary to h:1568-1569. Consequence: close retries the sync.

**B9 — low: `lfs3_file_rewind` prototype mismatch** (`int` h:1640 vs `lfs3_soff_t`
14700) — conflicting types where `int32_t` is `long`.

**B10 — low: `lfs3_file_opencfg_` is a non-static, undeclared global symbol (12590).**

**B11 — low: attr assert reuses file-flag helpers** (12810-12816): `LFS3_A_LAZY` (0x04) is
the `O_CREAT` bit, so an attr with `A_RDONLY|A_LAZY` asserts "require a writable attr".

**B12 — medium (config robustness): knobs not validated.** `fragment_size = 0` → fragment
loop computes `d = fragment_end - pos = 0` (14090) and never progresses while committing
each iteration (hang + wear) whenever data takes the fragment path; a zeroed cfg has
fragment_size 0. `crystal_thresh` has no checks (TODOs h:686-691). `fcache_size = 0` →
`lfs3_malloc(0)`.

**B13 — low/medium (untrusted images): decoded pointers not range-checked.**
`lfs3_data_readbptr`/`readbranch`/`readshrub` (2352-2390, 5174-5201, 6499-6524) accept any
block/off/cksize; the bd layer only asserts (204-205). Unexpected leaf tags hit
`LFS3_ASSERT`/`LFS3_UNREACHABLE` (12899, 2433) — `__builtin_unreachable` in NO_ASSERT builds.
A checksummed-but-malformed image can therefore crash or read out of bounds.

**B14 — low: `lfs3_file_read` asserts `file->pos + size <= 0x7fffffff` (12983)** instead of
clamping; a large "read everything" size aborts in debug builds.

**B15 — info/perf:** block-alignment heuristic applies `lfs3_bptr_off` to fragment
neighbours (13932-13943), where `off` is an rbyd offset, so alignment for fragment neighbours
is arbitrary [MEAS: after remount a 300-byte append started a fresh block rather than
copying the 1.5 KiB partial block, while small appends after reopen did copy it].

**B16 — info:** allocator masking reads `graft[i].u.disk.block` for in-RAM buffers too
(11191-11195) → harmless false "in use" marks.

**B17 — info (cross-ref metadata area):** the final `lfs3_bd_sync` failure in
`lfs3_mdir_commit_` returns without `lfs3_fs_revertgdelta` (9344-9347), unlike every other
failure path (`goto failed`, 9464-9467).

**B18 — info:** each small append+sync on a bshrub file issues 7 `cfg->sync` calls and 2
metadata commits [MEAS]; with prog_size 256 a 16-byte durable append costs ~768 programmed
bytes. Worth a performance requirement or an explicit non-goal.

**B19 — info (cross-area):** `.github/workflows/test.yml` is still v2-era: the size jobs
pass `-DLFS_NO_ASSERT`/`-DLFS_READONLY` (115-139) and build `lfs.*.csv` targets that the v3
Makefile names `lfs3.*.csv` (Makefile:393-400); line 354 uses `-DLFS_NO_INTRINSICS`. Tests
always run with asserts on, so release-mode (NO_ASSERT) behaviour — e.g. B1's silent
corruption instead of an assert — is never exercised.

Risk notes for the requirements/test plan:
- Durability of unsynced data is **never** promised (flush ≠ durable).
- Last-sync-wins across handles; no merge; a desynced handle's explicit sync overwrites
  newer data (B3a shows it also works from RDONLY handles).
- Reads on writable handles may write (and fail with NOSPC/IO), and set DESYNC.
- Erased-state reuse is an in-RAM optimisation lost on close/remount.
