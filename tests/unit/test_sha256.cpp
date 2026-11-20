#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <string>

#include "Sha256.hpp"

namespace {

// Reference digests come from the system sha256sum, not from this implementation.
struct Vector {
    std::size_t length;
    const char* digest;
};

std::string filled(std::size_t length) {
    return std::string(length, 'a');
}

}  // namespace

TEST_CASE("SHA-256 matches the standard test vectors", "[sha256]") {
    REQUIRE(Sha256::hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    REQUIRE(Sha256::hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    REQUIRE(Sha256::hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST_CASE("SHA-256 is right across the padding boundaries", "[sha256]") {
    const Vector vectors[] = {
        {1, "ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb"},
        {55, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"},
        {56, "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"},
        {63, "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34"},
        {64, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"},
        {65, "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0"},
        {119, "31eba51c313a5c08226adf18d4a359cfdfd8d2e816b13f4af952f7ea6584dcfb"},
        {120, "2f3d335432c70b580af0e8e1b3674a7c020d683aa5f73aaaedfdc55af904c21c"},
        {1000, "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3"},
    };
    for (const auto& v : vectors) {
        INFO("length " << v.length);
        REQUIRE(Sha256::hex(filled(v.length)) == v.digest);
    }
}

TEST_CASE("SHA-256 handles one million bytes and embedded NULs", "[sha256]") {
    REQUIRE(Sha256::hex(filled(1000000)) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    REQUIRE(Sha256::hex(std::string("hello\0world", 11)) ==
            "b206899bc103669c8e7b36de29d73f95b46795b508aa87d612b2ce84bfb29df2");
}

TEST_CASE("Feeding the hasher in odd chunks gives the same digest", "[sha256]") {
    const std::string data = filled(1000);
    for (std::size_t chunk : {1u, 7u, 63u, 64u, 65u, 333u}) {
        Sha256::Hasher hasher;
        for (std::size_t pos = 0; pos < data.size(); pos += chunk) {
            hasher.update(data.data() + pos, std::min(chunk, data.size() - pos));
        }
        INFO("chunk " << chunk);
        REQUIRE(hasher.finish_hex() == "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3");
    }
}

TEST_CASE("File hashing matches string hashing", "[sha256]") {
    const auto path = std::filesystem::temp_directory_path() / "aifs_sha256_test.bin";
    {
        std::ofstream out(path, std::ios::binary);
        out << filled(200000);  // bigger than one read buffer
    }
    REQUIRE(Sha256::file_hex(path) == Sha256::hex(filled(200000)));
    std::filesystem::remove(path);

    REQUIRE_FALSE(Sha256::file_hex(path).has_value());                                    // gone now
    REQUIRE_FALSE(Sha256::file_hex(std::filesystem::temp_directory_path()).has_value());  // a folder is not a file
}
