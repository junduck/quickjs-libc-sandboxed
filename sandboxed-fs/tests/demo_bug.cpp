#include "sandboxed-fs/vfs.hpp"
#include "sandboxed-fs/vfs_mem.hpp"

#include <iostream>
#include <vector>

namespace sfs = sandboxed_fs;

static void heading(const char *title) { std::cout << "\n\033[1m--- " << title << " ---\033[0m\n"; }
static void ok(const char *msg) { std::cout << "  \033[32m[OK]\033[0m " << msg << "\n"; }
static void bad(const char *msg) { std::cout << "  \033[31m[BROKEN]\033[0m " << msg << "\n"; }

int main() {
  auto memFs = std::make_shared<sfs::MemFSBackend>();
  memFs->writeFile("/file.txt", "TOP_LEVEL_FILE", 15);
  memFs->mkdir("/sub", false);
  memFs->writeFile("/sub/file.txt", "NESTED_FILE", 11);

  heading("Direct MemFS Backend (Reference)");
  std::cout << "  readFile(\"/file.txt\") = \"" << memFs->readFile("/file.txt").value() << "\"\n";
  std::cout << "  readFile(\"/sub/file.txt\") = \"" << memFs->readFile("/sub/file.txt").value() << "\"\n";
  {
    auto d = memFs->readdir("/sub");
    std::cout << "  readdir(\"/sub\") = {";
    if (d.has_value()) {
      for (size_t i = 0; i < d->size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << "\"" << (*d)[i] << "\"";
      }
    }
    std::cout << "}\n";
  }

  heading("VirtualFS with OVERLAPPING mounts to SAME backend");
  std::cout << "  Mounts: /app -> memFs, /app/sub -> memFs (backendPath=/sub)\n";
  std::cout << "  (Both point to the same MemFSBackend)\n";

  sfs::VirtualFS vfs({
      {"/app", sfs::Perm::Read, memFs},
      {"/app/sub", sfs::Perm::Read, memFs, "/sub"},
  });

  {
    auto r = vfs.readFile("/app/file.txt");
    std::string val = r ? *r : "(error)";
    std::cout << "  readFile(\"/app/file.txt\") = \"" << val << "\"\n";
    if (r && r->size() == 15) ok("correct"); else bad("WRONG");
  }
  {
    auto r = vfs.readFile("/app/sub/file.txt");
    std::string val = r ? *r : "(error)";
    std::cout << "  readFile(\"/app/sub/file.txt\") = \"" << val << "\" (expected \"NESTED_FILE\")\n";
    if (r && *r == "NESTED_FILE") ok("correct"); else bad("WRONG - returns root's file.txt content!");
  }
  {
    auto d = vfs.readdir("/app/sub");
    std::cout << "  readdir(\"/app/sub\") = {";
    if (d.has_value()) {
      for (size_t i = 0; i < d->size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << "\"" << (*d)[i] << "\"";
      }
    }
    std::cout << "} (expected {\"file.txt\"})\n";
    if (d && d->size() == 1 && (*d)[0] == "file.txt") ok("correct"); else bad("WRONG - shows root contents!");
  }
  {
    auto s = vfs.stat("/app/sub/file.txt");
    std::cout << "  stat(\"/app/sub/file.txt\").size = " << (s ? std::to_string(s->size) : "(error)") << " (expected 11)\n";
    if (s && s->size == 11) ok("correct"); else bad("WRONG - reports 15 (root file size)!");
  }

  heading("Bug Explanation");
  std::cout << "  When VirtualFS resolves path \"/app/sub\", it matches the longest\n";
  std::cout << "  prefix \"/app/sub\" and passes \"/\" as subPath to MemFS.\n";
  std::cout << "  But when it tries to access \"/\" in MemFS, the walkTo() for \"/\"\n";
  std::cout << "  returns the ROOT node, not the \"/sub\" directory node.\n";
  std::cout << "  This causes ALL file operations on /app/sub to resolve to root.\n";

  return 0;
}