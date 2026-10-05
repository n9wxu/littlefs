## littlefs v3 requirements

This document lists what littlefs v3 is required to do, stated so that each
requirement can be checked. For every requirement it names the observable
that decides it, the condition that passes, the condition that fails, and the
test that checks it. It also records, for the v3-alpha code, whether that test
exists and whether the code meets the requirement.

This is a draft. It does not describe how littlefs works. For that, see
[DESIGN.md](DESIGN.md) and [SPEC.md](SPEC.md). Both still describe v2 at the
time of writing, and their v3 versions are release blockers (LFS3-DOC-01,
LFS3-DOC-02).

```
   | | |     .---._____
  .-----.   |          |
--|o    |---| littlefs |
--|     |---|    v3    |
  '-----'   '----------'
   | | |
```

## Contents

1. [Purpose](#1-purpose)
2. [Scope](#2-scope)
3. [How to read a requirement](#3-how-to-read-a-requirement)
4. [Definitions](#4-definitions)
5. [Verification environments](#5-verification-environments)
6. [Requirements](#6-requirements)
   - [6.1 General and portability (GEN)](#61-general-and-portability-gen)
   - [6.2 Power-loss resilience (PL)](#62-power-loss-resilience-pl)
   - [6.3 Error detection and integrity (INT)](#63-error-detection-and-integrity-int)
   - [6.4 Flash failure handling (FAIL)](#64-flash-failure-handling-fail)
   - [6.5 Bad-block tracking (BAD)](#65-bad-block-tracking-bad)
   - [6.6 Metadata (META)](#66-metadata-meta)
   - [6.7 Files and data (FILE)](#67-files-and-data-file)
   - [6.8 Sync model and stickynotes (SYNC)](#68-sync-model-and-stickynotes-sync)
   - [6.9 Directories and paths (DIR)](#69-directories-and-paths-dir)
   - [6.10 Custom attributes (ATTR)](#610-custom-attributes-attr)
   - [6.11 Key-value API (KV)](#611-key-value-api-kv)
   - [6.12 Block allocation (ALLOC)](#612-block-allocation-alloc)
   - [6.13 Pre-erase (PRE)](#613-pre-erase-pre)
   - [6.14 Garbage collection and traversals (GC)](#614-garbage-collection-and-traversals-gc)
   - [6.15 Format, mount, grow, compatibility and versioning (MOUNT)](#615-format-mount-grow-compatibility-and-versioning-mount)
   - [6.16 Configuration validation (CFG)](#616-configuration-validation-cfg)
   - [6.17 Resource bounds (RES)](#617-resource-bounds-res)
   - [6.18 Performance (PERF)](#618-performance-perf)
   - [6.19 Thread safety (THR)](#619-thread-safety-thr)
   - [6.20 Build configurations (BUILD)](#620-build-configurations-build)
   - [6.21 Continuous integration (CI)](#621-continuous-integration-ci)
   - [6.22 Documentation (DOC)](#622-documentation-doc)
   - [6.23 Error handling for unattended systems (ERR)](#623-error-handling-for-unattended-systems-err)
   - [6.24 Graceful degradation (DEG)](#624-graceful-degradation-deg)
7. [Summary](#7-summary)
8. [Open questions](#8-open-questions)
9. [Requirements that need new tests](#9-requirements-that-need-new-tests)
- [Appendix A. Defect register](#appendix-a-defect-register)
- [Appendix B. Measurements](#appendix-b-measurements)
- [Appendix C. Coverage index](#appendix-c-coverage-index)

## 1. Purpose

The intent behind littlefs v3 is currently spread over the API comments in
`lfs3.h`, the roadmap in PR #1111, the discussion in issue #1114, commit
messages and code comments. This document collects that intent in one place
and turns each piece into a requirement with a pass/fail test.

It has three uses:

1. Review. The maintainer and interested parties can check that the stated
   intent is what they want before the on-disk format is frozen for v3-beta
   (#1114, 2026-04-22).

2. Status. Each requirement records whether the v3-alpha code meets it and
   whether any test checks it.

3. Test plan. Section 9 lists every requirement that needs a new test.

## 2. Scope

- **Code.** v3-alpha at commit `b10efaa` (2026-03-05): `lfs3.c`, `lfs3.h`,
  `lfs3_util.h`, `lfs3_util.c`, `bd/`, `runners/`, `scripts/` and `tests/`.
  Line numbers written `lfs3.c:N` or `lfs3.h:N` refer to `b10efaa`. The
  requirements were checked against the `v3-fixes` branch, which is
  `b10efaa` plus the fixes F-1 to F-8 of Appendix A. Its line numbers match
  `b10efaa` closely. Fixes for many of the other defects exist on further
  branches of the fork (`v3-fix-alloc`, `v3-fix-api`, `v3-fix-files`,
  `v3-fix-parity`, `v3-ci`); Appendix A names them. None is upstream yet,
  and the Status of every requirement still describes `b10efaa`.

- **On-disk format.** Disk version 0.0 (`LFS3_DISK_VERSION 0x00000000`,
  `lfs3.h:26`), as written by `b10efaa`. Version 0.0 marks the format as
  experimental. PR #1111 says the released v3 will refuse to mount it
  (LFS3-MOUNT-24).

- **API.** The 50 functions declared in `lfs3.h` at `b10efaa`, and the four
  exported helpers declared in `lfs3_util.h` (`lfs3_crc32c`,
  `lfs3_crc32c_mul`, `lfs3_toleb128`, `lfs3_fromleb128`). Appendix C maps
  each function, each `struct lfs3_cfg` field and each compile-time option to
  the requirements that cover it.

- **Planned work.** Bad-block tracking (release blocker #1) is included as a
  planned requirement set (6.5). DESIGN.md and SPEC.md (release blocker #2)
  are included as documentation requirements.

- **Not in scope.**
  - littlefs v2, and migration from v2 (a stretch goal in #1111).
  - Metadata redundancy, data redundancy and block deduplication. #1111
    plans them, but #1114 (2026-04-22) says they will probably not block
    stabilization.
  - 16-bit and 64-bit variants, and the other stretch goals and
    out-of-scope items listed in #1111.
  - `LFS3_KVONLY` and `LFS3_2BONLY`. Commit `5d905e6` removed them, and #1114
    (2025-09-29) moved them to the stretch goals.

## 3. How to read a requirement

Each requirement is written as a block:

```
#### LFS3-<AREA>-<NN>

littlefs shall <one behaviour>.

- Source:      where the requirement comes from, and its level
- Measure:     the observable that decides it
- Pass:        the exact condition that passes
- Fail:        the exact condition that fails
- Verified by: existing test cases, or NEW: the test that is needed
- Status:      state at b10efaa
- When:        how often the check must run
```

The sentence under the ID is the requirement. It describes one behaviour. A
requirement that needs several environments (builds, power-loss behaviours,
geometries) says so in its Pass condition, using the names defined in
section 5.

### 3.1 Requirement levels

Every Source field starts with one of three levels:

- **Stated.** The maintainer states the intent: in `lfs3.h`, in PR #1111, in
  issue #1114, in a commit message, or in a code or test comment. The Source
  cites it.

- **Derived.** The requirement follows from a stated requirement, or from a
  guarantee littlefs has always made. The Source gives the reason.

- **Proposal.** The requirement is ours. The maintainer has not stated it.
  Proposals are included where a behaviour needs a defined answer before the
  format or API is frozen. They are open to rejection.

### 3.2 Status values

Status describes `b10efaa` in the default build (`make test`, which runs
`-Pnone` and `-Plinear`), unless the note says otherwise.

- **Tested.** An existing case checks the Pass condition and runs in the
  default build.
- **Partly tested.** A case checks part of the Pass condition, or the case
  that checks it is compiled out of the default build, or only some of the
  required environments run it. The note says which part is missing.
- **Untested.** No case checks the Pass condition. The code may still meet it.
- **Not implemented (planned).** The maintainer plans the feature and the code
  does not have it yet.
- **Known defect (ref).** The code does not meet the requirement. The ref
  points to Appendix A. A defect is listed only when a probe program or a
  test run reproduced it, the code establishes it without doubt, or the
  maintainer records it as a known bug; the register says which. "Fixed on
  v3-fixes" means our branch fixes it and the fix has not yet been offered
  upstream.

A requirement whose status is not Tested is not a claim that the code is
wrong. It is a claim that nothing shows it is right.

### 3.3 When values

- **every CI run.** The check gates every push and pull request.
- **nightly.** The check is too slow to gate every push and runs on a
  schedule.
- **before v3-beta.** The requirement must be met, or explicitly decided,
  before the on-disk format is frozen. Most of these are one-off checks,
  documents or features.

### 3.4 Naming test cases

`suite::case` names the case `test_<suite>_<case>` in `tests/test_<suite>.toml`.
For example `dread::seek` is `test_dread_seek` in `tests/test_dread.toml`. A
`*` matches any suffix. **NEW:** marks a test that does not exist yet.

## 4. Definitions

- **block device (bd).** The `read`, `prog`, `erase` and `sync` callbacks in
  `struct lfs3_cfg` (`lfs3.h:468-493`).

- **prog unit.** `prog_size` bytes at a `prog_size`-aligned offset. The
  smallest region littlefs programs.

- **erased.** The state of a block after a successful `erase`. littlefs makes
  no assumption about the erased value (`lfs3.h:482-483`).

- **commit.** One atomic append to an rbyd log. It ends with a CKSUM tag. A
  commit is **visible** if and only if its checksum validates at fetch.

- **rbyd.** The append-only log in one block that holds a balanced tree of
  tags (#1111 "Efficient metadata compaction").

- **mdir.** A metadata pair: two blocks, of which the one with the newest
  valid commit is active.

- **mroot, anchor, mroot chain.** The mroot is the mdir that holds the
  filesystem configuration. The anchor is the mroot pair at blocks {0, 1}. The
  chain links the anchor to the current mroot with MROOT tags.

- **mtree.** The B-tree of mdirs that replaces v2's threaded tree
  (#1111 "A simpler/more robust metadata tree").

- **B-tree, B-shrub, shrub.** A file's data tree. A B-shrub is a B-tree
  whose root is stored inside the file's mdir. A shrub is any such inlined
  trunk.

- **fragment, bptr, crystallization.** A fragment is file data stored in a
  B-tree leaf. A bptr is a pointer to a data block. Crystallization packs
  fragments and new data into a freshly erased data block.

- **handle.** An open `lfs3_file_t`, `lfs3_dir_t` or `lfs3_trv_t`.

- **in-sync, desynced.** An in-sync file handle receives the updates other
  handles sync, and syncs on close. A desynced handle does neither
  (`lfs3.h:1562-1575`).

- **synced.** A file handle is synced when `lfs3_file_sync` or
  `lfs3_file_close` on it returned 0.

- **durable.** A state is durable when it survives a power loss that happens
  at any later point.

- **power loss.** The block device stops at an arbitrary write operation. The
  interrupted operation behaves as one of the emubd power-loss behaviours in
  5.5, and every later operation is lost. RAM state is lost.

- **torn write.** A prog or erase interrupted by a power loss.

- **remount.** `lfs3_unmount` (when possible) followed by `lfs3_mount` on the
  same block device with the same configuration.

- **bad block.** A block for which the block device returns
  `LFS3_ERR_CORRUPT` from `prog` or `erase` (`lfs3.h:475, 484`), or which
  behaves as one of the emubd bad-block behaviours in 5.6.

- **wear-out.** A block becomes bad after a number of erases. emubd models
  this with `erase_cycles`.

- **relocation.** Moving an mdir, B-tree node or data block to a newly
  allocated block, because of a bad block or because of wear levelling
  (`block_recycles`).

- **stickynote.** The on-disk entry of a file that was created by an open
  handle and has not been synced (#1111 "Stickynotes").

- **orphan.** A stickynote that no in-sync, non-zombie handle holds, or an
  entry pending in the grm. littlefs hides orphans.

- **zombie.** An open handle whose file was removed or replaced.

- **gstate.** State distributed over mdirs as XOR deltas: the gcksum, the grm
  and the gbmap.

- **gcksum.** The global checksum. The XOR of all gcksum deltas equals the
  crc32c cube of the XOR of the checksums of all mdirs
  (#1111 "Error detection! - Global-checksums"). `lfs3_fs_cksum` returns it.

- **grm.** The global remove queue: at most two mids whose removal is pending.

- **gbmap.** The optional on-disk global block map (`LFS3_GBMAP`,
  #1111 "Efficient block allocation").

- **known window.** The range of blocks `[window, window + known)` whose gbmap
  entries are trusted.

- **checkpoint.** A point at which every in-use block is reachable from the
  committed filesystem or from RAM. Between two checkpoints the allocator
  hands out each block at most once (`lfs3.c:2178-2187`).

- **pre-erase.** Erasing a free block during gc and recording it as erased in
  the gbmap, so that a later allocation can skip the erase (`LFS3_PREERASE`).

- **ecksum.** A checksum of the first bytes of an erased region, recorded so
  that littlefs can tell whether a program was attempted there.

- **janitorial work, gc.** Work that `lfs3_fs_gc`, `lfs3_fs_ck`, the traversal
  API and the mount/format flags perform: mkconsistent, lookahead or gbmap
  repopulation, pre-erase, compaction, ckmeta and ckdata.

- **checksum-valid image.** An image in which every commit's checksum
  validates. A **crafted image** is a checksum-valid image with content that
  littlefs does not write, for example an out-of-range block pointer.

## 5. Verification environments

### 5.1 Test runner and emubd

Tests are TOML cases in `tests/`, translated by `scripts/test.py` and linked
into `runners/test_runner`. The block device is emubd (`bd/lfs3_emubd.c`),
which keeps the disk in RAM and injects faults. `make test` runs every suite
with the power-loss schedules `none` and `linear`.

Implicit test defaults (`runners/test_defines.h`): `READ_SIZE=1`,
`PROG_SIZE=1`, `ERASE_SIZE=4096`, `BLOCK_SIZE=4096`, 1 MiB disk
(`BLOCK_COUNT=256`), `BLOCK_RECYCLES=-1`, `RCACHE_SIZE=PCACHE_SIZE=16`,
`FCACHE_SIZE=16`, `LOOKAHEAD_SIZE=16`, `SHRUB_SIZE=BLOCK_SIZE/4`,
`FRAGMENT_SIZE=min(BLOCK_SIZE/16,512)`, `CRYSTAL_THRESH=BLOCK_SIZE/16`,
`LOOKGBMAP_THRESH=BLOCK_COUNT/4`, `ERASE_VALUE=0xff`, `ERASE_CYCLES=0`,
`BADBLOCK_BEHAVIOR=PROGERROR`, `POWERLOSS_BEHAVIOR=ATOMIC`.

At `b10efaa` there are 25 suites, 842 cases and 880,240 permutations, of
which 618,712 run in the default build under `-Pnone`. 64 cases are
reentrant (run under power loss). A baseline run of the default build on
macOS/clang passed 634,435 of 634,616 runs. The 181 failures were two test
bugs that are fixed on v3-fixes (F-4, F-6).

### 5.2 Build configurations

| Name | Definition | Notes |
|---|---|---|
| B-DEF | no `LFS3_*` feature macro | `make test`. Compiles out `test_gbmap`, 27 of 32 `test_gc` cases, the `test_ck` ck* cases, and every bad-block permutation that needs `LFS3_CKPROGS` |
| B-BIG | `LFS3_BIGGEST` | GC, GBMAP, PREERASE, REVPERTURB, REVNOISE, CKPROGS, CKFETCHES, CKMETAPARITY, CKDATACKSUMS, BLEAFCACHE (`lfs3_util.h:28-60`). Does not compile at `b10efaa` (F-2) |
| B-YGB | `LFS3_YES_GBMAP` | every format and mount uses the gbmap |
| B-RO | `LFS3_RDONLY` | read-only driver. The test runner cannot drive it today; it needs pre-built images |
| B-YES-x | one `LFS3_YES_x`, for x in RDONLY, REVPERTURB, REVNOISE, CKPROGS, CKFETCHES, CKMETAPARITY, CKDATACKSUMS, GC, GBMAP, BLEAFCACHE, FLUSH, SYNC | forces the matching mount/format flag |
| B-NA | `LFS3_NO_ASSERT` | release-mode behaviour |
| B-NB | `LFS3_NO_BUILTINS` | C fallbacks for builtins |
| B-NS | `LFS3_NO_STRINGH` | C fallbacks for `string.h` |
| B-NM | `LFS3_NO_MALLOC` | `lfs3_malloc` returns NULL |
| B-TS | `LFS3_THREADSAFE` | adds `lock`/`unlock`; `make test-threadsafe` runs every suite with the runner's checking callbacks |

### 5.3 Architectures

| Name | Target | How |
|---|---|---|
| A-64LE | x86_64 or arm64, 64-bit little-endian | native |
| A-32LE | thumb (arm), 32-bit little-endian | qemu-arm |
| A-32BE | mips and powerpc, 32-bit big-endian | qemu-mips, qemu-ppc |

### 5.4 Power-loss schedules

Selected with `-P` (`runners/test_runner.c:2080-2107`). They apply only to
reentrant cases.

| Name | Meaning |
|---|---|
| `-Pnone` | no power loss |
| `-Plinear` | attempt k loses power after write operation 1 + k; every write point is hit once |
| `-Plog` | attempt k loses power after 2^(k+1) write operations |
| `-P'permute(n)'` | every combination of n power losses; `permute(1)` equals v2 CI's `-P1` |
| `-Pexhaustive` | every combination of power losses, unbounded depth |

A write operation is a prog or an erase. Reads and syncs are not counted.

### 5.5 emubd power-loss behaviours

Selected with `-DPOWERLOSS_BEHAVIOR=n` (`bd/lfs3_emubd.h:45-51`).

| n | Name | The interrupted operation |
|---|---|---|
| 0 | ATOMIC | is not applied |
| 1 | SOMEBITS | prog: one bit in the first `prog_size` bytes is programmed; erase: the old data stays and one bit flips |
| 2 | MOSTBITS | is applied, then one bit in the first `prog_size` bytes (or, for an erase, anywhere) flips |
| 3 | OOO | every block written since the last `sync` reverts, except the one being written |
| 4 | METASTABLE | is applied, then one bit that it wrote (for an erase, one bit of the block) becomes metastable and reads of it return random values until the block is erased, as a half-programmed cell does; later progs elsewhere in the block don't settle it. Bits outside the interrupted operation never change: a flip there is bit rot (F12 to F14), not power loss |

**PLB-TORN** means the set {0, 1, 2, 3}. **PLB-ALL** means {0, 1, 2, 3, 4}.

### 5.6 emubd bad-block behaviours

Selected with `-DBADBLOCK_BEHAVIOR=n` (`bd/lfs3_emubd.h:33-42`). A block is
bad when it was marked with `lfs3_emubd_mkbad`, or when its wear exceeds
`ERASE_CYCLES` (only if `ERASE_CYCLES > 0`).

| n | Name | A bad block |
|---|---|---|
| 0 | PROGERROR | prog returns `LFS3_ERR_CORRUPT` and writes nothing |
| 1 | ERASEERROR | erase returns `LFS3_ERR_CORRUPT` and erases nothing |
| 2 | READERROR | read returns `LFS3_ERR_CORRUPT` |
| 3 | PROGNOOP | prog silently does nothing |
| 4 | ERASENOOP | erase silently does nothing, and progs do nothing |
| 5 | PROGFLIP | prog lands, with the block's bad bit flipped |
| 6 | READFLIP | prog lands, and reads flip the bad bit with probability 1/2 |
| 7 | MANUAL | bits flip only when the test calls `lfs3_emubd_flip` |

### 5.7 Bit flips

Tests flip bits directly with `lfs3_emubd_flipbit`, `lfs3_emubd_flip` and
`lfs3_emubd_mkbadbit`. `test_ck` uses them to model bit rot after a write.

### 5.8 Geometries

v3 has no `-G` option. Geometries are `-D` overrides of the inputs
(`READ_SIZE`, `PROG_SIZE`, `ERASE_SIZE`, `DISK_SIZE`), not of `BLOCK_SIZE`, so
that suites which pin `BLOCK_SIZE` keep it.

| Name | read / prog / erase | Disk |
|---|---|---|
| G-NOR | 1 / 1 / 4096 | 1 MiB (the default) |
| G-EEPROM | 1 / 1 / 512 | 1 MiB |
| G-P16 | 16 / 16 / 512 | 1 MiB |
| G-EMMC | 512 / 512 / 512 | 1 MiB |
| G-NAND | 4096 / 4096 / 32768 | 1 MiB |
| G-BIGNAND | 2048 / 2048 / 131072 | 16 MiB |
| G-W25Q128 | 1 / {1, 256} / 4096 | 16 MiB (4096 blocks); used by the performance workload in Appendix B |

**G-ALL** means the first six. Each geometry is also run with
`ERASE_VALUE` in {0xff, 0x00, -1} where the Pass condition says so.

### 5.9 The standard pass condition

"A case passes" means: the runner exits with status 0 for every permutation
of the case that its `if` does not filter; asserts are enabled (unless the
build is B-NA); no `LFS3_ASSERT` fires; and the case is compiled into the
named build. A case that the named build compiles out does not pass.

### 5.10 Faults emubd cannot inject

emubd cannot at present make `sync` fail, return an error other than
`LFS3_ERR_CORRUPT`, fail a read once and then succeed, disturb bytes of a
prog outside its first `prog_size` bytes, disturb neighbouring blocks, or
fail only some offsets of a block. Requirements that depend on these faults
say "NEW: emubd extension".

## 6. Requirements

### 6.1 General and portability (GEN)

#### LFS3-GEN-01

littlefs shall pass the default test suite on a 32-bit little-endian target.

- **Source:** Derived: littlefs targets microcontrollers, and v2 CI ran
  thumb for this reason (`.github/workflows/test.yml` on v2 `master`).
- **Measure:** `test_runner` exit status under qemu-arm.
- **Pass:** `make test` (B-DEF, `-Pnone -Plinear`) exits 0 on A-32LE.
- **Fail:** the build fails, or any permutation fails.
- **Verified by:** all suites; NEW: CI job (LFS3-CI-02).
- **Status:** Untested (no v3 CI job exists).
- **When:** every CI run.

#### LFS3-GEN-02

littlefs shall read an image written on a host of the other byte order with
the same results as on the host that wrote it.

- **Source:** Derived: the on-disk format is byte-order independent (le32,
  leb128 and be16 tag encodings, `lfs3.h:957-1080`), so images must move
  between hosts.
- **Measure:** results of `lfs3_stat`, `lfs3_dir_read`, `lfs3_get` and
  `lfs3_fs_cksum` over a fixed image.
- **Pass:** an image built by a fixed sequence of operations on A-64LE gives
  identical results when mounted on A-32BE, and an image built on A-32BE
  gives identical results on A-64LE, with mips and with powerpc as A-32BE.
  The two images are also byte-identical.
- **Fail:** any mount error or any differing result.
- **Verified by:** `compat::endian_exchange` through
  `make test-compat-endian` (job `test-compat-endian`, mips and powerpc
  under qemu-user).
- **Status:** Tested on v3-integration (d91ce6d6). Run for arm64 against
  mips and powerpc with GCC and `-Werror` under Docker; the images were
  byte-identical.
- **When:** every CI run.

#### LFS3-GEN-03

littlefs shall not execute undefined behaviour in any test case of the
suite.

- **Source:** Derived: undefined behaviour lets the compiler produce code
  that does not match the source, which defeats every other requirement.
- **Measure:** UndefinedBehaviorSanitizer and AddressSanitizer reports in
  `lfs3.c`, `lfs3_util.c` and `lfs3_util.h`.
- **Pass:** a runner built with `-fsanitize=undefined,address` runs every
  suite with `-Pnone` in B-DEF and B-BIG with zero reports, with
  `UBSAN_OPTIONS=halt_on_error=1` so that a report fails its case.
- **Fail:** any report.
- **Verified by:** `make test-sanitize`, which also runs `-Plinear`: job
  `test-sanitize` in `.github/workflows/test.yml` for B-DEF, and job
  `test-sanitize-biggest` in `.github/workflows/nightly.yml` for B-BIG.
- **Status:** Tested on `v3-integration` (41165ec9): `make test-sanitize`
  reports nothing in the CI image (Ubuntu 24.04, GCC 13.3), in B-DEF with
  `-Pnone` and LeakSanitizer (642,838 permutations) and `-Plinear`
  (25,099), and in B-BIG with `-Pnone` and LeakSanitizer (1,101,749) and
  `-Plinear` (32,633); with clang on macOS the whole B-DEF suite and B-BIG
  `-Plinear` pass too. The jobs have not run on GitHub yet. 4-api R17
  (fb675e5b), 1-meta 0.7c (7f689b8), F-1, F-3 and D-7 (82ab4f07,
  b5888089) are fixed.
- **When:** every CI run in B-DEF; nightly in B-BIG.

#### LFS3-GEN-04

littlefs shall keep filesystems mounted on different `lfs3_t` objects
independent of each other.

- **Source:** Stated: `lfs3.h:1412-1413` ("Multiple filesystems may be
  mounted simultaneously with multiple littlefs objects").
- **Measure:** file contents and `lfs3_fs_ck` results on two filesystems on
  two emubd instances, with operations interleaved.
- **Pass:** both filesystems match their models, and
  `lfs3_fs_ck(LFS3_CK_CKMETA | LFS3_CK_CKDATA)` returns 0 on both.
- **Fail:** any mismatch or error.
- **Verified by:** NEW: two-instance interleaved fuzz.
- **Status:** Untested.
- **When:** every CI run.

#### LFS3-GEN-05

littlefs shall return, as a negative result from any public function, only
a value of `enum lfs3_err` or a negative value returned by a block device
callback during that call.

- **Source:** Stated: `lfs3.h:77-98` (error codes), `lfs3.h:468-492`
  (callback errors "are propagated to the user").
- **Measure:** every negative return value of every public call.
- **Pass:** a runner wrapper that checks every public call's negative result
  finds no other value in any suite (B-DEF, B-BIG, `-Pnone -Plinear`). The
  test block devices return only `enum lfs3_err` values, so any other value
  fails.
- **Fail:** any other negative value.
- **Verified by:** cases assert specific codes throughout; the hook and
  `scripts/ckerrs.py` of LFS3-ERR-01.
- **Status:** Partly tested at `b10efaa`; tested on `v3-integration`
  (9c9e8145).
- **When:** every CI run.

#### LFS3-GEN-06

littlefs shall not program or erase any block because of a mutating call
made on a filesystem mounted with `LFS3_M_RDONLY`, including in builds with
`LFS3_NO_ASSERT`.

- **Source:** Derived: `LFS3_M_RDONLY` (`lfs3.h:245`). In the default build
  the calls assert (`lfs3.c:16620`), which is a contract, not a guarantee
  once asserts are removed.
- **Measure:** emubd prog and erase counters around each of `lfs3_mkdir`,
  `lfs3_remove`, `lfs3_rename`, `lfs3_setattr`, `lfs3_removeattr`,
  `lfs3_set`, `lfs3_file_opencfg` with a write flag, `lfs3_fs_mkconsistent`
  and `lfs3_fs_grow` on an `LFS3_M_RDONLY` mount.
- **Pass:** counters unchanged in B-DEF (where each call must assert or
  return an error) and in B-NA.
- **Fail:** any counter increases.
- **Verified by:** NEW: death-test harness for B-DEF; plain test for B-NA.
- **Status:** Known defect (4-api R12). See also LFS3-MOUNT-17 and open
  question Q2.
- **When:** every CI run.

#### LFS3-GEN-07

littlefs shall return `LFS3_ERR_CORRUPT`, without asserting and without
accessing memory or storage out of range, when a checksum-valid image
contains an out-of-range block address, offset, size, weight or alt jump.

- **Source:** Proposal. The decoders do not range-check on-disk values and
  assert instead (2-files B13, 1-meta 7.7, 4-api R12). littlefs is often
  used on removable media and with images built elsewhere.
- **Measure:** return value and sanitizer reports while mounting, reading
  and traversing crafted images.
- **Pass:** for each field in the list, a crafted image with that field out
  of range gives `LFS3_ERR_CORRUPT` from the first call that uses it, with no
  assert in B-DEF and no sanitizer report in B-NA.
- **Fail:** an assert, a crash, a sanitizer report or a result other than
  `LFS3_ERR_CORRUPT`.
- **Verified by:** NEW: crafted-image suite (extends `mtree::truncated_*`).
- **Status:** Untested. The decoders assert (`lfs3.c:2352-2390`,
  `5174-5201`, `6519`, `7528`, `8036-8040`). See open question Q21.
- **When:** before v3-beta.

#### LFS3-GEN-08

littlefs shall not issue a block device operation outside
`[0, block_count)` blocks or outside `[0, block_size)` bytes of a block.

- **Source:** Stated: `lfs3.h:515-521` define the geometry; the bd wrappers
  assert it (`lfs3.c:31-107`).
- **Measure:** bounds of every bd call.
- **Pass:** no out-of-range call in any suite, and none in the scenario of
  LFS3-MOUNT-21.
- **Fail:** any out-of-range call.
- **Verified by:** every suite (wrapper asserts); NEW: LFS3-MOUNT-21 test.
- **Status:** Known defect (4-api R4: after `lfs3_fs_grow(128)` on a
  64-block configuration the allocator erases block 64).
- **When:** every CI run.

#### LFS3-GEN-09

littlefs shall return `LFS3_ERR_CORRUPT`, and not assert, when metadata
reads differently from when it was fetched or holds a value no writer
produces, with or without the read checks of `LFS3_M_CKMETAPARITY`.

- **Source:** Derived: asserts state what the code guarantees, not what the
  device returns. A bit left metastable by a power loss, or one that flips
  on read, changes metadata after the commit checksum accepted it, and
  without parity checks a lookup can be sent the wrong way. Such a read
  may fail the operation with an error, never the program. The cases'
  own comments state this criterion (`tests/test_ck.toml`,
  `tests/test_dirs.toml`); fixes of the same class are 42e7f9eb and
  e5646435.
- **Measure:** asserts and return codes under METASTABLE power loss and
  READFLIP bad bits.
- **Pass:** `dirs::rm_many_2layers_metastable` and `ck::metastable_alts`
  pass in B-DEF, B-YGB and B-BIG: every call returns 0 or an `enum lfs3_err`
  code and nothing asserts. In particular a tag with bit 7 set (reserved,
  written as 0) is never copied into a new commit, the operation fails
  instead; and a lookup that places a new entry before the root bookmark,
  at mid 0, fails the operation. A checksum-valid commit holding a bit-7
  tag still fetches: rejecting it there would silently roll back to the
  previous commit, and an unknown config tag with bit 7 must still fail
  mount with `LFS3_ERR_NOTSUP` (LFS3-MOUNT-09), not `LFS3_ERR_CORRUPT`.
- **Fail:** an assert, or any other negative result.
- **Verified by:** as listed; `ck::reserved_bit` and `ck::root_bookmark`
  (NEW-134), which make the same misreads happen deterministically in
  every build.
- **Status:** Tested on `v3-integration` (7d27de3d). Before, in B-YGB a
  misdirected bookmark lookup in `lfs3_mkdir` pushed mid 0 to the grm
  (assert in `lfs3_grm_push`, 6 permutations of
  `dirs::rm_many_2layers_metastable`), and a tag read with bit 7 set was
  copied into a new commit (assert in `lfs3_rbyd_appendtag`,
  `ck::metastable_alts`); NEW-134 hit both, and an assert in
  `lfs3_file_lookupnext`, in B-DEF (issue #18).
- **When:** every CI run.

### 6.2 Power-loss resilience (PL)

The requirements in this section use `POWERLOSS_BEHAVIOR=0` (ATOMIC) unless
they say otherwise. LFS3-PL-02 and LFS3-PL-03 extend every reentrant case to
the other behaviours.

#### LFS3-PL-01

littlefs shall mount successfully after a power loss at any write
operation.

- **Source:** Stated: power-loss resilience is the first design goal of
  littlefs (DESIGN.md); every reentrant case in `tests/` assumes it.
- **Measure:** `lfs3_mount` result on each re-entry of a reentrant case.
- **Pass:** every reentrant case passes under `-Plinear` with
  `POWERLOSS_BEHAVIOR=0` in B-DEF on A-64LE.
- **Fail:** any permutation fails.
- **Verified by:** the 64 reentrant cases (15,904 permutations), including
  `powerloss::*`, `dirs::*`, `files::pl_fuzz`, `relocations::spam_*_pl_fuzz`,
  `grow::incr_spam_*_pl_fuzz`, `stickynotes::*_pl`, `attrs::fattr_pl_fuzz_fuzz`,
  `dread::recursive_*`.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-PL-02

littlefs shall meet every power-loss requirement of this document when the
interrupted operation is torn as in SOMEBITS, MOSTBITS or OOO.

- **Source:** Derived: real flash does not stop cleanly; emubd models torn
  progs and erases and out-of-order writeback (5.5).
- **Measure:** reentrant case results with torn behaviours.
- **Pass:** every reentrant case passes under `-Plinear` with
  `-DPOWERLOSS_BEHAVIOR=1,2,3` in B-DEF and B-BIG.
- **Fail:** any permutation fails.
- **Verified by:** `powerloss::*` (the only cases that set these
  behaviours).
- **Status:** Partly tested (4 of 64 reentrant cases).
- **When:** nightly.

#### LFS3-PL-03

littlefs shall return from every read after a power loss that leaves a
metastable bit either data that a completed sync or the sync in progress
wrote, or `LFS3_ERR_CORRUPT`, when mounted with
`LFS3_M_CKMETAPARITY | LFS3_M_CKDATACKSUMS`, in both repair modes of
LFS3-DEG-12 and LFS3-DEG-13.

- **Source:** Proposal. #1111 ("Global-checksums") names metastability as a
  risk that checksums address. Without the ck* read checks, reads of
  already-fetched metadata are not re-checked (`tests/test_ck.toml:2725`).
- **Measure:** data returned by reads and the error codes after remount,
  under `POWERLOSS_BEHAVIOR=4` (the bit stays metastable until its block is
  erased, 5.5).
- **Pass:** `powerloss::metastable` (NEW-08) reports no read of data that
  neither a completed sync nor the sync in progress wrote, with
  `MODE` 0 (default) and 1 (`LFS3_M_SETTLE`), in B-BIG.
- **Fail:** any read returns other data without an error.
- **Verified by:** NEW-08.
- **Status:** Tested on `v3-rc` (`a12705da`): NEW-08 reads no wrong data in
  either mode, 11,832 power losses by default and 14,626 with
  `LFS3_M_SETTLE`, in B-BIG.
- **When:** nightly.

#### LFS3-PL-04

littlefs shall present each file, after a power loss and remount, either as
of the last `lfs3_file_sync` or `lfs3_file_close` that returned 0, or as of
the sync that was in progress.

- **Source:** Stated: #1111 "A well-defined sync model", rule 2 ("Syncing or
  closing an in-sync file atomically updates the on-disk state").
- **Measure:** content, size and file-attached attributes after remount,
  compared with the test's model.
- **Pass:** `powerloss::spam_f_pl_fuzz`, `powerloss::spam_fd_pl_fuzz`,
  `files::pl_fuzz`, `relocations::spam_f_pl_fuzz` and
  `grow::incr_spam_f_pl_fuzz` pass under `-Plinear` in B-DEF. These cover
  whole-file rewrites (`O_TRUNC`, write, close) and in-place rewrites of a
  small file's bytes followed by sync.
- **Fail:** a file matches neither state.
- **Verified by:** as listed. Other write patterns: LFS3-PL-12 to PL-17.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-PL-05

littlefs shall not change any file other than the one being written or
synced when power is lost.

- **Source:** Derived: copy-on-write; a commit changes only the entries it
  names.
- **Measure:** content and size of every other file after remount.
- **Pass:** `powerloss::spam_f_pl_fuzz` and `powerloss::spam_fd_pl_fuzz`,
  which check every surviving file, pass under `-Plinear` in B-DEF.
- **Fail:** any other file differs from its last synced state.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-PL-06

littlefs shall not make data written by `lfs3_file_write` or
`lfs3_file_flush` visible after a power loss unless a later
`lfs3_file_sync` or `lfs3_file_close` of that handle returned 0.

- **Source:** Stated: `lfs3.h:1553-1560` (flush "does not update metadata")
  and `lfs3.h:1594-1595` (a write is not stored "until either sync or close
  is called").
- **Measure:** file content after remount.
- **Pass:** a NEW reentrant case that writes, then flushes (explicitly, with
  `LFS3_O_FLUSH` and with `LFS3_M_FLUSH`) without syncing, finds after every
  power loss that the file equals its last synced state.
- **Fail:** any unsynced byte is visible.
- **Verified by:** implied by `powerloss::spam_f_pl_fuzz`;
  `powerloss_p1::flush_pl` (explicit flush, `LFS3_O_FLUSH`, `LFS3_M_FLUSH`;
  files of more than one block with `-DNIGHTLY=1`).
- **Status:** Tested on v3-integration (9cb7377f).
- **When:** every CI run.

#### LFS3-PL-07

littlefs shall hide, after a power loss, every file that was created by an
open handle and never synced.

- **Source:** Stated: #1111 "Stickynotes" ("If you lose power, stickynotes
  are hidden from the user and automatically cleaned up on the next mount").
- **Measure:** `lfs3_stat` and `lfs3_dir_read` after remount.
- **Pass:** `stickynotes::uncreat_pl`, `stickynotes::uncreat_many_pl`,
  `stickynotes::undesync_pl` and `stickynotes::undesync_many_pl` pass under
  `-Plinear` in B-DEF.
- **Fail:** such a file is visible after remount.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-PL-08

littlefs shall make `lfs3_mkdir` atomic under power loss: after remount the
directory either exists and is empty, or does not exist.

- **Source:** Derived: littlefs's atomic namespace operations; mkdir uses
  two commits joined by the grm (`lfs3.c:11600-11634`).
- **Measure:** `lfs3_stat`, `lfs3_dir_read` and `lfs3_mkdir` results after
  remount.
- **Pass:** `dirs::mkdir_*` pass under `-Plinear` in B-DEF.
- **Fail:** a half-created directory is visible, or a later `lfs3_mkdir` of
  the same name fails with an error other than `LFS3_ERR_EXIST`.
- **Verified by:** as listed. See LFS3-PL-27 for an excluded permutation.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-PL-09

littlefs shall make `lfs3_remove` atomic under power loss.

- **Source:** Derived: littlefs's atomic namespace operations; directory
  removal uses the grm (`lfs3.c:11713-11832`).
- **Measure:** `lfs3_stat` after remount.
- **Pass:** `dirs::rm_*` and `dread::recursive_rm` pass under `-Plinear` in
  B-DEF.
- **Fail:** after remount the entry is partly removed (for example a
  directory's bookmark survives without its name).
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-PL-10

littlefs shall make `lfs3_rename` atomic under power loss: after remount
exactly one of the old and new names refers to the entry, and a replaced
destination holds either its old content or the source.

- **Source:** Derived: littlefs's atomic rename; the source is pushed to the
  grm and moved in one commit (`lfs3.c:11940-12012`).
- **Measure:** `lfs3_stat` of both names after remount.
- **Pass:** `dirs::mv_*` and `dread::recursive_mv` pass under `-Plinear` in
  B-DEF.
- **Fail:** both names or neither name refer to the entry.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-PL-11

littlefs shall make `lfs3_setattr`, `lfs3_removeattr` and the attributes
written by a file sync atomic under power loss.

- **Source:** Stated: `lfs3.h:761-762, 799-802` (attributes "committed
  atomically").
- **Measure:** `lfs3_getattr` and `lfs3_sizeattr` after remount.
- **Pass:** `attrs::fattr_pl_fuzz_fuzz` and `attrs::setattr_pl_fuzz`
  (`lfs3_setattr` and `lfs3_removeattr` on files, directories and "/") pass
  under `-Plinear` in B-DEF.
- **Fail:** an attribute is torn, or has a value that was never set.
- **Verified by:** `attrs::fattr_pl_fuzz_fuzz` (file-attached attributes);
  `attrs::setattr_pl_fuzz` (path attributes and "/").
- **Status:** Tested on v3-integration (b686c1da).
- **When:** every CI run.

#### LFS3-PL-12

littlefs shall make `lfs3_set` atomic under power loss: after remount
`lfs3_get` returns either the old value in full or the new value in full.

- **Source:** Derived: `lfs3_set` replaces a file (`lfs3.h:1442-1447`) and
  is implemented as `O_CREAT | O_TRUNC` write and close
  (`lfs3.c:15045-15071`).
- **Measure:** `lfs3_get` and `lfs3_size` after remount.
- **Pass:** `powerloss_p1::kv_pl_fuzz` passes under `-Plinear` in B-DEF, for
  values below and above the one-commit size limit.
- **Fail:** any other value or size.
- **Verified by:** `powerloss_p1::kv_pl_fuzz`. Each value starts with the op
  that wrote it, since `lfs3_set` cannot commit an attribute with the value.
- **Status:** Tested on v3-integration (f05ccd38).
- **When:** every CI run.

#### LFS3-PL-13

littlefs shall present a file whose `lfs3_file_truncate` or
`lfs3_file_fruncate` was followed by a sync, after a power loss, as of either
that sync or the previous one.

- **Source:** Derived from LFS3-PL-04 for the truncate paths, which change
  the tree without writing data.
- **Measure:** content and size after remount.
- **Pass:** a NEW reentrant case of truncate and fruncate followed by sync
  (growing and shrinking, including the write-then-fruncate log pattern of
  #1111 "Easier logging APIs") passes under `-Plinear` in B-DEF.
- **Fail:** any other content or size.
- **Verified by:** NEW.
- **Status:** Untested.
- **When:** every CI run.

#### LFS3-PL-14

littlefs shall present a file after random overwrites, seeks past the end
and a sync, after a power loss, as of either that sync or the previous one.

- **Source:** Derived from LFS3-PL-04 for B-tree updates (#1111 "Efficient
  random writes").
- **Measure:** content and size after remount.
- **Pass:** a NEW reentrant variant of `fwrite::fuzz_unaligned` with
  periodic syncs passes under `-Plinear` in B-DEF.
- **Fail:** any other content or size.
- **Verified by:** NEW. `test_fwrite` has no reentrant case.
- **Status:** Untested.
- **When:** every CI run.

#### LFS3-PL-15

littlefs shall not change the bytes of a data block that a synced file
references when power is lost while it appends into the same block.

- **Source:** Derived: v3 resumes programming a partially programmed data
  block after a sync (`lfs3.c:13715-13756`); the synced bptr's checksum covers
  only the programmed prefix.
- **Measure:** content of the synced prefix after remount.
- **Pass:** a NEW reentrant case of small appends, each followed by sync,
  with `PROG_SIZE` in {1, 16, 256}, passes under `-Plinear` with
  PLB-TORN in B-DEF.
- **Fail:** any synced byte differs.
- **Verified by:** NEW.
- **Status:** Untested.
- **When:** nightly.

#### LFS3-PL-16

littlefs shall present a file after a power loss as of the last successful
sync from any of its handles, when several handles, desynced handles or
resynced handles write it.

- **Source:** Derived from #1111 "A well-defined sync model", rules 2 to 5.
- **Measure:** content and size after remount.
- **Pass:** `powerloss_p1::fsync_pl_fuzz` (two or three handles writing one
  file, with desyncs, resyncs and syncs) passes under `-Plinear` in B-DEF.
- **Fail:** the file matches no handle's synced state.
- **Verified by:** `powerloss_p1::fsync_pl_fuzz`.
- **Status:** Tested on v3-integration (5e8c3080).
- **When:** every CI run.

#### LFS3-PL-17

littlefs shall make the data of an `lfs3_file_write` on a handle opened
with `LFS3_O_SYNC` durable when the write returns.

- **Source:** Stated: `lfs3.h:141` ("Sync metadata on every write").
- **Measure:** file content after a power loss at any later point.
- **Pass:** `powerloss_p1::osync_pl` passes under `-Plinear` in B-DEF, with
  `LFS3_O_SYNC` and with `LFS3_M_SYNC`.
- **Fail:** data from a returned write is missing after remount.
- **Verified by:** `powerloss_p1::osync_pl`.
- **Status:** Tested on v3-integration (cba1a7d5).
- **When:** every CI run.

#### LFS3-PL-18

littlefs shall make `lfs3_fs_grow` atomic under power loss: after remount
the block count is either the old or the new value.

- **Source:** Derived: grow commits the new geometry (and gbmap) in one mroot
  commit (`lfs3.c:16967-16977`).
- **Measure:** `lfs3_fs_stat` block count after remount.
- **Pass:** `grow::incr_spam_f_pl_fuzz` and `grow::incr_spam_fd_pl_fuzz`
  pass under `-Plinear` in B-DEF (and with `GBMAP=true` in B-YGB).
- **Fail:** any other block count, or a mount failure.
- **Verified by:** as listed.
- **Status:** Tested (B-DEF). The `GBMAP=true` permutations run only in gbmap
  builds.
- **When:** every CI run.

#### LFS3-PL-19

littlefs shall make `lfs3_fs_mkgbmap` and `lfs3_fs_rmgbmap` atomic under
power loss: after remount the gbmap is either fully enabled or fully
disabled.

- **Source:** Derived: the wcompat flag and the gbmap delta are committed
  together (`lfs3.c:17002-17096`).
- **Measure:** `LFS3_I_GBMAP` in `lfs3_fs_stat`, and `lfs3_fs_ck` result,
  after remount.
- **Pass:** NEW reentrant variants of `gbmap::mkgbmap`, `gbmap::rmgbmap`,
  `gbmap::rmmkgbmap` and `gbmap::mkrmgbmap` pass under `-Plinear` in B-BIG.
- **Fail:** mount fails, `lfs3_fs_ck` fails, or the flag disagrees with the
  on-disk state.
- **Verified by:** NEW.
- **Status:** Untested.
- **When:** nightly.

#### LFS3-PL-20

littlefs shall survive a power loss during any janitorial work: gc,
traversal-driven work and mount-time work.

- **Source:** Derived: janitorial work commits (compaction, mkconsistent,
  gbmap sync) like any other operation.
- **Measure:** mount result and file contents after remount.
- **Pass:** NEW reentrant cases that call `lfs3_fs_gc`, `lfs3_fs_ck`, RDWR
  traversals with every `LFS3_T_*` work flag, and `lfs3_mount` with every
  `LFS3_M_*` work flag, pass under `-Plinear` in B-BIG.
- **Fail:** any permutation fails.
- **Verified by:** NEW. `test_gc` and `test_trvs` have no reentrant case.
- **Status:** Untested.
- **When:** nightly.

#### LFS3-PL-21

littlefs shall survive a torn write during wear-levelling relocation.

- **Source:** Derived: relocation writes new blocks and switches references
  in a parent commit (`lfs3.c:8786-8822`).
- **Measure:** reentrant case results.
- **Pass:** `relocations::spam_f_pl_fuzz` and `relocations::spam_fd_pl_fuzz`
  (`BLOCK_RECYCLES` 0, 1, 4) pass under `-Plinear` with PLB-TORN in B-DEF.
- **Fail:** any permutation fails.
- **Verified by:** as listed.
- **Status:** Partly tested (ATOMIC only; `test_powerloss` uses
  `BLOCK_RECYCLES=-1`).
- **When:** nightly.

#### LFS3-PL-22

littlefs shall format and then mount successfully after a power loss
during `lfs3_format`.

- **Source:** Derived: a device that loses power while being formatted must
  remain usable.
- **Measure:** `lfs3_format` and `lfs3_mount` results after the loss.
- **Pass:** a NEW reentrant case that formats, then formats and mounts again
  after each loss, passes under `-Plinear` with PLB-TORN in B-DEF and B-YGB.
- **Fail:** format or mount fails.
- **Verified by:** NEW.
- **Status:** Untested.
- **When:** every CI run.

#### LFS3-PL-23

littlefs shall pass every reentrant case with one power loss at every
possible write operation.

- **Source:** Derived: v2 CI ran `-P1` on every suite.
- **Measure:** reentrant case results.
- **Pass:** every reentrant case passes under `-P'permute(1)'` in B-DEF.
- **Fail:** any permutation fails.
- **Verified by:** all reentrant cases.
- **Status:** Untested (not run for v3).
- **When:** nightly.

#### LFS3-PL-24

littlefs shall pass the directory, relocation and power-loss suites with two
power losses at every pair of write operations.

- **Source:** Derived: v2 CI ran `-P2` on `test_dirs` and
  `test_relocations`. A second loss during recovery from the first is the
  case that `-Plinear` does not reach.
- **Measure:** reentrant case results.
- **Pass:** `dirs::*`, `relocations::*` and `powerloss::*` pass under
  `-P'permute(2)'` in B-DEF.
- **Fail:** any permutation fails.
- **Verified by:** as listed.
- **Status:** Untested (not run for v3).
- **When:** nightly.

#### LFS3-PL-25

littlefs shall survive a power loss during relocation away from a bad
block.

- **Source:** Derived: the combination of LFS3-PL-01 and LFS3-FAIL-01/02.
- **Measure:** reentrant case results.
- **Pass:** NEW reentrant variants of `badblocks::region_spam_file_fuzz` and
  `badblocks::alternating_spam_dir_fuzz` pass under `-Plinear` with
  PLB-TORN and `BADBLOCK_BEHAVIOR` in {0, 1} in B-DEF, and in {2, 3, 4} in
  B-BIG.
- **Fail:** any permutation fails.
- **Verified by:** NEW. `test_badblocks` has no reentrant case.
- **Status:** Untested.
- **When:** nightly.

#### LFS3-PL-26

littlefs shall survive a power loss while blocks are wearing out.

- **Source:** Derived: the combination of LFS3-PL-01 and LFS3-FAIL-11.
- **Measure:** reentrant case results, and the NOSPC classification at end
  of life.
- **Pass:** `exhaustion::spam_file_pl_fuzz` passes under `-Plinear` with
  `ERASE_CYCLES=10` in B-DEF, B-YGB and B-BIG. The wear under test is the
  workload's: a format interrupted before the workload starts is repeated,
  and the case resets the wear of every block format writes (the anchor
  blocks 0 and 1, and block 2, the gbmap root, when the gbmap is enabled)
  before each repeat. A format with block 2 worn out is LFS3-FAIL-16 and
  LFS3-BAD-14, not this requirement.
- **Fail:** any permutation fails, or end of life is reported as anything
  other than `LFS3_ERR_NOSPC`.
- **Verified by:** `exhaustion::spam_file_pl_fuzz` (NEW-10).
- **Status:** Tested on `v3-integration` (afdf7f05): passes in B-DEF,
  B-YGB and B-BIG. Before, 60 permutations failed in B-YGB because ten
  interrupted formats wore out block 2 before the workload started
  (issue #4).
- **When:** nightly.

#### LFS3-PL-27

littlefs shall pass `dirs::rm_many_2layers` under power loss with `N=4`.

- **Source:** Stated: the test excludes this permutation because of a known
  bug, "mkdir aligning with an mdir split puts a did in the wrong mdir"
  (`tests/test_dirs.toml:3442`).
- **Measure:** case result.
- **Pass:** the case passes under `-Plinear` in B-DEF with the exclusion
  removed.
- **Fail:** any permutation fails.
- **Verified by:** `dirs::rm_many_2layers`.
- **Status:** Known defect (4-api R27).
- **When:** every CI run.

### 6.3 Error detection and integrity (INT)

#### LFS3-INT-01

littlefs shall treat a commit as visible only if its CRC-32C, computed over
the revision count and every tag and data byte since the previous commit,
matches the stored checksum.

- **Source:** Stated: #1111 "Error detection! - Global-checksums";
  `lfs3.c:2708-3032` (fetch), `4388-4533` (commit).
- **Measure:** which commit a fetch returns for images with a truncated or
  corrupted tail.
- **Pass:** `mtree::truncated_tag`, `mtree::truncated_cksum`,
  `mtree::truncated_ecksum`, `mtree::truncated_gcksumdelta`,
  `ck::ckmeta_easy` and `ck::ckmeta_hard` pass in B-DEF.
- **Fail:** a commit with a bad checksum is used, or a valid earlier commit
  is ignored.
- **Verified by:** as listed.
- **Status:** Tested. (`mtree::truncated_tag` had a buffer overread in the
  test, F-8.)
- **When:** every CI run.

#### LFS3-INT-02

littlefs shall reject a commit whose checksum-tag phase bits differ from the
low two bits of the block address.

- **Source:** Stated: `lfs3.c:2837-2840`; the phase detects images shifted
  by whole blocks.
- **Measure:** mount result for an image with a shifted anchor.
- **Pass:** `mount::incompat_out_of_phase` (`PHASE` 1 to 4) passes in B-DEF.
- **Fail:** the shifted image mounts.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-INT-03

littlefs shall ensure that the erased bytes after a commit never parse as a
valid tag, whatever the erased value.

- **Source:** Stated: #1111 "Pre-erased block tracking" ("We also make no
  assumptions about erase value"); the perturb bit, `lfs3.c:4430-4434`.
- **Measure:** the tags fetch finds after the last commit, for erased values
  0x00, 0xff and random bytes.
- **Pass:** `rbyd::*` pass with `ERASE_VALUE` in {0xff, 0x00, -1}, and
  `rbyd::erased_values`, which fills the erased region after each commit
  with every byte value 0x00 to 0xff and with random data, and with a noop
  erase fills the whole block before the commits, finds no extra commit, in
  B-DEF.
- **Fail:** fetch accepts bytes that were never written as part of a
  commit.
- **Verified by:** `rbyd::*` (indirect); `rbyd::erased_values`.
- **Status:** Tested on v3-integration (f6e23134).
- **When:** every CI run.

#### LFS3-INT-04

littlefs shall not append a commit to an rbyd whose next prog unit fails its
erased-state checksum (ecksum).

- **Source:** Stated: `lfs3.c:2655-2682`; appending onto a torn or disturbed
  region would mix old and new bits.
- **Measure:** whether the next commit appends or compacts.
- **Pass:** `mtree::ecksum_disturb`, which flips the valid bit of the first
  byte after the last commit of an mdir, a bit of the last byte of that prog
  unit, or a bit of the last byte the ecksum covers, remounts and commits,
  finds the commit off the disturbed bytes, in B-DEF with `PROG_SIZE`
  in {1, 16} and `ERASE_VALUE` in {0xff, 0x00, -1}: either the commit
  compacts the mdir into its other block, or, since a disturbed erased
  region reads as a torn tail, the mount settles the mdir into its other
  block (LFS3-DEG-12) and the commit compacts the settled copy into the
  disturbed block, after erasing it. Without a flip the commit appends.
- **Fail:** the commit is appended to the disturbed block.
- **Verified by:** `mtree::ecksum_disturb`.
- **Status:** Tested on v3-integration (f6e23134); with the settling mount
  tested on `v3-rc` (`1a17402f`).
- **When:** every CI run.

#### LFS3-INT-05

littlefs shall compute CRC-32C (polynomial 0x1edc6f41, initial value and
final XOR 0xffffffff) in `lfs3_crc32c`, and the crc32c ring product in
`lfs3_crc32c_mul`, in every implementation option.

- **Source:** Stated: `lfs3_util.h:764-773`; the on-disk checksums depend on
  it.
- **Measure:** results against reference vectors.
- **Pass:** `ck::crc32c`, `ck::crc32c_incr`, `ck::crc32c_mul` and
  `ck::crc32c_mul_dist` pass in B-DEF and in builds with
  `LFS3_SMALLER_CRC32C`, `LFS3_FASTER_CRC32C` and `LFS3_PMUL_CRC32C`.
- **Fail:** any case fails, or an implementation option does not build.
- **Verified by:** as listed.
- **Status:** Partly tested (default implementation only; the PMUL option
  does not compile, LFS3-BUILD-05).
- **When:** every CI run.

#### LFS3-INT-06

littlefs shall fail `lfs3_mount` with `LFS3_ERR_CORRUPT` when any mdir other
than the one committed most recently shows an older commit than the one the
filesystem was built on.

- **Source:** Stated: #1111 "Global-checksums" (gcksums "prevent rollback
  issues caused by naive checksumming"); `lfs3.c:15869-15901`.
- **Measure:** mount result after an older commit of one mdir is restored.
- **Pass:** `ck::rollback` (commit X to mdir A, commit Y to mdir B, restore
  A's blocks from before X) gets `LFS3_ERR_CORRUPT` from mount with
  `LFS3_M_RDWR` and with `LFS3_M_RDONLY`, with A and B the mroot or mdirs in
  the mtree, in B-DEF.
- **Fail:** the mount succeeds.
- **Verified by:** `ck::ckmeta_hard` with `METHOD=3` (indirect, via bit
  flips); `ck::rollback`.
- **Status:** Tested on v3-integration (6d53943f).
- **When:** every CI run.

#### LFS3-INT-07

littlefs shall return the same `lfs3_fs_cksum` value before and after a
remount, and before and after any sequence of read-only calls.

- **Source:** Stated: `lfs3.h:1752-1766` (the checksum "can be stored
  externally").
- **Measure:** `lfs3_fs_cksum` values.
- **Pass:** `ck::cksum` passes in B-DEF.
- **Fail:** the value changes without a mutation.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-INT-08

littlefs shall change the `lfs3_fs_cksum` value whenever a successful call
changes a file's content, a name or an attribute.

- **Source:** Stated: `lfs3.h:1754` ("a checksum of all metadata + data").
  Data is covered through the B-tree and bptr checksums that the mdirs hold.
- **Measure:** `lfs3_fs_cksum` before and after each mutating call.
- **Pass:** a NEW fuzz case records the checksum after every successful
  mutating call that changes the model and finds no repeated consecutive
  value over 10,000 operations, in B-DEF.
- **Fail:** a model change leaves the checksum unchanged (collisions at the
  2^-32 rate aside; the fixed seed makes the run deterministic).
- **Verified by:** NEW.
- **Status:** Untested.
- **When:** every CI run.

#### LFS3-INT-09

littlefs shall change the `lfs3_fs_cksum` value when the most recent commit
is rolled back, so that a checksum stored outside the device detects the
rollback.

- **Source:** Stated: #1111 "Global-checksums" ("This checksum can be stored
  external to the filesystem to provide protection against last-commit
  rollback issues"). Mount cannot detect this rollback by design
  (`tests/test_ck.toml:615-623`).
- **Measure:** `lfs3_fs_cksum` before the last commit, after it, and after
  restoring the pre-commit block.
- **Pass:** `ck::rollback` finds the mount succeeds, the checksum after the
  rollback differs from the value after the commit and equals the value
  before it, and namespace operations after the rollback work, in B-DEF.
- **Fail:** the checksums are equal.
- **Verified by:** `ck::ckmeta_hard` comments and `METHOD=3` (indirect);
  `ck::rollback`.
- **Status:** Tested on v3-integration (6d53943f).
- **When:** every CI run.

#### LFS3-INT-10

littlefs shall return `LFS3_ERR_CORRUPT` from `lfs3_fs_ck` with
`LFS3_CK_CKMETA` when any metadata block (mdir or B-tree node) has a flipped
bit in its committed region.

- **Source:** Stated: `lfs3.h:1780-1787`.
- **Measure:** return value.
- **Pass:** `ck::ckmeta_easy` and `ck::ckmeta_hard` with `METHOD=0` pass in
  B-DEF.
- **Fail:** 0 is returned for a corrupted image.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-INT-11

littlefs shall return `LFS3_ERR_CORRUPT` from `lfs3_fs_ck` with
`LFS3_CK_CKDATA` when any data block has a flipped bit within its checksummed
range.

- **Source:** Stated: `lfs3.h:143-144, 1780-1787`.
- **Measure:** return value.
- **Pass:** `ck::ckdata_easy` and `ck::ckdata_hard` with `METHOD=0` pass in
  B-DEF.
- **Fail:** 0 is returned for a corrupted image.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-INT-12

littlefs shall fail `lfs3_mount` with `LFS3_ERR_CORRUPT` when mounted with
`LFS3_M_CKMETA` or `LFS3_M_CKDATA` on an image with a corrupted metadata or
data block, respectively.

- **Source:** Stated: `lfs3.h:284-285`; #1111 "Global-checksums".
- **Measure:** mount result.
- **Pass:** `mount::t_ckmeta`, `mount::t_ckdata`, and `ck::ckmeta_*`,
  `ck::ckdata_*` with `METHOD=3` pass in B-DEF.
- **Fail:** the mount succeeds.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-INT-13

littlefs shall return `LFS3_ERR_CORRUPT` from `lfs3_file_ck` with
`LFS3_CK_CKDATA`, and from `lfs3_file_opencfg` with `LFS3_O_CKDATA`, when
any B-tree node or data block of that file has a flipped bit in a byte a
checksum covers: the checksummed prefix of a data block, or the revision
count, tags and data of a B-tree node up to the end of the commit holding
its trunk. No checksum covers the padding after a commit's checksum.

- **Source:** Stated: `lfs3.h:143-144, 1648-1652`.
- **Measure:** return value.
- **Pass:** `ck::file_ckmeta_easy`, `ck::file_ckmeta_hard`,
  `ck::file_ckdata_easy` and `ck::file_ckdata_hard` pass in B-DEF, and
  `ck::file_ckdata_blocks`, which flips the first and last covered bit, the
  first and last data bit and random covered bits of every data block and
  B-tree node of files of half a block to 8.5 blocks, written whole or in
  syncs, gets `LFS3_ERR_CORRUPT` from `lfs3_file_ck`, from an open with
  `LFS3_O_CKDATA` and from `lfs3_fs_ck` with `LFS3_CK_CKDATA`, in B-DEF and
  B-BIG.
- **Fail:** 0 is returned for a corrupted file.
- **Verified by:** as listed; `ck::file_ckdata_blocks`.
- **Status:** Tested on v3-integration (23b617c1). The file's own mdir entry
  and inline data are open question Q13.
- **When:** every CI run.

#### LFS3-INT-14

littlefs shall return `LFS3_ERR_CORRUPT` from `lfs3_trv_read` on a
traversal opened with `LFS3_T_CKMETA` or `LFS3_T_CKDATA` when it reaches a
corrupted block of the checked kind.

- **Source:** Stated: `lfs3.h:375-376`; #1111 "Better traversal APIs".
- **Measure:** return value.
- **Pass:** `trvs::ckmdir_*`, `trvs::ckbtree_*` and `trvs::ckdata_*` pass in
  B-DEF.
- **Fail:** the traversal completes without the error.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-INT-15

littlefs shall return `LFS3_ERR_CORRUPT` from `lfs3_fs_gc` with
`LFS3_GC_CKMETA` or `LFS3_GC_CKDATA` in `gc_flags` when the checked data is
corrupted.

- **Source:** Stated: `lfs3.h:446-447`; #1111 "Global-checksums".
- **Measure:** return value.
- **Pass:** `ck::ckmeta_*` and `ck::ckdata_*` with `METHOD=1` pass in B-BIG.
- **Fail:** gc completes without the error.
- **Verified by:** as listed.
- **Status:** Partly tested (compiled out of B-DEF; needs `LFS3_GC`).
- **When:** every CI run.

#### LFS3-INT-16

littlefs shall set `LFS3_I_CKMETA` and `LFS3_I_CKDATA` again after
`lfs3_fs_unck` with the matching flag, so that the next gc or check repeats
the scan.

- **Source:** Stated: `lfs3.h:1801-1811`.
- **Measure:** `lfs3_fs_stat` flags before and after.
- **Pass:** `ck::*_hard` and `gc::ckmeta_unck`, `gc::ckdata_unck`,
  `gc::iflags_unck` pass in B-BIG.
- **Fail:** the flag stays clear, or the scan is not repeated.
- **Verified by:** as listed.
- **Status:** Partly tested (the `gc::*` cases need `LFS3_GC`).
- **When:** every CI run.

#### LFS3-INT-17

littlefs shall, when mounted with `LFS3_M_CKPROGS`, read back every prog
and treat a mismatch as a bad block, so that the operation completes on
another block.

- **Source:** Stated: #1111 "Global-checksums" ("Closed checking of data
  during progs"); `lfs3.h:193, 256`.
- **Measure:** operation result and file content with PROGFLIP blocks.
- **Pass:** `ck::ckprogs_mroot`, `ck::ckprogs_data`, `ck::ckprogs_btree`,
  `ck::ckprogs_overrecycling` and `ck::spam_*_fuzz` with `METHOD=0` pass in
  B-BIG.
- **Fail:** a mismatched prog is accepted, or the operation fails while free
  good blocks remain.
- **Verified by:** as listed.
- **Status:** Partly tested (compiled out of B-DEF).
- **When:** every CI run.

#### LFS3-INT-18

littlefs shall, when mounted with `LFS3_M_CKFETCHES`, verify the checksum of
each B-tree node and data block on first use and return `LFS3_ERR_CORRUPT` on
a mismatch.

- **Source:** Stated: #1111 "Global-checksums" ("Optimistic (not closed)
  checking of data during fetches"); `lfs3.h:258-261`.
- **Measure:** return value of the call that first uses the block.
- **Pass:** `ck::ckfetches_mroot`, `ck::ckfetches_data`,
  `ck::ckfetches_btree` and `ck::spam_*_fuzz` with `METHOD=2` pass in B-BIG.
- **Fail:** the corrupted block is used without the error.
- **Verified by:** as listed.
- **Status:** Partly tested (compiled out of B-DEF).
- **When:** every CI run.

#### LFS3-INT-19

littlefs shall, when mounted with `LFS3_M_CKMETAPARITY`, return
`LFS3_ERR_CORRUPT` when a single bit of a fetched tag or its data reads
differently from when it was fetched and the flip leaves the tag's length
intact: where the tag's encoding ends and where its data ends. A flip that
changes either (a leb128 continuation bit, a bit of the size, the alt bit)
moves the parity bit the check reads, and can be missed. This is the "most
flipped bits" of `lfs3.h`.

- **Source:** Stated: `lfs3.h:262-265` ("Check metadata tag parity bits")
  and the prog callback's note ("instead of most flipped bits");
  `lfs3.c:1368-1441`. The limit on length-changing flips was decided on
  2026-10-04 (issue #7, NEW-62), with no on-disk change.
- **Measure:** return value of the read.
- **Pass:** `ck::ckparity_mroot` and `ck::ckparity_btree` with PROGFLIP and
  READFLIP pass in B-BIG, and `ck_readflip::spam` finds no miss in class 0
  (length-preserving flips in the mroot and B-tree nodes, and flips in data
  blocks), with the bit metastable and with it flipped for the round, with
  and without `LFS3_M_CKFETCHES`, in B-BIG. It counts the misses of class 1
  (length-changing flips) and class 2 (mdirs in the mtree, see below), and
  the damage they leave for a remount to find, without failing on them.
- **Fail:** the flipped value is returned without the error.
- **Verified by:** as listed; `ck_readflip::spam` (NEW-62, in
  `tests/test_ck_readflip.toml`). An mdir in the mtree is fetched again on
  each lookup, and a fetch that reads a flipped bit falls back to the older
  commit without an error. That is not a parity question, and not issue
  #6 either: a read the block device fails is read again and never falls
  back (LFS3-INT-26), but a flipped bit read without an error can't be
  told from a commit a power loss interrupted, and reading again until it
  reads whole would take a commit that comes and goes for a whole one
  (LFS3-DEG-14). Class 2 of the case counts these.
- **Status:** Tested on v3-integration (59b39cf6, 6a0d2e1c). D-5 is resolved
  by the restatement.
- **When:** every CI run.

#### LFS3-INT-20

littlefs shall, when mounted with `LFS3_M_CKDATACKSUMS`, verify the checksum
of the whole checksummed range of a data block on every read of it and
return `LFS3_ERR_CORRUPT` on a mismatch.

- **Source:** Stated: `lfs3.h:266-269` ("Check data checksums on reads").
- **Measure:** return value of `lfs3_file_read`.
- **Pass:** `ck::ckdatacksums_data` and `ck::spam_*_fuzz` with `METHOD=3`
  pass in B-BIG.
- **Fail:** corrupted data is returned without the error.
- **Verified by:** as listed.
- **Status:** Known defect (F-2: the option did not compile at `b10efaa`;
  fixed on v3-fixes, where the cases pass in a B-BIG run, Appendix B.3; never
  run in upstream CI).
- **When:** every CI run.

#### LFS3-INT-21

littlefs shall return `LFS3_ERR_CORRUPT` from the next metadata check after
any mdir changes on disk while mounted.

- **Source:** Derived: the check compares the XOR of the mdir checksums with
  the in-RAM gcksum (`lfs3.c:10153-10162`).
- **Measure:** return value of `lfs3_fs_ck(LFS3_CK_CKMETA)` after an mdir
  block is replaced by an older copy behind littlefs's back.
- **Pass:** `ck::rollback` gets `LFS3_ERR_CORRUPT` from `lfs3_fs_ck` with
  `LFS3_CK_CKMETA` after an mdir's blocks are replaced by older copies while
  mounted, and namespace operations on top of the rollback return 0 or
  `LFS3_ERR_CORRUPT` and never assert, in B-DEF.
- **Fail:** the check returns 0.
- **Verified by:** `ck::ckmeta_hard` (indirect); `ck::rollback`.
- **Status:** Tested on v3-integration (6d53943f).
- **When:** every CI run.

#### LFS3-INT-22

littlefs shall return `LFS3_ERR_CORRUPT` from `lfs3_fs_ck` with
`LFS3_CK_CKMETA` when a gbmap B-tree node has a flipped bit.

- **Source:** Derived: gbmap nodes are metadata and are traversed as B-tree
  blocks (`lfs3.c:9913-9935`).
- **Measure:** return value.
- **Pass:** `ck::ckmeta_gbmap`, which flips the first and last covered bit
  and random covered bits of every gbmap node of a gbmap of about 25 nodes,
  gets `LFS3_ERR_CORRUPT` from `lfs3_fs_ck` with `LFS3_CK_CKMETA`, from a
  traversal with `LFS3_T_CKMETA` and from a mount with `LFS3_M_CKMETA`, in
  B-YGB and B-BIG.
- **Fail:** the check returns 0.
- **Verified by:** `ck::ckmeta_gbmap`.
- **Status:** Tested on v3-integration (36762484). Compiled out of B-DEF;
  runs in B-YGB and B-BIG.
- **When:** every CI run.

#### LFS3-INT-23

littlefs shall not return `LFS3_ERR_CORRUPT` because of
`LFS3_M_CKMETAPARITY` when no metadata bit has changed.

- **Source:** Derived: a check that fails on valid data makes the
  filesystem unusable. The byte after a commit's CKSUM tag is the next
  commit's valid bit, or erased state that the perturb bit makes
  intentionally invalid; it is not the CKSUM tag's parity.
- **Measure:** results of writes after remount, and of mounts with work
  flags, with `LFS3_M_CKMETAPARITY` set and `LFS3_M_CKFETCHES` clear.
- **Pass:** `ck::ckparity_btree_append` (appends to B-tree files after
  remounts, with and without the gbmap), `mount::flags` and
  `mount::format_flags` pass in B-BIG.
- **Fail:** any `LFS3_ERR_CORRUPT` on an unmodified image.
- **Verified by:** `mount::flags`, `mount::format_flags`;
  `ck::ckparity_btree_append` (added with the fix).
- **Status:** Known defect (D-2: `lfs3_bd_readtag` parity-checks CKSUM tags
  during quick fetches, so every B-tree commit to an rbyd not fetched since
  mount fails; 1275 permutations of `mount::flags` and `mount::format_flags`
  fail in B-BIG).
- **When:** every CI run.

#### LFS3-INT-24

littlefs shall, when mounted with the check option that covers the source of
a copy, return `LFS3_ERR_CORRUPT` instead of copying a flipped bit under a
fresh checksum. `LFS3_M_CKMETAPARITY` covers mdir compaction, B-tree node
compaction and the crystallization of fragments; `LFS3_M_CKFETCHES` covers
B-tree node compaction and crystallization from a data block;
`LFS3_M_CKDATACKSUMS` covers crystallization from a data block.

- **Source:** Proposal, from the results of NEW-63 (`ck::launder`): without
  the covering option every copy tried put the flipped bit under a fresh
  checksum, after which no check finds it; only a check run before the copy
  does. Documenting this is LFS3-DOC-13.
- **Measure:** result of the call that copies, and of `lfs3_fs_ck` before and
  after it.
- **Pass:** `ck::launder` passes in B-DEF and B-BIG: with the covering option
  the copy returns `LFS3_ERR_CORRUPT` and a check still finds the flip, and
  in every case `lfs3_fs_ck` before the copy returns `LFS3_ERR_CORRUPT`.
- **Fail:** the copy succeeds with the covering option, or a check before
  the copy misses the flip.
- **Verified by:** `ck::launder`.
- **Status:** Tested on v3-integration (69ce4c06). Without the covering
  option every copy tried laundered the flip.
- **When:** every CI run.

#### LFS3-INT-25

littlefs shall, when mounted with `LFS3_M_CKFETCHES`, verify each B-tree
node and data block against its stored checksum when it fetches it, and
return `LFS3_ERR_CORRUPT`, never the wrong data, for a flipped bit present
at that fetch. A bit that reads clean at the fetch and flipped at a later
read (a metastable bit) can be missed, and so can a flip in a block that is
not fetched again: the mroot while mounted, and the B-tree roots and cached
leaves held in RAM. The repairing check of issue #19 (`ck_passes`) is the
tool for those. An mdir in the mtree that falls back to an older commit when
fetched: a read that failed never makes it (LFS3-INT-26), a flipped bit
can, see LFS3-INT-19.

- **Source:** Proposal, decided on 2026-10-04 (issue #7, NEW-62): narrowed
  to what fetch checks do, `lfs3.h:283` ("Check block checksums before first
  use"), with no on-disk change and no per-rbyd buffer. Repeated reads catch
  a metastable bit only by chance.
- **Measure:** results of reads while one bit of a metadata block is flipped
  on the device for a round (`FLIP=1`), or reads differently on each read
  (`FLIP=0`, READFLIP); whether the block was fetched after the flip.
- **Pass:** `ck_readflip::spam` with `CK=1` (`LFS3_M_CKFETCHES`, with
  `LFS3_M_CKMETAPARITY` and `LFS3_M_CKDATACKSUMS`) and `FLIP=1` finds no
  miss in class 1 for a B-tree node fetched after the flip, in B-BIG; misses
  from flips not present at a fetch are counted and reported.
- **Fail:** a miss for a B-tree node fetched while the flipped bit was on
  the device.
- **Verified by:** `ck_readflip::spam` with `CK=1` and `FLIP=1`. The case
  tells a fetch by its read of the revision count, which lookups never read.
- **Status:** Tested on v3-integration (6a0d2e1c). D-6 is resolved by the
  restatement.
- **When:** every CI run.

#### LFS3-INT-26

littlefs shall never take the older block of a metadata pair for its latest
state, or a shorter log for the whole one, because the block device failed
a read: when a read of either revision count, or of the log of the block
it fetches, fails, it shall read the pair again, up to `ck_retries` times,
and then return `LFS3_ERR_CORRUPT` for the pair, at mount and while
mounted.

- **Source:** Proposal (issue #6; principles 1, 2 and 4; LFS3-ERR-05). A
  read can fail while the supply is low and pass later, so a failed read
  says nothing about which block is newer or where its log ends. At
  `6f80e646` one `LFS3_ERR_CORRUPT` from the read callback, on the
  revision count or the log of the newer block, made the fetch take the
  older block or stop the log early. When the gcksum couldn't tell (the
  mroot as the only mdir, or the mdir committed last) mount succeeded and
  every commit since was lost, without an error; a lookup of an mdir in
  the mtree, which fetches it again, returned the older state the same
  way while mounted. A pair whose only readable block is the older one
  is damaged: a read-only mount serves the rest of the filesystem
  (LFS3-DEG-03), a read-write mount fails. Only a block that can't be
  the newer is passed over: the mroot's other block, while mounted, when
  littlefs knows its newer block, and a block littlefs failed to write as
  the pair's next state, kept in RAM (`LFS3_MFAILED_SIZE`) until it is
  erased again: a compaction that fails into a block that then doesn't
  read, as a worn block does, must not stop the pair. A read-only mount
  builds nothing on what it reads, so it takes what reads of any pair,
  and says so (LFS3-DEG-03). After a remount nothing records a failed
  compaction, so at the end of a device's life a read-write mount can
  fail where a read-only one mounts degraded.
- **Measure:** results of mount and of lookups while mounted, and what
  they return, with each read of a mount failing in turn with
  `LFS3_ERR_CORRUPT`, and with the newer block of an mdir in the mtree
  failing its next 1, 3 or 64 reads while mounted, with `ck_retries` 0
  and 3.
- **Pass:** `mount::readerror` with `ERR` `LFS3_ERR_CORRUPT`: each mount
  returns `LFS3_ERR_CORRUPT`, or 0 with every file and directory as
  written and `lfs3_fs_ck` returning 0; `mount::readerror_mounted`:
  `lfs3_get` returns the second version when no more reads fail than
  `ck_retries`, else `LFS3_ERR_CORRUPT`, never the first version, and
  everything reads once the block does; `powerloss::settle_newer`: a
  read-write mount returns `LFS3_ERR_CORRUPT` while the newer block fails
  more reads than `ck_retries`, and finds the second version once it
  reads; `badblocks::mrootanchor_wear`, `badblocks::mrootanchor_stuck`
  and `badblocks::grow` with READERROR: a read-write mount of an anchor
  with a block that doesn't read returns `LFS3_ERR_CORRUPT`, a read-only
  one mounts degraded, and writes go on while mounted after the mroot's
  other block stops reading; `exhaustion::*` with READERROR and
  `LFS3_M_CKPROGS`: writes go on until NOSPC, and the read-only mount
  after it reads every synced file, degraded where a failed compaction
  left a block that doesn't read; in B-DEF, B-YGB and B-BIG.
- **Fail:** an older state, or a shorter log, returned without an error.
- **Verified by:** `mount::readerror` (NEW-140),
  `mount::readerror_mounted` (NEW-141), `powerloss::settle_newer`
  (NEW-137), `badblocks::mrootanchor_wear`,
  `badblocks::mrootanchor_stuck`, `badblocks::grow`.
- **Status:** Known defect at `6f80e646` (issue #6): every permutation of
  `mount::readerror` with `LFS3_ERR_CORRUPT` and of
  `mount::readerror_mounted` found the older state. Fixed and tested on
  `v3-r21` (`862e3695`).
- **When:** every CI run.

### 6.4 Flash failure handling (FAIL)

The block device contract (`lfs3.h:468-493`) lets `prog` and `erase` return
`LFS3_ERR_CORRUPT` for a bad block. Silent failures are caught at write time
only with `LFS3_M_CKPROGS`, and otherwise by checksums later. The table at
the end of this section maps every failure class to its requirements.

#### LFS3-FAIL-01

littlefs shall complete an operation on another block when a prog returns
`LFS3_ERR_CORRUPT`, while free good blocks remain.

- **Source:** Stated: `lfs3.h:473-475`; relocation at every write site
  (`lfs3.c:8121-8128`, `8705-8822`, `8978-9011`, `5861-6181`, `13644-13662`).
- **Measure:** operation result and file content.
- **Pass:** `badblocks::every_*`, `badblocks::region_*` and
  `badblocks::alternating_*` pass with `BADBLOCK_BEHAVIOR=0` in B-DEF.
- **Fail:** an error while free good blocks remain, or wrong content.
- **Verified by:** as listed (24 cases).
- **Status:** Tested. The blocks are marked bad before the test starts; a
  block that goes bad after data was written is LFS3-FILE-11.
- **When:** every CI run.

#### LFS3-FAIL-02

littlefs shall complete an operation on another block when an erase returns
`LFS3_ERR_CORRUPT`, while free good blocks remain.

- **Source:** Stated: `lfs3.h:481-484`; `lfs3.c:11251-11257` (allocation),
  `8168-8171` (mdir swap).
- **Measure:** operation result and file content.
- **Pass:** `badblocks::every_*`, `badblocks::region_*` and
  `badblocks::alternating_*` pass with `BADBLOCK_BEHAVIOR=1` in B-DEF.
- **Fail:** an error while free good blocks remain, or wrong content.
- **Verified by:** as listed.
- **Status:** Tested. Pre-erase is the exception (LFS3-PRE-06).
- **When:** every CI run.

#### LFS3-FAIL-03

littlefs shall, with `LFS3_M_CKPROGS`, complete an operation on another
block when the readback of a just-programmed block returns
`LFS3_ERR_CORRUPT`.

- **Source:** Derived from LFS3-INT-17.
- **Measure:** operation result and file content.
- **Pass:** `badblocks::*` and `exhaustion::*` pass with
  `BADBLOCK_BEHAVIOR=2` and `CKPROGS=true` in B-BIG.
- **Fail:** an error while free good blocks remain, or wrong content.
- **Verified by:** as listed.
- **Status:** Partly tested (the `CKPROGS=true` permutations are compiled out
  of B-DEF).
- **When:** every CI run.

#### LFS3-FAIL-04

littlefs shall return `LFS3_ERR_CORRUPT` from any call that needs to read a
metadata block that returns read errors, and shall not allocate any block
while it cannot read every block that might reference it.

- **Source:** Derived: allocating without knowing what an unreadable block
  references could overwrite live data (`lfs3.c:11174-11187`). The second
  half is what makes the first safe.
- **Measure:** return values and file content after a live, non-anchor mdir
  or B-tree node starts returning `LFS3_ERR_CORRUPT` on read.
- **Pass:** a NEW case (READERROR on a live mdir, a file B-tree node, and a
  gbmap node) finds `LFS3_ERR_CORRUPT` from reads and writes that touch the
  block, finds no prog or erase to any block referenced before the failure,
  and finds files in other mdirs still readable with `LFS3_M_RDONLY`, in
  B-DEF and B-YGB.
- **Fail:** any other error, a write to a referenced block, or a crash.
- **Verified by:** NEW.
- **Status:** Untested. The read-only half: LFS3-DEG-03 (Q6 settled),
  allocation: LFS3-DEG-04.
- **When:** every CI run.

#### LFS3-FAIL-05

littlefs shall, with `LFS3_M_CKPROGS`, complete an operation on another
block when a prog silently does nothing.

- **Source:** Derived from LFS3-INT-17.
- **Measure:** operation result and file content.
- **Pass:** `badblocks::*` and `exhaustion::*` pass with
  `BADBLOCK_BEHAVIOR=3` and `CKPROGS=true` in B-BIG.
- **Fail:** an error while free good blocks remain, or wrong content.
- **Verified by:** as listed.
- **Status:** Partly tested (compiled out of B-DEF).
- **When:** every CI run.

#### LFS3-FAIL-06

littlefs shall, with `LFS3_M_CKPROGS`, complete an operation on another
block when an erase silently does nothing.

- **Source:** Derived from LFS3-INT-17.
- **Measure:** operation result and file content.
- **Pass:** `badblocks::*` and `exhaustion::*` pass with
  `BADBLOCK_BEHAVIOR=4` and `CKPROGS=true` in B-BIG.
- **Fail:** an error while free good blocks remain, or wrong content.
- **Verified by:** as listed.
- **Status:** Partly tested (compiled out of B-DEF).
- **When:** every CI run.

#### LFS3-FAIL-07

littlefs shall, without `LFS3_M_CKPROGS`, either mount and return
previously synced data or return `LFS3_ERR_CORRUPT`, after progs or erases
silently did nothing.

- **Source:** Derived: without read-back, a lost prog looks like a power
  loss before the commit, or like a rollback that the gcksum catches.
- **Measure:** mount result and data returned.
- **Pass:** NEW variants of `badblocks::region_spam_file_fuzz` with
  `BADBLOCK_BEHAVIOR` in {3, 4} and `CKPROGS=false` finish with every read
  returning either data that some sync committed or `LFS3_ERR_CORRUPT`, with
  no assert, in B-DEF.
- **Fail:** an assert, a crash, or data that no sync committed.
- **Verified by:** NEW.
- **Status:** Untested. The limitation is documented by LFS3-DOC-12.
- **When:** nightly.

#### LFS3-FAIL-08

littlefs shall detect a prog that lands with a flipped bit: at write time
with `LFS3_M_CKPROGS`, and otherwise by `LFS3_ERR_CORRUPT` from the next
check or fetch that covers the block.

- **Source:** Stated: #1111 "Global-checksums".
- **Measure:** operation result, check result.
- **Pass:** `ck::ckprogs_*` and `ck::spam_*_fuzz` (all `METHOD`s) pass with
  `BADBLOCK_BEHAVIOR=5` in B-BIG.
- **Fail:** flipped data is returned without an error.
- **Verified by:** as listed.
- **Status:** Partly tested (the CKPROGS cases are compiled out of B-DEF).
- **When:** every CI run.

#### LFS3-FAIL-09

littlefs shall return `LFS3_ERR_CORRUPT`, with `LFS3_M_CKMETAPARITY` and
`LFS3_M_CKDATACKSUMS`, when a read of a block returns a flipped bit: always
for data blocks, and for metadata as LFS3-INT-19 says.

- **Source:** Stated: `lfs3.h:262-269`.
- **Measure:** data and errors returned by reads.
- **Pass:** `ck::ckparity_*` and `ck::ckdatacksums_data` pass with
  `BADBLOCK_BEHAVIOR=6` in B-BIG, and `ck_readflip::spam` (READFLIP bad
  blocks under directories and three file layouts) finds no miss in class 0
  in B-BIG.
- **Fail:** flipped data is returned without an error.
- **Verified by:** as listed; `ck_readflip::spam`, see LFS3-INT-19.
- **Status:** Tested on v3-integration (59b39cf6).
- **When:** every CI run.

#### LFS3-FAIL-10

littlefs shall return `LFS3_ERR_CORRUPT` from a metadata or data check that
covers a block whose bits flipped after it was written.

- **Source:** Stated: #1111 "Global-checksums".
- **Measure:** check results after `lfs3_emubd_flip`.
- **Pass:** `ck::spam_dir_fuzz`, `ck::spam_file_fuzz`,
  `ck::spam_fwrite_fuzz`, `ck::spam_uz_fuzz` and `ck::spam_uzd_fuzz` pass
  with `BADBLOCK_BEHAVIOR=7` and `METHOD` 1 to 3 in B-BIG.
- **Fail:** a check returns 0 over a flipped block.
- **Verified by:** as listed.
- **Status:** Partly tested (B-DEF runs only the methods that need no
  feature macro; at `b10efaa` `ck::spam_uz_fuzz` never updated its model
  after a sync, F-9).
- **When:** every CI run.

#### LFS3-FAIL-11

littlefs shall return `LFS3_ERR_NOSPC`, and not `LFS3_ERR_CORRUPT` or an
assert, when wear-out leaves no good block to write.

- **Source:** Stated: `tests/test_exhaustion.toml` header; derived from
  `lfs3.h:93` (NOSPC means no space).
- **Measure:** error returned at end of life.
- **Pass:** `exhaustion::spam_dir_fuzz`, `exhaustion::spam_file_fuzz`,
  `exhaustion::spam_fwrite_fuzz`, `exhaustion::spam_uz_fuzz` and
  `exhaustion::spam_uzd_fuzz` pass with `BADBLOCK_BEHAVIOR` 0 to 4 in B-BIG.
- **Fail:** any other error at end of life.
- **Verified by:** as listed.
- **Status:** Partly tested (behaviours 2 to 4 need `LFS3_CKPROGS`).
- **When:** every CI run.

#### LFS3-FAIL-12

littlefs shall mount with `LFS3_M_RDONLY` and return every file synced
before wear-out made it return `LFS3_ERR_NOSPC`.

- **Source:** Derived: a worn-out device must still give its data back.
- **Measure:** mount result and file contents after end of life.
- **Pass:** NEW: the `exhaustion::*` cases, after the first
  `LFS3_ERR_NOSPC`, remount read-only and compare every file with the model,
  for `BADBLOCK_BEHAVIOR` 0 and 1 in B-DEF.
- **Fail:** the mount fails or any synced file differs.
- **Verified by:** NEW.
- **Status:** Untested.
- **When:** every CI run.

#### LFS3-FAIL-13

littlefs shall spread wear dynamically, so that a device twice as large
lasts at least 1.8 times as many operations.

- **Source:** Stated: `tests/test_exhaustion.toml:13-44`
  (`lifetime(2N) * 1.1 > 2 * lifetime(N)`).
- **Measure:** operation counts to end of life at N and 2N blocks.
- **Pass:** `exhaustion::*` pass in B-DEF.
- **Fail:** the ratio assertion fails.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-FAIL-14

littlefs shall relocate a metadata pair before either of its blocks has been
erased more than `block_recycles + 1` times since the pair was allocated,
when `block_recycles` is 0 or more.

- **Source:** Stated: `lfs3.h:523-530` ("Number of erase cycles before
  metadata blocks are relocated"). The implementation rounds down to a power
  of two (`lfs3.c:15247-15263`), which is within this bound.
- **Measure:** emubd per-block erase counts of mdir blocks between
  relocations.
- **Pass:** a NEW case that commits repeatedly to one mdir with
  `block_recycles` in {0, 1, 4, 16, 100} finds no mdir block erased more
  than `block_recycles + 1` times between relocations, in B-DEF.
- **Fail:** any block exceeds the bound.
- **Verified by:** NEW. `relocations::*` check correctness only.
- **Status:** Untested.
- **When:** every CI run.

#### LFS3-FAIL-15

littlefs shall return `LFS3_ERR_NOSPC`, and keep every file readable, when
an anchor block (block 0 or 1) goes bad and the anchor must change.

- **Source:** Stated: `lfs3.c:9310-9336` ("Stuck mroot"). The anchor cannot
  move.
- **Measure:** error returned and file content after remount.
- **Pass:** `badblocks::mrootanchor_wear` passes in B-DEF.
- **Fail:** any other error, or unreadable files.
- **Verified by:** as listed. The gstate after this error is LFS3-META-10.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-FAIL-16

littlefs shall return an error from `lfs3_format`, and not report success,
when a block it must write during format is bad.

- **Source:** Derived: a format that returns 0 must leave a mountable
  filesystem.
- **Measure:** format result.
- **Pass:** `badblocks::mrootanchor_format` (blocks 0 and 1, all behaviours
  0 to 4) and `ck::ckprogs_mroot` pass in B-DEF, and a NEW case with block 2
  bad and `LFS3_F_GBMAP` passes in B-YGB.
- **Fail:** format returns 0 and the image does not mount.
- **Verified by:** as listed; NEW for block 2.
- **Status:** Partly tested.
- **When:** every CI run.

#### LFS3-FAIL-17

littlefs shall return `LFS3_ERR_CORRUPT`, after at most one relocation
attempt, when a read of the source block fails while compacting or
relocating, instead of treating the failure as a bad destination.

- **Source:** Derived: `lfs3.h:473-475` gives `LFS3_ERR_CORRUPT` from `prog`
  its bad-block meaning; a read error is a different event. At `b10efaa` a
  source read error takes the same `goto relocate` path as a bad prog, so each
  retry allocates and erases a new block until the allocator is exhausted
  (3-alloc B5).
- **Measure:** error returned and emubd erase count.
- **Pass:** a NEW case with READERROR on the source of an mdir compaction, a
  B-tree node relocation and a data-block rewrite gets `LFS3_ERR_CORRUPT`
  with at most one erase per operation, in B-DEF.
- **Fail:** `LFS3_ERR_NOSPC`, or more than one erase.
- **Verified by:** NEW.
- **Status:** Untested (suspected defect, 3-alloc B5).
- **When:** every CI run.

#### LFS3-FAIL-18

littlefs shall return the error that `cfg->sync` returns, and the
filesystem shall still mount and pass `lfs3_fs_ck` afterwards.

- **Source:** Stated: `lfs3.h:489-493` ("Negative error codes are
  propagated").
- **Measure:** return value; mount and check results afterwards.
- **Pass:** a NEW case, using an emubd extension that fails the n-th sync,
  gets that error from the call, and after remount `lfs3_fs_ck` with
  `LFS3_CK_CKMETA | LFS3_CK_CKDATA` returns 0, for every n, in B-DEF.
- **Fail:** another error, a mount failure, or a check failure.
- **Verified by:** NEW: emubd extension.
- **Status:** Untested. The in-RAM side is LFS3-META-10.
- **When:** every CI run.

#### LFS3-FAIL-19

littlefs shall return, unchanged, an error other than `LFS3_ERR_CORRUPT`
that `read`, `prog` or `erase` returns, and the filesystem shall still mount
afterwards.

- **Source:** Stated: `lfs3.h:468-487` ("Negative error codes are
  propagated to the user").
- **Measure:** return value; mount result afterwards.
- **Pass:** a NEW case, using an emubd extension that returns `LFS3_ERR_IO`
  from the n-th operation of each kind, gets `LFS3_ERR_IO`, and the image
  mounts, for every n, in B-DEF.
- **Fail:** another error, or a mount failure.
- **Verified by:** NEW: emubd extension.
- **Status:** Untested.
- **When:** every CI run.

#### LFS3-FAIL-20

littlefs shall meet LFS3-FAIL-01 to FAIL-06 with the gbmap enabled.

- **Source:** Derived: the gbmap changes the allocation and relocation paths.
- **Measure:** bad-block suite results.
- **Pass:** `badblocks::*` (including the B-tree cases that the gbmap build
  now excludes with `ifndef`) and `exhaustion::*` pass in B-YGB with
  `BADBLOCK_BEHAVIOR` 0 to 4.
- **Fail:** any permutation fails or is excluded.
- **Verified by:** as listed.
- **Status:** Untested (never run in upstream CI, and no B-YGB run here;
  `badblocks::*_btree_many`
  are excluded under gbmap).
- **When:** nightly.

#### Failure classes and the requirements that cover them

The rows follow the flash-failure matrix of the analysis (3-alloc §5).

| Failure class | Recovery requirement | Combinations |
|---|---|---|
| Power loss, ATOMIC | PL-01, PL-04 to PL-22 | PL-25 (bad block), PL-26 (wear-out), PRE-05 (pre-erase), PL-20 (gc), PL-21 (relocation) |
| Power loss, SOMEBITS / MOSTBITS / OOO | PL-02 | PL-21, PL-22, PL-25, PRE-05 |
| Power loss, METASTABLE | PL-03 | |
| Power loss, TORNTAIL | PRE-09 | PRE-05 (pre-erase) |
| Bad block, PROGERROR | FAIL-01 | FAIL-20 (gbmap), PL-25, FILE-10, FILE-11 |
| Bad block, ERASEERROR | FAIL-02 | FAIL-20, PL-25, PRE-06 (pre-erase) |
| Bad block, READERROR | FAIL-03 (write time), FAIL-04 (live data), FAIL-17 (source) | FAIL-20 |
| Bad block, PROGNOOP | FAIL-05, FAIL-07 | FAIL-20 |
| Bad block, ERASENOOP | FAIL-06, FAIL-07 | FAIL-20, PRE-08 (pre-erase) |
| Bad block, PROGFLIP | FAIL-08, INT-17 | |
| Bad block, READFLIP | FAIL-09, INT-19, INT-20 | |
| Bad block, MANUAL / bit rot | FAIL-10, INT-10, INT-11, INT-13, INT-22 | |
| Wear-out | FAIL-11, FAIL-12, FAIL-13, FAIL-14 | PL-26, BAD-09 |
| Anchor block failure | FAIL-15, FAIL-16 | |
| NOSPC | ALLOC-03, ALLOC-04, ALLOC-05 | GC-02 (gc near full), MOUNT-23 (grow) |
| sync error | FAIL-18, META-10 | |
| Other bd error (IO) | FAIL-19 | |
| Rollback | INT-06, INT-09, INT-21 | |
| Not injectable yet (5.10) | FAIL-18, FAIL-19, DOC-12 | |

### 6.5 Bad-block tracking (BAD)

Bad-block tracking is release blocker #1 (#1114, 2026-04-22): "This should be
a relatively easy addition to the gbmap, and would be significantly valuable
by making bd-level error-correction practical." #1111 ("Bad block tracking")
adds that "there are a few unanswered questions around the API and how to
handle bad blocks detected in rdonly contexts".

None of this exists at `b10efaa`, where the gbmap reserves `LFS3_TAG_BMBAD`
(`lfs3.h:865`) but nothing writes it. On `v3-integration` (94ecb238..9d6b2fd1,
4f6d5ef8) it is part of `LFS3_GBMAP`, with no separate option: a block whose
erase or prog returns `LFS3_ERR_CORRUPT`, or whose `LFS3_M_CKPROGS` read-back
fails, waits in a RAM queue of `LFS3_BADQ_SIZE` runs; the allocator skips
queued blocks; once a traversal shows a queued block is no longer referenced,
it is written into the gbmap as a BMBAD range, which the next mdir commit
persists. `lfs3_fs_mkbad`, `lfs3_fs_mkgood` and `lfs3_fs_nextbad` mark, clear
and list bad blocks. The suite is `test_badblocks_gbmap`, compiled with
`LFS3_GBMAP` (B-YGB and B-BIG).

A failed read is different: it does not prove the media is bad, since reads
can fail while the supply is low (issue #19). Blocks that fail a read or a
checksum are recorded as suspect, in RAM only (LFS3-BAD-16), and become bad
only when a repair finds they no longer erase and program cleanly
(LFS3-BAD-17, LFS3-DEG-10).

#### LFS3-BAD-01

littlefs shall not erase or program a block again, for the rest of the
mount, after the block returned `LFS3_ERR_CORRUPT` from an erase or a prog, or
failed an `LFS3_M_CKPROGS` read-back.

- **Source:** Stated: #1111 "Bad block tracking" ("avoiding reuse of blocks
  that are unreliable"). Design: 3-alloc §8.1 G1.
- **Measure:** emubd per-block erase and prog counts.
- **Pass:** in `badblocks_gbmap::recording`, after a block's first failure its
  erase and prog counts do not change until unmount, for `BADBLOCK_BEHAVIOR`
  0 and 1, and for 2 to 5 with `CKPROGS=true`, in B-YGB and B-BIG.
- **Fail:** any later erase or prog of the block.
- **Verified by:** `badblocks_gbmap::recording`,
  `badblocks_gbmap::recording_mdir`, `badblocks_gbmap::queue_full`.
- **Status:** Not implemented at `b10efaa`; tested on `v3-integration`
  (2e8ae492), and on `v3-rc` (`edc41794`) with more runs going bad at once
  than the queue holds. The mroot anchor (blocks 0 and 1) cannot move and is
  exempt.
- **When:** every CI run.

#### LFS3-BAD-02

littlefs shall keep a recorded bad block out of use across remounts.

- **Source:** Stated: #1111 "Efficient block allocation" (the gbmap enables
  "bad-block tracking"). Design: 3-alloc §8.1 G1, §8.2.
- **Measure:** emubd per-block erase and prog counts after remount.
- **Pass:** in `badblocks_gbmap::recording` and `badblocks_gbmap::reading`,
  after the mark is persisted (`LFS3_I_BADBLOCKS` clear), the block's counts
  do not change over remount cycles of continued writes, with allocation
  from the gbmap and from the lookahead buffer.
- **Fail:** any erase or prog of the block after remount.
- **Verified by:** `badblocks_gbmap::recording`, `badblocks_gbmap::reading`,
  `badblocks_gbmap::alloc_skip`.
- **Status:** Not implemented at `b10efaa`; tested on `v3-integration`
  (94ecb238, 2e8ae492, 4f6d5ef8).
- **When:** every CI run.

#### LFS3-BAD-03

littlefs shall record a block as bad only after it failed an erase, a prog
or an `LFS3_M_CKPROGS` read-back (or through `lfs3_fs_mkbad`), shall record
it whether or not the committed filesystem still references it, shall keep
the data in a referenced bad block readable, and shall remain consistent
when power is lost while recording.

- **Source:** Derived (issue #1 follow-up). Blocks that fail while in use
  (an mdir's inactive block after a failed append, a file's last data block)
  can outnumber any RAM queue, one per mdir or file, so a queue that waits
  for their release must forget some, and it forgets them all at unmount.
  The gbmap already keeps a bad mark on a block that is still referenced
  (LFS3-BAD-06), and the allocator treats it as in use, so the mark hides no
  data. This reverses the earlier rule that no referenced block is marked.
  Design: 3-alloc §8.1 G2 (a mark lost to power loss costs only a retry).
- **Measure:** gbmap content against emubd's bad blocks after every power
  loss; reads of files whose blocks went bad.
- **Pass:** `badblocks_gbmap::pl_fuzz` passes under `-Plinear` with PLB-TORN
  and `BADBLOCK_BEHAVIOR` 0 to 4; after every remount every BMBAD block is
  bad in emubd, files whose blocks went bad while in use read back
  (`badblocks_gbmap::queue_inuse`), and
  `lfs3_fs_ck(LFS3_CK_CKMETA | LFS3_CK_CKDATA)` returns 0.
- **Fail:** a block that never failed is marked (other than through
  `lfs3_fs_mkbad` or a merged run, LFS3-BAD-08), a referenced bad block's
  data can't be read, or the check fails.
- **Verified by:** `badblocks_gbmap::pl_fuzz`,
  `badblocks_gbmap::reading_inuse`, `badblocks_gbmap::queue_inuse`.
- **Status:** Not implemented at `b10efaa`; tested on `v3-integration`
  (9d6b2fd1) with no referenced block marked; tested as restated on `v3-rc`
  (`edc41794`).
- **When:** every CI run.

#### LFS3-BAD-04

littlefs shall not write to the block device to record a bad block in an
`LFS3_RDONLY` build or on an `LFS3_M_RDONLY` mount.

- **Source:** Stated: #1111 "Bad block tracking" raises the rdonly question.
  Design: 3-alloc §8.1 G3, §8.5. Read-only contexts report the error and
  record suspects in RAM only (LFS3-BAD-16); open question Q18.
- **Measure:** emubd prog and erase counters.
- **Pass:** `badblocks_gbmap::rdonly`: read-only mounts over images with bad
  blocks (READERROR, READFLIP, MANUAL) issue no prog or erase, and leave
  nothing queued, in B-YGB and B-BIG. `LFS3_RDONLY` builds have no queue.
- **Fail:** any prog or erase.
- **Verified by:** `badblocks_gbmap::rdonly`.
- **Status:** Not implemented at `b10efaa`; tested on `v3-integration`
  (9d6b2fd1) for read-only mounts; B-RO cannot run the suite (LFS3-BUILD-19).
- **When:** every CI run.

#### LFS3-BAD-05

littlefs shall treat a block in a BMBAD range as in use when reading an
image written by a driver that records bad blocks, including when it
allocates from the lookahead buffer, without a new compat flag.

- **Source:** Derived: the gbmap reserved BMBAD from the start, to be treated
  as in use (SPEC.md). Design: 3-alloc §8.1 G4. The decision not to add a
  wcompat flag follows principle 2 (issue #9): marks are advisory, so a
  driver that ignores one treats the block as it would an unmarked bad block
  (a failed erase or prog, then relocation), while a wcompat flag would make
  every such driver refuse to write the filesystem at all.
- **Measure:** erase and prog counts of BMBAD blocks, and mount results, when
  one build fills a disk written by another.
- **Pass:** `compat::gbmap_exchange` (`make test-compat-gbmap`): images with
  BMBAD ranges written by the default, `LFS3_GBMAP` and `LFS3_YES_GBMAP`
  builds mount RDWR in every gbmap build, which fill the disk four times
  without erasing or programming a BMBAD block, and keep the marks.
- **Fail:** a mount error, or a write to a BMBAD block.
- **Verified by:** `compat::gbmap_exchange`.
- **Status:** Partly met. Drivers from `v3-integration` 4f6d5ef8 on keep BMBAD
  blocks out of use. Earlier gbmap drivers, `b10efaa` included, allocate
  them from their lookahead fallback (53 erases of one BMBAD block in
  `compat::gbmap_exchange` without 4f6d5ef8), which costs them a failed
  erase or prog per use, as an unmarked bad block would. v0.0 promises no
  compatibility between alpha drivers (SPEC.md), so no flag guards this.
- **When:** every CI run (`compat::gbmap_exchange` in B-YGB and B-BIG, and
  `make test-compat-gbmap` in the test-compat-gbmap job, 1c768762).

#### LFS3-BAD-06

littlefs shall keep BMBAD ranges when it repopulates, extends or updates the
gbmap.

- **Source:** Derived: marks lost to repopulation would make BAD-02 false.
  At `b10efaa`, `lfs3_gbmap_setbptr` would overwrite BMBAD with BMINUSE for a
  referenced block (3-alloc §8.1 G4).
- **Measure:** BMBAD ranges before and after each operation.
- **Pass:** BMBAD ranges are unchanged after `lfs3_alloc_lookgbmap`,
  incremental gc repopulation, `lfs3_fs_grow`, and a traversal that visits a
  block marked bad while still referenced.
- **Fail:** any mark disappears.
- **Verified by:** `badblocks_gbmap::reading`, `badblocks_gbmap::reading_inuse`,
  `badblocks_gbmap::grow`.
- **Status:** Not implemented at `b10efaa`; tested on `v3-integration`
  (94ecb238, 9c0769c3).
- **When:** every CI run.

#### LFS3-BAD-07

littlefs shall hold pending bad-block marks and suspect blocks in RAM of a
size fixed at compile time, without `lfs3_malloc`.

- **Source:** Derived: littlefs's bounded-RAM design (LFS3-RES-01). Design:
  3-alloc §8.1 G5, §8.3.
- **Measure:** `sizeof(lfs3_t)` and malloc calls.
- **Pass:** `make test-nomalloc` builds the runner with `LFS3_NO_MALLOC` and
  `LFS3_GBMAP`, static cache buffers and per-file cache buffers, and
  `badblocks_gbmap::*` pass in it.
- **Fail:** any `lfs3_malloc` call, or a failure.
- **Verified by:** `make test-nomalloc` (`badblocks_gbmap::*`).
- **Status:** Not implemented at `b10efaa`. On `v3-integration` the queue
  and the suspect list are fixed arrays in `lfs3_t`; tested with
  `make test-nomalloc` (71a6f970, f09acb9d), which also runs `repair::*`,
  and run by the test-nomalloc CI job (1c768762).
- **When:** every CI run.

#### LFS3-BAD-08

littlefs shall never forget a failed block when more blocks go bad between
two allocator checkpoints than the pending-mark queue holds runs for, and
shall keep operating: a block that fails while the queue is full merges the
two closest runs, and the good blocks between them are marked bad with
them. Allocating a data block records the queue first, so a file write
records its failed data blocks as it goes.

- **Source:** Derived (issue #1 follow-up): a full queue that forgot its
  smallest run let the allocator erase and program a known-failed block
  again (`badblocks_gbmap::exhaustion` failed 36 of 256 seeds at
  `3c0afc90`). Under graceful degradation a failed block is never lost
  track of, and writes keep working: stopping the allocator until the
  queue drains would fail writes with `LFS3_ERR_NOSPC` while good blocks
  remain, which an unattended system reads as a full disk, while a few good
  blocks marked with a merged run only cost capacity.
- **Measure:** per-block erase and prog counts, results of operations, and
  gbmap content.
- **Pass:** `badblocks_gbmap::overflow` and `badblocks_gbmap::queue_full`
  (NEW-134): with more separate runs of bad blocks than `LFS3_BADQ_SIZE`
  ahead of the allocator, every write succeeds, each bad block is erased or
  programmed exactly once and is marked, and with up to
  `2*LFS3_BADQ_SIZE` runs no good block is marked; `badblocks_gbmap::
  queue_inuse` (NEW-135): more blocks going bad while in use than the queue
  holds are all marked, read back, and are never erased or programmed again
  after their files release them; `badblocks_gbmap::queue_merge` (NEW-136):
  a failure while the queue is full merges the two closest runs, every
  queued block stays known bad, allocation continues, and a checkpoint
  marks them all with the blocks between the merged runs.
- **Fail:** a bad block erased or programmed twice, a bad block never marked,
  or a write that fails while good blocks remain.
- **Verified by:** `badblocks_gbmap::overflow`, `badblocks_gbmap::queue_full`,
  `badblocks_gbmap::queue_inuse`, `badblocks_gbmap::queue_merge`.
- **Status:** Not implemented at `b10efaa`; tested on `v3-integration`
  (9d6b2fd1) with the queue forgetting its smallest runs; tested as
  restated on `v3-rc` (`edc41794`).
- **When:** every CI run.

#### LFS3-BAD-09

littlefs shall not erase a block again after the erase or prog failure that
made it bad, in the mount where it failed and, once the mark is on disk
(`LFS3_I_BADBLOCKS` clear), in every later mount, to the end of the
device's life.

- **Source:** Derived from BAD-01 and BAD-02 over a device's life
  (3-alloc §8.7 item 10).
- **Measure:** emubd per-block erase counts to end of life.
- **Pass:** `badblocks_gbmap::exhaustion` finds no erase of a block after
  its first failure, outside the mroot anchor, for every `SEED` in
  range(1024), with `BADBLOCK_BEHAVIOR` PROGERROR and ERASEERROR and `SIZE`
  `BLOCK_SIZE/8` and `BLOCK_SIZE`, in B-YGB and B-BIG, and twice the blocks
  give at least 1.82 times the operations.
- **Fail:** a dead block is erased again, or the lifetime ratio drops below
  1.82.
- **Verified by:** `badblocks_gbmap::exhaustion`.
- **Status:** Not implemented at `b10efaa`; tested on `v3-integration`
  (9d6b2fd1) with one erase allowed; tested as restated on `v3-rc`
  (`edc41794`).
- **When:** every CI run.

#### LFS3-BAD-10

littlefs shall record a block as bad, and continue, when pre-erasing it
fails.

- **Source:** Derived: pre-erase meets bad blocks first; see LFS3-PRE-06.
  Design: 3-alloc §8.3.
- **Measure:** gc return value and gbmap content.
- **Pass:** `badblocks_gbmap::preerase`: `lfs3_fs_gc` with `LFS3_GC_PREERASE`
  and a mount with `LFS3_M_PREERASE` over free ERASEERROR (and, with
  CKPROGS, READERROR) blocks return 0, and the blocks are recorded bad.
- **Fail:** gc returns an error, or the block is not recorded.
- **Verified by:** `badblocks_gbmap::preerase`.
- **Status:** Not implemented at `b10efaa`; tested on `v3-integration`
  (9c0769c3), in B-BIG.
- **When:** every CI run.

#### LFS3-BAD-11

littlefs shall provide a call that records a given block as bad, returning
`LFS3_ERR_INVAL` for a block at or beyond `block_count` and for blocks 0 and
1, `LFS3_ERR_NOTSUP` without a gbmap, and `LFS3_ERR_BUSY` for a block in use.

- **Source:** Proposal (3-alloc §8.4), implemented as `lfs3_fs_mkbad`. The
  maintainer has not fixed the API (#1111 "Bad block tracking").
- **Measure:** return values; later allocation behaviour.
- **Pass:** `badblocks_gbmap::api`: the listed blocks give `LFS3_ERR_INVAL`;
  a free block, once recorded, is never allocated; a referenced block gives
  `LFS3_ERR_BUSY`.
- **Fail:** any other result.
- **Verified by:** `badblocks_gbmap::api`, `badblocks_gbmap::factory`.
- **Status:** Not implemented at `b10efaa`; tested on `v3-integration`
  (1e27e318).
- **When:** every CI run.

#### LFS3-BAD-12

littlefs shall provide a call that clears a bad-block record.

- **Source:** Proposal (3-alloc §8.4), implemented as `lfs3_fs_mkgood`, for
  bench testing and for recovering from a false detection.
- **Measure:** allocation of the block after clearing.
- **Pass:** `badblocks_gbmap::api`: after clearing, the block is allocated
  again when free.
- **Fail:** the block stays out of use.
- **Verified by:** `badblocks_gbmap::api`.
- **Status:** Not implemented at `b10efaa`; tested on `v3-integration`
  (1e27e318).
- **When:** every CI run.

#### LFS3-BAD-13

littlefs shall report every recorded bad block, including bad blocks not yet
written to disk.

- **Source:** Proposal (3-alloc §8.4), implemented as `lfs3_fs_nextbad`;
  `lfs3_fs_usage` counts bad blocks as used. `LFS3_I_BADBLOCKS` says some are
  not yet on disk.
- **Measure:** the reported blocks against the emubd bad-block set.
- **Pass:** `badblocks_gbmap::api` and `badblocks_gbmap::api_ibadblocks`:
  after recording k bad blocks, `lfs3_fs_nextbad` lists exactly those k,
  before and after they are written to disk.
- **Fail:** any other set.
- **Verified by:** `badblocks_gbmap::api`, `badblocks_gbmap::api_ibadblocks`,
  `badblocks_gbmap::reading`.
- **Status:** Not implemented at `b10efaa`; tested on `v3-integration`
  (1e27e318).
- **When:** every CI run.

#### LFS3-BAD-14

littlefs shall format with the gbmap when blocks after the mroot anchor are
bad, placing the gbmap root on the first block from 2 on that erases and
programs, recording each block it skipped as bad, and returning
`LFS3_ERR_NOSPC` if no such block exists.

- **Source:** Proposal (3-alloc §8.4). NAND parts ship with factory bad
  blocks. At `b10efaa` the gbmap root is fixed at block 2 ("TODO should we
  try multiple blocks?", `lfs3.c:16130-16133`). A block device that knows a
  block is bad (a factory table) returns `LFS3_ERR_CORRUPT` from its erase
  without erasing it; other known-bad blocks are given to `lfs3_fs_mkbad`
  after format, before anything else is written, which keeps them from ever
  being erased.
- **Measure:** format result, the gbmap root's block, BMBAD ranges, and emubd
  per-block counts during and after format.
- **Pass:** `badblocks::gbmap_format`: with block 2 bad (PROGERROR,
  ERASEERROR, and READERROR, PROGNOOP, ERASENOOP with `LFS3_F_CKPROGS`),
  format with `LFS3_F_GBMAP` returns 0, the root is on another block, block 2
  is listed by `lfs3_fs_nextbad`, and block 2 sees one failed erase or prog
  at most. Without `LFS3_F_CKPROGS` a silent failure is found by
  `LFS3_F_CKMETA`, and format returns `LFS3_ERR_CORRUPT`. With every block
  from 2 on bad, format returns `LFS3_ERR_NOSPC`.
  `badblocks_gbmap::factory`: a factory list including blocks 2 and 7, with
  the device refusing to erase them, leaves block 2 with one refused erase or
  prog and block 7 with none, over 32 remount cycles.
- **Fail:** format fails while a good block remains, or a listed block is
  written.
- **Verified by:** `badblocks::gbmap_format`, `badblocks_gbmap::factory`.
- **Status:** Not implemented at `b10efaa`; tested on `v3-integration`
  (185efae3), which also fixes the 60 `LFS3_YES_GBMAP` permutations of
  `exhaustion::spam_file_pl_fuzz` that reformat a disk with block 2 worn
  out (issue #4).
- **When:** every CI run.

#### LFS3-BAD-15

littlefs shall include bad-block tracking only with `LFS3_GBMAP`, which
holds the marks, and leave builds without it unchanged.

- **Source:** Derived: without a gbmap there is nowhere to persist a mark.
  This replaces the proposed `LFS3_BADBLOCKS` option and its `#error`
  (3-alloc §8.4), which tracking without a separate option makes moot.
- **Measure:** symbols and `sizeof(lfs3_t)` in B-DEF.
- **Pass:** B-DEF declares none of `lfs3_fs_mkbad`, `lfs3_fs_mkgood`,
  `lfs3_fs_nextbad` and `lfs3_fs_nextsuspect`, and its `lfs3_t` has no
  queue.
- **Fail:** tracking code or RAM in B-DEF.
- **Verified by:** build (B-DEF, `#if` guards).
- **Status:** Not implemented at `b10efaa`; met on `v3-integration`.
- **When:** every CI run.

#### LFS3-BAD-16

littlefs shall record as suspect, in RAM and without writing, each block
that fails a check while being read: a read that returns `LFS3_ERR_CORRUPT`,
a B-tree node or data block whose checksum differs from the one its parent
records, or a tag parity or data checksum mismatch; and shall list suspect
blocks through `lfs3_fs_nextsuspect`.

- **Source:** Proposal (issues #9, #19): a failed read does not prove a
  block bad (principle 4), but the application needs to know where reads
  fail. Persisting suspects across mounts would need a new gbmap tag and a
  wcompat flag; that is left to the user (issue #19).
- **Measure:** `lfs3_fs_nextsuspect` against the blocks the test damaged;
  emubd counters.
- **Pass:** `badblocks_gbmap::suspect`: after checking every file on
  read-only and writable mounts over 1, 8 and 10 damaged data blocks
  (READERROR, and MANUAL flipped bits), `lfs3_fs_nextsuspect` lists the
  most recent `LFS3_SUSPECTS_SIZE` (8) damaged blocks and no other, no
  prog or erase happens, nothing is marked bad, and the list is empty after
  a remount, in B-YGB and B-BIG.
- **Fail:** a damaged block that was read is missing, an undamaged block is
  listed, or a write.
- **Verified by:** `badblocks_gbmap::suspect`.
- **Status:** Not implemented at `b10efaa`; tested on `v3-integration`
  (b81c02a3).
- **When:** every CI run.

#### LFS3-BAD-17

littlefs shall, after repairing a block by moving its contents (LFS3-DEG-10),
reuse the old block if it erases and programs cleanly, and record it as bad
if it does not, or if it needs repairing again in the same mount.

- **Source:** Proposal (issue #19). A block that read wrongly once may hold a
  weak write; one that fails twice is decaying.
- **Measure:** BMBAD ranges, suspect lists and allocations after repairs.
- **Pass:** `repair::reuse`, `repair::twice`: a block repaired once that
  then erases, programs and reads back cleanly is allocated again and is
  not bad; a block whose erase or program fails after the repair, and a
  block repaired twice in one mount, are listed by `lfs3_fs_nextbad` and
  never written again; both stay listed by `lfs3_fs_nextsuspect` until
  marked bad.
- **Fail:** a block that failed twice is reused, or a clean block is marked
  bad.
- **Verified by:** `repair::reuse`, `repair::twice`.
- **Status:** Not implemented at `b10efaa`; tested on `v3-integration`
  (f09acb9d).
- **When:** every CI run.

### 6.6 Metadata (META)

#### LFS3-META-01

littlefs shall keep each rbyd balanced, so that the bytes written per tag
stay within 12 × (2 × ⌈log2 n⌉ + 1) + 4 for an rbyd of n tags with
`prog_size` 1.

- **Source:** Stated: #1111 "Efficient metadata compaction" (lookup
  O(n) to O(log n)); the bound is asserted in `tests/test_rbyd.toml`
  (for example lines 2240-2252).
- **Measure:** bytes per tag in each permutation.
- **Pass:** `rbyd::*_permutations`, `rbyd::fuzz_*` and the other
  `rbyd::*` cases pass in B-DEF with `ERASE_VALUE` in {0xff, 0x00, -1}.
- **Fail:** any bound assertion fails.
- **Verified by:** as listed (107 cases).
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-META-02

littlefs shall keep every leaf of an rbyd at the same black height, with a
total height of at most 2 × black height + 2.

- **Source:** Stated: the invariant checked under `LFS3_DBGRBYDBALANCE`
  (`lfs3.c:2987-3027`).
- **Measure:** the debug balance check.
- **Pass:** `make test-balance`, which runs `rbyd::*`, `btree::*` and
  `mtree::*` in a build with `LFS3_DBGRBYDBALANCE`, passes.
- **Fail:** the check fires.
- **Verified by:** `make test-balance` (nightly job `test-balance`).
- **Status:** Tested on v3-integration (35d965ef). Runs nightly.
- **When:** nightly.

#### LFS3-META-03

littlefs shall refuse, with a negative error code from `lfs3.h` and before
writing anything, any single operation whose metadata cannot fit in an empty
metadata block.

- **Source:** Derived: the commit path asserts that this cannot happen and
  relies on "upper layers" to limit commit size (`lfs3.c:8772-8777`); they do
  not. The error code to use is open question Q3.
- **Measure:** return value, asserts, emubd prog counts.
- **Pass:** a NEW fuzz over block sizes {512, 1024, 4096}, name lengths 1 to
  `name_limit` and attribute sizes 0 to `block_size` gets 0 or an error code
  from `lfs3.h` for every `lfs3_mkdir`, `lfs3_file_opencfg` with
  `LFS3_O_CREAT`, `lfs3_rename` and `lfs3_setattr`, with no assert, and
  `lfs3_fs_ck` returns 0 afterwards, in B-DEF. An operation whose metadata
  fits a block but whose mtree B-tree split leaves the commit no room in
  the half it goes to also gets 0 or `LFS3_ERR_NOSPC`: `mtree::commit_too_big`
  passes with seed 637 at `ERASE_SIZE` 512 (issue #25) in B-DEF, B-YGB
  and B-BIG, and with `LFS3_NO_ASSERT`.
- **Fail:** an assert, or a check failure.
- **Verified by:** `mtree::commit_too_big` (NEW-41), with seed 637 at
  `ERASE_SIZE` 512 for D-9. See LFS3-DIR-02 and LFS3-ATTR-05 for the cases
  reproduced.
- **Status:** Tested on `v3-integration` (d428d7b8 for 1-meta 0.2,
  e38b42ae for D-9): `mtree::commit_too_big` passes in B-DEF, B-YGB and
  B-BIG and under ASan and UBSan, and with `-DSEED='range(4096)'` at all
  three block sizes no run fails in B-DEF (`-Pnone`, and `-Plinear` with
  atomic power loss), B-YGB, B-BIG or with `LFS3_NO_ASSERT` (`-Pnone`).
  Before e38b42ae, seed 637 at `ERASE_SIZE` 512 tripped
  `LFS3_ASSERT(err != LFS3_ERR_RANGE)` in an mtree B-tree split during
  `lfs3_mkdir`, in all 8 of its permutations (D-9, issue #25).
- **When:** every CI run.

#### LFS3-META-04

littlefs shall fetch the block of a metadata pair that has the newer
revision count, on hosts of either byte order.

- **Source:** Stated: `lfs3.c:7765-7767` ("most recent block has the most
  recent revision count").
- **Measure:** which block `lfs3_mdir_fetch` selects.
- **Pass:** all suites pass on A-32BE in B-DEF, and a NEW internal case with
  revision counts 0x0f80006d and 0x1000006d selects the second.
- **Fail:** the older block is selected.
- **Verified by:** NEW; all suites on A-32BE.
- **Status:** Known defect (1-meta 0.3: `lfs3.c:7864` reads into `revs[0]`,
  `7868` converts `revs[i]`).
- **When:** every CI run.

#### LFS3-META-05

littlefs shall split a metadata pair that no longer fits, drop one that
becomes empty, and relocate one that wears out, without changing any
entry.

- **Source:** Stated: #1111 "A simpler/more robust metadata tree";
  `lfs3.c:8878-9468`.
- **Measure:** entries and handles after each structural change.
- **Pass:** `mtree::split*`, `mtree::drop*`, `mtree::relocate*`,
  `mtree::uninline*` and `mtree::*_fuzz` pass in B-DEF.
- **Fail:** any entry changes or is lost.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-META-06

littlefs shall keep the mroot anchor at blocks 0 and 1, and extend the mroot
chain when the anchor itself must be rewritten elsewhere.

- **Source:** Stated: `lfs3.c:9290-9341`; the anchor is the only fixed
  location on disk.
- **Measure:** chain shape and mount result.
- **Pass:** `mtree::extend`, `mtree::extend_twice`, `mtree::relocate_mroot`,
  `mtree::relocate_extend` and `trvs::mutation_*_extend*` pass in B-DEF.
- **Fail:** the anchor moves or the image does not mount.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-META-07

littlefs shall keep the magic string "littlefs" at byte offset 8 of the
active anchor block after format, compaction and chain extension.

- **Source:** Stated: tested by `mtree::magic*`; tools identify littlefs
  images by it.
- **Measure:** bytes 8 to 15 of blocks 0 and 1.
- **Pass:** `mtree::magic`, `mtree::magic_extend` and
  `mtree::magic_extend_twice` pass in B-DEF.
- **Fail:** the magic is elsewhere.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-META-08

littlefs shall return `LFS3_ERR_CORRUPT`, and not loop, when the mroot chain
contains a cycle.

- **Source:** Stated: Brent's cycle detection, `lfs3.c:9876-9901`.
- **Measure:** mount result.
- **Pass:** `mtree::traversal_mroot_cycle` passes in B-DEF.
- **Fail:** the mount hangs or succeeds.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-META-09

littlefs shall hide an entry whose removal is pending in the global remove
queue, and shall complete the removal at the next mkconsistent.

- **Source:** Stated: `lfs3.c:7925-7950` (pending grm mids read as orphans),
  `16489-16535` (fixgrm).
- **Measure:** `lfs3_stat` of the entry; the grm after mkconsistent.
- **Pass:** `dirs::*` (reentrant) and `mount::t_mkconsistent` pass in B-DEF.
- **Fail:** the entry is visible, or the grm is not empty after
  `lfs3_fs_mkconsistent`.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-META-10

littlefs shall leave its in-RAM global state (gcksum and grm) equal to the
on-disk state after any failed metadata commit.

- **Source:** Stated: #1111 "Better recovery from runtime errors" ("Most
  in-RAM filesystem state should now revert to the last known-good state on
  error"); `lfs3_fs_revertgdelta` at `lfs3.c:9464-9467`.
- **Measure:** `lfs3_fs_cksum` before and after the failed call; mount
  result after a later successful commit.
- **Pass:** NEW cases that fail a commit through its final sync (emubd
  extension, LFS3-FAIL-18) and through a stuck anchor (as in
  `badblocks::mrootanchor_wear`) find `lfs3_fs_cksum` unchanged by the failed
  call, and after one more successful commit the image mounts, in B-DEF.
- **Fail:** the checksum changed, or the later mount fails with a gcksum
  mismatch.
- **Verified by:** NEW.
- **Status:** Known defect (3-alloc B3: `lfs3.c:9316`, `9336` and `9346`
  return without reverting).
- **When:** every CI run.

#### LFS3-META-11

littlefs shall return an error, and not use uninitialised data, when reading
a gbmap delta from an mdir fails.

- **Source:** Derived: the grm branch beside it checks the same error
  (`lfs3.c:7722-7725`).
- **Measure:** return value of mount (and of split or drop) with READERROR on
  an mdir that carries a gbmap delta.
- **Pass:** a NEW case in B-YGB gets `LFS3_ERR_CORRUPT` from mount, with no
  sanitizer report.
- **Fail:** mount succeeds with wrong gbmap state, or a sanitizer report.
- **Verified by:** NEW.
- **Status:** Known defect (1-meta 0.5: `lfs3.c:7744-7755`).
- **When:** every CI run.

#### LFS3-META-12

littlefs shall order the two blocks of a metadata pair correctly when the
revision count wraps around 2^32.

- **Source:** Stated: `lfs3.c:7765-7767` ("simple 32-bit counters", compared
  with sequence arithmetic); format deliberately writes a wrapped pair
  (`lfs3.c:16191-16199`).
- **Measure:** which block is fetched; content after compaction.
- **Pass:** every suite passes (format exercises one wrap), and
  `mtree::rev_wrap`, which sets an mdir pair's revision counts to 0xfffffffe
  and 0xffffffff and compacts across the wrap 24 more times, finds the
  newest commit by fetching either block first and by mounting, for the
  mroot and for an mdir in the mtree, in B-DEF.
- **Fail:** the older block is fetched.
- **Verified by:** all suites (incidental); `mtree::rev_wrap`.
- **Status:** Tested on v3-integration (ef18a87f).
- **When:** every CI run.

#### LFS3-META-13

littlefs shall update every open handle when the metadata pair holding its
entry is split, dropped, relocated or moved into a new mroot.

- **Source:** Stated: `lfs3.c:9350-9452` ("we must not error at this
  point").
- **Measure:** reads and writes through handles opened before the change.
- **Pass:** `mtree::opened_*`, `dread::read_with_*` and `trvs::mutation_*`
  pass in B-DEF.
- **Fail:** a handle reads another entry or fails.
- **Verified by:** as listed.
- **Status:** Tested. (`mtree::opened_relocate_l` and `_r` used unterminated
  names, F-7.)
- **When:** every CI run.

#### LFS3-META-14

littlefs shall visit every metadata pair exactly once during mkconsistent,
including when removing orphans drops a pair during the scan.

- **Source:** Derived: the scan steps back by one mid when a pair is
  dropped, marked "TODO big hack! is it big enough?" (`lfs3.c:10329-10337`).
- **Measure:** orphans left after `lfs3_fs_mkconsistent`.
- **Pass:** a case that leaves orphaned stickynotes as the only entries of
  at least two consecutive pairs, then runs `lfs3_fs_mkconsistent`, finds no
  orphan left and every other entry present, in B-DEF, B-YGB and B-BIG. How
  many entries fit a pair depends on the build (the gbmap's gstate takes
  room), so the case adds orphans until the scan finds such a run, rather
  than assuming a count.
- **Fail:** an orphan remains or an entry is skipped.
- **Verified by:** `stickynotes::cleanup_drop` (NEW-71).
- **Status:** Tested on `v3-integration` (68039c4d). Before, in B-YGB, 3
  permutations with 512-byte blocks and 8 orphans never built two
  orphan-only pairs, and the case's precondition failed (issue #18).
- **When:** every CI run.

#### LFS3-META-15

littlefs shall make metadata splits, drops, relocations and mroot chain
extensions atomic under power loss.

- **Source:** Derived: new blocks stay unreferenced until the parent commit,
  with a sync first (`lfs3.c:9198-9304`).
- **Measure:** reentrant case results.
- **Pass:** NEW reentrant variants of `mtree::split_fuzz`,
  `mtree::drop_fuzz`, `mtree::relocate_fuzz` and `mtree::extend*` pass under
  `-Plinear` in B-DEF.
- **Fail:** any permutation fails.
- **Verified by:** indirectly by `dirs::*`, `powerloss::*` and
  `relocations::*_pl_fuzz`; NEW.
- **Status:** Partly tested (`test_mtree` has no reentrant case).
- **When:** every CI run.

#### LFS3-META-16

littlefs shall pass the metadata suites at every geometry in G-ALL.

- **Source:** Derived: #1114 (2025-11-14): "v3 was redesigned so that all
  operations scale O(b log b) w.r.t. the block size". Geometry-dependent
  limits (for example LFS3-DIR-02) appear only at small block sizes.
- **Measure:** suite results.
- **Pass:** `mtree::*`, `dirs::*`, `dread::*` and `paths::*` pass with each
  geometry of G-ALL in B-DEF.
- **Fail:** any permutation fails.
- **Verified by:** as listed.
- **Status:** Partly tested (only 4096-byte blocks, and 32768 in
  `test_rbyd`).
- **When:** nightly.

#### LFS3-META-17

littlefs shall never compact an rbyd into more bytes than its size estimate.

- **Source:** Derived: split decisions rely on the estimate being an upper
  bound (`lfs3.c:4613`, `8451`, derivation at `15283-15318`).
- **Measure:** estimate and actual size after compaction.
- **Pass:** a NEW internal assertion in compaction holds over `mtree::*_fuzz`
  and `btree::*_fuzz` in B-DEF.
- **Fail:** the actual size exceeds the estimate.
- **Verified by:** NEW.
- **Status:** Untested.
- **When:** every CI run.

### 6.7 Files and data (FILE)

#### LFS3-FILE-01

littlefs shall return from `lfs3_file_read` the bytes most recently written
through the same handle at each position.

- **Source:** Stated: `lfs3.h:1585-1601`.
- **Measure:** bytes read back.
- **Pass:** `fwrite::simple`, `fwrite::incr`, `fwrite::reversed`,
  `fwrite::overwrite`, `fwrite::fuzz_aligned`, `fwrite::fuzz_unaligned` and
  `files::*` pass in B-DEF.
- **Fail:** any byte differs.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-FILE-02

littlefs shall return 0 from `lfs3_file_write` with size 0 and leave the file
and its handle unchanged.

- **Source:** Stated: `lfs3.c:14117-14119` (comment).
- **Measure:** return value, `lfs3_file_size`, `lfs3_file_tell`, the
  handle's sync state, emubd prog count.
- **Pass:** a NEW case finds all unchanged and no prog, in B-DEF.
- **Fail:** anything changes.
- **Verified by:** NEW.
- **Status:** Untested.
- **When:** every CI run.

#### LFS3-FILE-03

littlefs shall return `LFS3_ERR_FBIG` from `lfs3_file_write` exactly when the
write would end beyond `file_limit`, measured from where the write lands
(the end of file for `LFS3_O_APPEND`).

- **Source:** Stated: `lfs3.h:660-665` (file_limit "must be respected");
  `lfs3.c:14123-14126`.
- **Measure:** return value and resulting size.
- **Pass:** a NEW case with `file_limit=1000` finds: write 900, rewind,
  append 500 gives `LFS3_ERR_FBIG`; seek to 1000 on an empty `O_APPEND` file,
  write 10 gives 10; in B-DEF.
- **Fail:** the file exceeds `file_limit`, or FBIG for a write that fits.
- **Verified by:** NEW.
- **Status:** Known defect (2-files B2).
- **When:** every CI run.

#### LFS3-FILE-04

littlefs shall return the number of bytes available, and not assert, from
`lfs3_file_read` with a size larger than the rest of the file.

- **Source:** Stated: `lfs3.h:1585-1590` ("Returns the number of bytes
  read").
- **Measure:** return value.
- **Pass:** a NEW case reads with size 0xffffffff from positions 0 and 10 of
  a 100-byte file and gets 100 and 90, in B-DEF.
- **Fail:** an assert or another value.
- **Verified by:** NEW.
- **Status:** Known defect (2-files B14: asserts `pos + size <= 0x7fffffff`,
  `lfs3.c:12983`).
- **When:** every CI run.

#### LFS3-FILE-05

littlefs shall read zeros from holes and return 0 from reads at or beyond
the end of file.

- **Source:** Stated: `lfs3.h:1610-1628` (holes read "as if the file was
  filled with zeros"); #1111 "Sparse files".
- **Measure:** bytes and return values.
- **Pass:** `fwrite::holes`, `fwrite::clip_hole` and `fwrite::w_seek` pass
  in B-DEF.
- **Fail:** non-zero bytes in a hole, or a positive return at end of file.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-FILE-06

littlefs shall set the position from `lfs3_file_seek` for `LFS3_SEEK_SET`,
`LFS3_SEEK_CUR` and `LFS3_SEEK_END`, and shall return `LFS3_ERR_INVAL`
without moving when the result is negative or beyond `file_limit`.

- **Source:** Stated: `lfs3.h:1603-1608`; `lfs3.c:14684-14686`.
- **Measure:** return value and `lfs3_file_tell`.
- **Pass:** `fwrite::r_seek`, `fwrite::w_seek`, `fwrite::rw_seek` and
  `fwrite::seek_negative` pass in B-DEF.
- **Fail:** a wrong position, or no error.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-FILE-07

littlefs shall create a hole between the old end of file and the write
position when a write lands beyond the end of file.

- **Source:** Stated: #1111 "Sparse files" (`lfs3_file_seek` +
  `lfs3_file_write` past the end).
- **Measure:** size and content.
- **Pass:** `fwrite::holes` and `fwrite::w_seek` pass in B-DEF.
- **Fail:** wrong size or non-zero gap.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-FILE-08

littlefs shall set the file size with `lfs3_file_truncate`, cutting from or
growing at the end, without moving the position.

- **Source:** Stated: `lfs3.h:1610-1618`.
- **Measure:** size, content, `lfs3_file_tell`.
- **Pass:** `fwrite::truncate`, `fwrite::truncate_truncate`,
  `fwrite::truncate_pos`, `fwrite::truncate_litmus_zero` and
  `fwrite::truncate_litmus_fragment` pass in B-DEF.
- **Fail:** wrong size, content or position.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-FILE-09

littlefs shall set the file size with `lfs3_file_fruncate`, cutting from or
growing at the front, and keep the position at the same distance from the end
of file, clamped at 0.

- **Source:** Stated: `lfs3.h:1620-1628`; #1111 "Easier logging APIs";
  `lfs3.c:14902-14906`.
- **Measure:** size, content, `lfs3_file_tell`.
- **Pass:** `fwrite::fruncate*`, `fwrite::freversed*` and
  `fwrite::rwtf_fuzz` pass in B-DEF.
- **Fail:** wrong size, content or position.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-FILE-10

littlefs shall preserve a file's content when a data block goes bad during
an append after `lfs3_file_fruncate`.

- **Source:** Stated: data-block relocation on a bad prog rewrites the
  crystal (`lfs3.c:13644-13662`, commit `664d99d`).
- **Measure:** file content and size after the append, a sync and remount.
- **Pass:** a NEW case (prog_size 16; write 2000 bytes, sync, fruncate to
  1900, sync, mark the data block bad with PROGERROR, append 64 bytes, sync)
  reads back the expected 1964 bytes after remount, in B-DEF and B-NA.
- **Fail:** wrong content or size, an assert, or `LFS3_ERR_CORRUPT`.
- **Verified by:** NEW.
- **Status:** Known defect (2-files B1: `block_pos` wraps below zero, the
  rewrite copies nothing, and the file reads back with size -100 and then
  `LFS3_ERR_CORRUPT`).
- **When:** every CI run.

#### LFS3-FILE-11

littlefs shall preserve a file's content when the data block it is
appending to goes bad after data was written to it.

- **Source:** Derived from LFS3-FAIL-01 for the resumed-append path
  (`lfs3.c:13715-13756`), which the bad-block suites do not reach because
  they mark blocks bad before the test starts.
- **Measure:** file content after remount.
- **Pass:** a NEW case (write, sync, mark the file's last data block bad with
  PROGERROR and with ERASEERROR, append, sync) reads back the expected data,
  with `PROG_SIZE` in {1, 16}, in B-DEF.
- **Fail:** wrong content or an error.
- **Verified by:** NEW.
- **Status:** Untested.
- **When:** every CI run.

#### LFS3-FILE-12

littlefs shall keep a file's old content until the handle opened with
`LFS3_O_TRUNC` is synced or closed.

- **Source:** Stated: `lfs3.h:135`; #1111 "A well-defined sync model",
  rule 1.
- **Measure:** content seen by `lfs3_get` and by other handles before and
  after the sync.
- **Pass:** `files::trunc` and `powerloss::spam_f_pl_fuzz` pass in B-DEF.
- **Fail:** the old content disappears before the sync.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-FILE-13

littlefs shall place every write on a handle opened with `LFS3_O_APPEND` at
the end of file.

- **Source:** Stated: `lfs3.h:138`.
- **Measure:** content and size.
- **Pass:** `fwrite::incr` and `fsync::sync_wwrr_append` pass in B-DEF.
- **Fail:** a write lands elsewhere.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-FILE-14

littlefs shall create a missing file with `LFS3_O_CREAT`, and return
`LFS3_ERR_EXIST` with `LFS3_O_CREAT | LFS3_O_EXCL` when the name exists,
including as a stickynote visible through another handle.

- **Source:** Stated: `lfs3.h:129-133`.
- **Measure:** return values.
- **Pass:** `files::create`, `files::excl`, `stickynotes::uncreat_excl` and
  `stickynotes::orphan_excl` pass in B-DEF.
- **Fail:** any other result.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-FILE-15

littlefs shall return `LFS3_ERR_NOENT`, `LFS3_ERR_ISDIR`,
`LFS3_ERR_NOTDIR`, `LFS3_ERR_NAMETOOLONG` or `LFS3_ERR_NOTSUP` from
`lfs3_file_opencfg` for a missing file, a directory, a file used as a
directory, a name longer than `name_limit`, or an entry of unknown type,
respectively.

- **Source:** Derived: POSIX `open` semantics as adapted by littlefs;
  `lfs3.c:12640-12690`.
- **Measure:** return values.
- **Pass:** `files::noent`, `files::dir_not_file`, `files::file_not_dir`,
  `files::root_not_file`, `files::noent_not_file`, `paths::*` and
  `mount::incompat_unknown_type` pass in B-DEF.
- **Fail:** any other result.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-FILE-16

littlefs shall return `LFS3_ERR_NOMEM` from `lfs3_file_open` when the file
cache cannot be allocated, and leave no handle registered.

- **Source:** Stated: `lfs3.h:94`; `lfs3.c:12621`.
- **Measure:** return value; `lfs3_unmount` succeeds afterwards.
- **Pass:** a NEW case with a failing allocator gets `LFS3_ERR_NOMEM` and can
  unmount, with no leak reported by valgrind, in B-DEF.
- **Fail:** any other result, a leak, or an assert at unmount.
- **Verified by:** NEW.
- **Status:** Untested.
- **When:** every CI run.

#### LFS3-FILE-17

littlefs shall write, remount, read, truncate and fruncate a sparse file
that ends at byte 2^31 - 2 (size `LFS3_FILE_MAX`), with correct results and
without undefined behaviour: no rid, bid or weight sum in the file's tree
overflows a signed 32-bit integer.

- **Source:** Stated: `lfs3.h:69-74`; #1114 (2025-06-23): "The driver is
  currently limited to 2^31-1 for both file size and block count (though
  untested)". Derived: a file's bids reach 2^31 - 2 and its weight
  2^31 - 1, while rids are `int32_t` (`lfs3.h:43-44`), so the sum of a rid
  and a weight can pass 2^31 - 1 even when the value it stands for does
  not; signed overflow is undefined behaviour (LFS3-GEN-03, issue #22).
- **Measure:** size and content of the last bytes; the bids and weights a
  traversal of the file's tree reports; UndefinedBehaviorSanitizer reports.
- **Pass:** `fwrite::filemax` writes 16 bytes ending at 2^31 - 2, remounts,
  reads them back, and fruncates to 16 bytes; `fwrite::filemax_fuzz`
  reads, writes, truncates and fruncates in the last bytes of a file
  between 2^31 - 1 - `SIZE` and 2^31 - 1 bytes long, which splits, merges
  and compacts its B-tree there, then checks size and content, that the
  tree's entries tile the file and each inner node starts where its first
  entry starts, and `lfs3_fs_ck`. Both pass in B-DEF, and with no report
  from a runner built with `-fsanitize=address,undefined` and run with
  `UBSAN_OPTIONS=halt_on_error=1`, in B-DEF and B-BIG.
- **Fail:** any error, wrong content, a traversal whose entries do not tile
  the file, or a sanitizer report.
- **Verified by:** `fwrite::filemax` and `fwrite::filemax_fuzz` (NEW-73).
- **Status:** Tested on `v3-integration` (82ab4f07, b5888089): both cases
  pass in B-DEF, B-YGB and B-BIG, and the whole suite runs with no
  sanitizer report in B-DEF and B-BIG; CI has no sanitizer job yet
  (issue #17).
  Before, under UBSan, 16 of 24 permutations of `fwrite::filemax` and 2024
  of 2400 of `fwrite::filemax_fuzz` overflowed `int32_t` in
  `lfs3_rbyd_estimate` or `lfs3_btree_traverse`, and in B-DEF 299 of
  `fwrite::filemax_fuzz` saw an inner node reported at the wrong bid (D-7,
  issue #22).
- **When:** every CI run.

#### LFS3-FILE-18

littlefs shall store a hole without allocating data blocks for it.

- **Source:** Stated: #1111 "Sparse files" ("contiguous runs of zeros can be
  implied without actually taking up any disk space").
- **Measure:** `lfs3_fs_usage` before and after.
- **Pass:** a NEW case that grows a file by 1 MiB with
  `lfs3_file_truncate`, and one that writes 10 bytes at offset 1 MiB, each
  increase `lfs3_fs_usage` by at most 2 blocks, in B-DEF.
- **Fail:** a larger increase.
- **Verified by:** NEW.
- **Status:** Untested.
- **When:** every CI run.

#### LFS3-FILE-19

littlefs shall report from `lfs3_file_size` the size as seen by the handle,
including unsynced writes, and from `lfs3_stat` the size last synced.

- **Source:** Stated: `lfs3.h:1642-1646`; #1111 "A well-defined sync model".
- **Measure:** both sizes around a write and a sync.
- **Pass:** a NEW case finds the handle size grow at the write and the
  `lfs3_stat` size grow only at the sync, in B-DEF.
- **Fail:** the sizes change at the wrong time.
- **Verified by:** implied by `fsync::*`; NEW.
- **Status:** Partly tested.
- **When:** every CI run.

#### LFS3-FILE-20

littlefs shall make `lfs3_file_tell` equal to
`lfs3_file_seek(file, 0, LFS3_SEEK_CUR)`, and `lfs3_file_rewind` equal to
`lfs3_file_seek(file, 0, LFS3_SEEK_SET)`.

- **Source:** Stated: `lfs3.h:1630-1640`.
- **Measure:** positions.
- **Pass:** `fwrite::r_seek`, `fwrite::w_seek` and `fwrite::rw_seek` pass in
  B-DEF.
- **Fail:** the positions differ.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-FILE-21

littlefs shall release a file handle in `lfs3_file_close` even when the
close returns an error.

- **Source:** Stated: `lfs3.h:1535` ("Releases any allocated resources,
  even if there is an error").
- **Measure:** the handle list and `lfs3_unmount` after a failing close.
- **Pass:** a NEW case makes the sync inside close fail with NOSPC and with a
  bad block, then unmounts without an assert and without a leak, in B-DEF.
- **Fail:** an assert at unmount, or a leak.
- **Verified by:** NEW; `alloc::nospc_files` covers the NOSPC path
  partially.
- **Status:** Partly tested.
- **When:** every CI run.

#### LFS3-FILE-22

littlefs shall read a zero-size file as empty whether its entry has no
struct, a zero-weight B-shrub or a zero-weight B-tree.

- **Source:** Derived: other drivers may write any of these forms.
- **Measure:** `lfs3_file_size` and reads.
- **Pass:** `files::zero_bnull`, `files::zero_bshrub` and
  `files::zero_btree` pass in B-DEF.
- **Fail:** any other size or an error.
- **Verified by:** as listed.
- **Status:** Tested. (`files::zero_btree` failed at the baseline because of
  a test bug, F-6.)
- **When:** every CI run.

#### LFS3-FILE-23

littlefs shall keep a file's content while its representation changes from
inline data to a B-shrub, to a B-tree, and back.

- **Source:** Stated: #1111 "Efficient inline files".
- **Measure:** content through each transition.
- **Pass:** `files::more`, `files::mv_split` and
  `fwrite::*_litmus_*` pass in B-DEF.
- **Fail:** any byte differs.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-FILE-24

littlefs shall produce correct content when two handles of the same file
append alternately and sync.

- **Source:** Derived: both handles may hold the erased state of the same
  block; the one that programs first must take it from the other
  (`lfs3.c:13415-13423`).
- **Measure:** content after each sync, and after remount.
- **Pass:** a NEW case with two handles, each appending 16 bytes and syncing
  in turn, with `PROG_SIZE` in {1, 16}, matches the model, in B-DEF.
- **Fail:** any byte differs.
- **Verified by:** NEW.
- **Status:** Untested.
- **When:** every CI run.

#### LFS3-FILE-25

littlefs shall store data using the fragment and block layout that the
litmus cases define for sequential, reversed, incremental and truncated
writes.

- **Source:** Stated: the litmus cases encode the maintainer's expected
  layouts.
- **Measure:** internal layout checks.
- **Pass:** `fwrite::*_litmus_*` pass in B-DEF.
- **Fail:** any litmus check fails.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-FILE-26

littlefs shall enforce a configured `file_limit` smaller than
`LFS3_FILE_MAX` in `lfs3_file_write`, `lfs3_file_seek`, `lfs3_file_truncate`
and `lfs3_file_fruncate`.

- **Source:** Stated: `lfs3.h:660-665`.
- **Measure:** return values at and beyond the limit.
- **Pass:** NEW variants of `fwrite::fbig`, `fwrite::truncate_fbig` and
  `fwrite::fruncate_fbig` with `file_limit` in {1, 1000, 65536} pass in
  B-DEF.
- **Fail:** a file grows beyond the limit, or an operation within it fails.
- **Verified by:** NEW. The existing cases use `LFS3_FILE_MAX` only.
- **Status:** Untested.
- **When:** every CI run.

#### LFS3-FILE-27

littlefs shall have written every byte that `lfs3_file_write` accepted when
`lfs3_file_flush`, `lfs3_file_sync` or `lfs3_file_close` returns 0, and
shall show those bytes to reads and `lfs3_file_size` through the same
handle before then, including the bytes of an append that it keeps in the
file cache when a write flushes a full cache.

- **Source:** Derived: so that later appends in the same sync don't replace
  padded commits (LFS3-PERF-09, Appendix B.5 D), a write that flushes a
  full file cache in the middle of an append into a data block it can
  resume flushes only up to that block's last `prog_size` boundary, and
  keeps the rest, fewer than `prog_size` bytes, cached until the next
  flush, sync or close. Durability doesn't change: after a power loss
  nothing beyond the last successful sync is promised, cached or not
  (LFS3-PL-04, PL-06).
- **Measure:** content and size through the handle after each write; on
  disk after sync, close, desync, truncate and fruncate, after
  `LFS3_ERR_NOSPC`, and after a power loss.
- **Pass:** `fwrite::append_tail`, `fwrite::append_tail_nospc` and
  `powerloss::append_unsynced_pl` pass with `PROG_SIZE` 1, 16 and 256 and
  `FCACHE_SIZE` below and above `PROG_SIZE`, in B-DEF and B-BIG: every
  read through the handle matches the bytes written, a closed or synced
  file holds every byte written, a desynced or power-lost one holds what
  its last successful sync wrote, and a file left by `LFS3_ERR_NOSPC`
  holds what its last successful sync wrote.
- **Fail:** any byte or size differs.
- **Verified by:** `fwrite::append_tail`, `fwrite::append_tail_nospc`,
  `powerloss::append_unsynced_pl`.
- **Status:** Untested at `b10efaa`. Tested on `v3-integration`
  (`effb33ca`, `32eb36e7`).
- **When:** every CI run.

### 6.8 Sync model and stickynotes (SYNC)

PR #1111 ("A well-defined sync model") states five rules. LFS3-SYNC-01 to
SYNC-07 restate them one behaviour at a time.

#### LFS3-SYNC-01

littlefs shall keep writes to an open file handle invisible to other
handles and to the disk until that handle is synced.

- **Source:** Stated: #1111 rule 1 ("Open file handles are strictly
  snapshots of the on-disk state").
- **Measure:** content seen by other handles and by `lfs3_get`.
- **Pass:** `fsync::wrrr`, `fsync::wwww`, `fsync::wwrr`, `fsync::rwrw` and
  `fsync::*_fuzz` pass in B-DEF.
- **Fail:** another handle or `lfs3_get` sees an unsynced write.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-SYNC-02

littlefs shall update every other in-sync handle of the same file, including
its readable file-attached attributes, when a handle is synced or closed.

- **Source:** Stated: #1111 rule 2.
- **Measure:** content, size and attributes seen by the other handles.
- **Pass:** `fsync::sync_*`, `fsync::wrrr`, `fsync::wwrr` and
  `attrs::fattr_broadcast` pass in B-DEF.
- **Fail:** another in-sync handle keeps the old state.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-SYNC-03

littlefs shall discard an in-sync handle's unsynced writes when another
handle of the same file is synced.

- **Source:** Stated: #1111 rule 2 (the sync "atomically updates ... any
  other in-sync file handles").
- **Measure:** content of the handle that did not sync.
- **Pass:** `fsync::wwww` and `fsync::rwrw*` pass in B-DEF.
- **Fail:** the handle keeps its own unsynced writes.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-SYNC-04

littlefs shall not update a desynced handle when another handle is synced,
and shall not change the disk when a desynced handle is closed.

- **Source:** Stated: #1111 rule 3; `lfs3.h:1562-1566, 1537-1538`.
- **Measure:** the content of a handle desynced with `lfs3_file_desync` or
  opened with `LFS3_O_DESYNC`; disk content after its close.
- **Pass:** `fsync::desync_*`, `fsync::drrr`, `fsync::wddd` and
  `stickynotes::undesync_*` pass in B-DEF.
- **Fail:** the desynced handle changes, or its close changes the disk.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-SYNC-05

littlefs shall mark a file handle desynced whenever `lfs3_file_write`,
`lfs3_file_flush`, `lfs3_file_sync`, `lfs3_file_truncate` or
`lfs3_file_fruncate` returns an error.

- **Source:** Stated: `lfs3.h:1568-1569` ("If an error occurs during a write
  operation, the file is implicitly marked as desynchronized").
- **Measure:** whether a later close commits the handle.
- **Pass:** a NEW case makes each call fail (NOSPC, FBIG, a bad block) and
  finds that the following close leaves the disk unchanged, in B-DEF.
- **Fail:** the close commits.
- **Verified by:** NEW.
- **Status:** Known defect (2-files B8: errors from the graft and allocator
  checkpoint in truncate and fruncate return without desyncing,
  `lfs3.c:14738-14747`, `14828-14837`).
- **When:** every CI run.

#### LFS3-SYNC-06

littlefs shall, on `lfs3_file_sync` of a desynced handle, commit the
handle's content, update the other in-sync handles, and mark the handle
in-sync.

- **Source:** Stated: #1111 rule 4; `lfs3.h:1547-1548, 1571-1572`.
- **Measure:** disk content, other handles, later broadcasts to this handle.
- **Pass:** `fsync::desync_*`, `fsync::rwdrwd` and `fsync::rwd*_fuzz` pass
  in B-DEF.
- **Fail:** any of the three does not happen.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-SYNC-07

littlefs shall, on `lfs3_file_resync`, discard the handle's unsynced
changes and give it the content it would have after a close and reopen.

- **Source:** Stated: #1111 rule 5; `lfs3.h:1577-1583`.
- **Measure:** content and flags after resync.
- **Pass:** `fsync::resync_*`, `fsync::yrrr`, `fsync::wyyy` and
  `kv::interop_resync` pass in B-DEF.
- **Fail:** unsynced content survives, or the content differs from a
  reopen.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-SYNC-08

littlefs shall return 0 without writing from `lfs3_file_sync` on a handle
whose file was removed, and `LFS3_ERR_NOENT` from `lfs3_file_resync` on it.

- **Source:** Stated: `lfs3.c:14557-14560`, `14622-14661`.
- **Measure:** return values and emubd prog count.
- **Pass:** `stickynotes::zombie_*` and `stickynotes::zombify_*` pass in
  B-DEF.
- **Fail:** any other result or a write.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-SYNC-09

littlefs shall leave a filesystem that passes `lfs3_fs_ck` when a handle
that returned a write error is later synced successfully. The content of the
range the failed write touched is unspecified. If the failed write left the
handle torn, `lfs3_file_sync` refuses it with `LFS3_ERR_INVAL` and
`lfs3_file_resync` returns it to the last sync (LFS3-ERR-03).

- **Source:** Stated: #1111 "Better recovery from runtime errors" ("file
  data remains undefined after an error"), and `lfs3.h:1571-1572` (a
  successful sync re-syncs a desynced handle). A write that spans several
  entries commits them one at a time (`lfs3.c:13213-13232`), so a failure in
  the middle could leave the handle's tree inconsistent (2-files B5).
- **Measure:** `lfs3_fs_ck(LFS3_CK_CKMETA | LFS3_CK_CKDATA)` and
  `lfs3_file_size` against the tree's weight.
- **Pass:** `badblocks::error_then_sync` sweeps `LFS3_ERR_NOSPC` (0 to all
  blocks of a full disk freed), bad blocks (the n-th prog or erase fails
  with `LFS3_ERR_CORRUPT`) and device errors (the n-th prog fails with
  `LFS3_ERR_IO`) over every point of an overwrite spanning several fragments
  or several data blocks and of its sync, then syncs the handle (or resyncs
  it if torn) and remounts; `lfs3_fs_ck` returns 0 and bytes outside the
  failed range match the model, in B-DEF and B-BIG with the prog-once check.
- **Fail:** the check fails, or bytes outside the range differ.
- **Verified by:** `badblocks::error_then_sync`.
- **Status:** Tested on v3-integration (75eeba26). The case found D-3 and
  D-4, fixed in `4968164e` (a retried crystallization progged over its own
  progs after `LFS3_ERR_IO`) and `ce2a68d6` (a remove failed with
  `LFS3_ERR_NOSPC` on a nearly full disk when an mdir had to split).
- **When:** every CI run.

#### LFS3-SYNC-10

littlefs shall not change what other handles and `lfs3_stat` see when
`lfs3_file_flush` is called.

- **Source:** Stated: `lfs3.h:1553-1560` (flush "does not update
  metadata").
- **Measure:** other handles' content and `lfs3_stat` size.
- **Pass:** `fsync::sync_*` with the FLUSH variants pass in B-DEF, and a NEW
  check of `lfs3_stat` after flush passes.
- **Fail:** a flushed write becomes visible.
- **Verified by:** as listed; NEW.
- **Status:** Partly tested.
- **When:** every CI run.

#### LFS3-SYNC-11

littlefs shall flush after every write on a handle opened with
`LFS3_O_FLUSH`, or on any handle when mounted with `LFS3_M_FLUSH`.

- **Source:** Stated: `lfs3.h:140, 246`.
- **Measure:** cache state after each write.
- **Pass:** `fsync::sync_*` with `FLUSH` 1 to 3 pass in B-DEF.
- **Fail:** data stays in the cache.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-SYNC-12

littlefs shall sync after every write on a handle opened with
`LFS3_O_SYNC`, or on any handle when mounted with `LFS3_M_SYNC`.

- **Source:** Stated: `lfs3.h:141, 247`.
- **Measure:** content seen by other handles after each write.
- **Pass:** `fsync::sync_*` with `SYNC` 1 to 3 pass in B-DEF.
- **Fail:** a write is not visible to other handles after it returns.
- **Verified by:** as listed. Truncate and fruncate are not synced; see open
  question Q8.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-SYNC-13

littlefs shall report a file created by an open, in-sync handle and not yet
synced as `LFS3_TYPE_STICKYNOTE` with size 0 in `lfs3_stat` and
`lfs3_dir_read`.

- **Source:** Stated: `lfs3.h:110` ("An uncommitted file"); #1111
  "Stickynotes".
- **Measure:** `info.type` and `info.size`.
- **Pass:** `stickynotes::uncreat_*` pass in B-DEF.
- **Fail:** any other type or size.
- **Verified by:** as listed. The `lfs3_info.type` comment is LFS3-DOC-08.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-SYNC-14

littlefs shall hide a stickynote that no in-sync, non-zombie handle holds:
`lfs3_stat` returns `LFS3_ERR_NOENT` and `lfs3_dir_read` skips it.

- **Source:** Stated: #1111 "Stickynotes" ("hidden from the user");
  `lfs3.c:7925-7950`.
- **Measure:** `lfs3_stat` and `lfs3_dir_read`.
- **Pass:** `stickynotes::orphan_*` pass in B-DEF.
- **Fail:** the orphan is visible.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-SYNC-15

littlefs shall remove from disk every stickynote that no handle holds when
mkconsistent runs.

- **Source:** Stated: #1111 "Stickynotes" ("automatically cleaned up on the
  next mount"); `lfs3.c:16538-16590`.
- **Measure:** stickynote tags left on disk.
- **Pass:** `stickynotes::cleanup_*`, `mount::t_mkconsistent` and
  `trvs::mkconsistent_*` pass in B-DEF.
- **Fail:** an unheld stickynote remains after `lfs3_fs_mkconsistent`.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-SYNC-16

littlefs shall clean up every stickynote left by closing more uncreated
handles than the remove queue holds.

- **Source:** Stated: `lfs3.c:12846-12864` (push to the grm, else set
  `LFS3_I_MKCONSISTENT`).
- **Measure:** stickynotes left on disk; `LFS3_I_MKCONSISTENT`.
- **Pass:** a NEW case closes three or more desynced uncreated handles,
  finds `LFS3_I_MKCONSISTENT` set, runs `lfs3_fs_mkconsistent`, and finds no
  stickynote left, in B-DEF.
- **Fail:** a stickynote remains.
- **Verified by:** `stickynotes::*_many` (indirect); NEW.
- **Status:** Partly tested.
- **When:** every CI run.

#### LFS3-SYNC-17

littlefs shall, for a handle opened with `LFS3_O_DESYNC`, write to disk only
on an explicit `lfs3_file_sync`.

- **Source:** Stated: `lfs3.h:142`.
- **Measure:** disk content after writes and close.
- **Pass:** `stickynotes::undesync_*` and `fsync::desync_wrrd` pass in
  B-DEF.
- **Fail:** the disk changes without an explicit sync.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-SYNC-18

littlefs shall let open handles of a removed or replaced file keep reading
their snapshot, while the name becomes free for new files.

- **Source:** Stated: `lfs3.c:11762-11800`; #1111 "Stickynotes".
- **Measure:** reads through the old handle; create of the same name.
- **Pass:** `stickynotes::zombie_*`, `stickynotes::zombify_*` and
  `stickynotes::fileonzombie_*` pass in B-DEF.
- **Fail:** the old handle's reads change, or the name cannot be reused.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-SYNC-19

littlefs shall return 0 from `lfs3_file_close` of a handle desynced by an
earlier error, and leave the disk as of the last successful sync.

- **Source:** Stated: `lfs3.h:1537-1538` ("desynchronized files do not touch
  disk and will always return 0"), `1568-1569`. An application therefore
  learns about lost data only from the earlier error (2-files B4).
- **Measure:** close result and disk content.
- **Pass:** `fwrite::fbig` and `alloc::nospc_files` pass in B-DEF.
- **Fail:** close returns an error or commits.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-SYNC-20

littlefs shall leave a handle syncable after an error in an append whose
graft replaces only the file's last entry: `lfs3_file_sync` then commits
the file with the appends that succeeded, and does not return
`LFS3_ERR_INVAL`.

- **Source:** Derived: a graft that replaces several entries commits once
  per entry, because the entries can be in different leaves, and an error
  between those commits tears the handle (`LFS3_o_TORN`): sync returns
  `LFS3_ERR_INVAL` until `lfs3_file_resync` (LFS3-ERR-03). An append that
  coalesces with the file's last fragment replaces only that entry, and
  what it appends lands in the same leaf, so it needs one commit
  (Appendix B.5 C). A logger appending with errors then never sees a torn
  handle.
- **Measure:** `lfs3_file_sync` result after up to 64 appends with each
  block in turn bad, and the content after remount.
- **Pass:** `badblocks::append_torn` (fragments of up to 64 bytes,
  16-byte appends, `BADBLOCK_BEHAVIOR` PROGERROR, ERASEERROR and
  READERROR, `PROG_SIZE` 1 and 16) gets 0 from every sync, and reads back
  the old content followed by whole appends, in B-DEF.
- **Fail:** sync returns `LFS3_ERR_INVAL`, or the content differs.
- **Verified by:** `badblocks::append_torn`; for power loss,
  `powerloss::append_pl` and `powerloss::append_unsynced_pl`.
- **Status:** Known defect at `b10efaa` and `fd3157e3`, where
  `badblocks::append_torn` gets `LFS3_ERR_INVAL` with READERROR at
  `PROG_SIZE` 16. Tested on `v3-integration` (`554e89f9`).
- **When:** every CI run.

### 6.9 Directories and paths (DIR)

#### LFS3-DIR-01

littlefs shall return `LFS3_ERR_NOENT`, `LFS3_ERR_NOTDIR`,
`LFS3_ERR_EXIST`, `LFS3_ERR_INVAL` or `LFS3_ERR_NAMETOOLONG` from
`lfs3_mkdir` for a missing parent, a parent that is not a directory, an
existing name, an empty path or a path above the root, and a name longer than
`name_limit`, respectively.

- **Source:** Derived: POSIX `mkdir` semantics as adapted by littlefs;
  `lfs3.c:11479-11600`.
- **Measure:** return values.
- **Pass:** `dirs::mkdir_*` and `paths::*` pass in B-DEF.
- **Fail:** any other result.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-DIR-02

littlefs shall create a directory whose name is at most `name_limit` bytes,
or return an error code from `lfs3.h`, without asserting, at every block size
it accepts.

- **Source:** Derived from LFS3-META-03. With the default `name_limit` of
  255, `lfs3_mkdir` with a name of 184 to 255 bytes on 512-byte blocks trips
  `LFS3_ASSERT(err != LFS3_ERR_RANGE)` at `lfs3.c:8777`. `lfs3_set` with the
  same names works.
- **Measure:** return value and asserts.
- **Pass:** a NEW case runs `lfs3_mkdir` with names of 1 to 255 bytes on
  512-byte and 1024-byte blocks and gets 0 or an error code from `lfs3.h`
  (open question Q3), with no assert, in B-DEF.
- **Fail:** an assert.
- **Verified by:** NEW. `paths::namejustlongenough` runs at 4096 only.
- **Status:** Known defect (1-meta 0.2).
- **When:** every CI run.

#### LFS3-DIR-03

littlefs shall return `LFS3_ERR_NOENT` from `lfs3_remove` for a missing or
hidden entry, `LFS3_ERR_BUSY` for the root, and `LFS3_ERR_NOTEMPTY` for a
directory that has entries.

- **Source:** Stated: `lfs3.h:1450-1455` ("the directory must be empty");
  `lfs3.c:11733-11736`.
- **Measure:** return values.
- **Pass:** `dirs::rm_*`, `dirs::rm_root` and `paths::root` pass in B-DEF.
- **Fail:** any other result.
- **Verified by:** as listed. Whether an open uncreated file makes its
  directory non-empty is open question Q7.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-DIR-04

littlefs shall return `LFS3_ERR_NOENT`, `LFS3_ERR_BUSY`, `LFS3_ERR_ISDIR`,
`LFS3_ERR_NOTDIR` or `LFS3_ERR_NOTEMPTY` from `lfs3_rename` for a missing
source, the root as source or destination, a file onto a directory, a
directory onto a file, and a directory onto a non-empty directory,
respectively.

- **Source:** Stated: `lfs3.h:1458-1466` ("the destination ... must match
  the source in type"; "the directory must be empty").
- **Measure:** return values.
- **Pass:** `dirs::mv_*` and `paths::*` pass in B-DEF.
- **Fail:** any other result.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-DIR-05

littlefs shall return `LFS3_ERR_INVAL`, and change nothing, when
`lfs3_rename` would move a directory into itself or into one of its
descendants.

- **Source:** Derived: POSIX `rename` returns `EINVAL` here; otherwise the
  subtree is detached from the root and its entries occupy metadata forever.
- **Measure:** return value; `lfs3_stat` of both paths; directory listing.
- **Pass:** a NEW case (`mkdir a`, `mkdir a/x`, `rename("a", "a/b")`) gets
  `LFS3_ERR_INVAL`, and `a` and `a/x` still exist, in B-DEF.
- **Fail:** any other result.
- **Verified by:** NEW.
- **Status:** Known defect (4-api R1: the rename returns 0, `a` and `a/b`
  both give `LFS3_ERR_NOENT`, and `lfs3_fs_ck` returns 0).
- **When:** every CI run.

#### LFS3-DIR-06

littlefs shall return 0 and change nothing when `lfs3_rename` names the same
entry as source and destination.

- **Source:** Derived: POSIX `rename`; `lfs3.c:11835-12014`.
- **Measure:** return value; emubd prog count.
- **Pass:** `dirs::mv_noop` passes in B-DEF.
- **Fail:** an error or a change.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-DIR-07

littlefs shall replace an existing destination of the same type in
`lfs3_rename`, where a replaced directory must be empty.

- **Source:** Stated: `lfs3.h:1458-1466`.
- **Measure:** content at the destination; absence of the source.
- **Pass:** `dirs::mv_*` replace cases pass in B-DEF.
- **Fail:** the destination keeps its old content, or the source remains.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-DIR-08

littlefs shall fill `struct lfs3_info` from `lfs3_stat` with the entry's
type, its size for regular files (0 otherwise), and its final path
component, with "/" for the root.

- **Source:** Stated: `lfs3.h:719-732, 1468-1472`.
- **Measure:** `info` fields.
- **Pass:** `dirs::*` and `paths::*` pass in B-DEF.
- **Fail:** any field differs.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-DIR-09

littlefs shall return from `lfs3_dir_read` the entries ".", "..", then every
visible entry of the directory in name order, then `LFS3_ERR_NOENT` on this
and every later call.

- **Source:** Stated: `lfs3.h:1676-1681`.
- **Measure:** sequence of entries.
- **Pass:** `dirs::ordering`, `dirs::ordering_length` and
  `dread::read_idempotent` pass in B-DEF.
- **Fail:** any other sequence.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-DIR-10

littlefs shall return, from an open directory iteration, every entry that
existed during the whole iteration exactly once, while other calls create,
remove or rename entries.

- **Source:** Derived: handle position fix-ups keep open directories valid
  (`lfs3.c:9350-9452`).
- **Measure:** entries returned.
- **Pass:** `dread::read_with_*` and `dread::read_neighbor_*` pass in B-DEF.
- **Fail:** such an entry is missing or repeated.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-DIR-11

littlefs shall resume a directory iteration at the same entry after
`lfs3_dir_seek` to any value that `lfs3_dir_tell` returned, including 0 and
1, and including in a directory that holds orphaned stickynotes. A position
counts the entries `lfs3_dir_read` returns, so entries it hides do not
move it.

- **Source:** Stated: `lfs3.h:1683-1697` ("The new off must be a value
  previous returned from tell", "does indicate the current position in the
  directory iteration"). `lfs3_dir_read` hides orphaned stickynotes
  (LFS3-SYNC-14); counting them in seek but not in read sends a seek one
  entry back per orphan before the position (issue #12). Counting only what
  read returns also keeps a saved position valid when mkconsistent removes
  hidden orphans between tell and seek.
- **Measure:** entries read after the seek.
- **Pass:** `dread::seek_tell` records `lfs3_dir_tell` before each read of
  directories of 0 to 64 entries, with and without an orphaned stickynote
  before the first entry and after every entry, seeks back to each value
  from every position, and reads the same remaining sequence, in B-DEF,
  B-YGB and B-BIG.
- **Fail:** any other sequence.
- **Verified by:** `dread::seek_tell` (NEW-33).
- **Status:** Tested on `v3-integration` (233ff491). Known defect at
  `b10efaa` (1-meta 0.1: `off - 2` wraps at `lfs3.c:12241`); later, seek
  still counted orphaned stickynotes that read skips (issue #12).
- **When:** every CI run.

#### LFS3-DIR-12

littlefs shall restart a directory iteration at "." after
`lfs3_dir_rewind`.

- **Source:** Stated: `lfs3.h:1699-1702`.
- **Measure:** entries read after the rewind.
- **Pass:** `dread::rewind` passes in B-DEF.
- **Fail:** any other sequence.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-DIR-13

littlefs shall return `LFS3_ERR_NOENT` from `lfs3_dir_read` on a directory
handle whose directory was removed.

- **Source:** Derived: a removed directory has no entries to return;
  `lfs3.c:12148-12222`.
- **Measure:** return value.
- **Pass:** `dread::read_rm` and `dread::read_rm_remkdir` pass in B-DEF.
- **Fail:** entries of another directory are returned.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-DIR-14

littlefs shall resolve paths from the root, ignoring leading and repeated
"/" and "." components, cancelling "name/.." pairs lexically, and returning
`LFS3_ERR_INVAL` for an empty path or a ".." above the root.

- **Source:** Stated: the path rules tested in `tests/test_paths.toml`,
  including the documented deviation from POSIX for "name/.."
  (`tests/test_paths.toml:2016-2028`).
- **Measure:** results of `lfs3_stat`, `lfs3_mkdir`, `lfs3_file_opencfg`.
- **Pass:** `paths::*` (38 cases) pass in B-DEF.
- **Fail:** any other resolution.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-DIR-15

littlefs shall accept any byte except "/" and NUL in a name, check the
length only when a name is created, and return `LFS3_ERR_NOENT` when looking
up a name longer than `name_limit`.

- **Source:** Stated: `tests/test_paths.toml` (`nonutf8`, `oopsallffs`,
  `nonprintable`, `nametoolong`).
- **Measure:** results of create and lookup.
- **Pass:** `paths::nonutf8`, `paths::oopsallffs`, `paths::nonprintable`,
  `paths::nametoolong` and `paths::namejustlongenough` pass in B-DEF.
- **Fail:** any other result.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-DIR-16

littlefs shall give every directory a distinct id, including when ids
computed from names collide.

- **Source:** Stated: `lfs3.c:11552-11582`.
- **Measure:** directory contents.
- **Pass:** `dirs::did_collisions`, `dirs::did_zero`, `dirs::did_ones` and
  `dirs::did_leb128_boundaries` pass in B-DEF.
- **Fail:** entries appear in the wrong directory.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-DIR-17

littlefs shall report an entry of an unknown type as `LFS3_TYPE_UNKNOWN`,
refuse to open it with `LFS3_ERR_NOTSUP`, and allow it to be removed and
renamed.

- **Source:** Stated: `lfs3.h:102-111`.
- **Measure:** results of stat, open, remove and rename.
- **Pass:** `mount::incompat_unknown_type*` pass in B-DEF.
- **Fail:** any other result.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-DIR-18

littlefs shall store several directories in one metadata block.

- **Source:** Stated: #1111 "A simpler/more robust metadata tree"
  ("removing the 1-dir = 1-block minimum requirement").
- **Measure:** `lfs3_fs_usage` after creating empty directories.
- **Pass:** a NEW case creates 32 empty directories with short names on
  4096-byte blocks and finds `lfs3_fs_usage` increased by at most 4 blocks,
  in B-DEF.
- **Fail:** a larger increase.
- **Verified by:** NEW.
- **Status:** Untested.
- **When:** every CI run.

#### LFS3-DIR-19

littlefs shall return `LFS3_ERR_NOENT` from `lfs3_dir_open` for a missing
path and `LFS3_ERR_NOTDIR` for a path that is not a directory.

- **Source:** Derived: POSIX `opendir`; `lfs3.c:12087-12138`.
- **Measure:** return values.
- **Pass:** `dirs::*` and `paths::*` pass in B-DEF.
- **Fail:** any other result.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-DIR-20

littlefs shall release a directory handle in `lfs3_dir_close`.

- **Source:** Stated: `lfs3.h:1670-1674`.
- **Measure:** `lfs3_unmount` succeeds after the close.
- **Pass:** every suite that opens directories passes in B-DEF.
- **Fail:** unmount asserts on an open handle.
- **Verified by:** `dirs::*`, `dread::*`.
- **Status:** Tested.
- **When:** every CI run.

### 6.10 Custom attributes (ATTR)

#### LFS3-ATTR-01

littlefs shall create or replace an attribute with `lfs3_setattr`, and
return from `lfs3_getattr` the smaller of the buffer size and the attribute
size, silently truncating.

- **Source:** Stated: `lfs3.h:1474-1493`.
- **Measure:** return values and bytes.
- **Pass:** `attrs::setattr*`, `attrs::getattr*`, `attrs::setattr_trunc` and
  `attrs::fuzz` pass in B-DEF.
- **Fail:** any other result.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-ATTR-02

littlefs shall return `LFS3_ERR_NOATTR` from `lfs3_getattr` and
`lfs3_sizeattr` for an attribute that does not exist.

- **Source:** Stated: `lfs3.h:95`.
- **Measure:** return values.
- **Pass:** `attrs::*noattr*` pass in B-DEF.
- **Fail:** any other result.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-ATTR-03

littlefs shall remove an attribute with `lfs3_removeattr`, returning
`LFS3_ERR_NOATTR` if it does not exist.

- **Source:** Stated: `lfs3.h:1495-1500`.
- **Measure:** return values; `lfs3_sizeattr` afterwards.
- **Pass:** `attrs::removeattr` passes in B-DEF.
- **Fail:** any other result.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-ATTR-04

littlefs shall store an attribute of size 0, including one set with a NULL
buffer, as existing and empty.

- **Source:** Derived: an empty attribute differs from a missing one
  (`LFS3_ERR_NOATTR`).
- **Measure:** `lfs3_sizeattr`.
- **Pass:** `attrs::setattr_zero` and `attrs::setattr_null` pass in B-DEF.
- **Fail:** `LFS3_ERR_NOATTR` or another size.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-ATTR-05

littlefs shall define a maximum attribute size and return an error code
from `lfs3.h`, without asserting and without changing the disk, from
`lfs3_setattr` and from a file sync with a larger attribute.

- **Source:** Derived from LFS3-META-03. `lfs3.h` documents no limit; v2
  had `LFS_ATTR_MAX` and returned `LFS_ERR_NOSPC`.
- **Measure:** return value, asserts, emubd prog count.
- **Pass:** a NEW case sets attributes of 0 to `block_size` bytes on root
  and on a file, at block sizes 512 and 4096, and every call returns 0 or the
  documented error, with no assert, in B-DEF.
- **Fail:** an assert, an undocumented code, or a partial write.
- **Verified by:** NEW. `attrs::*` go up to 513 bytes.
- **Status:** Known defect (4-api R3: 2500 bytes on 4096-byte blocks asserts
  at `lfs3.c:9006`; with `LFS3_NO_ASSERT` 5000 bytes returns
  `LFS3_ERR_RANGE`).
- **When:** every CI run.

#### LFS3-ATTR-06

littlefs shall support attributes on the root directory ("/").

- **Source:** Derived: `lfs3_setattr` accepts any path, and root attributes
  live beside the configuration in the mroot.
- **Measure:** set, get, size and remove on "/".
- **Pass:** `attrs::root` sets, gets, sizes and removes attributes on "/"
  across remounts, an mroot chain extension and mroot relocations, with and
  without a file in the mroot, in B-DEF.
- **Fail:** any error or wrong value.
- **Verified by:** `attrs::root` (a probe passed at `b10efaa`).
- **Status:** Tested on v3-integration (d3ccde9a).
- **When:** every CI run.

#### LFS3-ATTR-07

littlefs shall store the attributes of all 256 type values independently.

- **Source:** Stated: `lfs3.h:914-917` (types 0x80-0xff map to a separate
  tag range).
- **Measure:** values after setting each type.
- **Pass:** a NEW case sets a distinct value for every type 0x00 to 0xff on
  one file and reads each back, in B-DEF.
- **Fail:** any type reads another type's value.
- **Verified by:** NEW. Whether reserved types should be refused is open
  question Q9.
- **Status:** Untested.
- **When:** every CI run.

#### LFS3-ATTR-08

littlefs shall load the readable attributes of `lfs3_file_cfg.attrs` at
open, and keep them current with syncs from other handles and with
`lfs3_setattr` and `lfs3_removeattr`.

- **Source:** Stated: `lfs3.h:799-802` ("If readable, these attributes will
  be kept up to date with the attributes on-disk").
- **Measure:** buffer contents and `*size`.
- **Pass:** `attrs::fattr_*` (including `_broadcast`, `_setattr_broadcast`,
  `_desync_no_receive`, `_resync_receive`) pass in B-DEF.
- **Fail:** a stale value.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-ATTR-09

littlefs shall write the writable attributes of `lfs3_file_cfg.attrs` in
the same commit as the file's sync, and write an attribute flagged
`LFS3_A_LAZY` only when the file itself changed.

- **Source:** Stated: `lfs3.h:173, 799-802`.
- **Measure:** attribute values on disk after syncs with and without file
  changes.
- **Pass:** `attrs::fattr_*` including `attrs::fattr_lazy` pass in B-DEF.
- **Fail:** an attribute is written separately, or a lazy one is written
  without a file change.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-ATTR-10

littlefs shall remove an attribute at sync when its `buffer_size` in
`lfs3_file_cfg.attrs` is `LFS3_ERR_NOATTR`.

- **Source:** Stated: `lfs3.h:777-779`.
- **Measure:** `lfs3_sizeattr` after the sync.
- **Pass:** a NEW case finds `LFS3_ERR_NOATTR` after the sync, in B-DEF.
- **Fail:** the attribute remains.
- **Verified by:** `attrs::fattr_*` (indirect); NEW.
- **Status:** Partly tested.
- **When:** every CI run.

#### LFS3-ATTR-11

littlefs shall move an entry's attributes with it on rename, and remove
them with it on remove.

- **Source:** Derived: attributes belong to the entry.
- **Measure:** attributes at the new name; attributes of a new entry that
  reuses the old name.
- **Pass:** `attrs::mv_src`, `attrs::mv_dst`, `attrs::rm` and
  `attrs::mvrm_fuzz_fuzz` pass in B-DEF.
- **Fail:** an attribute is lost, or survives on a new entry.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-ATTR-12

littlefs shall accept a file-attached attribute with flags
`LFS3_A_RDONLY | LFS3_A_LAZY`.

- **Source:** Stated: `lfs3.h:165-173` (the flags combine).
- **Measure:** open result.
- **Pass:** a NEW case opens a file with such an attribute and reads it, in
  B-DEF.
- **Fail:** an assert.
- **Verified by:** NEW.
- **Status:** Known defect (2-files B11: the check reuses the file-flag
  helper, and `LFS3_A_LAZY` equals `LFS3_O_CREAT`, `lfs3.c:12810-12816`).
- **When:** every CI run.

#### LFS3-ATTR-13

littlefs shall return the size of an existing attribute from
`lfs3_sizeattr`.

- **Source:** Stated: `lfs3.h:1482-1485`.
- **Measure:** return value.
- **Pass:** `attrs::*` pass in B-DEF.
- **Fail:** any other value.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-ATTR-14

littlefs shall fill with zeros the bytes of a readable
`lfs3_file_cfg.attrs` buffer past an attribute shorter than the buffer,
whenever it loads or updates the buffer, and shall leave the buffer as the
application set it when the attribute does not exist.

- **Source:** Derived. `lfs3.h` is silent on these bytes; littlefs v2
  states both rules for file attributes ("if the stored attribute is
  smaller than the buffer, it will be padded with zeros", "If the
  attribute is not found, it will be created implicitly", `lfs.h`). Without
  `size`, an attribute's size is `buffer_size` (`lfs3.h`), so a writable
  one writes the whole buffer back at every sync: unfilled bytes would put
  whatever the stack held on disk (issue #2, found by valgrind). An
  attribute that does not exist is created from the buffer, so the
  application initialises that buffer.
- **Measure:** buffer contents after open, after `lfs3_setattr` of a
  shorter value, and after another handle syncs a shorter value; valgrind
  over `test_attrs`.
- **Pass:** `attrs::fattr_zerofill` (NEW-131) finds zeros past the
  attribute and the application's bytes when the attribute is missing,
  with and without `size`, in B-DEF, B-YGB and B-BIG, and
  `test.py --valgrind -Pnone` over the CI valgrind suites reports nothing.
- **Fail:** a non-zero byte past the attribute, a changed buffer for a
  missing attribute, or a valgrind report.
- **Verified by:** `attrs::fattr_zerofill` (NEW-131); the CI valgrind job.
- **Status:** Tested on `v3-integration` (245b84c9), and the CI valgrind
  suite set passes. Before, open and the updates from `lfs3_setattr` and
  other handles' syncs left the bytes past the attribute untouched, and 9
  `attrs::fattr_*` cases with `MODE=2, MUTSIZE=0` failed under valgrind
  (issue #2).
- **When:** every CI run.

### 6.11 Key-value API (KV)

#### LFS3-KV-01

littlefs shall return from `lfs3_get` the first `min(size, file size)` bytes
of the file.

- **Source:** Stated: `lfs3.h:1429-1435`; #1111 "Simple key-value APIs".
- **Measure:** return value and bytes.
- **Pass:** `kv::set`, `kv::set_update`, `kv::many`, `kv::fuzz` pass in
  B-DEF.
- **Fail:** any other result.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-KV-02

littlefs shall return the file's size from `lfs3_size`.

- **Source:** Stated: `lfs3.h:1437-1440`.
- **Measure:** return value.
- **Pass:** `kv::*` pass in B-DEF.
- **Fail:** any other value.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-KV-03

littlefs shall create or replace the whole content of a file with
`lfs3_set`.

- **Source:** Stated: `lfs3.h:1442-1447`.
- **Measure:** content after the call and after remount.
- **Pass:** `kv::set`, `kv::set_trunc`, `kv::set_update`, `kv::many_big` and
  `kv::fuzz_big` pass in B-DEF.
- **Fail:** any other content.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-KV-04

littlefs shall return `LFS3_ERR_FBIG` from `lfs3_set` when the value is
larger than `file_limit`, and leave the file unchanged.

- **Source:** Stated: `lfs3.h:660-665` (file_limit "must be respected by
  other littlefs drivers").
- **Measure:** return value and the file afterwards.
- **Pass:** a NEW case with `file_limit=100` gets `LFS3_ERR_FBIG` for 101 and
  8192 bytes, and the old value remains, in B-DEF.
- **Fail:** the value is stored.
- **Verified by:** NEW.
- **Status:** Known defect (4-api R7: both calls return 0; the file then
  fails `lfs3_file_seek(..., 0, LFS3_SEEK_END)` with `LFS3_ERR_INVAL`).
- **When:** every CI run.

#### LFS3-KV-05

littlefs shall return `LFS3_ERR_NOENT` from `lfs3_get` and `lfs3_size` for a
missing path and `LFS3_ERR_ISDIR` for a directory.

- **Source:** Derived: they open the path as a file.
- **Measure:** return values.
- **Pass:** `kv::set_noent` and a NEW directory case pass in B-DEF.
- **Fail:** any other result.
- **Verified by:** as listed; NEW.
- **Status:** Partly tested.
- **When:** every CI run.

#### LFS3-KV-06

littlefs shall update open in-sync handles of the file when `lfs3_set`
replaces it.

- **Source:** Derived from LFS3-SYNC-02; `lfs3_set` syncs like a close.
- **Measure:** content seen by the open handle.
- **Pass:** `kv::interop_sync` passes in B-DEF.
- **Fail:** the handle keeps the old content.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-KV-07

littlefs shall store an empty value from `lfs3_set` with size 0, including
with a NULL buffer.

- **Source:** Derived: an empty file is a valid value.
- **Measure:** `lfs3_size`.
- **Pass:** `kv::set_zero` and `kv::set_null` pass in B-DEF.
- **Fail:** an error or another size.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-KV-08

littlefs shall return 0 bytes from `lfs3_get`, and 0 from `lfs3_size`, for
a file that an open handle created and has not synced.

- **Source:** Derived from LFS3-SYNC-13 (the stickynote is visible with
  size 0).
- **Measure:** return values.
- **Pass:** a NEW case passes in B-DEF.
- **Fail:** `LFS3_ERR_NOENT` or another value.
- **Verified by:** NEW.
- **Status:** Untested.
- **When:** every CI run.

#### LFS3-KV-09

littlefs shall create a new file with `lfs3_set` in a single metadata commit
when the value fits the inline limits (`shrub_size`, `fragment_size` and
`crystal_thresh`).

- **Source:** Stated: #1114 (2026-03-12): "In theory `lfs3_set` can improve
  performance by merging file name + data into a single commit";
  `lfs3.c:12707-12724`.
- **Measure:** number of mdir commits (emubd prog count and checksum tags).
- **Pass:** a NEW internal case counts one commit for a 16-byte value, in
  B-DEF.
- **Fail:** more than one commit.
- **Verified by:** NEW.
- **Status:** Untested.
- **When:** every CI run.

### 6.12 Block allocation (ALLOC)

#### LFS3-ALLOC-01

littlefs shall never allocate a block that the committed filesystem, an
open handle, or an in-flight write references.

- **Source:** Stated: `lfs3.c:2178-2187` (checkpoint protocol).
- **Measure:** internal clobber checks.
- **Pass:** `alloc::clobber_dirs`, `alloc::clobber_files`,
  `alloc::clobber_open_files`, `trvs::clobber_*` and
  `trvs::rewind_clobber_*` pass in B-DEF (and with `GBMAP=true` in B-YGB).
- **Fail:** a referenced block is allocated.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-ALLOC-02

littlefs shall allocate each block at most once between two allocator
checkpoints.

- **Source:** Stated: `lfs3.c:2183-2184` ("blocks are allocated at most
  once, and never reallocated, between checkpoints").
- **Measure:** internal allocation sequence.
- **Pass:** `alloc::alloc` and `alloc::reuse` pass in B-DEF.
- **Fail:** a block is returned twice.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-ALLOC-03

littlefs shall return `LFS3_ERR_NOSPC` when no block is free, and keep all
synced data intact and readable, before and after remount.

- **Source:** Stated: `lfs3.h:93`; `lfs3.c:11160-11166`.
- **Measure:** return values; content.
- **Pass:** `alloc::nospc_dirs` and `alloc::nospc_files` pass in B-DEF.
- **Fail:** another error, or changed data.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-ALLOC-04

littlefs shall let a write succeed after `LFS3_ERR_NOSPC` once enough files
have been removed.

- **Source:** Derived: a full filesystem must be recoverable by deleting.
- **Measure:** results of remove and of the following write.
- **Pass:** a NEW case fills the disk until `LFS3_ERR_NOSPC`, removes half
  the files, and writes a new file of the freed size, in B-DEF.
- **Fail:** the remove or the write fails.
- **Verified by:** NEW. `badblocks::error_then_sync` removes files from a
  full disk with 0 to all of their blocks freed.
- **Status:** Untested.
- **When:** every CI run.

#### LFS3-ALLOC-05

littlefs shall let `lfs3_remove` succeed on a full filesystem that uses the
gbmap.

- **Source:** Stated: commit `5d70e47` ("at least with an error the user can
  call rmgbmap"). Every commit checkpoints first, and a checkpoint may need
  new blocks to repopulate the gbmap (3-alloc B10).
- **Measure:** result of remove, and of `lfs3_fs_rmgbmap`, at NOSPC.
- **Pass:** a NEW case fills a gbmap filesystem until `LFS3_ERR_NOSPC`, then
  `lfs3_remove` of a file returns 0, in B-YGB and in B-BIG.
- **Fail:** the remove fails with `LFS3_ERR_NOSPC`.
- **Verified by:** NEW.
- **Status:** Untested (suspected, 3-alloc B10).
- **When:** every CI run.

#### LFS3-ALLOC-06

littlefs shall work with any `lookahead_size` from 1 byte up to more than
`block_count / 8` bytes.

- **Source:** Stated: `lfs3.h:552-558`.
- **Measure:** suite results.
- **Pass:** `alloc::*`, `files::*` and `dirs::*` pass with `LOOKAHEAD_SIZE`
  in {1, 2, 16, 32, 64} (the last two cover all 256 blocks) in B-DEF.
- **Fail:** any permutation fails.
- **Verified by:** as listed.
- **Status:** Partly tested (16 by default; large values only in the B-tree
  and bad-block suites).
- **When:** nightly.

#### LFS3-ALLOC-07

littlefs shall wrap the allocation window around the end of the device.

- **Source:** Stated: `lfs3.c:10999-11042`.
- **Measure:** content after many allocations.
- **Pass:** `alloc::wraparound_files` passes in B-DEF.
- **Fail:** any failure.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-ALLOC-08

littlefs shall still detect a full disk when a block is allocated before the
first allocator checkpoint of a mount.

- **Source:** Derived: `lookahead.ckpoint` is unsigned and starts at 0 at
  mount (`lfs3.c:15226`); an allocation before the first checkpoint, such as
  the gbmap commit during pre-erase, decrements it past zero (3-alloc B2).
- **Measure:** `LFS3_ERR_NOSPC` detection after mount with
  `LFS3_M_PREERASE` and without `LFS3_M_LOOKAHEAD` on a nearly full disk.
- **Pass:** a NEW case gets `LFS3_ERR_NOSPC` from the first write that does
  not fit, within 2 × `block_count` allocations, in B-BIG.
- **Fail:** the allocator loops, or hands out a block allocated in the same
  pre-erase commit.
- **Verified by:** NEW.
- **Status:** Untested (the underflow is certain from the code; reaching it
  was not confirmed).
- **When:** every CI run.

#### LFS3-ALLOC-09

littlefs shall allocate from the gbmap only blocks recorded free or erased
inside the known window.

- **Source:** Stated: commit `843412c`; `lfs3.c:11046-11119`.
- **Measure:** internal allocation checks.
- **Pass:** `gbmap::files`, `gbmap::gc_files` and `alloc::*` with
  `GBMAP=true` pass in B-YGB and B-BIG.
- **Fail:** a block outside the window, or in use, is allocated.
- **Verified by:** as listed.
- **Status:** Partly tested (compiled out of B-DEF).
- **When:** every CI run.

#### LFS3-ALLOC-10

littlefs shall keep the gbmap as a set of non-overlapping ranges covering
all blocks, merging equal neighbours and splitting ranges on update.

- **Source:** Stated: #1111 "Efficient block allocation" ("compress the
  block ranges in the B-tree"); `lfs3.c:10535-10662`.
- **Measure:** internal range checks.
- **Pass:** `gbmap::set_*` and `gbmap::set_ecksum_*` pass in B-YGB.
- **Fail:** any check fails.
- **Verified by:** as listed.
- **Status:** Partly tested (compiled out of B-DEF).
- **When:** every CI run.

#### LFS3-ALLOC-11

littlefs shall repopulate the gbmap at a checkpoint when at most
`lookgbmap_thresh` blocks have a known state, and when none do if
`lookgbmap_thresh` is 0.

- **Source:** Stated: `lfs3.h:704-716` ("When <= this many blocks have a
  known state"; "0 only repopulates the gbmap when empty").
- **Measure:** whether a checkpoint repopulates, for `known` equal to the
  threshold and for a threshold of 0 with `known` 0.
- **Pass:** a NEW internal case with `LOOKGBMAP_THRESH` in {0, 1,
  `BLOCK_COUNT/4`} sees a repopulation in both situations, in B-YGB.
- **Fail:** no repopulation.
- **Verified by:** NEW. The suites never vary `LOOKGBMAP_THRESH`.
- **Status:** Known defect (3-alloc B11: the code tests
  `known < min(thresh, block_count)`, `lfs3.c:10800-10802`, so a threshold of
  0 never repopulates at a checkpoint). Either the code or the comment must
  change.
- **When:** every CI run.

#### LFS3-ALLOC-12

littlefs shall never leave on disk a gbmap that records as free or erased,
inside its known window, a block that the committed filesystem references.

- **Source:** Derived: the gbmap is committed in the same mdir commit as the
  change that references the block (3-alloc §3 I2).
- **Measure:** gbmap content against a traversal, after every power loss.
- **Pass:** `powerloss::*` and `grow::incr_spam_*_pl_fuzz` in B-YGB pass
  under `-Plinear` with PLB-TORN, with a NEW internal check of the gbmap
  against a traversal after each remount.
- **Fail:** the check finds such a block.
- **Verified by:** as listed; NEW check.
- **Status:** Partly tested (the reentrant cases run with the gbmap only in
  gbmap builds; no case compares the gbmap with a traversal).
- **When:** nightly.

#### LFS3-ALLOC-13

littlefs shall enable the gbmap with `lfs3_fs_mkgbmap`, returning
`LFS3_ERR_EXIST` if it is already enabled.

- **Source:** Stated: `lfs3.h:1825-1831`; commit `ad2e8b3` for the return
  code.
- **Measure:** return value; `LFS3_I_GBMAP`; allocation afterwards.
- **Pass:** `gbmap::mkgbmap`, `gbmap::rmmkgbmap`, `gbmap::mkrmgbmap` and
  `gbmap::mkgbmap_exist` pass in a build with `LFS3_GBMAP` and without
  `LFS3_YES_GBMAP`.
- **Fail:** any other result.
- **Verified by:** as listed. The header comment is LFS3-DOC-07.
- **Status:** Partly tested (compiled out of B-DEF).
- **When:** every CI run.

#### LFS3-ALLOC-14

littlefs shall disable the gbmap with `lfs3_fs_rmgbmap`, returning
`LFS3_ERR_NOENT` if it is not enabled.

- **Source:** Stated: `lfs3.h:1833-1839`.
- **Measure:** return value; `LFS3_I_GBMAP`; mount by a build without
  `LFS3_GBMAP` afterwards.
- **Pass:** `gbmap::rmgbmap` and `gbmap::rmgbmap_noent` pass in a build with
  `LFS3_GBMAP` and without `LFS3_YES_GBMAP`.
- **Fail:** any other result.
- **Verified by:** as listed.
- **Status:** Partly tested (compiled out of B-DEF).
- **When:** every CI run.

#### LFS3-ALLOC-15

littlefs shall mount a gbmap image read-write only in builds that support
the gbmap, and read-only in any build.

- **Source:** Stated: `LFS3_WCOMPAT_GBMAP` (`lfs3.h:945`) with the wcompat
  rules of #1111 "compat flag system".
- **Measure:** mount results across builds.
- **Pass:** a NEW cross-build case: an image with a gbmap mounts RDWR in
  B-YGB and in a `LFS3_GBMAP` build, gives `LFS3_ERR_NOTSUP` for RDWR and 0
  for `LFS3_M_RDONLY` in B-DEF; an image without a gbmap mounts RDWR in
  B-DEF and in a `LFS3_GBMAP` build.
- **Fail:** any other result.
- **Verified by:** NEW.
- **Status:** Untested.
- **When:** every CI run.

#### LFS3-ALLOC-16

littlefs shall return blocks used by earlier versions of the gbmap tree to
the free pool by the next repopulation.

- **Source:** Derived: the gbmap allocates its own nodes from behind the
  window; the design relies on the next repopulation to reclaim them
  (3-alloc F3, commits `316ca1c` and `92620d3`).
- **Measure:** `lfs3_fs_usage` over a long run with a fixed file set.
- **Pass:** a NEW case rewrites the same files 10,000 times in B-YGB and
  finds `lfs3_fs_usage` bounded (no growth after the first 1,000 rewrites
  beyond 2 blocks).
- **Fail:** usage keeps growing.
- **Verified by:** NEW.
- **Status:** Untested.
- **When:** nightly.

#### LFS3-ALLOC-17

littlefs shall return from `lfs3_fs_usage` a count no smaller than the
number of distinct blocks in use.

- **Source:** Stated: `lfs3.h:1744-1750` ("best effort ... may be larger").
- **Measure:** return value against a traversal of distinct blocks.
- **Pass:** a NEW case compares the value with the distinct-block count from
  `lfs3_trv_read` over `files::*` style workloads, in B-DEF and B-YGB.
- **Fail:** a smaller value, or a negative value other than an error.
- **Verified by:** `grow::*` (only `>= 0`); NEW.
- **Status:** Partly tested.
- **When:** every CI run.

### 6.13 Pre-erase (PRE)

Pre-erase (`LFS3_PREERASE`) needs `LFS3_GBMAP` and `LFS3_REVPERTURB` at
compile time (`lfs3_util.h:111-114`) and `LFS3_M_REVPERTURB` at mount. The
second is enforced by asserts in format, mount, gc, check and traversal
(`lfs3.c:16042`, `16332`, `16812-16817`, `16854-16859`, `17137-17142`), but is
not documented next to `gc_preerase_count` (LFS3-DOC-04). All pre-erase cases
are compiled out of B-DEF.

#### LFS3-PRE-01

littlefs shall keep up to `gc_preerase_count` erased blocks ahead of the
allocation window, erasing free blocks during gc.

- **Source:** Stated: `lfs3.h:608-620`; #1111 "Pre-erased block tracking".
- **Measure:** emubd erase counts and the gbmap's erased ranges.
- **Pass:** `gc::preerase_progress`, `gc::preerase_relaxed`,
  `gc::preerase_decreasing` and `gbmap::gc_files` (`GC_PREERASE_COUNT` in
  {0, 4, `BLOCK_COUNT/2`, `BLOCK_COUNT-4`, -1}) pass with `ERASE_VALUE` in
  {0xff, 0x00, -1} in B-BIG.
- **Fail:** any case fails.
- **Verified by:** as listed.
- **Status:** Partly tested (compiled out of B-DEF).
- **When:** every CI run.

#### LFS3-PRE-02

littlefs shall not erase a block recorded as erased when it allocates it
and the block's erased-state checksum still matches.

- **Source:** Stated: #1111 "Pre-erased block tracking" ("reduce the latency
  [of] file writes in the critical path"); `lfs3.c:11227-11246`.
- **Measure:** emubd erase count during writes after gc pre-erased.
- **Pass:** `gc::preerase_*` erase-count checks pass in B-BIG, and a NEW case
  finds zero erases of pre-erased blocks during 100 subsequent block
  allocations.
- **Fail:** a pre-erased block is erased again at allocation.
- **Verified by:** as listed; NEW.
- **Status:** Partly tested (compiled out of B-DEF).
- **When:** every CI run.

#### LFS3-PRE-03

littlefs shall neither program nor erase, in that allocation, a block
recorded as erased whose erased-state checksum no longer matches.

- **Source:** Stated: commit `35a1ac9` (a mismatch means "a prog was
  attempted, then power was lost"); `lfs3.c:11227-11246`.
- **Measure:** emubd prog and erase counts of the block.
- **Pass:** a NEW case flips a bit in a pre-erased block's first prog unit,
  allocates, and finds the block skipped, in B-BIG.
- **Fail:** the block is programmed or erased in that allocation.
- **Verified by:** NEW.
- **Status:** Untested.
- **When:** every CI run.

#### LFS3-PRE-04

littlefs shall treat blocks recorded as erased as in use when mounted
without `LFS3_M_REVPERTURB`.

- **Source:** Stated: commit `e3bca2b` (cross-mode mounts are safe but waste
  the pre-erasure); `lfs3.c:11069-11083`.
- **Measure:** emubd prog counts of pre-erased blocks.
- **Pass:** a NEW case pre-erases with `LFS3_M_REVPERTURB`, remounts without
  it, fills the disk, and finds no pre-erased block programmed without an
  erase, in B-BIG.
- **Fail:** a pre-erased block is programmed without an erase.
- **Verified by:** NEW. `mount::t_preerase` always remounts with
  `LFS3_M_REVPERTURB`.
- **Status:** Untested.
- **When:** every CI run.

#### LFS3-PRE-05

littlefs shall remain consistent, and program no block without erasing it
unless its erased-state checksum matched, when power is lost during
pre-erase or during the gbmap commit that records it.

- **Source:** Derived: the safety argument in 3-alloc §3 ("Power loss during
  pre-erase"), which no test checks.
- **Measure:** mount result, `lfs3_fs_ck`, and emubd programs of blocks that
  were not erased since their last program.
- **Pass:** NEW reentrant cases that call `lfs3_fs_gc` with
  `LFS3_GC_PREERASE` between writes pass under `-Plinear` with PLB-TORN in
  B-BIG, with an emubd check that fails any prog to a region already
  programmed since its last erase.
- **Fail:** any permutation fails, or the check fires.
- **Verified by:** NEW.
- **Status:** Untested.
- **When:** nightly.

#### LFS3-PRE-06

littlefs shall skip a block whose pre-erase fails and continue, so that
`lfs3_fs_gc`, `lfs3_fs_ck`, and `lfs3_mount` and `lfs3_format` with the
pre-erase flag return 0.

- **Source:** Derived: allocation skips a block whose erase fails
  (`lfs3.c:11251-11257`); pre-erase should do the same.
- **Measure:** return values with an ERASEERROR block in the known window.
- **Pass:** a NEW case (a free block marked ERASEERROR) gets 0 from each call,
  and later gc calls make progress past the block, in B-BIG.
- **Fail:** any call returns the error, or gc stops at the block.
- **Verified by:** NEW.
- **Status:** Known defect (3-alloc B1: `lfs3_alloc_preerase` returns the
  erase error without advancing, `lfs3.c:11416-11419`, so every later gc
  call fails at the same block).
- **When:** every CI run.

#### LFS3-PRE-07

littlefs shall persist the removal of a pre-erased block from the known
window before programming data into it.

- **Source:** Stated: commit `476822a`; `lfs3_allocclaim`,
  `lfs3.c:11297-11328`. Data blocks cannot perturb their first bytes the way
  metadata blocks do.
- **Measure:** gbmap window on disk at the time of the first data prog.
- **Pass:** a NEW reentrant case of large writes after gc pre-erase passes
  under `-Plinear` with PLB-TORN, with an internal check that the on-disk
  window excludes the block at its first prog, in B-BIG.
- **Fail:** the check fires or any permutation fails.
- **Verified by:** NEW.
- **Status:** Untested.
- **When:** nightly.

#### LFS3-PRE-08

littlefs shall, with `LFS3_M_CKPROGS`, recover when a pre-erased block's
erase silently did nothing.

- **Source:** Derived: with ERASENOOP the recorded erased-state checksum
  covers old content and matches later (3-alloc §5a row 4).
- **Measure:** operation results and content.
- **Pass:** a NEW variant of `badblocks::region_spam_file_fuzz` with gc
  pre-erase and `BADBLOCK_BEHAVIOR=4` passes in B-BIG.
- **Fail:** wrong content or an error while good blocks remain.
- **Verified by:** NEW.
- **Status:** Untested.
- **When:** every CI run.

#### LFS3-PRE-09

littlefs shall not program a pre-erased block, or append to a metadata
log, without erasing it first, after a torn program that left the first
`prog_size` bytes of that program erased but changed bytes after them.

- **Source:** Derived: at `b10efaa` the erased-state checksum covers only
  the first `prog_size` bytes (`lfs3.c:2491-2502`), while the first program
  into erased space can be a `pcache_size` flush (3-alloc B17). Q19 is
  settled by widening the checksum to the whole first program, at least
  `max(pcache_size, LFS3_TAG_DSIZE)` bytes (`57493587`).
- **Measure:** programs to already-programmed regions, with emubd's
  TORNTAIL power-loss behaviour (`POWERLOSS_BEHAVIOR` 5), which leaves the
  first `prog_size` bytes of the interrupted program erased and programs
  part of the rest.
- **Pass:** `powerloss_p1::tear_tail` (`PCACHE_SIZE` 4 × `PROG_SIZE`),
  `powerloss::append_pl` (`PCACHE_SIZE` 4 × `PROG_SIZE`) and
  `powerloss::preerase_pl_fuzz` (gc pre-erase between writes, `PCACHE_SIZE`
  16 > `PROG_SIZE` 1), all with TORNTAIL and `CKPROGONCE`, pass under
  `-Plinear` in B-DEF and B-BIG; the pre-erase workload needs B-BIG.
- **Fail:** the prog-once check fires, or any permutation fails.
- **Verified by:** `powerloss_p1::tear_tail`, `powerloss::append_pl`,
  `powerloss::preerase_pl_fuzz`; every `powerloss::*` case also runs with
  torn tails.
- **Status:** Untested at `b10efaa` (3-alloc B17). Tested on
  `v3-integration` (`57493587`, `c9511dfe`); before `57493587`
  `test_powerloss` failed 2990 of 2998 permutations at `PROG_SIZE` 1 under
  TORNTAIL. What the wider checksums cost is in Appendix B.4.
- **When:** every CI run.

### 6.14 Garbage collection and traversals (GC)

`lfs3_fs_gc` exists only with `LFS3_GC` (#1111 "Incremental GC": "GC is now
an opt-in feature"). The same work is available without it through the
traversal API, `lfs3_fs_ck` and the mount and format flags.

#### LFS3-GC-01

littlefs shall perform at most `gc_steps` steps of pending janitorial work
in each call to `lfs3_fs_gc` (one step when `gc_steps` is 0), each step being
about one block.

- **Source:** Stated: `lfs3.h:565-577`, `1789-1798`.
- **Measure:** blocks read and written per call.
- **Pass:** `gc::*_progress` and `gc::*_relaxed` pass in B-BIG, and a NEW
  check bounds the blocks touched per call to `gc_steps` + 2 for
  `GC_STEPS` in {0, 1, 4}.
- **Fail:** a call does more work than the bound.
- **Verified by:** as listed; NEW bound.
- **Status:** Partly tested (compiled out of B-DEF; no per-call bound is
  checked).
- **When:** every CI run.

#### LFS3-GC-02

littlefs shall return from `lfs3_fs_gc` with `gc_steps` -1, and from
`lfs3_fs_ck`, after a bounded amount of work on any filesystem, including a
full one and one with metadata that compaction cannot shrink, and shall
leave clear every `LFS3_I_*` work flag the call was asked to work on.

- **Source:** Stated: `lfs3.h:571-572` ("steps=-1 will not return until all
  pending janitorial work has been completed"). Derived: an unattended
  system cannot recover from a call that never returns, nor from a flag
  that asks for work no call can finish (issue #5). This replaces the note
  in `lfs3.h` that steps=-1 may never return on a nearly full disk, or when
  metadata can't be compacted below `gc_compact_thresh`.
- **Measure:** bytes read, programmed and erased by each call; the
  `LFS3_I_*` flags afterwards.
- **Pass:** `gc::steps_unbounded` passes for every permutation of its work
  flags, gbmap, pre-erase, `gc_compact_thresh` and file size, in B-BIG: each
  `lfs3_fs_gc` call, from an empty disk to the first `LFS3_ERR_NOSPC` and for
  100 operations after it, does at most 10 × the disk size of I/O and leaves
  no flag of `gc_flags` set. `gc::compact_unshrinkable` passes in B-DEF,
  B-YGB and B-BIG: with an mdir whose compacted size is above
  `gc_compact_thresh`, `lfs3_fs_ck` with `LFS3_CK_COMPACT` (and
  `lfs3_fs_gc` with `GC_STEPS=-1` where built) returns within the same
  bound, clears `LFS3_I_COMPACT`, and compacts the mdir at most once.
- **Fail:** a call exceeds the bound, or returns with a requested work flag
  still set.
- **Verified by:** `gc::steps_unbounded`, `gc::compact_unshrinkable`.
- **Status:** Known defect at `b10efaa` (#5): with the gbmap, pre-erase and
  lookahead, 16 of 384 permutations of `gc::steps_unbounded` never return,
  because each gbmap repopulation and the sync after it allocate from the
  few free blocks and ask for another repopulation; and an mdir that
  compaction cannot shrink is compacted, and relocated, on every pass.
  Tested on `v3-integration` (b55ca37f, 6a1535f6).
- **When:** every CI run.

#### LFS3-GC-03

littlefs shall perform each kind of work named in `gc_flags` (mkconsistent,
lookahead, pre-erase, compact, ckmeta, ckdata), and clear the matching
`LFS3_I_*` flag, when that work is complete.

- **Source:** Stated: `lfs3.h:329-343` (`LFS3_I_*` flags), `560-563`.
- **Measure:** `lfs3_fs_stat` flags after gc.
- **Pass:** `gc::iflags`, `gc::iflags_unck`, `gc::lookahead_*`,
  `gc::compact_*`, `gc::mkconsistent_*`, `gc::ckmeta` and `gc::ckdata` pass
  in B-BIG.
- **Fail:** a flag stays set after its work, or clears early.
- **Verified by:** as listed.
- **Status:** Partly tested (compiled out of B-DEF).
- **When:** every CI run.

#### LFS3-GC-04

littlefs shall complete janitorial work when other calls modify the
filesystem between gc calls.

- **Source:** Derived: a checkpoint marks open traversals stale and gc
  restarts them (`lfs3.c:9804-9813`, `16660-16785`).
- **Measure:** `LFS3_I_*` flags eventually clear.
- **Pass:** `gc::*_mutation` and `gc::mutation*` pass in B-BIG.
- **Fail:** work never completes under steady mutation.
- **Verified by:** as listed.
- **Status:** Partly tested (compiled out of B-DEF).
- **When:** every CI run.

#### LFS3-GC-05

littlefs shall run the requested work to completion in `lfs3_fs_ck`, and
return `LFS3_ERR_CORRUPT` at the first checksum mismatch.

- **Source:** Stated: `lfs3.h:1780-1787`.
- **Measure:** return value and `LFS3_I_*` flags.
- **Pass:** `ck::ckmeta_*` and `ck::ckdata_*` with `METHOD=0` and
  `gc::ckmeta_explicit`, `gc::ckdata_explicit` pass in B-DEF (the `gc::`
  cases in B-BIG).
- **Fail:** incomplete work or a missed mismatch.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-GC-06

littlefs shall leave no traversal registered after `lfs3_fs_ck` returns, on
every exit path.

- **Source:** Derived: `lfs3_fs_ck` uses a traversal on its stack
  (`lfs3.c:16824-16825`); a traversal left on the handle list dangles once
  the call returns (3-alloc R3).
- **Measure:** the handle list after each call.
- **Pass:** a NEW internal check after every `lfs3_fs_ck` in `ck::*` and
  `gc::*` finds no handle beyond those the test opened, in B-BIG.
- **Fail:** a stack traversal remains on the list.
- **Verified by:** NEW.
- **Status:** Untested (suspected, 3-alloc R3; reachability unclear).
- **When:** every CI run.

#### LFS3-GC-07

littlefs shall return from `lfs3_trv_read` every block in use (both blocks
of each mdir, each B-tree node, each data block), then `LFS3_ERR_NOENT`.

- **Source:** Stated: `lfs3.h:1705-1733`; #1111 "Better traversal APIs".
- **Measure:** the set of blocks returned against the expected set.
- **Pass:** `trvs::simple`, `trvs::idempotent` and `trvs::spam_*` pass in
  B-DEF.
- **Fail:** a block in use is missing, or no `LFS3_ERR_NOENT` at the end.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-GC-08

littlefs shall return `LFS3_ERR_BUSY` from `lfs3_trv_read` on a traversal
opened with `LFS3_T_EXCL` once the filesystem has been modified since the
traversal was opened or rewound.

- **Source:** Stated: `lfs3.h:360` ("Error if filesystem modified").
- **Measure:** return value.
- **Pass:** `trvs::mutation_*` pass in B-DEF.
- **Fail:** the traversal continues without the error.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-GC-09

littlefs shall include in a traversal the blocks of open files that are not
yet synced.

- **Source:** Stated: `lfs3.c:9989-10023`; the allocator depends on it.
- **Measure:** blocks returned while a file handle holds unsynced data.
- **Pass:** `trvs::clobber_files_opened` and `trvs::mutation_*` pass in
  B-DEF.
- **Fail:** an unsynced block is missing.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-GC-10

littlefs shall perform, during a read-write traversal, the work named by
its `LFS3_T_MKCONSISTENT`, `LFS3_T_LOOKAHEAD` and `LFS3_T_COMPACT` flags.

- **Source:** Stated: #1111 "Better traversal APIs" ("Traversals can also
  perform janitorial work").
- **Measure:** `LFS3_I_*` flags and on-disk state after the traversal.
- **Pass:** `trvs::compact_*`, `trvs::mkconsistent_*` and `trvs::flags` pass
  in B-DEF.
- **Fail:** the work is not done.
- **Verified by:** as listed. `LFS3_T_PREERASE` does nothing (LFS3-DOC-17).
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-GC-11

littlefs shall return only metadata blocks from a traversal opened with
`LFS3_T_MTREEONLY`.

- **Source:** Stated: `lfs3.h:358-359` ("Only traverse the mtree").
- **Measure:** block types returned.
- **Pass:** a NEW case finds only `LFS3_BTYPE_MDIR` and mtree
  `LFS3_BTYPE_BTREE` blocks, in B-DEF; `ck::spam_*` with `METHOD=2` pass in
  B-BIG.
- **Fail:** a data block or file B-tree node is returned.
- **Verified by:** as listed; NEW.
- **Status:** Partly tested.
- **When:** every CI run.

#### LFS3-GC-12

littlefs shall, in `lfs3_fs_mkconsistent`, complete pending removes and
remove orphans, so that `LFS3_I_MKCONSISTENT` is clear afterwards.

- **Source:** Stated: `lfs3.h:1768-1777`.
- **Measure:** `LFS3_I_MKCONSISTENT`; on-disk orphans.
- **Pass:** `gc::mkconsistent_*` (B-BIG), `powerloss::*` with
  `MKCONSISTENT=true` and `stickynotes::cleanup_*` pass in B-DEF.
- **Fail:** the flag stays set, or an orphan remains.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-GC-13

littlefs shall perform during `lfs3_mount` and `lfs3_format` the work named
by the `LFS3_M_*` and `LFS3_F_*` work flags, and fail the call if the work
fails.

- **Source:** Stated: `lfs3.h:207-238, 270-297`.
- **Measure:** return values and `LFS3_I_*` flags afterwards.
- **Pass:** `mount::t_lookahead`, `mount::t_compact`,
  `mount::t_mkconsistent`, `mount::t_ckmeta`, `mount::t_ckdata` and
  `mount::format_flags` pass in B-DEF; `mount::t_lookgbmap` and
  `mount::t_preerase` pass in B-BIG.
- **Fail:** the work is not done, or a failure is not reported.
- **Verified by:** as listed.
- **Status:** Partly tested (`t_lookgbmap` and `t_preerase` compiled out of
  B-DEF; `format_flags` runs 32 of 8192 permutations there). In B-BIG, mounts
  with `LFS3_M_PREERASE` and `LFS3_M_CKMETAPARITY` but without
  `LFS3_M_CKFETCHES` fail because of D-2
  (LFS3-INT-23).
- **When:** every CI run.

#### LFS3-GC-14

littlefs shall compact, during janitorial work, every mdir whose log
exceeds `gc_compact_thresh` bytes (`block_size - block_size/8` when 0), and
none when it is -1.

- **Source:** Stated: `lfs3.h:622-634`.
- **Measure:** mdir log sizes after the work.
- **Pass:** `gc::compact_*` (B-BIG) and `trvs::compact_*` (B-DEF) pass, and a
  NEW case with `GC_COMPACT_THRESH=-1` finds no compaction.
- **Fail:** an mdir over the threshold remains, or compaction happens with
  -1.
- **Verified by:** as listed; NEW.
- **Status:** Partly tested (-1 is never tested).
- **When:** every CI run.

#### LFS3-GC-15

littlefs shall restart a traversal from the beginning on
`lfs3_trv_rewind`, clearing its modified state so that `LFS3_T_EXCL` applies
from the rewind.

- **Source:** Stated: `lfs3.h:1730-1733`; `lfs3.c:17105-17245`.
- **Measure:** blocks returned after rewind; `LFS3_ERR_BUSY` behaviour.
- **Pass:** `trvs::rewind` and `trvs::rewind_clobber_*` pass in B-DEF.
- **Fail:** the traversal does not restart, or stays busy.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-GC-16

littlefs shall release a traversal handle in `lfs3_trv_close`.

- **Source:** Stated: `lfs3.h:1715-1719`.
- **Measure:** `lfs3_unmount` succeeds after the close.
- **Pass:** `trvs::*` pass in B-DEF.
- **Fail:** unmount asserts on an open handle.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-GC-17

littlefs shall check every metadata and data block `ck_passes` times in
`lfs3_fs_ck`, and in a mount or format with `LFS3_M_CKMETA`,
`LFS3_M_CKDATA`, `LFS3_F_CKMETA` or `LFS3_F_CKDATA`, reading the device
each time; and `lfs3.h` shall recommend a value.

- **Source:** Proposal (issue #19). A bit that reads differently each time,
  such as one a power loss left metastable, is more likely to be caught by
  more passes, though no number of passes proves a cell won't drift later.
- **Measure:** device reads of each block during the call.
- **Pass:** `repair::passes`: with `ck_passes` 1, 2 and 3,
  `lfs3_fs_ck(LFS3_CK_CKDATA)` and a mount with `LFS3_M_CKDATA` read every
  data block at least that many times, and a block that fails only on its
  second read is found with 2 and 3 passes, in B-YGB and B-BIG.
- **Fail:** fewer reads, or the second-read failure missed with 2 passes.
- **Verified by:** `repair::passes`.
- **Status:** Not implemented at `b10efaa`; tested on `v3-integration`
  (f09acb9d).
- **When:** every CI run.

#### LFS3-GC-18

littlefs shall, when a B-tree node or data block fails its check during a
check, gc or traversal, read it again up to `ck_retries` times before
returning `LFS3_ERR_CORRUPT`, and continue as though the check passed if a
read passes; and `lfs3.h` shall recommend a value.

- **Source:** Proposal (issue #19). Principle 4: reads can fail while the
  supply is low, so a failed check does not prove the data lost, and how
  hard to try belongs to the application.
- **Measure:** results of `lfs3_fs_ck` over blocks whose reads fail some of
  the time and all of the time.
- **Pass:** `repair::data_lost`: over a data block whose next read
  returns a flipped bit, `lfs3_fs_ck(LFS3_CK_CKDATA)` returns
  `LFS3_ERR_CORRUPT` with `ck_retries` 0 and 0 with 3; over one whose every
  read does, it returns `LFS3_ERR_CORRUPT` with either; on read-only and
  writable mounts, in B-YGB and B-BIG. `repair::data` and `repair::btree`
  pass with 1 and 3 failed reads and `ck_retries` 3.
- **Fail:** another result.
- **Verified by:** `repair::data_lost`, `repair::data`, `repair::btree`.
- **Status:** Not implemented at `b10efaa`; tested on `v3-integration`
  (f09acb9d).
- **When:** every CI run.

### 6.15 Format, mount, grow, compatibility and versioning (MOUNT)

#### LFS3-MOUNT-01

littlefs shall format, and then mount, a device of any geometry that
`lfs3_init` accepts.

- **Source:** Stated: `lfs3.h:505-521, 1398-1419`.
- **Measure:** format and mount results.
- **Pass:** `mount::simple` and the setup of every suite pass for each
  geometry of G-ALL in B-DEF and B-YGB.
- **Fail:** any failure.
- **Verified by:** as listed.
- **Status:** Partly tested (one geometry by default).
- **When:** nightly.

#### LFS3-MOUNT-02

littlefs shall write the same configuration to both anchor blocks at
format, and leave the filesystem unmounted when `lfs3_format` returns.

- **Source:** Stated: `lfs3.h:1398-1403` ("does not leave the filesystem
  mounted"); `lfs3.c:16181-16182`.
- **Measure:** both anchor blocks after format; state of `lfs3_t`.
- **Pass:** `mtree::magic` and `mount::simple` pass in B-DEF.
- **Fail:** the anchors differ in configuration, or the filesystem is
  left mounted.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-MOUNT-03

littlefs shall refuse, before any block device operation, to format a
device with fewer than 2 blocks, or fewer than 3 with `LFS3_F_GBMAP`.

- **Source:** Derived: format writes blocks 0 and 1, and 2 for the gbmap
  (`lfs3.c:16129-16284`). Whether the refusal is an assert or
  `LFS3_ERR_INVAL` is open question Q14.
- **Measure:** return value or assert; emubd operation counts.
- **Pass:** a NEW case with `block_count` 1 (and 2 with `LFS3_F_GBMAP`) finds
  the refusal with no erase or prog issued, in B-DEF and B-YGB.
- **Fail:** any erase or prog before the refusal.
- **Verified by:** NEW.
- **Status:** Known defect (4-api R28: with `block_count` 1, format erases
  and writes block 0, then asserts in the bd wrapper, `lfs3.c:570`).
- **When:** every CI run.

#### LFS3-MOUNT-04

littlefs shall fail `lfs3_mount` with `LFS3_ERR_CORRUPT` when any mroot in
the chain lacks the "littlefs" magic.

- **Source:** Stated: `lfs3.c:15807-15827`.
- **Measure:** mount result.
- **Pass:** `mount::incompat_no_magic` and `mount::incompat_bad_magic` pass
  in B-DEF, for `LFS3_M_RDWR` and `LFS3_M_RDONLY`.
- **Fail:** the mount succeeds.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-MOUNT-05

littlefs shall fail `lfs3_mount` with `LFS3_ERR_NOTSUP` when the on-disk
major version differs from the driver's, or the on-disk minor version is
greater.

- **Source:** Stated: `lfs3.h:23-28`.
- **Measure:** mount result.
- **Pass:** `mount::incompat_major` and `mount::incompat_minor` pass in
  B-DEF.
- **Fail:** the mount succeeds.
- **Verified by:** as listed. A missing version tag is open question Q5.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-MOUNT-06

littlefs shall fail `lfs3_mount`, read-write or read-only, with
`LFS3_ERR_NOTSUP` when the image sets an rcompat flag the driver does not
know.

- **Source:** Stated: `lfs3.h:920-926` ("RCOMPAT => Must understand to
  read"); #1111 "A new and improved compat flag system".
- **Measure:** mount result.
- **Pass:** `mount::incompat_rcompat`, `mount::incompat_wronly`,
  `mount::incompat_rcompat_overflow` and `mount::incompat_rcompat_padding`
  pass in B-DEF.
- **Fail:** the mount succeeds.
- **Verified by:** as listed. Images that set fewer flags than the driver
  writes are open question Q4.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-MOUNT-07

littlefs shall fail a read-write `lfs3_mount` with `LFS3_ERR_NOTSUP`, and
allow a read-only mount, when the image sets a wcompat flag the driver does
not know.

- **Source:** Stated: `lfs3.h:920-926` ("WCOMPAT => Must understand to
  write").
- **Measure:** mount results.
- **Pass:** `mount::incompat_wcompat`, `mount::incompat_rdonly`,
  `mount::incompat_wcompat_overflow` and `mount::incompat_wcompat_padding`
  pass in B-DEF.
- **Fail:** a read-write mount succeeds, or a read-only mount fails.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-MOUNT-08

littlefs shall ignore ocompat flags when mounting.

- **Source:** Stated: `lfs3.h:924` ("OCOMPAT => No understanding
  necessary").
- **Measure:** mount result.
- **Pass:** `mount::incompat_ocompat` passes in B-DEF.
- **Fail:** the mount fails.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-MOUNT-09

littlefs shall fail `lfs3_mount` with `LFS3_ERR_NOTSUP` when the final mroot
contains any configuration tag (0x0100 to 0x01ff) that the driver does not
know.

- **Source:** Derived: configuration must be understood to be used, like
  rcompat; the driver looks for unknown tags only from 0x013b upwards
  (`lfs3.c:15750-15764`).
- **Measure:** mount result.
- **Pass:** a NEW internal case commits each of 0x0100, 0x0130, 0x0132,
  0x0133, 0x013b and 0x0142 to the mroot and gets `LFS3_ERR_NOTSUP` for each,
  in B-DEF.
- **Fail:** any of them mounts.
- **Verified by:** NEW. `mount::incompat_unknown_config` uses 0x0142 only.
- **Status:** Known defect (1-meta 0.4: 0x0100, 0x0130, 0x0132 and 0x0133
  mount).
- **When:** before v3-beta.

#### LFS3-MOUNT-10

littlefs shall fail `lfs3_mount` with `LFS3_ERR_NOTSUP` when the on-disk
block size differs from the configured one or the on-disk block count is
larger, and shall use a smaller on-disk block count.

- **Source:** Stated: `lfs3.c:15664-15695`.
- **Measure:** mount results; `lfs3_fs_stat` block count.
- **Pass:** `mount::incompat_block_size`, `mount::incompat_block_count`,
  `grow::mount_smaller` and `grow::mount_bigger` pass in B-DEF.
- **Fail:** any other result.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-MOUNT-11

littlefs shall fail `lfs3_mount` when the final mroot has no geometry.

- **Source:** Derived: without the geometry the image cannot be
  interpreted. At `b10efaa` the result is `LFS3_ERR_INVAL`
  (`lfs3.c:15664-15670`); open question Q22 asks which code it should be.
- **Measure:** mount result.
- **Pass:** a NEW internal case that removes the geometry tag gets a
  negative result, in B-DEF.
- **Fail:** the mount succeeds.
- **Verified by:** `mount::no_geometry`.
- **Status:** Untested at `b10efaa`; tested on `v3-integration` (e9aeb8b2),
  which returns `LFS3_ERR_CORRUPT` (Q22).
- **When:** every CI run.

#### LFS3-MOUNT-12

littlefs shall fail `lfs3_mount` with `LFS3_ERR_NOTSUP` when the on-disk
name or file limit is larger than the configured one, and shall otherwise
use the on-disk limits and report them in `lfs3_fs_stat`.

- **Source:** Stated: `lfs3.h:62-74, 652-665` ("Stored in superblock and
  must be respected by other littlefs drivers").
- **Measure:** mount result; `fsinfo.name_limit` and `fsinfo.file_limit`.
- **Pass:** `mount::incompat_name_limit` and `mount::incompat_file_limit`
  pass in B-DEF, and a NEW case formats with `name_limit=32` and
  `file_limit=1000`, mounts with the defaults, and reads 32 and 1000 from
  `lfs3_fs_stat`.
- **Fail:** any other result.
- **Verified by:** as listed; NEW.
- **Status:** Partly tested (the reported values are not checked; the test
  configuration never sets the limits at format).
- **When:** every CI run.

#### LFS3-MOUNT-13

littlefs shall use a name limit of 255 and a file limit of 2^31 - 1 when the
image has no name-limit or file-limit tag.

- **Source:** Stated: `lfs3.c:15698-15748` (defaults when the tags are
  absent). SPEC.md must record them.
- **Measure:** `lfs3_fs_stat` limits.
- **Pass:** a NEW internal case removes both tags and reads 255 and
  2147483647, in B-DEF.
- **Fail:** any other value or a mount failure.
- **Verified by:** NEW.
- **Status:** Untested.
- **When:** before v3-beta.

#### LFS3-MOUNT-14

littlefs shall mount a valid image in an `LFS3_RDONLY` build with a
zero-initialised `lfs3_t`.

- **Source:** Derived: `lfs3_t` is commonly static, and `lfs3.h:1410-1419`
  sets no other precondition.
- **Measure:** mount result.
- **Pass:** a NEW B-RO test mounts images from `mount::simple` with a
  zeroed and with a 0xff-filled `lfs3_t`, and both succeed.
- **Fail:** either mount fails.
- **Verified by:** NEW.
- **Status:** Known defect (4-api R5: the name and file limits are compared
  before they are set; a zeroed `lfs3_t` gets `LFS3_ERR_NOTSUP`).
- **When:** every CI run.

#### LFS3-MOUNT-15

littlefs shall check the checksum of every mdir and mtree node during
`lfs3_mount` and fail the mount on any mismatch.

- **Source:** Stated: `lfs3.c:15767-15979`.
- **Measure:** mount result on images with a corrupted mdir.
- **Pass:** `mount::t_ckmeta` and `ck::ckmeta_*` with `METHOD=3` pass in
  B-DEF.
- **Fail:** the mount succeeds.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-MOUNT-16

littlefs shall not program or erase any block, and shall release every
buffer it allocated, when `lfs3_mount` fails.

- **Source:** Derived: a failed mount must not make things worse.
- **Measure:** emubd prog and erase counts; valgrind.
- **Pass:** a NEW case runs every `mount::incompat_*` failure and a gcksum
  mismatch with counters and valgrind, and finds no prog, no erase and no
  leak, in B-DEF.
- **Fail:** any prog, erase or leak.
- **Verified by:** NEW.
- **Status:** Untested.
- **When:** every CI run.

#### LFS3-MOUNT-17

littlefs shall not program, erase or sync the block device while mounted
with `LFS3_M_RDONLY`, whatever calls are made.

- **Source:** Stated: `lfs3.h:245` ("Mount the filesystem as read only").
- **Measure:** emubd prog, erase and sync counters over the whole mount.
- **Pass:** a NEW case opens a file `LFS3_O_RDONLY` with an `LFS3_A_RDWR`
  attribute, desyncs it, calls `lfs3_file_sync`, runs traversals and
  `lfs3_fs_ck`, and finds all counters unchanged, in B-DEF.
- **Fail:** any counter increases.
- **Verified by:** NEW.
- **Status:** Known defect (2-files B3: `lfs3_file_sync` has no read-only
  guard, `lfs3.c:14553-14608`; a probe issued one prog on an
  `LFS3_M_RDONLY` mount). Syncing read-only handles on a read-write mount is
  open question Q1.
- **When:** every CI run.

#### LFS3-MOUNT-18

littlefs shall not access the block device in `lfs3_unmount`.

- **Source:** Stated: `lfs3.h:1421-1425` ("Does nothing besides releasing
  any allocated resources").
- **Measure:** emubd counters.
- **Pass:** every suite passes in B-DEF, and a NEW counter check around
  `lfs3_unmount` finds no operation.
- **Fail:** any bd operation.
- **Verified by:** all suites; NEW check.
- **Status:** Partly tested (every suite unmounts; no case counts bd
  operations during the unmount).
- **When:** every CI run.

#### LFS3-MOUNT-19

littlefs shall set `LFS3_I_RDONLY` in `lfs3_fs_stat` whenever the
filesystem is mounted read-only, including in `LFS3_RDONLY` builds.

- **Source:** Stated: `lfs3.h:300` ("Mounted read only").
- **Measure:** `fsinfo.flags`.
- **Pass:** a NEW case finds the flag set after `lfs3_mount` with
  `LFS3_M_RDONLY` in B-DEF, and after any mount in B-RO.
- **Fail:** the flag is clear.
- **Verified by:** NEW.
- **Status:** Known defect (4-api R25: an `LFS3_RDONLY` build mounted with
  flags 0 reports 0x3000). Reporting write-side work flags on a read-only
  mount is open question Q16.
- **When:** every CI run.

#### LFS3-MOUNT-20

littlefs shall increase the block count with `lfs3_fs_grow`, persist it,
and treat a call with the current count as a no-op.

- **Source:** Stated: `lfs3.h:1813-1823`.
- **Measure:** `lfs3_fs_stat` block count before and after remount.
- **Pass:** `grow::grow`, `grow::noop` and `grow::incr_spam_*` pass in B-DEF
  (and with `GBMAP=true` in B-YGB).
- **Fail:** any other block count.
- **Verified by:** as listed.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-MOUNT-21

littlefs shall return `LFS3_ERR_INVAL`, and change nothing, from
`lfs3_fs_grow` with a block count larger than `cfg->block_count`.

- **Source:** Derived: growing beyond the device makes littlefs address
  blocks that do not exist, and makes the image unmountable with the same
  configuration (`lfs3.c:15688-15693`).
- **Measure:** return value; `lfs3_fs_stat`; emubd bounds.
- **Pass:** a NEW case calls `lfs3_fs_grow(128)` on a 64-block
  configuration and gets `LFS3_ERR_INVAL`, with the block count still 64, in
  B-DEF.
- **Fail:** any other result.
- **Verified by:** NEW.
- **Status:** Known defect (4-api R4: the call returns 0, later writes
  erase block 64, and the next mount fails with `LFS3_ERR_NOTSUP`).
- **When:** every CI run.

#### LFS3-MOUNT-22

littlefs shall keep the allocator within the old block count after
`lfs3_fs_grow` fails.

- **Source:** Derived: the failure path restores the block count but not
  the allocation window (`lfs3.c:16985-16997`, `10886-10897`; 3-alloc B6).
- **Measure:** blocks allocated after the failed grow.
- **Pass:** a NEW case makes grow fail after the window has moved into the
  new range, then fills the disk, and finds no block at or beyond the old
  count, in B-DEF and B-YGB.
- **Fail:** such a block is allocated, or an assert fires.
- **Verified by:** NEW.
- **Status:** Untested (suspected, 3-alloc B6).
- **When:** every CI run.

#### LFS3-MOUNT-23

littlefs shall succeed in `lfs3_fs_grow` on a filesystem that returns
`LFS3_ERR_NOSPC` for every other write.

- **Source:** Stated: `lfs3.c:16909-16917` (grow skips mkconsistent "so that
  grow can always rescue a stuck filesystem").
- **Measure:** grow result on a full filesystem.
- **Pass:** `grow::incr_spam_*` (which grow by one block after each
  `LFS3_ERR_NOSPC`) pass in B-DEF.
- **Fail:** grow fails with `LFS3_ERR_NOSPC`.
- **Verified by:** as listed.
- **Status:** Tested. (`grow::incr_spam_uzd_fuzz` had a use-after-free in
  the test, F-4.)
- **When:** every CI run.

#### LFS3-MOUNT-24

littlefs shall, from the first release with a frozen on-disk format, refuse
to mount images of disk version 0.0.

- **Source:** Stated: #1111 "Hello!" ("When it is eventually released, v3
  will reject this version and fail to mount").
- **Measure:** mount result on a v0.0 image.
- **Pass:** `mount::incompat_*` with a v0.0 image gets `LFS3_ERR_NOTSUP` in
  the release build.
- **Fail:** the image mounts.
- **Verified by:** NEW (with the version change).
- **Status:** Not implemented (planned).
- **When:** before v3-beta.

#### LFS3-MOUNT-25

littlefs shall fail `lfs3_mount` on a littlefs v2 image without writing to
it.

- **Source:** Derived: #1111 "Wait, a disk breaking change?"; a v2 image
  must not be damaged by a v3 driver.
- **Measure:** mount result; emubd prog and erase counters.
- **Pass:** a NEW case mounts a v2.11 image (checked in as a fixture) and
  gets `LFS3_ERR_CORRUPT` or `LFS3_ERR_NOTSUP` with no prog or erase, in
  B-DEF.
- **Fail:** the image mounts, or is written.
- **Verified by:** NEW.
- **Status:** Untested.
- **When:** every CI run.

#### LFS3-MOUNT-26

littlefs shall mount, read and write images written by every earlier
release that has the same on-disk major version.

- **Source:** Stated: #1111 ("Once it's stabilized, it's stabilized");
  `tests/test_compat.toml`.
- **Measure:** `compat::forward_*` and `compat::backward_*` with the
  previous release linked as `LFSP`.
- **Pass:** the cases pass against each earlier v3-beta and v3 release, in
  B-DEF.
- **Fail:** any case fails.
- **Verified by:** `compat::*`.
- **Status:** Untested (without `LFSP` the suite tests the build against
  itself).
- **When:** every CI run.

#### LFS3-MOUNT-27

littlefs shall record in the image only the gbmap choice among the format
flags; the check and revision flags given to `lfs3_format` shall affect only
the writes made during format.

- **Source:** Stated: `lfs3.c:16288-16396`; mount flags are per mount.
- **Measure:** the image's compat flags; `LFS3_I_*` flags after a plain
  mount.
- **Pass:** `mount::format_flags` passes in B-BIG, and a NEW check finds a
  plain mount after formatting with every flag reports only `LFS3_I_GBMAP`
  (when `LFS3_F_GBMAP` was given) from the format flags.
- **Fail:** another format flag persists.
- **Verified by:** as listed; NEW.
- **Status:** Partly tested.
- **When:** every CI run.

### 6.16 Configuration validation (CFG)

`lfs3.h:1400-1402` says the configuration "must be zeroed for defaults and
backwards compatibility". littlefs checks configuration with `LFS3_ASSERT` in
`lfs3_init` (`lfs3.c:15082-15393`). Whether some checks should return
`LFS3_ERR_INVAL` instead is open question Q14. The Pass conditions below
accept either, and need a death-test harness where they rely on an assert.

#### LFS3-CFG-01

littlefs shall format, mount and pass the file suites with a `struct
lfs3_cfg` in which only the block device callbacks, `read_size`,
`prog_size`, `block_size`, `block_count`, `rcache_size`, `pcache_size` and
`lookahead_size` are set and every other field is zero.

- **Source:** Stated: `lfs3.h:1400-1402` ("The config struct must be zeroed
  for defaults"). The TODOs at `lfs3.h:667-668, 686-691` note that some
  defaults are not settled.
- **Measure:** suite results with a zeroed configuration.
- **Pass:** `files::*`, `fwrite::*` and `kv::*` pass with `SHRUB_SIZE`,
  `FRAGMENT_SIZE`, `CRYSTAL_THRESH`, `FCACHE_SIZE`, `BLOCK_RECYCLES` and the
  gc thresholds all 0, in B-DEF.
- **Fail:** any failure or hang.
- **Verified by:** NEW define set.
- **Status:** Known defect (2-files B12: `fragment_size` 0 makes the
  fragment write path loop without progress, committing on every
  iteration).
- **When:** every CI run.

#### LFS3-CFG-02

littlefs shall refuse, in `lfs3_format` and `lfs3_mount` and before any
block device operation, a zero `read_size`, `prog_size`, `rcache_size` or
`pcache_size`, a cache size that is not a multiple of its operation size, a
`block_size` that is not a multiple of `read_size` and `prog_size`, and a
`block_size` above 0x0fffffff.

- **Source:** Stated: `lfs3.h:505-518, 535-545`; `lfs3.c:15104-15126`.
- **Measure:** assert or error, and emubd counters.
- **Pass:** a NEW death test finds each case refused with no bd operation,
  in B-DEF.
- **Fail:** any case accepted, or a bd operation first.
- **Verified by:** NEW.
- **Status:** Untested (implemented as asserts).
- **When:** every CI run.

#### LFS3-CFG-03

littlefs shall document a minimum `block_size`, and refuse smaller values
in `lfs3_format` and `lfs3_mount`.

- **Source:** Proposal. There is no lower bound; `mbits = nlog2(block_size)
  - 3` underflows below 8 bytes (`lfs3.c:15357`), and small blocks limit
  names (LFS3-DIR-02). See open question Q14.
- **Measure:** documentation; assert or error.
- **Pass:** `lfs3.h` states the minimum, and a NEW death test finds a
  smaller value refused before any bd operation.
- **Fail:** no documented minimum, or a smaller value accepted.
- **Verified by:** NEW.
- **Status:** Untested.
- **When:** before v3-beta.

#### LFS3-CFG-04

littlefs shall accept `block_recycles` of -1 and of 0 to 1,048,574, and
refuse other values.

- **Source:** Stated: `lfs3.h:523-530`; `lfs3.c:15258-15264` (20 recycle
  bits).
- **Measure:** assert or error.
- **Pass:** a NEW case mounts with -1, 0 and 1,048,574, and a death test
  finds -2 and 1,048,575 refused, in B-DEF.
- **Fail:** any other result.
- **Verified by:** NEW.
- **Status:** Untested.
- **When:** every CI run.

#### LFS3-CFG-05

littlefs shall check `gc_compact_thresh` (0, -1, or `block_size/2` to
`block_size`) in every build that uses it.

- **Source:** Stated: `lfs3.h:622-634`. The value is used by
  `LFS3_M_COMPACT`, `LFS3_T_COMPACT` and `lfs3_fs_ck` in all builds
  (`lfs3.c:10339-10350`), but checked only with `LFS3_GC`
  (`lfs3.c:15139-15147`).
- **Measure:** assert or error for 1 and for `block_size + 1`.
- **Pass:** a NEW death test finds both refused in B-DEF and B-BIG.
- **Fail:** either is accepted in B-DEF.
- **Verified by:** NEW.
- **Status:** Known defect (1-meta 7.14).
- **When:** every CI run.

#### LFS3-CFG-06

littlefs shall refuse unknown bits in `gc_flags` when the filesystem is
mounted.

- **Source:** Stated: `lfs3.c:15128-15137` (asserted in `lfs3_init` with
  `LFS3_GC`); `lfs3.c:16833` asks whether `lfs3_fs_gc` should still check.
- **Measure:** assert or error.
- **Pass:** a NEW death test with `gc_flags = 0x1` finds it refused at
  mount, in B-BIG.
- **Fail:** it is accepted.
- **Verified by:** NEW.
- **Status:** Untested (implemented as an assert).
- **When:** every CI run.

#### LFS3-CFG-07

littlefs shall treat `fcache_size` 0 as "no file cache" and open files
without calling `lfs3_malloc(0)`.

- **Source:** Proposal. `lfs3_get` already opens with a zero-size cache and
  a sentinel buffer (`lfs3.c:14997-15001`); `lfs3_file_open` with
  `fcache_size` 0 calls `lfs3_malloc(0)`, which returns NULL on some C
  libraries (4-api 4.1). See open question Q15.
- **Measure:** open result; reads and writes.
- **Pass:** `files::*` and `fwrite::*` pass with `FCACHE_SIZE=0`, and an
  allocator whose `malloc(0)` returns NULL, in B-DEF.
- **Fail:** `LFS3_ERR_NOMEM` or a failure.
- **Verified by:** NEW.
- **Status:** Untested.
- **When:** every CI run.

#### LFS3-CFG-08

littlefs shall use the per-file `fcache_buffer` and `fcache_size` of
`struct lfs3_file_cfg` instead of the filesystem-wide `fcache_size`.

- **Source:** Stated: `lfs3.h:547-550, 788-797`; #1111 "Independent file
  caches".
- **Measure:** file results and allocations.
- **Pass:** `kv::*` pass, and a NEW case opens files with per-file caches of
  1, 16 and 4096 bytes and runs the `fwrite::fuzz_unaligned` workload, in
  B-DEF.
- **Fail:** any failure, or an allocation when a buffer is supplied.
- **Verified by:** `kv::*` (the only users); NEW.
- **Status:** Partly tested.
- **When:** every CI run.

#### LFS3-CFG-09

littlefs shall refuse at compile time an `LFS3_NAME_MAX` greater than
1022.

- **Source:** Stated: `lfs3.h:62-64` ("Limited to <= 1022").
- **Measure:** compiler result.
- **Pass:** a build with `-DLFS3_NAME_MAX=1023` fails with a diagnostic.
- **Fail:** it compiles.
- **Verified by:** NEW: build check.
- **Status:** Untested (not enforced).
- **When:** every CI run.

#### LFS3-CFG-10

littlefs shall store files correctly for every `crystal_thresh` the header
describes: 0, 1, less than `prog_size`, `block_size`, more than
`block_size`, and -1.

- **Source:** Stated: `lfs3.h:686-702`; commit `0698c49` allows
  `crystal_thresh < prog_size`.
- **Measure:** `fwrite::*` results.
- **Pass:** `fwrite::*` pass with `CRYSTAL_THRESH` in {0, 1, 15, 512, 4096,
  4097, -1} and `PROG_SIZE` in {1, 16}, in B-DEF; a block crystallized
  with `crystal_thresh < prog_size` checksums exactly its `cksize` bytes,
  so `ck::ckdata_unaligned` (NEW-133) checks it clean.
- **Fail:** any permutation fails, or a check reports an undamaged block
  corrupt.
- **Verified by:** as listed, NEW-133.
- **Status:** Partly tested (512 and -1 only; NEW-133 checks
  `crystal_thresh` 0, 1 and 8 with `prog_size` 16 on `v3-rc`, `a12705da`).
- **When:** nightly.

#### LFS3-CFG-11

littlefs shall store files correctly for every `shrub_size` from 0 (no
shrubs) to `block_size/4`.

- **Source:** Stated: `lfs3.h:670-677`.
- **Measure:** suite results.
- **Pass:** `files::*`, `fwrite::*` and `fsync::*` pass with `SHRUB_SIZE` in
  {0, 64, `BLOCK_SIZE/8`, `BLOCK_SIZE/4`} in B-DEF.
- **Fail:** any permutation fails.
- **Verified by:** as listed.
- **Status:** Partly tested (0 and `BLOCK_SIZE/4` only).
- **When:** nightly.

#### LFS3-CFG-12

littlefs shall store files correctly for every `fragment_size` from 1 to
`block_size/4`.

- **Source:** Stated: `lfs3.h:679-684`.
- **Measure:** suite results.
- **Pass:** `fwrite::*` and `files::*` pass with `FRAGMENT_SIZE` in {1, 16,
  64, `BLOCK_SIZE/8`, `BLOCK_SIZE/4`} in B-DEF.
- **Fail:** any permutation fails.
- **Verified by:** as listed.
- **Status:** Partly tested (1, 16, 64 and `BLOCK_SIZE/8` only).
- **When:** nightly.

#### LFS3-CFG-13

littlefs shall pass the file suites with `prog_size` equal to
`block_size`.

- **Source:** Stated: commit `0698c49` (SD and eMMC geometries).
- **Measure:** suite results.
- **Pass:** `files::*`, `fwrite::*` and `fsync::*` pass with G-EMMC and
  G-NAND in B-DEF.
- **Fail:** any permutation fails.
- **Verified by:** as listed.
- **Status:** Untested (no suite runs `prog_size` above 64).
- **When:** nightly.

#### LFS3-CFG-14

littlefs shall pass `cfg`, and so `cfg->context`, unchanged to every block
device callback.

- **Source:** Stated: `lfs3.h:464-466`.
- **Measure:** the pointer the callback receives.
- **Pass:** every suite passes in B-DEF (emubd finds its state through it).
- **Fail:** a callback receives another pointer.
- **Verified by:** all suites.
- **Status:** Tested.
- **When:** every CI run.

#### LFS3-CFG-15

littlefs shall never program a byte of a block twice without erasing the
block in between, including when it writes again after a write that failed
part way, and shall never program bytes a failed write left in its caches.

- **Source:** Stated: `lfs3.h:473-474` ("The block must have previously been
  erased"); #1111 "Pre-erased block tracking" ("littlefs has a very
  conservative model of flash, and avoids progging unless it is sure a prog
  has not been attempted"). A write can fail after programming part of a
  block (a read, prog or erase error, or a bad source block); the bytes it
  programmed are no longer erased, and what it left in the prog cache
  belongs to no commit (issue #3).
- **Measure:** an emubd check that fails any prog to a byte programmed since
  the block's last erase; data checksums after the retry.
- **Pass:** every suite passes in B-DEF and B-BIG with the check enabled;
  `badblocks::crystal_ioerror`, which fails the n-th read, prog or erase of
  a write that rewrites data blocks, then syncs, for every n, passes with
  the check enabled and `lfs3_fs_ck(LFS3_CK_CKMETA | LFS3_CK_CKDATA)`
  returning 0, and `badblocks::graft_torn` passes, in B-DEF, B-YGB and
  B-BIG.
- **Fail:** the check fires, a data checksum mismatches, or an assert.
- **Verified by:** NEW-12 (emubd check, all suites);
  `badblocks::crystal_ioerror`; `badblocks::graft_torn`;
  `powerloss_p1::tear_tail` and `badblocks::error_then_sync` (progs failing
  part way through a write) run with the check.
- **Status:** Partly tested on `v3-integration` (0c531757):
  `badblocks::crystal_ioerror` and `badblocks::graft_torn` pass in B-DEF,
  B-YGB and B-BIG. Before, a failed crystallization left its data block
  marked erased past the bytes it had programmed, and the next one flushed
  the failed attempt's prog cache into its own checksum (issue #3). The
  all-suite run with the check enabled (NEW-12) is not part of this.
- **When:** every CI run.

#### LFS3-CFG-16

littlefs shall repopulate the lookahead buffer during gc when at most
`gc_lookahead_thresh` blocks are known (only when empty for 0; after any
allocation for -1).

- **Source:** Stated: `lfs3.h:579-591`.
- **Measure:** repopulation during gc.
- **Pass:** `gc::lookahead_progress`, `gc::lookahead_relaxed` and
  `gc::lookahead_mutation` pass with `GC_LOOKAHEAD_THRESH` in {0, 16, -1} in
  B-BIG.
- **Fail:** a repopulation at the wrong point.
- **Verified by:** as listed.
- **Status:** Partly tested (compiled out of B-DEF; the default define is
  -1).
- **When:** every CI run.

#### LFS3-CFG-17

littlefs shall repopulate the gbmap during gc when at most
`max(gc_lookgbmap_thresh, lookgbmap_thresh)` blocks are known.

- **Source:** Stated: `lfs3.h:593-606`; `lfs3.c:10841-10851`.
- **Measure:** repopulation during gc.
- **Pass:** `gc::lookgbmap_*` pass with `GC_LOOKGBMAP_THRESH` in {0,
  `BLOCK_COUNT/2`, -1} in B-BIG.
- **Fail:** a repopulation at the wrong point.
- **Verified by:** as listed. The header text is LFS3-DOC-09.
- **Status:** Partly tested (compiled out of B-DEF; the default define is
  -1).
- **When:** every CI run.

### 6.17 Resource bounds (RES)

#### LFS3-RES-01

littlefs shall not call `lfs3_malloc` when the caller supplies
`rcache_buffer`, `pcache_buffer`, `lookahead_buffer` and, for every file,
`fcache_buffer`.

- **Source:** Stated: `lfs3.h:636-650, 790-792` ("By default lfs3_malloc is
  used").
- **Measure:** calls to the allocator.
- **Pass:** every suite, run with static buffers and an allocator that
  fails the test when called, passes in B-DEF; and B-NM builds and passes the
  same.
- **Fail:** any allocator call.
- **Verified by:** NEW.
- **Status:** Untested.
- **When:** every CI run.

#### LFS3-RES-02

littlefs shall not grow `sizeof(lfs3_t)` and the other structures in
`lfs3.h` without the change being reported in the pull request.

- **Source:** Proposal, following the v2 CI size reports and #1111
  "Code/stack size" (ctx 636 bytes default, 508 read-only; 660 and 776 with
  the gbmap in #1114, 2026-02-20).
- **Measure:** `make lfs3.ctx.csv lfs3.structs.csv` on thumb, against the
  base branch.
- **Pass:** CI posts the difference for B-DEF, B-RO, B-YGB and B-BIG.
- **Fail:** no report, or an increase that the PR does not explain.
- **Verified by:** NEW: CI size job (LFS3-CI-06).
- **Status:** Untested.
- **When:** every CI run.

#### LFS3-RES-03

littlefs shall not increase its maximum stack use without the change being
reported in the pull request.

- **Source:** Proposal, following #1111 "Code/stack size" (2280 bytes
  default, 808 read-only).
- **Measure:** `make lfs3.stack.csv` (GCC `-fcallgraph-info`) on thumb.
- **Pass:** CI posts the difference for B-DEF, B-RO, B-YGB and B-BIG.
- **Fail:** no report, or an increase that the PR does not explain.
- **Verified by:** NEW: CI size job.
- **Status:** Untested.
- **When:** every CI run.

#### LFS3-RES-04

littlefs shall not increase its code size without the change being
reported in the pull request.

- **Source:** Proposal, following #1111 "Code/stack size" and #1114
  (2025-09-29: "Open to any PRs/suggestions ... at reducing the code size").
- **Measure:** `make lfs3.code.csv` on thumb with `LFS3_NO_LOG` and
  `LFS3_NO_ASSERT`.
- **Pass:** CI posts the difference for B-DEF, B-RO, B-YGB and B-BIG.
- **Fail:** no report, or an increase that the PR does not explain.
- **Verified by:** NEW: CI size job.
- **Status:** Untested.
- **When:** every CI run.

#### LFS3-RES-05

littlefs shall have a bounded stack depth, with no recursion that the
static stack analysis cannot bound.

- **Source:** Derived: littlefs runs on fixed-size stacks.
- **Measure:** `scripts/stack.py` output.
- **Pass:** every public function has a finite maximum stack in B-DEF and
  B-BIG.
- **Fail:** any function is reported unbounded.
- **Verified by:** NEW: CI size job.
- **Status:** Untested.
- **When:** every CI run.

#### LFS3-RES-06

littlefs shall free only the buffers it allocated when `lfs3_format` or
`lfs3_mount` fails with `LFS3_ERR_NOMEM`.

- **Source:** Stated: `lfs3.h:94`; `lfs3.c:15175-15222` and `15400-15418`.
- **Measure:** crash and leak reports with a failing allocator.
- **Pass:** a NEW case fails the first, second and third allocation in turn,
  with `lfs3_t` filled with 0xab, and gets `LFS3_ERR_NOMEM` with no crash and
  no leak under valgrind, in B-DEF.
- **Fail:** a crash, an invalid free, or a leak.
- **Verified by:** NEW.
- **Status:** Known defect (4-api R8: a failure of the first or second
  allocation frees pointers that were never set).
- **When:** every CI run.

#### LFS3-RES-07

littlefs shall format, mount, fill and check a filesystem of 2^31 - 1
blocks.

- **Source:** Stated: #1114 (2025-06-23): the driver "is currently limited
  to 2^31-1 for both file size and block count (though untested)".
- **Measure:** results on a sparse block device (kiwibd or a sparse emubd).
- **Pass:** a NEW case with `block_count = 2^31 - 1` and 512-byte blocks
  formats, mounts, writes 1000 files spread over the address range, remounts
  and reads them back, in B-DEF and B-YGB.
- **Fail:** any failure.
- **Verified by:** NEW.
- **Status:** Untested.
- **When:** before v3-beta.

#### LFS3-RES-08

littlefs shall need, for each open file, no memory beyond the
`lfs3_file_t` and its file cache.

- **Source:** Derived: #1111 "Efficient inline files, no more RAM
  constraints".
- **Measure:** allocator calls during file operations.
- **Pass:** a NEW case with a counting allocator finds exactly one
  allocation of `fcache_size` per `lfs3_file_open`, and none during reads,
  writes and syncs, in B-DEF.
- **Fail:** any other allocation.
- **Verified by:** NEW.
- **Status:** Untested.
- **When:** every CI run.

### 6.18 Performance (PERF)

PR #1111 makes complexity claims for v3's data structures, and #1114 reports
simulated benchmarks. The in-tree benches (`benches/`, `make bench`) measure
reads, progs and erases but assert nothing. LFS3-PERF-01 to PERF-05 turn the
stated complexity into ratio checks, which hold for the stated growth and
fail for the next worse one. LFS3-PERF-06 to PERF-13 are about sync cost and
the logging workload of Appendix B; their thresholds are our proposals.
LFS3-PERF-14 asks for the cost of `prog_size` and `pcache_size` to be
documented where they are configured.

#### LFS3-PERF-01

littlefs shall compact an rbyd of n tags with O(n log n) reads.

- **Source:** Stated: #1111 "Efficient metadata compaction: O(n^2) to
  O(n log n)".
- **Measure:** `bench_reads` of `bench_rbyd` compaction for n from 256 to
  8192 tags.
- **Pass:** the reads for 2n tags are at most 2.5 times the reads for n
  tags, for every doubling (O(n log n) gives about 2.2; O(n^2) gives 4).
- **Fail:** any doubling exceeds 2.5.
- **Verified by:** NEW: assertion over `bench_rbyd`.
- **Status:** Untested (the bench exists; nothing checks it).
- **When:** nightly.

#### LFS3-PERF-02

littlefs shall look up a tag in an rbyd of n tags with O(log n) reads.

- **Source:** Stated: #1111 "Efficient metadata compaction" ("metadata
  lookup O(n) to O(log n)").
- **Measure:** `bench_reads` per lookup in `bench_rbyd`.
- **Pass:** reads per lookup at n = 4096 are at most 2.5 times those at
  n = 64 (O(log n) gives 2; O(n) gives 64).
- **Fail:** the ratio exceeds 2.5.
- **Verified by:** NEW: assertion over `bench_rbyd`.
- **Status:** Untested.
- **When:** nightly.

#### LFS3-PERF-03

littlefs shall perform a small random write into a file of n bytes with
O(log_b^2 n) programmed bytes.

- **Source:** Stated: #1111 "Efficient random writes: O(n) to
  O(log_b^2 n)".
- **Measure:** `bench_progged` per 64-byte random write and sync in
  `bench_wt` random, for 1 MiB and 16 MiB files.
- **Pass:** the ratio 16 MiB / 1 MiB is at most 2 (O(log^2 n) gives about
  1.4; O(n) gives 16).
- **Fail:** the ratio exceeds 2.
- **Verified by:** NEW: assertion over `bench_wt`.
- **Status:** Untested.
- **When:** nightly.

#### LFS3-PERF-04

littlefs shall look up a name among n entries with O(log_b n) reads.

- **Source:** Stated: #1111 "Efficient file name lookup: O(n) to
  O(log_b n)".
- **Measure:** `bench_reads` per `lfs3_stat` in `bench_dir`.
- **Pass:** reads per stat with 4096 entries are at most 3 times those with
  64 entries (O(n) gives 64).
- **Fail:** the ratio exceeds 3.
- **Verified by:** NEW: assertion over `bench_dir`.
- **Status:** Untested.
- **When:** nightly.

#### LFS3-PERF-05

littlefs shall scale the cost of each operation O(b log b) in the block
size b.

- **Source:** Stated: #1114 (2025-11-14): "v3 was redesigned so that all
  operations scale O(b log b) w.r.t. the block size".
- **Measure:** reads plus programmed bytes per operation in `bench_file`
  and `bench_dir`, at block sizes 4 KiB and 128 KiB.
- **Pass:** the ratio 128 KiB / 4 KiB is at most 64 for every operation
  (O(b log b) gives about 45; O(b^2) gives 1024).
- **Fail:** any ratio exceeds 64.
- **Verified by:** NEW: assertion over the benches.
- **Status:** Untested.
- **When:** nightly.

#### LFS3-PERF-06

littlefs shall program at most three prog units of metadata for a small
append followed by `lfs3_file_sync`, when `prog_size` is 256, excluding syncs
that compact.

- **Source:** Stated: #1114 (2025-11-14): "Sync still has a cost ... but it
  should be ~2 progs (pages) instead of ~1 block". The threshold of three is
  our proposal, from the measurement in 2-files §1.8.
- **Measure:** bytes programmed per 16-byte append and sync on a file held
  in a B-shrub, over 100 appends.
- **Pass:** the median is at most 768 bytes, and no sync that does not
  compact programs more than 1024 bytes, in B-DEF with G-W25Q128
  (`prog_size` 256).
- **Fail:** either bound is exceeded.
- **Verified by:** NEW: bench case. A probe measured about 768 bytes at
  `b10efaa`.
- **Status:** Partly tested (measured outside the suite).
- **When:** nightly.

#### LFS3-PERF-07

littlefs shall call `cfg->sync` at most twice for each `lfs3_file_sync` that
does not compact metadata.

- **Source:** Proposal. One sync before a commit that references new blocks
  and one after it are what the ordering needs (3-alloc §3 I5). `sync` is
  cheap on raw NOR flash but can be a cache flush on SD and eMMC.
- **Measure:** `cfg->sync` calls per `lfs3_file_sync`.
- **Pass:** a NEW bench case counts at most 2 over 100 small appends and
  syncs, in B-DEF.
- **Fail:** more than 2.
- **Verified by:** NEW.
- **Status:** Known defect (measured: 7 calls per small append and sync
  with `prog_size` 16, and 4 with `prog_size` 1; 2-files B18, Appendix B.2).
- **When:** nightly.

#### LFS3-PERF-08

littlefs shall perform no more erases than littlefs v2.11.3 on the logging
workload W-LOG at 1 row per second, for `prog_size` 1, 16 and 256, with
and without the gbmap and pre-erase.

- **Source:** Proposal, from #1111 "Better logging: No more sync-padding
  issues" and #1114 (2026-03-12: logging "is arguably the most important
  bench"). The workload is defined in Appendix B.
- **Measure:** erases per minute: the `bench_erases` of the `log` probe of
  NEW `bench_wlog_fresh` with `RATE=1`, times 60 and divided by `SECONDS`
  (600), for every permutation of `PROG_SIZE`, `GBMAP` and `PREERASE`, in
  B-BIG; v2.11.3's erases per minute on the same workload (Appendix B.1).
- **Pass:** every permutation is at or below v2.11.3's figure for its
  `prog_size`: 58.7 for `prog_size` 1 and 16, 62.7 for 256.
- **Fail:** any permutation is above it.
- **Verified by:** `bench_wlog_fresh`, against the v2.11.3 figures of
  Appendix B.1.
- **Status:** Partly tested at `b10efaa` (measured outside the suite).
  Tested on `v3-integration` (`32eb36e7`): at most 10.6 erases per minute
  (`prog_size` 256), against v2's 62.7 (Appendix B.1).
- **When:** nightly.

#### LFS3-PERF-09

littlefs shall perform no more erases than littlefs v2.11.3 on the logging
workload W-LOG at 50 rows per second, for `prog_size` 1, 16 and 256, with
and without the gbmap and pre-erase.

- **Source:** Proposal, as LFS3-PERF-08. The bound applies at every
  `prog_size`, 256 included (Q23).
- **Measure:** erases per minute: the `bench_erases` of the `log` probe of
  NEW `bench_wlog_fresh` with `RATE=50`, times 60 and divided by `SECONDS`
  (600), for every permutation of `PROG_SIZE`, `GBMAP` and `PREERASE`, in
  B-BIG; v2.11.3's erases per minute on the same workload (Appendix B.1).
- **Pass:** every permutation is at or below v2.11.3's figure for its
  `prog_size`: 76.7 for `prog_size` 1 and 16, 80.7 for 256.
- **Fail:** any permutation is above it.
- **Verified by:** `bench_wlog_fresh`, against the v2.11.3 figures of
  Appendix B.1.
- **Status:** Known defect at `b10efaa` (measured, M-3: with `prog_size`
  256 v3 does 89.5 erases per minute against v2's 80.7), and at
  `fd3157e3` (86.7). Tested on `v3-integration` (`ec0733b8`, `554e89f9`,
  `32eb36e7`, Appendix B.5 B, C and D): the worst permutation does 48.5
  erases per minute (`prog_size` 256) against v2's 80.7, and
  `bench_wlog_fresh` fails if any exceeds v2.
- **When:** nightly.

#### LFS3-PERF-10

littlefs shall perform at most one erase in any single `lfs3_file_write`
or `lfs3_file_sync` call on W-LOG, when blocks are pre-erased by gc before
logging starts.

- **Source:** Stated: #1111 "Pre-erased block tracking" ("should
  significantly reduce the latency [of] file writes in the critical path").
  The bound of one is our proposal: an mdir compaction erases its partner
  block, which pre-erase does not cover.
- **Measure:** erases per call: the `max_call_erases` result of NEW
  `bench_wlog_fresh` with `GBMAP` and `PREERASE`; longest call in simulated
  time (`max_call_ns`).
- **Pass:** on W-LOG at 1 row per second with `prog_size` 1, the gbmap,
  `LFS3_PREERASE` and `LFS3_M_REVPERTURB`, no call performs more than one
  erase.
- **Fail:** a call performs two or more erases.
- **Verified by:** `bench_wlog_fresh`. Measured (M-2): longest call 51 ms,
  one erase.
- **Status:** Partly tested at `b10efaa` (measured outside the suite).
  Tested on `v3-integration` (`32eb36e7`): every pre-erase permutation, at
  both rates and every `prog_size`, has at most one erase in a call, which
  `bench_wlog_fresh` asserts, and its longest call is 49.6 to 53.8 ms
  (Appendix B.1).
- **When:** nightly.

#### LFS3-PERF-11

littlefs shall not erase a block for a synced append of fewer than
`crystal_thresh` bytes to a file whose last data block still has erased
space, within the same mount and excluding metadata compaction.

- **Source:** Stated: #1111 "Better logging" (no sync-padding); the resume
  path, `lfs3.c:13715-13756`.
- **Measure:** erases per append and sync.
- **Pass:** a NEW case of 100 appends of 16 bytes, each followed by sync, to
  a file with a partially written data block, counts no data-block erase,
  with `PROG_SIZE` in {1, 16}, in B-DEF.
- **Fail:** any data-block erase.
- **Verified by:** NEW. A probe confirmed the behaviour at `b10efaa`
  (2-files §1.8). After a remount the erased state is lost by design
  (LFS3-DOC-18).
- **Status:** Partly tested (measured outside the suite).
- **When:** every CI run.

#### LFS3-PERF-12

littlefs shall not regress the reads, programmed bytes or erases of the
in-tree write and read benches by more than 10% without the change being
reported in the pull request.

- **Source:** Proposal. #1114 (2026-03-12) records a regression found by
  re-running the benchmarks.
- **Measure:** `make bench-marks-diff` for `bench_wt` and `bench_rt`
  (sequential, random, logging, many), NOR and NAND models.
- **Pass:** CI reports the differences and none exceeds 10% without an
  explanation in the pull request.
- **Fail:** a larger unexplained regression, or no report.
- **Verified by:** NEW: CI bench job.
- **Status:** Untested.
- **When:** nightly.

#### LFS3-PERF-13

littlefs shall allocate the first block after a mount on a gbmap filesystem
without traversing the whole filesystem.

- **Source:** Stated: #1111 "Efficient block allocation" ("persists free
  block information on-disk, avoiding the need to repopulate the lookahead
  buffer after every mount").
- **Measure:** blocks read by the first allocation after mount on a 128 MiB
  device with 10,000 files.
- **Pass:** with the gbmap, the first allocation reads at most 1% of the
  blocks that the same allocation reads without it, in B-YGB.
- **Fail:** a larger number.
- **Verified by:** NEW: bench case.
- **Status:** Untested.
- **When:** nightly.

#### LFS3-PERF-14

littlefs shall document, next to `prog_size` and `pcache_size` in
`lfs3.h`, what each costs on W-LOG.

- **Source:** Derived: `prog_size` multiplies v3's erases on a sync-heavy
  log (LFS3-PERF-09), and the erased-state checksums that Q19 widened to
  `pcache_size` read more and distrust older images (Appendix B.4). A user
  choosing these values cannot see either cost from the header at
  `b10efaa`.
- **Measure:** `lfs3.h`.
- **Pass:** next to `prog_size`, the header recommends the smallest
  program the device supports rather than its page size, with W-LOG's
  erases per minute at `prog_size` 1, 16 and 256; next to `pcache_size`, it
  states the extra bytes read per metadata fetch and commit, W-LOG's reads
  at mount and per minute, and the first-write cost on an image whose
  checksums are narrower (an older driver or a smaller `pcache_size`). The
  figures match `bench_wlog_fresh` and `bench_wlog_narrow`.
- **Fail:** any item is missing or disagrees with the benches by more than
  10%.
- **Verified by:** review against `bench_wlog_fresh` and
  `bench_wlog_narrow`.
- **Status:** Not implemented at `b10efaa`. Met on `v3-integration`: the
  comments on `prog_size` and `pcache_size` give the figures of Appendix
  B.1 and B.4, and DESIGN.md's worked example repeats them.
- **When:** before v3-beta.

### 6.19 Thread safety (THR)

#### LFS3-THR-01

littlefs shall, when built with `LFS3_THREADSAFE`, call `cfg->lock` once at
the start of each public function and `cfg->unlock` once before it
returns, on every path, errors included; shall run every block device
operation of the call between the two; and shall never call `cfg->lock`
while it holds the lock, including when one public function uses another.

- **Source:** Stated: `lfs3.h:495-503` documents the callbacks, and v2
  implemented them, with a wrapper around each public function. Q12 is
  decided (2026-10-05): v3 keeps the option, as `lfs3.h` declares it. A
  lock taken twice deadlocks with a non-recursive mutex, and one left
  held deadlocks the next call, so each must be exactly once, on the
  error paths too.
- **Measure:** calls to the callbacks for each public call, and whether
  the lock is held at each block device operation.
- **Pass:** in B-TS, the test runner's `lock` fails a case that takes the
  lock while it is held, its `unlock` one that releases it unheld, and
  `runners/test_errs.h` checks after each public call of every suite
  that the call took the lock once and released it (`make
  test-threadsafe`); NEW-89 calls every public function, on paths that
  succeed and paths that fail, and the uses of other public functions
  inside `lfs3_get`, `lfs3_size`, `lfs3_set`, `lfs3_file_open`,
  `lfs3_file_read`, `lfs3_file_write`, `lfs3_file_sync`,
  `lfs3_fs_health`, the writing calls, and mounts and formats with check
  flags, with block device callbacks that fail the case outside the
  lock.
- **Fail:** a public call that doesn't take the lock, takes it twice,
  takes it while held or returns holding it, or a block device operation
  outside it.
- **Verified by:** NEW-89 (`threadsafe::*`), and every suite under `make
  test-threadsafe`, job test-threadsafe.
- **Status:** Known defect (4-api R19: `lfs3.c` never calls them).
  Fixed and tested on `v3-r8` (`ecf44d33`): `make test-threadsafe` passes
  668,220 of 668,220 cases with the runner's checks, `threadsafe::*`
  included, which failed before at the first block device read, outside
  any lock, as every case of every suite did.
- **When:** every CI run.

#### LFS3-THR-02

littlefs shall, when `cfg->lock` fails, return its error before any block
device operation, with the filesystem, the `lfs3_t` and the call's handle
and buffers unchanged; when `cfg->unlock` fails after a call that
succeeded, return unlock's error, the call having taken effect; when the
call fails, return the call's own error whatever unlock returns; and
`lfs3.h` shall list both errors for every public function, and ERRORS.md
shall give their action and the state after them.

- **Source:** Stated: `lfs3.h:496-502` ("Negative error codes are
  propagated to the user"). Derived from principle 1 (every error a call
  can return has a documented action and state after it): a lock that
  fails, for example on a timeout, must leave nothing half done so the
  call can be retried; a failed call's own error states what the
  filesystem holds (ERRORS.md), which unlock's error would hide, so the
  call's error is returned; an unlock error after a success says the
  call took effect, and only its result, a count, a position or a size,
  is lost.
- **Measure:** return values, block device operations, and the bytes of
  the `lfs3_t`, the handles and the buffers, with failing callbacks; the
  "Returns" paragraph of each function in `lfs3.h`; ERRORS.md.
- **Pass:** NEW-89 in B-TS: with `lock` failing, every public function
  returns its error, with no block device operation and the `lfs3_t`,
  the handles and the buffers unchanged byte for byte; with `unlock`
  failing, every public function that succeeds returns unlock's error
  and its effect is there for the calls after it, and one that fails
  returns its own error; `scripts/ckerrs.py` finds lock and unlock
  errors in every function's "Returns" paragraph, and over the B-TS
  recordings accepts the test's lock and unlock codes from every
  function; ERRORS.md gives the action and the state after each.
- **Fail:** any other result.
- **Verified by:** NEW-89; `scripts/ckerrs.py` over the recordings of job
  test-threadsafe; review of ERRORS.md.
- **Status:** Known defect (4-api R19). Fixed and tested on `v3-r8`
  (`ecf44d33`): `threadsafe::lock_fails` and `threadsafe::unlock_fails`
  pass, and `scripts/ckerrs.py` over the B-TS recordings sees the lock
  and unlock codes from all 48 functions of that build, with 0 errors.
- **When:** every CI run.

#### LFS3-THR-03

littlefs shall allow different `lfs3_t` objects on different block devices
to be used from different threads at the same time without
`LFS3_THREADSAFE`.

- **Source:** Derived from LFS3-GEN-04: littlefs keeps no mutable global
  state.
- **Measure:** ThreadSanitizer reports and results.
- **Pass:** a NEW case runs two filesystems on two threads under
  `-fsanitize=thread` with no report, in B-DEF.
- **Fail:** any report or wrong result.
- **Verified by:** NEW.
- **Status:** Untested.
- **When:** nightly.

#### LFS3-THR-04

littlefs shall compile, without `LFS3_THREADSAFE`, to the same code as if
the lock were not implemented: the option shall cost a build that doesn't
define it no code, stack or RAM.

- **Source:** Derived: most targets run littlefs from one thread and
  must not pay for a lock they don't use; the owner's direction for issue
  #8 of the fork.
- **Measure:** the `.text` of `lfs3.o` built with `-Os -DLFS3_NO_LOG
  -DLFS3_NO_ASSERT`, by `arm-none-eabi-gcc -mthumb` and by the host's
  clang, in B-DEF, B-RO, B-YGB, B-BIG, B-NM and with `LFS3_GBMAP` and
  `LFS3_GC`.
- **Pass:** byte for byte the `.text` of the commit before the lock was
  implemented, and, from then on, no change in B-DEF's code size from a
  commit that only changes the locking.
- **Fail:** any difference.
- **Verified by:** comparison at the commit that implements the lock;
  J-SIZE reports the B-DEF and B-TS code sizes on every run.
- **Status:** Not implemented at `b10efaa`, which has no lock. Met on
  `v3-r8` (`ecf44d33`): `.text` is byte for byte that of `424c91f2` in all
  14 builds; B-TS costs 1,528 bytes on thumb (39,540 to 41,068), 1,796
  with `LFS3_BIGGEST`.
- **When:** every CI run.

### 6.20 Build configurations (BUILD)

#### LFS3-BUILD-01

littlefs shall compile the default configuration without warnings with GCC
and clang under `-std=c99 -Wall -Wextra -pedantic`.

- **Source:** Derived: the Makefile builds with these flags, and v2 CI
  builds with clang as well as GCC.
- **Measure:** compiler diagnostics.
- **Pass:** B-DEF compiles with `-Werror` under GCC and clang on A-64LE, and
  under the cross compilers for A-32LE and A-32BE.
- **Fail:** any warning or error.
- **Verified by:** NEW: CI build jobs.
- **Status:** Partly tested (GCC on Linux has 0 warnings; at `b10efaa` every
  build had a `-Warray-bounds` warning, F-3; the Makefile's GCC-only flags
  break clang, 5-verif §3.2).
- **When:** every CI run.

#### LFS3-BUILD-02

littlefs shall compile with `LFS3_BIGGEST`.

- **Source:** Stated: `lfs3_util.h:28` ("LFS3_BIGGEST enables all opt-in
  features").
- **Measure:** compiler result.
- **Pass:** B-BIG compiles with `-Werror` under GCC and clang.
- **Fail:** any error or warning.
- **Verified by:** NEW: CI build job.
- **Status:** Known defect (F-2; fixed on v3-fixes).
- **When:** every CI run.

#### LFS3-BUILD-03

littlefs shall compile with `LFS3_CKDATACKSUMS`, alone and with
`LFS3_RDONLY`.

- **Source:** Stated: `lfs3.h:203-206, 266-269`.
- **Measure:** compiler result.
- **Pass:** both builds compile with `-Werror`.
- **Fail:** any error or warning.
- **Verified by:** NEW: CI build job.
- **Status:** Known defect (F-2; fixed on v3-fixes).
- **When:** every CI run.

#### LFS3-BUILD-04

littlefs shall compile with `LFS3_RDONLY` combined with each of
`LFS3_CKFETCHES`, `LFS3_CKMETAPARITY`, `LFS3_CKDATACKSUMS`, `LFS3_GBMAP`,
`LFS3_GC`, `LFS3_BLEAFCACHE` and `LFS3_NO_MALLOC`.

- **Source:** Stated: `lfs3.h` declares read-side flags for these options
  outside `#ifndef LFS3_RDONLY` (for example `lfs3.h:258-269`, `301-303`).
- **Measure:** compiler result.
- **Pass:** each build compiles with `-Werror`.
- **Fail:** any error or warning.
- **Verified by:** NEW: CI build matrix.
- **Status:** Known defect (4-api R6: `RDONLY` with `CKMETAPARITY`, with
  `GBMAP` and with `GC` fail to compile).
- **When:** every CI run.

#### LFS3-BUILD-05

littlefs shall compile with `LFS3_PMUL_CRC32C`.

- **Source:** Stated: the option selects a crc32c implementation in
  `lfs3_util.c`.
- **Measure:** compiler result.
- **Pass:** the build compiles with `-Werror` and passes `ck::crc32c*`.
- **Fail:** any error, warning or failure.
- **Verified by:** NEW: CI build job; `ck::crc32c*`.
- **Status:** Known defect (1-meta 0.7b: `lfs3_fromle32_` is undefined at
  `lfs3_util.c:210`).
- **When:** every CI run.

#### LFS3-BUILD-06

littlefs shall compile with each debug option: `LFS3_DBGRBYDFETCHES`,
`LFS3_DBGRBYDCOMMITS`, `LFS3_DBGRBYDBALANCE`, `LFS3_DBGBTREEFETCHES`,
`LFS3_DBGBTREECOMMITS`, `LFS3_DBGMDIRFETCHES`, `LFS3_DBGMDIRCOMMITS` and
`LFS3_DBGALLOCS`.

- **Source:** Stated: the options are used in `lfs3.c`.
- **Measure:** compiler result.
- **Pass:** each build compiles.
- **Fail:** any error.
- **Verified by:** NEW: CI build matrix.
- **Status:** Known defect (2-files B6: `LFS3_DBGBTREECOMMITS` uses members
  that moved, `lfs3.c:6969-6983`).
- **When:** every CI run.

#### LFS3-BUILD-07

littlefs shall stop compilation with an error when `LFS3_PREERASE` is
defined without both `LFS3_GBMAP` and `LFS3_REVPERTURB`.

- **Source:** Stated: `lfs3_util.h:111-114`.
- **Measure:** compiler result.
- **Pass:** builds with `LFS3_PREERASE` alone and with only one of the two
  fail with the `#error` message.
- **Fail:** any of them compiles.
- **Verified by:** NEW: build check.
- **Status:** Untested (implemented).
- **When:** every CI run.

#### LFS3-BUILD-08

littlefs shall take its utilities from the user's header, and emit no
default utility code, when `LFS3_CFG` is defined.

- **Source:** Stated: `lfs3_util.h:14-21` ("If LFS3_CFG is used, none of
  the default utils will be emitted").
- **Measure:** symbols in `lfs3_util.o`.
- **Pass:** a NEW build with `-DLFS3_CFG=my_cfg.h` produces an
  `lfs3_util.o` without the default crc32c and leb128 code.
- **Fail:** the default code is emitted.
- **Verified by:** NEW.
- **Status:** Known defect (4-api R20: `lfs3_util.c:11` checks
  `LFS3_CONFIG`).
- **When:** every CI run.

#### LFS3-BUILD-09

littlefs shall give the same results with `LFS3_NO_STRINGH` as with the
C library string functions.

- **Source:** Derived: the fallbacks replace `string.h` functions
  (`lfs3_util.h:512-757`).
- **Measure:** results of each fallback against the C library for random
  inputs, including multi-character sets for `lfs3_strspn` and
  `lfs3_strcspn`.
- **Pass:** a NEW unit case finds identical results.
- **Fail:** any difference.
- **Verified by:** NEW.
- **Status:** Known defect (4-api R21: the `lfs3_strspn` fallback is wrong
  for sets of two or more characters; every current caller passes "/").
- **When:** every CI run.

#### LFS3-BUILD-10

littlefs shall export only the symbols that `lfs3.h` and `lfs3_util.h`
declare for the build configuration.

- **Source:** Derived: other code linked with littlefs must not collide with
  its internals; the `lfs3_` prefix exists so that versions can be linked
  side by side (#1114, 2025-06-23).
- **Measure:** `nm` of `lfs3.o` and `lfs3_util.o` against the declarations.
- **Pass:** a NEW check finds no extra external symbol in B-DEF, B-NM and
  B-BIG.
- **Fail:** any extra symbol.
- **Verified by:** NEW.
- **Status:** Known defect (2-files B10: `lfs3_file_opencfg_` is external;
  4-api R22: `lfs3_file_open` is defined under `LFS3_NO_MALLOC`, where the
  header hides it).
- **When:** every CI run.

#### LFS3-BUILD-11

littlefs shall define every public function with the prototype that
`lfs3.h` declares.

- **Source:** Derived: C requires compatible declarations.
- **Measure:** compiler diagnostics with the header included.
- **Pass:** B-DEF compiles with `-Werror` on a target where `int32_t` is
  `long` (for example arm-none-eabi).
- **Fail:** a conflicting-types error.
- **Verified by:** NEW: CI build job.
- **Status:** Known defect (2-files B9: `lfs3_file_rewind` is declared `int`
  and defined `lfs3_soff_t`).
- **When:** every CI run.

#### LFS3-BUILD-12

littlefs shall force the matching mount or format flag in every build with
an `LFS3_YES_*` option.

- **Source:** Stated: `lfs3_util.h:62-92`; `lfs3.c:15983-16009,
  16290-16310`.
- **Measure:** `lfs3_fs_stat` flags after a plain mount.
- **Pass:** a NEW case in each B-YES-x build finds the matching `LFS3_I_*`
  flag set after `lfs3_mount(..., 0, ...)`.
- **Fail:** the flag is clear.
- **Verified by:** NEW.
- **Status:** Untested.
- **When:** nightly.

#### LFS3-BUILD-13

littlefs shall pass the test suite in B-BIG.

- **Source:** Derived: B-BIG is the only build that compiles in the gc,
  gbmap, pre-erase and check cases (5-verif §0.2).
- **Measure:** runner results.
- **Pass:** `make test` with `LFS3_BIGGEST=1` exits 0.
- **Fail:** any failure.
- **Verified by:** all suites.
- **Status:** Known defect (F-2: B-BIG does not build at `b10efaa`, and is
  never run in upstream CI. Built on v3-fixes and run here (1,083,265
  permutations, Appendix B.3), 1275 permutations of `mount::flags` and
  `mount::format_flags` fail because of D-2, and an AddressSanitizer build
  finds 1-meta 0.7c in about 150 `btree::find*` permutations).
- **When:** every CI run.

#### LFS3-BUILD-14

littlefs shall pass the test suite in B-YGB.

- **Source:** Derived: `tests/test_gbmap.toml:3-6`; the `GBMAP=true`
  permutations of `test_alloc`, `test_grow`, `test_gc`, `test_trvs` and
  `test_mount`.
- **Measure:** runner results.
- **Pass:** `make test` with `LFS3_YES_GBMAP=1` exits 0.
- **Fail:** any failure.
- **Verified by:** all suites.
- **Status:** Tested on `v3-integration` (e529bb20): the full suite passes
  in B-YGB, after issues #3, #4 and #18; the `test-yes-gbmap` CI job runs
  it.
- **When:** every CI run.

#### LFS3-BUILD-15

littlefs shall pass the test suite in every B-YES-x build.

- **Source:** Derived: each `LFS3_YES_*` option changes the mount and format
  defaults.
- **Measure:** runner results.
- **Pass:** `make test` exits 0 for each x in REVPERTURB, REVNOISE,
  CKPROGS, CKFETCHES, CKMETAPARITY, CKDATACKSUMS, GC, BLEAFCACHE, FLUSH and
  SYNC.
- **Fail:** any failure.
- **Verified by:** all suites.
- **Status:** Untested.
- **When:** nightly.

#### LFS3-BUILD-16

littlefs shall pass the test suite in B-NA, built so that littlefs's own
asserts compile out as in a release build while the tests keep theirs.

- **Source:** Derived: release builds define `LFS3_NO_ASSERT`, and some
  defects show different behaviour there (for example 2-files B1 corrupts
  silently instead of asserting). The runner rewrites asserts with
  `scripts/prettyasserts.py` to show their operands; if it rewrites
  `LFS3_ASSERT` in `lfs3.c`, a B-NA runner still asserts and release
  behaviour can't be tested (issue #14).
- **Measure:** runner results; `mount::noassert`, an internal case built
  only with `LFS3_NO_ASSERT`, which passes only if `LFS3_ASSERT` in
  littlefs's sources compiles out.
- **Pass:** `make test-release` (B-NA, everything in `RELEASE_DIR`) exits
  0, including `mount::noassert` (cases that test an assert are excluded by
  `ifndef`); test code, emubd and the runner keep their asserts; a CI job
  runs it.
- **Fail:** any failure, or an `LFS3_ASSERT` in `lfs3.c` or `lfs3_util.c`
  that still fires in B-NA.
- **Verified by:** `make test-release` (NEW-133), `mount::noassert`.
- **Status:** Tested on `v3-integration` (e529bb20). Before, the B-NA
  runner didn't build (tests call `assert` from macros prettyasserts can't
  rewrite, and `lfs3_util.h` includes `<assert.h>` only with asserts on),
  and with that fixed every `LFS3_ASSERT` still trapped (issue #14). Death
  tests, which expect littlefs to assert, are built only without
  `LFS3_NO_ASSERT`.
- **When:** every CI run.

#### LFS3-BUILD-17

littlefs shall pass the test suite in B-NB and in B-NS.

- **Source:** Stated: `lfs3_util.h:356-363` (the fallbacks exist "for
  debugging purposes"); v2 CI ran `LFS_NO_INTRINSICS`.
- **Measure:** runner results.
- **Pass:** `make test` exits 0 with `-DLFS3_NO_BUILTINS` and with
  `-DLFS3_NO_STRINGH`.
- **Fail:** any failure.
- **Verified by:** all suites.
- **Status:** Untested.
- **When:** nightly.

#### LFS3-BUILD-18

littlefs shall honour a smaller `LFS3_NAME_MAX` and a smaller
`LFS3_FILE_MAX` defined at compile time.

- **Source:** Stated: `lfs3.h:62-74` ("may be redefined").
- **Measure:** `sizeof(struct lfs3_info)`; errors at the limits.
- **Pass:** a NEW build with `LFS3_NAME_MAX=32` and `LFS3_FILE_MAX=65535`
  passes `paths::*` and `fwrite::*fbig`, with `LFS3_ERR_NAMETOOLONG` beyond
  32 bytes and `LFS3_ERR_FBIG` beyond 65535.
- **Fail:** any failure.
- **Verified by:** NEW.
- **Status:** Untested.
- **When:** nightly.

#### LFS3-BUILD-19

littlefs shall read, in a B-RO build, every image that the full build
writes.

- **Source:** Derived: the read-only build is used in bootloaders to read
  images written by the full driver (#1111 comments, 2025-05-29).
- **Measure:** content read by B-RO.
- **Pass:** a NEW case writes images with `files::*`, `dirs::*` and
  `attrs::*` workloads in B-DEF and B-YGB, and B-RO reads every file,
  directory and attribute back.
- **Fail:** any mount error or difference.
- **Verified by:** NEW (the runner cannot drive B-RO today).
- **Status:** Untested.
- **When:** every CI run.

#### LFS3-BUILD-20

littlefs shall compile with `LFS3_NO_LOG`, with `LFS3_YES_TRACE`, and with
user definitions of `LFS3_TRACE`, `LFS3_DEBUG`, `LFS3_INFO`, `LFS3_WARN`,
`LFS3_ERROR`, `LFS3_ASSERT` and `LFS3_UNREACHABLE`.

- **Source:** Stated: `lfs3_util.h:94-212`.
- **Measure:** compiler result.
- **Pass:** each build compiles with `-Werror`.
- **Fail:** any error or warning.
- **Verified by:** NEW: CI build matrix.
- **Status:** Untested (the analysis found they compile; no CI job).
- **When:** every CI run.

### 6.21 Continuous integration (CI)

These requirements are on the project's CI rather than on the code. The
analysis in 5-verif §3 gives the changes needed.

#### LFS3-CI-01

littlefs shall run the default test suite on every push and pull request.

- **Source:** Derived: v2 practice; #1114 (2025-07-14): "Proving
  functionality through testing/benchmarking is currently the best ROI".
- **Measure:** CI job results.
- **Pass:** a v3 workflow runs `make test` on A-64LE and reports its result
  on each pull request.
- **Fail:** no such job, or the job cannot pass.
- **Verified by:** `.github/workflows/test.yml`.
- **Status:** Known defect (2-files B19: the workflow is the 2022 v2 file
  and cannot pass; for example `pip3 install toml` where the scripts need
  `tomllib`, and `-P1`, which v3 rejects).
- **When:** every CI run.

#### LFS3-CI-02

littlefs shall run the default test suite on A-32LE and A-32BE on every
push and pull request.

- **Source:** Derived: v2 CI ran thumb, mips and powerpc; LFS3-GEN-01,
  LFS3-META-04.
- **Measure:** CI job results.
- **Pass:** jobs for thumb, mips and powerpc run `make test` under qemu.
- **Fail:** any architecture missing, or a failing job.
- **Verified by:** `.github/workflows/test.yml`.
- **Status:** Known defect (2-files B19).
- **When:** every CI run.

#### LFS3-CI-03

littlefs shall run the test suite in B-BIG and B-YGB on every push and pull
request.

- **Source:** Derived: 5-verif §0.2 (58 cases run zero permutations in
  B-DEF).
- **Measure:** CI job results.
- **Pass:** both jobs exist and pass.
- **Fail:** either missing or failing.
- **Verified by:** `.github/workflows/test.yml`.
- **Status:** Untested (no such job).
- **When:** every CI run.

#### LFS3-CI-04

littlefs shall run the test suite under valgrind and under the address and
undefined-behaviour sanitizers.

- **Source:** Derived: v2 CI ran valgrind; LFS3-GEN-03.
- **Measure:** CI job results.
- **Pass:** jobs run `test.py --valgrind -Pnone` and a sanitizer build with
  `-Pnone`, and both pass. The sanitizer build runs every suite in B-DEF
  with `UBSAN_OPTIONS=halt_on_error=1`, and on Linux with LeakSanitizer,
  which checks the `-Pnone` run only: power loss longjmps out of a test
  and leaks what the test allocated.
- **Fail:** any job missing or failing.
- **Verified by:** `.github/workflows/test.yml`: jobs `test-valgrind` and
  `test-sanitize` (`make test-sanitize`).
- **Status:** Tested on `v3-integration` (41165ec9) in the CI image
  (Ubuntu 24.04, GCC 13.3): the `test-valgrind` command passes (2,433
  permutations), and `make test-sanitize` passes in B-DEF with
  LeakSanitizer (see LFS3-GEN-03). `test-valgrind` also passed on GitHub
  at 8fc4d1d0; `test-sanitize` has not run on GitHub yet.
- **When:** every CI run.

#### LFS3-CI-05

littlefs shall run the extended power-loss schedules and behaviours on a
schedule.

- **Source:** Derived: LFS3-PL-02, PL-03, PL-23, PL-24.
- **Measure:** scheduled job results.
- **Pass:** a nightly job runs `-P'permute(1)'` on all suites,
  `-P'permute(2)'` on `test_dirs`, `test_relocations` and `test_powerloss`,
  `-Plog`, and every reentrant case with `-DPOWERLOSS_BEHAVIOR=0,1,2,3,4`,
  and all pass.
- **Fail:** any part missing or failing.
- **Verified by:** NEW workflow.
- **Status:** Untested (no such job).
- **When:** nightly.

#### LFS3-CI-06

littlefs shall report code, stack, context and structure sizes for B-DEF,
B-RO, B-YGB and B-BIG on every pull request.

- **Source:** Derived: v2 CI reports sizes; LFS3-RES-02 to RES-05.
- **Measure:** CI job output.
- **Pass:** a job runs `make lfs3.code.csv lfs3.data.csv lfs3.stack.csv
  lfs3.ctx.csv lfs3.structs.csv` on thumb and posts the difference.
- **Fail:** no report.
- **Verified by:** `.github/workflows/test.yml`.
- **Status:** Known defect (2-files B19: the v2 size jobs use `LFS_*` macros
  and `lfs.*.csv` targets).
- **When:** every CI run.

#### LFS3-CI-07

littlefs shall not reduce per-function line coverage of `lfs3.c` without
the change being reported in the pull request.

- **Source:** Stated: #1111 "Out-of-scope" lists 100% line/branch coverage;
  a ratchet respects that. Proposal for the mechanism.
- **Measure:** `make lfs3.cov.csv` with `COVGEN=1`, B-DEF and B-BIG
  reported separately.
- **Pass:** CI posts the per-function difference, and every reduction is
  explained in the pull request.
- **Fail:** no report, or an unexplained reduction.
- **Verified by:** `.github/workflows/test.yml`.
- **Status:** Known defect (2-files B19: the v2 coverage job builds
  `lfs.cov.csv`).
- **When:** every CI run.

#### LFS3-CI-08

littlefs shall have test and bench tooling that runs as documented on
Linux and macOS.

- **Source:** Derived: the Makefile and scripts are the project's
  interface for contributors.
- **Measure:** results of `make test`, `make bench`, `PERFBDGEN=1 make
  bench` and `./scripts/test.py -j`.
- **Pass:** each runs without error on Ubuntu 24.04 and macOS, and
  `scripts/test.py` emits no `SyntaxWarning` on Python 3.12.
- **Fail:** any error or warning.
- **Verified by:** NEW: CI job.
- **Status:** Known defect (5-verif §0.6: `PERFBDGEN` passes
  `--trace-freq`, which the scripts reject; `test.py -j` uses the Linux-only
  `os.sched_getaffinity`; invalid escape sequences; `test_compat.toml`
  names a runner flag that does not exist).
- **When:** every CI run.

#### LFS3-CI-09

littlefs shall have test cases that are themselves free of undefined
behaviour.

- **Source:** Derived: a test with undefined behaviour can pass or fail for
  reasons unrelated to littlefs.
- **Measure:** sanitizer reports attributed to test code.
- **Pass:** the sanitizer job of LFS3-CI-04 reports nothing in
  `tests/*.toml` code, leaks included.
- **Fail:** any report.
- **Verified by:** job `test-sanitize` (`make test-sanitize`).
- **Status:** Tested on `v3-integration` (41165ec9): no report in B-DEF
  or B-BIG (see LFS3-GEN-03). F-4 to F-8 are fixed; LeakSanitizer found
  cases in `test_rbyd`, `test_btree`, `test_kv`, `test_mount` and
  `test_ck` that never freed what they allocated, fixed in 6fad1a62.
- **When:** every CI run.

#### LFS3-CI-10

littlefs shall track every test permutation excluded because of a known
bug with an issue referenced next to the exclusion.

- **Source:** Proposal. `tests/test_dirs.toml:3442` excludes a failing
  permutation with a comment only (LFS3-PL-27).
- **Measure:** `if` and `ifndef` conditions that mention a bug.
- **Pass:** each such condition cites an open issue.
- **Fail:** an exclusion without an issue.
- **Verified by:** review.
- **Status:** Untested.
- **When:** before v3-beta.

#### LFS3-CI-11

littlefs shall run the test suite at every geometry of G-ALL on a
schedule.

- **Source:** Derived: v2 ran 5 geometries by default; v3 runs one
  (5-verif §1.4).
- **Measure:** scheduled job results.
- **Pass:** a nightly job runs `make test` with each G-ALL geometry and with
  `ERASE_VALUE` in {0xff, 0x00, -1}, in B-DEF and B-BIG, and all pass.
- **Fail:** any part missing or failing.
- **Verified by:** NEW workflow.
- **Status:** Untested (no such job).
- **When:** nightly.

#### LFS3-CI-12

littlefs shall run, on every push and pull request, each check that lives
outside `make test`: `make test-rdonly`, `make test-compat-gbmap`,
`make test-nomalloc`, `make test-progonce`, `make test-threadsafe`, and the
error-code check of LFS3-ERR-01.

- **Source:** Derived (issue #17): LFS3-BUILD-19, BAD-05, BAD-07, ERR-01,
  ERR-06 and the cases that need the prog-once check (TEST_PLAN.md E-1) are
  checked only by these targets, and a target CI doesn't run breaks
  unnoticed. On fd3157e3 `make test-compat-gbmap` and `make test-progonce`
  failed to build with GCC and `-Werror` (`-Wtype-limits` in the ck retry
  loops without `LFS3_GBMAP`), which clang doesn't warn about.
- **Measure:** jobs in `.github/workflows/test.yml`.
- **Pass:** a job runs each target with `CFLAGS=-Werror` and GCC on x86_64
  and passes; the error-code job checks the recordings of the test,
  test-biggest, test-yes-gbmap and test-threadsafe jobs; each job's
  commands pass in the `lfs3-ci` Docker image (Ubuntu 24.04, GCC 13).
- **Fail:** a target without a job, or a failing job.
- **Verified by:** `.github/workflows/test.yml`.
- **Status:** Untested at fd3157e3, where only `make test-rdonly` had a
  job. On `v3-integration` (345c40f7, 1c768762) every target has a job and
  passes in the `lfs3-ci` image; not yet run on GitHub. `make
  test-threadsafe` has a job on `v3-r8` (`ecf44d33`).
- **When:** every CI run.

#### LFS3-CI-13

littlefs's workflows shall use only actions that run on a Node.js runtime
GitHub still supports.

- **Source:** Derived (issue #17): GitHub deprecated the Node 20 runtime for
  actions; `actions/checkout@v4`, `actions/upload-artifact@v4` and
  `actions/download-artifact@v4` run on it.
- **Measure:** `runs.using` in the `action.yml` of each version named by a
  `uses:` in `.github/workflows`.
- **Pass:** every one is `node24`.
- **Fail:** any `node20` or older.
- **Verified by:** review, with `gh api
  repos/actions/<name>/contents/action.yml?ref=<version>`.
- **Status:** Known defect at fd3157e3, where all three actions were at v4
  (node20); fixed on `v3-integration` (d32274b9): `actions/checkout@v5`,
  `actions/upload-artifact@v6`, `actions/download-artifact@v7`.
- **When:** every CI run.

### 6.22 Documentation (DOC)

#### LFS3-DOC-01

littlefs shall have a DESIGN.md that explains the v3 design.

- **Source:** Stated: #1114 (2026-04-22), release blocker #2: "IMO it would
  be foolish to commit to a disk format without at least these two
  documents".
- **Measure:** the document and its review.
- **Pass:** DESIGN.md describes v3 (rbyds, B-trees, the mtree, gstate and
  gcksums, the sync model, allocation, the gbmap and pre-erase, and
  bad-block tracking) and has been open for review before v3-beta.
- **Fail:** DESIGN.md still describes v2 at v3-beta.
- **Verified by:** review.
- **Status:** Not implemented (planned).
- **When:** before v3-beta.

#### LFS3-DOC-02

littlefs shall have a SPEC.md that specifies the v3 on-disk format to the
byte.

- **Source:** Stated: #1114 (2026-04-22), release blocker #2.
- **Measure:** whether an independent reader can be written from SPEC.md
  alone.
- **Pass:** SPEC.md defines every on-disk tag of `lfs3.h:814-905`, every
  encoding of `lfs3.h:957-1080`, the commit and checksum rules (including the
  canonical checksum that gcksum uses), the compat flags, the defaults for
  missing configuration tags, and the gstate encodings; and a reader written
  from it (for example an update of `scripts/dbglfs3.py`) decodes every image
  the test suite writes.
- **Fail:** any of these is missing, or the reader disagrees with littlefs.
- **Verified by:** review; NEW: SPEC-based reader cross-check.
- **Status:** Not implemented (planned).
- **When:** before v3-beta.

#### LFS3-DOC-03

littlefs shall have a README whose example uses the v3 API and compiles.

- **Source:** Stated: #1111 TODO "Document, document, document". v2 CI
  compiles the README example.
- **Measure:** CI compile of the README example.
- **Pass:** the example uses `lfs3_*` calls and compiles in CI.
- **Fail:** the example is v2, or does not compile.
- **Verified by:** NEW: CI job.
- **Status:** Not implemented (planned).
- **When:** before v3-beta.

#### LFS3-DOC-04

littlefs shall state, next to `gc_preerase_count` in `lfs3.h`, that
pre-erase needs `LFS3_GBMAP`, `LFS3_REVPERTURB` and a mount with
`LFS3_M_REVPERTURB`.

- **Source:** Derived: without the mount flag, gc with pre-erase asserts,
  and a mount without it does not use pre-erased blocks (LFS3-PRE-04).
- **Measure:** the header text.
- **Pass:** the comment names all three.
- **Fail:** any is missing.
- **Verified by:** review.
- **Status:** Known defect (D-1: the comment at `lfs3.h:608-620` names only
  the gbmap).
- **When:** before v3-beta.

#### LFS3-DOC-05

littlefs shall describe `LFS3_M_REVPERTURB` and `LFS3_F_REVPERTURB` by what
they do.

- **Source:** Derived: the flag is required for pre-erase safety.
- **Measure:** the header text.
- **Pass:** the comments at `lfs3.h:185-187` and `248-250` say that the
  first bit of each revision count is perturbed so that pre-erased blocks can
  be used.
- **Fail:** the comment says "Add debug info to revision counts".
- **Verified by:** review.
- **Status:** Known defect (3-alloc R8).
- **When:** before v3-beta.

#### LFS3-DOC-06

littlefs shall name, in the comment for `lfs3_fs_unck`, only functions and
flags that exist.

- **Source:** Derived: `lfs3.h:1801-1811`.
- **Measure:** the header text.
- **Pass:** the comment refers to `lfs3_fs_unck`, `LFS3_I_CKMETA` and
  `LFS3_I_CKDATA`.
- **Fail:** it names `lfs3_gc_unck` or `LFS3_I_CANCKMETA`.
- **Verified by:** review.
- **Status:** Known defect (4-api R23).
- **When:** before v3-beta.

#### LFS3-DOC-07

littlefs shall document that `lfs3_fs_mkgbmap` returns `LFS3_ERR_EXIST`
and `lfs3_fs_rmgbmap` returns `LFS3_ERR_NOENT` when there is nothing to do.

- **Source:** Stated: commit `ad2e8b3` changed the behaviour;
  `lfs3.h:1825-1839` still says "Does nothing".
- **Measure:** the header text.
- **Pass:** the comments give the error codes.
- **Fail:** they say "Does nothing".
- **Verified by:** review.
- **Status:** Known defect (3-alloc R8).
- **When:** before v3-beta.

#### LFS3-DOC-08

littlefs shall list, in the comment for `lfs3_info.type`, every type
`lfs3_stat` and `lfs3_dir_read` can return.

- **Source:** Derived: `LFS3_TYPE_STICKYNOTE` and `LFS3_TYPE_UNKNOWN` are
  returned (LFS3-SYNC-13, LFS3-DIR-17).
- **Measure:** the header text.
- **Pass:** the comment at `lfs3.h:721` lists REG, DIR, STICKYNOTE and
  UNKNOWN, and says a STICKYNOTE is a file created by a handle that is
  still open and not yet synced, with size 0. Reporting such a file as REG
  would hide from the application that it is not durable yet: a power loss
  removes it (LFS3-SYNC-13).
- **Fail:** it says "either LFS3_TYPE_REG or LFS3_TYPE_DIR".
- **Verified by:** review.
- **Status:** Known defect (4-api R23); fixed on `v3-integration`
  (170f0110, issue #12).
- **When:** before v3-beta.

#### LFS3-DOC-09

littlefs shall say, in the comment for `gc_lookgbmap_thresh`, that it
controls repopulation of the gbmap.

- **Source:** Derived: `lfs3.h:593-606`.
- **Measure:** the header text.
- **Pass:** the last sentence speaks of the gbmap.
- **Fail:** it says "repopulates the lookahead buffer".
- **Verified by:** review.
- **Status:** Known defect (3-alloc R8).
- **When:** before v3-beta.

#### LFS3-DOC-10

littlefs shall state the default of `gc_compact_thresh` as the value the
code uses.

- **Source:** Derived: `lfs3.h:622-634`; the code uses
  `block_size - block_size/8`.
- **Measure:** the header text.
- **Pass:** the comment says 87.5% or `block_size - block_size/8`.
- **Fail:** it says "~88%".
- **Verified by:** review.
- **Status:** Known defect (4-api R23).
- **When:** before v3-beta.

#### LFS3-DOC-11

littlefs shall document what `LFS3_ERR_CORRUPT` from the `read` callback
means to littlefs.

- **Source:** Derived: littlefs treats a read `LFS3_ERR_CORRUPT` specially
  (fallback to the other block of a pair, end of log at fetch, revision 0 in
  allocation; 3-alloc F7), but `lfs3.h:468-471` documents it only for `prog`
  and `erase`.
- **Measure:** the header text.
- **Pass:** the `read` comment states the meaning.
- **Fail:** it does not.
- **Verified by:** review.
- **Status:** Known defect (3-alloc R8).
- **When:** before v3-beta.

#### LFS3-DOC-12

littlefs shall document the assumptions it makes about the storage
hardware.

- **Source:** Derived: several guarantees rest on them (3-alloc B17, R5, R9;
  1-meta §3 P2).
- **Measure:** DESIGN.md or `lfs3.h`.
- **Pass:** the documentation states at least: blocks 0 and 1 (and block 2
  when formatting with the gbmap) must remain programmable, because the
  anchor cannot move; an interrupted prog changes no bytes outside that
  prog, which is at most `pcache_size` bytes rounded up to `prog_size`
  (Q19); progs within a block
  are issued in increasing offset order; read errors are persistent; the
  erased value is not assumed; and, without `LFS3_M_CKPROGS`, a prog that
  silently fails is indistinguishable from a power loss.
- **Fail:** any item is missing.
- **Verified by:** review.
- **Status:** Not implemented (planned).
- **When:** before v3-beta.

#### LFS3-DOC-13

littlefs shall document the limits of its error detection.

- **Source:** Stated: `tests/test_ck.toml:615-623` (rollback "a fundamental
  issue for any filesystem with logs"); #1111 "Global-checksums".
- **Measure:** DESIGN.md or `lfs3.h`.
- **Pass:** the documentation states that a rollback of the most recent
  commit, or of the whole image, is detectable only with a checksum kept
  outside the device (`lfs3_fs_cksum`); that a gcksum mismatch makes a
  read-write mount fail, and a read-only one degraded (LFS3-DEG-03); that
  a change after mount is found only
  by the next check; and that data blocks are verified only with
  `LFS3_M_CKFETCHES`, `LFS3_M_CKDATACKSUMS` or a ckdata check; and that a
  flipped bit read from the source of a copy (mdir compaction, B-tree node
  compaction, crystallization) is copied under a fresh checksum unless the
  check option that covers the source is enabled (LFS3-INT-24).
- **Fail:** any item is missing.
- **Verified by:** review.
- **Status:** Not implemented (planned).
- **When:** before v3-beta.

#### LFS3-DOC-14

littlefs shall document the maximum attribute size and the reserved
attribute types consistently in `lfs3.h` and the design documents.

- **Source:** Stated: #1111 "Standard custom attributes" (0x00-0x7f user,
  0x80-0xbf reserved standard, 0xc0-0xff system); `lfs3.h:766-768` says
  0x80-0xff "May be assigned a standard attribute".
- **Measure:** the header and document text.
- **Pass:** both give the same ranges and the maximum size of
  LFS3-ATTR-05.
- **Fail:** they differ, or the size is missing.
- **Verified by:** review.
- **Status:** Known defect (4-api §1.7; see open question Q9).
- **When:** before v3-beta.

#### LFS3-DOC-15

littlefs shall document the largest file size and block count that the
test suite verifies.

- **Source:** Stated: #1114 (2025-06-23), answering a request to "test and
  document ... maximum volume size and file size": "Noted."
- **Measure:** the documentation.
- **Pass:** the documentation states the verified limits (LFS3-FILE-17,
  LFS3-RES-07) and the theoretical ones.
- **Fail:** they are missing.
- **Verified by:** review.
- **Status:** Not implemented (planned).
- **When:** before v3-beta.

#### LFS3-DOC-16

littlefs shall document which API misuses are checked only by
`LFS3_ASSERT` and so are undefined with `LFS3_NO_ASSERT`.

- **Source:** Derived: preconditions are asserts (4-api §1.1, R12).
- **Measure:** `lfs3.h`.
- **Pass:** each function's comment, or one general section, lists the
  asserted preconditions (open handle, known flags, no write flags on a
  read-only mount, `LFS3_O_MODE` not 3, no shrinking in `lfs3_fs_grow`, no
  open handles at unmount, valid configuration).
- **Fail:** any is missing.
- **Verified by:** review.
- **Status:** Not implemented (planned).
- **When:** before v3-beta.

#### LFS3-DOC-17

littlefs shall document that `LFS3_T_PREERASE` has no effect on a
traversal, or remove the flag.

- **Source:** Stated: commit `843412c` (pre-erase lives only in
  `lfs3_fs_gc`); `lfs3.h:369-371` describes the flag as "Try to pre-erase
  free blocks".
- **Measure:** the header text and behaviour.
- **Pass:** the comment matches the behaviour.
- **Fail:** the comment promises pre-erase.
- **Verified by:** review.
- **Status:** Known defect (3-alloc R8).
- **When:** before v3-beta.

#### LFS3-DOC-18

littlefs shall document the costs a user can observe outside the calls that
cause them.

- **Source:** Derived: 1-meta §7.15; 2-files §1.5, §1.8; #1114
  (2026-03-12) on `lfs3_file_fruncate` against rename rotation.
- **Measure:** DESIGN.md or `lfs3.h`.
- **Pass:** the documentation states that mount reads every metadata pair;
  that the first write after mount scans for orphans unless
  `LFS3_M_MKCONSISTENT` was given; that reads on a writable handle may
  flush and so may write and fail; that the erased state of partially
  written data blocks is not kept across close or remount; and that a
  write-then-fruncate log costs two commits against one for rename
  rotation.
- **Fail:** any item is missing.
- **Verified by:** review.
- **Status:** Not implemented (planned).
- **When:** before v3-beta.

#### LFS3-DOC-19

littlefs shall document every compile-time option listed in 5.2 and in
LFS3-BUILD-01 to BUILD-20.

- **Source:** Derived: options such as `LFS3_YES_FLUSH` and `LFS3_YES_SYNC`
  are used in `lfs3.c` but described nowhere (4-api §4.2).
- **Measure:** `lfs3_util.h` or the README.
- **Pass:** each option has a description.
- **Fail:** any option is undocumented.
- **Verified by:** review.
- **Status:** Not implemented (planned).
- **When:** before v3-beta.

#### LFS3-DOC-20

littlefs shall provide debug scripts (`scripts/dbg*.py`) that decode every
image the driver writes, and that reject every commit, node and image the
driver rejects.

- **Source:** Derived (issue #13). People debug images with these scripts,
  and a script that accepts what the driver rejects hides corruption. A
  decoder written from SPEC.md found the scripts disagreeing with `lfs3.c`,
  and the C right, on the VERSION encoding, CKSUM phase bits and size,
  leb128 limits, the low redundancy bits of struct and magic tags, the
  number of mptr blocks, unvalidated commits when fetching at a trunk, the
  shrub bit on found leaves, and a BLOCK "e" bit the C doesn't have.
- **Measure:** `make test-dbg` (`scripts/test_dbg.py`): images written by
  B-DEF and B-YGB runners, and checksum-valid variants of them that break
  one rule each.
- **Pass:** every script decodes every image with `-e` and exits 0; every
  variant is rejected (`-e` exits 2) or shown the way the driver sees it
  (a commit the driver drops is not shown); an incompatible VERSION and a
  one-block mptr are rejected.
- **Fail:** any check fails.
- **Verified by:** `make test-dbg` (NEW-132).
- **Status:** Tested on `v3-integration` (cc19a932): `make test-dbg`
  passes 537 of 537 checks. The scripts at 3c0afc90 passed 248, the valid
  images and the bit-7 commits they already kept (issue #13).
- **When:** every CI run.

### 6.23 Error handling for unattended systems (ERR)

littlefs usually runs without a human to read an error. These requirements
make every error actionable by firmware: each code maps to one recommended
action (Retry, Rebuild or Fail), and the state after the error is stated, so
the action is safe. Tracked in issue #20 of the fork. The table, the state
after each call and the contingencies for Fail are in
[ERRORS.md](ERRORS.md).

#### LFS3-ERR-01

littlefs shall document, for every public function in `lfs3.h`, every error
code the function can return.

- **Source:** Proposal (issue #20). Unattended firmware must handle every
  code it can receive; at `b10efaa` `lfs3.h` says only "a negative error
  code" for most functions.
- **Measure:** the paragraph that starts "Returns" at the end of each
  function's comment in `lfs3.h`, against the (function, code) pairs the
  test runner records from every public call a test makes, with `TEST_ERRS`
  set (`runners/test_errs.h`), across the suites with fault injection
  (`lfs3_emubd_mkioerror`, bad blocks, NOSPC, NOMEM, power loss).
- **Pass:** `scripts/ckerrs.py` finds a "Returns" paragraph for every
  function, a hook for every function, and every recorded code in its
  function's paragraph, over the recordings of `make test` in B-DEF, B-BIG
  and B-YGB, and of `make test-threadsafe` in B-TS, where the paragraph
  must also name the `lock` and `unlock` callbacks (LFS3-THR-02).
- **Fail:** a function without the paragraph or the hook, or a recorded code
  its function doesn't list.
- **Verified by:** `scripts/ckerrs.py` over the recordings of the CI jobs
  test, test-biggest, test-yes-gbmap and test-threadsafe (`make test-errs`
  locally).
- **Status:** Not implemented at `b10efaa`; tested on `v3-integration`
  (14f90293, 9c9e8145): over the B-DEF, B-YGB and B-BIG suites the runner
  records 135 (function, code) pairs and every one is listed. Before the
  lists, the check found 85 undocumented pairs in the default suite. On
  `v3-r8` (`ecf44d33`) every paragraph names `lock` and `unlock`, and the
  B-TS recordings, 215 pairs, are listed too.
- **When:** every CI run.

#### LFS3-ERR-02

littlefs shall document one recommended application action for every error
code: Retry, Rebuild or Fail.

- **Source:** Proposal (issue #20). Retry: the same call can succeed later.
  Rebuild: repair or reclaim something, then retry. Fail: stop using this
  path and take the application's contingency.
- **Measure:** the documentation.
- **Pass:** a table maps each `LFS3_ERR_*`, and codes from the block
  device, to one action, with its conditions (e.g. a bound on retries); the
  contingencies for Fail are listed.
- **Fail:** a code without an action, or with an action that depends on
  which call returned it.
- **Verified by:** review of [ERRORS.md](ERRORS.md).
- **Status:** Not implemented at `b10efaa`; documented on `v3-integration`
  (40b9e33e). The root keeps `LFS3_ERR_BUSY` (Q25, decided).
- **When:** before v3-beta.

#### LFS3-ERR-03

littlefs shall give each error code one meaning, independent of the call that
returned it.

- **Source:** Proposal. Firmware dispatches on the code; a code whose meaning
  depends on context needs per-call handling and invites mistakes. A handle
  torn by an error needs its own code: none of the existing ones means "this
  handle, not the disk, is unusable until resynced". `LFS3_ERR_INVAL` means
  a caller bug, `LFS3_ERR_CORRUPT` data on disk that failed a check, and
  `LFS3_ERR_BUSY` a target in use. It takes `LFS3_ERR_BADFD`, -77, `EBADFD`
  ("file descriptor in bad state") in the Linux numbering the other codes
  follow.
- **Measure:** the documented meaning of each code, and the codes returned
  in the cases that had a second meaning.
- **Pass:** no function documents a meaning for a code that differs from the
  table of LFS3-ERR-02; `badblocks::graft_torn`: `lfs3_file_sync` of a torn
  handle returns `LFS3_ERR_BADFD`; `mount::no_geometry`: mounting an mroot
  without a geometry tag returns `LFS3_ERR_CORRUPT`.
- **Fail:** a code with two meanings.
- **Verified by:** review; `badblocks::graft_torn`, `mount::no_geometry`.
- **Status:** Known defect on `v3-integration` (fd3157e3): `lfs3_file_sync`
  returned `LFS3_ERR_INVAL` for a handle torn by a failed multi-commit write
  (until `lfs3_file_resync`), and `lfs3_mount` returned `LFS3_ERR_INVAL`
  for an mroot without a geometry tag, while `LFS3_ERR_INVAL` otherwise
  means a caller bug. Fixed and tested on `v3-integration` (d4211ea5,
  e9aeb8b2).
  Appends that replace only the file's last entry no longer tear
  (LFS3-SYNC-20).
- **When:** before v3-beta.

#### LFS3-ERR-04

littlefs shall document the state on disk and in RAM after each error, and
the state shall be the one documented.

- **Source:** Proposal. A retry or rebuild is only safe if the caller knows
  what happened.
- **Measure:** for each class of call, the state after an injected error:
  what lists, reads and `lfs3_fs_cksum` show in the same mount and after a
  remount, handle flags, positions, and device counters.
- **Pass:** each state in ERRORS.md holds, in B-DEF, B-BIG and B-YGB:
  - reads (`lfs3_stat`, `lfs3_get`, `lfs3_getattr`, `lfs3_file_read`,
    `lfs3_dir_read`, `lfs3_trv_read`) change nothing, the file and directory
    positions don't move, and calling again returns what an undisturbed call
    returns (`errs::ioerror`);
  - metadata operations (`lfs3_mkdir`, `lfs3_remove`, `lfs3_rename`,
    `lfs3_setattr`, `lfs3_removeattr`, `lfs3_set`, creating a file) did not
    happen, in RAM or on disk, except after an error from the sync callback:
    then they may have happened, alike in RAM and on disk, the error is
    returned once, and calling again finds them done (`LFS3_ERR_EXIST`,
    `LFS3_ERR_NOENT`, `LFS3_ERR_NOATTR`) or does them; `lfs3_remove` of a
    directory and `lfs3_rename` return 0 when only their cleanup fails,
    leaving `LFS3_I_MKCONSISTENT` set (`errs::ioerror`,
    `badblocks::ioerror`, `badblocks::badsync`, `alloc::nospc_*`);
  - file writes (`lfs3_file_write`, `flush`, `sync`, `truncate`,
    `fruncate`) leave the handle desynchronized and the file on disk as of
    its last successful sync, or, after an error from the sync callback in
    `lfs3_file_sync`, possibly as of that sync; `lfs3_file_close` then
    writes nothing and returns 0 (`errs::ioerror`,
    `badblocks::truncate_desync`, `files::close_error`);
  - mount and format: the filesystem is not mounted; a mount without
    mount-time work programs and erases nothing; a later format succeeds
    (`errs::ioerror`, `mount::readerror`, `mount::fail_nowrite`);
  - janitorial calls (`lfs3_fs_mkconsistent`, `lfs3_fs_ck`, `lfs3_fs_gc`,
    `lfs3_fs_grow`, `lfs3_fs_mkgbmap`, `lfs3_fs_rmgbmap`, `lfs3_fs_mkbad`,
    `lfs3_fs_mkgood`) leave the filesystem consistent with its contents
    unchanged, and calling again finishes the work; `lfs3_fs_mkbad` and
    `lfs3_fs_mkgood` may leave their mark in RAM only, for the next commit
    to write (`errs::ioerror`);
  - `lfs3_file_close` releases the handle on any error, and NOMEM leaves
    nothing allocated (`files::close_error`, `files::open_nomem`,
    `mount::nomem`);
  - a torn handle: LFS3-ERR-07.
- **Fail:** an observed state the documentation doesn't describe.
- **Verified by:** the cases above, and `errs::ioerror`.
- **Status:** Partly tested at `b10efaa`, where the states were documented
  for sync and close only; documented in ERRORS.md and tested on
  `v3-integration` (3c7a175e, cb80d770, 40b9e33e).
- **When:** every CI run.

#### LFS3-ERR-05

littlefs shall return `LFS3_ERR_IO` for device failures that may be
transient, and `LFS3_ERR_CORRUPT` only for data that failed a check or that
the device reported bad.

- **Source:** Proposal. IO maps to Retry and CORRUPT to Rebuild. Reads can
  fail while the supply is low, so the block device must be able to say
  "try later" without littlefs treating the data as bad (issues #6, #19).
- **Measure:** the code returned for an injected `LFS3_ERR_IO` and an
  injected `LFS3_ERR_CORRUPT` from each callback; device operations after
  an injected IO; the bad and suspect lists.
- **Pass:** `errs::ioerror`: for every class of call (format, mount, reads,
  metadata operations, file writes, janitorial calls, gbmap calls) and each
  read, prog, erase and sync of the call failing in turn with
  `LFS3_ERR_IO`, the call returns `LFS3_ERR_IO`; after a failed read, prog
  or erase it reads, programs and erases nothing more; and
  `lfs3_fs_nextbad` and `lfs3_fs_nextsuspect` list nothing, before or after
  a remount, in B-DEF, B-BIG and B-YGB. The one documented exception:
  `lfs3_remove` and `lfs3_rename` return 0 when only the cleanup after
  their commit fails; the cleanup stays pending (`LFS3_I_MKCONSISTENT`) and
  the next write retries it (Q26, decided). An injected CORRUPT from a
  read surfaces as CORRUPT, is read again up to `ck_retries` times, or
  degrades a read-only mount (LFS3-DEG-03), and never makes a metadata
  pair fall back to its older block (LFS3-INT-26); `mount::readerror`
  checks both codes.
- **Fail:** IO turned into CORRUPT, an IO swallowed, or a read of another
  copy, a relocation or a bad or suspect mark after an IO; a CORRUPT read
  turned into another code, or into an older state.
- **Verified by:** `mount::readerror` and `errs::ioerror` (IO);
  `mount::readerror`, `mount::readerror_mounted` (CORRUPT, LFS3-INT-26).
- **Status:** Partly tested. The IO half holds on `v3-integration` without
  a change to the code, tested by `errs::ioerror` (3c7a175e); the CORRUPT
  half is issue #6 (LFS3-INT-26). At `6f80e646` a read error in a
  configuration's name or file limit at mount returned
  `LFS3_ERR_NOTSUP`, as if the limit were too large. The CORRUPT half
  tested on `v3-r21` (`862e3695`).
- **When:** every CI run.

#### LFS3-ERR-06

littlefs shall never return `LFS3_ERR_RANGE`, `LFS3_ERR_UNKNOWN` or any
internal code from a public function.

- **Source:** Derived. These codes have no recommended action.
- **Measure:** codes returned under fault injection.
- **Pass:** `scripts/ckerrs.py` finds none of them, and no code outside
  `enum lfs3_err`, in the recordings of LFS3-ERR-01; no function documents
  them.
- **Fail:** any observation.
- **Verified by:** the hook and check of LFS3-ERR-01.
- **Status:** Known defect at `b10efaa` (RANGE reached the API from
  oversized commits; fixed on `v3-integration`, d428d7b8, 42e26e7e).
  Tested as a general property on `v3-integration` (9c9e8145). D-9
  (issue #25) is fixed on `v3-integration` (e38b42ae): a B-tree split
  that leaves its commit no room now splits again at the commit or
  returns `LFS3_ERR_NOSPC`, where it returned `LFS3_ERR_RANGE` behind an
  assert, and with `LFS3_NO_ASSERT` `lfs3_btree_commit` took the RANGE for
  a full root and tried a new root on block after block through the
  whole disk.
- **When:** every CI run.

#### LFS3-ERR-07

littlefs shall refuse, with `LFS3_ERR_BADFD`, every call that reads, writes,
seeks in or sizes a file handle that an error left matching no version of
its file (a torn handle), until `lfs3_file_resync`; `lfs3_file_close` shall
release such a handle without writing.

- **Source:** Proposal (issue #20; principles: never serve, or build on,
  data that no version of the file held). A write, truncate or fruncate
  spanning several entries commits them one at a time, and an error between
  them tears the handle (96883f8e). On fd3157e3, `lfs3_file_sync` refused
  such a handle, but `lfs3_file_read` returned 16256 bytes of a 16384-byte
  file matching neither its old nor its new contents, and `lfs3_file_size`
  reported 16256.
- **Measure:** results of `lfs3_file_read`, `lfs3_file_write`,
  `lfs3_file_flush`, `lfs3_file_sync`, `lfs3_file_truncate`,
  `lfs3_file_fruncate`, `lfs3_file_seek`, `lfs3_file_size` and
  `lfs3_file_ck` on a torn handle; emubd prog and erase counters; the file
  after `lfs3_file_resync` and after a remount.
- **Pass:** `badblocks::graft_torn`: on each torn handle every call above
  returns `LFS3_ERR_BADFD` and programs and erases nothing;
  `lfs3_file_tell`, `lfs3_file_rewind` and `lfs3_file_desync` still work;
  `lfs3_file_resync` returns 0 and the handle reads the file's synced
  contents; `lfs3_file_close` of a torn handle returns 0 and writes
  nothing.
- **Fail:** any of the calls returns data, a size or a position, or
  writes, or resync doesn't recover the handle.
- **Verified by:** `badblocks::graft_torn`.
- **Status:** Known defect on `v3-integration` (fd3157e3), see Source;
  fixed and tested on `v3-integration` (d4211ea5).
- **When:** every CI run.

### 6.24 Graceful degradation (DEG)

The flash is usually soldered into the product. Replacing it means replacing
the unit, and the data on it is lost with the old unit; units can also be
hard to reach. So littlefs must lose as little as possible as the flash
wears out or fails in places, keep the rest usable, and tell the application
enough to keep operating on a best-effort basis.

#### LFS3-DEG-01

littlefs shall keep every file as of its last completed sync after any power
loss, and shall re-establish the last writes of a write session that a power
loss interrupted from bytes that pass their checksum before it builds new
state on them. In the default mode this holds except in the residual cases
of LFS3-DEG-12; with `LFS3_M_SETTLE` (LFS3-DEG-13) it holds except for a
power loss during the mount-time repair itself.

- **Source:** Proposal (issue #1). Data in flight at a power loss may be lost;
  synced data may not. An interrupted program can leave cells that read one
  way at one mount and the other way at the next: a commit read as whole is
  built on, and when it later reads as torn everything after it in its
  block goes with it, or a pointer to a relocated pair reverts and every
  commit since is lost with no error.
- **Measure:** completed syncs after each power loss, under every emubd
  power-loss behaviour, METASTABLE (persistent until erase) included.
- **Pass:** `powerloss::metastable` (NEW-08) and
  `powerloss::metastable_builton` (NEW-130) lose no completed sync with
  `LFS3_M_SETTLE`, and in the default mode lose none outside the residual
  cases, which they count and print; the behaviour-4 permutations of NEW-05,
  NEW-06, NEW-11 and `dirs::rm_many_2layers`, mounted with `LFS3_M_SETTLE`,
  pass criterion A.
- **Fail:** a completed sync missing after a remount, with or without an
  error, outside the residual cases of the mode.
- **Verified by:** NEW-08, NEW-130, NEW-05, NEW-06, NEW-11,
  `dirs::rm_many_2layers`.
- **Status:** Tested on `v3-rc` (`a12705da`): with `LFS3_M_SETTLE` NEW-08 loses
  no completed sync in 14,626 power losses and NEW-130 none; by default
  NEW-08 counts 55 residual losses in 11,832 power losses and NEW-130
  loses `lfs3_set`'s commit in 3 of 8 seeds, both residual cases.
- **When:** every CI run (behaviours 0-5), nightly with permute(1).

#### LFS3-DEG-02

littlefs shall confine damage to a file's data blocks or B-tree nodes to
that file: reads that need the damage return `LFS3_ERR_CORRUPT`, every
other file stays readable, and writable as far as LFS3-DEG-04 allows, and
the damaged file can be removed without reading the damage, which leaves
its blocks unreferenced.

- **Source:** Proposal. A bad block in one file must not cost the others.
  A scan for free blocks can't see past a B-tree node it can't read, so
  writes that need one wait for the damaged file's removal
  (LFS3-DEG-04).
- **Measure:** reads and writes of the other files, `lfs3_remove` of the
  damaged file, and a traversal after it, once a data block or the
  B-tree root of one file fails every read.
- **Pass:** `badblocks::confined_data`: the file's reads return
  `LFS3_ERR_CORRUPT` (opening it may, with a damaged B-tree root); every
  other file reads as written and can be rewritten, except, with a
  damaged B-tree node and no gbmap, writes that need a scan, which return
  `LFS3_ERR_CORRUPT` until the file is removed; `lfs3_remove` returns 0,
  no traversal finds the file's blocks after it, then every file writes
  and `lfs3_fs_ck` returns 0, also after a remount. In B-DEF, B-YGB and
  B-BIG.
- **Fail:** an error on another file beyond the above, wrong data, or a
  damaged file that can't be removed or still references blocks.
- **Verified by:** `badblocks::confined_data` (NEW-143).
- **Status:** Partly tested at `6f80e646` (reads return CORRUPT; removal
  of a damaged file was untested). Tested on `v3-r21` (`dced877c`),
  without a change to the code.
- **When:** every CI run.

#### LFS3-DEG-03

littlefs shall mount read-only, in a degraded mode, a filesystem with a
damaged metadata pair in the mtree, or whose global checksum or global
state doesn't check: `lfs3_mount` with `LFS3_M_RDONLY` and no check flags
shall succeed and report `LFS3_I_DEGRADED`, every file in an undamaged
pair shall read as stored, and operations that need a damaged pair shall
return `LFS3_ERR_CORRUPT`. A read-write mount, or a mount with
`LFS3_M_CKMETA` or `LFS3_M_CKDATA`, of such a filesystem shall return
`LFS3_ERR_CORRUPT`.

- **Source:** Proposal (issue #21; principle 2). At `b10efaa` one
  unreadable mdir or a global checksum mismatch makes the whole volume
  unmountable (4-integrity R11; the mount code has TODOs for a degraded
  mode). For an unreachable unit that is total loss. Decided from the
  principles, Q6 (b): automatic for read-only mounts, reported by a flag,
  not an opt-in mount flag. A read-only mount writes nothing, so serving
  what reads risks nothing, and firmware that doesn't know the flag still
  keeps its undamaged files, where an opt-in flag would leave it with
  none. A read-write mount can't degrade: a damaged pair takes its share
  of the gcksum and of the global state (grm, gbmap) with it, so every
  commit would build on wrong deltas, and the next mount would find a
  mismatch even once the pair reads again; turning a read-write mount
  read-only would make the next write assert (Q2). Check flags ask for a
  verdict on the whole filesystem, and the damage is it.
- **Measure:** mount results, `lfs3_fs_stat` flags and reads after making
  one mdir of the mtree unreadable (read errors), unchecked (erased) or
  rolled back (an older copy, so the gcksum doesn't check), first the mdir
  with the root's bookmark, then the last.
- **Pass:** `mount::degraded`: `lfs3_mount` read-write, and read-only
  with `LFS3_M_CKMETA`, return `LFS3_ERR_CORRUPT`; read-only returns 0
  with `LFS3_I_RDONLY` and `LFS3_I_DEGRADED`, and without `LFS3_I_GBMAP`;
  `lfs3_get` and `lfs3_stat` read every file outside the damaged pair as
  written, and return `LFS3_ERR_CORRUPT` inside it (a rolled-back pair
  reads as it was); `lfs3_dir_read` lists entries up to the damaged pair,
  then returns `LFS3_ERR_CORRUPT` again and again, and `lfs3_dir_open` of
  a directory whose bookmark is in it returns `LFS3_ERR_CORRUPT`;
  `lfs3_fs_cksum` and `lfs3_fs_ck` return `LFS3_ERR_CORRUPT`;
  `lfs3_fs_nextbad` lists nothing; once the pair reads again a read-write
  mount finds everything and checks. `mount::fail_nowrite` (a gcksum
  mismatch) and `ck::rollback` mount read-only degraded, read-write not;
  `mount::readerror` with `N` 64 (an mtree) mounts read-only degraded
  around an mdir whose read failed, and every file reads. In B-DEF, B-YGB
  and B-BIG.
- **Fail:** a read-only mount that fails over a damaged pair of the mtree,
  a file outside it unreadable or wrong, wrong data inside it without an
  error, or a read-write mount that succeeds.
- **Verified by:** `mount::degraded` (NEW-142), `mount::fail_nowrite`,
  `ck::rollback`, `mount::readerror` (NEW-140).
- **Status:** Not implemented at `6f80e646`; tested on `v3-r21`
  (`862e3695`).
- **Limits:** a pair one of whose blocks doesn't read is taken from the
  block that does, which may be the older, and the mount reports it; an
  mroot with no block that checks, or a damaged
  B-tree node of the mtree, still fails the mount, a lookup through a
  damaged node can't be confined to its subtree without checking every
  fetch; a degraded mount
  has no pending removes, so a file a power loss left half removed or
  renamed shows under its old name too, and no gbmap; a directory lists
  only up to its first damaged pair, the rest of its entries open by
  name.
- **When:** before v3-beta.

#### LFS3-DEG-04

littlefs shall keep allocating, for writes that don't need a damaged block,
while a block that references other blocks (an mdir, a B-tree node) can't
be read: from the blocks the gbmap knows are free, or, without it, those
the last lookahead scan found free; it shall never reuse a block the
damaged block may reference, and shall return `LFS3_ERR_CORRUPT` once a
write needs a scan.

- **Source:** Proposal. At `b10efaa` a lookahead scan or gbmap
  repopulation traverses the whole tree, so one unreadable mdir makes
  every write that needs a scan fail (3-alloc R7). A scan can't see what
  an unreadable block references, so it can't prove any block free that
  the allocator didn't already know to be (LFS3-FAIL-04), but what it
  knew stays true: a repopulation that meets the damage keeps the gbmap
  it has. Without the gbmap nothing is known free after a mount, so after
  a remount every write that allocates needs a scan; the gbmap keeps its
  known window on disk.
- **Measure:** rewrites of files outside an mtree mdir that stops reading
  while mounted; progs and wear of the blocks that mdir references.
- **Pass:** `badblocks::alloc_with_damage`: the rewrites succeed until the
  known free blocks run out, with the gbmap also after repopulations
  became due, then return `LFS3_ERR_CORRUPT`; no block the mdir
  references is programmed or erased; once it reads again, everything
  reads and checks. `badblocks::live_readerror` and
  `badblocks::confined_data` pass. In B-DEF, B-YGB and B-BIG.
- **Fail:** `LFS3_ERR_CORRUPT` from a write while the allocator knows a
  free block, any other error, or a write to a referenced block.
- **Verified by:** `badblocks::alloc_with_damage` (NEW-144),
  `badblocks::live_readerror`, `badblocks::confined_data`.
- **Status:** Known defect (3-alloc R7). At `6f80e646` a gbmap
  repopulation that met the damage failed the write that checkpointed
  the allocator, and every write after it. Fixed and tested on `v3-r21`
  (`320dc4a8`).
- **When:** every CI run.

#### LFS3-DEG-05

littlefs shall let the application remove files and attributes on a full or
worn filesystem. A removal shall never need more room in its metadata
block than the block already holds, so that it always makes progress.

- **Source:** Proposal. Reclaiming space is the application's main way to
  keep operating, and a removal that needs room a full disk or a full mroot
  does not have can never free that room (issue #23).
- **Measure:** `lfs3_remove` and `lfs3_removeattr` results on a full disk,
  with and without the gbmap, and with a full mroot, including an inlined
  mroot whose compaction alone fills its block.
- **Pass:** they return 0, and space is freed. `mtree::commit_too_big`
  passes with its fuzz seeds and with the 44 seeds of issue #23 (found at
  `ERASE_SIZE` 512, 1024 and 4096: 34, 8 and 2), at every block size,
  under `-Pnone` and `-Plinear`, in B-DEF, B-YGB and B-BIG.
- **Fail:** `LFS3_ERR_NOSPC` or an assert from a removal.
- **Verified by:** `mtree::commit_too_big`, `gbmap::nospc_remove`,
  `alloc::nospc_recover`.
- **Status:** Known defect at `b10efaa`; fixed and tested on
  `v3-integration` (eba40790, 60026203, f29b8985, 0535a265, 501eda31,
  819e1a10): `mtree::commit_too_big` passes in B-DEF, B-YGB and B-BIG and
  under ASan and UBSan in B-DEF, and with `-DSEED='range(4096)'` no
  removal fails in B-DEF (`-Pnone`, and `-Plinear` with atomic power
  loss), B-YGB and B-BIG (`-Pnone`). Before 819e1a10, 44 of those 12,288
  runs failed in B-DEF (D-8, issue #23): 42 with `LFS3_ERR_NOSPC` from
  `lfs3_removeattr("/")`, 1 with `LFS3_ERR_NOSPC` from `lfs3_remove`, and
  1 where an `lfs3_remove` tripped `LFS3_ASSERT(err != LFS3_ERR_RANGE)`
  in the mtree's B-tree split. One run still fails, in a mkdir (D-9).
- **When:** every CI run.

#### LFS3-DEG-06

littlefs shall shrink its capacity as blocks go bad, returning
`LFS3_ERR_NOSPC` only when no good free block remains.

- **Source:** Proposal, building on bad-block tracking (LFS3-BAD-*).
- **Measure:** operation results and `lfs3_fs_usage` as blocks fail.
- **Pass:** writes succeed while good free blocks remain; `lfs3_fs_usage`
  and `lfs3_fs_nextbad` account for every bad block; at end of life the
  error is `LFS3_ERR_NOSPC`, in B-YGB and B-BIG.
- **Fail:** an error other than NOSPC while good free blocks remain, or a bad
  block missing from the count.
- **Verified by:** `badblocks_gbmap::exhaustion`, `badblocks_gbmap::reading`,
  `badblocks_gbmap::overflow`, `exhaustion::spam_file_pl_fuzz`.
- **Status:** Not implemented at `b10efaa`; tested with the gbmap on
  `v3-integration` (9d6b2fd1). Without the gbmap a bad block is retried each
  time the allocator reaches it (LFS3-BAD-15), and capacity still ends in
  NOSPC (LFS3-FAIL-11).
- **When:** every CI run.

#### LFS3-DEG-07

littlefs shall stay readable after it can no longer write.

- **Source:** Proposal. At end of life the data is what's worth saving.
- **Measure:** a read-only mount and reads after a write-to-exhaustion run.
- **Pass:** `lfs3_mount(LFS3_M_RDONLY)` succeeds and every synced file reads
  correctly, in B-DEF, B-YGB and B-BIG.
- **Fail:** the mount or a read fails.
- **Verified by:** `exhaustion::readback`.
- **Status:** Tested on `v3-integration` (NEW-15).
- **When:** every CI run.

#### LFS3-DEG-08

littlefs shall have no single block, other than the mroot anchor pair, whose
failure makes the filesystem unwritable: a gbmap whose root, or another of
its nodes, fails a read or check shall be dropped and built again
elsewhere, allocation falling back to lookahead scans meanwhile; and the
anchor pair's erases shall stay within the bound in `lfs3.h`:
2(`block_recycles`+2) plus all erases over
(`block_recycles`+1)(`block_size`/64).

- **Source:** Proposal. At `b10efaa` a corrupt gbmap root makes every gbmap
  lookup fail, and bad anchor blocks give NOSPC once the anchor must change
  (3-alloc R9). A dropped gbmap's root is erased and kept out of use until
  remount: the on-disk gstate names it until the next commit, and a root
  that read again after its other nodes were reused would hand out blocks
  in use. For the same reason mount checks the gbmap's root against the
  cksum gstate records, and a root that doesn't check is rebuilt at the
  first write. The bad blocks the old gbmap marked are lost, and found
  again as they fail. The anchor is erased up to `block_recycles`+1 times
  a block while it is the first mroot, then only to compact the pointer
  to the mroot it names, which changes once every `block_recycles`+1
  compactions of that mroot, at least `block_size`/64 pointers to a
  block, and once each time it extends the mroot chain. With
  `block_recycles` -1 nothing relocates, and the anchor wears as the mroot
  does.
- **Measure:** writes, the gbmap's root and the old root's erases and
  progs, with the gbmap's root unreadable, made so while mounted and
  before a mount; anchor erases against all erases over runs of commits
  to the mroot and to an mdir of the mtree.
- **Pass:** `badblocks::gbmap_root`: every write succeeds; a mount that
  finds the root bad reports `LFS3_I_MKGBMAP`, cleared once the gbmap is
  rebuilt; the gbmap's root moves; the old root is erased at most once and
  never programmed; every file reads back and checks, also after a
  remount, without the flag. `badblocks::gbmap_readerror` and
  `badblocks::live_readerror` (the gbmap's root) write on and check.
  `relocations::anchor_wear`, with `block_recycles` 0, 1, 4 and 16 and
  10,000 and 40,000 commits: the anchor's erases stay within the bound.
  `badblocks::gbmap_format`: block 2 bad at format. The gbmap cases in
  B-YGB and B-BIG, the anchor in B-DEF.
- **Fail:** a write that fails because of the gbmap, a block in use handed
  out, or anchor erases over the bound.
- **Verified by:** `badblocks::gbmap_root` (NEW-147),
  `relocations::anchor_wear` (NEW-148), `badblocks::gbmap_readerror`,
  `badblocks::live_readerror`, `badblocks::gbmap_format`.
- **Status:** Known defect (3-alloc R9). At `6f80e646` writes returned
  `LFS3_ERR_CORRUPT` once they needed a gbmap that didn't read
  (`badblocks::gbmap_readerror` expected it), the repair was
  `lfs3_fs_rmgbmap` and `lfs3_fs_mkgbmap`, which `LFS3_YES_GBMAP` builds
  lack, and the anchor's wear had no stated bound. Fixed and tested on
  `v3-r21` (`73001c31`); the anchor's bound tested on `v3-r21`
  (`4ac1a1bb`).
- **When:** every CI run.

#### LFS3-DEG-09

littlefs shall report its health in one call, so the application can act
before it fails: the blocks in use, the bad blocks, the suspect blocks,
and the good blocks left to write; `lfs3_fs_nextbad` and
`lfs3_fs_nextsuspect` shall list which blocks are bad and suspect.

- **Source:** Proposal (issues #9, #19, #21). An unattended application
  can, for example, log less or rotate sooner as capacity shrinks. One
  call, so firmware doesn't have to iterate two lists and run a
  traversal; `lfs3_fs_stat` never goes to disk, and counting blocks in
  use needs a traversal, so it's a call of its own, `lfs3_fs_health`.
- **Measure:** the reported values against the emubd state and a
  traversal.
- **Pass:** `repair::health`: with blocks marked bad and blocks that
  failed reads, `lfs3_fs_nextbad` lists exactly the bad blocks,
  `lfs3_fs_nextsuspect` exactly the suspect ones, `block_count` minus
  `lfs3_fs_usage` (which counts bad blocks as used) equals the free good
  blocks a traversal finds, and `lfs3_fs_health` reports the same
  counts, in B-YGB and B-BIG; `alloc::health`: as files are written and
  removed, `lfs3_fs_health` reports the blocks a traversal finds in use,
  no bad or suspect blocks, and the rest free, in B-DEF, B-YGB and B-BIG.
- **Fail:** a mismatch, or a free count larger than what can be written.
- **Verified by:** `repair::health`, `alloc::health` (NEW-146).
- **Status:** Not implemented at `b10efaa`; the lists and the usage
  tested on `v3-integration` (f09acb9d); there was no single call at
  `6f80e646`. `lfs3_fs_health` tested on `v3-r21` (`ad8752d1`).
- **When:** every CI run.

#### LFS3-DEG-10

littlefs shall offer repairs smaller than a reformat: moving the contents of
a block that reads unreliably to a new block, removing a damaged file
(LFS3-DEG-02), rebuilding the gbmap (LFS3-DEG-08), and rewriting or
dropping a damaged metadata pair (LFS3-DEG-16); `lfs3_fs_ck`, and a mount
with `LFS3_M_CKMETA` or `LFS3_M_CKDATA`, shall make the first of these on a
writable filesystem when `ck_retries` allows, and shall return
`LFS3_ERR_CORRUPT` only for damage they could not read or repair; and
ERRORS.md shall list the repairs as the Rebuild steps of
`LFS3_ERR_CORRUPT`, smallest first, ending, for a damaged metadata pair,
with the degraded read-only mount (LFS3-DEG-03) to copy out what reads, then
the salvage mount (LFS3-DEG-16).

- **Source:** Proposal (issues #19, #20). A reformat loses every file, which
  on an unreachable unit is the same as replacing it. A move copies exactly
  the bytes of a read that passed the checksum the block's parent records,
  so it never turns a bad read into data with a fresh checksum.
- **Measure:** the result of each repair on an image with that damage; the
  blocks that hold the repaired data; file contents.
- **Pass:** `repair::data`, `repair::btree`, `repair::gbmap`: a data
  block, a file B-tree node and a gbmap node whose next 1 or 3 reads return
  a flipped bit are moved to new blocks, with `ck_retries` 3, by
  `lfs3_fs_ck`, and the first two also by a mount with the matching check
  flag and by `lfs3_fs_gc`; the call returns 0; the old blocks are no longer referenced; every file
  reads back as written, also after a remount; a second check erases
  nothing. `repair::mdir`, `repair::mdir_twice` (NEW-139): an mroot or
  mtree mdir whose active block is suspect is settled into its other block
  (LFS3-DEG-14) by `lfs3_fs_ck` and `lfs3_fs_gc`, not while an open file
  holds it; every file reads back after a write and a remount; the
  suspect block is written again by the next compaction, and marked bad
  and left by a relocation if it needs settling off again.
  `repair::mtree` (NEW-145): the mtree's root, or a node below it, whose
  next 1 or 3 reads return a flipped bit is moved to a new block by
  `lfs3_fs_ck` with `ck_retries` 3, by a commit through the mtree and a
  commit of the new mtree to the mroot; the old block is no longer
  referenced, every file reads back, also after a remount, and a second
  check erases nothing.
  `repair::data_lost`: a block whose every read fails makes the
  check return `LFS3_ERR_CORRUPT` and stays listed by
  `lfs3_fs_nextsuspect`, and the damaged file can still be removed
  (LFS3-DEG-02). On a read-only mount nothing is moved and nothing is
  written. `gbmap::rmmkgbmap`: `lfs3_fs_rmgbmap` and `lfs3_fs_mkgbmap`
  rebuild the gbmap; `badblocks::gbmap_root`: a gbmap that doesn't read
  is rebuilt at the next write. ERRORS.md's Rebuild steps for
  `LFS3_ERR_CORRUPT`, reviewed: read again with `ck_retries`, remove the
  damaged file, let the gbmap rebuild, mount read-only to copy out what a
  damaged metadata pair leaves, then reformat.
- **Fail:** a repair that loses undamaged data, writes bytes that did not
  pass their checksum, reports an error for damage it repaired, or a
  repairable block left unrepaired on a writable mount.
- **Verified by:** `repair::data`, `repair::data_lost`, `repair::btree`,
  `repair::gbmap`, `repair::mdir`, `repair::mdir_twice`,
  `repair::mtree`; `gbmap::rmmkgbmap`.
- **Status:** Partly met. Not implemented at `b10efaa`; the repairing
  check is tested on `v3-integration` (f09acb9d); mdirs are settled
  instead of moved, tested on `v3-rc` (`e78b4263`), only when the other
  block reads as older, since at `6f80e646` a fetch that couldn't read the
  newer block fell back to the older one without an error (issue #6,
  LFS3-INT-26). mtree nodes were not moved at `6f80e646`, that needed a
  commit through the mtree and the mroot, as gbmap nodes have; one that
  needed a retry stayed listed as suspect. Moved and tested on `v3-r21`
  (`7aa70764`); the gbmap rebuilt at the next write on `v3-r21`
  (`73001c31`). A damaged metadata pair, a
  directory's entries, had no repair in place at `v3-r21` (`195ed2ef`):
  the degraded read-only mount (LFS3-DEG-03) copied out what read before
  a reformat; Q27 is decided, the repair is LFS3-DEG-16, tested on
  `v3-r21` (`3aa6ae9f`).
- **When:** every CI run.

#### LFS3-DEG-11

littlefs shall mark a file dirty on disk in the first metadata commit of each
write session, keep the mark through the session's syncs, and clear it in
the commit that ends the session; a new file's stickynote shall stand as its
mark until the sync that creates the file; the mark shall add no commit
except at most one at close, and no erase of its own.

- **Source:** Proposal (issue #1). The mark tells a mount which metadata
  pairs the interrupted session was writing, so only those are repaired.
- **Measure:** the `DIRTY` tag on disk (SPEC.md) and the commits, progs and
  erases of a session.
- **Pass:** `powerloss::dirty_mark` (NEW-131) shows: a new file's
  stickynote is its mark, its first sync replaces it with a `DIRTY` tag,
  and an existing file's first commit (a shrub commit or a sync) carries
  the tag; later syncs keep it; `lfs3_file_close` clears it without an
  erase, with one small extra commit only when nothing else is pending; a
  session that commits only at close, and `lfs3_set`, never mark; a mark
  left by a power loss is removed by the first `lfs3_fs_mkconsistent` after
  the mount that repaired it. The tag's bytes bring mdir compactions
  forward: on the cost bench (B.6) the number of commits is unchanged,
  erases grow by 11% when a file is reopened for each small append, and
  not at all when files are created and closed or appended to in one long
  session. `badblocks::ioerror` passes with the close's commit counted.
- **Fail:** a session commit without the mark, a mark left after a clean
  close, or an erase at close.
- **Verified by:** NEW-131, `badblocks::ioerror`.
- **Status:** Tested on `v3-rc` (`a12705da`).
- **When:** every CI run.

#### LFS3-DEG-12

littlefs shall, at every read-write mount in its default mode, rewrite from
verified bytes, before anything is appended, every metadata pair that holds
a dirty file, every pair on the path from the mroot anchor to it, and every
pair showing a power loss (a torn tail after its last commit, or a newer
block that fails to fetch), skipping pairs already settled by an earlier
repair and unwritten since; and shall write nothing at a mount when there is
no such pair.

- **Source:** Proposal (issue #1). Complete (clean) files are trusted as
  they are, so a mount after a clean shutdown costs nothing.
- **Measure:** which pairs a mount rewrites, the bd operations of a mount,
  and the residual cases.
- **Pass:** NEW-08 and NEW-130 with `MODE` 0 lose no completed sync outside
  the residual cases; NEW-131 shows no prog or erase at a mount without a
  dirty file or a torn pair. The residual cases are documented in `lfs3.h`:
  a power loss during (1) the commit that clears a mark (a file's close,
  `lfs3_set`), (2) a single-commit metadata operation (`lfs3_mkdir`'s
  second commit, `lfs3_remove`, `lfs3_rename`, `lfs3_setattr`,
  `lfs3_removeattr`, `lfs3_fs_grow`, the removals of
  `lfs3_fs_mkconsistent`), or (3) the repair itself, when the interrupted
  program reads as whole at one mount and as torn at a later one and
  something was built on it in between.
- **Fail:** a completed sync lost outside the residual cases, or a write at
  a mount with nothing to repair.
- **Verified by:** NEW-08, NEW-130, NEW-131.
- **Status:** Tested on `v3-rc` (`a12705da`).
- **When:** every CI run.

#### LFS3-DEG-13

littlefs shall, when mounted read-write with `LFS3_M_SETTLE`, rewrite from
verified bytes, before anything is appended, every metadata pair written
since the last mount that settled it, and every pair showing a power loss.

- **Source:** Proposal (issue #1). For applications that can't accept the
  residual cases of LFS3-DEG-12, at the cost of one erase per pair written
  since the last mount.
- **Measure:** completed syncs after power losses under METASTABLE; erases
  per mount.
- **Pass:** NEW-08 and NEW-130 with `MODE` 1 lose no completed sync; the
  behaviour-4 permutations of NEW-05, NEW-06, NEW-11 and
  `dirs::rm_many_2layers` pass criterion A; a pair written since the last
  mount costs one erase at the mount and one compaction at its next write
  (an append when it is too full to compact in place), and an unwritten
  settled pair costs nothing (B.6).
- **Fail:** a completed sync lost, or a settled pair rewritten again
  without a write.
- **Verified by:** NEW-08, NEW-130, NEW-05, NEW-06, NEW-11,
  `dirs::rm_many_2layers`.
- **Status:** Tested on `v3-rc` (`a12705da`).
- **When:** every CI run.

#### LFS3-DEG-14

littlefs shall rewrite a metadata pair only from bytes that pass the
checksum of the commit they belong to, as read for the copy, and shall not
append to a block whose state may not read the same twice; and shall not
take a read the block device fails for a power loss: a failed read shall
not count as a commit that reads differently, and a repair shall never
copy over a newer block it can't read, nor copy the older block over a
newer one that failed only a read and reads whole when read again.

- **Source:** Proposal (issue #1). A copy that checksums bytes it read
  differently launders a flipped bit under a fresh checksum. A read can
  fail while the supply is low and pass later (issue #19), and a repair
  that takes it for an interrupted write drops synced commits.
- **Measure:** the repair copy and the blocks littlefs appends to.
- **Pass:** the repair copies the active block commit by commit, checking
  each commit's checksum on the bytes it copies, over several reads, and
  stops before a commit that fails or reads differently; the copy keeps
  the revision count so the global state is unchanged, and only its last
  commit's `CKSUM` carries a settled generation (SPEC.md, settled copies),
  so a partial copy has none; a fetch of two blocks with equal revision
  counts prefers the block with a settled commit, then the successor
  generation, then the longer log; the first write to a settled pair
  compacts it, unless it is too full to compact in place; the first write
  to the older block of a pair whose newer block failed to fetch, and to a
  block without a settled commit in a pair with equal revision counts,
  compacts; the next compaction of the mroot after a mount finds a settled
  copy sets the `SETTLED` wcompat flag, without a commit of its own.
  NEW-08, NEW-130, and the behaviour-4 permutations of NEW-05, NEW-06,
  NEW-11 and `dirs::rm_many_2layers` pass with power losses during the
  repairs themselves; `mtree::commit_too_big` passes, a full mroot can
  still be emptied. `powerloss::settle_newer` (NEW-137): a newer block
  whose next 1, 3 or 64 reads fail as a mount settles its pair is read
  again up to `ck_retries` (0 and 3) times, and the mount returns
  `LFS3_ERR_CORRUPT` while it doesn't read (LFS3-INT-26), never
  overwritten by the older block;
  `powerloss::settle_rderr` (NEW-138): an active block whose next 1 to 16
  reads fail during the repair loses no synced commit, at that mount or
  the next.
- **Fail:** a repair that commits bytes other than those checked, an
  append to a settled copy that could compact in place, to a tie's block
  without a settled commit, or to a block whose newer partner failed, a
  write at mount that a full metadata pair can't take, or a synced commit
  lost to a read that failed.
- **Verified by:** NEW-08, NEW-130, NEW-131, NEW-05, NEW-06, NEW-11,
  `dirs::rm_many_2layers`, `mtree::commit_too_big`, NEW-137, NEW-138.
- **Status:** Tested on `v3-rc` (`a12705da`); the failed-read clauses
  tested on `v3-rc` (`e78b4263`).
- **When:** every CI run.

#### LFS3-DEG-15

littlefs shall check a data block's checksum on the bytes it copies when it
crystallizes them into a new block, in every build, not only with
`LFS3_M_CKDATACKSUMS`.

- **Source:** Proposal (issue #1). The first append after a remount copies
  the file's partial last block; an unchecked copy launders a flipped bit
  under the new block's checksum.
- **Measure:** the result of an append that copies a block with a flipped
  bit.
- **Pass:** `ck::crystallize_flipped` (NEW-132) returns
  `LFS3_ERR_CORRUPT` from the append in B-DEF and B-BIG, and the file's
  checksum still detects the flip.
- **Fail:** the append succeeds, or the flip reads back under a valid
  checksum.
- **Verified by:** NEW-132.
- **Status:** Tested on `v3-rc` (`a12705da`).
- **When:** every CI run.

#### LFS3-DEG-16

littlefs shall, when mounted read-write with `LFS3_M_SALVAGE`, repair
damaged metadata in place instead of failing the mount: rewrite each
metadata pair that one block still reads, from what reads and passes its
checksums, into a new pair; drop each pair of the mtree that no block
reads; remove the entries that a drop leaves unreachable, recreate the
directory bookmarks it took, and rebuild the global checksum and the
global state; and then mount as without the flag. The loss shall be
bounded: nothing outside the damaged pairs; for a rewritten pair, at most
the commits written to it since its last compaction; for a dropped pair,
the entries it held, and the subtrees of the directories it named. After
the salvage the filesystem shall mount read-write without
`LFS3_M_SALVAGE`, and `lfs3_fs_ck` with `LFS3_CK_CKMETA | LFS3_CK_CKDATA`
shall return 0 unless data blocks are damaged too.

- **Source:** Proposal (issue #21, Q27 decided on 2026-10-05: "Rewrite,
  else drop"). A damaged pair makes every read-write mount fail
  (LFS3-DEG-03), and a reformat loses every file. Losing the damaged
  pair's entries must be the application's call, since a failed read
  doesn't prove them gone (principle 4), so the salvage is an explicit
  mount flag, never automatic. A mount flag rather than a call on a
  mounted filesystem: the damage fails a read-write mount, and a degraded
  mount is read-only and writes nothing, so the repair has to be part of
  the mount that makes the filesystem writable again, as `LFS3_M_SETTLE`
  and `LFS3_M_MKCONSISTENT` are; with no damage it does nothing. The name
  says data may be lost, unlike the repairing check of `ck_retries`
  (LFS3-DEG-10), which never loses any.
- **The state after:** every entry outside the damaged pairs as it was; a
  rewritten pair as of its older block, or of the commits of its newer
  block that still read; a dropped pair's entries gone (`LFS3_ERR_NOENT`),
  and with them the bookmarks and entries of every directory whose name
  was there, recursively; a directory whose bookmark was there keeps its
  other entries under a new bookmark, the root included; no pending
  removes, so a file a power loss left half removed or renamed may keep
  its old name too; the gbmap rebuilt, and the bad blocks it marked
  marked again as they fail; with `LFS3_GBMAP`, the damaged pairs' blocks
  that failed reads tested, and marked bad if they still fail. Blocks a
  dropped pair referenced may be reused, so it stays lost even if it
  would read later. A damaged mroot chain with no block that checks, or a
  damaged B-tree node of the mtree, leaves nothing to salvage from: the
  mount returns `LFS3_ERR_CORRUPT`. A power loss during the salvage leaves
  the filesystem damaged, a read-write mount without the flag fails until
  a salvage completes, and the next salvage finishes the repair, losing
  nothing more.
- **Measure:** the mount's result, the files, directories and listings
  after it, a read-write mount without the flag and `lfs3_fs_ck` after
  that, with one mtree mdir damaged in each way, with a power loss at
  each prog and erase of the salvage, and the bad and suspect lists.
- **Pass:** `salvage::damage` (NEW-149): with the newer block of the pair
  failing every read, every file reads, the pair's files as of its older
  block; with both blocks failing reads or erased, the pair's files and
  the subdirectory named in it, with its contents, are `LFS3_ERR_NOENT`,
  a directory whose bookmark was in it lists the rest of its entries, and
  no entry of a removed directory is left; the first mdir (the root's
  bookmark) and the last; a rolled-back pair keeps its older state; then
  a read-write mount without the flag returns 0, writes and remounts, and
  `lfs3_fs_ck` returns 0; `LFS3_M_SALVAGE` on an undamaged filesystem
  programs and erases nothing more than a plain mount.
  `salvage::powerloss` (NEW-150): power lost at each prog and erase
  of the salvage, under every power-loss behaviour: a read-write mount
  without the flag returns `LFS3_ERR_CORRUPT` until a salvage completes,
  unless the salvage hadn't written anything and the pair reads again,
  and the next salvage leaves the state above, or loses less. `salvage::badblocks`
  (NEW-151), with `LFS3_GBMAP`: a pair whose blocks fail every read
  (READERROR) has them marked bad, listed by `lfs3_fs_nextbad`, and never
  programmed or erased again; blocks that read but don't check are
  reused. In B-DEF, B-YGB and B-BIG.
- **Fail:** a lost entry outside the damaged pairs, an entry of a
  rewritten pair older than its pair's last compaction, an unreachable
  entry left, a directory that can't be listed, a filesystem that doesn't
  mount read-write or check after the salvage, or one that mounts without
  the flag before a salvage completes.
- **Verified by:** NEW-149, NEW-150, NEW-151.
- **Status:** Not implemented at `v3-r21` (`195ed2ef`); tested on
  `v3-r21` (`3aa6ae9f`).
- **When:** every CI run.

## 7. Summary

Counts by area and by status at `b10efaa`. T = Tested, P = Partly tested, U
= Untested, N = Not implemented (planned), K = Known defect.

| Area | Code | Total | T | P | U | N | K |
|---|---|---|---|---|---|---|---|
| General and portability | GEN | 8 | 0 | 1 | 4 | 0 | 3 |
| Power-loss resilience | PL | 27 | 8 | 4 | 14 | 0 | 1 |
| Error detection and integrity | INT | 23 | 7 | 11 | 3 | 0 | 2 |
| Flash failure handling | FAIL | 20 | 4 | 8 | 8 | 0 | 0 |
| Bad-block tracking | BAD | 17 | 0 | 0 | 0 | 17 | 0 |
| Metadata | META | 17 | 7 | 3 | 3 | 0 | 4 |
| Files and data | FILE | 27 | 14 | 2 | 8 | 0 | 3 |
| Sync model and stickynotes | SYNC | 20 | 15 | 2 | 1 | 0 | 2 |
| Directories and paths | DIR | 20 | 16 | 0 | 1 | 0 | 3 |
| Custom attributes | ATTR | 13 | 8 | 1 | 2 | 0 | 2 |
| Key-value API | KV | 9 | 5 | 1 | 2 | 0 | 1 |
| Block allocation | ALLOC | 17 | 4 | 7 | 5 | 0 | 1 |
| Pre-erase | PRE | 9 | 0 | 2 | 6 | 0 | 1 |
| Garbage collection and traversals | GC | 18 | 8 | 6 | 1 | 2 | 1 |
| Format, mount, grow, compatibility | MOUNT | 27 | 10 | 4 | 6 | 1 | 6 |
| Configuration validation | CFG | 17 | 1 | 6 | 8 | 0 | 2 |
| Resource bounds | RES | 8 | 0 | 0 | 7 | 0 | 1 |
| Performance | PERF | 14 | 0 | 4 | 7 | 1 | 2 |
| Thread safety | THR | 3 | 0 | 0 | 1 | 0 | 2 |
| Build configurations | BUILD | 20 | 0 | 1 | 9 | 0 | 10 |
| Continuous integration | CI | 11 | 0 | 0 | 4 | 0 | 7 |
| Documentation | DOC | 19 | 0 | 0 | 0 | 9 | 10 |
| **All** | | **364** | **107** | **63** | **100** | **30** | **64** |

By level: 223 stated, 121 derived, 20 proposals. By When: 293 every CI run,
44 nightly, 27 before v3-beta.

107 requirements (29%) are fully checked by a case that runs in the default
build. 63 are partly checked, most often because the checking case is
compiled out of the default build. 64 are known defects; Appendix A says
which of them have fixes on our branches.

The known defects, with the fixes that exist on our branches (Appendix A):

| Requirement | Ref | Fix on our branches |
|---|---|---|
| LFS3-GEN-03 | 4-api R17, 1-meta 0.7c | 1-meta 0.7c: v3-fix-api `7f689b8`; F-1 and F-3: v3-fixes; R17: none |
| LFS3-GEN-06 | 4-api R12 | none |
| LFS3-GEN-08 | 4-api R4 | none |
| LFS3-PL-27 | 4-api R27 | none |
| LFS3-INT-20 | F-2 | v3-fixes `e4c046b` |
| LFS3-INT-23 | D-2 | v3-fix-parity `f90e132` (`ck::ckparity_btree_append`) |
| LFS3-META-03 | 1-meta 0.2, D-9 | 1-meta 0.2: v3-integration `d428d7b8`; D-9: v3-integration `e38b42ae` (issue #25) |
| LFS3-META-04 | 1-meta 0.3 | none |
| LFS3-META-10 | 3-alloc B3 | v3-fix-alloc `bd5bb8c` (`badblocks::mrootanchor_stuck`, `badblocks::badsync`; adds emubd `mkbadsync`) |
| LFS3-META-11 | 1-meta 0.5 | none |
| LFS3-FILE-03 | 2-files B2 | v3-fix-files `5500063` (`fwrite::append_fbig`) |
| LFS3-FILE-04 | 2-files B14 | v3-fix-files `25cfa66` (`files::read_big`) |
| LFS3-FILE-10 | 2-files B1 | v3-fix-files `b07be9e` (`badblocks::fruncate_append`) |
| LFS3-SYNC-05 | 2-files B8 | v3-fix-files `8c5241d` (`badblocks::truncate_desync`) |
| LFS3-SYNC-20 | `badblocks::append_torn` | v3-integration `554e89f9` |
| LFS3-DIR-02 | 1-meta 0.2 | none |
| LFS3-DIR-05 | 4-api R1 | v3-fix-api `067ebe7` (`dirs::mv_subtree`) |
| LFS3-DIR-11 | 1-meta 0.1 | v3-fix-api `cc4acb9` (`dread::seek_tell`) |
| LFS3-ATTR-05 | 4-api R3 | none |
| LFS3-ATTR-12 | 2-files B11 | v3-fix-files `1783b37` |
| LFS3-KV-04 | 4-api R7 | v3-fix-files `9c7deb7` (`kv::set_fbig`) |
| LFS3-ALLOC-11 | 3-alloc B11 | none |
| LFS3-PRE-06 | 3-alloc B1 | v3-fix-alloc `3ceb48b` (`badblocks::preerase`) |
| LFS3-GC-02 | #5 | v3-integration (`gc::steps_unbounded`, `gc::compact_unshrinkable`) |
| LFS3-MOUNT-03 | 4-api R28 | none |
| LFS3-MOUNT-09 | 1-meta 0.4 | none |
| LFS3-MOUNT-14 | 4-api R5 | none |
| LFS3-MOUNT-17 | 2-files B3 | v3-fix-files `b923cd8` (`attrs::fattr_rdonly_file`, `fsync::desync_wdrs`) |
| LFS3-MOUNT-19 | 4-api R25 | none |
| LFS3-MOUNT-21 | 4-api R4 | none |
| LFS3-CFG-01 | 2-files B12 | none |
| LFS3-CFG-05 | 1-meta 7.14 | none |
| LFS3-RES-06 | 4-api R8 | none |
| LFS3-PERF-07 | measured | none |
| LFS3-PERF-09 | measured, M-3, `bench_wlog_fresh` | v3-integration `ec0733b8`, `554e89f9`, `32eb36e7` (`bench_wlog_fresh`) |
| LFS3-THR-01 | 4-api R19 | v3-r8 `ecf44d33` (`threadsafe::locks`, `make test-threadsafe`) |
| LFS3-THR-02 | 4-api R19 | v3-r8 `ecf44d33` (`threadsafe::lock_fails`, `threadsafe::unlock_fails`) |
| LFS3-BUILD-02 | F-2 | v3-fixes `e4c046b` |
| LFS3-BUILD-03 | F-2 | v3-fixes `e4c046b` |
| LFS3-BUILD-04 | 4-api R6 | none |
| LFS3-BUILD-05 | 1-meta 0.7b | none |
| LFS3-BUILD-06 | 2-files B6 | none |
| LFS3-BUILD-08 | 4-api R20 | none |
| LFS3-BUILD-09 | 4-api R21 | none |
| LFS3-BUILD-10 | 2-files B10 | none |
| LFS3-BUILD-11 | 2-files B9 | none |
| LFS3-BUILD-13 | F-2 | F-2: v3-fixes `e4c046b`; D-2: v3-fix-parity `f90e132`; 1-meta 0.7c: v3-fix-api `7f689b8` |
| LFS3-CI-01 | 2-files B19 | none |
| LFS3-CI-02 | 2-files B19 | none |
| LFS3-CI-04 | 2-files B19 | none |
| LFS3-CI-06 | 2-files B19 | none |
| LFS3-CI-07 | 2-files B19 | none |
| LFS3-CI-08 | 5-verif §0.6 | escape sequences: v3-ci `dbbb5e6` |
| LFS3-CI-09 | F-4 to F-8 | v3-fixes |
| LFS3-DOC-04 | D-1 | none |
| LFS3-DOC-05 | 3-alloc R8 | none |
| LFS3-DOC-06 | 4-api R23 | none |
| LFS3-DOC-07 | 3-alloc R8 | none |
| LFS3-DOC-08 | 4-api R23 | none |
| LFS3-DOC-09 | 3-alloc R8 | none |
| LFS3-DOC-10 | 4-api R23 | none |
| LFS3-DOC-11 | 3-alloc R8 | none |
| LFS3-DOC-14 | 4-api §1.7 | none |
| LFS3-DOC-17 | 3-alloc R8 | none |

## 8. Open questions

These are behaviours where the code, `lfs3.h` and the roadmap do not settle
the intent. The requirements that depend on them say so. Each question lists
the options we see; none is chosen here.

**Q1. `lfs3_file_sync` on a handle opened `LFS3_O_RDONLY`.** At `b10efaa` it
commits the handle's snapshot like any other sync: a desynced reader can
revert a newer file to its stale contents, and writable file-attached
attributes on a read-only file are written (2-files B3). Options: (a) return
an error (`LFS3_ERR_INVAL`, or a new read-only code); (b) return 0 and do
nothing; (c) return 0 and resync the handle without writing, which is what
our branch `v3-fix-files` (`b923cd8`) does, reading writable attributes on
read-only files but never writing them, as v2 did; (d) keep the current
behaviour and document it. LFS3-MOUNT-17 holds under (a), (b) and (c).

**Q2. Mutating calls on an `LFS3_M_RDONLY` mount.** They assert at
`b10efaa` (`lfs3.c:16620`) and proceed with `LFS3_NO_ASSERT` (LFS3-GEN-06).
Options: (a) keep the assert and document it as a precondition
(LFS3-DOC-16); (b) return an error code, either an existing one
(`LFS3_ERR_INVAL`, `LFS3_ERR_NOTSUP`) or a new read-only code; (c) both: an
error code in all builds and an assert in debug builds.

**Q3. The error for an operation whose metadata cannot fit in an empty
block.** Long names on small blocks and large attributes trip an assert
(LFS3-META-03, DIR-02, ATTR-05). Options: (a) `LFS3_ERR_NAMETOOLONG` for
names and `LFS3_ERR_NOSPC` for attributes, as v2 did; (b) `LFS3_ERR_RANGE`,
which already leaks out with `LFS3_NO_ASSERT`, documented; (c) prevent the
case at format by capping `name_limit` from `block_size`, and define a
maximum attribute size (as v2's `LFS_ATTR_MAX`) checked by `lfs3_setattr`.

**Q4. rcompat matching.** `lfs3.h:920-926` says rcompat flags must be
"understood". The code requires exact equality, so an image that uses fewer
features than the driver writes (for example no mtree) is refused, even
read-only (4-api R10). Options: (a) exact match, documented; (b) accept any
subset of the flags the driver knows.

**Q5. A missing version tag.** It is read as version 0.0 at `b10efaa`
(`lfs3.c:15572-15594`). Options: (a) accept, and document the default in
SPEC.md; (b) refuse with `LFS3_ERR_CORRUPT` or `LFS3_ERR_NOTSUP`.

**Q6. A degraded mount.** A gcksum mismatch, an undecodable grm or gbmap, or
an unreadable metadata block fails the mount or blocks all allocation, with
no fallback ("TODO switch to read-only?", `lfs3.c:15921, 15948`; 4-api R11;
3-alloc R7). Options: (a) keep the hard failure and document it
(LFS3-DOC-13); (b) let `LFS3_M_RDONLY` mounts proceed and report the problem
through an info flag; (c) add an explicit "salvage" mount flag. Settled by
principle 2 (graceful degradation): (b), `LFS3_I_DEGRADED`, see
LFS3-DEG-03 for why not (c).

**Q7. Is an open, never-synced file a child of its directory?** At
`b10efaa` `lfs3_remove` of the directory returns `LFS3_ERR_NOTEMPTY`
(1-meta F16). Options: (a) yes, as now, and document it; (b) no: remove the
directory and make the handle a zombie, as removing an open file does.

**Q8. `LFS3_O_SYNC` and `LFS3_O_FLUSH` with truncate and fruncate.** They do
not sync or flush after `lfs3_file_truncate` and `lfs3_file_fruncate`
("note LFS3_O_SYNC does _not_ sync truncates", `tests/test_fsync.toml`).
Options: (a) keep, and document that the flags apply to writes only;
(b) apply them to every call that changes the file.

**Q9. Reserved attribute types.** #1111 reserves 0x80-0xbf for standard
attributes and suggests 0xc0-0xff for system attributes; `lfs3.h:766-768`
says 0x80-0xff "May be assigned a standard attribute". The API accepts every
type. Options: (a) document the #1111 split and accept every type;
(b) refuse the reserved range in `lfs3_setattr` and in file-attached
attributes; (c) refuse only 0x80-0xbf.

**Q10. `lfs3_remove` and `lfs3_rename` after a failed cleanup commit.** When
the second commit (fixgrm) fails, the call logs a warning and returns 0; the
remove stays pending and is retried by the next mkconsistent ("TODO is this
the right thing to do?", `lfs3.c:11822, 12004`; 4-api R26). Options: (a)
return 0, as now, since the visible change is done and atomic; (b) return
the error, although the operation has taken effect.

**Q11. `LFS3_M_CKREADS`.** #1111 lists it as planned ("Closed checking of
data during reads"). The tree has `LFS3_M_CKMETAPARITY` and
`LFS3_M_CKDATACKSUMS` instead, and `tests/test_ck.toml:2725` mentions a
"ckredund" that would "finally close the ckread hole". Is CKREADS still
planned, or superseded?

**Q12. `LFS3_THREADSAFE`.** The header documents `lock` and `unlock`
callbacks that `lfs3.c` never calls (LFS3-THR-01). Options: (a) implement
them as in v2; (b) remove the option and leave locking to the application.
Decided on 2026-10-05: (a), keep the option as `lfs3.h` declares it
(LFS3-THR-01, LFS3-THR-02, LFS3-THR-04).

**Q13. The scope of `lfs3_file_ck`.** It checks the file's B-tree and data
blocks but not the file's own mdir entry or inline data (4-api R29). Options:
(a) document the scope; (b) also re-fetch and check the file's mdir.

**Q14. How configuration errors are reported, and the minimum geometry.**
Configuration is checked with asserts. Some invalid values are not checked
at all (`block_count` below 2, a tiny `block_size`). Options: (a) asserts
only, with every limit asserted in `lfs3_init` and documented; (b)
`LFS3_ERR_INVAL` from `lfs3_format` and `lfs3_mount`. Separately: what are
the minimum `block_size` and `block_count`?

**Q15. `fcache_size` of 0.** It calls `lfs3_malloc(0)` at `b10efaa`.
Options: (a) "no file cache", as `lfs3_get` already uses internally
(LFS3-CFG-07); (b) refuse it; (c) use a documented default.

**Q16. Work flags on a read-only mount.** `lfs3_fs_stat` on an
`LFS3_M_RDONLY` mount reports `LFS3_I_MKCONSISTENT`, `LFS3_I_LOOKAHEAD` and
`LFS3_I_COMPACT`, work that cannot run (4-api R25). Options: (a) clear them
on read-only mounts; (b) keep them, as "work the image needs", and document
it.

**Q17. Disabling the gbmap while bad blocks are recorded.** Bad-block
records live in the gbmap, so `lfs3_fs_rmgbmap` drops them. Options:
(a) refuse with `LFS3_ERR_BUSY`; (b) drop them and document it.
`v3-integration` does (b): `lfs3.h` documents the loss, and
`badblocks_gbmap::rmgbmap` tests it.

**Q18. Bad blocks detected in read-only contexts, and the bad-block API.**
The maintainer's own open question (#1111 "Bad block tracking"). Options for
read-only contexts: (a) report the error only; (b) also count detections in
RAM and report them in `lfs3_fs_stat`. API options: the calls proposed in
LFS3-BAD-11 to BAD-14, or none beyond automatic tracking. `v3-integration`
does (a), and records blocks that fail reads as suspect in RAM, writable or
not, listed by `lfs3_fs_nextsuspect` (LFS3-BAD-16); its API is
`lfs3_fs_mkbad`, `lfs3_fs_mkgood` and `lfs3_fs_nextbad`, and format takes
known-bad blocks from the block device's errors (LFS3-BAD-14).

**Q19. Torn programs beyond the first prog unit.** Pre-erase trusts an
erased-state checksum of the first `prog_size` bytes, but the first program
of a new rbyd can be a `pcache_size` flush, and a torn program could leave
the checksummed bytes erased while changing later ones (3-alloc B17). The
rbyd ECKSUM has the same width. Options: (a) state the hardware assumption
(LFS3-DOC-12); (b) widen the checksum to the first flush size; (c) flush the
first `prog_size` bytes of a new rbyd separately, before the rest.
Answered: `v3-integration` does (b) (`57493587`). Erased-state checksums
cover `pcache_size` bytes (at least 11, the largest tag littlefs programs
on its own), rounded up to `prog_size` and clamped to the end of the
block, for rbyd appends and pre-erased blocks alike. The format is
unchanged, and a narrower checksum, from an older image or a smaller
`pcache_size`, is not trusted. The hardware assumption left for
LFS3-DOC-12 is that an interrupted program changes no bytes outside that
program. Appendix B.4 measures the cost, which LFS3-PERF-14 asks `lfs3.h`
to state.

**Q20. gc with no work flags.** `lfs3_fs_gc` with `gc_flags` 0 still
pre-erases and commits the gbmap when nothing is pending
(`lfs3.c:16748-16780`). Is that intended?

**Q21. Untrusted images.** Several decoders assert on on-disk values, which
becomes undefined behaviour with `LFS3_NO_ASSERT` (LFS3-GEN-07). Is
robustness against checksum-valid but malformed images in scope for v3?

**Q22. The error for a missing geometry tag.** `LFS3_ERR_INVAL` at
`b10efaa`; `LFS3_ERR_CORRUPT` or `LFS3_ERR_NOTSUP` would match the other
mount failures (LFS3-MOUNT-11). LFS3-ERR-03 rules out INVAL, which means a
caller bug. `v3-integration` (e9aeb8b2) returns `LFS3_ERR_CORRUPT`, as for
a missing magic string: every v3 mroot has a geometry, so its absence is
damage, not a feature this build lacks.

**Q23. Performance gates.** The PERF thresholds are ours. Which workloads
and bounds does the project want to gate on? In particular, is
"no more erases than v2 on a sync-heavy log" a goal at large `prog_size`
(LFS3-PERF-09)? Meeting it at `prog_size` 256 needs a change to how
littlefs writes an append-only file; Appendix B.5 measures the options.
Answered for W-LOG: yes, at every `prog_size`. `v3-integration` adopts
changes B, C and D of Appendix B.5; C and D bring LFS3-SYNC-20 and
LFS3-FILE-27.

**Q24. Telling alpha formats apart.** Every alpha image claims version 0.0,
so images from incompatible alpha commits cannot be told apart (4-api R24).
Options: (a) accept this until v3-beta; (b) bump the minor version on every
format change during the alpha.

**Q25. `LFS3_ERR_BUSY` for the root.** `lfs3_remove` and `lfs3_rename` of
the root return `LFS3_ERR_BUSY`, as Linux does; v2 returned
`LFS3_ERR_INVAL`. BUSY also means a block in use (`lfs3_fs_mkbad`) and a
filesystem changed under an `LFS3_T_EXCL` traversal, which can clear, while
the root never can. Options: (a) keep BUSY, with the one action Rebuild by
freeing the target, else Fail, which covers all three (LFS3-ERR-02);
(b) return `LFS3_ERR_INVAL` for the root, as v2 did, so that BUSY always
means "free the target and retry"; it changes about 30 assertions in
`dirs`, `files` and `paths`.

Decided in this fork: (a). BUSY keeps one meaning, "the target is in use",
the root is permanently in use, and the code matches Linux, which the v3
API and its tests already follow. Open to the maintainer's review.

**Q26. Cleanup errors after remove and rename.** `lfs3_remove` of a
directory and `lfs3_rename` commit, then clean up a grm; if the cleanup
fails, they log the error and return 0, leaving `LFS3_I_MKCONSISTENT` set
for the next write to retry (a TODO in `lfs3.c` asks whether to propagate
it). Options: (a) keep it: the operation is complete and nothing is lost;
(b) return the error once, as for a failed sync after a commit, so the
device's fault is reported where it happened, and a retry then returns
`LFS3_ERR_NOENT`.

Decided in this fork: (a). The operation is complete and consistent on
disk, the pending cleanup is reported by `LFS3_I_MKCONSISTENT`, and the
next write or `lfs3_fs_mkconsistent` retries it and returns its error if
it fails again, so no fault goes unreported for long and the application
never has to undo a success. It stays the one documented exception to
LFS3-ERR-05, with its state in ERRORS.md, and `errs::ioerror` checks it.
Open to the maintainer's review.


**Q27. Repairing a damaged metadata pair in place.** A metadata pair that
doesn't read or check makes read-write mounts fail (LFS3-DEG-03), so the
only way back to a writable filesystem was to copy out what a degraded
read-only mount serves and reformat. Options: (a) keep it so; (b) an
explicit call, for example a check flag accepted only on a mount opened for
repair, that drops each damaged mdir from the mtree, losing its entries,
commits a gcksum and global state rebuilt from the mdirs left, clears
pending removes, rebuilds the gbmap, and removes, or gathers into a
lost+found directory, the entries whose directory's bookmark was in the
dropped mdir; (c) for a pair with one block that reads, an explicit call
that rewrites the pair from that block, accepting that it may be older.
Principles 2 and 4 say any of these must be the application's call, never
automatic. Decided on 2026-10-05: "Rewrite, else drop", (c) where a block
reads, else (b), removing the unreachable entries, as `LFS3_M_SALVAGE`
(LFS3-DEG-16).

## 9. Requirements that need new tests

This section feeds the test plan. 9.1 lists the requirements whose Verified
by field names a NEW test, case, check or job; the description is short, and
the requirement's Pass condition is the exact specification. Where our
branches already add a case, it is named. 9.2 lists the requirements that
existing cases would check if they ran in another build, schedule or
geometry. Documentation requirements checked by review are not listed.

187 requirements need a new test (9.1) and 51 need an existing test run in a
new environment (9.2).

### 9.1 New tests

| ID | Status | When | Test needed |
|---|---|---|---|
| LFS3-GEN-01 | Untested | every CI run | CI job: `make test` on thumb under qemu-arm |
| LFS3-GEN-02 | Untested | every CI run | cross-endian image round trip, x86_64 and mips/powerpc |
| LFS3-GEN-03 | Tested | every CI run (B-DEF), nightly (B-BIG) | `make test-sanitize`, jobs `test-sanitize` and `test-sanitize-biggest` (41165ec9) |
| LFS3-GEN-04 | Untested | every CI run | two `lfs3_t` on two emubd instances, interleaved fuzz |
| LFS3-GEN-05 | Partly | every CI run | runner wrapper that checks every negative return value |
| LFS3-GEN-06 | Defect | every CI run | death-test harness; prog/erase counters around mutating calls on an `LFS3_M_RDONLY` mount, B-DEF and B-NA |
| LFS3-GEN-07 | Untested | before v3-beta | crafted-image suite: out-of-range block, offset, size, weight, alt jump |
| LFS3-GEN-08 | Defect | every CI run | the LFS3-MOUNT-21 case, with bounds checks |
| LFS3-GEN-09 | Tested | every CI run | `dirs::rm_many_2layers_metastable`, `ck::metastable_alts` in B-YGB, NEW-134 (7d27de3d) |
| LFS3-PL-03 | Tested | nightly | `powerloss::metastable` (NEW-08) in both repair modes, B-BIG |
| LFS3-PL-06 | Partly | every CI run | reentrant flush-without-sync case (explicit flush, `O_FLUSH`, `M_FLUSH`) |
| LFS3-PL-11 | Partly | every CI run | reentrant `lfs3_setattr`/`lfs3_removeattr` on paths |
| LFS3-PL-12 | Untested | every CI run | reentrant `lfs3_set` fuzz, values below and above the one-commit limit |
| LFS3-PL-13 | Untested | every CI run | reentrant truncate/fruncate and sync, including write-then-fruncate |
| LFS3-PL-14 | Untested | every CI run | reentrant random-overwrite fuzz with periodic syncs |
| LFS3-PL-15 | Untested | nightly | reentrant small appends and syncs, `PROG_SIZE` 1, 16, 256, PLB-TORN |
| LFS3-PL-16 | Untested | every CI run | reentrant multi-handle, desync and resync fuzz |
| LFS3-PL-17 | Untested | every CI run | reentrant `O_SYNC` and `M_SYNC` writes |
| LFS3-PL-19 | Untested | nightly | reentrant mkgbmap/rmgbmap |
| LFS3-PL-20 | Untested | nightly | reentrant gc, `lfs3_fs_ck`, RDWR traversals, mount work flags |
| LFS3-PL-22 | Untested | every CI run | reentrant format then mount |
| LFS3-PL-25 | Untested | nightly | reentrant variants of `badblocks::region_*`/`alternating_*` |
| LFS3-PL-26 | Tested | nightly | `exhaustion::spam_file_pl_fuzz` in B-YGB (afdf7f05) |
| LFS3-INT-03 | Partly | every CI run | fill the erased region after each commit with every byte value |
| LFS3-INT-04 | Untested | every CI run | flip a bit after the last commit; the next commit must compact |
| LFS3-INT-06 | Partly | every CI run | deterministic rollback of a non-latest mdir; mount must fail |
| LFS3-INT-08 | Untested | every CI run | fuzz: `lfs3_fs_cksum` changes after every model-changing call |
| LFS3-INT-09 | Partly | every CI run | deterministic rollback of the latest commit; checksum must differ |
| LFS3-INT-13 | Partly | every CI run | bit flips in data blocks for `lfs3_file_ck` and `O_CKDATA` |
| LFS3-INT-19 | Partly | every CI run | enable `CKMETAPARITY` in `ck::spam_*` |
| LFS3-INT-21 | Partly | every CI run | replace an mdir with an older copy after mount; `lfs3_fs_ck` must fail |
| LFS3-INT-22 | Untested | every CI run | bit flips in gbmap nodes |
| LFS3-INT-26 | Tested | every CI run | `mount::readerror` with `LFS3_ERR_CORRUPT`, `mount::readerror_mounted` (NEW-140, NEW-141), on `v3-r21` (`862e3695`) |
| LFS3-FAIL-04 | Untested | every CI run | READERROR on a live mdir, file B-tree node and gbmap node |
| LFS3-FAIL-07 | Untested | nightly | PROGNOOP/ERASENOOP without CKPROGS: no unsynced data returned |
| LFS3-FAIL-12 | Untested | every CI run | after end of life, read-only remount reads every synced file |
| LFS3-FAIL-14 | Untested | every CI run | per-block erase bound for mdir relocation |
| LFS3-FAIL-16 | Partly | every CI run | format with block 2 bad and `LFS3_F_GBMAP` |
| LFS3-FAIL-17 | Untested | every CI run | READERROR on the source of a compaction, relocation and rewrite |
| LFS3-FAIL-18 | Untested | every CI run | emubd fails the n-th sync (emubd `mkbadsync` exists on v3-fix-alloc) |
| LFS3-FAIL-19 | Untested | every CI run | emubd returns `LFS3_ERR_IO` from the n-th operation |
| LFS3-BAD-01 | Planned | every CI run | `badblocks_gbmap::recording`, `recording_mdir` (on `v3-integration`), `queue_full` (on `v3-rc`) |
| LFS3-BAD-02 | Planned | every CI run | `badblocks_gbmap::recording`, `reading`, `alloc_skip` (on `v3-integration`) |
| LFS3-BAD-03 | Planned | every CI run | `badblocks_gbmap::pl_fuzz`, `reading_inuse` (on `v3-integration`); only failed blocks marked, referenced ones included, `queue_inuse` (on `v3-rc`) |
| LFS3-BAD-04 | Planned | every CI run | `badblocks_gbmap::rdonly` (on `v3-integration`) |
| LFS3-BAD-05 | Planned | every CI run | `compat::gbmap_exchange` and `make test-compat-gbmap` (on `v3-integration`) |
| LFS3-BAD-06 | Planned | every CI run | `badblocks_gbmap::reading`, `reading_inuse`, `grow` (on `v3-integration`) |
| LFS3-BAD-07 | Planned | every CI run | `make test-nomalloc`: `badblocks_gbmap::*` with `LFS3_NO_MALLOC` |
| LFS3-BAD-08 | Planned | every CI run | `badblocks_gbmap::overflow`, `queue_full`, `queue_inuse`, `queue_merge`: none forgotten (on `v3-rc`) |
| LFS3-BAD-09 | Planned | every CI run | `badblocks_gbmap::exhaustion`, 1024 seeds: no erase of a dead block (on `v3-rc`) |
| LFS3-BAD-10 | Planned | every CI run | `badblocks_gbmap::preerase` (on `v3-integration`) |
| LFS3-BAD-11 | Planned | every CI run | `badblocks_gbmap::api`, `factory` (on `v3-integration`) |
| LFS3-BAD-12 | Planned | every CI run | `badblocks_gbmap::api` (on `v3-integration`) |
| LFS3-BAD-13 | Planned | every CI run | `badblocks_gbmap::api`, `api_ibadblocks` (on `v3-integration`) |
| LFS3-BAD-14 | Planned | every CI run | `badblocks::gbmap_format` with block 2 bad; `badblocks_gbmap::factory` including block 2 |
| LFS3-BAD-15 | Planned | every CI run | build check: no tracking symbols or RAM in B-DEF |
| LFS3-BAD-16 | Planned | every CI run | `badblocks_gbmap::suspect`: blocks failing reads listed by `lfs3_fs_nextsuspect` |
| LFS3-BAD-17 | Planned | every CI run | `repair::reuse`, `repair::twice`: reuse or mark bad after a repair |
| LFS3-META-03 | Tested | every CI run | `mtree::commit_too_big`: fuzz over block size, name length and attribute size; no assert (d428d7b8, e38b42ae) |
| LFS3-META-04 | Defect | every CI run | internal fetch-order case; all suites on A-32BE |
| LFS3-META-10 | Defect | every CI run | failed commit by sync failure and by stuck anchor (`badblocks::mrootanchor_stuck`, `badblocks::badsync` on v3-fix-alloc) |
| LFS3-META-11 | Defect | every CI run | READERROR on an mdir with a gbmap delta at mount |
| LFS3-META-12 | Partly | every CI run | revision counts across the 2^32 wrap |
| LFS3-META-14 | Untested | every CI run | orphans as the only entries of consecutive mdirs; mkconsistent |
| LFS3-META-15 | Partly | every CI run | reentrant mtree split, drop, relocate, extend |
| LFS3-META-17 | Untested | every CI run | internal check: estimate at least the compacted size |
| LFS3-FILE-02 | Untested | every CI run | zero-size write changes nothing |
| LFS3-FILE-03 | Defect | every CI run | `O_APPEND` with a small `file_limit` (`fwrite::append_fbig` on v3-fix-files) |
| LFS3-FILE-04 | Defect | every CI run | read with size 0xffffffff (`files::read_big` on v3-fix-files) |
| LFS3-FILE-10 | Defect | every CI run | fruncate, bad block, append (`badblocks::fruncate_append` on v3-fix-files) |
| LFS3-FILE-11 | Untested | every CI run | data block goes bad after data was written; append |
| LFS3-FILE-16 | Untested | every CI run | failing allocator at file open |
| LFS3-FILE-17 | Tested | every CI run | `fwrite::filemax`, `fwrite::filemax_fuzz`, with UBSan in B-DEF and B-BIG (82ab4f07, b5888089) |
| LFS3-FILE-18 | Untested | every CI run | holes allocate at most 2 blocks |
| LFS3-FILE-19 | Partly | every CI run | handle size against `lfs3_stat` size around a sync |
| LFS3-FILE-21 | Partly | every CI run | close with a failing sync releases the handle |
| LFS3-FILE-24 | Untested | every CI run | two handles appending to one file in turn |
| LFS3-FILE-26 | Untested | every CI run | `fwrite::*fbig` with `file_limit` 1, 1000, 65536 |
| LFS3-FILE-27 | Untested | every CI run | appends with a tail kept cached: reads, sync, close, desync, truncate, fruncate, NOSPC, power loss (`fwrite::append_tail`, `fwrite::append_tail_nospc`, `powerloss::append_unsynced_pl` on v3-integration) |
| LFS3-SYNC-05 | Defect | every CI run | each write-side call fails; close leaves the disk unchanged (`badblocks::truncate_desync` on v3-fix-files) |
| LFS3-SYNC-09 | Untested | every CI run | errors injected into multi-entry overwrites, then sync and check |
| LFS3-SYNC-10 | Partly | every CI run | `lfs3_stat` after flush |
| LFS3-SYNC-16 | Partly | every CI run | close three or more uncreated handles; flag and cleanup |
| LFS3-SYNC-20 | Defect | every CI run | appends that coalesce with the last fragment, each block in turn bad; sync never `LFS3_ERR_INVAL` (`badblocks::append_torn` on v3-integration) |
| LFS3-DIR-02 | Defect | every CI run | `lfs3_mkdir` with names of 1 to 255 bytes at 512 and 1024-byte blocks |
| LFS3-DIR-05 | Defect | every CI run | rename a directory into its own subtree (`dirs::mv_subtree` on v3-fix-api) |
| LFS3-DIR-11 | Defect | every CI run | tell/seek round trip at every position (`dread::seek_tell` on v3-fix-api) |
| LFS3-DIR-18 | Untested | every CI run | 32 empty directories in at most 4 blocks |
| LFS3-ATTR-05 | Defect | every CI run | attributes of 0 to `block_size` bytes on root and file, 512 and 4096-byte blocks |
| LFS3-ATTR-06 | Untested | every CI run | root attributes across remount and mroot extension |
| LFS3-ATTR-07 | Untested | every CI run | all 256 attribute types on one file |
| LFS3-ATTR-10 | Partly | every CI run | `buffer_size = LFS3_ERR_NOATTR` removes at sync |
| LFS3-ATTR-12 | Defect | every CI run | open with `LFS3_A_RDONLY | LFS3_A_LAZY` (`attrs::fattr_rdonly` on v3-fix-files) |
| LFS3-ATTR-14 | Tested | every CI run | zeros past a short attribute, `attrs::fattr_zerofill` (245b84c9) |
| LFS3-KV-04 | Defect | every CI run | `lfs3_set` above `file_limit` (`kv::set_fbig` on v3-fix-files) |
| LFS3-KV-05 | Partly | every CI run | `lfs3_get`/`lfs3_size` on a directory |
| LFS3-KV-08 | Untested | every CI run | `lfs3_get` on an uncreated file |
| LFS3-KV-09 | Untested | every CI run | one commit for a small new `lfs3_set` |
| LFS3-ALLOC-04 | Untested | every CI run | fill to NOSPC, remove half, write again |
| LFS3-ALLOC-05 | Untested | every CI run | fill a gbmap filesystem to NOSPC, then remove |
| LFS3-ALLOC-08 | Untested | every CI run | allocation before the first checkpoint on a nearly full disk |
| LFS3-ALLOC-11 | Defect | every CI run | `LOOKGBMAP_THRESH` 0, 1, `BLOCK_COUNT/4` |
| LFS3-ALLOC-12 | Partly | nightly | gbmap against traversal after every remount |
| LFS3-ALLOC-15 | Untested | every CI run | gbmap images across builds |
| LFS3-ALLOC-16 | Untested | nightly | `lfs3_fs_usage` bounded over 10,000 rewrites with the gbmap |
| LFS3-ALLOC-17 | Partly | every CI run | `lfs3_fs_usage` against distinct traversed blocks |
| LFS3-PRE-02 | Partly | every CI run | no erase of pre-erased blocks at allocation |
| LFS3-PRE-03 | Untested | every CI run | flipped bit in a pre-erased block: skipped |
| LFS3-PRE-04 | Untested | every CI run | remount without `LFS3_M_REVPERTURB` |
| LFS3-PRE-05 | Untested | nightly | reentrant gc pre-erase with an emubd prog-once check |
| LFS3-PRE-06 | Defect | every CI run | ERASEERROR block in the known window (`badblocks::preerase` on v3-fix-alloc) |
| LFS3-PRE-07 | Untested | nightly | reentrant data writes into pre-erased blocks, on-disk window check |
| LFS3-PRE-08 | Untested | every CI run | ERASENOOP with pre-erase and CKPROGS |
| LFS3-PRE-09 | Untested | nightly | emubd tear after the first `prog_size` bytes, `PCACHE_SIZE > PROG_SIZE` (`powerloss::append_pl`, `powerloss::preerase_pl_fuzz` on v3-integration) |
| LFS3-GC-01 | Partly | every CI run | per-call work bound |
| LFS3-GC-02 | Defect | every CI run | `GC_STEPS=-1` through and past NOSPC with every work flag; an mdir that compaction cannot shrink |
| LFS3-GC-06 | Untested | every CI run | no handle left after `lfs3_fs_ck` |
| LFS3-GC-11 | Partly | every CI run | block types from `LFS3_T_MTREEONLY` |
| LFS3-GC-14 | Partly | every CI run | `GC_COMPACT_THRESH=-1` |
| LFS3-GC-17 | Planned | every CI run | `repair::passes`: reads per block with `ck_passes` 1 to 3 |
| LFS3-GC-18 | Planned | every CI run | `repair::retries`: READFLIP and MANUAL blocks with `ck_retries` 0 and 16 |
| LFS3-MOUNT-03 | Defect | every CI run | `block_count` 1 (2 with the gbmap) refused before any bd operation |
| LFS3-MOUNT-09 | Defect | before v3-beta | configuration tags 0x0100, 0x0130, 0x0132, 0x0133 |
| LFS3-MOUNT-11 | Untested | every CI run | image without a geometry tag |
| LFS3-MOUNT-12 | Partly | every CI run | format with `name_limit` 32 and `file_limit` 1000; `lfs3_fs_stat` |
| LFS3-MOUNT-13 | Untested | before v3-beta | image without limit tags |
| LFS3-MOUNT-14 | Defect | every CI run | B-RO mount with a zeroed `lfs3_t` |
| LFS3-MOUNT-16 | Untested | every CI run | counters and valgrind on failed mounts |
| LFS3-MOUNT-17 | Defect | every CI run | counters over a whole `LFS3_M_RDONLY` mount (`attrs::fattr_rdonly_file`, `fsync::desync_wdrs` on v3-fix-files) |
| LFS3-MOUNT-18 | Partly | every CI run | counters around `lfs3_unmount` |
| LFS3-MOUNT-19 | Defect | every CI run | `LFS3_I_RDONLY` in B-DEF and B-RO |
| LFS3-MOUNT-21 | Defect | every CI run | `lfs3_fs_grow` beyond `cfg->block_count` |
| LFS3-MOUNT-22 | Untested | every CI run | failed grow, then fill; window range |
| LFS3-MOUNT-24 | Planned | before v3-beta | v0.0 image refused by the release |
| LFS3-MOUNT-25 | Untested | every CI run | v2.11 image fixture |
| LFS3-MOUNT-27 | Partly | every CI run | plain mount after formatting with every flag |
| LFS3-CFG-01 | Defect | every CI run | zeroed-configuration define set |
| LFS3-CFG-02 | Untested | every CI run | death test: invalid sizes |
| LFS3-CFG-03 | Untested | before v3-beta | death test: `block_size` below the minimum |
| LFS3-CFG-04 | Untested | every CI run | `block_recycles` bounds |
| LFS3-CFG-05 | Defect | every CI run | `gc_compact_thresh` checks in B-DEF |
| LFS3-CFG-06 | Untested | every CI run | unknown `gc_flags` bits |
| LFS3-CFG-07 | Untested | every CI run | `FCACHE_SIZE=0` with a `malloc(0)` that returns NULL |
| LFS3-CFG-08 | Partly | every CI run | per-file caches of 1, 16 and 4096 bytes |
| LFS3-CFG-09 | Untested | every CI run | build check: `LFS3_NAME_MAX=1023` |
| LFS3-CFG-15 | Untested | every CI run | emubd prog-once check over every suite |
| LFS3-RES-01 | Untested | every CI run | static buffers and an allocator that fails when called; B-NM |
| LFS3-RES-02 | Untested | every CI run | CI size job (ctx, structs) |
| LFS3-RES-03 | Untested | every CI run | CI size job (stack) |
| LFS3-RES-04 | Untested | every CI run | CI size job (code) |
| LFS3-RES-05 | Untested | every CI run | CI size job (stack bounds) |
| LFS3-RES-06 | Defect | every CI run | failing allocations in `lfs3_init` |
| LFS3-RES-07 | Untested | before v3-beta | sparse device of 2^31 - 1 blocks |
| LFS3-RES-08 | Untested | every CI run | counting allocator per open file |
| LFS3-PERF-01 | Untested | nightly | ratio check over `bench_rbyd` compaction |
| LFS3-PERF-02 | Untested | nightly | ratio check over `bench_rbyd` lookup |
| LFS3-PERF-03 | Untested | nightly | ratio check over `bench_wt` random |
| LFS3-PERF-04 | Untested | nightly | ratio check over `bench_dir` |
| LFS3-PERF-05 | Untested | nightly | ratio check across block sizes |
| LFS3-PERF-06 | Partly | nightly | bench: bytes per small append and sync at `prog_size` 256 |
| LFS3-PERF-07 | Defect | nightly | count `cfg->sync` calls per file sync |
| LFS3-PERF-08 | Partly | nightly | in-tree W-LOG bench with a v2.11.3 reference, 1 row/s (`bench_wlog_fresh`) |
| LFS3-PERF-09 | Defect | nightly | in-tree W-LOG bench with a v2.11.3 reference, 50 rows/s (`bench_wlog_fresh`) |
| LFS3-PERF-10 | Partly | nightly | in-tree W-LOG bench with pre-erase: erases per call (`bench_wlog_fresh`) |
| LFS3-PERF-11 | Partly | every CI run | data-block erases per small synced append |
| LFS3-PERF-12 | Untested | nightly | CI bench-diff job |
| LFS3-PERF-13 | Untested | nightly | reads of the first allocation after mount, with the gbmap |
| LFS3-THR-01 | Tested | every CI run | `threadsafe::locks` (NEW-89), and the runner's counting `lock`/`unlock` over every suite in B-TS (`make test-threadsafe`), on `v3-r8` (ecf44d33) |
| LFS3-THR-02 | Tested | every CI run | failing `lock`/`unlock` in `threadsafe::*` (NEW-89); `scripts/ckerrs.py` over the B-TS recordings, on `v3-r8` (ecf44d33) |
| LFS3-THR-03 | Untested | nightly | two filesystems on two threads under ThreadSanitizer |
| LFS3-BUILD-01 | Partly | every CI run | `-Werror` builds with GCC, clang and the cross compilers |
| LFS3-BUILD-02 | Defect | every CI run | B-BIG build |
| LFS3-BUILD-03 | Defect | every CI run | `LFS3_CKDATACKSUMS` builds |
| LFS3-BUILD-04 | Defect | every CI run | `LFS3_RDONLY` combination matrix |
| LFS3-BUILD-05 | Defect | every CI run | `LFS3_PMUL_CRC32C` build with `ck::crc32c*` |
| LFS3-BUILD-06 | Defect | every CI run | `LFS3_DBG*` matrix |
| LFS3-BUILD-07 | Untested | every CI run | `LFS3_PREERASE` `#error` check |
| LFS3-BUILD-08 | Defect | every CI run | `LFS3_CFG` build |
| LFS3-BUILD-09 | Defect | every CI run | string-fallback unit test |
| LFS3-BUILD-10 | Defect | every CI run | `nm` symbol check |
| LFS3-BUILD-11 | Defect | every CI run | `-Werror` build for arm-none-eabi |
| LFS3-BUILD-12 | Untested | nightly | forced flags in each B-YES-x build |
| LFS3-BUILD-18 | Untested | nightly | `LFS3_NAME_MAX=32`, `LFS3_FILE_MAX=65535` build |
| LFS3-BUILD-19 | Untested | every CI run | B-RO reads images written by B-DEF and B-YGB |
| LFS3-BUILD-20 | Untested | every CI run | logging macro build matrix |
| LFS3-CI-05 | Untested | nightly | nightly power-loss workflow |
| LFS3-CI-08 | Defect | every CI run | tooling job on Linux and macOS |
| LFS3-CI-11 | Untested | nightly | nightly geometry workflow |
| LFS3-DOC-02 | Planned | before v3-beta | SPEC-based reader cross-check |
| LFS3-DOC-03 | Planned | before v3-beta | README example compile job |
| LFS3-DOC-20 | Tested | every CI run | `make test-dbg` (cc19a932) |

### 9.2 Existing tests in a new environment

| ID | Status | When | Environment needed |
|---|---|---|---|
| LFS3-PL-02 | Partly | nightly | every reentrant case with `POWERLOSS_BEHAVIOR` 1 to 3 (LFS3-CI-05) |
| LFS3-PL-21 | Partly | nightly | `relocations::*_pl_fuzz` with PLB-TORN (LFS3-CI-05) |
| LFS3-PL-23 | Untested | nightly | `-P'permute(1)'` (LFS3-CI-05) |
| LFS3-PL-24 | Untested | nightly | `-P'permute(2)'` (LFS3-CI-05) |
| LFS3-PL-27 | Defect | every CI run | fix the bug and remove the exclusion |
| LFS3-INT-05 | Partly | every CI run | builds with each crc32c option |
| LFS3-INT-15 | Partly | every CI run | B-BIG (LFS3-CI-03) |
| LFS3-INT-16 | Partly | every CI run | B-BIG (LFS3-CI-03) |
| LFS3-INT-17 | Partly | every CI run | B-BIG (LFS3-CI-03) |
| LFS3-INT-18 | Partly | every CI run | B-BIG (LFS3-CI-03) |
| LFS3-INT-20 | Defect | every CI run | B-BIG once F-2 is fixed (LFS3-CI-03) |
| LFS3-INT-23 | Defect | every CI run | B-BIG with `ck::ckparity_btree_append` (on v3-fix-parity) |
| LFS3-FAIL-03 | Partly | every CI run | B-BIG (LFS3-CI-03) |
| LFS3-FAIL-05 | Partly | every CI run | B-BIG (LFS3-CI-03) |
| LFS3-FAIL-06 | Partly | every CI run | B-BIG (LFS3-CI-03) |
| LFS3-FAIL-08 | Partly | every CI run | B-BIG (LFS3-CI-03) |
| LFS3-FAIL-09 | Partly | every CI run | B-BIG (LFS3-CI-03) |
| LFS3-FAIL-10 | Partly | every CI run | B-BIG (LFS3-CI-03) |
| LFS3-FAIL-11 | Partly | every CI run | B-BIG (LFS3-CI-03) |
| LFS3-FAIL-20 | Untested | nightly | B-YGB, with the B-tree cases no longer excluded |
| LFS3-META-02 | Untested | nightly | build with `LFS3_DBGRBYDBALANCE` |
| LFS3-META-16 | Partly | nightly | G-ALL (LFS3-CI-11) |
| LFS3-ALLOC-06 | Partly | nightly | `LOOKAHEAD_SIZE` matrix |
| LFS3-ALLOC-09 | Partly | every CI run | B-YGB and B-BIG |
| LFS3-ALLOC-10 | Partly | every CI run | B-YGB |
| LFS3-ALLOC-13 | Partly | every CI run | a `LFS3_GBMAP` build |
| LFS3-ALLOC-14 | Partly | every CI run | a `LFS3_GBMAP` build |
| LFS3-PRE-01 | Partly | every CI run | B-BIG (LFS3-CI-03) |
| LFS3-GC-03 | Partly | every CI run | B-BIG (LFS3-CI-03) |
| LFS3-GC-04 | Partly | every CI run | B-BIG (LFS3-CI-03) |
| LFS3-GC-13 | Partly | every CI run | B-BIG (LFS3-CI-03) |
| LFS3-MOUNT-01 | Partly | nightly | G-ALL in B-DEF and B-YGB (LFS3-CI-11) |
| LFS3-MOUNT-26 | Untested | every CI run | `compat::*` with each earlier release linked as `LFSP` |
| LFS3-CFG-10 | Partly | nightly | `CRYSTAL_THRESH` matrix |
| LFS3-CFG-11 | Partly | nightly | `SHRUB_SIZE` matrix |
| LFS3-CFG-12 | Partly | nightly | `FRAGMENT_SIZE` matrix |
| LFS3-CFG-13 | Untested | nightly | G-EMMC and G-NAND |
| LFS3-CFG-16 | Partly | every CI run | B-BIG with `GC_LOOKAHEAD_THRESH` matrix |
| LFS3-CFG-17 | Partly | every CI run | B-BIG with `GC_LOOKGBMAP_THRESH` matrix |
| LFS3-BUILD-13 | Defect | every CI run | B-BIG (LFS3-CI-03) |
| LFS3-BUILD-14 | Tested | every CI run | B-YGB (LFS3-CI-03), passes at e529bb20 |
| LFS3-BUILD-15 | Untested | nightly | each B-YES-x build |
| LFS3-BUILD-16 | Tested | every CI run | `make test-release` (B-NA) and `mount::noassert` (e529bb20) |
| LFS3-BUILD-17 | Untested | nightly | B-NB and B-NS |
| LFS3-CI-01 | Defect | every CI run | v3 workflow |
| LFS3-CI-02 | Defect | every CI run | v3 workflow, cross architectures |
| LFS3-CI-03 | Untested | every CI run | v3 workflow, feature builds |
| LFS3-CI-04 | Tested | every CI run | jobs `test-valgrind` and `test-sanitize` (41165ec9) |
| LFS3-CI-06 | Defect | every CI run | v3 workflow, sizes |
| LFS3-CI-07 | Defect | every CI run | v3 workflow, coverage |
| LFS3-CI-09 | Tested | every CI run | job `test-sanitize`, leaks included (6fad1a62, 41165ec9) |


## Appendix A. Defect register

Refs of the form `2-files B1` or `3-alloc §8` point into the analysis notes
this document was written from (`1-metadata`, `2-files`, `3-alloc-failures`,
`4-integrity-api`, `5-verification-roadmap`). Every defect that a Status
field cites is summarised here so that this document stands on its own.

Evidence: **probe** means a standalone program reproduced it; **run** means
a test or build run showed it; **code** means the code establishes it without
doubt; **suspected** means the code suggests it but nothing has reproduced
it; **maintainer** means the maintainer records it.

The last column lists fixes on our branches of the fork. None of them is
upstream yet. Requirement status always describes `b10efaa`.

### A.1 Fixed on v3-fixes, and test-code defects

| Ref | Defect | Where | Evidence | Requirements | Fix |
|---|---|---|---|---|---|
| F-1 | `lfs3_fromle32` shifted a promoted `int` into the sign bit (undefined behaviour) | `lfs3_util.h:498-503` | code | GEN-03 | v3-fixes `8b2a82f` |
| F-2 | `LFS3_CKDATACKSUMS`, and so `LFS3_BIGGEST`, did not compile: `data.u.disk` used on a pointer, since `321e33d` | `lfs3.c:1736, 1808` | run | INT-20, BUILD-02, BUILD-03, BUILD-13 | v3-fixes `e4c046b` |
| F-3 | `LFS3_FROM_BRANCH` encoded a 13-byte branch into an 8-byte buffer; `-Warray-bounds` in every build | `lfs3.c:3505-3511` | run | GEN-03, BUILD-01 | v3-fixes `ba31df7` |
| F-4 | Test: use-after-free in `grow::incr_spam_uzd_fuzz` (180 baseline failures) | `tests/test_grow.toml` | run | MOUNT-23, CI-09 | v3-fixes `2574f54` |
| F-5 | Test: sim array overflows in `badblocks::alternating_*` and the rename paths | `tests/test_badblocks.toml` | run (glibc abort, ASan) | FAIL-01, CI-09 | v3-fixes `9ec4c44`, `55b2396` |
| F-6 | Test: rattr list without terminator in `files::zero_btree` (1 baseline failure) | `tests/test_files.toml:1241-1243` | run | FILE-22, CI-09 | v3-fixes `029db13` |
| F-7 | Test: unterminated names in `mtree::opened_relocate_l` and `_r` | `tests/test_mtree.toml` | run (ASan) | META-13, CI-09 | v3-fixes `55b2396` |
| F-8 | Test: buffer overread in `mtree::truncated_tag` | `tests/test_mtree.toml` | run (ASan) | INT-01, CI-09 | v3-fixes `55b2396` |
| F-9 | Test: always-true error check in `ck::spam_uz_fuzz`, so its model was never updated after a sync | `tests/test_ck.toml` | code | FAIL-10, CI-09 | v3-ci `77884c9` |
| F-10 | Test: string initializers without room for NUL in `test_rbyd` (246 warnings on newer compilers) | `tests/test_rbyd.toml` | run | BUILD-01 | v3-ci `1b89fb4` |

### A.2 Found while writing this document

| Ref | Defect | Where | Evidence | Requirements | Fix |
|---|---|---|---|---|---|
| D-1 | The `gc_preerase_count` comment does not say that pre-erase needs `LFS3_REVPERTURB` and a mount with `LFS3_M_REVPERTURB` | `lfs3.h:608-620` | code | DOC-04 | none |
| D-2 | With `LFS3_M_CKMETAPARITY` and without `LFS3_M_CKFETCHES`, `lfs3_bd_readtag` parity-checks CKSUM tags during quick fetches. The byte after a CKSUM tag is the next commit's valid bit, or erased state that the perturb bit makes intentionally invalid, so every B-tree commit to an rbyd not fetched since mount returns `LFS3_ERR_CORRUPT`. Appends to B-tree files fail after a remount, and mounts with `LFS3_M_PREERASE` fail: 1275 permutations of `mount::flags` and `mount::format_flags` in B-BIG | `lfs3.c:1368-1376` | run | INT-23, GC-13, BUILD-13 | v3-fix-parity `f90e132` (`ck::ckparity_btree_append`) |
| D-3 | After a prog fails with something other than `LFS3_ERR_CORRUPT` partway through a crystallization that resumes a file's leaf block, the leaf still claims the block is erased from where the crystallization started, and the next sync progs those bytes again | `lfs3_file_crystallize_` | run (`badblocks::error_then_sync`, `LFS3_ERR_IO` on the 146th prog) | SYNC-09, CFG-15 | `4968164e` |
| D-4 | An mdir split that finds blocks for its first sibling but not for the second, or not for the mtree node, fails with `LFS3_ERR_NOSPC` instead of compacting in place, so `lfs3_remove` fails on a nearly full disk | `lfs3_mdir_commit_` | run (`badblocks::error_then_sync` with `PROG_SIZE=16`) | ALLOC-04, SYNC-09 | `ce2a68d6` |
| D-5 | With `LFS3_M_CKMETAPARITY`, a flipped continuation bit in a tag's leb128 weight or size reframes the tag and passes the parity check half the time, and a re-fetch while mounted silently falls back to an older commit when a newer one fails its checksum; reads return wrong data without an error | `lfs3_bd_readtag`, `lfs3_rbyd_fetch_` | run (NEW-62 `ck::readflip_spam`, pending) | INT-19, FAIL-09 | resolved by restating LFS3-INT-19 and LFS3-FAIL-09; the re-fetch fallback is issue #6 |
| D-6 | With `LFS3_M_CKFETCHES`, a B-tree node is verified against its stored checksum when it is fetched, and the lookup then reads its tags from the device again, so a bit that reads differently on that later read is not covered; the mroot is not fetched again while mounted, and mdirs have no stored checksum. 15 of 604 class 1 rounds of `ck_readflip::spam` missed with `CK=1` | `lfs3_branch_fetch`, `lfs3_rbyd_lookupnext_` | run (NEW-62) | INT-25 | resolved by narrowing LFS3-INT-25 to flips present at a fetch |
| D-7 | Near the 31-bit file limit, rid and bid sums in a file's tree overflow `int32_t`: `lfs3_rbyd_estimate` tests `rid_ > a_rid + weight_ - 1` while compacting a shrub or B-tree node, and `lfs3_btree_traverse` reports an inner node's bid as `btrv->bid + rid__`, which is also wrong for every node but its parent's first. Signed overflow is undefined behaviour, so the compiler may miscompile the bounds | `lfs3_rbyd_estimate`, `lfs3_btree_traverse` | run (UBSan: 16 permutations of `fwrite::filemax`, 2024 of `fwrite::filemax_fuzz`; B-DEF: 299 of `fwrite::filemax_fuzz` see the wrong inner-node bid) | FILE-17, GEN-03 | `82ab4f07`, `b5888089` (issue #22) |
| D-8 | A commit that only removes still splits an mdir whose compaction estimate is over half a block. Splitting an inlined mroot whose root attrs fill its block moves its entries to a new mdir and needs an mtree the mroot has no room for, so `lfs3_removeattr("/")` fails with `LFS3_ERR_NOSPC` every time; splitting any other mdir needs new blocks and an mtree update that a full disk or a full mroot may not take, and can overflow the mtree's B-tree split | `lfs3_mdir_commit__`, `lfs3_mdir_commit_` | run (`mtree::commit_too_big` with `-DSEED='range(4096)'`: 44 of 12,288 in B-DEF) | DEG-05 | `819e1a10` (issue #23) |
| D-9 | An mtree B-tree split can leave the rattrs of an mdir split, two mdir pointers and the new mdir's first name, no room in the sibling they go to: on 512-byte blocks, with names within `name_limit` (132 bytes), a sibling held 378 bytes after compaction, and `lfs3_mkdir` tripped `LFS3_ASSERT(err != LFS3_ERR_RANGE)` in `lfs3_btree_commit_`. The `name_limit` bound assumes each half of a split fits in half a block, which a few large names in one node make untrue: the split balances what the node holds, not what the commit adds. With `LFS3_NO_ASSERT` the RANGE is taken for a full root, and a new root is tried on block after block through the whole disk before `lfs3_mkdir` returns `LFS3_ERR_NOSPC` | `lfs3_btree_commit_` | run (`mtree::commit_too_big` with `ERASE_SIZE=512`, `SEED=637`, in B-DEF and B-BIG; with `LFS3_NO_ASSERT`, about 3,900 root allocations) | META-03, DIR-02, ERR-06 | `e38b42ae` (issue #25) |

### A.3 From the analyses

| Ref | Defect | Where | Evidence | Requirements | Fix |
|---|---|---|---|---|---|
| 1-meta 0.1 | `lfs3_dir_seek(0)` and `(1)`: `off - 2` wraps and the cursor walks to the end of the mtree | `lfs3.c:12241` | probe | DIR-11 | v3-fix-api `cc4acb9` (`dread::seek_tell`) |
| 1-meta 0.2 | A commit larger than the space left after compaction trips `LFS3_ASSERT(err != LFS3_ERR_RANGE)`: `lfs3_mkdir` with 184-255-byte names on 512-byte blocks, `lfs3_setattr` of about 2500 bytes or more on 4096-byte blocks | `lfs3.c:8777` (also 8761, 8993, 9006, 9228, 9281, 9329) | probe | META-03, DIR-02, ATTR-05 | none |
| 1-meta 0.3 | Big-endian hosts: `lfs3_mdir_fetch` reads into `revs[0]` and converts `revs[i]`, so it can pick the older block of a pair (also 3-alloc B8, 4-api R13) | `lfs3.c:7862-7869` | code | META-04, GEN-02 | none |
| 1-meta 0.4 | Unknown configuration tags below 0x013b are ignored (also 4-api R9) | `lfs3.c:15750-15764` | probe | MOUNT-09 | none |
| 1-meta 0.5 | `lfs3_fs_consumegdelta` does not check the GBMAPDELTA lookup error and reads stale or uninitialised data (also 3-alloc B4, 4-api R14) | `lfs3.c:7744-7755` | code | META-11 | none |
| 1-meta 0.7b | `LFS3_PMUL_CRC32C` does not compile: `lfs3_fromle32_` is undefined | `lfs3_util.c:210` | run | BUILD-05 | none |
| 1-meta 0.7c | Under `LFS3_CKDATACKSUMS` the B-tree BNAME rattr and the stickynote-to-file name supply too few arguments; AddressSanitizer reports a stack-buffer-overflow in about 150 `btree::find*` permutations in B-BIG | `lfs3.c:6044, 3461-3465` | run (ASan) | GEN-03, BUILD-13 | v3-fix-api `7f689b8` |
| 1-meta 7.7 | Asserts on on-disk values (`lfs3_data_readshrub`, `lfs3_data_readgrm`, `lfs3_rbyd_fetchck`, mtree leaf checks, `lfs3_mroot_parent`, alt jumps) | various | code | GEN-07 | none |
| 1-meta 7.14 | `gc_compact_thresh` is checked only with `LFS3_GC` but used in every build | `lfs3.c:15139-15147` | code | CFG-05 | none |
| 2-files B1 | After `lfs3_file_fruncate`, a bad prog during a resumed append relocates from a wrapped `block_pos`, copies nothing, and leaves a negative size and a file that reads as `LFS3_ERR_CORRUPT` | `lfs3.c:13644-13662` | probe | FILE-10 | v3-fix-files `b07be9e` (`badblocks::fruncate_append`) |
| 2-files B2 | `LFS3_O_APPEND` writes check `file_limit` against the position before the append | `lfs3.c:14123-14135` | probe | FILE-03 | v3-fix-files `5500063` (`fwrite::append_fbig`) |
| 2-files B3 | `lfs3_file_sync` writes through `LFS3_O_RDONLY` handles, including on an `LFS3_M_RDONLY` mount | `lfs3.c:14553-14608` | probe | MOUNT-17, Q1 | v3-fix-files `b923cd8` (`attrs::fattr_rdonly_file`, `fsync::desync_wdrs`) |
| 2-files B4 | Close after a write error returns 0 and discards the handle's changes (documented behaviour) | `lfs3.c:12876-12879` | probe | SYNC-19 | not a defect |
| 2-files B5 | A write spanning several entries commits them one at a time; an error part way through may leave the handle's tree inconsistent, and a later sync may persist it | `lfs3.c:13213-13232` | suspected | SYNC-09 | none |
| 2-files B6 | `LFS3_DBGBTREECOMMITS` does not compile | `lfs3.c:6969-6983` | run | BUILD-06 | none |
| 2-files B8 | Errors from the graft and checkpoint in truncate and fruncate do not desync the handle | `lfs3.c:14738-14747, 14828-14837` | code | SYNC-05 | v3-fix-files `8c5241d` (`badblocks::truncate_desync`) |
| 2-files B9 | `lfs3_file_rewind` is declared `int` and defined `lfs3_soff_t` | `lfs3.h:1640`, `lfs3.c:14700` | code | BUILD-11 | none |
| 2-files B10 | `lfs3_file_opencfg_` is an undeclared external symbol | `lfs3.c:12590` | code | BUILD-10 | none |
| 2-files B11 | The attribute-flag check uses file-flag helpers, so `LFS3_A_RDONLY \| LFS3_A_LAZY` asserts | `lfs3.c:12810-12816` | code | ATTR-12 | v3-fix-files `1783b37` |
| 2-files B12 | `fragment_size` 0 (the zeroed default) makes the fragment path loop without progress, committing each time | `lfs3.c:14090` | code | CFG-01 | none |
| 2-files B13 | Decoded block pointers, branches and shrubs are not range-checked | `lfs3.c:2352-2390, 5174-5201, 6499-6524` | code | GEN-07 | none |
| 2-files B14 | `lfs3_file_read` asserts `pos + size <= 0x7fffffff` | `lfs3.c:12983` | code | FILE-04 | v3-fix-files `25cfa66` (`files::read_big`) |
| 2-files B18 | A small append and sync issues 7 `cfg->sync` calls | — | probe | PERF-07 | none |
| 2-files B19 | `.github/workflows/test.yml` is the 2022 v2 workflow and cannot pass on v3 | `.github/workflows/test.yml` | code | CI-01, CI-02, CI-04, CI-06, CI-07 | none |
| 3-alloc B1 | `lfs3_alloc_preerase` returns an erase or ecksum error without advancing, so gc, check, and mount or format with the pre-erase flag fail at the same block forever | `lfs3.c:11416-11430` | code | PRE-06 | v3-fix-alloc `3ceb48b` (`badblocks::preerase`) |
| 3-alloc B2 | `lookahead.ckpoint` is unsigned and 0 at mount; an allocation before the first checkpoint wraps it | `lfs3.c:11011, 11160, 15226` | suspected (the wrap is certain, reaching it is not) | ALLOC-08 | none |
| 3-alloc B3 | "Stuck mroot" and a failed final `lfs3_bd_sync` return from `lfs3_mdir_commit_` without reverting gstate | `lfs3.c:9316, 9336, 9346` | code | META-10 | v3-fix-alloc `bd5bb8c` (`badblocks::mrootanchor_stuck`, `badblocks::badsync`; adds emubd `mkbadsync`) |
| 3-alloc B5 | A source read error during compaction or relocation takes the bad-prog path, so each retry erases a new block until NOSPC | `lfs3.c:8705-8822, 8990-9009, 5868-6179, 13449-13622` | suspected | FAIL-17 | none |
| 3-alloc B6 | A failed `lfs3_fs_grow` restores the block count but not the allocation window | `lfs3.c:16985-16997, 10886-10897` | suspected | MOUNT-22 | none |
| 3-alloc B10 | With the gbmap, every commit checkpoints and may need blocks to repopulate, so a remove on a full disk may fail | `lfs3.c:9480-9493, 10797-10808` | suspected | ALLOC-05 | none |
| 3-alloc B11 | `lookgbmap_thresh`: the code tests `known < thresh`, the header says "<=" and "0 only repopulates the gbmap when empty" | `lfs3.c:10800-10802`, `lfs3.h:704-716` | code | ALLOC-11 | none |
| 3-alloc B17 | Pre-erase's erased-state checksum covers only the first `prog_size` bytes, while the first program can be a `pcache_size` flush | `lfs3.c:2491-2502` | code (depends on the hardware model) | PRE-09, Q19 | v3-integration `57493587` (`powerloss::append_pl`, `powerloss::preerase_pl_fuzz`) |
| 3-alloc R3 | `lfs3_fs_ck` may return with its stack traversal still linked | `lfs3.c:16772-16775, 16824-16825` | suspected | GC-06 | none |
| 3-alloc R8 | Header drift: `LFS3_M_REVPERTURB` comment, mkgbmap/rmgbmap returns, `gc_lookgbmap_thresh` text, read-callback CORRUPT, `LFS3_T_PREERASE` | `lfs3.h` | code | DOC-05, DOC-07, DOC-09, DOC-11, DOC-17 | none |
| 4-api R1 | `lfs3_rename` of a directory into its own subtree returns 0 and detaches the subtree | `lfs3.c:11835-12014` | probe | DIR-05 | v3-fix-api `067ebe7` (`dirs::mv_subtree`) |
| 4-api R3 | Large attributes assert, or return the undocumented `LFS3_ERR_RANGE` with `LFS3_NO_ASSERT` | `lfs3.c:9006` | probe | ATTR-05 | none |
| 4-api R4 | `lfs3_fs_grow` beyond `cfg->block_count` succeeds, erases out of range, and makes the image unmountable (also 3-alloc B7) | `lfs3.c:16899-16999` | probe | MOUNT-21, GEN-08 | none |
| 4-api R5 | An `LFS3_RDONLY` build compares uninitialised name and file limits, so a zeroed `lfs3_t` does not mount | `lfs3.c:15231-15243, 15715, 15741` | probe | MOUNT-14 | none |
| 4-api R6 | `LFS3_RDONLY` with `LFS3_CKMETAPARITY`, `LFS3_GBMAP` or `LFS3_GC` does not compile | `lfs3.c:15206-15207, 10483-10509, 15097, 15143-15146` | run | BUILD-04 | none |
| 4-api R7 | `lfs3_set` ignores `file_limit` | `lfs3.c:15045-15071` | probe | KV-04 | v3-fix-files `9c7deb7` (`kv::set_fbig`) |
| 4-api R8 | `lfs3_init` frees pointers it never set when an early allocation fails | `lfs3.c:15175-15222, 15400-15418` | probe | RES-06 | none |
| 4-api R12 | API preconditions are asserts; with `LFS3_NO_ASSERT` a mutating call on a read-only mount proceeds | `lfs3.c:16620` | code | GEN-06 | none |
| 4-api R17 | `lfs3_fromleb128` shifts signed values (undefined behaviour for large fifth bytes) | `lfs3_util.c:37-56` | code | GEN-03 | none |
| 4-api R19 | `LFS3_THREADSAFE` adds `lock` and `unlock`, which are never called | `lfs3.h:495-503` | code | THR-01, THR-02 | v3-r8 `ecf44d33` (`threadsafe::*`) |
| 4-api R20 | `lfs3_util.h` honours `LFS3_CFG`, `lfs3_util.c` checks `LFS3_CONFIG` | `lfs3_util.h:21`, `lfs3_util.c:11` | code | BUILD-08 | none |
| 4-api R21 | The `lfs3_strspn` fallback is wrong for sets of two or more characters | `lfs3_util.h:719-734` | code | BUILD-09 | none |
| 4-api R22 | `lfs3_file_open` is defined under `LFS3_NO_MALLOC`, where the header hides it | `lfs3.h:1511` | code | BUILD-10 | none |
| 4-api R23 | Stale header text: `lfs3_fs_unck`, `lfs3_info.type`, the `gc_compact_thresh` percentage | `lfs3.h:1801-1810, 721, 625` | code | DOC-06, DOC-08, DOC-10 | none |
| 4-api R25 | An `LFS3_RDONLY` build does not report `LFS3_I_RDONLY` | `lfs3.c:15156-15170` | probe | MOUNT-19, Q16 | none |
| 4-api R27 | `dirs::rm_many_2layers` under power loss with `N=4` is excluded as a known bug | `tests/test_dirs.toml:3442` | maintainer | PL-27 | none |
| 4-api R28 | `lfs3_format` with `block_count` 1 writes block 0, then asserts in the bd wrapper | `lfs3.c:570` | probe | MOUNT-03 | none |
| 4-api §1.7 | Attribute type ranges in `lfs3.h` differ from #1111 | `lfs3.h:766-768` | code | DOC-14 | none |
| 5-verif §0.6 | Tooling: `PERFBDGEN` passes `--trace-freq`; `test.py -j` is Linux-only; invalid escape sequences; `test_compat.toml` names a runner flag that does not exist | `Makefile:161-162`, `scripts/test.py` | run | CI-08 | escape sequences: v3-ci `dbbb5e6` |

## Appendix B. Measurements

### B.1 The logging workload W-LOG

W-LOG models a flight logger that streams fixed-size records to one file.
The first measurements (M-1 to M-3) were made by the authors with their own
harness on emubd. The in-tree bench `bench_wlog_fresh`
(`benches/bench_wlog.toml`) reproduces them, and its figures below are the
ones LFS3-PERF-08 to PERF-10 are judged by.

- **Device.** A simulated Winbond W25Q128JV NOR flash: 4096-byte erase
  sectors, 256-byte pages, of which the filesystem uses 8 MiB: `block_size`
  4096, `block_count` 2048. `read_size` 1, 1 KiB caches, `lookahead_size`
  16, `block_recycles` 512 (v2: `block_cycles` 500). Before logging, the
  disk holds 7 other files totalling 77 KiB.
  Simulated time uses the part's typical timings.
- **Workload.** One file. Rows of 22 bytes are produced at r rows per
  second, collected, and passed to `lfs3_file_write` every 200 ms.
  `lfs3_file_sync` runs every second. A run lasts 10 minutes.
- **Variants.** r = 1 and r = 50. `prog_size` 1, 16 or 256. With or
  without `LFS3_GBMAP` and `LFS3_PREERASE`; with pre-erase, the filesystem
  is mounted with `LFS3_M_REVPERTURB` and `lfs3_fs_gc` pre-erases every free
  block during an idle phase before logging starts (the time on the launch
  pad).
- **Reference.** littlefs v2.11.3 running the same workload, with
  `lfs_file_sync` every second: `bench/bench_v2.c` and `bench/model.h` on
  the `v3-notes` branch, built against v2.11.3 (`6cb4e865`) with its
  `prog_size` set to 1, 16 or 256.
- **Metrics.** Erases per minute (erase count over the run, divided by 10).
  Page programs per minute: 256-byte pages touched by each program, which
  is what kiwibd's `bench_progs` counts with the NOR model's `PROG_WIDTH`.
  Longest call: the simulated time of the slowest single `lfs3_file_write`
  or `lfs3_file_sync`.
- **Reproducing.** `make bench-runner BUILDDIR=build LFS3_BIGGEST=1`, then
  `./scripts/bench.py -R build/runners/bench_runner bench_wlog -o
  wlog.csv`. The `log` probe's erases, progs and progged bytes, times
  60/600, are the per-minute figures; `max_call_erases` and `max_call_ns`
  are the per-call ones. Erases and programs do not depend on the build:
  the default build runs the permutations without the gbmap and gives the
  same figures.

| Ref | Rows/s | Driver and configuration | Erases/min | Longest call |
|---|---|---|---|---|
| M-1 | 1 | v2.11.3 | about 63 | |
| M-2 | 1 | v3, `prog_size` 256 | 14.7 | |
| M-2 | 1 | v3, `prog_size` 1 | 4.1 | |
| M-2 | 1 | v3, `prog_size` 1, gbmap, pre-erase | 3.5 | 51 ms (one erase) |
| M-3 | 50 | v2.11.3 | 80.7 | |
| M-3 | 50 | v3, `prog_size` 256 | 89.5 | |
| M-3 | 50 | v3, `prog_size` 1, pre-erase | 4.5 | |

`bench_wlog_fresh` on `v3-integration` (`32eb36e7`), B-BIG, per minute:

| Rows/s | `prog_size` | Configuration | Erases | Page programs | Bytes programmed | Longest call | Most erases in a call | v2.11.3 erases (pages) |
|---|---|---|---|---|---|---|---|---|
| 1 | 1 | | 3.5 | 221.8 | 12349 | 184.6 ms | 4 | 58.7 (558.1) |
| 1 | 1 | gbmap | 3.6 | 223.6 | 12978 | 185.1 ms | 4 | |
| 1 | 1 | gbmap, pre-erase | 2.9 | 224.5 | 13069 | 50.2 ms | 1 | |
| 1 | 16 | | 5.7 | 336.1 | 21286 | 93.9 ms | 2 | 58.7 (551.0) |
| 1 | 16 | gbmap | 5.7 | 335.6 | 21232 | 185.1 ms | 4 | |
| 1 | 16 | gbmap, pre-erase | 5.0 | 339.3 | 21198 | 50.6 ms | 1 | |
| 1 | 256 | | 10.6 | 161.7 | 41395 | 184.6 ms | 4 | 62.7 (562.1) |
| 1 | 256 | gbmap | 10.5 | 160.5 | 41088 | 184.5 ms | 4 | |
| 1 | 256 | gbmap, pre-erase | 6.2 | 160.3 | 41037 | 49.6 ms | 1 | |
| 50 | 1 | | 23.1 | 654.3 | 90526 | 184.6 ms | 4 | 76.7 (838.9) |
| 50 | 1 | gbmap | 23.3 | 657.4 | 91670 | 185.1 ms | 4 | |
| 50 | 1 | gbmap, pre-erase | 3.4 | 677.4 | 92385 | 52.0 ms | 1 | |
| 50 | 16 | | 27.8 | 844.7 | 109077 | 145.3 ms | 3 | 76.7 (831.6) |
| 50 | 16 | gbmap | 27.9 | 843.0 | 109952 | 186.3 ms | 4 | |
| 50 | 16 | gbmap, pre-erase | 5.6 | 859.6 | 110960 | 53.8 ms | 1 | |
| 50 | 256 | | 48.5 | 759.1 | 194330 | 185.9 ms | 4 | 80.7 (843.6) |
| 50 | 256 | gbmap | 48.3 | 753.5 | 192896 | 185.9 ms | 4 | |
| 50 | 256 | gbmap, pre-erase | 15.4 | 777.4 | 199014 | 53.8 ms | 1 | |

At `fd3157e3`, before the changes of B.5, the erases were the first row of
B.5's table, with 86.7 (86.4 with the gbmap) at 50 rows per second and
`prog_size` 256. `bench_wlog_fresh` asserts the bounds of LFS3-PERF-08 to
PERF-10, so `make bench` fails if any permutation exceeds them.

The pre-erase runs first erase all 2023 or 2024 free blocks on the pad.
Every run reads back every row intact. v2.11.3's longest call is 96.6 ms
in every configuration. At `fd3157e3` the bench reproduced M-1 to M-3
within 3%: the one difference, 86.7 against M-3's 89.5, comes from
`57493587`, after which a fetch again trusts an erased-state checksum that
ends exactly at the end of the block, so a near-full rbyd is appended to
rather than compacted.

Observations:

- v3 avoids the sync-padding problem for small `prog_size`: at
  `prog_size` 1 it erases 15 times less than v2 at 1 row per second.
- Erases grow with `prog_size`: 3.5, 5.7 and 10.6 per minute at 1 row per
  second, 23.1, 27.8 and 48.5 at 50, for `prog_size` 1, 16 and 256. v2's
  hardly depend on it.
- With `prog_size` 256, every metadata commit is padded to a 256-byte
  page. At 50 rows per second this cost more erases than v2 until the
  changes of B.5 (LFS3-PERF-09).
- With pre-erase, the slowest call still contained one erase. This is
  expected if it was an mdir compaction, which erases the other block of the
  pair rather than an allocated block (LFS3-PERF-10).
- Pre-erase needs a mount with `LFS3_M_REVPERTURB`, which `lfs3.h` does not
  say next to `gc_preerase_count` (D-1, LFS3-DOC-04).

### B.2 Sync cost of a small append

From the analysis (2-files §1.8), measured on emubd with 4096-byte blocks,
`crystal_thresh` and `fragment_size` 256, `shrub_size` 1024 and
`fcache_size` 64. A 16-byte append followed by `lfs3_file_sync`, on a file
held in a B-shrub, in the same mount:

| `prog_size` | Programmed bytes | Erases | `cfg->sync` calls |
|---|---|---|---|
| 1 | about 206 | 0 | 4 |
| 16 | 464 to 480 (one 16-byte data prog into the existing block and two mdir commits) | 0; one mdir compaction about every 8 syncs | 7 |
| 256 | about 768 (two to three page-padded metadata commits); no data prog until 256 bytes are pending | 0, apart from periodic compaction | |

After the file is closed and reopened, or after a remount, the erased state
of its last data block is lost; the next crystallization either copies the
partial block into a new one or starts a new block.


### B.3 Test-suite runs

Runs of the in-tree suites on Apple silicon (macOS, Apple clang via a
wrapper that drops GCC-only flags), 14 cores, with the default power-loss
schedule (`-Plinear` on reentrant cases).

| Build | Code | Permutations | Failed | Notes |
|---|---|---|---|---|
| default | `b10efaa` + test fixes (`55b2396`) | 634,616 | 0 | |
| `LFS3_BIGGEST` | `55b2396` | 1,083,265 | 1,275 | every failure was the ckparity false positive (D-2) in test_mount_flags and test_mount_format_flags |
| `LFS3_BIGGEST` | `55b2396` + `f90e132` | test_ck 13,488, test_mount 28,878 | 0 | |
| default | `55b2396` + `f90e132` | 634,616 | 0 | |
| `LFS3_BIGGEST`, ASan + UBSan | `55b2396` | full suite | about 1,577 | ckparity (D-2), the BNAME FROM_DATA overread in test_btree_find, and zero-length VLAs in test_alloc_nospc_* (a test-side issue) |

Under GCC 13 on Ubuntu 24.04 (Docker), the default suite at `b10efaa`
failed 56 of 634,616 permutations, all in test_badblocks, where glibc's
FORTIFY checks caught the test sims' own overflow (fixed in `9ec4c44`).
That run has not yet been repeated on the fixed branches.

### B.4 Cost of the wider erased-state checksums

`57493587` widened erased-state checksums from `prog_size` bytes to
`pcache_size` bytes (at least 11), rounded up to `prog_size` and clamped to
the end of the block (Q19, LFS3-PRE-09). The checksum is computed when a
commit ends and checked when a metadata log is fetched and when a
pre-erased block is allocated, so a wider one reads more, up to
`pcache_size - prog_size` more bytes each time; the read cache already holds
some of them. The on-disk format did not change, but a checksum narrower
than the mount's is not trusted: the next commit to that log compacts it,
and a pre-erased block is erased again when it is allocated.

Measured on W-LOG (B.1, `pcache_size` 1024), `v3-integration` (`fd3157e3`)
against the same commit with `57493587`'s `lfs3.c` changes reverted, B-BIG:

| Rows/s | `prog_size` | Bytes read per minute, before → after | Bytes read at mount, before → after | Erases per minute, before → after |
|---|---|---|---|---|
| 1 | 1 | 24424 → 179715 | 4192 → 7363 | 4.1 → 4.1 |
| 1 | 16 | 53869 → 331277 | 6310 → 9575 | 7.6 → 7.6 |
| 1 | 256 | 87164 → 205232 | 6306 → 9299 | 14.7 → 14.7 |
| 50 | 1 | 173908 → 432204 | 8355 → 10529 | 24.6 → 24.6 |
| 50 | 16 | 544176 → 1217057 | 10410 → 12702 | 40.1 → 40.1 |
| 50 | 256 | 1006993 → 1582907 | 6328 → 8787 | 89.5 → 86.7 |

The mount fetches four metadata logs here. Page programs change by at
most 1.2%, and bytes programmed by at most 5%, at `prog_size` 16, where the
checksum's size field, now two bytes instead of one, sometimes pushes a
commit into another 16-byte unit. The gbmap and pre-erase permutations
change the same way. At the bench's 40 ns per byte read (50 MHz quad SPI),
the largest increase, 657 KiB a minute at 50 rows per second and
`prog_size` 16, is 27 ms a minute of reads, against 45 ms for each erase. Erases fall at 50 rows per second and
`prog_size` 256 because `57493587` also lets a fetch trust a checksum that
ends exactly at the end of the block (B.1).

The in-tree benches with their default configuration (NOR model,
`prog_size` 1, `pcache_size` 16, 60 simulated seconds) read 0 to 6% more
(`bench_rt_logging` +6.0%, `bench_wt_logging` +4.9%, `bench_file` +2.1%,
the others below 2%) and program and erase the same. With `pcache_size`
equal to `prog_size` and `prog_size` 11 or more, as in the NAND model,
nothing changes.

The first writes to an older image: `bench_wlog_narrow` logs for 5 minutes
with `pcache_size` equal to `prog_size`, remounts with 1024, and logs for
one more minute; the control remounts with the same 1024. An image written
before `57493587` gives the same figures (checked with a build of each
driver sharing one image). Erases in that minute, narrow → control:

| Rows/s | `prog_size` | Without pre-erase | Gbmap, pre-erase |
|---|---|---|---|
| 1 | 1 | 5 → 5 | 5 → 4 |
| 1 | 16 | 8 → 7 | 8 → 3 |
| 1 | 256 | 15 → 14 | 16 → 4 |
| 50 | 1 | 26 → 24 | 27 → 5 |
| 50 | 16 | 45 → 44 | 45 → 13 |
| 50 | 256 | 99 → 97 | 100 → 37 |

Without pre-erase the cost is one compaction of each metadata log the
first time it is committed to, 0 to 2 erases. With pre-erase, every block
pre-erased under the narrower checksum is erased again when it is
allocated, and gc does not re-erase blocks it already recorded as erased,
so logging runs at the rate without pre-erase until those blocks, about
2000 here, are used up. A mount with a smaller `pcache_size` than the image
was written with costs nothing.

These figures are for `fd3157e3`. After the changes of B.5 (`32eb36e7`),
at `prog_size` 1 and 1 row per second, the mount reads 8987 bytes and
logging 125065 a minute, against 6307 and 21480 with checksums of
`prog_size` bytes, and with pre-erase at 50 rows per second an image with
narrow checksums costs 25 erases in its first minute against 4. `lfs3.h`
quotes these.

### B.5 Large `prog_size` on fast logs: options and the changes made

At 50 rows per second with `prog_size` 256, W-LOG erases more than v2.11.3
(LFS3-PERF-09). Traced on `fd3157e3`, the log is a B-tree whose root is
kept in the mdir. Each second, the flush of the full file cache and the
sync each graft the longer block pointer, which takes two commits because
it carves both the old pointer and the old fragment, and then the bytes
left over, fewer than 256, as a new fragment. At the sync,
crystallization finds nothing new to write into the data block but still
marks it ungrafted, so the unchanged pointer is grafted again. That is
about seven commits a second to the B-tree leaf, each padded to 256 bytes
and each followed by a commit to the root in the mdir, also padded: the
leaf is relocated every 1.4 seconds and the mdir compacted every 2.1
seconds.

Changing how littlefs writes an append-only file was a design decision
left to the fork's maintainer (Q23). Four changes were prototyped in a copy
of `lfs3.c`, and B, C and D adopted: `ec0733b8`, `554e89f9` and `32eb36e7`
on `v3-integration`, which reproduce the prototypes' figures.
`bench_wlog_fresh`, erases per minute, without the gbmap unless marked:

| Change | 1 row/s, `prog_size` 1 / 16 / 256 | 1 row/s, 256, pre-erase | 50 rows/s, `prog_size` 1 / 16 / 256 | 50 rows/s, 256, pre-erase |
|---|---|---|---|---|
| none (`fd3157e3`) | 4.1 / 7.6 / 14.7 | 8.5 | 24.6 / 40.1 / 86.7 | 29.6 |
| A. evict a B-shrub at `shrub_size` instead of `shrub_size/2` | 4.1 / 7.6 / 16.4 | 16.2 | 24.7 / 40.2 / 87.7 | 31.5 |
| B. don't graft a block pointer that crystallization didn't change | 4.1 / 6.3 / 14.5 | 8.6 | 24.6 / 37.3 / 79.3 | 26.7 |
| C. carve a file's last entry and append past it in one commit | 3.5 / 7.0 / 10.8 | 6.4 | 23.1 / 37.1 / 71.1 | 23.1 |
| D. keep an append's unaligned tail in the file cache until the sync | 4.1 / 7.6 / 14.7 | 8.5 | 24.6 / 32.3 / 63.1 | 19.3 |
| B, C and D | 3.5 / 5.7 / 10.6 | 6.2 | 23.1 / 27.8 / 48.5 | 15.4 |
| v2.11.3 | 58.7 / 58.7 / 62.7 | | 76.7 / 76.7 / 80.7 | |

- **A** keeps fragments in the shrub longer. The 1 row per second log
  stays a B-shrub, but its commits then go to the mdir, whose compactions
  pre-erasing can't remove, and the 50 rows per second log outgrows the
  shrub anyway. It costs more than it saves.
- **B** saves a leaf commit and an mdir commit at each sync that wrote a
  fragment. What a sync leaves on disk is unchanged.
- **C** applies where a graft spans entries. Each entry is committed
  separately because entries can be in different leaves, but when the
  carved entry is the file's last, the append that follows goes to the
  same leaf. It helps every `prog_size`, `prog_size` 1 included. What a
  sync leaves on disk is unchanged.
- **D** avoids padded commits that more appends in the same sync will
  replace. When `lfs3_file_write` flushes a full file cache in the middle
  of an append to a data block whose erased state is known, it flushes up
  to the block's last `prog_size` boundary and keeps the rest, fewer than
  `prog_size` bytes, cached, so no fragment is written between syncs.
  `lfs3_file_flush`, `lfs3_file_sync` and close still write everything.
  The kept bytes are unsynced and so not durable either way.

With B, C and D together, every permutation of LFS3-PERF-09 passes, the
worst being 48.5 erases per minute against 80.7, and LFS3-PERF-10 still
holds: at most one erase in any call with pre-erase. As prototypes they
passed the full default suite and all but one of B-BIG's permutations: in
`repair::reuse` with `ERASEFAIL` false, C means fewer commits before the
disk fills, and the block the test frees is reused as the second block of
a new metadata pair, in use but not yet erased, while the test counted its
erases. LFS3-BAD-17 asks for the block to be allocated again, not erased,
so `effb33ca` makes the test accept a block in use. C also makes
LFS3-SYNC-20 hold, and D needs LFS3-FILE-27. On the other
benches, at their default configuration and with `PROG_SIZE` 16 and 256 for
60 simulated seconds, per byte written: `bench_wt_logging` erases 13 to 16%
less and `bench_wt_seq` 5 to 21% less; `bench_wt_random`, `bench_file` and
`bench_dir` change by at most 1.4%; `bench_wt_many` doesn't change. The read
probe of `bench_rt_logging`, a second handle's fruncate and sync, erases
11.5% more per byte at `prog_size` 256, but its writer, outside the probes,
erases less, and the bench as a whole 11.6% less per byte.

On `32eb36e7` the log at 50 rows per second and `prog_size` 256 makes about
three commits a second to its B-tree leaf instead of seven: the leaf is
relocated 17.9 times a minute instead of 42.0, and the mdir compacted 14.4
times instead of 28.5.

### B.6 Cost of the power-loss repair modes

The same device and W-LOG as B.1 (r = 50, 10 minutes, one write session),
plus two small-file workloads: W-SMALL rewrites 64 files of 100 bytes 4
times (256 sessions of open, write, close), and W-APPEND opens one file,
appends 32 bytes and closes it, 1000 times. Each run formats, writes 7
seed files, remounts, then runs the workload. "Clean" ends with
`lfs3_file_close` and `lfs3_unmount`; "cut" drops the `lfs3_t` after the
last sync, as a power loss would. Commits counts `cfg->sync` calls (one per
commit or data flush). Base is `3c0afc90`, before the dirty mark and
settling; A is the default mode, B is `LFS3_M_SETTLE`. The figures predate
the append changes of B.5.

| Workload | `prog_size` | Mode | Session commits | Session erases | Mount erases, clean | Mount erases, cut | Next mount |
|---|---|---|---|---|---|---|---|
| W-LOG | 256 | base | 4,494 | 867 | 0 | 0 | 0 |
| W-LOG | 256 | A | 4,495 | 867 | 0 | 2 | 0 |
| W-LOG | 256 | B | 4,495 | 869 | 3 | 3 | 0 |
| W-LOG | 16 | base | 4,398 | 400 | 0 | 0 | 0 |
| W-LOG | 16 | A | 4,399 | 400 | 0 | 2 | 0 |
| W-LOG | 16 | B | 4,440 | 398 | 3 | 3 | 0 |
| W-SMALL | 256 | base | 331 | 35 | 0 | 0 | 0 |
| W-SMALL | 256 | A | 331 | 35 | 0 | 2 | 0 |
| W-SMALL | 256 | B | 331 | 35 | 13 | 13 | 0 |
| W-SMALL | 16 | base | 328 | 26 | 0 | 0 | 0 |
| W-SMALL | 16 | A | 328 | 26 | 0 | 2 | 0 |
| W-SMALL | 16 | B | 328 | 26 | 10 | 10 | 0 |
| W-APPEND | 256 | base | 3,556 | 365 | 0 | 0 | 0 |
| W-APPEND | 256 | A | 3,556 | 365 | 0 | 2 | 0 |
| W-APPEND | 256 | B | 3,556 | 366 | 3 | 3 | 0 |
| W-APPEND | 16 | base | 3,556 | 228 | 0 | 0 | 0 |
| W-APPEND | 16 | A | 3,556 | 252 | 0 | 2 | 0 |
| W-APPEND | 16 | B | 3,556 | 252 | 3 | 3 | 0 |

Observations:

- The dirty mark rides in commits the session makes anyway: commit counts
  are unchanged. A new file's stickynote is its mark, so W-SMALL costs
  nothing; W-APPEND, which reopens a file for every 32-byte append, sets
  and clears a `DIRTY` tag each time, which brings compactions forward:
  11% more erases at `prog_size` 16.
- A mount in mode A after a power cut settles the dirty file's pair and its
  path: 2 erases here. A clean mount writes nothing, but scans each mdir
  for marks: 275 to 280 reads instead of 146 to 149 on W-LOG, 942 to
  1,431 instead of 434 to 578 on W-SMALL.
- Mode B pays at every read-write mount after writes, clean or not: one
  erase per pair written since the last mount (3 for one file, 10 to 13
  for 64 files spread over many pairs), and a compaction at each pair's
  next write. A mount with nothing written since costs nothing.
- Mount times stay within one erase per settled pair, 45 ms typical on
  this part.

## Appendix C. Coverage index

Where each public function, configuration field and compile-time option is
covered. Requirement IDs drop the `LFS3-` prefix. The flash-failure classes
are mapped at the end of 6.4.

### C.1 Public functions

| Function | Requirements |
|---|---|
| `lfs3_format` | PL-22, FAIL-16, PRE-06, GC-13, MOUNT-02, MOUNT-27, CFG-02, CFG-03, RES-06 |
| `lfs3_mount` | PL-01, PL-20, PL-22, INT-06, INT-12, PRE-06, GC-13, MOUNT-04, MOUNT-05, MOUNT-06, MOUNT-07, MOUNT-09, MOUNT-10, MOUNT-11, MOUNT-12, MOUNT-15, MOUNT-16, MOUNT-19, MOUNT-25, CFG-02, CFG-03, RES-06, BUILD-12 |
| `lfs3_unmount` | FILE-16, FILE-21, DIR-20, GC-16, MOUNT-18 |
| `lfs3_get` | GEN-02, PL-12, FILE-12, SYNC-01, KV-01, KV-05, KV-08, CFG-07 |
| `lfs3_size` | PL-12, KV-02, KV-05, KV-07, KV-08 |
| `lfs3_set` | GEN-06, PL-12, DIR-02, KV-03, KV-04, KV-06, KV-07, KV-09 |
| `lfs3_remove` | GEN-06, PL-09, DIR-03, ALLOC-05 |
| `lfs3_rename` | GEN-06, PL-10, META-03, DIR-04, DIR-05, DIR-06, DIR-07 |
| `lfs3_stat` | GEN-02, PL-07, PL-08, PL-09, PL-10, META-09, FILE-19, SYNC-10, SYNC-13, SYNC-14, DIR-05, DIR-08, DIR-14, PERF-04, DOC-08 |
| `lfs3_getattr` | PL-11, ATTR-01, ATTR-02 |
| `lfs3_sizeattr` | PL-11, ATTR-02, ATTR-03, ATTR-04, ATTR-10, ATTR-13 |
| `lfs3_setattr` | GEN-06, PL-11, META-03, ATTR-01, ATTR-05, ATTR-06, ATTR-08 |
| `lfs3_removeattr` | GEN-06, PL-11, ATTR-03, ATTR-08 |
| `lfs3_file_open` | FILE-16, CFG-07, RES-08, BUILD-10, FILE-14, FILE-15 |
| `lfs3_file_opencfg` | GEN-06, INT-13, META-03, FILE-15, DIR-14 |
| `lfs3_file_close` | PL-04, PL-06, FILE-21, FILE-27, SYNC-19 |
| `lfs3_file_sync` | PL-04, PL-06, SYNC-05, SYNC-06, SYNC-08, SYNC-09, SYNC-17, SYNC-20, FILE-27, MOUNT-17, PERF-06, PERF-07, PERF-10 |
| `lfs3_file_flush` | PL-06, FILE-27, SYNC-05, SYNC-10 |
| `lfs3_file_desync` | SYNC-04 |
| `lfs3_file_resync` | SYNC-07, SYNC-08 |
| `lfs3_file_read` | INT-20, FILE-01, FILE-04 |
| `lfs3_file_write` | PL-06, PL-17, FILE-02, FILE-03, FILE-07, FILE-26, FILE-27, SYNC-05, SYNC-20, PERF-10 |
| `lfs3_file_seek` | FILE-06, FILE-07, FILE-20, FILE-26, KV-04 |
| `lfs3_file_truncate` | PL-13, FILE-08, FILE-18, FILE-26, SYNC-05 |
| `lfs3_file_fruncate` | PL-13, FILE-09, FILE-10, FILE-26, SYNC-05, DOC-18 |
| `lfs3_file_tell` | FILE-02, FILE-06, FILE-08, FILE-09, FILE-20 |
| `lfs3_file_rewind` | FILE-20, BUILD-11 |
| `lfs3_file_size` | FILE-02, FILE-19, FILE-22, SYNC-09 |
| `lfs3_file_ck` | INT-13 |
| `lfs3_mkdir` | GEN-06, PL-08, META-03, DIR-01, DIR-02, DIR-14 |
| `lfs3_dir_open` | DIR-19 |
| `lfs3_dir_close` | DIR-20 |
| `lfs3_dir_read` | GEN-02, PL-07, PL-08, SYNC-13, SYNC-14, DIR-09, DIR-13, DOC-08 |
| `lfs3_dir_seek` | DIR-11 |
| `lfs3_dir_tell` | DIR-11 |
| `lfs3_dir_rewind` | DIR-12 |
| `lfs3_trv_open` | GC-08, GC-10, GC-11 |
| `lfs3_trv_close` | GC-16 |
| `lfs3_trv_read` | INT-14, BAD-13, ALLOC-17, GC-07, GC-08 |
| `lfs3_trv_rewind` | GC-15 |
| `lfs3_fs_stat` | PL-18, PL-19, INT-16, GC-03, MOUNT-10, MOUNT-12, MOUNT-13, MOUNT-19, MOUNT-20, MOUNT-21, BUILD-12 |
| `lfs3_fs_usage` | FILE-18, DIR-18, ALLOC-16, ALLOC-17, BAD-13, DEG-06 |
| `lfs3_fs_cksum` | GEN-02, INT-07, INT-08, INT-09, META-10, DOC-13 |
| `lfs3_fs_mkconsistent` | GEN-06, META-09, META-14, SYNC-15, SYNC-16, GC-12 |
| `lfs3_fs_ck` | GEN-04, PL-19, PL-20, INT-10, INT-11, INT-21, INT-22, FAIL-18, BAD-03, BAD-17, META-03, SYNC-09, DIR-05, PRE-05, PRE-06, GC-02, GC-05, GC-06, GC-17, GC-18, MOUNT-17, CFG-05, DEG-10 |
| `lfs3_fs_gc` | PL-20, INT-15, BAD-10, PRE-05, PRE-06, GC-01, GC-02, CFG-06, DOC-17 |
| `lfs3_fs_unck` | INT-16, DOC-06 |
| `lfs3_fs_grow` | GEN-06, GEN-08, PL-18, BAD-06, MOUNT-20, MOUNT-21, MOUNT-22, MOUNT-23, DOC-16 |
| `lfs3_fs_mkgbmap` | PL-19, ALLOC-13, DOC-07 |
| `lfs3_fs_rmgbmap` | PL-19, ALLOC-05, ALLOC-14, DOC-07 |
| `lfs3_fs_mkbad` | BAD-11, BAD-14 |
| `lfs3_fs_mkgood` | BAD-12 |
| `lfs3_fs_nextbad` | BAD-13, BAD-14, DEG-06 |
| `lfs3_fs_nextsuspect` | BAD-16 |
| `lfs3_crc32c` | INT-05 |
| `lfs3_crc32c_mul` | INT-05 |
| `lfs3_toleb128` | GEN-02, DOC-02 |
| `lfs3_fromleb128` | GEN-03, GEN-07, DOC-02 |

### C.2 `struct lfs3_cfg` and `struct lfs3_file_cfg` fields

| Field | Requirements |
|---|---|
| `context` | CFG-14 |
| `read` | GEN-05, FAIL-19, DOC-11 |
| `prog` | FAIL-01, CFG-15, GEN-08 |
| `erase` | FAIL-02, GEN-08 |
| `sync` | FAIL-18, PERF-07, MOUNT-17 |
| `lock` | THR-01, THR-02 |
| `unlock` | THR-01, THR-02 |
| `read_size` | CFG-01, CFG-02 |
| `prog_size` | PL-15, INT-04, META-01, FILE-11, FILE-24, PRE-09, CFG-01, CFG-02, CFG-10, CFG-13, PERF-06, PERF-07, PERF-08, PERF-09, PERF-14 |
| `block_size` | META-03, ATTR-05, CFG-01, CFG-02, CFG-03, CFG-05, CFG-10, CFG-11, CFG-12, CFG-13 |
| `block_count` | BAD-11, ALLOC-08, ALLOC-11, PRE-01, GC-02, MOUNT-03, CFG-01, CFG-17 |
| `block_recycles` | PL-21, FAIL-14, CFG-01, CFG-04 |
| `rcache_size` | CFG-01, CFG-02 |
| `pcache_size` | PRE-09, CFG-01, CFG-02, PERF-14 |
| `fcache_size` | CFG-01, CFG-07, CFG-08, RES-08, FILE-27 |
| `lookahead_size` | ALLOC-06, CFG-01 |
| `gc_flags` | INT-15, GC-03, CFG-06 |
| `gc_steps` | GC-01, GC-02 |
| `gc_lookahead_thresh` | CFG-16 |
| `gc_lookgbmap_thresh` | CFG-17, DOC-09 |
| `gc_preerase_count` | PRE-01, DOC-04 |
| `gc_compact_thresh` | GC-14, CFG-05, DOC-10 |
| `rcache_buffer` | RES-01 |
| `pcache_buffer` | RES-01 |
| `lookahead_buffer` | RES-01 |
| `name_limit` | META-03, FILE-15, DIR-01, DIR-02, DIR-15 |
| `file_limit` | FILE-03, FILE-06, FILE-26, KV-04 |
| `shrub_size` | KV-09, CFG-01, CFG-11 |
| `fragment_size` | KV-09, CFG-01, CFG-12 |
| `crystal_thresh` | KV-09, CFG-01, CFG-10, PERF-11 |
| `lookgbmap_thresh` | ALLOC-11 |
| `ck_retries` | GC-18, DEG-10, BAD-17 |
| `ck_passes` | GC-17 |
| `lfs3_file_cfg.fcache_buffer` | CFG-08, RES-01 |
| `lfs3_file_cfg.fcache_size` | CFG-08 |
| `lfs3_file_cfg.attrs`, `attr_count` | ATTR-08, ATTR-09, ATTR-10, ATTR-12, PL-11 |
| `lfs3_attr.flags` (`LFS3_A_*`) | ATTR-09, ATTR-12 |

### C.3 Compile-time options

| Option | Requirements |
|---|---|
| `LFS3_RDONLY` | BAD-04, MOUNT-14, MOUNT-19, BUILD-03, BUILD-04, BUILD-19 |
| `LFS3_YES_RDONLY` | BUILD-12, BUILD-15, BUILD-19 |
| `LFS3_GBMAP` | ALLOC-09, ALLOC-10, ALLOC-11, ALLOC-12, ALLOC-13, ALLOC-14, ALLOC-15, ALLOC-16, BAD-01 to BAD-16, BUILD-04, BUILD-14 |
| `LFS3_YES_GBMAP` | PL-18, FAIL-20, ALLOC-13, ALLOC-14, BUILD-14 |
| `LFS3_PREERASE` | PRE-01, PRE-02, PRE-03, PRE-04, PRE-05, PRE-06, PRE-07, PRE-08, PRE-09, PERF-10, BUILD-07 |
| `LFS3_REVPERTURB` | PRE-04, BUILD-07, DOC-04, DOC-05 |
| `LFS3_YES_REVPERTURB` | BUILD-12, BUILD-15 |
| `LFS3_REVNOISE` | BUILD-13, BUILD-15 |
| `LFS3_YES_REVNOISE` | BUILD-12, BUILD-15 |
| `LFS3_CKPROGS` | INT-17, FAIL-03, FAIL-05, FAIL-06, FAIL-08, FAIL-11 |
| `LFS3_YES_CKPROGS` | BUILD-12, BUILD-15 |
| `LFS3_CKFETCHES` | INT-18, INT-24, INT-25, BUILD-04 |
| `LFS3_YES_CKFETCHES` | BUILD-12, BUILD-15 |
| `LFS3_CKMETAPARITY` | INT-19, INT-23, INT-24, FAIL-09, BUILD-04 |
| `LFS3_YES_CKMETAPARITY` | BUILD-12, BUILD-15 |
| `LFS3_CKDATACKSUMS` | INT-20, INT-24, FAIL-09, BUILD-03, BUILD-04 |
| `LFS3_YES_CKDATACKSUMS` | BUILD-12, BUILD-15 |
| `LFS3_GC` | GC-01, GC-02, GC-03, GC-04, INT-15, INT-16, CFG-05, CFG-06, BUILD-04 |
| `LFS3_YES_GC` | BUILD-12, BUILD-15 |
| `LFS3_BLEAFCACHE` | BUILD-04, BUILD-13, BUILD-15 |
| `LFS3_YES_BLEAFCACHE` | BUILD-12, BUILD-15 |
| `LFS3_BIGGEST` | BUILD-02, BUILD-13 |
| `LFS3_YES_FLUSH` | BUILD-12, BUILD-15, DOC-19 |
| `LFS3_YES_SYNC` | BUILD-12, BUILD-15, DOC-19 |
| `LFS3_THREADSAFE` | THR-01, THR-02, THR-03, THR-04 |
| `LFS3_NO_MALLOC` | RES-01, BAD-07, BUILD-04, BUILD-10 |
| `LFS3_NO_STRINGH` | BUILD-09, BUILD-17 |
| `LFS3_NO_BUILTINS` | BUILD-01, BUILD-17 |
| `LFS3_NO_ASSERT` | GEN-06, ATTR-05, RES-04, BUILD-16, DOC-16 |
| `LFS3_NO_DEBUG` | BUILD-20 |
| `LFS3_NO_INFO` | BUILD-20 |
| `LFS3_NO_WARN` | BUILD-20 |
| `LFS3_NO_ERROR` | BUILD-20 |
| `LFS3_NO_LOG` | RES-04, BUILD-20 |
| `LFS3_YES_TRACE` | BUILD-20 |
| `LFS3_TRACE` | BUILD-20 |
| `LFS3_DEBUG` | BUILD-20 |
| `LFS3_INFO` | BUILD-20 |
| `LFS3_WARN` | BUILD-20 |
| `LFS3_ERROR` | BUILD-20 |
| `LFS3_ASSERT` | BUILD-20, DOC-16 |
| `LFS3_UNREACHABLE` | BUILD-20 |
| `LFS3_CFG` | BUILD-08 |
| `LFS3_SMALLER_CRC32C` | INT-05 |
| `LFS3_FASTER_CRC32C` | INT-05 |
| `LFS3_PMUL_CRC32C` | INT-05, BUILD-05 |
| `LFS3_NAME_MAX` | CFG-09, BUILD-18 |
| `LFS3_FILE_MAX` | FILE-17, FILE-26, BUILD-18 |
| `LFS3_BADQ_SIZE` | BAD-07, BAD-08 |
| `LFS3_SUSPECTS_SIZE` | BAD-07, BAD-16 |
| `LFS3_DBGRBYDFETCHES` | BUILD-06 |
| `LFS3_DBGRBYDCOMMITS` | BUILD-06 |
| `LFS3_DBGRBYDBALANCE` | META-02, BUILD-06 |
| `LFS3_DBGBTREEFETCHES` | BUILD-06 |
| `LFS3_DBGBTREECOMMITS` | BUILD-06 |
| `LFS3_DBGMDIRFETCHES` | BUILD-06 |
| `LFS3_DBGMDIRCOMMITS` | BUILD-06 |
| `LFS3_DBGALLOCS` | BUILD-06 |
