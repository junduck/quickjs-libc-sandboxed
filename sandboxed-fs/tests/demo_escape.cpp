#include "sandboxed-fs/vfs.hpp"
#include "sandboxed-fs/vfs_mem.hpp"
#include "sandboxed-fs/vfs_real.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace sfs = sandboxed_fs;
namespace fs = std::filesystem;

static void heading(const char *title) { std::cout << "\n\033[1m--- " << title << " ---\033[0m\n"; }

static void ok(const char *msg) { std::cout << "  \033[32m[OK]\033[0m " << msg << "\n"; }
static void bad(const char *msg) { std::cout << "  \033[31m[BROKEN]\033[0m " << msg << "\n"; }
static void note(const char *msg) { std::cout << "  " << msg << "\n"; }

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string readOutsideFile(const fs::path &p) {
  std::string s;
  std::ifstream(p) >> s;
  return s;
}

static bool outsideFileExists(const fs::path &p) { return fs::exists(p); }

static void cleanTmp(const fs::path &tmp) {
  std::error_code ec;
  fs::remove_all(tmp, ec);
}

// ---------------------------------------------------------------------------
// 1. RealFS — symlink / dot-dot escapes (should be blocked)
// ---------------------------------------------------------------------------

static void test_real_symlink_dotdot(const fs::path &tmp) {
  heading("RealFS: symlink + dot-dot escapes");

  auto sandbox = tmp / "sandbox";
  auto outside = tmp / "outside";
  std::error_code ec;
  fs::create_directories(sandbox, ec);
  fs::create_directories(outside, ec);
  std::ofstream(sandbox / "inside.txt") << "safe";
  std::ofstream(outside / "secret.txt") << "SECRET";

  sfs::RealFSBackend fs(sandbox);

  // dot-dot read
  auto r1 = fs.readFile("/../outside/secret.txt");
  if (!r1.has_value())
    ok("dot-dot read blocked");
  else
    bad("dot-dot read leaked outside data");

  // symlink to outside dir
  fs::create_symlink(outside, sandbox / "link");
  auto r2 = fs.readFile("/link/secret.txt");
  if (!r2.has_value())
    ok("symlink-to-outside read blocked");
  else
    bad("symlink-to-outside read leaked data");
  fs::remove(sandbox / "link", ec);

  // symlink to outside file
  fs::create_symlink(outside / "secret.txt", sandbox / "flink");
  auto r3 = fs.readFile("/flink");
  if (!r3.has_value())
    ok("symlink-to-outside-file read blocked");
  else
    bad("symlink-to-outside-file leaked data");
  fs::remove(sandbox / "flink", ec);

  // write through dot-dot
  auto r4 = fs.writeFile("/../outside/created.txt", "escape", 6);
  if (!r4.has_value() && !outsideFileExists(outside / "created.txt"))
    ok("dot-dot write blocked");
  else
    bad("dot-dot write created file outside sandbox");

  // dangling symlink write
  fs::create_symlink(outside / "noexist.txt", sandbox / "dangle");
  auto r5 = fs.writeFile("/dangle", "planted", 7);
  if (!r5.has_value() && !outsideFileExists(outside / "noexist.txt"))
    ok("dangling-symlink write blocked");
  else
    bad("dangling-symlink write escaped sandbox");
  fs::remove(sandbox / "dangle", ec);

  // symlink chain
  fs::create_symlink(outside, sandbox / "c1");
  fs::create_symlink(sandbox / "c1", sandbox / "c2");
  auto r6 = fs.readFile("/c2/secret.txt");
  if (!r6.has_value())
    ok("symlink-chain read blocked");
  else
    bad("symlink-chain read leaked data");
  fs::remove(sandbox / "c1", ec);
  fs::remove(sandbox / "c2", ec);
}

// ---------------------------------------------------------------------------
// 1b. RealFS — symlink escape via mkdir through symlink
// ---------------------------------------------------------------------------

static void test_real_symlink_mkdir_escape(const fs::path &tmp) {
  heading("RealFS: mkdir through symlink escapes sandbox");

  auto sandbox = tmp / "sandbox";
  auto outside = tmp / "outside";
  std::error_code ec;
  fs::create_directories(sandbox, ec);
  fs::create_directories(outside, ec);

  sfs::RealFSBackend fs(sandbox);

  fs::create_symlink(outside, sandbox / "link_dir");

  auto r = fs.mkdir("/link_dir/escaped_dir", false);
  if (!r.has_value()) {
    ok("mkdir through symlink to outside blocked");
  } else {
    if (fs::exists(outside / "escaped_dir"))
      bad("mkdir escaped sandbox — created directory outside");
    else if (fs::exists(sandbox / "link_dir" / "escaped_dir"))
      note("mkdir through symlink succeeded, target is inside sandbox");
    else
      note("mkdir succeeded but directory not in expected location");
  }

  fs::remove(sandbox / "link_dir", ec);
  fs::remove_all(outside / "escaped_dir", ec);
}

// ---------------------------------------------------------------------------
// 2. MemFS — readFileBytes should not destroy data
// ---------------------------------------------------------------------------

static void test_mem_readfilebytes() {
  heading("MemFS: readFileBytes data preservation");

  sfs::MemFSBackend fs;
  fs.writeFile("/data.txt", "important", 9);

  auto bytes = fs.readFileBytes("/data.txt");
  if (!bytes.has_value() || bytes->size() != 9) {
    bad("readFileBytes failed to return data");
    return;
  }

  auto content = fs.readFile("/data.txt");
  if (content.has_value() && *content == "important")
    ok("data still intact after readFileBytes (copy, not move)");
  else
    bad("readFileBytes destroyed data — subsequent readFile returns empty");
}

// ---------------------------------------------------------------------------
// 3. MemFS — copyFile must not overwrite directory
// ---------------------------------------------------------------------------

static void test_mem_copyfile_overwrite_dir() {
  heading("MemFS: copyFile vs directory");

  sfs::MemFSBackend fs;
  fs.mkdir("/dir", false);
  fs.writeFile("/dir/child.txt", "child", 5);
  fs.writeFile("/src.txt", "overwrite", 9);

  auto r = fs.copyFile("/src.txt", "/dir");
  if (!r.has_value()) {
    // copyFile rejected — check dir is intact
    if (fs.exists("/dir/child.txt"))
      ok("copyFile rejected (EISDIR), directory children preserved");
    else
      bad("copyFile rejected but directory children lost");
  } else {
    // copyFile succeeded — directory was overwritten
    if (fs.exists("/dir/child.txt"))
      ok("copyFile to dir path succeeded but children survived");
    else
      bad("copyFile overwrote directory, children destroyed");
  }
}

// ---------------------------------------------------------------------------
// 4. MemFS — rename cycle protection
// ---------------------------------------------------------------------------

static void test_mem_rename_cycle() {
  heading("MemFS: rename cycle protection");

  sfs::MemFSBackend fs;
  fs.mkdir("/a", false);
  fs.mkdir("/a/b", false);
  fs.writeFile("/a/b/file.txt", "data", 4);

  // rename parent into own child
  auto r1 = fs.rename("/a", "/a/b/moved");
  if (!r1.has_value())
    ok("rename parent->child blocked");
  else
    bad("rename parent->child succeeded — creates orphaned cycle");

  // rename dir into itself
  auto r2 = fs.rename("/a", "/a/self");
  if (!r2.has_value())
    ok("rename dir into itself blocked");
  else if (r2.has_value())
    note("rename dir into itself succeeded (no-op or accepted)");
}

// ---------------------------------------------------------------------------
// 5. MemFS — mkdir on file path
// ---------------------------------------------------------------------------

static void test_mem_mkdir_on_file() {
  heading("MemFS: mkdir on file path");

  sfs::MemFSBackend fs;
  fs.writeFile("/file.txt", "data", 4);

  auto r1 = fs.mkdir("/file.txt/sub", false);
  if (!r1.has_value())
    ok("mkdir /file.txt/sub (non-recursive) rejected");
  else
    bad("mkdir under file path succeeded — parent is not a directory");

  auto r2 = fs.mkdir("/file.txt/sub", true);
  if (!r2.has_value())
    ok("mkdir /file.txt/sub (recursive) rejected");
  else
    bad("recursive mkdir under file path succeeded");
}

// ---------------------------------------------------------------------------
// 6. VirtualFS — root mount "/" behavior
// ---------------------------------------------------------------------------

static void test_vfs_root_mount() {
  heading("VirtualFS: root mount '/'");

  auto memFs = std::make_shared<sfs::MemFSBackend>();
  memFs->writeFile("/hello.txt", "world", 5);

  try {
    sfs::VirtualFS vfs({{"/", sfs::Perm::Read | sfs::Perm::Write, memFs}});

    auto r = vfs.readFile("/hello.txt");
    if (r.has_value() && *r == "world")
      ok("root mount '/' correctly routes subpaths");
    else
      bad("root mount '/' fails to route subpaths");
  } catch (const std::exception &e) {
    bad((std::string("constructor threw: ") + e.what()).c_str());
  }
}

// ---------------------------------------------------------------------------
// 7. VirtualFS — cross-mount copyFile preserves source
// ---------------------------------------------------------------------------

static void test_vfs_crossmount_copy() {
  heading("VirtualFS: cross-mount copyFile source preservation");

  auto srcFs = std::make_shared<sfs::MemFSBackend>();
  auto dstFs = std::make_shared<sfs::MemFSBackend>();
  srcFs->writeFile("/secret.txt", "classified", 10);

  sfs::VirtualFS vfs({
      {"/src", sfs::Perm::Read, srcFs},
      {"/dst", sfs::Perm::Read | sfs::Perm::Write, dstFs},
  });

  vfs.copyFile("/src/secret.txt", "/dst/copied.txt");

  auto dstContent = vfs.readFile("/dst/copied.txt");
  if (dstContent.has_value() && *dstContent == "classified")
    ok("destination got the data");
  else
    bad("destination missing data");

  auto srcContent = vfs.readFile("/src/secret.txt");
  if (srcContent.has_value() && *srcContent == "classified")
    ok("source data preserved after cross-mount copy");
  else
    bad("source data DESTROYED by cross-mount copyFile");
}

// ---------------------------------------------------------------------------
// 8. VirtualFS — unmounted paths blocked
// ---------------------------------------------------------------------------

static void test_vfs_unmounted_paths() {
  heading("VirtualFS: unmounted paths blocked");

  auto memFs = std::make_shared<sfs::MemFSBackend>();
  sfs::VirtualFS vfs({{"/app", sfs::Perm::Read | sfs::Perm::Write, memFs}});

  auto r1 = vfs.readFile("/etc/passwd");
  if (!r1.has_value())
    ok("read from unmounted /etc/passwd blocked");
  else
    bad("read from unmounted path succeeded");

  auto r2 = vfs.writeFile("/tmp/hack", "x", 1);
  if (!r2.has_value())
    ok("write to unmounted /tmp/hack blocked");
  else
    bad("write to unmounted path succeeded");
}

// ---------------------------------------------------------------------------
// 9. VirtualFS — permission enforcement
// ---------------------------------------------------------------------------

static void test_vfs_permissions() {
  heading("VirtualFS: permission enforcement");

  auto roFs = std::make_shared<sfs::MemFSBackend>();
  roFs->writeFile("/readonly.txt", "data", 4);

  auto rwFs = std::make_shared<sfs::MemFSBackend>();

  sfs::VirtualFS vfs({
      {"/ro", sfs::Perm::Read, roFs},
      {"/rw", sfs::Perm::Read | sfs::Perm::Write, rwFs},
  });

  // read from RO should work
  auto r1 = vfs.readFile("/ro/readonly.txt");
  if (r1.has_value() && *r1 == "data")
    ok("read from RO mount works");
  else
    bad("read from RO mount failed");

  // write to RO should be blocked
  auto r2 = vfs.writeFile("/ro/new.txt", "x", 1);
  if (!r2.has_value())
    ok("write to RO mount blocked");
  else
    bad("write to RO mount succeeded — permission bypass");
}

// ---------------------------------------------------------------------------
// 10. VirtualFS — dynamic mount() permission upgrade
// ---------------------------------------------------------------------------

static void test_vfs_dynamic_mount_upgrade() {
  heading("VirtualFS: dynamic mount() permission upgrade");

  auto secretFs = std::make_shared<sfs::MemFSBackend>();
  secretFs->writeFile("/flag.txt", "hidden", 6);

  sfs::VirtualFS vfs({{"/secret", sfs::Perm::None, secretFs}});

  // verify None mount blocks reads
  auto r1 = vfs.readFile("/secret/flag.txt");
  if (!r1.has_value())
    ok("None-perm mount blocks read");
  else
    bad("None-perm mount allows read");

  // try to upgrade via mount()
  try {
    vfs.mount({"/secret", sfs::Perm::Read | sfs::Perm::Write, secretFs});
    bad("mount() with upgraded permissions was accepted — no exception thrown");
    auto r2 = vfs.readFile("/secret/flag.txt");
    if (r2.has_value())
      bad("after upgrade, previously-blocked read now succeeds");
    else
      ok("after upgrade attempt, read still blocked");
  } catch (const std::invalid_argument &e) {
    ok("mount() with conflicting permissions rejected (exception thrown)");
  }
}

// ---------------------------------------------------------------------------
// 11. VirtualFS — path normalization attacks
// ---------------------------------------------------------------------------

static void test_vfs_path_normalization() {
  heading("VirtualFS: path normalization attacks");

  auto memFs = std::make_shared<sfs::MemFSBackend>();
  memFs->writeFile("/flag.txt", "FLAG", 4);
  sfs::VirtualFS vfs({{"/app", sfs::Perm::Read | sfs::Perm::Write, memFs}});

  // null byte injection
  std::string nullPath = std::string("/app/flag.txt") + '\0' + std::string("/../../etc/passwd");
  auto r1 = vfs.readFile(nullPath);
  if (!r1.has_value())
    ok("null-byte injection blocked");
  else
    bad("null-byte in path bypassed mount boundary");

  // backslash normalization
  auto r2 = vfs.readFile("/app\\flag.txt");
  if (r2.has_value() && *r2 == "FLAG")
    ok("backslash correctly normalized, file found");
  else
    bad("backslash normalization broken");

  // dot-dot across mount
  vfs.writeFile("/app/data.txt", "private", 7);
  auto r3 = vfs.readFile("/app/../app/data.txt");
  if (r3.has_value() && *r3 == "private")
    ok("dot-dot across mount resolves correctly");
  else
    bad("dot-dot across mount broken");
}

// ---------------------------------------------------------------------------
// 12. VirtualFS + RealFS combined — symlink through sandbox
// ---------------------------------------------------------------------------

static void test_vfs_real_combined(const fs::path &tmp) {
  heading("VirtualFS+RealFS: combined symlink attacks");

  auto bundleDir = tmp / "bundle";
  auto sandboxDir = tmp / "sandbox";
  auto outsideDir = tmp / "outside";
  std::error_code ec;
  fs::create_directories(bundleDir, ec);
  fs::create_directories(sandboxDir, ec);
  fs::create_directories(outsideDir, ec);
  std::ofstream(bundleDir / "app.js") << "code";
  std::ofstream(sandboxDir / "data.txt") << "data";
  std::ofstream(outsideDir / "secret.txt") << "SECRET";

  auto bundle = std::make_shared<sfs::RealFSBackend>(bundleDir);
  auto sandbox = std::make_shared<sfs::RealFSBackend>(sandboxDir);

  sfs::VirtualFS vfs({
      {"/bundle", sfs::Perm::Read, bundle},
      {"/home", sfs::Perm::Read | sfs::Perm::Write, sandbox},
  });

  // symlink in sandbox -> outside
  fs::create_symlink(outsideDir, sandboxDir / "escape");
  auto r1 = vfs.readFile("/home/escape/secret.txt");
  if (!r1.has_value())
    ok("symlink sandbox->outside blocked");
  else
    bad("read outside file through sandbox symlink");
  fs::remove(sandboxDir / "escape", ec);

  // symlink in sandbox -> bundle (try to get write access to RO mount)
  fs::create_symlink(bundleDir, sandboxDir / "bndl");
  auto r2 = vfs.writeFile("/home/bndl/injected.js", "evil", 4);
  if (!r2.has_value())
    ok("write through symlink to bundle dir blocked");
  else
    bad("wrote to RO mount via symlink through sandbox");
  fs::remove(sandboxDir / "bndl", ec);

  // dot-dot outside via virtual layer
  auto r3 = vfs.readFile("/home/../outside/secret.txt");
  if (!r3.has_value())
    ok("dot-dot outside through virtual layer blocked");
  else
    bad("dot-dot escaped through virtual layer");

  // copy from RO bundle to writable sandbox
  auto r4 = vfs.copyFile("/bundle/app.js", "/home/copied.js");
  if (r4.has_value()) {
    auto content = vfs.readFile("/home/copied.js");
    if (content.has_value() && *content == "code")
      ok("copy RO->RW works correctly");
    else
      bad("copy RO->RW produced corrupt data");
  } else {
    bad("copy RO->RW failed");
  }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
  std::error_code ec;
  auto tmp = fs::temp_directory_path(ec) / "sandbox-escape-demo";
  cleanTmp(tmp);

  std::cout << "\nFilesystem Sandbox Escape Demo\n";
  std::cout << "==============================\n";

  test_real_symlink_dotdot(tmp);
  test_real_symlink_mkdir_escape(tmp);

  test_mem_readfilebytes();
  test_mem_copyfile_overwrite_dir();
  test_mem_rename_cycle();
  test_mem_mkdir_on_file();

  test_vfs_root_mount();
  test_vfs_crossmount_copy();
  test_vfs_unmounted_paths();
  test_vfs_permissions();
  test_vfs_dynamic_mount_upgrade();
  test_vfs_path_normalization();
  test_vfs_real_combined(tmp);

  cleanTmp(tmp);

  std::cout << "\n";
  return 0;
}
