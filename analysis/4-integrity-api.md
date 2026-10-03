# 4 - Integrity and the filesystem-level API (littlefs v3-alpha)

Analyst notes for the requirements document, DESIGN.md/SPEC.md (v3) and the
test plan. Scope: checksums and gcksum, superblock/config, format/mount,
fs-level calls, paths and directories, custom attributes, KV API,
compile-time options, traversal API (not the gc internals).

## 0. Provenance, legend, method

- Code base: `v3-work` = upstream v3-alpha `b10efaa`. **All findings and line
  numbers describe `b10efaa`.**
- While I worked, the checked-out branch became `v3-fixes`. Its commits are
  in-place edits, so line numbers are unchanged:
  - `2574f54`: test_grow use-after-free;
  - `8b2a82f`: `lfs3_fromle32` cast;
  - `e4c046b`: fixes the CKDATACKSUMS build (R6, first item);
  - `ba31df7`: FROM_BRANCH buffer (R18).
- At `ba31df7`, `-DLFS3_BIGGEST`, `-DLFS3_CKDATACKSUMS` and
  `-DLFS3_RDONLY -DLFS3_CKDATACKSUMS` compile cleanly and the
  `-Warray-bounds` warning is gone. Every other R-item still holds at
  `ba31df7`: the code at the cited lines is unchanged.
- Tags used:
  - **[DOC]**: documented intent (lfs3.h comments, test comments).
  - **[IMPL]**: implemented behaviour (lfs3.c).
  - **[TEST]**: exercised by `suite::case`.
  - **[PROBE]**: I checked it with a standalone program that `#include`s
    `lfs3.c`, using a RAM block device (4096 B x 64 blocks, read/prog 16,
    rcache/pcache 64, fcache 64, lookahead 16). The programs were built in the
    session scratchpad, not in the repo; each probe is described with enough
    detail to reproduce it as a test case.
- **How the baseline suite was built.** `runners/test_runner` has no
  `lfs3_fs_gc`, gbmap or ck* symbols (`nm`). So it was built with the
  **default configuration**: no `LFS3_GC`, `LFS3_GBMAP`, `LFS3_CK*`,
  `LFS3_REV*`, `LFS3_PREERASE` or `LFS3_BLEAFCACHE`. `runner -L` confirms that
  large parts of this area never ran (section 5.3).
- `.github/workflows/test.yml` is still the v2 workflow: it uses
  `-DLFS_READONLY`, `-DLFS_THREADSAFE`, `-DLFS_MIGRATE` and `lfs.*.csv`
  targets (lines 113-202). No v3 compile-time option is built in CI.
- `README.md` is the v2 README (`lfs.h`, `lfs_mount`). `DESIGN.md` and
  `SPEC.md` are v2 documents.

---------------------------------------------------------------------------

## 1. Features and API contracts

### 1.1 Common conventions

- Errors are negative `enum lfs3_err` (lfs3.h:79-98): OK 0, UNKNOWN -1,
  INVAL -22, NOTSUP -95, BUSY -16, IO -5, CORRUPT -84, NOENT -2, EXIST -17,
  NOTDIR -20, ISDIR -21, NOTEMPTY -39, FBIG -27, NOSPC -28, NOMEM -12,
  NOATTR -61, NAMETOOLONG -36, RANGE -34. Byte counts are returned as
  positive `lfs3_ssize_t`.
- **Precondition violations are `LFS3_ASSERT`s, not error codes [IMPL].**
  Examples: using a handle that is not open, unknown flags, write flags on a
  RDONLY mount, the forbidden `O_MODE==3`, `fs_grow` shrinking, `file_ck` on a
  WRONLY file, and `unmount` with open handles. With `LFS3_NO_ASSERT` these
  become undefined behaviour (section 7, R12).
- Mutating calls first call `lfs3_fs_mkconsistent`: `mkdir` 11481, `remove`
  11715, `rename` 11837, `setattr` 12366, `removeattr` 12421, `set`, and
  file open for writing (12596). It fixes pending grms and orphans, then
  checkpoints the allocator. On a RDONLY mount it **asserts** (16620).
- Every `lfs3_t` is independent. **No locking is implemented** (see
  `LFS3_THREADSAFE`, 4.2).
- `lfs3_cfg` must be zeroed for defaults, and `cfg`/`lfs3` must outlive the
  mount [DOC lfs3.h:1403-1420].

### 1.2 Lifecycle

**`int lfs3_format(lfs3_t*, uint32_t flags, const struct lfs3_cfg*)`**
(lfs3.h:1406; lfs3.c:16288-16396; `!LFS3_RDONLY`)
- `LFS3_YES_*` macros force the matching `F_*` flags (16290-16310).
- Allowed flags: `F_RDWR` (0), `F_GBMAP`, `F_REVPERTURB`, `F_REVNOISE`,
  `F_CKPROGS`, `F_CKFETCHES`, `F_CKMETAPARITY`, `F_CKDATACKSUMS`,
  `F_MKCONSISTENT`, `F_LOOKAHEAD`, `F_PREERASE`, `F_COMPACT`, `F_CKMETA`,
  `F_CKDATA`. Any other flag asserts (16312-16330). `PREERASE` without
  `REVPERTURB` asserts (16331-16333).
- Steps:
  1. `lfs3_init` (config asserts, buffer allocation; 15082-15393).
  2. `lfs3_formatinited` (16168-16284). If `F_GBMAP`, the gbmap is written to
     **block 2** (16129-16165; "TODO should we try multiple blocks?"). Then
     blocks 0 and 1 are each erased and written as a complete mroot anchor.
     The revision is `((i-1)<<28) | (0x00216968 & mask)`, so block 0 is
     `0xf0216968` and block 1 is `0x00216968`. Both copies hold magic,
     version, rcompat, wcompat, geometry, name limit, file limit, the
     optional gbmapdelta, the root bookmark (did 0), a `GCKSUMDELTA` equal to
     `cube(cksum)` and a commit cksum. Then `bd_sync`.
  3. `lfs3_mountinited` checks that the new image mounts (16361-16365).
  4. Optional `lfs3_fs_ck(flags & gc-flags)` (16367-16387).
  5. `lfs3_deinit`. The filesystem is **not** left mounted.
- The ck/rev flags affect only how format writes. **They are not persisted.**
  The only persisted feature choice is `WCOMPAT_GBMAP`.
- Errors: NOMEM (buffers), IO or CORRUPT from the bd, CORRUPT/NOTSUP from the
  self-mount, anything from `fs_ck`.
- Limits [IMPL, PROBE]: `block_count < 2` asserts in `lfs3_bd_erase`
  (lfs3.c:570). With `F_GBMAP`, `block_count < 3` asserts. There is no INVAL
  check.

**`int lfs3_mount(lfs3_t*, uint32_t flags, const struct lfs3_cfg*)`**
(lfs3.h:1418; lfs3.c:15981-16110)
- `LFS3_YES_RDONLY/FLUSH/SYNC/REV*/CK*` force the matching `M_*` flags
  (15983-16009).
- Allowed flags: `M_RDWR`, `M_RDONLY`, `M_FLUSH`, `M_SYNC`, `M_REVPERTURB`,
  `M_REVNOISE`, `M_CKPROGS`, `M_CKFETCHES`, `M_CKMETAPARITY`,
  `M_CKDATACKSUMS`, and the gc flags `M_MKCONSISTENT`, `M_LOOKAHEAD`,
  `M_PREERASE`, `M_COMPACT`, `M_CKMETA`, `M_CKDATA` (16011-16029).
  `M_RDONLY` combined with MKCONSISTENT/LOOKAHEAD/PREERASE/COMPACT asserts
  (16030-16038). `PREERASE` without `REVPERTURB` asserts.
- `lfs3_mountinited` (15767-15979) does the following:
  1. Traverses the whole mroot chain and mtree with
     `T_RDONLY|T_MTREEONLY|T_CKMETA`. Every mdir and mtree inner node is
     fetched with checksum validation.
  2. For each mroot (`mid <= -1`) it requires the `MAGIC` tag to equal
     `"littlefs"`, otherwise **CORRUPT** "No littlefs magic found"
     (15807-15827).
  3. The last mroot (no `MROOT` tag) becomes `lfs3->mroot`, and its config is
     validated by `lfs3_mountmroot`.
  4. It XORs every mdir cksum into `gcksum` and consumes grm/gbmap/gcksum
     deltas.
  5. It checks `crc32c_cube(gcksum) == gcksum_d`, otherwise **CORRUPT**
     "Found gcksum mismatch" (15894-15900).
  6. It decodes the grm and logs pending grms (fixed lazily later), decodes
     the gbmap if `I_GBMAP`, and seeds the lookahead window. Without a gbmap
     the window is `gcksum % block_count`, a pseudo-random wear spread
     (15966-15975).
  7. Cycles in the mroot chain → CORRUPT, via Brent's algorithm
     (9876-9892).
- Config validation in `lfs3_mountmroot` (15569-15765):

| Check | Result |
|---|---|
| `VERSION` major ≠ 0 or minor > 0. A missing tag reads as v0.0; a 1-byte tag gives minor = 0 [PROBE]. | **NOTSUP** |
| `RCOMPAT` ≠ `MMOSS\|MTREE\|BSHRUB\|BTREE\|GRM` (0x00010c90). **Exact equality** is required: `rmask = ~0` (15441-15444, 15611-15620). | **NOTSUP**, also for RDONLY mounts |
| `WCOMPAT` ≠ `GCKSUM\|DIR[\|GBMAP]` (0x01040000). `GBMAP` is masked out unless `LFS3_YES_GBMAP` (15446-15459). | **NOTSUP** unless `M_RDONLY` (then only a WARN, 15635-15652) |
| OCOMPAT | never checked |
| `GEOMETRY` missing | **INVAL** (15664-15670, [PROBE]) |
| `block_size ≠ cfg->block_size` | **NOTSUP** |
| disk `block_count > cfg->block_count` | **NOTSUP** |

- Further config rules:
  - A smaller on-disk block count is allowed and becomes
    `lfs3->block_count` (15679-15695).
  - `NAMELIMIT` (default 0xff when absent) greater than `lfs3->name_limit`
    → NOTSUP. The same for `FILELIMIT` (default 0x7fffffff). A corrupt
    leb128 is treated as -1 and so rejected. The on-disk values then **replace**
    the configured ones (15698-15748).
  - An unknown config tag is found only through
    `lookupnext(LFS3_tag_UNKNOWNCONFIG=0x013b)` with suptype 0x0100 → NOTSUP
    (15750-15764). Section 7, R9, covers the gap.
- Then, if any gc flag was passed, `lfs3_fs_ck(flags & gc-mask)` runs. A
  failure (for example CORRUPT from `M_CKMETA`) fails the mount and frees
  resources (16067-16087).
- Errors: NOMEM, IO, CORRUPT (no magic, no valid commit in both anchor
  blocks, gcksum mismatch, cycle, bad cksum under `M_CK*`), NOTSUP, INVAL.
- **There is no degraded or read-only fallback on CORRUPT** ("TODO switch to
  read-only?" 15921, 15948).

**`int lfs3_unmount(lfs3_t*)`** (lfs3.h:1425; lfs3.c:16112-16122)
- Asserts that no handles are open; the gc traversal handle is the only
  exception.
- It only frees buffers. It does **not** sync, and it never touches disk
  [DOC "Does nothing besides releasing any allocated resources"].

### 1.3 Filesystem-level calls

**`int lfs3_fs_stat(lfs3_t*, struct lfs3_fsinfo*)`** (1742; 16403-16444)
- Never goes to disk.
- `flags` = `lfs3->flags` masked to the `I_*` set, plus `I_MKCONSISTENT` if
  grms are pending, plus `I_PREERASE` if `alloc_canpreerase`.
- `block_size` comes from cfg. `block_count`, `name_limit` and `file_limit`
  are the mounted (on-disk) values.
- [PROBE] A fresh RDWR mount reports `0x3b00`: MKCONSISTENT, LOOKAHEAD,
  COMPACT, CKMETA, CKDATA. A RDONLY mount reports `0x3b01`, which advertises
  mkconsistent/lookahead/compact work that can never run (R25). After
  `fs_ck(CKMETA|CKDATA)` it reports `0x0b01`.

**`lfs3_sblock_t lfs3_fs_usage(lfs3_t*)`** (1750; 16446-16478)
- Runs a full `T_RDONLY` traversal. It counts an MDIR as 2 blocks, a BRANCH
  as 1 and a BLOCK as 1. Duplicates from CoW sharing are counted again
  [DOC "best effort"]. The gbmap tree is included when enabled.
- Returns the count or a negative error.
- [TEST] only `used >= 0` is asserted (test_grow, 4 cases).

**`int lfs3_fs_cksum(lfs3_t*, uint32_t *cksum)`** (1766; 16480-16484)
- Returns the in-RAM `gcksum`, the XOR of the canonical cksums of all mdirs.
  It always returns 0.
- [DOC 1752-1765] calls it "a checksum of all metadata + data". Data is
  covered only **indirectly**: bptr checksums and inline data live inside
  mdirs or btree nodes whose checksums chain up to the mdirs. It is 32 bits
  and order/history sensitive.
- [TEST] test_ck::test_ck_cksum checks that it is stable across a remount.
  test_ck hard cases check that it changes after a detected rollback.

**`int lfs3_fs_mkconsistent(lfs3_t*)`** (1777; 16618-16657; `!RDONLY`)
- Asserts RDWR. Runs `fixgrm` if grms are pending (16489-16535), and
  `fixorphans` (an MTREEONLY traversal with `T_MKCONSISTENT`) if
  `I_MKCONSISTENT` is set. Then runs `alloc_ckpoint`.
- Errors: IO, CORRUPT, NOSPC.
- [TEST] test_gc, test_powerloss, test_stickynotes.

**`int lfs3_fs_ck(lfs3_t*, uint32_t flags)`** (1787; 16789-16829)
- Flags: `CK_MKCONSISTENT`, `CK_LOOKAHEAD`, `CK_PREERASE`, `CK_COMPACT`,
  `CK_CKMETA`, `CK_CKDATA` (unknown flags assert). Write work on a RDONLY
  mount asserts.
- It first **re-arms** `I_CKMETA`/`I_CKDATA` (16822), so a check always runs.
  It then calls `lfs3_fs_gc_` with unbounded steps.
- Returns CORRUPT on the first checksum or gcksum mismatch. The `I_CK*` flag
  stays set on failure.
- The other work (mkconsistent/lookahead/compact) is done only if the
  matching `I_*` flag is pending.
- [TEST] test_ck (METHOD=0), test_gc.

**`int lfs3_fs_gc(lfs3_t*)`** (1798; 16832-16868; `LFS3_GC` only)
- Runs `cfg->gc_flags` work for `cfg->gc_steps` steps (0 means 1, -1 means
  until done).
- When nothing is pending it may still pre-erase or sync the gbmap
  (16748-16780); this happens even with `gc_flags = 0`.
- Details are outside my area.

**`int lfs3_fs_unck(lfs3_t*, uint32_t flags)`** (1811; 16871-16895)
- Takes `GC_*`-valued flags and ORs them into `lfs3->flags`, so the work is
  redone. It clears the same flags from an in-progress `lfs3->gc` traversal
  so that a partial scan cannot clear them.
- Always returns 0.
- [DOC] The header text names the non-existent `lfs3_gc_unck` and
  `LFS3_I_CANCKMETA`/`CANCKDATA` (stale, R23).

**`int lfs3_fs_grow(lfs3_t*, lfs3_size_t block_count)`** (1822; 16899-16999)
- Asserts RDWR and asserts `new >= current`. Equal is a no-op.
- Sets `lfs3->block_count` and discards the lookahead. With a gbmap it
  extends it: either grows the trailing BMFREE range or appends a new one.
  It commits `GEOMETRY` to the **mroot only**. It deliberately does *not*
  call mkconsistent, so a stuck filesystem can still be grown (16909-16917).
- On error it restores the block count and the gbmap.
- **It does not check `new <= cfg->block_count`** (R4).
- [DOC "This is irreversible"].
- [TEST] test_grow: 12 cases, including powerloss `*_pl_fuzz`.

**`int lfs3_fs_mkgbmap(lfs3_t*)` / `int lfs3_fs_rmgbmap(lfs3_t*)`**
(1830/1838; 17002-17061 / 17064-17099; only with `LFS3_GBMAP` and without
`LFS3_YES_GBMAP`)
- `mkgbmap` returns EXIST if a gbmap already exists. Otherwise it runs
  mkconsistent, builds an all-free gbmap, runs `alloc_ckpoint` (which
  repopulates it) and commits `WCOMPAT|GBMAP` atomically with the gbmap
  gstate. On failure it clears the flag and resets the gbmap.
- `rmgbmap` returns NOENT if there is none. Otherwise it commits wcompat
  without GBMAP. Stale gbmap deltas stay in mdirs until compaction ("garbage
  gdeltas", 17081-17084), and `consumegdelta` keeps XOR-ing them in.
- [TEST] test_gbmap only, which is not built by default.

### 1.4 Path resolution (`lfs3_mtree_pathlookup`, lfs3.c:9643-9771)

**Resolution rules [IMPL; TEST test_paths]:**

1. An empty path `""` → **INVAL** (9668-9671; test_paths_empty).
2. Leading `/` is ignored. Paths are always relative to the root, and there
   is no cwd (test_paths_absolute). Repeated `/` collapse
   (test_paths_redundant_slashes).
3. `.` components are skipped (9680-9684).
4. A `..` that would go above the root → **INVAL** (9686-9689;
   test_paths_root_dotdots).
5. `name/..` pairs cancel **lexically before lookup** (9691-9716). So
   `missing/../f` → `f` (0), and `file/../f` → `f`. This is a documented
   POSIX deviation (test_paths.toml:2016-2028) [PROBE both return 0].
6. A trailing `/`:
   - on an existing file → **NOTDIR** (9725-9730);
   - on create (`open O_CREAT`, `rename` to a new name when the source is
     not a dir) → **NOTDIR** (12648-12651, 11874-11876);
   - `mkdir("a/")` is allowed.
7. A non-directory in the middle of a path → **NOTDIR**. An orphan
   (stickynote with no open in-sync handle, or a pending grm) in the middle →
   **NOENT**.
8. A missing parent → **NOENT**, with `islast(path)` false.
9. The root resolves to `TAG_DIR` with `mdir.mid == -1`. `stat("/")` gives
   name `"/"`.
10. Name length is checked only on **creation** (mkdir 11501-11505, open
    `O_CREAT` 12654-12658, rename to a new name 11879-11882) → **NAMETOOLONG**
    if `strcspn(name, "/") > lfs3->name_limit`. Looking up a too-long name
    gives **NOENT** (test_paths_nametoolong).
11. Names are arbitrary bytes except `/` and NUL, including non-UTF-8, `0xff`
    and control characters (test_paths_nonutf8, oopsallffs, nonprintable).
    Only `.`, `..` and `/` are special.
12. Name-tag mapping (`lfs3_mdir_nametag` 7925-7954):
    - a pending grm → ORPHAN;
    - a STICKYNOTE with no open in-sync handle → ORPHAN;
    - a tag outside REG..BOOKMARK → `LFS3_tag_UNKNOWN`.

    APIs pretend orphans do not exist. `mkdir`/`open O_CREAT` reuse an
    orphan's slot.

### 1.5 Namespace calls

**`int lfs3_mkdir(lfs3_t*, const char *path)`** (1661; 11479-11600)

| Condition | Result |
|---|---|
| parent missing | NOENT |
| parent not a dir | NOTDIR |
| name exists, including `/`, `.` and the root | EXIST |
| empty path | INVAL |
| going above the root | INVAL |
| name longer than `name_limit` | NAMETOOLONG |
| write failures | NOSPC, IO, CORRUPT |

- Directory ids: `did = (parent_did ^ crc32c(name)) & dmask`, where
  `dmask = 2^(nlog2(mdirs) + nlog2(block_size/32)) - 1`. Collisions are
  resolved by linear probing. The loop has no termination guard (R30).
- Atomicity uses two commits:
  1. The bookmark is committed together with a self-grm, so a power loss
     removes it.
  2. The name and `DID` are committed while the grm is cancelled.
- If the target was an orphan, open uncreated handles on it become zombies.
  Open dir positions are adjusted.
- [TEST] test_dirs mkdir_* (reentrant/powerloss), test_dirs_did_* (did
  collisions, zero, all-ones, leb128 boundaries), test_paths.

**`int lfs3_remove(lfs3_t*, const char *path)`** (1455; 11713-11832)

| Condition | Result |
|---|---|
| missing, or an orphan | NOENT |
| the root | **BUSY** (11733-11736; test_dirs_rm_root, test_paths_root) |
| a non-empty dir | NOTEMPTY (grm_pushdid 11659-11710) |

- A dir is removed atomically: the entry is removed and the bookmark is
  queued in the grm in the same commit, then `fixgrm` cleans the bookmark.
- A file open elsewhere becomes a **stickynote** (zombie). Handles are
  marked `ZOMBIE|UNCREAT|UNSYNC|DESYNC`, open dirs on a removed dir become
  zombies, and dir positions are adjusted.
- An error from `fixgrm` after a successful remove is **swallowed** and only
  logged as a WARN (11818-11828, TODO). The call still returns 0 (R26).

**`int lfs3_rename(lfs3_t*, const char *old, const char *new)`** (1465;
11835-12014)

| Condition | Result |
|---|---|
| old missing or an orphan | NOENT |
| old or new is the root | **BUSY** |
| file → existing dir | ISDIR |
| dir → existing non-dir | NOTDIR |
| file → new name with a trailing `/` | NOTDIR |
| new name too long | NAMETOOLONG |
| dir → non-empty dir | NOTEMPTY |
| same mid | no-op, 0 |

- Replacing an existing entry of the same kind is allowed, including an
  empty dir over an empty dir, and reg ↔ stickynote.
- **Atomic [DOC/IMPL]:** the old mid is pushed to the grm. A single commit
  to the destination mdir writes the new name and MOVEs all of the source's
  tags, plus `grmdelta`. A power loss after it leaves the source hidden by
  the grm, and `fixgrm` removes it later. A power loss before it changes
  nothing.
- Unknown-type entries can be moved (11931-11940; test_mount_incompat_unknown_type_mv_*).
- Open handles follow the move.
- **There is no check that `new` lies inside `old`'s own subtree** (R1,
  [PROBE] data loss).

**`int lfs3_stat(lfs3_t*, const char *path, struct lfs3_info*)`** (1472;
12057-12081)
- `type` = tag subtype: 1 REG, 2 DIR, 3 STICKYNOTE (an uncreated file that is
  open), 7 UNKNOWN.
- `size` is the REG size (the first leb128 of the struct), and 0 otherwise.
- `name` is the final component, NUL-terminated, at most `LFS3_NAME_MAX`. The
  root gives `"/"`.
- Errors: NOENT (including orphans), NOTDIR, INVAL.

### 1.6 Directory iteration

- `lfs3_dir_open` (1668; 12087-12138)
  - Errors: NOENT, NOTDIR (the target is not a dir), INVAL.
  - It reads the `DID`; the root is did 0. It rewinds to just after the
    bookmark and registers the handle.
- `lfs3_dir_close` (1674; 12140-12146): unlinks the handle. It asserts that
  the handle is open.
- `lfs3_dir_read` (1681; 12148-12222)
  - pos 0 gives `"."` and pos 1 gives `".."`; both are DIR with size 0. Then
    entries follow, ordered by name within the did [TEST test_dirs_ordering].
    Orphans are skipped.
  - Returns NOENT at the end, which is idempotent (test_dread_read_idempotent),
    and NOENT if the dir was removed (zombie).
  - The iterator is positional: it scans mids until the did changes. It is
    kept consistent under concurrent mkdir/rm/mv through handle position
    fix-ups [TEST test_dread_read_with_*].
- `lfs3_dir_tell` (1697; 12269-12274): returns `pos`, which counts the two dot
  entries. It is only meaningful to `seek` [DOC].
- `lfs3_dir_seek` (1689; 12224-12267)
  - Rewinds, then advances `off-2` mids across mdirs.
  - On a zombie it is a no-op returning 0.
  - A seek beyond the end leads to NOENT on the next read.
  - **`off` of 0 or 1 is broken** (R2, [PROBE]).
- `lfs3_dir_rewind` (1702; 12276-12301): a namelookup of the bookmark; pos
  becomes 0. On a zombie it is a no-op.

### 1.7 Custom attributes

- An attr `type` byte becomes tag `LFS3_TAG_ATTR(t) = 0x0600 |
  ((t&0x80)<<1) | (t&0x7f)` (lfs3.h:914-917):
  - 0x00-0x7f → UATTR 0x0600-0x067f;
  - 0x80-0xff → SATTR 0x0700-0x077f.
- [DOC lfs3.h:766-768] "0x00-0x7f Free for custom attributes; 0x80-0xff May
  be assigned a standard attribute". The reserved range is **0x80-0xff**, not
  0x80-0xbf. **It is not enforced:** the user API can set 0x80 and 0xff
  [PROBE].
- Attributes live on the entry's mid. Attributes on the root (`"/"`) live in
  the mroot at rid -1, next to the config tags [PROBE: set/get/size/remove on
  "/" work]. No test covers root attributes.
- `lfs3_getattr(path, type, buf, size)` (1479; 12334-12347): returns
  `min(size, attrsize)` bytes. Errors: NOENT, NOTDIR, INVAL, **NOATTR**.
  Truncation is silent [TEST test_attrs_setattr_trunc].
- `lfs3_sizeattr` (1485; 12349-12362): returns the attr size, or NOATTR.
- `lfs3_setattr(path, type, buf, size)` (1491; 12364-12417)
  - Creates or replaces. `size=0` is a valid empty attr [TEST setattr_zero];
    NULL with size 0 is also handled [TEST setattr_null].
  - It broadcasts the new value into open, non-desync files that track the
    attr readably, truncated to their `buffer_size`.
  - **There is no size limit or check** (R3).
- `lfs3_removeattr` (1499; 12419-12470): NOATTR if absent. It sets
  `*size = NOATTR` in tracking open files.
- File-bound attrs (`struct lfs3_attr` in `lfs3_file_cfg.attrs`,
  lfs3.h:763-803):
  - `flags`: `A_RDONLY` (read at open and receive broadcasts), `A_WRONLY`
    (written on sync), `A_RDWR`, and `A_LAZY` (write only if the file
    changed).
  - `buffer_size = LFS3_ERR_NOATTR` means "remove".
  - The optional `*size` receives the size, or NOATTR.
  - They are written atomically with the file's sync commit, and only if
    different (8325-8374).
  - Readonly/wronly open-flag asserts (12806-12814).
  - [TEST] test_attrs_fattr_* (broadcast, desync, resync, zombie, lazy,
    big up to 513 B).

### 1.8 Key-value API (lfs3.c:14988-15072)

- `lfs3_get(path, buf, size)` (1434)
  - Opens RDONLY with `lfs3_file_kvcfg`: `fcache_buffer=(uint8_t*)1,
    fcache_size=0`, a sentinel with no cache.
  - Reads `min(size, filesize)` from position 0 and closes.
  - Errors: NOENT, ISDIR [PROBE get on a dir → -21], NOTSUP (unknown type),
    NOTDIR, INVAL, CORRUPT.
- `lfs3_size(path)` (1440): opens the same way and returns the file size.
- `lfs3_set(path, buf, size)` (1446; `!RDONLY`)
  - Opens with the internal `LFS3_o_WRSET|O_CREAT|O_TRUNC`, using the
    caller's buffer as the file cache (a const cast).
  - For a new file that fits `shrub_size`, `fragment_size` and
    `crystal_thresh`, it is created and written in **one commit** (12707-12724).
    Otherwise it follows the stickynote, flush and sync path at close.
  - **`file_limit` is not enforced** (R7).
- [TEST] test_kv (18 cases; interop with the file API and stickynotes).
- **`LFS3_KVONLY` and `LFS3_2BONLY` do not exist in this tree.** They were
  removed in commit `5d905e6` ("Dropped LFS3_KVONLY and LFS3_2BONLY modes for
  now", 2025-10-23: "the current state of testing means I have no idea if any
  of it still works"). There are no references in the code, tests, docs or CI.
  Requirements that mention them must be marked *future/unimplemented*.

### 1.9 Traversal API (lfs3.c:17105-17245)

- `lfs3_trv_open(trv, flags)` (1713)
  - Flags: `T_RDWR` (0) or `T_RDONLY`, `T_MTREEONLY`, `T_EXCL`,
    `T_MKCONSISTENT`, `T_LOOKAHEAD`, `T_PREERASE`, `T_COMPACT`, `T_CKMETA`,
    `T_CKDATA`.
  - These combinations assert (17107-17138):
    - a RDWR traversal on a RDONLY mount;
    - a RDONLY traversal with MKCONSISTENT, LOOKAHEAD, PREERASE or COMPACT;
    - MTREEONLY with LOOKAHEAD or CKDATA;
    - PREERASE without REVPERTURB.
  - It registers the traversal as a handle.
- `lfs3_trv_read(trv, tinfo)` (1727)
  - Returns the next `{btype, block}`: MDIR is reported **twice** (both
    blocks), BTREE (inner nodes of the mtree, file btrees and the gbmap) once,
    DATA once.
  - NOENT at the end.
  - **BUSY** if `T_EXCL` and the filesystem was modified (checkpointed) since
    open or rewind (17169-17173).
  - CORRUPT from `CKMETA`/`CKDATA` validation.
  - Blocks may repeat: they are CoW-shared, and a mutation during the
    traversal can revisit blocks (9804-9814). Unsynced blocks of open files
    are also visited (9989-10020).
  - A RDWR traversal performs the requested work as it goes (lfs3_mtree_gc
    10198-10410).
- `lfs3_trv_rewind` (1733): resets the traversal and clears DIRTY and
  CKPOINTED, re-arming EXCL.
- `lfs3_trv_close` (1719): unlinks the handle.
- [TEST] test_trvs (74 cases), test_ck METHOD=2, test_mount/test_gc.
  `T_MTREEONLY` is used only in test_ck spam cases and test_mtree.

### 1.10 File API (other analyst's area; listed so every public call appears)

- `lfs3_file_open` (1512; 12826; the header hides it under `LFS3_NO_MALLOC`,
  but the .c does not)
- `lfs3_file_opencfg` (1526; 12782-12823): open errors are NOENT, EXIST
  (`O_EXCL`), ISDIR, NOTDIR, NOTSUP (unknown type), NAMETOOLONG, INVAL, NOMEM
  (fcache), and CORRUPT (`O_CKMETA`/`O_CKDATA` run `lfs3_file_ck` after the
  open, 12761-12772).
- `lfs3_file_close` (1541; 12871; syncs unless rdonly or desync, and always
  releases)
- `lfs3_file_sync` (1551; 14553; a zombie makes it a no-op returning 0)
- `lfs3_file_flush` (1560; 14250)
- `lfs3_file_desync` (1575; 14610) and `lfs3_file_resync` (1583; 14622; a
  zombie gives NOENT)
- `lfs3_file_read` (1589; 12978) and `lfs3_file_write` (1599; 14107): write
  returns **FBIG** if `size > file_limit - pos`. **On any write error the
  handle is marked `O_DESYNC`** (14242-14246) [DOC lfs3.h:1568-1569].
- `lfs3_file_seek` (1607; 14665): INVAL if the position exceeds `file_limit`.
- `lfs3_file_truncate` and `lfs3_file_fruncate` (1617/1627; 14716/14806):
  FBIG.
- `lfs3_file_tell`, `lfs3_file_rewind` and `lfs3_file_size`
  (1634/1640/1646).

**`int lfs3_file_ck(lfs3_t*, lfs3_file_t*, uint32_t flags)`** (1652;
14918-15000)
- Flags: `CK_CKMETA` and `CK_CKDATA` only. It asserts on WRONLY files.
- `CKDATA` also checks an ungrafted leaf bptr.
- It walks only the file's bshrub/btree: `rbyd_fetchck` on every BRANCH, and
  `bptr_ck` on every BLOCK when CKDATA is set.
- **It does not re-validate the file's own mdir** or the inline/shrub data
  held in it. It returns CORRUPT on mismatch.
- [TEST] test_ck_file_* flips bits **only in BTREE blocks**
  (test_ck.toml:1231-1233).

### 1.11 Integrity mechanisms: what is checked, when, and which error

| Mechanism | Where | When | Error on failure |
|---|---|---|---|
| Per-commit crc32c: rev + tags + data + cksum-tag header, with a 1-bit valid (parity) prefix on each tag and phase = `block & 3` | `lfs3_rbyd_fetch_` 2708-3032, `appendcksum_` 4388-4533 | every rbyd fetch (mdir fetch, full branch fetch) | a bad commit is **silently skipped**. The fetch picks the latest valid commit and returns CORRUPT only if there is none (2936-2939). `lfs3_mdir_fetch` then tries the other block (7853-7909) |
| ecksum (erased-state cksum of the next `prog_size` bytes) | 2485-2585, 2655-2686 | fetch, to decide whether an rbyd can be appended to | no error. The rbyd is treated as unerased and compacted on the next commit |
| Branch cksum (parent stores child `{block,trunk,cksum}`) | `lfs3_rbyd_fetchck` 3067-3102 | `CKFETCHES`, ckmeta/ckdata scans, `file_ck` | CORRUPT "Found corrupted rbyd" or "rbyd cksum mismatch" |
| bptr cksum (`cksize`+`cksum` over `[0,cksize)` of a data block) | `lfs3_bptr_ck` 2455-2477 | `CKFETCHES` (bptr fetch), `CKDATA` scans, `file_ck(CKDATA)` | CORRUPT "Found bptr cksum mismatch" |
| gcksum (global) | mount 15894-15900; scans 10153-10162 | at mount (always); at the end of every CKMETA/CKDATA traversal that was not checkpointed | CORRUPT "Found gcksum mismatch" |
| mroot and open-handle mdir cksum cross-check | 10091-10118 | CKMETA/CKDATA traversals | CORRUPT |
| `LFS3_CKPROGS` (`M_CKPROGS`/`F_CKPROGS`) | `lfs3_bd_prog_` 317-368 | after every prog: read back and compare | CORRUPT from prog → the caller treats it as a bad block and relocates (for example mdir 8704-8783). Surfaces as NOSPC if it cannot relocate ("Stuck mdir") |
| `LFS3_CKFETCHES` | 2440-2450, 5215-5227, 5650-5665 | every btree branch and bptr fetch | CORRUPT at the operation that needed the node or block |
| `LFS3_CKMETAPARITY` | `lfs3_bd_readtag` 1368-1441; ptail 740-757 | every tag read that is not part of a cksum computation: the tag+data parity is compared with the next tag's valid bit, or with `ptail` inside an unfinished commit | CORRUPT "Found ckparity mismatch" (1-bit detection per tag) |
| `LFS3_CKDATACKSUMS` | `bd_readck`/`cmpck`/`cpyck` 762-1030; data_read 1613-1655 | every read, compare or copy of bptr data: the whole `[0,cksize)` is re-checksummed | CORRUPT "Found ckdatacksums mismatch". **Does not compile at b10efaa** (R6) |
| `M_CKMETA`/`M_CKDATA`, `F_CK*`, `T_CK*`, `CK_CK*`, `GC_CK*`, `O_CK*` | mtree_traverse 10064-10179 | at mount, format, `fs_ck`, `fs_gc`, trv, file open | CORRUPT |

**Rollback protection given by gcksum [IMPL + PROBE]**
- Definitions:
  - `cksum(m)`: the canonical checksum of mdir `m`'s latest valid commit.
    It runs from the revision count to the end of the last trunk, with the
    perturb term removed. It **excludes** the ecksum, gcksumdelta and cksum
    tags (2869-2890, 2917-2924; 8412-8430).
  - `Δ(m)`: the `GCKSUMDELTA` value in `m`'s latest commit, or 0.
- Each mdir commit writes `Δ' = Δ ^ cube(g_prev) ^ cube(g_new) ^ gcksum_d`
  (8420-8423). That keeps **`XOR Δ(m) == cube(XOR cksum(m))`** across the
  mroot chain and the mtree mdirs.
- Using the crc32c-ring cube stops the delta from cancelling to a function
  of only the local cksum, so one mdir cannot be rolled back consistently on
  its own (15869-15893).
- What is detected:
  1. **At mount:** any mdir showing an older commit than one the filesystem
     later built on. That happens when its newest commit is lost or corrupted
     and the fetch falls back to an older valid commit.
     [PROBE] 15 mdirs. Commit X was written to mdir A, then commit Y to mdir
     B. Restoring A's pre-X block gave mount **CORRUPT (-84) for both RDONLY
     and RDWR**.
  2. **After mount:** a CKMETA/CKDATA scan recomputes `XOR cksum` and
     compares it with the in-RAM gcksum. That catches *any* on-disk change of
     any mdir, including the latest commit.
     [PROBE] After a post-mount rollback of A, `fs_ck(CKMETA)` returned -84.
  3. **Externally:** `lfs3_fs_cksum` can be stored off-device to detect a
     whole-image replay or rollback of the latest commit.
- What is **not** detected:
  - Rollback of the **globally most recent commit** at mount. It cannot be
    told apart from power loss.
    [PROBE] Rolling Y back gave mount 0, old data, and a gcksum different from
    the pre-rollback value.
  - Replacement of the whole image with an older consistent one.
  - Post-mount rollback or corruption until a ckmeta scan runs.
    [PROBE] `lfs3_get` served the rolled-back value silently.
  - Data-block corruption without CKDATA, CKFETCHES or CKDATACKSUMS.
  - Payload corruption in an already-fetched mdir: reads are not
    re-checksummed unless CKMETAPARITY.
  - Collisions: 32-bit, and the delta "loses at most 3 bits" (15890-15892).
- Consequence (R11): a gcksum mismatch is fatal to mounting. A single rolled-back
  or corrupted tail commit in any mdir other than the last one written makes the
  filesystem unmountable, even read-only. No recovery tool or flag exists.

**"File data is not reverted" exceptions**
- I found no sentence in lfs3.h with exactly that wording. The closest
  documented and implemented exceptions:
  1. On a failed write, flush or sync, the open file's in-RAM state is **not
     rolled back**. It is marked desynchronized instead (lfs3.h:1568-1572;
     lfs3.c:14242-14246), and `lfs3_file_resync` discards it.
  2. On a failed mdir commit the gstate (gcksum, grm) is reverted to the
     on-disk values, but **the gbmap is not** ("note we do _not_ revert the
     on-disk gbmap ... any in-flight state would be lost", 7596-7612,
     9464-9467).
  3. Rollback of whole filesystem states is accepted as "a fundamental issue
     for any filesystem with logs" (test_ck.toml:615-623). The design aims
     only to *detect* inconsistency, not to prevent reverting to an older
     consistent state.
- Mark this as **uncertain**. The user's phrase may refer to documentation
  outside this tree.

### 1.12 Flag values (lfs3.h)

| Group | Bits |
|---|---|
| mode | `M/F/T_MODE=1`, `RDWR=0`, `RDONLY=1`; `O_MODE=3` (RDONLY 0, WRONLY 1, RDWR 2, and 3 is forbidden) |
| FLUSH 0x40, SYNC 0x80 | O, M, I |
| REVPERTURB 0x10, REVNOISE 0x20 | F, M, I |
| CKPROGS 0x100000, CKFETCHES 0x200000, CKMETAPARITY 0x400000, CKDATACKSUMS 0x1000000 | F, M, I |
| GBMAP 0x2000000 | F, I |
| MKCONSISTENT 0x100, LOOKAHEAD 0x200, PREERASE 0x400, COMPACT 0x800, CKMETA 0x1000, CKDATA 0x2000 | F, M, T, CK, GC, I; `O_` only for CKMETA/CKDATA |
| other O flags | CREAT 0x4, EXCL 0x8, TRUNC 0x10, APPEND 0x20, DESYNC 0x100000 |
| other T flags | MTREEONLY 0x2, EXCL 0x8 |
| other A flags | LAZY 0x4 |
| I_RDONLY | 0x1 |
| internal | `o_TYPE`, `t_*` upper bits (lfs3.h:146-154, 379-385) |

- Aliases `*_CK` and `*_GC` combine bits; they are unused in tests.
- The flag groups share bit values on purpose: `lfs3_init` and `lfs3_fs_ck`
  test M, F and CK flags with the `T_`/`M_` helpers.

---------------------------------------------------------------------------

## 2. On-disk format: superblock, config, gstate

The byte-level example below is from a fresh 4096x64 image made by
`lfs3_format`, dumped with `scripts/dbgrbyd.py -x` and `xxd`.

### 2.1 Metadata block (rbyd) layout

Revision count:

```
off 0: rev le32   (block 1: 68 69 21 00 = 0x00216968 ("hi!"), block 0: 0xf0216968)
```

Revision bit layout (7766-7785):
- `vvvv` is a 4-bit relocation count.
- `recycle_bits` hold the recycle counter.
- The optional noise bits come from `REVNOISE` (gcksum_p based).
- The perturb bit (bit 7) comes from `REVPERTURB`.
- The low debug bits mark the block type: mroot anchor / mdir / btree.
- The newer block of a pair has `scmp(rev) > 0` (7853-7875).

Tags (1307-1460; scripts/dbglfs3.py:40-90) are followed by data:

```
tag: be16  v ttt tttt tttt tttt   v = valid bit = parity(cksum so far) ^ perturb
     leb128 weight (≤5 bytes, ≤0x7fffffff)
     leb128 size   (≤4 bytes, ≤0x0fffffff)
```

- Alt pointers are `0x4000 | R:0x2000 | GT:0x1000 | key`. Their "size" field
  is a jump. Bit 7 of the tag is reserved and must be 0 (1466-1467).
- A commit ends with the following (4388-4533):

```
[ECKSUM 0x3200 w0 size=5..8: leb128 cksize (=prog_size), le32 crc32c of the next cksize erased bytes]
CKSUM  0x3000|perturb(0x4)|phase(block&3)  w0  size=leb128 padded to 4 bytes (padding to prog alignment)
       le32 commit cksum (^ ODDZERO 0xfca42daf if perturbed)
```

- The ecksum is present only if there is room for another commit in the block.

Commit trailer from the example (block 1, tag bytes include the valid bit):

```
0099: b3 00 00 04  ce 62 3e 69   GCKSUMDELTA (0x3300) = 0x693e62ce = cube(cksum)
00a1: b2 00 00 05  10 10 4c 2f ef   ECKSUM cksize=16, cksum 0xef2f4c10
00aa: b0 05 00 8f 80 80 00  ee 68 64 d4   CKSUM phase=1 perturb=1 pad=15, cksum 0xd46468ee
```

The next commit starts at 0xc0 ("size 192").

### 2.2 Superblock / mroot config (rid -1 of each mroot)

Example (block 1):

| off | bytes | meaning |
|---|---|---|
| 0004 | `81 31 00 08` + `6c 69 74 74 6c 65 66 73` | MAGIC 0x0131, "littlefs" (8 B). Required in **every** mroot of the chain |
| 0014 | `01 34 00 02` + `00 00` | VERSION 0x0134: u8 major, u8 minor. v0.0 = experimental |
| 0022 | `81 35 00 04` + `90 0c 01 00` | RCOMPAT 0x0135 = 0x00010c90: MMOSS 0x10, MTREE 0x80, BSHRUB 0x400, BTREE 0x800, GRM 0x10000 |
| 0036 | `81 36 00 04` + `00 00 04 01` | WCOMPAT 0x0136 = 0x01040000: GCKSUM 0x40000, DIR 0x1000000 (+ GBMAP 0x80000 if enabled) |
| 004a | `81 38 00 03` + `ff 1f 3f` | GEOMETRY 0x0138: lleb128(block_size-1 = 4095), leb128(block_count-1 = 63) |
| 0061 | `01 39 00 02` + `ff 01` | NAMELIMIT 0x0139: leb128 255 (written as lleb128) |
| 0077 | `01 3a 00 05` + `ff ff ff ff 07` | FILELIMIT 0x013a: leb128 0x7fffffff |
| 0094 | `03 04 01 01` + `00` | BOOKMARK 0x0304, weight 1 (rid 0), did 0 = root |
| (opt) | GBMAPDELTA 0x0234 | only if F_GBMAP (see 2.3) |
| (opt) | OCOMPAT 0x0137 | never written, ignored on read |

Interleaved `c1 31 00 0c`-style words are alt tags of the rbyd tree.

- Compat fields are little-endian, and truncated bytes read as 0. Extra
  trailing non-zero bytes set the internal `OVERFLOW` bit (0x80000000), so
  the flags no longer match (15475-15505) [TEST
  test_mount_incompat_*_overflow/_padding].
- Config tags use suptype 0x01xx. **Only tags ≥ 0x013b are rejected as
  unknown** (R9).
- An mroot chain is formed by a `MROOT` tag (0x0431), an mptr of two leb128
  block addresses (7045-7075). The anchor is always blocks {0,1}. The config
  of the **last** mroot is authoritative. The mtree is `MTREE` 0x043c
  (btree: leb128 weight, leb128 block, leb128 trunk, le32 cksum).
- Format writes identical config to **both** anchor blocks. The comment says
  this is "to hopefully avoid mounting an older filesystem on disk"
  (16181-16182).
- Uncertain: the "rr" (redund) bits in MAGIC (0x0131) and MROOT (0x0431)
  are defined in dbglfs3.py but not interpreted by the driver beyond
  masking.

### 2.3 Global state (gstate), XOR-distributed over mdirs (rid -1)

| gstate | tag | encoding | semantics |
|---|---|---|---|
| gcksum | GCKSUMDELTA 0x3300 (a cksum-class tag, written just before ECKSUM/CKSUM and outside the canonical cksum) | le32 | XOR of all Δ = `crc32c_cube(XOR of mdir cksums)`. Only the **last commit's** delta of each mdir counts (2753-2873) |
| grm | GRMDELTA 0x0230 | ≤10 B: up to 2 leb128 mids, 0-terminated, trailing zeros trimmed (7484-7533) | XOR of deltas = pending global removes (a queue of 2) |
| gbmap | GBMAPDELTA 0x0234 | ≤23 B: leb128 window, leb128 known, branch (leb128 block, leb128 trunk, le32 cksum) (10433-10459; lfs3.h:1003-1016) | on-disk block map root. Consumed even when the gbmap is disabled (7738-7757) |

- crc32c: poly 0x1edc6f41 (reflected 0x82f63b78), init and fini 0xffffffff
  (lfs3_util.h:764-769).
- `crc32c_mul` is carry-less multiplication mod P in the crc32c ring
  (lfs3_util.c:235-283).
- `ODDZERO = 0xfca42daf` flips parity without moving the ring position
  (lfs3_util.h:761-762).

### 2.4 Format version handling

- `LFS3_DISK_VERSION` is 0x00000000 (lfs3.h:26), so v0.0 is "experimental".
- Every v3-alpha image claims v0.0, so incompatible alpha revisions cannot be
  told apart (risk).
- On disk the version is only two u8 values, while the macros allow 16-bit
  major and minor (lfs3.h:27-28 vs 16213-16216).
- Mount rejects major ≠ 0 or minor > 0.
- [TEST] test_mount_incompat_major/minor.
- test_compat runs **only self-compatibility** unless a previous version is
  linked as `LFSP` (test_compat.toml:11-24). There is no v2→v3 test or
  migration.

---------------------------------------------------------------------------

## 3. Invariants

- **I1 gcksum:** `XOR_m Δ(m) = cube(XOR_m cksum(m))` over every mdir that can
  be reached (the mroot chain, then the mtree). This is checked at mount.
  During a commit, `gcksum_p`/`gcksum_d` stage the change, and a failure
  reverts it (7549-7614, 8878-9468).
- **I2 in-RAM gcksum:** `lfs3->gcksum` = XOR of current mdir cksums.
  `gcksum_p` = the last committed value. A ckmeta scan that was not
  checkpointed must reproduce `gcksum` (10153-10162).
- **I3 superblock:** the blocks {0,1} anchor holds a valid commit with
  MAGIC. The chain is acyclic and every link has MAGIC. The last mroot holds
  VERSION, RCOMPAT, WCOMPAT, GEOMETRY and, optionally, NAMELIMIT and
  FILELIMIT.
- **I4 geometry:** disk `block_size` = cfg `block_size`; disk `block_count` ≤
  cfg `block_count`. Every block address in use is below
  `lfs3->block_count` (the bd asserts at every call).
- **I5 limits:**
  - disk `name_limit` ≤ cfg `name_limit` ≤ `LFS3_NAME_MAX` ≤ 1022 [DOC];
    every stored name ≤ `name_limit` (enforced on create and rename);
  - disk `file_limit` ≤ cfg `file_limit` ≤ 2^31-1; every file size ≤
    `file_limit` (write, truncate and seek enforce it, but `lfs3_set` does
    **not**, R7).
- **I6 compat:** `rcompat_disk == rcompat_driver` exactly for any mount.
  `wcompat` equal (masked) for an RDWR mount.
- **I7 root:** did 0 is the root. Its bookmark is mid 0 and is never grm'd
  (7458-7462). Every directory has exactly one bookmark and one entry
  (name+DID) in its parent. Entries of a directory follow their bookmark
  contiguously in mid order, sorted by name.
- **I8 grm:** at most 2 pending removes, each below the mtree weight
  (asserted, 7528). A pending grm hides its mid (it looks like an orphan).
- **I9 commit validity:** a tag's valid bit = parity of the running cksum XOR
  perturb. Cksum phase = `block & 3`. The commit cksum covers everything
  since the revision. A commit is visible if and only if its cksum checks.
- **I10 revisions:** the newer mdir block has the greater (sequence-compared)
  revision. Fetch tries the newer block first, then the older.
- **I11 handles:** every open file, dir and traversal is in `lfs3->handles`.
  `unmount` requires an empty list (except the internal gc traversal).
- **I12 check flags:** `I_CKMETA`/`I_CKDATA` are cleared only by a complete,
  unmutated scan (10165-10176). `lfs3_fs_ck` re-arms them before it runs.
- **I13 open files:** a removed file with open handles persists as a
  stickynote (zombie) until the last handle closes. Then it is cleaned via
  grm, or through `I_MKCONSISTENT` if the grm queue is full (12843-12862).

---------------------------------------------------------------------------

## 4. Configuration

### 4.1 `struct lfs3_cfg` (lfs3.h:463-713; checks in lfs3_init 15082-15393)

| Field | Build | Valid range / default | Enforced |
|---|---|---|---|
| context | all | opaque | - |
| read | all | required | not NULL-checked |
| prog, erase, sync | !RDONLY | required; prog/erase may return CORRUPT for a bad block [DOC] | not NULL-checked |
| lock, unlock | THREADSAFE | [DOC] lock/unlock the bd | **never called** (R19) |
| read_size | all | > 0; divides block_size and rcache_size | assert 15104, 15114, 15120 |
| prog_size | !RDONLY | > 0; divides block_size and pcache_size | assert 15106, 15116, 15122 |
| block_size | all | ≤ 0x0fffffff; must equal disk; no minimum checked (test default ≥ 512) | assert 15126 |
| block_count | all | format ≥ 2 (≥ 3 with gbmap), else a bd assert; mount ≥ disk | NOTSUP at mount; no check at format |
| block_recycles | !RDONLY | -1 disables wear leveling; 0 means pure CoW; otherwise `recycle_bits = nlog2(2(r+1)+1)-1 ≤ 20` (r up to ~2^19) | assert 15260 |
| rcache_size | all | > 0, a multiple of read_size | assert |
| pcache_size | !RDONLY | > 0, a multiple of prog_size | assert |
| fcache_size | all | default per-file cache; 0 means `malloc(0)`, so NOMEM is possible; `lfs3_file_cfg.fcache_size` overrides it | none |
| lookahead_size | !RDONLY | > 0 (8 blocks per byte) | assert 15213 |
| gc_flags | GC | a subset of GC_* | assert 15130-15137 and 16834-16845 |
| gc_steps | GC | 0 means 1; -1 means unbounded | - |
| gc_lookahead_thresh | !RDONLY | 0 means repopulate when empty; -1 or ≥ 8·lookahead_size means after any alloc | - |
| gc_lookgbmap_thresh | !RDONLY && GBMAP | ≤ lookgbmap_thresh, or -1 | - |
| gc_preerase_count | !RDONLY && PREERASE | 0 or -1 | - |
| gc_compact_thresh | !RDONLY | 0 means `block_size - block_size/8`; -1 disables; otherwise [block_size/2, block_size] | asserted **only if LFS3_GC** (15139-15147), but also used by `M_COMPACT`/`T_COMPACT` without GC (10339-10350) |
| rcache_buffer, pcache_buffer, lookahead_buffer | (pcache/lookahead !RDONLY) | optional static buffers of the configured size | - |
| name_limit | !RDONLY | 0 means LFS3_NAME_MAX; ≤ LFS3_NAME_MAX; stored at format | assert 15232 |
| file_limit | !RDONLY | 0 means LFS3_FILE_MAX; ≤ LFS3_FILE_MAX; stored at format | assert 15238 |
| shrub_size | !RDONLY | ≤ block_size/4; 0 disables shrubs | assert 15150-15151 |
| fragment_size | !RDONLY | ≤ block_size/4 | assert 15152-15153 |
| crystal_thresh | !RDONLY | any; 0 behaves as 1; > block_size means fragments only (TODOs lfs3.h:686-691) | - |
| lookgbmap_thresh | GBMAP | 0 means repopulate when empty | - |

Notes on the table:
- **RDONLY builds lack `name_limit`/`file_limit` fields.** The mount
  comparison then reads uninitialized `lfs3->name_limit`/`file_limit` (R5).
- Test defaults (runners/test_defines.h): 1 MiB disk, erase 4096,
  read/prog 1, rcache/pcache 16, fcache 16, lookahead 16,
  `BLOCK_RECYCLES=-1`, `SHRUB_SIZE=BS/4`, `FRAGMENT_SIZE=min(BS/16,512)`,
  `CRYSTAL_THRESH=BS/16`, `GC_COMPACT_THRESH=0`, `GC_FLAGS=LFS3_GC_GC`.
  `name_limit` and `file_limit` are **never set** by the test cfg; only
  internal-commit mount tests tamper with the disk values.

### 4.2 Compile-time options (every `LFS3_*` used in `#if`)

The last column is from my `clang -std=c99 -Wall -Wextra -pedantic
-fsyntax-only` matrix over `lfs3.c` and `lfs3_util.c`.

| Option | Effect | Compiles @b10efaa |
|---|---|---|
| `LFS3_RDONLY` / `LFS3_YES_RDONLY` | removes all write paths, write flags and cfg write fields; `YES_` defines RDONLY and forces `M_RDONLY` | yes, but it **cannot mount a zeroed lfs3_t** (R5) |
| `LFS3_GBMAP` / `LFS3_YES_GBMAP` | on-disk global block map, `F_GBMAP`, mk/rmgbmap (not with YES), gbmap cfg fields; YES forces it on at format and makes a non-gbmap image unwritable (wmask) | yes; **RDONLY+GBMAP fails** (10483, 10499, 10509, 15097) |
| `LFS3_PREERASE` | gc pre-erase; requires GBMAP and REVPERTURB (`#error` lfs3_util.h:111-114); **there is no `LFS3_YES_PREERASE`** | only with both |
| `LFS3_REVPERTURB` / `YES_` | perturb bit in revisions; required to use pre-erased blocks | yes |
| `LFS3_REVNOISE` / `YES_` | noise in revisions; "we really don't want this enabled during testing" (3288-3290) | yes |
| `LFS3_CKPROGS` / `YES_` | read back after prog | yes |
| `LFS3_CKFETCHES` / `YES_` | check branch/bptr cksums on first use | yes |
| `LFS3_CKMETAPARITY` / `YES_` | tag parity checks, ptail | yes; **RDONLY+CKMETAPARITY fails** (15206-15207) |
| `LFS3_CKDATACKSUMS` / `YES_` | checked data reads | **no at b10efaa: `data.u.disk` on a pointer at 1736-1737 and 1808**; fixed in v3-fixes e4c046b |
| `LFS3_GC` / `YES_GC` | `lfs3_fs_gc`, gc cfg fields, `lfs3->gc` | yes; **RDONLY+GC fails** (15143-15146 `gc_compact_thresh`) |
| `LFS3_BLEAFCACHE` / `YES_` | btree leaf cache (speed/RAM trade) | yes |
| `LFS3_BIGGEST` | all opt-in features | **no at b10efaa** (because of CKDATACKSUMS); yes at ba31df7 |
| `LFS3_YES_FLUSH`, `LFS3_YES_SYNC` | force `O_FLUSH`/`O_SYNC` (7132-7148) and `M_FLUSH`/`M_SYNC` at mount | yes (not in util.h docs) |
| `LFS3_THREADSAFE` | adds `cfg->lock`/`unlock` fields only | yes, but not implemented |
| `LFS3_NO_MALLOC` | `lfs3_malloc` returns NULL; `lfs3_file_open` hidden in the header | yes |
| `LFS3_NO_STRINGH`, `LFS3_NO_BUILTINS` | fallbacks for string functions and intrinsics | yes (`strspn` fallback is wrong for multi-char sets, R21) |
| `LFS3_NO_ASSERT` | disables asserts; `LFS3_UNREACHABLE` becomes `__builtin_unreachable` | yes; changes semantics (R12) |
| `LFS3_NO_DEBUG/INFO/WARN/ERROR`, `LFS3_NO_LOG`, `LFS3_YES_TRACE` | logging | yes |
| `LFS3_TRACE/DEBUG/INFO/WARN/ERROR/ASSERT/UNREACHABLE` | overridable macros | - |
| `LFS3_CFG` (header) vs `LFS3_CONFIG` (lfs3_util.c:11) | user config header | **name mismatch** (R20) |
| `LFS3_SMALLER_CRC32C`, `LFS3_FASTER_CRC32C`, `LFS3_PMUL_CRC32C` | crc32c implementation choice | Smaller and Faster yes; **PMUL no** (lfs3_util.c:210 `lfs3_fromle32_` undefined) |
| `LFS3_NAME_MAX` (255, ≤ 1022 [DOC]), `LFS3_FILE_MAX` (2^31-1) | limits and `lfs3_info.name` size | - |
| `LFS3_DBGRBYDFETCHES`, `DBGRBYDCOMMITS`, `DBGRBYDBALANCE`, `DBGBTREEFETCHES`, `DBGBTREECOMMITS`, `DBGMDIRFETCHES`, `DBGMDIRCOMMITS`, `DBGALLOCS` | debug logging and balance asserts | yes |
| `LFS3_KVONLY`, `LFS3_2BONLY` | **removed** (commit 5d905e6) | n/a |

Combinations that compile cleanly:
- default;
- RDONLY, RDONLY+CKFETCHES, RDONLY+BLEAFCACHE, RDONLY+NO_MALLOC;
- GBMAP;
- GC+GBMAP;
- YES_GBMAP+GC;
- GBMAP+REVPERTURB+PREERASE;
- all features except CKDATACKSUMS;
- every `YES_` (except CKDATACKSUMS);
- NO_STRINGH+NO_BUILTINS;
- THREADSAFE;
- NO_ASSERT.

At b10efaa all of them emit `-Warray-bounds` at 3506-3511 (R18).

**Which combinations are tested.** Test cases gate on
`ifdef`/`LFS3_IFDEF_*`/`LFS3_IFYES_*`, so only the build's own option set
runs. The baseline run used the default build. No script or CI job builds
other v3 combinations. So **none** of RDONLY, GC, GBMAP, PREERASE, CK*,
REV*, BLEAFCACHE, THREADSAFE, NO_MALLOC, NO_STRINGH, NO_ASSERT or YES_* has
been run in this environment. RDONLY in particular has no test-side support:
the test runner cfg always sets prog fields.

---------------------------------------------------------------------------

## 5. Test coverage map

### 5.1 By function or behaviour

"Default runs" = permutations run in the default build, from
`runners/test_runner -L`.

| API / behaviour | Test cases | Notes |
|---|---|---|
| format + mount basic | test_mount_simple; every suite's setup | GBMAP=true is skipped by default (1/2) |
| mount flags → fsinfo flags | test_mount_flags (144/65536 run), test_mount_format_flags (32/8192) | only the non-CK/non-REV/non-GBMAP permutations run by default |
| mount with gc work | test_mount_t_lookahead, _t_lookgbmap (0 run), _t_preerase (0 run), _t_compact, _t_mkconsistent, _t_ckmeta, _t_ckdata | t_ckmeta/t_ckdata clobber whole MDIR/BTREE blocks with 0xcc and expect CORRUPT |
| bad or missing magic | test_mount_incompat_no_magic, _bad_magic | CORRUPT for RDWR and RDONLY |
| version | test_mount_incompat_major, _minor | NOTSUP; a missing VERSION tag is untested |
| rcompat, wcompat, ocompat | test_mount_incompat_rcompat, _wcompat, _ocompat, _rdonly, _wronly, *_overflow (72 bit positions), *_padding | only **added** bits are tested; **missing bits untested** |
| geometry, limits | test_mount_incompat_block_size, _block_count, _name_limit, _file_limit | missing GEOMETRY → INVAL is untested |
| unknown config | test_mount_incompat_unknown_config (tag 0x0142 only) | tags < 0x013b untested (R9) |
| unknown file type | test_mount_incompat_unknown_type, _rm, _mv_src, _mv_dst, _mv_src_dst, _mv_noop, _mv_notdir, _mv_isdir | |
| cksum phase | test_mount_incompat_out_of_phase (PHASE 1-4) | |
| format then mount compat | test_compat_* (14 cases, self-compat only) | no LFSP image and no golden images |
| fs_stat | test_mount_*, test_grow, test_gc, test_gbmap, test_trvs, test_stickynotes | name_limit/file_limit fields are not asserted anywhere I found |
| fs_usage | test_grow (4 cases) | only `>= 0` |
| fs_cksum | test_ck_cksum; test_ck_ckmeta_hard, _ckdata_hard | stable across remount; changes after a rollback |
| fs_ck CKMETA/CKDATA | test_ck_ckmeta/ckdata_easy/hard (METHOD=0) | |
| fs_gc CK | same with METHOD=1 | **skipped by default** (needs LFS3_GC) |
| trv CK | METHOD=2; test_trvs_ckmdir_*, ckbtree_*, ckdata_* | |
| mount CK | METHOD=3; test_mount_t_ck* | |
| fs_unck | test_ck_*_hard, test_gc | |
| file_ck, O_CKMETA/CKDATA | test_ck_file_ckmeta/ckdata_easy/hard | BTREE blocks only |
| CKPROGS | test_ck_ckprogs_mroot, _data, _btree, _overrecycling; test_badblocks; test_exhaustion; test_ck_spam_* METHOD=0 | **0 run by default** |
| CKFETCHES | test_ck_ckfetches_mroot, _data, _btree; spam METHOD=2 | **0 run by default** |
| CKMETAPARITY | test_ck_ckparity_mroot, _btree | **0 run**; the spam tests hard-code CKMETAPARITY=false |
| CKDATACKSUMS | test_ck_ckdatacksums_data; spam METHOD=3 | **cannot build at b10efaa** (buildable on v3-fixes; not yet run) |
| crc32c math | test_ck_crc32c, _incr, _mul, _mul_dist | default table implementation only |
| fs_mkconsistent | test_gc, test_powerloss, test_stickynotes | |
| fs_grow | test_grow_* (12, including 2 powerloss fuzz) | grow beyond cfg and shrink untested (R4) |
| mk/rmgbmap | test_gbmap | not built by default |
| mkdir | test_dirs_mkdir_* (reentrant), test_dirs_did_*, test_paths_* | test_dirs_rm_many_2layers excludes `TEST_PLS && N==4`, a known bug (test_dirs.toml:3442) |
| remove | test_dirs_rm_*, test_paths_*, test_files, test_stickynotes, test_kv_remove | |
| rename | test_dirs_mv_* (including _replace, _noop, _notempty, _root, _consistent), test_dread_*_mvs, test_attrs_mv_* | into-own-subtree untested (R1) |
| stat | test_dirs, test_paths, ... | |
| dir_open, dir_read, dir_close | test_dirs, test_dread, test_paths | |
| dir_tell, dir_rewind, dir_seek | test_dread_tell, _rewind, _seek, _read_* (SEEK=0-4) | seek(0/1) followed by further reads untested (R2) |
| path rules | test_paths (38 cases) | |
| getattr, sizeattr, setattr, removeattr | test_attrs_setattr_*, removeattr, all, many, fuzz, rm, mv_* | root "/" attrs untested; attrs > 513 B untested (R3); types 0x80-0xff not specifically tested |
| file attrs | test_attrs_fattr_* (30 cases) | |
| get, size, set | test_kv_* (18) | set vs file_limit untested (R7) |
| trv open, read, close, rewind | test_trvs (74), test_ck, test_gc, test_mount | |
| trv EXCL → BUSY | test_trvs_mutation_* | |
| trv MTREEONLY | test_ck_spam_* (METHOD 2), test_mtree | |
| unmount | every suite | |

### 5.2 Behaviours with no test (explicit list)

1. Renaming a directory into its own descendant (R1).
2. `dir_seek(0)` or `dir_seek(1)` followed by reading past the dot entries
   (R2).
3. Attributes larger than about block_size/2, and the attr size limit or
   error code (R3).
4. `fs_grow` beyond `cfg->block_count`; shrinking with NO_ASSERT (R4).
5. Any LFS3_RDONLY build (R5). Read-only *mount* is only lightly tested
   (test_mount_flags RDONLY, test_mount_incompat_*, test_gc, test_stickynotes).
6. `lfs3_set` beyond `file_limit`; any non-default `name_limit` or
   `file_limit` at format time (the cfg never sets them).
7. NOMEM paths in `lfs3_init`, `lfs3_file_opencfg` and with `fcache_size=0`
   (R8).
8. Unknown config tags below 0x013b; rcompat or wcompat with missing bits;
   missing VERSION; missing GEOMETRY → INVAL; missing NAMELIMIT/FILELIMIT
   defaults.
9. The mount-time gcksum check alone: tests find it only indirectly through
   bit flips. There is no *targeted* rollback test: an older-commit
   replay of a non-latest mdir → CORRUPT, and a latest-commit rollback →
   silent with a different gcksum. Deterministic cases would be easy
   (section 1.11 probe).
10. Detection of corruption in a file's own mdir by `file_ck`.
11. `fs_usage` values: bounds, duplicates, and the gbmap included.
12. `fs_stat` `name_limit`/`file_limit` reporting; RDONLY-mount `fs_stat`
    flags.
13. Mutating calls on a RDONLY mount: they assert rather than error, and no
    test pins that contract.
14. The `lfs3_fs_gc` path with `gc_flags=0`; the `gc_compact_thresh`
    assertion bounds.
15. Big-endian hosts (R13).
16. Every compile-time option other than the default (5.3); CI has none.
17. Format with `block_count` below the minimum; gbmap format when block 2 is
    bad (TODO 16130-16133).
18. Behaviour after a gcksum-mismatch mount failure: there is no recovery
    API.
19. `LFS3_THREADSAFE` locking (not implemented).
20. v2 → v3 compatibility or a clear rejection of v2 images.

### 5.3 Default-build test reality (from `runners/test_runner -L`)

- test_ck:
  - `ckprogs_*`, `ckfetches_*`, `ckparity_*` and `ckdatacksums_data` run
    0 of 1;
  - `ckmeta/ckdata_easy/hard` run 126/196 (METHOD=1 needs GC);
  - `spam_dir` runs 180/720 and the other spam cases 780/3920 or 480/2880,
    because METHOD 0, 2 and 3 need CKPROGS, CKFETCHES and CKDATACKSUMS.
- test_mount:
  - `flags` 144/65536, `format_flags` 32/8192;
  - `t_lookgbmap` and `t_preerase` 0/1;
  - every GBMAP=true permutation is skipped.
- test_grow and test_trvs: every GBMAP=true permutation is skipped.
- test_kv_many, test_kv_many_big and test_attrs_many* skip COMPACT=true
  (needs GC).
- Baseline failures (`.local/baseline_test.log`, 181 of 634616):
  - `test_files_zero_btree`: an assert at lfs3.c:9365, outside my area;
  - `test_grow_incr_spam_uzd_fuzz` with FORMAT_BLOCK_COUNT=2: NOSPC, a
    use-after-free in the test, fixed on v3-fixes 2574f54.

  The log lists only 3 failures in detail.

---------------------------------------------------------------------------

## 6. TODO/FIXME/XXX and unimplemented paths

### 6.1 In this area (lfs3.c unless noted)

- 15098 "TODO this all needs to be cleaned up" (`lfs3_init`).
- 15156 "move this to mount?"; 15245 "do we need to recalculate these after
  mount?"; 15368-15370 zeroing gstate twice.
- 15768 "should these be in lfs3_init?"; 15907 consumegdelta signature.
- **15921, 15948 "TODO switch to read-only?"**: grm and gbmap decode errors
  fail the mount. Degraded mode is unimplemented.
- 16089 "TODO this should use any configured values" (the mount log).
- **16130, 16132**: the gbmap is always written at block 2, with no bad-block
  fallback at format.
- 16833 "should we actually assert on these in lfs3_init?" (gc_flags).
- **11812** ZOMBIE on traversals.
- **11822, 12004**: remove and rename swallow `fixgrm` errors.
- 10026 "TODO is this correct?" (mtree traversal mid transition).
- 10332 "TODO big hack! is it big enough?" (mkconsistent dropping an mdir
  mid-traversal).
- 14669 "TODO check for out-of-range?" (`file_seek` before arithmetic;
  overflow in `pos + off` is not checked).
- 14998 "is this the best way?" (the kv cfg sentinel pointer `(uint8_t*)1`).
- 7550, 7167, 8246, 4611, 8449, 8727: commit overhead estimates and
  deduplication.
- lfs3.h:
  - 667, 686, 690: `shrub_size`/`fragment_size`/`crystal_thresh` defaults and
    missing asserts (`crystal_thresh < fragment_size` or `< prog_size` are
    "not really valid");
  - 1355, 1364: `grm_d`/`gcksum_d` kept in RDONLY.
- lfs3_util.h:26, 506; lfs3_util.c:14.

### 6.2 Tests

- test_ck.toml:2725 "revisit these when ckredund is implemented, ckredund
  should finally close the ckread hole". **ckredund does not exist.** The
  "ckread hole" is that normal reads of already-fetched metadata are not
  checked without CKMETAPARITY.
- test_dirs.toml:3442: a known failing powerloss case (mkdir aligning with
  an mdir split puts a did in the wrong mdir). It is excluded by `if`.

### 6.3 Unimplemented or removed features

- KVONLY and 2BONLY (removed).
- THREADSAFE locking.
- ckredund.
- Read-only fallback on mount errors.
- Trying multiple blocks for the gbmap at format.
- Recovery or repair (fsck) for gcksum mismatch or detached subtrees.
- `LFS3_YES_PREERASE` is absent while every other feature has a `YES_`.

---------------------------------------------------------------------------

## 7. Suspected bugs and risks (evidence; nothing has been fixed)

Severity: H = data loss, crash or brick on a normal API path; M =
contract, availability or memory safety; L = latent or documentation.

**R1 (H) Renaming a directory into its own subtree detaches it silently.**
- Code: `lfs3_rename` 11835-12014 has no ancestor check.
- [PROBE]
  - Steps: `mkdir a`, `mkdir a/x`, then `rename("a","a/b")`, which returns 0.
  - Afterwards `stat("a")` and `stat("a/b")` both return NOENT. `/` lists
    only `.` and `..`.
  - `fs_ck(CKMETA|CKDATA)` returns 0, and remounting with `M_CKMETA` returns 0.
- Result: the subtree is unreachable but its entries still occupy metadata
  forever. No mkconsistent or fsck path reclaims them.
- POSIX expects EINVAL.
- Untested.

**R2 (H) `lfs3_dir_seek(dir, 0 or 1)` loses every real entry.**
- Code: 12241 `lfs3_off_t off_ = off - 2;` underflows to about 2^32. The loop
  12242-12264 walks the cursor to the end of the mtree, NOENT breaks, and
  `pos` is set to `off`.
- Reads then give `.` and `..`, then NOENT.
- [PROBE] 3 subdirectories: `seek(0)` gave 2 entries instead of 5; `seek(1)`
  gave 1. `rewind` and `seek(2)` are correct.
- `tell` is 0 right after open, so the common "save tell(), seek() back"
  pattern breaks.
- test_dread_seek seeks to 0 and 1 but reads only one entry afterwards.

**R3 (H/M) Large custom attributes abort (debug) or return undocumented
RANGE.**
- Code: `lfs3_setattr` 12364-12417 has no size limit.
- [PROBE] 4096 B blocks:
  - 1000, 1900 and 2100 B: OK.
  - 2500 B: `Assertion failed: (err != LFS3_ERR_RANGE), lfs3_mdir_commit_,
    lfs3.c:9006`.
  - With NO_ASSERT, 5000 B returns -34 (RANGE). That code is not in the API
    docs; v2 had LFS_ATTR_MAX and NOSPC.
- File attrs through `lfs3_file_cfg` presumably take the same path through
  the MOVE/ATTRS commit (unverified).
- Tests go only up to 513 B.

**R4 (H) `lfs3_fs_grow` beyond the device bricks the configuration; shrink
is only an assert.**
- Code: 16899-16999 has no `block_count_ <= cfg->block_count` check.
- [PROBE]
  - `fs_grow(128)` on a 64-block cfg returns 0, and `fs_stat` reports 128.
  - Filling the fs makes the allocator erase block 64 (out of bounds): IO
    -5.
  - After unmount, `mount(cfg with 64 blocks)` fails with NOTSUP
    "Incompatible block count 128 (> 64)".
- Shrinking: `LFS3_ASSERT(block_count_ >= lfs3->block_count)` (16903). With
  NO_ASSERT, [PROBE] `fs_grow(smaller)` returned 0 and committed the smaller
  geometry, although blocks above it may still be in use.

**R5 (H for RDONLY users) LFS3_RDONLY mount compares uninitialized limits.**
- Code: `lfs3_init` sets `name_limit`/`file_limit` only under `!RDONLY`
  (15231-15243). `lfs3_mountmroot` compares them (15715, 15741) and assigns
  them afterwards.
- [PROBE] RDONLY build: a zero-initialized `lfs3_t` gets NOTSUP
  "Incompatible name limit 255 (> 0)"; a 0xff-filled one mounts.
- So a static `lfs3_t` never mounts. Likely fix direction (not done):
  compare against `LFS3_NAME_MAX`/`LFS3_FILE_MAX`.

**R6 (M/H) Several documented options do not compile at b10efaa.**
- `LFS3_CKDATACKSUMS` (and so `LFS3_BIGGEST`): lfs3.c:1736-1737 and 1808 use
  `data.u.disk.*` where `data` is `const lfs3_data_t*`.
- `LFS3_PMUL_CRC32C`: lfs3_util.c:210 calls the undefined `lfs3_fromle32_`.
- `LFS3_RDONLY+LFS3_CKMETAPARITY`: 15206-15207 refer to `ptail`, which does
  not exist in RDONLY.
- `LFS3_RDONLY+LFS3_GBMAP`: 10483 `gbmap->next`, 10499/10509 ecksum,
  15097 `LFS3_F_GBMAP`.
- `LFS3_RDONLY+LFS3_GC`: 15143-15146 `gc_compact_thresh`.
- Effect at b10efaa: test_ck_ckdatacksums_data and the METHOD=3 spam cases
  are dead. The whole CKDATACKSUMS feature is unverifiable.
- Status: the CKDATACKSUMS/BIGGEST item is fixed on v3-fixes `e4c046b`
  (`data.` → `data->`). PMUL and the three RDONLY combinations still fail.

**R7 (M) `lfs3_set` bypasses `file_limit`.**
- Code: `LFS3_o_WRSET` passes the data through the file cache
  (12609-12614, 15045-15071), so the FBIG check in `lfs3_file_write` (14123)
  never runs.
- [PROBE] With `file_limit=100`, `set` of 101 B and of 8192 B both return 0.
  `lfs3_size` returns 8192, and `lfs3_file_seek(0, SEEK_END)` then returns
  INVAL. The image still mounts.
- This breaks I5 and the [DOC] "must be respected by other littlefs
  drivers".

**R8 (M) `lfs3_init` NOMEM cleanup frees uninitialized pointers.**
- Code: on a failed rcache malloc, `lfs3_deinit` frees `pcache.buffer` and
  `lookahead.buffer`, which were never assigned (init 15175-15222 vs deinit 15400-15418).
  The same happens for the lookahead buffer when the pcache malloc fails.
- [PROBE] With the first or second malloc forced to NULL and `lfs3_t` filled
  with 0xab, format aborts (SIGABRT from free, exit 134). With the third
  malloc failing it correctly returns -12.
- This affects both `lfs3_format` and `lfs3_mount`.

**R9 (M) Detection of unknown config tags has a hole.**
- Code: 15750-15764 look only at the first tag ≥ 0x013b.
- [PROBE] A config tag 0x0101, 0x0132 or 0x0133 committed to the mroot mounts
  (0). 0x0142 gives NOTSUP.
- Future config tags in 0x0100-0x0130 or 0x0132-0x0133 would be silently
  ignored by this driver. That violates the rule that config must be
  understood.

**R10 (M) rcompat and wcompat need exact equality, not "understood".**
- Code: `rmask = ~0` (15441-15444). The check is `(rcompat_ & rmask) !=
  (rcompat & rmask)` (15611).
- [PROBE] rcompat minus MTREE gives NOTSUP even RDONLY. wcompat minus DIR
  gives NOTSUP for RDWR.
- [DOC lfs3.h:920-926] says "Must understand to read", meaning the driver
  must *know* every set bit. An image that uses *fewer* features is refused.
- Untested; this is a compatibility policy question for the spec.

**R11 (M) A gcksum mismatch is fatal at mount, with no degraded mode.**
- Code: 15894-15900; TODOs at 15921 and 15948.
- [PROBE] Rolling back one non-latest mdir gives -84 for RDONLY and RDWR.
- One lost or corrupted tail commit (bit rot, a bad prog without CKPROGS)
  in any mdir except the last written makes the whole volume unreadable.
  Meanwhile the most recent commit can roll back silently (by design).
- Post-mount rollbacks are served silently until the next ckmeta scan
  [PROBE `lfs3_get` returned the stale value].
- Unverified: a subsequent commit to the rolled-back mdir would combine the
  stale cksum into gcksum.

**R12 (M) Contract violations are asserts; NO_ASSERT builds proceed.**
- Every mutating API on an `M_RDONLY` mount reaches
  `LFS3_ASSERT(!lfs3_m_isrdonly)` in `lfs3_fs_mkconsistent` (16620).
  [PROBE] `mkdir` on a RDONLY mount aborts.
- With NO_ASSERT the call continues and writes to a "read-only" mount.
- Other examples: `fs_grow` shrink (R4); `lfs3_data_readgrm` asserting that
  the on-disk mid is within bounds (7528); `lfs3_stat_` asserting the on-disk
  name length (12024); the mtree lookup asserts (8027-8052). A
  checksum-valid but malicious or corrupt image can therefore trigger an
  assert, or undefined behaviour under NO_ASSERT.
- The spec should say which violations must be error codes (for example
  EROFS-like INVAL/NOTSUP for writes on RDONLY).

**R13 (L/M) Big-endian revision comparison in `lfs3_mdir_fetch`.**
- Code: 7859-7876. Each block's revision is read into `revs[0]`, but the code
  converts `revs[i]`. For i=1 it converts the already-converted `revs[1]`
  and leaves the new `revs[0]` raw.
- Little-endian hosts are unaffected (`fromle32` is the identity). On a
  big-endian host both values are byte-swapped, so the ordering is wrong. The
  older valid block may be fetched first, which is a silent metadata rollback
  at fetch.
- Not verified; there is no big-endian host.

**R14 (L) `lfs3_fs_consumegdelta` ignores lookup errors for GBMAPDELTA.**
- Code: 7744-7746 test only `!= NOENT`. An IO or CORRUPT result makes it
  read the uninitialized `data`. The GRMDELTA path does check (7720-7727).

**R15 (L) Pcache overlap in the `lfs3_bd_read` bypass path (bd layer; for the
bd analyst).**
- Code: at 260-273 the direct-read branch sets `d =
  lfs3_aligndown(size_, read_size)`. That ignores the earlier clamp of `d` to
  `pcache.off - off_` (and `rcache.off - off_`).
- A large aligned read that runs into an unflushed pcache region of the same
  block would read stale disk data. Unverified.

**R16 (L) `lfs3_bd_cmpck` subtracts `off` from the hint twice** (911).
Performance only.

**R17 (L) `lfs3_fromleb128` shifts signed values** (lfs3_util.c:37-56). For
the 5th byte with bit 3 or above set this is undefined behaviour.
Functionally it rejects values ≥ 2^31 with CORRUPT, which is the intended
31-bit limit.

**R18 (L) FROM_BRANCH encodes into `ctx.u.ecksum.buf`** (8 B) where
`branch.buf` (13 B) was meant (3506-3511). `-Warray-bounds` fires in every
build. It is benign only because of the union layout.
Fixed on v3-fixes `ba31df7`.

**R19 (M for multithreaded users) `LFS3_THREADSAFE` is a no-op.** The
`cfg->lock`/`unlock` fields exist (lfs3.h:495-503) but lfs3.c never calls
them. CI still passes `-DLFS_THREADSAFE` (the v2 name).

**R20 (L) Config header macro mismatch.** `lfs3_util.h:21` uses
`LFS3_CFG`, while `lfs3_util.c:11` guards with `LFS3_CONFIG`. With a custom
`LFS3_CFG`, `lfs3_util.c` still emits the default crc32c and leb128 code,
which can duplicate symbols.

**R21 (L) `lfs3_strspn` fallback** (lfs3_util.h:719-734, NO_STRINGH). It
returns at the first character that differs from *any* character in `cs`,
which is wrong when `cs` has two or more characters. The comments on
strspn/strcspn are swapped. Harmless today because every caller passes
`"/"`.

**R22 (L) Header and implementation mismatches.**
- `int lfs3_file_rewind` (lfs3.h:1640) vs `lfs3_soff_t` (14700).
- `lfs3_file_opencfg_` is non-static (12590): a namespace leak.
- `lfs3_file_open` is defined even with `LFS3_NO_MALLOC`.
- `lfs3_fs_grow` takes `lfs3_size_t`, not `lfs3_block_t`.

**R23 (L) Stale or incorrect docs.**
- `lfs3_fs_unck` text (lfs3.h:1801-1810).
- `lfs3_info.type` "either REG or DIR" (721); 3 and 7 are also returned.
- The test_paths_root comment says EINVAL; code and test use BUSY.
- README, DESIGN.md, SPEC.md and CI are v2.
- The gc_compact_thresh comment says "~88%"; the code uses 87.5%.

**R24 (L) Alpha images are all v0.0.** Format changes between alpha commits
are undetectable (2.4).

**R25 (L) `fs_stat` on a RDONLY mount reports `I_MKCONSISTENT`,
`I_LOOKAHEAD` and `I_COMPACT`** (set unconditionally in `lfs3_init`
15156-15170).
- [PROBE] flags `0x3b01`.
- An LFS3_RDONLY build mounted with flags=0 does not report `I_RDONLY`
  [PROBE `0x3000`].

**R26 (M) `remove`/`rename` return success when grm cleanup failed**
(11818-11828, 12000-12010). The grm stays pending and is retried by the
next mkconsistent. If the failure is persistent (NOSPC), every later write
fails inside `fs_mkconsistent`.

**R27 (info) Known excluded or failing tests in this area.**
test_dirs_rm_many_2layers powerloss N=4 (test_dirs.toml:3442) is a real
directory powerloss bug that the author acknowledges.

**R28 (L) Minimum geometry is not validated.**
- `block_count < 2` (or < 3 with gbmap) asserts in the bd layer during
  format [PROBE `block_count=1` aborts at lfs3.c:570].
- `block_size` has no lower bound: `mbits = nlog2(bs)-3`, and tag estimates
  assume a sane size.
- `fcache_size=0` is unvalidated.

**R29 (info) `lfs3_file_ck(CKMETA)` does not cover the file's mdir**
(14918-15000). The header says "Check a file for errors". A requirement
should either state the btree-only scope or extend it.

**R30 (L) The did collision search in `mkdir`** (11560-11575) loops without a
bound. It terminates in practice because `dmask` scales with the number of
mdirs.

---------------------------------------------------------------------------

## 8. Pointers for the deliverables

- Requirements, for SMART pass/fail criteria. Suggested pass conditions:
  - R1: `rename(dir, dir/sub)` → INVAL, and nothing on disk changes.
  - R2: `seek(tell())` round-trips for every position.
  - R3: a documented attr max size, with NOSPC or RANGE returned instead of
    an assert.
  - R4: `fs_grow` > cfg → INVAL.
  - R5: an RDONLY build mounts a zeroed `lfs3_t`.
  - R7: `set` > `file_limit` → FBIG.
  - R8: NOMEM leaves no crash.
  - R9 and R10: a defined compat policy.
  - R12: writes on a RDONLY mount → an error code.
  - R11: an optional read-only degraded mount on gcksum mismatch, or a
    documented hard failure.
- SPEC.md v3: section 2 gives the byte-level superblock, commit trailer and
  gstate formats; `scripts/dbglfs3.py:40-90` has the authoritative tag table
  with bit patterns. The *canonical cksum* definition (what is and is not
  covered) must be written down precisely, because gcksum depends on it.
- Test plan:
  - Build matrix: default, RDONLY, GC, GBMAP, GBMAP+REVPERTURB+PREERASE,
    each CK* option, YES_* variants, NO_ASSERT, NO_MALLOC,
    NO_STRINGH+NO_BUILTINS, BIGGEST.
  - Flash-failure recovery: CKPROGS with PROGFLIP bad blocks (exists, but
    not run by default); READFLIP with CKMETAPARITY and CKDATACKSUMS;
    deterministic rollback cases (1.11 probe); whole-block clobber
    (test_mount_t_ck*); powerloss (reentrant cases, `-P`).
  - Add the untested items in 5.2.
