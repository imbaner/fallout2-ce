#include "save_storage.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <windows.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace fallout::saveStorage {

namespace {

    bool syncFile(FILE* stream)
    {
        if (fflush(stream) != 0) {
            return false;
        }

#ifdef _WIN32
        return _commit(_fileno(stream)) == 0;
#else
        return fsync(fileno(stream)) == 0;
#endif
    }

} // namespace

bool inspect(const std::string& path, Info& info)
{
    info = Info();

#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &data)) {
        DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
    }

    info.exists = true;
    bool link = (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    info.directory = !link && (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    info.regular = !link && !info.directory;
    info.size = (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;

    // 100 ns ticks since 1601.
    std::uint64_t ticks = (static_cast<std::uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime;
    info.modified = static_cast<std::int64_t>(ticks / 10000000) - 11644473600LL;
    info.nanos = static_cast<std::int64_t>(ticks % 10000000) * 100;
#else
    struct stat data;
    if (lstat(path.c_str(), &data) != 0) {
        return errno == ENOENT;
    }

    info.exists = true;
    info.directory = S_ISDIR(data.st_mode);
    info.regular = S_ISREG(data.st_mode);
    info.size = data.st_size;
    info.modified = data.st_mtime;
#ifdef __APPLE__
    info.nanos = data.st_mtimespec.tv_nsec;
#else
    info.nanos = data.st_mtim.tv_nsec;
#endif
#endif

    return true;
}

bool makeDirectory(const std::string& path)
{
#ifdef _WIN32
    if (_mkdir(path.c_str()) == 0) {
        return true;
    }
#else
    // As the game's own directories (`compat_mkdir`): others may read them
    // (on Android: adb, for backups and checking saves).
    if (mkdir(path.c_str(), 0755) == 0) {
        return true;
    }
#endif

    Info info;
    return errno == EEXIST && inspect(path, info) && info.directory;
}

void openDirectories(const std::string& path)
{
#ifndef _WIN32
    struct stat st;
    if (lstat(path.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
        return;
    }

    if ((st.st_mode & 0755) != 0755) {
        chmod(path.c_str(), (st.st_mode & 07777) | 0755);
    }

    std::vector<std::string> names;
    if (children(path, names)) {
        for (const std::string& name : names) {
            openDirectories(path + "/" + name);
        }
    }
#endif
}

bool children(const std::string& path, std::vector<std::string>& names)
{
    names.clear();

#ifdef _WIN32
    WIN32_FIND_DATAA data;
    HANDLE handle = FindFirstFileA((path + "/*").c_str(), &data);
    if (handle == INVALID_HANDLE_VALUE) {
        return GetLastError() == ERROR_FILE_NOT_FOUND;
    }

    do {
        if (strcmp(data.cFileName, ".") != 0 && strcmp(data.cFileName, "..") != 0) {
            names.emplace_back(data.cFileName);
        }
    } while (FindNextFileA(handle, &data));

    bool listed = GetLastError() == ERROR_NO_MORE_FILES;
    FindClose(handle);
    return listed;
#else
    DIR* directory = opendir(path.c_str());
    if (directory == nullptr) {
        return false;
    }

    bool listed;
    while (true) {
        errno = 0;
        struct dirent* entry = readdir(directory);
        if (entry == nullptr) {
            listed = errno == 0;
            break;
        }

        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            names.emplace_back(entry->d_name);
        }
    }

    if (closedir(directory) != 0) {
        listed = false;
    }

    return listed;
#endif
}

bool removeTree(const std::string& path)
{
    Info info;
    if (!inspect(path, info)) {
        return false;
    }

    if (!info.exists) {
        return true;
    }

    if (info.directory) {
        std::vector<std::string> names;
        if (!children(path, names)) {
            return false;
        }

        for (const std::string& name : names) {
            if (!removeTree(path + "/" + name)) {
                return false;
            }
        }

#ifdef _WIN32
        return RemoveDirectoryA(path.c_str()) != 0;
#else
        return rmdir(path.c_str()) == 0;
#endif
    }

#ifdef _WIN32
    // A directory junction is removed as a directory, not entered.
    DWORD attributes = GetFileAttributesA(path.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return RemoveDirectoryA(path.c_str()) != 0;
    }
#endif

    return ::remove(path.c_str()) == 0;
}

bool move(const std::string& from, const std::string& to)
{
    Info target;
    return inspect(to, target) && !target.exists && rename(from.c_str(), to.c_str()) == 0;
}

bool copyTree(const std::string& from, const std::string& to)
{
    Info info;
    if (!inspect(from, info)) {
        return false;
    }

    if (info.directory) {
        if (!makeDirectory(to)) {
            return false;
        }

        std::vector<std::string> names;
        if (!children(from, names)) {
            return false;
        }

        for (const std::string& name : names) {
            if (!copyTree(from + "/" + name, to + "/" + name)) {
                return false;
            }
        }

        return true;
    }

    if (!info.regular) {
        return false;
    }

    FILE* source = fopen(from.c_str(), "rb");
    if (source == nullptr) {
        return false;
    }

    FILE* target = fopen(to.c_str(), "wb");
    if (target == nullptr) {
        fclose(source);
        return false;
    }

    char buffer[65536];
    size_t count;
    bool copied = true;
    while ((count = fread(buffer, 1, sizeof(buffer), source)) > 0) {
        if (fwrite(buffer, 1, count, target) != count) {
            copied = false;
            break;
        }
    }

    copied = copied && ferror(source) == 0 && syncFile(target);
    if (fclose(source) != 0) {
        copied = false;
    }
    if (fclose(target) != 0) {
        copied = false;
    }

    return copied;
}

bool writeFile(const std::string& path, const std::string& value)
{
    FILE* stream = fopen(path.c_str(), "wb");
    if (stream == nullptr) {
        return false;
    }

    bool written = fwrite(value.data(), 1, value.size(), stream) == value.size() && syncFile(stream);
    return fclose(stream) == 0 && written;
}

bool syncDirectory(const std::string& path)
{
#ifdef _WIN32
    // Files are committed one by one; directory handles vary by file system.
    return true;
#else
    int fd = open(path.c_str(), O_RDONLY | O_DIRECTORY);
    if (fd < 0) {
        return false;
    }

    bool synced = fsync(fd) == 0;

    // Android's emulated storage doesn't sync directories.
    if (!synced && (errno == EINVAL || errno == ENOTSUP)) {
        synced = true;
    }

    if (close(fd) != 0) {
        synced = false;
    }

    return synced;
#endif
}

bool syncTree(const std::string& path)
{
    Info info;
    if (!inspect(path, info)) {
        return false;
    }

    if (info.directory) {
        std::vector<std::string> names;
        if (!children(path, names)) {
            return false;
        }

        for (const std::string& name : names) {
            if (!syncTree(path + "/" + name)) {
                return false;
            }
        }

        return syncDirectory(path);
    }

    if (!info.regular) {
        return false;
    }

    FILE* stream = fopen(path.c_str(), "rb+");
    if (stream == nullptr) {
        return false;
    }

    bool synced = syncFile(stream);
    return fclose(stream) == 0 && synced;
}

} // namespace fallout::saveStorage
