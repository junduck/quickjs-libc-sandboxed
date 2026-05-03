#pragma once

#include <cstdint>

namespace sandboxed_fs {

// Bitmask controlling which operations a mount permits.
enum class Perm : uint8_t {
  None = 0,
  Read = 1 << 0,
  Write = 1 << 1,
};

constexpr Perm operator|(Perm a, Perm b) { return static_cast<Perm>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b)); }
constexpr Perm operator&(Perm a, Perm b) { return static_cast<Perm>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b)); }
constexpr bool hasRead(Perm p) { return (static_cast<uint8_t>(p & Perm::Read)) != 0; }
constexpr bool hasWrite(Perm p) { return (static_cast<uint8_t>(p & Perm::Write)) != 0; }

inline constexpr Perm ReadWrite = Perm::Read | Perm::Write;

// Platform-neutral stat result.  Timestamps are in milliseconds since epoch.
struct Stat {
  uint64_t dev = 0;
  uint64_t ino = 0;
  uint32_t mode = 0;
  uint64_t nlink = 0;
  uint32_t uid = 0;
  uint32_t gid = 0;
  uint64_t rdev = 0;
  int64_t size = 0;
  int64_t blksize = 0;
  int64_t blocks = 0;
  int64_t atimeMs = 0;
  int64_t mtimeMs = 0;
  int64_t ctimeMs = 0;

  bool isDir() const { return (mode & 0xF000) == 0x4000; }
  bool isFile() const { return (mode & 0xF000) == 0x8000; }
  bool isSymlink() const { return (mode & 0xF000) == 0xA000; }
};

// Standard access(2) mode constants.
namespace access_mode {
constexpr int F_OK = 0;
constexpr int R_OK = 4;
constexpr int W_OK = 2;
constexpr int X_OK = 1;
} // namespace access_mode

} // namespace sandboxed_fs
