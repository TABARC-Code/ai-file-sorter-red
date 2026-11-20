#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

/**
 * @brief Standard-library-only SHA-256, so duplicate checks can rest on a real hash
 *        without pulling in a crypto library.
 */
namespace Sha256 {

/** @brief Incremental hasher. Call finish_hex() once; after that the object is spent. */
class Hasher {
   public:
    Hasher();

    void update(const void* data, std::size_t size);

    /** @brief Lower-case hex digest, 64 characters. */
    std::string finish_hex();

   private:
    void process_block(const std::uint8_t* block);

    std::array<std::uint32_t, 8> state_;
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t buffered_{0};
    std::uint64_t total_bytes_{0};
};

/** @brief Hex digest of a byte string. */
std::string hex(const std::string& data);

/** @brief Hex digest of a file's bytes, or nullopt when the file cannot be read. */
std::optional<std::string> file_hex(const std::filesystem::path& path);

}  // namespace Sha256
