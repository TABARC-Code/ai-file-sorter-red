#include "MetadataApply.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <set>
#include <system_error>

#include "MetadataTags.hpp"

namespace fs = std::filesystem;

namespace metadata_apply {

namespace {

bool ends_with(const std::string& text, const std::string& tail) {
    return text.size() >= tail.size() && text.compare(text.size() - tail.size(), tail.size(), tail) == 0;
}

bool is_space(char c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

std::string trim(const std::string& text) {
    const auto first = std::find_if_not(text.begin(), text.end(), is_space);
    const auto last = std::find_if_not(text.rbegin(), text.rend(), is_space).base();
    return first < last ? std::string(first, last) : std::string();
}

bool all_digits(const std::string& text) {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; });
}

std::string strip_zeros(const std::string& digits) {
    const auto at = digits.find_first_not_of('0');
    return at == std::string::npos ? "0" : digits.substr(at);
}

bool is_number_key(const std::string& key) {
    return key == "track" || key == "disc";
}

bool is_mark(const std::string& name) {
    return ends_with(name, kBackupSuffix) || ends_with(name, kCreatedSuffix);
}

// Like Python's copy2: contents and modification time, so a backup keeps the plan's fingerprint.
bool copy_keeping_time(const std::string& from, const std::string& to) {
    std::error_code ec;
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        return false;
    }
    const auto stamp = fs::last_write_time(from, ec);
    if (ec) {
        return false;
    }
    fs::last_write_time(to, stamp, ec);
    return !ec;
}

Outcome failure(const std::string& message) {
    Outcome out;
    out.error = message;
    return out;
}

void put_back(const std::string& path, const std::string& backup) {
    copy_keeping_time(backup, path);
    std::error_code ec;
    fs::remove(backup, ec);
}

std::map<std::string, std::string> read_back(const std::string& path, const std::vector<Edit>& edits) {
    std::map<std::string, std::string> got;
    for (const Edit& edit : edits) {
        if (const auto value = metadata_tags::read_tag(path, edit.key)) {
            got[edit.key] = *value;
        }
    }
    return got;
}

// A backup matching the plan is ours from a crashed run: roll back to it. Anything else is someone else's.
std::string settle_old_backup(const std::string& path, const std::string& backup, const Fingerprint& planned) {
    const auto theirs = fingerprint(backup);
    if (!theirs || !(*theirs == planned)) {
        return "backup exists (" + fs::path(backup).filename().string() + ") from another job; restore or clean first";
    }
    if (!copy_keeping_time(backup, path)) {
        return "could not roll back the earlier attempt";
    }
    std::error_code ec;
    fs::remove(backup, ec);
    return "";
}

void collect_from_folder(const fs::path& folder, std::set<std::string>& found) {
    std::error_code ec;
    fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec);
    for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_directory(ec) && is_mark(it->path().filename().string())) {
            found.insert(it->path().string());
        }
    }
}

// Returns false when the target does not exist.
bool collect_from_target(const std::string& target, std::set<std::string>& found) {
    const fs::path p(target);
    std::error_code ec;
    if (fs::is_directory(p, ec)) {
        collect_from_folder(p, found);
    } else if (fs::exists(target + kBackupSuffix, ec)) {
        found.insert(target + kBackupSuffix);
    } else if (is_mark(p.filename().string()) && fs::exists(p, ec)) {
        found.insert(target);
    } else if (!fs::exists(p, ec)) {
        return false;
    }
    return true;
}

std::string without_suffix(const std::string& path, const std::string& suffix) {
    return path.substr(0, path.size() - suffix.size());
}

}  // namespace

std::string normalise(const std::string& value, const std::string& key) {
    const std::string text = trim(value);
    if (!is_number_key(key)) {
        return text;
    }
    const auto slash = text.find('/');
    const std::string head = text.substr(0, slash);
    const std::string tail = slash == std::string::npos ? "" : text.substr(slash + 1);
    if (!all_digits(head) || (slash != std::string::npos && !all_digits(tail))) {
        return text;
    }
    const std::string total = tail.empty() ? "0" : strip_zeros(tail);
    return total == "0" ? strip_zeros(head) : strip_zeros(head) + "/" + total;
}

std::optional<Fingerprint> fingerprint(const std::string& path) {
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    if (ec) {
        return std::nullopt;
    }
    const auto stamp = fs::last_write_time(path, ec);
    if (ec) {
        return std::nullopt;
    }
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(stamp.time_since_epoch()).count();
    return Fingerprint{size, static_cast<long long>(ns)};
}

Verdict verify_edits(const std::vector<Edit>& edits, const std::map<std::string, std::string>& got) {
    Verdict verdict;
    for (const Edit& edit : edits) {
        const auto found = got.find(edit.key);
        const bool present = found != got.end();
        const bool ok =
            edit.to.empty() ? !present : present && normalise(found->second, edit.key) == normalise(edit.to, edit.key);
        if (ok) {
            verdict.applied.push_back(edit.key);
        } else {
            verdict.dropped.push_back(
                {edit.key, edit.to, present ? std::optional<std::string>(found->second) : std::nullopt});
        }
    }
    return verdict;
}

Outcome apply_entry(const std::string& path, const Fingerprint& planned, const std::vector<Edit>& edits,
                    bool no_backup) {
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        return failure("file gone");
    }
    const std::string backup = path + kBackupSuffix;
    if (!no_backup && fs::exists(backup, ec)) {
        const std::string problem = settle_old_backup(path, backup, planned);
        if (!problem.empty()) {
            return failure(problem);
        }
    }
    const auto current = fingerprint(path);
    if (!current || !(*current == planned)) {
        return failure("changed since plan; re-plan");
    }
    if (!no_backup && !copy_keeping_time(path, backup)) {
        return failure("could not make a backup");
    }
    std::vector<metadata_tags::Change> changes;
    for (const Edit& edit : edits) {
        changes.push_back({edit.key, edit.to});
    }
    const auto wrote = metadata_tags::write_tags(path, changes);
    if (!wrote.ok) {
        if (!no_backup) {
            put_back(path, backup);
        }
        return failure(wrote.error);
    }
    Outcome out;
    out.ok = true;
    out.verdict = verify_edits(edits, read_back(path, edits));
    return out;
}

Marks find_marks(const std::vector<std::string>& targets) {
    std::set<std::string> found;
    Marks out;
    for (const std::string& target : targets) {
        if (!collect_from_target(target, found)) {
            out.error = "not found: " + target;
            return out;
        }
    }
    out.paths.assign(found.begin(), found.end());
    return out;
}

RestoreResult restore(const std::vector<std::string>& targets) {
    RestoreResult result;
    const Marks marks = find_marks(targets);
    if (!marks.error.empty()) {
        result.error = marks.error;
        return result;
    }
    for (const std::string& mark : marks.paths) {
        std::error_code ec;
        if (ends_with(mark, kCreatedSuffix)) {
            const std::string original = without_suffix(mark, kCreatedSuffix);
            fs::remove(original, ec);
            fs::remove(mark, ec);
            result.removed_created.push_back(original);
            continue;
        }
        const std::string original = without_suffix(mark, kBackupSuffix);
        fs::rename(mark, original, ec);
        if (ec) {
            result.error = "could not restore " + original + ": " + ec.message();
            return result;
        }
        result.restored.push_back(original);
    }
    return result;
}

CleanResult clean(const std::vector<std::string>& targets, bool yes) {
    CleanResult result;
    const Marks marks = find_marks(targets);
    if (!marks.error.empty()) {
        result.error = marks.error;
        return result;
    }
    if (!yes) {
        result.would_delete = marks.paths;
        return result;
    }
    for (const std::string& mark : marks.paths) {
        std::error_code ec;
        fs::remove(mark, ec);
        result.deleted.push_back(mark);
    }
    return result;
}

}  // namespace metadata_apply
