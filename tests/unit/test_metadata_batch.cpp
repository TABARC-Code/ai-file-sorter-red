#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>

#include "MetadataBatch.hpp"
#include "MetadataTags.hpp"

namespace mb = metadata_batch;
namespace ma = metadata_apply;
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
    const fs::path dir = fs::temp_directory_path() / ("mm-batch-" + tag + "-" + unique_suffix());
    fs::create_directories(dir);
    return dir.string();
}

std::string slurp(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

// Three copies of the MP3 fixture, planned with a title each.
std::vector<mb::PlannedEntry> plan_three(const std::string& dir) {
    std::vector<mb::PlannedEntry> entries;
    for (const char* name : {"one.mp3", "two.mp3", "three.mp3"}) {
        const std::string path = dir + "/" + name;
        fs::copy_file(fs::path(kFixtures) / "a.mp3", path);
        entries.push_back({path, *ma::fingerprint(path), {{"title", std::string("Title ") + name}}});
    }
    return entries;
}

}  // namespace

TEST_CASE("Progress lines survive tabs, newlines and backslashes in a path", "[batch]") {
    const std::string odd = "a\tb\nc\\d.mp3";
    const std::string line = mb::encode_progress("ok", odd);
    CHECK(line.find('\n') == std::string::npos);
    CHECK(mb::parse_progress(line + "\n") == std::set<std::string>{odd});
    CHECK(mb::encode_progress("failed", "x.mp3", "no\twriter") == "failed\tx.mp3\tno\\twriter");
    CHECK(mb::encode_progress("ok", "x.mp3") == "ok\tx.mp3");
}

TEST_CASE("Only ok lines count as done, and junk is ignored", "[batch]") {
    const std::string text = "ok\ta.mp3\nfailed\tb.mp3\tboom\n\nnonsense\nok\tc.mp3\nok\n";
    CHECK(mb::parse_progress(text) == std::set<std::string>{"a.mp3", "c.mp3"});
    CHECK(mb::parse_progress("").empty());
    CHECK(mb::parse_progress("ok\ta.mp3") == std::set<std::string>{"a.mp3"});
    CHECK(mb::parse_progress("ok\ta.mp3\r\nok\tb.mp3\r\n") == std::set<std::string>{"a.mp3", "b.mp3"});
}

TEST_CASE("Space is checked per folder with ten percent to spare", "[batch]") {
    const std::vector<mb::PlannedEntry> entries = {{"/m/a.mp3", {600, 0}, {}}, {"/m/b.mp3", {400, 0}, {}}};
    CHECK(mb::space_problem(entries, false, [](const std::string&) -> std::uintmax_t { return 1100; }).empty());
    CHECK_FALSE(mb::space_problem(entries, false, [](const std::string&) -> std::uintmax_t { return 1099; }).empty());
    CHECK(mb::space_problem(entries, true, [](const std::string&) -> std::uintmax_t { return 0; }).empty());
}

TEST_CASE("Folders are judged on their own", "[batch]") {
    const std::vector<mb::PlannedEntry> entries = {{"/one/a.mp3", {1000, 0}, {}}, {"/two/b.mp3", {1000, 0}, {}}};
    const auto free = [](const std::string& dir) -> std::uintmax_t { return dir == "/one" ? 5000 : 10; };
    const std::string problem = mb::space_problem(entries, false, free);
    CHECK(contains(problem, "/two"));
    CHECK_FALSE(contains(problem, "/one"));
}

TEST_CASE("The space message gives need and have in megabytes", "[batch]") {
    const std::vector<mb::PlannedEntry> entries = {{"/m/a.mp3", {3000000, 0}, {}}};
    const std::string problem =
        mb::space_problem(entries, false, [](const std::string&) -> std::uintmax_t { return 1000000; });
    CHECK(problem == "not enough free space in /m: need about 4 MB, have 1 MB");
}

TEST_CASE("Drops are listed by file name and key", "[batch]") {
    ma::Verdict verdict;
    verdict.dropped.push_back({"artist", "A", std::nullopt});
    verdict.dropped.push_back({"genre", "", std::string("Rock")});
    CHECK(mb::describe_drops("/music/song.mp3", verdict) ==
          std::vector<std::string>{"song.mp3: artist", "song.mp3: genre"});
    CHECK(mb::describe_drops("/music/song.mp3", {}).empty());
}

TEST_CASE("A plan runs in batches and records each file as done", "[batch]") {
    const std::string dir = scratch_dir("run");
    const auto entries = plan_three(dir);
    mb::Options options;
    options.batch_size = 2;
    options.progress_file = dir + "/plan.progress";
    const auto report = mb::run(entries, options);
    CHECK(report.error.empty());
    CHECK(report.ok == 3);
    CHECK(report.batches == 2);
    CHECK(report.failures.empty());
    CHECK(report.dropped.empty());
    CHECK(metadata_tags::read_tag(entries[1].path, "title") == std::optional<std::string>("Title two.mp3"));
    CHECK(mb::parse_progress(slurp(options.progress_file)).size() == 3);
    CHECK(fs::exists(entries[0].path + ma::kBackupSuffix));
    fs::remove_all(dir);
}

TEST_CASE("Running again skips what is already done", "[batch]") {
    const std::string dir = scratch_dir("again");
    const auto entries = plan_three(dir);
    mb::Options options;
    options.progress_file = dir + "/plan.progress";
    REQUIRE(mb::run(entries, options).ok == 3);
    const std::string written = slurp(entries[0].path);
    const auto second = mb::run(entries, options);
    CHECK(second.ok == 0);
    CHECK(second.already_done == 3);
    CHECK(second.batches == 0);
    CHECK(second.failures.empty());
    CHECK(slurp(entries[0].path) == written);
    fs::remove_all(dir);
}

TEST_CASE("Restart forgets progress and redoes every file from its backup", "[batch]") {
    const std::string dir = scratch_dir("restart");
    const auto entries = plan_three(dir);
    mb::Options options;
    options.progress_file = dir + "/plan.progress";
    REQUIRE(mb::run(entries, options).ok == 3);
    options.restart = true;
    const auto again = mb::run(entries, options);
    CHECK(again.ok == 3);
    CHECK(again.already_done == 0);
    CHECK(mb::parse_progress(slurp(options.progress_file)).size() == 3);
    fs::remove_all(dir);
}

TEST_CASE("A failing file is recorded and the rest carry on", "[batch]") {
    const std::string dir = scratch_dir("fail");
    auto entries = plan_three(dir);
    entries[1].edits = {{"track", "three"}};
    mb::Options options;
    options.progress_file = dir + "/plan.progress";
    const auto report = mb::run(entries, options);
    CHECK(report.ok == 2);
    REQUIRE(report.failures.size() == 1);
    CHECK(report.failures[0].path == entries[1].path);
    CHECK(contains(report.failures[0].error, "track/disc"));
    const std::string progress = slurp(options.progress_file);
    CHECK(contains(progress, "failed\t" + entries[1].path));
    CHECK(mb::parse_progress(progress) == std::set<std::string>{entries[0].path, entries[2].path});
    // Fix the entry and run again: only the failed file is attempted.
    entries[1].edits = {{"track", "3/12"}};
    const auto retry = mb::run(entries, options);
    CHECK(retry.ok == 1);
    CHECK(retry.already_done == 2);
    fs::remove_all(dir);
}

TEST_CASE("A batch size of zero still makes progress", "[batch]") {
    const std::string dir = scratch_dir("zero");
    const auto entries = plan_three(dir);
    mb::Options options;
    options.batch_size = 0;
    const auto report = mb::run(entries, options);
    CHECK(report.ok == 3);
    CHECK(report.batches == 3);
    fs::remove_all(dir);
}

TEST_CASE("Not enough space stops the run before any file is touched", "[batch]") {
    const std::string dir = scratch_dir("space");
    const auto entries = plan_three(dir);
    mb::Options options;
    options.progress_file = dir + "/plan.progress";
    options.free_space = [](const std::string&) -> std::uintmax_t { return 0; };
    const auto report = mb::run(entries, options);
    CHECK(contains(report.error, "not enough free space"));
    CHECK(report.ok == 0);
    CHECK_FALSE(fs::exists(options.progress_file));
    CHECK_FALSE(fs::exists(entries[0].path + ma::kBackupSuffix));
    fs::remove_all(dir);
}

TEST_CASE("Space is only judged for files still to do", "[batch]") {
    const std::string dir = scratch_dir("spacetodo");
    const auto entries = plan_three(dir);
    mb::Options options;
    options.progress_file = dir + "/plan.progress";
    REQUIRE(mb::run(entries, options).ok == 3);
    options.free_space = [](const std::string&) -> std::uintmax_t { return 0; };
    const auto report = mb::run(entries, options);
    CHECK(report.error.empty());
    CHECK(report.already_done == 3);
    fs::remove_all(dir);
}

TEST_CASE("Without a progress file nothing is remembered", "[batch]") {
    const std::string dir = scratch_dir("noprog");
    const auto entries = plan_three(dir);
    const auto report = mb::run(entries, {});
    CHECK(report.ok == 3);
    std::size_t files = 0;
    for (const auto& item : fs::directory_iterator(dir)) {
        files += item.path().extension() == ".progress" ? 1 : 0;
    }
    CHECK(files == 0);
    fs::remove_all(dir);
}

TEST_CASE("A lone trailing backslash is kept as it is", "[batch]") {
    CHECK(mb::parse_progress("ok\ta\\") == std::set<std::string>{"a\\"});
    CHECK(mb::parse_progress("ok\ta\\\\\n") == std::set<std::string>{"a\\"});
}

TEST_CASE("An ok line with extra fields still gives just the path", "[batch]") {
    CHECK(mb::parse_progress("ok\ta.mp3\textra\tmore\nok\tb.mp3\tx\n") == std::set<std::string>{"a.mp3", "b.mp3"});
}

TEST_CASE("Each batch reports its place, its successes and its failures", "[batch]") {
    const std::string dir = scratch_dir("callback");
    auto entries = plan_three(dir);
    entries[2].edits = {{"track", "three"}};
    mb::Options options;
    options.batch_size = 2;
    std::vector<mb::BatchSummary> seen;
    options.on_batch = [&seen](const mb::BatchSummary& summary) { seen.push_back(summary); };
    mb::run(entries, options);
    REQUIRE(seen.size() == 2);
    CHECK(seen[0].index == 1);
    CHECK(seen[0].count == 2);
    CHECK(seen[0].ok == 2);
    CHECK(seen[0].failed == 0);
    CHECK(seen[1].index == 2);
    CHECK(seen[1].count == 2);
    CHECK(seen[1].ok == 0);
    CHECK(seen[1].failed == 1);
    fs::remove_all(dir);
}

TEST_CASE("Batches split an uneven plan the way the count says", "[batch]") {
    const std::string dir = scratch_dir("uneven");
    const auto entries = plan_three(dir);
    mb::Options options;
    options.batch_size = 3;
    std::vector<mb::BatchSummary> seen;
    options.on_batch = [&seen](const mb::BatchSummary& summary) { seen.push_back(summary); };
    mb::run(entries, options);
    REQUIRE(seen.size() == 1);
    CHECK(seen[0].count == 1);
    CHECK(seen[0].ok == 3);
    fs::remove_all(dir);
}

TEST_CASE("Failure counts are per batch, not running totals", "[batch]") {
    const std::string dir = scratch_dir("deltas");
    auto entries = plan_three(dir);
    entries[0].edits = {{"track", "bad"}};
    entries[1].edits = {{"track", "bad"}};
    mb::Options options;
    options.batch_size = 1;
    std::vector<mb::BatchSummary> seen;
    options.on_batch = [&seen](const mb::BatchSummary& summary) { seen.push_back(summary); };
    mb::run(entries, options);
    REQUIRE(seen.size() == 3);
    CHECK(seen[0].failed == 1);
    CHECK(seen[1].failed == 1);
    CHECK(seen[2].failed == 0);
    CHECK(seen[2].ok == 1);
    fs::remove_all(dir);
}
