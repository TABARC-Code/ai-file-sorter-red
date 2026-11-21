#include "MetadataBatch.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;

namespace metadata_batch {

namespace {

std::string escape(const std::string& text) {
    std::string out;
    for (const char c : text) {
        switch (c) {
            case '\\':
                out += "\\\\";
                break;
            case '\t':
                out += "\\t";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            default:
                out += c;
        }
    }
    return out;
}

std::string unescape(const std::string& text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\\' || i + 1 == text.size()) {
            out += text[i];
            continue;
        }
        const char next = text[++i];
        out += next == 't' ? '\t' : next == 'n' ? '\n' : next == 'r' ? '\r' : next;
    }
    return out;
}

std::string slurp(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::uintmax_t disk_free(const std::string& folder) {
    std::error_code ec;
    const auto info = fs::space(folder, ec);
    return ec ? 0 : info.available;
}

std::string folder_of(const std::string& path) {
    return fs::path(path).parent_path().string();
}

std::set<std::string> load_done(const Options& options) {
    if (options.progress_file.empty()) {
        return {};
    }
    if (options.restart) {
        std::error_code ec;
        fs::remove(options.progress_file, ec);
        return {};
    }
    return parse_progress(slurp(options.progress_file));
}

void record(std::ofstream& progress, const std::string& line) {
    if (progress.is_open()) {
        progress << line << '\n';
        progress.flush();
    }
}

void run_one(const PlannedEntry& entry, const Options& options, std::ofstream& progress, Report& report) {
    const auto outcome = metadata_apply::apply_entry(entry.path, entry.fingerprint, entry.edits, options.no_backup);
    if (!outcome.ok) {
        report.failures.push_back({entry.path, outcome.error});
        record(progress, encode_progress("failed", entry.path, outcome.error));
        return;
    }
    ++report.ok;
    const auto drops = describe_drops(entry.path, outcome.verdict);
    report.dropped.insert(report.dropped.end(), drops.begin(), drops.end());
    record(progress, encode_progress("ok", entry.path));
}

}  // namespace

std::string encode_progress(const std::string& status, const std::string& path, const std::string& detail) {
    std::string line = status + "\t" + escape(path);
    if (!detail.empty()) {
        line += "\t" + escape(detail);
    }
    return line;
}

std::set<std::string> parse_progress(const std::string& text) {
    std::set<std::string> done;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const auto first = line.find('\t');
        if (first == std::string::npos || line.compare(0, first, "ok") != 0) {
            continue;
        }
        const auto second = line.find('\t', first + 1);
        done.insert(unescape(line.substr(first + 1, second == std::string::npos ? second : second - first - 1)));
    }
    return done;
}

std::string space_problem(const std::vector<PlannedEntry>& entries, bool no_backup, const FreeSpace& free_space) {
    std::map<std::string, std::uintmax_t> need;
    for (const PlannedEntry& entry : entries) {
        need[folder_of(entry.path)] += no_backup ? 0 : entry.fingerprint.size;
    }
    for (const auto& item : need) {
        const std::uintmax_t free = free_space(item.first);
        // Ten percent spare, in whole numbers so an exact fit is not lost to rounding.
        if (free * 10 < item.second * 11) {
            return "not enough free space in " + item.first + ": need about " +
                   std::to_string(item.second / 1000000 + 1) + " MB, have " + std::to_string(free / 1000000) + " MB";
        }
    }
    return "";
}

std::vector<std::string> describe_drops(const std::string& path, const metadata_apply::Verdict& verdict) {
    const std::string name = fs::path(path).filename().string();
    std::vector<std::string> out;
    for (const auto& dropped : verdict.dropped) {
        out.push_back(name + ": " + dropped.key);
    }
    return out;
}

Report run(const std::vector<PlannedEntry>& entries, const Options& options) {
    Report report;
    const std::set<std::string> done = load_done(options);
    std::vector<PlannedEntry> todo;
    for (const PlannedEntry& entry : entries) {
        if (done.count(entry.path) != 0) {
            ++report.already_done;
        } else {
            todo.push_back(entry);
        }
    }
    report.error =
        space_problem(todo, options.no_backup, options.free_space ? options.free_space : FreeSpace(disk_free));
    if (!report.error.empty()) {
        return report;
    }
    std::ofstream progress;
    if (!options.progress_file.empty()) {
        progress.open(options.progress_file, std::ios::app | std::ios::binary);
    }
    const std::size_t size = std::max<std::size_t>(options.batch_size, 1);
    const std::size_t count = (todo.size() + size - 1) / size;
    for (std::size_t start = 0; start < todo.size(); start += size) {
        ++report.batches;
        const std::size_t ok_before = report.ok;
        const std::size_t failed_before = report.failures.size();
        const std::size_t end = std::min(start + size, todo.size());
        for (std::size_t i = start; i < end; ++i) {
            run_one(todo[i], options, progress, report);
        }
        if (options.on_batch) {
            options.on_batch({report.batches, count, report.ok - ok_before, report.failures.size() - failed_before});
        }
    }
    return report;
}

}  // namespace metadata_batch
