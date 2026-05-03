#pragma once

#include "vfs_backend.hpp"
#include "vfs_types.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace sandboxed_fs {

// Pure in-memory VFS backend — no disk I/O.
// The filesystem is stored as a tree of Node objects.
// Intermediate directories are created implicitly by writeFile and mkdir.
class MemFSBackend : public VFSBackend {
public:
    MemFSBackend();

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
    struct Node {
        Stat stat;
        std::vector<uint8_t> data;                          // file content; empty for dirs
        std::map<std::string, std::unique_ptr<Node>> children; // only meaningful for dirs
    };

    std::unique_ptr<Node> m_root;
    uint64_t m_nextIno = 2;                                // inode counter (1 = root)

    struct WalkResult {
        Node* node;
        Node* parent;
        std::string name;
    };

    // Walk the tree to the named path.  If createMissing is true, intermediate
    // directories are created as normal dirs (mode 0755).
    WalkResult walkTo(const std::string& path, bool createMissing = false);
};

} // namespace sandboxed_fs
