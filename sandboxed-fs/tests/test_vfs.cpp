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
  fs.writeFile("/hello.txt", "Hello, World!", 13);
  EXPECT_TRUE(fs.exists("/hello.txt"));
  EXPECT_EQ(fs.readFile("/hello.txt"), "Hello, World!");

  auto bytes = fs.readFileBytes("/hello.txt");
  EXPECT_EQ(bytes.size(), 13);
  EXPECT_EQ(bytes[0], 'H');
}

TEST_F(MemFSTest, AppendFile) {
  fs.writeFile("/hello.txt", "Hello", 5);
  fs.appendFile("/hello.txt", " World", 6);
  EXPECT_EQ(fs.readFile("/hello.txt"), "Hello World");
}

TEST_F(MemFSTest, StatFile) {
  fs.writeFile("/data.bin", "0123456789", 10);
  auto st = fs.stat("/data.bin");
  EXPECT_TRUE(st.isFile());
  EXPECT_FALSE(st.isDir());
  EXPECT_EQ(st.size, 10);
}

TEST_F(MemFSTest, UnlinkFile) {
  fs.writeFile("/temp.txt", "x", 1);
  EXPECT_TRUE(fs.exists("/temp.txt"));
  fs.unlink("/temp.txt");
  EXPECT_FALSE(fs.exists("/temp.txt"));
}

TEST_F(MemFSTest, ReadNonexistentThrows) {
  EXPECT_THROW(fs.readFile("/nonexistent.txt"), sfs::VFSError);
  try {
    fs.readFile("/nonexistent.txt");
    FAIL() << "Expected VFSError";
  } catch (const sfs::VFSError &e) {
    EXPECT_EQ(e.code(), ENOENT);
  }
}

TEST_F(MemFSTest, ReadDirOnFileThrows) {
  fs.writeFile("/file.txt", "data", 4);
  EXPECT_THROW(fs.readdir("/file.txt"), sfs::VFSError);
  try {
    fs.readdir("/file.txt");
    FAIL() << "Expected VFSError";
  } catch (const sfs::VFSError &e) {
    EXPECT_EQ(e.code(), ENOTDIR);
  }
}

class MemFSDirTest : public ::testing::Test {
protected:
  sfs::MemFSBackend fs;
};

TEST_F(MemFSDirTest, Mkdir) {
  fs.mkdir("/a", false);
  EXPECT_TRUE(fs.exists("/a"));
  auto st = fs.stat("/a");
  EXPECT_TRUE(st.isDir());
}

TEST_F(MemFSDirTest, RecursiveMkdir) {
  fs.mkdir("/a/b/c", true);
  EXPECT_TRUE(fs.exists("/a"));
  EXPECT_TRUE(fs.exists("/a/b"));
  EXPECT_TRUE(fs.exists("/a/b/c"));

  auto entries = fs.readdir("/a");
  EXPECT_EQ(entries.size(), 1);
  EXPECT_EQ(entries[0], "b");
}

TEST_F(MemFSDirTest, Readdir) {
  fs.mkdir("/dir", false);
  fs.writeFile("/dir/a.txt", "a", 1);
  fs.writeFile("/dir/b.txt", "b", 1);
  fs.mkdir("/dir/sub", false);

  auto entries = fs.readdir("/dir");
  EXPECT_EQ(entries.size(), 3);
}

TEST_F(MemFSDirTest, RmdirNonEmptyFails) {
  fs.mkdir("/dir", false);
  fs.writeFile("/dir/file.txt", "x", 1);
  EXPECT_THROW(fs.rmdir("/dir"), sfs::VFSError);
  try {
    fs.rmdir("/dir");
    FAIL() << "Expected VFSError";
  } catch (const sfs::VFSError &e) {
    EXPECT_EQ(e.code(), ENOTEMPTY);
  }
}

TEST_F(MemFSDirTest, RemoveHierarchy) {
  fs.mkdir("/a/b/c", true);
  fs.writeFile("/a/b/c/file.txt", "data", 4);
  fs.unlink("/a/b/c/file.txt");
  fs.rmdir("/a/b/c");
  fs.rmdir("/a/b");
  fs.rmdir("/a");
  EXPECT_FALSE(fs.exists("/a"));
}

class MemFSRenameCopyTest : public ::testing::Test {
protected:
  sfs::MemFSBackend fs;
};

TEST_F(MemFSRenameCopyTest, RenameFile) {
  fs.writeFile("/src.txt", "source", 6);
  fs.rename("/src.txt", "/dst.txt");
  EXPECT_FALSE(fs.exists("/src.txt"));
  EXPECT_TRUE(fs.exists("/dst.txt"));
  EXPECT_EQ(fs.readFile("/dst.txt"), "source");
}

TEST_F(MemFSRenameCopyTest, CopyFile) {
  fs.writeFile("/src.txt", "source", 6);
  fs.copyFile("/src.txt", "/copy.txt");
  EXPECT_TRUE(fs.exists("/src.txt"));
  EXPECT_TRUE(fs.exists("/copy.txt"));
  EXPECT_EQ(fs.readFile("/copy.txt"), "source");
}

TEST_F(MemFSRenameCopyTest, RenameNonexistentThrows) { EXPECT_THROW(fs.rename("/nonexistent", "/foo"), sfs::VFSError); }

TEST_F(MemFSRenameCopyTest, RenameCrossDir) {
  fs.mkdir("/a", false);
  fs.mkdir("/b", false);
  fs.writeFile("/a/file.txt", "data", 4);
  fs.rename("/a/file.txt", "/b/renamed.txt");
  EXPECT_FALSE(fs.exists("/a/file.txt"));
  EXPECT_TRUE(fs.exists("/b/renamed.txt"));
  EXPECT_EQ(fs.readFile("/b/renamed.txt"), "data");
}

TEST_F(MemFSRenameCopyTest, Truncate) {
  fs.writeFile("/data.txt", "hello world", 11);
  fs.truncate("/data.txt", 5);
  EXPECT_EQ(fs.readFile("/data.txt"), "hello");
  EXPECT_EQ(fs.stat("/data.txt").size, 5);

  fs.truncate("/data.txt", 10);
  EXPECT_EQ(fs.stat("/data.txt").size, 10);
}

TEST_F(MemFSRenameCopyTest, Utimes) {
  fs.writeFile("/data.txt", "x", 1);
  fs.utimes("/data.txt", 1000, 2000);
  auto st = fs.stat("/data.txt");
  EXPECT_EQ(st.atimeMs, 1000);
  EXPECT_EQ(st.mtimeMs, 2000);
}

class VirtualFSTest : public ::testing::Test {
protected:
  void SetUp() override {
    bundleFs = std::make_shared<sfs::MemFSBackend>();
    bundleFs->writeFile("/app.js", "console.log(1)", 14);
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
  EXPECT_EQ(vfs->readFile("/bundle/app.js"), "console.log(1)");
}

TEST_F(VirtualFSTest, WriteToSandbox) {
  vfs->writeFile("/home/sandbox/data.txt", "sandbox-data", 12);
  EXPECT_EQ(vfs->readFile("/home/sandbox/data.txt"), "sandbox-data");
}

TEST_F(VirtualFSTest, WriteToReadOnlyMountThrows) {
  EXPECT_THROW(vfs->writeFile("/bundle/new.js", "hack", 4), sfs::VFSError);
  try {
    vfs->writeFile("/bundle/new.js", "hack", 4);
    FAIL() << "Expected VFSError";
  } catch (const sfs::VFSError &e) {
    EXPECT_EQ(e.code(), EACCES);
  }
}

TEST_F(VirtualFSTest, UnmountedPathThrows) {
  EXPECT_THROW(vfs->readFile("/other/file.txt"), sfs::VFSError);
  try {
    vfs->readFile("/other/file.txt");
    FAIL() << "Expected VFSError";
  } catch (const sfs::VFSError &e) {
    EXPECT_EQ(e.code(), EACCES);
  }
}

TEST_F(VirtualFSTest, ReaddirOnFileThrows) {
  EXPECT_THROW(vfs->readdir("/home/sandbox/data.txt"), sfs::VFSError);
  try {
    vfs->readdir("/home/sandbox/data.txt");
    FAIL() << "Expected VFSError";
  } catch (const sfs::VFSError &e) {
    EXPECT_EQ(e.code(), ENOENT);
  }
}

class MountOrderingTest : public ::testing::Test {
protected:
  void SetUp() override {
    aFs = std::make_shared<sfs::MemFSBackend>();
    aFs->writeFile("/f1.txt", "from-a", 6);
    bFs = std::make_shared<sfs::MemFSBackend>();
    bFs->writeFile("/f2.txt", "from-b", 6);
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
  EXPECT_EQ(vfs->readFile("/a/f1.txt"), "from-a");
  EXPECT_EQ(vfs->readFile("/a/b/f2.txt"), "from-b");
}

TEST_F(MountOrderingTest, Normalization) {
  EXPECT_EQ(vfs->readFile("/a/./f1.txt"), "from-a");
  EXPECT_EQ(vfs->readFile("/a/b/../f1.txt"), "from-a");
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
  EXPECT_EQ(realFs.readFile("/inside.txt"), "inside data");
}

TEST_F(RealFSTest, WriteInsideFile) {
  sfs::RealFSBackend realFs(sandboxDir);
  realFs.writeFile("/newfile.txt", "created", 7);
  EXPECT_TRUE(realFs.exists("/newfile.txt"));
  EXPECT_EQ(realFs.readFile("/newfile.txt"), "created");
}

TEST_F(RealFSTest, EscapeBlocked) {
  sfs::RealFSBackend realFs(sandboxDir);
  EXPECT_THROW(realFs.readFile("/../outside/secret.txt"), sfs::VFSError);
  try {
    realFs.readFile("/../outside/secret.txt");
    FAIL() << "Expected VFSError";
  } catch (const sfs::VFSError &e) {
    EXPECT_EQ(e.code(), EACCES);
  }
}

TEST_F(RealFSTest, ReaddirRoot) {
  sfs::RealFSBackend realFs(sandboxDir);
  auto entries = realFs.readdir("/");
  EXPECT_GE(entries.size(), 1);
}

TEST_F(RealFSTest, UnlinkFile) {
  sfs::RealFSBackend realFs(sandboxDir);
  realFs.unlink("/inside.txt");
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
  EXPECT_EQ(vfs->readFile("/bundle/app.js"), "const x = 1;");
}

TEST_F(VirtualFSWithRealTest, WriteSandboxThroughVFS) {
  vfs->writeFile("/home/sandbox/output.txt", "result", 6);
  EXPECT_TRUE(vfs->exists("/home/sandbox/output.txt"));
  EXPECT_EQ(vfs->readFile("/home/sandbox/output.txt"), "result");
}

TEST_F(VirtualFSWithRealTest, CrossMountCopy) {
  vfs->writeFile("/home/sandbox/data.txt", "copied", 6);
  vfs->copyFile("/home/sandbox/data.txt", "/home/sandbox/data2.txt");
  EXPECT_TRUE(vfs->exists("/home/sandbox/data2.txt"));
  EXPECT_EQ(vfs->readFile("/home/sandbox/data2.txt"), "copied");
}
