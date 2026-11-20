#include "SourceLedger.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <set>

namespace SourceLedger {

namespace {

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

bool all_digits(const std::string& text, std::size_t pos, std::size_t count) {
    if (pos + count > text.size()) {
        return false;
    }
    for (std::size_t i = pos; i < pos + count; ++i) {
        if (!is_digit(text[i])) {
            return false;
        }
    }
    return true;
}

bool digit_at(const std::string& text, std::size_t pos) {
    return pos < text.size() && is_digit(text[pos]);
}

int number(const std::string& text, std::size_t pos, std::size_t count) {
    return std::stoi(text.substr(pos, count));
}

std::string fixed(double value, int places) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.*f", places, value);
    return buffer;
}

double round_to(double value, double scale) {
    return std::round(value * scale) / scale;
}

bool ends_with(const std::string& text, const std::string& tail) {
    return text.size() >= tail.size() && text.compare(text.size() - tail.size(), tail.size(), tail) == 0;
}

bool host_matches(const std::string& host, const std::string& entry) {
    if (entry.front() == '.') {
        return ends_with(host, entry);
    }
    return host == entry || ends_with(host, "." + entry);
}

template <std::size_t N>
bool host_in(const std::string& host, const std::array<const char*, N>& entries) {
    return std::any_of(entries.begin(), entries.end(), [&](const char* entry) { return host_matches(host, entry); });
}

const std::array<const char*, 20> kTier1 = {"arxiv.org",     "pubmed.ncbi.nlm.nih.gov",
                                            "jstor.org",     "nature.com",
                                            "science.org",   "sciencedirect.com",
                                            "springer.com",  "wiley.com",
                                            "thelancet.com", "bmj.com",
                                            "nejm.org",      "jamanetwork.com",
                                            ".ac.uk",        ".edu",
                                            "nih.gov",       "who.int",
                                            "gov.uk",        "europa.eu",
                                            "ons.gov.uk",    "parliament.uk"};

const std::array<const char*, 15> kTier2 = {"bbc.co.uk",       "bbc.com",       "reuters.com",        "apnews.com",
                                            "ft.com",          "economist.com", "theguardian.com",    "thetimes.co.uk",
                                            "telegraph.co.uk", "nytimes.com",   "washingtonpost.com", "bloomberg.com",
                                            "wsj.com",         "statista.com",  "ourworldindata.org"};

const std::array<const char*, 7> kLowSignal = {"reddit.com", "twitter.com",  "x.com",    "facebook.com",
                                               "medium.com", "substack.com", "quora.com"};

bool is_tracking_key(const std::string& key) {
    const std::string k = lower(key);
    return k.rfind("utm_", 0) == 0 || k == "fbclid" || k == "gclid" || k == "ref" || k == "source";
}

using Group = std::pair<std::string, std::vector<std::string>>;

void add_param(std::vector<Group>& groups, const std::string& part) {
    const auto eq = part.find('=');
    if (eq == std::string::npos || eq + 1 == part.size()) {
        return;
    }
    const std::string key = part.substr(0, eq);
    if (is_tracking_key(key)) {
        return;
    }
    auto it = std::find_if(groups.begin(), groups.end(), [&](const Group& g) { return g.first == key; });
    if (it == groups.end()) {
        groups.push_back({key, {}});
        it = groups.end() - 1;
    }
    it->second.push_back(part.substr(eq + 1));
}

std::string filtered_query(const std::string& query) {
    std::vector<Group> groups;
    std::size_t start = 0;
    while (start < query.size()) {
        auto end = query.find('&', start);
        if (end == std::string::npos) {
            end = query.size();
        }
        add_param(groups, query.substr(start, end - start));
        start = end + 1;
    }
    std::string out;
    for (const auto& group : groups) {
        for (const auto& value : group.second) {
            out += (out.empty() ? "" : "&") + group.first + "=" + value;
        }
    }
    return out;
}

bool is_leap(int year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int days_in_month(int year, int month) {
    static const std::array<int, 12> days = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return month == 2 && is_leap(year) ? 29 : days.at(static_cast<std::size_t>(month - 1));
}

bool valid_date(int year, int month, int day) {
    return year >= 1 && month >= 1 && month <= 12 && day >= 1 && day <= days_in_month(year, month);
}

long days_from_civil(const Date& date) {
    const long y = date.year - (date.month <= 2 ? 1 : 0);
    const long era = (y >= 0 ? y : y - 399) / 400;
    const long yoe = y - era * 400;
    const long m = date.month;
    const long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + date.day - 1;
    const long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

std::string iso(int year, int month, int day) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%04d-%02d-%02d", year, month, day);
    return buffer;
}

/** @brief True when "/20dd/mm" starts at pos with no digit after the month. */
bool path_date_at(const std::string& url, std::size_t pos) {
    return pos + 8 <= url.size() && url[pos] == '/' && url[pos + 1] == '2' && url[pos + 2] == '0' &&
           all_digits(url, pos + 3, 2) && url[pos + 5] == '/' && all_digits(url, pos + 6, 2) && !digit_at(url, pos + 8);
}

bool has_day(const std::string& url, std::size_t pos) {
    return pos + 11 <= url.size() && url[pos + 8] == '/' && all_digits(url, pos + 9, 2) && !digit_at(url, pos + 11);
}

std::optional<std::string> path_date(const std::string& url) {
    for (std::size_t pos = 0; pos < url.size(); ++pos) {
        if (!path_date_at(url, pos)) {
            continue;
        }
        const int year = number(url, pos + 1, 4);
        const int month = number(url, pos + 6, 2);
        const int day = has_day(url, pos) ? number(url, pos + 9, 2) : 1;
        if (!valid_date(year, month, day)) {
            return std::nullopt;
        }
        return iso(year, month, day);
    }
    return std::nullopt;
}

std::optional<std::string> arxiv_date_after(const std::string& lowered, const std::string& marker) {
    const auto at = lowered.find(marker);
    if (at == std::string::npos) {
        return std::nullopt;
    }
    const auto pos = at + marker.size();
    if (!all_digits(lowered, pos, 4) || lowered.compare(pos + 4, 1, ".") != 0) {
        return std::nullopt;
    }
    const int month = number(lowered, pos + 2, 2);
    if (month < 1 || month > 12) {
        return std::nullopt;
    }
    return iso(2000 + number(lowered, pos, 2), month, 1);
}

std::optional<std::string> arxiv_date(const std::string& url) {
    const std::string lowered = lower(url);
    if (auto d = arxiv_date_after(lowered, "arxiv.org/abs/")) {
        return d;
    }
    return arxiv_date_after(lowered, "arxiv.org/pdf/");
}

/** Subskill ST-5b: penalty starts after `threshold` years, falls by `rate` per year, and stops at `floor`. */
struct DecayParams {
    double threshold;
    double rate;
    double floor;
};

DecayParams params_of(Decay decay) {
    switch (decay) {
        case Decay::Stable:
            return {999.0, 0.0, 1.0};
        case Decay::Slow:
            return {5.0, 0.010, 0.70};
        case Decay::Medium:
            return {3.0, 0.033, 0.60};
        case Decay::Fast:
            return {1.0, 0.080, 0.50};
        case Decay::Breaking:
            return {0.5, 0.150, 0.40};
    }
    return {3.0, 0.033, 0.60};
}

Temporal stale_by_default(double base, const char* note) {
    return {round_to(base * 0.85, 1e4), 0.15, 15.0, note};
}

}  // namespace

const char* tier_name(Tier tier) {
    switch (tier) {
        case Tier::Tier1:
            return "TIER_1";
        case Tier::Tier2:
            return "TIER_2";
        case Tier::Tier3:
            return "TIER_3";
        case Tier::Tier4:
            return "TIER_4";
    }
    return "TIER_3";
}

std::string host_of(const std::string& url) {
    std::string s = lower(trim(url));
    const auto scheme = s.find("://");
    if (scheme != std::string::npos) {
        s.erase(0, scheme + 3);
    }
    const auto end = s.find_first_of("/?#");
    if (end != std::string::npos) {
        s.resize(end);
    }
    const auto at = s.rfind('@');
    if (at != std::string::npos) {
        s.erase(0, at + 1);
    }
    const auto colon = s.rfind(':');
    if (colon != std::string::npos && all_digits(s, colon + 1, s.size() - colon - 1)) {
        s.resize(colon);
    }
    return s;
}

Tier classify_tier(const std::string& url) {
    const std::string host = host_of(url);
    if (host_in(host, kTier1)) {
        return Tier::Tier1;
    }
    if (host_in(host, kTier2)) {
        return Tier::Tier2;
    }
    return host_in(host, kLowSignal) ? Tier::Tier4 : Tier::Tier3;
}

std::string clean_url(const std::string& url) {
    std::string s = trim(url);
    while (!s.empty() && s.back() == '/') {
        s.pop_back();
    }
    std::string fragment;
    const auto hash = s.find('#');
    if (hash != std::string::npos) {
        fragment = s.substr(hash);
        s.resize(hash);
    }
    std::string query;
    const auto mark = s.find('?');
    if (mark != std::string::npos) {
        query = s.substr(mark + 1);
        s.resize(mark);
    }
    while (!s.empty() && s.back() == '/') {
        s.pop_back();
    }
    const std::string kept = filtered_query(query);
    std::string out = s + (kept.empty() ? "" : "?" + kept) + (fragment == "#" ? "" : fragment);
    return lower(out);
}

namespace {

int register_source(Normalised& n, const std::string& key, const std::string& url, const std::string& researcher) {
    const auto found = n.lookup.find(key);
    if (found == n.lookup.end()) {
        const int id = static_cast<int>(n.sources.size()) + 1;
        n.sources.push_back({id, url, classify_tier(url), {researcher}, false});
        n.lookup[key] = id;
        return id;
    }
    auto& by = n.sources[static_cast<std::size_t>(found->second - 1)].found_by;
    if (std::find(by.begin(), by.end(), researcher) == by.end()) {
        by.push_back(researcher);
    }
    return found->second;
}

void summarise(Normalised& n) {
    for (auto& source : n.sources) {
        source.convergent = source.found_by.size() >= 3;
        if (source.convergent) {
            n.convergent_ids.push_back(source.id);
        }
        switch (source.tier) {
            case Tier::Tier1:
                ++n.summary.tier1;
                break;
            case Tier::Tier2:
                ++n.summary.tier2;
                break;
            case Tier::Tier3:
                ++n.summary.tier3;
                break;
            case Tier::Tier4:
                ++n.summary.tier4;
                break;
        }
    }
    n.summary.total = n.sources.size();
    n.summary.convergent = n.convergent_ids.size();
}

}  // namespace

Normalised normalise(const std::vector<ResearcherCitations>& outputs) {
    Normalised n;
    for (const auto& output : outputs) {
        auto& remap = n.researcher_maps[output.researcher];
        for (const auto& [tag, url] : output.citation_map) {
            const std::string key = clean_url(url);
            if (key.empty()) {
                continue;
            }
            remap[tag] = "[SOURCE_" + std::to_string(register_source(n, key, url, output.researcher)) + "]";
        }
    }
    summarise(n);
    return n;
}

std::string remap_content(const std::string& content, const std::map<std::string, std::string>& remap) {
    std::vector<const std::pair<const std::string, std::string>*> entries;
    for (const auto& entry : remap) {
        if (!entry.first.empty()) {
            entries.push_back(&entry);
        }
    }
    std::stable_sort(entries.begin(), entries.end(),
                     [](const auto* a, const auto* b) { return a->first.size() > b->first.size(); });
    std::string out;
    std::size_t i = 0;
    while (i < content.size()) {
        const auto hit = std::find_if(entries.begin(), entries.end(), [&](const auto* e) {
            return content.compare(i, e->first.size(), e->first) == 0;
        });
        if (hit == entries.end()) {
            out += content[i++];
        } else {
            out += (*hit)->second;
            i += (*hit)->first.size();
        }
    }
    return out;
}

std::vector<int> citation_ids_in(const std::string& text) {
    const std::string prefix = "[SOURCE_";
    std::vector<int> ids;
    std::size_t pos = 0;
    while ((pos = text.find(prefix, pos)) != std::string::npos) {
        pos += prefix.size();
        std::size_t end = pos;
        while (digit_at(text, end)) {
            ++end;
        }
        if (end > pos && end - pos <= 9 && end < text.size() && text[end] == ']') {
            ids.push_back(number(text, pos, end - pos));
        }
    }
    return ids;
}

std::vector<int> unregistered_citation_ids(const std::string& text, const std::vector<int>& registry_ids) {
    const std::set<int> known(registry_ids.begin(), registry_ids.end());
    std::set<int> seen;
    std::vector<int> stray;
    for (const int id : citation_ids_in(text)) {
        if (known.count(id) == 0 && seen.insert(id).second) {
            stray.push_back(id);
        }
    }
    return stray;
}

std::optional<Date> parse_iso_date(const std::string& text) {
    if (text.size() != 10 || text[4] != '-' || text[7] != '-' || !all_digits(text, 0, 4) || !all_digits(text, 5, 2) ||
        !all_digits(text, 8, 2)) {
        return std::nullopt;
    }
    const Date date{number(text, 0, 4), number(text, 5, 2), number(text, 8, 2)};
    if (!valid_date(date.year, date.month, date.day)) {
        return std::nullopt;
    }
    return date;
}

long days_between(const Date& a, const Date& b) {
    return days_from_civil(b) - days_from_civil(a);
}

std::optional<std::string> extract_source_date(const std::string& url) {
    if (auto d = path_date(url)) {
        return d;
    }
    return arxiv_date(url);
}

const char* decay_name(Decay decay) {
    switch (decay) {
        case Decay::Stable:
            return "STABLE";
        case Decay::Slow:
            return "SLOW";
        case Decay::Medium:
            return "MEDIUM";
        case Decay::Fast:
            return "FAST";
        case Decay::Breaking:
            return "BREAKING";
    }
    return "MEDIUM";
}

std::optional<Decay> parse_decay(const std::string& name) {
    const std::string wanted = lower(name);
    for (const Decay d : {Decay::Stable, Decay::Slow, Decay::Medium, Decay::Fast, Decay::Breaking}) {
        if (lower(decay_name(d)) == wanted) {
            return d;
        }
    }
    return std::nullopt;
}

Decay decay_for_domain(const std::string& domain_class) {
    static const std::map<std::string, Decay> table = {
        {"HISTORICAL", Decay::Stable},       {"PHILOSOPHICAL", Decay::Stable}, {"SCIENTIFIC_MEDICAL", Decay::Medium},
        {"LEGAL_REGULATORY", Decay::Fast},   {"TECHNOLOGY_AI", Decay::Fast},   {"POLITICAL", Decay::Breaking},
        {"ECONOMIC_FINANCIAL", Decay::Fast}, {"SOCIAL_CULTURAL", Decay::Slow}, {"GENERAL", Decay::Medium}};
    const auto it = table.find(domain_class);
    return it == table.end() ? Decay::Medium : it->second;
}

Temporal apply_temporal_penalty(double base_confidence, const std::optional<std::string>& source_date, Decay decay,
                                const std::string& today) {
    if (!source_date) {
        return stale_by_default(base_confidence, "UNKNOWN_DATE: treated as potentially stale (15% penalty)");
    }
    const auto from = parse_iso_date(*source_date);
    const auto to = parse_iso_date(today);
    if (!from || !to) {
        return stale_by_default(base_confidence, "INVALID_DATE: treated as potentially stale");
    }
    const double years = static_cast<double>(days_between(*from, *to)) / 365.25;
    const DecayParams p = params_of(decay);
    const std::string name = decay_name(decay);
    if (years <= p.threshold) {
        return {base_confidence, 0.0, 0.0, name + ": within freshness threshold (" + fixed(years, 1) + "y old)"};
    }
    const double multiplier = std::max(p.floor, 1.0 - p.rate * (years - p.threshold));
    const double percent = round_to((1.0 - multiplier) * 100.0, 10.0);
    return {round_to(base_confidence * multiplier, 1e4), percent / 100.0, percent,
            name + ": " + fixed(years, 1) + "y old, penalty " + fixed(percent, 1) + "%"};
}

std::vector<StaleItem> stale_items(const std::vector<std::optional<std::string>>& dates,
                                   const std::string& domain_class, const std::string& today,
                                   double threshold_percent) {
    const Decay decay = decay_for_domain(domain_class);
    std::vector<StaleItem> stale;
    for (std::size_t i = 0; i < dates.size(); ++i) {
        const Temporal t = apply_temporal_penalty(1.0, dates[i], decay, today);
        if (t.penalty_percent > threshold_percent) {
            stale.push_back({i, t.penalty_percent, t.note});
        }
    }
    return stale;
}

RegistryTemporal registry_temporal(const std::string& url, const std::optional<std::string>& known_date,
                                   const std::string& domain_class, const std::string& today) {
    RegistryTemporal r;
    r.decay = decay_for_domain(domain_class);
    if (known_date && !known_date->empty()) {
        r.source_date = known_date;
    } else {
        r.source_date = extract_source_date(url);
    }
    const Temporal t = apply_temporal_penalty(1.0, r.source_date, r.decay, today);
    r.penalty = round_to(t.penalty, 1e4);
    r.note = t.note;
    return r;
}

const char* depth_name(Depth depth) {
    switch (depth) {
        case Depth::Primary:
            return "PRIMARY";
        case Depth::Secondary:
            return "SECONDARY";
        case Depth::Tertiary:
            return "TERTIARY";
        case Depth::QuaternaryPlus:
            return "QUATERNARY+";
    }
    return "TERTIARY";
}

double depth_penalty(Depth depth) {
    switch (depth) {
        case Depth::Primary:
            return 0.0;
        case Depth::Secondary:
            return 0.05;
        case Depth::Tertiary:
            return 0.12;
        case Depth::QuaternaryPlus:
            return 0.22;
    }
    return 0.12;
}

Depth classify_citation_depth(const std::string& url, Tier tier) {
    if (tier != Tier::Tier1) {
        return Depth::Tertiary;
    }
    const std::string lowered = lower(url);
    for (const char* word : {"review", "meta", "systematic", "cochrane"}) {
        if (lowered.find(word) != std::string::npos) {
            return Depth::Secondary;
        }
    }
    return Depth::Primary;
}

Propagated propagate_confidence(double base_confidence, const std::vector<Depth>& depths) {
    if (depths.empty()) {
        return {base_confidence * 0.80, "NO_CITATIONS: confidence reduced", false};
    }
    std::array<int, 4> counts{};
    double total = 0.0;
    for (const Depth d : depths) {
        ++counts[static_cast<std::size_t>(d)];
        total += depth_penalty(d);
    }
    const double average = total / static_cast<double>(depths.size());
    std::size_t dominant = 0;
    for (std::size_t i = 1; i < counts.size(); ++i) {
        if (counts[i] >= counts[dominant]) {
            dominant = i;
        }
    }
    const bool deep = counts[static_cast<std::size_t>(Depth::QuaternaryPlus)] > 0;
    std::string note = std::string("Citation depth: ") + depth_name(static_cast<Depth>(dominant)) +
                       " dominant (avg penalty " + fixed(average * 100.0, 0) + "%)";
    if (deep) {
        note += " - CITATION_CHAIN_DEEP flag";
    }
    const double adjusted = std::max(std::min(base_confidence, 0.20), base_confidence * (1.0 - average));
    return {adjusted, note, deep};
}

double final_weight(double temporal_penalty, double propagation_penalty) {
    return (1.0 - temporal_penalty) * (1.0 - propagation_penalty);
}

}  // namespace SourceLedger
