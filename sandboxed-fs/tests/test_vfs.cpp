#include "sandboxed-fs/vfs.hpp"
#include "sandboxed-fs/vfs_mem.hpp"
#include "sandboxed-fs/vfs_real.hpp"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

namespace sfs = sandboxed_fs;
namespace fs = std::filesystem;

class MemFSTest : public ::testing::Test {
protected:
  sfs::MemFSBackend fs;
};

TEST_F(MemFSTest, EmptyFS) {
  EXPECT_FALSE(fs.exists("/file.txt"));
  EXPECT_TRUE(fs.exists("/"));
}

TEST_F(MemFSTest, WriteAndReadFile) {
  ASSERT_TRUE(fs.writeFile("/hello.txt", "Hello, World!", 13).has_value());
  EXPECT_TRUE(fs.exists("/hello.txt"));
  auto content = fs.readFile("/hello.txt");
  ASSERT_TRUE(content.has_value());
  EXPECT_EQ(*content, "Hello, World!");

  auto bytes = fs.readFileBytes("/hello.txt");
  ASSERT_TRUE(bytes.has_value());
  EXPECT_EQ(bytes->size(), 13);
  EXPECT_EQ((*bytes)[0], 'H');
}

TEST_F(MemFSTest, AppendFile) {
  ASSERT_TRUE(fs.writeFile("/hello.txt", "Hello", 5).has_value());
  ASSERT_TRUE(fs.appendFile("/hello.txt", " World", 6).has_value());
  auto content = fs.readFile("/hello.txt");
  ASSERT_TRUE(content.has_value());
  EXPECT_EQ(*content, "Hello World");
}

TEST_F(MemFSTest, StatFile) {
  ASSERT_TRUE(fs.writeFile("/data.bin", "0123456789", 10).has_value());
  auto st = fs.stat("/data.bin");
  ASSERT_TRUE(st.has_value());
  EXPECT_TRUE(st->isFile());
  EXPECT_FALSE(st->isDir());
  EXPECT_EQ(st->size, 10);
}

TEST_F(MemFSTest, UnlinkFile) {
  ASSERT_TRUE(fs.writeFile("/temp.txt", "x", 1).has_value());
  EXPECT_TRUE(fs.exists("/temp.txt"));
  ASSERT_TRUE(fs.unlink("/temp.txt").has_value());
  EXPECT_FALSE(fs.exists("/temp.txt"));
}

TEST_F(MemFSTest, ReadNonexistent) {
  auto r = fs.readFile("/nonexistent.txt");
  EXPECT_FALSE(r.has_value());
  EXPECT_EQ(r.error(), ENOENT);
}

TEST_F(MemFSTest, ReadDirOnFile) {
  ASSERT_TRUE(fs.writeFile("/file.txt", "data", 4).has_value());
  auto r = fs.readdir("/file.txt");
  EXPECT_FALSE(r.has_value());
  EXPECT_EQ(r.error(), ENOTDIR);
}

class MemFSDirTest : public ::testing::Test {
protected:
  sfs::MemFSBackend fs;
};

TEST_F(MemFSDirTest, Mkdir) {
  ASSERT_TRUE(fs.mkdir("/a", false).has_value());
  EXPECT_TRUE(fs.exists("/a"));
  auto st = fs.stat("/a");
  ASSERT_TRUE(st.has_value());
  EXPECT_TRUE(st->isDir());
}

TEST_F(MemFSDirTest, RecursiveMkdir) {
  ASSERT_TRUE(fs.mkdir("/a/b/c", true).has_value());
  EXPECT_TRUE(fs.exists("/a"));
  EXPECT_TRUE(fs.exists("/a/b"));
  EXPECT_TRUE(fs.exists("/a/b/c"));

  auto entries = fs.readdir("/a");
  ASSERT_TRUE(entries.has_value());
  EXPECT_EQ(entries->size(), 1);
  EXPECT_EQ((*entries)[0], "b");
}

TEST_F(MemFSDirTest, Readdir) {
  ASSERT_TRUE(fs.mkdir("/dir", false).has_value());
  ASSERT_TRUE(fs.writeFile("/dir/a.txt", "a", 1).has_value());
  ASSERT_TRUE(fs.writeFile("/dir/b.txt", "b", 1).has_value());
  ASSERT_TRUE(fs.mkdir("/dir/sub", false).has_value());

  auto entries = fs.readdir("/dir");
  ASSERT_TRUE(entries.has_value());
  EXPECT_EQ(entries->size(), 3);
}

TEST_F(MemFSDirTest, RmdirNonEmptyFails) {
  ASSERT_TRUE(fs.mkdir("/dir", false).has_value());
  ASSERT_TRUE(fs.writeFile("/dir/file.txt", "x", 1).has_value());
  auto r = fs.rmdir("/dir");
  EXPECT_FALSE(r.has_value());
  EXPECT_EQ(r.error(), ENOTEMPTY);
}

TEST_F(MemFSDirTest, RemoveHierarchy) {
  ASSERT_TRUE(fs.mkdir("/a/b/c", true).has_value());
  ASSERT_TRUE(fs.writeFile("/a/b/c/file.txt", "data", 4).has_value());
  ASSERT_TRUE(fs.unlink("/a/b/c/file.txt").has_value());
  ASSERT_TRUE(fs.rmdir("/a/b/c").has_value());
  ASSERT_TRUE(fs.rmdir("/a/b").has_value());
  ASSERT_TRUE(fs.rmdir("/a").has_value());
  EXPECT_FALSE(fs.exists("/a"));
}

class MemFSRenameCopyTest : public ::testing::Test {
protected:
  sfs::MemFSBackend fs;
};

TEST_F(MemFSRenameCopyTest, RenameFile) {
  ASSERT_TRUE(fs.writeFile("/src.txt", "source", 6).has_value());
  ASSERT_TRUE(fs.rename("/src.txt", "/dst.txt").has_value());
  EXPECT_FALSE(fs.exists("/src.txt"));
  EXPECT_TRUE(fs.exists("/dst.txt"));
  auto content = fs.readFile("/dst.txt");
  ASSERT_TRUE(content.has_value());
  EXPECT_EQ(*content, "source");
}

TEST_F(MemFSRenameCopyTest, CopyFile) {
  ASSERT_TRUE(fs.writeFile("/src.txt", "source", 6).has_value());
  ASSERT_TRUE(fs.copyFile("/src.txt", "/copy.txt").has_value());
  EXPECT_TRUE(fs.exists("/src.txt"));
  EXPECT_TRUE(fs.exists("/copy.txt"));
  auto content = fs.readFile("/copy.txt");
  ASSERT_TRUE(content.has_value());
  EXPECT_EQ(*content, "source");
}

TEST_F(MemFSRenameCopyTest, RenameNonexistent) {
  auto r = fs.rename("/nonexistent", "/foo");
  EXPECT_FALSE(r.has_value());
  EXPECT_EQ(r.error(), ENOENT);
}

TEST_F(MemFSRenameCopyTest, RenameCrossDir) {
  ASSERT_TRUE(fs.mkdir("/a", false).has_value());
  ASSERT_TRUE(fs.mkdir("/b", false).has_value());
  ASSERT_TRUE(fs.writeFile("/a/file.txt", "data", 4).has_value());
  ASSERT_TRUE(fs.rename("/a/file.txt", "/b/renamed.txt").has_value());
  EXPECT_FALSE(fs.exists("/a/file.txt"));
  EXPECT_TRUE(fs.exists("/b/renamed.txt"));
  auto content = fs.readFile("/b/renamed.txt");
  ASSERT_TRUE(content.has_value());
  EXPECT_EQ(*content, "data");
}

TEST_F(MemFSRenameCopyTest, Truncate) {
  ASSERT_TRUE(fs.writeFile("/data.txt", "hello world", 11).has_value());
  ASSERT_TRUE(fs.truncate("/data.txt", 5).has_value());
  auto content = fs.readFile("/data.txt");
  ASSERT_TRUE(content.has_value());
  EXPECT_EQ(*content, "hello");
  auto st = fs.stat("/data.txt");
  ASSERT_TRUE(st.has_value());
  EXPECT_EQ(st->size, 5);

  ASSERT_TRUE(fs.truncate("/data.txt", 10).has_value());
  auto st2 = fs.stat("/data.txt");
  ASSERT_TRUE(st2.has_value());
  EXPECT_EQ(st2->size, 10);
}

TEST_F(MemFSRenameCopyTest, Utimes) {
  ASSERT_TRUE(fs.writeFile("/data.txt", "x", 1).has_value());
  ASSERT_TRUE(fs.utimes("/data.txt", 1000, 2000).has_value());
  auto st = fs.stat("/data.txt");
  ASSERT_TRUE(st.has_value());
  EXPECT_EQ(st->atimeMs, 1000);
  EXPECT_EQ(st->mtimeMs, 2000);
}

class VirtualFSTest : public ::testing::Test {
protected:
  void SetUp() override {
    bundleFs = std::make_shared<sfs::MemFSBackend>();
    ASSERT_TRUE(bundleFs->writeFile("/app.js", "console.log(1)", 14).has_value());
    sandboxFs = std::make_shared<sfs::MemFSBackend>();
    vfs = std::make_unique<sfs::VirtualFS>(std::vector<sfs::MountEntry>{
        {"/bundle", sfs::Perm::Read, bundleFs},
        {"/home/sandbox", sfs::Perm::Read | sfs::Perm::Write, sandboxFs},
    });
  }

  std::shared_ptr<sfs::MemFSBackend> bundleFs;
  std::shared_ptr<sfs::MemFSBackend> sandboxFs;
  std::unique_ptr<sfs::VirtualFS> vfs;
};

TEST_F(VirtualFSTest, ReadFromBundle) {
  EXPECT_TRUE(vfs->exists("/bundle/app.js"));
  auto content = vfs->readFile("/bundle/app.js");
  ASSERT_TRUE(content.has_value());
  EXPECT_EQ(*content, "console.log(1)");
}

TEST_F(VirtualFSTest, WriteToSandbox) {
  ASSERT_TRUE(vfs->writeFile("/home/sandbox/data.txt", "sandbox-data", 12).has_value());
  auto content = vfs->readFile("/home/sandbox/data.txt");
  ASSERT_TRUE(content.has_value());
  EXPECT_EQ(*content, "sandbox-data");
}

TEST_F(VirtualFSTest, WriteToReadOnlyMount) {
  auto r = vfs->writeFile("/bundle/new.js", "hack", 4);
  EXPECT_FALSE(r.has_value());
  EXPECT_EQ(r.error(), EACCES);
}

TEST_F(VirtualFSTest, UnmountedPath) {
  auto r = vfs->readFile("/other/file.txt");
  EXPECT_FALSE(r.has_value());
  EXPECT_EQ(r.error(), EACCES);
}

TEST_F(VirtualFSTest, ReaddirOnNonexistent) {
  auto r = vfs->readdir("/home/sandbox/data.txt");
  EXPECT_FALSE(r.has_value());
  EXPECT_EQ(r.error(), ENOENT);
}

class MountOrderingTest : public ::testing::Test {
protected:
  void SetUp() override {
    aFs = std::make_shared<sfs::MemFSBackend>();
    ASSERT_TRUE(aFs->writeFile("/f1.txt", "from-a", 6).has_value());
    bFs = std::make_shared<sfs::MemFSBackend>();
    ASSERT_TRUE(bFs->writeFile("/f2.txt", "from-b", 6).has_value());
    vfs = std::make_unique<sfs::VirtualFS>(std::vector<sfs::MountEntry>{
        {"/a/b", sfs::Perm::Read | sfs::Perm::Write, bFs},
        {"/a", sfs::Perm::Read | sfs::Perm::Write, aFs},
    });
  }

  std::shared_ptr<sfs::MemFSBackend> aFs;
  std::shared_ptr<sfs::MemFSBackend> bFs;
  std::unique_ptr<sfs::VirtualFS> vfs;
};

TEST_F(MountOrderingTest, LongestPrefixMatch) {
  auto r1 = vfs->readFile("/a/f1.txt");
  ASSERT_TRUE(r1.has_value());
  EXPECT_EQ(*r1, "from-a");
  auto r2 = vfs->readFile("/a/b/f2.txt");
  ASSERT_TRUE(r2.has_value());
  EXPECT_EQ(*r2, "from-b");
}

TEST_F(MountOrderingTest, Normalization) {
  auto r1 = vfs->readFile("/a/./f1.txt");
  ASSERT_TRUE(r1.has_value());
  EXPECT_EQ(*r1, "from-a");
  auto r2 = vfs->readFile("/a/b/../f1.txt");
  ASSERT_TRUE(r2.has_value());
  EXPECT_EQ(*r2, "from-a");
}

class RealFSTest : public ::testing::Test {
protected:
  void SetUp() override {
    std::error_code ec;
    tmpDir = fs::temp_directory_path(ec) / "sandboxed-fs-test";
    sandboxDir = tmpDir / "sandbox";

    fs::remove_all(tmpDir, ec);
    fs::create_directories(sandboxDir, ec);
    fs::create_directories(tmpDir / "outside", ec);

    std::ofstream(sandboxDir / "inside.txt") << "inside data";
    std::ofstream(tmpDir / "outside" / "secret.txt") << "secret";
  }

  void TearDown() override {
    std::error_code ec;
    fs::remove_all(tmpDir, ec);
  }

  fs::path tmpDir;
  fs::path sandboxDir;
};

TEST_F(RealFSTest, ReadInsideFile) {
  sfs::RealFSBackend realFs(sandboxDir);
  EXPECT_TRUE(realFs.exists("/inside.txt"));
  auto content = realFs.readFile("/inside.txt");
  ASSERT_TRUE(content.has_value());
  EXPECT_EQ(*content, "inside data");
}

TEST_F(RealFSTest, WriteInsideFile) {
  sfs::RealFSBackend realFs(sandboxDir);
  ASSERT_TRUE(realFs.writeFile("/newfile.txt", "created", 7).has_value());
  EXPECT_TRUE(realFs.exists("/newfile.txt"));
  auto content = realFs.readFile("/newfile.txt");
  ASSERT_TRUE(content.has_value());
  EXPECT_EQ(*content, "created");
}

TEST_F(RealFSTest, EscapeBlocked) {
  sfs::RealFSBackend realFs(sandboxDir);
  auto r = realFs.readFile("/../outside/secret.txt");
  EXPECT_FALSE(r.has_value());
  EXPECT_EQ(r.error(), EACCES);
}

TEST_F(RealFSTest, ReaddirRoot) {
  sfs::RealFSBackend realFs(sandboxDir);
  auto entries = realFs.readdir("/");
  ASSERT_TRUE(entries.has_value());
  EXPECT_GE(entries->size(), 1);
}

TEST_F(RealFSTest, UnlinkFile) {
  sfs::RealFSBackend realFs(sandboxDir);
  ASSERT_TRUE(realFs.unlink("/inside.txt").has_value());
  EXPECT_FALSE(realFs.exists("/inside.txt"));
}

class VirtualFSWithRealTest : public ::testing::Test {
protected:
  void SetUp() override {
    std::error_code ec;
    tmpDir = fs::temp_directory_path(ec) / "sandboxed-fs-test-vfs";
    bundleDir = tmpDir / "bundle";
    sandboxDir = tmpDir / "sandbox";

    fs::remove_all(tmpDir, ec);
    fs::create_directories(bundleDir, ec);
    fs::create_directories(sandboxDir, ec);

    std::ofstream(bundleDir / "app.js") << "const x = 1;";

    auto bundleFs = std::make_shared<sfs::RealFSBackend>(bundleDir);
    auto sandboxFs = std::make_shared<sfs::RealFSBackend>(sandboxDir);

    vfs = std::make_unique<sfs::VirtualFS>(std::vector<sfs::MountEntry>{
        {"/bundle", sfs::Perm::Read, bundleFs},
        {"/home/sandbox", sfs::Perm::Read | sfs::Perm::Write, sandboxFs},
    });
  }

  void TearDown() override {
    std::error_code ec;
    fs::remove_all(tmpDir, ec);
  }

  fs::path tmpDir;
  fs::path bundleDir;
  fs::path sandboxDir;
  std::unique_ptr<sfs::VirtualFS> vfs;
};

TEST_F(VirtualFSWithRealTest, ReadBundleThroughVFS) {
  EXPECT_TRUE(vfs->exists("/bundle/app.js"));
  auto content = vfs->readFile("/bundle/app.js");
  ASSERT_TRUE(content.has_value());
  EXPECT_EQ(*content, "const x = 1;");
}

TEST_F(VirtualFSWithRealTest, WriteSandboxThroughVFS) {
  ASSERT_TRUE(vfs->writeFile("/home/sandbox/output.txt", "result", 6).has_value());
  EXPECT_TRUE(vfs->exists("/home/sandbox/output.txt"));
  auto content = vfs->readFile("/home/sandbox/output.txt");
  ASSERT_TRUE(content.has_value());
  EXPECT_EQ(*content, "result");
}

TEST_F(VirtualFSWithRealTest, CrossMountCopy) {
  ASSERT_TRUE(vfs->writeFile("/home/sandbox/data.txt", "copied", 6).has_value());
  ASSERT_TRUE(vfs->copyFile("/home/sandbox/data.txt", "/home/sandbox/data2.txt").has_value());
  EXPECT_TRUE(vfs->exists("/home/sandbox/data2.txt"));
  auto content = vfs->readFile("/home/sandbox/data2.txt");
  ASSERT_TRUE(content.has_value());
  EXPECT_EQ(*content, "copied");
}
