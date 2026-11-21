#pragma once

#include <functional>
#include <optional>
#include <string>

#include "MetadataPlan.hpp"
#include "MetadataPlanFile.hpp"

/**
 * @brief Turns a request-level plan (what to set or clear per file) into a saved plan by checking each file as it
 *        is now. Values already in place are left out, so the saved plan lists only real changes. Reading the
 *        files is handed in, which keeps this pure.
 */
namespace MetadataResolve {

struct Probe {
    /** @brief "id3", "mp4" or "vorbis"; empty when no tag writer takes the file. */
    std::function<std::string(const std::string& path)> family;
    /** @brief Size and modification time now; nullopt when the file cannot be read. */
    std::function<std::optional<MetadataApply::Fingerprint>(const std::string& path)> fingerprint;
    /** @brief The tag as it stands; nullopt when absent. */
    std::function<std::optional<std::string>(const std::string& path, const std::string& key)> tag;
};

/**
 * @brief Build the saved plan. A file the probe cannot read or no writer takes is warned about and left out; a
 *        file with nothing to change is counted as scanned and left out. Set keys come first in key order, then
 *        clears; a clear of an absent or empty tag is not a change.
 */
MetadataPlanFile::Plan resolve(const MetadataPlan::Plan& plan, const std::string& source, const std::string& created,
                               const Probe& probe);

}  // namespace MetadataResolve
