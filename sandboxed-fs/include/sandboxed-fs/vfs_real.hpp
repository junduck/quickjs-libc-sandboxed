#pragma once

#include "vfs_backend.hpp"
#include "vfs_types.hpp"

#include <filesystem>
#include <string>

namespace sandboxed_fs {

// VFS backend backed by a real on-disk directory.
// Every path is validated via realpath / weakly_canonical before touching
// the filesystem — symlink escape and ".." traversal outside the root
// are blocked.
class RealFSBackend : public VFSBackend {
public:
    // realRoot is canonicalized immediately; throws VFSError if it does
    // not exist or cannot be resolved.
    explicit RealFSBackend(std::filesystem::path realRoot);

    std::string readFile(const std::string& path) override;
    std::vector<uint8_t> readFileBytes(const std::string& path) override;

    void writeFile(const std::string& path, const void* data, size_t len) override;
    void appendFile(const std::string& path, const void* data, size_t len) override;

    void unlink(const std::string& path) override;
    void mkdir(const std::string& path, bool recursive, uint32_t mode = 0777) override;
    void rmdir(const std::string& path) override;

    std::vector<std::string> readdir(const std::string& path) override;

    Stat stat(const std::string& path) override;
    Stat lstat(const std::string& path) override;
    bool exists(const std::string& path) override;

    void rename(const std::string& oldPath, const std::string& newPath) override;
    void copyFile(const std::string& src, const std::string& dst) override;
    void truncate(const std::string& path, int64_t size) override;
    void utimes(const std::string& path, int64_t atimeMs, int64_t mtimeMs) override;

    int access(const std::string& path, int mode) override;

private:
    std::filesystem::path m_root;

    // Join root with a (possibly absolute) relative path, stripping any
    // leading '/' so that operator/ produces a child of m_root.
    std::filesystem::path makePath(const std::string& path) const;

    // Canonicalize and prefix-check.  Used for paths that must already exist.
    std::string resolveExisting(const std::string& path);

    // weakly_canonical and prefix-check.  Used for paths being created.
    std::string resolveCreating(const std::string& path);
};

} // namespace sandboxed_fs
