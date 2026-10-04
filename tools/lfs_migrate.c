/*
 * lfs_migrate.c - copy every file and folder of one LittleFS image into a
 * new image of another size, as used when the ESP32's /fs partition moves
 * (for example to make room for OTA slots). Then it reads the new image
 * back and compares every file with the old one.
 *
 *   lfs_migrate <old.img> <new.img> <new-size> [--add <host-file> <fs-path>]...
 *
 * The LittleFS settings match the ESP-IDF build (joltwallet/littlefs with
 * this project's sdkconfig): 4096-byte blocks, 128-byte reads and writes,
 * 512-byte cache, 128-byte lookahead, names up to 255 bytes (LittleFS default), and the 't'
 * (modification time) attribute on every file and folder.
 *
 * Build (the LittleFS sources come with the ESP-IDF component):
 *   gcc -O2 -DLFS_NO_DEBUG -I<littlefs> tools/lfs_migrate.c
 *       <littlefs>/lfs.c <littlefs>/lfs_util.c -o lfs_migrate
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lfs.h"

#define BLOCK      4096
#define ATTR_MTIME 't'

typedef struct
{
    uint8_t *data;
    size_t size;
} image_t;

static int bd_read(const struct lfs_config *c, lfs_block_t b, lfs_off_t off, void *buf, lfs_size_t size)
{
    image_t *im = c->context;
    memcpy(buf, im->data + (size_t)b * c->block_size + off, size);
    return 0;
}

static int bd_prog(const struct lfs_config *c, lfs_block_t b, lfs_off_t off, const void *buf, lfs_size_t size)
{
    image_t *im = c->context;
    memcpy(im->data + (size_t)b * c->block_size + off, buf, size);
    return 0;
}

static int bd_erase(const struct lfs_config *c, lfs_block_t b)
{
    image_t *im = c->context;
    memset(im->data + (size_t)b * c->block_size, 0xFF, c->block_size);
    return 0;
}

static int bd_sync(const struct lfs_config *c)
{
    (void)c;
    return 0;
}

static void setup(struct lfs_config *cfg, image_t *im)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->context = im;
    cfg->read = bd_read;
    cfg->prog = bd_prog;
    cfg->erase = bd_erase;
    cfg->sync = bd_sync;
    cfg->read_size = 128;
    cfg->prog_size = 128;
    cfg->block_size = BLOCK;
    cfg->block_count = (lfs_size_t)(im->size / BLOCK);
    cfg->cache_size = 512;
    cfg->lookahead_size = 128;
    cfg->block_cycles = 512;
}

static uint8_t *read_host(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc(n > 0 ? (size_t)n : 1);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n)
    {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    *len = (size_t)n;
    return buf;
}

static long s_files, s_dirs, s_bytes;

/* Read a whole file from an image. */
static uint8_t *read_lfs(lfs_t *fs, const char *path, lfs_size_t size)
{
    lfs_file_t f;
    if (lfs_file_open(fs, &f, path, LFS_O_RDONLY) < 0)
        return NULL;
    uint8_t *buf = malloc(size ? size : 1);
    lfs_ssize_t n = buf ? lfs_file_read(fs, &f, buf, size) : -1;
    lfs_file_close(fs, &f);
    if (n != (lfs_ssize_t)size)
    {
        free(buf);
        return NULL;
    }
    return buf;
}

static int write_lfs(lfs_t *fs, const char *path, const uint8_t *data, size_t size)
{
    lfs_file_t f;
    int rc = lfs_file_open(fs, &f, path, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
    if (rc < 0)
        return rc;
    lfs_ssize_t n = size ? lfs_file_write(fs, &f, data, (lfs_size_t)size) : 0;
    rc = lfs_file_close(fs, &f);
    return n == (lfs_ssize_t)size ? rc : -1;
}

static void copy_attr(lfs_t *from, lfs_t *to, const char *path)
{
    uint8_t attr[16];
    lfs_ssize_t n = lfs_getattr(from, path, ATTR_MTIME, attr, sizeof(attr));
    if (n > 0)
        lfs_setattr(to, path, ATTR_MTIME, attr, (lfs_size_t)n);
}

/* Copy folder `dir` (and below) from one image to the other. */
static int copy_tree(lfs_t *from, lfs_t *to, const char *dir)
{
    lfs_dir_t d;
    struct lfs_info info;
    if (lfs_dir_open(from, &d, dir) < 0)
    {
        fprintf(stderr, "cannot open folder %s\n", dir);
        return -1;
    }
    int rc = 0;
    while (rc == 0 && lfs_dir_read(from, &d, &info) > 0)
    {
        if (!strcmp(info.name, ".") || !strcmp(info.name, ".."))
            continue;
        char path[512];
        snprintf(path, sizeof(path), "%s%s%s", dir, strcmp(dir, "/") ? "/" : "", info.name);
        if (info.type == LFS_TYPE_DIR)
        {
            if (lfs_mkdir(to, path) < 0)
            {
                fprintf(stderr, "mkdir %s failed\n", path);
                rc = -1;
                break;
            }
            copy_attr(from, to, path);
            s_dirs++;
            printf("  d          %s\n", path);
            rc = copy_tree(from, to, path);
        }
        else
        {
            uint8_t *buf = read_lfs(from, path, info.size);
            if (!buf || write_lfs(to, path, buf, info.size) < 0)
            {
                fprintf(stderr, "copy %s failed (%s)\n", path, buf ? "write" : "read");
                free(buf);
                rc = -1;
                break;
            }
            free(buf);
            copy_attr(from, to, path);
            s_files++;
            s_bytes += (long)info.size;
            printf("  f %8lu %s\n", (unsigned long)info.size, path);
        }
    }
    lfs_dir_close(from, &d);
    return rc;
}

/* Every file of `a` must be in `b` with the same bytes. */
static int compare_tree(lfs_t *a, lfs_t *b, const char *dir, long *checked)
{
    lfs_dir_t d;
    struct lfs_info info;
    if (lfs_dir_open(a, &d, dir) < 0)
        return -1;
    int rc = 0;
    while (rc == 0 && lfs_dir_read(a, &d, &info) > 0)
    {
        if (!strcmp(info.name, ".") || !strcmp(info.name, ".."))
            continue;
        char path[512];
        snprintf(path, sizeof(path), "%s%s%s", dir, strcmp(dir, "/") ? "/" : "", info.name);
        struct lfs_info other;
        if (lfs_stat(b, path, &other) < 0 || other.type != info.type)
        {
            fprintf(stderr, "missing in the new image: %s\n", path);
            rc = -1;
        }
        else if (info.type == LFS_TYPE_DIR)
        {
            rc = compare_tree(a, b, path, checked);
        }
        else
        {
            uint8_t *x = read_lfs(a, path, info.size), *y = read_lfs(b, path, other.size);
            if (!x || !y || other.size != info.size || memcmp(x, y, info.size) != 0)
            {
                fprintf(stderr, "different in the new image: %s\n", path);
                rc = -1;
            }
            free(x);
            free(y);
            (*checked)++;
        }
    }
    lfs_dir_close(a, &d);
    return rc;
}

static int mkdirs(lfs_t *fs, const char *path)
{
    char p[512];
    snprintf(p, sizeof(p), "%s", path);
    for (char *s = p + 1; *s; s++)
    {
        if (*s != '/')
            continue;
        *s = '\0';
        int rc = lfs_mkdir(fs, p);
        if (rc < 0 && rc != LFS_ERR_EXIST)
            return rc;
        *s = '/';
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 4)
    {
        fprintf(stderr, "usage: lfs_migrate <old.img> <new.img> <new-size> [--add <host-file> <fs-path>]...\n");
        return 2;
    }
    image_t old_im, new_im;
    old_im.data = read_host(argv[1], &old_im.size);
    if (!old_im.data || old_im.size % BLOCK)
    {
        fprintf(stderr, "cannot read %s (or its size is not a multiple of %d)\n", argv[1], BLOCK);
        return 1;
    }
    new_im.size = (size_t)strtoul(argv[3], NULL, 0);
    if (!new_im.size || new_im.size % BLOCK)
    {
        fprintf(stderr, "bad new size\n");
        return 1;
    }
    new_im.data = malloc(new_im.size);
    memset(new_im.data, 0xFF, new_im.size);

    struct lfs_config oc, nc;
    lfs_t ofs, nfs;
    setup(&oc, &old_im);
    setup(&nc, &new_im);
    if (lfs_mount(&ofs, &oc) < 0)
    {
        fprintf(stderr, "the old image does not mount as LittleFS\n");
        return 1;
    }
    printf("old image: %lu blocks, %ld used\n", (unsigned long)oc.block_count, (long)lfs_fs_size(&ofs));
    if (lfs_format(&nfs, &nc) < 0 || lfs_mount(&nfs, &nc) < 0)
    {
        fprintf(stderr, "cannot create the new image\n");
        return 1;
    }
    copy_attr(&ofs, &nfs, "/");
    if (copy_tree(&ofs, &nfs, "/") < 0)
        return 1;
    printf("copied %ld files (%ld bytes) and %ld folders\n", s_files, s_bytes, s_dirs);

    for (int i = 4; i + 2 < argc + 0 && i < argc; i++)
    {
        if (strcmp(argv[i], "--add") || i + 2 >= argc)
            continue;
        size_t len;
        uint8_t *buf = read_host(argv[i + 1], &len);
        if (!buf || mkdirs(&nfs, argv[i + 2]) < 0 || write_lfs(&nfs, argv[i + 2], buf, len) < 0)
        {
            fprintf(stderr, "cannot add %s as %s\n", argv[i + 1], argv[i + 2]);
            return 1;
        }
        printf("added %s (%lu bytes)\n", argv[i + 2], (unsigned long)len);
        free(buf);
        i += 2;
    }
    printf("new image: %lu blocks, %ld used\n", (unsigned long)nc.block_count, (long)lfs_fs_size(&nfs));
    lfs_unmount(&nfs);

    /* Read it back from scratch and compare. */
    lfs_t check;
    if (lfs_mount(&check, &nc) < 0)
    {
        fprintf(stderr, "the new image does not mount\n");
        return 1;
    }
    long checked = 0;
    if (compare_tree(&ofs, &check, "/", &checked) < 0)
        return 1;
    printf("verified: all %ld files match\n", checked);
    lfs_unmount(&check);
    lfs_unmount(&ofs);

    FILE *out = fopen(argv[2], "wb");
    if (!out || fwrite(new_im.data, 1, new_im.size, out) != new_im.size || fclose(out) != 0)
    {
        fprintf(stderr, "cannot write %s\n", argv[2]);
        return 1;
    }
    printf("wrote %s\n", argv[2]);
    return 0;
}
