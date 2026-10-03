/* The flight log on littlefs v3-alpha, on the same flash and workload as
 * bench_v2.c.
 *
 *   bench_v3 RATE_HZ SECONDS PROG_SIZE GBMAP PREERASE [FRAGMENT CRYSTAL]
 *
 * GBMAP 1 formats with the on-disk block map; PREERASE 1 runs gc with
 * pre-erasing to completion on the pad, before the log opens. */
#include "lfs3.h"
#include "bd/lfs3_emubd.h"
#include "model.h"

static int w_read(const struct lfs3_cfg *c, lfs3_block_t b, lfs3_off_t off, void *buf, lfs3_size_t size) {
    model_read(size);
    return lfs3_emubd_read(c, b, off, buf, size);
}
static int w_prog(const struct lfs3_cfg *c, lfs3_block_t b, lfs3_off_t off, const void *buf, lfs3_size_t size) {
    model_prog(off, size);
    return lfs3_emubd_prog(c, b, off, buf, size);
}
static int w_erase(const struct lfs3_cfg *c, lfs3_block_t b) {
    model_erase();
    return lfs3_emubd_erase(c, b);
}
static int w_sync(const struct lfs3_cfg *c) {
    return lfs3_emubd_sync(c);
}

int main(int argc, char **argv) {
    unsigned rate = argc > 1 ? (unsigned)atoi(argv[1]) : 1;
    unsigned seconds = argc > 2 ? (unsigned)atoi(argv[2]) : 600;
    unsigned prog = argc > 3 ? (unsigned)atoi(argv[3]) : 256;
    int gbmap = argc > 4 ? atoi(argv[4]) : 0;
    int preerase = argc > 5 ? atoi(argv[5]) : 0;
    unsigned fragment = argc > 6 ? (unsigned)atoi(argv[6]) : 256;
    unsigned crystal = argc > 7 ? (unsigned)atoi(argv[7]) : 256;

    static lfs3_emubd_t bd;
    struct lfs3_cfg cfg = {
        .context = &bd, .read = w_read, .prog = w_prog, .erase = w_erase, .sync = w_sync,
        .read_size = 1, .prog_size = prog, .block_size = 4096, .block_count = 2048,
        .block_recycles = 512, .rcache_size = 1024, .pcache_size = 1024, .fcache_size = 1024,
        .lookahead_size = 16,
        .gc_flags = LFS3_GC_GC, .gc_steps = -1, .gc_lookahead_thresh = -1,
        .gc_lookgbmap_thresh = -1, .gc_preerase_count = preerase ? -1 : 0,
        .gc_compact_thresh = 0,
        .shrub_size = 1024, .fragment_size = fragment, .crystal_thresh = crystal,
        .lookgbmap_thresh = 2048 / 4,
    };
    struct lfs3_emubd_cfg bdcfg = {.erase_value = 0xff, .erase_cycles = 0, .power_cycles = 0};
    if (lfs3_emubd_createcfg(&cfg, NULL, &bdcfg))
        return 2;
    lfs3_t lfs;
    if (lfs3_format(&lfs, LFS3_F_RDWR | (gbmap ? LFS3_F_GBMAP : 0), &cfg) || lfs3_mount(&lfs, LFS3_M_RDWR | (preerase ? LFS3_M_REVPERTURB : 0), &cfg))
        return 3;
    lfs3_mkdir(&lfs, "/www");
    static uint8_t junk[65536];
    for (unsigned i = 0; i < sizeof(junk); i++)
        junk[i] = (uint8_t)(i * 7u);
    for (unsigned i = 0; i < N_SEED; i++) {
        lfs3_file_t f;
        if (lfs3_file_open(&lfs, &f, SEED[i].path, LFS3_O_WRONLY | LFS3_O_CREAT | LFS3_O_TRUNC))
            return 4;
        lfs3_file_write(&lfs, &f, junk, SEED[i].size);
        lfs3_file_close(&lfs, &f);
    }

    memset(&M, 0, sizeof(M));
    double pad_ms = 0.0;
    uint64_t pad_erases = 0;
    if (preerase) {
        int err = lfs3_fs_gc(&lfs);
        if (err) {
            printf("gc: %d\n", err);
            return 9;
        }
        pad_ms = M.t_ns / 1e6;
        pad_erases = M.erases;
        memset(&M, 0, sizeof(M));
    }

    callstat_t cw = {0}, cs = {0};
    lfs3_file_t f;
    if (lfs3_file_open(&lfs, &f, "/flight_log.bin", LFS3_O_WRONLY | LFS3_O_CREAT | LFS3_O_TRUNC))
        return 5;
    uint32_t seq = 0;
    double due = 0.0;
    for (unsigned ms = 200; ms <= seconds * 1000u; ms += 200) {
        due += rate * 0.2;
        uint8_t buf[64 * ROW];
        unsigned n = 0;
        while (due >= 1.0 && n < 64) {
            make_row(seq++, buf + n * ROW);
            n++;
            due -= 1.0;
        }
        if (n) {
            span_t s = span_begin();
            if (lfs3_file_write(&lfs, &f, buf, n * ROW) != (lfs3_ssize_t)(n * ROW))
                return 6;
            span_end(s, &cw);
        }
        if (ms % 1000u == 0) {
            span_t s = span_begin();
            int err = lfs3_file_sync(&lfs, &f);
            if (err) {
                printf("sync: %d\n", err);
                return 7;
            }
            span_end(s, &cs);
        }
    }
    lfs3_file_close(&lfs, &f);
    printf("littlefs v3-alpha, prog %u, %s, fragment %u, crystal %u; %u rows/s for %u s: %u rows\n", prog,
           gbmap ? (preerase ? "gbmap + pre-erased on the pad" : "gbmap") : "no gbmap", fragment, crystal, rate,
           seconds, seq);
    if (preerase)
        printf("  pad: gc took %.0f ms, %llu erases\n", pad_ms, (unsigned long long)pad_erases);
    print_calls("write", &cw);
    print_calls("sync", &cs);
    printf("  per minute: %.1f erases, %.1f page programs, %.1f ms of program and erase\n",
           M.erases * 60.0 / seconds, M.progs * 60.0 / seconds, M.stop_ns / 1e6 * 60.0 / seconds);

    lfs3_unmount(&lfs);
    if (lfs3_mount(&lfs, LFS3_M_RDWR | (preerase ? LFS3_M_REVPERTURB : 0), &cfg))
        return 8;
    static uint8_t back[4 << 20];
    lfs3_file_open(&lfs, &f, "/flight_log.bin", LFS3_O_RDONLY);
    lfs3_ssize_t got = lfs3_file_read(&lfs, &f, back, sizeof(back));
    lfs3_file_close(&lfs, &f);
    long ok = check_rows(back, (size_t)(got > 0 ? got : 0));
    printf("  read back: %ld of %u rows %s\n", ok < 0 ? -ok - 1 : ok, seq, ok == (long)seq ? "whole" : "BAD");
    lfs3_unmount(&lfs);
    lfs3_emubd_destroy(&cfg);
    return ok == (long)seq ? 0 : 1;
}
