#pragma once

#include "vfs_types.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace sandboxed_fs {

// Abstract filesystem backend.
// All paths are relative to the backend's root.
// Every method returns std::expected<T, int> where the error is a POSIX errno.
class VFSBackend {
public:
  virtual ~VFSBackend() = default;

  virtual std::expected<std::string, int> readFile(const std::string &path) = 0;
  virtual std::expected<std::vector<uint8_t>, int> readFileBytes(const std::string &path) = 0;

  virtual std::expected<void, int> writeFile(const std::string &path, const void *data, size_t len) = 0;
  virtual std::expected<void, int> appendFile(const std::string &path, const void *data, size_t len) = 0;

  virtual std::expected<void, int> unlink(const std::string &path) = 0;
  virtual std::expected<void, int> mkdir(const std::string &path, bool recursive, uint32_t mode = 0777) = 0;
  virtual std::expected<void, int> rmdir(const std::string &path) = 0;

  virtual std::expected<std::vector<std::string>, int> readdir(const std::string &path) = 0;

  virtual std::expected<Stat, int> stat(const std::string &path) = 0;
  virtual std::expected<Stat, int> lstat(const std::string &path) = 0;
  virtual bool exists(const std::string &path) = 0;

  virtual std::expected<void, int> rename(const std::string &oldPath, const std::string &newPath) = 0;
  virtual std::expected<void, int> copyFile(const std::string &src, const std::string &dst) = 0;
  virtual std::expected<void, int> truncate(const std::string &path, int64_t size) = 0;
  virtual std::expected<void, int> utimes(const std::string &path, int64_t atimeMs, int64_t mtimeMs) = 0;

  virtual int access(const std::string &path, int mode) = 0;
};

} // namespace sandboxed_fs
