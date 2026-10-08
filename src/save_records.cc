#include "save_records.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#include "save_storage.h"

namespace fallout {

namespace {

    constexpr const char* kHeader = "# Fallout 2 CE: when each save was made and what the game was made of then (src/save_records.h).\n";

    // "SLOT01" -> 0, -1 - not that.
    int slotOf(const std::string& name)
    {
        int number;
        char trailing;
        if (sscanf(name.c_str(), "SLOT%d%c", &number, &trailing) != 1 || number < 1) {
            return -1;
        }
        return number - 1;
    }

    std::string slotName(int slot)
    {
        char name[32];
        snprintf(name, sizeof(name), "SLOT%02d", slot + 1);
        return name;
    }

} // namespace

void SaveRecords::configure(const std::string& path)
{
    filePath = path;
    reload();
}

void SaveRecords::reload()
{
    records.clear();
    fileIsNew = false;
    if (filePath.empty()) {
        return;
    }

    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        saveStorage::Info info;
        fileIsNew = saveStorage::inspect(filePath, info) && !info.exists;
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#') {
            continue;
        }

        std::string fields[5];
        std::istringstream stream(line);
        int count = 0;
        while (count < 5 && std::getline(stream, fields[count], '\t')) {
            count++;
        }
        int slot = slotOf(fields[0]);
        if (count < 4 || slot == -1 || fields[1].empty()) {
            continue;
        }

        SaveRecord record;
        record.created = std::strtoll(fields[2].c_str(), nullptr, 10);
        record.order = std::strtoll(fields[3].c_str(), nullptr, 10);
        record.composition = count == 5 ? fields[4] : std::string();
        records[slot] = { fields[1], record };
    }
}

bool SaveRecords::find(int slot, const std::string& identity, SaveRecord* record) const
{
    auto it = records.find(slot);
    if (identity.empty() || it == records.end() || it->second.first != identity) {
        return false;
    }
    *record = it->second.second;
    return true;
}

bool SaveRecords::put(int slot, const std::string& identity, const SaveRecord& record)
{
    if (identity.empty()) {
        return false;
    }
    records[slot] = { identity, record };
    return write();
}

bool SaveRecords::remove(int slot)
{
    if (records.erase(slot) == 0) {
        return true;
    }
    return write();
}

bool SaveRecords::write()
{
    if (filePath.empty()) {
        return false;
    }

    std::string text = kHeader;
    for (const auto& it : records) {
        const SaveRecord& record = it.second.second;
        text += slotName(it.first) + "\t" + it.second.first + "\t"
            + std::to_string(record.created) + "\t" + std::to_string(record.order) + "\t"
            + record.composition + "\n";
    }

    std::string temporary = filePath + ".tmp";
    if (!saveStorage::writeFile(temporary, text)) {
        saveStorage::removeTree(temporary);
        return false;
    }
    // Renamed over the old one (Windows can't: removed first there).
    if (std::rename(temporary.c_str(), filePath.c_str()) != 0) {
        std::remove(filePath.c_str());
        if (std::rename(temporary.c_str(), filePath.c_str()) != 0) {
            return false;
        }
    }
    fileIsNew = false;
    return true;
}

std::string saveRecordId(const std::string& saveDatPath)
{
    std::ifstream file(saveDatPath, std::ios::binary);
    if (!file) {
        return std::string();
    }
    char head[4096];
    file.read(head, sizeof(head));
    std::streamsize read = file.gcount();
    file.clear();
    file.seekg(0, std::ios::end);
    long long size = static_cast<long long>(file.tellg());
    if (read <= 0 || size <= 0) {
        return std::string();
    }

    std::uint64_t hash = 14695981039346656037ULL;
    for (std::streamsize index = 0; index < read; index++) {
        hash = (hash ^ static_cast<unsigned char>(head[index])) * 1099511628211ULL;
    }

    char id[48];
    snprintf(id, sizeof(id), "%016llx-%lld", static_cast<unsigned long long>(hash), size);
    return id;
}

SaveRecords& gameSaveRecords()
{
    static SaveRecords records;
    return records;
}

} // namespace fallout
