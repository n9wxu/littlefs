/*
 * sparsebd - A block device emulated in RAM that only allocates the
 * blocks that are erased or programmed
 *
 * Copyright (c) 2022, The littlefs authors.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "bd/lfs3_sparsebd.h"

#include <stdlib.h>


// mix all bits of the block address, so blocks that differ only in their
// high bits don't collide
static lfs3_block_t lfs3_sparsebd_hash(const lfs3_sparsebd_t *bd,
        lfs3_block_t block) {
    uint32_t x = block;
    x ^= x >> 16;
    x *= 0x7feb352d;
    x ^= x >> 15;
    x *= 0x846ca68b;
    x ^= x >> 16;
    return x & (bd->capacity-1);
}

static lfs3_sparsebd_block_t *lfs3_sparsebd_lookup(
        const lfs3_sparsebd_t *bd, lfs3_block_t block) {
    for (lfs3_block_t i = lfs3_sparsebd_hash(bd, block);;
            i = (i+1) & (bd->capacity-1)) {
        if (!bd->table[i].data || bd->table[i].block == block) {
            return &bd->table[i];
        }
    }
}

// find a block's data, allocating it if it isn't backed yet
static uint8_t *lfs3_sparsebd_mutblock(const struct lfs3_cfg *cfg,
        lfs3_block_t block) {
    lfs3_sparsebd_t *bd = cfg->context;

    lfs3_sparsebd_block_t *b = lfs3_sparsebd_lookup(bd, block);
    if (b->data) {
        return b->data;
    }

    // keep the table at most half full
    if (2*(bd->count+1) > bd->capacity) {
        lfs3_sparsebd_block_t *table = bd->table;
        lfs3_block_t capacity = bd->capacity;
        bd->capacity = 2*capacity;
        bd->table = calloc(bd->capacity, sizeof(lfs3_sparsebd_block_t));
        if (!bd->table) {
            bd->table = table;
            bd->capacity = capacity;
            return NULL;
        }

        for (lfs3_block_t i = 0; i < capacity; i++) {
            if (table[i].data) {
                *lfs3_sparsebd_lookup(bd, table[i].block) = table[i];
            }
        }
        free(table);

        b = lfs3_sparsebd_lookup(bd, block);
    }

    // unwritten blocks read as erased
    uint8_t *data = malloc(cfg->block_size);
    if (!data) {
        return NULL;
    }
    memset(data,
            (bd->cfg->erase_value >= 0) ? bd->cfg->erase_value : 0,
            cfg->block_size);

    b->block = block;
    b->data = data;
    bd->count += 1;
    return data;
}

int lfs3_sparsebd_createcfg(const struct lfs3_cfg *cfg,
        const struct lfs3_sparsebd_cfg *bdcfg) {
    LFS3_SPARSEBD_TRACE("lfs3_sparsebd_createcfg(%p {.context=%p, "
                ".read=%p, .prog=%p, .erase=%p, .sync=%p, "
                ".read_size=%"PRIu32", .prog_size=%"PRIu32", "
                ".block_size=%"PRIu32", .block_count=%"PRIu32"}, "
                "%p {.erase_value=%"PRId32"})",
            (void*)cfg, cfg->context,
            (void*)(uintptr_t)cfg->read, (void*)(uintptr_t)cfg->prog,
            (void*)(uintptr_t)cfg->erase, (void*)(uintptr_t)cfg->sync,
            cfg->read_size, cfg->prog_size, cfg->block_size, cfg->block_count,
            (void*)bdcfg, bdcfg->erase_value);
    lfs3_sparsebd_t *bd = cfg->context;
    bd->cfg = bdcfg;
    bd->capacity = 64;
    bd->count = 0;
    bd->reads = 0;
    bd->progs = 0;
    bd->erases = 0;

    bd->table = calloc(bd->capacity, sizeof(lfs3_sparsebd_block_t));
    if (!bd->table) {
        LFS3_SPARSEBD_TRACE("lfs3_sparsebd_createcfg -> %d", LFS3_ERR_NOMEM);
        return LFS3_ERR_NOMEM;
    }

    LFS3_SPARSEBD_TRACE("lfs3_sparsebd_createcfg -> %d", 0);
    return 0;
}

int lfs3_sparsebd_create(const struct lfs3_cfg *cfg) {
    LFS3_SPARSEBD_TRACE("lfs3_sparsebd_create(%p {.context=%p, "
                ".read=%p, .prog=%p, .erase=%p, .sync=%p, "
                ".read_size=%"PRIu32", .prog_size=%"PRIu32", "
                ".block_size=%"PRIu32", .block_count=%"PRIu32"})",
            (void*)cfg, cfg->context,
            (void*)(uintptr_t)cfg->read, (void*)(uintptr_t)cfg->prog,
            (void*)(uintptr_t)cfg->erase, (void*)(uintptr_t)cfg->sync,
            cfg->read_size, cfg->prog_size, cfg->block_size, cfg->block_count);
    static const struct lfs3_sparsebd_cfg defaults = {.erase_value=-1};
    int err = lfs3_sparsebd_createcfg(cfg, &defaults);
    LFS3_SPARSEBD_TRACE("lfs3_sparsebd_create -> %d", err);
    return err;
}

int lfs3_sparsebd_destroy(const struct lfs3_cfg *cfg) {
    LFS3_SPARSEBD_TRACE("lfs3_sparsebd_destroy(%p)", (void*)cfg);
    // clean up memory
    lfs3_sparsebd_t *bd = cfg->context;
    for (lfs3_block_t i = 0; i < bd->capacity; i++) {
        free(bd->table[i].data);
    }
    free(bd->table);
    LFS3_SPARSEBD_TRACE("lfs3_sparsebd_destroy -> %d", 0);
    return 0;
}

int lfs3_sparsebd_read(const struct lfs3_cfg *cfg, lfs3_block_t block,
        lfs3_off_t off, void *buffer, lfs3_size_t size) {
    LFS3_SPARSEBD_TRACE("lfs3_sparsebd_read(%p, "
                "0x%"PRIx32", %"PRIu32", %p, %"PRIu32")",
            (void*)cfg, block, off, buffer, size);
    lfs3_sparsebd_t *bd = cfg->context;

    // check if read is valid
    LFS3_ASSERT(block < cfg->block_count);
    LFS3_ASSERT(off  % cfg->read_size == 0);
    LFS3_ASSERT(size % cfg->read_size == 0);
    LFS3_ASSERT(off+size <= cfg->block_size);

    // read data, unwritten blocks read as erased
    const lfs3_sparsebd_block_t *b = lfs3_sparsebd_lookup(bd, block);
    if (b->data) {
        memcpy(buffer, &b->data[off], size);
    } else {
        memset(buffer,
                (bd->cfg->erase_value >= 0) ? bd->cfg->erase_value : 0,
                size);
    }

    bd->reads += 1;
    LFS3_SPARSEBD_TRACE("lfs3_sparsebd_read -> %d", 0);
    return 0;
}

int lfs3_sparsebd_prog(const struct lfs3_cfg *cfg, lfs3_block_t block,
        lfs3_off_t off, const void *buffer, lfs3_size_t size) {
    LFS3_SPARSEBD_TRACE("lfs3_sparsebd_prog(%p, "
                "0x%"PRIx32", %"PRIu32", %p, %"PRIu32")",
            (void*)cfg, block, off, buffer, size);
    lfs3_sparsebd_t *bd = cfg->context;

    // check if write is valid
    LFS3_ASSERT(block < cfg->block_count);
    LFS3_ASSERT(off  % cfg->prog_size == 0);
    LFS3_ASSERT(size % cfg->prog_size == 0);
    LFS3_ASSERT(off+size <= cfg->block_size);

    uint8_t *data = lfs3_sparsebd_mutblock(cfg, block);
    if (!data) {
        LFS3_SPARSEBD_TRACE("lfs3_sparsebd_prog -> %d", LFS3_ERR_NOMEM);
        return LFS3_ERR_NOMEM;
    }

    // were we erased properly?
    if (bd->cfg->erase_value >= 0) {
        for (lfs3_off_t i = 0; i < size; i++) {
            LFS3_ASSERT(data[off+i] == bd->cfg->erase_value);
        }
    }

    // program data
    memcpy(&data[off], buffer, size);

    bd->progs += 1;
    LFS3_SPARSEBD_TRACE("lfs3_sparsebd_prog -> %d", 0);
    return 0;
}

int lfs3_sparsebd_erase(const struct lfs3_cfg *cfg, lfs3_block_t block) {
    LFS3_SPARSEBD_TRACE("lfs3_sparsebd_erase(%p, 0x%"PRIx32" (%"PRIu32"))",
            (void*)cfg, block, cfg->block_size);
    lfs3_sparsebd_t *bd = cfg->context;

    // check if erase is valid
    LFS3_ASSERT(block < cfg->block_count);

    // erase the block, noop erases leave it as is
    if (bd->cfg->erase_value >= 0) {
        uint8_t *data = lfs3_sparsebd_mutblock(cfg, block);
        if (!data) {
            LFS3_SPARSEBD_TRACE("lfs3_sparsebd_erase -> %d", LFS3_ERR_NOMEM);
            return LFS3_ERR_NOMEM;
        }

        memset(data, bd->cfg->erase_value, cfg->block_size);
    }

    bd->erases += 1;
    LFS3_SPARSEBD_TRACE("lfs3_sparsebd_erase -> %d", 0);
    return 0;
}

int lfs3_sparsebd_sync(const struct lfs3_cfg *cfg) {
    LFS3_SPARSEBD_TRACE("lfs3_sparsebd_sync(%p)", (void*)cfg);

    // sync is a noop
    (void)cfg;

    LFS3_SPARSEBD_TRACE("lfs3_sparsebd_sync -> %d", 0);
    return 0;
}

lfs3_block_t lfs3_sparsebd_used(const struct lfs3_cfg *cfg) {
    LFS3_SPARSEBD_TRACE("lfs3_sparsebd_used(%p)", (void*)cfg);
    lfs3_sparsebd_t *bd = cfg->context;
    LFS3_SPARSEBD_TRACE("lfs3_sparsebd_used -> %"PRIu32, bd->count);
    return bd->count;
}
