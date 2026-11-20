// dewey-cli: the file-organising rules from the Dewey skills, without the GUI.
// Dry run by default. Nothing is moved without --apply, nothing is ever deleted or overwritten.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include "DecisionRegister.hpp"
#include "DeweyEngine.hpp"
#include "FileOrganizerRules.hpp"
#include "LinkAudit.hpp"
#include "QuietCouncil.hpp"
#include "Sha256.hpp"
#include "SourceLedger.hpp"
#include "WikiLint.hpp"

namespace fs = std::filesystem;
namespace rules = FileOrganizerRules;

namespace {

constexpr int kOk = 0;
constexpr int kFailed = 1;
constexpr int kUsage = 2;

const char* const kUsageText =
    "usage:\n"
    "  dewey-cli organise <folder> [--apply] [--archive-days N] [--protect PREFIX]... [--log FILE]\n"
    "  dewey-cli undo <folder> --log FILE [--apply]\n"
    "  dewey-cli duplicates <folder>\n"
    "  dewey-cli wiki-lint <vault> [--schema wiki|dewey]\n"
    "  dewey-cli register-check <register.md>\n"
    "  dewey-cli anchors\n"
    "  dewey-cli sha256 <file>\n"
    "  dewey-cli link-audit <skills-folder>\n"
    "  dewey-cli link-chains <skills-folder>\n"
    "  dewey-cli link-unwire <skills-folder> <skill> [replacement]\n"
    "  dewey-cli council-classify <topic> [--constraint TEXT]...\n"
    "  dewey-cli council-cost <quick|standard|deep|academic> [--ack]\n"
    "  dewey-cli council-session-id <kg-folder> [--date YYYY-MM-DD]\n"
    "  dewey-cli council-sources <citations.tsv>\n"
    "  dewey-cli council-kg-check <kg-folder> --topic TEXT [--domain CLASS] [--today YYYY-MM-DD]\n"
    "\n"
    "organise and undo only print a plan unless --apply is given. The other commands only report.\n";

std::int64_t now_epoch() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

/** C++17 has no portable file_time_type to system_clock conversion, so measure the offset from now. */
std::int64_t mtime_epoch(const fs::path& path) {
    std::error_code ec;
    const auto written = fs::last_write_time(path, ec);
    if (ec) {
        return 0;
    }
    const auto age = std::chrono::duration_cast<std::chrono::seconds>(fs::file_time_type::clock::now() - written);
    return now_epoch() - age.count();
}

struct Args {
    std::string command;
    std::vector<std::string> positional;
    bool apply{false};
    bool ack{false};
    int archive_days{180};
    std::vector<std::string> protect;
    std::vector<std::string> constraints;
    std::string log_path;
    std::string schema{"wiki"};
    std::string topic;
    std::string domain;
    std::string today;
    std::string date;
    bool ok{true};
    std::string error;
};

void set_archive_days(Args& args, const std::string& value) {
    try {
        args.archive_days = std::stoi(value);
    } catch (const std::exception&) {
        args.ok = false;
        args.error = "--archive-days needs a whole number";
    }
}

/** Options that take a value. Returns false for anything else. */
bool apply_value_option(Args& args, const std::string& name, const std::string& value) {
    using Setter = void (*)(Args&, const std::string&);
    static const std::map<std::string, Setter> setters = {
        {"--archive-days", set_archive_days},
        {"--protect", [](Args& a, const std::string& v) { a.protect.push_back(v); }},
        {"--constraint", [](Args& a, const std::string& v) { a.constraints.push_back(v); }},
        {"--log", [](Args& a, const std::string& v) { a.log_path = v; }},
        {"--schema", [](Args& a, const std::string& v) { a.schema = v; }},
        {"--topic", [](Args& a, const std::string& v) { a.topic = v; }},
        {"--domain", [](Args& a, const std::string& v) { a.domain = v; }},
        {"--today", [](Args& a, const std::string& v) { a.today = v; }},
        {"--date", [](Args& a, const std::string& v) { a.date = v; }},
    };
    const auto setter = setters.find(name);
    if (setter == setters.end()) {
        return false;
    }
    setter->second(args, value);
    return true;
}

Args parse_args(int argc, char** argv) {
    Args args;
    if (argc < 2) {
        args.ok = false;
        return args;
    }
    args.command = argv[1];
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool has_value = i + 1 < argc;
        if (arg == "--apply") {
            args.apply = true;
        } else if (arg == "--ack") {
            args.ack = true;
        } else if (has_value && apply_value_option(args, arg, argv[i + 1])) {
            ++i;
        } else if (arg.rfind("--", 0) == 0) {
            args.ok = false;
            args.error = "unknown or incomplete option: " + arg;
        } else {
            args.positional.push_back(arg);
        }
    }
    return args;
}

bool read_file(const fs::path& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    out = buffer.str();
    return true;
}

/** Rename without ever replacing something that is already there. Returns an empty string on success. */
std::string move_one(const fs::path& root, const rules::Move& move) {
    const fs::path from = root / move.from;
    const fs::path to = root / move.to;
    std::error_code ec;
    if (!fs::exists(from, ec)) {
        return "missing source";
    }
    if (fs::exists(to, ec)) {
        return "destination already exists";
    }
    fs::create_directories(to.parent_path(), ec);
    if (ec) {
        return "cannot create folder: " + ec.message();
    }
    fs::rename(from, to, ec);
    return ec ? ec.message() : std::string();
}

void print_plan(const rules::Plan& plan, bool apply) {
    for (const auto& move : plan.moves) {
        std::cout << (apply ? "move  " : "would ") << move.from << "  ->  " << move.to << "\n";
    }
    for (const auto& note : plan.skipped) {
        std::cout << "skip  " << note << "\n";
    }
    std::cout << plan.moves.size() << " move(s), " << plan.skipped.size() << " skipped\n";
}

/** Run the plan, returning the moves that really happened so the log only ever tells the truth. */
rules::Plan apply_plan(const fs::path& root, const rules::Plan& plan, int& failures) {
    rules::Plan done;
    for (const auto& move : plan.moves) {
        const std::string problem = move_one(root, move);
        if (problem.empty()) {
            done.moves.push_back(move);
        } else {
            std::cerr << "failed " << move.from << ": " << problem << "\n";
            ++failures;
        }
    }
    return done;
}

int run_organise(const Args& args) {
    if (args.positional.size() != 1) {
        return kUsage;
    }
    const fs::path root = args.positional[0];
    std::error_code ec;
    if (!fs::is_directory(root, ec)) {
        std::cerr << "not a folder: " << root << "\n";
        return kFailed;
    }

    std::vector<rules::FileInfo> files;
    std::vector<std::string> names;
    std::vector<std::string> symlinks;
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        const std::string name = entry.path().filename().string();
        names.push_back(name);
        if (entry.is_symlink(ec)) {
            symlinks.push_back(name);  // moving a link can break it, and a broken one has no usable date
            continue;
        }
        rules::FileInfo info;
        info.name = name;
        info.is_directory = entry.is_directory(ec);
        info.mtime_seconds = mtime_epoch(entry.path());
        files.push_back(info);
    }

    rules::Options options;
    options.archive_after_days = args.archive_days;
    options.protected_prefixes = args.protect;
    rules::Plan plan = rules::plan_by_type(files, now_epoch(), options, names);
    for (const auto& name : symlinks) {
        plan.skipped.push_back(name + ": symbolic link left in place");
    }
    print_plan(plan, args.apply);
    if (!args.apply) {
        std::cout << "dry run: nothing moved. Add --apply to do it.\n";
        return kOk;
    }

    int failures = 0;
    const rules::Plan done = apply_plan(root, plan, failures);
    const fs::path log_path = args.log_path.empty() ? root / (".dewey-moves-" + std::to_string(now_epoch()) + ".tsv")
                                                    : fs::path(args.log_path);
    std::ofstream log(log_path, std::ios::binary);
    log << rules::serialize_log(done);
    if (!log) {
        std::cerr << "could not write the undo log to " << log_path << "\n";
        return kFailed;
    }
    std::cout << "undo log: " << log_path.string() << "\n";
    return failures == 0 ? kOk : kFailed;
}

int run_undo(const Args& args) {
    if (args.positional.size() != 1 || args.log_path.empty()) {
        return kUsage;
    }
    const fs::path root = args.positional[0];
    std::string text;
    if (!read_file(args.log_path, text)) {
        std::cerr << "cannot read log: " << args.log_path << "\n";
        return kFailed;
    }
    const rules::Plan plan = rules::undo_plan(rules::parse_log(text));
    print_plan(plan, args.apply);
    if (!args.apply) {
        std::cout << "dry run: nothing moved. Add --apply to do it.\n";
        return kOk;
    }
    int failures = 0;
    apply_plan(root, plan, failures);
    std::cout << "folders created by the original run are left in place\n";
    return failures == 0 ? kOk : kFailed;
}

struct Seen {
    fs::path path;
    std::uint64_t size{0};
    std::int64_t mtime{0};
    std::string hash;
};

int run_duplicates(const Args& args) {
    if (args.positional.size() != 1) {
        return kUsage;
    }
    const fs::path root = args.positional[0];
    std::error_code ec;
    if (!fs::is_directory(root, ec)) {
        std::cerr << "not a folder: " << root << "\n";
        return kFailed;
    }

    std::map<std::uint64_t, std::vector<fs::path>> by_size;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; it != end;
         it.increment(ec)) {
        if (ec) {
            break;
        }
        std::error_code entry_ec;
        if (!it->is_regular_file(entry_ec) || it->is_symlink(entry_ec)) {
            continue;
        }
        const auto size = it->file_size(entry_ec);
        if (!entry_ec && size > 0) {  // empty files all match each other, which says nothing
            by_size[size].push_back(it->path());
        }
    }

    int groups = 0;
    for (const auto& [size, paths] : by_size) {
        if (paths.size() < 2) {
            continue;
        }
        std::map<std::string, std::vector<Seen>> by_hash;
        for (const auto& path : paths) {
            const auto hash = Sha256::file_hex(path);
            if (hash) {
                by_hash[*hash].push_back({path, size, mtime_epoch(path), *hash});
            }
        }
        for (const auto& [hash, copies] : by_hash) {
            if (copies.size() < 2) {
                continue;
            }
            ++groups;
            std::cout << "group " << groups << "  " << size << " bytes  sha256 " << hash.substr(0, 12) << "\n";
            for (const auto& copy : copies) {
                std::cout << "  " << fs::relative(copy.path, root, ec).string() << "\n";
            }
            DeweyEngine::FileEvidence a{copies[0].path.filename().string(), copies[0].size, copies[0].mtime,
                                        copies[0].hash};
            DeweyEngine::FileEvidence b{copies[1].path.filename().string(), copies[1].size, copies[1].mtime,
                                        copies[1].hash};
            const auto tier = DeweyEngine::duplicate_tier(a, b);
            const auto action = DeweyEngine::recommend_action(tier, false);
            std::cout << "  evidence: "
                      << (tier == DeweyEngine::EvidenceTier::Confirmed ? "confirmed" : "weaker than confirmed")
                      << ", suggested: "
                      << (action == DeweyEngine::DuplicateAction::Archive ? "archive the extras" : "review by hand")
                      << "\n";
        }
    }
    std::cout << groups << " group(s) of identical files. Nothing was changed.\n";
    return kOk;
}

int run_wiki_lint(const Args& args) {
    if (args.positional.size() != 1 || (args.schema != "wiki" && args.schema != "dewey")) {
        return kUsage;
    }
    const fs::path root = args.positional[0];
    std::error_code ec;
    if (!fs::is_directory(root, ec)) {
        std::cerr << "not a folder: " << root << "\n";
        return kFailed;
    }

    std::vector<WikiLint::Page> pages;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; it != end;
         it.increment(ec)) {
        if (ec) {
            break;
        }
        std::error_code entry_ec;
        if (it->is_directory(entry_ec) && it->path().filename().string().rfind(".", 0) == 0) {
            it.disable_recursion_pending();  // .obsidian, .git and friends are not notes
            continue;
        }
        if (!it->is_regular_file(entry_ec) || it->is_symlink(entry_ec) || it->path().extension() != ".md") {
            continue;
        }
        std::string text;
        if (read_file(it->path(), text)) {
            pages.push_back({fs::relative(it->path(), root, ec).generic_string(), text});
        }
    }
    std::sort(pages.begin(), pages.end(),
              [](const WikiLint::Page& a, const WikiLint::Page& b) { return a.path < b.path; });

    const auto& fields = args.schema == "dewey" ? WikiLint::dewey_required_fields() : WikiLint::required_fields();
    const auto issues = WikiLint::lint(pages, fields);
    for (const auto& issue : issues) {
        std::cout << (issue.severity == WikiLint::Severity::Error ? "error    " : "warning  ") << issue.page << "  "
                  << issue.code << "  " << issue.message << "\n";
    }
    const std::size_t errors = WikiLint::count_errors(issues);
    std::cout << pages.size() << " page(s), " << errors << " error(s), " << issues.size() - errors << " warning(s)\n";
    return errors == 0 ? kOk : kFailed;
}

int run_register_check(const Args& args) {
    if (args.positional.size() != 1) {
        return kUsage;
    }
    std::string text;
    if (!read_file(args.positional[0], text)) {
        std::cerr << "cannot read register: " << args.positional[0] << "\n";
        return kFailed;
    }
    const auto entries = DecisionRegister::parse_register(text);
    std::vector<std::string> problems;
    for (const auto& entry : entries) {
        std::vector<DecisionRegister::Entry> others;
        std::copy_if(entries.begin(), entries.end(), std::back_inserter(others),
                     [&](const DecisionRegister::Entry& e) { return &e != &entry; });
        for (const auto& problem : DecisionRegister::validate_entry(entry, others)) {
            problems.push_back("entry " + std::to_string(entry.number) + ": " + problem);
        }
    }
    for (const auto& finding : DecisionRegister::consistency_pass(entries)) {
        problems.push_back(finding);
    }
    for (const auto& problem : problems) {
        std::cout << problem << "\n";
    }
    std::cout << entries.size() << " entries, " << problems.size()
              << " problem(s). Contradictions with the sub-skills still need a person.\n";
    return problems.empty() ? kOk : kFailed;
}

int run_anchors() {
    for (const auto& anchor : DeweyEngine::anchor_vocabulary()) {
        std::cout << anchor.code << "  " << anchor.label << "\n";
    }
    return kOk;
}

int run_sha256(const Args& args) {
    if (args.positional.size() != 1) {
        return kUsage;
    }
    const auto hash = Sha256::file_hex(args.positional[0]);
    if (!hash) {
        std::cerr << "cannot read: " << args.positional[0] << "\n";
        return kFailed;
    }
    std::cout << *hash << "  " << args.positional[0] << "\n";
    return kOk;
}

// ---- skill links ---------------------------------------------------------------------------------------------
// A skills folder holds one folder per skill; every .md inside a skill folder belongs to that skill.
// Hidden folders, symlinks and anything that is not Markdown are left out.

void collect_skill_markdown(const fs::path& root, const fs::path& skill_dir, std::vector<LinkAudit::SkillFile>& files) {
    std::error_code ec;
    const std::string skill = skill_dir.filename().string();
    for (fs::recursive_directory_iterator it(skill_dir, fs::directory_options::skip_permission_denied, ec), end;
         it != end; it.increment(ec)) {
        if (ec) {
            break;
        }
        std::error_code entry_ec;
        if (it->is_directory(entry_ec) && it->path().filename().string().rfind(".", 0) == 0) {
            it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(entry_ec) || it->is_symlink(entry_ec) || it->path().extension() != ".md") {
            continue;
        }
        std::string text;
        if (read_file(it->path(), text)) {
            files.push_back({skill, fs::relative(it->path(), root, ec).generic_string(), text});
        }
    }
}

bool load_skills(const fs::path& root, std::vector<LinkAudit::SkillFile>& files) {
    std::error_code ec;
    if (!fs::is_directory(root, ec)) {
        std::cerr << "not a folder: " << root << "\n";
        return false;
    }
    std::vector<fs::path> skill_dirs;
    for (fs::directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; it != end;
         it.increment(ec)) {
        if (ec) {
            break;
        }
        std::error_code entry_ec;
        const bool hidden = it->path().filename().string().rfind(".", 0) == 0;
        if (it->is_directory(entry_ec) && !it->is_symlink(entry_ec) && !hidden) {
            skill_dirs.push_back(it->path());
        }
    }
    for (const auto& dir : skill_dirs) {
        collect_skill_markdown(root, dir, files);
    }
    std::sort(files.begin(), files.end(),
              [](const LinkAudit::SkillFile& a, const LinkAudit::SkillFile& b) { return a.file < b.file; });
    return true;
}

bool has_skill(const std::vector<LinkAudit::SkillFile>& files, const std::string& skill) {
    return std::any_of(files.begin(), files.end(), [&](const LinkAudit::SkillFile& f) { return f.skill == skill; });
}

std::string today_utc() {
    const std::time_t now = std::time(nullptr);
    const std::tm* utc = std::gmtime(&now);
    char buffer[16];
    if (utc == nullptr || std::strftime(buffer, sizeof buffer, "%Y-%m-%d", utc) == 0) {
        return "unknown";
    }
    return buffer;
}

int run_link_audit(const Args& args) {
    std::vector<LinkAudit::SkillFile> files;
    if (args.positional.size() != 1) {
        return kUsage;
    }
    if (!load_skills(args.positional[0], files)) {
        return kFailed;
    }
    std::set<std::string> skills;
    for (const auto& f : files) {
        skills.insert(f.skill);
    }
    const LinkAudit::Report report = LinkAudit::audit(files);
    const std::string scope = args.positional[0] + " (" + std::to_string(skills.size()) + " skills)";
    std::cout << LinkAudit::render_report(report, scope, today_utc());
    if (report.checked == 0) {
        std::cout << "Note: no contract blocks were found. Only '### > skill' and '### < skill' headings are read, so "
                     "skills written in an older contract style show up as having no links.\n";
    }
    // Critical, High and Medium are the report's "failures". Low findings are advice.
    const bool failed = std::any_of(report.findings.begin(), report.findings.end(),
                                    [](const LinkAudit::Finding& f) { return f.severity != LinkAudit::Severity::Low; });
    return failed ? kFailed : kOk;
}

int run_link_chains(const Args& args) {
    std::vector<LinkAudit::SkillFile> files;
    if (args.positional.size() != 1) {
        return kUsage;
    }
    if (!load_skills(args.positional[0], files)) {
        return kFailed;
    }
    std::vector<LinkAudit::Contract> contracts;
    for (const auto& f : files) {
        for (auto& c : LinkAudit::parse_contracts(f)) {
            contracts.push_back(std::move(c));
        }
    }
    const auto chains = LinkAudit::find_chains(contracts);
    for (const auto& chain : chains) {
        for (std::size_t i = 0; i < chain.skills.size(); ++i) {
            std::cout << (i == 0 ? "" : " > ") << chain.skills[i];
        }
        std::cout << (chain.loops_back ? "  (loops back)" : "") << "\n";
    }
    std::cout << chains.size() << " chain(s) of three or more skills\n";
    return kOk;
}

void print_mentions(const char* label, const std::vector<LinkAudit::Mention>& mentions) {
    for (const auto& m : mentions) {
        std::cout << label << "  " << m.file << ":" << m.line << "  " << m.text << "\n";
    }
}

/** The replacement protocol: the new skill has to be fully wired before the old one is touched. */
int check_replacement(const std::vector<LinkAudit::SkillFile>& files, const std::string& replacement) {
    if (!has_skill(files, replacement)) {
        std::cerr << "no such skill: " << replacement << "\n";
        return kFailed;
    }
    const bool wired = LinkAudit::replacement_is_wired(files, replacement);
    std::cout << "replacement " << replacement << ": "
              << (wired ? "wired" : "NOT wired, wire it before unwiring the old skill") << "\n";
    return wired ? kOk : kFailed;
}

int run_link_unwire(const Args& args) {
    std::vector<LinkAudit::SkillFile> files;
    if (args.positional.size() < 2 || args.positional.size() > 3) {
        return kUsage;
    }
    if (!load_skills(args.positional[0], files)) {
        return kFailed;
    }
    const std::string& departing = args.positional[1];
    if (!has_skill(files, departing)) {
        std::cerr << "no such skill: " << departing << "\n";
        return kFailed;
    }
    const LinkAudit::BlastRadius radius = LinkAudit::blast_radius(files, departing);
    // Same order as BlastRadius::in_update_order, with the category kept so the reader knows what to do.
    print_mentions("receives", radius.receivers);
    print_mentions("sends   ", radius.senders);
    print_mentions("chain   ", radius.chain_annotations);
    print_mentions("prose   ", radius.prose);
    std::cout << radius.in_update_order().size() << " reference(s) to " << departing
              << ", listed in the order to update them. Nothing was changed.\n";
    return args.positional.size() == 3 ? check_replacement(files, args.positional[2]) : kOk;
}

std::string fixed(double value, int places) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.*f", places, value);
    return buffer;
}

void print_domain(const QuietCouncil::DomainConfig& config) {
    std::cout << "domain  " << config.domain_class << "\n";
    for (const auto& r : QuietCouncil::researchers()) {
        const auto weight = config.researcher_weights.find(r.name);
        if (weight != config.researcher_weights.end()) {
            std::cout << "weight  " << r.name << "  " << fixed(weight->second, 2) << "\n";
        }
    }
    for (const auto& r : QuietCouncil::researchers()) {
        const auto temperature = config.temperature_overrides.find(r.name);
        if (temperature != config.temperature_overrides.end()) {
            std::cout << "temperature  " << r.name << "  " << fixed(temperature->second, 2) << "\n";
        }
    }
    std::cout << "addenda  " << config.prompt_addenda.size() << "\n";
}

int run_council_classify(const Args& args) {
    if (args.positional.size() != 1) {
        return kUsage;
    }
    print_domain(QuietCouncil::classify_domain(args.positional[0], args.constraints));
    return kOk;
}

int run_council_cost(const Args& args) {
    if (args.positional.size() != 1) {
        return kUsage;
    }
    const auto depth = QuietCouncil::parse_depth(args.positional[0]);
    if (!depth) {
        std::cerr << "unknown depth: " << args.positional[0] << "\n";
        return kUsage;
    }
    const QuietCouncil::CostEstimate cost = QuietCouncil::estimate_cost(*depth);
    if (QuietCouncil::needs_cost_ack(*depth) && !args.ack) {
        std::cerr << "academic depth is estimated at USD " << fixed(cost.total_usd, 3)
                  << " a session. Run it again with --ack to confirm.\n";
        return kFailed;
    }
    std::cout << "depth  " << QuietCouncil::depth_name(*depth) << "\n";
    std::cout << "models  " << cost.models.researcher << " / " << cost.models.contrarian << "\n";
    std::cout << "researchers  USD " << fixed(cost.researcher_usd, 3) << "\n";
    std::cout << "contrarian  USD " << fixed(cost.contrarian_usd, 3) << "\n";
    std::cout << "total  USD " << fixed(cost.total_usd, 3) << "\n";
    std::cout << "Estimate only. The prices are the skill's own figures and may be out of date.\n";
    return kOk;
}

bool valid_date(const std::string& text) {
    return SourceLedger::parse_iso_date(text).has_value();
}

int run_council_session_id(const Args& args) {
    if (args.positional.size() != 1) {
        return kUsage;
    }
    const std::string date = args.date.empty() ? today_utc() : args.date;
    if (!valid_date(date)) {
        std::cerr << "--date needs a real date written YYYY-MM-DD\n";
        return kUsage;
    }
    if (!fs::is_directory(args.positional[0])) {
        std::cerr << "no such folder: " << args.positional[0] << "\n";
        return kFailed;
    }
    const std::string log = QuietCouncil::read_file(fs::path(args.positional[0]) / "session-log.md");
    std::cout << QuietCouncil::next_session_id(date, log) << "\n";
    return kOk;
}

std::vector<std::string> split_tabs(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (true) {
        const std::size_t tab = line.find('\t', start);
        if (tab == std::string::npos) {
            fields.push_back(line.substr(start));
            return fields;
        }
        fields.push_back(line.substr(start, tab - start));
        start = tab + 1;
    }
}

/** Group researcher<TAB>tag<TAB>url rows by researcher, in order of first appearance. Returns the bad line count. */
int read_citation_rows(const std::string& text, std::vector<SourceLedger::ResearcherCitations>& out) {
    int bad = 0;
    std::istringstream lines(text);
    std::string line;
    for (int number = 1; std::getline(lines, line); ++number) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        const auto fields = split_tabs(line);
        if (fields.size() != 3) {
            std::cerr << "line " << number << ": need researcher, tag and url separated by tabs\n";
            ++bad;
            continue;
        }
        auto found = std::find_if(out.begin(), out.end(), [&](const SourceLedger::ResearcherCitations& r) {
            return r.researcher == fields[0];
        });
        if (found == out.end()) {
            out.push_back({fields[0], {}});
            found = out.end() - 1;
        }
        found->citation_map.emplace_back(fields[1], fields[2]);
    }
    return bad;
}

void print_sources(const SourceLedger::Normalised& registry) {
    for (const auto& source : registry.sources) {
        std::cout << "SOURCE_" << source.id << "  " << SourceLedger::tier_name(source.tier) << "  " << source.url
                  << "  found by: ";
        for (std::size_t i = 0; i < source.found_by.size(); ++i) {
            std::cout << (i == 0 ? "" : ", ") << source.found_by[i];
        }
        std::cout << (source.convergent ? "  convergent" : "") << "\n";
    }
    const SourceLedger::Summary& sum = registry.summary;
    std::cout << sum.total << " source(s): tier 1 " << sum.tier1 << ", tier 2 " << sum.tier2 << ", tier 3 " << sum.tier3
              << ", tier 4 " << sum.tier4 << ", convergent " << sum.convergent << "\n";
}

int run_council_sources(const Args& args) {
    if (args.positional.size() != 1) {
        return kUsage;
    }
    std::string text;
    if (!read_file(args.positional[0], text)) {
        std::cerr << "cannot read " << args.positional[0] << "\n";
        return kFailed;
    }
    std::vector<SourceLedger::ResearcherCitations> outputs;
    const int bad = read_citation_rows(text, outputs);
    print_sources(SourceLedger::normalise(outputs));
    return bad == 0 ? kOk : kFailed;
}

int run_council_kg_check(const Args& args) {
    if (args.positional.size() != 1 || args.topic.empty()) {
        return kUsage;
    }
    const std::string today = args.today.empty() ? today_utc() : args.today;
    if (!valid_date(today)) {
        std::cerr << "--today needs a real date written YYYY-MM-DD\n";
        return kUsage;
    }
    if (!fs::is_directory(args.positional[0])) {
        std::cerr << "no such folder: " << args.positional[0] << "\n";
        return kFailed;
    }
    const auto facts = QuietCouncil::parse_confirmed_facts(
        QuietCouncil::read_file(fs::path(args.positional[0]) / "confirmed-facts.md"));
    const std::string topic = QuietCouncil::slug(args.topic);
    const auto mine = QuietCouncil::facts_for_topic(facts, topic);
    const std::string block = QuietCouncil::prior_knowledge_block(mine);
    std::cout << "topic  " << topic << "\n";
    std::cout << (block.empty() ? "no prior knowledge for " + topic : block) << "\n";
    const std::string domain = args.domain.empty() ? "GENERAL" : args.domain;
    for (const std::size_t index : QuietCouncil::stale_fact_indexes(mine, domain, today)) {
        std::cout << "stale  " << mine[index].id << "  " << QuietCouncil::fact_date(mine[index]).value_or("undated")
                  << "\n";
    }
    return kOk;
}

using Handler = int (*)(const Args&);

int dispatch(const Args& args) {
    static const std::map<std::string, Handler> handlers = {{"organise", run_organise},
                                                            {"undo", run_undo},
                                                            {"duplicates", run_duplicates},
                                                            {"wiki-lint", run_wiki_lint},
                                                            {"register-check", run_register_check},
                                                            {"anchors", [](const Args&) { return run_anchors(); }},
                                                            {"sha256", run_sha256},
                                                            {"link-audit", run_link_audit},
                                                            {"link-chains", run_link_chains},
                                                            {"link-unwire", run_link_unwire},
                                                            {"council-classify", run_council_classify},
                                                            {"council-cost", run_council_cost},
                                                            {"council-session-id", run_council_session_id},
                                                            {"council-sources", run_council_sources},
                                                            {"council-kg-check", run_council_kg_check}};
    const auto handler = handlers.find(args.command);
    return handler == handlers.end() ? kUsage : handler->second(args);
}

}  // namespace

int main(int argc, char** argv) {
    const Args args = parse_args(argc, argv);
    int code = args.ok ? dispatch(args) : kUsage;
    if (!args.error.empty()) {
        std::cerr << args.error << "\n";
    }
    if (code == kUsage) {
        std::cerr << kUsageText;
    }
    return code;
}
