package com.alexbatalov.fallout2ce;

// How a save's composition compares with the game's (src/save_composition.h,
// in the import library: one rule for the game and the app).
final class SaveComposition {
    static {
        System.loadLibrary("fallout2-import");
    }

    private SaveComposition() {
    }

    // The level (GameSaves.SAME...), then each change as "kind<tab>archive",
    // one a line.
    static String compare(String made, String now) {
        return nativeCompare(made, now);
    }

    private static native String nativeCompare(String made, String now);
}
