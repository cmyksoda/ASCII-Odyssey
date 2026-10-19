// SPDX-License-Identifier: GPL-3.0-only
#ifndef INPUT_H
#define INPUT_H

#include "../core/screen.h"

void input_init(void);
// Merges every Wii Remote, Classic Controller, GameCube pad and USB keyboard into one Input.
void input_poll(Input *in);
// The power or reset button was pressed: shut down soon, without waiting for the app.
int input_power_off(void);
void input_shutdown(void);

#endif
