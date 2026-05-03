#pragma once

#include "vfs_backend.hpp"
#include "vfs_types.hpp"

#include <expected>
#include <memory>
#include <string>
#include <vector>

namespace sandboxed_fs {

// Associates a virtual-path prefix with a backend and a permission mask.
struct MountEntry {
  std::string prefix;
  Perm perm;
  std::shared_ptr<VFSBackend> backend;
};

// Composite VFS backend that routes operations to sub-backends by
// longest-prefix match on the virtual path.
// Permission checks happen here — a backend never sees an operation that
// its mount does not permit.
class VirtualFS : public VFSBackend {
public:
  VirtualFS() = default;
  explicit VirtualFS(std::vector<MountEntry> mounts);
  void mount(const MountEntry &entry);

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
  std::vector<MountEntry> m_mounts;

  struct Resolved {
    VFSBackend *backend;
    std::string subPath;
  };

  std::expected<Resolved, int> resolve(const std::string &path, Perm neededPerm);
  static std::string normalize(const std::string &path);
};

} // namespace sandboxed_fs
