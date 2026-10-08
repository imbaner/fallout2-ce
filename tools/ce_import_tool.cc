// The game import's archive reading (src/import_archive.cc) on a computer:
//   ce-import-tool list <archive>           - its files: size, flags, path
//   ce-import-tool extract <archive> <dir>  - all its files into <dir>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <string>
#include <vector>

#include "import_archive.h"

using namespace fallout;

namespace {

const char* errorName(ImportArchiveError error)
{
    switch (error) {
    case ImportArchiveError::kNone:
        return "none";
    case ImportArchiveError::kUnreadable:
        return "unreadable";
    case ImportArchiveError::kNotSeekable:
        return "not-seekable";
    case ImportArchiveError::kEncrypted:
        return "encrypted";
    case ImportArchiveError::kMultiVolume:
        return "multi-volume";
    case ImportArchiveError::kUnsupported:
        return "unsupported";
    case ImportArchiveError::kCorrupt:
        return "corrupt";
    case ImportArchiveError::kStopped:
        return "stopped";
    }
    return "?";
}

bool makeParents(const std::string& path)
{
    for (size_t slash = path.find('/', 1); slash != std::string::npos; slash = path.find('/', slash + 1)) {
        std::string folder = path.substr(0, slash);
        if (mkdir(folder.c_str(), 0755) != 0 && errno != EEXIST) {
            return false;
        }
    }
    return true;
}

class FileSink final : public ImportArchiveSink {
public:
    FileSink(const ImportArchive& archive, const std::string& root)
        : _archive(archive)
        , _root(root)
    {
    }

    bool begin(size_t index) override
    {
        const std::string& path = _archive.entries()[index].path;
        if (path.find("..") != std::string::npos) {
            fprintf(stderr, "bad path: %s\n", path.c_str());
            return false;
        }
        std::string target = _root + "/" + path;
        if (!makeParents(target)) {
            fprintf(stderr, "can't make folders for %s\n", target.c_str());
            return false;
        }
        _fd = ::open(target.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (_fd < 0) {
            fprintf(stderr, "can't write %s: %s\n", target.c_str(), strerror(errno));
            return false;
        }
        return true;
    }

    bool data(const uint8_t* bytes, size_t size) override
    {
        while (size > 0) {
            ssize_t written = write(_fd, bytes, size);
            if (written <= 0) {
                fprintf(stderr, "write failed: %s\n", strerror(errno));
                return false;
            }
            bytes += written;
            size -= static_cast<size_t>(written);
            _bytes += static_cast<uint64_t>(written);
        }
        return true;
    }

    bool end(size_t index) override
    {
        close(_fd);
        _fd = -1;
        _files++;
        return true;
    }

    uint64_t bytes() const { return _bytes; }
    size_t files() const { return _files; }

private:
    const ImportArchive& _archive;
    std::string _root;
    int _fd = -1;
    uint64_t _bytes = 0;
    size_t _files = 0;
};

double secondsSince(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 3 || (strcmp(argv[1], "list") != 0 && strcmp(argv[1], "extract") != 0) || (strcmp(argv[1], "extract") == 0 && argc < 4)) {
        fprintf(stderr, "usage: %s list <archive> | extract <archive> <dir>\n", argv[0]);
        return 2;
    }

    int fd = open(argv[2], O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "can't open %s: %s\n", argv[2], strerror(errno));
        return 1;
    }

    auto start = std::chrono::steady_clock::now();
    ImportArchiveError error;
    std::string message;
    std::unique_ptr<ImportArchive> archive = ImportArchive::open(fd, nullptr, &error, &message);
    close(fd);
    if (archive == nullptr) {
        printf("open failed: %s (%s)\n", errorName(error), message.c_str());
        return 1;
    }
    double listed = secondsSince(start);

    if (strcmp(argv[1], "list") == 0) {
        for (const ImportArchiveEntry& entry : archive->entries()) {
            printf("%12llu %c%c %s\n", static_cast<unsigned long long>(entry.size), entry.encrypted ? 'E' : '-', entry.supported ? '-' : 'U', entry.path.c_str());
        }
        printf("%zu files, listed in %.2f s\n", archive->entries().size(), listed);
        return 0;
    }

    std::vector<size_t> indices;
    for (size_t index = 0; index < archive->entries().size(); index++) {
        indices.push_back(index);
    }
    FileSink sink(*archive, argv[3]);
    start = std::chrono::steady_clock::now();
    bool ok = archive->read(indices, sink);
    double seconds = secondsSince(start);
    printf("listed in %.2f s; %zu files, %llu bytes in %.2f s\n", listed, sink.files(), static_cast<unsigned long long>(sink.bytes()), seconds);
    if (!ok) {
        printf("read failed: %s (%s)\n", errorName(archive->error()), archive->message().c_str());
        return 1;
    }
    return 0;
}
