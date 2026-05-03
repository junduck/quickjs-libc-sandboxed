#pragma once

#include "vfs_backend.hpp"
#include "vfs_types.hpp"

#include <memory>
#include <string>
#include <vector>

namespace sandboxed_fs {

// Associates a virtual-path prefix with a backend and a permission mask.
struct MountEntry {
    std::string prefix;                     // e.g. "/bundle", "/home/sandbox", "/"
    Perm perm;                              // allowed operations for this mount
    std::shared_ptr<VFSBackend> backend;    // the backing store
};

// Composite VFS backend that routes operations to sub-backends by
// longest-prefix match on the virtual path.
//
// Mounts are kept sorted by prefix length (descending) so the most specific
// match wins.  Path normalization (collapse ".", resolve "..") is applied
// before matching.  Permission checks happen here — a backend never sees
// an operation that its mount does not permit.
class VirtualFS : public VFSBackend {
public:
    VirtualFS() = default;

    // Construct with a mount list; entries are sorted automatically.
    explicit VirtualFS(std::vector<MountEntry> mounts);

    // Add a mount; re-sorts the internal list.
    void mount(const MountEntry& entry);

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
    std::vector<MountEntry> m_mounts;       // sorted: longest prefix first

    struct Resolved {
        VFSBackend* backend;
        std::string subPath;                // path relative to the backend's root
    };

    // Find the matching mount, check permissions, return (backend, sub-path).
    Resolved resolve(const std::string& path, Perm neededPerm);

    // Collapse ".", resolve "..", produce canonical virtual path.
    static std::string normalize(const std::string& path);
};

} // namespace sandboxed_fs
