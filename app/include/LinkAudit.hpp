#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/**
 * @brief Structured two-way contracts between skills, and the audit that checks them. A contract is a Markdown
 *        block: "### > target" (this skill sends) or "### < source" (this skill receives), then labelled fields.
 *        The audit is link-matching, not block-presence: every outbound block needs an inbound block at the other
 *        end and the reverse, with real content in the fields. Contract-shaped text inside code fences is
 *        documentation and is ignored.
 */
namespace LinkAudit {

enum class Direction { Outbound, Inbound };

struct SkillFile {
    /** @brief The skill this file belongs to. */
    std::string skill;
    /** @brief Path used in reports. */
    std::string file;
    std::string text;
};

struct Contract {
    Direction direction{Direction::Outbound};
    std::string skill;
    std::string file;
    /** @brief 1-based line of the heading. */
    int line{0};
    /** @brief The other end as written, such as "skill-b/sub-skill". */
    std::string other;
    /** @brief Sends (outbound) or Receives (inbound). */
    std::string data;
    /** @brief Action for target (outbound) or Action here (inbound). */
    std::string action;
    std::string feedback;
};

/** @brief The skill part of Contract::other, before any "/". */
std::string other_skill(const Contract& contract);

std::vector<Contract> parse_contracts(const SkillFile& file);

/** @brief Empty, "TBD", "TODO", "[fill in]", or an unfilled "[template text]". */
bool is_placeholder(const std::string& value);

/** @brief One or two words ending in a generic noun such as data, information, output or analysis. */
bool is_vague_data(const std::string& value);

/** @brief Fewer than three words, or one of the stock non-instructions such as "use as needed". */
bool is_vague_action(const std::string& value);

/** @brief "none", or a trigger of four or more words that names a target (a backticked name, a path, or the other
 * skill). */
bool feedback_is_valid(const std::string& value, const std::string& other_skill_name);

enum class Severity { Critical, High, Medium, Low };

struct Finding {
    Severity severity{Severity::Low};
    std::string file;
    int line{0};
    std::string code;
    std::string message;
    /** @brief True when the finding is about a contract block, so that block counts as failed. */
    bool on_contract{false};
};

struct Report {
    std::size_t checked{0};
    std::size_t passed{0};
    std::size_t failed{0};
    /** @brief Most severe first, then by file and line. */
    std::vector<Finding> findings;
};

/**
 * @brief Audit every file given. The scope is whatever is passed in: a target that is not among the skills
 *        provided is reported as missing, so a single-skill audit must include its neighbours.
 *
 * Critical: missing-backlink, missing-forward, missing-target, missing-source.
 * High: empty-field. Medium: vague-field, bad-feedback, see-also.
 * Low: prose-mention, orphan-skill, missing-chain-annotation.
 *
 * missing-chain-annotation: for x > y > z, the block "y > z" never names x in its fields and x has no contract with z.
 * That is a heuristic for the skill's chain rule 4 ("data originated from x"); it cannot tell a transformed chain
 * from a pass-through one, which is why it is only ever Low.
 *
 * A skill pair counts as linked for the prose checks if a contract names either end. An orphan is a skill with no
 * contract of its own that no other skill's contract names.
 */
Report audit(const std::vector<SkillFile>& files);

/** @brief The report in the skill's format. Pass the scope and date as text; nothing here reads a clock. */
std::string render_report(const Report& report, const std::string& scope, const std::string& date);

/** @brief Failed audits that are retried before a human has to look. */
inline constexpr int kMaxAuditCycles = 3;
bool must_escalate(int failed_cycles);

struct Cell {
    bool forward{false};
    bool backlink{false};
};

/** @brief (source skill, target skill) to which ends exist. Asymmetric cells are broken links. */
std::map<std::pair<std::string, std::string>, Cell> adjacency(const std::vector<Contract>& contracts);

struct Chain {
    std::vector<std::string> skills;
    /** @brief True when the last skill feeds the first again. */
    bool loops_back{false};
};

/**
 * @brief Every maximal chain of three or more skills along outbound contracts. Loops appear once, rotated to start
 *        at the alphabetically first skill. The search stops after max_chains candidates, so a very large ecosystem
 *        gets a truncated list rather than a hang.
 */
std::vector<Chain> find_chains(const std::vector<Contract>& contracts, std::size_t max_chains = 1000);

struct Mention {
    std::string skill;
    std::string file;
    int line{0};
    std::string text;
};

/** @brief Everything outside a skill that refers to it, in the order the unwiring procedure updates them. */
struct BlastRadius {
    /** @brief "### < departing" blocks: skills that receive from it. */
    std::vector<Mention> receivers;
    /** @brief "### > departing" blocks: skills that send to it. */
    std::vector<Mention> senders;
    /** @brief Other mentions on a line that talks about a chain or where data originated. */
    std::vector<Mention> chain_annotations;
    /** @brief Remaining plain mentions. */
    std::vector<Mention> prose;

    std::vector<Mention> in_update_order() const;
};

BlastRadius blast_radius(const std::vector<SkillFile>& files, const std::string& departing);

/** @brief A replacement may take over only when it has contracts and none of its own findings are Critical or High. */
bool replacement_is_wired(const std::vector<SkillFile>& files, const std::string& replacement);

enum class LinkType {
    TechnicalDependency,
    SupportingWorkflow,
    QualityControl,
    SharedDomain,
    OutputStyle,
    SimilarFunction,
    CreativeInfluence,
    UserDefined
};

/** @brief 10, 8, 8, 6, 6, 4, 3. User-defined takes the weight the user gives. */
int weight(LinkType type, int user_weight = 0);
std::string type_name(LinkType type);
std::optional<LinkType> parse_type(const std::string& name);

/** @brief The heavier of two types, or nullopt when their weights tie. */
std::optional<LinkType> heavier(LinkType a, LinkType b, int user_weight_a = 0, int user_weight_b = 0);

/**
 * @brief One contract block in the skill's format, or nullopt when any field is a placeholder, vague, or the
 *        feedback loop is neither "none" nor a named trigger. A wrong contract is worse than no contract.
 */
std::optional<std::string> render_contract(const Contract& contract);

/** @brief "## Skill relationships" with inbound blocks first, then outbound, each alphabetical by other end. */
std::optional<std::string> render_relationships_section(const std::vector<Contract>& contracts);

}  // namespace LinkAudit
