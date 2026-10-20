// SPDX-License-Identifier: GPL-3.0-only
#ifndef BEACON_H
#define BEACON_H

// Test runs on real hardware: with a beacon=ip:port argument from wiiload, logs, timings and screenshots
// stream to tools/beacon.py on the PC, so a console can be checked with nobody watching its TV.
int beacon_start(int argc, char **argv);
void beacon_log(const char *format, ...) __attribute__((format(printf, 1, 2)));
// The framebuffer exactly as the TV gets it: 640 wide, YUYV.
void beacon_shot(const char *name, const void *xfb, int width, int height);
const char *beacon_tour(void);
void beacon_stop(void);

#endif
