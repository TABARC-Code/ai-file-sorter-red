#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "DecisionRegister.hpp"

using namespace DecisionRegister;

namespace {

Entry make_entry(int number, const std::string& trigger = "scanned receipts") {
    Entry e;
    e.number = number;
    e.rule = "Anchor scanned receipts under 330.ECON.";
    e.reason = "The receipt is evidence of spending, not of the scanner.";
    e.trigger_pattern = trigger;
    e.anchor_guidance = "330.ECON, format IMG";
    e.link_guidance = "documents the matching project";
    e.organisation_guidance = "keep with the year folder";
    e.example = "receipt-2024-03.png";
    e.reviewed = "2026-11-20 - second occurrence";
    return e;
}

bool any_has(const std::vector<std::string>& lines, const std::string& needle) {
    for (const auto& line : lines) {
        if (line.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("An edge case earns an entry on its second appearance", "[register]") {
    REQUIRE(kWriteAfterOccurrences == 2);
    REQUIRE_FALSE(should_write_entry(0));
    REQUIRE_FALSE(should_write_entry(1));
    REQUIRE(should_write_entry(2));
    REQUIRE(should_write_entry(5));
}

TEST_CASE("Numbering starts at one and follows the highest", "[register]") {
    REQUIRE(next_number({}) == 1);
    REQUIRE(next_number({make_entry(1), make_entry(7), make_entry(3)}) == 8);
}

TEST_CASE("An entry renders in the template order", "[register]") {
    Entry e = make_entry(7);
    e.supersedes = {3, 4};
    const std::string text = render_entry(e);

    REQUIRE(text.rfind("## Entry 007\n", 0) == 0);
    const char* order[] = {"### Rule",
                           "### Reason",
                           "### Trigger pattern",
                           "### Anchor guidance",
                           "### Link guidance",
                           "### Organisation guidance",
                           "### Example",
                           "### Supersedes",
                           "### Status",
                           "### Reviewed"};
    std::size_t last = 0;
    for (const char* heading : order) {
        const auto at = text.find(heading);
        INFO(heading);
        REQUIRE(at != std::string::npos);
        REQUIRE(at >= last);
        last = at;
    }
    REQUIRE(text.find("### Supersedes\n3, 4\n") != std::string::npos);
    REQUIRE(text.find("### Status\nactive\n") != std::string::npos);
}

TEST_CASE("No superseded entries renders as nothing", "[register]") {
    REQUIRE(render_entry(make_entry(1)).find("### Supersedes\nnothing\n") != std::string::npos);
}

TEST_CASE("A register round-trips, including multi-line text", "[register]") {
    Entry a = make_entry(1);
    a.status = "superseded";
    Entry b = make_entry(2, "mixed folders");
    b.supersedes = {1};
    b.example = "line one\nline two\n\nline four";

    const auto parsed = parse_register(render_register({b, a}));
    REQUIRE(parsed.size() == 2);
    REQUIRE(parsed[0].number == 1);  // sorted by number
    REQUIRE(parsed[0].status == "superseded");
    REQUIRE(parsed[0].supersedes.empty());
    REQUIRE(parsed[1].number == 2);
    REQUIRE(parsed[1].supersedes == std::vector<int>{1});
    REQUIRE(parsed[1].example == "line one\nline two\n\nline four");
    REQUIRE(parsed[1].trigger_pattern == "mixed folders");
    REQUIRE(parsed[0].rule == a.rule);
    REQUIRE(parsed[0].reviewed == a.reviewed);
}

TEST_CASE("A register renders its entries in number order", "[register]") {
    const std::string text = render_register({make_entry(5, "b"), make_entry(2, "a")});
    REQUIRE(text.rfind("# Decision register\n", 0) == 0);
    REQUIRE(text.find("## Entry 002") < text.find("## Entry 005"));
}

TEST_CASE("Parsing sorts entries a person has put out of order", "[register]") {
    const auto parsed = parse_register(render_entry(make_entry(9)) + render_entry(make_entry(2, "other")));
    REQUIRE(parsed.size() == 2);
    REQUIRE(parsed[0].number == 2);
    REQUIRE(parsed[1].number == 9);
}

TEST_CASE("Parsing tolerates junk and drops entries without a number", "[register]") {
    REQUIRE(parse_register("").empty());
    REQUIRE(parse_register("just some notes\n").empty());
    const auto parsed = parse_register("# Decision register\n\nstray text\n\n## Entry abc\n\n### Rule\nx\n\n" +
                                       render_entry(make_entry(4)));
    REQUIRE(parsed.size() == 1);
    REQUIRE(parsed[0].number == 4);
}

TEST_CASE("A valid entry passes validation", "[register]") {
    REQUIRE(validate_entry(make_entry(2), {make_entry(1)}).empty());
}

TEST_CASE("Validation rejects each broken entry", "[register]") {
    const std::vector<Entry> existing = {make_entry(1)};
    auto problems_for = [&](auto mutate) {
        Entry e = make_entry(2);
        mutate(e);
        return validate_entry(e, existing);
    };

    REQUIRE(any_has(problems_for([](Entry& e) { e.rule.clear(); }), "rule"));
    REQUIRE(any_has(problems_for([](Entry& e) { e.reason.clear(); }), "reason"));
    REQUIRE(any_has(problems_for([](Entry& e) { e.trigger_pattern.clear(); }), "trigger"));
    REQUIRE(any_has(problems_for([](Entry& e) { e.rule = "One.\nTwo."; }), "single line"));
    REQUIRE(any_has(problems_for([](Entry& e) { e.status = "paused"; }), "status"));
    REQUIRE(any_has(problems_for([](Entry& e) { e.number = 1; }), "already used"));
    REQUIRE(any_has(problems_for([](Entry& e) { e.number = 0; }), "number"));
    REQUIRE(any_has(problems_for([](Entry& e) { e.supersedes = {9}; }), "9"));
    REQUIRE(any_has(problems_for([](Entry& e) { e.example = "fine\n## Entry 99\nbroken"; }), "heading"));
    REQUIRE(any_has(problems_for([](Entry& e) { e.reason = "a\n### Rule\nb"; }), "heading"));
}

TEST_CASE("Consistency flags two active entries with the same trigger", "[register]") {
    Entry a = make_entry(1, "Scanned   Receipts");
    Entry b = make_entry(2, "scanned receipts");
    b.rule = "Anchor scanned receipts under 000.";
    const auto findings = consistency_pass({a, b});
    REQUIRE(any_has(findings, "1"));
    REQUIRE(any_has(findings, "2"));
    REQUIRE(any_has(findings, "same trigger"));
}

TEST_CASE("Consistency ignores a superseded entry sharing a trigger", "[register]") {
    Entry old_entry = make_entry(1);
    old_entry.status = "superseded";
    Entry fresh = make_entry(2);
    fresh.supersedes = {1};
    REQUIRE(consistency_pass({old_entry, fresh}).empty());
}

TEST_CASE("Consistency flags anchor codes missing from the vocabulary", "[register]") {
    Entry e = make_entry(1);
    e.anchor_guidance = "Use 808.02.ESS.SUB for essays, or 006.3.AI, never 999.";
    const auto findings = consistency_pass({e});
    REQUIRE(any_has(findings, "808.02.ESS.SUB"));
    REQUIRE(any_has(findings, "999"));
    REQUIRE_FALSE(any_has(findings, "006.3.AI"));
}

TEST_CASE("Consistency does not mistake ordinary numbers for codes", "[register]") {
    Entry e = make_entry(1);
    e.anchor_guidance = "Seen in 2024, 1000 files, 100% of cases, version 2.0.0; anchor 330.ECON.";
    REQUIRE(consistency_pass({e}).empty());
}

TEST_CASE("Consistency skips percentages and decimals that merely look like codes", "[register]") {
    Entry e = make_entry(1);
    e.anchor_guidance = "Hit rate 999% last year, released as version 1.999, anchor 330.ECON.";
    REQUIRE(consistency_pass({e}).empty());
    e.anchor_guidance = "Use 999 for this.";
    REQUIRE(consistency_pass({e}).size() == 1);
}

TEST_CASE("Orphan candidates need usage data and a stale trigger", "[register]") {
    Entry fresh = make_entry(1);
    Entry stale = make_entry(2, "old pattern");
    Entry unknown = make_entry(3, "no data");
    Entry retired = make_entry(4, "retired pattern");
    retired.status = "retired";

    const auto orphans = orphan_candidates({fresh, stale, unknown, retired}, {{1, 10}, {2, 400}, {4, 900}}, 365);
    REQUIRE(orphans == std::vector<int>{2});
    REQUIRE(orphan_candidates({stale}, {{2, 365}}, 365) == std::vector<int>{2});
    REQUIRE(orphan_candidates({stale}, {{2, 364}}, 365).empty());
}

TEST_CASE("Superseding marks the old entry and links the new one", "[register]") {
    std::vector<Entry> entries = {make_entry(1)};
    Entry replacement = make_entry(2, "scanned receipts v2");
    std::string error;

    REQUIRE(supersede(entries, 1, replacement, error));
    REQUIRE(error.empty());
    REQUIRE(entries.size() == 2);
    REQUIRE(entries[0].status == "superseded");
    REQUIRE(entries[1].supersedes == std::vector<int>{1});
    REQUIRE(entries[1].status == "active");
}

TEST_CASE("Superseding refuses bad requests and changes nothing", "[register]") {
    std::vector<Entry> entries = {make_entry(1)};
    entries[0].status = "retired";
    std::string error;

    REQUIRE_FALSE(supersede(entries, 1, make_entry(2, "x"), error));
    REQUIRE_FALSE(error.empty());
    REQUIRE(entries.size() == 1);

    entries[0].status = "active";
    REQUIRE_FALSE(supersede(entries, 5, make_entry(2, "x"), error));
    REQUIRE(entries[0].status == "active");

    Entry bad = make_entry(2, "x");
    bad.rule.clear();
    REQUIRE_FALSE(supersede(entries, 1, bad, error));
    REQUIRE(entries.size() == 1);
    REQUIRE(entries[0].status == "active");
}
