#include "save_catalog.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>

#include "save_storage.h"

namespace fallout {

namespace {

    constexpr char kSaveDataName[] = "SAVE.DAT";
    constexpr char kMetadataName[] = "CE-META.TXT";
    constexpr char kSaveSignature[] = "FALLOUT SAVE FILE";
    constexpr int kMetadataVersion = 1;

    // Bytes of SAVE.DAT's start in the identity (signature, version, names,
    // dates, map), less than a whole header is not a save.
    constexpr int kIdentityHeaderSize = 256;
    constexpr int kMinimalHeaderSize = 131;

    std::string upperCase(const std::string& value)
    {
        std::string result = value;
        for (char& ch : result) {
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        }
        return result;
    }

    // Slot of a "SLOTnn" folder name in any case, -1 - not a slot folder.
    // Names like "SLOT0001" aren't the game's.
    int slotFromName(const std::string& name, int total)
    {
        std::string upper = upperCase(name);
        int number;
        char trailing;
        if (sscanf(upper.c_str(), "SLOT%d%c", &number, &trailing) != 1 || number < 1 || number > total) {
            return -1;
        }

        char canonical[32];
        snprintf(canonical, sizeof(canonical), "SLOT%02d", number);
        return upper == canonical ? number - 1 : -1;
    }

    // Slot of a ".ce-write-N" / ".ce-cleanup-N" folder, -1 - neither.
    int transactionSlot(const std::string& name, int total)
    {
        int slot;
        char trailing;
        if (sscanf(name.c_str(), ".ce-write-%d%c", &slot, &trailing) != 1
            && sscanf(name.c_str(), ".ce-cleanup-%d%c", &slot, &trailing) != 1) {
            return -1;
        }
        return slot >= 0 && slot < total ? slot : -1;
    }

    // SAVE.DAT of a slot folder, in any case (saves copied from Windows).
    std::string saveDataPath(const std::string& directory)
    {
        std::string canonical = directory + "/" + kSaveDataName;
        saveStorage::Info info;
        if (!saveStorage::inspect(canonical, info) || info.exists) {
            return canonical;
        }

        std::vector<std::string> names;
        if (saveStorage::children(directory, names)) {
            for (const std::string& name : names) {
                if (upperCase(name) == kSaveDataName) {
                    return directory + "/" + name;
                }
            }
        }

        return canonical;
    }

    // SAVE.DAT's file time, size and header. The metadata is trusted only
    // while it matches, so a SAVE.DAT replaced by another engine or by hand
    // gets its order from the file again.
    std::uint64_t identity(const std::string& path)
    {
        saveStorage::Info info;
        if (!saveStorage::inspect(path, info) || !info.regular) {
            return 0;
        }

        // FNV-1a.
        std::uint64_t hash = 14695981039346656037ULL;
        auto mix = [&](unsigned char byte) {
            hash = (hash ^ byte) * 1099511628211ULL;
        };

        std::uint64_t size = info.size;
        std::uint64_t stamp = static_cast<std::uint64_t>(info.modified * 1000000000LL + info.nanos);
        for (unsigned int shift = 0; shift < 64; shift += 8) {
            mix(static_cast<unsigned char>(size >> shift));
            mix(static_cast<unsigned char>(stamp >> shift));
        }

        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return 0;
        }

        char header[kIdentityHeaderSize];
        file.read(header, sizeof(header));
        std::streamsize length = file.gcount();
        if (file.bad() || length < kMinimalHeaderSize || memcmp(header, kSaveSignature, strlen(kSaveSignature)) != 0) {
            return 0;
        }

        for (std::streamsize index = 0; index < length; index++) {
            mix(static_cast<unsigned char>(header[index]));
        }

        return hash;
    }

    std::int64_t unixTimeNow()
    {
        return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    }

} // namespace

void SaveCatalog::configure(const std::string& root, int total, int firstQuick, int quickCount, SaveRecords* saveRecords)
{
    saveRoot = root;
    records = saveRecords;
    saveStorage::openDirectories(saveRoot);
    entries.assign(std::max(total, 0), Entry());
    slotNames.assign(entries.size(), std::string());
    firstQuickSlot = firstQuick;
    quickSlotCount = firstQuick >= 0 && firstQuick < total
        ? std::clamp(quickCount, 0, total - firstQuick)
        : 0;
    lastOrder = 0;
    resetSession();
}

std::string SaveCatalog::slotPath(int slot) const
{
    if (!slotNames[slot].empty()) {
        return saveRoot + "/" + slotNames[slot];
    }

    char name[32];
    snprintf(name, sizeof(name), "SLOT%02d", slot + 1);
    return saveRoot + "/" + name;
}

std::string SaveCatalog::saveDatPath(int slot) const
{
    return saveDataPath(slotPath(slot));
}

std::string SaveCatalog::transactionPath(int slot) const
{
    return saveRoot + "/.ce-write-" + std::to_string(slot);
}

std::string SaveCatalog::cleanupPath(int slot) const
{
    return saveRoot + "/.ce-cleanup-" + std::to_string(slot);
}

bool SaveCatalog::isQuick(int slot) const
{
    return slot >= firstQuickSlot && slot < firstQuickSlot + quickSlotCount;
}

const SaveCatalog::Entry& SaveCatalog::entry(int slot) const
{
    static const Entry empty;
    return slot >= 0 && slot < static_cast<int>(entries.size()) ? entries[slot] : empty;
}

SaveCatalog::Entry SaveCatalog::read(int slot) const
{
    Entry result;
    std::string path = slotPath(slot);

    saveStorage::Info info;
    if (!saveStorage::inspect(path, info)) {
        // Unreadable: taken, never reused.
        result.present = true;
        return result;
    }

    if (!info.exists) {
        return result;
    }

    result.present = true;
    if (!info.directory) {
        return result;
    }

    std::string data = saveDataPath(path);
    result.identity = identity(data);
    if (result.identity == 0) {
        return result;
    }

    if (saveStorage::inspect(data, info)) {
        result.created = info.modified;
        result.order = result.created * 1000000 + info.nanos / 1000;
    }

    result.recordId = saveRecordId(data);
    SaveRecord record;
    // A record without the time (a computer's save, imported) keeps its
    // place in the list; no time is shown for it.
    if (records != nullptr && records->find(slot, result.recordId, &record) && record.order > 0 && record.created >= 0) {
        result.created = record.created;
        result.order = record.order;
        return result;
    }

    int version;
    std::int64_t created;
    std::int64_t order;
    std::uint64_t fingerprint;
    std::ifstream metadata(path + "/" + kMetadataName);
    if (metadata >> version >> created >> order >> fingerprint
        && version == kMetadataVersion
        && created > 0
        && order > 0
        && order < std::numeric_limits<std::int64_t>::max() - 1
        && fingerprint == result.identity) {
        result.created = created;
        result.order = order;
        result.legacyMetadata = true;
    }

    return result;
}

// Finishes what an interrupted write or removal of [slot] left: without the
// commit mark the previous folder comes back; then the transaction folder is
// renamed to the cleanup one before its files are deleted, so a crash while
// deleting can't make a half deleted backup look like one to restore.
bool SaveCatalog::recover(int slot)
{
    std::string path = slotPath(slot);
    std::string transaction = transactionPath(slot);
    std::string garbage = cleanupPath(slot);

    if (!saveStorage::removeTree(garbage)) {
        return false;
    }

    saveStorage::Info info;
    if (!saveStorage::inspect(transaction, info)) {
        return false;
    }

    if (!info.exists) {
        return true;
    }

    if (!info.directory) {
        return false;
    }

    if (!saveStorage::inspect(transaction + "/committed", info)) {
        return false;
    }

    if (!info.regular) {
        std::string previous = transaction + "/previous";
        if (!saveStorage::inspect(previous, info)) {
            return false;
        }

        bool hadPrevious = info.directory;
        if (info.exists && !hadPrevious) {
            return false;
        }

        if (!saveStorage::inspect(transaction + "/new", info)) {
            return false;
        }

        if (hadPrevious || info.regular) {
            if (!saveStorage::removeTree(path)) {
                return false;
            }

            if (hadPrevious && !saveStorage::move(previous, path)) {
                return false;
            }

            if (!saveStorage::syncDirectory(saveRoot)) {
                return false;
            }
        }
    }

    return saveStorage::move(transaction, garbage)
        && saveStorage::syncDirectory(saveRoot)
        && saveStorage::removeTree(garbage);
}

bool SaveCatalog::refresh()
{
    int total = static_cast<int>(entries.size());
    if (!saveStorage::makeDirectory(saveRoot)) {
        return false;
    }

    std::vector<std::string> names;
    if (!saveStorage::children(saveRoot, names)) {
        return false;
    }

    std::fill(slotNames.begin(), slotNames.end(), std::string());
    std::vector<bool> interrupted(entries.size(), false);
    for (const std::string& name : names) {
        int slot = slotFromName(name, total);
        if (slot != -1) {
            slotNames[slot] = name;
            continue;
        }

        slot = transactionSlot(name, total);
        if (slot != -1) {
            interrupted[slot] = true;
        }
    }

    bool recovered = true;
    for (int slot = 0; slot < total; slot++) {
        if (interrupted[slot]) {
            if (!recover(slot)) {
                recovered = false;
            }
        } else if (slotNames[slot].empty()) {
            entries[slot] = Entry();
            continue;
        }

        entries[slot] = read(slot);
        lastOrder = std::max(lastOrder, entries[slot].order);
    }

    if (records != nullptr) {
        updateRecords();
    }

    return recovered;
}

// The records follow the folder: saves of a new records file (the first
// time) and of an earlier build's CE-META.TXT get one (the file is then
// removed); a slot with another save drops the old one's. A save gone from
// the folder keeps its record (moved away by hand and back, it is known
// again); deleting it in the game drops it (`remove`).
void SaveCatalog::updateRecords()
{
    bool first = records->isNew();
    for (int slot = 0; slot < static_cast<int>(entries.size()); slot++) {
        Entry& current = entries[slot];
        SaveRecord record;
        if (current.identity == 0 || current.recordId.empty() || records->find(slot, current.recordId, &record)) {
            continue;
        }
        if (records->all().count(slot) != 0) {
            // Another save there now.
            records->remove(slot);
        }
        if (current.legacyMetadata || first) {
            record.created = current.created;
            record.order = current.order;
            record.composition = first ? "*" : "";
            if (records->put(slot, current.recordId, record) && current.legacyMetadata) {
                saveStorage::removeTree(slotPath(slot) + "/" + kMetadataName);
                current.legacyMetadata = false;
            }
        }
    }
}

int SaveCatalog::freeManual() const
{
    for (int slot = 0; slot < static_cast<int>(entries.size()); slot++) {
        if (!isQuick(slot) && !entries[slot].present) {
            return slot;
        }
    }
    return -1;
}

int SaveCatalog::newest() const
{
    int newest = -1;
    for (int slot = 0; slot < static_cast<int>(entries.size()); slot++) {
        const Entry& current = entries[slot];
        if (current.present && current.identity != 0 && (newest == -1 || current.order > entries[newest].order)) {
            newest = slot;
        }
    }
    return newest;
}

int SaveCatalog::nextQuick(const std::function<bool(int)>& canReplace) const
{
    int oldest = -1;
    for (int slot = firstQuickSlot; slot < firstQuickSlot + quickSlotCount; slot++) {
        const Entry& current = entries[slot];
        if (!current.present) {
            return slot;
        }

        if (current.identity == 0 || (canReplace && !canReplace(slot))) {
            continue;
        }

        if (oldest == -1 || current.order < entries[oldest].order) {
            oldest = slot;
        }
    }
    return oldest;
}

bool SaveCatalog::write(int slot, const std::function<bool()>& writer, bool sessionSave)
{
    if (slot < 0 || slot >= static_cast<int>(entries.size()) || !recover(slot)) {
        return false;
    }

    std::string path = slotPath(slot);
    std::string transaction = transactionPath(slot);

    saveStorage::Info info;
    if (!saveStorage::inspect(path, info) || (info.exists && !info.directory)) {
        return false;
    }

    if (!saveStorage::makeDirectory(transaction)) {
        return false;
    }

    // The old folder is kept aside; a new slot is marked so an interrupted
    // write is removed.
    bool prepared = info.exists
        ? saveStorage::move(path, transaction + "/previous")
        : saveStorage::writeFile(transaction + "/new", "1\n");
    if (!prepared) {
        recover(slot);
        return false;
    }

    bool written = saveStorage::syncDirectory(transaction)
        && saveStorage::syncDirectory(saveRoot)
        && saveStorage::makeDirectory(path)
        && writer();

    // An earlier build's file copied along (a quick save's copy) isn't this
    // save's.
    saveStorage::removeTree(path + "/" + kMetadataName);

    Entry saved = read(slot);
    if (written && saved.identity != 0) {
        written = saveStorage::syncTree(path)
            && saveStorage::syncDirectory(saveRoot)
            && saveStorage::writeFile(transaction + "/commit.tmp", "1\n")
            && saveStorage::move(transaction + "/commit.tmp", transaction + "/committed");
        if (written) {
            saveStorage::syncDirectory(transaction);
        }
    } else {
        written = false;
    }

    // Rolls a failed write back, or only cleans up after a committed one (a
    // failed cleanup doesn't lose the save, the next refresh finishes it).
    recover(slot);

    if (!written) {
        entries[slot] = read(slot);
        lastOrder = std::max(lastOrder, entries[slot].order);
        return false;
    }

    // When it was made: in the records (a failed write only loses its place
    // in the list - SAVE.DAT's time is used).
    saved.created = unixTimeNow();
    saved.order = std::max(lastOrder + 1, saved.created * 1000000);
    saved.legacyMetadata = false;
    if (records != nullptr) {
        SaveRecord record;
        record.created = saved.created;
        record.order = saved.order;
        records->put(slot, saved.recordId, record);
    }

    entries[slot] = saved;
    lastOrder = saved.order;
    if (sessionSave) {
        loaded(slot);
    }

    return true;
}

int SaveCatalog::copyQuick(int source)
{
    if (!isQuick(source) || entry(source).identity == 0) {
        return -1;
    }

    int target = freeManual();
    if (target == -1) {
        return -1;
    }

    std::uint64_t sourceIdentity = entry(source).identity;
    std::string sourcePath = slotPath(source);
    std::string targetPath = slotPath(target);
    bool copied = write(target, [&]() {
        return saveStorage::copyTree(sourcePath, targetPath)
            && identity(saveDataPath(sourcePath)) == sourceIdentity;
    },
        false);
    if (!copied) {
        return -1;
    }

    // The same game made it: its composition too.
    SaveRecord sourceRecord;
    SaveRecord targetRecord;
    if (records != nullptr
        && records->find(source, entry(source).recordId, &sourceRecord)
        && records->find(target, entry(target).recordId, &targetRecord)) {
        targetRecord.composition = sourceRecord.composition;
        records->put(target, entry(target).recordId, targetRecord);
    }
    return target;
}

bool SaveCatalog::remove(int slot)
{
    if (slot < 0 || slot >= static_cast<int>(entries.size()) || !recover(slot)) {
        return false;
    }

    std::string path = slotPath(slot);
    saveStorage::Info info;
    if (!saveStorage::inspect(path, info) || !info.exists) {
        entries[slot] = read(slot);
        return false;
    }

    // One rename takes the slot away at once; its files are deleted from
    // the cleanup folder (what's left is deleted by the next refresh).
    std::string garbage = cleanupPath(slot);
    if (!saveStorage::move(path, garbage)) {
        return false;
    }

    saveStorage::syncDirectory(saveRoot);
    saveStorage::removeTree(garbage);

    slotNames[slot].clear();
    entries[slot] = Entry();
    forget(slot);
    if (records != nullptr) {
        records->remove(slot);
    }
    return true;
}

void SaveCatalog::resetSession()
{
    session.clear();
}

void SaveCatalog::loaded(int slot)
{
    if (slot < 0 || slot >= static_cast<int>(entries.size())) {
        return;
    }

    entries[slot] = read(slot);
    if (entries[slot].identity == 0) {
        return;
    }

    forget(slot);
    session.emplace_back(slot, entries[slot].identity);
}

void SaveCatalog::forget(int slot)
{
    session.erase(std::remove_if(session.begin(), session.end(), [slot](const std::pair<int, std::uint64_t>& value) {
        return value.first == slot;
    }),
        session.end());
}

int SaveCatalog::sessionTarget() const
{
    for (auto it = session.rbegin(); it != session.rend(); ++it) {
        if (read(it->first).identity == it->second) {
            return it->first;
        }
    }
    return -1;
}

} // namespace fallout
