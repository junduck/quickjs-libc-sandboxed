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
│   ├── vfs.cpp          VirtualFS::resolve, normalize, dispatch macros, backend conflict check
│   ├── vfs_real.cpp     RealFS impl: unified resolve() gate with canonical/weakly_canonical + side-channel guard
│   └── vfs_mem.cpp      MemFS impl: string_view splitPath, inline tree walk, find+emplace
├── tests/
│   ├── test_vfs.cpp         33 unit tests (MemFS, RealFS, VirtualFS)
│   ├── test_security.cpp    49 isolation tests (escape, symlink, mount, side-channel)
│   ├── demo_bug.cpp         Overlapping mounts + same backend demo
│   ├── demo_escape.cpp      Symlink escape demo
│   └── bench_vfs.cpp        Micro-benchmarks (read/write/route overhead)
└── CMakeLists.txt
```

## Module Graph

```
VFSBackend  (abstract, vfs_backend.hpp)
  ├── RealFSBackend  (disk, unified resolve gate with canonical/weakly_canonical)
  ├── MemFSBackend   (in-memory tree, string_view splitPath, inline parentDir)
  └── VirtualFS      (mount router, backendPath + perm conflict detection)
```

## VirtualFS Resolution Algorithm

```
resolve(virtualPath, neededPerm):
  1. normPath = normalize(virtualPath)        // collapse . , resolve .. , \\ → /
  2. for each mount in m_mounts (sorted prefix-descending):
       if mount.prefix == "/" → root catch-all
       if normPath doesn't start with prefix → continue
       if no '/' at prefix boundary (and not exact match) → continue
       if (mount.perm & neededPerm) != neededPerm → EACCES
       subPath = normPath == prefix ? backendPath : normPath.drop(prefix.size())
       if backendPath != "/" → prepend backendPath to subPath
       return {mount.backend, subPath}
  3. EACCES
```

Mounts sorted longest-first. Constructor and `mount()` both detect same-backend-with-conflicting-permissions and throw `std::invalid_argument`.

## MountEntry

```cpp
struct MountEntry {
    std::string prefix;                  // "/bundle", "/home/sandbox", "/"
    Perm perm;                           // Read, Write, or Read | Write
    std::shared_ptr<VFSBackend> backend;
    std::string backendPath = "/";       // path within backend this prefix maps to
};
```

`backendPath` handles overlapping mounts sharing a backend:
```
{"/app",     Perm::Read,          memFs},          // backendPath defaults to "/"
{"/app/sub", Perm::Read,          memFs,  "/sub"}, // explicit sub-path
```

## RealFS Sandbox Gate

```
resolve(path, ResolveMode) — single entry point for ALL filesystem access:

  Existing mode:  // path must exist (readFile, stat, unlink, ...)
    full = makePath(path)
    canonical = fs::canonical(full)
    if !isWithinRoot(canonical, m_root) → EACCES
    if canonical failed → weakly_canonical + isWithinRoot guard  // side-channel: never return ENOENT for outside-sandbox paths

  Creating mode:  // path may not exist yet (writeFile, mkdir, ...)
    full = makePath(path)
    try canonical first (handles existing targets)
    fallback: weakly_canonical + dangling-symlink guard
    guard: symlink_status → read_symlink → weakly_canonical → isWithinRoot check

isWithinRoot(path, root):
    rel = path.lexically_relative(root)
    return rel.empty() || *rel.begin() != ".."
```

## VFSBackend Methods

```
// returns expected<T, int>
readFile(path)     → expected<string, int>
readFileBytes(path) → expected<vector<uint8_t>, int>
readdir(path)      → expected<vector<string>, int>
stat(path)         → expected<Stat, int>          // follows symlinks
lstat(path)        → expected<Stat, int>          // does not follow

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
exists(path)       // false on sandbox rejection OR missing file

// returns int (0 or errno)
access(path, mode) // mode uses access_mode::kExist/kRead/kWrite/kExec
```

## MemFS Internal Structure

```
Node {
  Stat stat;
  vector<uint8_t> data;                          // file content
  map<string, unique_ptr<Node>> children;        // directory entries
}

walkTo(path)    → splits path with string_view tokenizer, walks tree, returns Node* or ENOENT
parentDir(path) → same split, pops last, inline tree walk to parent, returns Node* or error
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

// same backend, overlapping mounts — use backendPath
VirtualFS vfs2({
    {"/app",     Perm::Read,                    memFs},
    {"/app/sub", Perm::Read,                    memFs, "/sub"},
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

## access_mode Constants

```
access_mode::kExist  (0)    F_OK  — existence check
access_mode::kRead   (4)    R_OK  — read permission
access_mode::kWrite  (2)    W_OK  — write permission
access_mode::kExec   (1)    X_OK  — execute permission
```

Names deliberately avoid POSIX macro clashes (`F_OK`/`R_OK`/`W_OK`/`X_OK` are macros in `<unistd.h>`).

## Error Codes

```
ENOENT (2)    File/dir not found
EACCES (13)   Sandbox permission denied, RO mount, unmounted path, outside-sandbox path
EISDIR (21)   Is a directory
ENOTDIR (20)  Not a directory
ENOTEMPTY (66) Directory not empty
EEXIST (17)   File already exists
EXDEV (18)    Cross-device rename
EINVAL (22)   Invalid argument (negative truncate size, rename into own child)
EIO (5)       I/O write failure
```

## Sandbox Guarantees

```
.. traversal       ✓ blocked  VirtualFS: prefix match + RealFS: canonical gate
symlink escape     ✓ blocked  realpath resolves links; lexically_relative catches escaping .. in non-existing tail
dangling symlink   ✓ blocked  resolveCreating: symlink_status → read_symlink → target containment check
// double slash    ✓ blocked  normalized to /
write to RO mount  ✓ blocked  Perm check before backend dispatch
unmounted path     ✓ blocked  no mount match → EACCES
mount prefix conf. ✓ blocked  /bundlex ≠ /bundle; exact or prefix+'/' match
cross-mount rename ✓ blocked  different backends → EXDEV
root write/unlink  ✓ blocked  MemFS: EACCES; VirtualFS: prefix match
exists() bypass    ✓ blocked  RealFS::exists → resolve(Existing) (canonical gate)
access() bypass    ✓ blocked  VirtualFS::access → resolve(neededPermForAccess(mode))
same-backend RW/RO ✓ blocked  VirtualFS constructor + mount() throw on backend perm conflict
side-channel       ✓ blocked  Existing mode guard: never returns ENOENT for outside-sandbox paths
```

## Build

```
cmake -B build -DBUILD_TESTS=ON
cmake --build build
ctest --test-dir build          # 82 tests: 33 unit + 49 security
build/sandboxed-fs/tests/bench_vfs  # benchmarks
build/sandboxed-fs/tests/demo_bug   # overlapping mount demo
```

Requires: C++23 compiler, CMake ≥ 3.20. GTest/GBench auto-fetched via FetchContent.
