cmake_minimum_required(VERSION 3.16)
project(dewey-cli LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Standalone on purpose: std only, no Qt, so it builds anywhere a C++17 compiler does.
set(APP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../app")

add_executable(dewey-cli
    main.cpp
    "${APP_DIR}/lib/DeweyEngine.cpp"
    "${APP_DIR}/lib/FileOrganizerRules.cpp"
    "${APP_DIR}/lib/Sha256.cpp"
    "${APP_DIR}/lib/WikiLint.cpp"
    "${APP_DIR}/lib/DecisionRegister.cpp"
    "${APP_DIR}/lib/LinkAudit.cpp"
    "${APP_DIR}/lib/SourceLedger.cpp"
    "${APP_DIR}/lib/QuietCouncil.cpp")
target_include_directories(dewey-cli PRIVATE "${APP_DIR}/include")
target_compile_options(dewey-cli PRIVATE -Wall -Wextra -Wpedantic)
