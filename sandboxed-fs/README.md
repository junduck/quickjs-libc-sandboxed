# sandboxed-fs

Mount-based virtual filesystem. C++23. Zero dependencies. No exceptions — every operation returns `std::expected<T, int>` with POSIX errno.

## Files

```
sandboxed-fs/
├── include/sandboxed-fs/
│   ├── vfs_types.hpp    Stat, Perm enum, access_mode constants
│   ├── vfs_backend.hpp  Abstract VFSBackend (16 pure virtuals)
│   ├── vfs_real.hpp     RealFSBackend (realpath-sandboxed disk fs)
│   ├── vfs_mem.hpp      MemFSBackend (in-memory tree)
│   └── vfs.hpp          VirtualFS (mount router, composite)
├── src/
│   ├── vfs.cpp          VirtualFS::resolve, normalize, dispatch macros
│   ├── vfs_real.cpp     RealFS impl: canonical/weakly_canonical gate
│   └── vfs_mem.cpp      MemFS impl: recursive map walk
├── tests/
│   ├── test_vfs.cpp      32 unit tests (MemFS, RealFS, VirtualFS)
│   ├── test_security.cpp 48 isolation tests (escape, symlink, mount)
│   └── bench_vfs.cpp     Micro-benchmarks (read/write/route overhead)
└── CMakeLists.txt
```

## Module Graph

```
VFSBackend  (abstract, vfs_backend.hpp)
  ├── RealFSBackend  (disk, canonical gates)
  ├── MemFSBackend   (in-memory tree)
  └── VirtualFS      (mount router, IS-A VFSBackend)
```

## VirtualFS Resolution Algorithm

```
resolve(virtualPath, neededPerm):
  1. normPath = normalize(virtualPath)      // collapse . , resolve .. , // → /
  2. for each mount in m_mounts (sorted prefix-descending):
       if normPath == mount.prefix  OR  normPath.starts_with(mount.prefix + "/"):
         if (mount.perm & neededPerm) != neededPerm → return EACCES
         subPath = normPath.drop(mount.prefix.size())
         if subPath empty → subPath = "/"
         return {mount.backend, subPath}
  3. return EACCES
```

Mounts sorted longest-first. `VirtualFS::mount()` inserts via `lower_bound` (O(n)), not full sort.

## RealFS Sandbox Gate

```
resolveExisting(path):    // for ops on paths that must exist
  full = makePath(path)   // root + stripLeadingSlash(path)
  canonical = fs::canonical(full)
  if !pathStartsWith(canonical, m_root) → EACCES

resolveCreating(path):    // for ops creating new paths
  full = makePath(path)
  resolved = fs::weakly_canonical(full)
  if !pathStartsWith(resolved, m_root) → EACCES

makePath(path):
  strip leading '/' from path
  return m_root / stripped  (but path("/","/") → m_root directly)
```

## VFSBackend Methods

All paths are relative to backend root.

```
// returns expected<T, int>
readFile(path)   → expected<string, int>
readFileBytes(path) → expected<vector<uint8_t>, int>
readdir(path)    → expected<vector<string>, int>
stat(path)       → expected<Stat, int>          // follows symlinks
lstat(path)      → expected<Stat, int>          // does not follow

// returns expected<void, int>
writeFile(path, data_ptr, len)
appendFile(path, data_ptr, len)
unlink(path)
mkdir(path, recursive, mode=0777)
rmdir(path)
rename(oldPath, newPath)
copyFile(src, dst)
truncate(path, size)
utimes(path, atimeMs, mtimeMs)

// returns bool
exists(path)     // false on sandbox rejection OR missing file

// returns int (0 or errno)
access(path, mode)  // mode: F_OK=0, R_OK=4, W_OK=2, X_OK=1
```

## MemFS Internal Structure

```
Node {
  Stat stat;
  vector<uint8_t> data;                          // file content
  map<string, unique_ptr<Node>> children;        // directory entries
}

walkTo(path) → splits path on '/', walks tree, returns Node* or ENOENT
parentDir(path) → walks parent, returns parent Node* or error
```

## Usage Pattern (Copy-Paste)

```cpp
#include "sandboxed-fs/vfs.hpp"
#include "sandboxed-fs/vfs_mem.hpp"
#include "sandboxed-fs/vfs_real.hpp"

using namespace sandboxed_fs;

auto bundle = std::make_shared<MemFSBackend>();
auto sandbox = std::make_shared<RealFSBackend>("/var/sandbox/user/");

bundle->writeFile("/app.js", "console.log('hi')", 18);

VirtualFS vfs({
    {"/bundle",       Perm::Read,                     bundle},
    {"/home/sandbox", Perm::Read | Perm::Write,       sandbox},
});

// read
auto r = vfs.readFile("/bundle/app.js");
if (r) use(*r);
else handle(r.error());

// write
auto w = vfs.writeFile("/home/sandbox/out.txt", "data", 4);
if (!w) handle(w.error());

// error check
auto bad = vfs.writeFile("/bundle/hack.js", "x", 1);
assert(!bad.has_value() && bad.error() == EACCES);

// iterate dir
auto entries = vfs.readdir("/bundle");
if (entries) for (const auto& name : *entries) { ... }

// stat
auto st = vfs.stat("/bundle/app.js");
if (st) { st->size; st->isFile(); st->isDir(); }
```

## Stat Fields

```
Stat { dev, ino, mode, nlink, uid, gid, rdev, size, blksize, blocks,
       atimeMs, mtimeMs, ctimeMs }

mode bits:
  0x8000  S_IFREG    isFile()
  0x4000  S_IFDIR    isDir()
  0xA000  S_IFLNK    isSymlink()
  0x1000  S_IFIFO    isFifo()
  0x2000  S_IFCHR    isChr()
  0x6000  S_IFBLK    isBlk()
  0xC000  S_IFSOCK   isSock()

RealFSBackend fills: mode, size, nlink, mtimeMs. Others = 0.
MemFSBackend fills:  all fields.
```

## Error Codes

```
ENOENT (2)    File/dir not found
EACCES (13)   Sandbox permission denied, RO mount, unmounted path
EISDIR (21)   Is a directory
ENOTDIR (20)  Not a directory
ENOTEMPTY (66) Directory not empty
EEXIST (17)   File already exists
EXDEV (18)    Cross-device rename
EINVAL (22)   Invalid argument (negative truncate size)
EIO (5)       I/O write failure
```

## Sandbox Guarantees

```
.. traversal       ✓ blocked  VirtualFS: prefix match + RealFS: canonical gate
symlink escape     ✓ blocked  fs::canonical resolves links → prefix check fails
// double slash    ✓ blocked  normalized to /
write to RO mount  ✓ blocked  Perm check before backend dispatch
unmounted path     ✓ blocked  no mount match → EACCES
mount prefix conf. ✓ blocked  /bundlex ≠ /bundle; exact or prefix+'/' match
cross-mount rename ✓ blocked  different backends → EXDEV
root write/unlink  ✓ blocked  MemFS: EACCES; VirtualFS: prefix match
exists() bypass    ✓ blocked  RealFS::exists → resolveExisting (canonical gate)
access() bypass    ✓ blocked  VirtualFS::access → resolve(neededPermForAccess(mode))
```

## Build

```
cmake -B build -DBUILD_TESTS=ON
cmake --build build
ctest --test-dir build          # 80 tests: 32 unit + 48 security
build/sandboxed-fs/tests/bench_vfs  # benchmarks
```

Requires: C++23 compiler, CMake ≥ 3.20. GTest/GBench auto-fetched via FetchContent.
