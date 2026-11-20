#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "WikiLint.hpp"

using namespace WikiLint;

namespace {

std::string note(const std::string& body, const std::string& title = "T", const std::string& summary = "S") {
    return "---\ntitle: " + title + "\ntype: concept\ntags: [a, b]\nsummary: " + summary + "\n---\n" + body + "\n";
}

std::vector<Issue> with_code(const std::vector<Issue>& issues, const std::string& code) {
    std::vector<Issue> out;
    for (const auto& issue : issues) {
        if (issue.code == code) {
            out.push_back(issue);
        }
    }
    return out;
}

}  // namespace

TEST_CASE("Required fields are title, type, tags and summary", "[wiki]") {
    REQUIRE(required_fields() == std::vector<std::string>{"title", "type", "tags", "summary"});
}

TEST_CASE("Frontmatter parses plain, quoted and list values", "[wiki]") {
    const auto fm = parse_frontmatter(
        "---\nTitle: \"Quoted: title\"\ntype: 'concept'\ntags:\n  - alpha\n  - beta\nsummary: One "
        "line\nempty:\n---\nbody\n");
    REQUIRE(fm.present);
    REQUIRE(fm.fields.at("title") == "Quoted: title");
    REQUIRE(fm.fields.at("type") == "concept");
    REQUIRE(fm.fields.at("tags") == "alpha, beta");
    REQUIRE(fm.fields.at("summary") == "One line");
    REQUIRE(fm.fields.at("empty").empty());
}

TEST_CASE("Inline tag lists are normalised and empty ones are empty", "[wiki]") {
    REQUIRE(parse_frontmatter("---\ntags: [x,  y ,z]\n---\n").fields.at("tags") == "x, y, z");
    REQUIRE(parse_frontmatter("---\ntags: []\n---\n").fields.at("tags").empty());
}

TEST_CASE("Quotes are only stripped when they match", "[wiki]") {
    const auto fm = parse_frontmatter("---\na: \"\nb: 'mixed\"\nc: \"ok\"\nd: [unclosed\ne: x\n---\n");
    REQUIRE(fm.fields.at("a") == "\"");
    REQUIRE(fm.fields.at("b") == "'mixed\"");
    REQUIRE(fm.fields.at("c") == "ok");
    REQUIRE(fm.fields.at("d") == "[unclosed");
}

TEST_CASE("Empty items in an inline list are dropped", "[wiki]") {
    REQUIRE(parse_frontmatter("---\ntags: [a, , b,]\n---\n").fields.at("tags") == "a, b");
}

TEST_CASE("Indented lines and spaced keys are handled as plain keys", "[wiki]") {
    const auto fm = parse_frontmatter("---\ntags:\na b: v\n  nested: ignored\n---\n");
    REQUIRE(fm.fields.at("tags").empty());
    REQUIRE(fm.fields.at("a b") == "v");
    REQUIRE(fm.fields.count("nested") == 0);
}

TEST_CASE("Frontmatter is absent without an opening or a closing fence", "[wiki]") {
    REQUIRE_FALSE(parse_frontmatter("").present);
    REQUIRE_FALSE(parse_frontmatter("no fence here\n---\ntitle: x\n---\n").present);
    REQUIRE_FALSE(parse_frontmatter("---\ntitle: never closed\nbody\n").present);
}

TEST_CASE("Frontmatter copes with Windows line endings", "[wiki]") {
    const auto fm = parse_frontmatter("---\r\ntitle: Crlf\r\ntags: [a]\r\n---\r\nbody\r\n");
    REQUIRE(fm.present);
    REQUIRE(fm.fields.at("title") == "Crlf");
    REQUIRE(fm.fields.at("tags") == "a");
}

TEST_CASE("Wikilinks lose aliases, headings and block ids", "[wiki]") {
    const auto links =
        extract_wikilinks("See [[Alpha]], [[Beta|the b]], [[Gamma#Intro]], [[Delta^abc]] and [[ Eps | x ]].");
    REQUIRE(links == std::vector<std::string>{"Alpha", "Beta", "Gamma", "Delta", "Eps"});
}

TEST_CASE("Repeated links appear once, in first-seen order", "[wiki]") {
    REQUIRE(extract_wikilinks("[[b]] [[a]] [[b]]\n[[a]]") == std::vector<std::string>{"b", "a"});
}

TEST_CASE("Embeds, code and frontmatter do not count as links", "[wiki]") {
    const std::string text =
        "---\nrelated: \"[[InFrontmatter]]\"\n---\n"
        "![[image.png]] and [[Real]]\n"
        "`[[InlineCode]]` text\n"
        "```\n[[InFence]]\n```\n"
        "~~~\n[[InTilde]]\n~~~\n"
        "[[]] and [[ ]] and [[|alias]]\n"
        "after [[Tail]]\n";
    REQUIRE(extract_wikilinks(text) == std::vector<std::string>{"Real", "Tail"});
}

TEST_CASE("A reciprocal pair with full frontmatter lints clean", "[wiki]") {
    const std::vector<Page> pages = {{"a.md", note("[[b]]")}, {"b.md", note("[[a]]")}};
    REQUIRE(lint(pages).empty());
}

TEST_CASE("Each missing or blank field is an error naming the field", "[wiki]") {
    const std::string text = "---\ntitle: Only a title\ntype:\ntags: []\n---\nbody\n";
    const auto issues = with_code(lint({{"x.md", text}}), "missing-field");
    REQUIRE(issues.size() == 3);
    REQUIRE(issues[0].message.find("type") != std::string::npos);
    REQUIRE(issues[1].message.find("tags") != std::string::npos);
    REQUIRE(issues[2].message.find("summary") != std::string::npos);
    for (const auto& issue : issues) {
        REQUIRE(issue.severity == Severity::Error);
        REQUIRE(issue.page == "x.md");
    }
}

TEST_CASE("A page with no frontmatter gets one error, not four", "[wiki]") {
    const auto issues = lint({{"bare.md", "just text\n"}});
    REQUIRE(with_code(issues, "no-frontmatter").size() == 1);
    REQUIRE(with_code(issues, "missing-field").empty());
    REQUIRE(with_code(issues, "no-frontmatter")[0].severity == Severity::Error);
}

TEST_CASE("A link to nowhere is a warning on the page that holds it", "[wiki]") {
    const auto issues = with_code(lint({{"a.md", note("[[ghost]]")}}), "broken-link");
    REQUIRE(issues.size() == 1);
    REQUIRE(issues[0].severity == Severity::Warning);
    REQUIRE(issues[0].page == "a.md");
    REQUIRE(issues[0].message.find("ghost") != std::string::npos);
}

TEST_CASE("A one-way link is reported on the page that should link back", "[wiki]") {
    const std::vector<Page> pages = {{"a.md", note("[[b]]")}, {"b.md", note("no links")}};
    const auto issues = lint(pages);
    const auto hits = with_code(issues, "non-reciprocal");
    REQUIRE(hits.size() == 1);
    REQUIRE(hits[0].page == "b.md");
    REQUIRE(hits[0].message.find("a.md") != std::string::npos);
    REQUIRE(with_code(issues, "orphan").empty());
}

TEST_CASE("Hub pages may link out without being linked back, and are never orphans", "[wiki]") {
    const std::vector<Page> pages = {{"README.md", note("[[a]] [[b]]")},
                                     {"index.md", note("[[a]]")},
                                     {"a.md", note("[[b]]")},
                                     {"b.md", note("[[a]]")}};
    REQUIRE(lint(pages).empty());
    REQUIRE(lint({{"Log.md", note("nothing")}}).empty());
}

TEST_CASE("Self links are ignored", "[wiki]") {
    const auto issues = lint({{"a.md", note("[[a]]")}});
    REQUIRE(with_code(issues, "non-reciprocal").empty());
    REQUIRE(with_code(issues, "broken-link").empty());
    REQUIRE(with_code(issues, "orphan").size() == 1);
}

TEST_CASE("A page with no links in or out is an orphan", "[wiki]") {
    const std::vector<Page> pages = {{"a.md", note("[[b]]")}, {"b.md", note("[[a]]")}, {"lonely.md", note("nothing")}};
    const auto issues = with_code(lint(pages), "orphan");
    REQUIRE(issues.size() == 1);
    REQUIRE(issues[0].page == "lonely.md");
}

TEST_CASE("Names resolve without regard to case, extension or folder", "[wiki]") {
    const std::vector<Page> pages = {{"notes/Alpha.md", note("[[beta]]")}, {"deep/er/Beta.md", note("[[ALPHA]]")}};
    REQUIRE(lint(pages).empty());
}

TEST_CASE("A link may carry the .md extension", "[wiki]") {
    const std::vector<Page> pages = {{"a.md", note("[[b.md]]")}, {"b.md", note("[[A.MD]]")}};
    REQUIRE(lint(pages).empty());
}

TEST_CASE("Path links match the end of a longer path", "[wiki]") {
    const std::vector<Page> pages = {{"vault/x/idea.md", note("[[y/idea]]")}, {"vault/y/idea.md", note("[[x/idea]]")}};
    REQUIRE(lint(pages).empty());
}

TEST_CASE("A quoted blank value still counts as missing", "[wiki]") {
    const std::string text = "---\ntitle: \" \"\ntype: t\ntags: [a]\nsummary: s\n---\n";
    const auto issues = with_code(lint({{"x.md", text}}), "missing-field");
    REQUIRE(issues.size() == 1);
    REQUIRE(issues[0].message.find("title") != std::string::npos);
}

TEST_CASE("Path style links pick the right page", "[wiki]") {
    const std::vector<Page> pages = {{"x/idea.md", note("[[y/idea]]")}, {"y/idea.md", note("[[x/idea]]")}};
    REQUIRE(lint(pages).empty());
}

TEST_CASE("A bare name that matches two pages is an ambiguous link, not a broken one", "[wiki]") {
    const std::vector<Page> pages = {{"one/dup.md", note("")}, {"two/dup.md", note("")}, {"a.md", note("[[dup]]")}};
    const auto issues = lint(pages);
    const auto hits = with_code(issues, "ambiguous-link");
    REQUIRE(hits.size() == 1);
    REQUIRE(hits[0].severity == Severity::Warning);
    REQUIRE(hits[0].page == "a.md");
    REQUIRE(hits[0].message.find("dup") != std::string::npos);
    REQUIRE(with_code(issues, "broken-link").empty());
    REQUIRE(with_code(issues, "non-reciprocal").empty());
}

TEST_CASE("Hubs are not asked to link back either", "[wiki]") {
    const std::vector<Page> pages = {
        {"index.md", note("")}, {"a.md", note("[[index]] [[b]]")}, {"b.md", note("[[a]]")}};
    REQUIRE(with_code(lint(pages), "non-reciprocal").empty());
}

TEST_CASE("Issues come in page order, frontmatter before links", "[wiki]") {
    const std::vector<Page> pages = {{"a.md", "---\ntitle: t\n---\n[[ghost]]\n"}, {"b.md", "plain\n"}};
    const auto issues = lint(pages);
    REQUIRE(issues.size() >= 3);
    REQUIRE(issues[0].page == "a.md");
    REQUIRE(issues[0].code == "missing-field");
    std::size_t last_a = 0;
    std::size_t first_b = issues.size();
    for (std::size_t i = 0; i < issues.size(); ++i) {
        if (issues[i].page == "a.md") {
            last_a = i;
        } else if (first_b == issues.size()) {
            first_b = i;
        }
    }
    REQUIRE(last_a < first_b);
    REQUIRE(with_code(issues, "broken-link").size() == 1);
    REQUIRE(issues[last_a].code != "missing-field");
}

TEST_CASE("Error counting ignores warnings", "[wiki]") {
    const auto issues = lint({{"a.md", "---\ntitle: t\n---\n[[ghost]]\n"}});
    REQUIRE(count_errors(issues) == 3);
    REQUIRE(issues.size() > 3);
    REQUIRE(count_errors({}) == 0);
}

TEST_CASE("The Dewey schema names the ten record fields", "[wiki]") {
    REQUIRE(dewey_required_fields() ==
            std::vector<std::string>{"id", "title", "source_path", "format", "anchor_code", "anchor_label",
                                     "material_role", "classification_confidence", "worthiness_class", "status"});
}

TEST_CASE("Lint can be told which fields to require", "[wiki]") {
    const std::string text = "---\ntitle: T\nowner: Me\n---\nbody [[b]]\n";
    const std::vector<Page> pages = {{"a.md", text}, {"b.md", "---\ntitle: T\nowner: Me\n---\n[[a]]\n"}};
    REQUIRE(lint(pages, {"title", "owner"}).empty());
    const auto issues = with_code(lint(pages, {"title", "owner", "status"}), "missing-field");
    REQUIRE(issues.size() == 2);
    REQUIRE(issues[0].message.find("status") != std::string::npos);
    REQUIRE(with_code(lint(pages), "missing-field").size() == 6);
}

TEST_CASE("Hubs need no frontmatter", "[wiki]") {
    const std::vector<Page> pages = {{"README.md", "# Index\n[[a]]\n"}, {"a.md", note("[[README]]")}};
    const auto issues = lint(pages);
    REQUIRE(with_code(issues, "no-frontmatter").empty());
    REQUIRE(with_code(issues, "missing-field").empty());
    REQUIRE(lint({{"notes/Index.md", ""}}).empty());
    REQUIRE(with_code(lint({{"plain.md", ""}}), "no-frontmatter").size() == 1);
}
