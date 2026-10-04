## littlefs v3 test plan

This document plans the testing of littlefs v3 against the requirements in
[REQUIREMENTS.md](REQUIREMENTS.md). It says which tests exist, which are
missing, which configurations they must run in, and when the work is done.

The goal, in the words of the request that started it: "ensure all features
are tested ... Done is when all tests pass including recovery from flash
failures." Section 7 turns that into exit criteria.

```
   | | |     .---._____
  .-----.   |          |
--|o    |---| littlefs |
--|     |---|  tests   |
  '-----'   '----------'
   | | |
```

## Contents

1. [Purpose, scope and references](#1-purpose-scope-and-references)
2. [Test levels](#2-test-levels)
3. [Feature traceability](#3-feature-traceability)
4. [Flash-failure matrix](#4-flash-failure-matrix)
5. [Configuration matrix](#5-configuration-matrix)
6. [New test specifications](#6-new-test-specifications)
7. [Entry and exit criteria](#7-entry-and-exit-criteria)
8. [Procedures](#8-procedures)
9. [Risks and limits](#9-risks-and-limits)

## 1. Purpose, scope and references

### 1.1 Purpose

The v3 test suite is large: 25 suites, 842 cases and 634,616 permutations in
the default build, with 64 reentrant cases that run under power loss. It is
also uneven. Most of the gbmap, gc, pre-erase and checksum cases are compiled
out of the default build, only four cases exercise torn writes, no case
combines power loss with bad blocks, and upstream CI does not run v3 at all
(REQUIREMENTS.md, LFS3-CI-01). This plan closes those gaps in a fixed order:
first recovery from flash failures and the known defects, then the remaining
features, then performance and documentation checks.

### 1.2 Scope

- **Code under test.** littlefs v3-alpha at `b10efaa`, plus the fixes on the
  fork's branches (`v3-fixes`, `v3-fix-alloc`, `v3-fix-api`, `v3-fix-files`,
  `v3-fix-parity`, `v3-ci`; REQUIREMENTS.md Appendix A). The plan assumes these
  branches are merged into one test branch before the exit run (section 7).
- **What is tested.** Every public function of `lfs3.h`, every field of
  `struct lfs3_cfg` and `struct lfs3_file_cfg`, every compile-time option, and
  every flash-failure class that emubd can inject or can be extended to inject.
- **What is not.** v2 and v2-to-v3 migration; metadata and data redundancy;
  16-bit and 64-bit variants; the stretch goals of PR #1111. Bad-block tracking
  (release blocker #1) is planned in REQUIREMENTS.md 6.5; its tests (NEW-90)
  become required when the feature lands.

### 1.3 References

- **REQUIREMENTS.md** (this directory). Requirements are cited by ID, with the
  `LFS3-` prefix dropped in tables: `PL-04` is LFS3-PL-04. Defect refs (`F-2`,
  `D-2`, `2-files B1`) point to its Appendix A. Environment names come from its
  section 5: builds B-DEF, B-BIG, B-YGB, B-RO, B-YES-x, B-NA, B-NB, B-NS, B-NM,
  B-TS; architectures A-64LE, A-32LE, A-32BE; geometries G-NOR, G-EEPROM, G-P16,
  G-EMMC, G-NAND, G-BIGNAND (together G-ALL) and G-W25Q128; power-loss
  behaviour sets PLB-TORN (0-3) and PLB-ALL (0-4).
- **Test runner.** `scripts/test.py` and `runners/test_runner.c`. Cases are TOML
  in `tests/`; `make test` builds `runners/test_runner` and runs every suite
  with `-Pnone` and `-Plinear`. Test ids such as
  `test_grow_incr_spam_uzd_fuzz:s11t12u10g28h2gg4j20k2gg8` replay one
  permutation, including its power-loss sequence.
- **emubd.** `bd/lfs3_emubd.c`: the RAM block device with power-loss
  behaviours (`POWERLOSS_BEHAVIOR`), bad-block behaviours
  (`BADBLOCK_BEHAVIOR`), wear (`ERASE_CYCLES`, `lfs3_emubd_setwear`), bit
  flips (`lfs3_emubd_flipbit`, `lfs3_emubd_flip`, `lfs3_emubd_mkbadbit`),
  operation counters (`lfs3_emubd_reads/progs/erases`,
  `lfs3_emubd_readed/progged/erased`), per-block wear (`lfs3_emubd_wear`) and
  copy-on-write snapshots for `-P'permute(n)'`.
- **Benches.** `benches/*.toml`, `scripts/bench.py`, `runners/bench_runner.c`
  (kiwibd by default).

### 1.4 Naming

- `suite::case` names the case `test_<suite>_<case>` in
  `tests/test_<suite>.toml`, as in REQUIREMENTS.md.
- `NEW-nn` names a test that does not exist at `b10efaa`. Section 6 specifies
  each one. Some are already written on the fork's branches; section 6 says
  where.
- `J-xxx` names a configuration-matrix job (section 5).

## 2. Test levels

Each level below says what it finds, which existing cases form it, and what
this plan adds.

### 2.1 Unit tests of the data structures

Internal cases (`in = 'lfs3.c'`) that call static functions directly.

- `test_rbyd` (107 cases): append, lookup, removal, range deletes, weights,
  every rotation and split, exhaustive insertion orders up to 7 tags, 1,000-seed
  fuzzing. Each permutation asserts a size bound that implies balance
  (LFS3-META-01). Runs with `ERASE_VALUE` 0xff, 0x00 and -1 and 32 KiB blocks.
- `test_btree` (50 cases): push, update, pop, split, merge, drop, find,
  traversal, fuzzing.
- `test_mtree` (52 cases): mroot, uninlining, split, drop, relocation, mroot
  chain extension, open handles across structural changes, truncated commits,
  the "littlefs" magic.
- `test_gbmap` `set_*` cases (11): range split, merge and replace, with and
  without erased-state checksums.

This plan adds: the balance-check build (NEW-69), revision wrap (NEW-70),
estimate checks (NEW-95), power loss in `test_mtree` (NEW-07), perturb and
erased-state checksum tests (NEW-66), and other block sizes (J-GEO).

### 2.2 Feature suites

Black-box cases through the public API: `test_dirs`, `test_dread`,
`test_files`, `test_paths`, `test_alloc`, `test_fwrite`, `test_fsync`,
`test_stickynotes`, `test_attrs`, `test_kv`, `test_trvs`, `test_gc`,
`test_mount`, `test_grow`, `test_compat`, `test_bd`. Section 3 maps them to
features. Most use a model ("sim") of the expected state and compare after
every operation and after remount.

### 2.3 Power-loss (reentrant) testing

A reentrant case is rerun from the top on the same disk after every injected
power loss and must remount and continue. Schedules (`-P`): `none`, `linear`
(one loss at each write in turn, the default), `log`, `permute(n)` and
`exhaustive` (REQUIREMENTS.md 5.4). Behaviours of the interrupted write
(`-DPOWERLOSS_BEHAVIOR`): ATOMIC, SOMEBITS, MOSTBITS, OOO and METASTABLE (5.5).

Today 64 cases are reentrant (15,904 permutations; 2,226,875 power losses in
the default run), and only the four `test_powerloss` cases set a behaviour
other than ATOMIC. This plan runs every reentrant case with PLB-ALL nightly
(J-PL), adds reentrant cases for every operation that writes (NEW-01 to
NEW-11, NEW-55 to NEW-59), and adds `permute(1)` and `permute(2)` runs.

### 2.4 Flash-failure testing

Non-reentrant cases that make blocks bad before or during a workload, using
`BADBLOCK_BEHAVIOR` 0-7, `lfs3_emubd_mkbad`, `ERASE_CYCLES` and bit flips:
`test_badblocks` (26 cases), `test_exhaustion` (5), `test_ck` (28),
`test_relocations` (8). Section 4 is the full matrix. Three gaps dominate:
behaviours 2 to 4 run only with `LFS3_CKPROGS` (so not in B-DEF), blocks never
go bad after data has been written to them, and nothing combines bad blocks
with power loss. Faults emubd cannot inject need extensions (section 6.4).

### 2.5 Fuzzing

129 cases are seeded-PRNG fuzzers (`fuzz = 'SEED'`), for example
`dirs::mvrm_fuzz`, `fwrite::fuzz_unaligned`, `fsync::*_fuzz`,
`badblocks::*_spam_*_fuzz`, `ck::spam_*_fuzz`. They are deterministic and
replayable by test id. They are not coverage-guided; coverage-guided fuzzing
(#1030) is out of scope for this plan except as a P2 follow-up (section 9).
New fuzz cases in this plan follow the same pattern: a fixed list of seeds in
`defines.SEED` and a model checked after every step.

### 2.6 Sanitizers and memory checkers

- **AddressSanitizer and UndefinedBehaviorSanitizer.** A runner built with
  `-fsanitize=address,undefined -fno-omit-frame-pointer`, run with `-Pnone`
  and `-Plinear`. Measured: B-BIG under ASan and UBSan took 3,202 s on 14
  cores and found D-2, 1-meta 0.7c and a test-side zero-length array
  (REQUIREMENTS.md B.3).
- **valgrind.** `test.py --valgrind -Pnone` (power-loss `longjmp`s leak the
  test's allocations, so valgrind runs without power loss).
- **glibc FORTIFY.** The default GCC build on Ubuntu (`_FORTIFY_SOURCE` is on by
  default there) caught 56 test-side overflows that macOS did not (F-5).
- **ThreadSanitizer.** Only for LFS3-THR-03 (NEW-124).

All three of ASan/UBSan, valgrind and FORTIFY run nightly (J-SAN). Test code
is held to the same standard as littlefs (LFS3-CI-09).

### 2.7 Cross-architecture testing

- A-32LE (thumb under qemu-arm): 32-bit `size_t` and pointers.
- A-32BE (mips and powerpc under qemu): big-endian hosts. This is the only
  way to reach LFS3-META-04 (the revision-order defect), and it checks that
  every on-disk encoding is byte-order independent (LFS3-GEN-02, NEW-91).

### 2.8 Benches

`make bench` runs `bench_wt`, `bench_rt`, `bench_file`, `bench_dir`,
`bench_btree` and `bench_rbyd` on kiwibd with NOR and NAND timing models. They
record reads, programs and erases but assert nothing. This plan adds ratio
checks for the complexity claims of PR #1111 (NEW-119), the sync-cost and
logging-workload benches of REQUIREMENTS.md Appendix B (NEW-120, NEW-121), and
a regression report (NEW-122). Benches are nightly and gate only on large
regressions.

## 3. Feature traceability

Each row lists the requirements that cover an item, the existing cases that
check them, and the gaps: every covering requirement that is not fully
tested at `b10efaa`, with its status (P partly, U untested, N not
implemented, K known defect) and the new test or job that closes it. The
rows are generated from REQUIREMENTS.md (Appendix C and each requirement's
Pass and Verified by fields), so they change when it does. "review" marks a
documentation requirement checked by review rather than by a test.

### 3.1 Public functions

| Function | Requirements | Existing tests | Gaps → new tests or jobs |
|---|---|---|---|
| `lfs3_format` | PL-22, FAIL-16, PRE-06, GC-13, MOUNT-02, MOUNT-27, CFG-02, CFG-03, RES-06 | `badblocks::mrootanchor_format`, `ck::ckprogs_mroot`, `mount::t_lookahead`, `mount::t_compact`, `mount::t_mkconsistent`, `mount::t_ckmeta` and 6 more | PL-22 (U) → NEW-01; FAIL-16 (P) → NEW-16; PRE-06 (K) → NEW-22; GC-13 (P) → J-BIG; MOUNT-27 (P) → NEW-114; CFG-02 (U) → NEW-115; CFG-03 (U) → NEW-115; RES-06 (K) → NEW-50 |
| `lfs3_mount` | PL-01, PL-20, PL-22, INT-06, INT-12, PRE-06, GC-13, MOUNT-04, MOUNT-05, MOUNT-06, MOUNT-07, MOUNT-09, MOUNT-10, MOUNT-11, MOUNT-12, MOUNT-15, MOUNT-16, MOUNT-19, MOUNT-25, CFG-02, CFG-03, RES-06, BUILD-12 | `powerloss::*`, `dirs::*`, `files::pl_fuzz`, `relocations::spam_*_pl_fuzz`, `grow::incr_spam_*_pl_fuzz`, `stickynotes::*_pl` and 33 more | PL-20 (U) → NEW-05; PL-22 (U) → NEW-01; INT-06 (P) → NEW-65; PRE-06 (K) → NEW-22; GC-13 (P) → J-BIG; MOUNT-09 (K) → NEW-45; MOUNT-11 (U) → NEW-110; MOUNT-12 (P) → NEW-111; MOUNT-16 (U) → NEW-84; MOUNT-19 (K) → NEW-46; MOUNT-25 (U) → NEW-85; CFG-02 (U) → NEW-115; CFG-03 (U) → NEW-115; RES-06 (K) → NEW-50; BUILD-12 (U) → NEW-125 |
| `lfs3_unmount` | FILE-16, FILE-21, DIR-20, GC-16, MOUNT-18 | `alloc::nospc_files`, `dirs::*`, `dread::*`, `trvs::*` | FILE-16 (U) → NEW-72; FILE-21 (P) → NEW-74; MOUNT-18 (P) → NEW-112 |
| `lfs3_get` | GEN-02, PL-12, FILE-12, SYNC-01, KV-01, KV-05, KV-08, CFG-07 | `kv::set_*`, `files::trunc`, `powerloss::spam_f_pl_fuzz`, `fsync::wrrr`, `fsync::wwww`, `fsync::wwrr` and 9 more | GEN-02 (U) → NEW-91; PL-12 (U) → NEW-55; KV-05 (P) → NEW-104; KV-08 (U) → NEW-104; CFG-07 (U) → NEW-86 |
| `lfs3_size` | PL-12, KV-02, KV-05, KV-07, KV-08 | `kv::set_*`, `kv::*`, `kv::set_noent`, `kv::set_zero`, `kv::set_null` | PL-12 (U) → NEW-55; KV-05 (P) → NEW-104; KV-08 (U) → NEW-104 |
| `lfs3_set` | GEN-06, PL-12, DIR-02, KV-03, KV-04, KV-06, KV-07, KV-09 | `kv::set_*`, `paths::namejustlongenough`, `kv::set`, `kv::set_trunc`, `kv::set_update`, `kv::many_big` and 4 more | GEN-06 (K) → NEW-49; PL-12 (U) → NEW-55; DIR-02 (K) → NEW-41; KV-04 (K) → NEW-36; KV-09 (U) → NEW-105 |
| `lfs3_remove` | GEN-06, PL-09, DIR-03, ALLOC-05 | `dirs::rm_*`, `dread::recursive_rm`, `dirs::rm_root`, `paths::root` | GEN-06 (K) → NEW-49; ALLOC-05 (U) → NEW-30 |
| `lfs3_rename` | GEN-06, PL-10, META-03, DIR-04, DIR-05, DIR-06, DIR-07 | `dirs::mv_*`, `dread::recursive_mv`, `paths::*`, `dirs::mv_noop` | GEN-06 (K) → NEW-49; META-03 (K) → NEW-41; DIR-05 (K) → NEW-32 |
| `lfs3_stat` | GEN-02, PL-07, PL-08, PL-09, PL-10, META-09, FILE-19, SYNC-10, SYNC-13, SYNC-14, DIR-05, DIR-08, DIR-14, PERF-04, DOC-08 | `stickynotes::uncreat_pl`, `stickynotes::uncreat_many_pl`, `stickynotes::undesync_pl`, `stickynotes::undesync_many_pl`, `dirs::mkdir_*`, `dirs::rm_*` and 10 more | GEN-02 (U) → NEW-91; FILE-19 (P) → NEW-98; SYNC-10 (P) → NEW-99; DIR-05 (K) → NEW-32; PERF-04 (U) → NEW-119; DOC-08 (K) → review |
| `lfs3_getattr` | PL-11, ATTR-01, ATTR-02 | `attrs::fattr_pl_fuzz_fuzz`, `attrs::setattr*`, `attrs::getattr*`, `attrs::setattr_trunc`, `attrs::fuzz`, `attrs::*noattr*` | PL-11 (P) → NEW-59 |
| `lfs3_sizeattr` | PL-11, ATTR-02, ATTR-03, ATTR-04, ATTR-10, ATTR-13 | `attrs::fattr_pl_fuzz_fuzz`, `attrs::*noattr*`, `attrs::removeattr`, `attrs::setattr_zero`, `attrs::setattr_null`, `attrs::fattr_*` and 1 more | PL-11 (P) → NEW-59; ATTR-10 (P) → NEW-103 |
| `lfs3_setattr` | GEN-06, PL-11, META-03, ATTR-01, ATTR-05, ATTR-06, ATTR-08 | `attrs::fattr_pl_fuzz_fuzz`, `attrs::setattr*`, `attrs::getattr*`, `attrs::setattr_trunc`, `attrs::fuzz`, `attrs::*` and 1 more | GEN-06 (K) → NEW-49; PL-11 (P) → NEW-59; META-03 (K) → NEW-41; ATTR-05 (K) → NEW-41; ATTR-06 (U) → NEW-78 |
| `lfs3_removeattr` | GEN-06, PL-11, ATTR-03, ATTR-08 | `attrs::fattr_pl_fuzz_fuzz`, `attrs::removeattr`, `attrs::fattr_*` | GEN-06 (K) → NEW-49; PL-11 (P) → NEW-59 |
| `lfs3_file_open` | FILE-16, CFG-07, RES-08, BUILD-10, FILE-14, FILE-15 | `files::*`, `fwrite::*`, `files::create`, `files::excl`, `stickynotes::uncreat_excl`, `stickynotes::orphan_excl` and 7 more | FILE-16 (U) → NEW-72; CFG-07 (U) → NEW-86; RES-08 (U) → NEW-87; BUILD-10 (K) → NEW-53 |
| `lfs3_file_opencfg` | GEN-06, INT-13, META-03, FILE-15, DIR-14 | `ck::file_ckmeta_easy`, `ck::file_ckmeta_hard`, `ck::file_ckdata_easy`, `ck::file_ckdata_hard`, `files::noent`, `files::dir_not_file` and 5 more | GEN-06 (K) → NEW-49; INT-13 (P) → NEW-67; META-03 (K) → NEW-41 |
| `lfs3_file_close` | PL-04, PL-06, FILE-21, SYNC-19 | `powerloss::spam_f_pl_fuzz`, `powerloss::spam_fd_pl_fuzz`, `files::pl_fuzz`, `relocations::spam_f_pl_fuzz`, `grow::incr_spam_f_pl_fuzz`, `alloc::nospc_files` and 1 more | PL-06 (P) → NEW-58; FILE-21 (P) → NEW-74 |
| `lfs3_file_sync` | PL-04, PL-06, SYNC-05, SYNC-06, SYNC-08, SYNC-09, SYNC-17, MOUNT-17, PERF-06, PERF-07, PERF-10 | `powerloss::spam_f_pl_fuzz`, `powerloss::spam_fd_pl_fuzz`, `files::pl_fuzz`, `relocations::spam_f_pl_fuzz`, `grow::incr_spam_f_pl_fuzz`, `fsync::desync_*` and 6 more | PL-06 (P) → NEW-58; SYNC-05 (K) → NEW-38; SYNC-09 (U) → NEW-77; MOUNT-17 (K) → NEW-37; PERF-06 (P) → NEW-120; PERF-07 (K) → NEW-120; PERF-10 (P) → NEW-121 |
| `lfs3_file_flush` | PL-06, SYNC-05, SYNC-10 | `powerloss::spam_f_pl_fuzz`, `fsync::sync_*` | PL-06 (P) → NEW-58; SYNC-05 (K) → NEW-38; SYNC-10 (P) → NEW-99 |
| `lfs3_file_desync` | SYNC-04 | `fsync::desync_*`, `fsync::drrr`, `fsync::wddd`, `stickynotes::undesync_*` | none |
| `lfs3_file_resync` | SYNC-07, SYNC-08 | `fsync::resync_*`, `fsync::yrrr`, `fsync::wyyy`, `kv::interop_resync`, `stickynotes::zombie_*`, `stickynotes::zombify_*` | none |
| `lfs3_file_read` | INT-20, FILE-01, FILE-04 | `ck::ckdatacksums_data`, `ck::spam_*_fuzz`, `fwrite::simple`, `fwrite::incr`, `fwrite::reversed`, `fwrite::overwrite` and 3 more | INT-20 (K) → J-BIG; FILE-04 (K) → NEW-35 |
| `lfs3_file_write` | PL-06, PL-17, FILE-02, FILE-03, FILE-07, FILE-26, SYNC-05, PERF-10 | `powerloss::spam_f_pl_fuzz`, `fwrite::holes`, `fwrite::w_seek`, `fwrite::fbig`, `fwrite::truncate_fbig`, `fwrite::fruncate_fbig` | PL-06 (P) → NEW-58; PL-17 (U) → NEW-57; FILE-02 (U) → NEW-96; FILE-03 (K) → NEW-34; FILE-26 (U) → NEW-76; SYNC-05 (K) → NEW-38; PERF-10 (P) → NEW-121 |
| `lfs3_file_seek` | FILE-06, FILE-07, FILE-20, FILE-26, KV-04 | `fwrite::r_seek`, `fwrite::w_seek`, `fwrite::rw_seek`, `fwrite::seek_negative`, `fwrite::holes`, `fwrite::fbig` and 2 more | FILE-26 (U) → NEW-76; KV-04 (K) → NEW-36 |
| `lfs3_file_truncate` | PL-13, FILE-08, FILE-18, FILE-26, SYNC-05 | `fwrite::truncate`, `fwrite::truncate_truncate`, `fwrite::truncate_pos`, `fwrite::truncate_litmus_zero`, `fwrite::truncate_litmus_fragment`, `fwrite::fbig` and 2 more | PL-13 (U) → NEW-02; FILE-18 (U) → NEW-97; FILE-26 (U) → NEW-76; SYNC-05 (K) → NEW-38 |
| `lfs3_file_fruncate` | PL-13, FILE-09, FILE-10, FILE-26, SYNC-05, DOC-18 | `fwrite::fruncate*`, `fwrite::freversed*`, `fwrite::rwtf_fuzz`, `fwrite::fbig`, `fwrite::truncate_fbig`, `fwrite::fruncate_fbig` | PL-13 (U) → NEW-02; FILE-10 (K) → NEW-21; FILE-26 (U) → NEW-76; SYNC-05 (K) → NEW-38; DOC-18 (N) → review |
| `lfs3_file_tell` | FILE-02, FILE-06, FILE-08, FILE-09, FILE-20 | `fwrite::r_seek`, `fwrite::w_seek`, `fwrite::rw_seek`, `fwrite::seek_negative`, `fwrite::truncate`, `fwrite::truncate_truncate` and 6 more | FILE-02 (U) → NEW-96 |
| `lfs3_file_rewind` | FILE-20, BUILD-11 | `fwrite::r_seek`, `fwrite::w_seek`, `fwrite::rw_seek` | BUILD-11 (K) → NEW-53 |
| `lfs3_file_size` | FILE-02, FILE-19, FILE-22, SYNC-09 | `fsync::*`, `files::zero_bnull`, `files::zero_bshrub`, `files::zero_btree` | FILE-02 (U) → NEW-96; FILE-19 (P) → NEW-98; SYNC-09 (U) → NEW-77 |
| `lfs3_file_ck` | INT-13 | `ck::file_ckmeta_easy`, `ck::file_ckmeta_hard`, `ck::file_ckdata_easy`, `ck::file_ckdata_hard` | INT-13 (P) → NEW-67 |
| `lfs3_mkdir` | GEN-06, PL-08, META-03, DIR-01, DIR-02, DIR-14 | `dirs::mkdir_*`, `paths::*`, `paths::namejustlongenough` | GEN-06 (K) → NEW-49; META-03 (K) → NEW-41; DIR-02 (K) → NEW-41 |
| `lfs3_dir_open` | DIR-19 | `dirs::*`, `paths::*` | none |
| `lfs3_dir_close` | DIR-20 | `dirs::*`, `dread::*` | none |
| `lfs3_dir_read` | GEN-02, PL-07, PL-08, SYNC-13, SYNC-14, DIR-09, DIR-13, DOC-08 | `stickynotes::uncreat_pl`, `stickynotes::uncreat_many_pl`, `stickynotes::undesync_pl`, `stickynotes::undesync_many_pl`, `dirs::mkdir_*`, `stickynotes::uncreat_*` and 6 more | GEN-02 (U) → NEW-91; DOC-08 (K) → review |
| `lfs3_dir_seek` | DIR-11 | `dread::seek` | DIR-11 (K) → NEW-33 |
| `lfs3_dir_tell` | DIR-11 | `dread::seek` | DIR-11 (K) → NEW-33 |
| `lfs3_dir_rewind` | DIR-12 | `dread::rewind` | none |
| `lfs3_trv_open` | GC-08, GC-10, GC-11 | `trvs::mutation_*`, `trvs::compact_*`, `trvs::mkconsistent_*`, `trvs::flags`, `ck::spam_*` | GC-11 (P) → NEW-108 |
| `lfs3_trv_close` | GC-16 | `trvs::*` | none |
| `lfs3_trv_read` | INT-14, BAD-13, ALLOC-17, GC-07, GC-08 | `trvs::ckmdir_*`, `trvs::ckbtree_*`, `trvs::ckdata_*`, `files::*`, `grow::*`, `trvs::simple` and 3 more | BAD-13 (N) → NEW-90; ALLOC-17 (P) → NEW-106 |
| `lfs3_trv_rewind` | GC-15 | `trvs::rewind`, `trvs::rewind_clobber_*` | none |
| `lfs3_fs_stat` | PL-18, PL-19, INT-16, GC-03, MOUNT-10, MOUNT-12, MOUNT-13, MOUNT-19, MOUNT-20, MOUNT-21, BUILD-12 | `grow::incr_spam_f_pl_fuzz`, `grow::incr_spam_fd_pl_fuzz`, `gbmap::mkgbmap`, `gbmap::rmgbmap`, `gbmap::rmmkgbmap`, `gbmap::mkrmgbmap` and 19 more | PL-19 (U) → NEW-06; INT-16 (P) → J-BIG; GC-03 (P) → J-BIG; MOUNT-12 (P) → NEW-111; MOUNT-13 (U) → NEW-110; MOUNT-19 (K) → NEW-46; MOUNT-21 (K) → NEW-47; BUILD-12 (U) → NEW-125 |
| `lfs3_fs_usage` | FILE-18, DIR-18, ALLOC-16, ALLOC-17 | `files::*`, `grow::*` | FILE-18 (U) → NEW-97; DIR-18 (U) → NEW-101; ALLOC-16 (U) → NEW-80; ALLOC-17 (P) → NEW-106 |
| `lfs3_fs_cksum` | GEN-02, INT-07, INT-08, INT-09, META-10, DOC-13 | `ck::cksum`, `ck::ckmeta_hard`, `badblocks::mrootanchor_wear` | GEN-02 (U) → NEW-91; INT-08 (U) → NEW-94; INT-09 (P) → NEW-65; META-10 (K) → NEW-18; DOC-13 (N) → review |
| `lfs3_fs_mkconsistent` | GEN-06, META-09, META-14, SYNC-15, SYNC-16, GC-12 | `dirs::*`, `mount::t_mkconsistent`, `stickynotes::cleanup_*`, `trvs::mkconsistent_*`, `stickynotes::*_many`, `gc::mkconsistent_*` and 1 more | GEN-06 (K) → NEW-49; META-14 (U) → NEW-71; SYNC-16 (P) → NEW-100 |
| `lfs3_fs_ck` | GEN-04, PL-19, PL-20, INT-10, INT-11, INT-21, INT-22, FAIL-18, BAD-03, META-03, SYNC-09, DIR-05, PRE-05, PRE-06, GC-05, GC-06, MOUNT-17, CFG-05 | `gbmap::mkgbmap`, `gbmap::rmgbmap`, `gbmap::rmmkgbmap`, `gbmap::mkrmgbmap`, `ck::ckmeta_easy`, `ck::ckmeta_hard` and 8 more | GEN-04 (U) → NEW-92; PL-19 (U) → NEW-06; PL-20 (U) → NEW-05; INT-21 (P) → NEW-65; INT-22 (U) → NEW-64; FAIL-18 (U) → NEW-18; BAD-03 (N) → NEW-90; META-03 (K) → NEW-41; SYNC-09 (U) → NEW-77; DIR-05 (K) → NEW-32; PRE-05 (U) → NEW-11; PRE-06 (K) → NEW-22; GC-06 (U) → NEW-83; MOUNT-17 (K) → NEW-37; CFG-05 (K) → NEW-49 |
| `lfs3_fs_gc` | PL-20, INT-15, BAD-10, PRE-05, PRE-06, GC-01, GC-02, CFG-06, DOC-17 | `ck::ckmeta_*`, `ck::ckdata_*`, `gc::*_progress`, `gc::*_relaxed`, `gc::spam_*` | PL-20 (U) → NEW-05; INT-15 (P) → J-BIG; BAD-10 (N) → NEW-22; PRE-05 (U) → NEW-11; PRE-06 (K) → NEW-22; GC-01 (P) → NEW-107; GC-02 (U) → NEW-82; CFG-06 (U) → NEW-115; DOC-17 (K) → review |
| `lfs3_fs_unck` | INT-16, DOC-06 | `ck::*_hard`, `gc::ckmeta_unck`, `gc::ckdata_unck`, `gc::iflags_unck` | INT-16 (P) → J-BIG; DOC-06 (K) → review |
| `lfs3_fs_grow` | GEN-06, GEN-08, PL-18, BAD-06, MOUNT-20, MOUNT-21, MOUNT-22, MOUNT-23, DOC-16 | `grow::incr_spam_f_pl_fuzz`, `grow::incr_spam_fd_pl_fuzz`, `grow::grow`, `grow::noop`, `grow::incr_spam_*` | GEN-06 (K) → NEW-49; GEN-08 (K) → NEW-47; BAD-06 (N) → NEW-90; MOUNT-21 (K) → NEW-47; MOUNT-22 (U) → NEW-27; DOC-16 (N) → review |
| `lfs3_fs_mkgbmap` | PL-19, ALLOC-13, DOC-07 | `gbmap::mkgbmap`, `gbmap::rmgbmap`, `gbmap::rmmkgbmap`, `gbmap::mkrmgbmap`, `gbmap::mkgbmap_exist` | PL-19 (U) → NEW-06; ALLOC-13 (P) → J-BIG; DOC-07 (K) → review |
| `lfs3_fs_rmgbmap` | PL-19, ALLOC-05, ALLOC-14, DOC-07 | `gbmap::mkgbmap`, `gbmap::rmgbmap`, `gbmap::rmmkgbmap`, `gbmap::mkrmgbmap`, `gbmap::rmgbmap_noent` | PL-19 (U) → NEW-06; ALLOC-05 (U) → NEW-30; ALLOC-14 (P) → J-BIG; DOC-07 (K) → review |
| `lfs3_crc32c` | INT-05 | `ck::crc32c`, `ck::crc32c_incr`, `ck::crc32c_mul`, `ck::crc32c_mul_dist` | INT-05 (P) → NEW-53, J-BUILD |
| `lfs3_crc32c_mul` | INT-05 | `ck::crc32c`, `ck::crc32c_incr`, `ck::crc32c_mul`, `ck::crc32c_mul_dist` | INT-05 (P) → NEW-53, J-BUILD |
| `lfs3_toleb128` | GEN-02, DOC-02 | none | GEN-02 (U) → NEW-91; DOC-02 (N) → NEW-127 |
| `lfs3_fromleb128` | GEN-03, GEN-07, DOC-02 | `mtree::truncated_*` | GEN-03 (K) → NEW-54; GEN-07 (U) → NEW-68; DOC-02 (N) → NEW-127 |

### 3.2 Configuration fields

| Field | Requirements | Existing tests | Gaps → new tests or jobs |
|---|---|---|---|
| `context` | CFG-14 | none | none |
| `read` | GEN-05, FAIL-19, DOC-11 | none | GEN-05 (P) → NEW-93; FAIL-19 (U) → NEW-19; DOC-11 (K) → review |
| `prog` | FAIL-01, CFG-15, GEN-08 | `badblocks::every_*`, `badblocks::region_*`, `badblocks::alternating_*` | CFG-15 (U) → NEW-12; GEN-08 (K) → NEW-47 |
| `erase` | FAIL-02, GEN-08 | `badblocks::every_*`, `badblocks::region_*`, `badblocks::alternating_*` | GEN-08 (K) → NEW-47 |
| `sync` | FAIL-18, PERF-07, MOUNT-17 | none | FAIL-18 (U) → NEW-18; PERF-07 (K) → NEW-120; MOUNT-17 (K) → NEW-37 |
| `lock` | THR-01, THR-02 | none | THR-01 (K) → NEW-89; THR-02 (K) → NEW-89 |
| `unlock` | THR-01, THR-02 | none | THR-01 (K) → NEW-89; THR-02 (K) → NEW-89 |
| `read_size` | CFG-01, CFG-02 | `files::*`, `fwrite::*`, `kv::*` | CFG-01 (K) → NEW-48; CFG-02 (U) → NEW-115 |
| `prog_size` | PL-15, INT-04, META-01, FILE-11, FILE-24, PRE-09, CFG-01, CFG-02, CFG-10, CFG-13, PERF-06, PERF-07, PERF-08, PERF-09 | `rbyd::*_permutations`, `rbyd::fuzz_*`, `rbyd::*`, `files::*`, `fwrite::*`, `kv::*` and 1 more | PL-15 (U) → NEW-04; INT-04 (U) → NEW-66; FILE-11 (U) → NEW-20; FILE-24 (U) → NEW-75; PRE-09 (U) → NEW-60; CFG-01 (K) → NEW-48; CFG-02 (U) → NEW-115; CFG-10 (P) → J-DEFINES; CFG-13 (U) → J-GEO; PERF-06 (P) → NEW-120; PERF-07 (K) → NEW-120; PERF-08 (P) → NEW-121; PERF-09 (K) → NEW-121 |
| `block_size` | META-03, ATTR-05, CFG-01, CFG-02, CFG-03, CFG-05, CFG-10, CFG-11, CFG-12, CFG-13 | `attrs::*`, `files::*`, `fwrite::*`, `kv::*`, `fsync::*` | META-03 (K) → NEW-41; ATTR-05 (K) → NEW-41; CFG-01 (K) → NEW-48; CFG-02 (U) → NEW-115; CFG-03 (U) → NEW-115; CFG-05 (K) → NEW-49; CFG-10 (P) → J-DEFINES; CFG-11 (P) → J-DEFINES; CFG-12 (P) → J-DEFINES; CFG-13 (U) → J-GEO |
| `block_count` | BAD-11, ALLOC-08, ALLOC-11, PRE-01, GC-02, MOUNT-03, CFG-01, CFG-17 | `gc::preerase_progress`, `gc::preerase_relaxed`, `gc::preerase_decreasing`, `gbmap::gc_files`, `gc::spam_*`, `files::*` and 3 more | BAD-11 (N) → NEW-90; ALLOC-08 (U) → NEW-31; ALLOC-11 (K) → NEW-51; PRE-01 (P) → J-BIG; GC-02 (U) → NEW-82; MOUNT-03 (K) → NEW-44; CFG-01 (K) → NEW-48; CFG-17 (P) → J-BIG |
| `block_recycles` | PL-21, FAIL-14, CFG-01, CFG-04 | `relocations::spam_f_pl_fuzz`, `relocations::spam_fd_pl_fuzz`, `relocations::*`, `files::*`, `fwrite::*`, `kv::*` | PL-21 (P) → J-PL; FAIL-14 (U) → NEW-61; CFG-01 (K) → NEW-48; CFG-04 (U) → NEW-115 |
| `rcache_size` | CFG-01, CFG-02 | `files::*`, `fwrite::*`, `kv::*` | CFG-01 (K) → NEW-48; CFG-02 (U) → NEW-115 |
| `pcache_size` | PRE-09, CFG-01, CFG-02 | `files::*`, `fwrite::*`, `kv::*` | PRE-09 (U) → NEW-60; CFG-01 (K) → NEW-48; CFG-02 (U) → NEW-115 |
| `fcache_size` | CFG-01, CFG-07, CFG-08, RES-08 | `files::*`, `fwrite::*`, `kv::*`, `fwrite::fuzz_unaligned` | CFG-01 (K) → NEW-48; CFG-07 (U) → NEW-86; CFG-08 (P) → NEW-116; RES-08 (U) → NEW-87 |
| `lookahead_size` | ALLOC-06, CFG-01 | `alloc::*`, `files::*`, `dirs::*`, `fwrite::*`, `kv::*` | ALLOC-06 (P) → J-DEFINES; CFG-01 (K) → NEW-48 |
| `gc_flags` | INT-15, GC-03, CFG-06 | `ck::ckmeta_*`, `ck::ckdata_*`, `gc::iflags`, `gc::iflags_unck`, `gc::lookahead_*`, `gc::compact_*` and 3 more | INT-15 (P) → J-BIG; GC-03 (P) → J-BIG; CFG-06 (U) → NEW-115 |
| `gc_steps` | GC-01, GC-02 | `gc::*_progress`, `gc::*_relaxed`, `gc::spam_*` | GC-01 (P) → NEW-107; GC-02 (U) → NEW-82 |
| `gc_lookahead_thresh` | CFG-16 | `gc::lookahead_progress`, `gc::lookahead_relaxed`, `gc::lookahead_mutation` | CFG-16 (P) → J-BIG |
| `gc_lookgbmap_thresh` | CFG-17, DOC-09 | `gc::lookgbmap_*` | CFG-17 (P) → J-BIG; DOC-09 (K) → review |
| `gc_preerase_count` | PRE-01, DOC-04 | `gc::preerase_progress`, `gc::preerase_relaxed`, `gc::preerase_decreasing`, `gbmap::gc_files` | PRE-01 (P) → J-BIG; DOC-04 (K) → review |
| `gc_compact_thresh` | GC-14, CFG-05, DOC-10 | `gc::compact_*`, `trvs::compact_*` | GC-14 (P) → NEW-109; CFG-05 (K) → NEW-49; DOC-10 (K) → review |
| `rcache_buffer` | RES-01 | none | RES-01 (U) → NEW-87 |
| `pcache_buffer` | RES-01 | none | RES-01 (U) → NEW-87 |
| `lookahead_buffer` | RES-01 | none | RES-01 (U) → NEW-87 |
| `name_limit` | META-03, FILE-15, DIR-01, DIR-02, DIR-15 | `files::noent`, `files::dir_not_file`, `files::file_not_dir`, `files::root_not_file`, `files::noent_not_file`, `paths::*` and 7 more | META-03 (K) → NEW-41; DIR-02 (K) → NEW-41 |
| `file_limit` | FILE-03, FILE-06, FILE-26, KV-04 | `fwrite::r_seek`, `fwrite::w_seek`, `fwrite::rw_seek`, `fwrite::seek_negative`, `fwrite::fbig`, `fwrite::truncate_fbig` and 1 more | FILE-03 (K) → NEW-34; FILE-26 (U) → NEW-76; KV-04 (K) → NEW-36 |
| `shrub_size` | KV-09, CFG-01, CFG-11 | `files::*`, `fwrite::*`, `kv::*`, `fsync::*` | KV-09 (U) → NEW-105; CFG-01 (K) → NEW-48; CFG-11 (P) → J-DEFINES |
| `fragment_size` | KV-09, CFG-01, CFG-12 | `files::*`, `fwrite::*`, `kv::*` | KV-09 (U) → NEW-105; CFG-01 (K) → NEW-48; CFG-12 (P) → J-DEFINES |
| `crystal_thresh` | KV-09, CFG-01, CFG-10, PERF-11 | `files::*`, `fwrite::*`, `kv::*` | KV-09 (U) → NEW-105; CFG-01 (K) → NEW-48; CFG-10 (P) → J-DEFINES; PERF-11 (P) → NEW-120 |
| `lookgbmap_thresh` | ALLOC-11 | none | ALLOC-11 (K) → NEW-51 |
| `lfs3_file_cfg.fcache_buffer` | CFG-08, RES-01 | `kv::*`, `fwrite::fuzz_unaligned` | CFG-08 (P) → NEW-116; RES-01 (U) → NEW-87 |
| `lfs3_file_cfg.fcache_size` | CFG-08 | `kv::*`, `fwrite::fuzz_unaligned` | CFG-08 (P) → NEW-116 |
| `lfs3_file_cfg.attrs`, `attr_count` | ATTR-08, ATTR-09, ATTR-10, ATTR-12, PL-11 | `attrs::fattr_*`, `attrs::fattr_lazy`, `attrs::fattr_pl_fuzz_fuzz` | ATTR-10 (P) → NEW-103; ATTR-12 (K) → NEW-39; PL-11 (P) → NEW-59 |
| `lfs3_attr.flags` (`LFS3_A_*`) | ATTR-09, ATTR-12 | `attrs::fattr_*`, `attrs::fattr_lazy` | ATTR-12 (K) → NEW-39 |

### 3.3 Compile-time options

| Option | Requirements | Existing tests | Gaps → new tests or jobs |
|---|---|---|---|
| `LFS3_RDONLY` | BAD-04, MOUNT-14, MOUNT-19, BUILD-03, BUILD-04, BUILD-19 | `mount::simple`, `files::*`, `dirs::*`, `attrs::*` | BAD-04 (N) → NEW-90; MOUNT-14 (K) → NEW-46; MOUNT-19 (K) → NEW-46; BUILD-03 (K) → NEW-53; BUILD-04 (K) → NEW-53; BUILD-19 (U) → NEW-46 |
| `LFS3_YES_RDONLY` | BUILD-12, BUILD-15, BUILD-19 | `files::*`, `dirs::*`, `attrs::*` | BUILD-12 (U) → NEW-125; BUILD-15 (U) → J-YES; BUILD-19 (U) → NEW-46 |
| `LFS3_GBMAP` | ALLOC-09, ALLOC-10, ALLOC-11, ALLOC-12, ALLOC-13, ALLOC-14, ALLOC-15, ALLOC-16, BAD-15, BUILD-04, BUILD-14 | `gbmap::files`, `gbmap::gc_files`, `alloc::*`, `gbmap::set_*`, `gbmap::set_ecksum_*`, `powerloss::*` and 7 more | ALLOC-09 (P) → J-YGB; ALLOC-10 (P) → J-YGB; ALLOC-11 (K) → NEW-51; ALLOC-12 (P) → NEW-06; ALLOC-13 (P) → J-BIG; ALLOC-14 (P) → J-BIG; ALLOC-15 (U) → NEW-79; ALLOC-16 (U) → NEW-80; BAD-15 (N) → NEW-90; BUILD-04 (K) → NEW-53; BUILD-14 (U) → J-YGB |
| `LFS3_YES_GBMAP` | PL-18, FAIL-20, ALLOC-13, ALLOC-14, BUILD-14 | `grow::incr_spam_f_pl_fuzz`, `grow::incr_spam_fd_pl_fuzz`, `badblocks::*`, `exhaustion::*`, `gbmap::mkgbmap`, `gbmap::rmmkgbmap` and 4 more | FAIL-20 (U) → J-YGB; ALLOC-13 (P) → J-BIG; ALLOC-14 (P) → J-BIG; BUILD-14 (U) → J-YGB |
| `LFS3_PREERASE` | PRE-01, PRE-02, PRE-03, PRE-04, PRE-05, PRE-06, PRE-07, PRE-08, PRE-09, PERF-10, BUILD-07 | `gc::preerase_progress`, `gc::preerase_relaxed`, `gc::preerase_decreasing`, `gbmap::gc_files`, `gc::preerase_*`, `mount::t_preerase` and 1 more | PRE-01 (P) → J-BIG; PRE-02 (P) → NEW-81; PRE-03 (U) → NEW-23; PRE-04 (U) → NEW-24; PRE-05 (U) → NEW-11; PRE-06 (K) → NEW-22; PRE-07 (U) → NEW-11; PRE-08 (U) → NEW-25; PRE-09 (U) → NEW-60; PERF-10 (P) → NEW-121; BUILD-07 (U) → NEW-53 |
| `LFS3_REVPERTURB` | PRE-04, BUILD-07, DOC-04, DOC-05 | `mount::t_preerase` | PRE-04 (U) → NEW-24; BUILD-07 (U) → NEW-53; DOC-04 (K) → review; DOC-05 (K) → review |
| `LFS3_YES_REVPERTURB` | BUILD-12, BUILD-15 | none | BUILD-12 (U) → NEW-125; BUILD-15 (U) → J-YES |
| `LFS3_REVNOISE` | BUILD-13, BUILD-15 | none | BUILD-13 (K) → J-BIG; BUILD-15 (U) → J-YES |
| `LFS3_YES_REVNOISE` | BUILD-12, BUILD-15 | none | BUILD-12 (U) → NEW-125; BUILD-15 (U) → J-YES |
| `LFS3_CKPROGS` | INT-17, FAIL-03, FAIL-05, FAIL-06, FAIL-08, FAIL-11 | `ck::ckprogs_mroot`, `ck::ckprogs_data`, `ck::ckprogs_btree`, `ck::ckprogs_overrecycling`, `ck::spam_*_fuzz`, `badblocks::*` and 7 more | INT-17 (P) → J-BIG; FAIL-03 (P) → J-BIG; FAIL-05 (P) → J-BIG; FAIL-06 (P) → J-BIG; FAIL-08 (P) → J-BIG; FAIL-11 (P) → J-BIG |
| `LFS3_YES_CKPROGS` | BUILD-12, BUILD-15 | none | BUILD-12 (U) → NEW-125; BUILD-15 (U) → J-YES |
| `LFS3_CKFETCHES` | INT-18, BUILD-04 | `ck::ckfetches_mroot`, `ck::ckfetches_data`, `ck::ckfetches_btree`, `ck::spam_*_fuzz` | INT-18 (P) → J-BIG; BUILD-04 (K) → NEW-53 |
| `LFS3_YES_CKFETCHES` | BUILD-12, BUILD-15 | none | BUILD-12 (U) → NEW-125; BUILD-15 (U) → J-YES |
| `LFS3_CKMETAPARITY` | INT-19, INT-23, FAIL-09, BUILD-04 | `ck::ckparity_mroot`, `ck::ckparity_btree`, `ck::spam_*_fuzz`, `ck::ckparity_btree_append`, `mount::flags`, `mount::format_flags` and 2 more | INT-19 (P) → NEW-62; INT-23 (K) → NEW-40, J-BIG; FAIL-09 (P) → NEW-62, J-BIG; BUILD-04 (K) → NEW-53 |
| `LFS3_YES_CKMETAPARITY` | BUILD-12, BUILD-15 | none | BUILD-12 (U) → NEW-125; BUILD-15 (U) → J-YES |
| `LFS3_CKDATACKSUMS` | INT-20, FAIL-09, BUILD-03, BUILD-04 | `ck::ckdatacksums_data`, `ck::spam_*_fuzz`, `ck::ckparity_*` | INT-20 (K) → J-BIG; FAIL-09 (P) → NEW-62, J-BIG; BUILD-03 (K) → NEW-53; BUILD-04 (K) → NEW-53 |
| `LFS3_YES_CKDATACKSUMS` | BUILD-12, BUILD-15 | none | BUILD-12 (U) → NEW-125; BUILD-15 (U) → J-YES |
| `LFS3_GC` | GC-01, GC-02, GC-03, GC-04, INT-15, INT-16, CFG-05, CFG-06, BUILD-04 | `gc::*_progress`, `gc::*_relaxed`, `gc::spam_*`, `gc::iflags`, `gc::iflags_unck`, `gc::lookahead_*` and 11 more | GC-01 (P) → NEW-107; GC-02 (U) → NEW-82; GC-03 (P) → J-BIG; GC-04 (P) → J-BIG; INT-15 (P) → J-BIG; INT-16 (P) → J-BIG; CFG-05 (K) → NEW-49; CFG-06 (U) → NEW-115; BUILD-04 (K) → NEW-53 |
| `LFS3_YES_GC` | BUILD-12, BUILD-15 | none | BUILD-12 (U) → NEW-125; BUILD-15 (U) → J-YES |
| `LFS3_BLEAFCACHE` | BUILD-04, BUILD-13, BUILD-15 | none | BUILD-04 (K) → NEW-53; BUILD-13 (K) → J-BIG; BUILD-15 (U) → J-YES |
| `LFS3_YES_BLEAFCACHE` | BUILD-12, BUILD-15 | none | BUILD-12 (U) → NEW-125; BUILD-15 (U) → J-YES |
| `LFS3_BIGGEST` | BUILD-02, BUILD-13 | none | BUILD-02 (K) → NEW-53; BUILD-13 (K) → J-BIG |
| `LFS3_YES_FLUSH` | BUILD-12, BUILD-15, DOC-19 | none | BUILD-12 (U) → NEW-125; BUILD-15 (U) → J-YES; DOC-19 (N) → review |
| `LFS3_YES_SYNC` | BUILD-12, BUILD-15, DOC-19 | none | BUILD-12 (U) → NEW-125; BUILD-15 (U) → J-YES; DOC-19 (N) → review |
| `LFS3_THREADSAFE` | THR-01, THR-02, THR-03 | none | THR-01 (K) → NEW-89; THR-02 (K) → NEW-89; THR-03 (U) → NEW-124 |
| `LFS3_NO_MALLOC` | RES-01, BAD-07, BUILD-04, BUILD-10 | `badblocks_gbmap::*` | RES-01 (U) → NEW-87; BAD-07 (N) → NEW-90; BUILD-04 (K) → NEW-53; BUILD-10 (K) → NEW-53 |
| `LFS3_NO_STRINGH` | BUILD-09, BUILD-17 | none | BUILD-09 (K) → NEW-53; BUILD-17 (U) → J-NBNS |
| `LFS3_NO_BUILTINS` | BUILD-01, BUILD-17 | none | BUILD-01 (P) → NEW-53; BUILD-17 (U) → J-NBNS |
| `LFS3_NO_ASSERT` | GEN-06, ATTR-05, RES-04, BUILD-16, DOC-16 | `attrs::*` | GEN-06 (K) → NEW-49; ATTR-05 (K) → NEW-41; RES-04 (U) → NEW-118; BUILD-16 (U) → J-NA; DOC-16 (N) → review |
| `LFS3_NO_DEBUG` | BUILD-20 | none | BUILD-20 (U) → NEW-53 |
| `LFS3_NO_INFO` | BUILD-20 | none | BUILD-20 (U) → NEW-53 |
| `LFS3_NO_WARN` | BUILD-20 | none | BUILD-20 (U) → NEW-53 |
| `LFS3_NO_ERROR` | BUILD-20 | none | BUILD-20 (U) → NEW-53 |
| `LFS3_NO_LOG` | RES-04, BUILD-20 | none | RES-04 (U) → NEW-118; BUILD-20 (U) → NEW-53 |
| `LFS3_YES_TRACE` | BUILD-20 | none | BUILD-20 (U) → NEW-53 |
| `LFS3_TRACE` | BUILD-20 | none | BUILD-20 (U) → NEW-53 |
| `LFS3_DEBUG` | BUILD-20 | none | BUILD-20 (U) → NEW-53 |
| `LFS3_INFO` | BUILD-20 | none | BUILD-20 (U) → NEW-53 |
| `LFS3_WARN` | BUILD-20 | none | BUILD-20 (U) → NEW-53 |
| `LFS3_ERROR` | BUILD-20 | none | BUILD-20 (U) → NEW-53 |
| `LFS3_ASSERT` | BUILD-20, DOC-16 | none | BUILD-20 (U) → NEW-53; DOC-16 (N) → review |
| `LFS3_UNREACHABLE` | BUILD-20 | none | BUILD-20 (U) → NEW-53 |
| `LFS3_CFG` | BUILD-08 | none | BUILD-08 (K) → NEW-53 |
| `LFS3_SMALLER_CRC32C` | INT-05 | `ck::crc32c`, `ck::crc32c_incr`, `ck::crc32c_mul`, `ck::crc32c_mul_dist` | INT-05 (P) → NEW-53, J-BUILD |
| `LFS3_FASTER_CRC32C` | INT-05 | `ck::crc32c`, `ck::crc32c_incr`, `ck::crc32c_mul`, `ck::crc32c_mul_dist` | INT-05 (P) → NEW-53, J-BUILD |
| `LFS3_PMUL_CRC32C` | INT-05, BUILD-05 | `ck::crc32c`, `ck::crc32c_incr`, `ck::crc32c_mul`, `ck::crc32c_mul_dist`, `ck::crc32c*` | INT-05 (P) → NEW-53, J-BUILD; BUILD-05 (K) → NEW-53 |
| `LFS3_NAME_MAX` | CFG-09, BUILD-18 | `paths::*`, `fwrite::*fbig` | CFG-09 (U) → NEW-117; BUILD-18 (U) → NEW-126 |
| `LFS3_FILE_MAX` | FILE-17, FILE-26, BUILD-18 | `fwrite::fbig`, `fwrite::truncate_fbig`, `fwrite::fruncate_fbig`, `paths::*`, `fwrite::*fbig` | FILE-17 (U) → NEW-73; FILE-26 (U) → NEW-76; BUILD-18 (U) → NEW-126 |
| `LFS3_DBGRBYDFETCHES` | BUILD-06 | none | BUILD-06 (K) → NEW-53 |
| `LFS3_DBGRBYDCOMMITS` | BUILD-06 | none | BUILD-06 (K) → NEW-53 |
| `LFS3_DBGRBYDBALANCE` | META-02, BUILD-06 | `rbyd::*`, `btree::*`, `mtree::*` | META-02 (U) → NEW-69; BUILD-06 (K) → NEW-53 |
| `LFS3_DBGBTREEFETCHES` | BUILD-06 | none | BUILD-06 (K) → NEW-53 |
| `LFS3_DBGBTREECOMMITS` | BUILD-06 | none | BUILD-06 (K) → NEW-53 |
| `LFS3_DBGMDIRFETCHES` | BUILD-06 | none | BUILD-06 (K) → NEW-53 |
| `LFS3_DBGMDIRCOMMITS` | BUILD-06 | none | BUILD-06 (K) → NEW-53 |
| `LFS3_DBGALLOCS` | BUILD-06 | none | BUILD-06 (K) → NEW-53 |

### 3.4 Features

| Feature | Requirements | Existing tests | Gaps → new tests or jobs |
|---|---|---|---|
| rbyd metadata logs | META-01, META-02, META-17, INT-01, INT-02, INT-03, INT-04 | `rbyd::*_permutations`, `rbyd::fuzz_*`, `rbyd::*`, `btree::*`, `mtree::*`, `mtree::*_fuzz` and 8 more | META-02 (U) → NEW-69; META-17 (U) → NEW-95; INT-03 (P) → NEW-66; INT-04 (U) → NEW-66 |
| metadata pairs: compaction, split, drop, relocation | META-05, META-10, META-12, META-15, META-16, FAIL-14 | `mtree::split*`, `mtree::drop*`, `mtree::relocate*`, `mtree::uninline*`, `mtree::*_fuzz`, `badblocks::mrootanchor_wear` and 11 more | META-10 (K) → NEW-18; META-12 (P) → NEW-70; META-15 (P) → NEW-07; META-16 (P) → J-GEO; FAIL-14 (U) → NEW-61 |
| mtree and mroot chain | META-06, META-07, META-08, META-13 | `mtree::extend`, `mtree::extend_twice`, `mtree::relocate_mroot`, `mtree::relocate_extend`, `trvs::mutation_*_extend*`, `mtree::magic` and 6 more | none |
| global state: grm and gcksum | META-09, META-11, INT-06, INT-07, INT-08, INT-09, INT-21 | `dirs::*`, `mount::t_mkconsistent`, `ck::ckmeta_hard`, `ck::cksum` | META-11 (K) → NEW-43; INT-06 (P) → NEW-65; INT-08 (U) → NEW-94; INT-09 (P) → NEW-65; INT-21 (P) → NEW-65 |
| B-trees, B-shrubs and inline files | FILE-22, FILE-23, FILE-25, PERF-03 | `files::zero_bnull`, `files::zero_bshrub`, `files::zero_btree`, `files::more`, `files::mv_split`, `fwrite::*_litmus_*` | PERF-03 (U) → NEW-119 |
| crystallization and erased-state reuse | FILE-11, FILE-24, PL-15, PERF-11 | none | FILE-11 (U) → NEW-20; FILE-24 (U) → NEW-75; PL-15 (U) → NEW-04; PERF-11 (P) → NEW-120 |
| random writes, holes and sparse files | FILE-01, FILE-05, FILE-07, FILE-18, PL-14 | `fwrite::simple`, `fwrite::incr`, `fwrite::reversed`, `fwrite::overwrite`, `fwrite::fuzz_aligned`, `fwrite::fuzz_unaligned` and 4 more | FILE-18 (U) → NEW-97; PL-14 (U) → NEW-03 |
| truncate and fruncate | FILE-08, FILE-09, FILE-10, PL-13 | `fwrite::truncate`, `fwrite::truncate_truncate`, `fwrite::truncate_pos`, `fwrite::truncate_litmus_zero`, `fwrite::truncate_litmus_fragment`, `fwrite::fruncate*` and 2 more | FILE-10 (K) → NEW-21; PL-13 (U) → NEW-02 |
| sync model | SYNC-01, SYNC-02, SYNC-03, SYNC-04, SYNC-05, SYNC-06, SYNC-07, SYNC-08, SYNC-09, SYNC-10, SYNC-11, SYNC-12, SYNC-19, PL-16, PL-17 | `fsync::wrrr`, `fsync::wwww`, `fsync::wwrr`, `fsync::rwrw`, `fsync::*_fuzz`, `fsync::sync_*` and 16 more | SYNC-05 (K) → NEW-38; SYNC-09 (U) → NEW-77; SYNC-10 (P) → NEW-99; PL-16 (U) → NEW-56; PL-17 (U) → NEW-57 |
| stickynotes, orphans and zombies | SYNC-13, SYNC-14, SYNC-15, SYNC-16, SYNC-17, SYNC-18, PL-07 | `stickynotes::uncreat_*`, `stickynotes::orphan_*`, `stickynotes::cleanup_*`, `mount::t_mkconsistent`, `trvs::mkconsistent_*`, `stickynotes::*_many` and 9 more | SYNC-16 (P) → NEW-100 |
| directories and paths | DIR-01, DIR-02, DIR-03, DIR-04, DIR-05, DIR-06, DIR-07, DIR-08, DIR-09, DIR-10, DIR-11, DIR-12, DIR-13, DIR-14, DIR-15, DIR-16, DIR-17, DIR-18, DIR-19, DIR-20 | `dirs::mkdir_*`, `paths::*`, `paths::namejustlongenough`, `dirs::rm_*`, `dirs::rm_root`, `paths::root` and 22 more | DIR-02 (K) → NEW-41; DIR-05 (K) → NEW-32; DIR-11 (K) → NEW-33; DIR-18 (U) → NEW-101 |
| custom attributes | ATTR-01, ATTR-02, ATTR-03, ATTR-04, ATTR-05, ATTR-06, ATTR-07, ATTR-08, ATTR-09, ATTR-10, ATTR-11, ATTR-12, ATTR-13, PL-11 | `attrs::setattr*`, `attrs::getattr*`, `attrs::setattr_trunc`, `attrs::fuzz`, `attrs::*noattr*`, `attrs::removeattr` and 10 more | ATTR-05 (K) → NEW-41; ATTR-06 (U) → NEW-78; ATTR-07 (U) → NEW-102; ATTR-10 (P) → NEW-103; ATTR-12 (K) → NEW-39; PL-11 (P) → NEW-59 |
| key-value API | KV-01, KV-02, KV-03, KV-04, KV-05, KV-06, KV-07, KV-08, KV-09, PL-12 | `kv::set`, `kv::set_update`, `kv::many`, `kv::fuzz`, `kv::*`, `kv::set_trunc` and 7 more | KV-04 (K) → NEW-36; KV-05 (P) → NEW-104; KV-08 (U) → NEW-104; KV-09 (U) → NEW-105; PL-12 (U) → NEW-55 |
| compat flags and versioning | MOUNT-04, MOUNT-05, MOUNT-06, MOUNT-07, MOUNT-08, MOUNT-09, MOUNT-10, MOUNT-11, MOUNT-12, MOUNT-13, MOUNT-24, MOUNT-25, MOUNT-26 | `mount::incompat_no_magic`, `mount::incompat_bad_magic`, `mount::incompat_major`, `mount::incompat_minor`, `mount::incompat_rcompat`, `mount::incompat_wronly` and 16 more | MOUNT-09 (K) → NEW-45; MOUNT-11 (U) → NEW-110; MOUNT-12 (P) → NEW-111; MOUNT-13 (U) → NEW-110; MOUNT-24 (N) → NEW-113; MOUNT-25 (U) → NEW-85; MOUNT-26 (U) → J-COMPAT |
| checksums and the check APIs | INT-10, INT-11, INT-12, INT-13, INT-14, INT-15, INT-16, INT-17, INT-18, INT-19, INT-20, INT-22, INT-23 | `ck::ckmeta_easy`, `ck::ckmeta_hard`, `ck::ckdata_easy`, `ck::ckdata_hard`, `mount::t_ckmeta`, `mount::t_ckdata` and 27 more | INT-13 (P) → NEW-67; INT-15 (P) → J-BIG; INT-16 (P) → J-BIG; INT-17 (P) → J-BIG; INT-18 (P) → J-BIG; INT-19 (P) → NEW-62; INT-20 (K) → J-BIG; INT-22 (U) → NEW-64; INT-23 (K) → NEW-40, J-BIG |
| traversal API | GC-07, GC-08, GC-09, GC-10, GC-11, GC-15, GC-16 | `trvs::simple`, `trvs::idempotent`, `trvs::spam_*`, `trvs::mutation_*`, `trvs::clobber_files_opened`, `trvs::compact_*` and 6 more | GC-11 (P) → NEW-108 |
| incremental gc | GC-01, GC-02, GC-03, GC-04, GC-05, GC-06, GC-12, GC-13, GC-14 | `gc::*_progress`, `gc::*_relaxed`, `gc::spam_*`, `gc::iflags`, `gc::iflags_unck`, `gc::lookahead_*` and 23 more | GC-01 (P) → NEW-107; GC-02 (U) → NEW-82; GC-03 (P) → J-BIG; GC-04 (P) → J-BIG; GC-06 (U) → NEW-83; GC-13 (P) → J-BIG; GC-14 (P) → NEW-109 |
| runtime error recovery | SYNC-05, SYNC-09, META-10, FAIL-18, FAIL-19, FILE-21 | `badblocks::mrootanchor_wear`, `alloc::nospc_files` | SYNC-05 (K) → NEW-38; SYNC-09 (U) → NEW-77; META-10 (K) → NEW-18; FAIL-18 (U) → NEW-18; FAIL-19 (U) → NEW-19; FILE-21 (P) → NEW-74 |
| block allocation (lookahead) | ALLOC-01, ALLOC-02, ALLOC-03, ALLOC-04, ALLOC-06, ALLOC-07, ALLOC-08, ALLOC-17 | `alloc::clobber_dirs`, `alloc::clobber_files`, `alloc::clobber_open_files`, `trvs::clobber_*`, `trvs::rewind_clobber_*`, `alloc::alloc` and 8 more | ALLOC-04 (U) → NEW-29; ALLOC-06 (P) → J-DEFINES; ALLOC-08 (U) → NEW-31; ALLOC-17 (P) → NEW-106 |
| global block map (gbmap) | ALLOC-05, ALLOC-09, ALLOC-10, ALLOC-11, ALLOC-12, ALLOC-13, ALLOC-14, ALLOC-15, ALLOC-16, PL-19, INT-22 | `gbmap::files`, `gbmap::gc_files`, `alloc::*`, `gbmap::set_*`, `gbmap::set_ecksum_*`, `powerloss::*` and 8 more | ALLOC-05 (U) → NEW-30; ALLOC-09 (P) → J-YGB; ALLOC-10 (P) → J-YGB; ALLOC-11 (K) → NEW-51; ALLOC-12 (P) → NEW-06; ALLOC-13 (P) → J-BIG; ALLOC-14 (P) → J-BIG; ALLOC-15 (U) → NEW-79; ALLOC-16 (U) → NEW-80; PL-19 (U) → NEW-06; INT-22 (U) → NEW-64 |
| pre-erase | PRE-01, PRE-02, PRE-03, PRE-04, PRE-05, PRE-06, PRE-07, PRE-08, PRE-09 | `gc::preerase_progress`, `gc::preerase_relaxed`, `gc::preerase_decreasing`, `gbmap::gc_files`, `gc::preerase_*`, `mount::t_preerase` and 1 more | PRE-01 (P) → J-BIG; PRE-02 (P) → NEW-81; PRE-03 (U) → NEW-23; PRE-04 (U) → NEW-24; PRE-05 (U) → NEW-11; PRE-06 (K) → NEW-22; PRE-07 (U) → NEW-11; PRE-08 (U) → NEW-25; PRE-09 (U) → NEW-60 |
| power-loss resilience | PL-01, PL-02, PL-03, PL-04, PL-05, PL-06, PL-07, PL-08, PL-09, PL-10, PL-11, PL-12, PL-13, PL-14, PL-15, PL-16, PL-17, PL-18, PL-19, PL-20, PL-21, PL-22, PL-23, PL-24, PL-25, PL-26, PL-27 | `powerloss::*`, `dirs::*`, `files::pl_fuzz`, `relocations::spam_*_pl_fuzz`, `grow::incr_spam_*_pl_fuzz`, `stickynotes::*_pl` and 31 more | PL-02 (P) → J-PL; PL-03 (U) → NEW-08; PL-06 (P) → NEW-58; PL-11 (P) → NEW-59; PL-12 (U) → NEW-55; PL-13 (U) → NEW-02; PL-14 (U) → NEW-03; PL-15 (U) → NEW-04; PL-16 (U) → NEW-56; PL-17 (U) → NEW-57; PL-19 (U) → NEW-06; PL-20 (U) → NEW-05; PL-21 (P) → J-PL; PL-22 (U) → NEW-01; PL-23 (U) → J-PL; PL-24 (U) → J-PL; PL-25 (U) → NEW-09; PL-26 (U) → NEW-10; PL-27 (K) → NEW-52 |
| bad-block handling | FAIL-01, FAIL-02, FAIL-03, FAIL-04, FAIL-05, FAIL-06, FAIL-07, FAIL-08, FAIL-09, FAIL-10, FAIL-11, FAIL-12, FAIL-13, FAIL-14, FAIL-15, FAIL-16, FAIL-17, FAIL-18, FAIL-19, FAIL-20 | `badblocks::every_*`, `badblocks::region_*`, `badblocks::alternating_*`, `badblocks::*`, `exhaustion::*`, `badblocks::region_spam_file_fuzz` and 18 more | FAIL-03 (P) → J-BIG; FAIL-04 (U) → NEW-13; FAIL-05 (P) → J-BIG; FAIL-06 (P) → J-BIG; FAIL-07 (U) → NEW-14; FAIL-08 (P) → J-BIG; FAIL-09 (P) → NEW-62, J-BIG; FAIL-10 (P) → J-BIG; FAIL-11 (P) → J-BIG; FAIL-12 (U) → NEW-15; FAIL-14 (U) → NEW-61; FAIL-16 (P) → NEW-16; FAIL-17 (U) → NEW-17; FAIL-18 (U) → NEW-18; FAIL-19 (U) → NEW-19; FAIL-20 (U) → J-YGB |
| bad-block tracking (planned) | BAD-01, BAD-02, BAD-03, BAD-04, BAD-05, BAD-06, BAD-07, BAD-08, BAD-09, BAD-10, BAD-11, BAD-12, BAD-13, BAD-14, BAD-15 | `badblocks_gbmap::*`, `exhaustion::*` | BAD-01 (N) → NEW-90; BAD-02 (N) → NEW-90; BAD-03 (N) → NEW-90; BAD-04 (N) → NEW-90; BAD-05 (N) → NEW-79; BAD-06 (N) → NEW-90; BAD-07 (N) → NEW-90; BAD-08 (N) → NEW-90; BAD-09 (N) → NEW-90; BAD-10 (N) → NEW-22; BAD-11 (N) → NEW-90; BAD-12 (N) → NEW-90; BAD-13 (N) → NEW-90; BAD-14 (N) → NEW-90; BAD-15 (N) → NEW-90 |
| wear levelling | FAIL-13, FAIL-14, PL-21 | `exhaustion::*`, `relocations::*`, `relocations::spam_f_pl_fuzz`, `relocations::spam_fd_pl_fuzz` | FAIL-14 (U) → NEW-61; PL-21 (P) → J-PL |
| format, mount and grow | MOUNT-01, MOUNT-02, MOUNT-03, MOUNT-14, MOUNT-15, MOUNT-16, MOUNT-17, MOUNT-18, MOUNT-19, MOUNT-20, MOUNT-21, MOUNT-22, MOUNT-23, MOUNT-27 | `mount::simple`, `mtree::magic`, `mount::t_ckmeta`, `ck::ckmeta_*`, `mount::incompat_*`, `grow::grow` and 3 more | MOUNT-01 (P) → J-GEO; MOUNT-03 (K) → NEW-44; MOUNT-14 (K) → NEW-46; MOUNT-16 (U) → NEW-84; MOUNT-17 (K) → NEW-37; MOUNT-18 (P) → NEW-112; MOUNT-19 (K) → NEW-46; MOUNT-21 (K) → NEW-47; MOUNT-22 (U) → NEW-27; MOUNT-27 (P) → NEW-114 |
| configuration validation | CFG-01, CFG-02, CFG-03, CFG-04, CFG-05, CFG-06, CFG-07, CFG-08, CFG-09, CFG-10, CFG-11, CFG-12, CFG-13, CFG-14, CFG-15, CFG-16, CFG-17 | `files::*`, `fwrite::*`, `kv::*`, `fwrite::fuzz_unaligned`, `fsync::*`, `gc::lookahead_progress` and 3 more | CFG-01 (K) → NEW-48; CFG-02 (U) → NEW-115; CFG-03 (U) → NEW-115; CFG-04 (U) → NEW-115; CFG-05 (K) → NEW-49; CFG-06 (U) → NEW-115; CFG-07 (U) → NEW-86; CFG-08 (P) → NEW-116; CFG-09 (U) → NEW-117; CFG-10 (P) → J-DEFINES; CFG-11 (P) → J-DEFINES; CFG-12 (P) → J-DEFINES; CFG-13 (U) → J-GEO; CFG-15 (U) → NEW-12; CFG-16 (P) → J-BIG; CFG-17 (P) → J-BIG |
| resource bounds | RES-01, RES-02, RES-03, RES-04, RES-05, RES-06, RES-07, RES-08 | none | RES-01 (U) → NEW-87; RES-02 (U) → NEW-118; RES-03 (U) → NEW-118; RES-04 (U) → NEW-118; RES-05 (U) → NEW-118; RES-06 (K) → NEW-50; RES-07 (U) → NEW-88; RES-08 (U) → NEW-87 |
| performance | PERF-01, PERF-02, PERF-03, PERF-04, PERF-05, PERF-06, PERF-07, PERF-08, PERF-09, PERF-10, PERF-11, PERF-12, PERF-13 | none | PERF-01 (U) → NEW-119; PERF-02 (U) → NEW-119; PERF-03 (U) → NEW-119; PERF-04 (U) → NEW-119; PERF-05 (U) → NEW-119; PERF-06 (P) → NEW-120; PERF-07 (K) → NEW-120; PERF-08 (P) → NEW-121; PERF-09 (K) → NEW-121; PERF-10 (P) → NEW-121; PERF-11 (P) → NEW-120; PERF-12 (U) → NEW-122; PERF-13 (U) → NEW-123 |
| thread safety | THR-01, THR-02, THR-03 | none | THR-01 (K) → NEW-89; THR-02 (K) → NEW-89; THR-03 (U) → NEW-124 |
| portability | GEN-01, GEN-02, GEN-03, GEN-04, GEN-05, GEN-06, GEN-07, GEN-08 | `mtree::truncated_*` | GEN-01 (U) → J-ARCH; GEN-02 (U) → NEW-91; GEN-03 (K) → NEW-54; GEN-04 (U) → NEW-92; GEN-05 (P) → NEW-93; GEN-06 (K) → NEW-49; GEN-07 (U) → NEW-68; GEN-08 (K) → NEW-47 |
| build configurations | BUILD-01, BUILD-02, BUILD-03, BUILD-04, BUILD-05, BUILD-06, BUILD-07, BUILD-08, BUILD-09, BUILD-10, BUILD-11, BUILD-12, BUILD-13, BUILD-14, BUILD-15, BUILD-16, BUILD-17, BUILD-18, BUILD-19, BUILD-20 | `ck::crc32c*`, `paths::*`, `fwrite::*fbig`, `files::*`, `dirs::*`, `attrs::*` | BUILD-01 (P) → NEW-53; BUILD-02 (K) → NEW-53; BUILD-03 (K) → NEW-53; BUILD-04 (K) → NEW-53; BUILD-05 (K) → NEW-53; BUILD-06 (K) → NEW-53; BUILD-07 (U) → NEW-53; BUILD-08 (K) → NEW-53; BUILD-09 (K) → NEW-53; BUILD-10 (K) → NEW-53; BUILD-11 (K) → NEW-53; BUILD-12 (U) → NEW-125; BUILD-13 (K) → J-BIG; BUILD-14 (U) → J-YGB; BUILD-15 (U) → J-YES; BUILD-16 (U) → J-NA; BUILD-17 (U) → J-NBNS; BUILD-18 (U) → NEW-126; BUILD-19 (U) → NEW-46; BUILD-20 (U) → NEW-53 |

Of the 357 requirements, 250 have a gap. 184 are closed by a new test, 48
only need an existing test run in another configuration, and 18 are
documentation checked by review.

## 4. Flash-failure matrix

This section is the core of the plan. It crosses every failure class that
emubd can inject, or can be extended to inject, with every operation that
reads or writes the device, and says for each cell which test covers it and
what "recovered" means there. The requirements behind it are mainly
REQUIREMENTS.md 6.2 (PL), 6.3 (INT), 6.4 (FAIL) and 6.13 (PRE); its failure
table (end of 6.4) lists the same classes.

### 4.1 Operations (columns)

| Column | Operation | Includes |
|---|---|---|
| O1 | format | `lfs3_format`, with and without `LFS3_F_GBMAP` and the format work flags |
| O2 | mount | `lfs3_mount`, including the mount-time work flags (`LFS3_M_MKCONSISTENT`, `_LOOKAHEAD`, `_COMPACT`, `_PREERASE`, `_CKMETA`, `_CKDATA`); a plain mount does not write |
| O3 | namespace | `lfs3_mkdir`, `lfs3_remove`, `lfs3_rename` |
| O4 | write | `lfs3_file_write` and `lfs3_file_flush`: fragments, crystallization, data-block programs, resumed appends |
| O5 | sync | `lfs3_file_sync`, `lfs3_file_close`, `lfs3_set`, `lfs3_setattr`, `lfs3_removeattr` |
| O6 | truncate | `lfs3_file_truncate`, `lfs3_file_fruncate` |
| O7 | mdir | metadata compaction, split, drop and mroot chain extension |
| O8 | btree | B-tree node commits: file trees, the mtree, the gbmap tree |
| O9 | relocation | moving mdirs, B-tree nodes and data blocks away from bad or worn blocks |
| O10 | gc | janitorial work: `lfs3_fs_gc`, `lfs3_fs_mkconsistent`, `lfs3_fs_ck` with work flags, read-write traversals (compaction, lookahead and gbmap repopulation, orphan cleanup) |
| O11 | pre-erase | gc pre-erase, and claiming a pre-erased block for data |
| O12 | gbmap | `lfs3_fs_mkgbmap`, `lfs3_fs_rmgbmap` |
| O13 | grow | `lfs3_fs_grow` |
| O14 | check | read-only checks: `lfs3_fs_ck` and `lfs3_file_ck` with CKMETA/CKDATA only, `LFS3_O_CK*`, traversals with `LFS3_T_CK*` |

### 4.2 Failure classes (rows)

| Row | Failure class | How it is injected |
|---|---|---|
| F1 | Power loss, ATOMIC | `-Plinear`, `POWERLOSS_BEHAVIOR=0`: the interrupted write does not happen |
| F2 | Power loss, SOMEBITS | `POWERLOSS_BEHAVIOR=1`: one bit of the interrupted prog lands; an interrupted erase leaves old data with one bit flipped |
| F3 | Power loss, MOSTBITS | `POWERLOSS_BEHAVIOR=2`: the interrupted write lands with one bit wrong |
| F4 | Power loss, OOO | `POWERLOSS_BEHAVIOR=3`: every block written since the last sync reverts, except the one being written |
| F5 | Power loss, METASTABLE | `POWERLOSS_BEHAVIOR=4`: the write lands and one bit it wrote (for an erase, one bit of the block) then reads randomly until the block is erased |
| F6 | Bad block, PROGERROR | `BADBLOCK_BEHAVIOR=0` with `lfs3_emubd_mkbad`: prog returns `LFS3_ERR_CORRUPT` |
| F7 | Bad block, ERASEERROR | `BADBLOCK_BEHAVIOR=1`: erase returns `LFS3_ERR_CORRUPT` |
| F8 | Bad block, READERROR | `BADBLOCK_BEHAVIOR=2`: reads return `LFS3_ERR_CORRUPT`; at write time only `LFS3_M_CKPROGS` reads back |
| F9 | Bad block, PROGNOOP | `BADBLOCK_BEHAVIOR=3`: progs silently do nothing |
| F10 | Bad block, ERASENOOP | `BADBLOCK_BEHAVIOR=4`: erases (and progs) silently do nothing |
| F11 | Bad block, PROGFLIP | `BADBLOCK_BEHAVIOR=5`: progs land with one bit flipped |
| F12 | Bad block, READFLIP | `BADBLOCK_BEHAVIOR=6`: reads flip one bit with probability 1/2 |
| F13 | Bad block, MANUAL | `BADBLOCK_BEHAVIOR=7`: the test flips bits with `lfs3_emubd_flip` between operations |
| F14 | Targeted bit flips | `lfs3_emubd_flipbit`, `lfs3_emubd_mkbadbit`, or a block clobbered with 0xcc, at a chosen block and bit |
| F15 | Wear-out | `ERASE_CYCLES` > 0 (10 in `test_exhaustion`): blocks go bad after that many erases, with behaviours 0 to 4 |
| F16 | Out of space | every block in use (small `BLOCK_COUNT`, or filling the disk) |
| F17 | Power loss during bad-block relocation | PLB-TORN with `BADBLOCK_BEHAVIOR` 0 and 1 (2 to 4 with `LFS3_CKPROGS`) |
| F18 | Power loss during wear-levelling relocation | PLB-TORN with `BLOCK_RECYCLES` 0, 1 and 4 |
| F19 | Power loss during wear-out | PLB-TORN with `ERASE_CYCLES=10` |
| F20 | Power loss during janitorial work | PLB-TORN during gc, mkconsistent, compaction, repopulation and mount-time work |
| F21 | Power loss during pre-erase or a pre-erased claim | PLB-TORN with `LFS3_PREERASE` and gc pre-erasing between writes |
| F22 | Bad block during pre-erase | behaviours 1, 2 and 4 on free blocks inside the gbmap known window |
| F23 | Bad blocks with the gbmap | behaviours 0 to 4 in B-YGB, including gbmap nodes |
| F24 | Sync failure | emubd `mkbadsync` (on `v3-fix-alloc`): `cfg->sync` returns `LFS3_ERR_IO` |
| F25 | Other device error | NEW emubd hook: the n-th read, prog or erase returns `LFS3_ERR_IO` |
| F26 | Torn program beyond the first prog unit | NEW emubd power-loss behaviour: the bytes after the first `prog_size` of an interrupted prog are disturbed, the first `prog_size` are not (3-alloc B17) |
| F27 | Transient read error | NEW emubd hook: a read returns `LFS3_ERR_CORRUPT` once, then succeeds |

### 4.3 Recovery criteria

"Recovered" is decided by observables after the fault, normally after a
remount with the same configuration. Every criterion also includes **R0**:
no `LFS3_ASSERT` fires, the process does not crash, no sanitizer reports
anything in a sanitizer build, and every negative result is an `lfs3.h`
error code or the code the block device returned (LFS3-GEN-05).

| Code | Recovered means |
|---|---|
| A | **Power loss survived.** `lfs3_mount` returns 0. Every file reads back exactly as of its last `lfs3_file_sync` or `lfs3_file_close` that returned 0, or as of the sync that was in progress. Every `lfs3_mkdir`, `lfs3_remove` and `lfs3_rename` is either complete or absent. `lfs3_fs_ck(LFS3_CK_CKMETA \| LFS3_CK_CKDATA)` returns 0. The workload continues to the end. Allowed errors: `LFS3_ERR_EXIST` or `LFS3_ERR_NOENT` when the test repeats an operation that had already completed. (PL-01, PL-04 to PL-17) |
| A1 | **Format survived.** After the loss, `lfs3_format` and then `lfs3_mount` both return 0, and criterion A holds for a workload that follows. (PL-22) |
| B | **Metastable bit survived.** As A, except that `lfs3_mount`, reads and checks may also return `LFS3_ERR_CORRUPT`. With `LFS3_M_CKMETAPARITY \| LFS3_M_CKDATACKSUMS`, no read returns data that neither a completed sync nor the sync in progress wrote. No completed sync is lost: with `LFS3_M_SETTLE` none at all, in the default mode none outside the residual cases of LFS3-DEG-12, which the case counts and prints. (PL-03, DEG-01, DEG-12, DEG-13) |
| C | **Relocated.** The operation returns 0 while a good free block exists, and `LFS3_ERR_NOSPC` only when none does. Content equals the model before and after remount. `lfs3_fs_ck(LFS3_CK_CKMETA \| LFS3_CK_CKDATA)` returns 0. (FAIL-01 to FAIL-06, FILE-10, FILE-11) |
| D | **Detected.** The operation, or the next check that covers the block, returns `LFS3_ERR_CORRUPT`. No call returns flipped or stale data without an error, under the check options the test names. Nothing is programmed or erased on a block that held committed data before the fault. Files that do not use the bad block stay readable. (FAIL-04, FAIL-07 to FAIL-10, FAIL-17, INT-10 to INT-22) |
| E | **Format refused.** `lfs3_format` returns a negative error, not 0. A format of a device without the bad block then succeeds. (FAIL-16) |
| F | **Anchor stuck.** Writes that need a new anchor return `LFS3_ERR_NOSPC`; in-RAM state stays equal to the disk (`lfs3_fs_cksum` unchanged by the failed call, META-10); after remount every file is readable. (FAIL-15) |
| G | **Worn out.** Writes succeed until the device cannot hold the workload; then `LFS3_ERR_NOSPC`, never `LFS3_ERR_CORRUPT`; a remount with `LFS3_M_RDONLY` reads every file synced before the first `LFS3_ERR_NOSPC`. (FAIL-11, FAIL-12, FAIL-13) |
| H | **Out of space.** The call returns `LFS3_ERR_NOSPC`; the disk is as before the call, and a file handle that failed is desynced; synced data is intact after remount; after files are removed, the same write succeeds. (ALLOC-03, ALLOC-04, ALLOC-05, SYNC-19) |
| I | **Device error passed through.** The call returns the block device's error unchanged; a file handle becomes desynced; `lfs3_fs_cksum` is unchanged by the failed call; after remount the last synced state is present and `lfs3_fs_ck` returns 0. (FAIL-18, FAIL-19, META-10) |
| K | **Skipped.** The call returns 0. The failing block is not used: never programmed without an erase, not erased again in the same pass. Later calls make progress past it. (PRE-03, PRE-06) |
| L | **No rollback.** After a read that failed once, the newest commit of the metadata pair is still the one fetched, and the file contents are as last synced. (DOC-12, open question on transient errors) |

The prog-once check (NEW-12) strengthens A, B and K where the 4.5 tables say
so: an emubd option fails the test if any byte is programmed twice without
an erase in between (LFS3-CFG-15).

### 4.4 Overview

Rows are failure classes (4.2), columns are operations (4.1). Each cell
gives the coverage and the recovery criterion (4.3): **T** an existing case
covers it in the default run; **R** an existing case covers it but must run
in another build or schedule (job named in 4.5); **P** existing cases cover
part and a new test the rest; **Nnn** only the new test NEW-nn covers it;
**–** not applicable, with the reason in 4.5.

| | O1 format | O2 mount | O3 namespace | O4 write | O5 sync | O6 truncate | O7 mdir | O8 btree | O9 relocation | O10 gc | O11 pre-erase | O12 gbmap | O13 grow | O14 check |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| F1 Power loss, ATOMIC | N01/A1 | N05/A | T/A | P/A | P/A | N02/A | P/A | P/A | T/A | N05/A | N11/A | N06/A | T/A | – |
| F2 Power loss, SOMEBITS | N01/A1 | N05/A | P/A | P/A | P/A | N02/A | P/A | P/A | R/A | N05/A | N11/A | N06/A | R/A | – |
| F3 Power loss, MOSTBITS | N01/A1 | N05/A | P/A | P/A | P/A | N02/A | P/A | P/A | R/A | N05/A | N11/A | N06/A | R/A | – |
| F4 Power loss, OOO | N01/A1 | N05/A | P/A | P/A | P/A | N02/A | P/A | P/A | R/A | N05/A | N11/A | N06/A | R/A | – |
| F5 Power loss, METASTABLE | N01/B | N05/B | N08/B | N08/B | N08/B | N08/B | N08/B | N08/B | N08/B | N05/B | N11/B | N06/B | N08/B | – |
| F6 Bad block, PROGERROR | T/E | N26/C | T/C | P/C | T/C | P/C | T/C | T/C | T/C | N26/C | N26/C | N28/C | N27/C | – |
| F7 Bad block, ERASEERROR | T/E | N26/C | T/C | P/C | T/C | P/C | T/C | T/C | T/C | N26/C | N22/K | N28/C | N27/C | – |
| F8 Bad block, READERROR | T/E | N13/D | R/C | P/C | R/C | P/C | P/C | P/C | P/D | N13/D | N22/K | N28/D | N27/C | N13/D |
| F9 Bad block, PROGNOOP | T/E | N26/C | P/C | P/C | P/C | P/C | P/C | P/C | R/C | N26/C | N26/C | N28/C | – | – |
| F10 Bad block, ERASENOOP | T/E | N26/C | P/C | P/C | P/C | P/C | P/C | P/C | R/C | N26/C | N25/C | N28/C | – | – |
| F11 Bad block, PROGFLIP | R/C | N26/C | R/C | R/C | R/C | P/C | R/C | R/C | R/C | N26/C | N26/C | N28/C | – | R/D |
| F12 Bad block, READFLIP | – | R/D | N62/D | P/D | N62/D | N62/D | N62/D | R/D | N63/D | N62/D | – | – | – | R/D |
| F13 Bad block, MANUAL | – | T/D | R/D | R/D | R/D | R/D | N63/D | N63/D | N63/D | R/D | – | N64/D | – | T/D |
| F14 Targeted bit flips | – | T/D | N65/D | R/D | R/D | – | N63/D | R/D | N63/D | R/D | N23/K | N64/D | – | P/D |
| F15 Wear-out | – | N15/G | T/G | T/G | T/G | P/G | T/G | T/G | P/G | N15/G | N15/G | – | – | – |
| F16 Out of space | – | N29/H | T/H | T/H | T/H | N29/H | R/H | T/H | R/H | R/H | N31/H | N30/H | T/H | – |
| F17 Power loss during bad-block relocation | – | – | N09/A | N09/A | N09/A | – | N09/A | N09/A | N09/A | – | – | – | – | – |
| F18 Power loss during wear-levelling relocation | – | – | – | – | – | – | R/A | – | R/A | – | – | – | – | – |
| F19 Power loss during wear-out | – | – | N10/A | N10/A | N10/A | – | N10/A | – | N10/A | – | – | – | – | – |
| F20 Power loss during janitorial work | – | N05/A | – | – | – | – | P/A | – | – | N05/A | – | – | – | – |
| F21 Power loss during pre-erase or a pre-erased claim | – | – | – | N11/A | – | – | – | – | – | – | N11/A | – | – | – |
| F22 Bad block during pre-erase | – | N22/K | – | – | – | – | – | – | – | – | N22/K | – | – | – |
| F23 Bad blocks with the gbmap | – | – | R/C | R/C | R/C | – | R/C | R/C | R/C | N13/D | – | N28/C | N27/C | – |
| F24 Sync failure | N18/I | – | N18/I | – | N18/I | – | N18/I | – | – | – | – | – | N18/I | – |
| F25 Other device error | – | N19/I | N19/I | N19/I | N19/I | – | – | – | – | – | – | – | – | N19/I |
| F26 Torn program beyond the first prog unit | – | – | – | N60/A | – | – | N60/A | – | – | – | N60/A | – | – | – |
| F27 Transient read error | – | – | – | – | – | – | N129/L | – | N129/L | – | – | – | – | – |

The matrix has 27 rows and 14 columns, 378 cells: 31 covered by existing
cases in the default run, 41 by existing cases in another configuration, 46
partly, 122 only by new tests, and 138 not applicable. Of the 240 applicable
cells, 31 (13%) are fully covered today.

### 4.5 Cell details

One table per failure class. Cells with the same coverage are grouped.
"Recovered" refers to the criteria of 4.3.

#### F1. Power loss, ATOMIC

Injection: `-Plinear`, `POWERLOSS_BEHAVIOR=0`: the interrupted write does
not happen.

| Operations | Coverage | Tests | Recovered means |
|---|---|---|---|
| O1 format | N | NEW-01 | A1 |
| O2 mount, O10 gc | N | NEW-05 | A |
| O3 namespace | T | `dirs::mkdir_*`, `dirs::rm_*`, `dirs::mv_*`, `dread::recursive_*` | A |
| O4 write | P | `powerloss::spam_f_pl_fuzz`, `files::pl_fuzz`; NEW-03 (overwrites, holes), NEW-58 (flush without sync) | A |
| O5 sync | P | `powerloss::spam_f_pl_fuzz`, `powerloss::spam_fd_pl_fuzz`, `stickynotes::*_pl`, `attrs::fattr_pl_fuzz_fuzz`; NEW-55 (`lfs3_set`), NEW-57 (`O_SYNC`), NEW-59 (path attributes) | A |
| O6 truncate | N | NEW-02 | A |
| O7 mdir | P | `dirs::*`, `powerloss::spam_dir_many` (indirect); NEW-07 | A |
| O8 btree | P | `powerloss::spam_file_many` with `SHRUB_SIZE=0`, `files::pl_fuzz`; NEW-03 | A |
| O9 relocation | T | `relocations::spam_f_pl_fuzz`, `relocations::spam_fd_pl_fuzz` | A |
| O11 pre-erase | N | NEW-11 | A |
| O12 gbmap | N | NEW-06 | A |
| O13 grow | T | `grow::incr_spam_f_pl_fuzz`, `grow::incr_spam_fd_pl_fuzz` | A |
| O14 check | – | | N/A: read-only: there is no write for the power loss to interrupt |

#### F2. Power loss, SOMEBITS

Injection: `POWERLOSS_BEHAVIOR=1`: one bit of the interrupted prog lands; an
interrupted erase leaves old data with one bit flipped.

| Operations | Coverage | Tests | Recovered means |
|---|---|---|---|
| O1 format | N | NEW-01 (runs PLB-TORN) | A1 |
| O2 mount | N | NEW-05 (runs PLB-TORN) | A |
| O3 namespace | P | `powerloss::spam_dir_many` (mkdir only, runs today); `dirs::*` under J-PL | A |
| O4 write | P | `powerloss::spam_f_pl_fuzz` (runs today); NEW-03, NEW-58 | A |
| O5 sync | P | `powerloss::spam_f_pl_fuzz`, `powerloss::spam_fd_pl_fuzz` (run today); `stickynotes::*_pl`, `attrs::fattr_pl_fuzz_fuzz` under J-PL; NEW-55, NEW-57, NEW-59 | A |
| O6 truncate | N | NEW-02 (runs PLB-TORN) | A |
| O7 mdir | P | `powerloss::*` (indirect, runs today); NEW-07 | A |
| O8 btree | P | `powerloss::spam_file_many` (runs today); NEW-03 | A |
| O9 relocation | R | `relocations::spam_f_pl_fuzz`, `relocations::spam_fd_pl_fuzz` under J-PL | A |
| O10 gc | N | NEW-05 | A |
| O11 pre-erase | N | NEW-11 | A |
| O12 gbmap | N | NEW-06 | A |
| O13 grow | R | `grow::incr_spam_*_pl_fuzz` under J-PL | A |
| O14 check | – | | N/A: read-only: there is no write for the power loss to interrupt |

#### F3. Power loss, MOSTBITS

Injection: `POWERLOSS_BEHAVIOR=2`: the interrupted write lands with one bit
wrong.

| Operations | Coverage | Tests | Recovered means |
|---|---|---|---|
| O1 format | N | NEW-01 (runs PLB-TORN) | A1 |
| O2 mount | N | NEW-05 (runs PLB-TORN) | A |
| O3 namespace | P | `powerloss::spam_dir_many` (mkdir only, runs today); `dirs::*` under J-PL | A |
| O4 write | P | `powerloss::spam_f_pl_fuzz` (runs today); NEW-03, NEW-58 | A |
| O5 sync | P | `powerloss::spam_f_pl_fuzz`, `powerloss::spam_fd_pl_fuzz` (run today); `stickynotes::*_pl`, `attrs::fattr_pl_fuzz_fuzz` under J-PL; NEW-55, NEW-57, NEW-59 | A |
| O6 truncate | N | NEW-02 (runs PLB-TORN) | A |
| O7 mdir | P | `powerloss::*` (indirect, runs today); NEW-07 | A |
| O8 btree | P | `powerloss::spam_file_many` (runs today); NEW-03 | A |
| O9 relocation | R | `relocations::spam_f_pl_fuzz`, `relocations::spam_fd_pl_fuzz` under J-PL | A |
| O10 gc | N | NEW-05 | A |
| O11 pre-erase | N | NEW-11 | A |
| O12 gbmap | N | NEW-06 | A |
| O13 grow | R | `grow::incr_spam_*_pl_fuzz` under J-PL | A |
| O14 check | – | | N/A: read-only: there is no write for the power loss to interrupt |

#### F4. Power loss, OOO

Injection: `POWERLOSS_BEHAVIOR=3`: every block written since the last sync
reverts, except the one being written.

| Operations | Coverage | Tests | Recovered means |
|---|---|---|---|
| O1 format | N | NEW-01 (runs PLB-TORN) | A1 |
| O2 mount | N | NEW-05 (runs PLB-TORN) | A |
| O3 namespace | P | `powerloss::spam_dir_many` (mkdir only, runs today); `dirs::*` under J-PL | A |
| O4 write | P | `powerloss::spam_f_pl_fuzz` (runs today); NEW-03, NEW-58 | A |
| O5 sync | P | `powerloss::spam_f_pl_fuzz`, `powerloss::spam_fd_pl_fuzz` (run today); `stickynotes::*_pl`, `attrs::fattr_pl_fuzz_fuzz` under J-PL; NEW-55, NEW-57, NEW-59 | A |
| O6 truncate | N | NEW-02 (runs PLB-TORN) | A |
| O7 mdir | P | `powerloss::*` (indirect, runs today); NEW-07 | A |
| O8 btree | P | `powerloss::spam_file_many` (runs today); NEW-03 | A |
| O9 relocation | R | `relocations::spam_f_pl_fuzz`, `relocations::spam_fd_pl_fuzz` under J-PL | A |
| O10 gc | N | NEW-05 | A |
| O11 pre-erase | N | NEW-11 | A |
| O12 gbmap | N | NEW-06 | A |
| O13 grow | R | `grow::incr_spam_*_pl_fuzz` under J-PL | A |
| O14 check | – | | N/A: read-only: there is no write for the power loss to interrupt |

#### F5. Power loss, METASTABLE

Injection: `POWERLOSS_BEHAVIOR=4`: the write lands, and one bit it wrote (for
an erase, one bit of the block) then reads randomly until the block is
erased; later programs elsewhere in the block don't settle it. Bytes outside
the interrupted operation never change; `bd::metastable` checks this.

| Operations | Coverage | Tests | Recovered means |
|---|---|---|---|
| O1 format | N | NEW-01 (METASTABLE permutation) | B |
| O2 mount, O10 gc | N | NEW-05 (METASTABLE permutation) | B |
| O3 namespace, O4 write, O5 sync, O6 truncate, O7 mdir, O8 btree, O9 relocation, O13 grow | N | NEW-08 | B |
| O11 pre-erase | N | NEW-11 (METASTABLE permutation) | B |
| O12 gbmap | N | NEW-06 (METASTABLE permutation) | B |
| O14 check | – | | N/A: read-only: there is no write for the power loss to interrupt |

#### F6. Bad block, PROGERROR

Injection: `BADBLOCK_BEHAVIOR=0` with `lfs3_emubd_mkbad`: prog returns
`LFS3_ERR_CORRUPT`.

| Operations | Coverage | Tests | Recovered means |
|---|---|---|---|
| O1 format | T | `badblocks::mrootanchor_format` | E |
| O2 mount, O10 gc | N | NEW-26 | C |
| O3 namespace | T | `badblocks::{every,region,alternating}_spam_dir_many`, `_spam_dir_fuzz` | C |
| O4 write | P | `badblocks::*_spam_fwrite_fuzz`, `badblocks::*_spam_file_*` (blocks bad before the test); NEW-20 (block goes bad after data was written) | C |
| O5 sync | T | `badblocks::*_spam_file_*`, `badblocks::*_spam_uz_fuzz`, `badblocks::*_spam_uzd_fuzz` | C |
| O6 truncate | P | `badblocks::*_spam_fwrite_fuzz` (truncate); NEW-21 (fruncate, on `v3-fix-files`) | C |
| O7 mdir | T | `badblocks::*`; anchor blocks: `badblocks::mrootanchor_wear` | C; anchor: F |
| O8 btree | T | `badblocks::*_btree_many` | C |
| O9 relocation | T | `badblocks::*` (relocation is the recovery) | C |
| O11 pre-erase | N | NEW-26 (the gbmap commit after a pre-erase, and data written into a claimed block) | C |
| O12 gbmap | N | NEW-28 | C |
| O13 grow | N | NEW-27 | C |
| O14 check | – | | N/A: read-only: PROGERROR needs a prog |

#### F7. Bad block, ERASEERROR

Injection: `BADBLOCK_BEHAVIOR=1`: erase returns `LFS3_ERR_CORRUPT`.

| Operations | Coverage | Tests | Recovered means |
|---|---|---|---|
| O1 format | T | `badblocks::mrootanchor_format` | E |
| O2 mount | N | NEW-26; with `LFS3_M_PREERASE`: NEW-22 | C |
| O3 namespace | T | `badblocks::*_spam_dir_*` with `BADBLOCK_BEHAVIOR=1` | C |
| O4 write | P | `badblocks::*_spam_fwrite_fuzz`; NEW-20 | C |
| O5 sync | T | `badblocks::*_spam_file_*`, `*_spam_uz*_fuzz` | C |
| O6 truncate | P | `badblocks::*_spam_fwrite_fuzz` (truncate); NEW-21 | C |
| O7 mdir | T | `badblocks::*`; anchor: `badblocks::mrootanchor_wear` | C; anchor: F |
| O8 btree | T | `badblocks::*_btree_many` | C |
| O9 relocation | T | `badblocks::*` | C |
| O10 gc | N | NEW-26 | C |
| O11 pre-erase | N | NEW-22 (on `v3-fix-alloc`) | K |
| O12 gbmap | N | NEW-28 | C |
| O13 grow | N | NEW-27 | C |
| O14 check | – | | N/A: read-only: ERASEERROR needs an erase |

#### F8. Bad block, READERROR

Injection: `BADBLOCK_BEHAVIOR=2`: reads return `LFS3_ERR_CORRUPT`; at write
time only `LFS3_M_CKPROGS` reads back.

| Operations | Coverage | Tests | Recovered means |
|---|---|---|---|
| O1 format | T | `badblocks::mrootanchor_format` | E |
| O2 mount | N | NEW-13 (live mdir at mount) | D |
| O3 namespace | R | `badblocks::*_spam_dir_*` with `CKPROGS=true` under J-BIG | C |
| O4 write | P | `badblocks::*_spam_fwrite_fuzz` with `CKPROGS=true` under J-BIG; NEW-13 (live data block) | C at write time; D for live data |
| O5 sync | R | `badblocks::*_spam_file_*` with `CKPROGS=true` under J-BIG | C |
| O6 truncate | P | `badblocks::*_spam_fwrite_fuzz` (truncate) under J-BIG; NEW-21 | C |
| O7 mdir | P | `badblocks::*` with `CKPROGS=true` under J-BIG; NEW-17 (source of a compaction) | C at write time; D for the source |
| O8 btree | P | `badblocks::*_btree_many` under J-BIG; NEW-17 | C at write time; D for the source |
| O9 relocation | P | `badblocks::*` under J-BIG; NEW-17 (no relocation storm) | D for the source |
| O10 gc | N | NEW-13 (READERROR during a lookahead scan or gbmap repopulation) | D |
| O11 pre-erase | N | NEW-22 (erased-state checksum read fails) | K |
| O12 gbmap | N | NEW-28 | D |
| O13 grow | N | NEW-27 | C |
| O14 check | N | NEW-13 | D |

#### F9. Bad block, PROGNOOP

Injection: `BADBLOCK_BEHAVIOR=3`: progs silently do nothing.

| Operations | Coverage | Tests | Recovered means |
|---|---|---|---|
| O1 format | T | `badblocks::mrootanchor_format` | E |
| O2 mount, O10 gc, O11 pre-erase | N | NEW-26 | C |
| O3 namespace | P | `badblocks::*_spam_dir_*` with `BADBLOCK_BEHAVIOR=3`, `CKPROGS=true` under J-BIG; NEW-14 (without CKPROGS) | C with CKPROGS; D without |
| O4 write | P | `badblocks::*_spam_fwrite_fuzz` under J-BIG; NEW-14; NEW-20 | C with CKPROGS; D without |
| O5 sync | P | `badblocks::*_spam_file_*` under J-BIG; NEW-14 | C with CKPROGS; D without |
| O6 truncate | P | `badblocks::*_spam_fwrite_fuzz` under J-BIG; NEW-21 | C |
| O7 mdir | P | `badblocks::*` under J-BIG; NEW-14 | C with CKPROGS; D without |
| O8 btree | P | `badblocks::*_btree_many` under J-BIG; NEW-14 | C with CKPROGS; D without |
| O9 relocation | R | `badblocks::*` under J-BIG | C |
| O12 gbmap | N | NEW-28 | C |
| O13 grow | – | | N/A: the only write of grow is an mroot commit, covered by O7 |
| O14 check | – | | N/A: read-only: a silent write failure needs a write |

#### F10. Bad block, ERASENOOP

Injection: `BADBLOCK_BEHAVIOR=4`: erases (and progs) silently do nothing.

| Operations | Coverage | Tests | Recovered means |
|---|---|---|---|
| O1 format | T | `badblocks::mrootanchor_format` | E |
| O2 mount, O10 gc | N | NEW-26 | C |
| O3 namespace | P | `badblocks::*_spam_dir_*` with `BADBLOCK_BEHAVIOR=4`, `CKPROGS=true` under J-BIG; NEW-14 (without CKPROGS) | C with CKPROGS; D without |
| O4 write | P | `badblocks::*_spam_fwrite_fuzz` under J-BIG; NEW-14; NEW-20 | C with CKPROGS; D without |
| O5 sync | P | `badblocks::*_spam_file_*` under J-BIG; NEW-14 | C with CKPROGS; D without |
| O6 truncate | P | `badblocks::*_spam_fwrite_fuzz` under J-BIG; NEW-21 | C |
| O7 mdir | P | `badblocks::*` under J-BIG; NEW-14 | C with CKPROGS; D without |
| O8 btree | P | `badblocks::*_btree_many` under J-BIG; NEW-14 | C with CKPROGS; D without |
| O9 relocation | R | `badblocks::*` under J-BIG | C |
| O11 pre-erase | N | NEW-25 | C |
| O12 gbmap | N | NEW-28 | C |
| O13 grow | – | | N/A: the only write of grow is an mroot commit, covered by O7 |
| O14 check | – | | N/A: read-only: a silent write failure needs a write |

#### F11. Bad block, PROGFLIP

Injection: `BADBLOCK_BEHAVIOR=5`: progs land with one bit flipped.

| Operations | Coverage | Tests | Recovered means |
|---|---|---|---|
| O1 format | R | `ck::ckprogs_mroot` under J-BIG | C or E |
| O2 mount, O10 gc, O11 pre-erase | N | NEW-26 | C |
| O3 namespace | R | `ck::spam_dir_fuzz` with `METHOD=0` under J-BIG | C |
| O4 write | R | `ck::ckprogs_data`, `ck::spam_fwrite_fuzz` under J-BIG | C |
| O5 sync | R | `ck::spam_file_fuzz`, `ck::spam_uz_fuzz`, `ck::spam_uzd_fuzz` under J-BIG | C |
| O6 truncate | P | `ck::spam_fwrite_fuzz` (truncate) under J-BIG; NEW-21 | C |
| O7 mdir, O9 relocation | R | `ck::ckprogs_overrecycling` under J-BIG | C |
| O8 btree | R | `ck::ckprogs_btree` under J-BIG | C |
| O12 gbmap | N | NEW-28 | C |
| O13 grow | – | | N/A: the only write of grow is an mroot commit, covered by O7 |
| O14 check | R | `ck::spam_*_fuzz` with `METHOD` 1 to 3 (flips that no read-back caught) under J-BIG | D |

#### F12. Bad block, READFLIP

Injection: `BADBLOCK_BEHAVIOR=6`: reads flip one bit with probability 1/2.

| Operations | Coverage | Tests | Recovered means |
|---|---|---|---|
| O1 format | – | | N/A: READFLIP affects later reads; a flip after format is covered by O2 |
| O2 mount | R | `ck::ckparity_mroot` under J-BIG | D |
| O3 namespace, O5 sync, O6 truncate, O7 mdir, O10 gc | N | NEW-62 | D |
| O4 write | P | `ck::ckdatacksums_data` under J-BIG; NEW-62 | D |
| O8 btree | R | `ck::ckparity_btree` under J-BIG | D |
| O9 relocation | N | NEW-63 (flipped data copied by a relocation) | D |
| O11 pre-erase | – | | N/A: a flipped read of an erased block can only make littlefs skip it (PRE-03) |
| O12 gbmap | – | | N/A: gbmap nodes are B-tree nodes, covered by O8 |
| O13 grow | – | | N/A: grow reads the mroot like any commit, covered by O7 |
| O14 check | R | `ck::ckparity_*`, `ck::ckdatacksums_data` under J-BIG | D |

#### F13. Bad block, MANUAL

Injection: `BADBLOCK_BEHAVIOR=7`: the test flips bits with `lfs3_emubd_flip`
between operations.

| Operations | Coverage | Tests | Recovered means |
|---|---|---|---|
| O1 format | – | | N/A: flips are applied between operations; a flip after format is a mount case, O2 |
| O2 mount | T | `ck::ckmeta_*`, `ck::ckdata_*` with `METHOD=3` | D |
| O3 namespace | R | `ck::spam_dir_fuzz` under J-BIG (B-DEF runs some methods) | D |
| O4 write, O6 truncate | R | `ck::spam_fwrite_fuzz` under J-BIG | D |
| O5 sync | R | `ck::spam_file_fuzz`, `ck::spam_uz_fuzz` (F-9), `ck::spam_uzd_fuzz` under J-BIG | D |
| O7 mdir, O8 btree, O9 relocation | N | NEW-63 (a flip is not laundered into a fresh checksum by compaction or relocation) | D |
| O10 gc | R | `ck::ckmeta_*`, `ck::ckdata_*` with `METHOD=1` under J-BIG | D |
| O11 pre-erase | – | | N/A: a flipped erased block can only be skipped (PRE-03); covered by F14 |
| O12 gbmap | N | NEW-64 | D |
| O13 grow | – | | N/A: grow adds no read path beyond the mroot commit, O7 |
| O14 check | T | `ck::ckmeta_easy`, `ck::ckmeta_hard`, `ck::ckdata_easy`, `ck::ckdata_hard` | D |

#### F14. Targeted bit flips

Injection: `lfs3_emubd_flipbit`, `lfs3_emubd_mkbadbit`, or a block clobbered
with 0xcc, at a chosen block and bit.

| Operations | Coverage | Tests | Recovered means |
|---|---|---|---|
| O1 format | – | | N/A: a flip after format is a mount case, O2 |
| O2 mount | T | `mount::t_ckmeta`, `mount::t_ckdata`, `ck::*_hard` with `METHOD=3` | D |
| O3 namespace | N | NEW-65 (namespace operations after an mdir is rolled back or flipped) | D |
| O4 write | R | `ck::ckfetches_data` under J-BIG | D |
| O5 sync | R | `ck::ckfetches_mroot` under J-BIG | D |
| O6 truncate | – | | N/A: truncate reads the file tree through the same paths as O4 and O8 |
| O7 mdir, O9 relocation | N | NEW-63 | D |
| O8 btree | R | `ck::ckfetches_btree` under J-BIG | D |
| O10 gc | R | `ck::ckmeta_*`, `ck::ckdata_*` with `METHOD=1` under J-BIG; `trvs::ck*` | D |
| O11 pre-erase | N | NEW-23 (a pre-erased block that no longer matches its checksum) | K |
| O12 gbmap | N | NEW-64 | D |
| O13 grow | – | | N/A: grow adds no read path beyond the mroot commit, O7 |
| O14 check | P | `ck::ckmeta_*`, `ck::ckdata_*`, `ck::file_*`, `trvs::ckmdir_*`, `trvs::ckbtree_*`, `trvs::ckdata_*`; NEW-67 (data-block flips for `lfs3_file_ck`) | D |

#### F15. Wear-out

Injection: `ERASE_CYCLES` > 0 (10 in `test_exhaustion`): blocks go bad after
that many erases, with behaviours 0 to 4.

| Operations | Coverage | Tests | Recovered means |
|---|---|---|---|
| O1 format | – | | N/A: format runs before wear accumulates; a worn block at format is a bad-block row |
| O2 mount | N | NEW-15 (read-only remount after end of life) | G |
| O3 namespace | T | `exhaustion::spam_dir_fuzz` (behaviours 0, 1; 2 to 4 under J-BIG) | G |
| O4 write, O8 btree | T | `exhaustion::spam_fwrite_fuzz` | G |
| O5 sync | T | `exhaustion::spam_file_fuzz`, `exhaustion::spam_uz_fuzz`, `exhaustion::spam_uzd_fuzz` | G |
| O6 truncate | P | `exhaustion::spam_fwrite_fuzz` (truncate); NEW-15 (adds fruncate) | G |
| O7 mdir | T | `exhaustion::*` | G |
| O9 relocation | P | `exhaustion::*`; NEW-61 (erases per block between relocations) | G |
| O10 gc | N | NEW-15 (gc steps in the workload) | G |
| O11 pre-erase | N | NEW-15 (pre-erase steps, B-BIG) | G |
| O12 gbmap | – | | N/A: one-off operations; their failure paths are the bad-block rows |
| O13 grow | – | | N/A: grow adds fresh blocks and has no wear-specific path |
| O14 check | – | | N/A: read-only: reads do not wear |

#### F16. Out of space

Injection: every block in use (small `BLOCK_COUNT`, or filling the disk).

| Operations | Coverage | Tests | Recovered means |
|---|---|---|---|
| O1 format | – | | N/A: format needs a fixed number of blocks; too few is a configuration error (MOUNT-03, NEW-44) |
| O2 mount | N | NEW-29 (mount-time mkconsistent and compaction on a full disk) | H |
| O3 namespace | T | `alloc::nospc_dirs` | H |
| O4 write, O8 btree | T | `alloc::nospc_files` | H |
| O5 sync | T | `alloc::nospc_files` (close returns 0 after NOSPC, SYNC-19) | H |
| O6 truncate | N | NEW-29 (shrinking a file on a full disk succeeds) | H |
| O7 mdir | R | `ck::ckprogs_overrecycling` (`BLOCK_COUNT=2`) under J-BIG | H |
| O9 relocation | R | `ck::ckprogs_overrecycling` under J-BIG (overrecycling replaces relocation) | H |
| O10 gc | R | `gc::nospc` under J-BIG | H |
| O11 pre-erase | N | NEW-31 | H |
| O12 gbmap | N | NEW-30 | H |
| O13 grow | T | `grow::incr_spam_*` (grow by one block after each NOSPC) | H |
| O14 check | – | | N/A: read-only: needs no space |

#### F17. Power loss during bad-block relocation

Injection: PLB-TORN with `BADBLOCK_BEHAVIOR` 0 and 1 (2 to 4 with
`LFS3_CKPROGS`).

| Operations | Coverage | Tests | Recovered means |
|---|---|---|---|
| O1 format, O2 mount, O6 truncate, O10 gc, O11 pre-erase, O12 gbmap, O13 grow, O14 check | – | | N/A: outside this combination; see the single-fault rows |
| O3 namespace, O4 write, O5 sync, O7 mdir, O8 btree, O9 relocation | N | NEW-09 | A, then C |

#### F18. Power loss during wear-levelling relocation

Injection: PLB-TORN with `BLOCK_RECYCLES` 0, 1 and 4.

| Operations | Coverage | Tests | Recovered means |
|---|---|---|---|
| O1 format, O2 mount, O3 namespace, O4 write, O5 sync, O6 truncate, O8 btree, O10 gc, O11 pre-erase, O12 gbmap, O13 grow, O14 check | – | | N/A: outside this combination |
| O7 mdir, O9 relocation | R | `relocations::spam_f_pl_fuzz`, `relocations::spam_fd_pl_fuzz` under J-PL | A |

#### F19. Power loss during wear-out

Injection: PLB-TORN with `ERASE_CYCLES=10`.

| Operations | Coverage | Tests | Recovered means |
|---|---|---|---|
| O1 format, O2 mount, O6 truncate, O8 btree, O10 gc, O11 pre-erase, O12 gbmap, O13 grow, O14 check | – | | N/A: outside this combination |
| O3 namespace, O4 write, O5 sync, O7 mdir, O9 relocation | N | NEW-10 | A, then G |

#### F20. Power loss during janitorial work

Injection: PLB-TORN during gc, mkconsistent, compaction, repopulation and
mount-time work.

| Operations | Coverage | Tests | Recovered means |
|---|---|---|---|
| O1 format, O3 namespace, O4 write, O5 sync, O6 truncate, O8 btree, O9 relocation, O11 pre-erase, O12 gbmap, O13 grow, O14 check | – | | N/A: outside this combination |
| O2 mount, O10 gc | N | NEW-05 | A |
| O7 mdir | P | `dirs::*` (indirect); NEW-07 | A |

#### F21. Power loss during pre-erase or a pre-erased claim

Injection: PLB-TORN with `LFS3_PREERASE` and gc pre-erasing between writes.

| Operations | Coverage | Tests | Recovered means |
|---|---|---|---|
| O1 format, O2 mount, O3 namespace, O5 sync, O6 truncate, O7 mdir, O8 btree, O9 relocation, O10 gc, O12 gbmap, O13 grow, O14 check | – | | N/A: outside this combination |
| O4 write | N | NEW-11 (data written into a claimed block) | A, with the prog-once check |
| O11 pre-erase | N | NEW-11 | A, with the prog-once check |

#### F22. Bad block during pre-erase

Injection: behaviours 1, 2 and 4 on free blocks inside the gbmap known window.

| Operations | Coverage | Tests | Recovered means |
|---|---|---|---|
| O1 format, O3 namespace, O4 write, O5 sync, O6 truncate, O7 mdir, O8 btree, O9 relocation, O10 gc, O12 gbmap, O13 grow, O14 check | – | | N/A: outside this combination |
| O2 mount | N | NEW-22 (mount with `LFS3_M_PREERASE`) | K |
| O11 pre-erase | N | NEW-22 (ERASEERROR, READERROR), NEW-25 (ERASENOOP) | K |

#### F23. Bad blocks with the gbmap

Injection: behaviours 0 to 4 in B-YGB, including gbmap nodes.

| Operations | Coverage | Tests | Recovered means |
|---|---|---|---|
| O1 format, O2 mount, O6 truncate, O11 pre-erase, O14 check | – | | N/A: outside this combination |
| O3 namespace, O4 write, O5 sync, O7 mdir, O8 btree, O9 relocation | R | `badblocks::*` in B-YGB, with the B-tree cases no longer excluded (J-YGB) | C |
| O10 gc | N | NEW-13 (READERROR during gbmap repopulation) | D |
| O12 gbmap | N | NEW-28 | C |
| O13 grow | N | NEW-27 | C |

#### F24. Sync failure

Injection: emubd `mkbadsync` (on `v3-fix-alloc`): `cfg->sync` returns
`LFS3_ERR_IO`.

| Operations | Coverage | Tests | Recovered means |
|---|---|---|---|
| O1 format, O3 namespace, O5 sync, O7 mdir, O13 grow | N | NEW-18 (on `v3-fix-alloc`) | I |
| O2 mount, O4 write, O6 truncate, O8 btree, O9 relocation, O10 gc, O11 pre-erase, O12 gbmap, O14 check | – | | N/A: outside this combination; every write path ends in the same commit sync |

#### F25. Other device error

Injection: NEW emubd hook: the n-th read, prog or erase returns `LFS3_ERR_IO`.

| Operations | Coverage | Tests | Recovered means |
|---|---|---|---|
| O1 format, O6 truncate, O7 mdir, O8 btree, O9 relocation, O10 gc, O11 pre-erase, O12 gbmap, O13 grow | – | | N/A: outside this combination; the error is passed through the same bd wrappers |
| O2 mount, O3 namespace, O4 write, O5 sync, O14 check | N | NEW-19 | I |

#### F26. Torn program beyond the first prog unit

Injection: NEW emubd power-loss behaviour: the bytes after the first
`prog_size` of an interrupted prog are disturbed, the first `prog_size` are
not (3-alloc B17).

| Operations | Coverage | Tests | Recovered means |
|---|---|---|---|
| O1 format, O2 mount, O3 namespace, O5 sync, O6 truncate, O8 btree, O9 relocation, O10 gc, O12 gbmap, O13 grow, O14 check | – | | N/A: outside this combination |
| O4 write, O7 mdir, O11 pre-erase | N | NEW-60 | A, with the prog-once check |

#### F27. Transient read error

Injection: NEW emubd hook: a read returns `LFS3_ERR_CORRUPT` once, then
succeeds.

| Operations | Coverage | Tests | Recovered means |
|---|---|---|---|
| O1 format, O2 mount, O3 namespace, O4 write, O5 sync, O6 truncate, O8 btree, O10 gc, O11 pre-erase, O12 gbmap, O13 grow, O14 check | – | | N/A: outside this combination |
| O7 mdir, O9 relocation | N | NEW-129 | L |

## 5. Configuration matrix

### 5.1 Axes

| Axis | Values | Count |
|---|---|---|
| Build | B-DEF, B-BIG, B-YGB, B-RO, B-NA, B-NB, B-NS, B-NM, B-TS, and B-YES-x for x in RDONLY, REVPERTURB, REVNOISE, CKPROGS, CKFETCHES, CKMETAPARITY, CKDATACKSUMS, GC, GBMAP, BLEAFCACHE, FLUSH, SYNC | 21 |
| Compiler | GCC (Linux), clang (macOS, through `.local/cc`, and Linux) | 2 |
| Architecture | A-64LE (x86_64 and arm64), A-32LE (thumb, qemu-arm), A-32BE (mips and powerpc, qemu) | 4 targets |
| Geometry | G-NOR (default), G-EEPROM, G-P16, G-EMMC, G-NAND, G-BIGNAND; each with `ERASE_VALUE` 0xff, 0x00, -1 | 18 |
| Power-loss schedule | `-Pnone`, `-Plinear`, `-Plog`, `-P'permute(1)'`, `-P'permute(2)'`, `-Pexhaustive` | 6 |
| Power-loss behaviour | 0 ATOMIC, 1 SOMEBITS, 2 MOSTBITS, 3 OOO, 4 METASTABLE | 5 |
| Checker | none, ASan + UBSan, valgrind, glibc FORTIFY | 4 |

The full product is 21 × 2 × 4 × 18 × 6 × 5 × 4 = 362,880 combinations,
far too many to run. The jobs below pick the combinations that find
distinct defects: every build once in the default geometry, every
architecture in the builds that change byte order or word size, every
geometry in the two builds that compile all tests, and every power-loss
behaviour on every reentrant case. The rule for what is not crossed is in
5.4.

### 5.2 Measured run times

All on Apple silicon with 14 jobs (`-j14`), from the runs recorded in
REQUIREMENTS.md B.3 and `.local/*/test.log`:

| Run | Permutations | Power losses | Wall time | CPU time |
|---|---|---|---|---|
| B-DEF, `-Pnone -Plinear`, macOS clang | 634,616 | 2,226,875 | 134 s (baseline) to 263 s (latest) | |
| B-DEF, Docker Ubuntu 24.04 GCC 13 | 634,616 | 2,226,886 | 509 s | 2,449 s |
| B-BIG, macOS clang | 1,083,265 | 2,181,736 | 1,181 s | 5,808 s |
| B-BIG, ASan + UBSan, macOS clang | 1,083,265 | 2,498,824 | 3,202 s | 26,386 s |

In B-BIG the slowest suites are `test_gc` (1,178 CPU-s), `test_rbyd` (837),
`test_ck` (526) and `test_powerloss` (479). Under the sanitizers
`test_gc` (7,623) and `test_rbyd` (6,554) dominate. Sharding a job by suite
across CI machines therefore needs `test_gc` and `test_rbyd` on their own
shards.

Times marked "est." below are estimates from these figures, not
measurements; the first run of each job replaces them (section 8.5).
Estimates for qemu-user assume 5 to 20 times native speed; for valgrind, 20
to 50 times on the `-Pnone` part.

### 5.3 Jobs and tiers

**PR** runs on every push and pull request and must finish in about 30
minutes on the runners available (shard where needed). **Nightly** runs on
a schedule on the test branch. **Release** runs before the v3-beta tag and
before every release.

| Job | Build | Arch | Geometry | Schedule and behaviours | Checker | Tier | Time on 14 cores |
|---|---|---|---|---|---|---|---|
| J-DEF | B-DEF | A-64LE, GCC and clang | G-NOR | `-Pnone -Plinear`, behaviour 0 | – | PR | 263 s (GCC Docker: 509 s) |
| J-BIG | B-BIG | A-64LE, GCC and clang | G-NOR | `-Pnone -Plinear`, 0 | – | PR | 1,181 s (shard by suite) |
| J-YGB | B-YGB | A-64LE | G-NOR | `-Pnone -Plinear`, 0 | – | PR | est. 300-400 s |
| J-NA | B-NA | A-64LE | G-NOR | `-Pnone -Plinear`, 0 | – | PR | est. 250 s |
| J-BUILD | every build of 5.1, plus the combinations of LFS3-BUILD-04, BUILD-05, BUILD-06, BUILD-20 (NEW-53) | A-64LE, thumb | – | compile only, `-Werror` | – | PR | est. 5 min |
| J-RO | B-RO and B-YES-RDONLY reading images written by B-DEF and B-YGB (NEW-46) | A-64LE | G-NOR | `-Pnone` | – | PR | est. 1 min |
| J-SIZE | B-DEF, B-RO, B-YGB, B-BIG with `LFS3_NO_LOG -DLFS3_NO_ASSERT` (NEW-118) | thumb | – | – | – | PR | est. 2 min |
| J-TOOLS | `make test`, `make bench`, `test.py -j` on Linux and macOS (LFS3-CI-08) | A-64LE | – | smoke | – | PR | est. 5 min |
| J-ARCH | B-DEF | A-32LE, A-32BE (mips, powerpc) | G-NOR | `-Pnone` on PR; `-Pnone -Plinear` nightly | – | PR, nightly | est. 22-90 min per target for the full run |
| J-SAN | B-DEF and B-BIG | A-64LE | G-NOR | `-Pnone -Plinear` (ASan, UBSan, FORTIFY); `-Pnone` (valgrind) | ASan + UBSan, FORTIFY (GCC), valgrind | nightly | 3,202 s for B-BIG ASan; est. 1-3 h valgrind |
| J-PL | B-DEF and B-BIG | A-64LE | G-NOR | every reentrant case with `-Plinear` and behaviours 1, 2, 3; NEW-08 and the new reentrant cases with behaviour 4; `-P'permute(1)'` on every reentrant case | – | nightly | est. 15-40 min per behaviour; permute(1) est. as `-Plinear` |
| J-GEO | B-DEF and B-BIG | A-64LE | G-ALL, `ERASE_VALUE=0xff` | `-Pnone -Plinear`, 0 | – | nightly | est. 6 × (263 + 1,181) s ≈ 2.4 h |
| J-DEFINES | B-DEF | A-64LE | G-NOR | `-Pnone -Plinear` with the define matrices of ALLOC-06, CFG-10, CFG-11, CFG-12, FILE-26 on the affected suites | – | nightly | est. 30-60 min |
| J-YES | each B-YES-x except RDONLY (J-RO) and GBMAP (J-YGB) | A-64LE | G-NOR | `-Pnone -Plinear`, 0 | – | nightly | est. 10 × 300-1,200 s |
| J-NBNS | B-NB, B-NS, B-NM (with the static-buffer runner of NEW-87), B-TS | A-64LE | G-NOR | `-Pnone -Plinear`, 0 | – | nightly | est. 4 × 263 s |
| J-COV | B-DEF and B-BIG with `COVGEN=1`, reported separately | A-64LE, GCC | G-NOR | `-Pnone -Plinear` | – | nightly | est. 1.5 × J-DEF + 1.5 × J-BIG |
| J-BENCH | `make bench` (NOR and NAND models), NEW-119 to NEW-123 | A-64LE | bench geometries, G-W25Q128 | – | – | nightly | est. 1-2 h (each wt/rt bench simulates 1 h) |
| J-PL-DEEP | B-DEF and B-BIG | A-64LE | G-NOR | `-P'permute(2)'` on `test_dirs`, `test_relocations`, `test_powerloss` and the new reentrant cases; `-Plog` on every reentrant case; `-Pexhaustive` on cases with fewer than about 30 writes | – | release | est. hours; measure first, bound with `if` |
| J-GEO-EV | B-DEF and B-BIG | A-64LE | G-ALL with `ERASE_VALUE` 0x00 and -1 | `-Pnone -Plinear` | – | release | est. 2 × J-GEO |
| J-ARCH-BIG | B-BIG | A-32LE, A-32BE | G-NOR | `-Pnone -Plinear` | – | release | est. 1.5-6 h per target |
| J-COMPAT | B-DEF against each earlier v3-beta and v3 release linked as `LFSP` (LFS3-MOUNT-26, NEW-79) | A-64LE | G-NOR | `-Pnone` | – | release (from the first v3-beta) | est. 5 min |

The PR tier has 9 jobs, the nightly tier 8 more (plus the full J-ARCH run),
and the release tier 4 more. Every build appears in at least one job:
J-BUILD compiles all 21, the suite runs in 18, and B-RO and B-YES-RDONLY are
exercised through the image harness of J-RO. Every
architecture in J-ARCH, every geometry and erase value in J-GEO and
J-GEO-EV, every schedule in J-DEF, J-PL and J-PL-DEEP, every behaviour in
J-PL, and every checker in J-SAN.

### 5.4 What is not crossed, and why

- **Geometries in every build.** Only B-DEF and B-BIG run G-ALL. The other
  builds change code paths that do not depend on the block geometry.
- **Torn behaviours in every build.** J-PL runs behaviours 1 to 3 in B-DEF and
  B-BIG only. Every other build runs ATOMIC.
- **METASTABLE on existing cases.** Existing reentrant cases assert exact
  success after every remount. In the default mode METASTABLE does not
  promise that (LFS3-DEG-12 leaves residual cases), so behaviour 4 runs
  only on cases written for criterion B (NEW-08, NEW-130, the METASTABLE
  permutations of NEW-01), and on NEW-05, NEW-06, NEW-11 and
  `dirs::rm_many_2layers` mounted with `LFS3_M_SETTLE`, where criterion A
  holds.
- **valgrind under power loss.** Power-loss `longjmp`s leak the test's own
  allocations, so valgrind runs `-Pnone` only. ASan and UBSan run the
  power-loss schedule.
- **Architectures under deep schedules.** qemu-user is 5 to 20 times slower;
  A-32LE and A-32BE run `-Pnone -Plinear` only.
- **Bad-block behaviours 2 to 4 without `LFS3_CKPROGS`.** The existing cases
  exclude them because silent failures cannot be relocated without read-back.
  NEW-14 covers them with the detection criterion D instead.

## 6. New test specifications

### 6.1 Priorities and conventions

- **P0** tests are needed for "done" (section 7): recovery from every flash
  failure in the matrix of section 4, and a regression test for every known
  defect that has a behavioural requirement. There are 54.
- **P1** tests cover the remaining features and failure modes, including
  behaviours whose intent is still an open question in REQUIREMENTS.md
  section 8. There are 37. They are also required for "done" (section 7),
  except where a test waits on an open question.
- **P2** tests are lower-value checks, benches and reports. There are 38.
  They are tracked but not required for "done".

Conventions used by the specifications:

- **File and case.** `tests/test_<suite>.toml`, `[cases.test_<suite>_<case>]`.
  Cases that need static functions set `in = 'lfs3.c'`. Reentrant cases set
  `reentrant = true` and follow the idiom of `tests/test_powerloss.toml:34-52`:
  mount, and format if the mount fails, so that every re-entry continues on
  the same disk.
- **Step attribute.** Reentrant file workloads must know, after a power loss,
  which of their syncs completed. They store a step counter as a
  file-attached attribute (`lfs3_file_cfg.attrs`, `LFS3_A_WRONLY`), which is
  committed in the same commit as the file's data (LFS3-ATTR-09). The
  expected content of a file is a pure function of the seed and the step, so
  it can be rebuilt after any power loss. This also exercises attribute
  atomicity.
- **Standard check.** At the end of each entry, and after every remount:
  every file and directory matches the model, and
  `lfs3_fs_ck(&lfs3, LFS3_CK_CKMETA | LFS3_CK_CKDATA)` returns 0.
- **Counters.** `lfs3_emubd_progs`, `lfs3_emubd_erases`, `lfs3_emubd_reads`
  give device totals. Per-block erase counts come from `lfs3_emubd_wear` with
  `ERASE_CYCLES=0xffffffff`, which counts erases without making blocks bad.
  Per-block program counts need extension E-1.
- **Builds.** "B-DEF" cases must not be compiled out of the default build;
  "B-BIG" cases gate with `ifdef` or `LFS3_IFDEF_*` like the existing ones.
- **Branches.** A test marked "written on <branch>" exists on the fork and
  needs only to be merged.

Each specification gives: file and case; requirements and matrix cells;
defines; procedure; pass and fail; extension needed.

### 6.2 P0: recovery from flash failures

#### NEW-01 `powerloss::format_pl`

- **File and case:** `tests/test_powerloss.toml`, `test_powerloss_format_pl`,
  reentrant.
- **Covers:** PL-22. Matrix F1 to F5 × O1.
- **Defines:** `POWERLOSS_BEHAVIOR` 0 to 4; `GBMAP` false and true (true in
  B-YGB and B-BIG).
- **Procedure:** On entry, try `lfs3_mount`. If it fails, or a file "done" is
  missing, call `lfs3_format` (with `LFS3_F_GBMAP` when `GBMAP`) and
  `lfs3_mount`. Then create 8 files with `lfs3_set`, write "done", unmount.
- **Pass:** after every power loss, `lfs3_format` and the following
  `lfs3_mount` return 0 (A1); at the end the 8 files and "done" read back and
  the standard check passes. With behaviour 4, a mount before the reformat
  may return `LFS3_ERR_CORRUPT` (B).
- **Fail:** a format or a mount after format fails; any assert.
- **Extension:** none.

#### NEW-02 `powerloss::truncate_pl_fuzz`

- **File and case:** `tests/test_powerloss.toml`,
  `test_powerloss_truncate_pl_fuzz`, reentrant, fuzz.
- **Covers:** PL-13. Matrix F1 to F5 × O6.
- **Defines:** `POWERLOSS_BEHAVIOR` 0 to 3 (4 accepted under criterion B);
  `N` (files) 1 and 4; `SIZE` up to `4*BLOCK_SIZE`; `SEED` range(10);
  `PROG_SIZE` 1 and 16.
- **Procedure:** For each step: pick a file and one of append, truncate
  (shrink or grow), fruncate (shrink or grow), or the log pattern of #1111
  (append then `lfs3_file_fruncate(file, LIMIT)`); then `lfs3_file_sync`. The
  step counter is a file-attached attribute. After each power loss, rebuild
  each file's expected content from its step and compare.
- **Pass:** every file equals its expected content at the step stored with
  it (A); the standard check passes.
- **Fail:** any other content or size; a mount failure; an assert.
- **Extension:** none.

#### NEW-03 `powerloss::fwrite_pl_fuzz`

- **File and case:** `tests/test_powerloss.toml`,
  `test_powerloss_fwrite_pl_fuzz`, reentrant, fuzz.
- **Covers:** PL-14. Matrix F1 to F4 × O4 and O8.
- **Defines:** `POWERLOSS_BEHAVIOR` 0 to 3; `SHRUB_SIZE` 0 and
  `BLOCK_SIZE/4`; `FRAGMENT_SIZE` default and 16; `PROG_SIZE` 1 and 16;
  `SIZE` `BLOCK_SIZE/2` and `4*BLOCK_SIZE`; `SYNC_EVERY` 1 and 8; `SEED`
  range(10).
- **Procedure:** Random writes at random offsets, including beyond the end of
  file (holes) and overwrites of fragments and blocks; `lfs3_file_sync` every
  `SYNC_EVERY` writes, recording the step in the file's attribute.
- **Pass:** after every remount each file equals its expected content at the
  stored step (A); the standard check passes.
- **Fail:** any other content; an assert.
- **Extension:** none.

#### NEW-04 `powerloss::append_pl`

- **File and case:** `tests/test_powerloss.toml`, `test_powerloss_append_pl`,
  reentrant.
- **Covers:** PL-15. Matrix F1 to F4 × O4.
- **Defines:** `POWERLOSS_BEHAVIOR` 0 to 3; `PROG_SIZE` 1, 16 and 256;
  `CHUNK` 16 and 64; `FILES` 1 and 2 (two files appended in turn);
  `CKPROGONCE` true.
- **Procedure:** Append `CHUNK` bytes and sync, repeatedly, so that appends
  resume in partially programmed data blocks (`lfs3.c:13715-13756`). The
  synced length is the step attribute.
- **Pass:** after every remount, each file is exactly its first
  `step × CHUNK` bytes (A); the prog-once check never fires.
- **Fail:** a synced byte differs; a byte is programmed twice.
- **Extension:** E-1 (prog-once).

#### NEW-05 `powerloss::gc_pl_fuzz`

- **File and case:** `tests/test_powerloss.toml`, `test_powerloss_gc_pl_fuzz`,
  reentrant, fuzz. B-BIG for the `lfs3_fs_gc` steps.
- **Covers:** PL-20. Matrix F1 to F5 × O2 and O10, F20.
- **Defines:** `POWERLOSS_BEHAVIOR` 0 to 4; `GBMAP` false and true;
  `GC_STEPS` 1 and -1; `GC_COMPACT_THRESH` 0 and `BLOCK_SIZE/2`; `SEED`
  range(10).
- **Procedure:** A workload that leaves work pending (orphaned stickynotes
  from desynced handles, pending removes, full metadata logs, a drained
  lookahead). Between operations call, in a seeded order:
  `lfs3_fs_gc`, `lfs3_fs_mkconsistent`, `lfs3_fs_ck` with each work flag, a
  read-write traversal with `LFS3_T_COMPACT | LFS3_T_MKCONSISTENT |
  LFS3_T_LOOKAHEAD`, and a remount with each `LFS3_M_*` work flag.
- **Pass:** A for behaviours 0 to 3, and for 4 when every mount adds
  `LFS3_M_SETTLE` (DEG-13).
- **Fail:** as A.
- **Extension:** none.

#### NEW-06 `powerloss::gbmap_pl`

- **File and case:** `tests/test_powerloss.toml`, `test_powerloss_gbmap_pl`,
  reentrant, internal (`in = 'lfs3.c'`). Gated on `LFS3_GBMAP` and not
  `LFS3_YES_GBMAP`.
- **Covers:** PL-19, ALLOC-12. Matrix F1 to F5 × O12.
- **Defines:** `POWERLOSS_BEHAVIOR` 0 to 4; `SEED` range(10).
- **Procedure:** Alternate `lfs3_fs_mkgbmap` and `lfs3_fs_rmgbmap` with file
  writes and removes. After each remount, compare `LFS3_I_GBMAP` with the
  last call that returned, and run an internal check: traverse the
  filesystem and assert that no referenced block lies in a BMFREE or BMERASED
  range inside the gbmap's known window.
- **Pass:** the flag is consistent, the internal check holds, the standard
  check passes (A; for behaviour 4 every mount adds `LFS3_M_SETTLE`).
- **Fail:** a mount failure, a referenced block recorded free, a check
  failure.
- **Extension:** none.

#### NEW-07 `mtree::split_pl`, `drop_pl`, `relocate_pl`, `extend_pl`

- **File and case:** `tests/test_mtree.toml`, four reentrant internal cases,
  each a reentrant form of the existing `mtree::split_fuzz`,
  `mtree::drop_fuzz`, `mtree::relocate_fuzz` and `mtree::extend*`.
- **Covers:** META-15. Matrix F1 to F4 × O7, F20 × O7.
- **Defines:** `POWERLOSS_BEHAVIOR` 0 to 3; `BLOCK_RECYCLES` 0 and 1 (for
  relocate and extend); the existing size and seed defines.
- **Procedure:** As the existing cases, with the model kept in a reserved
  attribute on the root (committed atomically with each change) so that it
  survives power loss.
- **Pass:** A; every handle opened before the change still reads its entry.
- **Fail:** as A.
- **Extension:** none.

#### NEW-08 `powerloss::metastable`

- **File and case:** `tests/test_powerloss.toml`,
  `test_powerloss_metastable`, reentrant, fuzz. B-BIG.
- **Covers:** PL-03, DEG-01, DEG-12, DEG-13, DEG-14. Matrix F5 × O3 to O9
  and O13.
- **Defines:** `POWERLOSS_BEHAVIOR` 4; `MODE` 0 (default) and 1
  (`LFS3_M_SETTLE`); `BLOCK_RECYCLES` -1 and 1; `SEED` range(20).
- **Procedure:** Mount with `LFS3_M_CKMETAPARITY | LFS3_M_CKDATACKSUMS`. Run
  mkdir, remove, rename, writes, syncs, truncate, fruncate and
  `lfs3_fs_grow` by one block, with step attributes. After each power loss,
  if `lfs3_mount` returns `LFS3_ERR_CORRUPT`, count it and reformat. Read
  every file; a read may return `LFS3_ERR_CORRUPT`. At each power loss the
  case records which operation was running in the block emubd left
  metastable (`lfs3_emubd_metastable`), so it can tell a residual case of
  LFS3-DEG-12 from a protected one.
- **Pass:** no read returns data other than the expected content at the
  stored step or the step in progress (B); no completed sync is lost with
  `MODE` 1, and with `MODE` 0 none while every block still metastable was
  left by a protected operation; the residual losses are counted and
  printed; no assert.
- **Fail:** wrong data without an error; a completed sync lost outside the
  residual cases; an assert.
- **Extension:** E-10 (`lfs3_emubd_metastable`).

#### NEW-130 `powerloss::metastable_builton`

- **File and case:** `tests/test_powerloss.toml`,
  `test_powerloss_metastable_builton`. B-DEF and B-BIG.
- **Covers:** DEG-01, DEG-12, DEG-13, DEG-14.
- **Defines:** `MODE` 0 and 1; `KIND` 0 (`lfs3_set`, a residual case in
  the default mode) and 1 (a sync inside an open write session); `SEED`
  range(8).
- **Procedure:** On its own METASTABLE emubd, count the progs of the
  commit, then repeat it with a power loss at its last prog, which leaves a
  bit of that prog metastable until the block is erased. Mount until the
  commit reads as whole, commit `b` with `lfs3_set` (a sync that returns 0),
  unmount, then mount 64 more times and look for `b`.
- **Pass:** `b` is never missing, except with `MODE` 0 and `KIND` 0, where
  the losses are counted and printed.
- **Fail:** `b` missing in any other permutation.
- **Extension:** none.

#### NEW-131 `powerloss::dirty_mark`

- **File and case:** `tests/test_powerloss.toml`, `test_powerloss_dirty_mark`,
  internal (`in = 'lfs3.c'`).
- **Covers:** DEG-11, DEG-12.
- **Defines:** `SHAPE` 0 (inlined), 1 (bshrub), 2 (btree); `SYNCS` 0, 1, 3.
- **Procedure:** Open a file for writing, write and sync `SYNCS` times,
  close. After each step, look up the file's `DIRTY` tag on disk. Count
  commits (`cfg->sync` calls) and erases of the session against the same
  session with marks compiled out of the count. Then repeat without the
  close, drop the `lfs3_t` as a power loss would, remount with emubd
  counters, and check the repair, then the first `lfs3_fs_mkconsistent`.
  Finally unmount and mount once more.
- **Pass:** the first commit carries the mark and later syncs keep it;
  close clears it with at most one extra commit; `SYNCS` 0 and `lfs3_set`
  never mark; the mount after the dropped session rewrites the file's pair
  and the pairs on its path once; the first mkconsistent removes the stale
  mark; the last mount, after a clean shutdown, programs and erases
  nothing.
- **Fail:** any other mark state, an extra erase in a session, or a write
  at a clean mount.
- **Extension:** none.

#### NEW-132 `ck::crystallize_flipped`

- **File and case:** `tests/test_ck.toml`, `test_ck_crystallize_flipped`.
  B-DEF and B-BIG.
- **Covers:** DEG-15.
- **Defines:** `SIZE` so that the file's last block is partly full;
  `BIT` range(8).
- **Procedure:** Write and close a file whose last block is partly full,
  remount, flip a bit inside that block's checksummed range with
  `lfs3_emubd_flipbit`, then append enough to crystallize a new block.
- **Pass:** the append returns `LFS3_ERR_CORRUPT`, without
  `LFS3_M_CKDATACKSUMS`.
- **Fail:** the append succeeds and the flip is copied under the new
  block's checksum.
- **Extension:** none.

#### NEW-09 `badblocks::region_pl_fuzz`, `badblocks::alternating_pl_fuzz`

- **File and case:** `tests/test_badblocks.toml`, two reentrant fuzz cases
  derived from `badblocks::region_spam_file_fuzz` and
  `badblocks::alternating_spam_dir_fuzz`.
- **Covers:** PL-25. Matrix F17.
- **Defines:** `POWERLOSS_BEHAVIOR` 0 to 3; `BADBLOCK_BEHAVIOR` 0 and 1 in
  B-DEF, 2 to 4 with `CKPROGS=true` in B-BIG; `SEED` range(10).
- **Procedure:** On the first entry only, mark the region (or every other
  block) bad; emubd keeps the marks across power losses. Run the file and
  directory workloads with step attributes. A variant marks the block holding
  a file's newest data bad at a seeded step, so that relocation runs mid
  workload.
- **Pass:** A after every power loss, and C for the operations that follow.
- **Fail:** as A or C.
- **Extension:** none.

#### NEW-10 `exhaustion::spam_file_pl_fuzz`

- **File and case:** `tests/test_exhaustion.toml`,
  `test_exhaustion_spam_file_pl_fuzz`, reentrant, fuzz.
- **Covers:** PL-26. Matrix F19.
- **Defines:** `POWERLOSS_BEHAVIOR` 0 to 3; `ERASE_CYCLES` 10;
  `BADBLOCK_BEHAVIOR` 0 and 1; `BLOCK_RECYCLES` 4; `SEED` range(5).
- **Procedure:** Rewrite files until `LFS3_ERR_NOSPC`, with power losses
  throughout. After the first `LFS3_ERR_NOSPC`, remount read-only and read
  everything.
- **Pass:** A until end of life, then G.
- **Fail:** any error other than `LFS3_ERR_NOSPC` at end of life; data loss.
- **Extension:** none.

#### NEW-11 `powerloss::preerase_pl_fuzz`

- **File and case:** `tests/test_powerloss.toml`,
  `test_powerloss_preerase_pl_fuzz`, reentrant, fuzz. B-BIG.
- **Covers:** PRE-05, PRE-07. Matrix F1 to F5 × O11, F21.
- **Defines:** `POWERLOSS_BEHAVIOR` 0 to 4; `GC_PREERASE_COUNT` 4 and -1;
  `ERASE_VALUE` 0xff, 0x00 and -1; `CKPROGONCE` true; `SEED` range(10).
- **Procedure:** Mount with `LFS3_M_REVPERTURB`, gbmap enabled. Between
  writes call `lfs3_fs_gc` with `LFS3_GC_PREERASE` (one step). Writes include
  whole blocks, so that pre-erased blocks are claimed for data, and small
  appends, so that metadata uses them.
- **Pass:** A (for behaviour 4 every mount adds `LFS3_M_SETTLE`), and the
  prog-once check never fires: no
  block is programmed without an erase unless its erased-state checksum
  matched. An internal check at each remount: the on-disk gbmap window does
  not include a block that holds data.
- **Fail:** the prog-once check fires; A or B fails.
- **Extension:** E-1.

#### NEW-12 emubd prog-once check, all suites

- **What:** extension E-1 plus a define `CKPROGONCE` (default false) wired
  into `runners/test_defines.h`.
- **Covers:** CFG-15; strengthens NEW-04, NEW-11, NEW-23, NEW-24, NEW-60.
- **Procedure:** Run every suite with `-DCKPROGONCE=true` in B-DEF and B-BIG,
  excluding `BADBLOCK_BEHAVIOR` 3 and 4 (which break the device model on
  purpose).
- **Pass:** the check never fires.
- **Fail:** any byte programmed twice without an erase.
- **Extension:** E-1.

#### NEW-13 `badblocks::live_readerror`

- **File and case:** `tests/test_badblocks.toml`,
  `test_badblocks_live_readerror`.
- **Covers:** FAIL-04. Matrix F8 × O2, O4, O10, O14; F23 × O10.
- **Defines:** `TARGET` mdir (non-anchor), file B-tree node, data block,
  gbmap node (B-YGB); `BADBLOCK_BEHAVIOR` 2.
- **Procedure:** Build a filesystem of 16 directories and 32 files. Record
  every referenced block (traversal). Mark one block of the target kind
  READERROR. Then remount; read every file; write new files; run
  `lfs3_fs_ck`.
- **Pass:** D. Precisely: for an mdir, mount or the first access to its
  entries returns `LFS3_ERR_CORRUPT`; for a data block, reads of that file
  return `LFS3_ERR_CORRUPT` and other files read correctly; for a B-tree or
  gbmap node, writes that need an allocation scan return
  `LFS3_ERR_CORRUPT`; no block in the recorded set is programmed or erased
  (counters); a remount with `LFS3_M_RDONLY` reads files whose blocks are
  good.
- **Fail:** wrong data, a write to a recorded block, an assert.
- **Extension:** none.

#### NEW-14 `badblocks::silent_nockprogs`

- **File and case:** `tests/test_badblocks.toml`,
  `test_badblocks_silent_nockprogs`, fuzz.
- **Covers:** FAIL-07. Matrix F9, F10 × O3 to O8.
- **Defines:** `BADBLOCK_BEHAVIOR` 3 and 4; `CKPROGS` false; `SEED`
  range(20).
- **Procedure:** The `spam_file` workload with a history of every committed
  state per file. Remount after every 10 operations.
- **Pass:** every read returns a committed state of that file, or
  `LFS3_ERR_CORRUPT` (D); no assert.
- **Fail:** data that no sync committed; an assert.
- **Extension:** none.

#### NEW-15 `exhaustion::readback`

- **File and case:** `tests/test_exhaustion.toml`,
  `test_exhaustion_readback`, fuzz.
- **Covers:** FAIL-12. Matrix F15 × O2, O6, O10, O11.
- **Defines:** `ERASE_CYCLES` 10; `BADBLOCK_BEHAVIOR` 0 and 1 (2 to 4 with
  `CKPROGS` in B-BIG); `GC` steps on (B-BIG); `PREERASE` true and false
  (B-BIG).
- **Procedure:** The `spam_fwrite` workload extended with fruncate,
  `lfs3_fs_gc` steps and pre-erase, until the first `LFS3_ERR_NOSPC`. Save
  the model, unmount, mount with `LFS3_M_RDONLY`, read everything.
- **Pass:** G.
- **Fail:** another error at end of life; a read-only mount failure; a
  synced file missing or different.
- **Extension:** none.

#### NEW-16 `badblocks::gbmap_format`

- **File and case:** `tests/test_badblocks.toml`,
  `test_badblocks_gbmap_format`. B-YGB and B-BIG.
- **Covers:** FAIL-16. Matrix F6 to F10 × O1 with the gbmap.
- **Defines:** `BADBLOCK_BEHAVIOR` 0 to 4 (2 to 4 with `CKPROGS`); bad block
  2.
- **Procedure:** Mark block 2 bad, format with `LFS3_F_GBMAP`, mount.
- **Pass:** E: format returns an error and the mount fails cleanly. When
  LFS3-BAD-14 lands, format must instead succeed with the gbmap root on
  another block; update the case then.
- **Fail:** format returns 0 and the mount fails or the filesystem is
  inconsistent.
- **Extension:** none.

#### NEW-17 `badblocks::source_readerror`

- **File and case:** `tests/test_badblocks.toml`,
  `test_badblocks_source_readerror`.
- **Covers:** FAIL-17. Matrix F8 × O7, O8, O9.
- **Defines:** `SOURCE` mdir compaction, B-tree node relocation, data-block
  rewrite; `PROG_SIZE` 1 and 16.
- **Procedure:** Fill an mdir until the next commit compacts it (or a B-tree
  node until it splits, or a data block until crystallization copies it);
  mark the source block READERROR; perform the commit.
- **Pass:** the call returns `LFS3_ERR_CORRUPT`; the erase counter grows by
  at most 1 during the call (D).
- **Fail:** `LFS3_ERR_NOSPC`, or more than one erase (3-alloc B5).
- **Extension:** none.

#### NEW-18 `badblocks::badsync`, `badblocks::mrootanchor_stuck`

- **File and case:** `tests/test_badblocks.toml`. Written on `v3-fix-alloc`
  (`bd5bb8c`), with emubd `lfs3_emubd_mkbadsync` and `lfs3_emubd_mkgoodsync`.
- **Covers:** FAIL-18, META-10. Matrix F24; F6, F7 × O7 (anchor).
- **Procedure:** For each n, make the n-th `cfg->sync` of a format, mkdir,
  file sync, compaction and grow fail; then make sync good and continue.
  For the anchor case, make blocks 0 and 1 bad and force an anchor rewrite.
- **Pass:** I (the call returns `LFS3_ERR_IO`; `lfs3_fs_cksum` unchanged;
  later operations succeed; the remount and the standard check pass); F for
  the anchor.
- **Fail:** a gcksum mismatch at the next mount; a phantom remove.
- **Extension:** E-3 (exists on `v3-fix-alloc`).

#### NEW-19 `badblocks::ioerror`

- **File and case:** `tests/test_badblocks.toml`, `test_badblocks_ioerror`.
- **Covers:** FAIL-19. Matrix F25.
- **Defines:** `OP` read, prog, erase; `N` from 1 to the number of such
  operations in the workload.
- **Procedure:** Make the N-th operation of kind `OP` return `LFS3_ERR_IO`
  during a workload of mount, mkdir, writes, syncs and `lfs3_fs_ck`. Clear
  the fault and continue.
- **Pass:** I: the call that met the fault returns `LFS3_ERR_IO`; a file
  handle is desynced; after remount the last synced state is present and the
  standard check passes.
- **Fail:** another error, data loss, an assert.
- **Extension:** E-2.

#### NEW-20 `badblocks::resume_append`

- **File and case:** `tests/test_badblocks.toml`,
  `test_badblocks_resume_append`.
- **Covers:** FILE-11. Matrix F6, F7, F9 × O4.
- **Defines:** `BADBLOCK_BEHAVIOR` 0 and 1 (3 and 5 with `CKPROGS` in
  B-BIG); `PROG_SIZE` 1 and 16; `REOPEN` false and true.
- **Procedure:** Write 2000 bytes and sync. Find the file's last data block
  by traversal and mark it bad. Optionally close and reopen. Append 64 bytes,
  sync, remount, read.
- **Pass:** C: the file is the 2064 expected bytes.
- **Fail:** different content, an error, an assert.
- **Extension:** none.

#### NEW-21 `badblocks::fruncate_append`

- **File and case:** written on `v3-fix-files` (`b07be9e`).
- **Covers:** FILE-10. Matrix F6, F9, F11 × O6.
- **Procedure:** Write, sync, fruncate into the first block, sync, mark the
  data block bad (PROGERROR; PROGNOOP and PROGFLIP with `CKPROGS`), append,
  sync, remount, read the whole file.
- **Pass:** C. Must also pass in B-NA, where the defect corrupted silently.
- **Fail:** wrong content or size, `LFS3_ERR_CORRUPT`, an assert.
- **Extension:** none.

#### NEW-22 `badblocks::preerase`

- **File and case:** written on `v3-fix-alloc` (`3ceb48b`).
- **Covers:** PRE-06, and the "continue" half of BAD-10. Matrix F7, F8 ×
  O11; F22.
- **Procedure:** Put an ERASEERROR or READERROR block in the gbmap's known
  window; call `lfs3_fs_gc` with `LFS3_GC_PREERASE`, `lfs3_mount` with
  `LFS3_M_PREERASE`, and `lfs3_format` with `LFS3_F_PREERASE`. Add: repeated
  gc calls reach the `gc_preerase_count` target past the bad block.
- **Pass:** K: every call returns 0 and later calls make progress.
- **Fail:** any call returns the device error; gc stops at the block.
- **Extension:** none.

#### NEW-23 `gc::preerase_mismatch`

- **File and case:** `tests/test_gc.toml`, `test_gc_preerase_mismatch`.
  B-BIG.
- **Covers:** PRE-03. Matrix F14 × O11.
- **Defines:** `ERASE_VALUE` 0xff, 0x00 and -1; `PROG_SIZE` 1 and 16;
  `CKPROGONCE` true; `ERASE_CYCLES` 0xffffffff (to count erases).
- **Procedure:** Pre-erase blocks; unmount; flip one bit in the first prog
  unit of one pre-erased block; remount with `LFS3_M_REVPERTURB`; write until
  the allocator has passed that block.
- **Pass:** K: the block is not programmed before its next erase (prog-once
  check and per-block wear).
- **Fail:** the block is programmed without an erase.
- **Extension:** E-1.

#### NEW-24 `gc::preerase_norevperturb`

- **File and case:** `tests/test_gc.toml`, `test_gc_preerase_norevperturb`.
  B-BIG.
- **Covers:** PRE-04.
- **Defines:** `CKPROGONCE` true; `ERASE_CYCLES` 0xffffffff.
- **Procedure:** Pre-erase with `LFS3_M_REVPERTURB`; remount without it;
  fill the disk.
- **Pass:** every pre-erased block is erased again before it is programmed.
- **Fail:** a pre-erased block is programmed without an erase.
- **Extension:** E-1.

#### NEW-25 `badblocks::preerase_erasenoop`

- **File and case:** `tests/test_badblocks.toml`,
  `test_badblocks_preerase_erasenoop`. B-BIG.
- **Covers:** PRE-08. Matrix F10 × O11; F22.
- **Defines:** `CKPROGS` true and false.
- **Procedure:** Mark free blocks in the known window ERASENOOP; pre-erase
  them; write until they are used.
- **Pass:** C with `CKPROGS`; D without.
- **Fail:** wrong data returned without an error.
- **Extension:** none.

#### NEW-26 `badblocks::gc`

- **File and case:** `tests/test_badblocks.toml`, `test_badblocks_gc`, fuzz.
  B-BIG.
- **Covers:** FAIL-01 to FAIL-08 for janitorial work. Matrix F6 to F11 × O2,
  O10, O11 (except F7 × O11, which is NEW-22).
- **Defines:** `BADBLOCK_BEHAVIOR` 0 to 5 (2 to 5 with `CKPROGS`); the
  `every`, `region` and `alternating` families; `GBMAP` false and true;
  `SEED` range(10).
- **Procedure:** Mark blocks bad. Interleave a file workload with
  `lfs3_fs_gc` for each `LFS3_GC_*` flag, `lfs3_fs_ck` with work flags,
  read-write traversals with `LFS3_T_COMPACT | LFS3_T_MKCONSISTENT |
  LFS3_T_LOOKAHEAD`, gc pre-erase, and remounts with each `LFS3_M_*` work
  flag.
- **Pass:** C.
- **Fail:** an error while good blocks remain; wrong data.
- **Extension:** none.

#### NEW-27 `badblocks::grow`

- **File and case:** `tests/test_badblocks.toml`, `test_badblocks_grow`.
- **Covers:** MOUNT-22. Matrix F6 to F8 × O13, F23 × O13.
- **Defines:** `BADBLOCK_BEHAVIOR` 0 and 1 (2 with `CKPROGS` in B-BIG);
  `GBMAP` false and true; `ERASE_CYCLES` 0xffffffff.
- **Procedure:** Format with `FORMAT_BLOCK_COUNT` below `BLOCK_COUNT`, mark
  blocks bad (some in the new region, and the mroot's partner), call
  `lfs3_fs_grow`. When it fails, fill the disk.
- **Pass:** C when grow succeeds. When it fails: no block at or beyond the old
  count is erased or programmed afterwards (per-block wear), and the
  standard check passes.
- **Fail:** a write beyond the old count after a failed grow; an assert.
- **Extension:** none.

#### NEW-28 `badblocks::gbmap_mkrm`

- **File and case:** `tests/test_badblocks.toml`, `test_badblocks_gbmap_mkrm`.
  B-BIG with `LFS3_GBMAP` and not `LFS3_YES_GBMAP`.
- **Covers:** matrix F6 to F11 × O12, F23 × O12.
- **Defines:** `BADBLOCK_BEHAVIOR` 0 to 5 (2 to 5 with `CKPROGS`).
- **Procedure:** Alternate `lfs3_fs_mkgbmap` and `lfs3_fs_rmgbmap` with a
  file workload on a device with bad blocks.
- **Pass:** C (D for READERROR on a live gbmap node).
- **Fail:** as C or D.
- **Extension:** none.

#### NEW-29 `alloc::nospc_recover`

- **File and case:** `tests/test_alloc.toml`, `test_alloc_nospc_recover`.
- **Covers:** ALLOC-04. Matrix F16 × O2, O6.
- **Defines:** `GBMAP` false and true; `SIZE` 1 and 4 blocks per file.
- **Procedure:** Fill the disk with files until `LFS3_ERR_NOSPC`. Then:
  truncate one file to half and fruncate another to half; mount again with
  `LFS3_M_MKCONSISTENT | LFS3_M_COMPACT`; remove half the files; write a new
  file of the freed size.
- **Pass:** H: each call returns 0 or `LFS3_ERR_NOSPC` with the disk
  unchanged; the remove and the final write return 0; the standard check
  passes.
- **Fail:** the remove or the final write fails; data changes.
- **Extension:** none.

#### NEW-30 `alloc::nospc_gbmap`

- **File and case:** `tests/test_alloc.toml`, `test_alloc_nospc_gbmap`.
  B-YGB and B-BIG.
- **Covers:** ALLOC-05. Matrix F16 × O12.
- **Procedure:** Fill a gbmap filesystem until `LFS3_ERR_NOSPC`; remove a
  file; then (B-BIG) `lfs3_fs_rmgbmap` and `lfs3_fs_mkgbmap`.
- **Pass:** the remove returns 0; the gbmap calls return 0 or
  `LFS3_ERR_NOSPC` with the disk unchanged (H).
- **Fail:** the remove fails with `LFS3_ERR_NOSPC` (3-alloc B10).
- **Extension:** none.

#### NEW-31 `gc::preerase_ckpoint`

- **File and case:** `tests/test_gc.toml`, `test_gc_preerase_ckpoint`,
  internal. B-BIG.
- **Covers:** ALLOC-08. Matrix F16 × O11.
- **Procedure:** Fill the disk to within a few blocks. Remount with
  `LFS3_M_PREERASE` and without `LFS3_M_LOOKAHEAD`, `gc_preerase_count` -1.
  Write until `LFS3_ERR_NOSPC`. Assert after every allocation that
  `lfs3->lookahead.ckpoint <= lfs3->block_count`.
- **Pass:** H within `2*BLOCK_COUNT` allocations; the assertion holds.
- **Fail:** the counter wraps, or no `LFS3_ERR_NOSPC`.
- **Extension:** none.

### 6.3 P0: regressions for the known defects

Each test fails at `b10efaa` and passes once the defect is fixed. Tests
already written on the fork's branches are listed first.

| ID | Case | Covers | Status | Procedure and pass condition |
|---|---|---|---|---|
| NEW-32 | `dirs::mv_subtree` | DIR-05 | written on `v3-fix-api` (`067ebe7`) | `rename("a", "a/b")` and deeper forms return `LFS3_ERR_INVAL`; nothing changes |
| NEW-33 | `dread::seek_tell` | DIR-11 | written on `v3-fix-api` (`cc4acb9`) | save tell at every position, seek back from every position, read every remaining entry |
| NEW-34 | `fwrite::append_fbig` | FILE-03 | written on `v3-fix-files` (`5500063`) | `O_APPEND` after a rewind cannot pass `file_limit`; after a seek near the limit it writes at the end |
| NEW-35 | `files::read_big` | FILE-04 | written on `v3-fix-files` (`25cfa66`) | read with size -1 from the start, the middle and `LFS3_FILE_MAX` returns the remaining length |
| NEW-36 | `kv::set_fbig` | KV-04 | written on `v3-fix-files` (`9c7deb7`) | `lfs3_set` above `file_limit` returns `LFS3_ERR_FBIG`; existing and new files unchanged |
| NEW-37 | `mount::rdonly_nowrite` | MOUNT-17 | new; `attrs::fattr_rdonly_file` and `fsync::desync_wdrs` written on `v3-fix-files` (`b923cd8`) | on an `LFS3_M_RDONLY` mount, open `LFS3_O_RDONLY` with an `LFS3_A_RDWR` attribute, desync, sync, traverse, check: emubd prog, erase and sync counters unchanged. The branch cases fix the behaviour chosen for open question Q1 |
| NEW-38 | `badblocks::truncate_desync` | SYNC-05 | written on `v3-fix-files` (`8c5241d`) | a failed truncate or fruncate desyncs the handle; close leaves the disk unchanged |
| NEW-39 | `attrs::fattr_rdonly` with `LFS3_A_RDONLY \| LFS3_A_LAZY` | ATTR-12 | written on `v3-fix-files` (`1783b37`) | the open succeeds and the attribute is read |
| NEW-40 | `ck::ckparity_btree_append` | INT-23 | written on `v3-fix-parity` (`f90e132`) | appends to B-tree files after remounts, with and without the gbmap, under `LFS3_M_CKMETAPARITY`; plus `mount::flags` and `mount::format_flags` in B-BIG |
| NEW-41 | `mtree::commit_too_big` | META-03, DIR-02, ATTR-05 | new | `ERASE_SIZE` 512, 1024 and 4096; `lfs3_mkdir`, create and rename with names of 1 to `name_limit` bytes; `lfs3_setattr` and file attributes of 0 to `block_size` bytes. Every call returns 0 or an `lfs3.h` error, never asserts; the standard check passes. Tighten the accepted error codes once open question Q3 is decided |
| NEW-42 | `mtree::fetch_revorder` | META-04 | new, internal | write an mdir pair with revision counts 0x0f80006d and 0x1000006d (and a pair across 0xffffffff); fetch selects the newer. Also runs on A-32BE in J-ARCH |
| NEW-43 | `gbmap::consume_readerror` | META-11 | new, B-YGB | READERROR on an mdir block that carries a GBMAPDELTA; mount returns `LFS3_ERR_CORRUPT`; no sanitizer report |
| NEW-44 | `mount::format_tiny` | MOUNT-03 | new, death test | `block_count` 1 (2 with `LFS3_F_GBMAP`): format is refused (assert or `LFS3_ERR_INVAL`, per Q14) before any erase or prog |
| NEW-45 | `mount::incompat_unknown_config_range` | MOUNT-09 | new, internal | commit config tags 0x0100, 0x0130, 0x0132, 0x0133, 0x013b, 0x0142, 0x01ff; each mount, read-write and read-only, returns `LFS3_ERR_NOTSUP` |
| NEW-46 | read-only image harness | MOUNT-14, MOUNT-19, BUILD-19 | new | extension E-8. B-DEF and B-YGB write images of the `files`, `dirs` and `attrs` workloads; B-RO and B-YES-RDONLY mount them with a zeroed and a 0xff-filled `lfs3_t`, read everything, and find `LFS3_I_RDONLY` set |
| NEW-47 | `grow::beyond` | MOUNT-21, GEN-08 | new | `lfs3_fs_grow` to twice `cfg->block_count` returns `LFS3_ERR_INVAL`, the block count is unchanged, filling the disk stays in range, the remount succeeds |
| NEW-48 | `mount::zeroed_cfg` | CFG-01 | new | a define set with `SHRUB_SIZE`, `FRAGMENT_SIZE`, `CRYSTAL_THRESH`, `BLOCK_RECYCLES` and the gc thresholds 0; the `files`, `fwrite` and `kv` workloads pass; each operation stays under 100 × `BLOCK_COUNT` progs (to catch the loop of 2-files B12 without a timeout) |
| NEW-49 | death-test harness; `mount::rdonly_mutate`; `gc::compact_thresh_check` | GEN-06, CFG-05 | new | extension E-6. Each mutating call on an `LFS3_M_RDONLY` mount aborts (B-DEF) or returns an error (B-NA), with prog and erase counters unchanged in both; `gc_compact_thresh` 1 and `block_size + 1` are refused in B-DEF |
| NEW-50 | `alloc::init_nomem` | RES-06 | new | extension E-7. Fail the first, second and third allocation in turn with `lfs3_t` filled with 0xab; format and mount return `LFS3_ERR_NOMEM`; ASan and valgrind clean |
| NEW-51 | `gbmap::lookgbmap_thresh` | ALLOC-11 | new, internal | `LOOKGBMAP_THRESH` 0, 1, `BLOCK_COUNT/4`: a checkpoint repopulates when known equals the threshold, and when known is 0 with threshold 0 (or the header changes; record the decision) |
| NEW-52 | `dirs::rm_many_2layers` | PL-27 | existing case | remove the `TEST_PLS && N==4` exclusion (`tests/test_dirs.toml:3442`) once the "did in the wrong mdir" bug is fixed; the case passes under `-Plinear` with PLB-TORN |
| NEW-53 | build matrix (J-BUILD) | BUILD-01 to BUILD-11, BUILD-20, INT-05 | new job | every build of 5.1 and the combinations of BUILD-04, BUILD-05, BUILD-06 and BUILD-20 compile with `-Werror` under GCC and clang (and arm-none-eabi for BUILD-11); `ck::crc32c*` pass with each crc32c option; `nm` finds no undeclared external symbol (BUILD-10) |
| NEW-54 | sanitizer job (J-SAN) | GEN-03, CI-04, CI-09 | new job | ASan and UBSan in B-DEF and B-BIG with `-Pnone -Plinear`, valgrind with `-Pnone`, GCC FORTIFY: zero reports, test code included. Fix the zero-length arrays in `alloc::nospc_*` that B.3 found |

### 6.4 Extensions to emubd and the runner

Several tests need faults or checks that emubd and the runner do not provide
at `b10efaa` (REQUIREMENTS.md 5.10).

| Ext | What | Where | Used by |
|---|---|---|---|
| E-1 | **Prog-once check.** A bitmap per block of programmed bytes, cleared by erase. A prog that touches a programmed byte fails the test with the block and offset. Selected by a `ck_progonce` field in `struct lfs3_emubd_cfg` and a `CKPROGONCE` define. Also gives per-block prog counts | `bd/lfs3_emubd.c`, `runners/test_defines.h` | NEW-04, NEW-11, NEW-12, NEW-23, NEW-24, NEW-60 |
| E-2 | **Device error hook.** `lfs3_emubd_mkioerror(cfg, op, n)`: the n-th read, prog or erase returns `LFS3_ERR_IO` (or any given code) | `bd/lfs3_emubd.c` | NEW-19 |
| E-3 | **Sync failure.** `lfs3_emubd_mkbadsync` and `lfs3_emubd_mkgoodsync`. Exists on `v3-fix-alloc` (`bd5bb8c`); extend it to fail only the n-th sync | `bd/lfs3_emubd.c` | NEW-18 |
| E-4 | **Torn tail.** A power-loss behaviour, `POWERLOSS_BEHAVIOR=5`, in which the interrupted prog leaves its first `prog_size` bytes erased and programs or disturbs some of the bytes after them (3-alloc B17) | `bd/lfs3_emubd.c` | NEW-60 |
| E-5 | **Transient read error.** `lfs3_emubd_mktransient(cfg, block, n)`: the next n reads of the block return `LFS3_ERR_CORRUPT`, then reads succeed | `bd/lfs3_emubd.c` | NEW-129 |
| E-6 | **Death tests.** A case key, for example `death = 'assertion text'`, that runs each permutation in a forked child and passes when the child aborts with that assertion. The child reports emubd counters through a pipe so the parent can check them | `scripts/test.py`, `runners/test_runner.c` | NEW-44, NEW-49, NEW-115 |
| E-7 | **Allocator hook.** Wrap `malloc` and `free` in the test runner (`-Wl,--wrap=malloc`, as `BENCH_CFLAGS` already does for heap statistics) with counting and fail-the-k-th-call modes | `Makefile`, `runners/test_runner.c` | NEW-50, NEW-72, NEW-86, NEW-87 |
| E-8 | **Read-only image harness.** The runner sets the write fields of `struct lfs3_cfg`, so it cannot build in B-RO. A small program, `runners/rdonly_runner.c`, mounts disk images written with `test.py -d` by B-DEF and B-YGB runs and compares them with a manifest written next to them | `runners/`, `Makefile` | NEW-46 |
| E-9 | **Sparse device.** A block device whose memory grows only with the blocks written, for `block_count` near 2^31 (emubd keeps an array of block pointers) | `bd/` | NEW-88 |
| E-10 | **Metastable query.** `lfs3_emubd_metastable(cfg, block)`: whether the block holds a metastable bit, so a case can tell which operation left it | `bd/lfs3_emubd.c` | NEW-08 |

### 6.5 P1: remaining features and failure modes

| ID | Case (file) | Covers | Defines and procedure | Pass (fail otherwise) | Ext |
|---|---|---|---|---|---|
| NEW-55 | `powerloss::kv_pl_fuzz` | PL-12 | reentrant; `POWERLOSS_BEHAVIOR` 0-3; `lfs3_set` of values below and above the one-commit limit, value derived from (key, step), step stored in an attribute of the key | `lfs3_get` returns the old or the new value in full (A) | – |
| NEW-56 | `powerloss::fsync_pl_fuzz` | PL-16 | reentrant form of `fsync::rwdrwd` and `fsync::*_fuzz`: several handles, desync, resync, sync | the file equals one handle's last successful sync (A) | – |
| NEW-57 | `powerloss::osync_pl` | PL-17 | reentrant; handles opened with `LFS3_O_SYNC`, and a mount with `LFS3_M_SYNC`; each returned write recorded outside the device (in a variable that survives the longjmp) | every returned write is present after remount | – |
| NEW-58 | `powerloss::flush_pl` | PL-06 | reentrant; write and flush (explicit, `LFS3_O_FLUSH`, `LFS3_M_FLUSH`) without syncing | after remount the file equals its last synced state | – |
| NEW-59 | `attrs::setattr_pl_fuzz` | PL-11 | reentrant; `lfs3_setattr` and `lfs3_removeattr` on paths and on "/" with values derived from a step | every attribute has a value that some completed call set | – |
| NEW-60 | `powerloss::tear_tail` | PRE-09 | reentrant; `POWERLOSS_BEHAVIOR` 5 (E-4); `PCACHE_SIZE` 4 × `PROG_SIZE`; workloads of NEW-04 and NEW-11; `CKPROGONCE` true | no prog to an already-programmed region; A. Settle open question Q19 before making this P0 | E-4, E-1 |
| NEW-61 | `relocations::wear_bound` | FAIL-14 | `BLOCK_RECYCLES` 0, 1, 4, 16, 100; `ERASE_CYCLES` 0xffffffff; commit repeatedly to one mdir; record the pair's blocks after each relocation | no mdir block is erased more than `block_recycles + 1` times between relocations | – |
| NEW-62 | `ck::readflip_spam` | FAIL-09, INT-19 | `ck::spam_*_fuzz` with `BADBLOCK_BEHAVIOR=6` and `CKMETAPARITY=true`, mounted with `LFS3_M_CKMETAPARITY \| LFS3_M_CKDATACKSUMS` | every read returns the model's data or `LFS3_ERR_CORRUPT` (D) | – |
| NEW-63 | `ck::launder` | candidate requirement (INT-18 to INT-20, DOC-13) | flip a bit in the source of an mdir compaction, a B-tree relocation and a crystallization, with each of `LFS3_M_CKFETCHES`, `LFS3_M_CKMETAPARITY`, `LFS3_M_CKDATACKSUMS` and none; then compact or relocate; then `lfs3_fs_ck` | with the matching check option the copy is refused with `LFS3_ERR_CORRUPT`; without, `lfs3_fs_ck` before the copy reports the flip. Records whether a flip can be copied into a fresh checksum; propose a requirement from the result | – |
| NEW-64 | `ck::ckmeta_gbmap` | INT-22 | B-YGB; flip bits in gbmap nodes; `lfs3_fs_ck(LFS3_CK_CKMETA)` | `LFS3_ERR_CORRUPT` (D) | – |
| NEW-65 | `ck::rollback` | INT-06, INT-09, INT-21 | deterministic: commit X to mdir A, Y to mdir B; restore A's older block (mount must fail); restore B's (mount succeeds, `lfs3_fs_cksum` differs); restore A after mount (`lfs3_fs_ck` fails); also namespace operations after the rollback | as the three requirements; no assert | – |
| NEW-66 | `rbyd::erased_values`, `mtree::ecksum_disturb` | INT-03, INT-04 | fill the erased region after each commit with every byte value and with random data; flip a bit in the prog unit after the last commit and commit again | no extra commit is accepted; the disturbed block is compacted, not appended | – |
| NEW-67 | `ck::file_ckdata_blocks` | INT-13 | flip bits in each data block of a B-tree file; `lfs3_file_ck(LFS3_CK_CKDATA)` and open with `LFS3_O_CKDATA` | `LFS3_ERR_CORRUPT` | – |
| NEW-68 | `mount::crafted_*` | GEN-07 | internal; checksum-valid images with an out-of-range block, offset, size, weight and alt jump, built by committing raw tags | `LFS3_ERR_CORRUPT`, no assert (B-DEF), no sanitizer report (B-NA). Waits on open question Q21 | – |
| NEW-69 | balance-check build | META-02 | build with `-DLFS3_DBGRBYDBALANCE`; run `rbyd::*`, `btree::*`, `mtree::*` | the balance check never fires | – |
| NEW-70 | `mtree::rev_wrap` | META-12 | internal; set an mdir pair's revision counts to 0xfffffffe and 0xffffffff; compact across the wrap repeatedly | the newest commit is always fetched | – |
| NEW-71 | `stickynotes::cleanup_drop` | META-14 | orphaned stickynotes as the only entries of several consecutive mdirs; `lfs3_fs_mkconsistent` | no orphan left, every other entry present | – |
| NEW-72 | `files::open_nomem` | FILE-16 | fail the file-cache allocation | `LFS3_ERR_NOMEM`, the handle not registered, unmount succeeds, no leak | E-7 |
| NEW-73 | `fwrite::filemax` | FILE-17 | sparse write of 16 bytes ending at 2^31 - 2; remount; read; fruncate to 16 | each step succeeds with the expected data | – |
| NEW-74 | `files::close_error` | FILE-21 | make the sync inside close fail with NOSPC and with a bad block | the handle is released; unmount succeeds; no leak | – |
| NEW-75 | `fsync::append_alternate` | FILE-24 | two handles of one file append 16 bytes and sync in turn; `PROG_SIZE` 1 and 16; `CKPROGONCE` true | content matches the model after each sync and after remount | E-1 |
| NEW-76 | `fwrite::file_limit` | FILE-26 | `fwrite::fbig`, `truncate_fbig`, `fruncate_fbig` with `file_limit` 1, 1000, 65536; also writes ending exactly at the limit | FBIG beyond the limit, success at it | – |
| NEW-77 | `badblocks::error_then_sync` | SYNC-09 | inject NOSPC and bad blocks into overwrites that span several entries; sync the desynced handle; remount | `lfs3_fs_ck` returns 0; bytes outside the failed range match the model | – |
| NEW-78 | `attrs::root` | ATTR-06 | set, get, size and remove attributes on "/" across a remount and an mroot chain extension | values as set | – |
| NEW-79 | `compat::gbmap_*` | ALLOC-15, BAD-05 | link builds with and without `LFS3_GBMAP` (and, later, with bad-block tracking) as `LFSP`; exchange images | the mount results of ALLOC-15; an older gbmap driver never writes a BMBAD block | – |
| NEW-80 | `gbmap::leak` | ALLOC-16 | B-YGB; rewrite a fixed file set 10,000 times | `lfs3_fs_usage` stops growing after the first 1,000 rewrites (within 2 blocks) | – |
| NEW-81 | `gc::preerase_noerase` | PRE-02 | after gc pre-erase, 100 block allocations; `ERASE_CYCLES` 0xffffffff | no pre-erased block is erased again at allocation | – |
| NEW-82 | `gc::steps_unbounded` | GC-02 | `GC_STEPS=-1` after every operation of `gc::spam_*`, through and past `LFS3_ERR_NOSPC`; bound the work with a step counter | each call returns within 10 × `BLOCK_COUNT` steps | – |
| NEW-83 | `ck::no_dangling` | GC-06 | internal; after every `lfs3_fs_ck` in `ck::*` and `gc::*`, walk `lfs3->handles` | no handle the test did not open | – |
| NEW-84 | `mount::fail_nowrite` | MOUNT-16 | every `mount::incompat_*` failure and a gcksum mismatch, with counters and valgrind | no prog, erase or leak | – |
| NEW-85 | `mount::v2_image` | MOUNT-25 | a checked-in littlefs v2.11 image fixture | mount fails with `LFS3_ERR_CORRUPT` or `LFS3_ERR_NOTSUP`; no prog or erase | – |
| NEW-86 | `fwrite::fcache_zero` | CFG-07 | `FCACHE_SIZE=0` with an allocator whose `malloc(0)` returns NULL | `files::*` and `fwrite::*` workloads pass. Waits on open question Q15 | E-7 |
| NEW-87 | `alloc::static_buffers` | RES-01, RES-08 | static `rcache_buffer`, `pcache_buffer`, `lookahead_buffer`, per-file `fcache_buffer`; an allocator that fails the test when called; then the same suites in B-NM | no allocation; one allocation of `fcache_size` per `lfs3_file_open` when buffers are not given | E-7 |
| NEW-88 | `mount::max_blocks` | RES-07 | `block_count` 2^31 - 1, 512-byte blocks, sparse device; 1,000 files across the address range | format, mount, write, remount, read all succeed | E-9 |
| NEW-89 | `threadsafe::locks` | THR-01, THR-02 | B-TS; counting `lock` and `unlock`; failing `lock` | one lock and unlock around each public call; a failing lock's error is returned with no bd operation. Waits on open question Q12 | – |
| NEW-90 | `badblocks_gbmap` suite | BAD-01 to BAD-04, BAD-06 to BAD-09, BAD-11 to BAD-15 | the tests listed in 3-alloc §8.7 and the Pass conditions of REQUIREMENTS.md 6.5 | as the requirements. Becomes P0 when bad-block tracking (release blocker #1) is merged | E-1 |
| NEW-91 | cross-endian image round trip | GEN-02 | images written by a fixed workload on A-64LE and on A-32BE (`-d` disk files), each read on the other | identical `lfs3_stat`, `lfs3_dir_read`, `lfs3_get` and `lfs3_fs_cksum` results | – |

### 6.6 P2: lower-value checks, benches and reports

| ID | Case or job | Covers | Procedure | Pass | Ext |
|---|---|---|---|---|---|
| NEW-92 | `mount::two_fs` | GEN-04 | two filesystems on two emubd instances, interleaved fuzz | both match their models and pass `lfs3_fs_ck` | – |
| NEW-93 | runner error-domain check | GEN-05 | a wrapper around every public call in the runner checks each negative result | every negative result is an `lfs3_err` or the bd's | – |
| NEW-94 | `ck::cksum_changes` | INT-08 | record `lfs3_fs_cksum` after each model-changing call over 10,000 operations | no repeated consecutive value | – |
| NEW-95 | estimate check build | META-17 | a debug option that asserts compacted size ≤ estimate, run over `mtree::*_fuzz` and `btree::*_fuzz` | the assertion holds | – |
| NEW-96 | `fwrite::zero` | FILE-02 | write with size 0 | returns 0; size, position, sync state and prog count unchanged | – |
| NEW-97 | `fwrite::hole_usage` | FILE-18 | grow by 1 MiB with truncate; write 10 bytes at 1 MiB | `lfs3_fs_usage` grows by at most 2 blocks | – |
| NEW-98 | `fsync::size_views` | FILE-19 | `lfs3_file_size` and `lfs3_stat` around a write and a sync | the handle size changes at the write, the stat size at the sync | – |
| NEW-99 | `fsync::flush_stat` | SYNC-10 | `lfs3_stat` and other handles after a flush | no change until sync | – |
| NEW-100 | `stickynotes::grm_overflow` | SYNC-16 | close three or more desynced uncreated handles | `LFS3_I_MKCONSISTENT` set; no stickynote after mkconsistent | – |
| NEW-101 | `dirs::packing` | DIR-18 | 32 empty directories on 4096-byte blocks | `lfs3_fs_usage` grows by at most 4 blocks | – |
| NEW-102 | `attrs::all_types` | ATTR-07 | distinct values for types 0x00 to 0xff | each reads back its own value | – |
| NEW-103 | `attrs::fattr_remove` | ATTR-10 | `buffer_size = LFS3_ERR_NOATTR` in `lfs3_file_cfg.attrs` | `LFS3_ERR_NOATTR` after the sync | – |
| NEW-104 | `kv::get_dir`, `kv::get_uncreat` | KV-05, KV-08 | `lfs3_get` and `lfs3_size` on a directory and on an open uncreated file | `LFS3_ERR_ISDIR`; 0 bytes and size 0 | – |
| NEW-105 | `kv::set_one_commit` | KV-09 | internal; count commits of a 16-byte new `lfs3_set` | one commit | – |
| NEW-106 | `alloc::usage` | ALLOC-17 | compare `lfs3_fs_usage` with distinct blocks from `lfs3_trv_read` | usage is not smaller | – |
| NEW-107 | `gc::steps_bound` | GC-01 | blocks touched per `lfs3_fs_gc` call for `GC_STEPS` 0, 1, 4 | at most `gc_steps` + 2 | – |
| NEW-108 | `trvs::mtreeonly` | GC-11 | block types from `LFS3_T_MTREEONLY` | only mdirs and mtree nodes | – |
| NEW-109 | `gc::compact_off` | GC-14 | `GC_COMPACT_THRESH=-1` | no compaction during gc | – |
| NEW-110 | `mount::missing_geometry`, `missing_limits` | MOUNT-11, MOUNT-13 | internal; remove the geometry tag, the limit tags | mount fails; defaults 255 and 2147483647 | – |
| NEW-111 | `mount::limits_report` | MOUNT-12 | format with `name_limit` 32 and `file_limit` 1000, mount with defaults | `lfs3_fs_stat` reports 32 and 1000 | – |
| NEW-112 | `mount::unmount_noio` | MOUNT-18 | counters around `lfs3_unmount` | no bd operation | – |
| NEW-113 | `mount::reject_v00` | MOUNT-24 | at format freeze: a v0.0 image | `LFS3_ERR_NOTSUP` | – |
| NEW-114 | `mount::format_flags_persist` | MOUNT-27 | format with every flag, mount plainly | only `LFS3_I_GBMAP` persists | – |
| NEW-115 | configuration death tests | CFG-02, CFG-03, CFG-04, CFG-06 | invalid sizes, a too-small block size, `block_recycles` -2 and 1,048,575, unknown `gc_flags` | refused before any bd operation | E-6 |
| NEW-116 | `fwrite::file_cache` | CFG-08 | per-file caches of 1, 16, 4096 bytes on `fwrite::fuzz_unaligned` | passes; no allocation with a supplied buffer | – |
| NEW-117 | build check `LFS3_NAME_MAX=1023` | CFG-09 | compile | fails with a diagnostic | – |
| NEW-118 | size report job (J-SIZE) | RES-02 to RES-05 | `make lfs3.code.csv lfs3.data.csv lfs3.stack.csv lfs3.ctx.csv lfs3.structs.csv` on thumb for B-DEF, B-RO, B-YGB, B-BIG | the difference is posted; every function has a finite stack | – |
| NEW-119 | bench ratio checks | PERF-01 to PERF-05 | assertions over `bench_rbyd`, `bench_wt`, `bench_dir` and block-size sweeps | the ratios of REQUIREMENTS.md 6.18 | – |
| NEW-120 | sync-cost bench | PERF-06, PERF-07, PERF-11 | 100 small appends and syncs at `prog_size` 1, 16, 256; count programmed bytes, erases and `cfg->sync` calls | the bounds of the three requirements | – |
| NEW-121 | W-LOG bench | PERF-08, PERF-09, PERF-10 | the workload of REQUIREMENTS.md B.1 in `benches/`, with a v2.11.3 reference build | erases per minute ≤ v2; at most one erase per call with pre-erase | – |
| NEW-122 | bench diff job | PERF-12 | `make bench-marks-diff` nightly against the base | no unexplained regression above 10% | – |
| NEW-123 | gbmap first-allocation bench | PERF-13 | reads of the first allocation after mount on 128 MiB with 10,000 files | with the gbmap ≤ 1% of without | – |
| NEW-124 | two threads under TSan | THR-03 | two filesystems on two threads, `-fsanitize=thread` | no report | – |
| NEW-125 | forced-flag builds | BUILD-12 | each B-YES-x: mount with flags 0, read `lfs3_fs_stat` | the matching `LFS3_I_*` flag is set | – |
| NEW-126 | small-limit build | BUILD-18 | `LFS3_NAME_MAX=32`, `LFS3_FILE_MAX=65535`; `paths::*`, `fwrite::*fbig` | `LFS3_ERR_NAMETOOLONG` beyond 32, `LFS3_ERR_FBIG` beyond 65535 | – |
| NEW-127 | SPEC reader cross-check | DOC-02 | a reader written from SPEC.md (an update of `scripts/dbglfs3.py`) decodes every image the suite writes (`-d`) | agreement with littlefs | – |
| NEW-128 | README example job | DOC-03 | compile the README example in CI | compiles | – |
| NEW-129 | `badblocks::transient_readerror` | DOC-12 | a revision-count read fails once during allocation or compaction, then succeeds | criterion L: no rollback. Decides whether the "read errors are persistent" assumption needs to be stated or removed | E-5 |

## 7. Entry and exit criteria

### 7.1 Entry criteria

Work on the exit run starts when all of these hold:

1. **One test branch.** A branch that merges `v3-fixes`, `v3-fix-alloc`,
   `v3-fix-api`, `v3-fix-files`, `v3-fix-parity` and `v3-ci` onto the
   upstream commit under test, and builds B-DEF and B-BIG with `-Werror`.
2. **A green baseline.** J-DEF and J-BIG pass at 100% on that branch, as they
   did on the fixed branches in REQUIREMENTS.md B.3 (634,616 and 1,083,265
   permutations, 0 failures).
3. **The P0 extensions.** E-1 (prog-once), E-2 (device error), E-3 (sync
   failure), E-6 (death tests), E-7 (allocator hook) and E-8 (read-only image
   harness) are merged.
4. **A PR-tier workflow.** The jobs of the PR tier (5.3) run on every push
   and pull request (LFS3-CI-01).

### 7.2 Exit criteria: "done"

The request defines done as "all tests pass including recovery from flash
failures". Precisely, testing is done for a commit when all of the
following hold on that commit:

1. **Tests exist.** Every P0 test of section 6 (54) and every P1 test (37)
   is in the tree, except:
   - P1 tests that wait on an open question in REQUIREMENTS.md section 8
     while that question is open: NEW-60 (Q19), NEW-68 (Q21), NEW-86 (Q15)
     and NEW-89 (Q12);
   - NEW-90 until bad-block tracking is merged. Once it is, NEW-90 is P0.

   P2 tests are not required, but every P2 test that exists must pass.
2. **PR and nightly tiers pass.** Every job of the PR and nightly tiers in
   5.3 passes on the commit, which means in particular:
   - B-DEF, B-BIG and B-YGB on A-64LE with GCC and with clang;
   - B-NA, B-NB, B-NS, B-NM, B-TS and each B-YES-x;
   - B-DEF on A-32LE (thumb) and A-32BE (mips and powerpc);
   - every reentrant case under `-Plinear` with `POWERLOSS_BEHAVIOR` 0, 1, 2
     and 3 in B-DEF and B-BIG, and under `-P'permute(1)'` in B-DEF;
   - the METASTABLE cases (NEW-08 and the METASTABLE permutations of NEW-01,
     NEW-05, NEW-06, NEW-11) under criterion B;
   - every geometry of G-ALL in B-DEF and B-BIG;
   - ASan, UBSan, FORTIFY and valgrind with zero reports, test code
     included;
   - the build matrix of J-BUILD with zero warnings.

   "Passes" is the standard pass condition of REQUIREMENTS.md 5.9: every
   permutation not filtered by `if`, asserts enabled except in B-NA, no
   assert, no crash.
3. **Release tier passes once.** Before a tag, the release-tier jobs
   (J-PL-DEEP, J-GEO-EV, J-ARCH-BIG, and J-COMPAT from the first v3-beta)
   pass on the tagged commit.
4. **Flash-failure matrix closed.** Each of the 240 applicable cells of 4.4
   is covered by a test that passes in the job its row names: the T, R and
   P cells by their existing cases (and the new tests named for P), the N
   cells by their new tests. A cell may be marked not applicable only with a
   reason in 4.5.
5. **Known defects resolved.** Each of the 62 requirements with the status
   "Known defect" in REQUIREMENTS.md is either fixed, with its regression
   test (6.3, or the review for DOC requirements) passing, or waived by the
   maintainer with the decision recorded in REQUIREMENTS.md.
6. **No hidden exclusions.** No `if`, `ifdef` or `ifndef` in `tests/`
   excludes a permutation because it fails, unless an open issue is cited
   next to it (LFS3-CI-10).
7. **Results recorded.** The exit run's results are recorded as described in
   8.6.

Not part of done: P2 tests that do not exist yet, the performance
thresholds of REQUIREMENTS.md 6.18 (reported by J-BENCH, not gated), and the
documentation requirements that are not known defects (DESIGN.md and SPEC.md
are release blockers tracked by REQUIREMENTS.md, not by this plan).

### 7.3 Suspension and resumption

- **Suspend** a tier's gating when it has failed on the test branch for more
  than three consecutive runs for a reason outside littlefs (runner outage,
  toolchain change). Record the reason; do not merge to the test branch
  while suspended.
- **Do not suspend** for a littlefs failure. File it (8.7) and either fix it
  or, with the maintainer's agreement, exclude the permutation with a cited
  issue (criterion 6), which keeps the rest of the tier gating.
- **Resume** when one complete run of the tier passes.

## 8. Procedures

All commands run from the repository root. On macOS, Homebrew GCC is not
usable on the authors' machine; `.local/cc` is a wrapper that runs Apple
clang and drops the GCC-only flags of the Makefile (`-fcallgraph-info`,
`-ftrack-macro-expansion`, `-Wno-stringop-overflow`, `-Wno-format-overflow`).

### 8.1 Build

```sh
# default build (B-DEF)
make -j14 BUILDDIR=.local/b .local/b/runners/test_runner CC=$PWD/.local/cc

# all features (B-BIG)
make -j14 BUILDDIR=.local/big .local/big/runners/test_runner \
    CC=$PWD/.local/cc LFS3_BIGGEST=1

# any other build: every LFS3_* make variable becomes -DLFS3_*=value
make -j14 BUILDDIR=.local/ygb .local/ygb/runners/test_runner \
    CC=$PWD/.local/cc LFS3_YES_GBMAP=1
make -j14 BUILDDIR=.local/na  .local/na/runners/test_runner \
    CC=$PWD/.local/cc LFS3_NO_ASSERT=1

# sanitizers: pass the flags through the environment so the Makefile appends
CFLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -O1" \
    make -j14 BUILDDIR=.local/asan-big .local/asan-big/runners/test_runner \
    CC=$PWD/.local/cc LFS3_BIGGEST=1

# Linux with GCC (FORTIFY is on by default in Ubuntu's GCC)
make -j$(nproc) test-runner
```

If generated files look stale (for example after switching branches), run
`make clean` (or remove the `BUILDDIR`) and build again.

### 8.2 Run

```sh
# everything, default schedule (-Pnone and -Plinear), keep going, 14 jobs
python3 -W ignore ./scripts/test.py -R .local/b/runners/test_runner -j14 -k -F 50

# one suite, one case, or a list
python3 -W ignore ./scripts/test.py -R .local/b/runners/test_runner -j14 -k \
    test_badblocks test_powerloss:test_powerloss_spam_f_pl_fuzz

# torn power-loss behaviours on the reentrant cases (J-PL)
python3 -W ignore ./scripts/test.py -R .local/b/runners/test_runner -j14 -k \
    -Plinear -DPOWERLOSS_BEHAVIOR=1,2,3

# other schedules
... -P'permute(1)'          # one loss at every write
... -P'permute(2)' test_dirs test_relocations test_powerloss
... -Plog
... -Pexhaustive test_powerloss:<small case>

# a geometry (J-GEO): override the inputs, not BLOCK_SIZE
... -DREAD_SIZE=16 -DPROG_SIZE=16 -DERASE_SIZE=512 -DERASE_VALUE=0xff

# valgrind (no power loss)
python3 -W ignore ./scripts/test.py -R .local/b/runners/test_runner -j14 -k \
    --valgrind -Pnone

# qemu (J-ARCH), with a cross compiler
make -j14 BUILDDIR=.local/arm .local/arm/runners/test_runner \
    CC="arm-linux-gnueabi-gcc --static -mthumb"
python3 -W ignore ./scripts/test.py -R .local/arm/runners/test_runner \
    --exec=qemu-arm -j14 -k -Pnone
```

Useful listing commands: `-l` (suites), `-L` (cases), `-Y` (summary of
permutation counts), `--list-defines`, `--list-powerlosses`.

### 8.3 Replay a failure

Every failure prints a test id that pins the case, every permuted define,
any `-D` override and the power-loss sequence, for example
`test_grow_incr_spam_uzd_fuzz:s11t12u10g28h2gg4j20k2gg8`.

```sh
# replay exactly
.local/b/runners/test_runner test_grow_incr_spam_uzd_fuzz:s11t12u10g28h2gg4j20k2gg8

# replay through test.py with context, or under gdb
python3 -W ignore ./scripts/test.py -R .local/b/runners/test_runner -C 50 <id>
python3 -W ignore ./scripts/test.py -R .local/b/runners/test_runner --gdb <id>

# stop at a given power loss, or just before or after the failing one
... --gdb-pl 3 <id>
... --gdb-pl-before <id>
... --gdb-pl-after <id>

# keep the disk image and a block-device trace for the dbg scripts
... -d .local/trace.disk -t .local/trace.txt <id>
./scripts/dbglfs3.py -b4096 .local/trace.disk
```

### 8.4 The Linux run in Docker

```sh
docker build -t lfs3-linux .local/docker
docker run --rm -v $PWD:/work -w /work lfs3-linux \
    sh -c 'make -j$(nproc) test-runner &&
           python3 -W ignore ./scripts/test.py -R runners/test_runner \
               -j$(nproc) -k -o .local/linux/results.csv > .local/linux/test.log 2>&1'
```

The image (`.local/docker/Dockerfile`) is Ubuntu 24.04 with GCC, make,
Python 3, `python3-toml`, git and valgrind. Build inside the container into
the default `BUILDDIR`; do not reuse objects built on macOS.

### 8.5 Measuring a job's run time

Run the job once with `-o results.csv`; the wall time is the `done:` line
of the log, and per-permutation times are the `test_runtime` column. Replace
the estimates of 5.3 with these figures, and size the shards so that each
PR-tier shard finishes in about 15 minutes.

### 8.6 Recording results

For every job run, keep:

- the commit id and the build command line;
- `test.log` (the `done:` line gives passed, failed, power losses and wall
  time);
- `results.csv` from `-o` (one row per permutation: suite, case, every
  define, `test_passed`, `test_runtime`);
- for failures, the test ids and the first 50 lines of context (`-F 50`).

Store them as CI artifacts named `<job>-<commit>`; locally, in
`.local/<build>/` as the runs in REQUIREMENTS.md B.3 did. A per-suite
summary can be made from the CSV (pass counts and CPU seconds per suite).
Update REQUIREMENTS.md B.3 after each exit run.

### 8.7 Defect workflow

1. **Reproduce** the failure from its test id (8.3) on the test branch.
   Note the build, architecture and schedule.
2. **Classify**: a littlefs defect, a test defect (like F-4 to F-10), or a
   tooling defect. A littlefs defect gets a REQUIREMENTS.md entry: an
   existing requirement's status becomes "Known defect", or a new
   requirement is added, and Appendix A gets a row.
3. **Report** upstream as an issue with "v3" in the title (as #1114 asks),
   with the test id, the minimal case and the requirement ID.
4. **Write the regression test first**, in the style of the neighbouring
   cases, and see it fail on the unfixed code.
5. **Fix** with the smallest change, one commit per defect, message
   "`<area>: <Past-tense verb> ...`" as upstream does. Run the full default
   suite and the build in which the defect appeared.
6. **Close** when the regression test passes in every job of the PR and
   nightly tiers; update the requirement's status and the register.
7. **If the intended behaviour is unclear**, do not choose silently: add the
   question to REQUIREMENTS.md section 8 and mark the test "waits on Qn".

## 9. Risks and limits

### 9.1 Faults emubd cannot inject, even with this plan's extensions

- **Read and program disturb** of neighbouring cells and blocks.
- **Data retention loss** over time and temperature, beyond what random bit
  flips model.
- **Faults that depend on offset within a block**, other than the torn tail
  of E-4.
- **Partial erase** that leaves a block readable as erased but weak.
- **Controller behaviour** of SD and eMMC cards: internal remapping, write
  caches that ignore `sync`, power loss inside the card's FTL. The OOO
  behaviour models only reordering of whole unsynced blocks.
- **Timing**: no fault depends on how long an operation takes.

These limits are why REQUIREMENTS.md LFS3-DOC-12 asks for the hardware
assumptions to be written down: the tests show littlefs is correct for the
modelled device, and the assumptions say which real devices that covers.

### 9.2 Coverage of the schedules

- **`-Plinear` interrupts each write once, per attempt.** A second power loss
  during the recovery from the first is reached only by `permute(2)` and
  deeper. `permute(n)` costs about W^n runs for a case with W writes, so
  `permute(2)` is limited to three suites and `-Pexhaustive` to cases with
  fewer than about 30 writes. Larger cases are explored only by `-Plinear`,
  `-Plog` and `permute(1)`.
- **One fault model at a time.** Each run uses one power-loss behaviour and
  one bad-block behaviour. The combinations in rows F17 to F27 are the only
  mixed faults tested.
- **Fuzz cases are seeded, not coverage-guided.** They explore what their
  seeds reach. Coverage-guided fuzzing (#1030) and the coverage report of
  J-COV are the way to find unexplored paths; neither gates.

### 9.3 Time budget

- B-BIG takes 1,181 s on 14 cores and 3,202 s with sanitizers. On 4-vCPU CI
  runners that is roughly 70 minutes and 3 hours: the PR tier must shard
  J-BIG, and the sanitizer runs stay nightly.
- qemu-user runs are estimated at 5 to 20 times native. Full A-32LE and
  A-32BE runs of B-BIG are therefore release-tier only.
- `-P'permute(2)'` and `-Pexhaustive` have not been measured. Measure them
  on one suite first and bound them with `if` before adding them to the
  release tier, so that a release is not blocked by a run that cannot
  finish.

### 9.4 Other risks

- **Open questions block tests.** Four P1 tests (7.2) and the tightened
  error codes of NEW-41 wait on decisions in REQUIREMENTS.md section 8. If
  the decisions take long, those tests should be written against the
  current behaviour and marked, rather than left out.
- **Test-side defects.** Seven defects in test code (F-4 to F-10) were found
  in a few days of running the suite under new conditions.
  New conditions (ASan, FORTIFY, B-BIG) are likely to find more; they are
  defects to fix like any other (8.7).
- **Extensions change the device model.** E-1, E-4 and E-5 change emubd.
  Each needs its own test (in `test_bd`) so that a broken extension does not
  hide or invent littlefs failures.
- **The fork drifts from upstream.** The plan is written against `b10efaa`.
  Upstream is still changing the file-write logic and the disk format
  (#1114, 2026-04-22); re-check sections 3 and 4 against each new upstream
  commit before an exit run.
- **Performance is not gated.** J-BENCH reports regressions; it does not
  fail the build. A large regression can therefore pass the exit criteria
  and must be caught by review of the report.
