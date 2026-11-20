#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "MetadataPlan.hpp"

using namespace MetadataPlan;

namespace {

Request request(const std::string& source, std::vector<std::pair<std::string, std::string>> sets = {}) {
    Request r;
    r.source = source;
    r.sets = std::move(sets);
    return r;
}

bool contains_text(const std::vector<std::string>& lines, const std::string& needle) {
    for (const auto& line : lines) {
        if (line.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("Natural order compares digit runs as numbers", "[metadata]") {
    REQUIRE(natural_sort({"ch10.mp3", "ch2.mp3", "ch1.mp3"}) ==
            std::vector<std::string>{"ch1.mp3", "ch2.mp3", "ch10.mp3"});
    REQUIRE(natural_sort({"B2", "a10", "a9"}) == std::vector<std::string>{"a9", "a10", "B2"});
    REQUIRE(natural_sort({"track01", "track1"}) == std::vector<std::string>{"track1", "track01"});
    REQUIRE(natural_sort({"x100", "x99", "x100a"}) == std::vector<std::string>{"x99", "x100", "x100a"});
    REQUIRE(natural_sort({}).empty());
}

TEST_CASE("Natural order is a strict ordering", "[metadata]") {
    REQUIRE_FALSE(natural_less("a1", "a1"));
    REQUIRE(natural_less("A", "a"));
    REQUIRE_FALSE(natural_less("a", "A"));
    REQUIRE(natural_less("", "a"));
    REQUIRE_FALSE(natural_less("a", ""));
}

TEST_CASE("Tokens expand from the file context", "[metadata]") {
    TokenContext ctx;
    ctx.n = 3;
    ctx.total = 12;
    ctx.stem = "chapter-three";
    ctx.dir = "My Book";

    REQUIRE(expand_tokens("{n}", ctx) == "3");
    REQUIRE(expand_tokens("{n}/{total}", ctx) == "3/12");
    REQUIRE(expand_tokens("Chapter {n:2}", ctx) == "Chapter 03");
    REQUIRE(expand_tokens("Chapter {n:02}", ctx) == "Chapter 03");
    REQUIRE(expand_tokens("{n:4}", ctx) == "0003");
    REQUIRE(expand_tokens("{stem} from {dir}", ctx) == "chapter-three from My Book");
    REQUIRE(expand_tokens("plain text", ctx) == "plain text");
    ctx.n = 123;
    REQUIRE(expand_tokens("{n:2}", ctx) == "123");  // wider numbers are never cut
}

TEST_CASE("Bad tokens are refused, not guessed", "[metadata]") {
    TokenContext ctx;
    REQUIRE_FALSE(expand_tokens("{title}", ctx).has_value());
    REQUIRE_FALSE(expand_tokens("{n", ctx).has_value());
    REQUIRE_FALSE(expand_tokens("{n:}", ctx).has_value());
    REQUIRE_FALSE(expand_tokens("{n:0}", ctx).has_value());
    REQUIRE_FALSE(expand_tokens("{n:x}", ctx).has_value());
    REQUIRE_FALSE(expand_tokens("{total:2}", ctx).has_value());
    REQUIRE_FALSE(expand_tokens("{}", ctx).has_value());
}

TEST_CASE("Set arguments split at the first equals sign", "[metadata]") {
    REQUIRE(parse_set("artist=A. Author") == std::make_pair(std::string("artist"), std::string("A. Author")));
    REQUIRE(parse_set("comment=a=b") == std::make_pair(std::string("comment"), std::string("a=b")));
    REQUIRE(parse_set("title=") == std::make_pair(std::string("title"), std::string()));
    REQUIRE_FALSE(parse_set("=value").has_value());
    REQUIRE_FALSE(parse_set("novalue").has_value());
    REQUIRE_FALSE(parse_set("").has_value());
}

TEST_CASE("A plan is refused without a source", "[metadata]") {
    for (const char* source : {"", "   ", "\t"}) {
        const auto plan = build_plan({"a.mp3"}, request(source, {{"artist", "X"}}));
        REQUIRE_FALSE(plan.ok());
        REQUIRE(plan.entries.empty());
        REQUIRE(plan.error.find("source") != std::string::npos);
    }
}

TEST_CASE("Numbering follows natural order, not input order", "[metadata]") {
    const auto plan = build_plan({"book/ch10.mp3", "book/ch2.mp3", "book/ch1.mp3"},
                                 request("publisher's cover", {{"track", "{n}/{total}"}, {"title", "Chapter {n:02}"}}));

    REQUIRE(plan.ok());
    REQUIRE(plan.entries.size() == 3);
    REQUIRE(plan.entries[0].path == "book/ch1.mp3");
    REQUIRE(plan.entries[0].set.at("track") == "1/3");
    REQUIRE(plan.entries[0].set.at("title") == "Chapter 01");
    REQUIRE(plan.entries[1].path == "book/ch2.mp3");
    REQUIRE(plan.entries[2].path == "book/ch10.mp3");
    REQUIRE(plan.entries[2].set.at("track") == "3/3");
}

TEST_CASE("Stem and dir come from the path", "[metadata]") {
    const auto plan = build_plan({"/music/Album One/01 Intro.flac", "C:\\music\\Album Two\\02 Song.flac"},
                                 request("tags on the sleeve", {{"album", "{dir}"}, {"title", "{stem}"}}));

    REQUIRE(plan.ok());
    REQUIRE(plan.entries.size() == 2);
    REQUIRE(plan.entries[0].path == "/music/Album One/01 Intro.flac");
    REQUIRE(plan.entries[0].set.at("album") == "Album One");
    REQUIRE(plan.entries[0].set.at("title") == "01 Intro");
    REQUIRE(plan.entries[1].set.at("album") == "Album Two");
    REQUIRE(plan.entries[1].set.at("title") == "02 Song");
}

TEST_CASE("The clear marker removes a tag instead of setting it", "[metadata]") {
    const auto plan = build_plan({"a.mp3"}, request("user", {{"comment", kClearMarker}, {"artist", "Me"}}));

    REQUIRE(plan.ok());
    REQUIRE(plan.entries.size() == 1);
    REQUIRE(plan.entries[0].clear == std::vector<std::string>{"comment"});
    REQUIRE(plan.entries[0].set.count("comment") == 0);
    REQUIRE(plan.entries[0].set.at("artist") == "Me");
}

TEST_CASE("Images are opt-in", "[metadata]") {
    const std::vector<std::string> paths = {"cover.jpg", "ch1.mp3", "scan.PNG"};

    const auto plain = build_plan(paths, request("user", {{"title", "T"}}));
    REQUIRE(plain.entries.size() == 1);
    REQUIRE(plain.entries[0].path == "ch1.mp3");
    REQUIRE(contains_text(plain.skipped, "cover.jpg"));
    REQUIRE(contains_text(plain.skipped, "scan.PNG"));

    Request only = request("user", {{"title", "T"}});
    only.only_extensions = {"jpg", ".png"};
    const auto images = build_plan(paths, only);
    REQUIRE(images.entries.size() == 2);
    REQUIRE(contains_text(images.skipped, "ch1.mp3"));
}

TEST_CASE("The only filter picks extensions, ignoring dots and case", "[metadata]") {
    Request r = request("user", {{"title", "T"}});
    r.only_extensions = {"MP3"};
    const auto plan = build_plan({"a.mp3", "b.m4b", "c.flac"}, r);

    REQUIRE(plan.entries.size() == 1);
    REQUIRE(plan.entries[0].path == "a.mp3");
    REQUIRE(plan.skipped.size() == 2);
}

TEST_CASE("Read-only and unknown formats are skipped with a reason", "[metadata]") {
    const auto plan = build_plan({"old.avi", "show.ts", "song.wma", "raw.aac", "desc.txt", "ok.m4b"},
                                 request("user", {{"title", "T"}}));

    REQUIRE(plan.entries.size() == 1);
    REQUIRE(plan.entries[0].path == "ok.m4b");
    REQUIRE(contains_text(plan.skipped, "old.avi: read-only"));
    REQUIRE(contains_text(plan.skipped, "show.ts: read-only"));
    REQUIRE(contains_text(plan.skipped, "song.wma: read-only"));
    REQUIRE(contains_text(plan.skipped, "raw.aac: read-only"));
    REQUIRE(contains_text(plan.skipped, "desc.txt: not a taggable"));
}

TEST_CASE("Taggable formats include every container the skill names", "[metadata]") {
    const std::vector<std::string> names = {"a.mp3", "a.wav", "a.aiff", "a.flac", "a.ogg", "a.opus",
                                            "a.m4a", "a.m4b", "a.mp4",  "a.mov",  "a.mkv", "a.webm"};
    const auto plan = build_plan(names, request("user", {{"title", "T"}}));
    REQUIRE(plan.entries.size() == names.size());
    REQUIRE(plan.skipped.empty());
}

TEST_CASE("Bad templates and repeated fields fail the whole plan", "[metadata]") {
    const auto bad = build_plan({"a.mp3"}, request("user", {{"title", "{oops}"}}));
    REQUIRE_FALSE(bad.ok());
    REQUIRE(bad.entries.empty());
    REQUIRE(bad.error.find("title") != std::string::npos);

    const auto twice = build_plan({"a.mp3"}, request("user", {{"title", "A"}, {"title", "B"}}));
    REQUIRE_FALSE(twice.ok());
    REQUIRE(twice.error.find("title") != std::string::npos);
}

TEST_CASE("Empty plans say why", "[metadata]") {
    const auto no_sets = build_plan({"a.mp3"}, request("user"));
    REQUIRE(no_sets.ok());
    REQUIRE(no_sets.entries.empty());
    REQUIRE(contains_text(no_sets.warnings, "nothing to change"));

    const auto no_files = build_plan({"notes.txt"}, request("user", {{"title", "T"}}));
    REQUIRE(no_files.ok());
    REQUIRE(no_files.entries.empty());
    REQUIRE(contains_text(no_files.warnings, "no files"));
}

TEST_CASE("CSV parsing handles quotes, doubled quotes, CRLF and embedded newlines", "[metadata]") {
    const auto rows =
        parse_csv("path,title\r\na.mp3,\"Hello, \"\"World\"\"\"\r\nb.mp3,\"two\nlines\"\r\n\r\nc.mp3,\r\n");

    REQUIRE(rows.size() == 4);
    REQUIRE(rows[0] == std::vector<std::string>{"path", "title"});
    REQUIRE(rows[1] == std::vector<std::string>{"a.mp3", "Hello, \"World\""});
    REQUIRE(rows[2] == std::vector<std::string>{"b.mp3", "two\nlines"});
    REQUIRE(rows[3] == std::vector<std::string>{"c.mp3", ""});
    REQUIRE(parse_csv("").empty());
    REQUIRE(parse_csv("a,b").size() == 1);
}

TEST_CASE("CSV plans treat empty cells as no change", "[metadata]") {
    const auto plan = plan_from_csv("path,artist,title,comment\na.mp3,Me,,__clear__\nb.mp3,,Only Title,\nc.mp3,,,\n",
                                    "my spreadsheet");

    REQUIRE(plan.ok());
    REQUIRE(plan.entries.size() == 2);
    REQUIRE(plan.entries[0].path == "a.mp3");
    REQUIRE(plan.entries[0].set.size() == 1);
    REQUIRE(plan.entries[0].set.at("artist") == "Me");
    REQUIRE(plan.entries[0].clear == std::vector<std::string>{"comment"});
    REQUIRE(plan.entries[1].path == "b.mp3");
    REQUIRE(plan.entries[1].set.size() == 1);
    REQUIRE(plan.entries[1].set.at("title") == "Only Title");
    REQUIRE(contains_text(plan.skipped, "c.mp3: no changes"));
}

TEST_CASE("CSV plans are refused for missing source, header or duplicate paths", "[metadata]") {
    REQUIRE_FALSE(plan_from_csv("path,title\na.mp3,T\n", "").ok());
    REQUIRE_FALSE(plan_from_csv("file,title\na.mp3,T\n", "src").ok());
    REQUIRE_FALSE(plan_from_csv("", "src").ok());

    const auto dup = plan_from_csv("path,title\na.mp3,One\na.mp3,Two\n", "src");
    REQUIRE_FALSE(dup.ok());
    REQUIRE(dup.error.find("a.mp3") != std::string::npos);

    REQUIRE(plan_from_csv("PATH,Title\na.mp3,T\n", "src").ok());  // header case does not matter
}
