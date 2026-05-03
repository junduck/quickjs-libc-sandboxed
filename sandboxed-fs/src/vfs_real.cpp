#include "sandboxed-fs/vfs_real.hpp"

#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
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
  auto [mm, _] = std::mismatch(prefix.begin(), prefix.end(), path.begin(), path.end());
  return mm == prefix.end();
}

static std::string_view stripLeadingSlash(const std::string &path) {
  std::string_view sv = path;
  auto pos = sv.find_first_not_of('/');
  if (pos != std::string_view::npos)
    sv.remove_prefix(pos);
  else
    sv = {};
  return sv;
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

RealFSBackend::RealFSBackend(fs::path realRoot) {
  std::error_code ec;
  m_root = fs::canonical(fs::absolute(realRoot), ec);
  if (ec)
    throw std::runtime_error("RealFSBackend: cannot canonicalize root '" + realRoot.string() + "': " + ec.message());
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

std::expected<std::string, int> RealFSBackend::resolveExisting(const std::string &path) {
  auto fullPath = makePath(path);
  std::error_code ec;
  auto canonical = fs::canonical(fullPath, ec);
  if (ec)
    return std::unexpected(ec.value());
  if (!pathStartsWith(canonical, m_root))
    return std::unexpected(EACCES);
  return canonical.string();
}

std::expected<std::string, int> RealFSBackend::resolveCreating(const std::string &path) {
  auto fullPath = makePath(path);
  std::error_code ec;
  auto resolved = fs::weakly_canonical(fullPath, ec);
  if (ec)
    return std::unexpected(ec.value());
  if (!pathStartsWith(resolved, m_root))
    return std::unexpected(EACCES);

  // Guard against dangling symlinks: weakly_canonical does not resolve
  // symlink targets that don't exist, so a symlink pointing outside
  // the sandbox would pass the prefix check.  Resolve the target.
  auto linkSt = fs::symlink_status(resolved, ec);
  if (!ec && fs::is_symlink(linkSt)) {
    auto target = fs::read_symlink(resolved, ec);
    if (ec)
      return std::unexpected(ec.value());
    auto absTarget = target.is_absolute() ? target : resolved.parent_path() / target;
    auto canonTarget = fs::weakly_canonical(absTarget, ec);
    if (ec)
      return std::unexpected(EACCES);
    if (!pathStartsWith(canonTarget, m_root))
      return std::unexpected(EACCES);
  }

  return resolved.string();
}

// ---------------------------------------------------------------------------
// readFile / readFileBytes  (reads til EOF — no pre-size TOCTOU)
// ---------------------------------------------------------------------------

std::expected<std::string, int> RealFSBackend::readFile(const std::string &path) {
  auto r = resolveExisting(path);
  if (!r)
    return std::unexpected(r.error());

  std::ifstream in(*r, std::ios::binary);
  if (!in)
    return std::unexpected(EACCES);

  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::expected<std::vector<uint8_t>, int> RealFSBackend::readFileBytes(const std::string &path) {
  auto r = resolveExisting(path);
  if (!r)
    return std::unexpected(r.error());

  std::ifstream in(*r, std::ios::binary);
  if (!in)
    return std::unexpected(EACCES);

  return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// ---------------------------------------------------------------------------
// writeFile / appendFile
// ---------------------------------------------------------------------------

std::expected<void, int> RealFSBackend::writeFile(const std::string &path, const void *data, size_t len) {
  auto r = resolveCreating(path);
  if (!r)
    return std::unexpected(r.error());

  std::ofstream out(*r, std::ios::binary | std::ios::trunc);
  if (!out)
    return std::unexpected(EACCES);
  out.write(static_cast<const char *>(data), static_cast<std::streamsize>(len));
  if (!out)
    return std::unexpected(EIO);
  return {};
}

std::expected<void, int> RealFSBackend::appendFile(const std::string &path, const void *data, size_t len) {
  auto r = resolveCreating(path);
  if (!r)
    return std::unexpected(r.error());

  std::ofstream out(*r, std::ios::binary | std::ios::app);
  if (!out)
    return std::unexpected(EACCES);
  out.write(static_cast<const char *>(data), static_cast<std::streamsize>(len));
  if (!out)
    return std::unexpected(EIO);
  return {};
}

// ---------------------------------------------------------------------------
// unlink / mkdir / rmdir / readdir
// ---------------------------------------------------------------------------

std::expected<void, int> RealFSBackend::unlink(const std::string &path) {
  auto r = resolveExisting(path);
  if (!r)
    return std::unexpected(r.error());

  std::error_code ec;
  fs::remove(*r, ec);
  if (ec)
    return std::unexpected(ec.value());
  return {};
}

std::expected<void, int> RealFSBackend::mkdir(const std::string &path, bool recursive, uint32_t mode) {
  auto r = resolveCreating(path);
  if (!r)
    return std::unexpected(r.error());

  std::error_code ec;
  if (recursive)
    fs::create_directories(*r, ec);
  else
    fs::create_directory(*r, ec);
  if (ec)
    return std::unexpected(ec.value());

  if (mode != 0777) {
    fs::permissions(*r, modeToPerms(mode), fs::perm_options::replace, ec);
    if (ec)
      return std::unexpected(ec.value());
  }
  return {};
}

std::expected<void, int> RealFSBackend::rmdir(const std::string &path) {
  auto r = resolveExisting(path);
  if (!r)
    return std::unexpected(r.error());

  std::error_code ec;
  fs::remove(*r, ec);
  if (ec)
    return std::unexpected(ec.value());
  return {};
}

std::expected<std::vector<std::string>, int> RealFSBackend::readdir(const std::string &path) {
  auto r = resolveExisting(path);
  if (!r)
    return std::unexpected(r.error());

  std::vector<std::string> entries;
  std::error_code ec;
  for (const auto &entry : fs::directory_iterator(*r, ec))
    entries.push_back(entry.path().filename().string());
  if (ec)
    return std::unexpected(ec.value());
  return entries;
}

// ---------------------------------------------------------------------------
// stat / lstat
// ---------------------------------------------------------------------------

static std::expected<Stat, int> buildStat(const fs::path &resolved, fs::file_status st) {
  Stat s;
  s.mode = fileTypeToMode(st.type()) | permsToMode(st.permissions());

  std::error_code szEc;
  s.size = static_cast<int64_t>(fs::file_size(resolved, szEc));
  if (szEc)
    return std::unexpected(szEc.value());

  std::error_code nlinkEc;
  auto nlink = fs::hard_link_count(resolved, nlinkEc);
  s.nlink = nlinkEc ? 1 : nlink;

  std::error_code timeEc;
  auto ftime = fs::last_write_time(resolved, timeEc);
  if (!timeEc) {
    auto sysTime = fs::file_time_type::clock::to_sys(ftime);
    s.mtimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(sysTime.time_since_epoch()).count();
  }

  return s;
}

std::expected<Stat, int> RealFSBackend::stat(const std::string &path) {
  auto r = resolveExisting(path);
  if (!r)
    return std::unexpected(r.error());

  std::error_code ec;
  auto st = fs::status(*r, ec);
  if (ec)
    return std::unexpected(ec.value());
  return buildStat(*r, st);
}

std::expected<Stat, int> RealFSBackend::lstat(const std::string &path) {
  auto r = resolveExisting(path);
  if (!r)
    return std::unexpected(r.error());

  std::error_code ec;
  auto st = fs::symlink_status(*r, ec);
  if (ec)
    return std::unexpected(ec.value());
  return buildStat(*r, st);
}

// ---------------------------------------------------------------------------
// exists  (now uses resolveExisting — no sandbox bypass)
// ---------------------------------------------------------------------------

bool RealFSBackend::exists(const std::string &path) { return resolveExisting(path).has_value(); }

// ---------------------------------------------------------------------------
// rename / copyFile
// ---------------------------------------------------------------------------

std::expected<void, int> RealFSBackend::rename(const std::string &oldPath, const std::string &newPath) {
  auto oldR = resolveExisting(oldPath);
  if (!oldR)
    return std::unexpected(oldR.error());
  auto newR = resolveCreating(newPath);
  if (!newR)
    return std::unexpected(newR.error());

  std::error_code ec;
  fs::rename(*oldR, *newR, ec);
  if (ec)
    return std::unexpected(ec.value());
  return {};
}

std::expected<void, int> RealFSBackend::copyFile(const std::string &src, const std::string &dst) {
  auto srcR = resolveExisting(src);
  if (!srcR)
    return std::unexpected(srcR.error());
  auto dstR = resolveCreating(dst);
  if (!dstR)
    return std::unexpected(dstR.error());

  std::error_code ec;
  fs::copy_file(*srcR, *dstR, ec);
  if (ec)
    return std::unexpected(ec.value());
  return {};
}

// ---------------------------------------------------------------------------
// truncate / utimes / access
// ---------------------------------------------------------------------------

std::expected<void, int> RealFSBackend::truncate(const std::string &path, int64_t size) {
  if (size < 0)
    return std::unexpected(EINVAL);
  auto r = resolveExisting(path);
  if (!r)
    return std::unexpected(r.error());

  std::error_code ec;
  fs::resize_file(*r, static_cast<uintmax_t>(size), ec);
  if (ec)
    return std::unexpected(ec.value());
  return {};
}

std::expected<void, int> RealFSBackend::utimes(const std::string &path, int64_t /*atimeMs*/, int64_t mtimeMs) {
  // std::filesystem only exposes last_write_time; atime cannot be set portably.
  auto r = resolveExisting(path);
  if (!r)
    return std::unexpected(r.error());

  std::error_code ec;
  using Dur = fs::file_time_type::duration;
  fs::last_write_time(*r, fs::file_time_type(std::chrono::duration_cast<Dur>(std::chrono::milliseconds(mtimeMs))), ec);
  if (ec)
    return std::unexpected(ec.value());
  return {};
}

int RealFSBackend::access(const std::string &path, int mode) {
  auto r = resolveExisting(path);
  if (!r)
    return r.error();

  std::error_code ec;
  auto st = fs::status(*r, ec);
  if (ec)
    return ENOENT;
  if (mode == access_mode::F_OK)
    return 0;

  auto perms = st.permissions();
  if ((mode & access_mode::R_OK) && (perms & fs::perms::owner_read) == fs::perms::none)
    return EACCES;
  if ((mode & access_mode::W_OK) && (perms & fs::perms::owner_write) == fs::perms::none)
    return EACCES;
  if ((mode & access_mode::X_OK) && (perms & fs::perms::owner_exec) == fs::perms::none)
    return EACCES;
  return 0;
}

} // namespace sandboxed_fs
