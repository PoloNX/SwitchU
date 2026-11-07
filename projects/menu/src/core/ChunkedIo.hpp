#pragma once

#include <algorithm>
#include <cstddef>
#include <istream>
#include <ostream>

// The menu runs on a single fs session (__nx_fs_num_sessions = 1 in main.cpp),
// so one large read or write on a worker holds the SD card for its whole
// length and every UI-thread file access queues behind it: saving a 4.4 MB
// SteamGridDB logo in one write froze the HOME menu for ~575 ms. Splitting
// bulk transfers into chunks lets other requests in between, so the UI waits
// for at most one chunk (a few ms).
namespace switchu::io {

constexpr std::size_t kChunkBytes = 128 * 1024;

inline bool writeChunked(std::ostream& out, const void* data, std::size_t size) {
    const char* p = static_cast<const char*>(data);
    while (size > 0 && out) {
        const std::size_t n = std::min(size, kChunkBytes);
        out.write(p, static_cast<std::streamsize>(n));
        p += n;
        size -= n;
    }
    return static_cast<bool>(out);
}

inline bool readChunked(std::istream& in, void* data, std::size_t size) {
    char* p = static_cast<char*>(data);
    while (size > 0 && in) {
        const std::size_t n = std::min(size, kChunkBytes);
        in.read(p, static_cast<std::streamsize>(n));
        p += n;
        size -= n;
    }
    return static_cast<bool>(in);
}

} // namespace switchu::io
