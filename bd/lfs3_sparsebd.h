/*
 * sparsebd - A block device emulated in RAM that only allocates the
 * blocks that are erased or programmed
 *
 * Useful for testing block counts far larger than available RAM, up to
 * 2^31-1, as long as only a small part of the disk is written.
 *
 * Copyright (c) 2022, The littlefs authors.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef LFS3_SPARSEBD_H
#define LFS3_SPARSEBD_H

#include "lfs3.h"
#include "lfs3_util.h"


// Block device specific tracing
#ifndef LFS3_SPARSEBD_TRACE
#ifdef LFS3_SPARSEBD_YES_TRACE
#define LFS3_SPARSEBD_TRACE(...) LFS3_TRACE(__VA_ARGS__)
#else
#define LFS3_SPARSEBD_TRACE(...)
#endif
#endif

// sparsebd config (optional)
struct lfs3_sparsebd_cfg {
    // 8-bit erase value to use for simulating erases, blocks that were
    // never erased or programmed also read as this value. -1 makes
    // erase a noop, and unwritten blocks read as zeros.
    int32_t erase_value;
};

// a written block
typedef struct lfs3_sparsebd_block {
    lfs3_block_t block;
    uint8_t *data;
} lfs3_sparsebd_block_t;

// sparsebd state
typedef struct lfs3_sparsebd {
    // open-addressed hash table of written blocks, data=NULL marks an
    // unused slot
    lfs3_sparsebd_block_t *table;
    lfs3_block_t capacity;
    lfs3_block_t count;

    // number of read/prog/erase operations
    uint64_t reads;
    uint64_t progs;
    uint64_t erases;

    const struct lfs3_sparsebd_cfg *cfg;
} lfs3_sparsebd_t;


/// Block device API ///

// Create a sparse RAM block device using the geometry in lfs3_cfg
int lfs3_sparsebd_create(const struct lfs3_cfg *cfg);
int lfs3_sparsebd_createcfg(const struct lfs3_cfg *cfg,
        const struct lfs3_sparsebd_cfg *bdcfg);

// Clean up memory associated with block device
int lfs3_sparsebd_destroy(const struct lfs3_cfg *cfg);

// Read a block
int lfs3_sparsebd_read(const struct lfs3_cfg *cfg, lfs3_block_t block,
        lfs3_off_t off, void *buffer, lfs3_size_t size);

// Program a block
//
// The block must have previously been erased.
int lfs3_sparsebd_prog(const struct lfs3_cfg *cfg, lfs3_block_t block,
        lfs3_off_t off, const void *buffer, lfs3_size_t size);

// Erase a block
//
// A block must be erased before being programmed. The
// state of an erased block is undefined.
int lfs3_sparsebd_erase(const struct lfs3_cfg *cfg, lfs3_block_t block);

// Sync the block device
int lfs3_sparsebd_sync(const struct lfs3_cfg *cfg);


/// Additional sparsebd features for testing ///

// Get the number of blocks backed by memory, every block ever erased or
// programmed
lfs3_block_t lfs3_sparsebd_used(const struct lfs3_cfg *cfg);


#endif
