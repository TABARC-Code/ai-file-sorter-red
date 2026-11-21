#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>

#include "MetadataApply.hpp"
#include "MetadataFolder.hpp"

namespace mf = MetadataFolder;
namespace fs = std::filesystem;

namespace {

std::string unique_suffix() {
    static std::random_device device;
    static unsigned counter = 0;
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::to_string(device()) + "-" + std::to_string(ticks) + "-" + std::to_string(++counter);
}

std::string scratch_dir(const std::string& tag) {
    const fs::path dir = fs::temp_directory_path() / ("mm-folder-" + tag + "-" + unique_suffix());
    fs::create_directories(dir);
    return dir.string();
}

void spit(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
}

std::string slurp(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool has(const std::string& dir, const std::string& name) {
    return fs::exists(fs::path(dir) / name);
}

const std::string kStamp = "2026-01-02T03:04:05";
const std::string kTool = "test-tool 1.0";

mf::Change text_change(const std::string& target, const std::optional<std::string>& from, const std::string& to) {
    return {target, "", from, to};
}

mf::Change manifest_change(const std::string& key, const std::optional<std::string>& from, const std::string& to) {
    return {mf::kManifest, key, from, to};
}

}  // namespace

// ---------------------------------------------------------------- reading

TEST_CASE("An empty folder reads as nothing at all", "[folder]") {
    const std::string dir = scratch_dir("empty");
    const auto state = mf::read_folder(dir);
    CHECK_FALSE(state.reader.has_value());
    CHECK_FALSE(state.desc.has_value());
    CHECK(state.cover_files.empty());
    CHECK(state.manifest.empty());
    CHECK(state.manifest_error.empty());
    fs::remove_all(dir);
}

TEST_CASE("A folder that does not exist reads as empty, not as a crash", "[folder]") {
    const auto state = mf::read_folder("/no/such/folder/anywhere");
    CHECK_FALSE(state.reader.has_value());
    CHECK(state.cover_files.empty());
}

TEST_CASE("reader.txt and desc.txt are trimmed when read", "[folder]") {
    const std::string dir = scratch_dir("trim");
    spit(dir + "/reader.txt", "  Jane Reader \r\n\n");
    spit(dir + "/desc.txt", "\tA long summary.\n");
    const auto state = mf::read_folder(dir);
    CHECK(state.reader == std::optional<std::string>("Jane Reader"));
    CHECK(state.desc == std::optional<std::string>("A long summary."));
    fs::remove_all(dir);
}

TEST_CASE("An empty sidecar file is present but empty, which is not the same as missing", "[folder]") {
    const std::string dir = scratch_dir("blankfile");
    spit(dir + "/reader.txt", "  \n");
    const auto state = mf::read_folder(dir);
    REQUIRE(state.reader.has_value());
    CHECK(state.reader->empty());
    CHECK_FALSE(state.desc.has_value());
    fs::remove_all(dir);
}

TEST_CASE("Cover files are found by name, any case, sorted", "[folder]") {
    const std::string dir = scratch_dir("covers");
    for (const char* name : {"Folder.PNG", "cover.jpg", "cover.jpeg", "cover.gif", "mycover.jpg", "cover.jpg.bak",
                             "folder.png", "Cover.JPEG"}) {
        spit(dir + "/" + name, "x");
    }
    const auto state = mf::read_folder(dir);
    const std::vector<std::string> want = {"Cover.JPEG", "Folder.PNG", "cover.jpeg", "cover.jpg", "folder.png"};
    CHECK(state.cover_files == want);
    fs::remove_all(dir);
}

TEST_CASE("A manifest is read into text values, strings flagged as strings", "[folder]") {
    const std::string dir = scratch_dir("manifest");
    spit(dir + "/metadata-maestro.json",
         R"({"dewey":"813.54","year":2024,"tags":["a","b"],"flag":true,"nothing":null})");
    const auto state = mf::read_folder(dir);
    REQUIRE(state.manifest_error.empty());
    REQUIRE(state.manifest.size() == 5);
    CHECK(state.manifest.at("dewey").text == "813.54");
    CHECK(state.manifest.at("dewey").is_string);
    CHECK(state.manifest.at("year").text == "2024");
    CHECK_FALSE(state.manifest.at("year").is_string);
    CHECK(state.manifest.at("tags").text == "[\"a\",\"b\"]");
    CHECK_FALSE(state.manifest.at("tags").is_string);
    CHECK(state.manifest.at("flag").text == "true");
    CHECK(state.manifest.at("nothing").text == "null");
    CHECK_FALSE(state.manifest.at("nothing").is_string);
    fs::remove_all(dir);
}

TEST_CASE("A manifest that will not parse is reported, not thrown", "[folder]") {
    const std::string dir = scratch_dir("badjson");
    spit(dir + "/metadata-maestro.json", "{ not json");
    const auto state = mf::read_folder(dir);
    CHECK(state.manifest_error == "unreadable metadata-maestro.json");
    CHECK(state.manifest.empty());
    fs::remove_all(dir);
}

TEST_CASE("A manifest that is JSON but not an object is also unreadable", "[folder]") {
    const std::string dir = scratch_dir("arrayjson");
    spit(dir + "/metadata-maestro.json", "[1, 2, 3]");
    CHECK(mf::read_folder(dir).manifest_error == "unreadable metadata-maestro.json");
    spit(dir + "/metadata-maestro.json", "\"just a string\"");
    CHECK(mf::read_folder(dir).manifest_error == "unreadable metadata-maestro.json");
    fs::remove_all(dir);
}

TEST_CASE("A blank manifest, or a bare null, counts as no manifest", "[folder]") {
    const std::string dir = scratch_dir("blankjson");
    spit(dir + "/metadata-maestro.json", "  \n");
    auto state = mf::read_folder(dir);
    CHECK(state.manifest_error.empty());
    CHECK(state.manifest.empty());
    spit(dir + "/metadata-maestro.json", "null");
    state = mf::read_folder(dir);
    CHECK(state.manifest_error.empty());
    CHECK(state.manifest.empty());
    fs::remove_all(dir);
}

TEST_CASE("The state remembers which folder it came from", "[folder]") {
    const std::string dir = scratch_dir("dirname");
    CHECK(mf::read_folder(dir).dir == dir);
    fs::remove_all(dir);
}

// ---------------------------------------------------------------- sidecar planning

TEST_CASE("One planned value becomes the sidecar", "[folder][plan]") {
    const auto plan =
        mf::plan_sidecar("Book", "reader.txt", "narrator", {"Jane Reader", "Jane Reader"}, {}, std::nullopt);
    REQUIRE(plan.change.has_value());
    CHECK(plan.warning.empty());
    CHECK(plan.change->target == "reader.txt");
    CHECK(plan.change->key.empty());
    CHECK_FALSE(plan.change->from.has_value());
    CHECK(plan.change->to == "Jane Reader");
}

TEST_CASE("The change remembers what the file held before", "[folder][plan]") {
    const auto plan = mf::plan_sidecar("Book", "desc.txt", "description", {"New text"}, {}, std::string("Old text"));
    REQUIRE(plan.change.has_value());
    CHECK(plan.change->from == std::optional<std::string>("Old text"));
    CHECK(plan.change->to == "New text");
}

TEST_CASE("Several planned values are a warning and no change", "[folder][plan]") {
    const auto plan = mf::plan_sidecar("Book", "reader.txt", "narrator", {"Jane", "John"}, {"Jim"}, std::nullopt);
    CHECK_FALSE(plan.change.has_value());
    CHECK(plan.warning == "Book/reader.txt skipped: files disagree on planned narrator");
}

TEST_CASE("Planned values win over what the tags say now", "[folder][plan]") {
    const auto plan = mf::plan_sidecar("Book", "reader.txt", "narrator", {"Jane"}, {"John", "Jim"}, std::nullopt);
    REQUIRE(plan.change.has_value());
    CHECK(plan.change->to == "Jane");
}

TEST_CASE("With nothing planned, one existing tag value is used", "[folder][plan]") {
    const auto plan = mf::plan_sidecar("Book", "reader.txt", "narrator", {}, {"Jane", "Jane"}, std::nullopt);
    REQUIRE(plan.change.has_value());
    CHECK(plan.change->to == "Jane");
}

TEST_CASE("Empty existing values do not count towards agreement", "[folder][plan]") {
    const auto plan = mf::plan_sidecar("Book", "reader.txt", "narrator", {}, {"", "Jane", ""}, std::nullopt);
    REQUIRE(plan.change.has_value());
    CHECK(plan.change->to == "Jane");
}

TEST_CASE("Existing values that disagree are a warning", "[folder][plan]") {
    const auto plan = mf::plan_sidecar("Book", "desc.txt", "description", {}, {"One", "Two"}, std::nullopt);
    CHECK_FALSE(plan.change.has_value());
    CHECK(plan.warning == "Book/desc.txt skipped: files disagree on description");
}

TEST_CASE("No planned and no existing value is a different warning", "[folder][plan]") {
    const auto none = mf::plan_sidecar("Book", "desc.txt", "description", {}, {}, std::nullopt);
    CHECK_FALSE(none.change.has_value());
    CHECK(none.warning == "Book/desc.txt skipped: no description");
    const auto blanks = mf::plan_sidecar("Book", "desc.txt", "description", {}, {"", ""}, std::nullopt);
    CHECK(blanks.warning == "Book/desc.txt skipped: no description");
}

TEST_CASE("A sidecar that already says the same thing is left alone", "[folder][plan]") {
    const auto plan = mf::plan_sidecar("Book", "reader.txt", "narrator", {"Jane"}, {}, std::string("Jane"));
    CHECK_FALSE(plan.change.has_value());
    CHECK(plan.warning.empty());
}

TEST_CASE("Stray whitespace in a tag does not make a sidecar look out of date forever", "[folder][plan]") {
    const auto same = mf::plan_sidecar("Book", "reader.txt", "narrator", {"Jane \n"}, {}, std::string("Jane"));
    CHECK_FALSE(same.change.has_value());
    const auto fresh = mf::plan_sidecar("Book", "reader.txt", "narrator", {"  Jane "}, {}, std::nullopt);
    REQUIRE(fresh.change.has_value());
    CHECK(fresh.change->to == "Jane");
}

TEST_CASE("A planned blank clears an existing sidecar", "[folder][plan]") {
    const auto plan = mf::plan_sidecar("Book", "reader.txt", "narrator", {""}, {}, std::string("Jane"));
    REQUIRE(plan.change.has_value());
    CHECK(plan.change->to.empty());
    CHECK(plan.change->from == std::optional<std::string>("Jane"));
}

TEST_CASE("A planned blank with no file to clear is not a change", "[folder][plan]") {
    const auto plan = mf::plan_sidecar("Book", "reader.txt", "narrator", {""}, {}, std::nullopt);
    CHECK_FALSE(plan.change.has_value());
    CHECK(plan.warning.empty());
}

TEST_CASE("A planned blank over an empty file is not a change either", "[folder][plan]") {
    const auto plan = mf::plan_sidecar("Book", "reader.txt", "narrator", {""}, {}, std::string(""));
    CHECK_FALSE(plan.change.has_value());
}

// ---------------------------------------------------------------- manifest planning

TEST_CASE("New manifest keys become changes", "[folder][plan]") {
    mf::State state;
    state.dir = "/books/a";
    const auto plan = mf::plan_manifest(state, {{"dewey", "813.54"}, {"series", "Dune"}});
    REQUIRE(plan.error.empty());
    REQUIRE(plan.changes.size() == 2);
    CHECK(plan.changes[0].target == mf::kManifest);
    CHECK(plan.changes[0].key == "dewey");
    CHECK_FALSE(plan.changes[0].from.has_value());
    CHECK(plan.changes[0].to == "813.54");
    CHECK(plan.changes[1].key == "series");
}

TEST_CASE("A manifest value that already matches is skipped", "[folder][plan]") {
    mf::State state;
    state.manifest["dewey"] = {"813.54", true};
    const auto plan = mf::plan_manifest(state, {{"dewey", "813.54"}, {"series", "Dune"}});
    REQUIRE(plan.changes.size() == 1);
    CHECK(plan.changes[0].key == "series");
}

TEST_CASE("A different value records the old one", "[folder][plan]") {
    mf::State state;
    state.manifest["dewey"] = {"813", true};
    const auto plan = mf::plan_manifest(state, {{"dewey", "813.54"}});
    REQUIRE(plan.changes.size() == 1);
    CHECK(plan.changes[0].from == std::optional<std::string>("813"));
}

TEST_CASE("A number in the manifest is not equal to the same digits as a string", "[folder][plan]") {
    mf::State state;
    state.manifest["year"] = {"2024", false};
    const auto plan = mf::plan_manifest(state, {{"year", "2024"}});
    REQUIRE(plan.changes.size() == 1);
    CHECK(plan.changes[0].from == std::optional<std::string>("2024"));
}

TEST_CASE("Clearing a key that is not there is not a change", "[folder][plan]") {
    mf::State state;
    const auto plan = mf::plan_manifest(state, {{"dewey", ""}});
    CHECK(plan.changes.empty());
}

TEST_CASE("Clearing a key that is there is a change", "[folder][plan]") {
    mf::State state;
    state.manifest["dewey"] = {"813", true};
    const auto plan = mf::plan_manifest(state, {{"dewey", ""}});
    REQUIRE(plan.changes.size() == 1);
    CHECK(plan.changes[0].to.empty());
    CHECK(plan.changes[0].from == std::optional<std::string>("813"));
}

TEST_CASE("An unreadable manifest stops manifest planning, naming the folder", "[folder][plan]") {
    mf::State state;
    state.dir = "/books/a";
    state.manifest_error = "unreadable metadata-maestro.json";
    const auto plan = mf::plan_manifest(state, {{"dewey", "813"}});
    CHECK(plan.error == "unreadable metadata-maestro.json in /books/a");
    CHECK(plan.changes.empty());
}

TEST_CASE("An unreadable manifest is only an error when there is something to set", "[folder][plan]") {
    mf::State state;
    state.manifest_error = "unreadable metadata-maestro.json";
    const auto plan = mf::plan_manifest(state, {});
    CHECK(plan.error.empty());
    CHECK(plan.changes.empty());
}

// ---------------------------------------------------------------- applying

TEST_CASE("A new sidecar is written with a trailing newline and a created marker", "[folder][apply]") {
    const std::string dir = scratch_dir("newside");
    const auto result =
        mf::apply_folder(dir, {text_change("reader.txt", std::nullopt, "Jane Reader")}, false, kStamp, kTool);
    REQUIRE(result.ok);
    CHECK(result.error.empty());
    CHECK(result.written == std::vector<std::string>{"reader.txt"});
    CHECK(slurp(dir + "/reader.txt") == "Jane Reader\n");
    CHECK(has(dir, "reader.txt.mm-new"));
    CHECK(slurp(dir + "/reader.txt.mm-new").empty());
    CHECK_FALSE(has(dir, "reader.txt.mm-bak"));
    fs::remove_all(dir);
}

TEST_CASE("Text that already ends in a newline is not given a second one", "[folder][apply]") {
    const std::string dir = scratch_dir("newline");
    REQUIRE(
        mf::apply_folder(dir, {text_change("desc.txt", std::nullopt, "Line one\nLine two\n")}, true, kStamp, kTool).ok);
    CHECK(slurp(dir + "/desc.txt") == "Line one\nLine two\n");
    fs::remove_all(dir);
}

TEST_CASE("Overwriting a sidecar keeps the old one as a backup", "[folder][apply]") {
    const std::string dir = scratch_dir("overwrite");
    spit(dir + "/desc.txt", "Old text\n");
    REQUIRE(
        mf::apply_folder(dir, {text_change("desc.txt", std::string("Old text"), "New text")}, false, kStamp, kTool).ok);
    CHECK(slurp(dir + "/desc.txt") == "New text\n");
    CHECK(slurp(dir + "/desc.txt.mm-bak") == "Old text\n");
    CHECK_FALSE(has(dir, "desc.txt.mm-new"));
    fs::remove_all(dir);
}

TEST_CASE("With backups off nothing extra is left behind", "[folder][apply]") {
    const std::string dir = scratch_dir("nobak");
    spit(dir + "/desc.txt", "Old\n");
    REQUIRE(mf::apply_folder(dir, {text_change("desc.txt", std::string("Old"), "New")}, true, kStamp, kTool).ok);
    CHECK(slurp(dir + "/desc.txt") == "New\n");
    CHECK_FALSE(has(dir, "desc.txt.mm-bak"));
    CHECK_FALSE(has(dir, "desc.txt.mm-new"));
    fs::remove_all(dir);
}

TEST_CASE("Empty text deletes the sidecar but keeps a way back", "[folder][apply]") {
    const std::string dir = scratch_dir("delete");
    spit(dir + "/reader.txt", "Jane\n");
    const auto result =
        mf::apply_folder(dir, {text_change("reader.txt", std::string("Jane"), "")}, false, kStamp, kTool);
    REQUIRE(result.ok);
    CHECK(result.written == std::vector<std::string>{"reader.txt"});
    CHECK_FALSE(has(dir, "reader.txt"));
    CHECK(slurp(dir + "/reader.txt.mm-bak") == "Jane\n");
    fs::remove_all(dir);
}

TEST_CASE("Deleting a sidecar that was never there is harmless", "[folder][apply]") {
    const std::string dir = scratch_dir("deletemissing");
    REQUIRE(mf::apply_folder(dir, {text_change("reader.txt", std::nullopt, "")}, true, kStamp, kTool).ok);
    CHECK_FALSE(has(dir, "reader.txt"));
    fs::remove_all(dir);
}

TEST_CASE("An existing backup marker stops the whole folder before anything is written", "[folder][apply]") {
    const std::string dir = scratch_dir("marker");
    spit(dir + "/desc.txt", "Old\n");
    spit(dir + "/desc.txt.mm-bak", "Older\n");
    const auto result = mf::apply_folder(
        dir, {text_change("reader.txt", std::nullopt, "Jane"), text_change("desc.txt", std::string("Old"), "New")},
        false, kStamp, kTool);
    CHECK_FALSE(result.ok);
    CHECK(result.error == "backup exists for desc.txt; restore or clean first");
    CHECK(result.written.empty());
    CHECK_FALSE(has(dir, "reader.txt"));
    CHECK_FALSE(has(dir, "reader.txt.mm-new"));
    CHECK(slurp(dir + "/desc.txt") == "Old\n");
    fs::remove_all(dir);
}

TEST_CASE("A leftover created marker also counts as a backup", "[folder][apply]") {
    const std::string dir = scratch_dir("newmarker");
    spit(dir + "/reader.txt.mm-new", "");
    const auto result = mf::apply_folder(dir, {text_change("reader.txt", std::nullopt, "Jane")}, false, kStamp, kTool);
    CHECK_FALSE(result.ok);
    CHECK(result.error == "backup exists for reader.txt; restore or clean first");
    fs::remove_all(dir);
}

TEST_CASE("Markers are ignored when backups are off", "[folder][apply]") {
    const std::string dir = scratch_dir("markerignored");
    spit(dir + "/desc.txt.mm-bak", "Older\n");
    REQUIRE(mf::apply_folder(dir, {text_change("desc.txt", std::nullopt, "New")}, true, kStamp, kTool).ok);
    CHECK(slurp(dir + "/desc.txt") == "New\n");
    fs::remove_all(dir);
}

TEST_CASE("Manifest keys are merged into the file and stamped", "[folder][apply]") {
    const std::string dir = scratch_dir("merge");
    spit(dir + "/metadata-maestro.json", R"({"year":2024,"tags":["a","b"],"dewey":"813"})");
    const auto result = mf::apply_folder(
        dir, {manifest_change("dewey", std::string("813"), "813.54"), manifest_change("series", std::nullopt, "Dune")},
        false, kStamp, kTool);
    REQUIRE(result.ok);
    CHECK(result.written == std::vector<std::string>{"metadata-maestro.json"});
    const auto state = mf::read_folder(dir);
    REQUIRE(state.manifest_error.empty());
    CHECK(state.manifest.at("dewey").text == "813.54");
    CHECK(state.manifest.at("series").text == "Dune");
    CHECK(state.manifest.at("year").text == "2024");
    CHECK_FALSE(state.manifest.at("year").is_string);
    CHECK(state.manifest.at("tags").text == "[\"a\",\"b\"]");
    CHECK(state.manifest.at("_updated").text == kStamp);
    CHECK(state.manifest.at("_tool").text == kTool);
    CHECK(has(dir, "metadata-maestro.json.mm-bak"));
    fs::remove_all(dir);
}

TEST_CASE("A manifest is created when there was none", "[folder][apply]") {
    const std::string dir = scratch_dir("createman");
    REQUIRE(mf::apply_folder(dir, {manifest_change("dewey", std::nullopt, "813")}, false, kStamp, kTool).ok);
    const auto state = mf::read_folder(dir);
    CHECK(state.manifest.at("dewey").text == "813");
    CHECK(state.manifest.size() == 3);
    CHECK(has(dir, "metadata-maestro.json.mm-new"));
    fs::remove_all(dir);
}

TEST_CASE("The manifest is written as indented UTF-8 with a closing newline", "[folder][apply]") {
    const std::string dir = scratch_dir("manformat");
    REQUIRE(mf::apply_folder(dir, {manifest_change("title", std::nullopt, "Caf\xC3\xA9 \xE2\x80\x93 na\xC3\xAFve")},
                             true, kStamp, kTool)
                .ok);
    const std::string text = slurp(dir + "/metadata-maestro.json");
    CHECK(text.find("Caf\xC3\xA9 \xE2\x80\x93 na\xC3\xAFve") != std::string::npos);
    CHECK(text.find("\\u") == std::string::npos);
    CHECK(text.find("\n  \"title\": ") != std::string::npos);
    CHECK(text.back() == '\n');
    fs::remove_all(dir);
}

TEST_CASE("An empty manifest value removes the key", "[folder][apply]") {
    const std::string dir = scratch_dir("removekey");
    spit(dir + "/metadata-maestro.json", R"({"dewey":"813","series":"Dune"})");
    REQUIRE(mf::apply_folder(dir, {manifest_change("series", std::string("Dune"), "")}, true, kStamp, kTool).ok);
    const auto state = mf::read_folder(dir);
    CHECK(state.manifest.count("series") == 0);
    CHECK(state.manifest.at("dewey").text == "813");
    fs::remove_all(dir);
}

TEST_CASE("Removing a key that is not there still leaves a valid, stamped manifest", "[folder][apply]") {
    const std::string dir = scratch_dir("removemissing");
    REQUIRE(mf::apply_folder(dir, {manifest_change("series", std::nullopt, "")}, true, kStamp, kTool).ok);
    const auto state = mf::read_folder(dir);
    CHECK(state.manifest_error.empty());
    CHECK(state.manifest.at("_updated").text == kStamp);
    fs::remove_all(dir);
}

TEST_CASE("An unreadable manifest is not overwritten", "[folder][apply]") {
    const std::string dir = scratch_dir("badmerge");
    spit(dir + "/metadata-maestro.json", "{ broken");
    const auto result = mf::apply_folder(
        dir, {text_change("reader.txt", std::nullopt, "Jane"), manifest_change("dewey", std::nullopt, "813")}, false,
        kStamp, kTool);
    CHECK_FALSE(result.ok);
    CHECK(result.error == "unreadable metadata-maestro.json");
    CHECK(result.written.empty());
    CHECK(slurp(dir + "/metadata-maestro.json") == "{ broken");
    CHECK_FALSE(has(dir, "reader.txt"));
    fs::remove_all(dir);
}

TEST_CASE("Sidecars and manifest go in together, text files first", "[folder][apply]") {
    const std::string dir = scratch_dir("both");
    const auto result = mf::apply_folder(
        dir,
        {manifest_change("dewey", std::nullopt, "813"), text_change("reader.txt", std::nullopt, "Jane"),
         text_change("desc.txt", std::nullopt, "Summary")},
        true, kStamp, kTool);
    REQUIRE(result.ok);
    CHECK(result.written == std::vector<std::string>{"reader.txt", "desc.txt", "metadata-maestro.json"});
    fs::remove_all(dir);
}

TEST_CASE("No changes writes nothing and is not a failure", "[folder][apply]") {
    const std::string dir = scratch_dir("nothing");
    const auto result = mf::apply_folder(dir, {}, false, kStamp, kTool);
    CHECK(result.ok);
    CHECK(result.written.empty());
    CHECK(fs::is_empty(dir));
    fs::remove_all(dir);
}

TEST_CASE("A folder that is not there is an error", "[folder][apply]") {
    const auto result = mf::apply_folder("/no/such/folder/anywhere", {text_change("reader.txt", std::nullopt, "Jane")},
                                         true, kStamp, kTool);
    CHECK_FALSE(result.ok);
    CHECK(result.error == "folder gone");
}

TEST_CASE("Restore puts a folder back the way it was", "[folder][apply]") {
    const std::string dir = scratch_dir("restore");
    spit(dir + "/desc.txt", "Old\n");
    spit(dir + "/metadata-maestro.json", R"({"dewey":"813"})");
    const std::string manifest_before = slurp(dir + "/metadata-maestro.json");
    REQUIRE(mf::apply_folder(
                dir,
                {text_change("desc.txt", std::string("Old"), "New"), text_change("reader.txt", std::nullopt, "Jane"),
                 manifest_change("dewey", std::string("813"), "999")},
                false, kStamp, kTool)
                .ok);
    const auto restored = MetadataApply::restore({dir});
    CHECK(restored.error.empty());
    CHECK(slurp(dir + "/desc.txt") == "Old\n");
    CHECK(slurp(dir + "/metadata-maestro.json") == manifest_before);
    CHECK_FALSE(has(dir, "reader.txt"));
    CHECK(MetadataApply::find_marks({dir}).paths.empty());
    fs::remove_all(dir);
}

TEST_CASE("A target that is not a plain file name is refused before anything is written", "[folder][apply]") {
    const std::string dir = scratch_dir("badtarget");
    for (const char* bad : {"../escape.txt", "sub/reader.txt", "..", ".", "", "a\\b.txt"}) {
        const auto result = mf::apply_folder(
            dir, {text_change("desc.txt", std::nullopt, "fine"), text_change(bad, std::nullopt, "bad")}, true, kStamp,
            kTool);
        CHECK_FALSE(result.ok);
        CHECK(result.error == std::string("bad target name: ") + bad);
        CHECK(result.written.empty());
        CHECK_FALSE(has(dir, "desc.txt"));
    }
    fs::remove_all(dir);
}

TEST_CASE("Two changes to one sidecar write it once, last one wins", "[folder][apply]") {
    const std::string dir = scratch_dir("twice");
    const auto result =
        mf::apply_folder(dir,
                         {text_change("reader.txt", std::nullopt, "First"), text_change("desc.txt", std::nullopt, "D"),
                          text_change("reader.txt", std::nullopt, "Second")},
                         false, kStamp, kTool);
    REQUIRE(result.ok);
    CHECK(result.written == std::vector<std::string>{"reader.txt", "desc.txt"});
    CHECK(slurp(dir + "/reader.txt") == "Second\n");
    fs::remove_all(dir);
}

TEST_CASE("Manifest values this tool did not touch come back as they went in", "[folder][apply]") {
    const std::string dir = scratch_dir("passthrough");
    spit(dir + "/metadata-maestro.json",
         R"({"ratio":0.1,"big":1234567890123,"neg":-5,"nested":{"a":[1,2]},"half":1.5})");
    REQUIRE(mf::apply_folder(dir, {manifest_change("dewey", std::nullopt, "813")}, true, kStamp, kTool).ok);
    const auto state = mf::read_folder(dir);
    CHECK(state.manifest.at("ratio").text == "0.1");
    CHECK(state.manifest.at("half").text == "1.5");
    CHECK(state.manifest.at("big").text == "1234567890123");
    CHECK(state.manifest.at("neg").text == "-5");
    CHECK(state.manifest.at("nested").text == R"({"a":[1,2]})");
    fs::remove_all(dir);
}

TEST_CASE("Numbers are written with only as many digits as they need", "[folder][apply]") {
    const std::string dir = scratch_dir("digits");
    spit(dir + "/metadata-maestro.json", R"({"ratio":0.1,"half":1.5})");
    REQUIRE(mf::apply_folder(dir, {manifest_change("dewey", std::nullopt, "813")}, true, kStamp, kTool).ok);
    const std::string text = slurp(dir + "/metadata-maestro.json");
    CHECK(text.find("\"ratio\": 0.1\n") != std::string::npos);
    CHECK(text.find("\"half\": 1.5,") != std::string::npos);
    fs::remove_all(dir);
}

TEST_CASE("A number that needs all its digits keeps them", "[folder][apply]") {
    const std::string dir = scratch_dir("alldigits");
    spit(dir + "/metadata-maestro.json", R"({"sum":0.30000000000000004})");
    REQUIRE(mf::apply_folder(dir, {manifest_change("dewey", std::nullopt, "813")}, true, kStamp, kTool).ok);
    CHECK(mf::read_folder(dir).manifest.at("sum").text == "0.30000000000000004");
    fs::remove_all(dir);
}

TEST_CASE("A repeated manifest key is not an error; the last one counts", "[folder]") {
    const std::string dir = scratch_dir("dupkey");
    spit(dir + "/metadata-maestro.json", R"({"a":"first","a":"second"})");
    const auto state = mf::read_folder(dir);
    CHECK(state.manifest_error.empty());
    CHECK(state.manifest.at("a").text == "second");
    fs::remove_all(dir);
}

TEST_CASE("A leftover marker on the manifest alone stops the folder", "[folder][apply]") {
    const std::string dir = scratch_dir("manmarker");
    spit(dir + "/metadata-maestro.json.mm-bak", "{}");
    const auto result = mf::apply_folder(
        dir, {text_change("reader.txt", std::nullopt, "Jane"), manifest_change("dewey", std::nullopt, "1")}, false,
        kStamp, kTool);
    CHECK_FALSE(result.ok);
    CHECK(result.error == "backup exists for metadata-maestro.json; restore or clean first");
    CHECK_FALSE(has(dir, "reader.txt"));
    fs::remove_all(dir);
}

TEST_CASE("A sidecar that cannot be written is reported, with what was done so far", "[folder][apply]") {
    const std::string dir = scratch_dir("cantwrite");
    fs::create_directory(dir + "/desc.txt");
    const auto result = mf::apply_folder(
        dir, {text_change("reader.txt", std::nullopt, "Jane"), text_change("desc.txt", std::nullopt, "Text")}, true,
        kStamp, kTool);
    CHECK_FALSE(result.ok);
    CHECK(result.error == "could not write desc.txt");
    CHECK(result.written == std::vector<std::string>{"reader.txt"});
    fs::remove_all(dir);
}

TEST_CASE("A sidecar that cannot be backed up is reported before it is touched", "[folder][apply]") {
    const std::string dir = scratch_dir("cantbackup");
    fs::create_directory(dir + "/desc.txt");
    const auto result = mf::apply_folder(dir, {text_change("desc.txt", std::nullopt, "Text")}, false, kStamp, kTool);
    CHECK_FALSE(result.ok);
    CHECK(result.error == "could not make a backup of desc.txt");
    CHECK(fs::is_directory(dir + "/desc.txt"));
    fs::remove_all(dir);
}

TEST_CASE("A manifest that cannot be written is reported after the sidecars went in", "[folder][apply]") {
    const std::string dir = scratch_dir("cantwriteman");
    fs::create_directory(dir + "/metadata-maestro.json");
    const auto result = mf::apply_folder(
        dir, {text_change("reader.txt", std::nullopt, "Jane"), manifest_change("dewey", std::nullopt, "1")}, true,
        kStamp, kTool);
    CHECK_FALSE(result.ok);
    CHECK(result.error == "could not write metadata-maestro.json");
    CHECK(result.written == std::vector<std::string>{"reader.txt"});
    fs::remove_all(dir);
}

TEST_CASE("Only strict JSON counts as a readable manifest", "[folder]") {
    const std::string dir = scratch_dir("strict");
    for (const char* loose : {"{\"a\":1} trailing", "{\"a\":1 /* note */}", "{'a':1}", "{\"a\":1,}", "{\"a\":NaN}"}) {
        spit(dir + "/metadata-maestro.json", loose);
        CHECK(mf::read_folder(dir).manifest_error == "unreadable metadata-maestro.json");
    }
    fs::remove_all(dir);
}

TEST_CASE("The backup keeps the modification time of the file it copied", "[folder][apply]") {
    const std::string dir = scratch_dir("mtime");
    spit(dir + "/desc.txt", "Old\n");
    const auto old_time = fs::file_time_type::clock::now() - std::chrono::hours(24 * 400);
    fs::last_write_time(dir + "/desc.txt", old_time);
    const auto before = fs::last_write_time(dir + "/desc.txt");
    REQUIRE(mf::apply_folder(dir, {text_change("desc.txt", std::string("Old"), "New")}, false, kStamp, kTool).ok);
    CHECK(fs::last_write_time(dir + "/desc.txt.mm-bak") == before);
    fs::remove_all(dir);
}

TEST_CASE("Slashes inside strings are not comments", "[folder]") {
    const std::string dir = scratch_dir("slashes");
    spit(dir + "/metadata-maestro.json",
         R"({"url":"http://example.org/a","odd":"say \"hi\"/there","back":"a\\","end":"x"})");
    const auto state = mf::read_folder(dir);
    CHECK(state.manifest_error.empty());
    CHECK(state.manifest.at("url").text == "http://example.org/a");
    CHECK(state.manifest.at("odd").text == "say \"hi\"/there");
    CHECK(state.manifest.at("back").text == "a\\");
    fs::remove_all(dir);
}

TEST_CASE("A comment after a string that ends in a backslash is still caught", "[folder]") {
    const std::string dir = scratch_dir("slashafter");
    spit(dir + "/metadata-maestro.json", R"({"back":"a\\" /* c */})");
    CHECK(mf::read_folder(dir).manifest_error == "unreadable metadata-maestro.json");
    fs::remove_all(dir);
}
