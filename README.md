# littlefs v3 working notes

Notes from an analysis of littlefs v3 (v3-alpha, b10efaa), kept for
reference. The distilled results are SPEC.md, DESIGN.md, REQUIREMENTS.md and
TEST_PLAN.md on branch `v3-docs`, and the fixes and tests are on
`v3-integration`. Line numbers in these notes refer to b10efaa; most findings
have since been fixed, see the issues and REQUIREMENTS.md's defect register.

- `analysis/1-metadata.md`: rbyds, mdirs, the mtree, compaction, gstate, mount
- `analysis/2-files.md`: B-trees and B-shrubs, the write and sync paths,
  stickynotes, power-loss guarantees
- `analysis/3-alloc-failures.md`: lookahead and gbmap allocation, pre-erase,
  gc, flash error handling, the flash-failure matrix, and the bad-block
  tracking proposal (§8, now implemented on `v3-integration`)
- `analysis/4-integrity-api.md`: checksums, every public function's contract,
  configuration
- `analysis/5-verification-roadmap.md`: the test framework, CI, suites,
  roadmap

`bench/` holds the flight-log benchmark (v2.11.3 vs v3 on a simulated
W25Q128JV NOR flash) quoted in DESIGN.md and the prog_size performance
issue. Build `bench_v3.c` with lfs3.c, lfs3_util.c and bd/lfs3_emubd.c from
`v3-integration` (add `-DLFS3_GC -DLFS3_GBMAP -DLFS3_PREERASE
-DLFS3_REVPERTURB` for the pre-erase runs), and `bench_v2.c` with littlefs
v2.11.3.
