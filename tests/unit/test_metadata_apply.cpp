#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#endif

#include "MetadataApply.hpp"
#include "MetadataTags.hpp"

namespace ma = MetadataApply;
namespace mt = MetadataTags;
namespace fs = std::filesystem;

namespace {

const std::string kFixtures = FIXTURE_DIR;

std::string unique_suffix() {
    static std::random_device device;
    static unsigned counter = 0;
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::to_string(device()) + "-" + std::to_string(ticks) + "-" + std::to_string(++counter);
}

std::string scratch_dir(const std::string& tag) {
    const fs::path dir = fs::temp_directory_path() / ("mm-apply-" + tag + "-" + unique_suffix());
    fs::create_directories(dir);
    return dir.string();
}

std::string copy_fixture(const std::string& dir, const std::string& name) {
    const fs::path target = fs::path(dir) / name;
    fs::copy_file(fs::path(kFixtures) / name, target, fs::copy_options::overwrite_existing);
    return target.string();
}

std::string slurp(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

}  // namespace

TEST_CASE("Track and disc compare as numbers, everything else as trimmed text", "[apply]") {
    CHECK(ma::normalise("  Hello  ", "title") == "Hello");
    CHECK(ma::normalise("03", "track") == "3");
    CHECK(ma::normalise("03/012", "track") == "3/12");
    CHECK(ma::normalise("7/0", "track") == "7");
    CHECK(ma::normalise("0", "disc") == "0");
    CHECK(ma::normalise("00", "disc") == "0");
    CHECK(ma::normalise("2/10", "disc") == "2/10");
    CHECK(ma::normalise("three", "track") == "three");
    CHECK(ma::normalise("03", "title") == "03");
    CHECK(ma::normalise("", "track") == "");
    CHECK(ma::normalise("09/019", "track") == "9/19");
    CHECK(ma::normalise("7/", "track") == "7/");
    CHECK(ma::normalise("/5", "track") == "/5");
    CHECK(ma::normalise("7/x", "track") == "7/x");
    CHECK(ma::normalise("0x", "track") == "0x");
    CHECK(ma::normalise("x/05", "disc") == "x/05");
}

#ifndef _WIN32
TEST_CASE("A fingerprint's time is nanoseconds since the Unix epoch", "[apply]") {
    const std::string dir = scratch_dir("epoch");
    const std::string path = dir + "/a.mp3";
    fs::copy_file(fs::path(kFixtures) / "a.mp3", path);
    timespec times[2] = {{1700000000, 123456789}, {1700000000, 123456789}};
    REQUIRE(utimensat(AT_FDCWD, path.c_str(), times, 0) == 0);
    const auto print = ma::fingerprint(path);
    REQUIRE(print.has_value());
    CHECK(print->mtime_ns == 1700000000123456789LL);
}
#endif

TEST_CASE("A fingerprint is size and modification time, and a missing file has none", "[apply]") {
    const std::string dir = scratch_dir("fp");
    const std::string path = copy_fixture(dir, "a.mp3");
    const auto before = ma::fingerprint(path);
    REQUIRE(before.has_value());
    CHECK(before->size == fs::file_size(path));
    CHECK(*before == *ma::fingerprint(path));
    { std::ofstream(path, std::ios::app | std::ios::binary) << "x"; }
    CHECK_FALSE(*before == *ma::fingerprint(path));
    CHECK_FALSE(ma::fingerprint(dir + "/nope.mp3").has_value());
    fs::remove_all(dir);
}

TEST_CASE("Read-back keeps what stuck and reports what did not", "[apply]") {
    const std::vector<ma::Edit> edits = {
        {"title", "New"}, {"artist", "Gone"}, {"track", "3/12"}, {"album", ""}, {"genre", ""}};
    const std::map<std::string, std::string> got = {
        {"title", "New"}, {"artist", "Other"}, {"track", "03/012"}, {"genre", "Rock"}};
    const auto verdict = ma::verify_edits(edits, got);
    CHECK(verdict.applied == std::vector<std::string>{"title", "track", "album"});
    REQUIRE(verdict.dropped.size() == 2);
    CHECK(verdict.dropped[0].key == "artist");
    CHECK(verdict.dropped[0].wanted == "Gone");
    CHECK(verdict.dropped[0].got == std::optional<std::string>("Other"));
    CHECK(verdict.dropped[1].key == "genre");
    CHECK(verdict.dropped[1].wanted.empty());
    CHECK(verdict.dropped[1].got == std::optional<std::string>("Rock"));
}

TEST_CASE("A tag that never arrived is a drop with nothing held", "[apply]") {
    const auto verdict = ma::verify_edits({{"title", "Wanted"}}, {});
    REQUIRE(verdict.dropped.size() == 1);
    CHECK_FALSE(verdict.dropped[0].got.has_value());
}

TEST_CASE("A good write keeps a backup of the original and reads the tags back", "[apply]") {
    const std::string dir = scratch_dir("ok");
    const std::string path = copy_fixture(dir, "a.mp3");
    const auto planned = *ma::fingerprint(path);
    const std::string original = slurp(path);
    const auto out = ma::apply_entry(path, planned, {{"title", "Probe Title"}, {"track", "3/12"}}, false);
    REQUIRE(out.ok);
    CHECK(out.verdict.applied == std::vector<std::string>{"title", "track"});
    CHECK(out.verdict.dropped.empty());
    CHECK(mt::read_tag(path, "title") == std::optional<std::string>("Probe Title"));
    const std::string bak = path + ma::kBackupSuffix;
    REQUIRE(fs::exists(bak));
    CHECK(slurp(bak) == original);
    CHECK(*ma::fingerprint(bak) == planned);
    fs::remove_all(dir);
}

TEST_CASE("No backup is made when the caller opts out", "[apply]") {
    const std::string dir = scratch_dir("nobak");
    const std::string path = copy_fixture(dir, "a.flac");
    const auto out = ma::apply_entry(path, *ma::fingerprint(path), {{"title", "T"}}, true);
    REQUIRE(out.ok);
    CHECK_FALSE(fs::exists(path + ma::kBackupSuffix));
    fs::remove_all(dir);
}

TEST_CASE("A file that changed since the plan is left alone", "[apply]") {
    const std::string dir = scratch_dir("stale");
    const std::string path = copy_fixture(dir, "a.mp3");
    const auto planned = *ma::fingerprint(path);
    { std::ofstream(path, std::ios::app | std::ios::binary) << "x"; }
    const std::string before = slurp(path);
    const auto out = ma::apply_entry(path, planned, {{"title", "T"}}, false);
    CHECK_FALSE(out.ok);
    CHECK(contains(out.error, "changed since plan"));
    CHECK(slurp(path) == before);
    CHECK_FALSE(fs::exists(path + ma::kBackupSuffix));
    fs::remove_all(dir);
}

TEST_CASE("A missing file is reported, not created", "[apply]") {
    const std::string dir = scratch_dir("gone");
    const auto out = ma::apply_entry(dir + "/gone.mp3", {10, 5}, {{"title", "T"}}, false);
    CHECK_FALSE(out.ok);
    CHECK(contains(out.error, "file gone"));
    fs::remove_all(dir);
}

TEST_CASE("A backup from another job stops the write", "[apply]") {
    const std::string dir = scratch_dir("other");
    const std::string path = copy_fixture(dir, "a.mp3");
    const auto planned = *ma::fingerprint(path);
    { std::ofstream(path + ma::kBackupSuffix, std::ios::binary) << "someone else's backup"; }
    const std::string before = slurp(path);
    const auto out = ma::apply_entry(path, planned, {{"title", "T"}}, false);
    CHECK_FALSE(out.ok);
    CHECK(contains(out.error, "another job"));
    CHECK(contains(out.error, "a.mp3.mm-bak"));
    CHECK(slurp(path) == before);
    CHECK(slurp(path + ma::kBackupSuffix) == "someone else's backup");
    fs::remove_all(dir);
}

TEST_CASE("A matching backup is our own from a crash: roll back and redo", "[apply]") {
    const std::string dir = scratch_dir("resume");
    const std::string path = copy_fixture(dir, "a.mp3");
    const auto planned = *ma::fingerprint(path);
    // First run: backup made, write got half done, then the process died.
    REQUIRE(ma::apply_entry(path, planned, {{"artist", "Half Done"}}, false).ok);
    REQUIRE(mt::read_tag(path, "artist").has_value());
    const auto out = ma::apply_entry(path, planned, {{"title", "Redo"}}, false);
    REQUIRE(out.ok);
    CHECK(mt::read_tag(path, "title") == std::optional<std::string>("Redo"));
    CHECK_FALSE(mt::read_tag(path, "artist").has_value());
    CHECK(fs::exists(path + ma::kBackupSuffix));
    fs::remove_all(dir);
}

TEST_CASE("A failed write puts the original back and leaves no backup", "[apply]") {
    const std::string dir = scratch_dir("fail");
    const std::string path = copy_fixture(dir, "a.mp3");
    const auto planned = *ma::fingerprint(path);
    const std::string before = slurp(path);
    const auto out = ma::apply_entry(path, planned, {{"title", "T"}, {"track", "three"}}, false);
    CHECK_FALSE(out.ok);
    CHECK(contains(out.error, "track/disc"));
    CHECK(slurp(path) == before);
    CHECK_FALSE(fs::exists(path + ma::kBackupSuffix));
    fs::remove_all(dir);
}

TEST_CASE("A file type with no writer fails cleanly", "[apply]") {
    const std::string dir = scratch_dir("nowriter");
    const std::string path = dir + "/notes.txt";
    { std::ofstream(path) << "hello"; }
    const auto out = ma::apply_entry(path, *ma::fingerprint(path), {{"title", "T"}}, false);
    CHECK_FALSE(out.ok);
    CHECK(contains(out.error, "no tag writer"));
    CHECK(slurp(path) == "hello");
    CHECK_FALSE(fs::exists(path + ma::kBackupSuffix));
    fs::remove_all(dir);
}

TEST_CASE("Markers are found under folders and beside single files", "[apply]") {
    const std::string dir = scratch_dir("marks");
    fs::create_directories(dir + "/sub");
    { std::ofstream(dir + "/b.mp3.mm-bak") << "1"; }
    { std::ofstream(dir + "/sub/c.mp3.mm-new") << "2"; }
    { std::ofstream(dir + "/plain.mp3") << "3"; }
    { std::ofstream(dir + "/solo.mp3") << "4"; }
    { std::ofstream(dir + "/solo.mp3.mm-bak") << "5"; }
    const auto all = ma::find_marks({dir});
    CHECK(all.error.empty());
    CHECK(all.paths ==
          std::vector<std::string>{dir + "/b.mp3.mm-bak", dir + "/solo.mp3.mm-bak", dir + "/sub/c.mp3.mm-new"});
    CHECK(ma::find_marks({dir + "/solo.mp3"}).paths == std::vector<std::string>{dir + "/solo.mp3.mm-bak"});
    CHECK(ma::find_marks({dir + "/b.mp3.mm-bak"}).paths == std::vector<std::string>{dir + "/b.mp3.mm-bak"});
    CHECK(ma::find_marks({dir + "/plain.mp3"}).paths.empty());
    CHECK(ma::find_marks({dir, dir}).paths.size() == 3);
    const auto missing = ma::find_marks({dir + "/nope.mp3"});
    CHECK(contains(missing.error, "not found"));
    fs::remove_all(dir);
}

TEST_CASE("Restore puts backups back and deletes what the job created", "[apply]") {
    const std::string dir = scratch_dir("restore");
    { std::ofstream(dir + "/a.mp3") << "edited"; }
    { std::ofstream(dir + "/a.mp3.mm-bak") << "original"; }
    { std::ofstream(dir + "/new.txt") << "made by the job"; }
    { std::ofstream(dir + "/new.txt.mm-new") << ""; }
    const auto result = ma::restore({dir});
    CHECK(result.error.empty());
    CHECK(slurp(dir + "/a.mp3") == "original");
    CHECK_FALSE(fs::exists(dir + "/a.mp3.mm-bak"));
    CHECK_FALSE(fs::exists(dir + "/new.txt"));
    CHECK_FALSE(fs::exists(dir + "/new.txt.mm-new"));
    CHECK(result.restored == std::vector<std::string>{dir + "/a.mp3"});
    CHECK(result.removed_created == std::vector<std::string>{dir + "/new.txt"});
    fs::remove_all(dir);
}

TEST_CASE("Clean is a dry run until told otherwise", "[apply]") {
    const std::string dir = scratch_dir("clean");
    { std::ofstream(dir + "/a.mp3") << "kept"; }
    { std::ofstream(dir + "/a.mp3.mm-bak") << "backup"; }
    const auto dry = ma::clean({dir}, false);
    CHECK(dry.would_delete == std::vector<std::string>{dir + "/a.mp3.mm-bak"});
    CHECK(dry.deleted.empty());
    CHECK(fs::exists(dir + "/a.mp3.mm-bak"));
    const auto real = ma::clean({dir}, true);
    CHECK(real.deleted == std::vector<std::string>{dir + "/a.mp3.mm-bak"});
    CHECK_FALSE(fs::exists(dir + "/a.mp3.mm-bak"));
    CHECK(slurp(dir + "/a.mp3") == "kept");
    fs::remove_all(dir);
}

TEST_CASE("A file named exactly like a marker still counts as one", "[apply]") {
    const std::string dir = scratch_dir("bare");
    { std::ofstream(dir + "/.mm-bak") << "1"; }
    { std::ofstream(dir + "/.mm-new") << "2"; }
    CHECK(ma::find_marks({dir}).paths == std::vector<std::string>{dir + "/.mm-bak", dir + "/.mm-new"});
    fs::remove_all(dir);
}

#ifndef _WIN32
TEST_CASE("A backup that cannot be made stops the write before it starts", "[apply]") {
    const std::string dir = scratch_dir("nobackup");
    const std::string path = copy_fixture(dir, "a.mp3");
    const auto planned = *ma::fingerprint(path);
    const std::string before = slurp(path);
    // A dangling link in the backup slot: it looks absent, but nothing can be written through it.
    std::error_code link_error;
    fs::create_symlink(dir + "/missing-folder/backup", path + ma::kBackupSuffix, link_error);
    REQUIRE_FALSE(link_error);
    const auto out = ma::apply_entry(path, planned, {{"title", "T"}}, false);
    CHECK_FALSE(out.ok);
    CHECK(contains(out.error, "could not make a backup"));
    CHECK(slurp(path) == before);
    fs::remove_all(dir);
}
#endif
