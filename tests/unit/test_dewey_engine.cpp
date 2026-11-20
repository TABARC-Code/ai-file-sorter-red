#include <catch2/catch_test_macros.hpp>
#include <set>
#include <string>
#include <vector>

#include "DeweyEngine.hpp"

using namespace DeweyEngine;

namespace {

Record make_record(const std::string& id) {
    Record r;
    r.id = id;
    r.title = "Title " + id;
    r.source_path = "/archive/" + id + ".pdf";
    r.format = "PDF";
    r.anchor_code = "006.3.AI";
    r.anchor_label = "Artificial intelligence and machine learning";
    r.material_role = "reference";
    r.classification_confidence = "high";
    r.worthiness_class = "reference";
    return r;
}

// Two records joined by part-of / has-part, with the reverse written on both sides.
std::vector<Record> linked_pair() {
    Record child = make_record("child");
    Record parent = make_record("parent");
    child.relations.push_back({"parent", "part-of", 1});
    child.reverse_relations.push_back({"parent", "part-of", 1});
    parent.relations.push_back({"child", "has-part", 1});
    parent.reverse_relations.push_back({"child", "has-part", 1});
    return {child, parent};
}

bool mentions(const std::vector<std::string>& problems, const std::string& needle) {
    for (const auto& p : problems) {
        if (p.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("Dewey vocabulary holds the 45 legal anchors with matching labels", "[dewey]") {
    const auto& vocab = anchor_vocabulary();
    REQUIRE(vocab.size() == 45);

    std::set<std::string> codes;
    for (const auto& entry : vocab) {
        codes.insert(entry.code);
    }
    REQUIRE(codes.size() == 45);

    REQUIRE(anchor_label("000") == "General works and unclassified");
    REQUIRE(anchor_label("006.3.AI") == "Artificial intelligence and machine learning");
    REQUIRE(anchor_label("920.MEM") == "Biography, memoir, personal journals");
    REQUIRE_FALSE(anchor_label("808.02.ESS.SUB").has_value());
    REQUIRE_FALSE(anchor_label("").has_value());
}

TEST_CASE("Dewey enumerations accept only the legal values", "[dewey]") {
    for (const char* v : {"BK", "ESS", "ART", "PDF", "NOTE", "NB", "IMG", "AUD", "VID", "OBJ", "ZINE", "PROJ", "SK",
                          "MAP", "ARC", "FOLDER", "CODE", "DATA"}) {
        REQUIRE(is_legal_format(v));
    }
    REQUIRE_FALSE(is_legal_format("pdf"));
    REQUIRE_FALSE(is_legal_format("DOCX"));

    for (const char* v : {"aesthetic", "autobiographical", "critical", "documentary", "ecological", "economic",
                          "ethnographic", "experimental", "historical", "material", "pedagogical", "philosophical",
                          "poetic", "political", "speculative", "symbolic", "technical", "theological"}) {
        REQUIRE(is_legal_lens(v));
    }
    REQUIRE_FALSE(is_legal_lens("funny"));

    for (const char* v : {"source", "process", "reference", "output", "residue", "duplicate-candidate"}) {
        REQUIRE(is_legal_role(v));
    }
    REQUIRE_FALSE(is_legal_role("archival"));

    for (const char* v : {"archival", "reference", "process", "temporary/intake", "residue", "duplicate-candidate"}) {
        REQUIRE(is_legal_worthiness(v));
    }
    REQUIRE_FALSE(is_legal_worthiness("source"));

    for (const char* v : {"high", "medium", "low", "provisional"}) {
        REQUIRE(is_legal_confidence(v));
    }
    REQUIRE_FALSE(is_legal_confidence("certain"));
}

TEST_CASE("Dewey relation reversal covers symmetric and asymmetric types", "[dewey]") {
    for (const char* v :
         {"related-to", "contrasts-with", "same-topic-different-lens", "parallel-to", "overlaps-with"}) {
        REQUIRE(reverse_relation_type(v) == v);
    }

    const std::vector<std::pair<std::string, std::string>> pairs = {
        {"influenced-by", "influenced"}, {"critique-of", "critiqued-by"},      {"subset-of", "has-subset"},
        {"part-of", "has-part"},         {"applies-to", "has-application-in"}, {"uses-methods-from", "methods-used-in"},
        {"documents", "documented-by"},  {"derived-from", "gives-rise-to"},    {"references", "referenced-by"}};
    for (const auto& [forward, reverse] : pairs) {
        REQUIRE(reverse_relation_type(forward) == reverse);
        REQUIRE(reverse_relation_type(reverse) == forward);
    }

    REQUIRE_FALSE(reverse_relation_type("likes").has_value());
    REQUIRE_FALSE(reverse_relation_type("").has_value());
}

TEST_CASE("Dewey graph verification passes when every reverse is matched", "[dewey]") {
    const auto report = verify_graph(linked_pair());
    REQUIRE(report.pass);
    REQUIRE(report.total == 4);
    REQUIRE(report.matched == 4);
    REQUIRE(report.missing == 0);
    REQUIRE(report.dangling == 0);
    REQUIRE(report.summary() == "PASS: 4/4 matched, 0 missing, 0 dangling");
}

TEST_CASE("Dewey graph verification fails on a missing reverse", "[dewey]") {
    auto records = linked_pair();
    records[1].relations.clear();  // parent no longer says has-part
    records[1].reverse_relations.clear();

    const auto report = verify_graph(records);
    REQUIRE_FALSE(report.pass);
    REQUIRE(report.total == 2);
    REQUIRE(report.matched == 0);
    REQUIRE(report.missing == 2);
    REQUIRE(report.dangling == 0);
    REQUIRE(report.summary() == "FAIL: 0/2 matched, 2 missing, 0 dangling");
    REQUIRE_FALSE(report.problems.empty());
}

TEST_CASE("Dewey graph verification fails on a dangling target", "[dewey]") {
    Record lone = make_record("lone");
    lone.relations.push_back({"ghost", "related-to", 2});

    const auto report = verify_graph({lone});
    REQUIRE_FALSE(report.pass);
    REQUIRE(report.total == 1);
    REQUIRE(report.matched == 0);
    REQUIRE(report.missing == 0);
    REQUIRE(report.dangling == 1);
    REQUIRE(mentions(report.problems, "ghost"));
}

TEST_CASE("Dewey graph verification demands the exact reverse type", "[dewey]") {
    auto records = linked_pair();
    records[1].relations[0].type = "related-to";  // wrong: part-of needs has-part
    records[1].reverse_relations[0].type = "related-to";

    const auto report = verify_graph(records);
    REQUIRE_FALSE(report.pass);
    REQUIRE(report.missing > 0);
}

TEST_CASE("Dewey graph verification treats symmetric types as self-reversing", "[dewey]") {
    Record a = make_record("a");
    Record b = make_record("b");
    a.relations.push_back({"b", "contrasts-with", 2});
    a.reverse_relations.push_back({"b", "contrasts-with", 2});
    b.relations.push_back({"a", "contrasts-with", 2});
    b.reverse_relations.push_back({"a", "contrasts-with", 2});

    REQUIRE(verify_graph({a, b}).pass);
}

TEST_CASE("Dewey graph with no relations passes with nothing to match", "[dewey]") {
    const auto report = verify_graph({make_record("solo")});
    REQUIRE(report.pass);
    REQUIRE(report.total == 0);
    REQUIRE(report.summary() == "PASS: 0/0 matched, 0 missing, 0 dangling");
}

TEST_CASE("Dewey validation accepts a complete linked record", "[dewey]") {
    const auto records = linked_pair();
    REQUIRE(validate_record(records[0], records).empty());
}

TEST_CASE("Dewey validation rejects each of the eleven failures", "[dewey]") {
    const auto base = linked_pair();
    auto check = [&](auto mutate, const std::string& needle) {
        Record r = base[0];
        mutate(r);
        const auto problems = validate_record(r, base);
        INFO("expected a problem mentioning: " << needle);
        REQUIRE_FALSE(problems.empty());
        REQUIRE(mentions(problems, needle));
    };

    check([](Record& r) { r.title.clear(); }, "title");                           // 1 required field
    check([](Record& r) { r.anchor_code = "808.02.ESS.SUB"; }, "anchor_code");    // 2 anchor in vocabulary
    check([](Record& r) { r.anchor_label = "Something else"; }, "anchor_label");  // 3 label matches entry
    check([](Record& r) { r.format = "DOCX"; }, "format");                        // 4 legal format
    check([](Record& r) { r.material_role = "archival"; }, "material_role");      // 5 role
    check([](Record& r) { r.worthiness_class = "source"; }, "worthiness_class");  // 6 worthiness
    check([](Record& r) { r.classification_confidence = "certain"; }, "classification_confidence");  // 7 confidence
    check([](Record& r) { r.lens_tags = {"funny"}; }, "lens_tags");                                  // 8 lens
    check([](Record& r) { r.relations[0].type = "likes"; }, "relation");                             // 9 relation type
    check([](Record& r) { r.relations[0].tier = 4; }, "tier");                                       // 9 tier
    check([](Record& r) { r.reverse_relations.clear(); }, "reverse_relations");  // 10 reverse non-empty
    check([](Record& r) { r.relations[0].target = "ghost"; }, "ghost");          // 11 target resolves
}

TEST_CASE("Dewey relation cap warns above six", "[dewey]") {
    Record r = make_record("busy");
    for (int i = 0; i < 6; ++i) {
        r.relations.push_back({"t" + std::to_string(i), "related-to", 3});
    }
    REQUIRE_FALSE(over_relation_cap(r));
    r.relations.push_back({"t6", "related-to", 3});
    REQUIRE(over_relation_cap(r));
}

TEST_CASE("Dewey note depth follows material role", "[dewey]") {
    REQUIRE(note_depth("source", false) == NoteDepth::Full);
    REQUIRE(note_depth("process", true) == NoteDepth::Full);
    REQUIRE(note_depth("process", false) == NoteDepth::Short);
    REQUIRE(note_depth("reference", false) == NoteDepth::Short);
    REQUIRE(note_depth("output", false) == NoteDepth::Short);
    REQUIRE(note_depth("residue", true) == NoteDepth::None);
    REQUIRE(note_depth("duplicate-candidate", true) == NoteDepth::None);
}

TEST_CASE("Dewey naming uses kebab-case with the right prefix", "[dewey]") {
    REQUIRE(folder_name("700", "Studio Practice & Notes") == "700-studio-practice-notes");
    REQUIRE(folder_name("006", "  AI  Papers ") == "006-ai-papers");
    REQUIRE(note_file_name("006.3.AI", "Attention Is All You Need") == "006.3.AI-attention-is-all-you-need.md");
    REQUIRE(note_file_name("000", "notes") == "000-notes.md");
}

TEST_CASE("Dewey frontmatter carries every field even when empty", "[dewey]") {
    Record r;
    r.id = "x";
    const std::string fm = to_frontmatter(r);

    REQUIRE(fm.rfind("---\n", 0) == 0);
    REQUIRE(fm.size() >= 8);
    REQUIRE(fm.substr(fm.size() - 4) == "---\n");
    for (const char* key :
         {"id:", "title:", "source_path:", "format:", "anchor_code:", "anchor_label:", "material_role:",
          "primary_function:", "secondary_domains:", "lens_tags:", "context_tags:", "classification_confidence:",
          "relations:", "reverse_relations:", "decision_note:", "worthiness_class:", "status:"}) {
        INFO("missing key " << key);
        REQUIRE(fm.find(std::string("\n") + key) != std::string::npos);
    }
    REQUIRE(fm.find("status: \"catalogued\"") != std::string::npos);
}

TEST_CASE("Dewey frontmatter writes relations with target, type and tier", "[dewey]") {
    const auto records = linked_pair();
    const std::string fm = to_frontmatter(records[0]);
    REQUIRE(fm.find("- target: \"parent\"") != std::string::npos);
    REQUIRE(fm.find("type: \"part-of\"") != std::string::npos);
    REQUIRE(fm.find("tier: 1") != std::string::npos);
}

TEST_CASE("Dewey frontmatter escapes quotes in values", "[dewey]") {
    Record r;
    r.title = "The \"Big\" Plan";
    REQUIRE(to_frontmatter(r).find("title: \"The \\\"Big\\\" Plan\"") != std::string::npos);
}

TEST_CASE("Dewey duplicate tier follows the evidence ladder", "[dewey]") {
    FileEvidence a{"report.pdf", 1000, 5000, std::nullopt};
    FileEvidence b{"report copy.pdf", 1000, 5001, std::nullopt};

    SECTION("matching hash is Confirmed whatever the names say") {
        a.hash = "abc";
        b.hash = "abc";
        b.name = "totally different.bin";
        REQUIRE(duplicate_tier(a, b) == EvidenceTier::Confirmed);
    }
    SECTION("different hashes mean not duplicates") {
        a.hash = "abc";
        b.hash = "abd";
        REQUIRE(duplicate_tier(a, b) == EvidenceTier::None);
    }
    SECTION("same size, same extension, compatible timestamps is Strong") {
        REQUIRE(duplicate_tier(a, b) == EvidenceTier::Strong);
    }
    SECTION("same size but timestamps far apart is only Probable") {
        b.mtime_seconds = 999999;
        REQUIRE(duplicate_tier(a, b) == EvidenceTier::Probable);
    }
    SECTION("same size but different extension is only Probable") {
        b.name = "report copy.txt";
        REQUIRE(duplicate_tier(a, b) == EvidenceTier::Probable);
    }
    SECTION("filename pattern with no size data is Advisory") {
        a.size.reset();
        b.size.reset();
        REQUIRE(duplicate_tier(a, b) == EvidenceTier::Advisory);
    }
    SECTION("similar names with different sizes is Speculative") {
        b.size = 2000;
        REQUIRE(duplicate_tier(a, b) == EvidenceTier::Speculative);
    }
    SECTION("unrelated names and sizes give None") {
        b.name = "holiday.jpg";
        b.size = 2000;
        REQUIRE(duplicate_tier(a, b) == EvidenceTier::None);
    }
}

TEST_CASE("Dewey duplicate patterns are recognised", "[dewey]") {
    FileEvidence base{"IMG_0847.png", std::nullopt, std::nullopt, std::nullopt};
    for (const char* name : {"IMG_0847 (1).png", "IMG_0847_copy.png", "IMG_0847_v2.png", "IMG_0847_final.png"}) {
        FileEvidence other{name, std::nullopt, std::nullopt, std::nullopt};
        INFO(name);
        REQUIRE(duplicate_tier(base, other) == EvidenceTier::Advisory);
    }
}

TEST_CASE("Dewey keeper nomination needs Probable or better", "[dewey]") {
    REQUIRE_FALSE(can_nominate_keeper(EvidenceTier::None));
    REQUIRE_FALSE(can_nominate_keeper(EvidenceTier::Speculative));
    REQUIRE_FALSE(can_nominate_keeper(EvidenceTier::Advisory));
    REQUIRE(can_nominate_keeper(EvidenceTier::Probable));
    REQUIRE(can_nominate_keeper(EvidenceTier::Strong));
    REQUIRE(can_nominate_keeper(EvidenceTier::Confirmed));
}

TEST_CASE("Dewey never deletes without Confirmed evidence and approval", "[dewey]") {
    REQUIRE(recommend_action(EvidenceTier::Confirmed, true) == DuplicateAction::Delete);
    REQUIRE(recommend_action(EvidenceTier::Confirmed, false) == DuplicateAction::Archive);
    REQUIRE(recommend_action(EvidenceTier::Strong, true) == DuplicateAction::Archive);
    REQUIRE(recommend_action(EvidenceTier::Probable, true) == DuplicateAction::Archive);
    REQUIRE(recommend_action(EvidenceTier::Advisory, true) == DuplicateAction::Review);
    REQUIRE(recommend_action(EvidenceTier::Speculative, true) == DuplicateAction::Review);
    REQUIRE(recommend_action(EvidenceTier::None, true) == DuplicateAction::Keep);
}
