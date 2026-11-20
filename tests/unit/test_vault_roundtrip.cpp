#include <catch2/catch_test_macros.hpp>
#include <map>
#include <string>
#include <vector>

#include "VaultExport.hpp"
#include "WikiLint.hpp"

// What VaultExport writes has to pass the lint that is meant to guard a vault.

namespace {

VaultExport::Note make_note(const std::string& id, const std::string& title, const std::string& role) {
    VaultExport::Note n;
    n.record.id = id;
    n.record.title = title;
    n.record.source_path = "/archive/" + id + ".pdf";
    n.record.format = "PDF";
    n.record.anchor_code = "006.3.AI";
    n.record.anchor_label = "Artificial intelligence and machine learning";
    n.record.material_role = role;
    n.record.classification_confidence = "high";
    n.record.worthiness_class = "reference";
    n.summary = "About " + title + ".";
    return n;
}

std::vector<WikiLint::Page> render_vault(std::vector<VaultExport::Note> notes) {
    const auto names = VaultExport::assign_file_names(notes);
    std::map<std::string, std::string> titles;
    for (const auto& note : notes) {
        titles[note.record.id] = note.record.title;
    }
    std::vector<WikiLint::Page> pages;
    for (const auto& note : notes) {
        pages.push_back({names.at(note.record.id), VaultExport::render_note(note, names, titles)});
    }
    pages.push_back({"README.md", VaultExport::render_index(notes, names)});
    return pages;
}

std::vector<VaultExport::Note> linked_pair() {
    VaultExport::Note parent = make_note("parent", "The Parent", "source");
    VaultExport::Note child = make_note("child", "The Child", "reference");
    child.record.relations.push_back({"parent", "part-of", 1});
    child.record.reverse_relations.push_back({"parent", "has-part", 1});
    parent.record.relations.push_back({"child", "has-part", 1});
    parent.record.reverse_relations.push_back({"child", "part-of", 1});
    return {child, parent};
}

}  // namespace

TEST_CASE("A vault written by VaultExport lints clean under the Dewey schema", "[vault][wiki]") {
    const auto pages = render_vault(linked_pair());
    REQUIRE(pages.size() == 3);
    const auto issues = WikiLint::lint(pages, WikiLint::dewey_required_fields());
    for (const auto& issue : issues) {
        INFO(issue.page << ": " << issue.code << ": " << issue.message);
    }
    REQUIRE(issues.empty());
}

TEST_CASE("The same vault is not a valid llm-wiki vault, which is why the schema is a choice", "[vault][wiki]") {
    const auto issues = WikiLint::lint(render_vault(linked_pair()));
    REQUIRE(WikiLint::count_errors(issues) > 0);
}

TEST_CASE("Removing a link back from a written note is caught", "[vault][wiki]") {
    auto pages = render_vault(linked_pair());
    for (auto& page : pages) {
        if (page.path.find("the-parent") != std::string::npos) {
            const auto at = page.text.find("[[");
            REQUIRE(at != std::string::npos);
            page.text.erase(at);  // drop everything from the first link on
        }
    }
    bool saw = false;
    for (const auto& issue : WikiLint::lint(pages, WikiLint::dewey_required_fields())) {
        saw = saw || issue.code == "non-reciprocal";
    }
    REQUIRE(saw);
}
