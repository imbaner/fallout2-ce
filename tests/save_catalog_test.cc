// Save catalog test (src/save_catalog.cc) on a real folder: retention of quick
// saves, the session's quick load, manual slots, copies, removal, failed and
// interrupted writes, saves changed outside the game, slot ranges, the
// records of when saves were made (src/save_records.cc).
//
// save_catalog_test [empty folder to create] - a temporary one by default.

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <fstream>
#include <iterator>
#include <string>
#include <utility>

#include "save_catalog.h"
#include "save_records.h"
#include "save_storage.h"

using fallout::SaveCatalog;
using fallout::SaveRecord;
using fallout::SaveRecords;
namespace storage = fallout::saveStorage;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "FAIL: line %d: %s\n", __LINE__, #condition);     \
            exit(EXIT_FAILURE);                                                \
        }                                                                      \
    } while (false)

namespace {

std::string snapshot(char value)
{
    std::string data(1024, value);
    data.replace(0, 17, "FALLOUT SAVE FILE");
    return data;
}

std::string read(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

bool exists(const std::string& path)
{
    storage::Info info;
    return storage::inspect(path, info) && info.exists;
}

void makeDirectories(const std::string& path)
{
    for (size_t slash = path.find('/', 1); slash != std::string::npos; slash = path.find('/', slash + 1)) {
        storage::makeDirectory(path.substr(0, slash));
    }
    CHECK(storage::makeDirectory(path));
}

std::string slotName(int slot)
{
    char name[32];
    snprintf(name, sizeof(name), "SLOT%02d", slot + 1);
    return name;
}

} // namespace

int main(int argc, char** argv)
{
    std::string root;
    bool temporary = argc < 2;
    if (temporary) {
        char pattern[] = "/tmp/save_catalog_test-XXXXXX";
        CHECK(mkdtemp(pattern) != nullptr);
        root = std::string(pattern) + "/saves";
    } else {
        root = argv[1];
        CHECK(!exists(root));
    }
    std::string outside = root + "-outside";
    makeDirectories(root);

    SaveRecords records;
    records.configure(root + "-records.txt");
    CHECK(records.isNew());
    SaveCatalog store;
    store.configure(root, 1000, 10, 10, &records);
    CHECK(store.refresh());

    auto write = [&](int slot, char value) {
        std::string path = root + "/" + slotName(slot);
        return store.write(slot, [&]() {
            return storage::writeFile(path + "/SAVE.DAT", snapshot(value))
                && storage::makeDirectory(path + "/proto")
                && storage::writeFile(path + "/proto/mod.sav", "mod data");
        });
    };

    // Quick saves fill their range, then replace the oldest one.
    CHECK(store.freeManual() == 0 && store.nextQuick() == 10 && store.newest() == -1);
    for (int slot = 10; slot < 20; slot++) {
        CHECK(store.nextQuick() == slot);
        CHECK(write(slot, static_cast<char>('a' + slot)));
    }
    CHECK(store.nextQuick() == 10);

    // Loading an old quick save doesn't change which one is replaced next;
    // quick load follows the last loaded or made save.
    store.loaded(12);
    CHECK(store.sessionTarget() == 12 && store.nextQuick() == 10);
    CHECK(write(0, 'M'));
    CHECK(store.sessionTarget() == 0 && store.nextQuick() == 10);
    CHECK(write(store.nextQuick(), 'N'));
    CHECK(store.sessionTarget() == 10 && store.nextQuick() == 11);
    // Continue loads the save made last (loading 12 above didn't change it).
    CHECK(store.newest() == 10);

    // A failed write leaves the previous save, a failed new one nothing.
    std::int64_t lastOrder = store.entry(10).order;
    std::string original = read(root + "/SLOT11/SAVE.DAT");
    CHECK(!store.write(10, [&]() {
        storage::writeFile(root + "/SLOT11/SAVE.DAT", "partial");
        return false;
    }));
    CHECK(read(root + "/SLOT11/SAVE.DAT") == original);
    CHECK(store.entry(10).order == lastOrder && store.sessionTarget() == 10);
    CHECK(!store.write(2, []() { return false; }));
    CHECK(!exists(root + "/SLOT03"));

    // A copy of a quick save: all its files in a manual slot, a new save.
    int copy = store.copyQuick(10);
    CHECK(copy == 1 && store.sessionTarget() == 10);
    CHECK(read(root + "/SLOT02/SAVE.DAT") == original);
    CHECK(read(root + "/SLOT02/proto/mod.sav") == "mod data");
    CHECK(store.copyQuick(copy) == -1);
    CHECK(store.entry(copy).order > lastOrder);

    // When saves were made is in the records, not in their folders; a copy
    // of the folder with them keeps the order (the app's import of saves).
    CHECK(!exists(root + "/SLOT02/CE-META.TXT"));
    SaveRecord record;
    CHECK(records.find(copy, store.entry(copy).recordId, &record) && record.order == store.entry(copy).order);
    {
        std::string copied = root + "-copied";
        CHECK(storage::copyTree(root, copied));
        SaveRecords copiedRecords;
        copiedRecords.configure(root + "-records.txt");
        SaveCatalog other;
        other.configure(copied, 1000, 10, 10, &copiedRecords);
        CHECK(other.refresh());
        CHECK(other.entry(copy).order == store.entry(copy).order && other.newest() == store.newest());
        CHECK(storage::removeTree(copied));
    }

    // Removing a slot frees it at once and forgets it in the session; a link
    // inside is removed, its target stays.
    makeDirectories(outside);
    CHECK(storage::writeFile(outside + "/keep.txt", "keep"));
    CHECK(symlink(outside.c_str(), (root + "/SLOT01/linked").c_str()) == 0);
    CHECK(store.remove(0) && !exists(root + "/SLOT01") && store.freeManual() == 0);
    CHECK(read(outside + "/keep.txt") == "keep");
    CHECK(storage::removeTree(outside));
    CHECK(!store.remove(0));
    CHECK(store.remove(13) && store.nextQuick() == 13);

    // An interrupted removal is finished by the next refresh.
    makeDirectories(root + "/.ce-cleanup-5/maps");
    CHECK(store.refresh() && !exists(root + "/.ce-cleanup-5"));

    // An interrupted replacement restores every file of the previous save.
    CHECK(storage::makeDirectory(root + "/.ce-write-10"));
    CHECK(storage::move(root + "/SLOT11", root + "/.ce-write-10/previous"));
    CHECK(storage::makeDirectory(root + "/SLOT11"));
    CHECK(storage::writeFile(root + "/SLOT11/SAVE.DAT", "partial"));
    SaveCatalog restarted;
    restarted.configure(root, 1000, 10, 10, &records);
    CHECK(restarted.refresh());
    CHECK(read(root + "/SLOT11/SAVE.DAT") == original);
    CHECK(read(root + "/SLOT11/proto/mod.sav") == "mod data");

    // A new session: quick load follows loads; removed saves drop out.
    CHECK(restarted.sessionTarget() == -1);
    restarted.loaded(1);
    CHECK(restarted.sessionTarget() == 1);
    restarted.loaded(10);
    CHECK(restarted.sessionTarget() == 10);
    CHECK(restarted.remove(10));
    CHECK(restarted.sessionTarget() == 1);

    // A half deleted cleanup folder never rolls a committed save back.
    makeDirectories(root + "/.ce-cleanup-1/previous");
    CHECK(restarted.refresh());
    CHECK(read(root + "/SLOT02/SAVE.DAT") == original);

    // A folder without SAVE.DAT and a link aren't free slots.
    CHECK(storage::makeDirectory(root + "/SLOT01"));
    CHECK(symlink((root + "/SLOT02").c_str(), (root + "/SLOT03").c_str()) == 0);
    restarted.refresh();
    CHECK(restarted.freeManual() == 3);

    // A save replaced outside the game gets its order from the file again
    // and is no longer the session's.
    std::int64_t oldOrder = restarted.entry(1).order;
    CHECK(storage::writeFile(root + "/SLOT02/SAVE.DAT", snapshot('X')));
    restarted.refresh();
    CHECK(restarted.sessionTarget() == -1);
    CHECK(restarted.entry(1).order != oldOrder);

    // A save moved away and back keeps its record; removing it drops it.
    {
        CHECK(restarted.remove(0) || !exists(root + "/SLOT01"));
        CHECK(restarted.write(0, [&]() { return storage::writeFile(root + "/SLOT01/SAVE.DAT", snapshot('A')); }));
        std::int64_t order = restarted.entry(0).order;
        std::string recordId = restarted.entry(0).recordId;
        SaveRecord kept;
        CHECK(records.find(0, recordId, &kept) && kept.order == order);
        CHECK(storage::move(root + "/SLOT01", outside + "-away"));
        CHECK(restarted.refresh() && !restarted.entry(0).present);
        CHECK(records.find(0, recordId, &kept));
        CHECK(storage::move(outside + "-away", root + "/SLOT01"));
        CHECK(restarted.refresh() && restarted.entry(0).order == order);
        CHECK(restarted.remove(0) && !records.find(0, recordId, &kept));
    }

    // An earlier build's CE-META.TXT: moved to the records, the file goes.
    {
        std::string legacy = root + "-legacy";
        makeDirectories(legacy + "/SLOT01");
        CHECK(storage::writeFile(legacy + "/SLOT01/SAVE.DAT", snapshot('Q')));
        SaveCatalog probe;
        probe.configure(legacy, 20, 10, 10);
        CHECK(probe.refresh());
        std::uint64_t fingerprint = probe.entry(0).identity;
        CHECK(storage::writeFile(legacy + "/SLOT01/CE-META.TXT", "1 1700000000 1700000000000123 " + std::to_string(fingerprint) + "\n"));

        SaveRecords legacyRecords;
        legacyRecords.configure(legacy + "-records.txt");
        SaveCatalog migrated;
        migrated.configure(legacy, 20, 10, 10, &legacyRecords);
        CHECK(migrated.refresh());
        CHECK(migrated.entry(0).created == 1700000000 && migrated.entry(0).order == 1700000000000123);
        CHECK(!exists(legacy + "/SLOT01/CE-META.TXT"));
        // A new records file: every save is the game's own ("*").
        CHECK(legacyRecords.find(0, migrated.entry(0).recordId, &record) && record.composition == "*");

        SaveRecords reread;
        reread.configure(legacy + "-records.txt");
        SaveCatalog again;
        again.configure(legacy, 20, 10, 10, &reread);
        CHECK(again.refresh() && again.entry(0).order == 1700000000000123);
        CHECK(storage::removeTree(legacy) && storage::removeTree(legacy + "-records.txt"));
    }

    // Any quick range, none, one clipped by the slot count.
    const std::pair<int, int> ranges[] = { { 0, 10 }, { 400, 30 }, { 990, 100 }, { 0, 0 } };
    for (const auto& range : ranges) {
        SaveCatalog other;
        other.configure(root + "/range" + std::to_string(range.first) + "-" + std::to_string(range.second), 1000, range.first, range.second);
        CHECK(other.refresh());
        CHECK(other.freeManual() == (range.first == 0 && range.second > 0 ? 10 : 0));
        CHECK(other.nextQuick() == (range.second == 0 ? -1 : range.first));
        CHECK(!other.isQuick(1000));
    }

    SaveCatalog full;
    full.configure(root, 2, 0, 1);
    CHECK(full.refresh());
    CHECK(full.freeManual() == -1 && full.copyQuick(0) == -1);

    // Saves copied from Windows may have lower case names.
    std::string lower = root + "/lower";
    makeDirectories(lower + "/slot01");
    CHECK(storage::writeFile(lower + "/slot01/save.dat", snapshot('L')));
    SaveCatalog imported;
    imported.configure(lower, 20, 0, 10);
    CHECK(imported.refresh() && imported.entry(0).identity != 0);
    CHECK(imported.nextQuick() == 1);
    CHECK(imported.copyQuick(0) == 10);
    CHECK(read(lower + "/SLOT11/save.dat") == snapshot('L'));

    // A committed write stays even when its cleanup didn't run.
    makeDirectories(lower + "/.ce-write-0/previous");
    CHECK(storage::writeFile(lower + "/.ce-write-0/previous/SAVE.DAT", snapshot('P')));
    CHECK(storage::writeFile(lower + "/.ce-write-0/committed", "1\n"));
    CHECK(imported.refresh());
    CHECK(read(lower + "/slot01/save.dat") == snapshot('L'));

    // Quick saves never replace what the game refuses.
    SaveCatalog single;
    single.configure(lower, 1, 0, 1);
    CHECK(single.refresh());
    CHECK(single.nextQuick([](int) { return false; }) == -1);
    CHECK(single.nextQuick() == 0);

    if (temporary) {
        storage::removeTree(root.substr(0, root.rfind('/')));
    }

    printf("PASS: save catalog\n");
    return EXIT_SUCCESS;
}
