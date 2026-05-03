# quickjs-libc-sandboxed

A sandboxed standard library for QuickJS — No engine patches. No exceptions. C++23 + stdlib + Boost only.

## Goal

Build a drop-in replacement for QuickJS's `libc` (`std` + `os` modules) that sandboxes every OS interaction. When a JS worker calls `readFile("/etc/passwd")`, it should get `EACCES`, not raw POSIX access.

Architecture: C++ primitives → C bridge (`extern "C"`) → QuickJS `JSCFunctionListEntry` modules.

## Design Rules

| Rule | Why |
|------|-----|
| `std::expected<T, int>` returns, never exceptions | Clean C-ABI bridge later |
| C++23 standard library + Boost only | No platform `#ifdef`s |
| Every op goes through a single resolution gate | No scattered security checks |
| No engine core changes | Drop-in libc replacement |

## Progress

```
✅ sandboxed-fs       VFS: mount-based filesystem with MemFS and realpath-sandboxed RealFS
                      82 tests (33 unit + 49 security), side-channel guarded

📋 sandboxed-os       Host info masking: hostname, cpus, memory, platform → fixed/dummy values
                      Purely data-returning, no syscalls. Per-worker config struct.

📋 sandboxed-proc     Process isolation: per-worker env bindings, argv, exit scoped to worker
                      No fork/exec/spawn. pid=1, ppid=0, cwd="/".

📋 sandboxed-net      Socket policy: TCP/UDP allowed, Unix domain SOCKETS BLOCKED
                      Unix sockets live in filesystem namespace → VFS conflict.
                      Wraps asio transport layer, verifyAddress rejects path-like addrs.

📋 sandboxed-rand     Entropy passthrough: wraps std::random_device. Side effect unavoidable.

📋 QuickJS bridge      C++ → C shim layer. Maps std::expected returns to JS values/errors.
                      JS_NewCModule pattern from quickjs-libc.c.

📋 Non-sandboxed      url, encoding, crypto-hash, stream, path, timers, fetch/http (boost.beast)
                      Pure logic or controlled network — no sandbox policy needed.
```

## Project Layout

```
quickjs-libc-sandboxed/
├── CMakeLists.txt                   Root build
├── README.md                        ← this file
├── sandbox-plan.md                  Module plan and socket policy
├── cmake/
│   └── GoogleBenchAndTest.cmake     FetchContent for GTest + GBench
├── sandboxed-fs/                    ✅ VFS — done
│   ├── CMakeLists.txt
│   ├── README.md
│   ├── include/sandboxed-fs/        vfs_types, vfs_backend, vfs_real, vfs_mem, vfs.hpp
│   ├── src/                         vfs.cpp, vfs_real.cpp, vfs_mem.cpp
│   └── tests/                       test_vfs, test_security, demo_bug, demo_escape, bench_vfs
├── sandboxed-os/                    📋 planned
├── sandboxed-proc/                  📋 planned
├── sandboxed-net/                   📋 planned
├── sandboxed-rand/                  📋 planned
├── quickjs/                         QuickJS engine (upstream, unmodified)
├── quickjs.h                        Symlink to quickjs/quickjs.h
└── refs/                            Reference implementations
```

## Build

```
cmake -B build -DBUILD_TESTS=ON
cmake --build build
ctest --test-dir build
```

C++23 compiler, CMake ≥ 3.20.
