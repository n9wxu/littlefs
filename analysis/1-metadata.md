# 1 — Metadata layer (rbyd, mdir, mtree, mroot chain, gstate) — littlefs v3-alpha

Tree: `~/Documents/littlefs`, branch `v3-work` = upstream v3-alpha `b10efaa`. Citations are
`lfs3.c:<line>` unless another file is named. `DESIGN.md`/`SPEC.md` in the tree still describe v2.

Evidence tags used throughout:

- **[D]** documented intent (header docs in `lfs3.h`, comments in `lfs3.c`)
- **[I]** implemented behaviour (read from code)
- **[T]** tested behaviour (named test case)
- **[V]** verified by me: a probe program or Python recomputation, outside the repo. The probes are in
  a scratch directory (not kept).
  Each probe `#include`s `lfs3.c` and uses `lfs3_rambd`/`lfs3_filebd`.
- **[?]** uncertain / inferred

No repository file was modified. The full test suite was not run. The baseline log
`.local/baseline_fg.log` (produced by someone else) was read.

---

## 0. Headline findings

1. **`lfs3_dir_seek(dir, 0|1)` loses every entry.** [V] `lfs3_off_t off_ = off - 2`
   (`lfs3.c:12241`) wraps to 0xfffffffe for off<2. The loop then walks to the end of the
   **whole mtree**, so reads after ".", ".." return NOENT. Probe output: `after seek(0): '.' '..'
   [end]`, `after seek(1): '..' [end]`. `test_dread_seek` (tests/test_dread.toml:189-197) seeks
   to 0 and 1 but only reads one entry after each, so the bug is not caught.
2. **Valid user input trips `LFS3_ASSERT(err != LFS3_ERR_RANGE)` at `lfs3.c:8777`.** [V] In
   both cases the commit is larger than what fits after compaction. The comment at 8772 says "upper layers
   should make sure this can't fail by limiting the maximum commit size", but they do not:
   - `lfs3_mkdir` with a name of 184–255 bytes on 512-byte blocks (default `name_limit`=255)
     crashes. `lfs3_set` with the same names is fine. 1024- and 4096-byte blocks are fine.
   - `lfs3_setattr` with ~4000-byte attrs on 4096-byte blocks crashes, on root and on files
     (3000 bytes works). `lfs3.h` documents no attr size limit.
   - With `LFS3_NO_ASSERT` the RANGE error would be treated as "split" by `lfs3_mdir_commit_`
     (8940). [?] The final result is probably `LFS3_ERR_RANGE` returned to the user; not traced
     to the end.
3. **Big-endian hosts: `lfs3_mdir_fetch` can pick the older block of a pair.** [I] Line 7864
   reads into `revs[0]`, but line 7868 converts `revs[i]`. On little-endian both values come out
   right because `fromle32` is idempotent on a host-order value. On big-endian both revisions end
   up byte-swapped, which breaks the sequence comparison. Example: 0x0f80006d vs 0x1000006d →
   wrong winner. The older block is still a valid rbyd, so the fetch returns stale metadata;
   mount then fails the gcksum check. The CI workflow lists mips/powerpc, but it is the v2 workflow
   (agent 5 report).
4. **Unknown config tags are only partly detected.** [V] `lfs3_mountmroot` scans only from
   `LFS3_tag_UNKNOWNCONFIG`=0x013b upwards (15751). Probe: 0x0100, 0x0130, 0x0132 and 0x0133
   mount OK; 0x013b and 0x0142 give NOTSUP. `test_mount_incompat_unknown_config` only uses 0x0142.
5. **Missing error check in `lfs3_fs_consumegdelta` (GBMAP builds).** [I] At `lfs3.c:7744-7755` a
   negative `tag` other than NOENT (IO/CORRUPT from `lfs3_rbyd_lookup`) is treated as found. The
   code then reads an uninitialised or stale `data`. The GRMDELTA branch just above (7722-7725)
   does check.
6. **`test_files_zero_btree` failure in the baseline is a test bug.** [I] `tests/test_files.toml:1241-1243`
   passes a rattr list with no `LFS3_RATTR_NULL`. `lfs3_mdir_commit_` walks past the end of the
   compound literal (UB) and trips `LFS3_ASSERT(lfs3_o_type(h->flags) != LFS3_TYPE_REG)` at
   `lfs3.c:9365`, which is the baseline failure. The sibling bshrub case (1113-1117) has the terminator.
7. **Build breakage in optional configs that touch this layer.** [V]
   - `-DLFS3_CKDATACKSUMS` and `-DLFS3_BIGGEST` do not compile: `data.u.disk.block` on a pointer
     at `lfs3.c:1736` and `1808`.
   - `-DLFS3_PMUL_CRC32C` does not compile: `lfs3_fromle32_` is undefined at `lfs3_util.c:210`.
   - Separately, the BNAME rattr at `lfs3.c:6044` supplies only 3 FROM_DATA args, while
     `appendrattr_` reads args[3..4] under `LFS3_CKDATACKSUMS` (3461-3465). That is a latent overrun
     once the build is fixed.
8. **Coverage gaps.**
   - No metadata-suite test runs at a block size other than 4096, except rbyd/btree at 32768.
   - `test_mtree` has no power-loss (reentrant) cases.
   - The rbyd balance invariant check (`LFS3_DBGRBYDBALANCE`, 2987-3027) is never enabled.
   - `POWERLOSS_METASTABLE` is never used.
   - `test_rbyd` never tests fetch-side validation, ecksum or perturb directly.

---

## 1. Features

Each entry gives: guarantee / inputs → outputs / errors / limits / citations.

### F1 Tag encode/decode (`lfs3_bd_progtag` 1462, `lfs3_bd_readtag` 1307)

- **[I] Write.** Emits be16 tag with bit15 = valid bit, then leb128 weight (≤5 B) and leb128 size
  (≤4 B, ≤28 bits). Valid bit = `parity(running cksum) ^ perturb` (1478-1480). The valid bit is
  excluded from the checksum by pre-XORing 0x80 into the crc state.
- **[I] Write asserts.** Bit 15 must be clear, bit 7 is reserved (1468-1469), weight ≤ 0x7fffffff,
  size ≤ 0x0fffffff.
- **[I] Read.**
  - Needs ≥4 bytes left in the block, else CORRUPT (1314).
  - Valid bit must equal `parity(cksum)` when a cksum is supplied, i.e. during fetch (1323-1333).
  - weight > 0x7fffffff → CORRUPT.
  - size > 0x0fffffff → CORRUPT.
  - A non-alt whose data overruns the block → CORRUPT (1360).
  - Optional parity check (`LFS3_CKMETAPARITY`, 1368-1438): the top bit of the byte following
    tag+data must equal the parity of that tag+data. For a tail not yet written it uses
    `lfs3->ptail` (1414-1420).
  - Returns tag with bit15 cleared.
- **[V] Valid-bit equivalence.** The valid bit equals the parity of all preceding bytes of the
  block (valid bits excluded), XOR the previous commit's perturb bit. This holds because crc32c is
  parity-preserving: its polynomial has even weight, so (x+1) divides it. Verified in Python.
- **[I] Errors.** CORRUPT (malformed or invalid), or bd errors.

### F2 rbyd append — "red-black-yellow Dhara tree" (`lfs3_rbyd_appendrattr` 3760-4385)

- **[D] Model.** An append-only log in one block. Each append writes a new *trunk*: a sequence of
  alt pointers to older trunks, then a leaf tag. A 2-3-4 (red-black) tree is emulated with a
  3-entry alt FIFO (`p_push/p_pop/p_flush/p_recolor`, 3653-3758). "Yellow" means two consecutive
  reds, i.e. a 4-node split (4151-4210). Each append writes O(log n) alts; lookup is O(log n).
- **Inputs.** `rid` and one rattr:
  - weight delta: `>0` insert (non-grow), `<0` remove, `0` update; GROW bit (0x4000 in-RAM) means
    change weight without inserting.
  - mask mode: MASK0 exact key, MASK2 `key&~3`, MASK8 suptype, MASK12 all tags of the rid
    (in-RAM 0x0000/0x1000/0x2000/0x3000, 1109-1122).
  - RM bit (0x8000 in-RAM): remove the matching tag(s). The leaf is written as NULL and made
    unreachable with an "alt-always" (4313-4335).
- **[I] Range operations.** Remove of a tag range or rid range may *diverge* into two trunks
  (lower and upper) terminated by a NULL/shrub-NULL tag and stitched together (3839-3848,
  3951-4010, 4249-4282).
- **[I] Pruning.** Unreachable black alts cannot be pruned without breaking black balance. They
  are kept as jump=0, weight=0 alts (4129-4148); unreachable red and root alts are pruned.
- **[I] Balance.** Debug-only check under `LFS3_DBGRBYDBALANCE` (2987-3027): all leaves have the
  same black height, and height ≤ 2·bheight+2 ("2·bheight+1 for normal appends, +2 with range
  removals").
- **[I] Errors.** `LFS3_ERR_RANGE` if it does not fit (`eoff + LFS3_TAG_DSIZE (+ size) > block_size`,
  3329-3333, 3578-3582). Callers treat RANGE and CORRUPT as "compact/relocate".
- **[I] Multi-rattr commits** (`lfs3_rbyd_appendrattrs` 4548): inserts after the first rattr are
  treated as splits, i.e. insert-after (4553-4557). `start_rid`/`end_rid` filter by rid, which is
  how mdir split halves receive only their own rattrs.
- **[T]** test_rbyd (107 cases): commit, lookup, get, bifoliate…sextifoliate, bflips/rflips,
  rotations, ysplits, prunes, *_permutations (N ≤ 7 exhaustive), large (ORDER 0/1/2), remove*,
  delete_range_{b,r,y,rydy,rydye,dryy}(+backwards), sparse*/grow/shrink (weights),
  subwide/supwide (mask ranges), unreachable_hole*, fuzz_{append_removes, create_deletes, mixed,
  sparse} (1000 seeds). Every case runs with ERASE_VALUE ∈ {0xff, 0x00, -1}.
  - A size bound is asserted per permutation: worst bytes/tag ≤ 12·(2·nlog2(n)+1)+4 when
    PROG_SIZE=1 (e.g. tests/test_rbyd.toml:2240-2252). That is an indirect balance check.
  - No power loss and no corruption in this suite.

### F3 Commit finalisation (`lfs3_rbyd_appendcksum_` 4388-4533)

- **[I] Middle-of-block commit.** If `off_ = alignup(eoff+23, prog_size) < block_size`:
  1. Read the first byte of the next prog unit (erased state). Set `perturb = (e>>7) ==
     parity(canonical cksum)` (4434), so erased flash never looks like a valid next tag.
  2. Checksum `prog_size` erased bytes at `off_` and append an ECKSUM tag (4436-4450).
- **[I] End-of-block commit.** Otherwise, if 11 bytes fit, write a CKSUM with no ECKSUM and set
  `off_=block_size`; the rbyd is then "unerased". Otherwise RANGE (4452-4460).
- **[I] CKSUM tag (4462-4497).**
  - tag = 0x3000 | perturb<<2 | (block & 3), called the "phase".
  - weight 0.
  - size = fully expanded 4-byte leb128 = `off_ - (eoff+7)`, i.e. 4 cksum bytes + padding.
  - le32 checksum = running crc over the tag header, XOR `LFS3_CRC32C_ODDZERO` if the
    *previous* commit was perturbed.
  - Programmed, then `lfs3_bd_flush` (4499-4510). Padding bytes are **never programmed** [V]:
    with prog_size=1 they stay erased, and the sparse image showed holes.
- **[I] After commit.** `rbyd->eoff = perturb<<31 | off_`. `rbyd->cksum` reverts to the
  *canonical* cksum, which excludes ECKSUM/GCKSUMDELTA/CKSUM (4512-4518).
- **[V] ODDZERO.** `LFS3_CRC32C_ODDZERO`=0xfca42daf is a fixed point of the crc register update
  with odd parity, so `crc(s^Z, d) = crc(s, d)^Z`. Checked in Python.

### F4 rbyd fetch — what makes a commit valid (`lfs3_rbyd_fetch_` 2708-3032)

- **[I] Scan.** Starts at off 4 with `cksum = crc32c(rev)`. For each tag:
  - check the valid bit;
  - checksum tag and data;
  - trunk tags (mode != 0x3000) update the *canonical* cksum;
  - ECKSUM and GCKSUMDELTA are remembered;
  - a CKSUM tag (suptype 0x3000) needs size ≥ 4, phase == `block & 3` and a matching le32
    (2830-2860). On success it commits eoff, trunk, weight, cksum, gcksumdelta and perturb
    (2862-2888).
  - Any failure `break`s: the scan stops at the first invalid tag.
- **Result.** The last CKSUM-validated commit wins. trunk = last *non-shrub* trunk completed
  before it (2890-2925). weight = sum of weights along that trunk.
- **[I] Erased state (2941-2960).** Erased iff `lfs3_rbyd_ckecksum` passes (2655-2682):
  - `eoff + cksize < block_size` and eoff is prog-aligned;
  - the next byte's valid bit, adjusted for perturb, must **not** equal `parity(cksum)`;
  - crc of `cksize` bytes at eoff equals the ECKSUM.
  - Otherwise `eoff = -1`: appends are refused and the next write compacts.
- **[I] Errors.** No valid commit → CORRUPT (2936-2939). bd errors other than CORRUPT propagate; a bd
  CORRUPT mid-scan just ends the scan.
- **[I] Variants.**
  - `lfs3_rbyd_fetch` (3034): trunk=0 means the latest; a nonzero trunk means stop after the
    commit containing that trunk (2756-2757, 2892).
  - `lfs3_rbyd_fetchquick` (3046): starts at the trunk, trusts the given cksum, no checksumming;
    only finds eoff, perturb and ecksum. Used when appending to btree nodes without ckfetches
    (5654-5660).
  - `lfs3_rbyd_fetchck` (3067): full fetch, then `rbyd->cksum == expected` else CORRUPT, then
    **asserts** `trunk == requested` (3098).
- **[T]**
  - test_mtree_truncated_{tag,cksum,ecksum,gcksumdelta}: hand-built malformed mroots → CORRUPT.
  - test_mount_incompat_out_of_phase (PHASE 1..4): phase bits → CORRUPT.
  - test_ck_ckmeta_{easy,hard} and test_ck_ck{fetches,parity}_{mroot,btree}: bit flips.
  - test_powerloss_* (ATOMIC/SOMEBITS/MOSTBITS/OOO): torn commits, implicitly.
  - No test asserts perturb selection or ecksum rejection directly.

### F5 Lookup

- `lfs3_rbyd_lookupnext_` (3106): finds the smallest (rid', tag') ≥ (rid, tag).
  - Tag 0 is bumped to 1 (3115) because null tags break alt comparisons.
  - Leaf rid = `upper_rid-1`, weight = `upper-lower`, data = on-disk slice.
  - NOENT past the end or on a null leaf.
- `lfs3_rbyd_lookup` (3227) matches rid exactly and compares `tag & mask(tag)`.
- `lfs3_rbyd_namelookup` (4951): binary search by rid over NAME-suptype tags.
  - Non-NAME rids count as LT.
  - Comparison is `lfs3_data_namecmp` (1775): leb128 did first, then memcmp of the name, then
    length.
  - Returns EQ, or LT/GT with the best insertion neighbour.
- `lfs3_mdir_lookup`/`lookupnext` (7981/7956) restrict to the mdir's rid and map NAME tags
  through `lfs3_mdir_nametag` (7925):
  - grm'd mid → ORPHAN;
  - STICKYNOTE not open in-sync → ORPHAN;
  - types outside REG..BOOKMARK → UNKNOWN.
- **[T]** test_rbyd lookup/get/*_traverse*; test_mtree_*; test_dirs_ordering{,_length};
  test_mount_incompat_unknown_type*.

### F6 Compaction and estimate

- **[I] `lfs3_rbyd_appendcompactrbyd` (4717).** Re-appends every tag of rids in
  [start, end) in (rid, tag) order as single-tag trunks with their full weights.
- **[I] `lfs3_rbyd_appendcompaction` (4756).** Builds a perfectly balanced tree bottom-up: each
  layer pairs trunks with an `altRle`/`altBle` (or `altBgt` for the last) and a NULL terminator,
  until one trunk remains.
  - An empty rbyd gets a single NULL tag (4768-4781).
  - Shrub trunks are skipped unless compacting a shrub (4816-4823).
- **[I] Cost.** n lookups × O(log n) reads plus O(n) writes, i.e. O(n log n) reads and O(n)
  bytes. [D] No O(n log n) claim appears in repo text; this is my derivation.
- **[I] Space estimate** (`lfs3_rbyd_estimate` 4613, `lfs3_mdir_estimate___` 8451).
  - Upper bound per tag: `rattr_estimate` = 3t+4 with t = 2 + ⌈log128(file_limit+1)⌉ +
    ⌈log128(block_size)⌉, or `mattr_estimate` for mdirs (weight ≤ 1).
  - Formula derivation: 15283-15318.
  - Shrubs count at their compacted estimate; open unsynced file shrubs are included (8525-8541).
  - Also returns a balanced `split_rid`.
  - [D] "TODO do we need to include commit overhead here?" (4611, 8449, 8727): the estimate
    excludes the pending commit, which is the root of finding 0.2.
- **[I] Shrubs.** `lfs3_rbyd_appendshrub` (4921) and `lfs3_shrub_compact` (6550) re-home
  secondary trees (tags with the 0x1000 bit) inside the mdir block during compaction.

### F7 Revision counts and metadata wear levelling (7763-7822, 3262-3316, 15247-15263)

- **[D] Requirement.** "The only strict requirement … most recent block has the most recent
  revision count, all drivers must be ok with simple 32-bit counters" (7765-7767). Comparison is
  sequence arithmetic, `lfs3_scmp` (lfs3_util.h:455).
- **[D]/[I] Layout.** See §2.11. `rev_init` bumps the top nibble and clears the rest (7787).
- **[I] Recycle counter** (7795-7820). Modulus 2^r − 1, with r =
  `nlog2(2·(block_recycles+1)+1) − 1` (15259).
  - `needsrelocation` fires when `rev + 2u` would carry into the top nibble, with
    u = 1<<(28−r).
  - The odd modulus alternates which block of the pair triggers relocation (7803-7805).
  - block_recycles=0 → r=1 → relocate on every compaction (pure COW, matches [D] lfs3.h:523-530).
  - −1 disables. r ≤ 20 is asserted (15260), so block_recycles ≲ 2^20−2.
  - Effective erases per block per mdir lifetime ≈ (2^r−1)/2, between ~b/2 and b.
- **[I] Optional bits.**
  - REVPERTURB: rev bit 7 = inverted top bit of the erased byte (3272-3285).
  - REVNOISE: XOR `gcksum_p` into bits [8, 28−r) (3292-3299).
  - Debug byte: 'm' mdir, 'b' btree (appendinit default, 3630), "hi!" anchor at format.
  - `lfs3_rev_btree` is declared (5543) but never defined or used.
- **[T]** BLOCK_RECYCLES {0,1,4}: test_mtree_relocate*/extend*/relocate_fuzz,
  test_relocations_* (incl. 2 power-loss fuzz), test_trvs_*relocate*, test_exhaustion (4).
  - Default tests use BLOCK_RECYCLES=-1 (runners/test_defines.h:14), so wear levelling is off in
    most suites.
  - REVPERTURB/REVNOISE: test_mtree (all), test_mount_flags, gc, trvs, gbmap.

### F8 mdir fetch (`lfs3_mdir_fetch` 7853-7910)

- **[I]** Reads both revs, then orders the blocks newest-first. A read CORRUPT counts as older;
  equal revs prefer `mptr[1]`.
- **[I]** Tries `rbyd_fetch_` on the newest, falling back to the other on CORRUPT.
- **Result.** `mdir->r.blocks[0]` = active block, `blocks[1]` = partner, `gcksumdelta` = the
  GCKSUMDELTA of the *last valid commit* (0 if that commit had none).
- **[I] Errors.** CORRUPT if both fail. Big-endian defect: finding 0.3.
- **[D]** A rollback to an older valid commit or block is not detected here; the gcksum catches it
  (10074-10083).

### F9 mdir commit pipeline (8067-9515)

- **`lfs3_mdir_commit___` (8186).** Appends rattrs plus internal pseudo-rattrs:
  - `RATTRS` nested list;
  - `SHRUBCOMMIT` (commit into a file's shrub trunk);
  - `GRMPUSH` (handled in commit_);
  - `MOVE` (copy every tag ≥ STRUCT of a source rid, plus bshrubs and unsynced shrubs);
  - `ATTRS` (user attrs, written only if changed).

  Then:
  - A non-mroot mdir whose weight reaches 0 aborts with NOENT, so the caller drops it (8390-8402).
  - If `start_rid ≤ −2`: append GRMDELTA/GBMAPDELTA (tree tags at rid −1, `lfs3_rbyd_appendgdelta`
    7616). Save the canonical cksum, then append a raw GCKSUMDELTA =
    `old ^ cube(gcksum_p) ^ cube(gcksum ^ cksum) ^ gcksum_d` (8404-8432).
  - `appendcksum_`, then `lfs3->gcksum ^= new cksum`.
- **`lfs3_mdir_commit__` (8684).** Copies the mdir and **claims** (eoff=−1) the source, the mroot
  copy and every handle copy (8692-8702). Tries an append; on RANGE/CORRUPT, compacts:
  - `estimate > block_size/2` → RANGE, meaning split (8728).
  - Else `swap___` (8137): erase the partner, write rev = `rev_inc`. If
    `needsrelocation` → NOSPC → relocate.
  - `compact___` (8568) with rids ≥ `start_rid`. On relocation gstate is not copied:
    `start_rid_ = max(start_rid,−1)`, and the old delta is folded into `gcksum_d` (8786-8789).
  - Then commit___ again (8774; the assert fires here, finding 0.2).
  - Relocation: `mdir_alloc___` (8079) allocates a partner block *without erase*, reads its stale
    rev, sets `rev_init(rev)|'m'` (guaranteed newer), then allocates and erases blocks[0].
  - Bad prog (CORRUPT) → next block.
  - No free block → "Overrecycling": compact in place with `force`. A bad block in the
    overrecycled pair gives "Stuck mdir" → NOSPC (8790-8822).
- **`lfs3_mdir_commit_` (8878), the atomic "high-level" commit.**
  1. Pre-applies GRMPUSH and shifts pending grm mids by rattr weights (8893-8913).
  2. Zeroes gdeltas and XORs out the old cksum.
  3. Stages bshrubs (`b_ = b.r`).
  4. `commit__(-2,-2)`. Then:
     - **Split** (RANGE): consume the old mdir's gdeltas if not mroot. Allocate and compact two
       halves [0,split) and [split,∞), each with its share of the rattrs, the half holding the
       target mid last (8954-9017). A half that ends up empty turns into drop or relocate. Read
       the first name of the right half as MNAME. Create the mtree (inlined-mroot case) or update
       the MDIR/MNAME leaves (9058-9109).
     - **Drop** (NOENT): consume gdeltas; `RM` the mtree leaf (9111-9139).
     - **Relocate**: update the MDIR leaf, or create an mtree with one leaf (9141-9177).
  5. Patch grms for mid shifts (9179-9192).
  6. If the mtree changed: `bd_sync`, then commit MTREE to the mroot with `(-2, 0)`, which drops
     leftover rid≥0 entries. When the mroot itself split, a compaction is forced because the
     first attempt claimed `lfs3->mroot` (8694-8696) [I, subtle].
  7. If the mroot moved: walk the chain via `lfs3_mroot_parent` (8826), `bd_sync`, and commit
     MROOT into each parent until a commit lands in place. If the anchor itself had to move,
     *extend* the chain: `swap___(force)` the anchor and write MAGIC + MROOT (9290-9341). A bad
     anchor block gives "Stuck mroot" → NOSPC.
  8. `bd_sync`. Then in-RAM state is updated and must not fail (9343-9452): handle mids, zombie
     flags, split/drop re-mapping, `lfs3->mroot`, `lfs3->mtree`, staged shrubs,
     `lfs3_fs_commitgdelta`, `LFS3_I_COMPACT`.
  - On failure: `lfs3_fs_revertgdelta` (9464-9467).
- **`lfs3_mdir_commit` (9480)** additionally checkpoints the allocator.
  `lfs3_mdir_compact{,_}` (9496/9505) = claim + empty commit.
- **[T]**
  - test_mtree_{mroot*, uninline*, split*, drop*, relocate*, extend*, opened_*, split/drop/relocate_fuzz}
  - test_trvs_mutation_{mroot,mtree}_{split,extend,relocate}*, test_trvs_compact*
  - test_relocations_*, test_ck_ckprogs_{mroot,overrecycling} (BLOCK_COUNT=2), test_badblocks_*
  - Power loss only via test_dirs (45 reentrant), test_relocations_{f,fd}_pl_fuzz,
    test_powerloss_*, test_stickynotes_*_pl, test_dread_recursive_{rm,mv},
    test_grow_*_pl_fuzz.

### F10 mtree and mids (6991-7018, 8001-8064, 9515-9632)

- **[I] mid.** `mid = (mdir_index << mbits) | rid`, with `mbits = nlog2(block_size) − 3`
  (15357; rationale 15320-15356).
  - `lfs3_mbid(mid) = mid | (2^mbits−1)` is the btree bid of the mdir.
  - `lfs3_mrid` maps mid ≤ −1 → rid −1.
- **[I] mtree.** A btree whose leaves are `MDIR` (mptr) tags of weight 2^mbits, plus an optional
  `MNAME` (the first name at split time; may be vestigial, 5553-5580).
  - No mtree (weight 0) means files are inlined in the mroot, mids in [0, 2^mbits)
    (`lfs3_mtree_weight` 8003, lookup 8008).
  - Name lookup: `lfs3_btree_namelookup` over BNAME/MNAME, then fetch the mdir, then
    `lfs3_mdir_namelookup` (9565).
  - Every lookup fetches (full-scans and CRCs) the mdir log. [I] This is a performance note.
- **[I] Asserts on disk-derived values** (8036-8040, 9587-9589): leaf weight == 2^mbits, bid
  alignment, tag ∈ {MNAME, MDIR}.

### F11 mroot chain and superblock (7020-7040, 8826-8866, 9194-9303, 9829-9920)

- **[I] Shape.**
  - The anchor is always mptr {0,1}. `lfs3_mptr_ismrootanchor` only checks `mptr[0] ≤ 1`.
  - Every chain mroot carries MAGIC.
  - Non-final mroots carry `MROOT` (mptr to the next); the final mroot carries config, gdeltas
    and optional `MTREE`.
- **[I] Traversal** follows MROOT tags. Brent cycle detection on the chain only (9876-9901) gives
  CORRUPT. Any other struct tag at rid −1 → "Weird mroot entry?" CORRUPT (9912-9916).
- **[I]** `lfs3_mroot_parent` has no cycle detection and asserts the chain contains the child
  (8848).
- **[T]** test_mtree_{extend, extend_twice, relocate_mroot, relocate_extend, *_extend},
  test_mtree_traversal_mroot_cycle, test_mtree_magic{,_extend,_extend_twice}. MAGIC sits at
  byte 8 of both anchor blocks: rev(4) + tag(2) + w(1) + size(1).

### F12 Global state (7446-7760)

- **[I] Triads** per gstate: current (`gcksum`, `grm`, `gbmap`), on-disk (`*_p`) and pending delta
  (`*_d`).
  - zero (7549), commit (7570), revert (7596), consume (7716), append (7616).
- **[I] gcksum.** Invariant: `XOR_i gcksumdelta_i == crc32c_cube(XOR_i cksum_i)` over all mdirs
  in the chain and mtree, where `cksum_i` is canonical.
  - Checked at mount → CORRUPT (15894-15901).
  - The cube is non-linear, so a single stale mdir cannot keep the invariant (reasoning in
    15870-15893).
  - `lfs3_fs_cksum` returns `lfs3->gcksum` (16480).
- **[I] grm (global remove).** A 2-entry queue of mids; 0 = empty (mid 0 is always the root
  bookmark). Encoded as ≤2 leb128 mids, trailing zeros trimmed; the XOR of GRMDELTAs gives the
  on-disk grm.
  - `grm_push` asserts room (7458).
  - `lfs3_data_readgrm` **asserts** `mid < mtree_weight` on disk data (7528).
  - RAM may hold extra entries pushed by `lfs3_file_close` for orphaned uncreated files
    (12857-12866). The next −2 commit persists them.
- **[I] gbmap** delta: encoding only here (window, known, branch). Allocator is out of scope.
- **[T]**
  - test_ck_cksum (stability).
  - test_ck_ckmeta_hard METHOD=3: mount detects rollback, or at least gives a different
    `fs_cksum` (tests/test_ck.toml:615-667). This documents that a rollback to a *globally
    consistent* older state is undetectable internally.
  - test_mtree_truncated_gcksumdelta.

### F13 Mount-time metadata validation (`lfs3_mountinited` 15767, `lfs3_mountmroot` 15569)

- **[I] Traversal.** MTREEONLY + CKMETA over the whole chain and mtree. Every mdir is fetched;
  every mtree inner node is `fetchck`'d. Any failure fails the mount; there is no degraded mode.
  Cost is O(#mdirs · block_size) reads.
- **[I] Per mroot.** MAGIC must be "littlefs", else "No littlefs magic found" → CORRUPT.
- **[I] Config**, read on the final mroot only:
  - VERSION major must equal, minor ≤. A missing tag counts as v0.0.
  - RCOMPAT must **exactly equal** {MMOSS, MTREE, BSHRUB, BTREE, GRM} (0x00010c90), else NOTSUP.
  - WCOMPAT must exactly equal {GCKSUM, DIR (+GBMAP)} (GBMAP optional); a mismatch is allowed on
    RDONLY mounts.
  - Truncated compat bytes read as zero; non-zero bytes beyond 4 set the overflow bit.
  - GEOMETRY is mandatory (else INVAL): block_size must match and block_count ≤ cfg.
  - NAMELIMIT/FILELIMIT ≤ configured, else NOTSUP. A corrupt leb128 counts as −1, i.e. NOTSUP.
  - Unknown config ≥ 0x013b → NOTSUP (finding 0.4).
- **[I] State set up.** gstate is consumed; cube check; grm decoded (pending grm = INFO);
  lookahead window = `gcksum % block_count` (15974).
- **[T]** test_mount_* (39): simple, flags, format_flags, t_{lookahead, lookgbmap, preerase,
  compact, mkconsistent, ckmeta, ckdata}, incompat_{no_magic, bad_magic, major, minor, rcompat,
  wcompat, ocompat, rdonly, wronly, *_overflow, *_padding, block_size, block_count, name_limit,
  file_limit, unknown_config, unknown_type*, out_of_phase}.

### F14 Format (`lfs3_formatinited` 16168, `lfs3_format` 16288)

- **[I]** Erases blocks 0 and 1 and writes one commit to each:
  - rev = `(i−1)<<28 | (0x00216968 & mask)`, so block 1 (top nibble 0) is newer than block 0
    (top nibble 0xf). This is deliberate, to exercise wraparound.
  - Tags: MAGIC, VERSION, RCOMPAT, WCOMPAT, GEOMETRY, NAMELIMIT (lleb128), FILELIMIT (leb128),
    optional GBMAPDELTA, BOOKMARK(did 0, w+1), GCKSUMDELTA = cube(cksum), CKSUM.
- **[I]** Then `bd_sync` and a full `lfs3_mountinited` as self-check.
- **[I] Limits.**
  - No assert on block_count ≥ 2 (≥3 with gbmap, which uses block 2, 16129-16163).
  - A bad block 0/1 makes format fail. There is no alternative anchor
    ([T] test_ck_ckprogs_mroot accepts CORRUPT from format).

### F15 Naming, did allocation, path lookup (9658-9770, 11479-11653)

- **[I] Key.** Global name = (did leb128, name bytes). The root did is 0.
- **[I] Bookmarks.** Every directory has a BOOKMARK entry (did, "") that sorts first among its
  children. The parent holds DIR(parent_did, name) + DID(did).
- **[I] did choice.** `(parent_did ^ crc32c(name)) & dmask`, with
  dmask = 2^min(nlog2(#mdirs) + nlog2(block_size/32), 31) − 1, then linear probing on collision
  (11560-11582).
  - [D] The comment claims 1-byte dids for a single 4 KiB mdir. [V] `lfs3_nlog2(1)` returns 1,
    not 0, so dmask=0xff: the probe's did 0x9e takes 2 bytes.
- **[I] mkdir** is 2 commits made atomic by grm:
  1. BOOKMARK + GRMPUSH (self-remove on power loss);
  2. re-lookup, `grm_pop`, then MASK12 DIR + DID (11600-11634).
- **[I] Path parsing.** Lexical: "." skipped; ".." cancels the preceding name *without checking it
  exists*; an unmatched ".." → INVAL; the empty path → INVAL (9658-9770).
- **[T]** test_dirs_{did_collisions, did_zero, did_ones, did_leb128_boundaries, ordering,
  ordering_length}; test_paths_* (not in my list: nametoolong, namejustlongenough at 4096 only).

### F16 Directory read/seek/remove/rename (11713-12300)

- **[I] remove.** A dir needs `lfs3_grm_pushdid`: NOTEMPTY if the entry after the bookmark has
  the same did (11661-11708).
  - An open uncreated child (STICKYNOTE) counts as a child. [V] dir_read shows it with type 3,
    stat returns type 3, and rmdir gives NOTEMPTY. `lfs3.h` `lfs3_info.type` documents only
    REG/DIR.
  - The remove commit then `lfs3_fs_fixgrm`. A cleanup error is only WARNed and the call returns 0
    (11816-11829).
- **[I] rename.** GRMPUSH of the old mid plus `MASK12|old_tag` + `MOVE` into the new place in one
  commit, then fixgrm (11940-12012). The destination dir gets a grm via pushdid.
- **[I] dir_read.** Skips ORPHAN and terminates on a did mismatch.
- **[I] dir_seek.** O(#entries) walk; bug for off<2 (finding 0.1). `dir_tell` = logical position.
- **[T]** test_dread_* (17), test_dirs_{rm,mv}* (reentrant).

### F17 mkconsistent / orphans (16489-16658, 10198-10390)

- **[I] `lfs3_fs_mkconsistent`.**
  1. `fixgrm`: for each pending grm, pop and commit `RM` atomically.
  2. If `LFS3_I_MKCONSISTENT` (set at every mount, 15157-15171): full mtree traversal removing
     STICKYNOTEs with no open handle (`lfs3_mdir_mkconsistent` 16538).
  3. Checkpoint the allocator.
  - Called at the start of every mutating API. The first write after mount therefore pays a
    full metadata scan unless `LFS3_M_MKCONSISTENT` ran it at mount.
- **[I]** A dropped mdir during the traversal is handled by "TODO big hack! is it big enough?"
  `mid -= 1` (10330-10337).
- **[T]** test_mount_t_mkconsistent (ORPHANS 0..100), test_gc_mkconsistent_*,
  test_trvs_mkconsistent, test_stickynotes_*, test_dirs_*_consistent.

### F18 Metadata traversal, ckmeta, gc compaction (9773-10410)

- **[I] `lfs3_mtree_traverse_`** yields MDIR (anchor, chain, each mdir), BRANCH (mtree and file
  btree inner nodes) and BLOCK. It also visits unsynced open-file btrees.
- **[I] ckmeta/ckdata** (10074-10154):
  - Each mdir cksum must equal the RAM mroot's and any open handle's.
  - The XOR of mdir cksums must equal `lfs3->gcksum` at the end, unless the fs was
    checkpointed/mutated mid-traversal.
  - BRANCH nodes are `fetchck`'d.
- **[I] gc compaction.** An mdir is compacted when `eoff > gc_compact_thresh` (0 → bs − bs/8).
  An unerased mdir has eoff 0x7fffffff, so it is always compacted (10340-10363).

### F19 Open-handle synchronisation (7366-7444, 9350-9452)

- **[I]** All open files, dirs and traversals sit on `lfs3->handles`. Each successful commit
  rewrites their `mdir` copies: mid shifts by rattr weight, zombie on removal (non-REG only; REG
  asserted), split/drop remap, mroot sync. Failed commits leave the copies claimed.

### F20 fs_grow / fs_stat / fs_usage (metadata parts)

- **[I] `lfs3_fs_grow`** (16899) commits a new GEOMETRY to `lfs3->mroot` without mkconsistent
  (rationale 16912-16920). It asserts only `block_count_ ≥ current`. **No check against
  `cfg->block_count`**; mount would then reject the fs with NOTSUP (15688-15693).
- **[I]** `lfs3_fs_usage` counts 2 blocks per mdir, may double count, and is documented best
  effort.

---

## 2. On-disk format (metadata)

### 2.1 Primitive encodings

| enc | bytes | range | notes |
|---|---|---|---|
| le32 | 4 | 32 bit | revision count, cksums, gcksumdelta, compat words |
| leb128 | ≤5 | ≤ 0x7fffffff | `lfs3_data_readleb128` (1647) rejects >31 bits. Non-canonical (over-long) encodings are *accepted* on read (lfs3_util.c:37-57) [I] |
| lleb128 | ≤4 | ≤ 0x0fffffff | "little leb128", 28 bits (1675) |
| be16 | 2 | — | tag word only |

### 2.2 Tag header (`lfs3.h:957-963`, `lfs3.c:1307-1509`, `scripts/dbgtag.py:15-79`)

```
byte0            byte1
v a c d s s s s  + t t t t t t t      then leb128 weight, leb128 size
bit15 v   valid bit (parity, §2.6); stripped after read
bit14 a   1 = alt pointer
  alt:    bit13 c = red(1)/black(0), bit12 d = gt(1)/le(0), bits11..0 = key
  non-alt: bits13..12 mode: 00 normal, 01 SHRUB (secondary trunk), 11 CKSUM-class,
           10 unused on disk
bits11..8 suptype nibble, bit7 '+' reserved (asserted 0 on write, not checked on read),
bits6..0 subtype. Keys compare as tag & 0x0fff.
```

- **[I]** Low 2 bits of many struct tags are "redund" (`lfs3_tag_redund` 1057). mptr tags use
  rr=01 (a pair). [?] Redundancy >1 is not implemented.
- Alt: weight = rid span of the side that is jumped to; size field = **backward** jump distance
  from the alt's own offset (jump=0 means an unreachable black alt).
- Leaf: weight = rid span it closes; size = data length.

### 2.3 Tag catalogue (metadata-relevant)

| tag | name | where | weight | data |
|---|---|---|---|---|
| 0x0000 / 0x1000 | NULL / shrub NULL | trunk terminator, removed placeholder | 0 | none |
| 0x0131 | MAGIC | rid −1 of every mroot | 0 | "littlefs" (8 B) |
| 0x0134 | VERSION | final mroot | 0 | u8 major, u8 minor (v0.0) |
| 0x0135/6/7 | RCOMPAT/WCOMPAT/OCOMPAT | final mroot | 0 | LE bitfield, any length. OCOMPAT is never written or read |
| 0x0138 | GEOMETRY | final mroot | 0 | lleb128 block_size−1, leb128 block_count−1 |
| 0x0139 | NAMELIMIT | final mroot | 0 | lleb128 (read as leb128) |
| 0x013a | FILELIMIT | final mroot | 0 | leb128 |
| 0x0230 | GRMDELTA | rid −1, any mdir | 0 | XOR delta of grm (≤10 B, zero-trimmed) |
| 0x0234 | GBMAPDELTA | rid −1 | 0 | XOR delta of gbmap (≤23 B) |
| 0x0300 | BNAME | btree inner nodes | branch weight | did+name of first leaf at split |
| 0x0301/2/3/4 | REG/DIR/STICKYNOTE/BOOKMARK | mdir rid ≥ 0 | 1 | leb128 did + name bytes. BOOKMARK name is empty |
| 0x0305-7 | ORPHAN/TRV/UNKNOWN | RAM only | — | — |
| 0x0330 | MNAME | mtree leaf | 2^mbits | did+name |
| 0x0400 | BRANCH | btree inner | child weight | leb128 block, lleb128 trunk, le32 cksum |
| 0x0420 | DID | DIR entry | 0 | leb128 did |
| 0x0428 | BSHRUB | file entry | 0 | leb128 weight(=size), lleb128 trunk (in this block) |
| 0x042c | BTREE | file entry | 0 | leb128 weight + branch |
| 0x0431 | MROOT | non-final chain mroot, rid −1 | 0 | mptr: 2× leb128 block |
| 0x0435 | MDIR | mtree leaf | 2^mbits | mptr |
| 0x043c | MTREE | final mroot rid −1 | 0 | btree encoding (weight, block, trunk, cksum) |
| 0x0600+t / 0x0700+t | UATTR/SATTR | any rid, incl. −1 for root attrs | 0 | user bytes; `LFS3_TAG_ATTR` maps types ≥0x80 to 0x07xx |
| 0x3000\|p<<2\|q | CKSUM | end of commit | 0 | le32 cksum, then padding. size = 4-byte fully expanded leb128 |
| 0x3100 | NOTE | reserved, never written | — | checksummed, not trunk |
| 0x3200 | ECKSUM | before CKSUM | 0 | lleb128 cksize (= prog_size), le32 crc of erased bytes |
| 0x3300 | GCKSUMDELTA | mdir commits with gstate | 0 | le32 |
| 0x4000\|c\|d\|key | ALT | trunks | subtree span | size = jump |

- **[I] Config tag gap.** 0x0100-0x0130, 0x0132 and 0x0133 are not known config tags, yet mount
  does not reject them (finding 0.4).

### 2.4 rbyd block and commit layout

```
off 0   le32 revision count
off 4   commit 0: trunk* [GCKSUMDELTA] [ECKSUM] CKSUM(+le32+padding)   -> ends prog-aligned
        commit 1: ...                                                  (appends)
        ... erased (ECKSUM covers the first prog_size bytes after the last commit)
trunk = ALT* leaf   (leaf = any non-alt, non-CKSUM-class tag incl. NULL/shrub)
```

- **[V] Worked example**: block 1 of a fresh format (4096×16, prog 1), probe `fmt.img`.

  | offset | bytes | meaning |
  |---|---|---|
  | 00 | `68 69 21 00` | rev 0x00216968 |
  | 04 | `81 31 00 08 "littlefs"` | v=1 MAGIC w0 s8 |
  | 10 | `c1 31 00 0c` | v=1 alt B LE 0x131 w0 jump 12 → 0x04 |
  | 14 | `01 34 00 02 00 00` | VERSION v0.0 |
  | … | … | … |
  | 94 | `03 04 01 01 00` | BOOKMARK w1 s1 did 0 |
  | 99 | `b3 00 00 04 da62fbdb` | GCKSUMDELTA |
  | a1 | `b2 00 00 05 01 51537d52` | ECKSUM cksize 1 |
  | aa | `b0 01 00 87 80 80 00 6f67abaf` | CKSUM phase 1, no perturb, size 7 (cksum + 3 B padding) |
  | b8 | | next commit |

- **[V]** A later commit in `ops.img` shows `cksumq1p`: the perturb bit is set, and the next
  commit's valid bits are inverted.
- **[V]** In `ops.img` the mroot tree holds rids −1 (config, `uattr 0x61`), 0 bookmark,
  1 dir+did, 2 bookmark(did 0x9e), 3 reg+bshrub. The bshrub trunk (`0x1404` shrub DATA "hello")
  lives in the same block.

### 2.5 Trunk and alt semantics (`lfs3_tag_follow` 1147, lookup 3106-3210)

- **Search state.** Bounds [lower, upper) over rids plus a tag range. Start: [0, weight),
  trunk = `rbyd->trunk`.
- **LE alt** (weight w): taken if `rid < lower+w−1`, or `rid == lower+w−1` and `key ≤ altkey`.
  - Taken: jump to `alt_off − jump`, upper = lower + w.
  - Not taken: lower += w.
- **GT alt**: taken if `rid > upper−w−1`, or `rid == upper−w−1` and `key > altkey`.
  - Taken: lower = upper − w.
  - Not taken: upper −= w.
- **Leaf.** rid = upper−1, span = upper−lower.
- **Weights.** rbyd weight = Σ weights along the trunk. rid −1 = the weight-0 tags before rid 0.
  Weight-0 tags attach to the rid of the preceding weighted tag. In mdirs the NAME-class tag
  (lowest key) carries weight 1 [V dbg output].
- **Shrub trunks** (mode 01) are ignored by fetch when locating the main trunk (2899-2904) and are
  referenced by BSHRUB trunk offsets.

### 2.6 Checksums

- **Running cksum.** crc32c (poly 0x1EDC6F41 reflected 0x82f63b78, init/fini 0xffffffff), starting
  with the 4 rev bytes. Covers every tag header (valid bit masked) and all tag data.
- **Canonical cksum** = running cksum after the last *trunk* tag (mode != 11). It is what branch
  pointers, gcksum and `rbyd->cksum` use (2885-2924, 4517).
- **Valid bit** = parity(running cksum) ⊕ perturb_prev, which by F1 = bit parity of every
  preceding byte.
- **Perturb.**
  - The writer sets p=1 when the erased byte after the commit would otherwise read as a valid tag.
  - The next commit's checksum state is XORed with ODDZERO, which flips all subsequent valid bits
    and the stored CKSUM (4491-4497, 2885-2887).
- **Phase.** CKSUM low 2 bits = block & 3, to detect shifted images (2837-2840).
- **ECKSUM.** Commits are appendable only if the next prog unit still matches the ECKSUM and does
  not start with a "valid" bit (2655-2682).
- **Mismatch** [I]: the writer emits ECKSUM whenever `off_ < block_size` (4420), but the reader
  rejects `eoff + cksize >= block_size` (2658). A commit ending exactly one prog unit before the
  block end is appendable in-session and unerased after remount. Harmless; 1 prog unit is lost.
- v2 equivalents: CRC tag + FCRC. v3 renames FCRC to ECKSUM and adds perturb, phase and the
  canonical/gcksum concepts.

### 2.7 mdir (metadata pair)

- Two blocks, mptr `{a,b}` (order-insensitive compare, 7025).
- Active block = newest rev with a valid commit.
- rid −1 holds mdir-level tags; rids ≥ 0 are one entry each (weight 1), in (did, name) order.
- The GCKSUMDELTA of the last commit only is the mdir's delta. **Every commit that must preserve
  a nonzero delta has to rewrite it**, which the −2 path does.

### 2.8 mroot chain and superblock

- Anchor {0,1} → MROOT → … → final mroot (config + gdeltas + root attrs + MTREE, or inlined
  files).
- MAGIC at offset 8 of anchor blocks after format, compaction or extension ([T] test_mtree_magic*).
- dbglfs3 prints e.g. `littlefs v0.0 4096x16 0x{1,0} w0.512` = mtree weight 0, 512 rids per mdir.

### 2.9 mtree

- Btree (branch encoding as in `lfs3.h:1018-1026`), root stored as MTREE in the final mroot.
- Leaf per mdir: [MNAME (except the first)] + MDIR, weight 2^mbits.
- Inner nodes: BRANCH (+BNAME).
- Btree nodes are single blocks with rev `'b'`. They are appended in place (old trunks stay
  valid) and relocated on every compaction (5746-5870).

### 2.10 gstate encodings

- **grm:** `leb128 mid0 [leb128 mid1]`, zero-terminated, ≤10 B.
- **gbmap:** `leb128 window, leb128 known, leb128 block, lleb128 trunk, le32 cksum`.
- **gcksum delta:** le32.
- Deltas combine by byte-wise XOR with zero padding (7626-7628, 7732, 7754;
  `scripts/dbglfs3.py:2723-2745`).

### 2.11 Revision count layout (7769-7784)

```
vvvv rrrrrrrrrr nnnnnnnnnnn p ddddddd      (r = recycle_bits bits, n = noise, p = perturb)
v: relocation counter (rev_init +1), r: recycle counter mod 2^r−1, n: REVNOISE,
p: REVPERTURB, d: debug ASCII ('m','b','h')
```

### 2.12 Differences between dbg scripts and C (relevant for SPEC)

- Python `_fetch` does not check phase bits (`scripts/dbgrbyd.py:584-680`).
- Python breaks rev ties by larger trunk; C prefers mptr[1].
- `dbgrbyd`/`dbglfs3` need the full block present. [V] They report "size 0" on sparse images from
  `lfs3_filebd`.
- `scripts/crc32c.py` recurses infinitely if `scripts/` is on `sys.path` when imported as a module
  (it imports itself as `crc32c`).

---

## 3. Invariants

### On-disk

| id | invariant | cite |
|---|---|---|
| I1 | Every rbyd block begins with a le32 rev; commit 0 starts at 4 | 3262-3316, 2721-2723 |
| I2 | Valid bit of each tag = parity(preceding bytes, excluding valid bits) ⊕ perturb_prev | F1, 1486 |
| I3 | Each commit ends with CKSUM{phase=block&3, crc}; commits end prog-aligned | 4412-4497 |
| I4 | Alts only jump backwards; jump=0 only on black unreachable alts; each trunk ends in a non-alt | 3653-3672, 4129-4148 |
| I5 | All leaves have equal black height; height ≤ 2·bh+2 (debug only) | 3020-3027 |
| I6 | Newest valid block of an mdir pair has scmp-greater rev; a freshly allocated mdir's rev beats any stale rev in its partner | 8108, 7787 |
| I7 | Non-mroot mdirs have weight > 0; each rid ≥ 0 has weight 1; mdir weight < 2^mbits (**not asserted**) | 8390-8402, 8881-8887 |
| I8 | Names globally sorted by (did, name, len); each directory's entries are contiguous after its bookmark; mid 0 = root bookmark, never removed | 1775, 16248-16250, 7461 |
| I9 | mtree weight = #mdirs·2^mbits; leaves MDIR(+MNAME) | 8036-8040 |
| I10 | Chain: anchor {0,1}; MAGIC in every chain mroot; MROOT in all but the last; no cycles | 15803-15840, 9876 |
| I11 | ⊕ gcksumdelta = cube(⊕ canonical cksums) | 15894 |
| I12 | ⊕ GRMDELTA = persisted grm; each grm mid < mtree weight | 7507-7532 |
| I13 | Config lives only in the final mroot; MAGIC at offset 8 of the anchor | 5261-5343 (tests) |

### In-RAM

| id | invariant |
|---|---|
| R1 | `lfs3->mroot.mid == -1` (8889); `lfs3->mroot`/`mtree` equal on-disk after every successful commit_ |
| R2 | After a failed or partial commit, every RAM copy of that mdir is claimed (eoff=−1) and never appended to (8692-8702) |
| R3 | `rbyd.eoff`: sign = perturb; ≥block_size = unerased; 0 with trunk 0 = uncommitted; 0 with trunk > 0 = unfetched (lfs3.h:1145-1148) |
| R4 | gstate X / X_p / X_d discipline: X_d collects deltas of mdirs that disappear (split, drop, relocation). Exactly one −2 commit lands per commit_ |
| R5 | Only one commit per `lfs3_mdir_commit_` carries gdeltas. Relocated, split and dropped mdirs use start_rid ≥ −1; the mroot or chain parent carries −2 |
| R6 | Handles list reflects every open mdir; the in-RAM update after `bd_sync` must not fail (9350) |
| R7 | grm count ≤ 2; after `lfs3_fs_mkconsistent` the count is 0 before an op pushes (mkdir/rm/rename push ≤ 2) |

### Power-loss safety arguments (all [I]; test evidence noted)

| id | argument | evidence |
|---|---|---|
| P1 | Commit atomicity: a commit is visible iff its CKSUM landed with the right crc. Earlier bytes are never rewritten, so a torn commit reverts to the previous one | [T] test_powerloss ATOMIC/SOMEBITS/MOSTBITS/OOO |
| P2 | No append onto a torn tail: the ECKSUM of the last valid commit covers the next prog unit. Any torn write there makes fetch mark the rbyd unerased. The in-RAM claim covers the same session. Assumes progs within a block are issued in increasing offset order and the first prog unit is written first | — |
| P3 | Compaction (swap): the partner is erased and rewritten with rev+; it only wins once its first CKSUM lands; until then the old block is used | — |
| P4 | Relocate / split / drop / mtree change: new blocks stay unreferenced until the mroot commit; `bd_sync` precedes it (9198, 9230, 9259). The new mdir's rev beats stale data in its unerased partner (I6) | — |
| P5 | Chain: child first, sync, then parent MROOT. Anchor extension is a swap (P3). Btree (mtree) appends keep old trunks valid because references pin (block, trunk, cksum) | — |
| P6 | Multi-commit namespace ops (mkdir, rename, rmdir, orphaned uncreated files) are covered by grm. Pending grm'd mids read as ORPHAN until `fixgrm` | [T] test_dirs reentrant, test_stickynotes_*_pl |
| P7 | Global check: a gcksum mismatch at mount means an mdir came from a different epoch (rollback or partial corruption) | [T] test_ck_ckmeta_hard |

- **Limitation** [D in test]: a rollback to a globally consistent older state passes; only the
  external `lfs3_fs_cksum` changes.

---

## 4. Configuration knobs affecting this layer

| knob | effect here | valid range / default | checked? |
|---|---|---|---|
| block_size | mbits=nlog2(bs)−3; split at estimate > bs/2 (8728); gc compaction default bs−bs/8; tag estimates | ≤0x0fffffff; multiple of read/prog size | asserted (15120-15126). **No lower bound**: bs<8 underflows mbits; bs=512 breaks mkdir with long names (0.2) |
| block_count | format uses blocks 0,1 (+2 with gbmap); on-disk GEOMETRY ≤ cfg | ≥2 implied | not asserted |
| read_size/prog_size | commits prog-aligned; ECKSUM = prog_size bytes; prog_size=block_size forces a compaction per commit (tests use this) | divides block_size | asserted |
| rcache/pcache | fetch/commit cost only | multiples | asserted |
| block_recycles | recycle counter r=nlog2(2(b+1)+1)−1; −1 off; 0 = COW | int32; −1 or 0..~2^20−2 | r≤20 asserted (15260); values < −1 assert. [D] header "rounded down to power-of-2" is approximate (F7) |
| gc_compact_thresh | gc/ck compaction trigger | 0 (→bs−bs/8), [bs/2, bs], or −1 = off | asserted only `#ifdef LFS3_GC` (15139-15146) although used by `lfs3_fs_ck` in all builds |
| gc_flags/steps | COMPACT, MKCONSISTENT, CKMETA work | flags asserted | — |
| name_limit | NAMELIMIT; mkdir/rename NAMETOOLONG | ≤LFS3_NAME_MAX (default 255, header says ≤1022, not asserted) | no check against block_size (0.2) |
| file_limit | FILELIMIT; rattr_estimate width | ≤LFS3_FILE_MAX | — |
| shrub_size, fragment_size | ≤bs/4; shrubs live in mdirs and count in estimates | asserted | — |
| Mount/format flags | M_REVPERTURB, M_REVNOISE (rev bits), M_CKPROGS (read-back → CORRUPT → relocate), M_CKFETCHES (btree fetchck), M_CKMETAPARITY (tag parity), M_MKCONSISTENT/COMPACT/CKMETA (run gc at mount), M_RDONLY (wcompat mismatch allowed) | compile-gated by LFS3_* | preerase needs revperturb (asserted 16042, 16332) |
| Compile-time | LFS3_RDONLY, LFS3_GBMAP (+YES), LFS3_CKDATACKSUMS (**does not build**), LFS3_BIGGEST (**does not build**), LFS3_DBG{RBYDBALANCE, RBYDFETCHES, RBYDCOMMITS, MDIRFETCHES, MDIRCOMMITS, BTREE*}, LFS3_NAME_MAX, LFS3_{SMALLER,FASTER,PMUL}_CRC32C (PMUL **does not build**), LFS3_NO_ASSERT (changes 0.2 behaviour) | — | — |

---

## 5. Test coverage map

PL = power-loss (reentrant, default `-Plinear`). BB = bad-block behaviours.
Default geometry for every metadata suite: read=prog=1, block 4096, 1 MiB, BLOCK_RECYCLES −1.

| feature | tests | PL | BB | gaps |
|---|---|---|---|---|
| F1 tag enc/dec | test_rbyd (all), test_mtree_truncated_* | – | – | over-long leb128 acceptance; bit7-set tags on read; weight/size limit rejection |
| F2 append/balance | test_rbyd 107 cases, fuzz 1000 seeds | – | – | balance invariant only via size bound; DBGRBYDBALANCE never built |
| F3 finalise/perturb/ecksum | implicit (ERASE_VALUE 0xff/0x00/−1 in test_rbyd, test_gc_preerase_*, test_mount_t_preerase) | indirect | – | no direct perturb, ecksum or phase-write test; last-prog-unit mismatch (§2.6) |
| F4 fetch validity | test_mtree_truncated_*, test_mount_incompat_out_of_phase, test_ck_* | test_powerloss (4 behaviours) | test_ck_ckparity/ckfetches | METASTABLE unused; no test of fetchck assert path |
| F5 lookup/namelookup | test_rbyd *lookup*, test_dirs_ordering* | – | – | — |
| F6 compaction/estimate | test_mtree_*compact*, FORCE_COMPACTION variants, test_gc_compact_*, test_trvs_compact*, test_mount_t_compact | via dirs | test_badblocks_* | commit larger than space left after compaction (0.2); estimate vs actual never asserted |
| F7 rev/wear | test_mtree_relocate*/extend* (BR=0), relocate_fuzz (4,1,0), test_relocations_* (4,1,0), test_exhaustion (4), test_ck_ckprogs_overrecycling | test_relocations_{f,fd}_pl_fuzz | test_badblocks, test_exhaustion | large block_recycles; r=20 boundary; full 32-bit rev wrap only incidental |
| F8 mdir fetch | everything | yes | yes | big-endian (0.3); equal revs |
| F9 commit/split/drop/reloc/extend | test_mtree (52), test_trvs_mutation_*, test_relocations | **test_mtree none**; indirect via test_dirs/test_powerloss/test_relocations | test_badblocks_* (every/region/alternating × PROGERROR…READFLIP) | stuck mroot anchor (only ckprogs_mroot at format); no PL+BB combined |
| F10 mtree/mids | test_mtree_split_many (N ≤ 320), traversal_* | indirect | – | mdir weight ≥ 2^mbits guard (unasserted) |
| F11 chain | test_mtree_extend*, magic*, traversal_mroot_cycle, test_trvs_*mroot_extend* | indirect | – | `lfs3_mroot_parent` on a corrupted chain |
| F12 gstate/gcksum/grm | test_ck_cksum, test_ck_ckmeta_hard, test_mount_t_mkconsistent, test_dirs_* (grm) | yes (dirs) | – | gbmap-delta consume error path (0.5); readgrm assert on bad data |
| F13 mount checks | test_mount_incompat_* (22) | – | – | config tags <0x13b (0.4); missing VERSION accepted; rcompat subset (older driver) → NOTSUP untested |
| F14 format | all suites; test_ck_ckprogs_mroot | – | yes (block 0/1) | block_count < 2/3 |
| F15 naming/did | test_dirs_did_*, ordering*, test_paths_namejustlongenough (4096 only) | yes | – | long names at small block sizes (0.2); dmask/nlog2(1) |
| F16 dir read/seek | test_dread_* (17, 2 reentrant) | 2 | – | seek(0/1) then read entries (0.1); rmdir with open uncreated child |
| F17 mkconsistent | test_gc_mkconsistent_*, test_trvs_mkconsistent, test_mount_t_mkconsistent, test_stickynotes_* | yes | – | the "big hack" drop path during fixorphans (only indirectly) |
| F18 traversal/ckmeta | test_trvs_*, test_gc_ck*, test_mount_t_ck* | – | – | — |
| F19 handles | test_mtree_opened_*, test_dread_read_with_*, test_trvs_mutation_* | – | – | — |
| F20 grow | test_grow_* | 2 pl fuzz | – | grow beyond cfg->block_count; baseline has 180 failing test_grow_incr_spam_uzd_fuzz perms (FORMAT_BLOCK_COUNT=2, "No more free space"). Grow/allocator owner to triage |

Explicitly untested or weakly tested:

- dir_seek to 0/1.
- Commits exceeding post-compaction space: long names at 512 B blocks, attrs ≈ block_size.
- Big-endian.
- Unknown config tags below 0x13b.
- rcompat exact-match rejecting subset images.
- Missing VERSION tag.
- Balance invariant.
- Direct perturb/ecksum/phase write-side checks.
- METASTABLE power loss.
- Power loss inside test_mtree scenarios.
- Combined power loss + bad blocks.
- Non-default geometries.
- gc_compact_thresh=−1.
- Error-injection on read during `lfs3_fs_consumegdelta`.
- Crafted images with bad alt jumps. Lookups follow `branch − jump` unchecked, and an
  out-of-range offset hits the `lfs3_bd_read` asserts (37-40) instead of returning CORRUPT.

---

## 6. TODO/FIXME/hack comments in this area

| line | comment |
|---|---|
| 12-14 | where scmp/sbool typedefs belong |
| 3201 | "TODO how many of these need to be conditional?" (rbyd lookup outputs) |
| 4611, 8449, 8727 | "TODO do we need to include (mdir) commit overhead here?" — root of 0.2 |
| 4619 | adopt a/b naming in appendrattr |
| 5403, 5511, 6302 | output conditionality in btree lookups |
| 5458 | dedupe btree lookupnext/parent |
| 6902 | "TODO bit of a hack" shrub estimate survival across staging |
| 7550 | make gdeltas contiguous |
| 8246 | dedupe MOVE with compact___ |
| 8974 | "this is a bit of a hack" split ordering for shrub staging |
| 10026 | "TODO is this correct?" mid transition in `mtree_traverse_` |
| 10332 | "TODO big hack! is it big enough?" mid −= 1 after mkconsistent drops an mdir |
| 11812 | ZOMBIE on traversals |
| 11822, 12004 | "TODO is this the right thing to do?" swallowing fixgrm errors after remove/rename |
| 15098, 15156, 15245, 15368-15370, 15768 | init/mount cleanup; "do we need to recalculate these after mount?" |
| 15907 | consumegdelta params |
| 15921, 15948 | "TODO switch to read-only?" on bad grm/gbmap decode at mount |
| 16089 | log configured values |
| 16130-16132 | formatgbmap single block |
| lfs3.h:1355, 1364 | grm_d/gcksum_d in RDONLY builds |

- Comment/code mismatch: 8332 says "skip readonly attrs and lazy attrs" but only readonly attrs
  are skipped.
- Unimplemented: RCOMPAT MSPROUT/MSHRUB (mdir pointer / inlined mtree) and redund>1 pairs are
  defined in `lfs3.h` but not produced; a chain mroot with an MDIR or other struct → CORRUPT
  (9912).
- Declared but not defined: `lfs3_rev_btree` (5543). Tag NOTE (0x3100) reserved, unused.

---

## 7. Suspected bugs and risks (evidence; nothing fixed)

1. **dir_seek(0/1)** — high. See 0.1. Fix direction: compute `off_` only for off≥2, else rewind.
   [V] probe.
2. **Oversized commit assert** — high for small-block or attr-heavy users. See 0.2.
   `lfs3.c:8777` (same pattern at 8761, 8993, 9006, 9228, 9281, 9329, and in `compact___` 8619-8672). Needs an up-front size limit (names vs
   block_size, attrs) or a clean NOSPC/FBIG/NAMETOOLONG. [V] probes 2-4.
3. **BE rev decode** — high on BE targets. See 0.3. `lfs3.c:7863-7868`. [I] static analysis.
4. **Unknown config tags <0x13b ignored** — medium (forward compatibility). `lfs3.c:15751`. [V]
5. **GBMAPDELTA lookup error unchecked** — low/medium (GBMAP only, IO-error path). `lfs3.c:7744-7755`. [I]
6. **test_files_zero_btree missing terminator** — test bug causing a baseline failure.
   tests/test_files.toml:1241-1243. [I]
7. **Assertions on on-disk data** — robustness against foreign or crafted images:
   - `lfs3_data_readshrub` (6519);
   - `lfs3_data_readgrm` (7528);
   - `lfs3_rbyd_fetchck` trunk (3098);
   - mtree leaf checks (8036-8040, 9587-9589);
   - `lfs3_mroot_parent` (8850);
   - alt-jump following (3184 and 4236 `branch_ != branch`, and bd_read bounds asserts).
   All become UB or crash with `LFS3_NO_ASSERT`. [I]
8. **rcompat exact match** (15614): an image missing a flag this driver supports is rejected.
   [D] lfs3.h:920-926 says "must understand", not "must equal". Low. [I]
9. **Missing VERSION accepted as v0.0** (15572-15594). Low. [I]
10. **ECKSUM write/read boundary mismatch** (4420 vs 2658). Low, wasted space only. [I]
11. **`lfs3_fs_grow` has no upper bound** vs `cfg->block_count` (16903): the fs becomes
    unmountable with the same cfg. Low/medium. [I]
12. **`nlog2(1)=1`** (lfs3_util.h:411-415) contradicts its own comment and the did-size rationale
    (11552-11556). Cosmetic, costs did bytes. [V]
13. **No lower bound on block_size / block_count asserted**; mbits underflow for bs<8. Low. [I]
14. **gc_compact_thresh validated only with LFS3_GC** (15139-15146) but used by `lfs3_fs_ck`.
    Low. [I]
15. **Mount cost.** Full traversal and fetch of all mdirs at mount, plus a full orphan scan on the
    first write after every mount (`LFS3_I_MKCONSISTENT` set in `lfs3_init` 15159). A
    performance characteristic to state in requirements. [I]
16. **Swallowed fixgrm errors** after remove/rename (11816-11829, 11998-12011): the op returns 0
    with a pending grm (harmless on disk, fixed on next write). Documented TODO. [I]
17. **Stickynote visibility vs docs.** dir_read/stat report `LFS3_TYPE_STICKYNOTE` (3) for open
    uncreated files. `lfs3_info.type` docs (lfs3.h:721) say REG or DIR only. rmdir then gives
    NOTEMPTY. [V] Doc divergence; owner is the stickynotes/files analysis.
18. **Build/config rot** — see 0.7. [V]
19. **Rollback to a consistent older state is undetectable internally** (by design; exposed via
    `lfs3_fs_cksum`). Must be a documented limitation in requirements. [T] test_ck_ckmeta_hard
    comments.
