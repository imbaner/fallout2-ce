#include "import_archive.h"

#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <unordered_map>

#include <archive.h>
#include <archive_entry.h>
#include <zlib.h>

#include "infback9.h"

namespace fallout {

namespace {

    constexpr size_t kBufferSize = 1024 * 1024;

    // Reads [size] bytes at [offset]; false on an error or the file's end.
    bool readAt(int fd, uint64_t offset, void* buffer, size_t size)
    {
        uint8_t* bytes = static_cast<uint8_t*>(buffer);
        while (size > 0) {
            ssize_t read = pread(fd, bytes, size, static_cast<off_t>(offset));
            if (read < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return false;
            }
            if (read == 0) {
                return false;
            }
            bytes += read;
            size -= static_cast<size_t>(read);
            offset += static_cast<uint64_t>(read);
        }
        return true;
    }

    uint16_t le16(const uint8_t* bytes)
    {
        return static_cast<uint16_t>(bytes[0] | (bytes[1] << 8));
    }

    uint32_t le32(const uint8_t* bytes)
    {
        return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) | (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
    }

    uint64_t le64(const uint8_t* bytes)
    {
        return static_cast<uint64_t>(le32(bytes)) | (static_cast<uint64_t>(le32(bytes + 4)) << 32);
    }

    // RAR 5's variable length numbers: 7 bits a byte, the high bit - more.
    bool readVint(const uint8_t*& bytes, const uint8_t* end, uint64_t* value)
    {
        *value = 0;
        for (int shift = 0; shift < 64 && bytes < end; shift += 7) {
            uint8_t byte = *bytes++;
            *value |= static_cast<uint64_t>(byte & 0x7F) << shift;
            if ((byte & 0x80) == 0) {
                return true;
            }
        }
        return false;
    }

    bool isValidUtf8(const std::string& text)
    {
        size_t index = 0;
        while (index < text.size()) {
            uint8_t byte = static_cast<uint8_t>(text[index]);
            size_t length;
            if (byte < 0x80) {
                length = 1;
            } else if (byte >= 0xC2 && byte <= 0xDF) {
                length = 2;
            } else if (byte >= 0xE0 && byte <= 0xEF) {
                length = 3;
            } else if (byte >= 0xF0 && byte <= 0xF4) {
                length = 4;
            } else {
                return false;
            }
            if (index + length > text.size()) {
                return false;
            }
            for (size_t next = 1; next < length; next++) {
                if ((static_cast<uint8_t>(text[index + next]) & 0xC0) != 0x80) {
                    return false;
                }
            }
            index += length;
        }
        return true;
    }

    void appendUtf8(std::string& text, uint32_t code)
    {
        if (code < 0x80) {
            text += static_cast<char>(code);
        } else if (code < 0x800) {
            text += static_cast<char>(0xC0 | (code >> 6));
            text += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            text += static_cast<char>(0xE0 | (code >> 12));
            text += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            text += static_cast<char>(0x80 | (code & 0x3F));
        }
    }

    // CP866's 0xB0-0xDF (box drawing) and 0xF0-0xFF.
    const uint16_t kCp866Box[48] = {
        0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556, 0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
        0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F, 0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
        0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B, 0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580
    };
    const uint16_t kCp866Tail[16] = {
        0x0401, 0x0451, 0x0404, 0x0454, 0x0407, 0x0457, 0x040E, 0x045E, 0x00B0, 0x2219, 0x00B7, 0x221A, 0x2116, 0x00A4, 0x25A0, 0x00A0
    };

    uint32_t cp866(uint8_t byte)
    {
        if (byte < 0x80) {
            return byte;
        }
        if (byte < 0xB0) {
            // А-Я, а-п.
            return 0x0410 + (byte - 0x80);
        }
        if (byte < 0xE0) {
            return kCp866Box[byte - 0xB0];
        }
        if (byte < 0xF0) {
            // р-я.
            return 0x0440 + (byte - 0xE0);
        }
        return kCp866Tail[byte - 0xF0];
    }

    // An archive's path as the importer wants it: "/" between folders, no
    // leading "/" or "./".
    std::string normalizePath(std::string path)
    {
        std::replace(path.begin(), path.end(), '\\', '/');
        while (true) {
            if (path.compare(0, 1, "/") == 0) {
                path.erase(0, 1);
            } else if (path.compare(0, 2, "./") == 0) {
                path.erase(0, 2);
            } else {
                break;
            }
        }
        return path;
    }

} // namespace

std::string importArchiveDecodeName(const std::string& raw)
{
    if (isValidUtf8(raw)) {
        return raw;
    }

    std::string text;
    text.reserve(raw.size() * 2);
    for (char ch : raw) {
        appendUtf8(text, cp866(static_cast<uint8_t>(ch)));
    }
    return text;
}

ImportArchive::ImportArchive(int fd, uint64_t size)
    : _fd(fd)
    , _size(size)
{
}

ImportArchive::~ImportArchive()
{
    close(_fd);
}

bool ImportArchive::fail(ImportArchiveError error, const std::string& message)
{
    _error = error;
    _message = message;
    return false;
}

namespace {

    // .zip, read here: its central directory (zip64 too, after a
    // self-extracting program as well), stored, Deflate and Deflate64 (zlib's
    // inflateBack9) data, checked by its CRC.
    class ZipArchive final : public ImportArchive {
    public:
        ZipArchive(int fd, uint64_t size)
            : ImportArchive(fd, size)
        {
        }

        // False: not a .zip (`error` kUnreadable, message empty) or a broken
        // or split one.
        bool parse();

        bool read(const std::vector<size_t>& indices, ImportArchiveSink& sink) override;

    private:
        struct Item {
            uint64_t localOffset;
            uint64_t compressedSize;
            uint32_t crc;
            uint16_t method;
        };

        bool findEnd(uint64_t* directoryOffset, uint64_t* directorySize, uint64_t* count);
        bool readItem(size_t index, ImportArchiveSink& sink);
        bool readStored(size_t index, uint64_t offset, ImportArchiveSink& sink, uint32_t* crc, uint64_t* written);
        bool readDeflated(size_t index, uint64_t offset, ImportArchiveSink& sink, uint32_t* crc, uint64_t* written);
        bool readDeflated64(size_t index, uint64_t offset, ImportArchiveSink& sink, uint32_t* crc, uint64_t* written);

        std::vector<Item> _items;
        // Where the archive starts in the file (a program before it).
        uint64_t _base = 0;
        std::vector<uint8_t> _input;
        std::vector<uint8_t> _output;
    };

    bool ZipArchive::findEnd(uint64_t* directoryOffset, uint64_t* directorySize, uint64_t* count)
    {
        // The end record: 22 bytes and a comment of up to 64 KB at the end.
        size_t tailSize = static_cast<size_t>(std::min<uint64_t>(_size, 22 + 65535));
        if (tailSize < 22) {
            return fail(ImportArchiveError::kUnreadable, "");
        }
        std::vector<uint8_t> tail(tailSize);
        uint64_t tailOffset = _size - tailSize;
        if (!readAt(_fd, tailOffset, tail.data(), tailSize)) {
            return fail(ImportArchiveError::kUnreadable, std::string("can't read: ") + strerror(errno));
        }

        size_t end = tailSize - 22 + 1;
        const uint8_t* record = nullptr;
        while (end-- > 0) {
            if (le32(&tail[end]) == 0x06054B50 && end + 22 + le16(&tail[end + 20]) <= tailSize) {
                record = &tail[end];
                break;
            }
        }
        if (record == nullptr) {
            return fail(ImportArchiveError::kUnreadable, "");
        }
        uint64_t recordOffset = tailOffset + end;

        uint16_t disk = le16(record + 4);
        uint16_t directoryDisk = le16(record + 6);
        *count = le16(record + 10);
        *directorySize = le32(record + 12);
        *directoryOffset = le32(record + 16);

        // Zip64: its end record, found through the locator before this one.
        if (*count == 0xFFFF || *directorySize == 0xFFFFFFFF || *directoryOffset == 0xFFFFFFFF) {
            uint8_t locator[20];
            if (recordOffset < 20 || !readAt(_fd, recordOffset - 20, locator, sizeof(locator)) || le32(locator) != 0x07064B50) {
                return fail(ImportArchiveError::kCorrupt, "zip64 end locator missing");
            }
            if (le32(locator + 16) > 1) {
                return fail(ImportArchiveError::kMultiVolume, "zip64 archive of several disks");
            }
            uint64_t zip64Offset = le64(locator + 8);
            uint8_t zip64[56];
            // After a program before the archive the offset is off by its
            // size: the record is right before the locator.
            if (!readAt(_fd, zip64Offset, zip64, sizeof(zip64)) || le32(zip64) != 0x06064B50) {
                if (recordOffset < 20 + 56 || !readAt(_fd, recordOffset - 20 - 56, zip64, sizeof(zip64)) || le32(zip64) != 0x06064B50) {
                    return fail(ImportArchiveError::kCorrupt, "zip64 end record missing");
                }
                recordOffset = recordOffset - 20 - 56;
            } else {
                recordOffset = zip64Offset;
            }
            disk = static_cast<uint16_t>(std::min<uint32_t>(le32(zip64 + 16), 0xFFFF));
            directoryDisk = static_cast<uint16_t>(std::min<uint32_t>(le32(zip64 + 20), 0xFFFF));
            *count = le64(zip64 + 32);
            *directorySize = le64(zip64 + 40);
            *directoryOffset = le64(zip64 + 48);
        }

        if (disk != 0 || directoryDisk != 0) {
            return fail(ImportArchiveError::kMultiVolume, "zip archive of several disks");
        }

        // The directory ends where the end record starts; a program before
        // the archive moves everything by its size.
        if (*directorySize > recordOffset) {
            return fail(ImportArchiveError::kCorrupt, "zip directory larger than the file");
        }
        uint64_t actualOffset = recordOffset - *directorySize;
        if (actualOffset < *directoryOffset) {
            return fail(ImportArchiveError::kMultiVolume, "zip directory outside the file");
        }
        _base = actualOffset - *directoryOffset;
        *directoryOffset = actualOffset;
        return true;
    }

    bool ZipArchive::parse()
    {
        uint64_t directoryOffset;
        uint64_t directorySize;
        uint64_t count;
        if (!findEnd(&directoryOffset, &directorySize, &count)) {
            return false;
        }

        if (directorySize > 512ull * 1024 * 1024 || count > directorySize / 46 + 1) {
            return fail(ImportArchiveError::kCorrupt, "zip directory too large");
        }
        std::vector<uint8_t> directory(static_cast<size_t>(directorySize));
        if (!readAt(_fd, directoryOffset, directory.data(), directory.size())) {
            return fail(ImportArchiveError::kCorrupt, "can't read the zip directory");
        }

        size_t position = 0;
        for (uint64_t number = 0; number < count; number++) {
            if (position + 46 > directory.size() || le32(&directory[position]) != 0x02014B50) {
                return fail(ImportArchiveError::kCorrupt, "broken zip directory");
            }
            const uint8_t* header = &directory[position];
            uint16_t madeBy = le16(header + 4);
            uint16_t flags = le16(header + 8);
            uint16_t method = le16(header + 10);
            uint32_t crc = le32(header + 16);
            uint64_t compressedSize = le32(header + 20);
            uint64_t size = le32(header + 24);
            uint16_t nameLength = le16(header + 28);
            uint16_t extraLength = le16(header + 30);
            uint16_t commentLength = le16(header + 32);
            uint32_t attributes = le32(header + 38);
            uint64_t localOffset = le32(header + 42);
            size_t next = position + 46 + nameLength + extraLength + commentLength;
            if (next > directory.size()) {
                return fail(ImportArchiveError::kCorrupt, "broken zip directory");
            }

            std::string rawName(reinterpret_cast<const char*>(header + 46), nameLength);
            std::string name;

            // Extra fields: zip64 sizes and offset (those set to all ones,
            // in this order), the UTF-8 name of Info-ZIP's tools.
            const uint8_t* extra = header + 46 + nameLength;
            const uint8_t* extraEnd = extra + extraLength;
            while (extra + 4 <= extraEnd) {
                uint16_t id = le16(extra);
                uint16_t length = le16(extra + 2);
                const uint8_t* data = extra + 4;
                if (data + length > extraEnd) {
                    break;
                }
                if (id == 0x0001) {
                    const uint8_t* field = data;
                    const uint8_t* fieldEnd = data + length;
                    if (size == 0xFFFFFFFF && field + 8 <= fieldEnd) {
                        size = le64(field);
                        field += 8;
                    }
                    if (compressedSize == 0xFFFFFFFF && field + 8 <= fieldEnd) {
                        compressedSize = le64(field);
                        field += 8;
                    }
                    if (localOffset == 0xFFFFFFFF && field + 8 <= fieldEnd) {
                        localOffset = le64(field);
                    }
                } else if (id == 0x7075 && length >= 5 && data[0] == 1) {
                    uint32_t nameCrc = le32(data + 1);
                    if (nameCrc == crc32(0, header + 46, nameLength)) {
                        name.assign(reinterpret_cast<const char*>(data + 5), length - 5);
                    }
                }
                extra = data + length;
            }

            if (name.empty()) {
                // Bit 11: the name is UTF-8.
                name = (flags & 0x0800) != 0 ? rawName : importArchiveDecodeName(rawName);
            }
            position = next;

            bool folder = !rawName.empty() && (rawName.back() == '/' || rawName.back() == '\\');
            // Links of Unix tools (the file type in the high attributes).
            bool link = (madeBy >> 8) == 3 && ((attributes >> 16) & 0170000) == 0120000;
            if (folder || link) {
                continue;
            }

            ImportArchiveEntry entry;
            entry.path = normalizePath(name);
            entry.size = size;
            // Bit 0: encrypted; 99: WinZip's AES.
            entry.encrypted = (flags & 0x0001) != 0 || method == 99;
            entry.supported = !entry.encrypted && (method == 0 || method == 8 || method == 9);
            _entries.push_back(entry);
            _items.push_back({ localOffset, compressedSize, crc, method });
        }
        return true;
    }

    bool ZipArchive::read(const std::vector<size_t>& indices, ImportArchiveSink& sink)
    {
        std::vector<size_t> ordered = indices;
        std::sort(ordered.begin(), ordered.end(), [this](size_t a, size_t b) {
            return _items[a].localOffset < _items[b].localOffset;
        });

        _input.resize(kBufferSize);
        _output.resize(kBufferSize);
        for (size_t index : ordered) {
            if (index >= _items.size()) {
                return fail(ImportArchiveError::kCorrupt, "no such entry");
            }
            if (!readItem(index, sink)) {
                return false;
            }
        }
        return true;
    }

    bool ZipArchive::readItem(size_t index, ImportArchiveSink& sink)
    {
        const ImportArchiveEntry& entry = _entries[index];
        const Item& item = _items[index];
        if (entry.encrypted) {
            return fail(ImportArchiveError::kEncrypted, entry.path);
        }
        if (!entry.supported) {
            return fail(ImportArchiveError::kUnsupported, entry.path + ": zip method " + std::to_string(item.method));
        }

        uint8_t local[30];
        uint64_t localOffset = _base + item.localOffset;
        if (!readAt(_fd, localOffset, local, sizeof(local)) || le32(local) != 0x04034B50) {
            return fail(ImportArchiveError::kCorrupt, entry.path + ": no local header");
        }
        uint64_t dataOffset = localOffset + 30 + le16(local + 26) + le16(local + 28);
        if (dataOffset + item.compressedSize > _size) {
            return fail(ImportArchiveError::kCorrupt, entry.path + ": data past the end of the file");
        }

        if (!sink.begin(index)) {
            return fail(ImportArchiveError::kStopped, entry.path);
        }

        uint32_t crc = crc32(0, nullptr, 0);
        uint64_t written = 0;
        bool ok;
        switch (item.method) {
        case 0:
            ok = readStored(index, dataOffset, sink, &crc, &written);
            break;
        case 8:
            ok = readDeflated(index, dataOffset, sink, &crc, &written);
            break;
        default:
            ok = readDeflated64(index, dataOffset, sink, &crc, &written);
            break;
        }
        if (!ok) {
            return false;
        }

        if (written != entry.size || crc != item.crc) {
            return fail(ImportArchiveError::kCorrupt, entry.path + ": wrong size or CRC");
        }
        if (!sink.end(index)) {
            return fail(ImportArchiveError::kStopped, entry.path);
        }
        return true;
    }

    bool ZipArchive::readStored(size_t index, uint64_t offset, ImportArchiveSink& sink, uint32_t* crc, uint64_t* written)
    {
        uint64_t remaining = _items[index].compressedSize;
        while (remaining > 0) {
            size_t chunk = static_cast<size_t>(std::min<uint64_t>(remaining, _input.size()));
            if (!readAt(_fd, offset, _input.data(), chunk)) {
                return fail(ImportArchiveError::kCorrupt, _entries[index].path + ": can't read");
            }
            *crc = crc32(*crc, _input.data(), static_cast<uInt>(chunk));
            if (!sink.data(_input.data(), chunk)) {
                return fail(ImportArchiveError::kStopped, _entries[index].path);
            }
            offset += chunk;
            remaining -= chunk;
            *written += chunk;
        }
        return true;
    }

    bool ZipArchive::readDeflated(size_t index, uint64_t offset, ImportArchiveSink& sink, uint32_t* crc, uint64_t* written)
    {
        const std::string& path = _entries[index].path;
        z_stream stream;
        memset(&stream, 0, sizeof(stream));
        if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
            return fail(ImportArchiveError::kCorrupt, path + ": inflateInit failed");
        }

        uint64_t remaining = _items[index].compressedSize;
        int result = Z_OK;
        bool ok = true;
        while (result != Z_STREAM_END) {
            if (stream.avail_in == 0) {
                if (remaining == 0) {
                    ok = fail(ImportArchiveError::kCorrupt, path + ": deflate data cut off");
                    break;
                }
                size_t chunk = static_cast<size_t>(std::min<uint64_t>(remaining, _input.size()));
                if (!readAt(_fd, offset, _input.data(), chunk)) {
                    ok = fail(ImportArchiveError::kCorrupt, path + ": can't read");
                    break;
                }
                offset += chunk;
                remaining -= chunk;
                stream.next_in = _input.data();
                stream.avail_in = static_cast<uInt>(chunk);
            }

            stream.next_out = _output.data();
            stream.avail_out = static_cast<uInt>(_output.size());
            result = inflate(&stream, Z_NO_FLUSH);
            if (result != Z_OK && result != Z_STREAM_END) {
                ok = fail(ImportArchiveError::kCorrupt, path + ": bad deflate data");
                break;
            }

            size_t produced = _output.size() - stream.avail_out;
            if (produced > 0) {
                *crc = crc32(*crc, _output.data(), static_cast<uInt>(produced));
                *written += produced;
                if (!sink.data(_output.data(), produced)) {
                    ok = fail(ImportArchiveError::kStopped, path);
                    break;
                }
            }
        }
        inflateEnd(&stream);
        return ok;
    }

    struct Deflate64Context {
        int fd;
        uint64_t offset;
        uint64_t remaining;
        std::vector<uint8_t>* input;
        ImportArchiveSink* sink;
        uint32_t crc;
        uint64_t written;
        bool readFailed;
        bool stopped;
    };

    unsigned deflate64In(void* descriptor, unsigned char** buffer)
    {
        Deflate64Context* context = static_cast<Deflate64Context*>(descriptor);
        size_t chunk = static_cast<size_t>(std::min<uint64_t>(context->remaining, context->input->size()));
        if (chunk == 0) {
            return 0;
        }
        if (!readAt(context->fd, context->offset, context->input->data(), chunk)) {
            context->readFailed = true;
            return 0;
        }
        context->offset += chunk;
        context->remaining -= chunk;
        *buffer = context->input->data();
        return static_cast<unsigned>(chunk);
    }

    int deflate64Out(void* descriptor, unsigned char* buffer, unsigned length)
    {
        Deflate64Context* context = static_cast<Deflate64Context*>(descriptor);
        context->crc = crc32(context->crc, buffer, length);
        context->written += length;
        if (!context->sink->data(buffer, length)) {
            context->stopped = true;
            return 1;
        }
        return 0;
    }

    bool ZipArchive::readDeflated64(size_t index, uint64_t offset, ImportArchiveSink& sink, uint32_t* crc, uint64_t* written)
    {
        const std::string& path = _entries[index].path;
        std::vector<unsigned char> window(65536);
        z_stream stream;
        memset(&stream, 0, sizeof(stream));
        if (inflateBack9Init(&stream, window.data()) != Z_OK) {
            return fail(ImportArchiveError::kCorrupt, path + ": inflateBack9Init failed");
        }

        Deflate64Context context = { _fd, offset, _items[index].compressedSize, &_input, &sink, *crc, *written, false, false };
        stream.next_in = Z_NULL;
        stream.avail_in = 0;
        int result = inflateBack9(&stream, deflate64In, &context, deflate64Out, &context);
        inflateBack9End(&stream);

        *crc = context.crc;
        *written = context.written;
        if (context.stopped) {
            return fail(ImportArchiveError::kStopped, path);
        }
        if (result != Z_STREAM_END || context.readFailed) {
            return fail(ImportArchiveError::kCorrupt, path + ": bad deflate64 data");
        }
        return true;
    }

    // libarchive converts names to the locale's character set: UTF-8 for
    // the calling thread while it works (the process' locale stays).
    class Utf8Locale {
    public:
        Utf8Locale()
        {
            static const char* const kNames[] = { "C.UTF-8", "UTF-8", "en_US.UTF-8" };
            for (const char* name : kNames) {
                _locale = newlocale(LC_CTYPE_MASK, name, static_cast<locale_t>(0));
                if (_locale != static_cast<locale_t>(0)) {
                    _previous = uselocale(_locale);
                    break;
                }
            }
        }

        ~Utf8Locale()
        {
            if (_locale != static_cast<locale_t>(0)) {
                uselocale(_previous);
                freelocale(_locale);
            }
        }

    private:
        locale_t _locale = static_cast<locale_t>(0);
        locale_t _previous = static_cast<locale_t>(0);
    };

    // .7z, .rar, .tar and the rest through libarchive: each pass (listing,
    // reading) a new reader from the file's start; entries are matched by
    // their place in the archive.
    class LibArchive final : public ImportArchive {
    public:
        LibArchive(int fd, uint64_t size)
            : ImportArchive(fd, size)
        {
        }

        bool list(const ImportArchiveProgress& progress);

        bool read(const std::vector<size_t>& indices, ImportArchiveSink& sink) override;

    private:
        struct Io {
            int fd;
            uint64_t size;
            uint64_t offset;
            std::vector<uint8_t> buffer;
            const ImportArchiveProgress* progress;
            bool cancelled;
        };

        static la_ssize_t readCallback(struct archive* archive, void* data, const void** buffer);
        static la_int64_t seekCallback(struct archive* archive, void* data, la_int64_t offset, int whence);
        static la_int64_t skipCallback(struct archive* archive, void* data, la_int64_t request);

        struct archive* begin(Io& io);
        bool failWith(struct archive* archive, const std::string& path);
        // Reads the entries of [wanted] (header's place -> entry) in one pass.
        bool readPass(const std::unordered_map<uint64_t, size_t>& wanted, ImportArchiveSink& sink);

        // For each entry: the place in the archive of the header with its
        // data (a hard link's: its file's).
        std::vector<uint64_t> _headers;
    };

    la_ssize_t LibArchive::readCallback(struct archive* archive, void* data, const void** buffer)
    {
        Io* io = static_cast<Io*>(data);
        if (io->progress != nullptr && *io->progress && !(*io->progress)(io->offset, io->size)) {
            io->cancelled = true;
            archive_set_error(archive, ECANCELED, "cancelled");
            return -1;
        }

        size_t chunk = static_cast<size_t>(std::min<uint64_t>(io->size - std::min(io->offset, io->size), io->buffer.size()));
        if (chunk == 0) {
            return 0;
        }
        while (true) {
            ssize_t read = pread(io->fd, io->buffer.data(), chunk, static_cast<off_t>(io->offset));
            if (read < 0 && errno == EINTR) {
                continue;
            }
            if (read < 0) {
                archive_set_error(archive, errno, "can't read: %s", strerror(errno));
                return -1;
            }
            io->offset += static_cast<uint64_t>(read);
            *buffer = io->buffer.data();
            return read;
        }
    }

    la_int64_t LibArchive::seekCallback(struct archive* archive, void* data, la_int64_t offset, int whence)
    {
        Io* io = static_cast<Io*>(data);
        int64_t base;
        switch (whence) {
        case SEEK_SET:
            base = 0;
            break;
        case SEEK_CUR:
            base = static_cast<int64_t>(io->offset);
            break;
        case SEEK_END:
            base = static_cast<int64_t>(io->size);
            break;
        default:
            return ARCHIVE_FATAL;
        }
        int64_t target = base + offset;
        if (target < 0) {
            return ARCHIVE_FATAL;
        }
        io->offset = static_cast<uint64_t>(target);
        return target;
    }

    la_int64_t LibArchive::skipCallback(struct archive* archive, void* data, la_int64_t request)
    {
        Io* io = static_cast<Io*>(data);
        if (request <= 0) {
            return 0;
        }
        uint64_t skipped = std::min<uint64_t>(static_cast<uint64_t>(request), io->size - std::min(io->offset, io->size));
        io->offset += skipped;
        return static_cast<la_int64_t>(skipped);
    }

    struct archive* LibArchive::begin(Io& io)
    {
        struct archive* archive = archive_read_new();
        if (archive == nullptr) {
            return nullptr;
        }
        archive_read_support_format_7zip(archive);
        archive_read_support_format_rar(archive);
        archive_read_support_format_rar5(archive);
        archive_read_support_format_tar(archive);
        archive_read_support_filter_gzip(archive);
        archive_read_support_filter_xz(archive);
        archive_read_support_filter_lzma(archive);

        archive_read_set_read_callback(archive, readCallback);
        archive_read_set_seek_callback(archive, seekCallback);
        archive_read_set_skip_callback(archive, skipCallback);
        archive_read_set_callback_data(archive, &io);
        if (archive_read_open1(archive) != ARCHIVE_OK) {
            failWith(archive, "");
            archive_read_free(archive);
            return nullptr;
        }
        return archive;
    }

    bool LibArchive::failWith(struct archive* archive, const std::string& path)
    {
        const char* text = archive_error_string(archive);
        std::string message = text != nullptr ? text : "unknown error";
        if (!path.empty()) {
            message = path + ": " + message;
        }

        if (archive_errno(archive) == ECANCELED) {
            return fail(ImportArchiveError::kStopped, message);
        }
        if (archive_read_has_encrypted_entries(archive) == 1) {
            return fail(ImportArchiveError::kEncrypted, message);
        }
        if (message.find("nsupported") != std::string::npos) {
            return fail(ImportArchiveError::kUnsupported, message);
        }
        if (archive_format(archive) == 0) {
            // Nothing recognized it.
            return fail(ImportArchiveError::kUnreadable, message);
        }
        if (message.find("runcated") != std::string::npos || message.find("remature end") != std::string::npos) {
            return fail(ImportArchiveError::kMultiVolume, message);
        }
        return fail(ImportArchiveError::kCorrupt, message);
    }

    bool LibArchive::list(const ImportArchiveProgress& progress)
    {
        Utf8Locale utf8;
        Io io = { _fd, _size, 0, std::vector<uint8_t>(kBufferSize), &progress, false };
        struct archive* archive = begin(io);
        if (archive == nullptr) {
            return false;
        }

        bool ok = true;
        std::unordered_map<std::string, size_t> byPath;
        struct archive_entry* header;
        for (uint64_t number = 0;; number++) {
            int result = archive_read_next_header(archive, &header);
            if (result == ARCHIVE_EOF) {
                break;
            }
            if (result < ARCHIVE_WARN) {
                ok = failWith(archive, "");
                break;
            }

            // A hard link (tar, RAR 5) has no data of its own: its file's
            // (listed before it) is read for it. Links to anything else
            // (folders, other links) are left out, as symbolic links are.
            const char* link = archive_entry_hardlink_utf8(header);
            if (link == nullptr) {
                link = archive_entry_hardlink(header);
            }
            bool hardLink = link != nullptr && link[0] != '\0';
            if (!hardLink && archive_entry_filetype(header) != AE_IFREG) {
                continue;
            }

            // Names libarchive couldn't convert (an old RAR's DOS names are
            // given as they are) are decoded here.
            const char* name = archive_entry_pathname_utf8(header);
            if (name == nullptr) {
                name = archive_entry_pathname(header);
            }
            if (name == nullptr || name[0] == '\0') {
                ok = fail(ImportArchiveError::kCorrupt, "a name that can't be read");
                break;
            }
            std::string path = normalizePath(importArchiveDecodeName(name));

            ImportArchiveEntry entry;
            uint64_t dataHeader = number;
            if (hardLink) {
                auto target = byPath.find(normalizePath(importArchiveDecodeName(link)));
                if (target == byPath.end()) {
                    continue;
                }
                entry = _entries[target->second];
                dataHeader = _headers[target->second];
            } else {
                entry.size = archive_entry_size_is_set(header) ? static_cast<uint64_t>(std::max<la_int64_t>(archive_entry_size(header), 0)) : 0;
                entry.encrypted = archive_entry_is_data_encrypted(header) != 0;
                entry.supported = !entry.encrypted;
            }
            entry.path = path;
            byPath[path] = _entries.size();
            _entries.push_back(entry);
            _headers.push_back(dataHeader);
        }
        archive_read_free(archive);
        return ok;
    }

    bool LibArchive::read(const std::vector<size_t>& indices, ImportArchiveSink& sink)
    {
        // Entries sharing one header's data (a file and its hard links)
        // can't be read in one pass: the n-th of each in the n-th pass.
        std::unordered_map<uint64_t, std::vector<size_t>> byHeader;
        size_t passes = 0;
        for (size_t index : indices) {
            if (index >= _entries.size()) {
                return fail(ImportArchiveError::kCorrupt, "no such entry");
            }
            if (_entries[index].encrypted) {
                return fail(ImportArchiveError::kEncrypted, _entries[index].path);
            }
            std::vector<size_t>& sharing = byHeader[_headers[index]];
            sharing.push_back(index);
            passes = std::max(passes, sharing.size());
        }

        for (size_t pass = 0; pass < passes; pass++) {
            std::unordered_map<uint64_t, size_t> wanted;
            for (const auto& it : byHeader) {
                if (pass < it.second.size()) {
                    wanted[it.first] = it.second[pass];
                }
            }
            if (!readPass(wanted, sink)) {
                return false;
            }
        }
        return true;
    }

    bool LibArchive::readPass(const std::unordered_map<uint64_t, size_t>& wanted, ImportArchiveSink& sink)
    {
        Utf8Locale utf8;
        Io io = { _fd, _size, 0, std::vector<uint8_t>(kBufferSize), nullptr, false };
        struct archive* archive = begin(io);
        if (archive == nullptr) {
            return false;
        }

        std::vector<uint8_t> buffer(kBufferSize);
        size_t remaining = wanted.size();
        bool ok = true;
        struct archive_entry* header;
        for (uint64_t number = 0; ok && remaining > 0; number++) {
            int result = archive_read_next_header(archive, &header);
            if (result == ARCHIVE_EOF) {
                ok = fail(ImportArchiveError::kCorrupt, "entries missing on reading");
                break;
            }
            if (result < ARCHIVE_WARN) {
                ok = failWith(archive, "");
                break;
            }

            auto it = wanted.find(number);
            if (it == wanted.end()) {
                continue;
            }
            size_t index = it->second;
            const std::string& path = _entries[index].path;
            remaining--;

            if (!sink.begin(index)) {
                ok = fail(ImportArchiveError::kStopped, path);
                break;
            }
            while (true) {
                la_ssize_t read = archive_read_data(archive, buffer.data(), buffer.size());
                if (read == 0) {
                    break;
                }
                if (read < 0) {
                    ok = failWith(archive, path);
                    break;
                }
                if (!sink.data(buffer.data(), static_cast<size_t>(read))) {
                    ok = fail(ImportArchiveError::kStopped, path);
                    break;
                }
            }
            if (ok && !sink.end(index)) {
                ok = fail(ImportArchiveError::kStopped, path);
            }
        }
        archive_read_free(archive);
        return ok;
    }

    // Signs of a part of a split archive (the first one alone can't be
    // read): RAR's volume flag, 7z's header past the file's end, zip's
    // split marker.
    bool isVolume(int fd, uint64_t size, std::string* message)
    {
        uint8_t start[64];
        size_t length = static_cast<size_t>(std::min<uint64_t>(size, sizeof(start)));
        if (!readAt(fd, 0, start, length)) {
            return false;
        }

        if (length >= 4 && le32(start) == 0x08074B50) {
            *message = "split zip";
            return true;
        }

        static const uint8_t k7z[6] = { '7', 'z', 0xBC, 0xAF, 0x27, 0x1C };
        if (length >= 32 && memcmp(start, k7z, sizeof(k7z)) == 0) {
            uint64_t headerEnd = 32 + le64(start + 12) + le64(start + 20);
            if (headerEnd > size) {
                *message = "7z header past the end of the file";
                return true;
            }
            return false;
        }

        static const uint8_t kRar[6] = { 'R', 'a', 'r', '!', 0x1A, 0x07 };
        if (length >= 13 && memcmp(start, kRar, sizeof(kRar)) == 0) {
            if (start[6] == 0x00) {
                // RAR 4: the main header (type 0x73) right after the
                // signature, flag 0x0001 - a volume.
                if (start[9] == 0x73 && (le16(start + 10) & 0x0001) != 0) {
                    *message = "rar volume";
                    return true;
                }
            } else if (start[6] == 0x01 && start[7] == 0x00) {
                // RAR 5: CRC, header size, type 1 (main), header flags,
                // [extra size], archive flags: 0x0001 - a volume.
                const uint8_t* bytes = start + 12;
                const uint8_t* end = start + length;
                uint64_t headerSize;
                uint64_t type;
                uint64_t headerFlags;
                uint64_t extraSize;
                uint64_t archiveFlags;
                if (readVint(bytes, end, &headerSize)
                    && readVint(bytes, end, &type) && type == 1
                    && readVint(bytes, end, &headerFlags)
                    && ((headerFlags & 0x0001) == 0 || readVint(bytes, end, &extraSize))
                    && readVint(bytes, end, &archiveFlags)
                    && (archiveFlags & 0x0001) != 0) {
                    *message = "rar5 volume";
                    return true;
                }
            }
        }
        return false;
    }

} // namespace

std::unique_ptr<ImportArchive> ImportArchive::open(int fd, const ImportArchiveProgress& progress, ImportArchiveError* error, std::string* message)
{
    *error = ImportArchiveError::kNone;
    message->clear();

    off_t end = lseek(fd, 0, SEEK_END);
    if (end < 0) {
        *error = errno == ESPIPE ? ImportArchiveError::kNotSeekable : ImportArchiveError::kUnreadable;
        *message = std::string("can't seek: ") + strerror(errno);
        return nullptr;
    }
    uint64_t size = static_cast<uint64_t>(end);

    int own = fcntl(fd, F_DUPFD_CLOEXEC, 0);
    if (own < 0) {
        *error = ImportArchiveError::kUnreadable;
        *message = std::string("can't dup: ") + strerror(errno);
        return nullptr;
    }

    if (isVolume(own, size, message)) {
        close(own);
        *error = ImportArchiveError::kMultiVolume;
        return nullptr;
    }

    // What the file starts with: a zip's file or end record, or one of
    // libarchive's kinds (7z, RAR, gzip, xz, tar's "ustar"); a program
    // (self-extracting archives) or anything else - a zip is tried first
    // (its end record), then libarchive.
    uint8_t start[262] = { 0 };
    readAt(own, 0, start, static_cast<size_t>(std::min<uint64_t>(size, sizeof(start))));
    bool zipStart = le32(start) == 0x04034B50 || le32(start) == 0x06054B50;
    bool otherStart = memcmp(start, "7z\xBC\xAF\x27\x1C", 6) == 0
        || memcmp(start, "Rar!\x1A\x07", 6) == 0
        || (start[0] == 0x1F && start[1] == 0x8B)
        || memcmp(start, "\xFD"
                         "7zXZ",
               6)
            == 0
        || memcmp(start + 257, "ustar", 5) == 0;

    ImportArchiveError zipError = ImportArchiveError::kUnreadable;
    std::string zipMessage;
    if (!otherStart) {
        std::unique_ptr<ZipArchive> zip(new ZipArchive(own, size));
        if (zip->parse()) {
            return zip;
        }
        zipError = zip->error();
        zipMessage = zip->message();

        if (zipStart) {
            *error = zipError;
            *message = zipMessage;
            if (zipError == ImportArchiveError::kUnreadable && zipMessage.empty()) {
                // A zip's start without its end: cut off (a download not
                // finished).
                *error = ImportArchiveError::kMultiVolume;
                *message = "zip without its end";
            }
            return nullptr;
        }

        // ZipArchive closed its descriptor.
        own = fcntl(fd, F_DUPFD_CLOEXEC, 0);
        if (own < 0) {
            *error = ImportArchiveError::kUnreadable;
            *message = std::string("can't dup: ") + strerror(errno);
            return nullptr;
        }
    }

    std::unique_ptr<LibArchive> other(new LibArchive(own, size));
    if (!other->list(progress)) {
        *error = other->error();
        *message = other->message();
        // Not libarchive's: what was wrong with it as a zip (an end record
        // found, the rest broken) tells more.
        if (other->error() == ImportArchiveError::kUnreadable && !zipMessage.empty()) {
            *error = zipError;
            *message = zipMessage;
        }
        return nullptr;
    }
    return other;
}

} // namespace fallout
