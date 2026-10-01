/*
 * Runner for littlefs tests
 *
 * Copyright (c) 2022, The littlefs authors.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef TEST_RUNNER_H
#define TEST_RUNNER_H

#define TEST_STRINGIFY_(x) #x
#define TEST_STRINGIFY(x) TEST_STRINGIFY_(x)

// the default TEST_DEFINES path can be overridden to add shims for
// other filesystems out-of-tree
//
// note this is an unusual header file! instead of being included once,
// TEST_DEFINES is included several times with various "query macros"
// defined before inclusion:
//
// - TEST_INCLUDE - common includes (optional)
// - TEST_DEFINE(name, value) - name and default values for test defines
// - TEST_CFG[+_CFG] - struct lfs3_cfg definition
// - TEST_BDCFG[+_CFG] - struct lfs3_*bd_cfg definition
//
#ifndef TEST_DEFINES
#define TEST_DEFINES runners/test_defines.h
#endif

// default to using emubd for tests
#if !defined(TEST_EMUBD) && !defined(TEST_KIWIBD)
#define TEST_EMUBD
#endif

// ifdef macros for emubd vs kiwibd
#ifdef TEST_EMUBD
#define TEST_IFDEF_EMUBD(a, b) (a)
#else
#define TEST_IFDEF_EMUBD(a, b) (b)
#endif
#ifdef TEST_KIWIBD
#define TEST_IFDEF_KIWIBD(a, b) (a)
#else
#define TEST_IFDEF_KIWIBD(a, b) (b)
#endif

// override LFS3_TRACE
#ifndef LFS3_NO_TRACE
void test_trace(const char *fmt, ...);
#define LFS3_TRACE_(fmt, ...) \
    test_trace("%s:%d:trace: " fmt "%s\n", __FILE__, __LINE__, __VA_ARGS__)
#define LFS3_TRACE(...) LFS3_TRACE_(__VA_ARGS__, "")
#define LFS3_EMUBD_TRACE(...) LFS3_TRACE_(__VA_ARGS__, "")
#define LFS3_KIWIBD_TRACE(...) LFS3_TRACE_(__VA_ARGS__, "")
#else
#define LFS3_TRACE(...)
#define LFS3_EMUBD_TRACE(...)
#define LFS3_KIWIBD_TRACE(...)
#endif

// route lfs3_malloc/lfs3_free through the runner's allocator hooks
#include <stddef.h>
void *test_malloc(size_t size);
void test_free(void *p);
#ifndef LFS3_MALLOC
#define LFS3_MALLOC(size) test_malloc(size)
#endif
#ifndef LFS3_FREE
#define LFS3_FREE(p) test_free(p)
#endif


// note these are indirectly included in any generated files
#define TEST_INCLUDE
    #include TEST_STRINGIFY(TEST_DEFINES)
#undef TEST_INCLUDE

#ifndef TEST_KIWIBD
#include "bd/lfs3_emubd.h"
#else
#include "bd/lfs3_kiwibd.h"
#endif
#include "lfs3_util.h"

#include <stdio.h>
#include <stdint.h>

// give source a chance to define feature macros
#undef _FEATURES_H
#undef _STDIO_H


// some common types
#ifndef TEST_KIWIBD
typedef lfs3_emubd_ns_t   test_ns_t;
typedef lfs3_emubd_sns_t  test_sns_t;
typedef lfs3_emubd_powercycles_t  test_powercycles_t;
typedef lfs3_emubd_spowercycles_t test_spowercycles_t;
#else
typedef lfs3_kiwibd_ns_t  test_ns_t;
typedef lfs3_kiwibd_sns_t test_sns_t;
typedef void              test_powercycles_t;
typedef void              test_spowercycles_t;
#endif

// generated test configurations
struct lfs3_cfg;

enum test_flags {
    TEST_INTERNAL  = 0x1,
    TEST_REENTRANT = 0x2,
    TEST_FUZZ      = 0x4,
};
typedef uint8_t test_flags_t;

typedef struct test_define {
    const char *name;
    intmax_t *define;
    intmax_t (*cb)(void *data, size_t i);
    void *data;
    size_t permutations;
} test_define_t;

struct test_case {
    const char *name;
    const char *path;
    test_flags_t flags;

    const test_define_t *defines;
    size_t permutations;

    bool (*if_)(void);
    void (*run)(const struct lfs3_cfg *cfg);

    // death tests
    const char *death;
    void (*death_run)(const struct lfs3_cfg *cfg);
};

struct test_suite {
    const char *name;
    const char *path;
    test_flags_t flags;

    const test_define_t *defines;
    size_t define_count;

    const struct test_case *cases;
    size_t case_count;
};

extern const struct test_suite *const test_suites[];
extern const size_t test_suite_count;


// this variable tracks the number of powerlosses triggered during the
// current test permutation, this is useful for both tests and debugging
extern volatile test_powercycles_t TEST_PLS;

// deterministic prng for pseudo-randomness in tests
uint32_t test_prng(uint32_t *state);

#define TEST_PRNG(state) test_prng(state)

// generation of specific permutations of an array for exhaustive testing
size_t test_factorial(size_t x);
void test_permutation(size_t i, uint32_t *buffer, size_t size);

#define TEST_FACTORIAL(x) test_factorial(x)
#define TEST_PERMUTATION(i, buffer, size) test_permutation(i, buffer, size)

// option to pause trace output
void test_trace_pause(void);
void test_trace_resume(void);

#define TEST_TRACE_PAUSE() test_trace_pause()
#define TEST_TRACE_RESUME() test_trace_resume()

// allocator hooks, these see every lfs3_malloc and lfs3_free, but not
// the runner's or emubd's own allocations, and reset for every
// permutation
//
// - TEST_MALLOCS() - number of lfs3_malloc calls, including failed calls
// - TEST_MALLOCS_LIVE() - number of allocations not yet lfs3_free'd
// - TEST_FAILMALLOC(n) - make the nth lfs3_malloc from now return NULL,
//   only that call fails, 0 disables
//
size_t test_mallocs(void);
size_t test_mallocs_live(void);
void test_failmalloc(size_t n);

#define TEST_MALLOCS() test_mallocs()
#define TEST_MALLOCS_LIVE() test_mallocs_live()
#define TEST_FAILMALLOC(n) test_failmalloc(n)

// death tests
//
// A case with death = 'text' runs each permutation in a forked child, and
// passes only if the child dies on an assert whose report
// (path:line:assert: message) or source line contains text, '' matches
// any assert. The case's optional death_code then runs in the parent,
// with TEST_DEATH holding the child's emubd counters when it died, so
// lfs3_emubd_simreset in the child marks where counting starts. Death
// cases can't be reentrant.
//
// test_death runs fn(data) the same way from inside a test, reporting
// cfg's emubd counters in TEST_DEATH. The child's writes, including to a
// -d disk file, don't reach the parent's device.
//
typedef struct test_death {
    uint64_t reads;
    uint64_t progs;
    uint64_t erases;
    uint64_t readed;
    uint64_t progged;
    uint64_t erased;
} test_death_t;

extern test_death_t TEST_DEATH;

enum test_death_result {
    TEST_DEATH_DIED     = 0, // died on a matching assert
    TEST_DEATH_SURVIVED = 1, // returned
    TEST_DEATH_MISMATCH = 2, // died on an assert that doesn't match
    TEST_DEATH_KILLED   = 3, // died without an assert
};

int test_death(const struct lfs3_cfg *cfg, const char *death,
        void (*fn)(void *data), void *data);


// declare implicit defines as global intmax_ts
#define TEST_DEFINE(k, v) \
        extern intmax_t k;
    #include TEST_STRINGIFY(TEST_DEFINES)
#undef TEST_DEFINE


#endif
