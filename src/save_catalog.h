#ifndef FALLOUT_SAVE_CATALOG_H_
#define FALLOUT_SAVE_CATALOG_H_

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "save_records.h"

namespace fallout {

// Saves of the game's SAVEGAME folder (loadsave.cc): which physical slots
// (SLOTnn, compatible with CE/sfall) are used, their creation order, where
// quick and new manual saves go and what quick load loads in this session.
// The slot the legacy engine or the UI has selected plays no part in it.
//
// Writes are transactions over the whole slot folder: the old folder is
// moved aside (`.ce-write-N/previous`) while the game writes the new one and
// comes back if the write fails or the game stops midway (recovered by the
// next refresh). Order and creation time are kept in the port's records
// (save_records.h), not in the folder; saves without a record (other
// engines, by hand) use SAVE.DAT's file time. Earlier builds kept them in
// `CE-META.TXT` next to SAVE.DAT: moved to the records by a refresh.
class SaveCatalog {
public:
    struct Entry {
        // Something is in the slot (a save, a broken or unknown folder).
        bool present = false;
        // Unix seconds, shown to the player.
        std::int64_t created = 0;
        // Creation order, grows even when the clock doesn't.
        std::int64_t order = 0;
        // SAVE.DAT's file time, size and header; 0 - no valid SAVE.DAT.
        std::uint64_t identity = 0;
        // Its identity in the records (save_records.h).
        std::string recordId;
        // Order and time from an earlier build's CE-META.TXT, to move to the
        // records.
        bool legacyMetadata = false;
    };

    // [quickCount] slots from [firstQuick] take quick saves (clipped to
    // [total]; 0 - no quick range), the rest manual ones. [records] - where
    // order and creation time are kept (none: SAVE.DAT's time only). The
    // first refresh with new records gives every save one (its composition
    // "*": the game's now, save_compatibility.h).
    void configure(const std::string& root, int total, int firstQuick, int quickCount, SaveRecords* records = nullptr);

    // Reads the folder again (one listing, then only the slots that exist)
    // and recovers interrupted writes. False when a recovery failed.
    bool refresh();

    bool isQuick(int slot) const;
    // The SAVE.DAT of [slot] (whether it's there or not).
    std::string saveDatPath(int slot) const;
    const Entry& entry(int slot) const;

    // First free slot outside the quick range, -1 - none.
    int freeManual() const;

    // The save made last (valid SAVE.DAT, the highest order), quick or
    // manual; -1 - none.
    int newest() const;

    // Quick save target: a free quick slot, otherwise the oldest valid quick
    // save ([canReplace] may refuse some). Broken or unknown folders are
    // never replaced. -1 - none.
    int nextQuick(const std::function<bool(int)>& canReplace = {}) const;

    // Writes a complete save into [slot]: [writer] fills the empty slot
    // folder. [sessionSave] - quick load loads it next.
    bool write(int slot, const std::function<bool()>& writer, bool sessionSave = true);

    // Copies quick save [source] with all its files into a free manual slot
    // (a new save made now). Returns the slot or -1.
    int copyQuick(int source);

    // Removes the whole folder of [slot] (saves, maps, mod files, previews;
    // links inside are removed, not followed). False when nothing was
    // removed.
    bool remove(int slot);

    // Quick load: the last save loaded or made in this session.
    void resetSession();
    void loaded(int slot);
    int sessionTarget() const;

private:
    std::string saveRoot;
    SaveRecords* records = nullptr;
    int firstQuickSlot = 0;
    int quickSlotCount = 0;
    std::vector<Entry> entries;
    // Folder names as they are on disk (the game accepts "slot01" too).
    std::vector<std::string> slotNames;
    // Slots and identities of loaded/made saves, the last one latest.
    std::vector<std::pair<int, std::uint64_t>> session;
    std::int64_t lastOrder = 0;

    std::string slotPath(int slot) const;
    std::string transactionPath(int slot) const;
    std::string cleanupPath(int slot) const;
    bool recover(int slot);
    void updateRecords();
    Entry read(int slot) const;
    void forget(int slot);
};

} // namespace fallout

#endif /* FALLOUT_SAVE_CATALOG_H_ */
