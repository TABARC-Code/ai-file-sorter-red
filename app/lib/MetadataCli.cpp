#include "MetadataCli.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <ostream>
#include <set>
#include <sstream>

#include "MetadataApply.hpp"
#include "MetadataBatch.hpp"
#include "MetadataFolder.hpp"
#include "MetadataLedger.hpp"
#include "MetadataPlan.hpp"
#include "MetadataPlanFile.hpp"
#include "MetadataResolve.hpp"
#include "MetadataTags.hpp"

namespace fs = std::filesystem;

namespace MetadataCli {

namespace {

const char* const kUsage =
    "usage:\n"
    "  plan  --source TEXT [--set key=value]... [--clear key]... [--ext ext]... [--out FILE] PATH...\n"
    "  apply PLAN [--no-backup] [--restart] [--batch N] [--progress FILE] [--ledger FILE]\n";

/** Walks the arguments, handing each flag its value. Anything not starting with "--" is a positional. */
class Args {
   public:
    explicit Args(const std::vector<std::string>& args) : args_(args) {}

    bool done() const { return at_ >= args_.size(); }
    const std::string& current() const { return args_[at_]; }
    bool is_flag() const { return current().rfind("--", 0) == 0; }
    void next() { ++at_; }

    /** The value after a flag, or false when the flag is last. */
    bool value(std::string& out) {
        if (at_ + 1 >= args_.size()) {
            return false;
        }
        out = args_[++at_];
        return true;
    }

   private:
    const std::vector<std::string>& args_;
    std::size_t at_{0};
};

bool read_text(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

// ---------------------------------------------------------------- plan

struct PlanArgs {
    MetadataPlan::Request request;
    std::vector<std::string> paths;
    std::string out_file;
};

/** Empty when the flag was understood, else the complaint. */
std::string take_plan_flag(const std::string& flag, Args& args, PlanArgs& plan) {
    static const std::set<std::string> kKnown = {"--source", "--set", "--clear", "--ext", "--out"};
    if (kKnown.count(flag) == 0) {
        return "unknown option: " + flag;
    }
    std::string value;
    if (!args.value(value)) {
        return "missing value for " + flag;
    }
    if (flag == "--source") {
        plan.request.source = value;
    } else if (flag == "--set") {
        const auto pair = MetadataPlan::parse_set(value);
        if (!pair) {
            return "bad --set: " + value;
        }
        plan.request.sets.push_back(*pair);
    } else if (flag == "--clear") {
        plan.request.sets.emplace_back(value, MetadataPlan::kClearMarker);
    } else if (flag == "--ext") {
        plan.request.only_extensions.push_back(value);
    } else {
        plan.out_file = value;
    }
    return "";
}

std::string parse_plan_args(const std::vector<std::string>& raw, PlanArgs& plan) {
    Args args(raw);
    while (!args.done()) {
        if (args.is_flag()) {
            const std::string flag = args.current();
            const std::string problem = take_plan_flag(flag, args, plan);
            if (!problem.empty()) {
                return problem;
            }
        } else {
            plan.paths.push_back(args.current());
        }
        args.next();
    }
    return plan.paths.empty() ? "no paths given" : "";
}

/** Files as given, with each folder replaced by the files under it in path order. */
std::vector<std::string> expand_paths(const std::vector<std::string>& paths) {
    std::vector<std::string> files;
    for (const std::string& path : paths) {
        std::error_code ec;
        if (!fs::is_directory(path, ec)) {
            files.push_back(path);
            continue;
        }
        std::vector<std::string> found;
        for (fs::recursive_directory_iterator it(path, ec), end; !ec && it != end; it.increment(ec)) {
            if (it->is_regular_file(ec)) {
                found.push_back(it->path().string());
            }
        }
        std::sort(found.begin(), found.end());
        files.insert(files.end(), found.begin(), found.end());
    }
    return files;
}

std::string family_name(const std::string& path) {
    switch (MetadataTags::family_of(path)) {
        case MetadataTags::Family::Id3:
            return "id3";
        case MetadataTags::Family::Mp4:
            return "mp4";
        case MetadataTags::Family::Vorbis:
            return "vorbis";
        case MetadataTags::Family::Unsupported:
            break;
    }
    return "";
}

MetadataResolve::Probe disk_probe() {
    MetadataResolve::Probe probe;
    probe.family = family_name;
    probe.fingerprint = MetadataApply::fingerprint;
    probe.tag = MetadataTags::read_tag;
    return probe;
}

int write_out(const std::string& file, const std::string& text, std::ostream& out, std::ostream& err) {
    if (file.empty()) {
        out << text;
        return 0;
    }
    std::ofstream target(file, std::ios::binary);
    target << text;
    target.flush();
    if (!target) {
        err << "cannot write " << file << "\n";
        return 1;
    }
    return 0;
}

int run_plan(const std::vector<std::string>& raw, std::ostream& out, std::ostream& err, const std::string& now) {
    PlanArgs args;
    const std::string problem = parse_plan_args(raw, args);
    if (!problem.empty()) {
        err << problem << "\n" << kUsage;
        return 2;
    }
    const MetadataPlan::Plan wanted = MetadataPlan::build_plan(expand_paths(args.paths), args.request);
    if (!wanted.ok()) {
        err << wanted.error << "\n";
        return 1;
    }
    const MetadataPlanFile::Plan saved = MetadataResolve::resolve(wanted, args.request.source, now, disk_probe());
    for (const std::string& warning : saved.warnings) {
        err << warning << "\n";
    }
    return write_out(args.out_file, MetadataPlanFile::serialise(saved), out, err);
}

// ---------------------------------------------------------------- apply

struct ApplyArgs {
    std::string plan;
    MetadataBatch::Options batch;
    std::string ledger;
};

/** Empty when the flag was understood, else the complaint. */
std::string take_apply_flag(const std::string& flag, Args& args, ApplyArgs& apply) {
    if (flag == "--no-backup") {
        apply.batch.no_backup = true;
        return "";
    }
    if (flag == "--restart") {
        apply.batch.restart = true;
        return "";
    }
    static const std::set<std::string> kKnown = {"--progress", "--ledger", "--batch"};
    if (kKnown.count(flag) == 0) {
        return "unknown option: " + flag;
    }
    std::string value;
    if (!args.value(value)) {
        return "missing value for " + flag;
    }
    if (flag == "--progress") {
        apply.batch.progress_file = value;
    } else if (flag == "--ledger") {
        apply.ledger = value;
    } else {
        const long size = std::strtol(value.c_str(), nullptr, 10);
        if (size < 1) {
            return "bad --batch: " + value;
        }
        apply.batch.batch_size = static_cast<std::size_t>(size);
    }
    return "";
}

std::string parse_apply_args(const std::vector<std::string>& raw, ApplyArgs& apply) {
    Args args(raw);
    while (!args.done()) {
        if (args.is_flag()) {
            const std::string flag = args.current();
            const std::string problem = take_apply_flag(flag, args, apply);
            if (!problem.empty()) {
                return problem;
            }
        } else if (apply.plan.empty()) {
            apply.plan = args.current();
        } else {
            return "more than one plan given";
        }
        args.next();
    }
    return apply.plan.empty() ? "no plan given" : "";
}

struct FolderTally {
    std::size_t written{0};
    std::vector<std::string> errors;
};

FolderTally apply_folders(const MetadataPlanFile::Plan& plan, bool no_backup, const std::string& now) {
    FolderTally tally;
    for (const MetadataPlanFile::Folder& folder : plan.folders) {
        const auto result = MetadataFolder::apply_folder(folder.dir, MetadataPlanFile::folder_changes(folder),
                                                         no_backup, now, MetadataPlanFile::kTool);
        if (result.ok) {
            ++tally.written;
        } else {
            tally.errors.push_back(folder.dir + ": " + result.error);
        }
    }
    return tally;
}

std::string plural(std::size_t count, const std::string& noun) {
    return std::to_string(count) + " " + noun + (count == 1 ? "" : "s");
}

void say_result(const MetadataBatch::Report& report, const FolderTally& folders, std::ostream& out, std::ostream& err) {
    out << plural(report.ok, "file") << " written, " << report.already_done << " already done, "
        << report.failures.size() << " failed, " << plural(folders.written, "folder") << " written\n";
    for (const MetadataBatch::Failure& failure : report.failures) {
        err << failure.path << ": " << failure.error << "\n";
    }
    for (const std::string& dropped : report.dropped) {
        err << "dropped: " << dropped << "\n";
    }
    for (const std::string& problem : folders.errors) {
        err << problem << "\n";
    }
}

void record_run(const ApplyArgs& args, const MetadataBatch::Report& report, const FolderTally& folders,
                const std::string& now, std::ostream& err) {
    MetadataLedger::ApplyRecord record;
    record.plan = args.plan;
    record.ok = report.ok;
    record.already_done = report.already_done;
    record.folders_written = folders.written;
    record.failures = report.failures;
    record.dropped = report.dropped;
    const std::string problem = MetadataLedger::append_line(args.ledger, MetadataLedger::apply_line(now, record));
    if (!problem.empty()) {
        err << problem << "\n";
    }
}

int run_apply(const std::vector<std::string>& raw, std::ostream& out, std::ostream& err, const std::string& now) {
    ApplyArgs args;
    const std::string problem = parse_apply_args(raw, args);
    if (!problem.empty()) {
        err << problem << "\n" << kUsage;
        return 2;
    }
    std::string text;
    if (!read_text(args.plan, text)) {
        err << "cannot read " << args.plan << "\n";
        return 1;
    }
    const MetadataPlanFile::Parsed parsed = MetadataPlanFile::parse(text);
    if (!parsed.ok()) {
        err << parsed.error << "\n";
        return 1;
    }
    const MetadataPlanFile::BatchInput input = MetadataPlanFile::batch_input(parsed.plan);
    for (const std::string& note : input.unsupported) {
        err << note << "\n";
    }
    const MetadataBatch::Report report = MetadataBatch::run(input.entries, args.batch);
    if (!report.error.empty()) {
        err << report.error << "\n";
        return 1;
    }
    const FolderTally folders = apply_folders(parsed.plan, args.batch.no_backup, now);
    say_result(report, folders, out, err);
    if (!args.ledger.empty()) {
        record_run(args, report, folders, now, err);
    }
    return report.failures.empty() && folders.errors.empty() ? 0 : 1;
}

}  // namespace

int run(const std::vector<std::string>& args, std::ostream& out, std::ostream& err, const std::string& now) {
    if (args.empty()) {
        err << kUsage;
        return 2;
    }
    const std::vector<std::string> rest(args.begin() + 1, args.end());
    if (args[0] == "plan") {
        return run_plan(rest, out, err, now);
    }
    if (args[0] == "apply") {
        return run_apply(rest, out, err, now);
    }
    err << "unknown command: " << args[0] << "\n" << kUsage;
    return 2;
}

}  // namespace MetadataCli
