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

std::expected<MemFSBackend::WalkResult, int> MemFSBackend::walkTo(const std::string &path, bool createMissing) {
  auto parts = splitPath(path);

  Node *node = m_root.get();
  Node *parent = nullptr;
  std::string lastName;

  for (const auto &part : parts) {
    parent = node;
    lastName = part;

    if (!node->children.contains(part)) {
      if (!createMissing)
        return std::unexpected(ENOENT);

      auto newNode = std::make_unique<Node>();
      newNode->stat.mode = 040755;
      newNode->stat.ino = m_nextIno++;
      newNode->stat.uid = 1000;
      newNode->stat.gid = 1000;
      auto now = nowMs();
      newNode->stat.atimeMs = now;
      newNode->stat.mtimeMs = now;
      newNode->stat.ctimeMs = now;
      node->children[part] = std::move(newNode);
    }

    node = node->children[part].get();
  }

  return WalkResult{node, parent, lastName};
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
  return w->node->data;
}

std::expected<void, int> MemFSBackend::writeFile(const std::string &path, const void *data, size_t len) {
  auto parts = splitPath(path);
  if (parts.empty())
    return std::unexpected(EISDIR);

  std::string fileName = parts.back();
  parts.pop_back();

  Node *dir = m_root.get();
  if (!parts.empty()) {
    std::string parentPath = "/";
    for (size_t i = 0; i < parts.size(); ++i) {
      if (i > 0)
        parentPath += "/";
      parentPath += parts[i];
    }
    auto w = walkTo(parentPath);
    if (!w)
      return std::unexpected(w.error());
    dir = w->node;
  }

  if (!dir->stat.isDir())
    return std::unexpected(ENOTDIR);

  auto now = nowMs();
  auto &entry = dir->children[fileName];
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
  entry->stat.ctimeMs = now;
  return {};
}

std::expected<void, int> MemFSBackend::appendFile(const std::string &path, const void *data, size_t len) {
  auto parts = splitPath(path);
  if (parts.empty())
    return std::unexpected(EISDIR);

  std::string fileName = parts.back();
  parts.pop_back();

  Node *dir = m_root.get();
  if (!parts.empty()) {
    std::string parentPath = "/";
    for (size_t i = 0; i < parts.size(); ++i) {
      if (i > 0)
        parentPath += "/";
      parentPath += parts[i];
    }
    auto w = walkTo(parentPath);
    if (!w)
      return std::unexpected(w.error());
    dir = w->node;
  }

  if (!dir->stat.isDir())
    return std::unexpected(ENOTDIR);

  auto now = nowMs();
  auto &entry = dir->children[fileName];
  if (!entry) {
    return writeFile(path, data, len);
  }
  if (entry->stat.isDir())
    return std::unexpected(EISDIR);

  const auto *buf = static_cast<const uint8_t *>(data);
  entry->data.insert(entry->data.end(), buf, buf + len);
  entry->stat.size = static_cast<int64_t>(entry->data.size());
  entry->stat.mtimeMs = now;
  entry->stat.ctimeMs = now;
  return {};
}

std::expected<void, int> MemFSBackend::unlink(const std::string &path) {
  auto parts = splitPath(path);
  if (parts.empty())
    return std::unexpected(EACCES);

  std::string fileName = parts.back();
  std::string parentPath = "/";
  for (size_t i = 0; i + 1 < parts.size(); ++i) {
    parentPath += (i > 0 ? "/" : "") + parts[i];
  }
  if (parts.size() == 1)
    parentPath = "/";

  auto w = walkTo(parentPath);
  if (!w)
    return std::unexpected(w.error());

  auto it = w->node->children.find(fileName);
  if (it == w->node->children.end())
    return std::unexpected(ENOENT);
  if (it->second->stat.isDir() && !it->second->children.empty())
    return std::unexpected(ENOTEMPTY);
  w->node->children.erase(it);
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
    partsCopy.pop_back();

    Node *parent = m_root.get();
    if (!partsCopy.empty()) {
      std::string pp = "/";
      for (size_t i = 0; i < partsCopy.size(); ++i) {
        pp += (i > 0 ? "/" : "") + partsCopy[i];
      }
      auto w = walkTo(pp);
      if (!w)
        return std::unexpected(w.error());
      parent = w->node;
    }

    if (parent->children.contains(fileName))
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
    parent->children[fileName] = std::move(node);
  }
  return {};
}

std::expected<void, int> MemFSBackend::rmdir(const std::string &path) { return unlink(path); }

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
  auto oldParts = splitPath(oldPath);
  if (oldParts.empty())
    return std::unexpected(EACCES);
  std::string oldName = oldParts.back();
  oldParts.pop_back();

  std::string oldParentPath = "/";
  for (size_t i = 0; i < oldParts.size(); ++i) {
    oldParentPath += (i > 0 ? "/" : "") + oldParts[i];
  }
  if (oldParts.empty())
    oldParentPath = "/";

  auto oldW = walkTo(oldParentPath);
  if (!oldW)
    return std::unexpected(oldW.error());
  auto it = oldW->node->children.find(oldName);
  if (it == oldW->node->children.end())
    return std::unexpected(ENOENT);

  auto newParts = splitPath(newPath);
  if (newParts.empty())
    return std::unexpected(EACCES);
  std::string newName = newParts.back();
  newParts.pop_back();

  std::string newParentPath = "/";
  for (size_t i = 0; i < newParts.size(); ++i) {
    newParentPath += (i > 0 ? "/" : "") + newParts[i];
  }
  if (newParts.empty())
    newParentPath = "/";

  auto newW = walkTo(newParentPath);
  if (!newW)
    return std::unexpected(newW.error());

  if (newW->node->children.contains(newName))
    return std::unexpected(EEXIST);

  auto node = std::move(it->second);
  oldW->node->children.erase(it);
  newW->node->children[newName] = std::move(node);
  return {};
}

std::expected<void, int> MemFSBackend::copyFile(const std::string &src, const std::string &dst) {
  auto w = walkTo(src);
  if (!w)
    return std::unexpected(w.error());
  if (w->node->stat.isDir())
    return std::unexpected(EISDIR);
  return writeFile(dst, w->node->data.data(), w->node->data.size());
}

std::expected<void, int> MemFSBackend::truncate(const std::string &path, int64_t size) {
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
  w->node->stat.mtimeMs = nowMs();
  w->node->stat.ctimeMs = nowMs();
  return {};
}

std::expected<void, int> MemFSBackend::utimes(const std::string &path, int64_t atimeMs, int64_t mtimeMs) {
  auto w = walkTo(path);
  if (!w)
    return std::unexpected(w.error());
  w->node->stat.atimeMs = atimeMs;
  w->node->stat.mtimeMs = mtimeMs;
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
