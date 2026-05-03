#include "sandboxed-fs/vfs_mem.hpp"

#include <algorithm>
#include <cerrno>
#include <ctime>
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

MemFSBackend::WalkResult MemFSBackend::walkTo(const std::string &path, bool createMissing) {
  auto parts = splitPath(path);

  Node *node = m_root.get();
  Node *parent = nullptr;
  std::string lastName;

  for (const auto &part : parts) {
    parent = node;
    lastName = part;

    if (!node->children.contains(part)) {
      if (!createMissing) {
        throw VFSError(ENOENT, "MemFS: no such file or directory: '" + path + "'");
      }
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

  return {node, parent, lastName};
}

std::string MemFSBackend::readFile(const std::string &path) {
  auto [node, _p, _n] = walkTo(path);
  if (node->stat.isDir()) {
    throw VFSError(EISDIR, "MemFS: is a directory: '" + path + "'");
  }
  return std::string(reinterpret_cast<const char *>(node->data.data()), node->data.size());
}

std::vector<uint8_t> MemFSBackend::readFileBytes(const std::string &path) {
  auto [node, _p, _n] = walkTo(path);
  if (node->stat.isDir()) {
    throw VFSError(EISDIR, "MemFS: is a directory: '" + path + "'");
  }
  return node->data;
}

void MemFSBackend::writeFile(const std::string &path, const void *data, size_t len) {
  auto parts = splitPath(path);
  if (parts.empty()) {
    throw VFSError(EISDIR, "MemFS: cannot write to root");
  }

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
    auto [node, _p, _n] = walkTo(parentPath);
    dir = node;
  }

  if (!dir->stat.isDir()) {
    throw VFSError(ENOTDIR, "MemFS: not a directory: parent of '" + path + "'");
  }

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
    throw VFSError(EISDIR, "MemFS: is a directory: '" + path + "'");
  }

  const auto *buf = static_cast<const uint8_t *>(data);
  entry->data.assign(buf, buf + len);
  entry->stat.size = static_cast<int64_t>(len);
  entry->stat.mtimeMs = now;
  entry->stat.ctimeMs = now;
}

void MemFSBackend::appendFile(const std::string &path, const void *data, size_t len) {
  auto parts = splitPath(path);
  if (parts.empty()) {
    throw VFSError(EISDIR, "MemFS: cannot write to root");
  }

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
    auto [node, _p, _n] = walkTo(parentPath);
    dir = node;
  }

  if (!dir->stat.isDir()) {
    throw VFSError(ENOTDIR, "MemFS: not a directory: parent of '" + path + "'");
  }

  auto now = nowMs();
  auto &entry = dir->children[fileName];
  if (!entry) {
    writeFile(path, data, len);
    return;
  }
  if (entry->stat.isDir()) {
    throw VFSError(EISDIR, "MemFS: is a directory: '" + path + "'");
  }

  const auto *buf = static_cast<const uint8_t *>(data);
  entry->data.insert(entry->data.end(), buf, buf + len);
  entry->stat.size = static_cast<int64_t>(entry->data.size());
  entry->stat.mtimeMs = now;
  entry->stat.ctimeMs = now;
}

void MemFSBackend::unlink(const std::string &path) {
  auto parts = splitPath(path);
  if (parts.empty()) {
    throw VFSError(EACCES, "MemFS: cannot unlink root");
  }

  std::string fileName = parts.back();
  std::string parentPath = "/";
  for (size_t i = 0; i + 1 < parts.size(); ++i) {
    parentPath += (i > 0 ? "/" : "") + parts[i];
  }
  if (parts.size() == 1)
    parentPath = "/";

  auto [parent, _p2, _n] = walkTo(parentPath);

  auto it = parent->children.find(fileName);
  if (it == parent->children.end()) {
    throw VFSError(ENOENT, "MemFS: no such file: '" + path + "'");
  }
  if (it->second->stat.isDir() && !it->second->children.empty()) {
    throw VFSError(ENOTEMPTY, "MemFS: directory not empty: '" + path + "'");
  }
  parent->children.erase(it);
}

void MemFSBackend::mkdir(const std::string &path, bool recursive, uint32_t mode) {
  auto parts = splitPath(path);
  if (parts.empty())
    return;

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
        throw VFSError(ENOTDIR, "MemFS: not a directory: '" + path + "'");
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
      auto [n, _p, _n] = walkTo(pp);
      parent = n;
    }

    if (parent->children.contains(fileName)) {
      throw VFSError(EEXIST, "MemFS: file exists: '" + path + "'");
    }

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
}

void MemFSBackend::rmdir(const std::string &path) { unlink(path); }

std::vector<std::string> MemFSBackend::readdir(const std::string &path) {
  auto [node, _p, _n] = walkTo(path);
  if (!node->stat.isDir()) {
    throw VFSError(ENOTDIR, "MemFS: not a directory: '" + path + "'");
  }
  std::vector<std::string> entries;
  for (const auto &[name, _child] : node->children) {
    entries.push_back(name);
  }
  return entries;
}

Stat MemFSBackend::stat(const std::string &path) {
  auto [node, _p, _n] = walkTo(path);
  return node->stat;
}

Stat MemFSBackend::lstat(const std::string &path) { return stat(path); }

bool MemFSBackend::exists(const std::string &path) {
  try {
    walkTo(path);
    return true;
  } catch (const VFSError &) {
    return false;
  }
}

void MemFSBackend::rename(const std::string &oldPath, const std::string &newPath) {
  auto oldParts = splitPath(oldPath);
  if (oldParts.empty()) {
    throw VFSError(EACCES, "MemFS: cannot rename root");
  }
  std::string oldName = oldParts.back();
  oldParts.pop_back();

  std::string oldParentPath = "/";
  for (size_t i = 0; i < oldParts.size(); ++i) {
    oldParentPath += (i > 0 ? "/" : "") + oldParts[i];
  }
  if (oldParts.empty())
    oldParentPath = "/";

  auto [oldParent, _p2, _n] = walkTo(oldParentPath);
  auto it = oldParent->children.find(oldName);
  if (it == oldParent->children.end()) {
    throw VFSError(ENOENT, "MemFS: no such file: '" + oldPath + "'");
  }

  auto newParts = splitPath(newPath);
  if (newParts.empty()) {
    throw VFSError(EACCES, "MemFS: cannot rename to root");
  }
  std::string newName = newParts.back();
  newParts.pop_back();

  std::string newParentPath = "/";
  for (size_t i = 0; i < newParts.size(); ++i) {
    newParentPath += (i > 0 ? "/" : "") + newParts[i];
  }
  if (newParts.empty())
    newParentPath = "/";

  auto [newParent, _p4, _n2] = walkTo(newParentPath);

  if (newParent->children.contains(newName)) {
    throw VFSError(EEXIST, "MemFS: file exists: '" + newPath + "'");
  }

  auto node = std::move(it->second);
  oldParent->children.erase(it);
  newParent->children[newName] = std::move(node);
}

void MemFSBackend::copyFile(const std::string &src, const std::string &dst) {
  auto [srcNode, _p1, _n1] = walkTo(src);
  if (srcNode->stat.isDir()) {
    throw VFSError(EISDIR, "MemFS: is a directory: '" + src + "'");
  }
  writeFile(dst, srcNode->data.data(), srcNode->data.size());
}

void MemFSBackend::truncate(const std::string &path, int64_t size) {
  auto [node, _p, _n] = walkTo(path);
  if (node->stat.isDir()) {
    throw VFSError(EISDIR, "MemFS: is a directory: '" + path + "'");
  }
  if (size > static_cast<int64_t>(node->data.size())) {
    node->data.resize(size, 0);
  } else {
    node->data.resize(size);
  }
  node->stat.size = size;
  node->stat.mtimeMs = nowMs();
  node->stat.ctimeMs = nowMs();
}

void MemFSBackend::utimes(const std::string &path, int64_t atimeMs, int64_t mtimeMs) {
  auto [node, _p, _n] = walkTo(path);
  node->stat.atimeMs = atimeMs;
  node->stat.mtimeMs = mtimeMs;
}

int MemFSBackend::access(const std::string &path, int mode) {
  auto [node, _p, _n] = walkTo(path);

  if (mode == access_mode::F_OK)
    return 0;

  uint32_t stMode = node->stat.mode;
  if ((mode & access_mode::R_OK) && !(stMode & 0400))
    return EACCES;
  if ((mode & access_mode::W_OK) && !(stMode & 0200))
    return EACCES;
  if ((mode & access_mode::X_OK) && !(stMode & 0100))
    return EACCES;

  return 0;
}

} // namespace sandboxed_fs
