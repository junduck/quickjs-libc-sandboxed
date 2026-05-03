#include "sandboxed-fs/vfs_mem.hpp"

#include <algorithm>
#include <cerrno>
#include <ctime>
#include <expected>
#include <sstream>

namespace sandboxed_fs {

static std::vector<std::string> splitPath(const std::string &path) {
  std::vector<std::string> parts;
  if (path.empty() || path == "/")
    return parts;
  std::string p = path;
  if (p[0] == '/' || p[0] == '\\')
    p = p.substr(1);
  std::replace(p.begin(), p.end(), '\\', '/');
  std::istringstream ss(p);
  std::string part;
  while (std::getline(ss, part, '/')) {
    if (!part.empty() && part != ".")
      parts.push_back(part);
  }
  return parts;
}

static uint64_t nowMs() { return static_cast<uint64_t>(std::time(nullptr)) * 1000; }

MemFSBackend::MemFSBackend() {
  m_root = std::make_unique<Node>();
  m_root->stat.dev = 0;
  m_root->stat.ino = 1;
  m_root->stat.mode = 040777;
  m_root->stat.nlink = 1;
  m_root->stat.uid = 1000;
  m_root->stat.gid = 1000;
  auto now = nowMs();
  m_root->stat.atimeMs = now;
  m_root->stat.mtimeMs = now;
  m_root->stat.ctimeMs = now;
}

std::expected<MemFSBackend::WalkResult, int> MemFSBackend::walkTo(const std::string &path) {
  auto parts = splitPath(path);

  Node *node = m_root.get();
  Node *parent = nullptr;
  std::string lastName;

  for (const auto &part : parts) {
    parent = node;
    lastName = part;

    auto it = node->children.find(part);
    if (it == node->children.end())
      return std::unexpected(ENOENT);

    node = it->second.get();
  }

  return WalkResult{node, parent, lastName};
}

std::expected<MemFSBackend::Node *const, int> MemFSBackend::parentDir(const std::string &path) {
  auto parts = splitPath(path);
  if (parts.empty())
    return m_root.get();

  parts.pop_back();
  if (parts.empty())
    return m_root.get();

  std::string parentPath = "/";
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i > 0)
      parentPath += "/";
    parentPath += parts[i];
  }
  auto w = walkTo(parentPath);
  if (!w)
    return std::unexpected(w.error());
  if (!w->node->stat.isDir())
    return std::unexpected(ENOTDIR);
  return w->node;
}

std::expected<std::string, int> MemFSBackend::readFile(const std::string &path) {
  auto w = walkTo(path);
  if (!w)
    return std::unexpected(w.error());
  if (w->node->stat.isDir())
    return std::unexpected(EISDIR);
  return std::string(reinterpret_cast<const char *>(w->node->data.data()), w->node->data.size());
}

std::expected<std::vector<uint8_t>, int> MemFSBackend::readFileBytes(const std::string &path) {
  auto w = walkTo(path);
  if (!w)
    return std::unexpected(w.error());
  if (w->node->stat.isDir())
    return std::unexpected(EISDIR);
  return w->node->data;  // copy — read must not destroy stored content
}

std::expected<void, int> MemFSBackend::writeFile(const std::string &path, const void *data, size_t len) {
  auto parts = splitPath(path);
  if (parts.empty())
    return std::unexpected(EISDIR);

  std::string fileName = parts.back();
  auto p = parentDir(path);
  if (!p)
    return std::unexpected(p.error());

  auto now = nowMs();
  auto &entry = (*p)->children[fileName];
  if (!entry) {
    entry = std::make_unique<Node>();
    entry->stat.dev = 0;
    entry->stat.ino = m_nextIno++;
    entry->stat.mode = 0100644;
    entry->stat.nlink = 1;
    entry->stat.uid = 1000;
    entry->stat.gid = 1000;
    entry->stat.atimeMs = now;
    entry->stat.mtimeMs = now;
    entry->stat.ctimeMs = now;
  } else if (entry->stat.isDir()) {
    return std::unexpected(EISDIR);
  }

  const auto *buf = static_cast<const uint8_t *>(data);
  entry->data.assign(buf, buf + len);
  entry->stat.size = static_cast<int64_t>(len);
  entry->stat.mtimeMs = now;
  return {};
}

std::expected<void, int> MemFSBackend::appendFile(const std::string &path, const void *data, size_t len) {
  auto parts = splitPath(path);
  if (parts.empty())
    return std::unexpected(EISDIR);

  std::string fileName = parts.back();
  auto p = parentDir(path);
  if (!p)
    return std::unexpected(p.error());

  auto now = nowMs();
  auto &entry = (*p)->children[fileName];
  if (!entry)
    return writeFile(path, data, len);
  if (entry->stat.isDir())
    return std::unexpected(EISDIR);

  const auto *buf = static_cast<const uint8_t *>(data);
  entry->data.insert(entry->data.end(), buf, buf + len);
  entry->stat.size = static_cast<int64_t>(entry->data.size());
  entry->stat.mtimeMs = now;
  return {};
}

std::expected<void, int> MemFSBackend::unlink(const std::string &path) {
  auto parts = splitPath(path);
  if (parts.empty())
    return std::unexpected(EACCES);

  std::string fileName = parts.back();
  auto p = parentDir(path);
  if (!p)
    return std::unexpected(p.error());

  auto it = (*p)->children.find(fileName);
  if (it == (*p)->children.end())
    return std::unexpected(ENOENT);
  if (it->second->stat.isDir() && !it->second->children.empty())
    return std::unexpected(ENOTEMPTY);
  (*p)->children.erase(it);
  return {};
}

std::expected<void, int> MemFSBackend::mkdir(const std::string &path, bool recursive, uint32_t mode) {
  auto parts = splitPath(path);
  if (parts.empty())
    return {};

  if (recursive) {
    Node *current = m_root.get();
    for (const auto &part : parts) {
      auto &child = current->children[part];
      if (!child) {
        child = std::make_unique<Node>();
        child->stat.dev = 0;
        child->stat.ino = m_nextIno++;
        child->stat.mode = 0x4000 | (mode & 0777);
        child->stat.nlink = 1;
        child->stat.uid = 1000;
        child->stat.gid = 1000;
        auto now = nowMs();
        child->stat.atimeMs = now;
        child->stat.mtimeMs = now;
        child->stat.ctimeMs = now;
      } else if (!child->stat.isDir()) {
        return std::unexpected(ENOTDIR);
      }
      current = child.get();
    }
    current->stat.mode = 0x4000 | (mode & 0777);
  } else {
    auto partsCopy = parts;
    std::string fileName = partsCopy.back();

    auto p = parentDir(path);
    if (!p)
      return std::unexpected(p.error());
    if ((*p)->children.contains(fileName))
      return std::unexpected(EEXIST);

    auto now = nowMs();
    auto node = std::make_unique<Node>();
    node->stat.dev = 0;
    node->stat.ino = m_nextIno++;
    node->stat.mode = 0x4000 | (mode & 0777);
    node->stat.nlink = 1;
    node->stat.uid = 1000;
    node->stat.gid = 1000;
    node->stat.atimeMs = now;
    node->stat.mtimeMs = now;
    node->stat.ctimeMs = now;
    (*p)->children[fileName] = std::move(node);
  }
  return {};
}

std::expected<void, int> MemFSBackend::rmdir(const std::string &path) {
  auto w = walkTo(path);
  if (!w)
    return std::unexpected(w.error());
  if (!w->node->stat.isDir())
    return std::unexpected(ENOTDIR);
  if (!w->node->children.empty())
    return std::unexpected(ENOTEMPTY);
  return unlink(path);
}

std::expected<std::vector<std::string>, int> MemFSBackend::readdir(const std::string &path) {
  auto w = walkTo(path);
  if (!w)
    return std::unexpected(w.error());
  if (!w->node->stat.isDir())
    return std::unexpected(ENOTDIR);
  std::vector<std::string> entries;
  for (const auto &[name, _child] : w->node->children) {
    entries.push_back(name);
  }
  return entries;
}

std::expected<Stat, int> MemFSBackend::stat(const std::string &path) {
  auto w = walkTo(path);
  if (!w)
    return std::unexpected(w.error());
  return w->node->stat;
}

std::expected<Stat, int> MemFSBackend::lstat(const std::string &path) { return stat(path); }

bool MemFSBackend::exists(const std::string &path) { return walkTo(path).has_value(); }

std::expected<void, int> MemFSBackend::rename(const std::string &oldPath, const std::string &newPath) {
  if (oldPath == newPath) return {};

  auto oldParts = splitPath(oldPath);
  if (oldParts.empty())
    return std::unexpected(EACCES);
  std::string oldName = oldParts.back();

  auto oldP = parentDir(oldPath);
  if (!oldP)
    return std::unexpected(oldP.error());

  auto it = (*oldP)->children.find(oldName);
  if (it == (*oldP)->children.end())
    return std::unexpected(ENOENT);

  // Reject rename of directory into its own descendant (creates cycle)
  if (it->second->stat.isDir() && newPath.size() > oldPath.size() &&
      newPath[oldPath.size()] == '/' &&
      newPath.compare(0, oldPath.size(), oldPath) == 0)
    return std::unexpected(EINVAL);

  auto newParts = splitPath(newPath);
  if (newParts.empty())
    return std::unexpected(EACCES);
  std::string newName = newParts.back();

  auto newP = parentDir(newPath);
  if (!newP)
    return std::unexpected(newP.error());

  auto &dstEntry = (*newP)->children[newName];
  if (dstEntry) {
    if (dstEntry->stat.isDir() && !dstEntry->children.empty())
      return std::unexpected(ENOTEMPTY);
  }

  auto node = std::move(it->second);
  (*oldP)->children.erase(it);
  (*newP)->children[newName] = std::move(node);
  return {};
}

std::expected<void, int> MemFSBackend::copyFile(const std::string &src, const std::string &dst) {
  auto w = walkTo(src);
  if (!w)
    return std::unexpected(w.error());
  if (w->node->stat.isDir())
    return std::unexpected(EISDIR);

  auto parts = splitPath(dst);
  if (parts.empty())
    return std::unexpected(EISDIR);
  std::string fileName = parts.back();

  auto p = parentDir(dst);
  if (!p)
    return std::unexpected(p.error());

  auto it = (*p)->children.find(fileName);
  if (it != (*p)->children.end()) {
    if (it->second->stat.isDir())
      return std::unexpected(EISDIR);
  }
  auto clone = std::make_unique<Node>();
  clone->stat = w->node->stat;
  clone->data = w->node->data;
  clone->stat.ino = m_nextIno++;
  (*p)->children[fileName] = std::move(clone);
  return {};
}

std::expected<void, int> MemFSBackend::truncate(const std::string &path, int64_t size) {
  if (size < 0)
    return std::unexpected(EINVAL);
  auto w = walkTo(path);
  if (!w)
    return std::unexpected(w.error());
  if (w->node->stat.isDir())
    return std::unexpected(EISDIR);
  if (size > static_cast<int64_t>(w->node->data.size())) {
    w->node->data.resize(size, 0);
  } else {
    w->node->data.resize(size);
  }
  w->node->stat.size = size;
  auto now = nowMs();
  w->node->stat.mtimeMs = now;
  w->node->stat.ctimeMs = now;
  return {};
}

std::expected<void, int> MemFSBackend::utimes(const std::string &path, int64_t atimeMs, int64_t mtimeMs) {
  auto w = walkTo(path);
  if (!w)
    return std::unexpected(w.error());
  w->node->stat.atimeMs = atimeMs;
  w->node->stat.mtimeMs = mtimeMs;
  w->node->stat.ctimeMs = nowMs();
  return {};
}

int MemFSBackend::access(const std::string &path, int mode) {
  auto w = walkTo(path);
  if (!w)
    return w.error();

  if (mode == access_mode::F_OK)
    return 0;

  uint32_t stMode = w->node->stat.mode;
  if ((mode & access_mode::R_OK) && !(stMode & 0400))
    return EACCES;
  if ((mode & access_mode::W_OK) && !(stMode & 0200))
    return EACCES;
  if ((mode & access_mode::X_OK) && !(stMode & 0100))
    return EACCES;

  return 0;
}

} // namespace sandboxed_fs
