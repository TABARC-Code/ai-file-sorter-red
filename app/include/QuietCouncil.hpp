#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

/**
 * @brief The deterministic half of the Quiet Council research skill: the thirteen-researcher registry, depth and
 *        cost rules, domain routing, audience calibration, the rules that grade contradictions and steelman
 *        results, the Kaizen flag checks, and the plain-Markdown knowledge graph (confirmed facts, disputes,
 *        topic index, session log, wiki pages). The search calls and every judgement step stay outside: this
 *        module never touches the network and never reads the clock.
 */
namespace QuietCouncil {

struct Researcher {
    std::string name;
    std::string lens;
    double temperature{0.0};
};

/** @brief The thirteen researchers in dispatch order; The Contrarian is last. */
const std::vector<Researcher>& researchers();

bool is_contrarian(const std::string& name);

enum class Depth { Quick, Standard, Deep, Academic };

const char* depth_name(Depth depth);

std::optional<Depth> parse_depth(const std::string& text);

struct Models {
    std::string researcher;
    std::string contrarian;
};

/** @brief Search model for the twelve lenses and for The Contrarian at a depth. */
Models models_for(Depth depth);

struct CostEstimate {
    Models models;
    double researcher_usd{0.0};
    double contrarian_usd{0.0};
    double total_usd{0.0};
};

/**
 * @brief Rough price before dispatch, assuming 2k tokens in and 2k out per call. The per-model prices are the
 *        skill's own figures and will drift; treat the result as an order of magnitude.
 */
CostEstimate estimate_cost(Depth depth, int lens_count = 12);

/** @brief Academic depth must be acknowledged by the user before anything is dispatched. */
bool needs_cost_ack(Depth depth);

/**
 * @brief "session-YYYY-MM-DD-NNN". NNN is one more than the highest number already logged for that date, so a
 *        gap in the log can never produce a repeat.
 */
std::string next_session_id(const std::string& date, const std::string& session_log_text);

/** @brief "YYYY-MM-DD" from a session id, or nullopt. */
std::optional<std::string> session_date(const std::string& session_id);

struct DomainConfig {
    std::string domain_class{"GENERAL"};
    /** @brief Researcher name to weight; a researcher not listed weighs 1.0. */
    std::map<std::string, double> researcher_weights;
    std::map<std::string, double> temperature_overrides;
    /** @brief Researcher name, or "all", to the extra guidance for the prompt. */
    std::map<std::string, std::string> prompt_addenda;
};

/** @brief The configured class by name, or the General config if the name is not one of the six. */
DomainConfig domain_config(const std::string& domain_class);

/** @brief Keywords of each class that the text mentions, by class name. Only classes with a hit appear. */
std::map<std::string, int> domain_scores(const std::string& text);

/**
 * @brief Pick the domain for a topic. A class that scores twice the runner-up wins outright; otherwise the two
 *        leading classes are blended. A keyword matches at the start of a word; one of three letters or fewer
 *        must be the whole word (an "s" or "es" ending allowed), so "AI" no longer fires inside "Spain".
 */
DomainConfig classify_domain(const std::string& topic, const std::vector<std::string>& constraints = {});

/**
 * @brief Average two classes' weights (a researcher one class leaves out counts as 1.0), to two decimals. Class
 *        a's temperature overrides and prompt addenda win on a clash. Every configured weight is between 0.5 and
 *        1.6, so an average cannot leave that range.
 */
DomainConfig merge_domain_configs(const std::string& a, const std::string& b);

double weight_for(const DomainConfig& config, const std::string& researcher);

double temperature_for(const DomainConfig& config, const Researcher& researcher);

enum class Persona { Analyst, Journalist, PolicyMaker, TechnicalSpecialist, GeneralPublic };

const char* persona_name(Persona persona);

/** @brief Accepts "policy maker", "policy-maker", "POLICY_MAKER", "journalist / writer" and similar. */
std::optional<Persona> parse_persona(const std::string& text);

/** @brief An explicit audience if it parses, then a hint from the output format, then Analyst. */
Persona detect_audience(const std::string& explicit_audience, const std::string& output_format);

struct Calibration {
    Persona persona{Persona::Analyst};
    std::string register_style;
    std::string depth;
    std::string executive_summary_style;
    std::string contradiction_framing;
    std::string source_format;
    std::string confidence_format;
    std::string technical_terms;
    std::string sentence_length;
    std::string section_emphasis;
    std::vector<std::string> additional_sections;
    /** @brief Skill to hand the synthesis to afterwards, empty for none. */
    std::string route_after_synthesis;
};

Calibration calibration_for(Persona persona);

enum class ContradictionType { A, B, C, D, E };
enum class Priority { Low, Medium, High, Critical };
enum class Confidence { Low, Medium, High };
enum class Status { Confirmed, Disputed, Definitional, NeedsMore, TypeEStale };
enum class SteelmanOutcome { Survives, Weakened, Defeated };
enum class Adjustment { Raise, Maintain, Lower };
enum class SteelmanRule { Required, Recommended, Skip };
enum class Destination { ConfirmedFacts, DisputedClaims, RecommendedQueries, SessionLog, ReconfirmationQueue };

/** @brief "TYPE_A" to "TYPE_E". */
const char* type_name(ContradictionType type);
std::optional<ContradictionType> parse_type(const std::string& text);
const char* priority_name(Priority priority);
std::optional<Priority> parse_priority(const std::string& text);
/** @brief "NEEDS_MORE", "TYPE_E_STALE" and so on. */
const char* status_name(Status status);
std::optional<Status> parse_status(const std::string& text);
const char* outcome_name(SteelmanOutcome outcome);
std::optional<SteelmanOutcome> parse_outcome(const std::string& text);
const char* adjustment_name(Adjustment adjustment);

/**
 * @brief Whether a contradiction must be steelmanned before it is logged. Contrarian challenges (D) and
 *        high-against-high factual conflicts (A) must be; any other factual conflict that is not low against
 *        low should be; framing (B, C) and cross-session (E) conflicts are skipped, since E is re-researched
 *        rather than argued.
 */
SteelmanRule steelman_rule(ContradictionType type, Confidence a, Confidence b);

/** @brief A factual conflict, high against high, that is not settled needs a follow-up query before synthesis. */
bool needs_followup_query(ContradictionType type, Confidence a, Confidence b, Status status);

Adjustment adjustment_for(SteelmanOutcome outcome);

/** @brief Survives lowers the priority one step (never below Low), weakened keeps it, defeated makes it Critical. */
Priority apply_steelman(Priority priority, SteelmanOutcome outcome);

/** @brief Where a resolved or unresolved contradiction is written. */
std::vector<Destination> destinations_for(Status status);

/** @brief A confirmed claim leaves the disputed register. */
bool removes_from_disputed(Status status);

struct DisputeRef {
    ContradictionType type{ContradictionType::A};
    Status status{Status::Disputed};
    std::string subtopic;
};

/** @brief Subtopics with three or more disputed factual conflicts, in order of first appearance. */
std::vector<std::string> gap_clusters(const std::vector<DisputeRef>& disputes);

struct ResearcherStat {
    std::string name;
    bool ok{true};
    int citations{0};
};

struct SessionMetrics {
    std::vector<ResearcherStat> researchers;
    int tier1{0};
    int tier2{0};
    int tier3{0};
    int tier4{0};
    int contradictions_found{0};
    bool contrarian_agreed_on_everything{false};
    bool kg_checked{true};
    int type_e_uninvestigated{0};
    int gaps{0};
    int gaps_with_followup{0};
    /** @brief Synthesis claims with no citation, as counted by the caller. */
    int unsourced_claims{0};
};

struct Flag {
    std::string type;
    std::string detail;
    std::string action;
};

/** @brief The eight Kaizen flag names in the quality checker's order. */
const std::vector<std::string>& kaizen_flag_types();

/** @brief Flags raised by a finished session. LOW_CITATION_RESEARCHER appears once per researcher at fault. */
std::vector<Flag> kaizen_flags(const SessionMetrics& metrics);

/**
 * @brief Flag types that fired in each of the last `streak` sessions. history runs oldest to newest, one list of
 *        flag type names per session; fewer than `streak` sessions never qualifies.
 */
std::vector<std::string> mandatory_improvements(const std::vector<std::vector<std::string>>& history,
                                                std::size_t streak = 3);

/** @brief Share of sources in Tier 1 or 2, 0 for an empty registry. */
double tier_1_2_share(const SessionMetrics& metrics);

/** @brief Fewer than 300 words. */
bool executive_summary_ok(const std::string& text);

/** @brief Title-case phrases of two or more words, most frequent first, ties in order of first appearance. */
struct Entity {
    std::string concept_name;
    int count{0};
};
std::vector<Entity> extract_entities(const std::vector<std::string>& texts, std::size_t limit = 15);

/** @brief Lower-cased, punctuation removed, runs of spaces and underscores to a hyphen, 80 characters at most. */
std::string slug(const std::string& topic);

/** @brief True when a line of the text is exactly "## " followed by the heading. */
bool has_section(const std::string& text, const std::string& heading);

struct ConfirmedFact {
    std::string id{"FACT-XXX"};
    std::string topic_slug;
    std::string claim;
    std::vector<std::string> confirmed_by;
    std::vector<std::string> sessions;
    /** @brief Global tags such as "[SOURCE_4]". */
    std::vector<std::string> citations;
    std::string confidence{"HIGH"};
    std::string temporal_scope;
    std::string notes;
};

/** @brief Markdown to append to confirmed-facts.md. The topic heading is added if the file lacks it. */
std::string render_confirmed_fact(const ConfirmedFact& fact, bool section_exists);

struct DisputeClaim {
    std::string text;
    std::string researcher;
    std::string session;
    std::string citation;
};

struct Dispute {
    std::string id{"DISPUTE-XXX"};
    std::string topic_slug;
    ContradictionType type{ContradictionType::A};
    Priority priority{Priority::High};
    DisputeClaim claim_a;
    DisputeClaim claim_b;
    Status status{Status::NeedsMore};
    std::string resolution_note;
    std::string recommended_query;
};

/** @brief Markdown to append to disputed-claims.md: the heading when new, and always the dispute itself. */
std::string render_disputed_claim(const Dispute& dispute, bool section_exists, const std::string& date);

struct TopicEntry {
    std::string topic;
    std::string session_id;
    std::vector<std::string> key_entities;
    int confirmed_count{0};
    int dispute_count{0};
    std::string confidence{"MEDIUM"};
};

std::string render_topic_index_entry(const TopicEntry& entry, const std::string& date);

struct SessionLogEntry {
    std::string session_id;
    std::string topic;
    Depth depth{Depth::Standard};
    int researchers_completed{0};
    int total_sources{0};
    double tier_1_2_share{0.0};
    int contradictions_found{0};
    int confirmed_facts_added{0};
    int disputed_claims_added{0};
    std::vector<Flag> flags;
    std::string notes;
};

std::string render_session_log_entry(const SessionLogEntry& entry, const std::string& date);

struct SpiderEntity {
    std::string concept_name;
    std::vector<std::string> links;
    std::string context;
};

struct WikiInput {
    std::string topic;
    std::string session_id;
    std::string date;
    std::string depth;
    int researchers_completed{0};
    int contradictions_found{0};
    std::string confidence;
    std::string executive_summary;
    std::vector<SpiderEntity> entities;
    std::vector<std::string> confirmed_facts;
    std::vector<std::string> active_disputes;
    std::vector<std::string> recommended_queries;
};

/**
 * @brief Fill the wiki page template. The template's sections are rewritten by heading, so nothing of the
 *        placeholder text is left behind. Sections the template lacks are skipped.
 */
std::string render_wiki_page(const std::string& template_text, const WikiInput& input);

/** @brief Append to a file, creating folders. A failure is added to `failures`, never thrown. */
bool append_file(const std::filesystem::path& path, const std::string& content, std::vector<std::string>& failures);

/** @brief Create a file only if absent. An existing file is left alone and reported as a failure. */
bool create_file_once(const std::filesystem::path& path, const std::string& content,
                      std::vector<std::string>& failures);

/** @brief The whole file, or empty when it cannot be read. */
std::string read_file(const std::filesystem::path& path);

/** @brief Facts as written by render_confirmed_fact and the skill's own schema. */
std::vector<ConfirmedFact> parse_confirmed_facts(const std::string& text);

/** @brief Facts filed under a topic slug, in file order. */
std::vector<ConfirmedFact> facts_for_topic(const std::vector<ConfirmedFact>& facts, const std::string& topic_slug);

/** @brief The prior-knowledge block for researcher prompts: the first `limit` claims, or empty for none. */
std::string prior_knowledge_block(const std::vector<ConfirmedFact>& facts, std::size_t limit = 5);

/** @brief Earliest session date of a fact, the closest thing the file has to an "established" date. */
std::optional<std::string> fact_date(const ConfirmedFact& fact);

/** @brief Indexes of facts whose age costs at least threshold_percent under the domain's decay class. */
std::vector<std::size_t> stale_fact_indexes(const std::vector<ConfirmedFact>& facts, const std::string& domain_class,
                                            const std::string& today, double threshold_percent = 20.0);

}  // namespace QuietCouncil
