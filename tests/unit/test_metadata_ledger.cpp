#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <random>
#include <string>

#include "MetadataLedger.hpp"

namespace ml = MetadataLedger;
namespace fs = std::filesystem;

namespace {

std::string unique_suffix() {
    static std::random_device device;
    static unsigned counter = 0;
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::to_string(device()) + "-" + std::to_string(ticks) + "-" + std::to_string(++counter);
}

std::string scratch_dir(const std::string& tag) {
    const fs::path dir = fs::temp_directory_path() / ("mm-ledger-" + tag + "-" + unique_suffix());
    fs::create_directories(dir);
    return dir.string();
}

bool has(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

ml::ApplyRecord sample() {
    ml::ApplyRecord record;
    record.plan = "/plans/p.json";
    record.ok = 3;
    record.already_done = 1;
    record.folders_written = 2;
    record.failures = {{"/a/bad.mp3", "file gone"}};
    record.dropped = {"song.FLAC: genre", "other.m4a: isrc"};
    return record;
}

std::string drop_line(const std::string& ext, const std::string& key) {
    return R"({"at":"t","event":"apply","drops":[{"ext":")" + ext + R"(","key":")" + key + R"("}]})" + "\n";
}

}  // namespace

// ---------------------------------------------------------------- lines

TEST_CASE("The default ledger sits in a hidden folder under the base", "[ledger]") {
    CHECK(ml::default_path("/work") == "/work/.metadata-maestro/ledger.jsonl");
    CHECK(ml::default_path("/work/") == "/work/.metadata-maestro/ledger.jsonl");
}

TEST_CASE("An apply line is one line of JSON that names the event and the time", "[ledger]") {
    const std::string line = ml::apply_line("2026-01-02T03:04:05", sample());
    CHECK(line.find('\n') == std::string::npos);
    CHECK(has(line, "\"at\":\"2026-01-02T03:04:05\""));
    CHECK(has(line, "\"event\":\"apply\""));
    CHECK(has(line, "\"plan\":\"/plans/p.json\""));
}

TEST_CASE("An apply line keeps the summary mm.py wrote", "[ledger]") {
    const std::string line = ml::apply_line("t", sample());
    CHECK(has(line, "\"ok\":3"));
    CHECK(has(line, "\"already_done\":1"));
    CHECK(has(line, "\"folders_written\":2"));
    CHECK(has(line, "\"failed\":1"));
    CHECK(has(line, "\"dropped\":[\"song.FLAC: genre\",\"other.m4a: isrc\"]"));
}

TEST_CASE("Drops carry a lower-case extension and the key", "[ledger]") {
    const std::string line = ml::apply_line("t", sample());
    CHECK(has(line, R"({"ext":".flac","key":"genre"})"));
    CHECK(has(line, R"({"ext":".m4a","key":"isrc"})"));
}

TEST_CASE("Failures are listed with file and error", "[ledger]") {
    const std::string line = ml::apply_line("t", sample());
    CHECK(has(line, R"("failed":[{"error":"file gone","file":"/a/bad.mp3"}])"));
}

TEST_CASE("A drop line that cannot be split is kept in the summary but not counted as a drop", "[ledger]") {
    ml::ApplyRecord record;
    record.dropped = {"no separator here", "noext: title", ": key", "x.mp3: "};
    const std::string line = ml::apply_line("t", record);
    CHECK(has(line, "\"dropped\":[\"no separator here\",\"noext: title\",\": key\",\"x.mp3: \"]"));
    CHECK(has(line, "\"drops\":[]"));
}

TEST_CASE("A name that contains a colon still splits on the last separator", "[ledger]") {
    ml::ApplyRecord record;
    record.dropped = {"a: b.mp3: title"};
    CHECK(has(ml::apply_line("t", record), R"({"ext":".mp3","key":"title"})"));
}

TEST_CASE("Text that needs escaping stays on one line", "[ledger]") {
    ml::ApplyRecord record;
    record.plan = "/plans/\"quoted\"\\path\n.json";
    record.failures = {{"a\tb", "line1\nline2"}};
    const std::string line = ml::apply_line("t", record);
    CHECK(line.find('\n') == std::string::npos);
    CHECK(line.find('\t') == std::string::npos);
}

TEST_CASE("Non-ASCII text is written as it is", "[ledger]") {
    ml::ApplyRecord record;
    record.plan = "/plans/Caf\xC3\xA9.json";
    const std::string line = ml::apply_line("t", record);
    CHECK(has(line, "Caf\xC3\xA9"));
    CHECK_FALSE(has(line, "\\u"));
}

TEST_CASE("A restore line lists the files", "[ledger]") {
    const std::string line = ml::restore_line("t", {"/a/one.mp3", "/a/two.mp3"});
    CHECK(has(line, "\"event\":\"restore\""));
    CHECK(has(line, "\"files\":[\"/a/one.mp3\",\"/a/two.mp3\"]"));
    CHECK(has(ml::restore_line("t", {}), "\"files\":[]"));
}

TEST_CASE("A verdict line has sorted, unique keys and the note", "[ledger]") {
    const std::string line = ml::verdict_line("t", {"/p.json", "accept", {"title", "album", "title"}, "looked right"});
    CHECK(has(line, "\"event\":\"verdict\""));
    CHECK(has(line, "\"verdict\":\"accept\""));
    CHECK(has(line, "\"keys\":[\"album\",\"title\"]"));
    CHECK(has(line, "\"note\":\"looked right\""));
}

TEST_CASE("Only accept and reject are verdicts", "[ledger]") {
    CHECK_FALSE(ml::verdict_line("t", {"/p.json", "reject", {}, ""}).empty());
    for (const char* bad : {"", "maybe", "Accept", "accept "}) {
        INFO(bad);
        CHECK(ml::verdict_line("t", {"/p.json", bad, {}, ""}).empty());
    }
}

// ---------------------------------------------------------------- files

TEST_CASE("Appending makes the folder and adds one line each time", "[ledger]") {
    const std::string dir = scratch_dir("append");
    const std::string path = ml::default_path(dir);
    CHECK(ml::append_line(path, "{\"a\":1}").empty());
    CHECK(ml::append_line(path, "{\"a\":2}").empty());
    CHECK(ml::read_ledger(path) == "{\"a\":1}\n{\"a\":2}\n");
    fs::remove_all(dir);
}

TEST_CASE("A ledger that is not there reads as empty", "[ledger]") {
    CHECK(ml::read_ledger("/no/such/ledger.jsonl").empty());
}

TEST_CASE("An empty line is not written", "[ledger]") {
    const std::string dir = scratch_dir("emptyline");
    const std::string path = dir + "/l.jsonl";
    CHECK(ml::append_line(path, "").empty());
    CHECK_FALSE(fs::exists(path));
    fs::remove_all(dir);
}

TEST_CASE("A ledger that cannot be written is reported", "[ledger]") {
    const std::string dir = scratch_dir("cantwrite");
    fs::create_directory(dir + "/l.jsonl");
    CHECK(ml::append_line(dir + "/l.jsonl", "{}") == "could not write the ledger");
    fs::remove_all(dir);
}

// ---------------------------------------------------------------- lessons

TEST_CASE("An empty ledger has no lessons", "[ledger][lessons]") {
    const auto lessons = ml::lessons("", "/x/ledger.jsonl");
    CHECK(lessons.ledger == "/x/ledger.jsonl");
    CHECK(lessons.entries == 0);
    CHECK(lessons.container_drops.empty());
    CHECK(lessons.verdicts.empty());
    CHECK(lessons.restores == 0);
    CHECK(lessons.propose_edit_for.empty());
    CHECK(lessons.recent_notes.empty());
}

TEST_CASE("Drops are counted by container and key, and three make a proposal", "[ledger][lessons]") {
    std::string text;
    for (int i = 0; i < 3; ++i) {
        text += drop_line(".flac", "genre");
    }
    text += drop_line(".flac", "isrc");
    text += drop_line(".flac", "isrc");
    const auto lessons = ml::lessons(text);
    CHECK(lessons.entries == 5);
    CHECK(lessons.container_drops.at(".flac:genre") == 3);
    CHECK(lessons.container_drops.at(".flac:isrc") == 2);
    CHECK(lessons.propose_edit_for == std::vector<std::string>{".flac:genre"});
}

TEST_CASE("Drops from a real apply line round-trip into the lessons", "[ledger][lessons]") {
    std::string text;
    for (int i = 0; i < 3; ++i) {
        text += ml::apply_line("t", sample()) + "\n";
    }
    const auto lessons = ml::lessons(text);
    CHECK(lessons.container_drops.at(".flac:genre") == 3);
    CHECK(lessons.container_drops.at(".m4a:isrc") == 3);
    CHECK(lessons.propose_edit_for == std::vector<std::string>{".flac:genre", ".m4a:isrc"});
}

TEST_CASE("Verdicts are counted per key and verdict, and notes are kept", "[ledger][lessons]") {
    std::string text = ml::verdict_line("t", {"/p", "accept", {"title", "album"}, "fine"}) + "\n";
    text += ml::verdict_line("t", {"/p", "reject", {"title"}, ""}) + "\n";
    text += ml::verdict_line("t", {"/p", "accept", {"title"}, "again"}) + "\n";
    const auto lessons = ml::lessons(text);
    CHECK(lessons.verdicts.at("title:accept") == 2);
    CHECK(lessons.verdicts.at("title:reject") == 1);
    CHECK(lessons.verdicts.at("album:accept") == 1);
    CHECK(lessons.recent_notes == std::vector<std::string>{"fine", "again"});
}

TEST_CASE("Only the last ten notes are kept, oldest first", "[ledger][lessons]") {
    std::string text;
    for (int i = 1; i <= 13; ++i) {
        text += ml::verdict_line("t", {"/p", "accept", {"k"}, "note " + std::to_string(i)}) + "\n";
    }
    const auto lessons = ml::lessons(text);
    REQUIRE(lessons.recent_notes.size() == 10);
    CHECK(lessons.recent_notes.front() == "note 4");
    CHECK(lessons.recent_notes.back() == "note 13");
}

TEST_CASE("Restores add up the files, not the entries", "[ledger][lessons]") {
    std::string text = ml::restore_line("t", {"a", "b", "c"}) + "\n" + ml::restore_line("t", {"d"}) + "\n";
    const auto lessons = ml::lessons(text);
    CHECK(lessons.restores == 4);
    CHECK(lessons.entries == 2);
}

TEST_CASE("Blank lines, junk and non-objects are not entries", "[ledger][lessons]") {
    const std::string text =
        "\n   \nnot json\n[1,2]\n\"string\"\n42\nnull\n{\"event\":\"restore\",\"files\":[\"a\"]}\n";
    const auto lessons = ml::lessons(text);
    CHECK(lessons.entries == 1);
    CHECK(lessons.restores == 1);
}

TEST_CASE("Windows line endings read the same", "[ledger][lessons]") {
    const auto lessons =
        ml::lessons("{\"event\":\"restore\",\"files\":[\"a\"]}\r\n{\"event\":\"restore\",\"files\":[\"b\"]}\r\n");
    CHECK(lessons.entries == 2);
    CHECK(lessons.restores == 2);
}

TEST_CASE("A last line with no newline still counts", "[ledger][lessons]") {
    CHECK(ml::lessons("{\"event\":\"restore\",\"files\":[\"a\"]}").entries == 1);
}

TEST_CASE("An entry missing fields only loses those fields", "[ledger][lessons]") {
    const std::string text =
        "{\"event\":\"verdict\",\"keys\":[\"title\"]}\n"            // no verdict
        "{\"event\":\"verdict\",\"verdict\":\"accept\"}\n"          // no keys
        "{\"event\":\"verdict\",\"verdict\":7,\"keys\":[\"t\"]}\n"  // verdict not text
        "{\"event\":\"apply\",\"drops\":[{\"ext\":\".mp3\"},{\"key\":\"x\"},5,{\"ext\":\".mp3\",\"key\":\"ok\"}]}\n"
        "{\"event\":\"apply\"}\n"
        "{\"event\":\"restore\"}\n"
        "{\"event\":\"restore\",\"files\":\"not a list\"}\n"
        "{\"event\":\"mystery\"}\n"
        "{\"no_event\":true}\n";
    const auto lessons = ml::lessons(text);
    CHECK(lessons.entries == 9);
    CHECK(lessons.verdicts.empty());
    CHECK(lessons.container_drops.size() == 1);
    CHECK(lessons.container_drops.at(".mp3:ok") == 1);
    CHECK(lessons.restores == 0);
}

TEST_CASE("Keys that are not text are ignored when counting verdicts", "[ledger][lessons]") {
    const auto lessons = ml::lessons("{\"event\":\"verdict\",\"verdict\":\"accept\",\"keys\":[\"a\",1,null,\"b\"]}\n");
    CHECK(lessons.verdicts.size() == 2);
    CHECK(lessons.verdicts.count("a:accept") == 1);
}

TEST_CASE("A note that is empty or not text is not kept", "[ledger][lessons]") {
    const auto lessons = ml::lessons(
        "{\"event\":\"verdict\",\"verdict\":\"accept\",\"keys\":[],\"note\":\"\"}\n"
        "{\"event\":\"verdict\",\"verdict\":\"accept\",\"keys\":[],\"note\":5}\n"
        "{\"event\":\"verdict\",\"verdict\":\"accept\",\"keys\":[],\"note\":\"kept\"}\n");
    CHECK(lessons.recent_notes == std::vector<std::string>{"kept"});
}

TEST_CASE("Lessons are the same whether the ledger came from a file or from text", "[ledger][lessons]") {
    const std::string dir = scratch_dir("fromfile");
    const std::string path = ml::default_path(dir);
    REQUIRE(ml::append_line(path, ml::apply_line("t", sample())).empty());
    REQUIRE(ml::append_line(path, ml::restore_line("t", {"x"})).empty());
    const auto lessons = ml::lessons(ml::read_ledger(path), path);
    CHECK(lessons.entries == 2);
    CHECK(lessons.restores == 1);
    CHECK(lessons.ledger == path);
    fs::remove_all(dir);
}

TEST_CASE("Eleven notes keep ten", "[ledger][lessons]") {
    std::string text;
    for (int i = 1; i <= 11; ++i) {
        text += ml::verdict_line("t", {"/p", "accept", {"k"}, "note " + std::to_string(i)}) + "\n";
    }
    const auto lessons = ml::lessons(text);
    REQUIRE(lessons.recent_notes.size() == 10);
    CHECK(lessons.recent_notes.front() == "note 2");
}

TEST_CASE("Only restore entries count restored files", "[ledger][lessons]") {
    const auto lessons = ml::lessons(
        "{\"event\":\"apply\",\"files\":[\"x\",\"y\"]}\n{\"event\":\"verdict\",\"files\":[\"z\"]}\n{\"event\":"
        "\"mystery\",\"files\":[\"q\"]}\n");
    CHECK(lessons.entries == 3);
    CHECK(lessons.restores == 0);
}

TEST_CASE("A line is only an entry when it is strict JSON", "[ledger][lessons]") {
    const std::string text =
        "{\"event\":\"restore\",\"files\":[\"a\"]} trailing\n"
        "{'event':'restore','files':['a']}\n"
        "{\"event\":\"restore\",\"files\":[\"a\"],}\n"
        "{\"event\":\"restore\",\"files\":[NaN]}\n";
    CHECK(ml::lessons(text).entries == 0);
}
