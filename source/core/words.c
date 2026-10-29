// SPDX-License-Identifier: GPL-3.0-or-later
#include "words.h"

#include "rng.h"

#include <stdio.h>

// Short, soft words: a random seed doubles as the piece's title, so every pair should sound like one.
static const char *const ADJECTIVES[] = {
    "amber",   "ashen",   "autumn",  "azure",   "bright",  "broken",  "calm",    "candle",  "cedar",   "cinder",
    "clear",   "cloud",   "cobalt",  "copper",  "coral",   "crimson", "crystal", "dawn",    "deep",    "distant",
    "dream",   "drifting", "dusk",   "dusty",   "echo",    "ember",   "evening", "faded",   "feather", "fern",
    "fallen",  "first",   "floating", "foggy",  "frost",   "gentle",  "ghost",   "gilded",  "glass",   "gold",
    "hidden",  "hollow",  "honey",   "indigo",  "iron",    "ivory",   "jade",    "kindred", "last",    "lavender",
    "lilac",   "linen",   "little",  "lonely",  "lost",    "lunar",   "marble",  "meadow",  "midnight", "misty",
    "moonlit", "morning", "moss",    "mossy",   "neon",    "night",   "northern", "ocean",  "old",     "opal",
    "paper",   "pastel",  "pearl",   "pine",    "polar",   "quiet",   "rain",    "rainy",   "rose",    "rusty",
    "sable",   "saffron", "salt",    "scarlet", "secret",  "shining", "silent",  "silk",    "silver",  "sleepy",
    "slow",    "snowy",   "soft",    "solar",   "spring",  "starlit", "still",   "storm",   "summer",  "sunken",
    "tidal",   "twilight", "velvet", "violet",  "wandering", "warm",  "wild",    "willow",  "winter",  "woven",
};

static const char *const NOUNS[] = {
    "anchor",  "arrow",   "atlas",   "aurora",  "beacon",  "bell",    "bird",    "bloom",   "bridge",  "candle",
    "canyon",  "cat",     "cloud",   "comet",   "compass", "coral",   "crown",   "current", "dawn",    "delta",
    "drift",   "dune",    "echo",    "ember",   "feather", "fern",    "field",   "fjord",   "flame",   "flower",
    "forest",  "fountain", "garden", "glacier", "grove",   "harbor",  "haven",   "heron",   "hill",    "horizon",
    "island",  "kite",    "lagoon",  "lake",    "lantern", "leaf",    "light",   "lily",    "meadow",  "meridian",
    "mirror",  "moon",    "moth",    "mountain", "nebula", "nest",    "ocean",   "orbit",   "orchard", "owl",
    "path",    "pearl",   "petal",   "pier",    "planet",  "pond",    "prism",   "quartz",  "rain",    "reef",
    "ribbon",  "river",   "road",    "rose",    "sail",    "shell",   "shore",   "signal",  "sky",     "snow",
    "song",    "sparrow", "spiral",  "spring",  "star",    "stone",   "storm",   "stream",  "summit",  "swan",
    "thread",  "tide",    "tower",   "trail",   "tree",    "valley",  "veil",    "voyage",  "wave",    "whale",
    "willow",  "wind",    "wing",    "wish",    "wolf",    "wren",
};

void seed_random(uint64_t entropy, char *out, int size) {
    Rng r;
    rng_init(&r, entropy, "words");
    int na = (int)(sizeof ADJECTIVES / sizeof ADJECTIVES[0]), nn = (int)(sizeof NOUNS / sizeof NOUNS[0]);
    const char *a = ADJECTIVES[rng_range(&r, 0, na)], *n = NOUNS[rng_range(&r, 0, nn)];
    for (int tries = 0; tries < 8 && !__builtin_strcmp(a, n); tries++) n = NOUNS[rng_range(&r, 0, nn)];
    snprintf(out, (size_t)size, "%s %s", a, n);
}
