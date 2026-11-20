#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

/**
 * @brief Deterministic rules from the DeweyGraph archive model: vocabulary, relation
 *        reversal, graph verification, record validation, naming and duplicate evidence.
 *
 * Judgement calls (folder typology, worthiness, anchor choice) stay with the model.
 * Nothing here touches the filesystem, so every rule can be tested on its own.
 */
namespace DeweyEngine {

/** @brief One legal anchor code with its fixed label. */
struct AnchorEntry {
    std::string code;
    std::string label;
};

/** @brief Full legal anchor vocabulary, in the order the skill lists it. */
const std::vector<AnchorEntry>& anchor_vocabulary();

/** @brief Label for a legal anchor code, or nullopt when the code is not in the vocabulary. */
std::optional<std::string> anchor_label(const std::string& code);

bool is_legal_format(const std::string& value);
bool is_legal_lens(const std::string& value);
bool is_legal_role(const std::string& value);
bool is_legal_worthiness(const std::string& value);
bool is_legal_confidence(const std::string& value);

/**
 * @brief Reverse of a relation type: symmetric types return themselves, asymmetric types their pair.
 * @return nullopt when the type is not a legal relation type.
 */
std::optional<std::string> reverse_relation_type(const std::string& type);

/** @brief One edge on a record. Tier is 1 (structural), 2 (substantive) or 3 (associative). */
struct Relation {
    std::string target;
    std::string type;
    int tier{2};
};

/** @brief One catalogued item, mirroring the skill's frontmatter schema. */
struct Record {
    std::string id;
    std::string title;
    std::string source_path;
    std::string format;
    std::string anchor_code;
    std::string anchor_label;
    std::string material_role;
    std::string primary_function;
    std::vector<std::string> secondary_domains;
    std::vector<std::string> lens_tags;
    std::vector<std::string> context_tags;
    std::string classification_confidence;
    std::vector<Relation> relations;
    std::vector<Relation> reverse_relations;
    std::string decision_note;
    std::string worthiness_class;
    std::string status{"catalogued"};
};

/** @brief Result of match-checking every relation against its required reverse. */
struct GraphReport {
    std::size_t total{0};
    std::size_t matched{0};
    std::size_t missing{0};
    std::size_t dangling{0};
    bool pass{false};
    std::vector<std::string> problems;

    /** @brief "PASS|FAIL: n/m matched, x missing, y dangling". */
    std::string summary() const;
};

/** @brief Flatten all relations, require each triple's reverse by exact match, and count the gaps. */
GraphReport verify_graph(const std::vector<Record>& records);

/** @brief Problems that stop a record being written; empty means it passed all eleven checks. */
std::vector<std::string> validate_record(const Record& record, const std::vector<Record>& all_records);

/** @brief True when a record carries more relations than the suggested cap of six. */
bool over_relation_cap(const Record& record);

enum class NoteDepth { None, Short, Full };

/** @brief How much of a note a role earns. Process earns Full only inside a project corpus. */
NoteDepth note_depth(const std::string& role, bool in_project_corpus);

/** @brief "DDD-kebab-case" folder name from a three-digit class and a title. */
std::string folder_name(const std::string& three_digit_class, const std::string& title);

/** @brief "<anchor>-<kebab-title>.md" note file name. */
std::string note_file_name(const std::string& anchor_code, const std::string& title);

/** @brief YAML frontmatter with every field present, even when empty. */
std::string to_frontmatter(const Record& record);

/** @brief What is known about one file when comparing it with another. */
struct FileEvidence {
    std::string name;
    std::optional<std::uint64_t> size;
    std::optional<std::int64_t> mtime_seconds;
    std::optional<std::string> hash;
};

enum class EvidenceTier { None, Speculative, Advisory, Probable, Strong, Confirmed };

/** @brief Strongest honest tier two files support. */
EvidenceTier duplicate_tier(const FileEvidence& a, const FileEvidence& b);

/** @brief A keeper may be nominated at Probable or above. */
bool can_nominate_keeper(EvidenceTier tier);

enum class DuplicateAction { Keep, Archive, Review, Delete };

/** @brief Action for a non-keeper copy. Delete needs a Confirmed tier and explicit approval. */
DuplicateAction recommend_action(EvidenceTier tier, bool explicit_user_approval);

}  // namespace DeweyEngine
