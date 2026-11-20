#include "QuietCouncil.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>

#include "SourceLedger.hpp"

namespace QuietCouncil {

namespace {

const char* const kEmDash = "\xE2\x80\x94";
const char* const kArrow = "\xE2\x86\x92";

std::string lower_ascii(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return text;
}

bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

bool is_alnum(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

/** A byte that can be part of a word: ASCII letters and digits, underscore, and any byte of a UTF-8 sequence. */
bool is_word_byte(char c) {
    return is_alnum(c) || c == '_' || static_cast<unsigned char>(c) >= 0x80;
}

const char* const kBlanks = " \t\n\r\f\v";

std::string trim(const std::string& text) {
    const std::size_t first = text.find_first_not_of(kBlanks);
    if (first == std::string::npos) {
        return "";
    }
    return text.substr(first, text.find_last_not_of(kBlanks) - first + 1);
}

std::string join(const std::vector<std::string>& items, const std::string& separator) {
    std::string out;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i > 0) {
            out += separator;
        }
        out += items[i];
    }
    return out;
}

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (true) {
        const std::size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            lines.push_back(text.substr(start));
            return lines;
        }
        lines.push_back(text.substr(start, end - start));
        start = end + 1;
    }
}

std::vector<std::string> split_commas(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream pieces(text);
    std::string piece;
    while (std::getline(pieces, piece, ',')) {
        piece = trim(piece);
        if (!piece.empty()) {
            out.push_back(piece);
        }
    }
    return out;
}

bool starts_with(const std::string& text, const std::string& prefix) {
    return text.compare(0, prefix.size(), prefix) == 0;
}

double round_places(double value, double scale) {
    return std::round(value * scale) / scale;
}

std::string percent_text(double share) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(1) << share * 100.0 << "%";
    return out.str();
}

/** "a b-c" and "A_B" become "a_b_c" and "a_b"; empty ends are dropped. */
std::string normalise_key(const std::string& text) {
    std::string out;
    for (const char c : text) {
        if (is_alnum(c)) {
            out += lower_ascii(std::string(1, c));
        } else if (!out.empty() && out.back() != '_') {
            out += '_';
        }
    }
    while (!out.empty() && out.back() == '_') {
        out.pop_back();
    }
    return out;
}

// ---- UTF-8 for slugs ------------------------------------------------------------------------------------------

std::size_t sequence_length(unsigned char lead) {
    if (lead < 0x80) {
        return 1;
    }
    if (lead >= 0xC2 && lead <= 0xDF) {
        return 2;
    }
    if (lead >= 0xE0 && lead <= 0xEF) {
        return 3;
    }
    return lead >= 0xF0 && lead <= 0xF4 ? 4 : 0;
}

bool has_continuations(const std::string& text, std::size_t at, std::size_t length) {
    if (at + length > text.size()) {
        return false;
    }
    for (std::size_t k = 1; k < length; ++k) {
        if ((static_cast<unsigned char>(text[at + k]) & 0xC0U) != 0x80U) {
            return false;
        }
    }
    return true;
}

/** Code points of valid UTF-8. A stray, truncated or malformed byte is skipped on its own. */
std::vector<char32_t> decode_utf8(const std::string& text) {
    static const unsigned char lead_masks[] = {0x7F, 0x1F, 0x0F, 0x07};
    std::vector<char32_t> out;
    std::size_t i = 0;
    while (i < text.size()) {
        const auto lead = static_cast<unsigned char>(text[i]);
        const std::size_t length = sequence_length(lead);
        if (length == 0 || !has_continuations(text, i, length)) {
            ++i;
            continue;
        }
        char32_t code = lead & lead_masks[length - 1];
        for (std::size_t k = 1; k < length; ++k) {
            code = (code << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3FU);
        }
        out.push_back(code);
        i += length;
    }
    return out;
}

void encode_utf8(char32_t code, std::string& out) {
    if (code < 0x80) {
        out += static_cast<char>(code);
    } else if (code < 0x800) {
        out += static_cast<char>(0xC0U | (code >> 6));
        out += static_cast<char>(0x80U | (code & 0x3FU));
    } else if (code < 0x10000) {
        out += static_cast<char>(0xE0U | (code >> 12));
        out += static_cast<char>(0x80U | ((code >> 6) & 0x3FU));
        out += static_cast<char>(0x80U | (code & 0x3FU));
    } else {
        out += static_cast<char>(0xF0U | (code >> 18));
        out += static_cast<char>(0x80U | ((code >> 12) & 0x3FU));
        out += static_cast<char>(0x80U | ((code >> 6) & 0x3FU));
        out += static_cast<char>(0x80U | (code & 0x3FU));
    }
}

struct CodeRange {
    char32_t first;
    char32_t last;
};

bool in_any(char32_t code, const std::vector<CodeRange>& ranges) {
    return std::any_of(ranges.begin(), ranges.end(),
                       [code](const CodeRange& r) { return code >= r.first && code <= r.last; });
}

/** What Python's \s matches beyond ASCII. */
bool is_unicode_space(char32_t c) {
    static const std::vector<CodeRange> spaces = {{0x85, 0x85},     {0xA0, 0xA0},     {0x1680, 0x1680},
                                                  {0x2000, 0x200A}, {0x2028, 0x2029}, {0x202F, 0x202F},
                                                  {0x205F, 0x205F}, {0x3000, 0x3000}};
    return in_any(c, spaces);
}

/**
 * Characters Python's \w would not keep: controls, Latin-1 symbols (the letters at 0xAA, 0xB5 and 0xBA stay), the
 * multiplication and division signs, General Punctuation, currency, arrows and symbols, CJK punctuation, variation
 * selectors and the emoji planes. Spaces are tested first, so they are not listed here.
 */
bool is_unicode_punctuation(char32_t c) {
    static const std::vector<CodeRange> marks = {
        {0x80, 0x9F},     {0xA1, 0xBF},     {0xD7, 0xD7},     {0xF7, 0xF7},     {0x200B, 0x206F},
        {0x20A0, 0x20CF}, {0x2190, 0x2BFF}, {0x3001, 0x303F}, {0xFE00, 0xFE0F}, {0x1F000, 0x1FAFF}};
    static const std::vector<CodeRange> latin_letters = {{0xAA, 0xAA}, {0xB5, 0xB5}, {0xBA, 0xBA}};
    return in_any(c, marks) && !in_any(c, latin_letters);
}

char32_t lower_code(char32_t c) {
    if (c >= 'A' && c <= 'Z') {
        return c + 32;
    }
    return c >= 0xC0 && c <= 0xDE && c != 0xD7 ? c + 32 : c;
}

enum class SlugClass { Space, Drop, Keep };

SlugClass slug_class(char32_t code) {
    if (code == '_' || is_unicode_space(code) || (code < 0x80 && is_space(static_cast<char>(code)))) {
        return SlugClass::Space;
    }
    if (code < 0x80) {
        return is_alnum(static_cast<char>(code)) || code == '-' ? SlugClass::Keep : SlugClass::Drop;
    }
    return is_unicode_punctuation(code) ? SlugClass::Drop : SlugClass::Keep;
}

/** Non-ASCII spaces become a space and non-ASCII punctuation becomes a full stop, so ASCII rules can run on it. */
std::string fold_separators(const std::string& text) {
    std::string out;
    for (const char32_t code : decode_utf8(text)) {
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (is_unicode_space(code)) {
            out += ' ';
        } else if (is_unicode_punctuation(code)) {
            out += '.';
        } else {
            encode_utf8(code, out);
        }
    }
    return out;
}

// ---- researchers ----------------------------------------------------------------------------------------------

const std::vector<Researcher> kResearchers = {
    {"The Historian", "Chronological context, precedent, origin", 0.25},
    {"The Scientist", "Empirical evidence, mechanism, replication", 0.20},
    {"The Economist", "Incentives, costs, markets, distributional effects", 0.30},
    {"The Sociologist", "Social structure, institutions, group behaviour", 0.35},
    {"The Technologist", "Technical feasibility, systems, implementation", 0.30},
    {"The Ethicist", "Moral stakes, values conflicts, harm and consent", 0.40},
    {"The Lawyer", "Legal frameworks, jurisdiction, liability, precedent", 0.20},
    {"The Psychologist", "Cognition, motivation, individual behaviour", 0.30},
    {"The Journalist", "Public narrative, investigation, who said what", 0.50},
    {"The Futurist", "Trajectories, scenarios, second-order effects", 0.50},
    {"The Practitioner", "Real-world practice, what works on the ground", 0.40},
    {"The Philosopher", "Concepts, definitions, assumptions, argument validity", 0.35},
    {"The Contrarian", "Inversion, adversarial challenge, what everyone missed", 0.70},
};

// ---- cost -----------------------------------------------------------------------------------------------------

struct Price {
    double per_1k_in;
    double per_1k_out;
    double flat;
};

Price price_of(const std::string& model) {
    if (model == "sonar-pro") {
        return {0.003, 0.015, 0.0};
    }
    if (model == "sonar-deep-research") {
        return {0.002, 0.008, 5.00};
    }
    return {0.001, 0.001, 0.0};
}

double call_cost(const std::string& model) {
    const Price price = price_of(model);
    return 2.0 * price.per_1k_in + 2.0 * price.per_1k_out + price.flat;
}

// ---- domain tables --------------------------------------------------------------------------------------------

struct DomainKeywords {
    const char* domain;
    std::vector<const char*> keywords;
};

const std::vector<DomainKeywords>& domain_keywords() {
    static const std::vector<DomainKeywords> table = {
        {"SCIENTIFIC_MEDICAL",
         {"trial",         "study",      "efficacy",   "drug",      "treatment", "clinical",     "disease",
          "health",        "vaccine",    "genome",     "neural",    "therapy",   "mechanism",    "evidence",
          "meta-analysis", "randomised", "randomized", "placebo",   "dosage",    "pharmacology", "epidemiology",
          "pathogen",      "diagnosis",  "symptom",    "mortality", "morbidity", "NHS",          "FDA",
          "MHRA",          "EMA",        "WHO",        "cancer",    "surgery",   "patient"}},
        {"LEGAL_REGULATORY",
         {"law",       "regulation",  "compliance",  "statute",     "ruling",       "court",    "GDPR",
          "rights",    "liability",   "legislation", "enforcement", "jurisdiction", "contract", "patent",
          "copyright", "tort",        "criminal",    "civil",       "appeals",      "tribunal", "sanctions",
          "ICO",       "FCA",         "Ofcom",       "CMA",         "SEC",          "FTC",      "legal",
          "judge",     "prosecution", "defendant",   "plaintiff",   "precedent",    "case law"}},
        {"TECHNOLOGY_AI",
         {"AI",
          "machine learning",
          "neural network",
          "LLM",
          "model",
          "algorithm",
          "software",
          "hardware",
          "API",
          "platform",
          "infrastructure",
          "deployment",
          "architecture",
          "ML",
          "deep learning",
          "transformer",
          "GPU",
          "data centre",
          "cloud",
          "open source",
          "benchmark",
          "inference",
          "training",
          "fine-tuning",
          "autonomous",
          "robotics",
          "semiconductor",
          "chip",
          "quantum computing"}},
        {"POLITICAL",
         {"election",   "policy",       "government",     "party",          "war",      "conflict",  "diplomacy",
          "sanctions",  "trade",        "foreign policy", "sovereignty",    "power",    "state",     "parliament",
          "congress",   "senate",       "president",      "prime minister", "minister", "campaign",  "ballot",
          "vote",       "constituency", "geopolitics",    "NATO",           "UN",       "bilateral", "multilateral",
          "referendum", "coup",         "protest",        "democracy"}},
        {"ECONOMIC_FINANCIAL",
         {"market",       "economy",       "inflation", "GDP",        "investment",      "trade",         "currency",
          "bank",         "interest rate", "recession", "growth",     "fiscal",          "monetary",      "debt",
          "bond",         "equity",        "stock",     "hedge fund", "venture capital", "IPO",           "M&A",
          "supply chain", "tariff",        "subsidy",   "tax",        "budget",          "deficit",       "surplus",
          "unemployment", "wage",          "pension",   "insurance",  "fintech",         "cryptocurrency"}},
        {"SOCIAL_CULTURAL", {"culture",   "society",   "community",    "identity",      "race",    "gender",
                             "class",     "religion",  "inequality",   "education",     "welfare", "behaviour",
                             "norms",     "ethnicity", "migration",    "immigration",   "poverty", "discrimination",
                             "diversity", "inclusion", "social media", "mental health", "family",  "marriage",
                             "crime",     "addiction", "homelessness", "youth",         "ageing"}},
    };
    return table;
}

using StringMap = std::map<std::string, double>;
using TextMap = std::map<std::string, std::string>;

DomainConfig make_config(const std::string& name, StringMap weights, StringMap temperatures, TextMap addenda) {
    DomainConfig config;
    config.domain_class = name;
    config.researcher_weights = std::move(weights);
    config.temperature_overrides = std::move(temperatures);
    config.prompt_addenda = std::move(addenda);
    return config;
}

DomainConfig scientific_config() {
    return make_config(
        "SCIENTIFIC_MEDICAL",
        {{"The Scientist", 1.5},
         {"The Lawyer", 1.2},
         {"The Ethicist", 1.2},
         {"The Practitioner", 1.3},
         {"The Futurist", 0.7},
         {"The Journalist", 0.8}},
        {{"The Scientist", 0.15}},
        {{"all",
          "Focus on peer-reviewed sources. Note study design, sample sizes, and replication status. Do not report "
          "press releases about studies as if they were the study."},
         {"The Scientist",
          "Distinguish between RCTs, cohort studies, and meta-analyses. Flag any conflict of interest in funding "
          "sources."}});
}

DomainConfig legal_config() {
    return make_config(
        "LEGAL_REGULATORY",
        {{"The Lawyer", 1.6},
         {"The Economist", 1.2},
         {"The Historian", 1.1},
         {"The Philosopher", 1.1},
         {"The Futurist", 0.6}},
        {{"The Lawyer", 0.15}},
        {{"The Lawyer",
          "Always specify jurisdiction. Note when law is unsettled or contested. Flag pending legislation or "
          "recent rulings that may change the picture."}});
}

DomainConfig technology_config() {
    return make_config(
        "TECHNOLOGY_AI",
        {{"The Technologist", 1.5},
         {"The Futurist", 1.3},
         {"The Economist", 1.1},
         {"The Ethicist", 1.1},
         {"The Historian", 0.7}},
        {{"The Futurist", 0.60}},
        {{"The Technologist",
          "Distinguish between what the system claims to do and what benchmarks show it actually does. Hype is "
          "endemic in this domain."},
         {"The Futurist", "Cite credible forecasts. Don't extrapolate from press releases."}});
}

DomainConfig political_config() {
    return make_config(
        "POLITICAL",
        {{"The Journalist", 1.5},
         {"The Historian", 1.3},
         {"The Economist", 1.2},
         {"The Sociologist", 1.1},
         {"The Technologist", 0.6}},
        {{"The Contrarian", 0.80}},
        {{"all",
          "Note political framing of sources. A claim from an advocacy organisation is not equivalent to a claim "
          "from an independent researcher."}});
}

DomainConfig economic_config() {
    return make_config(
        "ECONOMIC_FINANCIAL",
        {{"The Economist", 1.6},
         {"The Historian", 1.2},
         {"The Journalist", 1.1},
         {"The Psychologist", 1.1},
         {"The Philosopher", 0.6}},
        {},
        {{"The Economist",
          "Distinguish between mainstream economic consensus and heterodox positions. Both exist. Neither "
          "should be silenced or assumed correct."}});
}

DomainConfig social_config() {
    return make_config("SOCIAL_CULTURAL",
                       {{"The Sociologist", 1.5},
                        {"The Psychologist", 1.3},
                        {"The Historian", 1.1},
                        {"The Philosopher", 1.1},
                        {"The Technologist", 0.5}},
                       {},
                       {{"all",
                         "Distinguish between evidence of difference and claims about cause. Note cultural context "
                         "\xE2\x80\x94 what's true in one population may not transfer."}});
}

// ---- keyword matching -----------------------------------------------------------------------------------------

/** True when the byte at pos does not continue a word. pos may equal text.size(), where the terminator sits. */
bool boundary_at(const std::string& text, std::size_t pos) {
    return !is_word_byte(text[pos]);
}

/** A short keyword is the whole word, with an optional "s" or "es" on the end. */
bool whole_word_at(const std::string& text, std::size_t end) {
    if (boundary_at(text, end)) {
        return true;
    }
    if (text[end] == 's' && boundary_at(text, end + 1)) {
        return true;
    }
    return text.compare(end, 2, "es") == 0 && boundary_at(text, end + 2);
}

bool keyword_found(const std::string& text, const std::string& keyword) {
    const bool short_keyword = keyword.size() <= 3;
    std::size_t pos = text.find(keyword);
    while (pos != std::string::npos) {
        const bool starts_word = pos == 0 || !is_word_byte(text[pos - 1]);
        if (starts_word && (!short_keyword || whole_word_at(text, pos + keyword.size()))) {
            return true;
        }
        pos = text.find(keyword, pos + 1);
    }
    return false;
}

bool keyword_hit(const std::string& original, const std::string& lowered, const char* keyword) {
    const std::string word = keyword;
    if (word == "WHO") {
        return keyword_found(original, word);
    }
    return keyword_found(lowered, lower_ascii(word));
}

/** Scores in the table's own order, which breaks ties exactly as the Python's dict order does. */
std::vector<std::pair<std::string, int>> ordered_scores(const std::string& raw) {
    const std::string text = fold_separators(raw);
    const std::string lowered = lower_ascii(text);
    std::vector<std::pair<std::string, int>> out;
    for (const auto& entry : domain_keywords()) {
        int score = 0;
        for (const char* keyword : entry.keywords) {
            if (keyword_hit(text, lowered, keyword)) {
                ++score;
            }
        }
        if (score > 0) {
            out.emplace_back(entry.domain, score);
        }
    }
    return out;
}

// ---- contradictions -------------------------------------------------------------------------------------------

template <typename E, std::size_t N>
std::optional<E> parse_enum(const std::string& text, const std::array<const char*, N>& names) {
    const std::string wanted = lower_ascii(trim(text));
    for (std::size_t i = 0; i < N; ++i) {
        if (wanted == lower_ascii(names[i])) {
            return static_cast<E>(i);
        }
    }
    return std::nullopt;
}

const std::array<const char*, 5> kTypeNames = {"TYPE_A", "TYPE_B", "TYPE_C", "TYPE_D", "TYPE_E"};
const std::array<const char*, 4> kPriorityNames = {"LOW", "MEDIUM", "HIGH", "CRITICAL"};
const std::array<const char*, 5> kStatusNames = {"CONFIRMED", "DISPUTED", "DEFINITIONAL", "NEEDS_MORE", "TYPE_E_STALE"};
const std::array<const char*, 3> kOutcomeNames = {"SURVIVES", "WEAKENED", "DEFEATED"};

// ---- flags ----------------------------------------------------------------------------------------------------

Flag make_flag(const char* type, std::string detail, const char* action) {
    return Flag{type, std::move(detail), action};
}

void add_citation_flags(const SessionMetrics& metrics, std::vector<Flag>& flags) {
    for (const auto& stat : metrics.researchers) {
        if (stat.ok && stat.citations < 3) {
            flags.push_back(make_flag("LOW_CITATION_RESEARCHER",
                                      stat.name + " returned " + std::to_string(stat.citations) + " citations",
                                      "Tighten that agent's system prompt"));
        }
    }
}

bool tier4_too_high(const SessionMetrics& metrics) {
    const long total = static_cast<long>(metrics.tier1) + metrics.tier2 + metrics.tier3 + metrics.tier4;
    return static_cast<long>(metrics.tier4) * 100 > 15 * total;
}

// ---- text helpers for the graph ------------------------------------------------------------------------------

std::string heading_block(const std::string& slug_text) {
    return "\n## " + slug_text + "\n\n";
}

bool is_tag_digits(const std::string& text, std::size_t from, std::size_t to) {
    if (from >= to) {
        return false;
    }
    for (std::size_t i = from; i < to; ++i) {
        if (!is_digit(text[i])) {
            return false;
        }
    }
    return true;
}

/** Removes one trailing "[SOURCE_n]" from the claim and returns it, or returns empty and leaves the claim alone. */
std::string pop_source_tag(std::string& claim) {
    claim = trim(claim);
    const std::size_t open = claim.rfind("[SOURCE_");
    if (open == std::string::npos || claim.back() != ']' || !is_tag_digits(claim, open + 8, claim.size() - 1)) {
        return "";
    }
    const std::string tag = claim.substr(open);
    claim = trim(claim.substr(0, open));
    return tag;
}

std::string fact_field(const std::string& line, const char* label) {
    const std::string prefix = std::string("- ") + label + ":";
    const std::string body = trim(line);
    if (!starts_with(body, prefix)) {
        return "";
    }
    return trim(body.substr(prefix.size()));
}

const std::string kFactOpen = "- **";
const std::string kBold = "**";

bool is_fact_line(const std::string& line) {
    return starts_with(line, kFactOpen + "FACT-") && line.find(kBold, kFactOpen.size()) != std::string::npos;
}

ConfirmedFact start_fact(const std::string& line, const std::string& topic) {
    ConfirmedFact fact;
    fact.topic_slug = topic;
    const std::size_t id_end = line.find(kBold, kFactOpen.size());
    fact.id = line.substr(kFactOpen.size(), id_end - kFactOpen.size());
    fact.claim = trim(line.substr(id_end + kBold.size()));
    std::vector<std::string> tags;
    for (std::string tag = pop_source_tag(fact.claim); !tag.empty(); tag = pop_source_tag(fact.claim)) {
        tags.push_back(tag);
    }
    fact.citations.assign(tags.rbegin(), tags.rend());
    return fact;
}

void apply_fact_field(ConfirmedFact& fact, const std::string& line) {
    if (const std::string v = fact_field(line, "Confirmed by"); !v.empty()) {
        fact.confirmed_by = split_commas(v);
    } else if (const std::string s = fact_field(line, "Session"); !s.empty()) {
        fact.sessions = split_commas(s);
    } else if (const std::string c = fact_field(line, "Confidence"); !c.empty()) {
        fact.confidence = c;
    } else if (const std::string t = fact_field(line, "Temporal scope"); !t.empty()) {
        fact.temporal_scope = t;
    } else if (const std::string n = fact_field(line, "Notes"); !n.empty()) {
        fact.notes = n;
    }
}

}  // namespace

// ---- registry, depth, cost ------------------------------------------------------------------------------------

const std::vector<Researcher>& researchers() {
    return kResearchers;
}

bool is_contrarian(const std::string& name) {
    return name == "The Contrarian";
}

const char* depth_name(Depth depth) {
    switch (depth) {
        case Depth::Quick:
            return "quick";
        case Depth::Standard:
            return "standard";
        case Depth::Deep:
            return "deep";
        case Depth::Academic:
            return "academic";
    }
    return "standard";
}

std::optional<Depth> parse_depth(const std::string& text) {
    const std::string wanted = lower_ascii(trim(text));
    for (const Depth depth : {Depth::Quick, Depth::Standard, Depth::Deep, Depth::Academic}) {
        if (wanted == depth_name(depth)) {
            return depth;
        }
    }
    return std::nullopt;
}

Models models_for(Depth depth) {
    switch (depth) {
        case Depth::Quick:
            return {"sonar", "sonar-pro"};
        case Depth::Standard:
        case Depth::Deep:
            return {"sonar-pro", "sonar-deep-research"};
        case Depth::Academic:
            return {"sonar-deep-research", "sonar-deep-research"};
    }
    return {"sonar-pro", "sonar-deep-research"};
}

CostEstimate estimate_cost(Depth depth, int lens_count) {
    CostEstimate estimate;
    estimate.models = models_for(depth);
    estimate.researcher_usd = round_places(call_cost(estimate.models.researcher) * lens_count, 1e4);
    estimate.contrarian_usd = round_places(call_cost(estimate.models.contrarian), 1e4);
    estimate.total_usd = round_places(estimate.researcher_usd + estimate.contrarian_usd, 1e4);
    return estimate;
}

bool needs_cost_ack(Depth depth) {
    return depth == Depth::Academic;
}

// ---- sessions -------------------------------------------------------------------------------------------------

std::string next_session_id(const std::string& date, const std::string& session_log_text) {
    const std::string prefix = "session-" + date + "-";
    long highest = 0;
    std::size_t pos = session_log_text.find(prefix);
    while (pos != std::string::npos) {
        const std::size_t begin = pos + prefix.size();
        const std::size_t stop = session_log_text.find_first_not_of("0123456789", begin);
        const std::size_t digits = (stop == std::string::npos ? session_log_text.size() : stop) - begin;
        if (digits >= 1 && digits <= 9) {
            highest = std::max(highest, std::stol(session_log_text.substr(begin, digits)));
        }
        pos = session_log_text.find(prefix, begin);
    }
    std::ostringstream id;
    id << prefix << std::setw(3) << std::setfill('0') << highest + 1;
    return id.str();
}

std::optional<std::string> session_date(const std::string& session_id) {
    const std::string head = "session-";
    if (!starts_with(session_id, head) || session_id.size() < head.size() + 12) {
        return std::nullopt;
    }
    const std::string date = session_id.substr(head.size(), 10);
    if (!SourceLedger::parse_iso_date(date).has_value() || session_id[head.size() + 10] != '-') {
        return std::nullopt;
    }
    if (!is_tag_digits(session_id, head.size() + 11, session_id.size())) {
        return std::nullopt;
    }
    return date;
}

// ---- domain routing -------------------------------------------------------------------------------------------

DomainConfig domain_config(const std::string& domain_class) {
    if (domain_class == "SCIENTIFIC_MEDICAL") {
        return scientific_config();
    }
    if (domain_class == "LEGAL_REGULATORY") {
        return legal_config();
    }
    if (domain_class == "TECHNOLOGY_AI") {
        return technology_config();
    }
    if (domain_class == "POLITICAL") {
        return political_config();
    }
    if (domain_class == "ECONOMIC_FINANCIAL") {
        return economic_config();
    }
    if (domain_class == "SOCIAL_CULTURAL") {
        return social_config();
    }
    return DomainConfig{};
}

std::map<std::string, int> domain_scores(const std::string& text) {
    std::map<std::string, int> out;
    for (const auto& [domain, score] : ordered_scores(text)) {
        out[domain] = score;
    }
    return out;
}

DomainConfig classify_domain(const std::string& topic, const std::vector<std::string>& constraints) {
    std::string combined = topic;
    for (const auto& constraint : constraints) {
        combined += " " + constraint;
    }
    auto ranked = ordered_scores(combined);
    if (ranked.empty()) {
        return DomainConfig{};
    }
    // Stable, so a tie keeps the table's order, as the Python's max() and sorted() do.
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    // The Python also filters on "at least 0.7 of the runner-up", but the top two always pass that test.
    // Every ranked score is a hit count, so at least 1.
    if (ranked.size() == 1 || ranked[0].second >= 2 * ranked[1].second) {
        return domain_config(ranked[0].first);
    }
    return merge_domain_configs(ranked[0].first, ranked[1].first);
}

DomainConfig merge_domain_configs(const std::string& a, const std::string& b) {
    const DomainConfig first = domain_config(a);
    const DomainConfig second = domain_config(b);
    DomainConfig merged;
    merged.domain_class = a + "+" + b;
    std::vector<std::string> names;
    for (const auto& entry : first.researcher_weights) {
        names.push_back(entry.first);
    }
    for (const auto& entry : second.researcher_weights) {
        names.push_back(entry.first);
    }
    for (const auto& name : names) {
        const double sum = weight_for(first, name) + weight_for(second, name);
        merged.researcher_weights[name] = round_places(sum / 2.0, 100.0);
    }
    merged.temperature_overrides = second.temperature_overrides;
    for (const auto& entry : first.temperature_overrides) {
        merged.temperature_overrides[entry.first] = entry.second;
    }
    merged.prompt_addenda = second.prompt_addenda;
    for (const auto& entry : first.prompt_addenda) {
        merged.prompt_addenda[entry.first] = entry.second;
    }
    return merged;
}

double weight_for(const DomainConfig& config, const std::string& researcher) {
    const auto found = config.researcher_weights.find(researcher);
    return found == config.researcher_weights.end() ? 1.0 : found->second;
}

double temperature_for(const DomainConfig& config, const Researcher& researcher) {
    const auto found = config.temperature_overrides.find(researcher.name);
    return found == config.temperature_overrides.end() ? researcher.temperature : found->second;
}

// ---- audience -------------------------------------------------------------------------------------------------

const char* persona_name(Persona persona) {
    switch (persona) {
        case Persona::Analyst:
            return "ANALYST";
        case Persona::Journalist:
            return "JOURNALIST";
        case Persona::PolicyMaker:
            return "POLICY_MAKER";
        case Persona::TechnicalSpecialist:
            return "TECHNICAL_SPECIALIST";
        case Persona::GeneralPublic:
            return "GENERAL_PUBLIC";
    }
    return "ANALYST";
}

std::optional<Persona> parse_persona(const std::string& text) {
    static const std::map<std::string, Persona> keys = {
        {"analyst", Persona::Analyst},
        {"journalist", Persona::Journalist},
        {"writer", Persona::Journalist},
        {"journalist_writer", Persona::Journalist},
        {"policy_maker", Persona::PolicyMaker},
        {"policy", Persona::PolicyMaker},
        {"decision_maker", Persona::PolicyMaker},
        {"policy_maker_decision_maker", Persona::PolicyMaker},
        {"technical_specialist", Persona::TechnicalSpecialist},
        {"technical", Persona::TechnicalSpecialist},
        {"specialist", Persona::TechnicalSpecialist},
        {"general_public", Persona::GeneralPublic},
        {"general", Persona::GeneralPublic},
        {"public", Persona::GeneralPublic},
    };
    const auto found = keys.find(normalise_key(text));
    if (found == keys.end()) {
        return std::nullopt;
    }
    return found->second;
}

Persona detect_audience(const std::string& explicit_audience, const std::string& output_format) {
    if (const auto persona = parse_persona(explicit_audience)) {
        return *persona;
    }
    static const std::map<std::string, Persona> formats = {
        {"brief", Persona::PolicyMaker},
        {"article", Persona::Journalist},
        {"wiki", Persona::Analyst},
        {"summary", Persona::GeneralPublic},
        {"technical", Persona::TechnicalSpecialist},
        {"report", Persona::Analyst},
    };
    const auto found = formats.find(lower_ascii(trim(output_format)));
    return found == formats.end() ? Persona::Analyst : found->second;
}

Calibration calibration_for(Persona persona) {
    Calibration c;
    c.persona = persona;
    switch (persona) {
        case Persona::Analyst:
            c = {persona,
                 "formal_analytical",
                 "full",
                 "concise_answer_with_confidence",
                 "prominent",
                 "attached",
                 "visible_everywhere",
                 "retained",
                 "varied, medium-long",
                 "all equal",
                 {},
                 ""};
            break;
        case Persona::Journalist:
            c = {persona,
                 "narrative_journalistic",
                 "curated",
                 "lede_style_hook_then_nut_graf",
                 "narrative_reframe",
                 "footnote_style",
                 "translated",
                 "defined_on_first_use",
                 "short_to_medium",
                 "tension_and_human_angle_foregrounded",
                 {},
                 "magazine-writer"};
            break;
        case Persona::PolicyMaker:
            c = {persona,
                 "executive_brief",
                 "decision_focused",
                 "extended_recommendation_format",
                 "reframed_as_risk_table",
                 "appendix_only",
                 "translated_to_risk_language",
                 "avoided_or_glossarised",
                 "short",
                 "recommendation_and_risk_foregrounded",
                 {"DECISION_BRIEF"},
                 ""};
            break;
        case Persona::TechnicalSpecialist:
            c = {persona,
                 "technical_specialist",
                 "full_plus_methodology",
                 "compressed",
                 "full_technical_detail",
                 "inline_and_full",
                 "numerical",
                 "retained_and_assumed_known",
                 "dense",
                 "evidence_and_methodology_foregrounded",
                 {},
                 ""};
            break;
        case Persona::GeneralPublic:
            c = {persona,
                 "accessible_plain_english",
                 "curated_essential",
                 "plain_language_headline",
                 "reframed",
                 "where_this_comes_from_section",
                 "translated",
                 "avoided_or_explained_by_analogy",
                 "short_and_direct",
                 "relevance_to_reader_foregrounded",
                 {},
                 ""};
            break;
    }
    return c;
}

// ---- contradiction vocabulary and rules -----------------------------------------------------------------------

const char* type_name(ContradictionType type) {
    return kTypeNames[static_cast<std::size_t>(type)];
}

std::optional<ContradictionType> parse_type(const std::string& text) {
    const std::string wanted = trim(text);
    return parse_enum<ContradictionType>(wanted.size() == 1 ? "type_" + wanted : wanted, kTypeNames);
}

const char* priority_name(Priority priority) {
    return kPriorityNames[static_cast<std::size_t>(priority)];
}

std::optional<Priority> parse_priority(const std::string& text) {
    return parse_enum<Priority>(text, kPriorityNames);
}

const char* status_name(Status status) {
    return kStatusNames[static_cast<std::size_t>(status)];
}

std::optional<Status> parse_status(const std::string& text) {
    return parse_enum<Status>(text, kStatusNames);
}

const char* outcome_name(SteelmanOutcome outcome) {
    return kOutcomeNames[static_cast<std::size_t>(outcome)];
}

std::optional<SteelmanOutcome> parse_outcome(const std::string& text) {
    return parse_enum<SteelmanOutcome>(text, kOutcomeNames);
}

const char* adjustment_name(Adjustment adjustment) {
    switch (adjustment) {
        case Adjustment::Raise:
            return "RAISE";
        case Adjustment::Maintain:
            return "MAINTAIN";
        case Adjustment::Lower:
            return "LOWER";
    }
    return "MAINTAIN";
}

SteelmanRule steelman_rule(ContradictionType type, Confidence a, Confidence b) {
    if (type == ContradictionType::D) {
        return SteelmanRule::Required;
    }
    if (type != ContradictionType::A) {
        return SteelmanRule::Skip;
    }
    if (a == Confidence::High && b == Confidence::High) {
        return SteelmanRule::Required;
    }
    if (a == Confidence::Low && b == Confidence::Low) {
        return SteelmanRule::Skip;
    }
    return SteelmanRule::Recommended;
}

bool needs_followup_query(ContradictionType type, Confidence a, Confidence b, Status status) {
    return type == ContradictionType::A && a == Confidence::High && b == Confidence::High &&
           status != Status::Confirmed;
}

Adjustment adjustment_for(SteelmanOutcome outcome) {
    switch (outcome) {
        case SteelmanOutcome::Survives:
            return Adjustment::Lower;
        case SteelmanOutcome::Weakened:
            return Adjustment::Maintain;
        case SteelmanOutcome::Defeated:
            return Adjustment::Raise;
    }
    return Adjustment::Maintain;
}

Priority apply_steelman(Priority priority, SteelmanOutcome outcome) {
    if (outcome == SteelmanOutcome::Defeated) {
        return Priority::Critical;
    }
    if (outcome == SteelmanOutcome::Survives && priority != Priority::Low) {
        return static_cast<Priority>(static_cast<int>(priority) - 1);
    }
    return priority;
}

std::vector<Destination> destinations_for(Status status) {
    switch (status) {
        case Status::Confirmed:
            return {Destination::ConfirmedFacts};
        case Status::Disputed:
            return {Destination::DisputedClaims};
        case Status::NeedsMore:
            return {Destination::RecommendedQueries, Destination::DisputedClaims};
        case Status::TypeEStale:
            return {Destination::SessionLog, Destination::ReconfirmationQueue};
        case Status::Definitional:
            return {};
    }
    return {};
}

bool removes_from_disputed(Status status) {
    return status == Status::Confirmed;
}

std::vector<std::string> gap_clusters(const std::vector<DisputeRef>& disputes) {
    std::vector<std::string> order;
    std::map<std::string, int> counts;
    for (const auto& dispute : disputes) {
        if (dispute.type != ContradictionType::A || dispute.status != Status::Disputed) {
            continue;
        }
        const std::string key = lower_ascii(trim(dispute.subtopic));
        if (counts[key]++ == 0) {
            order.push_back(trim(dispute.subtopic));
        }
    }
    std::vector<std::string> out;
    for (const auto& name : order) {
        if (counts[lower_ascii(name)] >= 3) {
            out.push_back(name);
        }
    }
    return out;
}

// ---- Kaizen ---------------------------------------------------------------------------------------------------

const std::vector<std::string>& kaizen_flag_types() {
    static const std::vector<std::string> types = {
        "LOW_CITATION_RESEARCHER",   "CONTRARIAN_FAILED_BRIEF", "ZERO_CONTRADICTIONS", "HIGH_TIER4_RATIO",
        "UNSOURCED_SYNTHESIS_CLAIM", "TYPE_E_NOT_INVESTIGATED", "KG_NOT_QUERIED",      "GAP_SECTION_TOO_SHALLOW"};
    return types;
}

std::vector<Flag> kaizen_flags(const SessionMetrics& metrics) {
    std::vector<Flag> flags;
    add_citation_flags(metrics, flags);
    if (metrics.contrarian_agreed_on_everything) {
        flags.push_back(make_flag("CONTRARIAN_FAILED_BRIEF", "The Contrarian agreed with the majority on every point",
                                  "Raise temperature; sharpen adversarial brief"));
    }
    if (metrics.contradictions_found == 0) {
        flags.push_back(make_flag("ZERO_CONTRADICTIONS", "No contradictions found",
                                  "Either topic is settled (note it) or detection missed something"));
    }
    if (tier4_too_high(metrics)) {
        flags.push_back(make_flag("HIGH_TIER4_RATIO", "More than 15% of sources are Tier 4",
                                  "Domain may lack quality sources; note explicitly"));
    }
    if (metrics.unsourced_claims > 0) {
        flags.push_back(make_flag("UNSOURCED_SYNTHESIS_CLAIM",
                                  std::to_string(metrics.unsourced_claims) + " synthesis claims without a citation",
                                  "synthesis-engine.md defect; fix and re-run"));
    }
    if (metrics.type_e_uninvestigated > 0) {
        flags.push_back(make_flag(
            "TYPE_E_NOT_INVESTIGATED",
            std::to_string(metrics.type_e_uninvestigated) + " cross-session conflicts not resolved before synthesis",
            "Process defect; update ST-6 procedure"));
    }
    if (!metrics.kg_checked) {
        flags.push_back(
            make_flag("KG_NOT_QUERIED", "Knowledge graph check skipped", "ST-2 defect; check session-log for pattern"));
    }
    if (metrics.gaps < 3 || metrics.gaps_with_followup < metrics.gaps) {
        flags.push_back(make_flag("GAP_SECTION_TOO_SHALLOW",
                                  "Gaps section has fewer than 3 items or lacks follow-up queries",
                                  "Insufficient gap analysis"));
    }
    return flags;
}

std::vector<std::string> mandatory_improvements(const std::vector<std::vector<std::string>>& history,
                                                std::size_t streak) {
    std::vector<std::string> out;
    if (streak == 0 || history.size() < streak) {
        return out;
    }
    for (const auto& type : kaizen_flag_types()) {
        const bool in_every_session =
            std::all_of(history.end() - static_cast<std::ptrdiff_t>(streak), history.end(),
                        [&](const std::vector<std::string>& session) {
                            return std::find(session.begin(), session.end(), type) != session.end();
                        });
        if (in_every_session) {
            out.push_back(type);
        }
    }
    return out;
}

double tier_1_2_share(const SessionMetrics& metrics) {
    const int total = metrics.tier1 + metrics.tier2 + metrics.tier3 + metrics.tier4;
    if (total == 0) {
        return 0.0;
    }
    return static_cast<double>(metrics.tier1 + metrics.tier2) / total;
}

bool executive_summary_ok(const std::string& text) {
    std::istringstream words(text);
    std::string word;
    int count = 0;
    while (words >> word) {
        ++count;
    }
    return count < 300;
}

// ---- entities and slugs ---------------------------------------------------------------------------------------

namespace {

struct Word {
    std::size_t begin;
    std::size_t end;
};

bool is_title_word(const std::string& text, const Word& word) {
    if (word.end - word.begin < 2 || text[word.begin] < 'A' || text[word.begin] > 'Z') {
        return false;
    }
    for (std::size_t i = word.begin + 1; i < word.end; ++i) {
        if (text[i] < 'a' || text[i] > 'z') {
            return false;
        }
    }
    return true;
}

std::vector<Word> words_of(const std::string& text) {
    std::vector<Word> words;
    auto it = text.begin();
    while ((it = std::find_if(it, text.end(), is_word_byte)) != text.end()) {
        const auto stop = std::find_if_not(it, text.end(), is_word_byte);
        words.push_back({static_cast<std::size_t>(it - text.begin()), static_cast<std::size_t>(stop - text.begin())});
        it = stop;
    }
    return words;
}

bool only_space_between(const std::string& text, std::size_t from, std::size_t to) {
    for (std::size_t i = from; i < to; ++i) {
        if (!is_space(text[i])) {
            return false;
        }
    }
    return true;
}

/** Title-case chains of two or more words, in order of appearance. */
std::vector<std::string> title_chains(const std::string& raw) {
    std::vector<std::string> chains;
    const std::string text = fold_separators(raw);
    const auto words = words_of(text);
    std::size_t i = 0;
    while (i < words.size()) {
        if (!is_title_word(text, words[i])) {
            ++i;
            continue;
        }
        std::size_t last = i;
        while (last + 1 < words.size() && is_title_word(text, words[last + 1]) &&
               only_space_between(text, words[last].end, words[last + 1].begin)) {
            ++last;
        }
        if (last > i) {
            chains.push_back(text.substr(words[i].begin, words[last].end - words[i].begin));
        }
        i = last + 1;
    }
    return chains;
}

}  // namespace

std::vector<Entity> extract_entities(const std::vector<std::string>& texts, std::size_t limit) {
    std::vector<Entity> entities;
    for (const auto& text : texts) {
        for (const auto& chain : title_chains(text)) {
            auto found = std::find_if(entities.begin(), entities.end(),
                                      [&](const Entity& e) { return e.concept_name == chain; });
            if (found == entities.end()) {
                entities.push_back({chain, 1});
            } else {
                ++found->count;
            }
        }
    }
    std::stable_sort(entities.begin(), entities.end(),
                     [](const Entity& a, const Entity& b) { return a.count > b.count; });
    entities.resize(std::min(entities.size(), limit));
    return entities;
}

std::string slug(const std::string& topic) {
    std::vector<char32_t> kept;
    bool pending_dash = false;
    for (const char32_t code : decode_utf8(topic)) {
        const SlugClass kind = slug_class(code);
        if (kind == SlugClass::Space) {
            pending_dash = !kept.empty();
        } else if (kind == SlugClass::Keep) {
            if (pending_dash) {
                kept.push_back('-');
                pending_dash = false;
            }
            kept.push_back(lower_code(code));
        }
    }
    kept.resize(std::min<std::size_t>(kept.size(), 80));
    std::string out;
    for (const char32_t code : kept) {
        encode_utf8(code, out);
    }
    return out;
}

bool has_section(const std::string& text, const std::string& heading) {
    const std::string wanted = "## " + heading;
    for (const auto& line : split_lines(text)) {
        const std::size_t last = line.find_last_not_of(" \t\r");
        if (last != std::string::npos && line.compare(0, last + 1, wanted) == 0 && last + 1 == wanted.size()) {
            return true;
        }
    }
    return false;
}

// ---- writers --------------------------------------------------------------------------------------------------

std::string render_confirmed_fact(const ConfirmedFact& fact, bool section_exists) {
    std::string out = section_exists ? "" : heading_block(fact.topic_slug);
    out += "- **" + fact.id + "** " + fact.claim;
    for (const auto& citation : fact.citations) {
        out += " " + citation;
    }
    out += "\n  - Confirmed by: " + join(fact.confirmed_by, ", ");
    out += "\n  - Session: " + join(fact.sessions, ", ");
    out += "\n  - Confidence: " + fact.confidence + "\n";
    if (!fact.temporal_scope.empty()) {
        out += "  - Temporal scope: " + fact.temporal_scope + "\n";
    }
    if (!fact.notes.empty()) {
        out += "  - Notes: " + fact.notes + "\n";
    }
    return out + "\n";
}

namespace {

std::string claim_line(const char* label, const DisputeClaim& claim) {
    std::string line = std::string("- **") + label + ":** " + claim.text;
    if (!claim.citation.empty()) {
        line += " " + claim.citation;
    }
    return line + " " + kEmDash + " " + claim.researcher + ", session " + claim.session + "\n";
}

}  // namespace

std::string render_disputed_claim(const Dispute& dispute, bool section_exists, const std::string& date) {
    std::string out = section_exists ? "" : heading_block(dispute.topic_slug);
    out += "### " + dispute.id + " [" + type_name(dispute.type) + "] " + kEmDash + " " +
           priority_name(dispute.priority) + "\n";
    out += claim_line("Claim A", dispute.claim_a);
    out += claim_line("Claim B", dispute.claim_b);
    out += std::string("- **Status:** ") + status_name(dispute.status) + "\n";
    if (!dispute.resolution_note.empty()) {
        out += "- **Resolution note:** " + dispute.resolution_note + "\n";
    }
    if (!dispute.recommended_query.empty()) {
        out += "- **Recommended query:** \"" + dispute.recommended_query + "\"\n";
    }
    return out + "- **Last reviewed:** " + date + "\n\n";
}

std::string render_topic_index_entry(const TopicEntry& entry, const std::string& date) {
    const std::string name = slug(entry.topic);
    std::string entities = "none";
    if (!entry.key_entities.empty()) {
        entities.clear();
        for (std::size_t i = 0; i < entry.key_entities.size(); ++i) {
            entities += (i > 0 ? ", [[" : "[[") + entry.key_entities[i] + "]]";
        }
    }
    std::string out = "\n## " + name + "\n";
    out += "- Display: " + entry.topic + "\n";
    out += "- Sessions: [" + entry.session_id + "]\n";
    out += "- Last updated: " + date + "\n";
    out += "- Overall confidence: " + entry.confidence + "\n";
    out += "- Confirmed facts: " + std::to_string(entry.confirmed_count) + "\n";
    out += "- Active disputes: " + std::to_string(entry.dispute_count) + "\n";
    out += "- Key entities: " + entities + "\n";
    return out + "- See: wiki/" + name + ".md\n\n";
}

std::string render_session_log_entry(const SessionLogEntry& entry, const std::string& date) {
    std::vector<std::string> flag_names;
    for (const auto& flag : entry.flags) {
        flag_names.push_back(flag.type);
    }
    std::string out = "\n## " + entry.session_id + "\n";
    out += "- Date: " + date + "\n";
    out += "- Topic: " + entry.topic + "\n";
    out += std::string("- Depth: ") + depth_name(entry.depth) + "\n";
    out += "- Researchers completed: " + std::to_string(entry.researchers_completed) + "/13\n";
    out += "- Total sources: " + std::to_string(entry.total_sources) + "\n";
    out += "- TIER 1/2 pct: " + percent_text(entry.tier_1_2_share) + "\n";
    out += "- Contradictions found: " + std::to_string(entry.contradictions_found) + "\n";
    out += "- KG confirmed facts added: " + std::to_string(entry.confirmed_facts_added) + "\n";
    out += "- KG disputed claims added: " + std::to_string(entry.disputed_claims_added) + "\n";
    out += "- Kaizen flags: " + (flag_names.empty() ? std::string("none") : join(flag_names, ", ")) + "\n";
    out += "- Notes: " + (entry.notes.empty() ? std::string("none") : entry.notes) + "\n";
    return out + "\n";
}

// ---- wiki page ------------------------------------------------------------------------------------------------

namespace {

constexpr std::size_t kWikiEntityLimit = 10;
constexpr std::size_t kWikiLinkLimit = 5;

using Lines = std::vector<std::string>;

Lines bullet_lines(const Lines& items, const char* empty_text) {
    Lines out;
    for (const auto& item : items) {
        out.push_back("- " + item);
    }
    if (out.empty()) {
        out.push_back(empty_text);
    }
    return out;
}

Lines entity_lines(const WikiInput& input) {
    Lines out;
    for (std::size_t i = 0; i < input.entities.size() && i < kWikiEntityLimit; ++i) {
        const auto& entity = input.entities[i];
        std::string line = "- [[" + entity.concept_name + "]]";
        if (!entity.context.empty()) {
            line += std::string(" ") + kEmDash + " " + entity.context;
        }
        out.push_back(line);
    }
    if (out.empty()) {
        out.push_back("*(none yet)*");
    }
    return out;
}

Lines spider_lines(const WikiInput& input) {
    Lines out;
    for (std::size_t i = 0; i < input.entities.size() && i < kWikiEntityLimit; ++i) {
        const auto& entity = input.entities[i];
        std::string line = "- [[" + entity.concept_name + "]]";
        for (std::size_t k = 0; k < entity.links.size() && k < kWikiLinkLimit; ++k) {
            line += (k == 0 ? std::string(" ") + kArrow + " related: [[" : ", [[") + entity.links[k] + "]]";
        }
        out.push_back(line);
    }
    if (out.empty()) {
        out.push_back("*(none yet)*");
    }
    return out;
}

Lines history_lines(const WikiInput& input) {
    return {"| Session ID | Date | Depth | Researchers completed | Contradictions found | Overall confidence |",
            "|---|---|---|---|---|---|",
            "| " + input.session_id + " | " + input.date + " | " + input.depth + " | " +
                std::to_string(input.researchers_completed) + "/13 | " + std::to_string(input.contradictions_found) +
                " | " + input.confidence + " |"};
}

Lines query_lines(const WikiInput& input) {
    Lines out;
    for (std::size_t i = 0; i < input.recommended_queries.size(); ++i) {
        out.push_back(std::to_string(i + 1) + ". \"" + input.recommended_queries[i] + "\"");
    }
    if (out.empty()) {
        out.push_back("*(none)*");
    }
    return out;
}

/** The body for a known template heading, or nullopt for a heading that is left alone. */
std::optional<Lines> section_lines(const std::string& heading, const WikiInput& input) {
    if (heading == "What this topic is") {
        return Lines{input.executive_summary.empty() ? "*(summary pending)*" : input.executive_summary};
    }
    if (heading == "Key entities") {
        return entity_lines(input);
    }
    if (heading == "What the Council has confirmed") {
        return bullet_lines(input.confirmed_facts, "*(none confirmed yet)*");
    }
    if (heading == "Active disputes") {
        return bullet_lines(input.active_disputes, "*(no active disputes)*");
    }
    if (heading == "Session history") {
        return history_lines(input);
    }
    if (heading == "Recommended follow-up queries") {
        return query_lines(input);
    }
    if (heading == "Spider trail") {
        return spider_lines(input);
    }
    return std::nullopt;
}

std::string without_carriage_return(std::string line) {
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    return line;
}

}  // namespace

std::string render_wiki_page(const std::string& template_text, const WikiInput& input) {
    const Lines lines = split_lines(template_text);
    Lines out;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string line = without_carriage_return(lines[i]);
        if (starts_with(line, "# [[{")) {
            out.push_back("# [[" + input.topic + "]]");
            continue;
        }
        const auto body = starts_with(line, "## ") ? section_lines(line.substr(3), input) : std::nullopt;
        if (!body) {
            out.push_back(lines[i]);
            continue;
        }
        out.push_back(line);
        out.push_back("");
        out.insert(out.end(), body->begin(), body->end());
        out.push_back("");
        while (i + 1 < lines.size() && without_carriage_return(lines[i + 1]) != "---") {
            ++i;
        }
    }
    return join(out, "\n");
}

// ---- files ----------------------------------------------------------------------------------------------------

namespace {

bool prepare_parent(const std::filesystem::path& path, std::vector<std::string>& failures) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        failures.push_back(path.string() + ": " + error.message());
        return false;
    }
    return true;
}

bool write_stream(const std::filesystem::path& path, const std::string& content, std::ios::openmode mode,
                  std::vector<std::string>& failures) {
    std::ofstream out(path, mode);
    out << content;
    out.flush();
    if (!out) {
        failures.push_back(path.string() + ": write failed");
        return false;
    }
    return true;
}

}  // namespace

bool append_file(const std::filesystem::path& path, const std::string& content, std::vector<std::string>& failures) {
    return prepare_parent(path, failures) && write_stream(path, content, std::ios::app | std::ios::binary, failures);
}

bool create_file_once(const std::filesystem::path& path, const std::string& content,
                      std::vector<std::string>& failures) {
    std::error_code error;
    if (std::filesystem::exists(path, error)) {
        failures.push_back(path.string() + ": exists, left alone");
        return false;
    }
    return prepare_parent(path, failures) && write_stream(path, content, std::ios::binary, failures);
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return "";
    }
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// ---- reading the graph back -----------------------------------------------------------------------------------

std::vector<ConfirmedFact> parse_confirmed_facts(const std::string& text) {
    std::vector<ConfirmedFact> facts;
    std::string topic;
    bool in_fence = false;
    for (const auto& raw : split_lines(text)) {
        const std::string line = without_carriage_return(raw);
        if (starts_with(trim(line), "```")) {
            in_fence = !in_fence;
        } else if (in_fence) {
            continue;
        } else if (starts_with(line, "## ")) {
            topic = trim(line.substr(3));
        } else if (is_fact_line(line)) {
            facts.push_back(start_fact(line, topic));
        } else if (!facts.empty()) {
            apply_fact_field(facts.back(), line);
        }
    }
    return facts;
}

std::vector<ConfirmedFact> facts_for_topic(const std::vector<ConfirmedFact>& facts, const std::string& topic_slug) {
    std::vector<ConfirmedFact> out;
    std::copy_if(facts.begin(), facts.end(), std::back_inserter(out),
                 [&](const ConfirmedFact& f) { return f.topic_slug == topic_slug; });
    return out;
}

std::string prior_knowledge_block(const std::vector<ConfirmedFact>& facts, std::size_t limit) {
    if (facts.empty() || limit == 0) {
        return "";
    }
    std::string out = "PRIOR CONFIRMED KNOWLEDGE (do not re-research; extend beyond this):";
    for (std::size_t i = 0; i < facts.size() && i < limit; ++i) {
        out += "\n- " + facts[i].claim;
    }
    return out;
}

std::optional<std::string> fact_date(const ConfirmedFact& fact) {
    std::vector<std::string> dates;
    for (const auto& session : fact.sessions) {
        if (const auto date = session_date(session)) {
            dates.push_back(*date);
        }
    }
    if (dates.empty()) {
        return std::nullopt;
    }
    return *std::min_element(dates.begin(), dates.end());
}

std::vector<std::size_t> stale_fact_indexes(const std::vector<ConfirmedFact>& facts, const std::string& domain_class,
                                            const std::string& today, double threshold_percent) {
    const SourceLedger::Decay decay = SourceLedger::decay_for_domain(domain_class);
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < facts.size(); ++i) {
        const auto result = SourceLedger::apply_temporal_penalty(1.0, fact_date(facts[i]), decay, today);
        if (result.penalty_percent > threshold_percent) {
            out.push_back(i);
        }
    }
    return out;
}

}  // namespace QuietCouncil
