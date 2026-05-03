#include "sandboxed-fs/vfs_real.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace sandboxed_fs {

static void throwErrno(int err, const std::string& msg) {
    throw VFSError(err, msg + ": " + std::strerror(err));
}

static Stat makeStat(const struct stat& st) {
    Stat s;
    s.dev = st.st_dev;
    s.ino = st.st_ino;
    s.mode = st.st_mode;
    s.nlink = st.st_nlink;
    s.uid = st.st_uid;
    s.gid = st.st_gid;
    s.rdev = st.st_rdev;
    s.size = st.st_size;
    s.blksize = st.st_blksize;
    s.blocks = st.st_blocks;
#if defined(__APPLE__)
    s.atimeMs = static_cast<int64_t>(st.st_atimespec.tv_sec) * 1000 +
                st.st_atimespec.tv_nsec / 1000000;
    s.mtimeMs = static_cast<int64_t>(st.st_mtimespec.tv_sec) * 1000 +
                st.st_mtimespec.tv_nsec / 1000000;
    s.ctimeMs = static_cast<int64_t>(st.st_ctimespec.tv_sec) * 1000 +
                st.st_ctimespec.tv_nsec / 1000000;
#else
    s.atimeMs = static_cast<int64_t>(st.st_atim.tv_sec) * 1000 +
                st.st_atim.tv_nsec / 1000000;
    s.mtimeMs = static_cast<int64_t>(st.st_mtim.tv_sec) * 1000 +
                st.st_mtim.tv_nsec / 1000000;
    s.ctimeMs = static_cast<int64_t>(st.st_ctim.tv_sec) * 1000 +
                st.st_ctim.tv_nsec / 1000000;
#endif
    return s;
}

static bool pathStartsWith(const fs::path& path, const fs::path& prefix) {
    auto [mm, _] = std::mismatch(prefix.begin(), prefix.end(), path.begin(), path.end());
    return mm == prefix.end();
}

static std::string_view stripLeadingSlash(const std::string& path) {
    std::string_view sv = path;
    while (!sv.empty() && sv.front() == '/') sv = sv.substr(1);
    return sv;
}

RealFSBackend::RealFSBackend(fs::path realRoot) {
    std::error_code ec;
    m_root = fs::canonical(fs::absolute(realRoot), ec);
    if (ec) {
        throw VFSError(ec.value(),
                       "RealFSBackend: cannot canonicalize root '" + realRoot.string() +
                           "': " + ec.message());
    }
}

fs::path RealFSBackend::makePath(const std::string& path) const {
    if (path.empty() || path == "/") return m_root;
    auto rel = stripLeadingSlash(path);
    if (rel.empty()) return m_root;
    return m_root / rel;
}

std::string RealFSBackend::resolveExisting(const std::string& path) {
    auto fullPath = makePath(path);
    std::error_code ec;
    auto canonical = fs::canonical(fullPath, ec);
    if (ec) {
        throw VFSError(ec.value(),
                       "RealFSBackend: '" + path + "': " + ec.message());
    }
    if (!pathStartsWith(canonical, m_root)) {
        throw VFSError(EACCES,
                       "RealFSBackend: path escapes sandbox: '" + path + "'");
    }
    return canonical.string();
}

std::string RealFSBackend::resolveCreating(const std::string& path) {
    auto fullPath = makePath(path);
    std::error_code ec;
    auto resolved = fs::weakly_canonical(fullPath, ec);
    if (ec) {
        throw VFSError(ec.value(),
                       "RealFSBackend: '" + path + "': " + ec.message());
    }
    if (!pathStartsWith(resolved, m_root)) {
        throw VFSError(EACCES,
                       "RealFSBackend: path escapes sandbox: '" + path + "'");
    }
    return resolved.string();
}

std::string RealFSBackend::readFile(const std::string& path) {
    auto resolved = resolveExisting(path);

    int fd = ::open(resolved.c_str(), O_RDONLY);
    if (fd < 0) throwErrno(errno, "readFile '" + path + "'");

    struct stat st;
    if (::fstat(fd, &st) < 0) {
        int saved = errno;
        ::close(fd);
        throwErrno(saved, "readFile fstat '" + path + "'");
    }

    std::string data(st.st_size, '\0');
    ssize_t total = 0;
    while (total < st.st_size) {
        ssize_t n = ::read(fd, data.data() + total, st.st_size - total);
        if (n < 0) {
            if (errno == EINTR) continue;
            int saved = errno;
            ::close(fd);
            throwErrno(saved, "readFile read '" + path + "'");
        }
        if (n == 0) break;
        total += n;
    }
    ::close(fd);
    data.resize(total);
    return data;
}

std::vector<uint8_t> RealFSBackend::readFileBytes(const std::string& path) {
    auto resolved = resolveExisting(path);

    int fd = ::open(resolved.c_str(), O_RDONLY);
    if (fd < 0) throwErrno(errno, "readFileBytes '" + path + "'");

    struct stat st;
    if (::fstat(fd, &st) < 0) {
        int saved = errno;
        ::close(fd);
        throwErrno(saved, "readFileBytes fstat '" + path + "'");
    }

    std::vector<uint8_t> data(st.st_size);
    ssize_t total = 0;
    while (total < st.st_size) {
        ssize_t n = ::read(fd, data.data() + total, st.st_size - total);
        if (n < 0) {
            if (errno == EINTR) continue;
            int saved = errno;
            ::close(fd);
            throwErrno(saved, "readFileBytes read '" + path + "'");
        }
        if (n == 0) break;
        total += n;
    }
    ::close(fd);
    data.resize(total);
    return data;
}

void RealFSBackend::writeFile(const std::string& path, const void* data, size_t len) {
    auto resolved = resolveCreating(path);

    int fd = ::open(resolved.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) throwErrno(errno, "writeFile '" + path + "'");

    const auto* buf = static_cast<const uint8_t*>(data);
    size_t total = 0;
    while (total < len) {
        ssize_t n = ::write(fd, buf + total, len - total);
        if (n < 0) {
            if (errno == EINTR) continue;
            int saved = errno;
            ::close(fd);
            throwErrno(saved, "writeFile write '" + path + "'");
        }
        total += n;
    }
    ::close(fd);
}

void RealFSBackend::appendFile(const std::string& path, const void* data, size_t len) {
    auto resolved = resolveCreating(path);

    int fd = ::open(resolved.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) throwErrno(errno, "appendFile '" + path + "'");

    const auto* buf = static_cast<const uint8_t*>(data);
    size_t total = 0;
    while (total < len) {
        ssize_t n = ::write(fd, buf + total, len - total);
        if (n < 0) {
            if (errno == EINTR) continue;
            int saved = errno;
            ::close(fd);
            throwErrno(saved, "appendFile write '" + path + "'");
        }
        total += n;
    }
    ::close(fd);
}

void RealFSBackend::unlink(const std::string& path) {
    auto resolved = resolveExisting(path);
    std::error_code ec;
    fs::remove(resolved, ec);
    if (ec) throw VFSError(ec.value(), "unlink '" + path + "': " + ec.message());
}

void RealFSBackend::mkdir(const std::string& path, bool recursive, uint32_t mode) {
    auto resolved = resolveCreating(path);
    std::error_code ec;
    if (recursive) {
        fs::create_directories(resolved, ec);
    } else {
        fs::create_directory(resolved, ec);
    }
    if (ec) throw VFSError(ec.value(), "mkdir '" + path + "': " + ec.message());
    if (!recursive || mode != 0777) {
        ::chmod(resolved.c_str(), mode);
    }
}

void RealFSBackend::rmdir(const std::string& path) {
    auto resolved = resolveExisting(path);
    std::error_code ec;
    fs::remove(resolved, ec);
    if (ec) throw VFSError(ec.value(), "rmdir '" + path + "': " + ec.message());
}

std::vector<std::string> RealFSBackend::readdir(const std::string& path) {
    auto resolved = resolveExisting(path);
    std::vector<std::string> entries;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(resolved, ec)) {
        entries.push_back(entry.path().filename().string());
    }
    if (ec) throw VFSError(ec.value(), "readdir '" + path + "': " + ec.message());
    return entries;
}

Stat RealFSBackend::stat(const std::string& path) {
    auto resolved = resolveExisting(path);
    struct stat st;
    if (::stat(resolved.c_str(), &st) < 0)
        throwErrno(errno, "stat '" + path + "'");
    return makeStat(st);
}

Stat RealFSBackend::lstat(const std::string& path) {
    auto resolved = resolveExisting(path);
    struct stat st;
    if (::lstat(resolved.c_str(), &st) < 0)
        throwErrno(errno, "lstat '" + path + "'");
    return makeStat(st);
}

bool RealFSBackend::exists(const std::string& path) {
    auto fullPath = makePath(path);
    std::error_code ec;
    return fs::exists(fullPath, ec);
}

void RealFSBackend::rename(const std::string& oldPath, const std::string& newPath) {
    auto resolvedOld = resolveExisting(oldPath);
    auto resolvedNew = resolveCreating(newPath);
    std::error_code ec;
    fs::rename(resolvedOld, resolvedNew, ec);
    if (ec) throw VFSError(ec.value(), "rename '" + oldPath + "' -> '" + newPath + "': " + ec.message());
}

void RealFSBackend::copyFile(const std::string& src, const std::string& dst) {
    auto resolvedSrc = resolveExisting(src);
    auto resolvedDst = resolveCreating(dst);
    std::error_code ec;
    fs::copy_file(resolvedSrc, resolvedDst, ec);
    if (ec) throw VFSError(ec.value(), "copyFile '" + src + "' -> '" + dst + "': " + ec.message());
}

void RealFSBackend::truncate(const std::string& path, int64_t size) {
    auto resolved = resolveExisting(path);
    if (::truncate(resolved.c_str(), size) < 0)
        throwErrno(errno, "truncate '" + path + "'");
}

void RealFSBackend::utimes(const std::string& path, int64_t atimeMs, int64_t mtimeMs) {
    auto resolved = resolveExisting(path);
    struct timespec ts[2];
    ts[0].tv_sec = static_cast<time_t>(atimeMs / 1000);
    ts[0].tv_nsec = static_cast<long>((atimeMs % 1000) * 1000000);
    ts[1].tv_sec = static_cast<time_t>(mtimeMs / 1000);
    ts[1].tv_nsec = static_cast<long>((mtimeMs % 1000) * 1000000);
    if (::utimensat(AT_FDCWD, resolved.c_str(), ts, 0) < 0)
        throwErrno(errno, "utimes '" + path + "'");
}

int RealFSBackend::access(const std::string& path, int mode) {
    auto fullPath = makePath(path);
    if (::access(fullPath.c_str(), mode) < 0) {
        if (errno == EACCES || errno == ENOENT) return errno;
        throwErrno(errno, "access '" + path + "'");
    }
    return 0;
}

} // namespace sandboxed_fs
