#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <string>
#include <vector>

#include "SourceLedger.hpp"

using namespace SourceLedger;
using Catch::Approx;

namespace {

const std::string kToday = "2026-11-20";

ResearcherCitations who(const std::string& name, std::vector<std::pair<std::string, std::string>> map) {
    return {name, std::move(map)};
}

}  // namespace

TEST_CASE("Tier names", "[source-ledger]") {
    CHECK(std::string(tier_name(Tier::Tier1)) == "TIER_1");
    CHECK(std::string(tier_name(Tier::Tier2)) == "TIER_2");
    CHECK(std::string(tier_name(Tier::Tier3)) == "TIER_3");
    CHECK(std::string(tier_name(Tier::Tier4)) == "TIER_4");
}

TEST_CASE("host_of strips everything but the host", "[source-ledger]") {
    CHECK(host_of("https://www.Nature.com/articles/x?a=1#top") == "www.nature.com");
    CHECK(host_of("http://user:pw@example.org:8080/p") == "example.org");
    CHECK(host_of("nature.com/articles") == "nature.com");
    CHECK(host_of("https://example.org?q=1") == "example.org");
    CHECK(host_of("https://example.org#frag") == "example.org");
    CHECK(host_of("") == "");
    CHECK(host_of("http://[::1]:8080/x") == "[::1]");
    CHECK(host_of("http://user:p@ss@host.com/x") == "host.com");
}

TEST_CASE("classify_tier recognises the lists", "[source-ledger]") {
    CHECK(classify_tier("https://www.nature.com/articles/abc") == Tier::Tier1);
    CHECK(classify_tier("https://pubmed.ncbi.nlm.nih.gov/123/") == Tier::Tier1);
    CHECK(classify_tier("https://web.mit.edu/x") == Tier::Tier1);
    CHECK(classify_tier("https://www.ox.ac.uk/") == Tier::Tier1);
    CHECK(classify_tier("https://www.gov.uk/guidance") == Tier::Tier1);
    CHECK(classify_tier("https://gov.uk") == Tier::Tier1);
    CHECK(classify_tier("https://arxiv.org/abs/2412.00001") == Tier::Tier1);
    CHECK(classify_tier("https://www.bbc.co.uk/news") == Tier::Tier2);
    CHECK(classify_tier("https://www.reuters.com/x") == Tier::Tier2);
    CHECK(classify_tier("https://ourworldindata.org/x") == Tier::Tier2);
    CHECK(classify_tier("https://old.reddit.com/r/x") == Tier::Tier4);
    CHECK(classify_tier("https://x.com/someone") == Tier::Tier4);
    CHECK(classify_tier("https://blog.medium.com/p") == Tier::Tier4);
    CHECK(classify_tier("https://en.wikipedia.org/wiki/X") == Tier::Tier3);
    CHECK(classify_tier("https://some-blog.example/post") == Tier::Tier3);
}

TEST_CASE("classify_tier cannot be fooled by the path, query or a lookalike host", "[source-ledger]") {
    CHECK(classify_tier("https://example.com/?r=nature.com") == Tier::Tier3);
    CHECK(classify_tier("https://example.com/nature.com/x") == Tier::Tier3);
    CHECK(classify_tier("https://notnature.com/x") == Tier::Tier3);
    CHECK(classify_tier("https://fedex.com/x") == Tier::Tier3);
    CHECK(classify_tier("https://university.education/x") == Tier::Tier3);
    CHECK(classify_tier("https://notgov.uk/x") == Tier::Tier3);
    CHECK(classify_tier("https://example.com/reddit.com") == Tier::Tier3);
}

TEST_CASE("classify_tier takes tier 1 before the low-signal list", "[source-ledger]") {
    // A host that is in a Tier 1 list stays there; the order is the source's.
    CHECK(classify_tier("https://www.nature.com") == Tier::Tier1);
    CHECK(classify_tier("") == Tier::Tier3);
}

TEST_CASE("clean_url removes tracking and normalises", "[source-ledger]") {
    CHECK(clean_url("  HTTPS://Example.com/Path/  ") == "https://example.com/path");
    CHECK(clean_url("https://a.com/x?utm_source=tw&utm_medium=s") == "https://a.com/x");
    CHECK(clean_url("https://a.com/x?id=7&utm_campaign=z&fbclid=abc") == "https://a.com/x?id=7");
    CHECK(clean_url("https://a.com/x?gclid=1") == "https://a.com/x");
    CHECK(clean_url("https://a.com/x?ref=home&id=7") == "https://a.com/x?id=7");
    CHECK(clean_url("https://a.com/x?source=feed&id=7") == "https://a.com/x?id=7");
    CHECK(clean_url("https://a.com/x?UTM_Source=a&id=7") == "https://a.com/x?id=7");
}

TEST_CASE("clean_url only drops tracking keys that match whole", "[source-ledger]") {
    CHECK(clean_url("https://a.com/doc?reference=123") == "https://a.com/doc?reference=123");
    CHECK(clean_url("https://a.com/doc?sourceid=9") == "https://a.com/doc?sourceid=9");
    CHECK(clean_url("https://a.com/doc?reference=1") != clean_url("https://a.com/doc?reference=2"));
}

TEST_CASE("clean_url query details", "[source-ledger]") {
    CHECK(clean_url("https://a.com/x?flag&id=7") == "https://a.com/x?id=7");
    CHECK(clean_url("https://a.com/x?id=&k=v") == "https://a.com/x?k=v");
    CHECK(clean_url("https://a.com/x?a=1&b=2&a=3") == "https://a.com/x?a=1&a=3&b=2");
    CHECK(clean_url("https://a.com/x?id=7#Sec") == "https://a.com/x?id=7#sec");
    CHECK(clean_url("https://a.com/x/?utm_a=1") == clean_url("https://a.com/x"));
    CHECK(clean_url("https://a.com/x/?id=7") == "https://a.com/x?id=7");
    CHECK(clean_url("https://a.com/x#") == "https://a.com/x");
    CHECK(clean_url("") == "");
    CHECK(clean_url("   ") == "");
    CHECK(clean_url("///") == "");
}

TEST_CASE("normalise numbers sources across researchers and merges duplicates", "[source-ledger]") {
    const auto n = normalise({
        who("The Historian", {{"[SOURCE_1]", "https://www.nature.com/a"}, {"[SOURCE_2]", "https://bbc.co.uk/b"}}),
        who("The Scientist",
            {{"[SOURCE_1]", "https://www.nature.com/a?utm_source=x"}, {"[SOURCE_2]", "https://c.example/c"}}),
    });
    REQUIRE(n.sources.size() == 3);
    CHECK(n.sources[0].id == 1);
    CHECK(n.sources[0].url == "https://www.nature.com/a");
    CHECK(n.sources[0].tier == Tier::Tier1);
    CHECK(n.sources[0].found_by == std::vector<std::string>{"The Historian", "The Scientist"});
    CHECK(n.sources[1].id == 2);
    CHECK(n.sources[2].id == 3);
    CHECK(n.sources[2].tier == Tier::Tier3);
    CHECK(n.lookup.at("https://www.nature.com/a") == 1);
    CHECK(n.researcher_maps.at("The Historian").at("[SOURCE_2]") == "[SOURCE_2]");
    CHECK(n.researcher_maps.at("The Scientist").at("[SOURCE_1]") == "[SOURCE_1]");
    CHECK(n.researcher_maps.at("The Scientist").at("[SOURCE_2]") == "[SOURCE_3]");
}

TEST_CASE("normalise flags a source as convergent at three researchers, not two", "[source-ledger]") {
    const std::vector<std::pair<std::string, std::string>> one = {{"[SOURCE_1]", "https://a.org/x"}};
    const auto two = normalise({who("R1", one), who("R2", one)});
    CHECK_FALSE(two.sources[0].convergent);
    CHECK(two.convergent_ids.empty());
    const auto three = normalise({who("R1", one), who("R2", one), who("R3", one)});
    CHECK(three.sources[0].convergent);
    CHECK(three.convergent_ids == std::vector<int>{1});
    CHECK(three.summary.convergent == 1);
}

TEST_CASE("normalise counts a researcher once however often they cite a source", "[source-ledger]") {
    const auto n = normalise({who("R1", {{"[SOURCE_1]", "https://a.org/x"},
                                         {"[SOURCE_2]", "https://a.org/x/?utm_a=1"},
                                         {"[SOURCE_3]", "https://a.org/x"}}),
                              who("R2", {{"[SOURCE_1]", "https://a.org/x"}})});
    REQUIRE(n.sources.size() == 1);
    CHECK(n.sources[0].found_by.size() == 2);
    CHECK_FALSE(n.sources[0].convergent);
    CHECK(n.researcher_maps.at("R1").at("[SOURCE_2]") == "[SOURCE_1]");
    CHECK(n.researcher_maps.at("R1").at("[SOURCE_3]") == "[SOURCE_1]");
}

TEST_CASE("normalise skips empty URLs and keeps empty researchers", "[source-ledger]") {
    const auto n = normalise(
        {who("R1", {{"[SOURCE_1]", ""}, {"[SOURCE_2]", "   "}, {"[SOURCE_3]", "https://a.org"}}), who("R2", {})});
    REQUIRE(n.sources.size() == 1);
    CHECK(n.researcher_maps.at("R1").count("[SOURCE_1]") == 0);
    CHECK(n.researcher_maps.at("R1").at("[SOURCE_3]") == "[SOURCE_1]");
    CHECK(n.researcher_maps.count("R2") == 1);
    CHECK(n.researcher_maps.at("R2").empty());
}

TEST_CASE("normalise summarises tiers", "[source-ledger]") {
    const auto n = normalise({who("R", {{"[SOURCE_1]", "https://nature.com/a"},
                                        {"[SOURCE_2]", "https://bbc.com/b"},
                                        {"[SOURCE_3]", "https://x.example/c"},
                                        {"[SOURCE_4]", "https://reddit.com/d"},
                                        {"[SOURCE_5]", "https://reddit.com/e"}})});
    CHECK(n.summary.total == 5);
    CHECK(n.summary.tier1 == 1);
    CHECK(n.summary.tier2 == 1);
    CHECK(n.summary.tier3 == 1);
    CHECK(n.summary.tier4 == 2);
    CHECK(n.summary.convergent == 0);
}

TEST_CASE("normalise of nothing is empty", "[source-ledger]") {
    const auto n = normalise({});
    CHECK(n.sources.empty());
    CHECK(n.summary.total == 0);
}

TEST_CASE("remap_content replaces whole tags only", "[source-ledger]") {
    const std::map<std::string, std::string> m = {{"[SOURCE_1]", "[SOURCE_7]"}, {"[SOURCE_10]", "[SOURCE_8]"}};
    CHECK(remap_content("a [SOURCE_1] b [SOURCE_10] c", m) == "a [SOURCE_7] b [SOURCE_8] c");
    CHECK(remap_content("[SOURCE_1][SOURCE_1]", m) == "[SOURCE_7][SOURCE_7]");
}

TEST_CASE("remap_content lets the longest key win", "[source-ledger]") {
    CHECK(remap_content("abc ab", {{"ab", "X"}, {"abc", "Y"}}) == "Y X");
}

TEST_CASE("remap_content ignores an empty key", "[source-ledger]") {
    CHECK(remap_content("abc", {{"", "X"}}) == "abc");
}

TEST_CASE("remap_content never replaces a replacement", "[source-ledger]") {
    const std::map<std::string, std::string> m = {{"[SOURCE_1]", "[SOURCE_2]"}, {"[SOURCE_2]", "[SOURCE_5]"}};
    CHECK(remap_content("[SOURCE_1] and [SOURCE_2]", m) == "[SOURCE_2] and [SOURCE_5]");
}

TEST_CASE("remap_content leaves unmapped text alone", "[source-ledger]") {
    CHECK(remap_content("see [SOURCE_9] and [x]", {{"[SOURCE_1]", "[SOURCE_2]"}}) == "see [SOURCE_9] and [x]");
    CHECK(remap_content("anything", {}) == "anything");
    CHECK(remap_content("", {{"[SOURCE_1]", "[SOURCE_2]"}}) == "");
    CHECK(remap_content("[SOURCE_1", {{"[SOURCE_1]", "[SOURCE_2]"}}) == "[SOURCE_1");
}

TEST_CASE("citation_ids_in reads tags in order", "[source-ledger]") {
    CHECK(citation_ids_in("a [SOURCE_3] b [SOURCE_12][SOURCE_3]") == std::vector<int>{3, 12, 3});
    CHECK(citation_ids_in("no tags").empty());
    CHECK(citation_ids_in("[SOURCE_] [SOURCE_x] SOURCE_4 [source_5] [SOURCE_6").empty());
    CHECK(citation_ids_in("[SOURCE_0]") == std::vector<int>{0});
}

TEST_CASE("citation_ids_in takes nine digits and refuses ten", "[source-ledger]") {
    CHECK(citation_ids_in("[SOURCE_123456789]") == std::vector<int>{123456789});
    CHECK(citation_ids_in("[SOURCE_1234567890]").empty());
    CHECK(citation_ids_in("[SOURCE_6x] [SOURCE_7 ]").empty());
}

TEST_CASE("citation_ids_in ignores numbers too large to be ids", "[source-ledger]") {
    CHECK(citation_ids_in("[SOURCE_99999999999999999999] [SOURCE_5]") == std::vector<int>{5});
}

TEST_CASE("unregistered_citation_ids lists each stray id once", "[source-ledger]") {
    const std::string text = "[SOURCE_1] [SOURCE_9] [SOURCE_2] [SOURCE_9] [SOURCE_4]";
    CHECK(unregistered_citation_ids(text, {1, 2, 3}) == std::vector<int>{9, 4});
    CHECK(unregistered_citation_ids(text, {1, 2, 4, 9}).empty());
    CHECK(unregistered_citation_ids("none", {}).empty());
}

TEST_CASE("parse_iso_date is strict", "[source-ledger]") {
    const auto d = parse_iso_date("2024-02-29");
    REQUIRE(d.has_value());
    CHECK(d->year == 2024);
    CHECK(d->month == 2);
    CHECK(d->day == 29);
    CHECK_FALSE(parse_iso_date("2023-02-29").has_value());
    CHECK_FALSE(parse_iso_date("2024-02-30").has_value());
    CHECK_FALSE(parse_iso_date("2024-13-01").has_value());
    CHECK_FALSE(parse_iso_date("2024-00-10").has_value());
    CHECK_FALSE(parse_iso_date("2024-04-31").has_value());
    CHECK_FALSE(parse_iso_date("2024-3-1").has_value());
    CHECK_FALSE(parse_iso_date("2024-03-01x").has_value());
    CHECK_FALSE(parse_iso_date("2024/03/01").has_value());
    CHECK_FALSE(parse_iso_date("2024-03/01").has_value());
    CHECK_FALSE(parse_iso_date("2024/03-01").has_value());
    CHECK_FALSE(parse_iso_date("0000-01-01").has_value());
    CHECK_FALSE(parse_iso_date("2024-0a-01").has_value());
    CHECK_FALSE(parse_iso_date("2024-03-00").has_value());
    CHECK_FALSE(parse_iso_date("2024-00-10").has_value());
    CHECK_FALSE(parse_iso_date("2024-03-0a").has_value());
    CHECK_FALSE(parse_iso_date("20x4-03-01").has_value());
    CHECK(parse_iso_date("0001-01-01").has_value());
    CHECK_FALSE(parse_iso_date("").has_value());
    CHECK_FALSE(parse_iso_date("abcd-ef-gh").has_value());
    CHECK(parse_iso_date("1900-02-28").has_value());
    CHECK_FALSE(parse_iso_date("1900-02-29").has_value());
    CHECK(parse_iso_date("2000-02-29").has_value());
}

TEST_CASE("days_between counts calendar days", "[source-ledger]") {
    CHECK(days_between({2024, 3, 1}, {2024, 3, 1}) == 0);
    CHECK(days_between({2024, 2, 28}, {2024, 3, 1}) == 2);
    CHECK(days_between({2023, 2, 28}, {2023, 3, 1}) == 1);
    CHECK(days_between({2023, 1, 1}, {2024, 1, 1}) == 365);
    CHECK(days_between({2024, 1, 1}, {2025, 1, 1}) == 366);
    CHECK(days_between({2024, 3, 1}, {2024, 2, 1}) == -29);
    CHECK(days_between({1999, 12, 31}, {2000, 1, 1}) == 1);
}

TEST_CASE("extract_source_date reads a path date", "[source-ledger]") {
    CHECK(extract_source_date("https://x.org/2024/03/15/a") == std::optional<std::string>("2024-03-15"));
    CHECK(extract_source_date("https://x.org/2024/03/") == std::optional<std::string>("2024-03-01"));
    CHECK(extract_source_date("https://x.org/2024/02/29") == std::optional<std::string>("2024-02-29"));
    CHECK(extract_source_date("https://x.org/2099/12/31/z") == std::optional<std::string>("2099-12-31"));
}

TEST_CASE("extract_source_date rejects what is not a date", "[source-ledger]") {
    CHECK_FALSE(extract_source_date("https://x.org/a").has_value());
    CHECK_FALSE(extract_source_date("https://x.org/2024/02/30/a").has_value());
    CHECK_FALSE(extract_source_date("https://x.org/2024/13/01").has_value());
    CHECK_FALSE(extract_source_date("https://x.org/1999/05/05").has_value());
    CHECK_FALSE(extract_source_date("https://x.org/2023/1234/z").has_value());
    CHECK_FALSE(extract_source_date("").has_value());
}

TEST_CASE("extract_source_date handles the edges of the pattern", "[source-ledger]") {
    CHECK(extract_source_date("https://x.org/2024/03") == std::optional<std::string>("2024-03-01"));
    CHECK_FALSE(extract_source_date("https://x.org/2024-03-15").has_value());
    CHECK_FALSE(extract_source_date("https://x.org/2199/05/05").has_value());
    CHECK_FALSE(extract_source_date("https://x.org/2124/05/05").has_value());
    CHECK_FALSE(extract_source_date("https://x.org/2024/ab/05").has_value());
    CHECK_FALSE(extract_source_date("https://x.org/x024/05/05").has_value());
    CHECK(extract_source_date("https://x.org/2024/03-15") == std::optional<std::string>("2024-03-01"));
    CHECK(extract_source_date("https://arxiv.org/abs/2412.") == std::optional<std::string>("2024-12-01"));
    CHECK_FALSE(extract_source_date("https://arxiv.org/abs/2412").has_value());
    CHECK_FALSE(extract_source_date("https://arxiv.org/abs/2412x0001").has_value());
}

TEST_CASE("extract_source_date keeps a month when the day is not a clean pair", "[source-ledger]") {
    CHECK(extract_source_date("https://x.org/2024/03/156") == std::optional<std::string>("2024-03-01"));
    CHECK(extract_source_date("https://x.org/2024/03/1") == std::optional<std::string>("2024-03-01"));
}

TEST_CASE("extract_source_date reads arXiv ids", "[source-ledger]") {
    CHECK(extract_source_date("https://arxiv.org/abs/2412.01234") == std::optional<std::string>("2024-12-01"));
    CHECK(extract_source_date("https://arxiv.org/pdf/0704.0001") == std::optional<std::string>("2007-04-01"));
    CHECK_FALSE(extract_source_date("https://arxiv.org/pdf/2413.01234").has_value());
    CHECK_FALSE(extract_source_date("https://arxiv.org/pdf/2400.01234").has_value());
    CHECK_FALSE(extract_source_date("https://arxiv.org/list/2412").has_value());
}

TEST_CASE("a path date wins over an arXiv id", "[source-ledger]") {
    CHECK(extract_source_date("https://arxiv.org/abs/2412.01234?from=https://x.org/2020/05/05/a") ==
          std::optional<std::string>("2020-05-05"));
}

TEST_CASE("an invalid path date falls through to the arXiv id", "[source-ledger]") {
    CHECK(extract_source_date("https://arxiv.org/abs/2412.01234?from=https://x.org/2020/13/05/a") ==
          std::optional<std::string>("2024-12-01"));
}

TEST_CASE("Decay names and parsing", "[source-ledger]") {
    CHECK(std::string(decay_name(Decay::Stable)) == "STABLE");
    CHECK(std::string(decay_name(Decay::Slow)) == "SLOW");
    CHECK(std::string(decay_name(Decay::Medium)) == "MEDIUM");
    CHECK(std::string(decay_name(Decay::Fast)) == "FAST");
    CHECK(std::string(decay_name(Decay::Breaking)) == "BREAKING");
    CHECK(parse_decay("fast") == std::optional<Decay>(Decay::Fast));
    CHECK(parse_decay("BREAKING") == std::optional<Decay>(Decay::Breaking));
    CHECK(parse_decay("Stable") == std::optional<Decay>(Decay::Stable));
    CHECK(parse_decay("slow") == std::optional<Decay>(Decay::Slow));
    CHECK(parse_decay("medium") == std::optional<Decay>(Decay::Medium));
    CHECK_FALSE(parse_decay("").has_value());
    CHECK_FALSE(parse_decay("glacial").has_value());
}

TEST_CASE("Domains map to a default decay class", "[source-ledger]") {
    CHECK(decay_for_domain("SCIENTIFIC_MEDICAL") == Decay::Medium);
    CHECK(decay_for_domain("LEGAL_REGULATORY") == Decay::Fast);
    CHECK(decay_for_domain("TECHNOLOGY_AI") == Decay::Fast);
    CHECK(decay_for_domain("POLITICAL") == Decay::Breaking);
    CHECK(decay_for_domain("ECONOMIC_FINANCIAL") == Decay::Fast);
    CHECK(decay_for_domain("SOCIAL_CULTURAL") == Decay::Slow);
    CHECK(decay_for_domain("GENERAL") == Decay::Medium);
    CHECK(decay_for_domain("HISTORICAL") == Decay::Stable);
    CHECK(decay_for_domain("PHILOSOPHICAL") == Decay::Stable);
    CHECK(decay_for_domain("nonsense") == Decay::Medium);
    CHECK(decay_for_domain("LEGAL_REGULATORY+POLITICAL") == Decay::Medium);
    CHECK(decay_for_domain("") == Decay::Medium);
}

TEST_CASE("Temporal penalty inside the freshness threshold is nil", "[source-ledger]") {
    const auto t = apply_temporal_penalty(0.8, std::string("2026-01-01"), Decay::Fast, kToday);
    CHECK(t.adjusted == Approx(0.8));
    CHECK(t.penalty == Approx(0.0));
    CHECK(t.note == "FAST: within freshness threshold (0.9y old)");
    const auto s = apply_temporal_penalty(0.8, std::string("2010-01-01"), Decay::Stable, kToday);
    CHECK(s.adjusted == Approx(0.8));
    CHECK(s.note == "STABLE: within freshness threshold (16.9y old)");
    const auto slow = apply_temporal_penalty(0.8, std::string("2022-06-15"), Decay::Slow, kToday);
    CHECK(slow.adjusted == Approx(0.8));
    CHECK(slow.note == "SLOW: within freshness threshold (4.4y old)");
}

TEST_CASE("Temporal penalty grows per period past the threshold", "[source-ledger]") {
    auto run = [](const char* date, Decay d) { return apply_temporal_penalty(0.8, std::string(date), d, kToday); };
    CHECK(run("2019-03-01", Decay::Slow).adjusted == Approx(0.7782).margin(1e-4));
    CHECK(run("2019-03-01", Decay::Slow).note == "SLOW: 7.7y old, penalty 2.7%");
    CHECK(run("2022-06-15", Decay::Medium).adjusted == Approx(0.7622).margin(1e-4));
    CHECK(run("2022-06-15", Decay::Medium).note == "MEDIUM: 4.4y old, penalty 4.7%");
    CHECK(run("2010-01-01", Decay::Medium).adjusted == Approx(0.48).margin(1e-4));
    CHECK(run("2024-11-20", Decay::Fast).adjusted == Approx(0.7361).margin(1e-4));
    CHECK(run("2024-11-20", Decay::Fast).note == "FAST: 2.0y old, penalty 8.0%");
    CHECK(run("2022-06-15", Decay::Fast).adjusted == Approx(0.5803).margin(1e-4));
    CHECK(run("2019-03-01", Decay::Fast).note == "FAST: 7.7y old, penalty 50.0%");
    CHECK(run("2026-01-01", Decay::Breaking).adjusted == Approx(0.7539).margin(1e-4));
    CHECK(run("2025-11-20", Decay::Breaking).note == "BREAKING: 1.0y old, penalty 7.5%");
    CHECK(run("2024-11-20", Decay::Breaking).adjusted == Approx(0.6202).margin(1e-4));
}

TEST_CASE("Temporal adjusted values are rounded to four places", "[source-ledger]") {
    auto run = [](const char* date, Decay d) { return apply_temporal_penalty(0.8, std::string(date), d, kToday); };
    CHECK(run("2024-11-20", Decay::Fast).adjusted == 0.7361);
    CHECK(run("2010-01-01", Decay::Medium).adjusted == 0.48);
    CHECK(run("2022-06-15", Decay::Fast).adjusted == 0.5803);
}

TEST_CASE("Temporal penalty stops at each class's cap", "[source-ledger]") {
    const auto f = apply_temporal_penalty(0.8, std::string("2010-01-01"), Decay::Fast, kToday);
    CHECK(f.adjusted == Approx(0.4).margin(1e-4));
    CHECK(f.penalty == Approx(0.5).margin(1e-4));
    CHECK(f.note == "FAST: 16.9y old, penalty 50.0%");
    const auto b = apply_temporal_penalty(0.8, std::string("2019-03-01"), Decay::Breaking, kToday);
    CHECK(b.adjusted == Approx(0.32).margin(1e-4));
}

TEST_CASE("Temporal penalty reports the fraction as well as the note", "[source-ledger]") {
    const auto t = apply_temporal_penalty(1.0, std::string("2022-06-15"), Decay::Fast, kToday);
    CHECK(t.penalty == Approx(0.275).margin(1e-3));
    CHECK(t.adjusted == Approx(0.725).margin(2e-3));
}

TEST_CASE("A future-dated source is treated as fresh", "[source-ledger]") {
    const auto t = apply_temporal_penalty(0.8, std::string("2030-01-01"), Decay::Breaking, kToday);
    CHECK(t.adjusted == Approx(0.8));
    CHECK(t.note == "BREAKING: within freshness threshold (-3.1y old)");
}

TEST_CASE("A missing or broken date costs fifteen percent", "[source-ledger]") {
    const auto none = apply_temporal_penalty(0.8, std::nullopt, Decay::Fast, kToday);
    CHECK(none.adjusted == Approx(0.68));
    CHECK(none.penalty == Approx(0.15));
    CHECK(none.note == "UNKNOWN_DATE: treated as potentially stale (15% penalty)");
    const auto bad = apply_temporal_penalty(0.8, std::string("garbage"), Decay::Fast, kToday);
    CHECK(bad.adjusted == Approx(0.68));
    CHECK(bad.penalty == Approx(0.15));
    CHECK(bad.note == "INVALID_DATE: treated as potentially stale");
    const auto badToday = apply_temporal_penalty(0.8, std::string("2024-01-01"), Decay::Fast, "nope");
    CHECK(badToday.adjusted == Approx(0.68));
    CHECK(badToday.note == "INVALID_DATE: treated as potentially stale");
}

TEST_CASE("Temporal adjusted value is rounded to four places", "[source-ledger]") {
    const auto t = apply_temporal_penalty(0.123456, std::nullopt, Decay::Medium, kToday);
    CHECK(t.adjusted == 0.1049);
}

TEST_CASE("stale_items flags items whose penalty reaches the threshold", "[source-ledger]") {
    const std::vector<std::optional<std::string>> dates = {std::string("2026-06-01"), std::string("2024-11-20"),
                                                           std::string("2022-06-15"), std::nullopt,
                                                           std::string("2010-01-01")};
    const auto stale = stale_items(dates, "TECHNOLOGY_AI", kToday);
    REQUIRE(stale.size() == 2);
    CHECK(stale[0].index == 2);
    CHECK(stale[0].penalty_percent == Approx(27.5));
    CHECK(stale[0].note == "FAST: 4.4y old, penalty 27.5%");
    CHECK(stale[1].index == 4);
    CHECK(stale[1].penalty_percent == Approx(50.0));
}

TEST_CASE("stale_items threshold is strict and unknown dates count at fifteen", "[source-ledger]") {
    const std::vector<std::optional<std::string>> dates = {std::string("2022-06-15")};
    CHECK(stale_items(dates, "TECHNOLOGY_AI", kToday, 27.4).size() == 1);
    CHECK(stale_items(dates, "TECHNOLOGY_AI", kToday, 27.5).empty());
    const std::vector<std::optional<std::string>> unknown = {std::nullopt};
    CHECK(stale_items(unknown, "TECHNOLOGY_AI", kToday).empty());
    CHECK(stale_items(unknown, "TECHNOLOGY_AI", kToday, 14.9).size() == 1);
    CHECK(stale_items(unknown, "TECHNOLOGY_AI", kToday, 15.0).empty());
    CHECK(stale_items({}, "TECHNOLOGY_AI", kToday).empty());
}

TEST_CASE("stale_items uses the domain's decay class", "[source-ledger]") {
    const std::vector<std::optional<std::string>> dates = {std::string("2022-06-15")};
    CHECK(stale_items(dates, "TECHNOLOGY_AI", kToday).size() == 1);
    CHECK(stale_items(dates, "SOCIAL_CULTURAL", kToday).empty());
}

TEST_CASE("registry_temporal reads the URL date when none is given", "[source-ledger]") {
    const auto r = registry_temporal("https://x.org/2020/05/05/a", std::nullopt, "TECHNOLOGY_AI", kToday);
    CHECK(r.source_date == std::optional<std::string>("2020-05-05"));
    CHECK(r.decay == Decay::Fast);
    CHECK(r.penalty == Approx(0.443).margin(1e-4));
    CHECK(r.note == "FAST: 6.5y old, penalty 44.3%");
}

TEST_CASE("registry_temporal charges an undated source", "[source-ledger]") {
    const auto r = registry_temporal("https://x.org/nodate", std::nullopt, "TECHNOLOGY_AI", kToday);
    CHECK_FALSE(r.source_date.has_value());
    CHECK(r.penalty == Approx(0.15));
    CHECK(r.note == "UNKNOWN_DATE: treated as potentially stale (15% penalty)");
}

TEST_CASE("registry_temporal prefers a known date and ignores an empty one", "[source-ledger]") {
    const auto known =
        registry_temporal("https://x.org/2010/01/01/a", std::string("2026-10-01"), "TECHNOLOGY_AI", kToday);
    CHECK(known.source_date == std::optional<std::string>("2026-10-01"));
    CHECK(known.penalty == Approx(0.0));
    const auto empty = registry_temporal("https://x.org/2020/05/05/a", std::string(""), "TECHNOLOGY_AI", kToday);
    CHECK(empty.source_date == std::optional<std::string>("2020-05-05"));
    const auto blend =
        registry_temporal("https://x.org/2020/05/05/a", std::nullopt, "LEGAL_REGULATORY+POLITICAL", kToday);
    CHECK(blend.decay == Decay::Medium);
}

TEST_CASE("Depth names and penalties", "[source-ledger]") {
    CHECK(std::string(depth_name(Depth::Primary)) == "PRIMARY");
    CHECK(std::string(depth_name(Depth::Secondary)) == "SECONDARY");
    CHECK(std::string(depth_name(Depth::Tertiary)) == "TERTIARY");
    CHECK(std::string(depth_name(Depth::QuaternaryPlus)) == "QUATERNARY+");
    CHECK(depth_penalty(Depth::Primary) == 0.0);
    CHECK(depth_penalty(Depth::Secondary) == 0.05);
    CHECK(depth_penalty(Depth::Tertiary) == 0.12);
    CHECK(depth_penalty(Depth::QuaternaryPlus) == 0.22);
}

TEST_CASE("Citation depth follows tier and URL", "[source-ledger]") {
    CHECK(classify_citation_depth("https://arxiv.org/abs/1", Tier::Tier1) == Depth::Primary);
    CHECK(classify_citation_depth("https://nature.com/reviews/x", Tier::Tier1) == Depth::Secondary);
    CHECK(classify_citation_depth("https://nih.gov/Meta-analysis", Tier::Tier1) == Depth::Secondary);
    CHECK(classify_citation_depth("https://nih.gov/systematic", Tier::Tier1) == Depth::Secondary);
    CHECK(classify_citation_depth("https://cochranelibrary.com/x", Tier::Tier1) == Depth::Secondary);
    CHECK(classify_citation_depth("https://bbc.com/news", Tier::Tier2) == Depth::Tertiary);
    CHECK(classify_citation_depth("https://x.example/review", Tier::Tier3) == Depth::Tertiary);
    CHECK(classify_citation_depth("https://reddit.com/x", Tier::Tier4) == Depth::Tertiary);
}

TEST_CASE("propagate_confidence averages the depth penalties", "[source-ledger]") {
    const auto p = propagate_confidence(0.75, {Depth::Tertiary, Depth::Tertiary, Depth::Primary});
    CHECK(p.confidence == Approx(0.69));
    CHECK(p.note == "Citation depth: TERTIARY dominant (avg penalty 8%)");
    CHECK_FALSE(p.chain_deep);
    const auto clean = propagate_confidence(0.9, {Depth::Primary, Depth::Primary});
    CHECK(clean.confidence == Approx(0.9));
    CHECK(clean.note == "Citation depth: PRIMARY dominant (avg penalty 0%)");
}

TEST_CASE("propagate_confidence breaks a tie towards the deeper class", "[source-ledger]") {
    const auto p = propagate_confidence(0.8, {Depth::Primary, Depth::Tertiary});
    CHECK(p.note == "Citation depth: TERTIARY dominant (avg penalty 6%)");
}

TEST_CASE("propagate_confidence flags a deep citation chain", "[source-ledger]") {
    const auto p = propagate_confidence(0.8, {Depth::QuaternaryPlus, Depth::Tertiary});
    CHECK(p.confidence == Approx(0.664));
    CHECK(p.chain_deep);
    CHECK(p.note == "Citation depth: QUATERNARY+ dominant (avg penalty 17%) - CITATION_CHAIN_DEEP flag");
}

TEST_CASE("propagate_confidence with no citations costs twenty percent", "[source-ledger]") {
    const auto p = propagate_confidence(0.5, {});
    CHECK(p.confidence == Approx(0.4));
    CHECK(p.note == "NO_CITATIONS: confidence reduced");
    CHECK_FALSE(p.chain_deep);
}

TEST_CASE("propagate_confidence floors at 0.20 but never raises a score", "[source-ledger]") {
    CHECK(propagate_confidence(0.25, {Depth::QuaternaryPlus}).confidence == Approx(0.2));
    CHECK(propagate_confidence(0.2, {Depth::QuaternaryPlus}).confidence == Approx(0.2));
    CHECK(propagate_confidence(0.1, {Depth::QuaternaryPlus}).confidence == Approx(0.1));
    CHECK(propagate_confidence(0.5, {Depth::QuaternaryPlus}).confidence == Approx(0.39));
}

TEST_CASE("final_weight multiplies what survives each penalty", "[source-ledger]") {
    CHECK(final_weight(0.1, 0.2) == Approx(0.72));
    CHECK(final_weight(0.0, 0.0) == Approx(1.0));
    CHECK(final_weight(0.6, 0.22) == Approx(0.312));
}

// Added after the mutation pass on SourceLedger: calendar edges, path and arXiv dates, citation id lengths.

TEST_CASE("Every month ends where the calendar says, and no later", "[source-ledger]") {
    const int lengths[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    for (int m = 1; m <= 12; ++m) {
        const std::string mm = m < 10 ? "0" + std::to_string(m) : std::to_string(m);
        const std::string last = "2023-" + mm + "-" + std::to_string(lengths[m - 1]);
        const std::string over = "2023-" + mm + "-" + std::to_string(lengths[m - 1] + 1);
        INFO("month " << m);
        CHECK(parse_iso_date(last).has_value());
        CHECK_FALSE(parse_iso_date(over).has_value());
    }
}

TEST_CASE("Leap years follow the Gregorian rule", "[source-ledger]") {
    CHECK(parse_iso_date("2024-02-29").has_value());
    CHECK(parse_iso_date("2000-02-29").has_value());
    CHECK_FALSE(parse_iso_date("2023-02-29").has_value());
    CHECK_FALSE(parse_iso_date("1900-02-29").has_value());
    CHECK_FALSE(parse_iso_date("2100-02-29").has_value());
}

TEST_CASE("Day counts cross century and 400-year boundaries", "[source-ledger]") {
    const auto d = [](const char* s) { return *parse_iso_date(s); };
    CHECK(days_between(d("1970-01-01"), d("2000-01-01")) == 10957);
    CHECK(days_between(d("1600-01-01"), d("2000-01-01")) == 146097);
    CHECK(days_between(d("2000-02-28"), d("2000-03-01")) == 2);
    CHECK(days_between(d("1900-02-28"), d("1900-03-01")) == 1);
    CHECK(days_between(d("2100-02-28"), d("2100-03-01")) == 1);
    CHECK(days_between(d("2024-02-28"), d("2024-03-01")) == 2);
}

TEST_CASE("Path dates read a year, a month and an optional day", "[source-ledger]") {
    CHECK(extract_source_date("https://x.org/2024/05/07/post") == std::optional<std::string>("2024-05-07"));
    CHECK(extract_source_date("https://x.org/2024/05/x") == std::optional<std::string>("2024-05-01"));
    CHECK(extract_source_date("https://x.org/2024/12/31") == std::optional<std::string>("2024-12-31"));
    CHECK_FALSE(extract_source_date("https://x.org/2024/13/01").has_value());
    CHECK_FALSE(extract_source_date("https://x.org/2024/02/30").has_value());
    CHECK_FALSE(extract_source_date("https://x.org/2024/00/10").has_value());
}

TEST_CASE("arXiv identifiers give the month, including January and December", "[source-ledger]") {
    CHECK(extract_source_date("https://arxiv.org/abs/2401.00001") == std::optional<std::string>("2024-01-01"));
    CHECK(extract_source_date("https://arxiv.org/abs/2412.00001") == std::optional<std::string>("2024-12-01"));
    CHECK_FALSE(extract_source_date("https://arxiv.org/abs/2400.00001").has_value());
    CHECK_FALSE(extract_source_date("https://arxiv.org/abs/2413.00001").has_value());
}

TEST_CASE("Citation ids read one to nine digits, and only those", "[source-ledger]") {
    CHECK(citation_ids_in("[SOURCE_7]") == std::vector<int>{7});
    CHECK(citation_ids_in("[SOURCE_12] and [SOURCE_345]") == std::vector<int>{12, 345});
    CHECK(citation_ids_in("[SOURCE_999999999]") == std::vector<int>{999999999});
    CHECK(citation_ids_in("[SOURCE_1234567890]").empty());
    CHECK(citation_ids_in("[SOURCE_12x]").empty());
}

TEST_CASE("A bare low-signal host is low signal, with or without a path", "[source-ledger]") {
    CHECK(classify_tier("https://reddit.com/r/x") == Tier::Tier4);
    CHECK(classify_tier("https://reddit.com") == Tier::Tier4);
}

TEST_CASE("Short and edge-position date strings are read or refused, never misread", "[source-ledger]") {
    CHECK_FALSE(extract_source_date("https://arxiv.org/abs/").has_value());
    CHECK_FALSE(extract_source_date("https://arxiv.org/abs/24").has_value());
    CHECK(extract_source_date("/2024/05/07") == std::optional<std::string>("2024-05-07"));
    CHECK(extract_source_date("/2024/05/") == std::optional<std::string>("2024-05-01"));
}

TEST_CASE("Only the exact ISO shape parses", "[source-ledger]") {
    CHECK_FALSE(parse_iso_date("2024/05/07").has_value());
    CHECK_FALSE(parse_iso_date("2024-05/07").has_value());
    CHECK_FALSE(parse_iso_date("2024-05-07x").has_value());
    CHECK_FALSE(parse_iso_date("202-05-07").has_value());
    CHECK_FALSE(parse_iso_date("2024-5-07").has_value());
    CHECK_FALSE(parse_iso_date("2024-05-").has_value());
    CHECK(parse_iso_date("2024-05-07").has_value());
}

TEST_CASE("A port number is dropped from the host", "[source-ledger]") {
    CHECK(host_of("https://example.com:8080/x") == "example.com");
    CHECK(host_of("https://example.com/x") == "example.com");
}

TEST_CASE("The most common citation depth wins, whichever depth that is", "[source-ledger]") {
    const auto p = propagate_confidence(0.8, {Depth::Secondary, Depth::Secondary, Depth::Primary});
    CHECK(p.note.find("SECONDARY dominant") != std::string::npos);
}

TEST_CASE("Remapping prefers the longest key and keeps equal-length keys in order", "[source-ledger]") {
    CHECK(remap_content("ab", {{"a", "1"}, {"ab", "2"}}) == "2");
    CHECK(remap_content("aa bb", {{"aa", "X"}, {"bb", "Y"}}) == "X Y");
}

TEST_CASE("A host with a leading dot is not a known host", "[source-ledger]") {
    CHECK(classify_tier("https://.arxiv.org/x") == Tier::Tier3);
    CHECK(classify_tier("https://.nature.com/x") == Tier::Tier3);
    CHECK(classify_tier("https://sub.arxiv.org/x") == Tier::Tier1);
}

TEST_CASE("A port that is not all digits stays in the host", "[source-ledger]") {
    CHECK(host_of("https://example.com:80x/p") == "example.com:80x");
    CHECK(host_of("https://example.com:/p") == "example.com");
}

TEST_CASE("ISO dates with a bad digit in the month or day are refused", "[source-ledger]") {
    CHECK_FALSE(parse_iso_date("2024-0x-01").has_value());
    CHECK_FALSE(parse_iso_date("2024-0x-0y").has_value());
    CHECK_FALSE(parse_iso_date("2024-03-1").has_value());
    CHECK_FALSE(parse_iso_date("2024-03-011").has_value());
    CHECK_FALSE(parse_iso_date("2024-1x-01").has_value());
    CHECK_FALSE(parse_iso_date("2024-03-3x").has_value());
    CHECK_FALSE(parse_iso_date("2024-1x-3x").has_value());
}
