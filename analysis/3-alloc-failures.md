# 3 — Block allocation and flash-failure handling (littlefs v3-alpha)

Scope: lookahead allocator, global on-disk block map (gbmap), pre-erase,
incremental GC and traversal-driven GC, flash-error handling
(prog/erase/read), relocation, wear leveling, exhaustion/NOSPC, fs_grow,
plus a proposal for persistent bad-block tracking.

Baseline: `v3-work` @ b10efaa. The working copy moved to `v3-fixes`
(e4c046b) while this analysis ran; that diff only touches lfs3.c:1736,
lfs3.c:1808, lfs3_util.h:498-502 and tests/test_grow.toml. Every line
number below is the same in both commits.

Legend used throughout:
- **[D]** documented intent (lfs3.h comments, code comments, commit messages)
- **[I]** implemented behaviour (read from code)
- **[T]** tested behaviour (named test case)
- **UNTESTED** means no test case exercises it
- **(?)** means the claim is uncertain; the reason is given
- `file:line` refers to lfs3.c unless another file is named

---

## 0. Test reality check (read first)

1. **The default build compiles none of the optional features.** A plain `make test`, which is what the
   background run uses (see `.local/linux/build.log`), defines none of `LFS3_GC`, `LFS3_GBMAP`,
   `LFS3_PREERASE`, `LFS3_REVPERTURB`, `LFS3_CKPROGS`, `LFS3_CKFETCHES`, `LFS3_CKMETAPARITY` or
   `LFS3_CKDATACKSUMS`. lfs3_util.h:26-114 enables them only through `LFS3_BIGGEST` or `LFS3_YES_*`,
   and the Makefile forwards `LFS3_*` variables at Makefile:102. test.py filters cases by `ifdef` at
   compile time (scripts/test.py:478-487). In the default build, therefore:
   - test_gbmap is skipped entirely (suite `ifdef = 'LFS3_GBMAP'`).
   - Every test_gc case that calls `lfs3_fs_gc` is skipped (`ifdef = 'LFS3_GC'`).
   - Every preerase test is skipped.
   - The `GBMAP=true` permutations of test_alloc, test_grow, test_gc, test_trvs and test_mount are
     removed by `if = LFS3_IFYES_GBMAP(GBMAP, true, !GBMAP)`.
   - Every badblock or exhaustion permutation with `CKPROGS=true` is removed by
     `if = LFS3_IFDEF_CKPROGS(true, !CKPROGS)`. That permutation covers READERROR, PROGNOOP and
     ERASENOOP. **In the default build, bad-block recovery is tested only for PROGERROR and ERASEERROR.** The one exception is
     `test_badblocks_mrootanchor_format`, which has no CKPROGS gate and so covers format failure for all 5
     behaviours.
2. **CI does not run v3.** `.github/workflows/test.yml` is still the v2 file: it uses `LFS_*` macros and
   builds nothing with `LFS3_*` features. No CI job runs the GBMAP, PREERASE, GC or CKPROGS
   configurations.
3. **The CKDATACKSUMS build does not compile at b10efaa.** The code uses `data.u.disk.block` on a
   pointer at lfs3.c:1736 and lfs3.c:1808. Another agent fixed this in e4c046b, so every
   `LFS3_CKDATACKSUMS` test was unrunnable at the baseline.
4. **Powerloss coverage is narrow.** The runner's default powerloss schedule is `none` plus `linear`,
   one loss per write op (runners/test_runner.c:2111-2116). Every non-ATOMIC behaviour
   (SOMEBITS, MOSTBITS, OOO) is used only by the 4 cases in test_powerloss.toml. **METASTABLE is used
   by no test anywhere.**
5. **Geometry barely varies.** PROG_SIZE=1 and PCACHE_SIZE=16 in every suite in my area; only
   test_fwrite, test_mtree and test_rbyd override them. As a result the ecksum window is 1 byte in
   essentially every failure test.

---

## 1. Features

### F1. Allocator core: lookahead bitmap plus checkpoints

**State** (lfs3.h:1336-1342, `struct lfs3_lookahead`):
- `window`: absolute block number of the next candidate. It is always equal to `gbmap.window` when the
  gbmap is enabled (asserted at lfs3.c:7663-7667).
- `off`: circular bit offset of `window` inside `buffer`.
- `known`: how many blocks from `window` onwards have valid bits.
- `ckpoint`: how many window increments remain before the allocator declares NOSPC.
- `buffer`: `lookahead_size` bytes, one bit per block; 1 means in use.

**Checkpoint protocol** [D] 2178-2187 and 10785-10791; [I] 10768-10811:
- A caller calls `lfs3_alloc_ckpoint` only when every in-use block is reachable from the committed
  filesystem, open handles, `lfs3->graft`, or the in-RAM or on-disk gbmap trees.
- `ckpoint_` (10768) sets `ckpoint = block_count` and marks every open traversal handle
  CKPOINTED | DIRTY | STALE (9804-9813).
- With the gbmap enabled, the checkpoint then repopulates the gbmap if
  `gbmap.known < min(lookgbmap_thresh, block_count)` (10797-10808) and checkpoints again.
- Guarantee [D] 2183-2184: "blocks are allocated at most once, and never reallocated, between
  checkpoints".
- Mechanism: the window only moves forward (`lfs3_alloc_inc`, 10999-11042). Every increment
  decrements `ckpoint` and `known`. Adopted knowledge is capped at `known <= ckpoint`
  (`lfs3_alloc_adopt`, 10947-10969). Blocks consumed since the checkpoint lie behind the window and
  outside `[window, window + ckpoint)`, so they cannot be handed out again.

**Allocation functions:**
- `lfs3_alloc_findfree` (11046-11119): scans forward.
  - If `gbmap.known > 0` it uses the gbmap; the current range is cached in `gbmap.next`
    (+n free, -n in use).
  - Otherwise it uses the lookahead bits.
  - Otherwise it returns `LFS3_ERR_NOSPC`, meaning "out of known blocks", which is not yet the
    final answer.
- `lfs3_alloc__` (11127-11200):
  - On success it asserts the block is not 0 or 1 (11139-11140), advances past it, and eagerly
    finds the next free block.
  - When no block is known and `ckpoint <= 0`, it logs "No more free space" and returns
    `LFS3_ERR_NOSPC` (11160-11166).
  - Otherwise it runs a full traversal (`lfs3_mtree_traverse` with `LFS3_T_RDONLY|LFS3_T_LOOKAHEAD`),
    marks in-use bits and masks in-flight graft blocks (11189-11195), then calls
    `lfs3_alloc_adopt(ckpoint)`.
- `lfs3_alloc_` (11205-11270), with `LFS3_alloc_ERASE`:
  - If the gbmap supplied an ecksum (a pre-erased block), it checks the ecksum. On `LFS3_ERR_CORRUPT`
    it skips to another block without erasing (11227-11246).
  - Otherwise it calls `lfs3_bd_erase`. On `LFS3_ERR_CORRUPT` it tries another block (11251-11257).
  - It returns any other error.
- `lfs3_alloc` (11277): plain allocation. [D] "preerase: caller is responsible for perturbing
  erased-state". The only callers are `lfs3_rbyd_alloc` (2643, with erase) and
  `lfs3_mdir_alloc___` (8088 without erase, 8112 with erase).
- `lfs3_allocclaim` (11297-11328): if the block came pre-erased, it immediately runs
  `lfs3_mdir_commit_` with no rattrs and without a checkpoint. That commit persists the advanced
  gbmap window before the block is written. Its only live caller is data-block crystallization at
  13650. `lfs3_bptr_alloc` (2395) is dead code.

**Order and start position:**
- Allocation scans linearly and wraps modulo `block_count`.
- Without a gbmap, mount seeds `window = gcksum % block_count` (15974), which avoids always
  allocating near block 0 after a power cycle [D] 15968-15973.
- With a gbmap, mount resumes at the persisted `gbmap.window` (15952-15954).
- At mount `ckpoint = 0` (15226). Every mutating operation must checkpoint before allocating;
  §9 B2 covers the one path that does not.

**Errors:**
- `LFS3_ERR_NOSPC`: every block has been tried since the last checkpoint.
- Bd errors other than CORRUPT are propagated unchanged.
- Traversal errors raised during a lookahead scan propagate, typically `LFS3_ERR_CORRUPT` from an
  unreadable mdir or btree node. **One unreadable metadata block therefore makes every allocation
  that needs a scan fail.**

**Limits:**
- The bitmap covers `8*lookahead_size` blocks.
- `lookahead_size > 0` is asserted (15213). No alignment is required.
- Block numbers must be ≤ 31 bits (asserted in encoders, e.g. 2311-2312 and 5149-5150).

[T] test_alloc:
- `test_alloc_alloc` and `test_alloc_reuse` (ERASE false/true, internal API).
- `test_alloc_clobber_{dirs,files,open_files}`.
- `test_alloc_wraparound_files`.
- `test_alloc_nospc_{dirs,files}`: NOSPC plus data integrity after NOSPC and after remount.
  **Recovery (delete then write again) is UNTESTED.**

### F2. Global on-disk block map (gbmap)

**Purpose** [D] commit 843412c: "the gbmap's responsibility is to track *free* blocks, in-use blocks
are secondary". It replaces repeated full lookahead scans with a persisted, range-compressed map,
and it stores pre-erase state.

**Compile and runtime switches:**
- `LFS3_GBMAP` compiles the feature. `LFS3_YES_GBMAP` forces it on for every format and mount
  (7356, 16290).
- The format flag is `LFS3_F_GBMAP` (0x02000000).
- The on-disk flag is `LFS3_WCOMPAT_GBMAP` (0x00080000). The info flag is `LFS3_I_GBMAP`.
- At mount, `wcompat` sets `I_GBMAP`, which is the same bit as `F_GBMAP` (15653-15658).
- Write-compat masking (15453-15459) depends on the build:
  - A build with `LFS3_GBMAP` but without YES ignores the flag, so it can mount images with or
    without a gbmap.
  - A build without `LFS3_GBMAP` treats the flag as unknown: a RDWR mount returns
    `LFS3_ERR_NOTSUP`, while a RDONLY mount is allowed.
  - A YES_GBMAP build requires the flag to be present.

**In-RAM state** (lfs3.h:1367-1385):
- `window`, `known`: the known window, meaning entries are trusted only in
  `[window, window + known)`.
- `next`: signed cache of the current range length; not persisted.
- With PREERASE: `ecksum` (the current range's ecksum) and `preeraser{known, count}`.
- `b`: the in-RAM gbmap btree.
- `b_p`: the committed btree.
- `gbmap_p[23]`: the committed encoding.
- `gbmap_d[23]`: deltas consumed from dropped or split mdirs.

**Tree:**
- A btree whose weight is `block_count`. The weight is not stored on disk; it is implied by the
  geometry (`lfs3_data_readbranch(..., lfs3->block_count, ...)` at 10475).
- Each entry is a range: a tag in `{BMFREE, BMINUSE, BMERASED, BMBAD}` with weight = range length,
  keyed by the range's last block.
- `lfs3_gbmap_lookupnext` (10497-10518) returns the range containing the block, its end (bid) and its
  weight, plus the decoded ecksum when the tag is BMERASED with a payload.

**Set operation** `lfs3_gbmap_set__` (10535-10662):
- It works on a copy, so an error leaves the in-RAM tree unchanged. The original is only marked
  unfetched (10555-10559).
- It merges with the right or left neighbour when tag and ecksum are equal
  (`lfs3_ecksum_cmp`, 2523-2536).
- It splits a range when setting inside it, and does the whole update in one btree commit
  (10627-10648).
- A weight greater than 1 is allowed only inside a single existing range, which is used for bulk
  zeroing [D] 10531-10534.

**Range compression limits:**
- BMERASED ranges merge only when their ecksums are equal. On noop-erase media (erase_value -1, SD,
  RAM) every pre-erased block gets its own range [D] commit 843412c.
- Worst-case fragmentation (alternating free and in-use) gives O(block_count) entries.
- No bound or limit is documented (§9 R6).

### F3. gbmap repopulation ("lookgbmap") and the self-allocation catch-22

**Synchronous path** (`lfs3_alloc_lookgbmap`, 11332-11384): called from `lfs3_alloc_ckpoint` when
`gbmap.known < min(lookgbmap_thresh, block_count)`.
1. `gbmap_ = copy of gbmap.b`. The old tree is reused so that erased and bad information survives
   [D] 11340-11344 and commit 633cbe8.
2. `lfs3_gbmap_zerounknown(gbmap_, window + known, ckpoint - known)` (10724-10755). This turns
   **BMINUSE and BMERASED** ranges in the *unknown* region into BMFREE. BMBAD is preserved and so is
   everything inside the known window [D] commit 35a1ac9.
3. A full traversal (`lfs3_mtree_traverse`, flags `LFS3_T_RDONLY`) sets BMINUSE for every
   mdir, btree and data block via `lfs3_gbmap_setbptr` (10687-10720). The traversal covers
   `lfs3->gbmap.b` (MID_GBMAP) and `b_p` when it differs (MID_GBMAP_P) (9913-9935), open unsynced
   files and ungrafted leaves (9964-9990).
4. `lfs3_alloc_adoptgbmap(gbmap_, lookahead.ckpoint)` (10972-10996) sets `known = ckpoint`, resets
   the preeraser, eagerly runs findfree and clears `I_LOOKAHEAD`.
5. The result is **not** committed immediately. It rides on the next mdir commit's gdelta
   [D] 11375-11381 and commit 726cccf.

**The catch-22** (building a tree that allocates from itself):
- Btree commits to `gbmap_` allocate blocks from the *old* in-RAM gbmap, or from the lookahead
  fallback.
- Those blocks are consumed after the checkpoint, so they lie behind the window. The adopted
  `known = ckpoint` excludes them. They are never marked in use inside `gbmap_`, yet they are never
  handed out until the next repopulation.
- The next repopulation's traversal visits them through MID_GBMAP and marks them in use.
- The old gbmap's blocks are marked in use because they were traversed as MID_GBMAP; they leak until
  the following repopulation.
- [D] commits 316ca1c and 92620d3 ("recheckpoint after rebuilding").

**Incremental path** (`lfs3_mtree_gc`, 10198-10411), used by `lfs3_fs_gc`, `lfs3_fs_ck` and
`lfs3_trv_read`:
- At MROOTANCHOR, with `T_LOOKAHEAD`, not MTREEONLY and not checkpointed, it chooses the gbmap if
  `canlookgbmap && !canlookahead`, otherwise the lookahead buffer (10224-10277).
- In gbmap mode it runs `ckpoint_`, copies the tree and zeroes the unknown region immediately (10240-10258). It
  then marks in use per traversal step (10294-10312) and adopts at end-of-traversal only if no
  checkpoint happened meanwhile (10374-10392).
- Otherwise the gbmap is `gbmap_.r.weight=0` (lookahead mode). Adopting the lookahead buffer and the
  gbmap from one traversal is deliberately impossible [D] commit 9e4bbdf.

**Lookahead fallback:**
- `lfs3_alloc_canlookahead` (10819-10834):
  `max(lookahead.known, gbmap.known if gbmap) <= min(gc_lookahead_thresh, 8*lookahead_size-1, block_count-1)`.
- `lfs3_alloc_canlookgbmap` (10841-10851):
  `gbmap.known <= min(max(gc_lookgbmap_thresh, lookgbmap_thresh), block_count-1)`.
- `I_LOOKAHEAD` is set by `alloc_inc` when either holds (11036-11040), and cleared by adopt or
  adoptgbmap.

**Errors:**
- A repopulation that cannot allocate gbmap nodes returns `LFS3_ERR_NOSPC`. That error comes back
  from **any mdir commit**, because every commit checkpoints first (9480-9493).
- [D] commit 5d70e47: the design rationale is "at least with an error the user can call rmgbmap"
  (disputed in §9 B10).

### F4. gbmap lifecycle: format, mkgbmap, rmgbmap, grow, mount

- **Format** `lfs3_formatgbmap` (16129-16165):
  - It writes the root rbyd to **fixed block 2**: `[0,3)` BMINUSE and `[3,block_count)` BMFREE,
    with `window=3` and `known=block_count`.
  - The mroot anchor is fixed at blocks 0 and 1 (16178-16275), and both anchor blocks receive the
    same superblock, including the gbmap delta.
  - [D] TODO 16130-16133: "should we try multiple blocks?". A bad block 0, 1 or 2 makes format fail.
- **`lfs3_fs_mkgbmap`** (17002-17060):
  - Returns EXIST if a gbmap is already present. [D] lfs3.h:1827 says "Does nothing"; that is stale,
    see commit ad2e8b3.
  - Runs mkconsistent, then commits a single BMFREE range of `block_count` into a fresh btree
    (allocated from the lookahead), sets `F_GBMAP` and `gbmap.window = lookahead.window`, and
    checkpoints, which repopulates the gbmap if `known(0) < lookgbmap_thresh`.
  - Commits `WCOMPAT_GBMAP` to the mroot together with the gbmap delta, atomically.
  - On failure it clears the flag and reinitialises.
  - Not available with `LFS3_YES_GBMAP`.
- **`lfs3_fs_rmgbmap`** (17064-17096):
  - Returns NOENT if no gbmap is present; the header's "Does nothing" is stale.
  - Runs mkconsistent, then commits the wcompat change without the gbmap bit.
  - [D] 17076-17080 claims leftover GBMAPDELTA tags "should be cleaned up implicitly as mdirs are
    compacted". This is doubtful (§9 R4).
  - After this, `commitgdelta` keeps xoring `gbmap_d` into `gbmap_p` (7586-7591), so re-enabling
    later accounts for the lingering deltas correctly.
- **Grow** (`lfs3_fs_grow`, 16899-16998):
  - Asserts the fs is RDWR and that the new count is not smaller.
  - Sets the new `block_count` and calls `lfs3_alloc_discard` (10886-10897): lookahead
    `known = 0` plus a memset, and gbmap `known = next = 0`.
  - With a gbmap it extends the last BMFREE range, or appends a new BMFREE range of
    `new - old` (16933-16964), then commits GEOMETRY to the mroot (16967-16977). That commit
    implies a checkpoint and a gbmap repopulation.
  - [D] 16910-16918: mkconsistent is deliberately skipped so that grow can always rescue a stuck fs.
  - Failure (16985-16997) restores `block_count`, discards again and restores `gbmap.b = b_p`.
    The **window is not reset** (§9 B6).
  - There is **no upper bound against `cfg->block_count`** (§9 B7).
  - [T] test_grow (12 cases) runs with GBMAP false and true. The incr cases expect grow to fail
    with NOSPC and retry with +1 block (tests/test_grow.toml grow loop). The pl_fuzz cases are
    reentrant.
- **Mount:**
  - Mount reads the gbmap gstate but does not fetch the tree (15939-15966). The tree is fetched
    lazily on first lookup.
  - With `LFS3_RDONLY` builds the gbmap is not decoded at all, so a RDONLY traversal does not visit
    gbmap blocks (it traverses an empty `gbmap.b`).

### F5. Pre-erase (`LFS3_PREERASE`, requires `LFS3_GBMAP` + `LFS3_REVPERTURB`, #error at lfs3_util.h:111-114)

[D] commits 843412c, 476822a, e3bca2b, b3ab83d, 35a1ac9, 2955090.

**Gc side** (`lfs3_alloc_preerase`, 11389-11457, one block per call):
- Walks forward from `window + preeraser.known` inside the known window. Non-free ranges are skipped;
  existing BMERASED ranges count toward `preeraser.count`.
- For a BMFREE block: `lfs3_bd_erase`, then `lfs3_ecksum_read` computes the CRC32C of the **first
  `prog_size` bytes** at offset 0 (2491-2502). The block is then set to `BMERASED{cksize=prog_size,
  cksum}` in the in-RAM `gbmap.b`.
- The `gbmap_set` commit may allocate the very block it just erased. Success is therefore counted
  only if the block is still inside the known window (11437-11453).
- The gbmap `next` cache is invalidated when the edit lands inside the cached range.
- The erased state persists only when the gbmap is next committed: by any mdir commit, or by the gc
  "sync" step `lfs3_alloc_syncgbmap` (11462-11470), which commits the mroot [D] commit be69c99.
- `lfs3_alloc_canpreerase` (10856-10866) requires a gbmap,
  `preeraser.count < gc_preerase_count` and `preeraser.known < gbmap.known`.
- `gc_preerase_count` is the target number of erased blocks kept **ahead of the window**, not a
  per-call count.

**Alloc side** (how littlefs decides a block may be programmed without an erase):
1. The block must be inside the persisted or in-RAM known window with tag BMERASED, and the mount
   must have `LFS3_M_REVPERTURB`. Otherwise BMERASED is treated like in-use (11069-11083).
   Cross-mode mounts are therefore safe but waste the pre-erasure [D] commit e3bca2b.
2. `lfs3_ecksum_ck(block, off 0, cksize)` must match (2505-2519, used at 11227-11246). A mismatch
   means "a prog was attempted, then power was lost"; the block is skipped, not erased, not reused,
   and it leaks until the next repopulation [D] commit 35a1ac9.
3. The writer must then guarantee that a later partial write changes the first `prog_size` bytes,
   or must persist the claim before writing:
   - **rbyds (mdirs, btree nodes, gbmap nodes):** every rbyd starts with `lfs3_rbyd_appendrev`
     (3262-3316, called from appendinit 3619-3640, mdir_alloc 8123, swap 8174, format 16202).
     With REVPERTURB, bit 7 of revision byte 0 is set to the inverse of the byte currently on disk
     (3268-3284), so the first prog always flips at least one bit relative to the recorded erased
     state [D] commit b3ab83d.
   - **data blocks:** the content is uncontrolled and could equal the erased state, so
     `lfs3_allocclaim` commits the gbmap, whose window now excludes the block, **before** the data is
     written (11312-11325) [D] commit 476822a.

**Known-window mechanism** [D] commit 476822a:
- "If we consider BMERASED ranges outside the gbmap's known window as invalid, we just need to
  decrement the known window to make progress."
- Every allocation advances the window. Every mdir commit persists the window atomically with the
  change that makes the block referenced. After a power loss, a block written but not committed is
  back inside the persisted window: it is either BMFREE, so it is erased before reuse, or BMERASED,
  so the ecksum must still match.

**Mount and format flags:**
- `LFS3_M_PREERASE` and `LFS3_F_PREERASE` run `lfs3_fs_ck` with PREERASE (16067-16083, 16367-16383).
- REVPERTURB is asserted in format, mount, gc, ck and trv_open (16042, 16332, 16812-16817,
  16854-16859, 17137-17142).
- REVPERTURB is **not** recorded on disk. It is a per-mount behaviour, and the format flag only
  affects format-time writes.

**Other points:**
- `LFS3_T_PREERASE` is accepted by `lfs3_trv_open` but ignored. Pre-erase lives only in
  `lfs3_fs_gc_` [D] commit 843412c.
- `LFS3_I_PREERASE` is computed lazily in `lfs3_fs_stat` (16428-16436).

[T]:
- test_gc_preerase_{progress,relaxed,decreasing}: erase-count bounds, ERASE_VALUE 0xff/0x00/-1.
- test_gbmap_gc_files: GC_PREERASE_COUNT 0/4/COUNT/2/COUNT-4/-1.
- test_mount_t_preerase: REMOUNT 0/1/2. REMOUNT=2 still mounts with REVPERTURB.
- test_gc_iflags, test_gc_mutation, test_gc_nospc and test_gc_spam_* with PREERASE=true.
- test_trvs_flags: T_PREERASE no-op.
- **Every one of these is non-reentrant, so pre-erase under power loss is UNTESTED.** It is also
  compiled only with LFS3_GC + GBMAP + REVPERTURB + PREERASE.

### F6. Incremental GC, traversal-driven GC, mkconsistent and compaction

- **`lfs3_fs_gc`** (16832-16868, `#ifdef LFS3_GC`):
  - Asserts the flags (TODO 16833: whether to move this to init). Runs
    `lfs3_fs_gc_(&lfs3->gc, cfg->gc_flags, gc_steps ?: 1)`.
  - The persistent traversal lives in `lfs3->gc` and is registered as a handle. Unmount tolerates
    it (16113-16119).
- **`lfs3_fs_gc_`** (16660-16785), one step per loop iteration:
  - Pending work = `flags & lfs3->flags & {MKCONSISTENT, LOOKAHEAD, COMPACT, CKMETA, CKDATA}`, plus
    MKCONSISTENT whenever grm entries are pending.
  - LOOKAHEAD suppresses MKCONSISTENT and COMPACT (16677-16684) so that allocators are fed before
    work that allocates.
  - Starts or continues a traversal, masks out flags that changed, restarts when no progress is
    possible, and uses MTREEONLY when neither lookahead nor ck is needed. One call to
    `lfs3_mtree_gc` counts as one step (roughly one block).
  - With nothing pending it does, in order: one pre-erase block; else one gbmap sync (mroot commit);
    else it stops (16745-16778).
  - `steps < 0` means unbounded. **[D] test comment at tests/test_gc.toml:2992 ("DON'T test with
    GC_STEPS=-1, it may never terminate!") marks a near-full liveness hazard that lfs3.h does not
    mention.**
- **`lfs3_fs_ck(flags)`** (16789-16826): the same loop with a stack mgc and unbounded steps. It first
  ORs `CKMETA`/`CKDATA` into `lfs3->flags` so the work is redone.
- **`lfs3_fs_unck`** (16871-16893): re-marks work as pending and clears it from the ongoing gc
  traversal. [D] header lfs3.h:1801-1808 uses stale names: `lfs3_gc_unck`, `LFS3_I_CANCKMETA`.
- **`lfs3_mtree_gc`** (10198-10411), per step:
  - Fixes pending grm entries whenever MKCONSISTENT is set (10202-10214).
  - Sets up the lookahead or gbmap at traversal start.
  - Per block: marks in use; runs mkconsistent on mdirs (deletes orphaned stickynotes,
    `lfs3_mdir_mkconsistent` 16538-16590). A drop is handled by a "TODO big hack!" at 10329-10335.
  - Compacts mdirs whose `eoff > gc_compact_thresh`, or ~7/8 of `block_size` when that is 0
    (10340-10364), via `lfs3_mdir_compact` (9505-9511), which relocates according to block_recycles.
  - At end-of-traversal it adopts the allocator state and clears `I_MKCONSISTENT` if the traversal is
    not dirty and `I_COMPACT` if it was not checkpointed. `I_CKMETA`/`I_CKDATA` are cleared in
    `lfs3_mtree_traverse` at eot (10153-10179).
- **Traversal API** (17105-17245): `LFS3_T_{MKCONSISTENT,LOOKAHEAD,PREERASE,COMPACT,CKMETA,CKDATA,
  MTREEONLY,EXCL}` drive the same `lfs3_mtree_gc`. `EXCL` returns BUSY when the traversal is dirty.
  Any allocator checkpoint (any mutation) marks open traversals CKPOINTED | DIRTY | STALE.
- **Mount and format**: `LFS3_M_*` and `LFS3_F_*` gc flags run `lfs3_fs_ck` inside mount and format.
  **Any gc error then fails the mount** (16076-16083).
- **`lfs3_fs_mkconsistent`** (16618-16652): fixgrm, fixorphans (an MTREEONLY mkconsistent
  traversal), then a checkpoint.

[T]:
- test_gc (32 cases): lookahead/lookgbmap progress, mutation and relaxed; preerase; compact;
  mkconsistent; ckmeta/ckdata (± unck); iflags; mutation; nospc; spam_*.
- test_trvs (74 cases): flags, mutation × many, compact × 6, mkconsistent × 8, spam_*.
- test_mount_t_* (7 cases).
- **None of these are reentrant.**

### F7. Flash error handling: the block-device contract and what littlefs does

**bd contract** [D] lfs3.h:468-490:
- prog and erase "May return `LFS3_ERR_CORRUPT` if the block should be considered bad". Other
  negative codes are propagated.
- Read: "Negative error codes are propagated". The header does **not** document that littlefs gives
  a read's CORRUPT special meaning (see below).
- Wrappers assert alignment and bounds (31-107). `LFS3_INFO` logs every bad read, prog or erase.

**Erase errors:**

| Site | CORRUPT | Other error |
|---|---|---|
| alloc (11251-11257) | skip the block, allocate the next one | return |
| mdir swap (8168-8171 → 8733-8738) | relocate the pair (overrecycling not allowed) | return |
| pre-erase (11416-11419) | **returned to gc** (§9 B1) | return |
| format 0/1/2 (16144, 16186) | format fails | format fails |
| mroot extension, forced `lfs3_mdir_swap___` (9309-9317) | "Stuck mroot" → NOSPC | return |

**Prog errors** (CORRUPT from `bd_prog`, or from the `LFS3_CKPROGS` readback mismatch at 331-352):
- New mdir block: `goto relocate` inside `lfs3_mdir_alloc___` (8121-8128).
- mdir append or compaction: compact, then relocate (8705-8822). The pair is re-allocated;
  `relocated=true` keeps `blocks[1]` and only re-allocates `blocks[0]`.
- mdir split: `split_relocate` (8978-9011).
- btree nodes: relocate, split_relocate_l/r, merge_relocate, commitroot (5861-5982, 6059-6105,
  6151-6181).
- Data blocks: `relocate` → `allocclaim` → rewrite the whole block from `block_pos` (13399-13660).
- mroot anchor (0/1): "Stuck mroot" → NOSPC (9310-9336).
- Format: fails.

**Read errors:**
- `lfs3_mdir_fetch` (7853-7910) tolerates CORRUPT while reading revisions and tries the other
  block. That fallback is silent and may roll back to the older revision.
- `lfs3_rbyd_fetch_` treats CORRUPT as the end of the log (2768-2770, 2788, 2807, 2823, 2849).
- Revision reads during alloc and swap substitute `rev=0` on CORRUPT (8101-8106, 8147-8152).
  [D] "as long as they are consistent".
- Everywhere else CORRUPT propagates to the caller.
- **Inside write paths a source-side read CORRUPT cannot be told apart from a destination prog
  failure (§9 B5).**

**Silent failures:**
- PROGNOOP, ERASENOOP and PROGFLIP are caught at write time only with `LFS3_M_CKPROGS`.
- Otherwise they are caught later by rbyd checksums at fetch, ckmeta or ckdata scans,
  `LFS3_M_CKFETCHES` or `LFS3_M_CKDATACKSUMS` (see agent 4 for checksums in general).
- Data-block contents are protected only by `bptr.cksum`, which is checked by `lfs3_bptr_ck`
  (2455+). That check runs under CKFETCHES (2440-2447), under a T_CKDATA traversal (10141-10151)
  or under CKDATACKSUMS reads.

**Not implemented:**
- There is no persistent bad-block memory: a bad block is retried after the window wraps, or after a
  remount or repopulation.
- There is no erase verification beyond the pre-erase and rbyd ecksums.
- There is no bound on relocation retries other than allocator exhaustion.
- There is no retry of transient errors and no read-only degradation. See TODO "switch to
  read-only?" at 15921 and 15948.

### F8. Wear leveling (`block_recycles`) and the revision count

**Encoding** (7765-7784) [D] commit b3ab83d:
- `vvvv` (bits 28-31): relocation revision, incremented by `lfs3_rev_init` on (re)allocation (7787-7791).
- `rrrr…` (bits `28-rb` to 27): recycle counter.
- `n…`: REVNOISE noise from `gcksum_p` (3291-3300).
- `p` (bit 7 of byte 0): REVPERTURB.
- `ddddddd` (bits 0-6): debug character `'m'` (mdir), `'b'` (btree) or `'h'` (anchor).

**Recycle-bits formula** (`recycle_bits`, 15258-15264):
- `rb = nlog2(2*(block_recycles+1)+1) - 1`, asserted `<= 20`, so the valid range is
  0 ≤ block_recycles ≤ 1,048,574, plus -1.
- The effective erases per block before relocation are `2^(rb-1)`, i.e. rounded down to a power of
  two (4→4, 16→16, 100→64).
- `-1` disables mdir relocation. `0` relocates on every compaction ("pure copy-on-write").
- Any other negative value underflows and trips the assert.

**Compaction and relocation:**
- `lfs3_mdir_swap___` (8137-8181) returns internal NOSPC when `lfs3_rev_needsrelocation`
  (7795-7806), which triggers relocation.
- When relocation cannot allocate, it "overrecycles" by swapping in place (8800-8820, WARN).
- A CORRUPT during the forced swap gives "Stuck mdir" → `LFS3_ERR_NOSPC`.
- btree nodes and data blocks are copy-on-write on every compaction or rewrite; their wear leveling
  comes from allocator rotation only (dynamic leveling [D] tests/test_exhaustion.toml header).

[T] test_relocations (BLOCK_RECYCLES 4/1/0, 8 cases, 2 reentrant under ATOMIC).
test_exhaustion (5 cases).

### F9. Exhaustion and NOSPC

**NOSPC sources:**
- Allocator exhausted since the checkpoint (11160).
- Stuck mdir (8816).
- Stuck mroot (9316, 9336).
- gbmap repopulation unable to allocate (via checkpoint).
- File writes and commits propagate NOSPC. On a write error the file becomes desynchronized
  [D] lfs3.h:1568-1569.

**Wear-out (all blocks bad):**
- Allocation skips erase-fail blocks, and prog failures relocate until the allocator is exhausted,
  at which point NOSPC is returned.
- mdirs overrecycle before giving up.
- [T] test_exhaustion_spam_{dir,file,fwrite,uz,uzd}_fuzz with ERASE_CYCLES=10 and PROGERROR,
  ERASEERROR, READERROR, PROGNOOP, ERASENOOP (the last three only with CKPROGS). It asserts
  `lifetime(2N blocks)*1.1 > 2*lifetime(N)` and that failures surface as NOSPC.
- **UNTESTED:** readability or remount after death; behaviour after death; wear-out combined with
  gbmap (only in YES_GBMAP builds), pre-erase, bit-flip modes or power loss.

**Free space:**
- There is no free-space API. `lfs3_fs_usage` (16445-16476) counts traversed blocks, may
  double-count copy-on-write sharing, and includes gbmap trees.
- A best-effort `known_free` field was prototyped and reverted (commit 465e9fb).

---

## 2. On-disk format: gbmap and the related perturb and erased state

Byte encodings use leb128 and le32 as in lfs3.h:957-1080. Tag values are in lfs3.h:814-895.
Bit strings come from scripts/dbgtag.py:28 and :46-50.

**gstate delta tag `LFS3_TAG_GBMAPDELTA = 0x0234`** (`v--- --1- +-11 -1rr`, gstate supertype 0x02xx):
- It is stored at rid -1 of mdirs.
- The on-disk gbmap value is the **XOR over every mdir's GBMAPDELTA payload**, zero-padded to 23 bytes.
- A commit appends `delta' = old_delta ^ enc(new) ^ gbmap_p ^ gbmap_d`, or removes the tag when that
  value is all zero (7657-7708).
- Exactly one mdir per atomic `lfs3_mdir_commit_` receives it: the committed mdir, or the mroot or
  mroot parent when the mdir relocated, split or dropped. Relocated or split children are written
  with `start_rid > -2` and carry forward their old rid -1 tags through compaction.
  (?) I reached this by reading 8743-8746 and 8985-9011; I did not verify it with dbglfs3.

**Payload encoding** (lfs3.h:1003-1016, `lfs3_data_fromgbmap` 10433-10460, `readgbmap` 10464-10490):
```
window : leb128 (<=5)   next candidate block (== lookahead.window at commit)
known  : leb128 (<=5)   number of blocks from window whose map entries are trusted
block  : leb128 (<=5)   gbmap btree root block      \
trunk  : leb128 (<=4)   root rbyd trunk offset       } lfs3_data_frombranch (5147-5171)
cksum  : le32   (4)     root rbyd canonical cksum   /
max 23 bytes; trailing zero bytes trimmed (lfs3_memlen)
```
- The root weight is not stored; it is implied `= geometry.block_count`.
- `next`, `ecksum` (the cached range), the preeraser and the lookahead are not persisted. Mount sets
  `next=0` (10480-10481).

**gbmap range entries** (btree rattrs):

| tag | value | bits | meaning | payload |
|---|---|---|---|---|
| BMFREE | 0x0440 | `v--- -1-- +1-- ----` | free (erase before use) | none |
| BMINUSE | 0x0441 | `…---1` | referenced (or conservatively assumed) | none |
| BMERASED | 0x0442 | `…--1-` | erased, safe to prog without erase if ecksum matches and window-valid | ecksum: `cksize` lleb128 (<=4) + `cksum` le32 (CRC32C of first cksize bytes) (`lfs3_data_fromecksum` 2541-2563) |
| BMBAD | 0x0443 | `…--11` | reserved; **never written by current code**; readers treat as in-use; zerounknown preserves it | none |

- A two-bit state subfield: "in-use + erased can only be bad" [D] commit e622656.
- Weight = run length. The entry key (bid) is the last block of the run.
- BMERASED runs merge only when the ecksums are equal.
- An empty BMERASED payload decodes as "no ecksum" (10508-10514). (?) I found no code that writes
  one.

**Format image with a gbmap:**
- Blocks 0 and 1: anchor, same superblock written twice with revisions `-1<<28|…` and `0<<28|…`
  (16191-16199). It includes the GBMAPDELTA with `window=3`, `known=block_count` and a root at
  block 2.
- Block 2: gbmap root rbyd with `BMINUSE w3` and `BMFREE w(block_count-3)`.
- In test images `FORMAT_BLOCK_COUNT = 3` with a gbmap and 2 without.

**Revision count** (first 4 bytes of every rbyd, le32): see F8. The perturb bit is bit 7 of byte 0,
which the ecksum check at offset 0 relies on. The debug bits in byte 0 (`'m'`=0x6d, `'b'`=0x62,
`'h'`) already make byte 0 differ from 0x00 and 0xff even without REVPERTURB. REVPERTURB makes that
hold for arbitrary erase patterns.

**Commit-level perturb** (do not confuse with REVPERTURB):
- The ECKSUM tag 0x3200 is written before each commit's CKSUM tag. It holds
  `{cksize=prog_size, crc of prog_size bytes after the padded end}` (4417-4447).
- The CKSUM tag's `LFS3_TAG_PERTURB` bit (0x0004) is set when the next erased byte's top bit equals
  the current parity. That inverts the meaning of the next commit's valid bits (4430-4434,
  4468-4483).
- Fetch verifies the erased state with `lfs3_rbyd_ckecksum` (2655-2680). Agent 4 covers this in
  detail.

**Tooling:**
- `scripts/dbglfs3.py --gstate/--gdelta/--gstructs` decodes and prints the gbmap gstate
  (window, known, root) and dumps the range tree (dbglfs3.py:2837-2860, 4253+).
- `dbgbtree.py`, `dbgrbyd.py` and `dbgtag.py` know the BM tag names.
- `scripts/dbgbmap.py` renders **traversal-derived** usage (mdir, btree, data, corrupt, conflict,
  unused; dbgbmap.py:32-50). It does **not** render gbmap free, erased or bad states, so there is no
  visual check of gbmap versus reality (a tooling gap).

---

## 3. Invariants and safety arguments

- **I1 (allocation uniqueness).** Between two checkpoints each block is returned at most once, and
  never a block that is reachable from committed or in-RAM state at the checkpoint.
  - Proof sketch: the window is monotonic, `known <= ckpoint` by construction (adopt and
    adoptgbmap), and every increment decrements both. The lookahead scan marks every reachable
    block, including grafts and open handles.
  - Requires that every mutating API checkpoints first. The pre-erase path violates this (§9 B2).
- **I2 (gbmap soundness).** For the persisted pair (fs state S, gbmap G): every block in
  `[G.window, G.window+G.known)` marked BMFREE or BMERASED is unreferenced by S.
  - At repopulation G is built from a traversal of all in-use blocks, including in-flight ones. The
    known window excludes blocks allocated since the checkpoint.
  - Afterwards blocks only leave the window at the front, by allocation, and freed blocks stay
    BMINUSE, which is conservative.
  - S and G are committed in the same mdir commit, since the gdelta rides with it.
- **I3 (erased soundness).** A persisted `BMERASED(E)` inside the known window means the block was
  erased after its last use, and any later write either (a) changed the first `E.cksize` bytes, for
  rbyds via REVPERTURB and the debug bits, or (b) was preceded by a persisted window advance, for
  data via allocclaim. Hence `ecksum_ck` passes only if no write happened.
  - **Assumption:** an interrupted first prog disturbs the first `prog_size` bytes. See §9 B17: the
    first flush can be `pcache_size` bytes, and emubd cannot model a violation.
- **I4 (gbmap self-hosting).** gbmap nodes allocated while building or editing the gbmap are behind
  the window. They are reached by the next traversal through MID_GBMAP or MID_GBMAP_P (9913-9935).
  - An abandoned incremental copy (the traversal was checkpointed) is garbage that the next
    repopulation reclaims.
- **I5 (commit atomicity across blocks).** Each `lfs3_mdir_commit_`:
  - calls `lfs3_bd_sync` before mtree or mroot updates (9199-9203, 9262-9266, 9300-9304);
  - ends with a sync (9343-9347);
  - makes each commit's CKSUM prog the last prog before a sync.
  - Under emubd OOO (every unsynced block reverts except the one being written), a referencing
    commit is therefore never durable without its referents.
- **I6 (gstate revert).** Every failure inside `lfs3_mdir_commit_` must restore gcksum and grm from
  the on-disk copies (`lfs3_fs_revertgdelta`, 7596-7613). The gbmap is deliberately not reverted,
  because in-flight in-RAM state must survive.
  - The implementation violates this at three returns (§9 B3).
- **I7 (anchor).** Blocks 0 and 1 are always the mroot anchor and cannot move. Losing either block
  eventually yields "Stuck mroot" → NOSPC [T] test_badblocks_mrootanchor_wear.

**Power loss during allocation or commit** (ATOMIC, SOMEBITS, MOSTBITS, OOO):
- Any block written but not committed is either behind the in-RAM window (RAM state is lost) or
  inside the on-disk known window as BMFREE (erase before reuse) or BMERASED (ecksum rejects it).
- Without a gbmap the next mount rescans. Unreferenced blocks look free and are erased before reuse.
- [T] only for the default config (test_powerloss). With a gbmap, only in YES_GBMAP builds.

**Power loss during pre-erase** (UNTESTED):
- (a) During the erase: the BMERASED mark exists only in RAM, so on disk the block is still BMFREE
  and will be erased again.
- (b) During the `gbmap_set` commit (appends to the gbmap root, or new nodes): these blocks were
  BMFREE (erased on reuse) or BMERASED (the perturbed rev fails the ecksum) in the persisted window.
- (c) After syncgbmap: persisted, consistent by I2 and I3.
- (d) While a pre-erased block is being used by an rbyd: the perturbed rev ensures the ecksum
  mismatches, so the block is skipped. Under emubd SOMEBITS the flipped bit lies in the first
  prog_size bytes, so it is caught. Under OOO the block reverts to erased and matches, which is fine.
  Under METASTABLE the rev is written, so it mismatches.
- (e) While a pre-erased block is being used for data: allocclaim persisted the window first, so it
  is not in the window.
- Conclusion: safe under emubd's models by argument. **Unverified by any test.** The real-hardware
  risk is §9 B17.

**Power loss during relocation:**
- New blocks are written first. The reference switches only in the parent commit: the mtree update
  in the mroot, or the mroot chain in the parent anchor.
- Relocated children carry their gstate tags forward, so there is no double application.
- [T] only under ATOMIC: test_relocations_*_pl_fuzz, test_grow_incr_*_pl_fuzz.
- Power loss with SOMEBITS, MOSTBITS, OOO or METASTABLE during wear-relocation is UNTESTED, because
  test_powerloss uses BLOCK_RECYCLES=-1.

**Power loss during mkgbmap:** the wcompat bit and the delta are in the same mroot commit, so the
change is atomic. UNTESTED under power loss.

**Power loss during grow:** the geometry and the extended gbmap tree are in the same mroot commit,
so the change is atomic. [T] test_grow_incr_*_pl_fuzz (ATOMIC).

---

## 4. Configuration knobs

| knob | type | zeroed-cfg default | test default (runners/test_defines.h) | valid range / semantics | notes |
|---|---|---|---|---|---|
| `lookahead_size` | size | none (assert >0, 15213) | 16 | >0 bytes; covers 8*size blocks | `lookahead_buffer` optional static buffer |
| `block_recycles` | int32 | 0 (= relocate every compaction) | -1 | -1, or 0..1048574 (rb<=20 assert 15260) | effective = 2^(nlog2(2r+3)-2) per block |
| `gc_flags` (`LFS3_GC`) | u32 | 0 | `LFS3_GC_GC` | subset of MKCONSISTENT, LOOKAHEAD, PREERASE, COMPACT, CKMETA, CKDATA (asserted in `lfs3_fs_gc` 16834-16842, not in init) | PREERASE also needs M_REVPERTURB |
| `gc_steps` (`LFS3_GC`) | soff | 0 → 1 | 0 | >0 steps; any negative = unbounded | unbounded may not terminate near full (test comment) |
| `gc_lookahead_thresh` | block | 0 = only when empty | -1 | repopulate when max(lookahead.known, gbmap.known) <= min(thresh, 8*la-1, count-1) | gc only; alloc repopulates on empty |
| `gc_lookgbmap_thresh` (`GBMAP`) | block | 0 | -1 | repopulate gbmap in gc when known <= min(max(this, lookgbmap_thresh), count-1) | header text says "repopulates the lookahead buffer" (copy-paste error, lfs3.h:601-603) |
| `lookgbmap_thresh` (`GBMAP`) | block | **0 = never on checkpoint** (code `<`, 10800) | BLOCK_COUNT/4 | checkpoint repopulates when known < min(thresh, count) | header says "<=" and "0 only repopulates when empty": **mismatch (§9 B11)**; never overridden in tests |
| `gc_preerase_count` (`PREERASE`) | block | 0 = disabled | -1 | target erased blocks ahead of the window; -1 = all known free | requires gbmap + REVPERTURB |
| `gc_compact_thresh` | size | 0 → block_size - block_size/8 | 0 | 0, [block_size/2, block_size], or -1 (disable) (asserts 15143-15146 only with LFS3_GC) | affects gc and T_COMPACT only |
| `LFS3_M_REVPERTURB` / `F_` | flag | off | per-test | required for pre-erase; per-mount, not on disk | header M_ comment "Add debug info…" is stale |
| `LFS3_M_CKPROGS` / `F_` | flag | off | per-test | readback after every prog; turns silent failures into relocation | only defense against PROGNOOP, ERASENOOP, PROGFLIP at write time |
| `LFS3_F_GBMAP` / `LFS3_YES_GBMAP` | flag / macro | off | GBMAP define | format with gbmap; YES forces on and removes mkgbmap/rmgbmap | FORMAT_BLOCK_COUNT 3 vs 2 |
| mount/format gc flags `LFS3_M_{MKCONSISTENT,LOOKAHEAD,PREERASE,COMPACT,CKMETA,CKDATA}` | flag | off | per-test | run `lfs3_fs_ck` in mount/format; any error fails mount | M_PREERASE + bad block → mount fails (§9 B1) |
| `erase_value` (emubd) | int | test harness 0xff | 0xff | 0x00-0xff, -1 noop, -2 nor-mask | pre-erase tests use 0xff/0x00/-1 |
| `erase_cycles`, `badblock_behavior`, `powerloss_behavior` (emubd) | — | 0 / PROGERROR / ATOMIC | same | see §5 | erase_cycles=0 means only mkbad blocks are bad (emubd.c:1004-1009) |

---

## 5. Flash-failure matrix

Tested means a named case asserts behaviour under that failure. All badblock suites are
**non-reentrant**, so bad blocks combined with power loss are UNTESTED in every row.

### 5a. Bad-block behaviours

emubd `badblock_behavior` applies when `wear > erase_cycles`, via `lfs3_emubd_mkbad` or wear-out.

| # | behaviour (emubd semantics) | littlefs v3 today | recovers? | tests | untested cells |
|---|---|---|---|---|---|
| 0 | **PROGERROR**: prog returns CORRUPT, nothing written (emubd.c:696-702) | Relocation at every write site (F7). Anchor → Stuck mroot → NOSPC. Format of 0, 1 or 2 fails. Nothing remembered: the block is retried after the window wraps or after remount. | Yes, except anchor, format and exhaustion. | test_badblocks_{every,region,alternating}_{btree_many,spam_dir_many,spam_dir_fuzz,spam_file_many,spam_file_fuzz,spam_fwrite_fuzz,spam_uz_fuzz,spam_uzd_fuzz} (24 cases); test_badblocks_mrootanchor_format, mrootanchor_wear; test_exhaustion_* (5) | with PREERASE; with GBMAP only in YES_GBMAP builds (btree cases excluded via ifndef); block 2 with gbmap; under power loss |
| 1 | **ERASEERROR**: erase returns CORRUPT and does not erase; progs proceed (emubd.c:1011-1015) | alloc skips; mdir swap relocates; **pre-erase returns CORRUPT to gc, ck, mount or format and repeats forever (§9 B1)**; format fails | Yes, except pre-erase, anchor and format | same as PROGERROR | **pre-erase path UNTESTED**; under power loss |
| 2 | **READERROR**: every read returns CORRUPT (emubd.c:412-419) | At write time (CKPROGS) the readback fails → relocate. Live data on a bad block: mdir falls back to the other block (silent rollback) or the mount-time gcksum check fails the mount (15894-15899); btree or data gives CORRUPT; lookahead or gbmap scans that traverse it fail, **blocking all allocation** | Write time: yes, with CKPROGS only. Live data: no | same suites, **CKPROGS=true only**, so only in LFS3_CKPROGS builds; mrootanchor_wear (anchor gone bad after format: the rollback target is identical, so it is not a real rollback test) | without CKPROGS; READERROR on a live non-anchor mdir, btree or gbmap node; READERROR during a scan or repopulation; relocation storm (§9 B5) |
| 3 | **PROGNOOP**: prog silently ignored (emubd.c:704-709) | CKPROGS: relocate. Without CKPROGS: the commit is acknowledged but absent → later rollback or CORRUPT | CKPROGS only | badblocks and exhaustion, CKPROGS only | without CKPROGS (silent data loss, by design) |
| 4 | **ERASENOOP**: erase silently ignored, and emubd also ignores progs (emubd.c:704-709, 1016-1019) | as PROGNOOP. With pre-erase: the "erased" ecksum records old content, matches later, and the progs no-op, so CKPROGS relocates | CKPROGS only | badblocks and exhaustion, CKPROGS only | with pre-erase; without CKPROGS |
| 5 | **PROGFLIP**: prog lands but flips `bad_bit` when in range; the bit is re-randomized on each erase (emubd.c:712-720) | CKPROGS: relocate. Otherwise detected later by rbyd cksum, ckmeta/ckdata or CKDATACKSUMS | CKPROGS: yes. Tests also **accept `LFS3_ERR_CORRUPT`** as an outcome | test_ck_ckprogs_{mroot,data,btree,overrecycling}; test_ck_ckparity_{mroot,btree}; test_ck_ckdatacksums_data (**build broken at b10efaa**); test_ck_spam_{dir,file,fwrite,uz,uzd}_fuzz METHOD=0 | in badblock and exhaustion suites; with gbmap or pre-erase; under power loss |
| 6 | **READFLIP**: prog lands and the block becomes metastable, so reads flip `bad_bit` with p=½ (emubd.c:722-728, 424-431) | Detection only. [D] test comment "we can't detect metastable tags" | No | test_ck_ckparity_*, test_ck_ckdatacksums_data (limited) | recovery of any kind; mdir rollback due to intermittent reads |
| 7 | **MANUAL**: bits flip only on `lfs3_emubd_flip()`; prog and erase normal | Bit rot after write: detected by ckmeta, ckdata, CKFETCHES or CKDATACKSUMS → CORRUPT; no redundancy | No (detect only) | test_ck_spam_* METHOD 1-3 | recovery; bit rot in gbmap nodes |
| — | **Wear-out** (`erase_cycles`>0, emubd.c:1004-1006) | as behaviours 0-4 once exhausted. mdir overrecycling (WARN), then NOSPC | Graceful to NOSPC | test_exhaustion_spam_{dir,file,fwrite,uz,uzd}_fuzz (ERASE_CYCLES=10, BLOCK_RECYCLES=4, behaviours 0-4) | post-death remount and read; gbmap, pre-erase, flip modes; power loss |
| — | **Manual bit flip** `lfs3_emubd_flipbit` | CORRUPT on ck or fetch | detect only | test_ck_ck{meta,data}_hard, test_ck_file_ck{meta,data}_hard | gbmap tree flips; flips in `BMERASED` payloads |

### 5b. Power-loss behaviours

emubd triggers on the Nth prog or erase (emubd.c:484-676, 795-1000). "Current op" means the prog
or erase that triggered the loss.

| behaviour | emubd semantics | expected littlefs behaviour | tests | untested cells |
|---|---|---|---|---|
| **ATOMIC** | current op not applied | full recovery (COW, atomic commits, I1-I5) | every reentrant case (default `-Plinear`): test_powerloss_{spam_dir_many,spam_file_many,spam_f_pl_fuzz,spam_fd_pl_fuzz}; test_relocations_spam_{f,fd}_pl_fuzz (BLOCK_RECYCLES 4/1/0); test_grow_incr_spam_{f,fd}_pl_fuzz (GBMAP false/true in GBMAP builds); plus reentrant cases in other suites (outside this area) | **gc, trv-driven gc, pre-erase, syncgbmap, mkgbmap/rmgbmap, allocclaim, bad blocks, wear-out: all UNTESTED** |
| **SOMEBITS** | prog: only 1 bit flipped, within the first `prog_size` bytes of the prog (emubd.c:491-503). erase: 1 random bit flipped, no erase (795-806) | partial commit rejected by cksum or valid bit; partial erase: ecksum or fetch fails, block erased again | test_powerloss_* (4) only | with relocation (BLOCK_RECYCLES=-1); with gbmap (YES_GBMAP builds only); pre-erase; PROG_SIZE>1; **partial prog outside the first prog_size bytes is not modelled** |
| **MOSTBITS** | prog applied plus 1 bit flipped in the first `prog_size` bytes; erase applied (if erase_value≠-1) plus 1 random bit flipped | as SOMEBITS | test_powerloss_* (4) only | as SOMEBITS |
| **OOO** | every unsynced block except the current one reverts to the last sync; current op not applied (emubd.c:568-600, 1083-1097) | safe by I5 (sync before cross-block references) | test_powerloss_* (4) only | pre-erase (erase unsynced then gbmap sync), allocclaim, gbmap repopulation (YES_GBMAP only), relocation |
| **METASTABLE** | op applied, then a random bit anywhere in the block becomes metastable (reads flip p=½ until next prog or erase) (emubd.c:604-640, 910-945) | may corrupt *previously committed* data in the same block: mdir rollback to the other block, CORRUPT, or a mount gcksum failure; no recovery | **NONE** | **every cell UNTESTED** |

### 5c. Failure classes emubd cannot inject

All of these are UNTESTED:
- `sync` returning an error: this exercises §9 B3.
- read, prog or erase returning a non-CORRUPT error such as `LFS3_ERR_IO` or a timeout.
- Transient read errors (CORRUPT once, OK on retry): this exercises the rev=0 fallback (§9 R5).
- A partially programmed page where the first `prog_size` bytes are intact (§9 B17).
- Program or read disturb of neighbouring blocks.
- A factory bad-block table.
- A block that fails only for some offsets.

---

## 6. Test coverage map

| feature | tests | weak or untested |
|---|---|---|
| F1 lookahead alloc, checkpoint, NOSPC | test_alloc_* (8, GBMAP both); every suite indirectly | delete-after-NOSPC recovery; `gc_lookahead_thresh=0`; small `lookahead_size` (1) or large (≥ block_count/8 appears only in btree and badblock tests); alloc with `8*lookahead_size >= block_count` wraparound combined with checkpoint underflow (§9 B2) |
| F2 gbmap set, ranges | test_gbmap_set_{split,replace,merge,noop,bounds,fuzz}, test_gbmap_set_ecksum_{split,replace,merge,nomerge,fuzz} | BMBAD tag handling; fragmentation bounds; weight>1 `set_` (zerounknown path, only indirectly) |
| F3 repopulation (sync + incremental) | test_gbmap_files, test_gc_lookgbmap_{progress,mutation,relaxed}, test_trvs_* (GBMAP true), test_mount_t_lookgbmap | **`lookgbmap_thresh` ≠ BLOCK_COUNT/4 (never varied)**; repopulation failing with NOSPC then recovery (test_gc_nospc only checks data after NOSPC); repopulation under power loss (only YES_GBMAP builds, ATOMIC plus test_powerloss behaviours); READERROR during repopulation |
| F4 mkgbmap, rmgbmap, format, grow | test_gbmap_{rmgbmap,mkgbmap,rmmkgbmap,mkrmgbmap,rmgbmap_noent,mkgbmap_exist}; test_grow_* (12) | mk/rm under power loss; rmgbmap as NOSPC escape (§9 B10); lingering deltas after rm (§9 R4); grow failure other than NOSPC; grow beyond `cfg->block_count`; window restore after failed grow (§9 B6); format with bad block 2 |
| F5 pre-erase | test_gc_preerase_{progress,relaxed,decreasing}, test_gbmap_gc_files, test_mount_t_preerase, test_gc_{iflags,iflags_unck,mutation,mutation_unck,nospc,spam_*} with PREERASE | **power loss anywhere in pre-erase or allocclaim**; bad blocks during pre-erase (§9 B1); pre-erase before first checkpoint (§9 B2); remount without REVPERTURB on a pre-erased image (the claimed cross-mode compatibility, commit e3bca2b); ecksum mismatch skip path (needs a torn write); PROG_SIZE>1; noop-erase gbmap growth |
| F6 gc, ck, trv | test_gc (32), test_trvs (74), test_mount_t_* (7), test_ck_* (ck API) | any gc or trv under power loss; `gc_steps=-1` termination near full; `lfs3_fs_ck` handle cleanup when all work vanishes mid-traversal (§9 R3); `LFS3_T_PREERASE` semantics (no-op) |
| F7 error handling, relocation | test_badblocks (26), test_exhaustion (5), test_ck_ckprogs_* (4), test_relocations (8) | source-read-error relocation storm (§9 B5); bad block holding live data; sync errors (§9 B3); IO errors; "Stuck mroot" gstate revert; bad blocks with gbmap or pre-erase; badblock and power loss |
| F8 wear leveling | test_relocations (4/1/0), test_exhaustion | wear distribution (only the lifetime ratio is asserted); REVNOISE interaction; large recycles (≥ 2^10) |
| F9 exhaustion | test_exhaustion | post-death behaviour; NOSPC classification vs CORRUPT |
| on-disk gbmap format | indirectly via mount in gbmap tests | no golden-image or compat test for GBMAPDELTA or BM ranges (test_compat does not cover the gbmap) (?) not checked in detail |

---

## 7. TODO, FIXME, hack comments and unimplemented paths relevant here

- 16130-16133 `lfs3_formatgbmap`: "TODO should we try multiple blocks?". The gbmap root is fixed at
  block 2, and test_badblocks would need updating.
- 16833 `lfs3_fs_gc`: "TODO should we actually assert on these in lfs3_init?" gc_flags are validated
  only when gc runs.
- 15921, 15948 mountinited: "TODO switch to read-only?" A corrupt grm or gbmap gstate fails the
  mount; there is no degraded mount.
- 10329-10335 `lfs3_mtree_gc`: "TODO big hack! is it big enough?" This is the traversal step-back
  when mkconsistent drops an mdir.
- 10026 `lfs3_mtree_traverse_`: "TODO is this correct?" (the mid transition).
- 11822, 12004 remove and rename: "TODO is this the right thing to do?" A fixgrm failure after a
  completed remove or rename is only `LFS3_WARN`-logged; the grm stays pending until the next
  mkconsistent. **A flash failure in the second half of a remove or rename is not reported.**
- 8449, 8727, 4611: "TODO do we need to include commit overhead here?" in the estimate that decides
  compact vs split (a potential RANGE or NOSPC edge).
- 13597: "a bit of a hack" truncating the pcache to prog alignment in crystallize.
- 8974: "a bit of a hack" in the split compaction ordering.
- 15368-15370: TODOs on duplicate zeroing of gstate.
- lfs3.h:667, 686, 690: config-default TODOs (shrub, fragment, crystal).

**Not implemented or planned:**
- BMBAD writer: the tag is reserved, readers skip it, zerounknown preserves it, and nothing writes it.
- `LFS3_BADBLOCKS` ("BADBLOCKS (future)", commit ecd780a).
- A free-space API (commit 465e9fb reverted).
- Pre-erase through `lfs3_trv_read` (intentionally not implemented, commit 843412c).
- Multi-block format fallback.
- Erase verification.
- Retries.
- `lfs3_bptr_alloc` (2395) is defined but never called.
- `lfs3_rev_btree` is declared at 5543 but never defined.

---

## 8. PROPOSAL: persistent bad-block tracking in the gbmap

This section is a design sketch, not existing behaviour.

### 8.1 Goals and requirements

- G1: a block that returns `LFS3_ERR_CORRUPT` from erase, prog or ckprog readback is never
  erased or programmed again, across remounts, once the mark is persisted.
- G2: marking is advisory and power-loss safe. Losing a mark costs only a retry. A mark must never
  make a referenced block allocatable or unreadable.
- G3: no writes in RDONLY builds or mounts. Read paths must not need to allocate.
- G4: the on-disk format stays backward compatible with gbmap-aware v3 drivers.
  - Current code already treats BMBAD as in-use (11069-11083 default branch).
  - The preeraser skips it (11406-11412).
  - zerounknown preserves it (10741).
  - BMBAD therefore needs **no new compat flag**.
  - The gap: `lfs3_gbmap_setbptr` would overwrite BMBAD with BMINUSE when the bad block is still
    referenced; a pure overwrite is harmless, but the mark is lost.
- G5: bounded RAM, with no dynamic allocation.

### 8.2 On-disk representation

- Use the existing `LFS3_TAG_BMBAD = 0x0443` range entry, weight = run length, no payload.
- Optionally add a payload later: `reason` (u8: erase, prog, ckprog, read) plus a count, for
  diagnostics. An empty payload must remain valid.
- Bad marks are **window-independent**: they are valid anywhere, unlike FREE and ERASED, which are
  trusted only inside the known window. Current readers already behave this way.
- Without a gbmap there is no persistent store. Options:
  - (a) Require `LFS3_GBMAP` for `LFS3_BADBLOCKS`. This is recommended and matches ecd780a's plan.
  - (b) A RAM-only list that is lost on unmount.

### 8.3 In-RAM state and flow

- Add `lfs3->badq`, a small ring of pending bad blocks (`LFS3_BADBLOCKS_QUEUE`, e.g. 4 entries,
  compile-time). Overflow drops the oldest entry, which is safe because marks are advisory.
- **Detection sites** need a destination-specific signal instead of the overloaded
  `LFS3_ERR_CORRUPT` (this also fixes §9 B5):
  - Record the failing destination block in `lfs3_bd_erase` (568-582) and `lfs3_bd_prog_`
    (317-367), including the CKPROGS mismatch at 347-352. Use `lfs3->badblock_last = block`, or
    return an internal `LFS3_ERR_BADBLOCK` code that is translated at the API boundary.
  - Relocation loops then `goto relocate` only when the failing block is the destination.
  - A source read CORRUPT propagates as CORRUPT.
- **Enqueue** at:
  - `lfs3_alloc_` 11251-11257 (erase fail)
  - `lfs3_mdir_alloc___` 8121-8128
  - `lfs3_mdir_swap___` 8168-8171
  - `lfs3_mdir_commit__` relocate paths 8733-8781
  - split 8990-9009
  - btree relocate labels (5861, 5899, 5943, 6059, 6154)
  - crystallize relocate (13453-13620)
  - `lfs3_alloc_preerase` 11416-11430: mark and continue instead of returning, which fixes §9 B1.
- **Masking before persistence:**
  - `lfs3_alloc_findfree` skips queued blocks (a linear scan of the tiny queue).
  - `lfs3_alloc__`'s lookahead scan calls `lfs3_alloc_setinuse` for each queued block, as it already
    does for grafts (11189-11195).
  - `lfs3_gbmap_setbptr` must not downgrade BMBAD: look up first, and skip if the entry is BMBAD.
- **Flush** into `lfs3->gbmap.b` with `lfs3_gbmap_set(block, BMBAD)` at safe points:
  - (i) inside `lfs3_alloc_ckpoint` after `ckpoint_`: every block is at rest, and the gbmap commit
    may allocate, which is allowed after the checkpoint;
  - (ii) in `lfs3_fs_gc_` as a new step before pre-erase;
  - (iii) in `lfs3_alloc_lookgbmap` after zerounknown.
  - It persists with the next mdir commit, through the existing gdelta piggyback, or through
    syncgbmap.
  - The flush itself can hit a bad block: the new bad block is enqueued, and the loop is bounded by
    the queue size plus the allocator checkpoint.
- **Referenced bad blocks** (read errors or ckdata failures on live data): marking must wait until
  the data has been relocated. Either mark only destination failures in v1 and leave read-side
  (scrub) marking to a later "ck relocate" feature, or mark the block as "suspect" in RAM and let
  gc rewrite the owner.

### 8.4 API

- Build macro `LFS3_BADBLOCKS`, which requires `LFS3_GBMAP` (add a `#error` like
  lfs3_util.h:111-114). The cfg needs no runtime knob, or at most `badblock_queue_size`.
- `int lfs3_fs_mkbad(lfs3_t*, lfs3_block_t)`: mark a factory-bad or user-known block.
  - `LFS3_ERR_INVAL` if out of range or the block is 0 or 1.
  - If the block is currently referenced, either return `LFS3_ERR_BUSY` or relocate its owner (v2).
- `int lfs3_fs_mkgood(lfs3_t*, lfs3_block_t)`: clear a mark, e.g. after a bench test.
- Reporting:
  - `struct lfs3_fsinfo` gains `bad_count` (best effort, from a gbmap scan), or
  - `lfs3_trv_read` yields `LFS3_BTYPE_BAD` entries when `LFS3_T_BADBLOCKS` is set.
- Info flag `LFS3_I_BADBLOCKS`: pending marks not yet persisted (clears on sync).
- Format: `struct lfs3_cfg` gets an optional `const lfs3_block_t *badblocks` list, or a bd callback
  `isbad(block)`, so that a NAND factory-bad table is honoured before the first erase.
  `lfs3_formatgbmap` then needs to pick the first good block ≥ 2 for the gbmap root (TODO 16130).
- Anchor blocks 0 and 1: marking is meaningless because they cannot move. Keep the current
  "Stuck mroot" → NOSPC and document it as a hardware requirement ("blocks 0 and 1 must be
  reliable").

### 8.5 Read-only contexts

- `LFS3_RDONLY` builds and `LFS3_M_RDONLY` mounts never enqueue or flush.
- Detection still returns `LFS3_ERR_CORRUPT`. Optionally keep a RAM count for `lfs3_fs_stat`.
- A read-only mount must still honour BMBAD from disk. It needs nothing, because it does not
  allocate.

### 8.6 Interactions to specify

- **rmgbmap** loses every mark. Either refuse rmgbmap when BMBAD entries exist (`LFS3_ERR_BUSY`)
  or document the loss.
- **mkgbmap** starts clean.
- **grow**: new blocks are BMFREE, and a factory table may mark some of them bad.
- **Pre-erase** skips BMBAD, which already happens.
- **zerounknown** preserves BMBAD, which already happens.
- **NOSPC** when only bad blocks remain: unchanged.
- **Exhaustion**: lifetime should improve, and repeated erases of dead blocks should drop to zero.
- **Wear leveling**: none.

### 8.7 Tests needed (new test_badblocks_gbmap suite)

1. For each of behaviours 0-5 × {CKPROGS} × {gbmap}:
   - after the first failure the block's erase count (`lfs3_emubd_wear` or a per-block erase
     counter) stops increasing;
   - it stays frozen across remount and repopulation.
2. BMBAD survives `lfs3_alloc_lookgbmap`, incremental gc repopulation, zerounknown, grow and the
   setbptr-on-referenced case.
3. Reentrant (power loss at every op, ATOMIC, SOMEBITS, MOSTBITS, OOO) while marking: the fs stays
   consistent; a mark is either present or lost; it is never present on a referenced block.
4. Queue overflow with more than N simultaneous bad blocks: no crash; eventually marked.
5. RDONLY mount: no writes (emubd prog and erase counters unchanged).
6. mkbad and mkgood API: range and anchor rejection; mkbad on a referenced block.
7. Format with a factory table including block 2.
8. Pre-erase encountering an erase error: gc continues and returns 0 (regression for §9 B1).
9. A source read error during compaction returns CORRUPT, **not** NOSPC, after at most one
   relocation (regression for §9 B5).
10. Exhaustion: lifetime ratio unchanged or better; total erases of bad blocks ≤ 1 each.

---

## 9. Suspected bugs and risks (not fixed; evidence given)

Severity (H/M/L) reflects the potential impact. Confidence reflects how certain the code reading is.

- **B1 Pre-erase aborts gc, ck, mount and format on a bad block. (M; confidence high)**
  - Evidence: `lfs3_alloc_preerase` returns any `lfs3_bd_erase` or `lfs3_ecksum_read` error
    (11416-11430). `lfs3_fs_gc_` returns it (16752-16755). The preeraser does not advance, so the
    next call hits the same block.
  - `lfs3_mount(… | LFS3_M_PREERASE)` and `lfs3_format(… | LFS3_F_PREERASE)` run `lfs3_fs_ck` and
    fail (16076-16083, 16376-16383). An ERASEERROR or READERROR block in the known window therefore
    makes every gc call fail, and makes a mount with M_PREERASE fail, until allocation moves the
    window past the block.
  - Contrast `lfs3_alloc_`, which skips on CORRUPT (11251-11257).
  - UNTESTED: no badblock or pre-erase test exists.
- **B2 `lookahead.ckpoint` unsigned underflow. (M impact, L-M likelihood; confidence high on the
  underflow)**
  - Evidence: `ckpoint` is `lfs3_block_t` and is initialised to 0 at mount (15226). `lfs3_alloc_inc`
    decrements it unconditionally (11011), and the NOSPC guard is `ckpoint <= 0` (11160).
  - The only allocation that does not checkpoint first is the pre-erase `gbmap_set` commit, reached
    through a gc with PREERASE but without LOOKAHEAD pending, or through M_PREERASE or F_PREERASE
    (test_mount_t_preerase with LOOKAHEAD=false reaches this path; with the test default
    gc_preerase_count=-1, about 250 gbmap sets on a 256-block disk probably force a root compaction,
    which allocates, but I did not confirm this by running it).
  - If the gbmap root needs a new block, `ckpoint` wraps to 0xFFFFFFFF. Until the next checkpoint:
    - (a) NOSPC can no longer be detected, so a full disk can loop about 2^32 times;
    - (b) a lookahead fallback adopts `known = 8*lookahead_size`, which can wrap onto blocks allocated
      in the same pre-erase commit when `8*lookahead_size ≳ block_count`.
  - Needs a targeted test.
- **B3 `lfs3_mdir_commit_` has error returns that skip the gstate revert. (H if it happens;
  confidence M)**
  - Evidence: the "Stuck mroot" paths `return LFS3_ERR_NOSPC` (9310-9316, 9330-9336). The final
    `lfs3_bd_sync` failure does `return err` (9343-9347) instead of `goto failed`. `failed:`
    (9464-9467) is the only place that calls `lfs3_fs_revertgdelta`.
  - `lfs3->gcksum` has already been xored with the old and new mdir cksums (8919-8920, 8442-8443) and the grm
    has been played out (8891-8915), but `lfs3->mroot` and the handles were not updated.
  - The next commit then computes its `gcksumdelta` from the wrong gcksum (8419-8424). That can
    persist a mismatch that fails the mount-time check (15894-15899, `LFS3_ERR_CORRUPT`), or leave a
    phantom grm that `lfs3_fs_fixgrm` later removes: after a failed rename, the source entry could be
    deleted while the destination was never committed.
  - UNTESTED: emubd cannot fail sync, and the anchor tests stop at NOSPC without testing later
    operations.
- **B4 `lfs3_fs_consumegdelta` does not check the GBMAPDELTA lookup error. (M on error paths;
  confidence high)**
  - Evidence: at 7744-7746 the code tests `if (tag != LFS3_ERR_NOENT)` and then reads `data` even
    when `tag < 0` (an IO or CORRUPT read error).
  - `data` is stale from the grm lookup, or uninitialised: undefined behaviour. The grm branch
    directly above (7724) does check.
  - Reached at mount for every mdir, and on split or drop.
- **B5 A source read error cannot be told apart from a destination prog failure, causing a
  relocation storm. (M-H; confidence M-H)**
  - Evidence: `LFS3_ERR_CORRUPT` from reading the *old* mdir, rbyd or data during
    `lfs3_mdir_compact___` (8567+), `lfs3_rbyd_compact` → appendcompactrbyd (4900-4915),
    `lfs3_bd_progdata` or cpyck (with CKDATACKSUMS it is also a checksum mismatch), or during
    commit-time lookups, is handled by `goto relocate` exactly like a bad prog.
  - Sites: 8705-8709, 8757-8782, 8990-9009, 5868-5982, 6066-6105, 6159-6179, 13449-13622.
  - Each retry allocates **and erases** a fresh block, and the loop ends only when the allocator is
    exhausted. The result is `LFS3_ERR_NOSPC` (8795-8816 for mdirs, 11160 elsewhere) after up to
    ~block_count erases.
  - Misreported error, wasted wear, long latency. UNTESTED, because tests only mark blocks bad
    before use.
- **B6 A failed `lfs3_fs_grow` can leave the allocator window out of range. (M; confidence M)**
  - Evidence: during grow `block_count` is already the new value (16928), so `lfs3_alloc_inc` can
    move `lookahead.window` or `gbmap.window` into `[old, new)` (11005, 11016).
  - The failure path restores `block_count` (16986) and calls `lfs3_alloc_discard` (16988), but
    `alloc_discard` does not reset the window (10886-10897).
  - `findfree` returns `window` verbatim (11094, 11109). The next allocation can therefore yield a
    block ≥ `block_count`, which trips `LFS3_ASSERT(block < lfs3->block_count)` in the bd wrappers,
    or causes a real out-of-range I/O when asserts are disabled.
  - test_grow_incr_* exercises grow failing with NOSPC, but whether the window ends in the new region
    depends on allocation patterns.
- **B7 `lfs3_fs_grow` has no upper bound. (L-M; confidence high)**
  - Only `block_count_ >= lfs3->block_count` is asserted (16903). Nothing checks against
    `cfg->block_count`.
  - Growing beyond the device yields bd accesses out of range, or an fs that the next mount rejects
    (15688-15693, `LFS3_ERR_NOTSUP`). The API doc (lfs3.h:1813-1823) is silent.
- **B8 `lfs3_mdir_fetch` compares revisions wrongly on big-endian hosts. (H on BE, none on LE;
  confidence high; mostly agent 4's area)**
  - Evidence: at 7862-7869 both reads go into `revs[0]`, but the conversion is
    `revs[i] = lfs3_fromle32(&revs[i])`.
  - For i=1 this re-converts `revs[1]` (already host order) and leaves `revs[0]` raw. On BE the
    `lfs3_scmp` then compares byte-swapped revisions and can choose the older, still-valid block of
    a pair: a silent rollback.
  - On LE it is harmless. No BE testing exists for v3.
- **B10 rmgbmap is not an escape from a gbmap repopulation that fails with NOSPC. With a full disk
  and a gbmap, even deletes can fail. (M; confidence M)**
  - `lfs3_fs_rmgbmap` → `lfs3_fs_mkconsistent` → `lfs3_alloc_ckpoint` → `lfs3_alloc_lookgbmap`
    (16646-16650, 10797-10808), then `lfs3_mdir_commit` checkpoints again (9483).
  - Every mdir commit, including `lfs3_remove`, first runs the checkpoint. When
    `gbmap.known < lookgbmap_thresh` it needs new blocks for the copy-on-write gbmap tree.
  - This contradicts the rationale in commit 5d70e47 ("the user can call rmgbmap to try to make
    progress"). No test deletes after NOSPC.
- **B11 `lookgbmap_thresh` does not match its documentation. (L-M; confidence high)**
  - The code uses `gbmap.known < min(thresh, block_count)` (10800-10802). The header says "When <=
    this many…" and "0 only repopulates the gbmap when empty" (lfs3.h:704-716).
  - With the zeroed-config default of 0, a checkpoint **never** repopulates the gbmap. The allocator
    silently degrades to lookahead scans, and pre-erase stops once the known window drains, unless
    gc with LOOKAHEAD runs.
  - Tests always use BLOCK_COUNT/4.
- **B17 The pre-erase ecksum covers only the first `prog_size` bytes. (H on real NOR-like parts
  without CKPROGS; confidence M, because it depends on the hardware model)**
  - `lfs3_ecksum_read` uses `cksize = prog_size` (2491-2502). The first write to a fresh rbyd is a
    pcache flush of up to `pcache_size` bytes (376-402, 457-460).
  - A power loss in that program operation can leave bytes in `[prog_size, pcache_size)` partially
    programmed while bytes `[0, prog_size)`, which carry the perturbed rev, still read as erased.
    The ecksum then matches, and the block is later programmed without an erase.
  - emubd cannot produce this state: SOMEBITS and MOSTBITS flip bits only within the first
    `prog_size` bytes (emubd.c:493-503, 530-541).
  - The same assumption underlies the rbyd append ECKSUM (4417-4447), which is agent 4's area.
  - Must be stated as an explicit hardware requirement, or the ecksum width raised to the first-flush
    size. CKPROGS mitigates it.
- **B18 The CKDATACKSUMS build does not compile at b10efaa** (lfs3.c:1736, 1808; fixed in e4c046b).
  Every CKDATACKSUMS assertion in test_ck was unrunnable at the baseline.
- **R1 No powerloss test covers gc, pre-erase, allocclaim, syncgbmap, mkgbmap/rmgbmap, bad blocks or
  wear-out.** METASTABLE is unused. The reentrant cases in my area are only test_powerloss (4),
  test_relocations (2, ATOMIC) and test_grow (2, ATOMIC); see §5b.
- **R2 Features are untested in CI.** GBMAP, PREERASE, GC and CKPROGS are exercised only when
  someone builds with `LFS3_BIGGEST` or `LFS3_YES_*` (§0).
- **R3 `lfs3_fs_ck` can leave a stack handle linked.**
  - `lfs3_fs_gc_` exits through `break` (16772-16775) without closing `mgc->t.h` if pending work
    vanishes while a traversal is open.
  - `lfs3_fs_ck` passes a stack `mgc` (16824-16825) and never closes it. That would leave a dangling
    node in `lfs3->handles`.
  - Reachability is unclear (LOW confidence): pending bits only clear at end-of-traversal or through
    a nested adopt.
- **R4 Lingering GBMAPDELTA tags after rmgbmap are copied forward by compaction.**
  - `lfs3_mdir_compact___` copies every rid -1 tag (8583-8640), while `lfs3_rbyd_appendgdelta` skips
    the gbmap when it is disabled (7662).
  - The comment's claim that they are "cleaned up implicitly" (17076-17080) looks wrong. The effect
    is harmless extra bytes. Confidence M.
- **R5 A transient read error on a revision count resets `rev` to 0** during alloc or swap
  (8101-8106, 8147-8152).
  - The new block's revision can then compare as older than the surviving old block on the next
    mount: rollback. [D] "as long as they are consistent", i.e. the code assumes read errors are
    persistent. It is untestable with emubd.
- **R6 gbmap size is unbounded under fragmentation or noop-erase pre-erase:** one range per block.
  No limit or documentation exists.
- **R7 A single unreadable metadata block blocks every allocation.** A lookahead scan or gbmap
  repopulation traverses the whole tree (11174-11187, 11357-11372). A CORRUPT metadata block makes
  every write that needs a scan fail with CORRUPT. There is no degraded read-only mode (TODO 15921,
  15948).
- **R8 Doc and implementation drift:**
  - mkgbmap and rmgbmap return values (lfs3.h:1825-1839 vs 17004-17006, 17066-17068);
    `gc_lookgbmap_thresh` wording;
  - `LFS3_M_REVPERTURB` comment (lfs3.h:248-250);
  - `lfs3_fs_unck` doc names;
  - the read callback does not document `LFS3_ERR_CORRUPT` semantics (lfs3.h:468-469), unlike prog and erase (475, 484);
  - `LFS3_T_PREERASE` is a silent no-op;
  - `gc_steps=-1` non-termination is undocumented.
- **R9 Anchors and gbmap root are single points of failure:**
  - bad blocks 0 or 1 → "Stuck mroot" → NOSPC once the anchor must change;
  - a bad block 2, or 0/1, at format → format fails;
  - a corrupt gbmap root → every gbmap lookup fails. rmgbmap needs a checkpoint and may repopulate,
    so there is no clean recovery. A read-only mount still works because it never fetches the
    gbmap tree.
