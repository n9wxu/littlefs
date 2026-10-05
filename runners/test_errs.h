/*
 * Record the error codes littlefs returns to the tests
 *
 * Copyright (c) 2022, The littlefs authors.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef TEST_ERRS_H
#define TEST_ERRS_H

#include "lfs3.h"

// test.py includes this after the runner's headers and any source it
// compiles, and before any test code, so every public call a test makes
// goes through test_err, while littlefs's own calls don't
//
// test_err returns err unchanged. With TEST_ERRS naming a file, it
// appends each new (function, negative result) pair to it, see
// scripts/ckerrs.py. With LFS3_THREADSAFE, it also checks that the call
// took the lock once and released it, see TEST_LOCK.
int test_err(const char *func, int err);

#define TEST_ERR(func, ...) test_err(#func, func(__VA_ARGS__))

// with LFS3_THREADSAFE, the codes tests give the runner's lock and unlock
// to fail with, outside enum lfs3_err, scripts/ckerrs.py accepts them
// from a function whose "Returns" paragraph names that callback
#define TEST_ERR_LOCK   -1001
#define TEST_ERR_UNLOCK -1002

#define lfs3_format(...)         TEST_ERR(lfs3_format, __VA_ARGS__)
#define lfs3_mount(...)          TEST_ERR(lfs3_mount, __VA_ARGS__)
#define lfs3_unmount(...)        TEST_ERR(lfs3_unmount, __VA_ARGS__)
#define lfs3_get(...)            TEST_ERR(lfs3_get, __VA_ARGS__)
#define lfs3_size(...)           TEST_ERR(lfs3_size, __VA_ARGS__)
#define lfs3_set(...)            TEST_ERR(lfs3_set, __VA_ARGS__)
#define lfs3_remove(...)         TEST_ERR(lfs3_remove, __VA_ARGS__)
#define lfs3_rename(...)         TEST_ERR(lfs3_rename, __VA_ARGS__)
#define lfs3_stat(...)           TEST_ERR(lfs3_stat, __VA_ARGS__)
#define lfs3_getattr(...)        TEST_ERR(lfs3_getattr, __VA_ARGS__)
#define lfs3_sizeattr(...)       TEST_ERR(lfs3_sizeattr, __VA_ARGS__)
#define lfs3_setattr(...)        TEST_ERR(lfs3_setattr, __VA_ARGS__)
#define lfs3_removeattr(...)     TEST_ERR(lfs3_removeattr, __VA_ARGS__)
#ifndef LFS3_NO_MALLOC
#define lfs3_file_open(...)      TEST_ERR(lfs3_file_open, __VA_ARGS__)
#else
// without malloc the runner gives each file a cache
#undef lfs3_file_open
#define lfs3_file_open(...) \
        test_err("lfs3_file_open", test_file_open(__VA_ARGS__))
#endif
#define lfs3_file_opencfg(...)   TEST_ERR(lfs3_file_opencfg, __VA_ARGS__)
#define lfs3_file_close(...)     TEST_ERR(lfs3_file_close, __VA_ARGS__)
#define lfs3_file_sync(...)      TEST_ERR(lfs3_file_sync, __VA_ARGS__)
#define lfs3_file_flush(...)     TEST_ERR(lfs3_file_flush, __VA_ARGS__)
#define lfs3_file_desync(...)    TEST_ERR(lfs3_file_desync, __VA_ARGS__)
#define lfs3_file_resync(...)    TEST_ERR(lfs3_file_resync, __VA_ARGS__)
#define lfs3_file_read(...)      TEST_ERR(lfs3_file_read, __VA_ARGS__)
#define lfs3_file_write(...)     TEST_ERR(lfs3_file_write, __VA_ARGS__)
#define lfs3_file_seek(...)      TEST_ERR(lfs3_file_seek, __VA_ARGS__)
#define lfs3_file_truncate(...)  TEST_ERR(lfs3_file_truncate, __VA_ARGS__)
#define lfs3_file_fruncate(...)  TEST_ERR(lfs3_file_fruncate, __VA_ARGS__)
#define lfs3_file_tell(...)      TEST_ERR(lfs3_file_tell, __VA_ARGS__)
#define lfs3_file_rewind(...)    TEST_ERR(lfs3_file_rewind, __VA_ARGS__)
#define lfs3_file_size(...)      TEST_ERR(lfs3_file_size, __VA_ARGS__)
#define lfs3_file_ck(...)        TEST_ERR(lfs3_file_ck, __VA_ARGS__)
#define lfs3_mkdir(...)          TEST_ERR(lfs3_mkdir, __VA_ARGS__)
#define lfs3_dir_open(...)       TEST_ERR(lfs3_dir_open, __VA_ARGS__)
#define lfs3_dir_close(...)      TEST_ERR(lfs3_dir_close, __VA_ARGS__)
#define lfs3_dir_read(...)       TEST_ERR(lfs3_dir_read, __VA_ARGS__)
#define lfs3_dir_seek(...)       TEST_ERR(lfs3_dir_seek, __VA_ARGS__)
#define lfs3_dir_tell(...)       TEST_ERR(lfs3_dir_tell, __VA_ARGS__)
#define lfs3_dir_rewind(...)     TEST_ERR(lfs3_dir_rewind, __VA_ARGS__)
#define lfs3_trv_open(...)       TEST_ERR(lfs3_trv_open, __VA_ARGS__)
#define lfs3_trv_close(...)      TEST_ERR(lfs3_trv_close, __VA_ARGS__)
#define lfs3_trv_read(...)       TEST_ERR(lfs3_trv_read, __VA_ARGS__)
#define lfs3_trv_rewind(...)     TEST_ERR(lfs3_trv_rewind, __VA_ARGS__)
#define lfs3_fs_stat(...)        TEST_ERR(lfs3_fs_stat, __VA_ARGS__)
#define lfs3_fs_usage(...)       TEST_ERR(lfs3_fs_usage, __VA_ARGS__)
#define lfs3_fs_health(...)      TEST_ERR(lfs3_fs_health, __VA_ARGS__)
#define lfs3_fs_cksum(...)       TEST_ERR(lfs3_fs_cksum, __VA_ARGS__)
#define lfs3_fs_mkconsistent(...) \
        TEST_ERR(lfs3_fs_mkconsistent, __VA_ARGS__)
#define lfs3_fs_ck(...)          TEST_ERR(lfs3_fs_ck, __VA_ARGS__)
#define lfs3_fs_gc(...)          TEST_ERR(lfs3_fs_gc, __VA_ARGS__)
#define lfs3_fs_unck(...)        TEST_ERR(lfs3_fs_unck, __VA_ARGS__)
#define lfs3_fs_grow(...)        TEST_ERR(lfs3_fs_grow, __VA_ARGS__)
#define lfs3_fs_mkgbmap(...)     TEST_ERR(lfs3_fs_mkgbmap, __VA_ARGS__)
#define lfs3_fs_rmgbmap(...)     TEST_ERR(lfs3_fs_rmgbmap, __VA_ARGS__)
#define lfs3_fs_mkbad(...)       TEST_ERR(lfs3_fs_mkbad, __VA_ARGS__)
#define lfs3_fs_mkgood(...)      TEST_ERR(lfs3_fs_mkgood, __VA_ARGS__)
#define lfs3_fs_nextbad(...)     TEST_ERR(lfs3_fs_nextbad, __VA_ARGS__)
#define lfs3_fs_nextsuspect(...) TEST_ERR(lfs3_fs_nextsuspect, __VA_ARGS__)

#endif
