#ifndef FALLOUT_IMPORT_ARCHIVE_H_
#define FALLOUT_IMPORT_ARCHIVE_H_

#include <stddef.h>
#include <stdint.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace fallout {

// The archive the player chose for the game's import (Android: GameImport,
// through import_archive_jni.cc; tools/ce_import_tool.cc on a computer):
// .zip read here (stored, Deflate, Windows' Deflate64 of big archives), .7z,
// .rar (4 and 5), .tar(.gz/.xz) through libarchive.

enum class ImportArchiveError {
    kNone,
    // Not an archive of these kinds, or it can't be read.
    kUnreadable,
    // Not a file that can be read in any order (a pipe of some providers).
    kNotSeekable,
    kEncrypted,
    // A part of a split archive, or a cut off one.
    kMultiVolume,
    // A compression method that isn't supported.
    kUnsupported,
    // Damaged data.
    kCorrupt,
    // Stopped by the sink (cancelled, or it couldn't write).
    kStopped,
};

struct ImportArchiveEntry {
    // UTF-8, "/" between folders, from the archive's root.
    std::string path;
    // 0 when the archive doesn't tell.
    uint64_t size;
    bool encrypted;
    // False - its compression method isn't supported (known for .zip only:
    // other kinds tell it when read).
    bool supported;
};

// What is read: begin, the data in pieces, end, for each entry. False from
// any of them stops the reading.
class ImportArchiveSink {
public:
    virtual ~ImportArchiveSink() = default;
    virtual bool begin(size_t index) = 0;
    virtual bool data(const uint8_t* bytes, size_t size) = 0;
    virtual bool end(size_t index) = 0;
};

// While an archive is opened (its names are read; some kinds read through
// the whole file for that): bytes of the file read so far, its size. False
// stops (cancel).
using ImportArchiveProgress = std::function<bool(uint64_t done, uint64_t total)>;

class ImportArchive {
public:
    // Opens the archive in [fd] (duplicated: the caller keeps its own) and
    // lists its files (not folders or links). Null on failure, [error] and
    // [message] (details for the log, in English) tell why.
    static std::unique_ptr<ImportArchive> open(int fd, const ImportArchiveProgress& progress, ImportArchiveError* error, std::string* message);

    virtual ~ImportArchive();

    const std::vector<ImportArchiveEntry>& entries() const { return _entries; }

    // Reads the entries of [indices] (of `entries`) into [sink], in the
    // archive's own order (the only one solid archives can be read in).
    // False on failure: `error` and `message` tell why.
    virtual bool read(const std::vector<size_t>& indices, ImportArchiveSink& sink) = 0;

    ImportArchiveError error() const { return _error; }
    const std::string& message() const { return _message; }

protected:
    explicit ImportArchive(int fd, uint64_t size);

    bool fail(ImportArchiveError error, const std::string& message);

    int _fd;
    uint64_t _size;
    std::vector<ImportArchiveEntry> _entries;
    ImportArchiveError _error = ImportArchiveError::kNone;
    std::string _message;
};

// An archive's name as UTF-8: UTF-8 when it is valid UTF-8, else the DOS code
// page of Russian Windows (CP866), which zips and old RARs made there use.
std::string importArchiveDecodeName(const std::string& raw);

} // namespace fallout

#endif /* FALLOUT_IMPORT_ARCHIVE_H_ */
