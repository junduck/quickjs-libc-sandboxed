#pragma once

#include "vfs_backend.hpp"
#include "vfs_types.hpp"

#include <cstdint>
#include <expected>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace sandboxed_fs {

// Pure in-memory VFS backend — no disk I/O.
// Intermediate directories are created implicitly by writeFile and mkdir.
class MemFSBackend : public VFSBackend {
public:
  MemFSBackend();

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
  struct Node {
    Stat stat;
    std::vector<uint8_t> data;
    std::map<std::string, std::unique_ptr<Node>> children;
  };

  std::unique_ptr<Node> m_root;
  uint64_t m_nextIno = 2;

  struct WalkResult {
    Node *node;
    Node *parent;
    std::string name;
  };

  std::expected<WalkResult, int> walkTo(const std::string &path, bool createMissing = false);
};

} // namespace sandboxed_fs
