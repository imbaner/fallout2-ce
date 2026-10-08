#ifndef FALLOUT_SAVE_COMPOSITION_H_
#define FALLOUT_SAVE_COMPOSITION_H_

#include <string>
#include <vector>

namespace fallout {

// What the game is made of, as kept for each save (save_records.h), and how
// two of them compare: one rule for the game (save_compatibility.cc) and the
// app's import of saves (import_archive_jni.cc). No engine code: the import
// library builds it too.
//
// Encoded: "name=fingerprint:K;..." - each archive's path in the game's
// folder ("mods/rpu.dat"), a fingerprint of its index, its kind: G -
// Fallout 2's own files, L - game logic (protos, scripts, maps, data), C -
// only looks and sounds.

enum class SaveCompatibility {
    // Made with this game, or only its looks changed.
    kSame,
    // A mod with game logic updated or added: likely works.
    kLikely,
    // Fallout 2's own files changed, or a mod with game logic is gone:
    // likely won't load right.
    kUnlikely,
    // Nothing known about it (made by another engine, elsewhere, before
    // this).
    kUnknown,
};

struct SaveCompatibilityChange {
    enum class Kind {
        kGameChanged,
        kModUpdated,
        kModAdded,
        kModRemoved,
    };

    Kind kind;
    // The archive's path in the game's folder ("mods/rpu.dat").
    std::string name;
};

// A save made with [made] against the game made of [now] (both encoded);
// [changes] (may be null): what differs that matters. kUnknown when [made]
// is empty.
SaveCompatibility saveCompositionCompare(const std::string& made, const std::string& now, std::vector<SaveCompatibilityChange>* changes);

} // namespace fallout

#endif /* FALLOUT_SAVE_COMPOSITION_H_ */
