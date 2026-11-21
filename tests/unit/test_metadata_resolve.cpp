#include <catch2/catch_test_macros.hpp>
#include <map>
#include <string>

#include "MetadataResolve.hpp"

namespace mr = MetadataResolve;

namespace {

struct Disk {
    std::map<std::string, std::map<std::string, std::string>> tags;
    std::map<std::string, MetadataApply::Fingerprint> prints;
    std::map<std::string, std::string> families;

    Disk& file(const std::string& path, const std::string& family = "id3",
               std::map<std::string, std::string> current = {}) {
        tags[path] = std::move(current);
        prints[path] = {static_cast<std::uintmax_t>(100 + prints.size()), 5000 + static_cast<long long>(prints.size())};
        families[path] = family;
        return *this;
    }

    mr::Probe probe() const {
        mr::Probe p;
        p.family = [this](const std::string& path) {
            const auto it = families.find(path);
            return it == families.end() ? std::string() : it->second;
        };
        p.fingerprint = [this](const std::string& path) -> std::optional<MetadataApply::Fingerprint> {
            const auto it = prints.find(path);
            if (it == prints.end()) {
                return std::nullopt;
            }
            return it->second;
        };
        p.tag = [this](const std::string& path, const std::string& key) -> std::optional<std::string> {
            const auto file = tags.find(path);
            if (file == tags.end()) {
                return std::nullopt;
            }
            const auto it = file->second.find(key);
            if (it == file->second.end()) {
                return std::nullopt;
            }
            return it->second;
        };
        return p;
    }
};

MetadataPlan::Entry entry(const std::string& path, std::map<std::string, std::string> set = {},
                          std::vector<std::string> clear = {}) {
    MetadataPlan::Entry e;
    e.path = path;
    e.set = std::move(set);
    e.clear = std::move(clear);
    return e;
}

MetadataPlan::Plan plan_of(std::vector<MetadataPlan::Entry> entries) {
    MetadataPlan::Plan plan;
    plan.entries = std::move(entries);
    return plan;
}

}  // namespace

TEST_CASE("A new value becomes a change with nothing before it", "[resolve]") {
    Disk disk;
    disk.file("/a.mp3");
    const auto out = mr::resolve(plan_of({entry("/a.mp3", {{"title", "One"}})}), "user", "T", disk.probe());
    REQUIRE(out.entries.size() == 1);
    const auto& e = out.entries[0];
    CHECK(e.file == "/a.mp3");
    CHECK(e.family == "id3");
    CHECK(e.fingerprint == disk.prints["/a.mp3"]);
    REQUIRE(e.changes.size() == 1);
    CHECK(e.changes[0].key == "title");
    CHECK_FALSE(e.changes[0].from.has_value());
    CHECK(e.changes[0].to == "One");
    CHECK(e.changes[0].source == "user");
}

TEST_CASE("The saved plan carries tool, source and time", "[resolve]") {
    Disk disk;
    const auto out = mr::resolve(plan_of({}), "csv", "2026-01-01T00:00:00+00:00", disk.probe());
    CHECK(out.tool == "metadata-maestro");
    CHECK(out.version == MetadataPlanFile::kVersion);
    CHECK(out.source == "csv");
    CHECK(out.created == "2026-01-01T00:00:00+00:00");
}

TEST_CASE("A changed value remembers what it was", "[resolve]") {
    Disk disk;
    disk.file("/a.mp3", "id3", {{"title", "Old"}});
    const auto out = mr::resolve(plan_of({entry("/a.mp3", {{"title", "New"}})}), "user", "T", disk.probe());
    REQUIRE(out.entries.size() == 1);
    CHECK(out.entries[0].changes[0].from == std::optional<std::string>("Old"));
}

TEST_CASE("A value already in place is not a change", "[resolve]") {
    Disk disk;
    disk.file("/a.mp3", "id3", {{"title", "Same"}});
    const auto out = mr::resolve(plan_of({entry("/a.mp3", {{"title", "Same"}})}), "user", "T", disk.probe());
    CHECK(out.entries.empty());
    CHECK(out.summary.files_scanned == 1);
    CHECK(out.summary.files_changing == 0);
}

TEST_CASE("Only the keys that differ are kept", "[resolve]") {
    Disk disk;
    disk.file("/a.mp3", "id3", {{"title", "Same"}, {"artist", "Old"}});
    const auto out =
        mr::resolve(plan_of({entry("/a.mp3", {{"title", "Same"}, {"artist", "New"}})}), "user", "T", disk.probe());
    REQUIRE(out.entries.size() == 1);
    REQUIRE(out.entries[0].changes.size() == 1);
    CHECK(out.entries[0].changes[0].key == "artist");
}

TEST_CASE("Set keys come first in key order, then clears in key order", "[resolve]") {
    Disk disk;
    disk.file("/a.mp3", "id3", {{"genre", "Old"}, {"comment", "Old"}});
    const auto out = mr::resolve(plan_of({entry("/a.mp3", {{"title", "T"}, {"album", "A"}}, {"genre", "comment"})}),
                                 "user", "T", disk.probe());
    REQUIRE(out.entries[0].changes.size() == 4);
    CHECK(out.entries[0].changes[0].key == "album");
    CHECK(out.entries[0].changes[1].key == "title");
    CHECK(out.entries[0].changes[2].key == "genre");
    CHECK(out.entries[0].changes[3].key == "comment");
}

TEST_CASE("Clearing a tag that exists writes an empty value and keeps the old one", "[resolve]") {
    Disk disk;
    disk.file("/a.mp3", "id3", {{"genre", "Rock"}});
    const auto out = mr::resolve(plan_of({entry("/a.mp3", {}, {"genre"})}), "user", "T", disk.probe());
    REQUIRE(out.entries.size() == 1);
    CHECK(out.entries[0].changes[0].to.empty());
    CHECK(out.entries[0].changes[0].from == std::optional<std::string>("Rock"));
}

TEST_CASE("Clearing an absent or empty tag is not a change", "[resolve]") {
    Disk disk;
    disk.file("/a.mp3", "id3", {{"genre", ""}});
    const auto out = mr::resolve(plan_of({entry("/a.mp3", {}, {"genre", "title"})}), "user", "T", disk.probe());
    CHECK(out.entries.empty());
}

TEST_CASE("Setting an empty value over a tag clears it, and over nothing does nothing", "[resolve]") {
    Disk disk;
    disk.file("/a.mp3", "id3", {{"genre", "Rock"}});
    const auto out = mr::resolve(plan_of({entry("/a.mp3", {{"genre", ""}, {"title", ""}})}), "user", "T", disk.probe());
    REQUIRE(out.entries.size() == 1);
    REQUIRE(out.entries[0].changes.size() == 1);
    CHECK(out.entries[0].changes[0].key == "genre");
}

TEST_CASE("The clear marker counts as a clear", "[resolve]") {
    Disk disk;
    disk.file("/a.mp3", "id3", {{"genre", "Rock"}});
    const auto out =
        mr::resolve(plan_of({entry("/a.mp3", {{"genre", MetadataPlan::kClearMarker}})}), "user", "T", disk.probe());
    REQUIRE(out.entries.size() == 1);
    CHECK(out.entries[0].changes[0].to.empty());
    CHECK(out.entries[0].changes[0].from == std::optional<std::string>("Rock"));
}

TEST_CASE("A file that cannot be read is warned about and left out", "[resolve]") {
    Disk disk;
    disk.file("/b.mp3");
    const auto out = mr::resolve(plan_of({entry("/gone.mp3", {{"title", "T"}}), entry("/b.mp3", {{"title", "T"}})}),
                                 "user", "T", disk.probe());
    REQUIRE(out.entries.size() == 1);
    CHECK(out.entries[0].file == "/b.mp3");
    REQUIRE(out.warnings.size() == 1);
    CHECK(out.warnings[0] == "/gone.mp3: cannot read the file");
    CHECK(out.summary.files_scanned == 2);
}

TEST_CASE("A file no tag writer takes is warned about and left out", "[resolve]") {
    Disk disk;
    disk.file("/a.xyz", "");
    const auto out = mr::resolve(plan_of({entry("/a.xyz", {{"title", "T"}})}), "user", "T", disk.probe());
    CHECK(out.entries.empty());
    REQUIRE(out.warnings.size() == 1);
    CHECK(out.warnings[0] == "/a.xyz: no tag writer for this file");
}

TEST_CASE("The request's own warnings and skips come through", "[resolve]") {
    Disk disk;
    MetadataPlan::Plan plan;
    plan.warnings = {"watch out"};
    plan.skipped = {"/x.png: images are not taggable"};
    const auto out = mr::resolve(plan, "user", "T", disk.probe());
    CHECK(out.warnings == std::vector<std::string>{"watch out", "/x.png: images are not taggable"});
}

TEST_CASE("Summary counts files scanned and files changing", "[resolve]") {
    Disk disk;
    disk.file("/a.mp3", "id3", {{"title", "X"}}).file("/b.m4a", "mp4").file("/c.flac", "vorbis", {{"title", "Y"}});
    const auto out = mr::resolve(plan_of({entry("/a.mp3", {{"title", "X"}}), entry("/b.m4a", {{"title", "N"}}),
                                          entry("/c.flac", {{"title", "Z"}})}),
                                 "user", "T", disk.probe());
    CHECK(out.summary.files_scanned == 3);
    CHECK(out.summary.files_changing == 2);
    CHECK(out.entries[0].family == "mp4");
    CHECK(out.entries[1].family == "vorbis");
}

TEST_CASE("Resolved plans survive writing and reading", "[resolve]") {
    Disk disk;
    disk.file("/a.mp3", "id3", {{"title", "Old"}});
    const auto out = mr::resolve(plan_of({entry("/a.mp3", {{"title", "New"}})}), "user", "T", disk.probe());
    const auto parsed = MetadataPlanFile::parse(MetadataPlanFile::serialise(out));
    REQUIRE(parsed.ok());
    CHECK(parsed.plan.entries[0].changes[0].from == std::optional<std::string>("Old"));
    const auto batch = MetadataPlanFile::batch_input(parsed.plan);
    REQUIRE(batch.entries.size() == 1);
    CHECK(batch.entries[0].fingerprint == disk.prints["/a.mp3"]);
}
