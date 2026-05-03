#pragma once

#include "vfs_backend.hpp"
#include "vfs_types.hpp"

#include <expected>
#include <filesystem>
#include <string>

namespace sandboxed_fs {

// VFS backend backed by a real on-disk directory.
// Every path is validated via canonical / weakly_canonical before touching
// the filesystem — symlink escape and ".." traversal outside the root
// are blocked.
class RealFSBackend : public VFSBackend {
public:
  // realRoot is canonicalized immediately; throws std::runtime_error if it
  // does not exist or cannot be resolved.
  explicit RealFSBackend(std::filesystem::path realRoot);

  std::expected<std::string, int> readFile(const std::string &path) override;
  std::expected<std::vector<uint8_t>, int> readFileBytes(const std::string &path) override;

  std::expected<void, int> writeFile(const std::string &path, const void *data, size_t len) override;
  std::expected<void, int> appendFile(const std::string &path, const void *data, size_t len) override;

  std::expected<void, int> unlink(const std::string &path) override;
  std::expected<void, int> mkdir(const std::string &path, bool recursive, uint32_t mode = 0777) override;
  std::expected<void, int> rmdir(const std::string &path) override;

  std::expected<std::vector<std::string>, int> readdir(const std::string &path) override;

  std::expected<Stat, int> stat(const std::string &path) override;
  std::expected<Stat, int> lstat(const std::string &path) override;
  bool exists(const std::string &path) override;

  std::expected<void, int> rename(const std::string &oldPath, const std::string &newPath) override;
  std::expected<void, int> copyFile(const std::string &src, const std::string &dst) override;
  std::expected<void, int> truncate(const std::string &path, int64_t size) override;
  std::expected<void, int> utimes(const std::string &path, int64_t atimeMs, int64_t mtimeMs) override;

  int access(const std::string &path, int mode) override;

private:
  std::filesystem::path m_root;

  enum class ResolveMode { Existing, Creating };

  std::filesystem::path makePath(const std::string &path) const;
  std::expected<std::string, int> resolve(const std::string &path, ResolveMode mode);
};

} // namespace sandboxed_fs
