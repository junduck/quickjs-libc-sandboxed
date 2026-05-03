#include "sandboxed-fs/vfs.hpp"
#include "sandboxed-fs/vfs_mem.hpp"
#include "sandboxed-fs/vfs_real.hpp"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

namespace sfs = sandboxed_fs;
namespace fs = std::filesystem;

// POSIX headers (included transitively by GTest on macOS) may #define
// F_OK / R_OK / W_OK / X_OK as macros.  Undef them so our scoped
// sfs::access_mode constants work.
#ifdef F_OK
#undef F_OK
#endif
#ifdef R_OK
#undef R_OK
#endif
#ifdef W_OK
#undef W_OK
#endif
#ifdef X_OK
#undef X_OK
#endif

#define EXPECT_ERR(expr, errCode)                                                                                                                    \
  do {                                                                                                                                               \
    auto _r = (expr);                                                                                                                                \
    EXPECT_FALSE(_r.has_value()) << "expected error " << errCode;                                                                                    \
    if (!_r.has_value())                                                                                                                             \
      EXPECT_EQ(_r.error(), (errCode));                                                                                                              \
  } while (0)

#define EXPECT_OK(expr)                                                                                                                              \
  do {                                                                                                                                               \
    auto _r = (expr);                                                                                                                                \
    EXPECT_TRUE(_r.has_value()) << "unexpected error " << (_r.has_value() ? 0 : _r.error());                                                         \
  } while (0)

#define ASSERT_OK(expr)                                                                                                                              \
  do {                                                                                                                                               \
    auto _r = (expr);                                                                                                                                \
    ASSERT_TRUE(_r.has_value()) << "unexpected error " << (_r.has_value() ? 0 : _r.error());                                                         \
  } while (0)

// ==========================================================================
// MemFS — path traversal attempts
// ==========================================================================

class MemFSSecurityTest : public ::testing::Test {
protected:
  sfs::MemFSBackend fs;
};

TEST_F(MemFSSecurityTest, DotDotEscape) {
  EXPECT_ERR(fs.readFile("/../outside/secret.txt"), ENOENT);
  EXPECT_ERR(fs.readFile("/a/../../etc/passwd"), ENOENT);
  // MemFS has no ".." directory — the path simply doesn't exist.
  EXPECT_ERR(fs.writeFile("/../outside/hack.txt", "x", 1), ENOENT);
}

TEST_F(MemFSSecurityTest, DoubleDotEscapeDeep) { EXPECT_ERR(fs.readFile("/a/b/c/../../../../etc/passwd"), ENOENT); }

TEST_F(MemFSSecurityTest, DotPathConfusion) {
  EXPECT_ERR(fs.readFile("/./../etc/passwd"), ENOENT);
  EXPECT_ERR(fs.readFile("/a/./b/../../etc/shadow"), ENOENT);
}

TEST_F(MemFSSecurityTest, DoubleSlash) {
  ASSERT_OK(fs.writeFile("/data.txt", "valid", 5));
  auto r = fs.readFile("//data.txt");
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, "valid");
}

TEST_F(MemFSSecurityTest, TrailingSlashOnFile) {
  ASSERT_OK(fs.writeFile("/file.txt", "x", 1));
  // Trailing slash is normalized away in splitPath — reads as /file.txt.
  auto r = fs.readFile("/file.txt/");
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, "x");
}

TEST_F(MemFSSecurityTest, WriteToRoot) {
  EXPECT_ERR(fs.writeFile("/", "x", 1), EISDIR);
  EXPECT_ERR(fs.writeFile("", "x", 1), EISDIR);
}

TEST_F(MemFSSecurityTest, UnlinkRoot) {
  EXPECT_ERR(fs.unlink("/"), EACCES);
  EXPECT_ERR(fs.rmdir("/"), EACCES);
}

TEST_F(MemFSSecurityTest, RenameRoot) {
  EXPECT_ERR(fs.rename("/", "/dst"), EACCES);
  // /file does not exist → ENOENT, not a security bypass.
  EXPECT_ERR(fs.rename("/file", "/"), ENOENT);
}

TEST_F(MemFSSecurityTest, StatOutsideTree) {
  EXPECT_ERR(fs.stat("/../nonexistent"), ENOENT);
  EXPECT_ERR(fs.lstat("/../nonexistent"), ENOENT);
}

TEST_F(MemFSSecurityTest, ReaddirEscape) {
  EXPECT_ERR(fs.readdir("/../"), ENOENT);
  EXPECT_ERR(fs.readdir("/a/../b/../../c"), ENOENT);
}

TEST_F(MemFSSecurityTest, AccessEscape) {
  EXPECT_EQ(fs.access("/../nonexistent", sfs::access_mode::F_OK), ENOENT);
  EXPECT_EQ(fs.access("/../../etc/passwd", sfs::access_mode::R_OK), ENOENT);
}

TEST_F(MemFSSecurityTest, MkdirEscape) { EXPECT_ERR(fs.mkdir("/../outside", false), ENOENT); }

TEST_F(MemFSSecurityTest, CopyFileFromOutside) { EXPECT_ERR(fs.copyFile("/../secret.txt", "/copy.txt"), ENOENT); }

TEST_F(MemFSSecurityTest, CopyFileToOutside) {
  ASSERT_OK(fs.writeFile("/data.txt", "x", 1));
  EXPECT_ERR(fs.copyFile("/data.txt", "/../leak.txt"), ENOENT);
}

TEST_F(MemFSSecurityTest, RenameCrossEscape) {
  ASSERT_OK(fs.writeFile("/data.txt", "x", 1));
  EXPECT_ERR(fs.rename("/data.txt", "/../stolen.txt"), ENOENT);
}

// ==========================================================================
// RealFS — symlink and real-path escape
// ==========================================================================

class RealFSSecurityTest : public ::testing::Test {
protected:
  void SetUp() override {
    std::error_code ec;
    tmpDir = fs::temp_directory_path(ec) / "sandboxed-fs-sec";
    sandboxDir = tmpDir / "sandbox";
    outsideDir = tmpDir / "outside";

    fs::remove_all(tmpDir, ec);
    fs::create_directories(sandboxDir, ec);
    fs::create_directories(outsideDir, ec);

    std::ofstream(sandboxDir / "inside.txt") << "inside";
    std::ofstream(outsideDir / "secret.txt") << "secret";
  }

  void TearDown() override {
    std::error_code ec;
    fs::remove_all(tmpDir, ec);
  }

  fs::path tmpDir;
  fs::path sandboxDir;
  fs::path outsideDir;
};

TEST_F(RealFSSecurityTest, DotDotEscape) {
  sfs::RealFSBackend realFs(sandboxDir);
  EXPECT_ERR(realFs.readFile("/../outside/secret.txt"), EACCES);
  // Two levels up — the resolved path doesn't exist → canonical fails.
  EXPECT_ERR(realFs.readFile("/../../outside/secret.txt"), ENOENT);
}

TEST_F(RealFSSecurityTest, DotPathConfusion) {
  sfs::RealFSBackend realFs(sandboxDir);
  // ../outside/secret.txt exists one level up — blocked.
  EXPECT_ERR(realFs.readFile("/./../outside/secret.txt"), EACCES);
  // a/b/../../outside/secret.txt — intermediate a/b don't exist,
  // so canonical fails with ENOENT before the escape check.
  EXPECT_ERR(realFs.readFile("/a/./b/../../outside/secret.txt"), ENOENT);
}

TEST_F(RealFSSecurityTest, SymlinkEscapeRead) {
  sfs::RealFSBackend realFs(sandboxDir);
  auto linkPath = sandboxDir / "escape_link";
  fs::create_symlink(outsideDir, linkPath);

  EXPECT_ERR(realFs.readFile("/escape_link/secret.txt"), EACCES);

  fs::remove(linkPath);
}

TEST_F(RealFSSecurityTest, SymlinkEscapeStat) {
  sfs::RealFSBackend realFs(sandboxDir);
  auto linkPath = sandboxDir / "stat_link";
  fs::create_symlink(outsideDir, linkPath);

  EXPECT_ERR(realFs.stat("/stat_link/secret.txt"), EACCES);

  fs::remove(linkPath);
}

TEST_F(RealFSSecurityTest, SymlinkEscapeWrite) {
  sfs::RealFSBackend realFs(sandboxDir);
  auto linkPath = sandboxDir / "write_link";
  fs::create_symlink(outsideDir, linkPath);

  EXPECT_ERR(realFs.writeFile("/write_link/evil.txt", "hacked", 6), EACCES);

  fs::remove(linkPath);
}

TEST_F(RealFSSecurityTest, SymlinkToAbsolute) {
  sfs::RealFSBackend realFs(sandboxDir);
  auto linkPath = sandboxDir / "abs_link";
  fs::create_symlink("/etc", linkPath);

  EXPECT_ERR(realFs.readFile("/abs_link/passwd"), EACCES);
  EXPECT_ERR(realFs.stat("/abs_link"), EACCES);

  fs::remove(linkPath);
}

TEST_F(RealFSSecurityTest, MkdirEscape) {
  sfs::RealFSBackend realFs(sandboxDir);
  EXPECT_ERR(realFs.mkdir("/../outside/newdir", false), EACCES);
}

TEST_F(RealFSSecurityTest, RenameEscape) {
  sfs::RealFSBackend realFs(sandboxDir);
  EXPECT_ERR(realFs.rename("/inside.txt", "/../outside/stolen.txt"), EACCES);
}

TEST_F(RealFSSecurityTest, CopyFileEscape) {
  sfs::RealFSBackend realFs(sandboxDir);
  EXPECT_ERR(realFs.copyFile("/../outside/secret.txt", "/copy.txt"), EACCES);
}

TEST_F(RealFSSecurityTest, ReaddirEscape) {
  sfs::RealFSBackend realFs(sandboxDir);
  EXPECT_ERR(realFs.readdir("/../outside"), EACCES);
}

TEST_F(RealFSSecurityTest, TruncateEscape) {
  sfs::RealFSBackend realFs(sandboxDir);
  EXPECT_ERR(realFs.truncate("/../outside/secret.txt", 0), EACCES);
}

TEST_F(RealFSSecurityTest, UtimesEscape) {
  sfs::RealFSBackend realFs(sandboxDir);
  EXPECT_ERR(realFs.utimes("/../outside/secret.txt", 0, 0), EACCES);
}

TEST_F(RealFSSecurityTest, AccessEscape) {
  sfs::RealFSBackend realFs(sandboxDir);
  EXPECT_EQ(realFs.access("/../outside/secret.txt", sfs::access_mode::F_OK), EACCES);
}

TEST_F(RealFSSecurityTest, WeaklyCanonicalEscape) {
  // writeFile uses resolveCreating with weakly_canonical
  sfs::RealFSBackend realFs(sandboxDir);
  EXPECT_ERR(realFs.writeFile("/../outside/new.txt", "esc", 3), EACCES);
}

// ==========================================================================
// VirtualFS — mount boundary attacks
// ==========================================================================

class VirtualFSSecurityTest : public ::testing::Test {
protected:
  void SetUp() override {
    bundleFs = std::make_shared<sfs::MemFSBackend>();
    ASSERT_OK(bundleFs->writeFile("/code.js", "secret_code", 11));
    ASSERT_OK(bundleFs->writeFile("/config.json", "{}", 2));

    sandboxFs = std::make_shared<sfs::MemFSBackend>();

    // Mount with None permission — should reject everything
    noPermFs = std::make_shared<sfs::MemFSBackend>();
    ASSERT_OK(noPermFs->writeFile("/data.txt", "hidden", 6));

    vfs = std::make_unique<sfs::VirtualFS>(std::vector<sfs::MountEntry>{
        {"/bundle", sfs::Perm::Read, bundleFs},
        {"/home/sandbox", sfs::Perm::Read | sfs::Perm::Write, sandboxFs},
        {"/none", sfs::Perm::None, noPermFs},
    });
  }

  std::shared_ptr<sfs::MemFSBackend> bundleFs;
  std::shared_ptr<sfs::MemFSBackend> sandboxFs;
  std::shared_ptr<sfs::MemFSBackend> noPermFs;
  std::unique_ptr<sfs::VirtualFS> vfs;
};

TEST_F(VirtualFSSecurityTest, WriteToReadOnlyMount) {
  EXPECT_ERR(vfs->writeFile("/bundle/hack.js", "evil", 4), EACCES);
  EXPECT_ERR(vfs->appendFile("/bundle/code.js", "evil", 4), EACCES);
  EXPECT_ERR(vfs->unlink("/bundle/code.js"), EACCES);
  EXPECT_ERR(vfs->mkdir("/bundle/dir", false), EACCES);
  EXPECT_ERR(vfs->rmdir("/bundle/dir"), EACCES);
  EXPECT_ERR(vfs->truncate("/bundle/code.js", 0), EACCES);
  EXPECT_ERR(vfs->utimes("/bundle/code.js", 0, 0), EACCES);
  EXPECT_ERR(vfs->rename("/bundle/code.js", "/bundle/renamed.js"), EACCES);
}

TEST_F(VirtualFSSecurityTest, ReadFromNoPermMount) {
  EXPECT_ERR(vfs->readFile("/none/data.txt"), EACCES);
  EXPECT_ERR(vfs->readFileBytes("/none/data.txt"), EACCES);
  EXPECT_ERR(vfs->stat("/none/data.txt"), EACCES);
  EXPECT_ERR(vfs->readdir("/none"), EACCES);
  EXPECT_FALSE(vfs->exists("/none/data.txt"));
}

TEST_F(VirtualFSSecurityTest, WriteToNoPermMount) { EXPECT_ERR(vfs->writeFile("/none/new.txt", "x", 1), EACCES); }

TEST_F(VirtualFSSecurityTest, UnmountedPath) {
  EXPECT_ERR(vfs->readFile("/etc/passwd"), EACCES);
  EXPECT_ERR(vfs->writeFile("/tmp/hack", "x", 1), EACCES);
  EXPECT_ERR(vfs->stat("/secret"), EACCES);
  EXPECT_ERR(vfs->readdir("/var"), EACCES);
  EXPECT_ERR(vfs->mkdir("/var/log", false), EACCES);
  EXPECT_ERR(vfs->unlink("/etc/shadow"), EACCES);
  EXPECT_ERR(vfs->truncate("/proc/cpuinfo", 0), EACCES);
  EXPECT_ERR(vfs->rename("/bundle/code.js", "/tmp/stolen"), EACCES);
}

TEST_F(VirtualFSSecurityTest, PathNormalizationTraversal) {
  // /bundle/../home/sandbox should route to sandbox, not break isolation
  ASSERT_OK(vfs->writeFile("/home/sandbox/data.txt", "cross", 5));
  auto r = vfs->readFile("/bundle/../home/sandbox/data.txt");
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, "cross");
}

TEST_F(VirtualFSSecurityTest, MountPrefixConfusion) {
  // "/bundlex" should NOT match "/bundle" mount
  EXPECT_ERR(vfs->readFile("/bundlex/code"), EACCES);
  // "/bundle" without trailing slash should match exactly
  auto r = vfs->readFile("/bundle/code.js");
  ASSERT_TRUE(r.has_value());
  // "/bundlesuffix" should not match
  EXPECT_ERR(vfs->readFile("/bundlesuffix"), EACCES);
}

TEST_F(VirtualFSSecurityTest, DoubleDotAcrossMounts) {
  ASSERT_OK(vfs->writeFile("/home/sandbox/secret.txt", "private", 7));
  // /bundle/../home/sandbox/secret.txt → normalize → /home/sandbox/secret.txt
  auto r = vfs->readFile("/bundle/../home/sandbox/secret.txt");
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, "private");
  // Should NOT be able to read bundle file via sandbox traversal
  EXPECT_ERR(vfs->readFile("/home/sandbox/../bundle/code.js"), EACCES);
}

TEST_F(VirtualFSSecurityTest, CopyFileCrossMountReadRestriction) {
  ASSERT_OK(vfs->writeFile("/home/sandbox/data.txt", "data", 4));
  // Copy from sandbox to sandbox should work
  EXPECT_OK(vfs->copyFile("/home/sandbox/data.txt", "/home/sandbox/copy.txt"));
  // Copy from bundle (read-only) to sandbox should work
  EXPECT_OK(vfs->copyFile("/bundle/code.js", "/home/sandbox/code_copy.js"));
  // Copy from sandbox to bundle should fail (bundle is RO)
  EXPECT_ERR(vfs->copyFile("/home/sandbox/data.txt", "/bundle/write_back.js"), EACCES);
}

TEST_F(VirtualFSSecurityTest, RenameCrossMount) {
  EXPECT_ERR(vfs->rename("/home/sandbox/data.txt", "/bundle/data.txt"), EACCES);
  EXPECT_ERR(vfs->rename("/bundle/code.js", "/home/sandbox/code.js"), EACCES);
  // resolve permissions first (both need Write), so rename triggers EACCES
  // Actually: oldPath "/bundle/code.js" needs Write, but bundle is Read → EACCES
}

TEST_F(VirtualFSSecurityTest, NormalizeSlashesAndDots) {
  auto r = vfs->readFile("/bundle//code.js");
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, "secret_code");

  auto r2 = vfs->readFile("/bundle/./code.js");
  ASSERT_TRUE(r2.has_value());
  EXPECT_EQ(*r2, "secret_code");

  auto r3 = vfs->readFile("/bundle/foo/../code.js");
  ASSERT_TRUE(r3.has_value());
  EXPECT_EQ(*r3, "secret_code");
}

// ==========================================================================
// VirtualFS + RealFS — combined real symlink attacks
// ==========================================================================

class VirtualFSRealSecurityTest : public ::testing::Test {
protected:
  void SetUp() override {
    std::error_code ec;
    tmpDir = fs::temp_directory_path(ec) / "sandboxed-fs-vfs-sec";
    bundleDir = tmpDir / "bundle";
    sandboxDir = tmpDir / "sandbox";
    outsideDir = tmpDir / "outside";

    fs::remove_all(tmpDir, ec);
    fs::create_directories(bundleDir, ec);
    fs::create_directories(sandboxDir, ec);
    fs::create_directories(outsideDir, ec);

    std::ofstream(bundleDir / "app.js") << "const x = 1;";
    std::ofstream(sandboxDir / "data.txt") << "sandbox-data";
    std::ofstream(outsideDir / "secret.txt") << "outside-secret";

    auto bundleBackend = std::make_shared<sfs::RealFSBackend>(bundleDir);
    auto sandboxBackend = std::make_shared<sfs::RealFSBackend>(sandboxDir);

    vfs = std::make_unique<sfs::VirtualFS>(std::vector<sfs::MountEntry>{
        {"/bundle", sfs::Perm::Read, bundleBackend},
        {"/home/sandbox", sfs::Perm::Read | sfs::Perm::Write, sandboxBackend},
    });
  }

  void TearDown() override {
    std::error_code ec;
    fs::remove_all(tmpDir, ec);
  }

  fs::path tmpDir;
  fs::path bundleDir;
  fs::path sandboxDir;
  fs::path outsideDir;
  std::unique_ptr<sfs::VirtualFS> vfs;
};

TEST_F(VirtualFSRealSecurityTest, SymlinkInSandboxToOutside) {
  auto linkPath = sandboxDir / "escape";
  fs::create_symlink(outsideDir, linkPath);

  EXPECT_ERR(vfs->readFile("/home/sandbox/escape/secret.txt"), EACCES);
  EXPECT_ERR(vfs->writeFile("/home/sandbox/escape/hack.txt", "x", 1), EACCES);
  EXPECT_ERR(vfs->stat("/home/sandbox/escape/secret.txt"), EACCES);

  fs::remove(linkPath);
}

TEST_F(VirtualFSRealSecurityTest, SymlinkAbsoluteInSandbox) {
  auto linkPath = sandboxDir / "abs_escape";
  fs::create_symlink("/etc", linkPath);

  EXPECT_ERR(vfs->readFile("/home/sandbox/abs_escape/passwd"), EACCES);

  fs::remove(linkPath);
}

TEST_F(VirtualFSRealSecurityTest, WriteSymlinkForLaterExploit) {
  auto linkPath = sandboxDir / "link_dir";
  fs::create_symlink(outsideDir, linkPath);

  EXPECT_ERR(vfs->writeFile("/home/sandbox/link_dir/planted.txt", "x", 1), EACCES);

  fs::remove(linkPath);
}

TEST_F(VirtualFSRealSecurityTest, DotDotThroughVirtualToReal) {
  // /bundle/../home/sandbox/../../outside/secret.txt
  EXPECT_ERR(vfs->readFile("/bundle/../home/sandbox/../outside/secret.txt"), EACCES);
  EXPECT_ERR(vfs->readFile("/home/sandbox/../bundle/../outside/secret.txt"), EACCES);
}

TEST_F(VirtualFSRealSecurityTest, MkdirSymlinkEscape) {
  auto linkPath = sandboxDir / "mkdir_link";
  fs::create_symlink(outsideDir, linkPath);

  EXPECT_ERR(vfs->mkdir("/home/sandbox/mkdir_link/nope", false), EACCES);

  fs::remove(linkPath);
}

TEST_F(VirtualFSRealSecurityTest, TruncateOutsideViaPath) { EXPECT_ERR(vfs->truncate("/home/sandbox/../outside/secret.txt", 0), EACCES); }

TEST_F(VirtualFSRealSecurityTest, UtimesOutsideViaPath) { EXPECT_ERR(vfs->utimes("/home/sandbox/../outside/secret.txt", 0, 0), EACCES); }

TEST_F(VirtualFSRealSecurityTest, ReaddirOutsideViaPath) { EXPECT_ERR(vfs->readdir("/home/sandbox/../outside"), EACCES); }

TEST_F(VirtualFSRealSecurityTest, AccessOutsideViaPath) {
  EXPECT_EQ(vfs->access("/home/sandbox/../outside/secret.txt", sfs::access_mode::F_OK), EACCES);
}
