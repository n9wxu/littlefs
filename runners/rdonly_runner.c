/*
 * Runner for read-only image checks
 *
 * The test runner sets the write fields of struct lfs3_cfg, so it can't
 * be built with LFS3_RDONLY. This program instead mounts disk images
 * written by the test runner (test.py -d) and compares everything it
 * reads with a manifest written next to each image, <image>.manifest.
 *
 * Steps:
 *
 *   # write images with a read-write build (B-DEF, and B-YGB with
 *   # LFS3_YES_GBMAP=1), one case per image, in the default geometry
 *   make BUILDDIR=.local/b .local/b/runners/test_runner \
 *           .local/b/runners/rdonly_runner
 *   rm -f .local/img/files.disk
 *   ./scripts/test.py -R .local/b/runners/test_runner -Pnone \
 *           -d .local/img/files.disk test_files_many
 *
 *   # write the manifest with the same build, through its read paths
 *   .local/b/runners/rdonly_runner -m .local/img/files.disk
 *
 *   # check the images with LFS3_RDONLY builds (B-RO, B-YES-RDONLY)
 *   make BUILDDIR=.local/ro .local/ro/runners/rdonly_runner LFS3_RDONLY=1
 *   .local/ro/runners/rdonly_runner .local/img/files.disk
 *
 * An image holds the state the case's last permutation left, so pick
 * cases whose last permutation leaves files behind, and start from a
 * missing image file, since emubd doesn't truncate it. -b and -r give
 * the block and read size of images not in the default geometry.
 *
 * Each image is mounted with LFS3_M_RDONLY, and in LFS3_RDONLY builds
 * also with no flags, into an lfs3_t filled with 0x00 and with 0xff.
 * Each mount must succeed, report LFS3_I_RDONLY, pass lfs3_fs_ck, never
 * prog, erase or sync, and read back exactly the manifest.
 *
 * The manifest is text, one item per line, in the order lfs3_dir_read
 * returns entries, with bytes outside 0x21-0x7e and '%' in paths escaped
 * as %xx:
 *
 *   lfs3-manifest 1
 *   geometry read_size=1 block_size=4096 block_count=256
 *   fs name_limit=255 file_limit=2147483647 cksum=0x1b2e3f40
 *   dir /
 *   dir /a
 *   attr /a type=0x41 size=3 crc32c=0x8a9136aa
 *   reg /a/b size=12 crc32c=0x2f3a4c59
 *   end 3
 *
 * Copyright (c) 2022, The littlefs authors.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "lfs3.h"
#include "lfs3_util.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


// growable text buffer
typedef struct text {
    char *data;
    size_t size;
    size_t cap;
} text_t;

static void text_printf(text_t *t, const char *fmt, ...) {
    if (!t->data) {
        t->cap = 256;
        t->data = malloc(t->cap);
        if (!t->data) {
            fprintf(stderr, "error: out of memory\n");
            exit(-1);
        }
    }

    while (true) {
        va_list va;
        va_start(va, fmt);
        int n = vsnprintf(t->data + t->size, t->cap - t->size, fmt, va);
        va_end(va);
        if (n < 0) {
            fprintf(stderr, "error: vsnprintf failed\n");
            exit(-1);
        }

        if (t->size + n < t->cap) {
            t->size += n;
            return;
        }

        while (t->cap <= t->size + n) {
            t->cap *= 2;
        }
        t->data = realloc(t->data, t->cap);
        if (!t->data) {
            fprintf(stderr, "error: out of memory\n");
            exit(-1);
        }
    }
}

static void text_path(text_t *t, const char *path) {
    for (const char *p = path; *p; p++) {
        uint8_t c = *p;
        if (c <= 0x20 || c >= 0x7f || c == '%') {
            text_printf(t, "%%%02x", c);
        } else {
            text_printf(t, "%c", c);
        }
    }
}


// a block device over an image in RAM, refusing writes
typedef struct image {
    uint8_t *data;
    size_t size;
    lfs3_size_t writes;
} image_t;

static int image_read(const struct lfs3_cfg *cfg, lfs3_block_t block,
        lfs3_off_t off, void *buffer, lfs3_size_t size) {
    image_t *img = cfg->context;
    size_t addr = (size_t)block*cfg->block_size + off;
    if (block >= cfg->block_count
            || off + size > cfg->block_size
            || addr + size > img->size) {
        return LFS3_ERR_IO;
    }

    memcpy(buffer, &img->data[addr], size);
    return 0;
}

#ifndef LFS3_RDONLY
static int image_prog(const struct lfs3_cfg *cfg, lfs3_block_t block,
        lfs3_off_t off, const void *buffer, lfs3_size_t size) {
    (void)block;
    (void)off;
    (void)buffer;
    (void)size;
    image_t *img = cfg->context;
    img->writes += 1;
    return LFS3_ERR_IO;
}

static int image_erase(const struct lfs3_cfg *cfg, lfs3_block_t block) {
    (void)block;
    image_t *img = cfg->context;
    img->writes += 1;
    return LFS3_ERR_IO;
}

static int image_sync(const struct lfs3_cfg *cfg) {
    image_t *img = cfg->context;
    img->writes += 1;
    return LFS3_ERR_IO;
}
#endif

static void *slurp(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }

    uint8_t *data = NULL;
    size_t size_ = 0;
    size_t cap = 0;
    while (true) {
        if (size_ == cap) {
            cap = (cap < 4096) ? 4096 : 2*cap;
            data = realloc(data, cap+1);
            if (!data) {
                fprintf(stderr, "error: out of memory\n");
                exit(-1);
            }
        }

        size_t n = fread(&data[size_], 1, cap - size_, f);
        size_ += n;
        if (n == 0) {
            break;
        }
    }
    fclose(f);

    // terminate for text
    data[size_] = '\0';
    *size = size_;
    return data;
}


// walk the filesystem, writing every item to the text

static size_t walk_items = 0;

static void walk_attrs(lfs3_t *lfs3, text_t *t, const char *path) {
    for (unsigned type = 0; type < 256; type++) {
        lfs3_ssize_t size = lfs3_sizeattr(lfs3, path, type);
        if (size == LFS3_ERR_NOATTR) {
            continue;
        }
        if (size < 0) {
            text_printf(t, "error ");
            text_path(t, path);
            text_printf(t, " sizeattr type=0x%02x err=%"PRId32"\n",
                    type, size);
            return;
        }

        uint8_t *buffer = malloc((size) ? size : 1);
        lfs3_ssize_t res = lfs3_getattr(lfs3, path, type, buffer, size);
        if (res != size) {
            text_printf(t, "error ");
            text_path(t, path);
            text_printf(t, " getattr type=0x%02x err=%"PRId32"\n",
                    type, res);
            free(buffer);
            return;
        }

        text_printf(t, "attr ");
        text_path(t, path);
        text_printf(t, " type=0x%02x size=%"PRId32" crc32c=0x%08"PRIx32"\n",
                type, size, lfs3_crc32c(0, buffer, size));
        walk_items += 1;
        free(buffer);
    }
}

static void walk_reg(lfs3_t *lfs3, text_t *t, const char *path,
        lfs3_size_t size) {
    lfs3_file_t file;
    int err = lfs3_file_open(lfs3, &file, path, LFS3_O_RDONLY);
    if (err) {
        text_printf(t, "error ");
        text_path(t, path);
        text_printf(t, " file_open err=%d\n", err);
        return;
    }

    lfs3_soff_t fsize = lfs3_file_size(lfs3, &file);
    uint32_t crc = 0;
    lfs3_size_t total = 0;
    while (true) {
        uint8_t buffer[512];
        lfs3_ssize_t res = lfs3_file_read(lfs3, &file,
                buffer, sizeof(buffer));
        if (res < 0) {
            text_printf(t, "error ");
            text_path(t, path);
            text_printf(t, " file_read off=%"PRIu32" err=%"PRId32"\n",
                    total, res);
            lfs3_file_close(lfs3, &file);
            return;
        }
        if (res == 0) {
            break;
        }

        crc = lfs3_crc32c(crc, buffer, res);
        total += res;
    }

    err = lfs3_file_close(lfs3, &file);
    if (err || fsize != (lfs3_soff_t)total || size != total) {
        text_printf(t, "error ");
        text_path(t, path);
        text_printf(t, " read=%"PRIu32" file_size=%"PRId32" "
                    "dir_read=%"PRIu32" file_close=%d\n",
                total, fsize, size, err);
        return;
    }

    text_printf(t, "reg ");
    text_path(t, path);
    text_printf(t, " size=%"PRIu32" crc32c=0x%08"PRIx32"\n", total, crc);
    walk_items += 1;
    walk_attrs(lfs3, t, path);
}

static void walk_dir(lfs3_t *lfs3, text_t *t, const char *path) {
    text_printf(t, "dir ");
    text_path(t, path);
    text_printf(t, "\n");
    walk_items += 1;
    walk_attrs(lfs3, t, path);

    // read the entries first, so only one dir is open at a time
    lfs3_dir_t dir;
    int err = lfs3_dir_open(lfs3, &dir, path);
    if (err) {
        text_printf(t, "error ");
        text_path(t, path);
        text_printf(t, " dir_open err=%d\n", err);
        return;
    }

    struct lfs3_info *infos = NULL;
    size_t count = 0;
    while (true) {
        struct lfs3_info info;
        err = lfs3_dir_read(lfs3, &dir, &info);
        if (err == LFS3_ERR_NOENT) {
            break;
        }
        if (err) {
            text_printf(t, "error ");
            text_path(t, path);
            text_printf(t, " dir_read err=%d\n", err);
            break;
        }

        if (strcmp(info.name, ".") == 0 || strcmp(info.name, "..") == 0) {
            continue;
        }

        infos = realloc(infos, (count+1)*sizeof(struct lfs3_info));
        if (!infos) {
            fprintf(stderr, "error: out of memory\n");
            exit(-1);
        }
        infos[count] = info;
        count += 1;
    }

    err = lfs3_dir_close(lfs3, &dir);
    if (err) {
        text_printf(t, "error ");
        text_path(t, path);
        text_printf(t, " dir_close err=%d\n", err);
    }

    for (size_t i = 0; i < count; i++) {
        size_t len = strlen(path);
        char *child = malloc(len + 1 + strlen(infos[i].name) + 1);
        sprintf(child, "%s%s%s",
                path,
                (len > 0 && path[len-1] == '/') ? "" : "/",
                infos[i].name);

        // stat should agree with dir_read
        struct lfs3_info info;
        err = lfs3_stat(lfs3, child, &info);
        if (err
                || info.type != infos[i].type
                || (info.type == LFS3_TYPE_REG
                    && info.size != infos[i].size)) {
            text_printf(t, "error ");
            text_path(t, child);
            text_printf(t, " stat err=%d type=%u size=%"PRIu32"\n",
                    err, info.type, info.size);

        } else if (infos[i].type == LFS3_TYPE_DIR) {
            walk_dir(lfs3, t, child);

        } else if (infos[i].type == LFS3_TYPE_REG) {
            walk_reg(lfs3, t, child, infos[i].size);

        } else {
            text_printf(t, "other ");
            text_path(t, child);
            text_printf(t, " type=%u\n", infos[i].type);
            walk_items += 1;
        }

        free(child);
    }

    free(infos);
}


// mount an image and write what we find to the text, returns 0 if the
// mount itself behaved
static int walk(const char *name, image_t *img, struct lfs3_cfg *cfg,
        uint8_t fill, uint32_t flags, text_t *t) {
    lfs3_t lfs3;
    memset(&lfs3, fill, sizeof(lfs3));
    img->writes = 0;
    walk_items = 0;

    text_printf(t, "lfs3-manifest 1\n");
    text_printf(t, "geometry read_size=%"PRIu32" block_size=%"PRIu32" "
                "block_count=%"PRIu32"\n",
            cfg->read_size, cfg->block_size, cfg->block_count);

    int err = lfs3_mount(&lfs3, flags, cfg);
    if (err) {
        printf("%s: fill 0x%02x flags 0x%"PRIx32": mount failed: %d\n",
                name, fill, flags, err);
        return -1;
    }

    int failed = 0;
    struct lfs3_fsinfo fsinfo = {0};
    err = lfs3_fs_stat(&lfs3, &fsinfo);
    if (err) {
        printf("%s: fill 0x%02x flags 0x%"PRIx32": fs_stat failed: %d\n",
                name, fill, flags, err);
        failed = -1;
    } else if (!(fsinfo.flags & LFS3_I_RDONLY)) {
        printf("%s: fill 0x%02x flags 0x%"PRIx32": "
                    "LFS3_I_RDONLY not set, flags 0x%08"PRIx32"\n",
                name, fill, flags, fsinfo.flags);
        failed = -1;
    }

    uint32_t cksum = 0;
    err = lfs3_fs_cksum(&lfs3, &cksum);
    if (err) {
        text_printf(t, "error fs_cksum err=%d\n", err);
    }
    text_printf(t, "fs name_limit=%"PRIu32" file_limit=%"PRIu32" "
                "cksum=0x%08"PRIx32"\n",
            fsinfo.name_limit, fsinfo.file_limit, cksum);

    walk_dir(&lfs3, t, "/");
    text_printf(t, "end %zu\n", walk_items);

    err = lfs3_fs_ck(&lfs3, LFS3_CK_CKMETA | LFS3_CK_CKDATA);
    if (err) {
        printf("%s: fill 0x%02x flags 0x%"PRIx32": fs_ck failed: %d\n",
                name, fill, flags, err);
        failed = -1;
    }

    err = lfs3_unmount(&lfs3);
    if (err) {
        printf("%s: fill 0x%02x flags 0x%"PRIx32": unmount failed: %d\n",
                name, fill, flags, err);
        failed = -1;
    }

    if (img->writes) {
        printf("%s: fill 0x%02x flags 0x%"PRIx32": "
                    "%"PRIu32" progs, erases or syncs\n",
                name, fill, flags, img->writes);
        failed = -1;
    }

    return failed;
}

// compare a walk with the manifest, reporting the first differing line
static int compare(const char *name, uint8_t fill, uint32_t flags,
        const char *manifest, const char *found) {
    size_t lineno = 1;
    while (true) {
        size_t mlen = strcspn(manifest, "\n");
        size_t flen = strcspn(found, "\n");
        if (mlen != flen || memcmp(manifest, found, mlen) != 0
                || (!manifest[mlen]) != (!found[flen])) {
            printf("%s.manifest:%zu: fill 0x%02x flags 0x%"PRIx32": "
                        "expected \"%.*s\", found \"%.*s\"\n",
                    name, lineno, fill, flags,
                    (int)mlen, manifest, (int)flen, found);
            return -1;
        }

        if (!manifest[mlen]) {
            return 0;
        }
        manifest += mlen+1;
        found += flen+1;
        lineno += 1;
    }
}

static int parse_geometry(const char *manifest, struct lfs3_cfg *cfg) {
    const char *geometry = strstr(manifest, "\ngeometry ");
    if (!geometry
            || strncmp(manifest, "lfs3-manifest 1\n",
                strlen("lfs3-manifest 1\n")) != 0) {
        return -1;
    }

    unsigned long read_size, block_size, block_count;
    if (sscanf(geometry, "\ngeometry read_size=%lu block_size=%lu "
                "block_count=%lu",
                &read_size, &block_size, &block_count) != 3) {
        return -1;
    }

    cfg->read_size = read_size;
    cfg->block_size = block_size;
    cfg->block_count = block_count;
    return 0;
}

static void usage(const char *argv0) {
    printf("usage: %s [-b block_size] [-r read_size] -m image\n", argv0);
    printf("       %s image...\n", argv0);
    printf("\n");
    printf("  -m  Write image.manifest from the image.\n");
    printf("  -b  Block size of the image, defaults to 4096.\n");
    printf("  -r  Read size of the image, defaults to 1.\n");
    printf("\n");
    printf("Without -m, check each image against image.manifest.\n");
}

int main(int argc, char **argv) {
    bool write_manifest = false;
    unsigned long block_size = 4096;
    unsigned long read_size = 1;
    const char **images = malloc(argc * sizeof(const char*));
    size_t image_count = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-m") == 0) {
            write_manifest = true;
        } else if (strcmp(argv[i], "-b") == 0 && i+1 < argc) {
            block_size = strtoul(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "-r") == 0 && i+1 < argc) {
            read_size = strtoul(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "-h") == 0 || argv[i][0] == '-') {
            usage(argv[0]);
            return (strcmp(argv[i], "-h") == 0) ? 0 : 1;
        } else {
            images[image_count++] = argv[i];
        }
    }
    if (!image_count || (write_manifest && image_count != 1)) {
        usage(argv[0]);
        return 1;
    }

    int failed = 0;
    for (size_t i = 0; i < image_count; i++) {
        const char *name = images[i];
        image_t img = {.data=NULL, .size=0, .writes=0};
        img.data = slurp(name, &img.size);
        if (!img.data) {
            printf("%s: could not read image\n", name);
            failed = 1;
            continue;
        }

        char *mname = malloc(strlen(name) + strlen(".manifest") + 1);
        sprintf(mname, "%s.manifest", name);

        struct lfs3_cfg cfg = {
            .context            = &img,
            .read               = image_read,
            #ifndef LFS3_RDONLY
            .prog               = image_prog,
            .erase              = image_erase,
            .sync               = image_sync,
            #endif
        };

        char *manifest = NULL;
        if (write_manifest) {
            cfg.read_size = read_size;
            cfg.block_size = block_size;
            cfg.block_count = img.size / block_size;
        } else {
            size_t msize;
            manifest = slurp(mname, &msize);
            if (!manifest || parse_geometry(manifest, &cfg)) {
                printf("%s: could not read %s\n", name, mname);
                failed = 1;
                free(manifest);
                free(mname);
                free(img.data);
                continue;
            }
        }

        // caches and write-side limits as the test runner sets them,
        // the write side only has to pass lfs3_init's checks
        cfg.rcache_size = lfs3_max(16, cfg.read_size);
        cfg.fcache_size = 16;
        #ifndef LFS3_RDONLY
        cfg.prog_size = cfg.read_size;
        cfg.block_recycles = -1;
        cfg.pcache_size = lfs3_max(16, cfg.prog_size);
        cfg.lookahead_size = 16;
        cfg.gc_lookahead_thresh = -1;
        cfg.shrub_size = cfg.block_size/4;
        cfg.fragment_size = lfs3_min(cfg.block_size/16, 512);
        cfg.crystal_thresh = cfg.block_size/16;
        #endif
        #if !defined(LFS3_RDONLY) && defined(LFS3_GBMAP)
        cfg.gc_lookgbmap_thresh = -1;
        #endif
        #if !defined(LFS3_RDONLY) && defined(LFS3_PREERASE)
        cfg.gc_preerase_count = -1;
        #endif
        #ifdef LFS3_GBMAP
        cfg.lookgbmap_thresh = cfg.block_count/4;
        #endif

        if (write_manifest) {
            text_t t = {NULL, 0, 0};
            int err = walk(name, &img, &cfg, 0x00, LFS3_M_RDONLY, &t);
            if (err || strstr(t.data, "\nerror ")) {
                printf("%s: not writing %s:\n%s", name, mname, t.data);
                failed = 1;
            } else {
                FILE *f = fopen(mname, "w");
                if (!f || fwrite(t.data, 1, t.size, f) != t.size
                        || fclose(f) != 0) {
                    printf("%s: could not write %s\n", name, mname);
                    failed = 1;
                } else {
                    printf("%s: wrote %s, %zu items\n",
                            name, mname, walk_items);
                }
            }
            free(t.data);

        } else {
            // mount into zeroed and 0xff-filled lfs3_ts, and in an
            // LFS3_RDONLY build also with no flags, which must still
            // mount read-only
            static const uint8_t fills[] = {0x00, 0xff};
            static const uint32_t flagss[] = {
                LFS3_M_RDONLY,
                #ifdef LFS3_RDONLY
                0,
                #endif
            };
            for (size_t j = 0; j < sizeof(flagss)/sizeof(flagss[0]); j++) {
                for (size_t k = 0; k < sizeof(fills); k++) {
                    text_t t = {NULL, 0, 0};
                    int err = walk(name, &img, &cfg,
                            fills[k], flagss[j], &t);
                    if (err) {
                        failed = 1;
                    }
                    if (t.data) {
                        err = compare(name, fills[k], flagss[j],
                                manifest, t.data);
                        if (err) {
                            failed = 1;
                        } else {
                            printf("%s: fill 0x%02x flags 0x%"PRIx32": "
                                        "%zu items match\n",
                                    name, fills[k], flagss[j],
                                    walk_items);
                        }
                    }
                    free(t.data);
                }
            }
        }

        free(manifest);
        free(mname);
        free(img.data);
    }

    free(images);
    return failed;
}
