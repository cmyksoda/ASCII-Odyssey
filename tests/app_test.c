// SPDX-License-Identifier: GPL-3.0-or-later
// The app's first screens answer the first press the way a player expects.
#include "check.h"
#include "core/app.h"

#include <stdio.h>

static AppRequest press(App *app, unsigned buttons, double wall) {
    Input in = {0};
    in.pressed = buttons;
    AppClock clock = {.wall = wall, .music = -1};
    Screen screen = {0};
    AppRequest req;
    app_frame(app, &clock, &in, &screen, &req);
    return req;
}

static void start_sails_at_once(void) {
    AppConfig config = {0};
    settings_defaults(&config.settings);
    App *app = app_create(&config);
    AppRequest req = press(app, INPUT_CONFIRM, 1.0);
    CHECK(req.compose);
    CHECK(!req.save_settings);
    app_destroy(app);
}

static void first_boot_asks_first(void) {
    AppConfig config = {0};
    settings_defaults(&config.settings);
    config.first_boot = 1;
    App *app = app_create(&config);
    AppRequest req = press(app, INPUT_DOWN, 1.0);
    req = press(app, INPUT_CONFIRM, 1.1);
    CHECK(!req.compose);
    CHECK(req.save_settings);
    CHECK(app_settings(app)->calm && app_settings(app)->still);
    req = press(app, INPUT_CONFIRM, 1.2);
    CHECK(req.compose);
    app_destroy(app);
}

int main(void) {
    app_boot();
    start_sails_at_once();
    first_boot_asks_first();
    if (!failures) printf("app: all passed\n");
    return failures ? 1 : 0;
}
