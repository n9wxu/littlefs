/* The flight log on littlefs v2, as the firmware configures it
 * that motivated this: read 1, prog 256, 4 kB blocks, 1 kB caches.
 *
 *   bench_v2 RATE_HZ SECONDS */
#include "lfs.h"
#include "bd/lfs_emubd.h"
#include "model.h"

static int w_read(const struct lfs_config *c, lfs_block_t b, lfs_off_t off, void *buf, lfs_size_t size) {
    model_read(size);
    return lfs_emubd_read(c, b, off, buf, size);
}
static int w_prog(const struct lfs_config *c, lfs_block_t b, lfs_off_t off, const void *buf, lfs_size_t size) {
    model_prog(off, size);
    return lfs_emubd_prog(c, b, off, buf, size);
}
static int w_erase(const struct lfs_config *c, lfs_block_t b) {
    model_erase();
    return lfs_emubd_erase(c, b);
}
static int w_sync(const struct lfs_config *c) {
    return lfs_emubd_sync(c);
}

int main(int argc, char **argv) {
    unsigned rate = argc > 1 ? (unsigned)atoi(argv[1]) : 1;
    unsigned seconds = argc > 2 ? (unsigned)atoi(argv[2]) : 600;
    static lfs_emubd_t bd;
    static uint8_t rbuf[1024], pbuf[1024], look[16], fbuf[1024];
    struct lfs_config cfg = {
        .context = &bd, .read = w_read, .prog = w_prog, .erase = w_erase, .sync = w_sync,
        .read_size = 1, .prog_size = 256, .block_size = 4096, .block_count = 2048,
        .cache_size = 1024, .lookahead_size = 16, .block_cycles = 500,
        .read_buffer = rbuf, .prog_buffer = pbuf, .lookahead_buffer = look,
    };
    struct lfs_emubd_config bdcfg = {.read_size = 1, .prog_size = 256, .erase_size = 4096,
                                     .erase_count = 2048, .erase_value = 0xff};
    if (lfs_emubd_create(&cfg, &bdcfg))
        return 2;
    lfs_t lfs;
    if (lfs_format(&lfs, &cfg) || lfs_mount(&lfs, &cfg))
        return 3;
    lfs_mkdir(&lfs, "/www");
    static uint8_t junk[65536];
    for (unsigned i = 0; i < sizeof(junk); i++)
        junk[i] = (uint8_t)(i * 7u);
    for (unsigned i = 0; i < N_SEED; i++) {
        lfs_file_t f;
        struct lfs_file_config fc = {.buffer = fbuf};
        if (lfs_file_opencfg(&lfs, &f, SEED[i].path, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC, &fc))
            return 4;
        lfs_file_write(&lfs, &f, junk, SEED[i].size);
        lfs_file_close(&lfs, &f);
    }

    memset(&M, 0, sizeof(M));
    callstat_t cw = {0}, cs = {0};
    lfs_file_t f;
    struct lfs_file_config fc = {.buffer = fbuf};
    if (lfs_file_opencfg(&lfs, &f, "/flight_log.bin", LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC, &fc))
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
            if (lfs_file_write(&lfs, &f, buf, n * ROW) != (lfs_ssize_t)(n * ROW))
                return 6;
            span_end(s, &cw);
        }
        if (ms % 1000u == 0) {
            span_t s = span_begin();
            if (lfs_file_sync(&lfs, &f))
                return 7;
            span_end(s, &cs);
        }
    }
    lfs_file_close(&lfs, &f);
    printf("littlefs v2.11.3, %u rows/s for %u s: %u rows, %u bytes\n", rate, seconds, seq, seq * ROW);
    print_calls("write", &cw);
    print_calls("sync", &cs);
    printf("  per minute: %.1f erases, %.1f page programs, %.1f ms of program and erase\n",
           M.erases * 60.0 / seconds, M.progs * 60.0 / seconds, M.stop_ns / 1e6 * 60.0 / seconds);

    lfs_unmount(&lfs);
    if (lfs_mount(&lfs, &cfg))
        return 8;
    static uint8_t back[4 << 20];
    lfs_file_open(&lfs, &f, "/flight_log.bin", LFS_O_RDONLY);
    lfs_ssize_t got = lfs_file_read(&lfs, &f, back, sizeof(back));
    lfs_file_close(&lfs, &f);
    long ok = check_rows(back, (size_t)got);
    printf("  read back: %ld of %u rows %s\n", ok < 0 ? -ok - 1 : ok, seq, ok == (long)seq ? "whole" : "BAD");
    lfs_unmount(&lfs);
    lfs_emubd_destroy(&cfg);
    return ok == (long)seq ? 0 : 1;
}
