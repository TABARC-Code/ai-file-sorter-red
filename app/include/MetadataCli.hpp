#pragma once

#include <iosfwd>
#include <string>
#include <vector>

/**
 * @brief The command line behind the metadata modules. `plan` checks files against what you want set or cleared
 *        and prints a saved plan; `apply` carries a saved plan out, tag files first, then folder files, and can
 *        record the run in a ledger. The clock is passed in so a run can be repeated exactly.
 *
 *        plan  --source TEXT [--set key=value]... [--clear key]... [--ext ext]... [--out FILE] PATH...
 *        apply PLAN [--no-backup] [--restart] [--batch N] [--progress FILE] [--ledger FILE]
 */
namespace MetadataCli {

/** @return 0 on success, 1 when the work failed or part of it did, 2 for a usage mistake. */
int run(const std::vector<std::string>& args, std::ostream& out, std::ostream& err, const std::string& now);

}  // namespace MetadataCli
