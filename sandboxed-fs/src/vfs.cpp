#include "sandboxed-fs/vfs.hpp"

#include <algorithm>
#include <cerrno>
#include <expected>
#include <sstream>
#include <string>

namespace sandboxed_fs {

static void checkBackendConflict(const std::vector<MountEntry> &mounts) {
  for (size_t i = 0; i < mounts.size(); ++i) {
    for (size_t j = i + 1; j < mounts.size(); ++j) {
      if (mounts[i].backend.get() == mounts[j].backend.get() &&
          mounts[i].perm != mounts[j].perm) {
        throw std::invalid_argument(
            "VirtualFS: cannot share same backend with conflicting permissions "
            "(mount '" + mounts[i].prefix + "' has perm " +
            std::to_string(static_cast<int>(mounts[i].perm)) +
            ", mount '" + mounts[j].prefix + "' has perm " +
            std::to_string(static_cast<int>(mounts[j].perm)) + ")");
      }
    }
  }
}

VirtualFS::VirtualFS(std::vector<MountEntry> mounts) : m_mounts(std::move(mounts)) {
  checkBackendConflict(m_mounts);
  std::sort(m_mounts.begin(), m_mounts.end(),
            [](const MountEntry &a, const MountEntry &b) { return a.prefix.size() > b.prefix.size(); });
}

void VirtualFS::mount(const MountEntry &entry) {
  for (const auto &m : m_mounts) {
    if (m.backend.get() == entry.backend.get() && m.perm != entry.perm) {
      throw std::invalid_argument(
          "MountEntry: cannot share same backend with conflicting "
          "permissions (mount '" +
          m.prefix + "' has perm " + std::to_string(static_cast<int>(m.perm)) +
          ", new mount '" + entry.prefix + "' has perm " +
          std::to_string(static_cast<int>(entry.perm)) + ")");
    }
  }
  auto it = std::lower_bound(m_mounts.begin(), m_mounts.end(), entry,
                             [](const MountEntry &a, const MountEntry &b) { return a.prefix.size() > b.prefix.size(); });
  m_mounts.insert(it, entry);
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

  size_t total = 0;
  for (const auto &seg : parts)
    total += seg.size() + 1;
  std::string result;
  result.reserve(total);
  for (const auto &seg : parts) {
    result += "/";
    result += seg;
  }
  return result;
}

std::expected<VirtualFS::Resolved, int> VirtualFS::resolve(const std::string &path, Perm neededPerm) {
  auto normPath = normalize(path);

  for (const auto &mount : m_mounts) {
    // Root mount matches everything (catches at end since sorted last).
    if (mount.prefix == "/") {
      if ((mount.perm & neededPerm) != neededPerm)
        return std::unexpected(EACCES);
      return Resolved{mount.backend.get(), normPath};
    }

    if (normPath.size() < mount.prefix.size())
      continue;
    if (normPath.compare(0, mount.prefix.size(), mount.prefix) != 0)
      continue;
    if (normPath.size() != mount.prefix.size() && normPath[mount.prefix.size()] != '/')
      continue;

    if ((mount.perm & neededPerm) != neededPerm)
      return std::unexpected(EACCES);

    if (normPath == mount.prefix)
      return Resolved{mount.backend.get(), "/"};

    std::string subPath = normPath.substr(mount.prefix.size());
    if (subPath.empty())
      subPath = "/";
    return Resolved{mount.backend.get(), subPath};
  }

  return std::unexpected(EACCES);
}

static Perm neededPermForAccess(int mode) {
  Perm p = Perm::None;
  if (mode & access_mode::kRead)
    p = p | Perm::Read;
  if (mode & access_mode::kWrite)
    p = p | Perm::Write;
  return p;
}

#define VFS_DISPATCH_READ(method, path)                                                                                                              \
  do {                                                                                                                                               \
    auto _r = resolve(path, Perm::Read);                                                                                                             \
    if (!_r)                                                                                                                                         \
      return std::unexpected(_r.error());                                                                                                            \
    return _r->backend->method(_r->subPath);                                                                                                         \
  } while (0)

#define VFS_DISPATCH_WRITE(method, path, ...)                                                                                                        \
  do {                                                                                                                                               \
    auto _r = resolve(path, Perm::Write);                                                                                                            \
    if (!_r)                                                                                                                                         \
      return std::unexpected(_r.error());                                                                                                            \
    return _r->backend->method(_r->subPath __VA_OPT__(, ) __VA_ARGS__);                                                                              \
  } while (0)

std::expected<std::string, int> VirtualFS::readFile(const std::string &path) { VFS_DISPATCH_READ(readFile, path); }

std::expected<std::vector<uint8_t>, int> VirtualFS::readFileBytes(const std::string &path) { VFS_DISPATCH_READ(readFileBytes, path); }

std::expected<void, int> VirtualFS::writeFile(const std::string &path, const void *data, size_t len) {
  VFS_DISPATCH_WRITE(writeFile, path, data, len);
}

std::expected<void, int> VirtualFS::appendFile(const std::string &path, const void *data, size_t len) {
  VFS_DISPATCH_WRITE(appendFile, path, data, len);
}

std::expected<void, int> VirtualFS::unlink(const std::string &path) { VFS_DISPATCH_WRITE(unlink, path); }

std::expected<void, int> VirtualFS::mkdir(const std::string &path, bool recursive, uint32_t mode) {
  VFS_DISPATCH_WRITE(mkdir, path, recursive, mode);
}

std::expected<void, int> VirtualFS::rmdir(const std::string &path) { VFS_DISPATCH_WRITE(rmdir, path); }

std::expected<std::vector<std::string>, int> VirtualFS::readdir(const std::string &path) { VFS_DISPATCH_READ(readdir, path); }

std::expected<Stat, int> VirtualFS::stat(const std::string &path) { VFS_DISPATCH_READ(stat, path); }

std::expected<Stat, int> VirtualFS::lstat(const std::string &path) { VFS_DISPATCH_READ(lstat, path); }

std::expected<void, int> VirtualFS::truncate(const std::string &path, int64_t size) { VFS_DISPATCH_WRITE(truncate, path, size); }

std::expected<void, int> VirtualFS::utimes(const std::string &path, int64_t atimeMs, int64_t mtimeMs) {
  VFS_DISPATCH_WRITE(utimes, path, atimeMs, mtimeMs);
}

// -- non-macro: custom logic --------------------------------------------------

bool VirtualFS::exists(const std::string &path) {
  auto r = resolve(path, Perm::Read);
  if (!r)
    return false;
  return r->backend->exists(r->subPath);
}

std::expected<void, int> VirtualFS::rename(const std::string &oldPath, const std::string &newPath) {
  auto oldR = resolve(oldPath, Perm::Write);
  if (!oldR)
    return std::unexpected(oldR.error());
  auto newR = resolve(newPath, Perm::Write);
  if (!newR)
    return std::unexpected(newR.error());

  if (oldR->backend != newR->backend)
    return std::unexpected(EXDEV);
  return oldR->backend->rename(oldR->subPath, newR->subPath);
}

std::expected<void, int> VirtualFS::copyFile(const std::string &src, const std::string &dst) {
  auto srcR = resolve(src, Perm::Read);
  if (!srcR)
    return std::unexpected(srcR.error());
  auto dstR = resolve(dst, Perm::Write);
  if (!dstR)
    return std::unexpected(dstR.error());

  if (srcR->backend == dstR->backend)
    return srcR->backend->copyFile(srcR->subPath, dstR->subPath);

  auto data = srcR->backend->readFileBytes(srcR->subPath);
  if (!data)
    return std::unexpected(data.error());
  return dstR->backend->writeFile(dstR->subPath, data->data(), data->size());
}

int VirtualFS::access(const std::string &path, int mode) {
  Perm needed = neededPermForAccess(mode);
  auto r = resolve(path, needed);
  if (!r)
    return r.error();
  return r->backend->access(r->subPath, mode);
}

} // namespace sandboxed_fs
