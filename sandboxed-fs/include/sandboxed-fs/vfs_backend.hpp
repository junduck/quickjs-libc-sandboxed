#pragma once

#include "vfs_types.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sandboxed_fs {

// Abstract filesystem backend.
// All paths are relative to the backend's root.  Every method throws VFSError
// on failure (ENOENT, EACCES, EISDIR, etc.).
class VFSBackend {
public:
    virtual ~VFSBackend() = default;

    // Read entire file as a UTF-8 string.
    virtual std::string readFile(const std::string& path) = 0;
    // Read entire file as raw bytes.
    virtual std::vector<uint8_t> readFileBytes(const std::string& path) = 0;

    // Create or overwrite a file.
    virtual void writeFile(const std::string& path, const void* data, size_t len) = 0;
    // Create or append to a file.
    virtual void appendFile(const std::string& path, const void* data, size_t len) = 0;

    // Remove a file (not a directory unless empty — backends may vary).
    virtual void unlink(const std::string& path) = 0;
    // Create a directory.  If recursive, intermediate directories are created.
    virtual void mkdir(const std::string& path, bool recursive, uint32_t mode = 0777) = 0;
    // Remove an empty directory.
    virtual void rmdir(const std::string& path) = 0;

    // List entries (bare names) in a directory.
    virtual std::vector<std::string> readdir(const std::string& path) = 0;

    // stat follows symlinks; lstat does not.
    virtual Stat stat(const std::string& path) = 0;
    virtual Stat lstat(const std::string& path) = 0;
    virtual bool exists(const std::string& path) = 0;

    // Rename / move a file or directory.
    virtual void rename(const std::string& oldPath, const std::string& newPath) = 0;
    // Copy file contents.
    virtual void copyFile(const std::string& src, const std::string& dst) = 0;
    // Resize a file.  Extending fills with zeros.
    virtual void truncate(const std::string& path, int64_t size) = 0;
    // Set access and modification times (milliseconds since epoch).
    virtual void utimes(const std::string& path, int64_t atimeMs, int64_t mtimeMs) = 0;

    // Check access using access_mode constants.  Returns 0 on success or errno.
    virtual int access(const std::string& path, int mode) = 0;
};

} // namespace sandboxed_fs
