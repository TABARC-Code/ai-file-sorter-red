#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "VaultExport.hpp"

using namespace VaultExport;

namespace {

Note make_note(const std::string& id, const std::string& title, const std::string& role = "reference") {
    Note n;
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

void link(Note& child, Note& parent) {
    child.record.relations.push_back({parent.record.id, "part-of", 1});
    child.record.reverse_relations.push_back({parent.record.id, "has-part", 1});
    parent.record.relations.push_back({child.record.id, "has-part", 1});
    parent.record.reverse_relations.push_back({child.record.id, "part-of", 1});
}

std::vector<Note> linked_pair() {
    Note parent = make_note("parent", "The Parent", "source");
    Note child = make_note("child", "The Child");
    link(child, parent);
    return {child, parent};
}

std::string slurp(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

bool has(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

bool any_has(const std::vector<std::string>& lines, const std::string& needle) {
    for (const auto& line : lines) {
        if (has(line, needle)) {
            return true;
        }
    }
    return false;
}

struct TempDir {
    std::filesystem::path path;
    explicit TempDir(const std::string& name) : path(std::filesystem::temp_directory_path() / name) {
        std::filesystem::remove_all(path);
    }
    ~TempDir() { std::filesystem::remove_all(path); }
};

}  // namespace

TEST_CASE("File names come from anchor and title, one per note", "[vault]") {
    const auto names = assign_file_names(linked_pair());
    REQUIRE(names.size() == 2);
    REQUIRE(names.at("child") == "006.3.AI-the-child.md");
    REQUIRE(names.at("parent") == "006.3.AI-the-parent.md");
}

TEST_CASE("Clashing names get a counter and unusable titles fall back to the id", "[vault]") {
    std::vector<Note> notes = {make_note("one", "Same Title"), make_note("two", "Same Title"),
                               make_note("three", "???")};
    const auto names = assign_file_names(notes);
    REQUIRE(names.at("one") == "006.3.AI-same-title.md");
    REQUIRE(names.at("two") == "006.3.AI-same-title-2.md");
    REQUIRE(names.at("three") == "006.3.AI-three.md");
    std::set<std::string> unique;
    for (const auto& entry : names) {
        unique.insert(entry.second);
    }
    REQUIRE(unique.size() == 3);
}

TEST_CASE("Residue and duplicate candidates earn no file", "[vault]") {
    const auto names = assign_file_names(
        {make_note("a", "Keep"), make_note("b", "Bin", "residue"), make_note("c", "Twin", "duplicate-candidate")});
    REQUIRE(names.size() == 1);
    REQUIRE(names.count("a") == 1);
}

TEST_CASE("Notes carry frontmatter, summary and wikilinks", "[vault]") {
    const auto notes = linked_pair();
    const auto names = assign_file_names(notes);
    const std::map<std::string, std::string> titles = {{"child", "The Child"}, {"parent", "The Parent"}};

    const std::string text = render_note(notes[0], names, titles);
    REQUIRE(text.rfind("---\n", 0) == 0);
    REQUIRE(has(text, "id: \"child\""));
    REQUIRE(has(text, "# The Child"));
    REQUIRE(has(text, "About The Child."));
    REQUIRE(has(text, "part-of: [[006.3.AI-the-parent|The Parent]]"));
}

TEST_CASE("Links to notes that do not exist stay as plain text", "[vault]") {
    Note lone = make_note("lone", "Lone");
    lone.record.relations.push_back({"ghost", "related-to", 3});
    const auto names = assign_file_names({lone});

    const std::string text = render_note(lone, names, {{"lone", "Lone"}});
    REQUIRE(has(text, "related-to: ghost (not in the vault)"));
    REQUIRE_FALSE(has(text, "[[ghost"));
}

TEST_CASE("Wikilink text is made safe", "[vault]") {
    Note a = make_note("a", "Pipes | and ]] brackets");
    Note b = make_note("b", "B");
    b.record.relations.push_back({"a", "related-to", 2});
    const auto names = assign_file_names({a, b});

    const std::string text = render_note(b, names, {{"a", a.record.title}, {"b", "B"}});
    REQUIRE(has(text, "related-to: [[006.3.AI-pipes-and-brackets|Pipes / and )) brackets]]"));
}

TEST_CASE("Only full notes carry context and the decision note", "[vault]") {
    Note source = make_note("s", "Origin", "source");
    source.context = "Where this came from.";
    source.record.decision_note = "Chose AI over 000 because the paper is about models.";
    Note reference = make_note("r", "Paper", "reference");
    reference.context = "Should not appear.";
    reference.record.decision_note = "Should not appear either.";
    const auto names = assign_file_names({source, reference});

    const std::string full = render_note(source, names, {});
    REQUIRE(has(full, "## Context"));
    REQUIRE(has(full, "Where this came from."));
    REQUIRE(has(full, "## Decision note"));

    const std::string brief = render_note(reference, names, {});
    REQUIRE_FALSE(has(brief, "## Context"));
    REQUIRE_FALSE(has(brief, "Should not appear."));
}

TEST_CASE("Process notes go full only inside a project corpus", "[vault]") {
    Note process = make_note("p", "Draft", "process");
    process.context = "Notes on the draft.";
    const auto names = assign_file_names({process});
    REQUIRE_FALSE(has(render_note(process, names, {}), "## Context"));
    process.in_project_corpus = true;
    REQUIRE(has(render_note(process, names, {}), "## Context"));
}

TEST_CASE("The index groups notes by anchor and links each one", "[vault]") {
    Note ai = make_note("ai", "Attention Paper");
    Note poem = make_note("poem", "A Poem");
    poem.record.anchor_code = "821.POET";
    poem.record.anchor_label = "Poetry";
    const std::vector<Note> notes = {poem, ai};
    const auto names = assign_file_names(notes);

    const std::string index = render_index(notes, names);
    REQUIRE(has(index, "# Archive index"));
    REQUIRE(has(index, "2 notes"));
    REQUIRE(has(index, "\n## 006.3.AI - Artificial intelligence and machine learning\n"));
    REQUIRE(has(index, "[[006.3.AI-attention-paper|Attention Paper]]"));
    REQUIRE(has(index, "\n## 821.POET - Poetry\n"));
    REQUIRE(index.find("006.3.AI -") < index.find("821.POET -"));  // anchors in code order
}

TEST_CASE("The graph file lists written nodes and their edges", "[vault]") {
    const auto notes = linked_pair();
    const auto names = assign_file_names(notes);
    const std::string json = render_graph_json(notes, names);

    REQUIRE(json ==
            "{\"nodes\":[{\"id\":\"child\",\"title\":\"The Child\",\"anchor\":\"006.3.AI\",\"role\":\"reference\"},"
            "{\"id\":\"parent\",\"title\":\"The Parent\",\"anchor\":\"006.3.AI\",\"role\":\"source\"}],"
            "\"edges\":[{\"source\":\"child\",\"target\":\"parent\",\"type\":\"part-of\",\"tier\":1},"
            "{\"source\":\"parent\",\"target\":\"child\",\"type\":\"has-part\",\"tier\":1}]}");
}

TEST_CASE("The graph file leaves out edges to notes that are not written", "[vault]") {
    Note n = make_note("n", "N");
    n.record.relations.push_back({"ghost", "related-to", 3});
    const std::string json = render_graph_json({n}, assign_file_names({n}));
    REQUIRE(has(json, "\"edges\":[]"));
}

TEST_CASE("The graph file escapes awkward characters", "[vault]") {
    Note n = make_note("q", "He said \"hi\"\nand left\\");
    const std::string json = render_graph_json({n}, assign_file_names({n}));
    REQUIRE(has(json, "\"title\":\"He said \\\"hi\\\"\\nand left\\\\\""));
}

TEST_CASE("Lint passes a complete, reciprocal set", "[vault]") {
    const auto report = lint(linked_pair());
    REQUIRE(report.pass());
    REQUIRE(report.errors.empty());
    REQUIRE(report.graph.pass);
}

TEST_CASE("Lint fails on a missing summary, a bad record or a repeated id", "[vault]") {
    auto notes = linked_pair();
    notes[0].summary.clear();
    auto report = lint(notes);
    REQUIRE_FALSE(report.pass());
    REQUIRE(any_has(report.errors, "child"));
    REQUIRE(any_has(report.errors, "summary"));

    notes = linked_pair();
    notes[0].record.format = "DOCX";
    report = lint(notes);
    REQUIRE_FALSE(report.pass());
    REQUIRE(any_has(report.errors, "format"));

    notes = linked_pair();
    notes[1].record.id = "child";
    report = lint(notes);
    REQUIRE_FALSE(report.pass());
    REQUIRE(any_has(report.errors, "more than once"));
}

TEST_CASE("Lint fails on non-reciprocal links", "[vault]") {
    auto notes = linked_pair();
    notes[1].record.relations.clear();
    notes[1].record.reverse_relations.clear();
    const auto report = lint(notes);
    REQUIRE_FALSE(report.pass());
    REQUIRE_FALSE(report.graph.pass);
    REQUIRE(any_has(report.errors, "FAIL"));
}

TEST_CASE("A link to a residue record counts as dangling", "[vault]") {
    Note keep = make_note("keep", "Keep");
    Note bin = make_note("bin", "Bin", "residue");
    keep.record.relations.push_back({"bin", "related-to", 3});
    keep.record.reverse_relations.push_back({"bin", "related-to", 3});
    const auto report = lint({keep, bin});
    REQUIRE_FALSE(report.pass());
    REQUIRE(report.graph.dangling > 0);
}

TEST_CASE("Lint warns about busy notes without failing", "[vault]") {
    Note hub = make_note("hub", "Hub");
    std::vector<Note> notes = {hub};
    for (int i = 0; i < 7; ++i) {
        Note spoke = make_note("s" + std::to_string(i), "Spoke " + std::to_string(i));
        link(spoke, notes[0]);
        notes.push_back(spoke);
    }
    const auto report = lint(notes);
    REQUIRE(report.pass());
    REQUIRE(any_has(report.warnings, "hub"));
}

TEST_CASE("Writing a vault creates notes, index and graph, and skips residue", "[vault]") {
    TempDir dir("aifs_vault_write");
    auto notes = linked_pair();
    notes.push_back(make_note("bin", "Bin", "residue"));

    const auto report = write_vault(dir.path / "vault", notes);
    REQUIRE(report.error.empty());
    REQUIRE(report.written.size() == 4);  // two notes, README.md, graph.json
    REQUIRE(std::filesystem::exists(dir.path / "vault" / "006.3.AI-the-child.md"));
    REQUIRE(std::filesystem::exists(dir.path / "vault" / "006.3.AI-the-parent.md"));
    REQUIRE(std::filesystem::exists(dir.path / "vault" / "README.md"));
    REQUIRE(std::filesystem::exists(dir.path / "vault" / "graph.json"));
    REQUIRE_FALSE(std::filesystem::exists(dir.path / "vault" / "006.3.AI-bin.md"));
    REQUIRE(has(slurp(dir.path / "vault" / "006.3.AI-the-child.md"), "# The Child"));
}

TEST_CASE("Writing twice changes nothing the second time", "[vault]") {
    TempDir dir("aifs_vault_twice");
    REQUIRE(write_vault(dir.path, linked_pair()).error.empty());
    const auto again = write_vault(dir.path, linked_pair());
    REQUIRE(again.written.empty());
    REQUIRE(again.unchanged.size() == 4);
    REQUIRE(again.conflicts.empty());
}

TEST_CASE("Existing files with other content are left alone unless overwrite is set", "[vault]") {
    TempDir dir("aifs_vault_conflict");
    std::filesystem::create_directories(dir.path);
    { std::ofstream(dir.path / "README.md") << "my own readme"; }

    const auto safe = write_vault(dir.path, linked_pair());
    REQUIRE(safe.error.empty());
    REQUIRE(any_has(safe.conflicts, "README.md"));
    REQUIRE(slurp(dir.path / "README.md") == "my own readme");
    REQUIRE(std::filesystem::exists(dir.path / "graph.json"));  // the rest still went out

    const auto forced = write_vault(dir.path, linked_pair(), true);
    REQUIRE(forced.conflicts.empty());
    REQUIRE(has(slurp(dir.path / "README.md"), "# Archive index"));
}

TEST_CASE("A failed lint writes nothing at all", "[vault]") {
    TempDir dir("aifs_vault_fail");
    auto notes = linked_pair();
    notes[0].summary.clear();

    const auto report = write_vault(dir.path / "vault", notes);
    REQUIRE_FALSE(report.error.empty());
    REQUIRE(report.written.empty());
    REQUIRE_FALSE(std::filesystem::exists(dir.path / "vault"));
}

TEST_CASE("Written file names never contain path separators", "[vault]") {
    Note n = make_note("evil", "../../etc/passwd");
    const auto names = assign_file_names({n});
    REQUIRE(names.at("evil").find('/') == std::string::npos);
    REQUIRE(names.at("evil").find('\\') == std::string::npos);
    REQUIRE(names.at("evil").find("..") == std::string::npos);
}
