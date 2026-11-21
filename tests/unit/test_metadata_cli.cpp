#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <sstream>
#include <string>

#include "MetadataCli.hpp"
#include "MetadataPlanFile.hpp"
#include "MetadataTags.hpp"

namespace fs = std::filesystem;

namespace {

const std::string kFixtures = FIXTURE_DIR;
const std::string kNow = "2026-02-03T04:05:06+00:00";

struct Run {
    int code{0};
    std::string out;
    std::string err;
};

Run cli(const std::vector<std::string>& args) {
    std::ostringstream out;
    std::ostringstream err;
    Run run;
    run.code = MetadataCli::run(args, out, err, kNow);
    run.out = out.str();
    run.err = err.str();
    return run;
}

std::string scratch() {
    static std::random_device device;
    static unsigned counter = 0;
    const fs::path dir =
        fs::temp_directory_path() / ("mm-cli-" + std::to_string(device()) + "-" + std::to_string(++counter));
    fs::create_directories(dir);
    return dir.string();
}

std::string slurp(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void spit(const std::string& path, const std::string& text) {
    std::ofstream(path, std::ios::binary) << text;
}

bool has(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

std::string copy_fixture(const std::string& dir, const std::string& fixture, const std::string& name) {
    const std::string path = dir + "/" + name;
    fs::copy_file(fs::path(kFixtures) / fixture, path);
    return path;
}

}  // namespace

TEST_CASE("No command, or an unknown one, is a usage mistake", "[cli]") {
    CHECK(cli({}).code == 2);
    CHECK(has(cli({}).err, "usage:"));
    const Run run = cli({"frobnicate"});
    CHECK(run.code == 2);
    CHECK(has(run.err, "unknown command: frobnicate"));
}

TEST_CASE("Plan needs a source and at least one path", "[cli]") {
    const std::string dir = scratch();
    CHECK(cli({"plan", "--set", "title=T", dir}).code == 1);
    CHECK(has(cli({"plan", "--set", "title=T", dir}).err, "source"));
    const Run none = cli({"plan", "--source", "me", "--set", "title=T"});
    CHECK(none.code == 2);
    CHECK(has(none.err, "no paths"));
}

TEST_CASE("Plan refuses a malformed set, a missing value and an unknown flag", "[cli]") {
    const std::string dir = scratch();
    CHECK(cli({"plan", "--source", "me", "--set", "oops", dir}).code == 2);
    CHECK(has(cli({"plan", "--source", "me", "--set", "oops", dir}).err, "bad --set: oops"));
    CHECK(cli({"plan", "--source"}).code == 2);
    const Run flag = cli({"plan", "--source", "me", "--wat", dir});
    CHECK(flag.code == 2);
    CHECK(has(flag.err, "unknown option: --wat"));
}

TEST_CASE("Plan prints a plan that reads back, with the files' own state in it", "[cli]") {
    const std::string dir = scratch();
    const std::string mp3 = copy_fixture(dir, "a.mp3", "a.mp3");
    const Run run = cli({"plan", "--source", "me", "--set", "title=Hello", dir});
    REQUIRE(run.code == 0);
    const auto parsed = MetadataPlanFile::parse(run.out);
    REQUIRE(parsed.ok());
    CHECK(parsed.plan.source == "me");
    CHECK(parsed.plan.created == kNow);
    REQUIRE(parsed.plan.entries.size() == 1);
    CHECK(parsed.plan.entries[0].file == mp3);
    CHECK(parsed.plan.entries[0].family == "id3");
    CHECK(parsed.plan.entries[0].changes[0].key == "title");
    CHECK(parsed.plan.entries[0].changes[0].to == "Hello");
}

TEST_CASE("Plan walks folders, in order, and skips what it cannot tag", "[cli]") {
    const std::string dir = scratch();
    fs::create_directories(dir + "/sub");
    copy_fixture(dir, "a.mp3", "b.mp3");
    copy_fixture(dir + "/sub", "a.flac", "a.flac");
    spit(dir + "/notes.txt", "x");
    const Run run = cli({"plan", "--source", "me", "--set", "album=Al", dir});
    REQUIRE(run.code == 0);
    const auto parsed = MetadataPlanFile::parse(run.out);
    REQUIRE(parsed.ok());
    REQUIRE(parsed.plan.entries.size() == 2);
    CHECK(parsed.plan.entries[0].file == dir + "/b.mp3");
    CHECK(parsed.plan.entries[1].file == dir + "/sub/a.flac");
    CHECK(has(run.err, "notes.txt"));
}

TEST_CASE("Plan can be limited to some extensions", "[cli]") {
    const std::string dir = scratch();
    copy_fixture(dir, "a.mp3", "a.mp3");
    copy_fixture(dir, "a.flac", "a.flac");
    const auto parsed =
        MetadataPlanFile::parse(cli({"plan", "--source", "me", "--set", "album=Al", "--ext", "flac", dir}).out);
    REQUIRE(parsed.ok());
    REQUIRE(parsed.plan.entries.size() == 1);
    CHECK(parsed.plan.entries[0].family == "vorbis");
}

TEST_CASE("Plan can clear a tag, and leaves out files already as wanted", "[cli]") {
    const std::string dir = scratch();
    const std::string mp3 = copy_fixture(dir, "a.mp3", "a.mp3");
    REQUIRE(MetadataTags::write_tags(mp3, {{"genre", "Rock"}}).ok);
    const auto cleared = MetadataPlanFile::parse(cli({"plan", "--source", "me", "--clear", "genre", mp3}).out);
    REQUIRE(cleared.ok());
    REQUIRE(cleared.plan.entries.size() == 1);
    CHECK(cleared.plan.entries[0].changes[0].to.empty());
    CHECK(cleared.plan.entries[0].changes[0].from == std::optional<std::string>("Rock"));
    const auto same = MetadataPlanFile::parse(cli({"plan", "--source", "me", "--set", "genre=Rock", mp3}).out);
    REQUIRE(same.ok());
    CHECK(same.plan.entries.empty());
}

TEST_CASE("Plan writes to a file when asked, and prints nothing", "[cli]") {
    const std::string dir = scratch();
    const std::string mp3 = copy_fixture(dir, "a.mp3", "a.mp3");
    const std::string target = dir + "/plan.json";
    const Run run = cli({"plan", "--source", "me", "--set", "title=T", "--out", target, mp3});
    CHECK(run.code == 0);
    CHECK(run.out.empty());
    CHECK(MetadataPlanFile::parse(slurp(target)).ok());
}

TEST_CASE("Apply writes the tags and says what it did", "[cli]") {
    const std::string dir = scratch();
    const std::string mp3 = copy_fixture(dir, "a.mp3", "a.mp3");
    const std::string plan = dir + "/plan.json";
    REQUIRE(cli({"plan", "--source", "me", "--set", "title=Applied", "--out", plan, mp3}).code == 0);
    const Run run = cli({"apply", plan});
    CHECK(run.code == 0);
    CHECK(has(run.out, "1 file written"));
    CHECK(MetadataTags::read_tag(mp3, "title") == std::optional<std::string>("Applied"));
    CHECK(fs::exists(mp3 + ".mm-bak"));
}

TEST_CASE("Apply with no backup leaves no marker", "[cli]") {
    const std::string dir = scratch();
    const std::string mp3 = copy_fixture(dir, "a.mp3", "a.mp3");
    const std::string plan = dir + "/plan.json";
    REQUIRE(cli({"plan", "--source", "me", "--set", "title=T", "--out", plan, mp3}).code == 0);
    REQUIRE(cli({"apply", plan, "--no-backup"}).code == 0);
    CHECK_FALSE(fs::exists(mp3 + ".mm-bak"));
}

TEST_CASE("Apply stops a file that changed since the plan, and exits 1", "[cli]") {
    const std::string dir = scratch();
    const std::string mp3 = copy_fixture(dir, "a.mp3", "a.mp3");
    const std::string plan = dir + "/plan.json";
    REQUIRE(cli({"plan", "--source", "me", "--set", "title=T", "--out", plan, mp3}).code == 0);
    {
        std::ofstream append(mp3, std::ios::binary | std::ios::app);
        append << "tail";
    }
    const Run run = cli({"apply", plan});
    CHECK(run.code == 1);
    CHECK(has(run.err, mp3));
    CHECK_FALSE(MetadataTags::read_tag(mp3, "title").has_value());
}

TEST_CASE("Apply names the plan problems it meets", "[cli]") {
    const std::string dir = scratch();
    CHECK(cli({"apply"}).code == 2);
    const Run missing = cli({"apply", dir + "/nope.json"});
    CHECK(missing.code == 1);
    CHECK(has(missing.err, "cannot read"));
    spit(dir + "/bad.json", "{\"version\":\"3.0.0\",\"entries\":[],\"folders\":[]}");
    const Run old = cli({"apply", dir + "/bad.json"});
    CHECK(old.code == 1);
    CHECK(has(old.err, "plan is from version 3.0.0; re-run plan"));
}

TEST_CASE("Apply reports entries no writer handles and still does the rest", "[cli]") {
    const std::string dir = scratch();
    const std::string mp3 = copy_fixture(dir, "a.mp3", "a.mp3");
    const std::string plan = dir + "/plan.json";
    REQUIRE(cli({"plan", "--source", "me", "--set", "title=T", "--out", plan, mp3}).code == 0);
    auto parsed = MetadataPlanFile::parse(slurp(plan));
    MetadataPlanFile::Entry image;
    image.file = dir + "/p.jpg";
    image.family = "image";
    parsed.plan.entries.push_back(image);
    spit(plan, MetadataPlanFile::serialise(parsed.plan));
    const Run run = cli({"apply", plan});
    CHECK(run.code == 0);
    CHECK(has(run.err, "p.jpg: no writer for image files yet"));
    CHECK(MetadataTags::read_tag(mp3, "title") == std::optional<std::string>("T"));
}

TEST_CASE("Apply writes folder files from the plan", "[cli]") {
    const std::string dir = scratch();
    MetadataPlanFile::Plan plan;
    plan.folders.push_back({dir,
                            {{"reader.txt", "", std::nullopt, "Jane", "me"},
                             {"metadata-maestro.json", "dewey", std::nullopt, "813", "me"}}});
    spit(dir + "/plan.json", MetadataPlanFile::serialise(plan));
    const Run run = cli({"apply", dir + "/plan.json"});
    CHECK(run.code == 0);
    CHECK(has(run.out, "1 folder written"));
    CHECK(slurp(dir + "/reader.txt") == "Jane\n");
    const std::string manifest = slurp(dir + "/metadata-maestro.json");
    CHECK(has(manifest, "\"dewey\": \"813\""));
    CHECK(has(manifest, kNow));
}

TEST_CASE("A folder that cannot be written is reported and fails the run", "[cli]") {
    const std::string dir = scratch();
    MetadataPlanFile::Plan plan;
    plan.folders.push_back({dir + "/gone", {{"reader.txt", "", std::nullopt, "Jane", "me"}}});
    spit(dir + "/plan.json", MetadataPlanFile::serialise(plan));
    const Run run = cli({"apply", dir + "/plan.json"});
    CHECK(run.code == 1);
    CHECK(has(run.err, "folder gone"));
}

TEST_CASE("Apply records the run in the ledger when given one", "[cli]") {
    const std::string dir = scratch();
    const std::string mp3 = copy_fixture(dir, "a.mp3", "a.mp3");
    const std::string plan = dir + "/plan.json";
    const std::string ledger = dir + "/ledger.jsonl";
    REQUIRE(cli({"plan", "--source", "me", "--set", "title=T", "--out", plan, mp3}).code == 0);
    REQUIRE(cli({"apply", plan, "--ledger", ledger}).code == 0);
    const std::string text = slurp(ledger);
    CHECK(has(text, "\"event\":\"apply\""));
    CHECK(has(text, kNow));
    CHECK(has(text, plan));
}

TEST_CASE("Apply takes a batch size and a progress file", "[cli]") {
    const std::string dir = scratch();
    const std::string a = copy_fixture(dir, "a.mp3", "a.mp3");
    const std::string b = copy_fixture(dir, "a.mp3", "b.mp3");
    const std::string plan = dir + "/plan.json";
    const std::string progress = dir + "/progress.txt";
    REQUIRE(cli({"plan", "--source", "me", "--set", "title=T", "--out", plan, a, b}).code == 0);
    REQUIRE(cli({"apply", plan, "--batch", "1", "--progress", progress}).code == 0);
    CHECK(has(slurp(progress), "ok\t" + a));
    const Run again = cli({"apply", plan, "--progress", progress});
    CHECK(again.code == 0);
    CHECK(has(again.out, "2 already done"));
    CHECK(cli({"apply", plan, "--batch", "zero"}).code == 2);
    CHECK(cli({"apply", plan, "--batch", "0"}).code == 2);
}

TEST_CASE("A flag with nothing after it is named", "[cli]") {
    const Run plan = cli({"plan", "--source"});
    CHECK(plan.code == 2);
    CHECK(has(plan.err, "missing value for --source"));
    const Run apply = cli({"apply", "p.json", "--ledger"});
    CHECK(apply.code == 2);
    CHECK(has(apply.err, "missing value for --ledger"));
}

TEST_CASE("Plan names the container family for each kind of file", "[cli]") {
    const std::string dir = scratch();
    copy_fixture(dir, "a.m4a", "a.m4a");
    copy_fixture(dir, "a.ogg", "b.ogg");
    const auto parsed = MetadataPlanFile::parse(cli({"plan", "--source", "me", "--set", "album=Al", dir}).out);
    REQUIRE(parsed.ok());
    REQUIRE(parsed.plan.entries.size() == 2);
    CHECK(parsed.plan.entries[0].family == "mp4");
    CHECK(parsed.plan.entries[1].family == "vorbis");
}

TEST_CASE("Files in a folder are planned in name order", "[cli]") {
    const std::string dir = scratch();
    for (const char* name : {"h", "c", "k", "a", "f", "j", "b", "e", "i", "d", "g"}) {
        copy_fixture(dir, "a.mp3", std::string(name) + ".mp3");
    }
    const auto parsed = MetadataPlanFile::parse(cli({"plan", "--source", "me", "--set", "album=Al", dir}).out);
    REQUIRE(parsed.ok());
    REQUIRE(parsed.plan.entries.size() == 11);
    for (std::size_t i = 1; i < parsed.plan.entries.size(); ++i) {
        CHECK(parsed.plan.entries[i - 1].file < parsed.plan.entries[i].file);
    }
}

TEST_CASE("Plan reports an output file it cannot write", "[cli]") {
    if (!fs::exists("/dev/full")) {
        SKIP("no /dev/full here");
    }
    const std::string dir = scratch();
    const std::string mp3 = copy_fixture(dir, "a.mp3", "a.mp3");
    const Run run = cli({"plan", "--source", "me", "--set", "title=T", "--out", "/dev/full", mp3});
    CHECK(run.code == 1);
    CHECK(has(run.err, "cannot write /dev/full"));
}

TEST_CASE("Restart tries the files again, and the stale plan stops them", "[cli]") {
    const std::string dir = scratch();
    const std::string mp3 = copy_fixture(dir, "a.mp3", "a.mp3");
    const std::string plan = dir + "/plan.json";
    const std::string progress = dir + "/progress.txt";
    REQUIRE(cli({"plan", "--source", "me", "--set", "title=T", "--out", plan, mp3}).code == 0);
    REQUIRE(cli({"apply", plan, "--progress", progress, "--no-backup"}).code == 0);
    const Run again = cli({"apply", plan, "--progress", progress, "--restart", "--no-backup"});
    CHECK(again.code == 1);
    CHECK(has(again.err, mp3));
}

TEST_CASE("A ledger that cannot be written is reported but does not fail the run", "[cli]") {
    const std::string dir = scratch();
    const std::string mp3 = copy_fixture(dir, "a.mp3", "a.mp3");
    const std::string plan = dir + "/plan.json";
    REQUIRE(cli({"plan", "--source", "me", "--set", "title=T", "--out", plan, mp3}).code == 0);
    const Run run = cli({"apply", plan, "--ledger", dir});
    CHECK(run.code == 0);
    CHECK(has(run.err, "could not write the ledger"));
}

TEST_CASE("Apply refuses a second plan and an unknown option", "[cli]") {
    CHECK(has(cli({"apply", "a.json", "b.json"}).err, "more than one plan given"));
    CHECK(has(cli({"apply", "a.json", "--wat"}).err, "unknown option: --wat"));
    CHECK(has(cli({"apply", "--no-backup"}).err, "no plan given"));
}
