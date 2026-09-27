/*
 * td_fs_stdio.c - td_fs_ops_t on top of the C library and <dirent.h>.
 * Used by the desktop hosts and by the ESP-IDF port (its VFS provides the
 * same calls for LittleFS).
 */
#include "td_fs_stdio.h"
#include "tinydesk/td_config.h"   /* TD_PATH_MAX */

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef _WIN32
#include <direct.h>
#define make_dir(path) _mkdir(path)
#else
#define make_dir(path) mkdir((path), 0755)
#endif

#define MAX_DEPTH 8          /* recursive delete depth limit */

static td_fs_ops_t s_ops;
static char s_root[TD_PATH_MAX];

static int fs_list(const char *dir,
                   void (*fn)(const char *name, bool is_dir, uint32_t size, void *user),
                   void *user)
{
    DIR *d = opendir(dir);
    if (!d) return -1;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        char path[TD_PATH_MAX + 64];
        int w = snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        struct stat st;
        bool is_dir = false;
        uint32_t size = 0;
        /* A path that does not fit is listed without a size, never stat()ed cut. */
        if (w > 0 && w < (int)sizeof(path) && stat(path, &st) == 0) {
            is_dir = S_ISDIR(st.st_mode);
            size = (uint32_t)st.st_size;
        }
        fn(e->d_name, is_dir, size, user);
        n++;
    }
    closedir(d);
    return n;
}

static int fs_read(const char *path, char *buf, int cap)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    int n = (int)fread(buf, 1, (size_t)cap, f);
    fclose(f);
    return n;
}

/* Delete a directory's contents, then the directory. */
static int remove_tree(const char *path, int depth)
{
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    if (!S_ISDIR(st.st_mode)) return remove(path);
    if (depth >= MAX_DEPTH) return -1;

    DIR *d = opendir(path);
    if (!d) return -1;
    int rc = 0;
    struct dirent *e;
    while (rc == 0 && (e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        char child[TD_PATH_MAX + 64];
        int w = snprintf(child, sizeof(child), "%s/%s", path, e->d_name);
        if (w < 0 || w >= (int)sizeof(child)) {   /* never delete a cut-off path */
            rc = -1;
            break;
        }
        rc = remove_tree(child, depth + 1);
    }
    closedir(d);
    return rc == 0 ? rmdir(path) : rc;
}

static int fs_remove(const char *path) { return remove_tree(path, 0); }

static int fs_mkdir(const char *path) { return make_dir(path); }

static int fs_write(const char *path, const char *data, int len)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    bool ok = len == 0 || fwrite(data, 1, (size_t)len, f) == (size_t)len;
    if (fclose(f) != 0) ok = false;
    return ok ? 0 : -1;
}

static int fs_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static int fs_rename(const char *from, const char *to)
{
    if (fs_exists(to)) return -1;   /* never overwrite by renaming */
    return rename(from, to);
}

const td_fs_ops_t *td_fs_stdio(const char *root)
{
    snprintf(s_root, sizeof(s_root), "%s", root);
    s_ops.root = s_root;
    s_ops.list = fs_list;
    s_ops.read = fs_read;
    s_ops.remove = fs_remove;
    s_ops.mkdir = fs_mkdir;
    s_ops.write = fs_write;
    s_ops.rename = fs_rename;
    s_ops.exists = fs_exists;
    return &s_ops;
}
