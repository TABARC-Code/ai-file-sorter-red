#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "LinkAudit.hpp"

using namespace LinkAudit;

namespace {

const char* const kData = "wound/lie/flaw chain and ensemble friction pairs";
const char* const kAction = "Cross-reference received beat positions against the arc stage map";

std::string block(char dir, const std::string& other, const std::string& data = kData,
                  const std::string& action = kAction, const std::string& feedback = "none") {
    const bool out = dir == '>';
    return std::string("### ") + dir + " " + other + "\n\n" + (out ? "**Sends:** " : "**Receives:** ") + data + "\n" +
           (out ? "**Action for target:** " : "**Action here:** ") + action + "\n**Feedback loop:** " + feedback +
           "\n\n";
}

SkillFile file(const std::string& skill, const std::string& text, const std::string& name = "SKILL.md") {
    return {skill, skill + "/" + name, text};
}

std::vector<SkillFile> clean_pair() {
    return {file("a", "## Skill relationships\n\n" + block('>', "b")),
            file("b", "## Skill relationships\n\n" + block('<', "a"))};
}

std::vector<Finding> with_code(const Report& report, const std::string& code) {
    std::vector<Finding> out;
    for (const auto& f : report.findings) {
        if (f.code == code) {
            out.push_back(f);
        }
    }
    return out;
}

std::vector<Contract> contracts_of(const std::vector<SkillFile>& files) {
    std::vector<Contract> all;
    for (const auto& f : files) {
        for (auto& c : parse_contracts(f)) {
            all.push_back(std::move(c));
        }
    }
    return all;
}

bool has(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

}  // namespace

// ---- parsing -------------------------------------------------------------------------------------------------

TEST_CASE("Outbound and inbound blocks parse with every field and the heading line", "[links]") {
    const std::string text =
        "# Skill\n\nprose\n\n" + block('<', "src/sub", "inbound data here", "do the inbound thing now", "none") +
        block('>', "`tgt`", "outbound data here", "do the outbound thing now", "If x fails, return y to `tgt`");
    const auto parsed = parse_contracts(file("me", text));
    REQUIRE(parsed.size() == 2);

    REQUIRE(parsed[0].direction == Direction::Inbound);
    REQUIRE(parsed[0].skill == "me");
    REQUIRE(parsed[0].file == "me/SKILL.md");
    REQUIRE(parsed[0].line == 5);
    REQUIRE(parsed[0].other == "src/sub");
    REQUIRE(other_skill(parsed[0]) == "src");
    REQUIRE(parsed[0].data == "inbound data here");
    REQUIRE(parsed[0].action == "do the inbound thing now");
    REQUIRE(parsed[0].feedback == "none");

    REQUIRE(parsed[1].direction == Direction::Outbound);
    REQUIRE(parsed[1].other == "tgt");
    REQUIRE(parsed[1].line == 11);
    REQUIRE(parsed[1].data == "outbound data here");
    REQUIRE(parsed[1].feedback == "If x fails, return y to `tgt`");
}

TEST_CASE("Contract-shaped text inside code fences is documentation, not a contract", "[links]") {
    const std::string text =
        "```markdown\n" + block('>', "fake") + "```\n~~~\n" + block('<', "fake2") + "~~~\n" + block('>', "real");
    const auto parsed = parse_contracts(file("me", text));
    REQUIRE(parsed.size() == 1);
    REQUIRE(parsed[0].other == "real");
}

TEST_CASE("A field continues over following lines until a blank line or the next label", "[links]") {
    const std::string text =
        "### > b\n\n**Sends:** first part\nsecond part\n**Action for target:** act\n\n**Feedback loop:** none\n";
    const auto parsed = parse_contracts(file("a", text));
    REQUIRE(parsed.size() == 1);
    REQUIRE(parsed[0].data == "first part second part");
    REQUIRE(parsed[0].action == "act");
    REQUIRE(parsed[0].feedback == "none");
}

TEST_CASE("A block stops at the next heading and labels for the wrong direction do not count", "[links]") {
    const std::string text = "### > b\n\n**Receives:** wrong label\n## Next section\n**Sends:** belongs to nobody\n";
    const auto parsed = parse_contracts(file("a", text));
    REQUIRE(parsed.size() == 1);
    REQUIRE(parsed[0].data.empty());
    REQUIRE(parsed[0].action.empty());
    REQUIRE(parsed[0].feedback.empty());
}

TEST_CASE("Headings with no name and ordinary headings are not contracts", "[links]") {
    REQUIRE(parse_contracts(file("a", "### >\n\n### <   \n\n### Called by\n\n#### > deeper\n")).empty());
}

// ---- field rules ---------------------------------------------------------------------------------------------

TEST_CASE("Placeholders are empty, TBD, TODO or unfilled template text", "[links]") {
    for (const char* v :
         {"", "   ", "TBD", "tbd.", "TODO", "[fill in]", "[specific data this skill produces for the target]"}) {
        INFO(v);
        REQUIRE(is_placeholder(v));
    }
    for (const char* v :
         {"none", "wound chain", "TBD later but with real words after", "[x] done and dusted", "[a] and [b]"}) {
        INFO(v);
        REQUIRE_FALSE(is_placeholder(v));
    }
}

TEST_CASE("Data fields are vague when one or two words end in a generic noun", "[links]") {
    for (const char* v : {"data", "Information", "output.", "character data", "structural analysis", "some results"}) {
        INFO(v);
        REQUIRE(is_vague_data(v));
    }
    for (const char* v : {"wound/lie/flaw chain", "beat-by-beat severity ratings", "arc type and complexity level",
                          "character arc data", "scene map"}) {
        INFO(v);
        REQUIRE_FALSE(is_vague_data(v));
    }
}

TEST_CASE("Actions are vague under three words or when they are stock non-instructions", "[links]") {
    for (const char* v : {"integrate", "Use it.", "use as needed", "Use this to inform the analysis", "as needed"}) {
        INFO(v);
        REQUIRE(is_vague_action(v));
    }
    REQUIRE_FALSE(is_vague_action("Flag any beat that serves the wrong stage as a Medium misalignment"));
    REQUIRE_FALSE(is_vague_action("Reject the draft outright"));
    REQUIRE_FALSE(is_vague_action("Reject the draft"));
}

TEST_CASE("A feedback loop is none or a named trigger", "[links]") {
    REQUIRE(feedback_is_valid("none", "b"));
    REQUIRE(feedback_is_valid("None.", "b"));
    REQUIRE(feedback_is_valid("If score drops below 70%, return the three lowest beats to `novel/05-arc`", "b"));
    REQUIRE(feedback_is_valid("When b rejects the output, resend with the failing ids attached", "b"));
    REQUIRE(feedback_is_valid("If it fails return the report to plan/arc-design now", "x"));
    REQUIRE_FALSE(feedback_is_valid("Return feedback if issues found", "b"));
    REQUIRE_FALSE(feedback_is_valid("feedback as needed", "b"));
    REQUIRE_FALSE(feedback_is_valid("", "b"));
    REQUIRE_FALSE(feedback_is_valid("to b", "b"));
    REQUIRE(feedback_is_valid("On failure resend `fixer`", "b"));
    REQUIRE_FALSE(feedback_is_valid("resend to `fixer`", "b"));
}

// ---- audit ---------------------------------------------------------------------------------------------------

TEST_CASE("A matched pair passes with nothing to report", "[links]") {
    const Report r = audit(clean_pair());
    REQUIRE(r.checked == 2);
    REQUIRE(r.passed == 2);
    REQUIRE(r.failed == 0);
    REQUIRE(r.findings.empty());
}

TEST_CASE("A forward block with no backlink is Critical and fails only that block", "[links]") {
    const Report r = audit({file("a", block('>', "b")), file("b", "just prose\n")});
    const auto hits = with_code(r, "missing-backlink");
    REQUIRE(hits.size() == 1);
    REQUIRE(hits[0].severity == Severity::Critical);
    REQUIRE(hits[0].file == "a/SKILL.md");
    REQUIRE(hits[0].line == 1);
    REQUIRE(hits[0].on_contract);
    REQUIRE(has(hits[0].message, "a > b"));
    REQUIRE(has(hits[0].message, "b has no < a"));
    REQUIRE(r.checked == 1);
    REQUIRE(r.failed == 1);
    REQUIRE(r.passed == 0);
}

TEST_CASE("A backlink with no forward block is Critical", "[links]") {
    const Report r = audit({file("a", "just prose\n"), file("b", block('<', "a"))});
    const auto hits = with_code(r, "missing-forward");
    REQUIRE(hits.size() == 1);
    REQUIRE(hits[0].severity == Severity::Critical);
    REQUIRE(hits[0].file == "b/SKILL.md");
    REQUIRE(has(hits[0].message, "b < a"));
    REQUIRE(has(hits[0].message, "a has no > b"));
}

TEST_CASE("A reference to a skill outside the audited scope is Critical", "[links]") {
    const Report r = audit({file("a", block('>', "ghost")), file("b", block('<', "phantom"))});
    REQUIRE(with_code(r, "missing-target").size() == 1);
    REQUIRE(with_code(r, "missing-source").size() == 1);
    REQUIRE(with_code(r, "missing-target")[0].severity == Severity::Critical);
    REQUIRE(with_code(r, "missing-target")[0].file == "a/SKILL.md");
    REQUIRE(with_code(r, "missing-source")[0].file == "b/SKILL.md");
}

TEST_CASE("Matching is by whole skill name, and sub-skill suffixes are allowed", "[links]") {
    const Report near_miss = audit({file("a", block('>', "b")), file("b", block('<', "ab")), file("ab", "prose")});
    REQUIRE(with_code(near_miss, "missing-backlink").size() == 1);

    const Report sub =
        audit({file("a", block('>', "b/deep"), "one.md"), file("b", block('<', "a/other"), "inner/two.md")});
    REQUIRE(sub.findings.empty());
    REQUIRE(sub.passed == 2);
}

TEST_CASE("Each direction of a bidirectional link is its own pair of blocks", "[links]") {
    const std::vector<SkillFile> files = {file("a", block('<', "b") + block('>', "b")),
                                          file("b", block('<', "a") + block('>', "a"))};
    const Report r = audit(files);
    REQUIRE(r.checked == 4);
    REQUIRE(r.passed == 4);

    const Report one_sided = audit({file("a", block('<', "b") + block('>', "b")), file("b", block('<', "a"))});
    REQUIRE(with_code(one_sided, "missing-backlink").empty());
    const auto missing = with_code(one_sided, "missing-forward");
    REQUIRE(missing.size() == 1);
    REQUIRE(missing[0].file == "a/SKILL.md");
}

TEST_CASE("Empty and placeholder fields are High and name the field", "[links]") {
    const Report r =
        audit({file("a", block('>', "b", "TBD", "[fill in]", "")), file("b", block('<', "a", "", "TODO", "none"))});
    const auto empties = with_code(r, "empty-field");
    REQUIRE(empties.size() == 5);
    for (const auto& f : empties) {
        REQUIRE(f.severity == Severity::High);
        REQUIRE(f.on_contract);
    }
    REQUIRE(has(empties[0].message, "Sends"));
    REQUIRE(r.failed == 2);
}

TEST_CASE("Vague fields are Medium and a vague feedback loop is Medium", "[links]") {
    const Report r =
        audit({file("a", block('>', "b", "character data", "use as needed", "Return feedback if issues found")),
               file("b", block('<', "a"))});
    REQUIRE(with_code(r, "vague-field").size() == 2);
    REQUIRE(with_code(r, "bad-feedback").size() == 1);
    for (const auto& f : r.findings) {
        REQUIRE(f.severity == Severity::Medium);
    }
    REQUIRE(r.failed == 1);
    REQUIRE(r.passed == 1);
}

TEST_CASE("A skill named in a See also line with no contract is Medium", "[links]") {
    const Report r = audit({file("a", "See also: b for more\n"), file("b", "prose\n")});
    const auto hits = with_code(r, "see-also");
    REQUIRE(hits.size() == 1);
    REQUIRE(hits[0].severity == Severity::Medium);
    REQUIRE(hits[0].line == 1);
    REQUIRE_FALSE(hits[0].on_contract);
    REQUIRE(with_code(r, "prose-mention").empty());
    REQUIRE(r.failed == 0);  // a See also line is not a contract block
}

TEST_CASE("A skill named in prose with no contract is Low, once per file", "[links]") {
    const Report r = audit({file("a", "intro\nWe lean on b here.\nAnd b again.\n"), file("b", "prose\n")});
    const auto hits = with_code(r, "prose-mention");
    REQUIRE(hits.size() == 1);
    REQUIRE(hits[0].severity == Severity::Low);
    REQUIRE(hits[0].line == 2);
}

TEST_CASE("Prose mentions respect whole names, code fences and existing contracts", "[links]") {
    const Report r = audit({file("a", "b-extra and xb and `b`.\n```\nuse b here\n```\n"), file("b", "prose\n"),
                            file("c", "mentions a.\n" + block('>', "a")), file("a2", block('<', "c"))});
    const auto hits = with_code(r, "prose-mention");
    std::size_t in_a = 0;
    for (const auto& f : hits) {
        in_a += f.file == "a/SKILL.md" ? 1 : 0;
    }
    REQUIRE(in_a == 1);  // only the backticked `b` counts as a whole-name mention
    for (const auto& f : hits) {
        REQUIRE(f.file != "c/SKILL.md");  // c has a contract to a
    }
}

TEST_CASE("A skill with no contracts in either direction is an orphan", "[links]") {
    auto files = clean_pair();
    files.push_back(file("loner", "nothing here\n"));
    const Report r = audit(files);
    const auto hits = with_code(r, "orphan-skill");
    REQUIRE(hits.size() == 1);
    REQUIRE(hits[0].severity == Severity::Low);
    REQUIRE(has(hits[0].message, "loner"));
}

TEST_CASE("Findings come most severe first, then by file and line", "[links]") {
    const Report r = audit({file("a", block('>', "b", "TBD") + "See also: c\n"), file("b", "prose\n"),
                            file("c", "prose\n"), file("z", "orphan\n")});
    std::vector<Severity> got;
    for (const auto& f : r.findings) {
        got.push_back(f.severity);
    }
    const std::vector<Severity> want = {Severity::Critical, Severity::High, Severity::Medium, Severity::Low,
                                        Severity::Low};
    REQUIRE(got == want);
    REQUIRE(r.findings[3].file == "c/SKILL.md");  // equal severity: by file
    REQUIRE(r.findings[4].file == "z/SKILL.md");
}

TEST_CASE("A skill that another skill's contract names is not an orphan", "[links]") {
    const Report r = audit({file("a", block('>', "b")), file("b", "prose\n")});
    REQUIRE(with_code(r, "orphan-skill").empty());
}

TEST_CASE("An empty scope audits to nothing", "[links]") {
    const Report r = audit({});
    REQUIRE(r.checked == 0);
    REQUIRE(r.findings.empty());
}

TEST_CASE("Audits are repeatable", "[links]") {
    const auto files = std::vector<SkillFile>{file("a", block('>', "b", "TBD")), file("b", "See also: a\n")};
    const Report one = audit(files);
    const Report two = audit(files);
    REQUIRE(one.findings.size() == two.findings.size());
    for (std::size_t i = 0; i < one.findings.size(); ++i) {
        REQUIRE(one.findings[i].message == two.findings[i].message);
    }
}

// ---- report --------------------------------------------------------------------------------------------------

TEST_CASE("The report follows the skill's layout", "[links]") {
    const Report r = audit({file("a", block('>', "b")), file("b", "prose\n")});
    const std::string text = render_report(r, "a, b", "2026-11-20");
    REQUIRE(text.rfind("## Bidirectional Link Audit Report\n", 0) == 0);
    REQUIRE(has(text, "Scope: a, b\n"));
    REQUIRE(has(text, "Method: Link-matching\n"));
    REQUIRE(has(text, "Date: 2026-11-20\n"));
    REQUIRE(has(text, "### Summary\n- Total contracts checked: 1\n- Passed: 0\n- Failed: 1\n- Pass rate: 0.0%\n"));
    REQUIRE(has(text, "### Critical failures\n[a/SKILL.md:1] "));
    REQUIRE(has(text, "### High failures\nnone\n"));
    REQUIRE(has(text, "### Medium failures\nnone\n"));
    REQUIRE(has(text, "### Low findings\n"));
}

TEST_CASE("Pass rate is a percentage to one place, and n/a with nothing to check", "[links]") {
    REQUIRE(has(render_report(audit(clean_pair()), "s", "d"), "- Pass rate: 100.0%\n"));
    const Report third =
        audit({file("a", block('>', "b") + block('>', "c")), file("b", block('<', "a")), file("c", "p")});
    REQUIRE(has(render_report(third, "s", "d"), "- Pass rate: 66.7%\n"));
    REQUIRE(has(render_report(audit({}), "s", "d"), "- Pass rate: n/a\n"));
}

TEST_CASE("Three failed cycles hand over to a human", "[links]") {
    REQUIRE(kMaxAuditCycles == 3);
    REQUIRE_FALSE(must_escalate(0));
    REQUIRE_FALSE(must_escalate(2));
    REQUIRE(must_escalate(3));
    REQUIRE(must_escalate(4));
}

// ---- graph ---------------------------------------------------------------------------------------------------

TEST_CASE("The adjacency matrix shows which ends exist", "[links]") {
    const auto cells = adjacency(contracts_of({file("a", block('>', "b") + block('>', "c")), file("b", block('<', "a")),
                                               file("d", block('<', "a")), file("c", "x")}));
    REQUIRE(cells.at({"a", "b"}).forward);
    REQUIRE(cells.at({"a", "b"}).backlink);
    REQUIRE(cells.at({"a", "c"}).forward);
    REQUIRE_FALSE(cells.at({"a", "c"}).backlink);
    REQUIRE_FALSE(cells.at({"a", "d"}).forward);
    REQUIRE(cells.at({"a", "d"}).backlink);
    REQUIRE(cells.count({"b", "a"}) == 0);
}

TEST_CASE("Chains of three or more skills are found, shorter ones are not", "[links]") {
    const auto two = contracts_of({file("a", block('>', "b")), file("b", block('<', "a"))});
    REQUIRE(find_chains(two).empty());

    const auto three =
        contracts_of({file("a", block('>', "b")), file("b", block('>', "c")), file("c", block('<', "b"))});
    const auto chains = find_chains(three);
    REQUIRE(chains.size() == 1);
    REQUIRE(chains[0].skills == std::vector<std::string>{"a", "b", "c"});
    REQUIRE_FALSE(chains[0].loops_back);
}

TEST_CASE("Only maximal chains are listed and branches are separate chains", "[links]") {
    const auto contracts = contracts_of({file("a", block('>', "b")), file("b", block('>', "c") + block('>', "d")),
                                         file("c", block('>', "e")), file("d", "x"), file("e", "x")});
    const auto chains = find_chains(contracts);
    REQUIRE(chains.size() == 2);
    REQUIRE(chains[0].skills == std::vector<std::string>{"a", "b", "c", "e"});
    REQUIRE(chains[1].skills == std::vector<std::string>{"a", "b", "d"});
}

TEST_CASE("A loop is reported once and marked", "[links]") {
    const auto contracts =
        contracts_of({file("a", block('>', "b")), file("b", block('>', "c")), file("c", block('>', "a"))});
    const auto chains = find_chains(contracts);
    REQUIRE(chains.size() == 1);
    REQUIRE(chains[0].loops_back);
    REQUIRE(chains[0].skills == std::vector<std::string>{"a", "b", "c"});
}

TEST_CASE("Chain search is capped", "[links]") {
    std::vector<SkillFile> files;
    files.push_back(file("s", block('>', "m1") + block('>', "m2") + block('>', "m3") + block('>', "m4")));
    for (const char* m : {"m1", "m2", "m3", "m4"}) {
        files.push_back(file(m, block('>', "t1") + block('>', "t2") + block('>', "t3")));
    }
    for (const char* t : {"t1", "t2", "t3"}) {
        files.push_back(file(t, "x"));
    }
    REQUIRE(find_chains(contracts_of(files)).size() == 12);
    REQUIRE(find_chains(contracts_of(files), 5).size() == 5);
}

// ---- unwiring ------------------------------------------------------------------------------------------------

TEST_CASE("The blast radius sorts every outside reference into its category", "[links]") {
    const std::vector<SkillFile> files = {file("old", block('>', "x") + "old mentions itself\n"),
                                          file("takes", block('<', "old")),
                                          file("gives", block('>', "old/sub")),
                                          file("chained", "Data originated from old, transformed here.\n"),
                                          file("chainy", "this sits in a chain through old\n"),
                                          file("plain", "we used to rely on old.\n"),
                                          file("other", "old-school and bold are not it\n")};
    const auto r = blast_radius(files, "old");
    REQUIRE(r.receivers.size() == 1);
    REQUIRE(r.receivers[0].skill == "takes");
    REQUIRE(r.senders.size() == 1);
    REQUIRE(r.senders[0].skill == "gives");
    REQUIRE(r.chain_annotations.size() == 2);
    REQUIRE(r.prose.size() == 1);
    REQUIRE(r.prose[0].skill == "plain");
    REQUIRE(r.prose[0].line == 1);

    const auto order = r.in_update_order();
    REQUIRE(order.size() == 5);
    REQUIRE(order[0].skill == "takes");
    REQUIRE(order[1].skill == "gives");
    REQUIRE(order[4].skill == "plain");
}

TEST_CASE("Nothing refers to a skill nobody mentions", "[links]") {
    const auto r = blast_radius(clean_pair(), "elsewhere");
    REQUIRE(r.in_update_order().empty());
}

TEST_CASE("A replacement is wired only when it has contracts and none are Critical or High", "[links]") {
    REQUIRE(replacement_is_wired(clean_pair(), "b"));
    REQUIRE_FALSE(replacement_is_wired({file("b", "no contracts")}, "b"));
    REQUIRE_FALSE(replacement_is_wired({file("a", "prose"), file("b", block('<', "a"))}, "b"));
    REQUIRE_FALSE(replacement_is_wired({file("a", block('>', "b")), file("b", block('<', "a", "TBD"))}, "b"));
    REQUIRE(replacement_is_wired({file("a", block('>', "b")), file("b", block('<', "a", "character data"))}, "b"));
}

// ---- taxonomy ------------------------------------------------------------------------------------------------

TEST_CASE("The eight link types carry their weights", "[links]") {
    REQUIRE(weight(LinkType::TechnicalDependency) == 10);
    REQUIRE(weight(LinkType::SupportingWorkflow) == 8);
    REQUIRE(weight(LinkType::QualityControl) == 8);
    REQUIRE(weight(LinkType::SharedDomain) == 6);
    REQUIRE(weight(LinkType::OutputStyle) == 6);
    REQUIRE(weight(LinkType::SimilarFunction) == 4);
    REQUIRE(weight(LinkType::CreativeInfluence) == 3);
    REQUIRE(weight(LinkType::UserDefined, 7) == 7);
    REQUIRE(weight(LinkType::SharedDomain, 99) == 6);
}

TEST_CASE("Type names round trip and unknown names are rejected", "[links]") {
    for (const auto type :
         {LinkType::TechnicalDependency, LinkType::SupportingWorkflow, LinkType::QualityControl, LinkType::SharedDomain,
          LinkType::OutputStyle, LinkType::SimilarFunction, LinkType::CreativeInfluence, LinkType::UserDefined}) {
        REQUIRE(parse_type(type_name(type)) == type);
    }
    REQUIRE(type_name(LinkType::QualityControl) == "quality-control");
    REQUIRE(parse_type("Technical-Dependency") == LinkType::TechnicalDependency);
    REQUIRE(parse_type("  output-style \n") == LinkType::OutputStyle);
    REQUIRE_FALSE(parse_type("friendship").has_value());
}

TEST_CASE("The heavier type wins and a tie decides nothing", "[links]") {
    REQUIRE(heavier(LinkType::TechnicalDependency, LinkType::CreativeInfluence) == LinkType::TechnicalDependency);
    REQUIRE(heavier(LinkType::CreativeInfluence, LinkType::TechnicalDependency) == LinkType::TechnicalDependency);
    REQUIRE_FALSE(heavier(LinkType::SupportingWorkflow, LinkType::QualityControl).has_value());
    REQUIRE(heavier(LinkType::UserDefined, LinkType::SharedDomain, 9, 0) == LinkType::UserDefined);
    REQUIRE(heavier(LinkType::UserDefined, LinkType::SharedDomain, 2, 0) == LinkType::SharedDomain);
}

// ---- writing contracts ---------------------------------------------------------------------------------------

TEST_CASE("A contract renders in the exact block format", "[links]") {
    Contract c;
    c.direction = Direction::Outbound;
    c.other = "b/sub";
    c.data = kData;
    c.action = kAction;
    c.feedback = "none";
    REQUIRE(render_contract(c) == block('>', "b/sub"));

    c.direction = Direction::Inbound;
    c.other = "a";
    REQUIRE(render_contract(c) == block('<', "a"));
}

TEST_CASE("A contract with placeholder or vague fields is not written", "[links]") {
    Contract c;
    c.other = "b";
    c.data = kData;
    c.action = kAction;
    c.feedback = "none";
    REQUIRE(render_contract(c).has_value());
    Contract bad = c;
    bad.data = "TBD";
    REQUIRE_FALSE(render_contract(bad).has_value());
    bad = c;
    bad.data = "character data";
    REQUIRE_FALSE(render_contract(bad).has_value());
    bad = c;
    bad.action = "integrate";
    REQUIRE_FALSE(render_contract(bad).has_value());
    bad = c;
    bad.feedback = "feedback as needed";
    REQUIRE_FALSE(render_contract(bad).has_value());
    bad = c;
    bad.other = "";
    REQUIRE_FALSE(render_contract(bad).has_value());
}

TEST_CASE("A field that spans lines is not written, it would corrupt the block", "[links]") {
    Contract c;
    c.other = "b";
    c.data = kData;
    c.action = kAction;
    c.feedback = "none";
    c.data = "first line\n### > injected";
    REQUIRE_FALSE(render_contract(c).has_value());
    c.data = kData;
    c.action = "do this\nthen that now";
    REQUIRE_FALSE(render_contract(c).has_value());
    c.action = kAction;
    c.feedback = "none\n";
    REQUIRE_FALSE(render_contract(c).has_value());
}

TEST_CASE("A written contract parses back to the same fields", "[links]") {
    Contract c;
    c.direction = Direction::Inbound;
    c.other = "src/sub";
    c.data = kData;
    c.action = kAction;
    c.feedback = "If the map is incomplete, return the gaps to `src/sub` for repair";
    const auto parsed = parse_contracts(file("me", *render_contract(c)));
    REQUIRE(parsed.size() == 1);
    REQUIRE(parsed[0].other == c.other);
    REQUIRE(parsed[0].data == c.data);
    REQUIRE(parsed[0].action == c.action);
    REQUIRE(parsed[0].feedback == c.feedback);
}

TEST_CASE("The relationships section puts inbound first and sorts each group", "[links]") {
    auto make = [](Direction d, const std::string& other) {
        Contract c;
        c.direction = d;
        c.other = other;
        c.data = kData;
        c.action = kAction;
        c.feedback = "none";
        return c;
    };
    const auto text = render_relationships_section(
        {make(Direction::Outbound, "zed"), make(Direction::Inbound, "mid"), make(Direction::Outbound, "alpha"),
         make(Direction::Inbound, "beta"), make(Direction::Outbound, "mid")});
    REQUIRE(text.has_value());
    REQUIRE(text->rfind("## Skill relationships\n\n", 0) == 0);
    const auto at = [&](const std::string& heading) { return text->find(heading); };
    REQUIRE(at("### < beta") < at("### < mid"));
    REQUIRE(at("### < mid") < at("### > alpha"));
    REQUIRE(at("### > alpha") < at("### > mid"));
    REQUIRE(at("### > mid") < at("### > zed"));
}

TEST_CASE("A section is refused if any block in it is unwritable", "[links]") {
    Contract good;
    good.other = "b";
    good.data = kData;
    good.action = kAction;
    good.feedback = "none";
    Contract bad = good;
    bad.other = "c";
    bad.data = "";
    REQUIRE_FALSE(render_relationships_section({good, bad}).has_value());
    REQUIRE_FALSE(render_relationships_section({}).has_value());
}

// ---- gaps found by mutation testing --------------------------------------------------------------------------

TEST_CASE("Windows line endings parse the same as Unix ones", "[links]") {
    const std::string lf = block('<', "src") + block('>', "tgt");
    std::string crlf;
    for (const char c : lf) {
        if (c == '\n') {
            crlf += '\r';
        }
        crlf += c;
    }
    const auto a = parse_contracts(file("me", lf));
    const auto b = parse_contracts(file("me", crlf));
    REQUIRE(a.size() == 2);
    REQUIRE(b.size() == 2);
    for (std::size_t i = 0; i < a.size(); ++i) {
        REQUIRE(b[i].line == a[i].line);
        REQUIRE(b[i].other == a[i].other);
        REQUIRE(b[i].data == a[i].data);
        REQUIRE(b[i].action == a[i].action);
        REQUIRE(b[i].feedback == a[i].feedback);
    }
}

TEST_CASE("A tilde line inside a backtick fence does not close it", "[links]") {
    const auto parsed = parse_contracts(file("me", "```\n~~~\n" + block('>', "fake") + "```\n" + block('>', "real")));
    REQUIRE(parsed.size() == 1);
    REQUIRE(parsed[0].other == "real");
}

TEST_CASE("A skill naming itself in its own text is not a finding", "[links]") {
    const Report r = audit({file("solo", "solo does one thing.\nSee also: solo\n")});
    REQUIRE(with_code(r, "prose-mention").empty());
    REQUIRE(with_code(r, "see-also").empty());
}

TEST_CASE("The first See also line is the one reported", "[links]") {
    const Report r = audit({file("a", "intro\nSee also: b\nSee also: b again\n"), file("b", "prose\n")});
    const auto hits = with_code(r, "see-also");
    REQUIRE(hits.size() == 1);
    REQUIRE(hits[0].line == 2);
}

TEST_CASE("A finding with no line renders without a line number", "[links]") {
    const Report r = audit({file("loner", "nothing\n")});
    REQUIRE(has(render_report(r, "s", "d"), "[loner/SKILL.md] orphan-skill: "));
}

TEST_CASE("A neighbour's broken contracts do not count against the replacement", "[links]") {
    const std::vector<SkillFile> files = {file("a", block('>', "b") + block('>', "ghost")), file("b", block('<', "a"))};
    REQUIRE(replacement_is_wired(files, "b"));
    REQUIRE_FALSE(replacement_is_wired(files, "a"));
}

TEST_CASE("A loop with a tail reports both the loop and the tail", "[links]") {
    const auto chains = find_chains(contracts_of({file("a", block('>', "b")), file("b", block('>', "c")),
                                                  file("c", block('>', "a") + block('>', "d")), file("d", "x")}));
    REQUIRE(chains.size() == 2);
    REQUIRE(chains[0].skills == std::vector<std::string>{"a", "b", "c", "d"});
    REQUIRE_FALSE(chains[0].loops_back);
    REQUIRE(chains[1].skills == std::vector<std::string>{"a", "b", "c"});
    REQUIRE(chains[1].loops_back);
}

TEST_CASE("A chain found early is dropped when a later start extends it", "[links]") {
    const auto chains = find_chains(contracts_of(
        {file("z", block('>', "a")), file("a", block('>', "b")), file("b", block('>', "c")), file("c", "x")}));
    REQUIRE(chains.size() == 1);
    REQUIRE(chains[0].skills == std::vector<std::string>{"z", "a", "b", "c"});
}

// ---- chain annotations (the skill's Low finding for an unmarked middle of a chain) ---------------------------

TEST_CASE("A middle skill that does not say where its data came from gets a Low finding", "[links]") {
    const Report r =
        audit({file("a", block('>', "b")), file("b", block('<', "a") + block('>', "c")), file("c", block('<', "b"))});
    const auto hits = with_code(r, "missing-chain-annotation");
    REQUIRE(hits.size() == 1);
    REQUIRE(hits[0].severity == Severity::Low);
    REQUIRE(hits[0].file == "b/SKILL.md");
    REQUIRE(hits[0].line == 7);  // each block is six lines, so "### > c" follows the "< a" block
    REQUIRE_FALSE(hits[0].on_contract);
    REQUIRE(has(hits[0].message, "b > c"));
    REQUIRE(has(hits[0].message, "a"));
    REQUIRE(r.failed == 0);  // advice, not a failure
}

TEST_CASE("Naming the origin in the outbound contract satisfies the chain check", "[links]") {
    const std::string said = "Data originated from a, reshaped into scene beats";
    const Report in_data = audit(
        {file("a", block('>', "b")), file("b", block('<', "a") + block('>', "c", said)), file("c", block('<', "b"))});
    REQUIRE(with_code(in_data, "missing-chain-annotation").empty());

    const Report in_action =
        audit({file("a", block('>', "b")),
               file("b", block('<', "a") + block('>', "c", kData, "Rebuild the beats a sent before passing them on")),
               file("c", block('<', "b"))});
    REQUIRE(with_code(in_action, "missing-chain-annotation").empty());

    const Report in_feedback =
        audit({file("a", block('>', "b")),
               file("b", block('<', "a") + block('>', "c", kData, kAction,
                                                 "If c rejects the beats, return them to a with the failing ids")),
               file("c", block('<', "b"))});
    REQUIRE(with_code(in_feedback, "missing-chain-annotation").empty());
}

TEST_CASE("A direct contract from the origin to the end of the chain also satisfies it", "[links]") {
    const Report r = audit({file("a", block('>', "b") + block('>', "c")), file("b", block('<', "a") + block('>', "c")),
                            file("c", block('<', "b") + block('<', "a"))});
    REQUIRE(with_code(r, "missing-chain-annotation").empty());
}

TEST_CASE("Pairs and two-way pairs are not chains", "[links]") {
    REQUIRE(with_code(audit(clean_pair()), "missing-chain-annotation").empty());
    const Report both =
        audit({file("a", block('<', "b") + block('>', "b")), file("b", block('<', "a") + block('>', "a"))});
    REQUIRE(with_code(both, "missing-chain-annotation").empty());
}

TEST_CASE("Every unnamed origin is listed once, on the one block that needs the note", "[links]") {
    const Report r =
        audit({file("alpha", block('>', "b")), file("delta", block('>', "b")),
               file("b", block('<', "alpha") + block('<', "delta") + block('>', "c")), file("c", block('<', "b"))});
    const auto hits = with_code(r, "missing-chain-annotation");
    REQUIRE(hits.size() == 1);
    REQUIRE(has(hits[0].message, "from alpha, delta but"));
}

TEST_CASE("Only the origins that are not named are listed", "[links]") {
    const Report r = audit({file("alpha", block('>', "b")), file("delta", block('>', "b")),
                            file("b", block('<', "alpha") + block('<', "delta") +
                                          block('>', "c", "Data originated from alpha, then merged")),
                            file("c", block('<', "b"))});
    const auto hits = with_code(r, "missing-chain-annotation");
    REQUIRE(hits.size() == 1);
    REQUIRE(has(hits[0].message, "delta"));
    REQUIRE_FALSE(has(hits[0].message, "alpha"));
}

// ---- rejected patterns that must stay rejected ---------------------------------------------------------------

TEST_CASE("A merged bidirectional block is not a contract, but the mention is still noticed", "[links]") {
    const std::string merged =
        "### <> b (bidirectional)\n**Exchanges:** character data and structural analysis\n**Action:** mutual "
        "enrichment\n";
    REQUIRE(parse_contracts(file("a", merged)).empty());
    const Report r = audit({file("a", merged), file("b", "prose\n")});
    REQUIRE(with_code(r, "prose-mention").size() == 1);
    REQUIRE(r.checked == 0);
}

TEST_CASE("A promise of a future link is prose, not a contract", "[links]") {
    const Report r =
        audit({file("a", "Will integrate with b in a future version.\nTODO: add link to b\n"), file("b", "prose\n")});
    REQUIRE(parse_contracts(file("a", "TODO: add link to b\n")).empty());
    REQUIRE(with_code(r, "prose-mention").size() == 1);
    REQUIRE(r.checked == 0);
}

TEST_CASE("The note is asked for on the first block when a skill sends to the next in several places", "[links]") {
    const Report r =
        audit({file("a", block('>', "b")), file("b", block('<', "a") + block('>', "c/one") + block('>', "c/two")),
               file("c", block('<', "b/one") + block('<', "b/two"))});
    const auto hits = with_code(r, "missing-chain-annotation");
    REQUIRE(hits.size() == 1);
    REQUIRE(hits[0].line == 7);
}

TEST_CASE("Chains are judged by skill, so a direct contract to any part of the end skill counts", "[links]") {
    const Report r =
        audit({file("a", block('>', "b") + block('>', "c/x")), file("b", block('<', "a") + block('>', "c/y")),
               file("c", block('<', "b/z") + block('<', "a/w"))});
    REQUIRE(with_code(r, "missing-chain-annotation").empty());
}

TEST_CASE("Skills that do not feed the middle of the chain are not named as origins", "[links]") {
    const Report r =
        audit({file("a", block('>', "b")), file("b", block('<', "a") + block('>', "c")), file("c", block('<', "b")),
               file("pipe", block('>', "quay")), file("quay", block('<', "pipe"))});
    const auto hits = with_code(r, "missing-chain-annotation");
    REQUIRE(hits.size() == 1);
    REQUIRE_FALSE(has(hits[0].message, "pipe"));
    REQUIRE_FALSE(has(hits[0].message, "quay"));
}
