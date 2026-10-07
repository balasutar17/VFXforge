#include "vfx/FileIO.h"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <random>
#include <system_error>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace vfx {

namespace fs = std::filesystem;

fs::path pathFromUtf8(std::string_view text) {
    // The char8_t constructor is the one that means "this is UTF-8" on every
    // platform. A plain char string would be read in the ANSI code page on
    // Windows and mangle non-English file names.
    return fs::path(std::u8string(text.begin(), text.end()));
}

std::string pathToUtf8(const fs::path& path) {
    const std::u8string s = path.u8string();
    return std::string(s.begin(), s.end());
}

namespace {

std::string randomSuffix() {
    static const char digits[] = "0123456789abcdef";
    std::random_device device;
    std::string out;
    for (int i = 0; i < 4; ++i) {
        const unsigned int v = device();
        for (int shift = 0; shift < 32; shift += 4) {
            out += digits[(v >> shift) & 0xF];
        }
    }
    return out;
}

Error saveFailed(const fs::path& path, const std::string& detail) {
    return makeError("\"" + pathToUtf8(path.filename()) +
                         "\" could not be saved. Your previous file has not been changed.",
                     detail + " (" + pathToUtf8(path) + ")");
}

#if defined(_WIN32)

std::string lastErrorText(const char* step) {
    return std::string(step) + " failed with Windows error " + std::to_string(GetLastError());
}

Status writeAndSwap(const fs::path& target, const fs::path& temp, std::string_view bytes) {
    HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return saveFailed(target, lastErrorText("CreateFileW"));
    }
    const char* data = bytes.data();
    std::size_t left = bytes.size();
    while (left > 0) {
        const DWORD chunk = static_cast<DWORD>(left > 0x40000000u ? 0x40000000u : left);
        DWORD written = 0;
        if (!WriteFile(file, data, chunk, &written, nullptr) || written == 0) {
            const std::string detail = lastErrorText("WriteFile");
            CloseHandle(file);
            DeleteFileW(temp.c_str());
            return saveFailed(target, detail);
        }
        data += written;
        left -= written;
    }
    if (!FlushFileBuffers(file)) {
        const std::string detail = lastErrorText("FlushFileBuffers");
        CloseHandle(file);
        DeleteFileW(temp.c_str());
        return saveFailed(target, detail);
    }
    CloseHandle(file);

    // ReplaceFileW keeps the original file's attributes and permissions. It
    // needs the target to exist, so a first save falls through to a move.
    if (ReplaceFileW(target.c_str(), temp.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS,
                     nullptr, nullptr)) {
        return {};
    }
    if (MoveFileExW(temp.c_str(), target.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return {};
    }
    const std::string detail = lastErrorText("MoveFileExW");
    DeleteFileW(temp.c_str());
    return saveFailed(target, detail);
}

#else

std::string errnoText(const char* step) {
    return std::string(step) + " failed: " + std::strerror(errno);
}

Status writeAndSwap(const fs::path& target, const fs::path& temp, std::string_view bytes) {
    // Match the permissions of the file being replaced, if there is one.
    mode_t mode = 0644;
    struct stat existing {};
    if (::stat(target.c_str(), &existing) == 0) {
        mode = existing.st_mode & 0777;
    }

    const int fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, mode);
    if (fd < 0) {
        return saveFailed(target, errnoText("open"));
    }
    auto fail = [&](const char* step) {
        const std::string detail = errnoText(step);
        ::close(fd);
        ::unlink(temp.c_str());
        return saveFailed(target, detail);
    };

    const char* data = bytes.data();
    std::size_t left = bytes.size();
    while (left > 0) {
        const ssize_t written = ::write(fd, data, left);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return fail("write");
        }
        data += written;
        left -= static_cast<std::size_t>(written);
    }

#if defined(__APPLE__)
    // On macOS fsync only hands the data to the drive. F_FULLFSYNC asks the
    // drive to put it on permanent storage. Some file systems do not support
    // it, so fall back to fsync.
    if (::fcntl(fd, F_FULLFSYNC) != 0 && ::fsync(fd) != 0) {
        return fail("fsync");
    }
#else
    if (::fsync(fd) != 0) {
        return fail("fsync");
    }
#endif
    if (::close(fd) != 0) {
        const std::string detail = errnoText("close");
        ::unlink(temp.c_str());
        return saveFailed(target, detail);
    }

    if (::rename(temp.c_str(), target.c_str()) != 0) {
        const std::string detail = errnoText("rename");
        ::unlink(temp.c_str());
        return saveFailed(target, detail);
    }

    // Flush the folder too so the rename itself survives a power cut. This is
    // best effort: the file is already safely in place.
    fs::path folder = target.parent_path();
    if (folder.empty()) {
        folder = ".";
    }
    const int dir = ::open(folder.c_str(), O_RDONLY | O_CLOEXEC);
    if (dir >= 0) {
        ::fsync(dir);
        ::close(dir);
    }
    return {};
}

#endif

}  // namespace

Status writeFileAtomic(const fs::path& path, std::string_view bytes) {
    if (path.empty() || !path.has_filename()) {
        return makeError("No file name was given.");
    }
    // The temporary file sits beside the target so the final swap never
    // crosses to another drive, which is what keeps it a single step.
    fs::path temp = path;
    temp += pathFromUtf8(".tmp-" + randomSuffix());
    return writeAndSwap(path, temp, bytes);
}

Result<std::string> readFile(const fs::path& path, std::size_t maxBytes) {
    const std::string shown = pathToUtf8(path.filename());
    std::error_code ec;
    if (!fs::exists(path, ec) || ec) {
        return makeError("\"" + shown + "\" could not be found.", pathToUtf8(path));
    }
    if (!fs::is_regular_file(path, ec) || ec) {
        return makeError("\"" + shown + "\" is not a file.", pathToUtf8(path));
    }
    const std::uintmax_t size = fs::file_size(path, ec);
    if (ec) {
        return makeError("\"" + shown + "\" could not be opened.", ec.message());
    }
    if (size > maxBytes) {
        return makeError("\"" + shown + "\" is too large to open.");
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return makeError("\"" + shown + "\" could not be opened.", pathToUtf8(path));
    }
    std::string bytes(static_cast<std::size_t>(size), '\0');
    in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (in.bad()) {
        return makeError("\"" + shown + "\" could not be read.", pathToUtf8(path));
    }
    bytes.resize(static_cast<std::size_t>(in.gcount()));
    return bytes;
}

Status saveEffect(const fs::path& path, const Effect& effect) {
    return writeFileAtomic(path, writeEffect(effect));
}

Result<LoadedEffect> loadEffect(const fs::path& path) {
    auto bytes = readFile(path);
    if (!bytes) {
        return bytes.error();
    }
    auto loaded = readEffect(bytes.value());
    if (!loaded) {
        Error error = loaded.error();
        error.detail = pathToUtf8(path) + ": " + error.detail;
        return error;
    }
    return loaded;
}

}  // namespace vfx
