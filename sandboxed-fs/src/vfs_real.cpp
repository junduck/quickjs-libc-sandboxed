#include "sandboxed-fs/vfs_real.hpp"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string_view>

namespace fs = std::filesystem;

namespace sandboxed_fs {

static constexpr uint32_t kTypeFifo = 0x1000;
static constexpr uint32_t kTypeChr = 0x2000;
static constexpr uint32_t kTypeDir = 0x4000;
static constexpr uint32_t kTypeBlk = 0x6000;
static constexpr uint32_t kTypeReg = 0x8000;
static constexpr uint32_t kTypeLink = 0xA000;
static constexpr uint32_t kTypeSock = 0xC000;

static uint32_t permsToMode(fs::perms p) {
  uint32_t m = 0;
  if ((p & fs::perms::owner_read) != fs::perms::none)
    m |= 0400;
  if ((p & fs::perms::owner_write) != fs::perms::none)
    m |= 0200;
  if ((p & fs::perms::owner_exec) != fs::perms::none)
    m |= 0100;
  if ((p & fs::perms::group_read) != fs::perms::none)
    m |= 0040;
  if ((p & fs::perms::group_write) != fs::perms::none)
    m |= 0020;
  if ((p & fs::perms::group_exec) != fs::perms::none)
    m |= 0010;
  if ((p & fs::perms::others_read) != fs::perms::none)
    m |= 0004;
  if ((p & fs::perms::others_write) != fs::perms::none)
    m |= 0002;
  if ((p & fs::perms::others_exec) != fs::perms::none)
    m |= 0001;
  return m;
}

static fs::perms modeToPerms(uint32_t mode) {
  fs::perms p = fs::perms::none;
  if (mode & 0400)
    p |= fs::perms::owner_read;
  if (mode & 0200)
    p |= fs::perms::owner_write;
  if (mode & 0100)
    p |= fs::perms::owner_exec;
  if (mode & 0040)
    p |= fs::perms::group_read;
  if (mode & 0020)
    p |= fs::perms::group_write;
  if (mode & 0010)
    p |= fs::perms::group_exec;
  if (mode & 0004)
    p |= fs::perms::others_read;
  if (mode & 0002)
    p |= fs::perms::others_write;
  if (mode & 0001)
    p |= fs::perms::others_exec;
  return p;
}

static uint32_t fileTypeToMode(fs::file_type t) {
  switch (t) {
  case fs::file_type::regular:
    return kTypeReg;
  case fs::file_type::directory:
    return kTypeDir;
  case fs::file_type::symlink:
    return kTypeLink;
  case fs::file_type::character:
    return kTypeChr;
  case fs::file_type::block:
    return kTypeBlk;
  case fs::file_type::fifo:
    return kTypeFifo;
  case fs::file_type::socket:
    return kTypeSock;
  default:
    return 0;
  }
}

static bool pathStartsWith(const fs::path &path, const fs::path &prefix) {
  auto [mm, _] =
      std::mismatch(prefix.begin(), prefix.end(), path.begin(), path.end());
  return mm == prefix.end();
}

static std::string_view stripLeadingSlash(const std::string &path) {
  std::string_view sv = path;
  while (!sv.empty() && sv.front() == '/')
    sv = sv.substr(1);
  return sv;
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

RealFSBackend::RealFSBackend(fs::path realRoot) {
  std::error_code ec;
  m_root = fs::canonical(fs::absolute(realRoot), ec);
  if (ec) {
    throw VFSError(ec.value(), "RealFSBackend: cannot canonicalize root '" +
                                   realRoot.string() + "': " + ec.message());
  }
}

// ---------------------------------------------------------------------------
// Path helpers
// ---------------------------------------------------------------------------

fs::path RealFSBackend::makePath(const std::string &path) const {
  if (path.empty() || path == "/")
    return m_root;
  auto rel = stripLeadingSlash(path);
  if (rel.empty())
    return m_root;
  return m_root / rel;
}

std::string RealFSBackend::resolveExisting(const std::string &path) {
  auto fullPath = makePath(path);
  std::error_code ec;
  auto canonical = fs::canonical(fullPath, ec);
  if (ec)
    throw VFSError(ec.value(),
                   "RealFSBackend: '" + path + "': " + ec.message());
  if (!pathStartsWith(canonical, m_root))
    throw VFSError(EACCES,
                   "RealFSBackend: path escapes sandbox: '" + path + "'");
  return canonical.string();
}

std::string RealFSBackend::resolveCreating(const std::string &path) {
  auto fullPath = makePath(path);
  std::error_code ec;
  auto resolved = fs::weakly_canonical(fullPath, ec);
  if (ec)
    throw VFSError(ec.value(),
                   "RealFSBackend: '" + path + "': " + ec.message());
  if (!pathStartsWith(resolved, m_root))
    throw VFSError(EACCES,
                   "RealFSBackend: path escapes sandbox: '" + path + "'");
  return resolved.string();
}

// ---------------------------------------------------------------------------
// readFile / readFileBytes
// ---------------------------------------------------------------------------

std::string RealFSBackend::readFile(const std::string &path) {
  auto resolved = resolveExisting(path);
  std::error_code ec;
  auto sz = static_cast<size_t>(fs::file_size(resolved, ec));
  if (ec)
    throw VFSError(ec.value(), "readFile '" + path + "': " + ec.message());

  std::ifstream in(resolved, std::ios::binary);
  if (!in)
    throw VFSError(EACCES, "readFile '" + path + "': cannot open");

  std::string data(sz, '\0');
  in.read(data.data(), static_cast<std::streamsize>(sz));
  return data;
}

std::vector<uint8_t> RealFSBackend::readFileBytes(const std::string &path) {
  auto resolved = resolveExisting(path);
  std::error_code ec;
  auto sz = static_cast<size_t>(fs::file_size(resolved, ec));
  if (ec)
    throw VFSError(ec.value(), "readFileBytes '" + path + "': " + ec.message());

  std::ifstream in(resolved, std::ios::binary);
  if (!in)
    throw VFSError(EACCES, "readFileBytes '" + path + "': cannot open");

  std::vector<uint8_t> data(sz);
  in.read(reinterpret_cast<char *>(data.data()),
          static_cast<std::streamsize>(sz));
  return data;
}

// ---------------------------------------------------------------------------
// writeFile / appendFile
// ---------------------------------------------------------------------------

void RealFSBackend::writeFile(const std::string &path, const void *data,
                              size_t len) {
  auto resolved = resolveCreating(path);
  std::ofstream out(resolved, std::ios::binary | std::ios::trunc);
  if (!out)
    throw VFSError(EACCES, "writeFile '" + path + "': cannot open");
  out.write(static_cast<const char *>(data), static_cast<std::streamsize>(len));
  if (!out)
    throw VFSError(EIO, "writeFile '" + path + "': write failed");
}

void RealFSBackend::appendFile(const std::string &path, const void *data,
                               size_t len) {
  auto resolved = resolveCreating(path);
  std::ofstream out(resolved, std::ios::binary | std::ios::app);
  if (!out)
    throw VFSError(EACCES, "appendFile '" + path + "': cannot open");
  out.write(static_cast<const char *>(data), static_cast<std::streamsize>(len));
  if (!out)
    throw VFSError(EIO, "appendFile '" + path + "': write failed");
}

// ---------------------------------------------------------------------------
// unlink / mkdir / rmdir / readdir
// ---------------------------------------------------------------------------

void RealFSBackend::unlink(const std::string &path) {
  auto resolved = resolveExisting(path);
  std::error_code ec;
  fs::remove(resolved, ec);
  if (ec)
    throw VFSError(ec.value(), "unlink '" + path + "': " + ec.message());
}

void RealFSBackend::mkdir(const std::string &path, bool recursive,
                          uint32_t mode) {
  auto resolved = resolveCreating(path);
  std::error_code ec;
  if (recursive) {
    fs::create_directories(resolved, ec);
  } else {
    fs::create_directory(resolved, ec);
  }
  if (ec)
    throw VFSError(ec.value(), "mkdir '" + path + "': " + ec.message());
  if (mode != 0777) {
    fs::permissions(resolved, modeToPerms(mode), fs::perm_options::replace, ec);
  }
}

void RealFSBackend::rmdir(const std::string &path) {
  auto resolved = resolveExisting(path);
  std::error_code ec;
  fs::remove(resolved, ec);
  if (ec)
    throw VFSError(ec.value(), "rmdir '" + path + "': " + ec.message());
}

std::vector<std::string> RealFSBackend::readdir(const std::string &path) {
  auto resolved = resolveExisting(path);
  std::vector<std::string> entries;
  std::error_code ec;
  for (const auto &entry : fs::directory_iterator(resolved, ec))
    entries.push_back(entry.path().filename().string());
  if (ec)
    throw VFSError(ec.value(), "readdir '" + path + "': " + ec.message());
  return entries;
}

// ---------------------------------------------------------------------------
// stat / lstat
// ---------------------------------------------------------------------------

static Stat buildStat(const fs::path &resolved, fs::file_status st,
                      bool /*followSymlinks*/) {
  Stat s;
  s.mode = fileTypeToMode(st.type()) | permsToMode(st.permissions());

  std::error_code ec;
  s.size = static_cast<int64_t>(fs::file_size(resolved, ec));
  if (ec)
    s.size = 0;

  auto nlink = fs::hard_link_count(resolved, ec);
  s.nlink = ec ? 1 : nlink;

  auto ftime = fs::last_write_time(resolved, ec);
  if (!ec) {
    auto sysTime = fs::file_time_type::clock::to_sys(ftime);
    s.mtimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    sysTime.time_since_epoch())
                    .count();
  }

  return s;
}

Stat RealFSBackend::stat(const std::string &path) {
  auto resolved = resolveExisting(path);
  std::error_code ec;
  auto st = fs::status(resolved, ec);
  if (ec)
    throw VFSError(ec.value(), "stat '" + path + "': " + ec.message());
  return buildStat(resolved, st, true);
}

Stat RealFSBackend::lstat(const std::string &path) {
  auto resolved = resolveExisting(path);
  std::error_code ec;
  auto st = fs::symlink_status(resolved, ec);
  if (ec)
    throw VFSError(ec.value(), "lstat '" + path + "': " + ec.message());
  return buildStat(resolved, st, false);
}

// ---------------------------------------------------------------------------
// exists
// ---------------------------------------------------------------------------

bool RealFSBackend::exists(const std::string &path) {
  auto fullPath = makePath(path);
  std::error_code ec;
  return fs::exists(fullPath, ec);
}

// ---------------------------------------------------------------------------
// rename / copyFile
// ---------------------------------------------------------------------------

void RealFSBackend::rename(const std::string &oldPath,
                           const std::string &newPath) {
  auto resolvedOld = resolveExisting(oldPath);
  auto resolvedNew = resolveCreating(newPath);
  std::error_code ec;
  fs::rename(resolvedOld, resolvedNew, ec);
  if (ec)
    throw VFSError(ec.value(), "rename '" + oldPath + "' -> '" + newPath +
                                   "': " + ec.message());
}

void RealFSBackend::copyFile(const std::string &src, const std::string &dst) {
  auto resolvedSrc = resolveExisting(src);
  auto resolvedDst = resolveCreating(dst);
  std::error_code ec;
  fs::copy_file(resolvedSrc, resolvedDst, ec);
  if (ec)
    throw VFSError(ec.value(),
                   "copyFile '" + src + "' -> '" + dst + "': " + ec.message());
}

// ---------------------------------------------------------------------------
// truncate / utimes / access
// ---------------------------------------------------------------------------

void RealFSBackend::truncate(const std::string &path, int64_t size) {
  auto resolved = resolveExisting(path);
  std::error_code ec;
  fs::resize_file(resolved, static_cast<uintmax_t>(size), ec);
  if (ec)
    throw VFSError(ec.value(), "truncate '" + path + "': " + ec.message());
}

void RealFSBackend::utimes(const std::string &path, int64_t /*atimeMs*/,
                           int64_t mtimeMs) {
  auto resolved = resolveExisting(path);
  std::error_code ec;
  fs::last_write_time(
      resolved, fs::file_time_type(std::chrono::milliseconds(mtimeMs)), ec);
  if (ec)
    throw VFSError(ec.value(), "utimes '" + path + "': " + ec.message());
}

int RealFSBackend::access(const std::string &path, int mode) {
  auto fullPath = makePath(path);
  std::error_code ec;
  auto st = fs::status(fullPath, ec);
  if (ec)
    return ENOENT;
  if (mode == access_mode::F_OK)
    return 0;

  auto perms = st.permissions();
  if ((mode & access_mode::R_OK) &&
      (perms & fs::perms::owner_read) == fs::perms::none)
    return EACCES;
  if ((mode & access_mode::W_OK) &&
      (perms & fs::perms::owner_write) == fs::perms::none)
    return EACCES;
  if ((mode & access_mode::X_OK) &&
      (perms & fs::perms::owner_exec) == fs::perms::none)
    return EACCES;
  return 0;
}

} // namespace sandboxed_fs
