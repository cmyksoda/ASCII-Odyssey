// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef STORAGE_H
#define STORAGE_H

// Small text files in sd:/apps/asciiodyssey/ (or usb:/ when there is no card).
int storage_init(void);
int storage_read(const char *name, char *out, int size);
// Writes through a temporary file, so a power cut never leaves half a file behind.
int storage_write(const char *name, const char *text);

#endif
