#include <ctime>
#include <iostream>
#include <string>
#include <vector>

#include "MetadataCli.hpp"

namespace {

std::string timestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char buffer[40];
    std::strftime(buffer, sizeof buffer, "%Y-%m-%dT%H:%M:%S%z", &local);
    return buffer;
}

}  // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    return MetadataCli::run(args, std::cout, std::cerr, timestamp());
}
