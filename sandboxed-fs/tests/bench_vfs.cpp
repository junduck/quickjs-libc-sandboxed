#include "sandboxed-fs/vfs.hpp"
#include "sandboxed-fs/vfs_mem.hpp"
#include "sandboxed-fs/vfs_real.hpp"

#include <benchmark/benchmark.h>
#include <filesystem>
#include <fstream>

namespace sfs = sandboxed_fs;
namespace fs = std::filesystem;

static void BM_MemFS_WriteFile(benchmark::State& state) {
    sfs::MemFSBackend fs;
    std::string data(state.range(0), 'x');
    int n = 0;
    for (auto _ : state) {
        auto path = "/file" + std::to_string(n++) + ".txt";
        fs.writeFile(path, data.data(), data.size());
    }
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * state.range(0));
}
BENCHMARK(BM_MemFS_WriteFile)->Arg(1024)->Arg(65536)->Arg(1048576);

static void BM_MemFS_ReadFile(benchmark::State& state) {
    sfs::MemFSBackend fs;
    std::string data(state.range(0), 'x');
    fs.writeFile("/data.txt", data.data(), data.size());
    for (auto _ : state) {
        auto result = fs.readFile("/data.txt");
        benchmark::DoNotOptimize(result);
    }
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * state.range(0));
}
BENCHMARK(BM_MemFS_ReadFile)->Arg(1024)->Arg(65536)->Arg(1048576);

static void BM_RealFS_WriteFile(benchmark::State& state) {
    std::error_code ec;
    auto tmpDir = fs::temp_directory_path(ec) / "sandboxed-fs-bench";
    fs::remove_all(tmpDir, ec);
    fs::create_directories(tmpDir, ec);

    sfs::RealFSBackend fs(tmpDir);
    std::string data(state.range(0), 'x');
    int n = 0;
    for (auto _ : state) {
        auto path = "/file" + std::to_string(n++) + ".txt";
        fs.writeFile(path, data.data(), data.size());
    }
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * state.range(0));

    fs::remove_all(tmpDir, ec);
}
BENCHMARK(BM_RealFS_WriteFile)->Arg(1024)->Arg(65536);

static void BM_RealFS_ReadFile(benchmark::State& state) {
    std::error_code ec;
    auto tmpDir = fs::temp_directory_path(ec) / "sandboxed-fs-bench";
    fs::remove_all(tmpDir, ec);
    fs::create_directories(tmpDir, ec);

    sfs::RealFSBackend fs(tmpDir);
    std::string data(state.range(0), 'x');
    fs.writeFile("/data.txt", data.data(), data.size());
    for (auto _ : state) {
        auto result = fs.readFile("/data.txt");
        benchmark::DoNotOptimize(result);
    }
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * state.range(0));

    fs::remove_all(tmpDir, ec);
}
BENCHMARK(BM_RealFS_ReadFile)->Arg(1024)->Arg(65536);

static void BM_VirtualFS_RouteOverhead(benchmark::State& state) {
    auto memFs = std::make_shared<sfs::MemFSBackend>();
    memFs->writeFile("/app.js", "console.log(1)", 14);

    sfs::VirtualFS vfs({
        {"/bundle", sfs::Perm::Read, memFs},
    });

    for (auto _ : state) {
        auto result = vfs.readFile("/bundle/app.js");
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_VirtualFS_RouteOverhead);

BENCHMARK_MAIN();
