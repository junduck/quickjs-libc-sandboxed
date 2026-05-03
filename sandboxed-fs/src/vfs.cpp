#include "sandboxed-fs/vfs.hpp"

#include <algorithm>
#include <cerrno>
#include <sstream>
#include <string>

namespace sandboxed_fs {

VirtualFS::VirtualFS(std::vector<MountEntry> mounts) : m_mounts(std::move(mounts)) {
  std::sort(m_mounts.begin(), m_mounts.end(), [](const MountEntry &a, const MountEntry &b) { return a.prefix.size() > b.prefix.size(); });
}

void VirtualFS::mount(const MountEntry &entry) {
  m_mounts.push_back(entry);
  std::sort(m_mounts.begin(), m_mounts.end(), [](const MountEntry &a, const MountEntry &b) { return a.prefix.size() > b.prefix.size(); });
}

std::string VirtualFS::normalize(const std::string &path) {
  if (path.empty())
    return "/";

  std::string p = path;
  if (p[0] != '/' && p[0] != '\\')
    p = "/" + p;
  std::replace(p.begin(), p.end(), '\\', '/');

  std::vector<std::string> parts;
  std::istringstream ss(p);
  std::string part;
  while (std::getline(ss, part, '/')) {
    if (part.empty() || part == ".")
      continue;
    if (part == "..") {
      if (!parts.empty())
        parts.pop_back();
    } else {
      parts.push_back(part);
    }
  }

  if (parts.empty())
    return "/";

  std::string result;
  for (const auto &seg : parts) {
    result += "/" + seg;
  }
  return result;
}

VirtualFS::Resolved VirtualFS::resolve(const std::string &path, Perm neededPerm) {
  auto normPath = normalize(path);

  for (const auto &mount : m_mounts) {
    if (normPath == mount.prefix || normPath.starts_with(mount.prefix + "/")) {
      if (static_cast<uint8_t>(mount.perm & neededPerm) == 0) {
        throw VFSError(EACCES, "VirtualFS: permission denied for '" + path + "' (needed " + (neededPerm == Perm::Read ? "read" : "write") +
                                   ", mount has " +
                                   (mount.perm == ReadWrite    ? "readwrite"
                                    : mount.perm == Perm::Read ? "read"
                                                               : "none") +
                                   ")");
      }

      if (normPath == mount.prefix) {
        return {mount.backend.get(), "/"};
      }
      std::string subPath = normPath.substr(mount.prefix.size());
      if (subPath.empty())
        subPath = "/";
      return {mount.backend.get(), subPath};
    }
  }

  throw VFSError(EACCES, "VirtualFS: no mount for path '" + path + "'");
}

std::string VirtualFS::readFile(const std::string &path) {
  auto [backend, sub] = resolve(path, Perm::Read);
  return backend->readFile(sub);
}

std::vector<uint8_t> VirtualFS::readFileBytes(const std::string &path) {
  auto [backend, sub] = resolve(path, Perm::Read);
  return backend->readFileBytes(sub);
}

void VirtualFS::writeFile(const std::string &path, const void *data, size_t len) {
  auto [backend, sub] = resolve(path, Perm::Write);
  backend->writeFile(sub, data, len);
}

void VirtualFS::appendFile(const std::string &path, const void *data, size_t len) {
  auto [backend, sub] = resolve(path, Perm::Write);
  backend->appendFile(sub, data, len);
}

void VirtualFS::unlink(const std::string &path) {
  auto [backend, sub] = resolve(path, Perm::Write);
  backend->unlink(sub);
}

void VirtualFS::mkdir(const std::string &path, bool recursive, uint32_t mode) {
  auto [backend, sub] = resolve(path, Perm::Write);
  backend->mkdir(sub, recursive, mode);
}

void VirtualFS::rmdir(const std::string &path) {
  auto [backend, sub] = resolve(path, Perm::Write);
  backend->rmdir(sub);
}

std::vector<std::string> VirtualFS::readdir(const std::string &path) {
  auto [backend, sub] = resolve(path, Perm::Read);
  return backend->readdir(sub);
}

Stat VirtualFS::stat(const std::string &path) {
  auto [backend, sub] = resolve(path, Perm::Read);
  return backend->stat(sub);
}

Stat VirtualFS::lstat(const std::string &path) {
  auto [backend, sub] = resolve(path, Perm::Read);
  return backend->lstat(sub);
}

bool VirtualFS::exists(const std::string &path) {
  auto [backend, sub] = resolve(path, Perm::Read);
  return backend->exists(sub);
}

void VirtualFS::rename(const std::string &oldPath, const std::string &newPath) {
  auto [oldBackend, oldSub] = resolve(oldPath, Perm::Write);
  auto [newBackend, newSub] = resolve(newPath, Perm::Write);

  if (oldBackend != newBackend) {
    throw VFSError(EXDEV, "VirtualFS: cross-device rename not supported: '" + oldPath + "' -> '" + newPath + "'");
  }
  oldBackend->rename(oldSub, newSub);
}

void VirtualFS::copyFile(const std::string &src, const std::string &dst) {
  auto [srcBackend, srcSub] = resolve(src, Perm::Read);
  auto [dstBackend, dstSub] = resolve(dst, Perm::Write);

  if (srcBackend == dstBackend) {
    srcBackend->copyFile(srcSub, dstSub);
  } else {
    auto data = srcBackend->readFileBytes(srcSub);
    dstBackend->writeFile(dstSub, data.data(), data.size());
  }
}

void VirtualFS::truncate(const std::string &path, int64_t size) {
  auto [backend, sub] = resolve(path, Perm::Write);
  backend->truncate(sub, size);
}

void VirtualFS::utimes(const std::string &path, int64_t atimeMs, int64_t mtimeMs) {
  auto [backend, sub] = resolve(path, Perm::Write);
  backend->utimes(sub, atimeMs, mtimeMs);
}

int VirtualFS::access(const std::string &path, int mode) {
  auto [backend, sub] = resolve(path, Perm::Read);
  return backend->access(sub, mode);
}

} // namespace sandboxed_fs
