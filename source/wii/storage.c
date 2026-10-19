// SPDX-License-Identifier: GPL-3.0-only
#include "storage.h"

#include <fat.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char root[48];

int storage_init(void) {
    if (!fatInitDefault()) return -1;
    static const char *const devices[] = {"sd:", "usb:"};
    for (unsigned i = 0; i < sizeof devices / sizeof devices[0]; i++) {
        char probe[32];
        snprintf(probe, sizeof probe, "%s/", devices[i]);
        struct stat st;
        if (stat(probe, &st) != 0) continue;
        snprintf(root, sizeof root, "%s/apps", devices[i]);
        mkdir(root, 0777);
        snprintf(root, sizeof root, "%s/apps/asciiodyssey", devices[i]);
        mkdir(root, 0777);
        return 0;
    }
    return -1;
}

int storage_read(const char *name, char *out, int size) {
    if (!root[0]) return -1;
    char path[96];
    snprintf(path, sizeof path, "%s/%s", root, name);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int n = (int)fread(out, 1, (size_t)(size - 1), f);
    fclose(f);
    out[n < 0 ? 0 : n] = 0;
    return n;
}

int storage_write(const char *name, const char *text) {
    if (!root[0]) return -1;
    char path[96], temp[104];
    snprintf(path, sizeof path, "%s/%s", root, name);
    snprintf(temp, sizeof temp, "%s.tmp", path);
    FILE *f = fopen(temp, "w");
    if (!f) return -1;
    size_t n = strlen(text);
    int ok = fwrite(text, 1, n, f) == n;
    ok &= fclose(f) == 0;
    if (!ok) {
        remove(temp);
        return -1;
    }
    // libfat won't rename onto an existing file, so the old one steps aside first.
    remove(path);
    return rename(temp, path) == 0 ? 0 : -1;
}
