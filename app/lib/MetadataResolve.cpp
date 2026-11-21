#include "MetadataResolve.hpp"

namespace MetadataResolve {

namespace {

bool clears(const std::string& value) {
    return value == MetadataPlan::kClearMarker;
}

/** The change a wanted value makes to the current one, or none when the tag is already as wanted. */
std::optional<MetadataPlanFile::Change> change_for(const std::string& key, const std::string& wanted,
                                                   const std::optional<std::string>& current,
                                                   const std::string& source) {
    const std::string target = clears(wanted) ? std::string() : wanted;
    const std::string now = current.value_or(std::string());
    if (target == now) {
        return std::nullopt;
    }
    return MetadataPlanFile::Change{key, current, target, source};
}

void add_change(std::vector<MetadataPlanFile::Change>& out, const std::optional<MetadataPlanFile::Change>& change) {
    if (change) {
        out.push_back(*change);
    }
}

std::vector<MetadataPlanFile::Change> changes_for(const MetadataPlan::Entry& entry, const std::string& source,
                                                  const Probe& probe) {
    std::vector<MetadataPlanFile::Change> out;
    for (const auto& [key, wanted] : entry.set) {
        add_change(out, change_for(key, wanted, probe.tag(entry.path, key), source));
    }
    for (const std::string& key : entry.clear) {
        add_change(out, change_for(key, std::string(), probe.tag(entry.path, key), source));
    }
    return out;
}

}  // namespace

MetadataPlanFile::Plan resolve(const MetadataPlan::Plan& plan, const std::string& source, const std::string& created,
                               const Probe& probe) {
    MetadataPlanFile::Plan out;
    out.source = source;
    out.created = created;
    out.warnings = plan.warnings;
    out.warnings.insert(out.warnings.end(), plan.skipped.begin(), plan.skipped.end());
    out.summary.files_scanned = plan.entries.size();
    for (const MetadataPlan::Entry& entry : plan.entries) {
        const std::optional<MetadataApply::Fingerprint> print = probe.fingerprint(entry.path);
        if (!print) {
            out.warnings.push_back(entry.path + ": cannot read the file");
            continue;
        }
        const std::string family = probe.family(entry.path);
        if (family.empty()) {
            out.warnings.push_back(entry.path + ": no tag writer for this file");
            continue;
        }
        MetadataPlanFile::Entry planned;
        planned.file = entry.path;
        planned.family = family;
        planned.fingerprint = *print;
        planned.changes = changes_for(entry, source, probe);
        if (!planned.changes.empty()) {
            out.entries.push_back(planned);
        }
    }
    out.summary.files_changing = out.entries.size();
    return out;
}

}  // namespace MetadataResolve
