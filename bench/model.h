/* The flash as the flight core feels it: every program and erase stops the
 * flight core of an RP2040 logger; reads are XIP and do not. W25Q128JV
 * typical figures from the Winbond datasheet, revision H, AC
 * characteristics. */
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define PAGE 256u
#define SECTOR 4096u
static double PROG_NS = 400000.0;    /* per 256-byte page, typ (max 3 ms)   */
static double ERASE_NS = 45000000.0; /* per 4 kB sector, typ (max 400 ms)   */
#define READ_NS_PER_BYTE 25.0        /* XIP                                  */
#define READ_NS_CALL 1000.0

typedef struct {
    double t_ns;                 /* all simulated time                      */
    double stop_ns;              /* the flight core stopped: progs + erases */
    uint64_t reads, progs, erases, read_bytes, prog_bytes;
} model_t;
static model_t M;

static void model_read(uint32_t size) {
    M.t_ns += READ_NS_CALL + size * READ_NS_PER_BYTE;
    M.reads++;
    M.read_bytes += size;
}
static void model_prog(uint32_t off, uint32_t size) {
    uint32_t first = off / PAGE, last = (off + size - 1) / PAGE;
    uint32_t pages = last - first + 1;
    M.t_ns += pages * PROG_NS;
    M.stop_ns += pages * PROG_NS;
    M.progs += pages;
    M.prog_bytes += size;
}
static void model_erase(void) {
    M.t_ns += ERASE_NS;
    M.stop_ns += ERASE_NS;
    M.erases++;
}

/* Per call: how long it took, and whether it erased. */
typedef struct {
    uint64_t calls, calls_erasing;
    double max_ns, sum_ns, max_stop_ns;
    uint64_t erases, progs;
} callstat_t;

typedef struct {
    model_t before;
} span_t;
static span_t span_begin(void) {
    span_t s = {M};
    return s;
}
static void span_end(span_t s, callstat_t *c) {
    double dt = M.t_ns - s.before.t_ns, ds = M.stop_ns - s.before.stop_ns;
    uint64_t de = M.erases - s.before.erases;
    c->calls++;
    c->sum_ns += dt;
    if (dt > c->max_ns)
        c->max_ns = dt;
    if (ds > c->max_stop_ns)
        c->max_stop_ns = ds;
    if (de)
        c->calls_erasing++;
    c->erases += de;
    c->progs += M.progs - s.before.progs;
}

/* A flight-log row: 22 bytes, as flight_log.h's, with a sequence number and a
 * CRC so a log read back can be checked. */
#define ROW 22u
static uint32_t crc32(const uint8_t *p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & -(c & 1u));
    }
    return ~c;
}
static void make_row(uint32_t seq, uint8_t *r) {
    memset(r, 0, ROW);
    memcpy(r, &seq, 4);
    for (unsigned i = 4; i < ROW - 4; i++)
        r[i] = (uint8_t)(seq * 31u + i);
    uint32_t c = crc32(r, ROW - 4);
    memcpy(r + ROW - 4, &c, 4);
}
/* Rows 0..n-1 in order and whole: n, or -1 at the first bad one. */
static long check_rows(const uint8_t *buf, size_t len) {
    size_t n = len / ROW;
    for (size_t i = 0; i < n; i++) {
        uint8_t want[ROW];
        make_row((uint32_t)i, want);
        if (memcmp(buf + i * ROW, want, ROW) != 0)
            return -(long)i - 1;
    }
    return (long)n;
}

/* The files a board holds besides the log. */
typedef struct {
    const char *path;
    uint32_t size;
} seed_t;
static const seed_t SEED[] = {
    {"/config.ini", 226},       {"/pins.ini", 1024},       {"/beep.ini", 512},
    {"/serial.txt", 12},        {"/www/index.html", 14461}, {"/www/app.js", 59121},
    {"/www/style.css", 2076},
};
#define N_SEED (sizeof(SEED) / sizeof(SEED[0]))

static void print_calls(const char *what, const callstat_t *c) {
    printf("  %-6s %6llu calls, %5llu erasing; mean %7.2f ms, max %7.2f ms; flight core stopped at most %6.2f ms; "
           "%llu erases, %llu page programs\n",
           what, (unsigned long long)c->calls, (unsigned long long)c->calls_erasing,
           c->calls ? c->sum_ns / c->calls / 1e6 : 0.0, c->max_ns / 1e6, c->max_stop_ns / 1e6,
           (unsigned long long)c->erases, (unsigned long long)c->progs);
}
