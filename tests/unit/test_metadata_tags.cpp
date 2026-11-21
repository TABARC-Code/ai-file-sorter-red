#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include "MetadataTags.hpp"

namespace mt = metadata_tags;
namespace fs = std::filesystem;

namespace {

const std::string kFixtures = FIXTURE_DIR;

// Copies a fixture into a fresh temp folder so each case starts with no tags.
std::string unique_suffix() {
    static std::random_device device;
    static unsigned counter = 0;
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::to_string(device()) + "-" + std::to_string(ticks) + "-" + std::to_string(++counter);
}

std::string scratch_copy(const std::string& name) {
    const fs::path dir = fs::temp_directory_path() / ("mm-tags-" + name + "-" + unique_suffix());
    fs::create_directories(dir);
    const fs::path target = dir / ("tagged" + fs::path(name).extension().string());
    fs::copy_file(fs::path(kFixtures) / name, target, fs::copy_options::overwrite_existing);
    return target.string();
}

}  // namespace

TEST_CASE("Extensions pick the container family, in any case", "[metadata-tags]") {
    CHECK(mt::family_of("a.mp3") == mt::Family::Id3);
    CHECK(mt::family_of("a.WAV") == mt::Family::Id3);
    CHECK(mt::family_of("book.M4B") == mt::Family::Mp4);
    CHECK(mt::family_of("clip.mov") == mt::Family::Mp4);
    CHECK(mt::family_of("a.Flac") == mt::Family::Vorbis);
    CHECK(mt::family_of("a.opus") == mt::Family::Vorbis);
    CHECK(mt::family_of("notes.txt") == mt::Family::Unsupported);
    CHECK(mt::family_of("noextension") == mt::Family::Unsupported);
}

TEST_CASE("ID3 frames follow the mapping, and unknown keys become TXXX", "[metadata-tags]") {
    CHECK(mt::frame_name(mt::Family::Id3, "title") == std::optional<std::string>("TIT2"));
    CHECK(mt::frame_name(mt::Family::Id3, "track") == std::optional<std::string>("TRCK"));
    CHECK(mt::frame_name(mt::Family::Id3, "comment") == std::optional<std::string>("COMM"));
    CHECK(mt::frame_name(mt::Family::Id3, "narrator") == std::optional<std::string>("TXXX:NARRATOR"));
    CHECK(mt::frame_name(mt::Family::Id3, "made_up") == std::optional<std::string>("TXXX:MADE_UP"));
}

TEST_CASE("MP4 atoms follow the mapping, with freeform names for the rest", "[metadata-tags]") {
    CHECK(mt::frame_name(mt::Family::Mp4, "title") == std::optional<std::string>("\xa9nam"));
    CHECK(mt::frame_name(mt::Family::Mp4, "track") == std::optional<std::string>("trkn"));
    CHECK(mt::frame_name(mt::Family::Mp4, "description") == std::optional<std::string>("desc"));
    CHECK(mt::frame_name(mt::Family::Mp4, "isrc") == std::optional<std::string>("----:com.apple.iTunes:ISRC"));
    CHECK(mt::frame_name(mt::Family::Mp4, "made_up") == std::optional<std::string>("----:com.apple.iTunes:MADE_UP"));
}

TEST_CASE("Vorbis fields follow the mapping, with upper-cased custom keys", "[metadata-tags]") {
    CHECK(mt::frame_name(mt::Family::Vorbis, "track") == std::optional<std::string>("TRACKNUMBER"));
    CHECK(mt::frame_name(mt::Family::Vorbis, "album_artist") == std::optional<std::string>("ALBUMARTIST"));
    CHECK(mt::frame_name(mt::Family::Vorbis, "made_up") == std::optional<std::string>("MADE_UP"));
    CHECK_FALSE(mt::frame_name(mt::Family::Unsupported, "title").has_value());
}

TEST_CASE("Track and disc numbers split into number and total", "[metadata-tags]") {
    const auto plain = mt::split_number("3");
    REQUIRE(plain.has_value());
    CHECK(plain->number == 3);
    CHECK(plain->total == 0);
    const auto pair = mt::split_number(" 3 / 12 ");
    REQUIRE(pair.has_value());
    CHECK(pair->number == 3);
    CHECK(pair->total == 12);
    CHECK_FALSE(mt::split_number("three").has_value());
    CHECK_FALSE(mt::split_number("3/").has_value());
    CHECK_FALSE(mt::split_number("").has_value());
}

TEST_CASE("Writing and reading back works for every fixture family", "[metadata-tags]") {
    for (const std::string name : {"a.mp3", "a.m4a", "a.flac", "a.ogg"}) {
        INFO(name);
        const std::string path = scratch_copy(name);
        const auto result = mt::write_tags(
            path, {{"title", "Probe Title"}, {"artist", "Probe Artist"}, {"track", "3/12"}, {"narrator", "Someone"}});
        INFO(result.error);
        REQUIRE(result.ok);
        CHECK(mt::read_tag(path, "title") == std::optional<std::string>("Probe Title"));
        CHECK(mt::read_tag(path, "artist") == std::optional<std::string>("Probe Artist"));
        CHECK(mt::read_tag(path, "track") == std::optional<std::string>("3/12"));
        CHECK(mt::read_tag(path, "narrator") == std::optional<std::string>("Someone"));
        fs::remove_all(fs::path(path).parent_path());
    }
}

TEST_CASE("An empty value removes a tag and leaves the others alone", "[metadata-tags]") {
    for (const std::string name : {"a.mp3", "a.m4a", "a.flac", "a.ogg"}) {
        INFO(name);
        const std::string path = scratch_copy(name);
        REQUIRE(mt::write_tags(path, {{"title", "Keep"}, {"artist", "Drop"}}).ok);
        REQUIRE(mt::write_tags(path, {{"artist", ""}}).ok);
        CHECK(mt::read_tag(path, "title") == std::optional<std::string>("Keep"));
        CHECK_FALSE(mt::read_tag(path, "artist").has_value());
        fs::remove_all(fs::path(path).parent_path());
    }
}

TEST_CASE("Writing to an unsupported file reports an error and changes nothing", "[metadata-tags]") {
    const fs::path dir = fs::temp_directory_path() / ("mm-tags-unsupported-" + unique_suffix());
    fs::create_directories(dir);
    const std::string path = (dir / "notes.txt").string();
    { std::ofstream(path) << "hello"; }
    const auto result = mt::write_tags(path, {{"title", "x"}});
    CHECK_FALSE(result.ok);
    CHECK_FALSE(result.error.empty());
    fs::remove_all(dir);
}

TEST_CASE("A bad track number is refused before the file is touched", "[metadata-tags]") {
    const std::string path = scratch_copy("a.mp3");
    const auto result = mt::write_tags(path, {{"track", "three"}});
    CHECK_FALSE(result.ok);
    CHECK_FALSE(mt::read_tag(path, "track").has_value());
    fs::remove_all(fs::path(path).parent_path());
}
