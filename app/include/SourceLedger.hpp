#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/**
 * @brief The arithmetic behind the Quiet Council's source handling: which tier a URL sits in, how citations from
 *        many researchers collapse into one numbered registry, how a source's age and its distance from the
 *        original evidence lower its weight. Pure functions over strings and numbers. No network, no clock: the
 *        caller passes "today".
 */
namespace SourceLedger {

enum class Tier { Tier1, Tier2, Tier3, Tier4 };

/** @brief "TIER_1" to "TIER_4". */
const char* tier_name(Tier tier);

/** @brief Lower-cased host of a URL, without scheme, credentials, port, path, query or fragment. */
std::string host_of(const std::string& url);

/**
 * @brief Source authority, judged on the host only. A host is in a list when it equals an entry or is a
 *        subdomain of it; entries that begin with a dot (".edu", ".ac.uk") match as a suffix. Matching the host
 *        rather than the whole URL means "https://example.com/?r=nature.com" is not Tier 1. Default is Tier 3.
 */
Tier classify_tier(const std::string& url);

/**
 * @brief Dedupe key for a URL: trimmed, trailing slashes dropped, utm_* / fbclid / gclid / ref / source query
 *        parameters and blank-valued parameters removed, lower-cased. Empty input gives an empty key.
 */
std::string clean_url(const std::string& url);

struct ResearcherCitations {
    std::string researcher;
    /** @brief Local tag such as "[SOURCE_3]" and its URL, in the order the researcher gave them. */
    std::vector<std::pair<std::string, std::string>> citation_map;
};

struct Source {
    int id{0};
    /** @brief The URL as first seen. */
    std::string url;
    Tier tier{Tier::Tier3};
    std::vector<std::string> found_by;
    /** @brief Found independently by three or more researchers. */
    bool convergent{false};
};

struct Summary {
    std::size_t total{0};
    std::size_t tier1{0};
    std::size_t tier2{0};
    std::size_t tier3{0};
    std::size_t tier4{0};
    std::size_t convergent{0};
};

struct Normalised {
    /** @brief Ordered by id, which counts from 1. */
    std::vector<Source> sources;
    /** @brief clean_url to id. */
    std::map<std::string, int> lookup;
    /** @brief researcher to (local tag to "[SOURCE_n]"). */
    std::map<std::string, std::map<std::string, std::string>> researcher_maps;
    std::vector<int> convergent_ids;
    Summary summary;
};

/** @brief Merge every researcher's citations into one registry with global ids. */
Normalised normalise(const std::vector<ResearcherCitations>& outputs);

/** @brief Replace local tags with global ones in a single pass, so a replacement is never replaced again. */
std::string remap_content(const std::string& content, const std::map<std::string, std::string>& remap);

/** @brief Every n in a "[SOURCE_n]" tag, in order of appearance, duplicates kept. */
std::vector<int> citation_ids_in(const std::string& text);

/** @brief Ids cited in the text that the registry does not hold, each once, in order of appearance. */
std::vector<int> unregistered_citation_ids(const std::string& text, const std::vector<int>& registry_ids);

struct Date {
    int year{0};
    int month{0};
    int day{0};
};

/** @brief Strict YYYY-MM-DD that exists on the calendar. */
std::optional<Date> parse_iso_date(const std::string& text);

/** @brief Whole days from a to b; negative when b is earlier. */
long days_between(const Date& a, const Date& b);

/** @brief A date hinted at by the URL: "/2024/03/15/", "/2024/03/" or an arXiv id. No hint gives nullopt. */
std::optional<std::string> extract_source_date(const std::string& url);

enum class Decay { Stable, Slow, Medium, Fast, Breaking };

/** @brief "STABLE", "SLOW", "MEDIUM", "FAST" or "BREAKING". */
const char* decay_name(Decay decay);

std::optional<Decay> parse_decay(const std::string& name);

/**
 * @brief Default decay class for a domain class from the domain router. Anything unknown, a blended class such as
 *        "LEGAL_REGULATORY+POLITICAL" included, falls back to Medium.
 */
Decay decay_for_domain(const std::string& domain_class);

struct Temporal {
    double adjusted{0.0};
    /** @brief Fraction removed, 0 to 0.6, at the one-decimal-percent precision the note shows. */
    double penalty{0.0};
    /** @brief The same figure as a percentage, rounded to one decimal place. */
    double penalty_percent{0.0};
    std::string note;
};

/**
 * @brief Age-based confidence penalty. No date, or a date or "today" that does not parse, costs 15%. Otherwise
 *        nothing is lost inside the class's freshness threshold, then the loss grows linearly per year and stops at
 *        the class cap: 30% SLOW, 40% MEDIUM, 50% FAST, 60% BREAKING.
 */
Temporal apply_temporal_penalty(double base_confidence, const std::optional<std::string>& source_date, Decay decay,
                                const std::string& today);

struct StaleItem {
    std::size_t index{0};
    double penalty_percent{0.0};
    std::string note;
};

/** @brief Indexes (into dates) whose temporal penalty is more than threshold_percent. */
std::vector<StaleItem> stale_items(const std::vector<std::optional<std::string>>& dates,
                                   const std::string& domain_class, const std::string& today,
                                   double threshold_percent = 20.0);

struct RegistryTemporal {
    std::optional<std::string> source_date;
    Decay decay{Decay::Medium};
    double penalty{0.0};
    std::string note;
};

/** @brief Temporal tags for one registry entry. A known_date wins over one found in the URL. */
RegistryTemporal registry_temporal(const std::string& url, const std::optional<std::string>& known_date,
                                   const std::string& domain_class, const std::string& today);

enum class Depth { Primary, Secondary, Tertiary, QuaternaryPlus };

/** @brief "PRIMARY", "SECONDARY", "TERTIARY" or "QUATERNARY+". */
const char* depth_name(Depth depth);

/** @brief 0, 0.05, 0.12 or 0.22. */
double depth_penalty(Depth depth);

/**
 * @brief How far a source sits from the original evidence. Tier 1 is primary unless the URL says review, meta,
 *        systematic or cochrane; every other tier is tertiary.
 */
Depth classify_citation_depth(const std::string& url, Tier tier);

struct Propagated {
    double confidence{0.0};
    std::string note;
    bool chain_deep{false};
};

/**
 * @brief Average the depth penalties of a finding's citations. No citations costs 20%. The result is floored at
 *        0.20, or at the base itself when that is already lower, so the floor never raises a score.
 */
Propagated propagate_confidence(double base_confidence, const std::vector<Depth>& depths);

/** @brief (1 - temporal) * (1 - propagation). */
double final_weight(double temporal_penalty, double propagation_penalty);

}  // namespace SourceLedger
