# 5 — Verification machinery and roadmap (littlefs v3-alpha)

Tree: `~/Documents/littlefs`, branch `v3-work` = upstream v3-alpha `b10efaa` (2026-03-05).
Sources: `runners/`, `scripts/`, `tests/`, `benches/`, `bd/`, `Makefile`, `.github/workflows/`;
roadmap texts `pr_1111.txt` (PR #1111 body + comments) and `issue_1114.txt` (discussion) in
copies of the upstream threads (not kept). Citations are `file:line`. Numbers marked
"(runner)" come from `./runners/test_runner --summary/-l/-L` on the pre-built default runner
(no `LFS3_*` feature macros). No test case was run for these notes.

---

## 0. Findings that matter for the deliverables

1. **Scale.** 25 suites, 842 cases, 634,616 of 900,921 permutations run by default
   (`-Pnone` + `-Plinear`) (runner). 64 cases are reentrant (power-loss tested), 129 are
   fuzz cases (seeded PRNG, not coverage-guided), 299 are internal (white-box, compiled into
   `lfs3.c`). The PR's figure (23 suites / 784 cases / 804,655 perms / 2,228,513 power losses /
   1323 s, `pr_1111.txt:355-361`) is from an earlier snapshot.
2. **The default build compiles out a large part of the suite.** Without feature macros,
   58 cases run zero permutations: all 19 of `test_gbmap`, 27 of 32 `test_gc`, 10 `test_ck`
   ck* cases, 2 `test_mount` cases. In `test_badblocks`/`test_exhaustion`, only
   `PROGERROR`/`ERASEERROR` run; `READERROR`/`PROGNOOP`/`ERASENOOP` need `LFS3_CKPROGS`
   (1528 perms vs 3 perms, runner with `-DBADBLOCK_BEHAVIOR=n`). CI must build with
   `LFS3_BIGGEST` (`lfs3_util.h:28-60`) and `LFS3_YES_GBMAP` (`tests/test_gbmap.toml:3-6`)
   as well as the default.
3. **Power-loss coverage is narrow in its fault models.** Only `test_powerloss` (4 cases)
   permutes `POWERLOSS_BEHAVIOR` (ATOMIC/SOMEBITS/MOSTBITS/OOO). `POWERLOSS_METASTABLE` is
   implemented in emubd but no test uses it. No test combines power loss with bad blocks: the
   bad-block suites are not reentrant.
4. **v3 has no geometry option (`-G`).** v2 ran 5 built-in geometries by default. v3 runs
   one geometry (read=prog=1, erase=block=4096, 1 MiB) unless `-D` overrides are given.
   `DISK_GEOMETRY` in `runners/test_defines.h:8` is a leftover with no effect on tests. It is
   live only in the bench runner (`runners/bench_defines.h:8-13,22`).
5. **CI on this branch is the 2022 v2 workflow and cannot pass.** Details and fixes are in §3.
   v2 `master` has since moved to `@v4` actions and per-matrix artifact names; v3-work never
   picked that up.
6. **Framework defects found:**
   - `PERFBDGEN` passes `--trace-freq=100` (`Makefile:161-162`), which neither `test.py` nor
     `bench.py` accepts (they take `--trace-runfreq`). Verified: `test.py: error: unrecognized
     arguments: --trace-freq=100`.
   - `test.py -j` uses `os.sched_getaffinity`, which is Linux-only (`scripts/test.py:1432`).
   - `test.py` emits `SyntaxWarning`s for invalid escape sequences on Python ≥3.12
     (`scripts/test.py:242-243,482,...`). A future Python will make these errors.
   - `test_compat.toml:19-21` refers to a runner "-a/--all" flag that does not exist; the
     flag is `--force`.
   - `LFS3_THREADSAFE` only adds `lock`/`unlock` fields to the config (`lfs3.h:495-503`).
     `lfs3.c` never calls them (0 references).
7. **Coverage.** `COVGEN=1` plus `scripts/cov.py` gives line, branch and function coverage
   per function of `lfs3.c` (`-bfunction`). This needs GCC's `gcov --json-format`. Apple's
   llvm gcov has no JSON output, so collect coverage on Linux.
8. **Roadmap.** Every "performance" item is done (2026-02-20). The maintainer's stated
   release blockers are **bad-block tracking** (not in the code; only a TODO at
   `lfs3.c:16132`) and **DESIGN.md + SPEC.md**, followed by a review period
   (`issue_1114.txt:835-838`). The plan is a **v3-beta** with a stable disk format and an
   API still in flux (`issue_1114.txt:842`). Redundancy and dedup are "high risk, low value"
   and will not block (`issue_1114.txt:840`). No stretch goal is expected before release
   (`issue_1114.txt:380`).

---

## 1. Test framework

### 1.1 Pipeline

- **Translation.** `tests/X.toml` → `./scripts/test.py -c X.toml -o X.t.c` (`Makefile:815-816`).
  Source files get internal (`in=`) cases injected: `test.py -c $(TESTS) -s lfs3.c`
  (`Makefile:818-819`), which writes `#line 1 "lfs3.c"` and appends the case functions
  (`scripts/test.py:704-760`).
- **Assert rewriting.** `scripts/prettyasserts.py -Plfs3_` produces `*.t.a.c`
  (`Makefile:809-813`). It rewrites `expr => value;` (`ARROW`, `scripts/prettyasserts.py:63,527`)
  and `assert`/`LFS3_ASSERT` into asserts that print both values.
- **Linking.** All `*.t.a.o` link into `runners/test_runner` (`Makefile:789-790`). Each suite
  becomes a `struct test_suite`, and `test_suites[]` is emitted weak into every generated
  file (`scripts/test.py:767-786`).
- **Ordering.** Suites run in topological order of `after =` (`scripts/test.py:380-403`):
  bd → rbyd → btree → mtree → dirs → gbmap → dread → files → paths → alloc → fwrite →
  fsync → stickynotes → attrs → kv → trvs → gc → mount → ck → compat → grow → badblocks →
  powerloss → relocations → exhaustion (runner `-l`).
- **Runner contract.** `runners/test_runner.h` defines flags INTERNAL/REENTRANT/FUZZ
  (`:97-101`), `TEST_PLS` (power losses so far, `:142`), `TEST_PRNG` (xorshift32,
  `test_runner.c:597-611`), `TEST_PERMUTATION`/`TEST_FACTORIAL` (`:150-154`) and trace
  pause/resume (`:157-161`).
- **Block device.** emubd by default; `TEST_KIWIBD` selects kiwibd, which has no power-loss
  scenarios (`runners/test_runner.h:29-44`, `test_runner.c:2080-2092`).

### 1.2 Writing a suite (TOML keys)

Suite-level keys are parsed at `scripts/test.py:223-345`, case-level keys at `:79-186`.

| key | level | meaning | cite |
|---|---|---|---|
| `[cases.<name>]` | case | one test case; its name must be globally unique | test.py:238-261, 405-436 |
| `code` | both | Suite `code` is a shared prologue (includes, helpers). Case `code` is the body of `void __test__<case>__run(const struct lfs3_cfg *CFG)` | test.py:283, 101, 529-541 |
| `defines` | both | Dict, or list of dicts, of define → value(s). Suite defines apply to every case | test.py:122-186, 297 |
| `if` | both | C predicate(s) over defines and `TEST_PLS`, checked at runtime; false → "skipped" | test.py:86-90, 266; 516-527 |
| `ifdef` / `ifndef` | both | Compile-time gate on `LFS3_*` macros; a gated-out case has `run=NULL` and counts as filtered | test.py:91-100, 272-281, 478-487 |
| `in` | both | Inject the case into a source file (e.g. `'lfs3.c'`) so it can reach static functions; implies `internal` | test.py:103-120, 288 |
| `internal` | both | Flag only; masked by `--no-internal` | test.py:106-109 |
| `reentrant` | both | May run under power-loss scenarios. Non-reentrant cases run only under `none` | test.py:110-113; test_runner.c:780-783, 823-826 |
| `fuzz` | both | Truthy, conventionally the seed define's name (`fuzz = 'SEED'`). Only sets `TEST_FUZZ`, masked by `--no-fuzz` | test.py:114-117 |
| `after` | suite | Ordering dependencies | test.py:290-295 |

**Define values** (`scripts/test.py:149-171`):
- a TOML list, or a comma-separated string (commas inside parentheses are kept);
- `range(stop)`, `range(start,stop[,step])`;
- booleans;
- C expressions as strings that may reference other defines, e.g. `'(GBMAP) ? 3 : 2'` or
  `'BADBLOCK_BEHAVIOR >= LFS3_EMUBD_BADBLOCK_READERROR'`.

The runtime `-D` parser accepts the same syntax (`test_runner.c:2431-2580`).

**Reentrant idiom.** `tests/test_powerloss.toml:34-52` and `tests/test_dirs.toml:1344-1370`
show the pattern: mount, and on failure format then mount ("format once per test"). Every
mutating call tolerates having already happened, e.g.
`assert(!err || (TEST_PLS && err == LFS3_ERR_EXIST))`. Heavy permutations are limited under
power loss, e.g. `if = '!TEST_PLS || N <= 8'`.

### 1.3 Defines and permutations

**Implicit defines** (`runners/test_defines.h:5-36`), with defaults (runner
`--list-implicit-defines`):

| define | default |
|---|---|
| `DISK_SIZE` | 1 MiB |
| `DISK_GEOMETRY` | 0 (unused) |
| `READ_SIZE` | 1 |
| `PROG_SIZE` | 1 |
| `ERASE_SIZE` | 4096 |
| `BLOCK_SIZE` | max(ERASE_SIZE, 512) = 4096 |
| `BLOCK_COUNT` | DISK_SIZE/BLOCK_SIZE = 256 |
| `BLOCK_RECYCLES` | -1 |
| `RCACHE_SIZE` / `PCACHE_SIZE` | 16 / 16 |
| `FCACHE_SIZE` | 16 |
| `LOOKAHEAD_SIZE` | 16 |
| `GC_FLAGS` | LFS3_GC_GC |
| `GC_STEPS` | 0 |
| `GC_LOOKAHEAD_THRESH` / `GC_LOOKGBMAP_THRESH` / `GC_PREERASE_COUNT` | -1 / -1 / -1 |
| `GC_COMPACT_THRESH` | 0 |
| `SHRUB_SIZE` | BLOCK_SIZE/4 |
| `FRAGMENT_SIZE` | min(BLOCK_SIZE/16, 512) |
| `CRYSTAL_THRESH` | BLOCK_SIZE/16 |
| `LOOKGBMAP_THRESH` | BLOCK_COUNT/4 |
| `ERASE_VALUE` | 0xff |
| `ERASE_CYCLES` | 0 |
| `BADBLOCK_BEHAVIOR` | PROGERROR |
| `POWERLOSS_BEHAVIOR` | ATOMIC |
| `BD_SEED` | 0 |

These feed `struct lfs3_cfg` (`:40-72`) and `struct lfs3_emubd_cfg` (`:76-98`). GBMAP,
PREERASE and GC config fields exist only under their macros (`:54-64`).

**Permutation mechanics:**
- **Case permutations.** A `defines` list of k dicts gives k case permutations, crossed
  with the suite defines (`scripts/test.py:173-183`).
- **Mixed-radix expansion.** Within a case permutation, each define has n values. Index p is
  decomposed mixed-radix across the defines (`test_runner.c:320-364`).
- **Recursive defines.** Dependencies between defines are resolved by fixed-point
  iteration, up to `--define-depth` (default 1000, `:181`, error at `:359`).
- **Deduplication.** Permutations whose values match a previous one are dropped (trie,
  `:680-733`; used at `:813`).
- **Precedence**, highest first (`test_define_suite` `:198-293`, `test_define_case`
  `:295-318`):
  1. explicit test id
  2. `-D` override (applies even to case- and suite-level defines)
  3. case define
  4. suite define
  5. implicit define
- **Overrides.** `-DNAME=v1,v2,range(a,b,s)` multiplies permutations (`:384-429`, parse
  `:2431-2580`). Overriding a name the suite does not use is not an error (`:276`).
- **Queries.** `--list-defines`, `--list-permutation-defines`, `--list-implicit-defines`,
  `-Q/--query-define`.

**Test ids.** Format `case:<leb16 (index,value) pairs>[:<powercycles>]`
(`perm_printid` `test_runner.c:651-678`, parse `:2907-3000`), e.g.
`test_grow_incr_spam_uzd_fuzz:s11t12u10g28h2gg4j20k2gg8`. The id pins every permuted define,
including `-D` overrides, and the power-loss cycle list. Any failure can be replayed exactly
with `./runners/test_runner <id>`.

### 1.4 Geometry (the `-G` / `DISK_GEOMETRY` question)

- **v2.** `runners/test_runner.c` on `master` had `-G` with built-ins
  `default{prog 16, block 512}`, `eeprom{1,512}`, `emmc{-,512}`, `nor{1,4096}` and
  `nand{4096,32768}`. All 5 ran by default (`git show master:runners/test_runner.c` lines
  1283-1293), and CI used `-Gdefault` for valgrind (`.github/workflows/test.yml:374`).
- **v3.** There is no `-G` in `test_runner.c` or `test.py` (option table
  `test_runner.c:2244-2281`). The only geometry is the implicit defaults above: NOR-like,
  256 × 4 KiB.
- **Geometry in v3 is expressed as `-D` overrides.** Override the *inputs*
  (`READ_SIZE`, `PROG_SIZE`, `ERASE_SIZE`, `DISK_SIZE`) rather than `BLOCK_SIZE`, so that
  suites which pin `BLOCK_SIZE` (e.g. `test_rbyd` 32 KiB, `tests/test_rbyd.toml` suite
  defines) keep it. `-D` beats case and suite defines, so an override of `BLOCK_SIZE` would
  silently rewrite them.
- **Measured permutation counts** (runner `--summary`, default powerloss set):

  | geometry | `-D` overrides | perms (filtered/total) |
  |---|---|---|
  | eeprom-like | `READ_SIZE=1 PROG_SIZE=1 ERASE_SIZE=512` | 605,998/852,051 |
  | prog16 | `READ/PROG=16, ERASE=512` | 605,998/852,051 |
  | emmc-like | `512/512/512` | 220,567/852,036 |
  | v2-nand | `4096/4096/32768` | 203,774/852,051 |
  | w25n01gv-ish | `2048/2048/131072, DISK_SIZE=16 MiB` | 193,694/852,051 |

  `-DREAD_SIZE=1,16 -DPROG_SIZE=1,16` gives 2,332,100 perms: a ~3.8× multiplier, because
  the override replaces each case's own `PROG_SIZE` lists.
- **Bench geometry.** `DISK_GEOMETRY` 0 = NOR (w25q64jv) and 1 = NAND (w25n01gv) select
  sizes, widths and timings (`runners/bench_defines.h:8-13,60-141`). Make switches:
  `BENCH_NOR`, `BENCH_NAND`, `BENCH_PERBYTE` (`Makefile:198-207`).

### 1.5 Filtering and masking

- **Compile time.** `ifdef`/`ifndef` gates produce `run=NULL` (filtered).
- **Runtime.** `if` predicates are evaluated with `TEST_PLS = (scenario != none)`
  (`test_runner.c:870, 2167`), which lets a case shrink its permutations under power loss.
- **Filtered counts.** The runner reports filtered/total (`perm_count` `:845-878`).
  `--force` ignores filters.
- **Masks.** `--no-internal/--no-reentrant/--no-fuzz` mask flags (`:2812-2828`).
- **Stepping.** `--step=a,b,s` selects a stride of (case, permutation, scenario) steps
  (`:2760-2810`). `test.py` uses it for parallelism (`scripts/test.py:1273-1290`).
- **Feature-gate helpers.** `LFS3_IFDEF_X(a,b)` / `LFS3_IFYES_X(a,b,c)` pick branches by
  build config (`lfs3_util.h:223-330`). Suites use them in `if` so that, for example,
  `GBMAP=[false,true]` runs only false without `LFS3_GBMAP`, both with it, and only true
  with `LFS3_YES_GBMAP` (`tests/test_gc.toml:9`, `test_trvs.toml:12`, ...).

### 1.6 Power-loss testing

**Mechanism** (emubd):
- `power_cycles` counts **write operations**, meaning prog and erase, never read or sync
  (`bd/lfs3_emubd.c:487-490, 792-794`). When it reaches 0, emubd applies
  `POWERLOSS_BEHAVIOR` to the in-flight operation and calls `powerloss_cb`
  (`:649-650, 957-958`).
- The runner's callback `longjmp`s out of littlefs (`test_runner.c:1610-1613`). RAM state
  (the `lfs3_t`, open handles, mallocs) is abandoned; the emubd disk image is kept.
- **Resume.** The runner prints `powerloss <id>`, increments `TEST_PLS`, re-arms the
  counter and calls `case_->run(CFG)` again **from the top on the same disk**
  (`:1675-1705` linear, `:1779-1809` log, `:1872-1896` list). The test must remount and
  continue. The run completes when a pass finishes without the counter firing.

**Scenarios** (verbatim from `./scripts/test.py -R runners/test_runner --list-powerlosses`;
table at `test_runner.c:2080-2107`):

```
none                    Run with no powerlosses.
linear                  Run with linearly-decreasing powerlosses.
log                     Run with exponentially-decreasing powerlosses.
permute(n)              Run all permutations of n powerlosses.
exhaustive              Run all powerloss permutations, this may take a while.
range(a,b,s)            Run a range of powerlosses.
logrange(a,b,s)         Run a range of 2^n powerlosses.
[1,2,3]                 Run explicit list of powerlosses.
:1248g1                 Run leb16-encoded list of powerlosses.
```

Semantics:
- **Default.** `-Pnone -Plinear` (`:2109-2119`). Non-reentrant cases are skipped for every
  scenario except `none` (`:780-783`).
- **`linear`.** Attempt k loses power after write #(1 + k·step) of that attempt, so every
  write point is hit once. About W attempts for a test with W writes, O(W²) total.
  `range(a,b,s)` bounds k (`:1617-1718`). Replay suffix `:x<start><stop><step>`.
- **`log`.** Attempt k loses power after 2^(k+1) writes, O(W log W). `logrange` bounds it
  (`:1721-1822`). Suffix `:y…`.
- **`permute(n)`** (depth n) and **`exhaustive`** (unbounded depth). At every write, emubd
  takes a copy-on-write snapshot (`lfs3_emubd_cpy`, `:1928-1950`) and records a branch. The
  runner then recurses into every branch (`:1954-2020`) with `TEST_PLS = depth` (`:1970`).
  Cost is about W^n. `permute(1)` = one power loss at every possible write, which is
  equivalent to v2 CI's `-P1`. `permute(2)` = all pairs; v2 CI ran this only on
  `test_dirs test_relocations`.
- **`[1,2,3]` / `:leb16`.** An explicit list of power-cycle counts, one per successive
  attempt (`:1825-1906`). This is what failure ids replay.
- **Counts.** Every scenario other than `none` selects the same reentrant set: 15,904 of
  20,681 perms (runner, `-Plinear --summary`). By suite (runner `-Plinear -l`):

  | suite | reentrant perms |
  |---|---|
  | test_powerloss | 11,328 |
  | test_relocations | 1,980 |
  | test_files | 900 |
  | test_dirs | 880 |
  | test_grow | 660 |
  | test_dread | 120 |
  | test_attrs | 20 |
  | test_stickynotes | 16 |

- **Debugging.** `test.py --gdb-pl N | --gdb-pl-before | --gdb-pl-after` break at the case
  entry and skip N, or n-1, or n, re-entries (`scripts/test.py:1589-1627`).

### 1.7 emubd fault models and how tests select them

**Power-loss behaviours** (`bd/lfs3_emubd.h:44-51`; prog `bd/lfs3_emubd.c:487-676`,
erase `:792-980`). Selected with define `POWERLOSS_BEHAVIOR` (`test_defines.h:33`).

| value | behaviour on the interrupted prog or erase | tests |
|---|---|---|
| ATOMIC=0 | Operation not applied | default everywhere |
| SOMEBITS=1 | Prog: only one random bit of the region flips. Erase: old data stays, one bit flips | test_powerloss only |
| MOSTBITS=2 | Operation applied, then one random bit flipped | test_powerloss only |
| OOO=3 | Every block written since the last `sync` is reverted except the current one; snapshot taken in `lfs3_emubd_sync` (`:1083-1098`) | test_powerloss only |
| METASTABLE=4 | Prog applied; a random bit becomes metastable and flips randomly on later reads (`:606-647`, read `:424-431`) | **none** |

**Bad-block behaviours** (`bd/lfs3_emubd.h:29-42`). A block is bad when
`wear > erase_cycles`. With `ERASE_CYCLES=0` (default) wear is never counted
(`bd/lfs3_emubd.c:1003-1006`), so blocks become bad only through `lfs3_emubd_mkbad`
(wear = -1, `:1246-1274`) or `setwear`. Selected with `BADBLOCK_BEHAVIOR`, `ERASE_CYCLES`
and `BD_SEED` (`test_defines.h:31-34`).

| value | effect on a bad block | tests |
|---|---|---|
| PROGERROR=0 | Prog returns `LFS3_ERR_CORRUPT` | test_badblocks, test_exhaustion |
| ERASEERROR=1 | Erase returns `LFS3_ERR_CORRUPT` | same |
| READERROR=2 | Read returns `LFS3_ERR_CORRUPT` (`:411-418`) | same, **only with `LFS3_CKPROGS`** |
| PROGNOOP=3 | Prog silently does nothing | same, **only with `LFS3_CKPROGS`** |
| ERASENOOP=4 | Erase silently does nothing (and progs no-op) | same, **only with `LFS3_CKPROGS`** |
| PROGFLIP=5 | Prog flips the block's bad bit (`:712-721`) | test_ck (ckprogs_*, ckparity, ckdatacksums, spam_* METHOD=0) |
| READFLIP=6 | Prog marks the block metastable; reads flip the bad bit | test_ck ckparity/ckdatacksums |
| MANUAL=7 | Test flips bits explicitly (`lfs3_emubd_flip`) | test_ck spam_* METHOD≠0 |

**Fault-injection APIs used by tests** (`bd/lfs3_emubd.h:271-322`):
- `mkbad`: test_badblocks, test_ck
- `setwear`: test_exhaustion
- `flipbit`, `flip`, `mkbadbit`, `seed`: test_ck
- `wear`, `setbadbit`, `setpowercycles`, `cpy`: no direct test use (`cpy` is used by the
  runner)

`BLOCK_RECYCLES` (wear-leveling relocation) is permuted in test_relocations (8 cases),
test_exhaustion, test_mtree and test_trvs.

### 1.8 `test.py` / runner options

Parse at `scripts/test.py:1676-1972`; runner options at `test_runner.c:2244-2313`. Make
passes `-b` plus the make `-j` (`Makefile:151-155`).

| option | effect | cite |
|---|---|---|
| `-j[N]` | Parallel runners striding with `--step=j,,N`; `-j` alone means one per core (Linux only) | test.py:1338-1345, 1432 |
| `-k` | Keep going; resumes after the failing step | test.py:1321-1330 |
| `-b` / `-B` | Stage by suite / by case | |
| `-i` | One process per permutation | test.py:1283-1286 |
| `-F n`, `-C n` | Number of failures and lines of context shown | |
| `-o csv` | Per-permutation `i, suite, case, <defines>, test_passed, test_runtime` (feeds `make test-marks`, `test-bottlenecks`) | test.py:1224-1250; Makefile:529-563 |
| `-O file` | Raw stdout | |
| `-d disk`, `-t trace`, `--trace-backtrace`, `--trace-step`, `--trace-runfreq` | Mirror the bd image to a file; `LFS3_TRACE` output | test_runner.h:46-58 |
| `--read/prog/erase-sleep` | Artificial latency | |
| `--valgrind` | Prepends `valgrind --leak-check=full --track-origins=yes --error-exitcode=4 -q` and forces isolation | test.py:798-803, 1283 |
| `--exec` | Run under e.g. `qemu-arm`; Make passes `EXEC` | Makefile:176-179 |
| `-p/--perf` | Wrap in `scripts/perf.py -e` (`perf record` with `cycles,branch-misses,...`, perf.py:41,215-224); needs a `PERFGEN=1` build (frame pointers) | Makefile:94-96, 156-159 |
| `PERFBDGEN` | Traces bd ops with backtraces for `scripts/perfbd.py`; **broken flag, see §0.6** | Makefile:97-99, 160-163 |

Valgrind should be used with `-Pnone` only. Power-loss `longjmp`s leak the test's
`malloc`s, and `--leak-check=full` with `--error-exitcode` would report those leaks as
failures.

### 1.9 Local evidence (from the caller's runs, not mine)

- **macOS/clang baseline**, `.local/baseline_test.log`: 634,435/634,616 passed,
  181 failed, 2,226,875 power losses, 134 s on 14 cores. Example failures include
  `test_files_zero_btree` and `test_grow_incr_spam_uzd_fuzz`.
- **Linux, Ubuntu 24.04, GCC**, `.local/linux/build.log`: the default runner compiled with
  **0 warnings**, so `-Werror` should hold for the default config. The Linux run was in
  progress when these notes were written.

---

## 2. Bench framework

**Pipeline.** Same as tests: `benches/*.toml` → `scripts/bench.py -c` → `*.b.c`
(`Makefile:821-825`), built into `runners/bench_runner` (`Makefile:792-793`). The block
device defaults to **kiwibd** (`runners/bench_runner.h:29-32`), a lighter emubd variant for
large disks with no fault injection (`bd/lfs3_kiwibd.c:1-8`).

**Bench flags.**
- `BENCH_INTERNAL`, `BENCH_LITMUS` (`bench_runner.h:97-100`).
- Stack and heap watermarks through `-Wl,--wrap=printf/vprintf/malloc/free/realloc`,
  unless `NO_STACK`/`NO_HEAP` (`Makefile:107-123`).
- `BENCH_START/STOP/RESULT(probe, n, value)`, `BENCH_SIMTIME/SIMRESET`,
  `BENCH_STACK_*`, `BENCH_HEAP_*` (`bench_runner.h:144-227`).

**Runner options** (`bench_runner.c:2647-2686`): `--list-probes`,
`-S/--probe`, `--probe-step/-runfreq/-simfreq`, `--no-litmus`, `--trace-simfreq`, plus the
same define and step machinery as the test runner.

**Defaults** (`runners/bench_defines.h:21-58`):
- 128 MiB disk, `BLOCK_RECYCLES=100`, `ERASE_VALUE=-1` (erases not simulated).
- NOR (w25q64jv) and NAND (w25n01gv) models give `*_SIZE`, `*_WIDTH` and per-byte
  `*_TIMING`: bus+buffer simulation when `DISK_SIM=0`, per-byte when `DISK_SIM=1`
  (`:60-141`).
- `make bench` excludes litmus benches unless `BENCH_ALL` (`Makefile:193-196`).

**What is benchmarked** (`benches/`):

| suite | cases | measures | cite |
|---|---|---|---|
| bench_wt | seq, random, logging, many | Write throughput. Each runs for `SIM_TIME` = 1 simulated hour after a warm-up, with `SIZE=1MiB`, `CHUNK=64`. **seq**: append and truncate at SIZE, no sync. **random**: random 64 B writes in a 1 MiB file. **logging**: append + sync + `lfs3_file_fruncate` (or `lfs3_rename` rotation when `NO_FRUNCATE`). **many**: small-file kv-style churn. Probes `write`, `stack`, `heap`, `usage` | bench_wt.toml:9-19, 28, 105, 178-231, 262 |
| bench_rt | seq, random, logging, many | Read throughput under the same four workloads | bench_rt.toml:28, 119, 208, 299 |
| bench_file (litmus) | bench_file | 1 MiB file with random-unaligned writes then reads; probes write, read, usage | bench_file.toml:10-28 |
| bench_dir (litmus) | bench_dir | 1024 dir entries; write, stat, read, usage | bench_dir.toml:10-26 |
| bench_btree (litmus, internal) | ids, names | B-tree commit, lookup, namelookup, usage at N=8192; `ifndef LFS3_GBMAP` | bench_btree.toml:5, 12-28, 114-131 |
| bench_rbyd (litmus, internal) | attrs, ids | rbyd append, remove, create, delete, fetch, lookup, usage | bench_rbyd.toml:7-32, 127-145 |

**Helpers.** `bench_helpers_warmup` writes a 1-block file 2×block_count times.
`bench_helpers_usage` counts used blocks exactly with `lfs3_trv_*`
(`benches/bench_helpers.c`).

**Output CSV fields** (`scripts/bench.py:1320-1327, 1509-1516`): `bench_reads`,
`bench_progs`, `bench_erases`, `bench_readed`, `bench_progged`, `bench_erased`,
`bench_simtime`, `bench_runtime`.

**Make reports:** `bench-marks`, `bench-ops`, `bench-widths`, `bench-ram`/`bench-usage`,
`bench-bottlenecks`, and `-diff` variants against `lfs3.bench.csv` (`Makefile:591-727`).

**Relation to the published results.** The maintainer's published benchmarks
(`issue_1114.txt:699-715`) use an external `littlefs-benchmarks` repo with benches
`bench_p26_wt.toml` sequential/random/logging/many. The in-tree `bench_wt`/`bench_rt` are the
same four workloads.

---

## 3. CI

### 3.1 State

`.github/workflows/test.yml` was last changed 2022-11-30 (`cda2f6f`) and is identical to
the old v2 workflow. v2 `master` has since updated its copy (`8c458fa`, 2025-05) with
checkout@v4, upload/download-artifact@v4, per-matrix artifact names, test-compat,
test-multiversion and so on. v3-work carries none of that.

The workflows are `test.yml` (776 lines), `status.yml`, `release.yml` and
`post-release.yml`.

### 3.2 What breaks, and the fix

| test.yml step | v2 assumption | v3 reality | fix |
|---|---|---|---|
| install `pip3 install toml` (:30) | `toml` package | test.py/bench.py import `tomllib`, else `tomli` (`scripts/test.py:35-38`). ubuntu-22.04 has Python 3.10, so the import fails | `runs-on: ubuntu-24.04` (Python 3.12 has tomllib); drop the pip line, or `apt install python3-tomli` |
| actions (:24, 217, 255, 264, 302, 436, 470, 613-630, 771) | checkout/upload/download@v2 | artifact actions v1-v3 are retired by GitHub | `@v4`. v4 artifacts are immutable per name, so matrix jobs need `name: sizes-${{matrix.arch}}` and consumers `pattern:` + `merge-multiple: true` (as v2 master does). `dawidd6/action-download-artifact@v2` in status.yml:16,70 and release.yml:33-52 → a v4-compatible release |
| test-example (:78-89) | README has a v2 `lfs_*` example | README example is still v2 API (`README.md` `lfs_mount(&lfs,&cfg)`); there is no `lfs.h` | Port the README example to `lfs3_*` (`lfs3_mount(&lfs3, LFS3_M_RDWR, &cfg)`), or drop the step |
| cov (:102-108) | `make lfs.cov.csv` | Target is `lfs3.cov.csv` (`Makefile:760`) and needs a `COVGEN=1` build or there is no gcda | `COVGEN=1 make test` then `make lfs3.cov.csv`; `cov.py -u lfs3.cov.csv` |
| sizes* (:111-213) | `LFS_NO_*`, `LFS_READONLY`, `LFS_THREADSAFE`, `LFS_MIGRATE`, `scripts/summary.py`, `lfs.*.csv` | `LFS3_NO_ASSERT/DEBUG/INFO/WARN/ERROR` or `LFS3_NO_LOG` (`lfs3_util.h:98-112`), `LFS3_RDONLY`. THREADSAFE is a no-op (§0.6); MIGRATE does not exist (stretch goal). `summary.py` is gone, replaced by `scripts/csv.py`. New ctx metric | `make lfs3.code.csv lfs3.data.csv lfs3.stack.csv lfs3.ctx.csv lfs3.structs.csv`, summarise with `make summary` (`Makefile:441-453`, csv.py `-fstack='max(stack_limit)'`). Rewrite the awk parsing for csv.py output. Configs: default, rdonly, gbmap, biggest (the maintainer reports default/rdonly/gbmap: `issue_1114.txt:621-624`, `pr_1111.txt:513-523`). Set `OBJDUMP=<cross>-objdump` for cross archs (`Makefile:138-146`; code/data/stack/ctx/structs.py all call objdump) |
| status tables (:221-305, :633-744) | parse `summary.py` output; context names | Formats changed | Regenerate against csv.py and cov.py `-Y` output |
| test-pls (:310-337) | `-P1`, `-P2` | **Invalid in v3**: `error: invalid powerloss: 1` (verified) | `-P'permute(1)'` for all; `-P'permute(2)' test_dirs test_relocations` (add `test_powerloss`) |
| test-no-intrinsics (:340-354) | `-DLFS_NO_INTRINSICS` | Renamed | `-DLFS3_NO_BUILTINS` (`lfs3_util.h:207,342-534`) |
| test-valgrind (:357-374) | `--valgrind -Gdefault -Pnone` | No `-G` in v3 | `--valgrind -Pnone`. Consider `--context=1024` as v2 master does; `--no-fuzz` or a suite subset if time-bound |
| test-clang (:378-396) | override CFLAGS without GCC flags | v3 Makefile adds `-fcallgraph-info=su`, `-ftrack-macro-expansion=0`, `-Wno-stringop-overflow` unconditionally (`Makefile:72-82`). Clang rejects the first two (unknown argument) and warns on the third, fatal under `-Werror` (verified). The override also loses `-Wno-unused-label/-function` (`:77-79`) | Override with `CFLAGS="$CFLAGS -MMD -g3 -I. -std=c99 -Wall -Wextra -pedantic -Wno-unused-label -Wno-unused-function"`, or make the GCC-only flags conditional in the Makefile (the caller's `.local/cc` wrapper does the latter) |
| bench (:401-474) | `make lfs.bench.csv`, `summary.py`, fields `bench_readed/proged/erased`, `make lfs.perfbd.csv` | `lfs3.bench.csv`; fields `bench_progged` etc.; perfbd requires `PERFBDGEN`, which is broken (`--trace-freq`, `Makefile:161-162`) | Fix the Makefile flag to `--trace-runfreq=100`, rename the files, use `csv.py`. Note each wt/rt bench simulates 1 h on a 128 MiB disk, so budget time or set `-DSIM_TIME` lower |
| fuse, migrate (:477-596) | littlefs-fuse `v2`/`v1` checkouts self-hosting `make test` | lfs-fuse v2 cannot build lfs3; there is no v3 migrate | Remove both until a v3 littlefs-fuse exists |
| status (:599-775) | `needs: [test, bench]`, base `master` | OK structurally | Adjust after the artifact renames |
| release.yml:55-78, 214-236 | greps `#define LFS_VERSION` in `lfs.h`; creates `vN-prefix` branches via `changeprefix.py lfs lfsN` | `lfs3.h` `LFS3_VERSION` (currently `0x00000000`, `lfs3.h:19`); already prefixed `lfs3_` | Update the grep. Drop prefix-branch creation: the maintainer plans to sunset `-prefix` branches (`pr_1111.txt:807-809`) |

### 3.3 Recommended matrix

The time estimates below are extrapolations, not measurements. The only data point is 134 s
× 14 cores on Apple silicon for the default suite, so about 10-20 min per native x86_64
4-vCPU GitHub runner, and roughly 5-20× that under qemu-user.

**Tier A: every push and PR (gating)**

| job | arch | build config | powerloss | geometry |
|---|---|---|---|---|
| test | x86_64, thumb (32-bit LE), mips (32-bit BE), powerpc (32-bit BE) | default | `-Pnone -Plinear` (make default) | default NOR 1/1/4096 |
| test-biggest | x86_64, mips | `LFS3_BIGGEST=1` (GC, GBMAP, PREERASE, REVPERTURB, REVNOISE, CKPROGS, CKFETCHES, CKMETAPARITY, CKDATACKSUMS, BLEAFCACHE). Restores the 58 filtered cases and the CKPROGS bad-block variants | default | default |
| test-yes-gbmap | x86_64 | `LFS3_YES_GBMAP=1` | default | default |
| test-no-builtins | x86_64 | `-DLFS3_NO_BUILTINS` | default | default |
| test-clang | x86_64 | clang, flags fixed | default | default |
| test-valgrind | x86_64 | default | `-Pnone` | default |
| sizes | thumb (report), all (build) | default, `LFS3_RDONLY`, `LFS3_GBMAP`, `LFS3_BIGGEST`, each with `LFS3_NO_LOG -DLFS3_NO_ASSERT` | – | – |
| cov | x86_64 | `COVGEN=1`, default (+ BIGGEST as a separate report) | default | default |

The Linux 32-bit cross arches exercise endianness (le32/leb128 on-disk encodings) and
32-bit `size_t`/`lfs3_off_t` paths.

**Tier B: nightly or scheduled**

| job | spec |
|---|---|
| geometry (x86_64, default and BIGGEST) | `-D` sets: eeprom `READ_SIZE=1 PROG_SIZE=1 ERASE_SIZE=512`; prog16 `16/16/512`; emmc `512/512/512`; v2-nand `4096/4096/32768`; big-NAND `2048/2048/131072 DISK_SIZE=16777216`; plus `ERASE_VALUE=0x00,-1` |
| exhaustive power loss | `-P'permute(1)'` all suites (v2 parity); `-P'permute(2)' test_dirs test_relocations test_powerloss`; `-Plog` |
| power-loss fault models | reentrant suites with `-Plinear -DPOWERLOSS_BEHAVIOR=0,1,2,3,4` (adds METASTABLE, adds non-ATOMIC models beyond test_powerloss) |
| YES_* modes | `LFS3_YES_CKPROGS`, `YES_CKFETCHES`, `YES_CKMETAPARITY`, `YES_CKDATACKSUMS`, `YES_REVPERTURB`, `YES_REVNOISE`, `YES_GC`, `YES_BLEAFCACHE` (`lfs3_util.h:62-96`); `LFS3_YES_TRACE` build |
| bench | `make bench` with `BENCH_NOR` and `BENCH_NAND`; track `lfs3.bench.csv` diffs (not pass/fail) |

### 3.4 Makefile targets

`make help`, `Makefile:225-869`:

| area | targets |
|---|---|
| Build | `build/all`, `asm`, `size`, `tags` |
| Sizes | `code`, `data`, `stack` (needs `.ci` from GCC `-fcallgraph-info`), `ctx`, `structs`, `funcs`, `summary/sizes`, each with `-csv` and `-diff` |
| Codemaps | `codemap`, `codemap-tiny` |
| Coverage | `cov`, `cov-csv`, `cov-diff` |
| Perf | `perf`, `perfbd` (+ `-csv`, `-diff`) |
| Tests | `test-runner/build-tests`, `test`, `test-list`, `test-marks[-csv|-diff]`, `test-bottlenecks` |
| Benches | `bench-runner`, `bench`, `bench-list`, `bench-marks[-csv|-diff]`, `bench-bottlenecks`, `bench-ops`, `bench-widths`, `bench-ram/usage` |
| Other | `clean` |

**Make variables:**
- `DEBUG` (-O0), `TRACE` (`LFS3_YES_TRACE`), `COVGEN`, `PERFGEN`, `PERFBDGEN`, `TESTMARKS`,
  `NO_BENCHMARKS`, `BENCH_ALL`, `BENCH_NOR/NAND/PERBYTE`, `NO_STACK`, `NO_HEAP`, `EXEC`,
  `VERBOSE`, `BUILDDIR`, `TESTS`/`SRC` overrides.
- Every `LFS3_*` environment variable is forwarded as `-D` (`Makefile:102`).

---

## 4. Test suites

Flags: R = reentrant, F = fuzz, I = internal (`in = 'lfs3.c'`). "perms" is
filtered/total with `-Pnone` in the default build (runner `-Pnone -l`); "PL" is the
reentrant permutation count under `-Plinear`.

| suite | cases | R | F | I | perms | PL | suite-level permuted / gates | fault injection |
|---|---|---|---|---|---|---|---|---|
| test_bd | 5 | 0 | 0 | 0 | 20/20 | 0 | – | – |
| test_rbyd | 107 | 0 | 4 | 107 | 385,878/385,878 | 0 | ERASE_VALUE {0xff,0,-1}, BLOCK_SIZE=32768 | – |
| test_btree | 50 | 0 | 15 | 50 | 7,664/8,094 | 0 | ifndef YES_GBMAP; LOOKAHEAD=full | – |
| test_mtree | 52 | 0 | 4 | 50 | 16,943/67,772 | 0 | REVPERTURB×REVNOISE (if-gated) | truncated tags/cksums, BLOCK_RECYCLES |
| test_dirs | 49 | 45 | 4 | 0 | 1,890/1,958 | 880 | – | powerloss |
| test_gbmap | 19 | 0 | 2 | 11 | **0/19** | 0 | ifdef LFS3_GBMAP | ERASE_VALUE |
| test_dread | 17 | 2 | 0 | 0 | 18,524/28,910 | 120 | – | powerloss |
| test_files | 31 | 4 | 5 | 3 | 4,479/5,601 | 900 | – | powerloss |
| test_paths | 38 | 0 | 0 | 5 | 71/71 | 0 | – | – |
| test_alloc | 8 | 0 | 0 | 5 | 376/884 | 0 | GBMAP {F,T} | – |
| test_fwrite | 41 | 0 | 6 | 19 | 77,134/104,652 | 0 | FRAGMENT_SIZE {1,16,64} × PROG_SIZE {1,16}, CRYSTAL_THRESH=512 | – |
| test_fsync | 50 | 0 | 16 | 0 | 40,310/40,310 | 0 | – | – |
| test_stickynotes | 67 | 4 | 2 | 4 | 7,212/9,024 | 16 | – | powerloss |
| test_attrs | 48 | 1 | 7 | 0 | 669/677 | 20 | – | powerloss |
| test_kv | 18 | 0 | 4 | 0 | 288/291 | 0 | – | – |
| test_trvs | 74 | 0 | 5 | 5 | 21,278/48,556 | 0 | GBMAP {F,T} | clobbered blocks, BLOCK_RECYCLES |
| test_gc | 32 | 0 | 5 | 4 | 8,152/50,407 (27 cases compiled out) | 0 | GBMAP; ifdef LFS3_GC(+GBMAP/REVPERTURB/PREERASE) | clobbered blocks, ERASE_VALUE |
| test_mount | 39 | 0 | 0 | 29 | 639/74,684 | 0 | GBMAP; flag permutations | corrupt superblock/compat |
| test_ck | 28 | 0 | 13 | 4 | 4,294/16,951 (10 compiled out) | 0 | ifdef CKPROGS/CKFETCHES/CKMETAPARITY/CKDATACKSUMS | **bit flips**: PROGFLIP/READFLIP/MANUAL, `flipbit`/`mkbadbit` |
| test_compat | 14 | 0 | 0 | 0 | 38/38 | 0 | `LFSP` previous-version link | – |
| test_grow | 12 | 2 | 6 | 0 | 2,541/6,162 | 660 | GBMAP | powerloss |
| test_badblocks | 26 | 0 | 18 | 3 | 2,765/7,970 | 0 | FORMAT_BLOCK_COUNT | **bad blocks**: 5 behaviours × `mkbad` |
| test_powerloss | 4 | 4 | 2 | 0 | 11,328/13,184 | 11,328 | – | **POWERLOSS_BEHAVIOR** ATOMIC/SOMEBITS/MOSTBITS/OOO |
| test_relocations | 8 | 2 | 6 | 0 | 5,919/7,167 | 1,980 | BLOCK_RECYCLES | wear relocation, powerloss |
| test_exhaustion | 5 | 0 | 5 | 0 | 300/960 | 0 | ERASE_CYCLES=10, BLOCK_RECYCLES=4 | **wear-out**: `setwear`, 5 bad-block behaviours |
| **total** | **842** | **64** | **129** | **299** | **618,712/880,240** | **15,904** | | |

### Case groups, one line each

**test_bd** (bd sanity; permutes the `READ`/`PROG` chunking)
- one_block, two_block, last_block: read/prog/erase round trips.
- powers_of_two, fibonacci: block-address patterns.

**test_rbyd** (internal red-black-yellow Dhara tree)
- atomic_commit, commit, commit_fetch_commit: commit and fetch.
- lookup, get: attribute lookup.
- bifoliate … sextifoliate, bflips, rflips, rotations, ysplits, prunes: every tree
  shape/rebalance transition.
- *_permutations: exhaustive insertion orders via `TEST_PERMUTATION`.
- traverse*: in-order traversal.
- remove*, create*, mixed*: remove, create and mixed attr operations.
- delete*, delete_range_*: id deletion, including range deletes (b/r/y/rydy/dryy
  variants).
- sparse*: weighted ids.
- subwide/supwide: subtype- and supertype-wide tags.
- unreachable_hole*: null tag from altgt-only trees.
- fuzz_append_removes, fuzz_create_deletes, fuzz_mixed, fuzz_sparse: balancing fuzz.

**test_btree** (internal B-tree over rbyds)
- zero/one/two/three (+backwards): trivial trees.
- push, update, pop (+sparse, +fuzz): append, update and remove at scale.
- split (+sparse, +fuzz): node splits.
- drop, drop_compact, drop_split, drop_merge: node removal edge cases.
- general_*fuzz: mixed operations.
- find*: lookup by did/name.
- traversal (+fuzz).

**test_mtree** (internal metadata tree)
- mroot, mroot_rattrs, mroot_compact, mroot_many_commits: single mroot.
- uninline*: mtree creation.
- split*, drop*: mdir split/drop.
- relocate*, extend*: mroot anchor chain and relocations.
- opened_*: open handles tracked across structural changes.
- traversal*: including cycle detection.
- truncated_{tag,cksum,ecksum,gcksumdelta}: torn-commit rejection.
- magic*: "littlefs" magic at off 8 through mroot extension.

**test_dirs**
- mkdir*: exists, root, noent, siblings, children, many, 2-3 layers, linkedlist, fuzz.
- ordering*: dir-entry order.
- did_*: directory-id collisions, zero/ones, leb128 boundaries.
- rm*, mv*, mvrm_fuzz: all under power loss except noent, stat_root and ordering.

**test_gbmap**
- set_*: range split, replace, merge, noop, bounds, fuzz.
- set_ecksum_*: the same with erased-state cksums (pre-erase tracking).
- files, gc_files: end-to-end with the gbmap.
- rmgbmap, mkgbmap, rmmkgbmap, mkrmgbmap, *_noent, *_exist: enabling and disabling the
  gbmap on a live fs, with remount permutations.

**test_dread** (directory iteration)
- tell, rewind, seek, read_idempotent.
- read_neighbor_*, read_with_*: concurrent mkdir/rm/mv in the same or neighbouring dirs
  during a read.
- read_rm, read_rm_remkdir: iterating a directory that is removed.
- recursive_rm, recursive_mv: removal/rename while iterating, reentrant.

**test_files**
- create, hello (inline), trunc, noent, excl, *_not_* type errors.
- more: inline → shrub → block pointer → btree size transitions.
- many (R), fuzz.
- zero_bnull, zero_bshrub, zero_btree (I): zero-size representations.
- rm*, mv*: including mv_split over an mdir split.
- pl_fuzz: power-loss fuzz, R+F.

**test_paths**
- Path grammar: absolute, redundant or trailing slashes, `.`, `..`, `...`, leading dots,
  root `..`.
- noent, notdir, notsup variants; notsup cases are internal.
- empty, root aliases, magic_noent, magic_conflict.
- name length limits, UTF-8, spaces, nonprintable/DEL/0xff bytes.

**test_alloc**
- alloc, reuse, wraparound_files, nospc_dirs, nospc_files: block allocator basics.
- clobber_dirs, clobber_files, clobber_open_files (I): allocator traversal must see every
  in-use block, including open files. With and without gbmap.

**test_fwrite** (write strategy)
- simple, incr, reversed, freversed with *_litmus_{fragments,blocks}: optimal
  fragment/block layout, checked internally.
- overwrite, holes.
- clip_cache, clip_leaf, clip_hole (I).
- truncate, fruncate (+_pos, _litmus_zero, _litmus_fragment, double applications).
- overwrite_compaction, hole_compaction.
- fuzz_aligned, fuzz_unaligned, r_seek, w_seek, rw_seek, seek_negative, rwtf_fuzz.
- fbig, truncate_fbig, fruncate_fbig: file-size limits.
- bigger_than_expected_*.

**test_fsync** (sync model). Letters: w = write, r = read, s = sync, d = desync, y = resync,
t = truncate, f = fruncate, o = one handle opened with O_SYNC.
- sync_*: SYNC via `file_sync` / `O_SYNC` / `M_SYNC` × FLUSH via `file_flush` /
  `O_FLUSH` / `M_FLUSH` (`test_fsync.toml:107-117`).
- rrrr, wrrr, wwww, wwrr, rwrw (+fuzz, +sparse): multi-handle broadcast semantics.
- desync_*, resync_*, drrr, wddd, rwdrwd, yrrr, wyyy (+fuzz).

**test_stickynotes** (0-size / uncreated files)
- uncreat* (+pl R): created but unsynced files are invisible, including after power loss.
- undesync*.
- orphan* (desynced-then-closed).
- zombie*, zombify_* (removed while open), fileonzombie_*.
- cleanup* (I): orphan cleanup at mount.
- file_mv*: handles follow renames, including over mdir splits.
- uz_fuzz, uzd_fuzz.

**test_attrs**
- setattr, getattr, removeattr: trunc, noattr, update, zero, null, big (>256), all, many.
- fuzz, fuzz_fuzz, rm, mv_dst, mv_src, mvrm_fuzz_fuzz.
- fattr_*: file-attached attrs, including O_TRUNC/uncreat, rdonly/wronly, noop/lazy,
  broadcast/no_receive/desync/resync/zombie semantics, many, and fuzz.
- fattr_pl_fuzz_fuzz: R+F.

**test_kv**
- set, set_trunc, set_noent, set_update, remove, set_zero, set_null, many, many_big, fuzz.
- interop_*: kv files as normal files, sync/desync/resync interplay, fuzz.

**test_trvs** (traversal API)
- simple, rewind, idempotent.
- clobber_* and rewind_clobber_*: allocator view.
- ckmdir, ckbtree, ckdata × dirs/files/opened: detect every clobbered block.
- flags.
- mutation_*: fs modified mid-traversal across bsprout/bshrub/btree, mroot/mtree
  split/extend/relocate.
- compact_* (some I): traversal-driven compaction.
- mkconsistent_*.
- spam_* many/fuzz.

**test_gc** (incremental GC API)
- lookahead_*, lookgbmap_*, preerase_*, compact_*, mkconsistent_*: progress, mutation
  and relaxed thresholds for each GC job.
- ckmeta, ckdata (+_explicit, _unck): clobber detection via GC or `lfs3_fs_ck`,
  `lfs3_fs_unck`.
- iflags*, mutation*, nospc, spam_*.
- 27 of the 32 cases need `LFS3_GC` or more.

**test_mount**
- simple, flags, format_flags.
- t_lookahead, t_lookgbmap, t_preerase, t_compact, t_mkconsistent, t_ckmeta, t_ckdata:
  mount-time traversal jobs.
- incompat_* (29, I): no or bad magic, major/minor version, r/w/ocompat flags including
  overflow and padding, rdonly/wronly images, block size/count, name and file limits,
  unknown config, unknown file type (rm/mv variants), out_of_phase (mrootanchor shift
  detection).

**test_ck** (checksums)
- crc32c* (math, F).
- cksum: `lfs3_fs_cksum`.
- ckmeta/ckdata easy/hard: via mount/open/traversal/GC METHODs, with bit flips.
- file_ck*.
- ckprogs_{mroot,data,btree,overrecycling}: PROGFLIP caught by `LFS3_CKPROGS`.
- ckfetches_*: `LFS3_CKFETCHES`.
- ckparity_*: `LFS3_CKMETAPARITY`, PROGFLIP/READFLIP.
- ckdatacksums_data.
- spam_{dir,file,fwrite,uz,uzd}_fuzz: random bit errors over 4 detection METHODs with a
  protected mrootanchor option.

**test_compat**
- forward_* and backward_* × mount, read and write of dirs and files. They test
  `lfsp` → `lfs3` when a previous version is linked with `-DLFSP=...` via
  `scripts/changeprefix.py`; otherwise `lfsp` aliases `lfs3` (`test_compat.toml:11-67`).
  They are gated on `LFS3_VERSION == LFSP_VERSION`.

**test_grow**
- mount_smaller: allowed.
- mount_bigger: refused.
- grow, noop: `lfs3_fs_grow`.
- incr_spam_*: many/fuzz.
- incr_spam_f(d)_pl_fuzz: R.

**test_badblocks**
- every_*: every single block bad in turn.
- region_*: a contiguous bad region, testing cascading failures.
- alternating_*: every other block bad; hard for mdir pair allocation.
- Each of the three families covers btree_many (I), spam_dir, spam_file, spam_fwrite,
  spam_uz and spam_uzd (many/fuzz) × 5 BADBLOCK_BEHAVIORs × MIRROR.
- mrootanchor_format (0/1 bad at format → error), mrootanchor_wear.

**test_powerloss**
- spam_dir_many, spam_file_many, spam_f_pl_fuzz, spam_fd_pl_fuzz, × POWERLOSS_BEHAVIOR
  {ATOMIC, SOMEBITS, MOSTBITS, OOO} × MKCONSISTENT.
- The fuzz cases keep a progress-counter file instead of a model
  (`test_powerloss.toml:216-221`).

**test_relocations**
- spam_dir/file_many, spam_*_fuzz, spam_uz/uzd_fuzz, spam_f(d)_pl_fuzz (R), under small
  BLOCK_RECYCLES: relocations vs gstate, shrubs, uncreats and zombies.

**test_exhaustion**
- spam_{dir,file,fwrite,uz,uzd}_fuzz: run to device death at ½ and at full block_count
  and compare operation counts. This is the dynamic wear-leveling litmus: lifetime scales
  with size (`test_exhaustion.toml:13-44`).

---

## 5. Roadmap (maintainer's own words)

### 5.1 Feature status

**Implemented** (PR "What's new → Implemented", `pr_1111.txt:187-394`; TODO list `:139-157`):

| feature | maintainer's intent (quote/paraphrase) | cite | code at b10efaa |
|---|---|---|---|
| rbyd metadata | "Efficient metadata compaction: O(n²) → O(n log n)" | pr:189-195 | yes |
| B-tree files | "Efficient random writes: O(n) → O(log_b² n)" | :197-203 | yes |
| Sync-padding fix | "v3's B-trees support inlining data directly in the B-tree nodes… without needing to pad things" | :205-209 | yes |
| B-shrubs | "Efficient inline files, no more RAM constraints" | :211-215 | yes |
| Independent caches | pcache/rcache/fcache configured independently | :217-219 | yes (`fcache_size`) |
| `lfs3_file_fruncate` | "truncate from both the end and front of files" for logs/FIFOs | :221-230 | yes |
| Sparse files | via truncate/fruncate/seek+write | :232-242 | yes |
| Name lookup O(log_b n) | B-tree over names | :244-246 | yes |
| M-tree | Replaces the threaded tree; "removing an entire category of possible bugs"; several dirs per block | :248-254 | yes |
| Sync model | Handles are snapshots; sync/close broadcast; desync; resync (5 rules) | :256-273 | yes (`lfs3_file_desync/resync`) |
| Stickynotes | `LFS3_TYPE_STICKYNOTE`, "hidden from the user and automatically cleaned up on the next mount" | :275-281 | yes |
| Compat flags | rcompat/wcompat/ocompat | :283-293 | yes |
| gcksums | Filesystem-wide checksum `lfs3_fs_cksum`. Check APIs: M/O/T/GC_CKMETA/CKDATA, `LFS3_M_CKPROGS`, `M_CKFETCHES`; `LFS3_M_CKREADS` **(planned)** | :295-313 | yes, but the API is now `lfs3_fs_ck/lfs3_file_ck/lfs3_fs_unck(flags)` (`lfs3.h:1652,1787,1811`). CKREADS is absent |
| Traversal API | `lfs3_trv_open/read`, no callbacks | :315-321 | yes |
| Incremental GC | `lfs3_fs_gc`, `gc_flags`, `gc_steps`; opt-in `LFS3_GC` | :323-329 | yes |
| Runtime error recovery | "Most in-RAM filesystem state should now revert to the last known-good state on error"; file *data* stays undefined | :331-339 | yes (claimed) |
| Standard attrs | 0x00-7f user, 0x80-bf reserved std, 0xc0-ff system | :341-351 | yes |
| More tests | "The goal is not 100% line/branch coverage, but just to have more confidence" | :353-365 | yes |
| KV API | `lfs3_get/size/set/remove`. `LFS3_KVONLY` and `LFS3_2BONLY` were reverted to stretch (issue:378) | :367-376; issue:162-183, 376-378 | API yes; KVONLY/2BONLY absent |
| gbmap | "(1) faster block allocation… (2) pre-erased block tracking, and (3) bad-block tracking" | :378-386; issue:613-625 | yes (`LFS3_GBMAP`, `lfs3_fs_mkgbmap/rmgbmap`) |
| Pre-erased blocks | Via gbmap plus "perturb bits in mdir revision counts" | :388-394; issue:627-633 | yes (`LFS3_PREERASE` needs GBMAP+REVPERTURB, `lfs3_util.h:115-118`) |

**In progress / planned** (`pr_1111.txt:396-424`; status 2026-02-20 `issue_1114.txt:637-640`):

| feature | intent | status |
|---|---|---|
| Bad-block tracking | "leverages the gbmap to mark blocks as bad"; open questions on the API and rdonly contexts (pr:398-402) | ⧖. **Release blocker #1**: "would be significantly valuable by making bd-level error-correction practical" (issue:837). Not in code: only `lfs3.c:16132` TODO |
| Metadata redundancy | "naive copies", using the unused second mdir block (pr:404-412) | ⧖; will not block (issue:840). Tag has a redund field (`lfs3.c:1057`) |
| Data redundancy | dedup tree + parity tree, RAID-6-like up to 3 blocks (pr:414-416) | ⧖; will not block |
| Block dedup | falls out of the virtual→physical map needed for ECC (pr:418-424) | ⧖; may fall out of scope |
| 16/64-bit | "stretch" (pr:432), later "⁇" (issue:640) | Disk impact to be investigated before stabilization (issue:642) |
| Docs | "Document, document, document" (pr:156) | **Release blocker #2**: DESIGN.md + SPEC.md, "a hard requirement before releasing" (issue:644, 838) |

**Stretch goals** (`pr_1111.txt:426-452`): "Please expect no stretch goals to be completed.
These will be moved to a wishlist of sorts after release" (`issue_1114.txt:380`).
- lfs3_migrate v2→v3 ("high-effort, high-risk, low-value", pr:795)
- 16/64-bit variants
- Config API rework
- Block-device API rework (context pointer, `bd_*` names, issue:430-438)
- Custom attr API rework
- Alternative write strategies
- punchhole/insertrange/collapserange/SEEK_DATA/HOLE
- cowcopy/copy
- Reserved blocks against CoW lockups
- Metadata lockup checks
- Integrated block ECC (ramcrc32bd, ramrsbd)
- Disk-level RAID
- `LFS3_KVONLY`, `LFS3_2BONLY` (downgraded in issue:378)

**Out of scope** (`pr_1111.txt:454-474`):
- alternative checksums
- feature-limited builds
- `openat`, `openn`
- transparent compression
- fs shrinking
- high-level caches
- symlinks
- **100% line/branch coverage**
- also: truly async API ("low-priority", issue:445-447) and MISRA ("fork", issue:323-343)

### 5.2 Release blockers and the v3-beta plan

- **2026-04-22** (`issue_1114.txt:829-842`):
  - "~2-3 months minimum of work left". The maintainer is simplifying file-write logic
    ("~70% improved fwrite performance"), still "finding room for improvement in the disk
    format".
  - Blockers: (1) bad-block tracking; (2) DESIGN.md and SPEC.md, "IMO it would be foolish
    to commit to a disk format without at least these two documents. And ideally some time
    for review by interested parties".
  - Redundancy and dedup: "high risk for the disk format, they're also low-value. I may
    prototype the ideas during the review period… they'll probably not 'block'".
  - **v3-beta:** "a sort of 'v3-beta' release, where the on-disk version is stable, but
    the (not-on-disk) API is still under flux… eager users can… pin a specific v3-beta
    version".
- **Disk version.** `v0.0` marks experimental images; "When it is eventually released, v3
  will reject this version and fail to mount" (`pr_1111.txt:163`, `lfs3.h:26`
  `LFS3_DISK_VERSION 0x00000000`). The API "will be under heavy flux" (`:165`).
- **Timeline.** "best guess is 2026" (issue:319). All performance work complete by
  2026-02-20 (issue:611-644). The last maintainer update in the captured thread is
  2026-04-22. A user asked on 2026-06-29 for v3-beta news (issue:846-851); no answer is
  captured.

### 5.3 Maintainer policy and concerns on contributions

- **Bug reports and discussion.** "For bugs, low-level questions, concerns, etc, feel free
  to create new issues with 'v3' in the title. I will label relevant issues with the v3
  label" (`issue_1114.txt:30`, `pr_1111.txt:718`). Keep #1114 to "high-level v3-wide
  discussion" (`pr_1111.txt:716`).
- **AI-generated code** (`issue_1114.txt:796-804`):
  - "littlefs in particular is a poor fit for AI generated code, as we have a relatively
    small amount of code where correctness is important, and bugs can be extremely subtle.
    This goes into the tests too. Writing tests requires quite a bit of thought to create
    scenarios where things are likely to break, without taking forever to run."
  - "one area where AI could be useful is all the scripts around littlefs. That's a lot of
    low-risk work."
  - Concerns: subscription-averse; "paranoia they're going to introduce
    difficult-to-reason-about bugs"; copyright/"poison a codebase".
  - Discussion moved to #1199 (`:823`).
- **Fuzzing.** "There's also a large body of work around fuzz testing which deserves
  exploration (#1030)" (`issue_1114.txt:800`). A user pushed for ~100% fuzzing branch
  coverage (`:820-821`). The maintainer's stance is that 100% coverage is out of scope
  (`pr_1111.txt:474`).
- **Testing priority.** "Proving functionality through testing/benchmarking is currently
  the best ROI in terms of building confidence in the on-disk format. Ensuring a bug free
  codebase is somewhat humorously a secondary concern" (`issue_1114.txt:333`).
- **Configurations.** "One thing that has worked well for littlefs2/1 is a small number of
  configurations"; KVONLY/2BONLY "have already caused problems for refactoring work, and
  I'm not convinced all permutations are tested well" (`issue_1114.txt:376-378`). Also "8
  different builds already… challenges fine grain configurations will introduce for
  building and testing" (`:183`, `:299`).
- **Code size.** "Open to any PRs/suggestions if anyone else wants to poke around at
  reducing the code size" (`issue_1114.txt:368`). Numbers: default 37352 B code / 2280 B
  stack / 636 B ctx vs v2 17144/1440/580 (`pr_1111.txt:513-517`); rdonly 10616/808/508
  (`:519-523`); later default 35256 and gbmap 38612 (`issue_1114.txt:621-624`).
- **Benchmarks.** "highly encourage others to do their own benchmarking… please share"
  (`pr_1111.txt:559`). Methodology and findings are at `issue_1114.txt:668-787`, e.g.
  fruncate is slower than rename rotation (`:757-759`) and the gbmap has little
  performance effect on NOR/NAND (`:753-755`).
- **Stability promise.** "This work may continue to break the on-disk format" (`pr:159`);
  "Once it's stabilized, it's stabilized" (`:161`); "I think a 4th version is very
  unlikely" (`:813`).

---

## 6. Coverage infrastructure

**Build and collect:**
- `COVGEN=1` adds `--coverage` to all CFLAGS (`Makefile:91-93`). `build-tests` deletes
  stale `$(TEST_GCDA)` (`:509-511`), so each (re)build starts clean. `GCDA` =
  `lfs3.t.a.gcda lfs3_util.t.a.gcda` (`:19`).
- `make cov` → `./scripts/cov.py $(GCDA) -Flfs3.c -Flfs3_util.c -s` (`:383-389`).
  `make lfs3.cov.csv` saves (`:760-763`); `make cov-diff` compares (`:395-400`).
- `cov.py` runs `gcov -b -t --json-format <gcda>` (`scripts/cov.py:297-312`). That needs
  **GCC ≥ 9 gcov**. Apple `gcov` (LLVM 21) has no JSON mode, so run coverage in the Linux
  container (`.local/docker/Dockerfile`, ubuntu:24.04 GCC).

**Result model.** `CovResult(file, function, line; calls, hits, funcs, lines, branches)`
(`scripts/cov.py:259-283`).
- Internal test functions (`__test__*`) and anything starting with `__` are dropped
  (`:343-367`).
- Injected test code maps back to `lfs3.c` through `#line 1 "lfs3.c"`
  (`scripts/test.py:707`). `-F` then restricts results to the real sources.

**Per-function coverage for `lfs3.c`: yes.**

```
COVGEN=1 make test            # or COVGEN=1 make test-runner && ./scripts/test.py -R runners/test_runner -b -j
./scripts/cov.py lfs3.t.a.gcda -Flfs3.c -bfunction -s      # per-function lines/branches/funcs
./scripts/cov.py lfs3.t.a.gcda -Flfs3.c -L                 # uncovered lines (-B branches, -A annotate)
./scripts/cov.py lfs3.t.a.gcda -Flfs3.c -e / -E            # exit non-zero on any uncovered line/branch
./scripts/cov.py -u lfs3.cov.csv -bfunction -o per_func.csv
```

Other options: `-b file|function|line`, `-f calls|hits|funcs|lines|branches`, `-S`/`-s`
sort, `-d` diff, `-Y` summary, `-O` JSON (`scripts/cov.py:1134-1345`).

**Caveats for the test plan:**
1. **Runtime variations can be merged, compile-time ones cannot.** gcda counters
   accumulate across runs of the *same* binary. Running `test.py` several times (other
   `-P` scenarios, `-D` geometries) without rebuilding merges coverage.
   Different compile-time configs (default vs `LFS3_BIGGEST`) produce different gcno.
   `cov.py` folds duplicate `(file, function, line)` rows by **summing** fractions
   (`CsvFrac.__add__` `:220-221`, `CovResult.__add__` `:277-283`). Feeding it two builds
   therefore double-counts denominators instead of taking a union. Report per config, or
   merge with gcovr/lcov.
2. **Crashes lose data.** Processes that `abort()` on an assert do not flush gcda. Coverage
   from failing permutations is lost.
3. **CI scope.** v2 CI deliberately measured coverage only on the quick default run
   ("we intentionally exclude more aggressive powerloss testing", `test.yml:97-101`).
   Under the default v3 build, the 58 feature-gated cases contribute nothing. For the
   gbmap, gc, preerase and ck* paths, measure coverage on a `LFS3_BIGGEST` build as well.
4. **Gating.** 100% coverage is explicitly out of scope (`pr_1111.txt:474`). A
   requirements doc should use a ratchet instead: `make cov-diff` against a baseline, with
   no regression in per-function line coverage. Do not use `-e`/`-E` hard gates.

---

## 7. Gaps to feed the test plan

Derived from §1-§6; each item is verifiable with runner list commands.

1. **METASTABLE never tested.** `POWERLOSS_METASTABLE` is not used by any test. The non-ATOMIC
   power-loss models are used only in the 4 `test_powerloss` cases.
2. **No combined faults.** No test injects bad blocks or bit flips together with power
   loss; `test_badblocks`, `test_exhaustion` and `test_ck` are not reentrant.
3. **Default build is partial.** It never exercises gbmap, GC, pre-erase, the ck* detection
   features, or READERROR/PROGNOOP/ERASENOOP bad blocks. CI must add `LFS3_BIGGEST` and
   `LFS3_YES_GBMAP` builds.
4. **Single geometry.** Only one geometry runs by default (v2 ran 5). Large-prog/NAND
   geometries filter out about two thirds of permutations (§1.4), so coverage there is
   thinner. There is no test of `read_size ≠ prog_size`, or of `cache_size >
   read/prog size` combos, beyond individual suites.
5. **Unimplemented or untested features.**
   - Bad-block tracking (planned, blocker): no tests.
   - `LFS3_M_CKREADS` (planned): absent.
   - `LFS3_THREADSAFE`: no implementation.
   - `LFS3_RDONLY`: build-only in v2 CI. v3 has no rdonly test run; it would need
     pre-built images.
6. **Limits.** No test covers the maximum-size limits the maintainer calls "untested":
   "limited to 2^31-1 for both file size and block count (though untested)"
   (`pr_1111.txt:785`). test_fwrite `*_fbig` covers only the file-size limit, and only at
   small scale.
7. **Missing in-tree harnesses.**
   - Migration: none (no v3 migrate). test_compat covers cross-version only with an
     externally linked `lfsp`, and only after disk stabilization.
   - littlefs-fuse self-host: gone from CI.
   - Coverage-guided fuzzing: the fuzz cases are PRNG-seeded; see #1030
     (`issue_1114.txt:800`).
