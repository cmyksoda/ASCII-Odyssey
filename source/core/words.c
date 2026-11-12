// SPDX-License-Identifier: GPL-3.0-or-later
#include "words.h"

#include "rng.h"

#include <stdio.h>

// Short words for things you can picture: a random seed doubles as the piece's title, so each pair should read as one.
static const char *const ADJECTIVES[] = {
    "amber",   "analog",  "arcade",  "arctic",  "autumn",  "backyard", "bright", "broken",  "bubblegum", "calm",
    "candle",  "cedar",   "cherry",  "chrome",  "clear",   "cloud",   "cobalt",  "cocoa",   "concrete", "copper",
    "coral",   "crimson", "crooked", "crystal", "cyan",    "deep",    "desert",  "digital", "disco",   "distant",
    "dizzy",   "double",  "downtown", "dusk",   "dusty",   "electric", "evening", "faded",  "fern",    "first",
    "floating", "floppy", "foggy",   "frost",   "frozen",  "fuzzy",   "ghost",   "giant",   "glass",   "gold",
    "hidden",  "holiday", "honey",   "indigo",  "iron",    "ivory",   "jade",    "laser",   "last",    "late",
    "lavender", "lemon",  "lilac",   "little",  "lonely",  "lost",    "lucky",   "lunar",   "magenta", "magnetic",
    "mango",   "marble",  "meadow",  "melon",   "midnight", "mint",   "morning", "mossy",   "neon",    "night",
    "northern", "ocean",  "old",     "opal",    "orbital", "paper",   "pastel",  "peach",   "pearl",   "pine",
    "pixel",   "plastic", "plum",    "pocket",  "polar",   "prairie", "quiet",   "rainy",   "remote",  "rocket",
    "rooftop", "rose",    "rusty",   "saffron", "salt",    "sandy",   "satellite", "scarlet", "secret", "shy",
    "silent",  "silver",  "sleepy",  "slow",    "snowy",   "soft",    "solar",   "spare",   "spring",  "static",
    "stereo",  "storm",   "subway",  "sugar",   "summer",  "sunday",  "sunken",  "sunny",   "teal",    "thunder",
    "tidal",   "tiny",    "tropical", "turbo",  "upstairs", "vanilla", "vector", "violet",  "warm",    "weekend",
    "wild",    "willow",  "winter",  "wobbly",  "yellow",
};

static const char *const NOUNS[] = {
    "airport", "anchor",  "antenna", "aquarium", "arcade", "arpeggio", "arrow",  "atlas",   "aurora",  "bakery",
    "balloon", "beach",   "bead",    "beetle",  "bell",    "bicycle", "bird",    "bridge",  "button",  "campfire",
    "canal",   "candle",  "canyon",  "carnival", "carousel", "cassette", "cat",  "channel", "chorus",  "cloud",
    "comet",   "compass", "cookie",  "coral",   "cove",    "crab",    "crayon",  "crown",   "dawn",    "delta",
    "diner",   "dolphin", "donut",   "dune",    "eclipse", "elevator", "encore", "feather", "fern",    "ferry",
    "field",   "fjord",   "flame",   "flower",  "forest",  "fountain", "fox",    "frog",    "galaxy",  "garden",
    "gecko",   "glacier", "goose",   "grove",   "harbor",  "heron",   "highway", "hill",    "horizon", "island",
    "jukebox", "kite",    "koi",     "lagoon",  "lake",    "lantern", "laundry", "leaf",    "library", "light",
    "lily",    "loop",    "lullaby", "market",  "marsh",   "meadow",  "meridian", "mirror", "mixtape", "modem",
    "moon",    "motel",   "moth",    "mountain", "museum", "nebula",  "nest",    "noodle",  "ocean",   "octave",
    "orbit",   "orchard", "otter",   "owl",     "path",    "pearl",   "penguin", "petal",   "pier",    "pigeon",
    "planet",  "plaza",   "pond",    "postcard", "prism",  "pulsar",  "quartz",  "rabbit",  "radio",   "rain",
    "rainbow", "reef",    "ribbon",  "river",   "road",    "robot",   "rocket",  "rooftop", "rose",    "sail",
    "shell",   "shore",   "signal",  "sky",     "skyline", "snail",   "snow",    "soda",    "song",    "sparrow",
    "spiral",  "spring",  "star",    "station", "sticker", "stone",   "storm",   "stream",  "subway",  "summit",
    "sundae",  "swan",    "sweater", "taxi",    "teacup",  "thread",  "ticket",  "tide",    "tower",   "trail",
    "train",   "tram",    "tree",    "tundra",  "umbrella", "valley", "village", "volcano", "voyage",  "waltz",
    "wave",    "whale",   "willow",  "wind",    "window",  "wing",    "wish",    "wolf",    "wren",    "yoyo",
};

void seed_random(uint64_t entropy, char *out, int size) {
    Rng r;
    rng_init(&r, entropy, "words");
    int na = (int)(sizeof ADJECTIVES / sizeof ADJECTIVES[0]), nn = (int)(sizeof NOUNS / sizeof NOUNS[0]);
    const char *a = ADJECTIVES[rng_range(&r, 0, na)], *n = NOUNS[rng_range(&r, 0, nn)];
    for (int tries = 0; tries < 8 && !__builtin_strcmp(a, n); tries++) n = NOUNS[rng_range(&r, 0, nn)];
    snprintf(out, (size_t)size, "%s %s", a, n);
}
