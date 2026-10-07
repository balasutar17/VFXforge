#include <catch2/catch_amalgamated.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "helpers.h"
#include "vfx/FileIO.h"

using namespace vfx;
namespace fs = std::filesystem;

namespace {

// A fresh, empty folder for one test, removed afterwards.
class TempDir {
public:
    TempDir() {
        IdGenerator ids = IdGenerator::fromEntropy();
        path_ = fs::temp_directory_path() / ("vfxforge-test-" + formatId('t', ids.next()));
        fs::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    const fs::path& path() const { return path_; }

    std::size_t fileCount() const {
        std::size_t n = 0;
        for (const auto& entry : fs::directory_iterator(path_)) {
            (void)entry;
            ++n;
        }
        return n;
    }

private:
    fs::path path_;
};

std::string slurp(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

}  // namespace

TEST_CASE("Saving writes exactly the canonical text and leaves no temporary file") {
    TempDir dir;
    const fs::path file = dir.path() / "effect.vfx";
    const Effect effect = testing::sampleEffect();

    REQUIRE(saveEffect(file, effect).ok());
    CHECK(slurp(file) == writeEffect(effect));
    CHECK(dir.fileCount() == 1);
}

TEST_CASE("Saving over an existing file replaces it whole") {
    TempDir dir;
    const fs::path file = dir.path() / "effect.vfx";
    Effect effect = testing::sampleEffect();
    REQUIRE(saveEffect(file, effect).ok());

    effect.layers.pop_back();  // the second save is shorter than the first
    effect.name = "Second";
    REQUIRE(saveEffect(file, effect).ok());
    CHECK(slurp(file) == writeEffect(effect));
    CHECK(dir.fileCount() == 1);

    auto loaded = loadEffect(file);
    REQUIRE(loaded.ok());
    CHECK(loaded.value().effect.name == "Second");
    CHECK(loaded.value().effect.layers.size() == 1);
}

TEST_CASE("A failed save leaves the previous file untouched") {
    TempDir dir;
    const fs::path missingFolder = dir.path() / "no-such-folder" / "effect.vfx";
    const Status status = saveEffect(missingFolder, testing::sampleEffect());
    REQUIRE_FALSE(status.ok());
    CHECK(status.error().message.find("could not be saved") != std::string::npos);
    CHECK(dir.fileCount() == 0);

    CHECK_FALSE(writeFileAtomic(fs::path(), "x").ok());
}

#if !defined(_WIN32)
TEST_CASE("A save that cannot complete keeps the old contents and cleans up") {
    TempDir dir;
    const fs::path file = dir.path() / "effect.vfx";
    REQUIRE(writeFileAtomic(file, "original").ok());

    // A folder with the target's name as a child cannot be renamed over.
    const fs::path blocked = dir.path() / "blocked";
    fs::create_directories(blocked / "child");
    CHECK_FALSE(writeFileAtomic(blocked, "new contents").ok());
    CHECK(fs::is_directory(blocked));
    CHECK(slurp(file) == "original");
    CHECK(dir.fileCount() == 2);  // the file and the folder, no stray temporary file
}

TEST_CASE("Saving keeps the file's permissions") {
    TempDir dir;
    const fs::path file = dir.path() / "effect.vfx";
    REQUIRE(writeFileAtomic(file, "one").ok());
    fs::permissions(file, fs::perms::owner_read | fs::perms::owner_write);
    REQUIRE(writeFileAtomic(file, "two").ok());
    const auto perms = fs::status(file).permissions();
    CHECK((perms & fs::perms::group_read) == fs::perms::none);
    CHECK((perms & fs::perms::others_read) == fs::perms::none);
    CHECK(slurp(file) == "two");
}
#endif

TEST_CASE("File names in any language work") {
    TempDir dir;
    const fs::path file = dir.path() / pathFromUtf8("火花 Étoile ✨.vfx");
    const Effect effect = testing::sampleEffect();
    REQUIRE(saveEffect(file, effect).ok());
    auto loaded = loadEffect(file);
    REQUIRE(loaded.ok());
    CHECK(writeEffect(loaded.value().effect) == writeEffect(effect));
    CHECK(pathToUtf8(file.filename()) == "火花 Étoile ✨.vfx");
}

TEST_CASE("Loading something that is not there gives a readable error") {
    TempDir dir;
    auto missing = loadEffect(dir.path() / "nope.vfx");
    REQUIRE_FALSE(missing.ok());
    CHECK(missing.error().message.find("nope.vfx") != std::string::npos);
    CHECK(missing.error().message.find("could not be found") != std::string::npos);

    auto folder = loadEffect(dir.path());
    REQUIRE_FALSE(folder.ok());
}

TEST_CASE("A damaged file reports which file it was") {
    TempDir dir;
    const fs::path file = dir.path() / "broken.vfx";
    REQUIRE(writeFileAtomic(file, "{ not an effect").ok());
    auto loaded = loadEffect(file);
    REQUIRE_FALSE(loaded.ok());
    CHECK(loaded.error().detail.find("broken.vfx") != std::string::npos);
}

TEST_CASE("Files over the size limit are refused before being read") {
    TempDir dir;
    const fs::path file = dir.path() / "big.bin";
    REQUIRE(writeFileAtomic(file, std::string(4096, 'x')).ok());
    CHECK(readFile(file, 4096).ok());
    CHECK_FALSE(readFile(file, 4095).ok());
}

TEST_CASE("Empty files and binary content pass through unchanged") {
    TempDir dir;
    const fs::path file = dir.path() / "data.bin";
    REQUIRE(writeFileAtomic(file, "").ok());
    auto empty = readFile(file);
    REQUIRE(empty.ok());
    CHECK(empty.value().empty());

    std::string binary;
    for (int i = 0; i < 256; ++i) {
        binary += static_cast<char>(i);
    }
    binary += "\r\n\r\n";
    REQUIRE(writeFileAtomic(file, binary).ok());
    auto back = readFile(file);
    REQUIRE(back.ok());
    CHECK(back.value() == binary);  // no line-ending translation on any platform
}
