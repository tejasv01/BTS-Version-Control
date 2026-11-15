#ifndef BTS_PLATFORM_H
#define BTS_PLATFORM_H

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <sys/stat.h>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <windows.h>

#ifndef S_ISLNK
#define S_ISLNK(m) (((m) & 0170000) == 0120000)
#endif
#ifndef S_IFLNK
#define S_IFLNK 0120000
#endif
#ifndef S_ISREG
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif
#ifndef fileno
#define fileno _fileno
#endif

namespace bts {
namespace platform {

inline int lstat_impl(const std::filesystem::path& p, struct stat* st) {
    if (!st) return -1;
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(p.wstring().c_str(), GetFileExInfoStandard, &data)) {
        errno = ENOENT;
        return -1;
    }
    std::memset(st, 0, sizeof(*st));
    if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
        st->st_mode = S_IFLNK | 0777;
    } else if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        st->st_mode = S_IFDIR | 0777;
    } else {
        st->st_mode = S_IFREG | 0666;
    }
    st->st_size = (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;

    ULARGE_INTEGER ull;
    ull.LowPart = data.ftLastWriteTime.dwLowDateTime;
    ull.HighPart = data.ftLastWriteTime.dwHighDateTime;
    if (ull.QuadPart >= 116444736000000000ULL) {
        st->st_mtime = static_cast<time_t>((ull.QuadPart - 116444736000000000ULL) / 10000000ULL);
    }
    return 0;
}

inline int fsync(int fd) {
    return _commit(fd);
}

inline int open_exclusive_lock(const std::filesystem::path& path) {
    return ::_wopen(path.wstring().c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE);
}

inline unsigned long get_pid() {
    return static_cast<unsigned long>(GetCurrentProcessId());
}

} // namespace platform
} // namespace bts

// Global namespace shims for seamless compatibility with ::lstat calls
inline int lstat(const wchar_t* path, struct stat* st) {
    return bts::platform::lstat_impl(path, st);
}
inline int lstat(const char* path, struct stat* st) {
    return bts::platform::lstat_impl(std::filesystem::u8path(path ? path : ""), st);
}
inline int lstat(const std::filesystem::path& path, struct stat* st) {
    return bts::platform::lstat_impl(path, st);
}

#else
#include <fcntl.h>
#include <unistd.h>

namespace bts {
namespace platform {

inline int fsync(int fd) {
    return ::fsync(fd);
}

inline int open_exclusive_lock(const std::filesystem::path& path) {
    return ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
}

inline unsigned long get_pid() {
    return static_cast<unsigned long>(::getpid());
}

} // namespace platform
} // namespace bts

#endif

#endif // BTS_PLATFORM_H
