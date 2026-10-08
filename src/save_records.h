#ifndef FALLOUT_SAVE_RECORDS_H_
#define FALLOUT_SAVE_RECORDS_H_

#include <cstdint>
#include <map>
#include <string>

namespace fallout {

// What the port knows of each save, kept outside SAVEGAME so that folder
// stays as on a computer (its saves load in any engine): when the save was
// made - the save list's order, which a copy keeps (a file's time doesn't) -
// and what the game was made of then (save_compatibility.h).
//
// By slot and the save's identity (`saveRecordId`): a save replaced by
// other means (another engine, by hand) has no record. The app's export of
// saves carries the records, its import writes them (os/android
// GameSaves.java, which reads and writes this file the same way).
//
// One text file, rewritten whole (a temporary file renamed over it):
//   SLOT01 <tab> identity <tab> created <tab> order <tab> composition
// The app's import gives "*" for the composition: the game's own when it
// next starts (save_compatibility.cc).
struct SaveRecord {
    // Unix seconds, 0 - unknown.
    std::int64_t created = 0;
    // Creation order (save_catalog.h), 0 - unknown.
    std::int64_t order = 0;
    // Encoded composition (save_compatibility.cc), "" - unknown.
    std::string composition;
};

class SaveRecords {
public:
    // [path] - the file; "" - none (nothing is kept).
    void configure(const std::string& path);
    // Reads the file again (the app's import may have written it). Once there
    // was no file: `isNew` until something is written.
    void reload();
    bool isNew() const { return fileIsNew; }

    // The record of [slot]'s save of [identity]; false - none.
    bool find(int slot, const std::string& identity, SaveRecord* record) const;
    // Sets it (replacing the slot's), writes the file.
    bool put(int slot, const std::string& identity, const SaveRecord& record);
    // Forgets [slot]'s, writes the file when there was one.
    bool remove(int slot);

    // Records by slot (identity, record), to go through.
    const std::map<int, std::pair<std::string, SaveRecord>>& all() const { return records; }

private:
    std::string filePath;
    bool fileIsNew = false;
    std::map<int, std::pair<std::string, SaveRecord>> records;

    bool write();
};

// The save's identity: FNV-1a 64 of SAVE.DAT's first 4 KB and its size
// ("0123456789abcdef-68434"); "" when it can't be read.
std::string saveRecordId(const std::string& saveDatPath);

// The game's records (loadsave.cc configures them).
SaveRecords& gameSaveRecords();

} // namespace fallout

#endif /* FALLOUT_SAVE_RECORDS_H_ */
