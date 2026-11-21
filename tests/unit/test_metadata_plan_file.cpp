#include <catch2/catch_test_macros.hpp>
#include <string>

#include "MetadataPlanFile.hpp"

namespace pf = MetadataPlanFile;

namespace {

pf::Plan sample() {
    pf::Plan plan;
    plan.created = "2026-01-02T03:04:05+00:00";
    plan.source = "user";
    plan.summary = {10, 2, 1};
    plan.warnings = {"x.mp3: genre was dropped by .mp3 3x before"};
    pf::Entry mp3;
    mp3.file = "/books/a/01.mp3";
    mp3.family = "id3";
    mp3.type = "audio";
    mp3.fingerprint = {123456, 1700000000123456789LL};
    mp3.changes = {{"title", std::string("Old"), "New", "user"}, {"genre", std::nullopt, "Jazz", "user+csv"}};
    mp3.warnings = {"careful"};
    plan.entries.push_back(mp3);
    pf::Entry flac;
    flac.file = "/books/a/02.flac";
    flac.family = "vorbis";
    flac.fingerprint = {7, 8};
    plan.entries.push_back(flac);
    pf::Folder folder;
    folder.dir = "/books/a";
    folder.changes = {{"reader.txt", "", std::nullopt, "Jane", "user"},
                      {"metadata-maestro.json", "dewey", std::string("813"), "823", "user"}};
    plan.folders.push_back(folder);
    return plan;
}

bool has(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

const char* const kMinimal = R"({"version":"4.0.0","entries":[],"folders":[]})";

std::string with_entry(const std::string& entry) {
    return R"({"version":"4.0.0","entries":[)" + entry + R"(],"folders":[]})";
}

const char* const kGoodEntry =
    R"({"file":"/a.mp3","family":"id3","fingerprint":{"size":5,"mtime_ns":6},"changes":[{"key":"title","from":null,"to":"T","source":"s"}]})";

}  // namespace

// ---------------------------------------------------------------- writing

TEST_CASE("A plan names its tool and version", "[planfile]") {
    const std::string text = pf::serialise(pf::Plan{});
    CHECK(has(text, "\"tool\": \"metadata-maestro\""));
    CHECK(has(text, "\"version\": \"4.0.0\""));
    CHECK(text.back() == '\n');
}

TEST_CASE("A plan is indented and readable", "[planfile]") {
    const std::string text = pf::serialise(sample());
    CHECK(has(text, "\n  \"entries\":"));
    CHECK(has(text, "\n  \"summary\":"));
    CHECK(has(text, "\"files_scanned\": 10"));
}

TEST_CASE("Non-ASCII text is written as it is", "[planfile]") {
    pf::Plan plan;
    plan.source = "Caf\xC3\xA9";
    const std::string text = pf::serialise(plan);
    CHECK(has(text, "Caf\xC3\xA9"));
    CHECK_FALSE(has(text, "\\u"));
}

TEST_CASE("A change with nothing before it writes null", "[planfile]") {
    const std::string text = pf::serialise(sample());
    CHECK(has(text, "\"from\": null"));
    CHECK(has(text, "\"from\": \"Old\""));
}

TEST_CASE("A sidecar change has no key, a manifest change does", "[planfile]") {
    const std::string text = pf::serialise(sample());
    const auto sidecar = text.find("\"target\": \"reader.txt\"");
    REQUIRE(sidecar != std::string::npos);
    const auto open = text.rfind('{', sidecar);
    const auto end = text.find('}', sidecar);
    CHECK(text.substr(open, end - open).find("\"key\"") == std::string::npos);
    CHECK(has(text, "\"key\": \"dewey\""));
}

TEST_CASE("A cover is written as an object, and null when there is none", "[planfile]") {
    pf::Plan plan = sample();
    plan.entries[0].cover = R"({"file":"cover.jpg","width":500})";
    const std::string text = pf::serialise(plan);
    CHECK(has(text, "\"cover\":"));
    CHECK(has(text, "\"width\": 500"));
    CHECK(has(text, "\"cover\": null"));
}

TEST_CASE("A cover that is not JSON is written as null, not as broken output", "[planfile]") {
    pf::Plan plan = sample();
    plan.entries[0].cover = "{ not json";
    const auto parsed = pf::parse(pf::serialise(plan));
    REQUIRE(parsed.ok());
    CHECK(parsed.plan.entries[0].cover.empty());
}

// ---------------------------------------------------------------- round trip

TEST_CASE("A plan survives writing and reading", "[planfile]") {
    const pf::Plan plan = sample();
    const auto parsed = pf::parse(pf::serialise(plan));
    REQUIRE(parsed.ok());
    const pf::Plan& got = parsed.plan;
    CHECK(got.tool == "metadata-maestro");
    CHECK(got.version == "4.0.0");
    CHECK(got.created == plan.created);
    CHECK(got.source == "user");
    CHECK(got.summary.files_scanned == 10);
    CHECK(got.summary.files_changing == 2);
    CHECK(got.summary.folders_changing == 1);
    CHECK(got.warnings == plan.warnings);
    REQUIRE(got.entries.size() == 2);
    CHECK(got.entries[0].file == "/books/a/01.mp3");
    CHECK(got.entries[0].family == "id3");
    CHECK(got.entries[0].type == "audio");
    CHECK(got.entries[0].fingerprint.size == 123456);
    CHECK(got.entries[0].fingerprint.mtime_ns == 1700000000123456789LL);
    CHECK(got.entries[0].warnings == std::vector<std::string>{"careful"});
    REQUIRE(got.entries[0].changes.size() == 2);
    CHECK(got.entries[0].changes[0].key == "title");
    CHECK(got.entries[0].changes[0].from == std::optional<std::string>("Old"));
    CHECK(got.entries[0].changes[0].to == "New");
    CHECK(got.entries[0].changes[0].source == "user");
    CHECK_FALSE(got.entries[0].changes[1].from.has_value());
    REQUIRE(got.folders.size() == 1);
    CHECK(got.folders[0].dir == "/books/a");
    REQUIRE(got.folders[0].changes.size() == 2);
    CHECK(got.folders[0].changes[0].target == "reader.txt");
    CHECK(got.folders[0].changes[0].key.empty());
    CHECK(got.folders[0].changes[1].key == "dewey");
    CHECK(got.folders[0].changes[1].from == std::optional<std::string>("813"));
}

TEST_CASE("Writing a plan twice gives the same text", "[planfile]") {
    const std::string once = pf::serialise(sample());
    CHECK(pf::serialise(pf::parse(once).plan) == once);
}

TEST_CASE("A cover keeps its contents through a round trip", "[planfile]") {
    pf::Plan plan = sample();
    plan.entries[0].cover = R"({"file":"cover.jpg","width":500})";
    const auto parsed = pf::parse(pf::serialise(plan));
    REQUIRE(parsed.ok());
    CHECK(parsed.plan.entries[0].cover == plan.entries[0].cover);
}

TEST_CASE("An empty value is kept, which is how a tag is cleared", "[planfile]") {
    pf::Plan plan;
    pf::Entry entry;
    entry.file = "/a.mp3";
    entry.family = "id3";
    entry.changes = {{"title", std::string("Old"), "", "user"}};
    plan.entries.push_back(entry);
    const auto parsed = pf::parse(pf::serialise(plan));
    REQUIRE(parsed.ok());
    CHECK(parsed.plan.entries[0].changes[0].to.empty());
}

// ---------------------------------------------------------------- reading

TEST_CASE("The smallest valid plan reads", "[planfile]") {
    const auto parsed = pf::parse(kMinimal);
    CHECK(parsed.ok());
    CHECK(parsed.plan.entries.empty());
    CHECK(parsed.plan.folders.empty());
}

TEST_CASE("Fields the reader does not know are ignored", "[planfile]") {
    const auto parsed = pf::parse(R"({"version":"4.1.7","extra":[1],"entries":[],"folders":[],"summary":{"other":1}})");
    CHECK(parsed.ok());
}

TEST_CASE("Text that is not JSON, or not an object, is refused", "[planfile]") {
    for (const char* bad : {"", "nonsense", "[1]", "42", "null", "\"s\"", "{\"version\":"}) {
        INFO(bad);
        const auto parsed = pf::parse(bad);
        CHECK_FALSE(parsed.ok());
        CHECK(parsed.error == "plan is not a JSON object");
    }
}

TEST_CASE("Only strict JSON is a plan", "[planfile]") {
    CHECK_FALSE(pf::parse(std::string(kMinimal) + " trailing").ok());
    CHECK_FALSE(pf::parse(R"({"version":"4.0.0","entries":[],"folders":[],})").ok());
}

TEST_CASE("A plan from another major version is refused with the version named", "[planfile]") {
    const auto parsed = pf::parse(R"({"version":"3.2.0","entries":[],"folders":[]})");
    CHECK(parsed.error == "plan is from version 3.2.0; re-run plan");
    CHECK(pf::parse(R"({"version":"5.0.0","entries":[],"folders":[]})").error ==
          "plan is from version 5.0.0; re-run plan");
}

TEST_CASE("A plan with no usable version is refused", "[planfile]") {
    CHECK(pf::parse(R"({"entries":[],"folders":[]})").error == "plan is from version ?; re-run plan");
    CHECK(pf::parse(R"({"version":4,"entries":[],"folders":[]})").error == "plan is from version ?; re-run plan");
    CHECK(pf::parse(R"({"version":"","entries":[],"folders":[]})").error == "plan is from version ?; re-run plan");
}

TEST_CASE("Version 4 with any minor and patch is accepted, and 40 is not", "[planfile]") {
    CHECK(pf::parse(R"({"version":"4","entries":[],"folders":[]})").ok());
    CHECK(pf::parse(R"({"version":"4.9.9","entries":[],"folders":[]})").ok());
    CHECK_FALSE(pf::parse(R"({"version":"40.0.0","entries":[],"folders":[]})").ok());
}

TEST_CASE("A plan needs its entries and folders lists", "[planfile]") {
    CHECK(pf::parse(R"({"version":"4.0.0","folders":[]})").error == "plan has no entries list");
    CHECK(pf::parse(R"({"version":"4.0.0","entries":{},"folders":[]})").error == "plan has no entries list");
    CHECK(pf::parse(R"({"version":"4.0.0","entries":[]})").error == "plan has no folders list");
    CHECK(pf::parse(R"({"version":"4.0.0","entries":[],"folders":"x"})").error == "plan has no folders list");
}

TEST_CASE("A good entry reads", "[planfile]") {
    const auto parsed = pf::parse(with_entry(kGoodEntry));
    REQUIRE(parsed.ok());
    CHECK(parsed.plan.entries[0].fingerprint.size == 5);
    CHECK(parsed.plan.entries[0].fingerprint.mtime_ns == 6);
    CHECK(parsed.plan.entries[0].changes[0].source == "s");
}

TEST_CASE("A bad entry is refused with its number", "[planfile]") {
    const std::string two = std::string(kGoodEntry) + ",5";
    CHECK(pf::parse(with_entry(two)).error == "entry 2: not an object");
    CHECK(pf::parse(with_entry("{}")).error == "entry 1: missing file");
    CHECK(pf::parse(with_entry(R"({"file":"/a.mp3"})")).error == "entry 1: missing family");
    CHECK(pf::parse(with_entry(R"({"file":"/a.mp3","family":"id3"})")).error == "entry 1: missing fingerprint");
    CHECK(pf::parse(with_entry(R"({"file":"","family":"id3"})")).error == "entry 1: missing file");
    CHECK(pf::parse(with_entry(R"({"file":"/a","family":""})")).error == "entry 1: missing family");
}

TEST_CASE("A fingerprint needs whole numbers", "[planfile]") {
    for (const char* bad :
         {R"({"size":"5","mtime_ns":6})", R"({"size":5})", R"({"mtime_ns":6})", R"({"size":-1,"mtime_ns":6})",
          R"({"size":1.5,"mtime_ns":6})", R"({"size":5,"mtime_ns":null})", R"("x")"}) {
        INFO(bad);
        const auto parsed =
            pf::parse(with_entry(std::string(R"({"file":"/a","family":"id3","fingerprint":)") + bad + "}"));
        CHECK(parsed.error == "entry 1: missing fingerprint");
    }
}

TEST_CASE("A negative modification time is allowed", "[planfile]") {
    const auto parsed =
        pf::parse(with_entry(R"({"file":"/a","family":"id3","fingerprint":{"size":0,"mtime_ns":-5},"changes":[]})"));
    REQUIRE(parsed.ok());
    CHECK(parsed.plan.entries[0].fingerprint.mtime_ns == -5);
}

TEST_CASE("Changes are optional on an entry but must be well formed when there", "[planfile]") {
    const std::string head = R"({"file":"/a","family":"id3","fingerprint":{"size":1,"mtime_ns":2})";
    CHECK(pf::parse(with_entry(head + "}")).ok());
    CHECK(pf::parse(with_entry(head + R"(,"changes":"x"})")).error == "entry 1: changes is not a list");
    CHECK(pf::parse(with_entry(head + R"(,"changes":[5]})")).error == "entry 1: change 1 is not an object");
    CHECK(pf::parse(with_entry(head + R"(,"changes":[{"to":"x"}]})")).error == "entry 1: change 1 has no key");
    CHECK(pf::parse(with_entry(head + R"(,"changes":[{"key":"t"}]})")).error == "entry 1: change 1 has no value");
    CHECK(pf::parse(with_entry(head + R"(,"changes":[{"key":"t","to":5}]})")).error ==
          "entry 1: change 1 has no value");
    CHECK(pf::parse(with_entry(head + R"(,"changes":[{"key":"t","to":"x"},{"key":"u"}]})")).error ==
          "entry 1: change 2 has no value");
}

TEST_CASE("A before value may be null, absent or text, but not anything else", "[planfile]") {
    const std::string head = R"({"file":"/a","family":"id3","fingerprint":{"size":1,"mtime_ns":2},"changes":[)";
    CHECK(pf::parse(with_entry(head + R"({"key":"t","to":"x"}]})")).ok());
    CHECK(pf::parse(with_entry(head + R"({"key":"t","from":null,"to":"x"}]})")).ok());
    CHECK(pf::parse(with_entry(head + R"({"key":"t","from":"y","to":"x"}]})")).ok());
    CHECK(pf::parse(with_entry(head + R"({"key":"t","from":7,"to":"x"}]})")).error ==
          "entry 1: change 1 has a bad before value");
}

TEST_CASE("A missing source reads as empty", "[planfile]") {
    const auto parsed = pf::parse(with_entry(
        R"({"file":"/a","family":"id3","fingerprint":{"size":1,"mtime_ns":2},"changes":[{"key":"t","to":"x"}]})"));
    REQUIRE(parsed.ok());
    CHECK(parsed.plan.entries[0].changes[0].source.empty());
}

TEST_CASE("Warnings that are not text are left out", "[planfile]") {
    const auto parsed = pf::parse(
        R"({"version":"4.0.0","warnings":["a",5,null,"b"],"entries":[{"file":"/a","family":"id3","fingerprint":{"size":1,"mtime_ns":2},"warnings":["w",1]}],"folders":[]})");
    REQUIRE(parsed.ok());
    CHECK(parsed.plan.warnings == std::vector<std::string>{"a", "b"});
    CHECK(parsed.plan.entries[0].warnings == std::vector<std::string>{"w"});
}

TEST_CASE("Folder entries are checked too", "[planfile]") {
    const std::string pre = R"({"version":"4.0.0","entries":[],"folders":[)";
    CHECK(pf::parse(pre + R"(7]})").error == "folder 1: not an object");
    CHECK(pf::parse(pre + R"({}]})").error == "folder 1: missing dir");
    CHECK(pf::parse(pre + R"({"dir":"/d"}]})").ok());
    CHECK(pf::parse(pre + R"({"dir":"/d","changes":"x"}]})").error == "folder 1: changes is not a list");
    CHECK(pf::parse(pre + R"({"dir":"/d","changes":[3]}]})").error == "folder 1: change 1 is not an object");
    CHECK(pf::parse(pre + R"({"dir":"/d","changes":[{"to":"x"}]}]})").error == "folder 1: change 1 has no target");
    CHECK(pf::parse(pre + R"({"dir":"/d","changes":[{"target":"reader.txt"}]}]})").error ==
          "folder 1: change 1 has no value");
    CHECK(pf::parse(pre + R"({"dir":"/d","changes":[{"target":"reader.txt","to":"x","from":4}]}]})").error ==
          "folder 1: change 1 has a bad before value");
}

TEST_CASE("The second folder is numbered two", "[planfile]") {
    const auto parsed = pf::parse(R"({"version":"4.0.0","entries":[],"folders":[{"dir":"/a"},{}]})");
    CHECK(parsed.error == "folder 2: missing dir");
}

TEST_CASE("A plan written by the original script reads", "[planfile]") {
    const std::string text = R"({
  "tool": "metadata-maestro",
  "version": "4.0.0",
  "created": "2026-01-02T03:04:05+00:00",
  "source": "user",
  "summary": {"files_scanned": 1, "files_changing": 1, "folders_changing": 1},
  "warnings": [],
  "entries": [{"file": "/b/01.mp3", "family": "id3", "fingerprint": {"size": 100, "mtime_ns": 1700000000000000000},
               "type": "mp3", "changes": [{"key": "title", "from": null, "to": "One", "source": "user+filename"}],
               "cover": null, "warnings": []}],
  "folders": [{"dir": "/b", "changes": [{"target": "reader.txt", "from": null, "to": "Jane", "source": "user"}]}]
})";
    const auto parsed = pf::parse(text);
    REQUIRE(parsed.ok());
    CHECK(parsed.plan.entries[0].fingerprint.mtime_ns == 1700000000000000000LL);
    CHECK(parsed.plan.entries[0].changes[0].source == "user+filename");
    CHECK(parsed.plan.folders[0].changes[0].target == "reader.txt");
}

// ---------------------------------------------------------------- handing on

TEST_CASE("Entries the tag writers handle become batch input, in order", "[planfile]") {
    const auto input = pf::batch_input(sample());
    REQUIRE(input.entries.size() == 2);
    CHECK(input.unsupported.empty());
    CHECK(input.entries[0].path == "/books/a/01.mp3");
    CHECK(input.entries[0].fingerprint.size == 123456);
    CHECK(input.entries[0].fingerprint.mtime_ns == 1700000000123456789LL);
    REQUIRE(input.entries[0].edits.size() == 2);
    CHECK(input.entries[0].edits[0].key == "title");
    CHECK(input.entries[0].edits[0].to == "New");
    CHECK(input.entries[1].path == "/books/a/02.flac");
    CHECK(input.entries[1].edits.empty());
}

TEST_CASE("All three tag families are handled", "[planfile]") {
    pf::Plan plan;
    for (const char* family : {"id3", "mp4", "vorbis"}) {
        pf::Entry entry;
        entry.file = std::string("/a.") + family;
        entry.family = family;
        plan.entries.push_back(entry);
    }
    const auto input = pf::batch_input(plan);
    CHECK(input.entries.size() == 3);
    CHECK(input.unsupported.empty());
}

TEST_CASE("Other families are listed as unsupported, not dropped silently", "[planfile]") {
    pf::Plan plan = sample();
    pf::Entry image;
    image.file = "/pics/a.jpg";
    image.family = "image";
    plan.entries.insert(plan.entries.begin() + 1, image);
    pf::Entry remux;
    remux.file = "/film/b.mkv";
    remux.family = "remux";
    plan.entries.push_back(remux);
    const auto input = pf::batch_input(plan);
    REQUIRE(input.entries.size() == 2);
    CHECK(input.unsupported == std::vector<std::string>{"/pics/a.jpg: no writer for image files yet",
                                                        "/film/b.mkv: no writer for remux files yet"});
}

TEST_CASE("Folder changes carry over without the source", "[planfile]") {
    const auto changes = pf::folder_changes(sample().folders[0]);
    REQUIRE(changes.size() == 2);
    CHECK(changes[0].target == "reader.txt");
    CHECK(changes[0].key.empty());
    CHECK_FALSE(changes[0].from.has_value());
    CHECK(changes[0].to == "Jane");
    CHECK(changes[1].target == "metadata-maestro.json");
    CHECK(changes[1].key == "dewey");
    CHECK(changes[1].from == std::optional<std::string>("813"));
    CHECK(changes[1].to == "823");
}

// ---------------------------------------------------------------- edges

TEST_CASE("Comments are not JSON and are refused", "[planfile]") {
    CHECK(pf::parse(R"({"version":"4.0.0",/* c */"entries":[],"folders":[]})").error == "plan is not a JSON object");
    CHECK_FALSE(pf::parse("{\"version\":\"4.0.0\",\"entries\":[],\"folders\":[]} // c").ok());
    CHECK_FALSE(pf::parse("// c\n{\"version\":\"4.0.0\",\"entries\":[],\"folders\":[]}").ok());
}

TEST_CASE("Slashes inside strings are not comments", "[planfile]") {
    const auto parsed = pf::parse(
        R"({"version":"4.0.0","source":"a//b /* c */","entries":[{"file":"/x/y.mp3","family":"id3","fingerprint":{"size":1,"mtime_ns":2}}],"folders":[]})");
    REQUIRE(parsed.ok());
    CHECK(parsed.plan.source == "a//b /* c */");
    CHECK(parsed.plan.entries[0].file == "/x/y.mp3");
}

TEST_CASE("An escaped quote does not end a string early", "[planfile]") {
    const auto parsed =
        pf::parse(R"({"version":"4.0.0","source":"say \"hi\" // not a comment","entries":[],"folders":[]})");
    REQUIRE(parsed.ok());
    CHECK(parsed.plan.source == "say \"hi\" // not a comment");
}

TEST_CASE("A string ending in a backslash does not hide a comment after it", "[planfile]") {
    CHECK_FALSE(pf::parse(R"({"version":"4.0.0","source":"a\\", /* c */ "entries":[],"folders":[]})").ok());
    CHECK(pf::parse(R"({"version":"4.0.0","source":"a\\","entries":[],"folders":[]})").ok());
}

TEST_CASE("A document that opens with an escaped quote still reads its strings in step", "[planfile]") {
    const auto parsed = pf::parse(R"({"\"k":"x","source":"a/b","version":"4.0.0","entries":[],"folders":[]})");
    REQUIRE(parsed.ok());
    CHECK(parsed.plan.source == "a/b");
}

TEST_CASE("A repeated key is not an error, the last one wins", "[planfile]") {
    const auto parsed = pf::parse(R"({"version":"4.0.0","source":"one","source":"two","entries":[],"folders":[]})");
    REQUIRE(parsed.ok());
    CHECK(parsed.plan.source == "two");
}

TEST_CASE("Version numbers are read as plain digits", "[planfile]") {
    CHECK(pf::parse(R"({"version":"x.0.0","entries":[],"folders":[]})").error == "plan is from version ?; re-run plan");
    CHECK(pf::parse(R"({"version":"4a.0.0","entries":[],"folders":[]})").error ==
          "plan is from version ?; re-run plan");
    CHECK(pf::parse(R"({"version":".0.0","entries":[],"folders":[]})").error == "plan is from version ?; re-run plan");
    CHECK(pf::parse(R"({"version":"0.0.0","entries":[],"folders":[]})").error ==
          "plan is from version 0.0.0; re-run plan");
    CHECK(pf::parse(R"({"version":"9.0.0","entries":[],"folders":[]})").error ==
          "plan is from version 9.0.0; re-run plan");
}

TEST_CASE("The first problem stops the read, whatever follows", "[planfile]") {
    const std::string good = kGoodEntry;
    CHECK(pf::parse(R"({"version":"3.0.0","entries":[)" + good + R"(],"folders":[{"dir":"/d"}]})").error ==
          "plan is from version 3.0.0; re-run plan");
    CHECK(pf::parse(R"({"version":"4.0.0","entries":[{},)" + good + R"(],"folders":[]})").error ==
          "entry 1: missing file");
    CHECK(pf::parse(R"({"version":"4.0.0","entries":[],"folders":[{},{"dir":"/d"}]})").error ==
          "folder 1: missing dir");
}

TEST_CASE("A good entry after a bad one does not rescue the plan", "[planfile]") {
    const std::string good = kGoodEntry;
    const auto parsed = pf::parse(R"({"version":"4.0.0","entries":[5,)" + good + R"(],"folders":[{"dir":"/d"}]})");
    CHECK_FALSE(parsed.ok());
}

TEST_CASE("A missing before value reads as none", "[planfile]") {
    const auto parsed = pf::parse(
        R"({"version":"4.0.0","entries":[],"folders":[{"dir":"/d","changes":[{"target":"reader.txt","to":"x"}]}]})");
    REQUIRE(parsed.ok());
    CHECK_FALSE(parsed.plan.folders[0].changes[0].from.has_value());
    CHECK(parsed.plan.folders[0].changes[0].source.empty());
}
