# libs/crc32c.cmake

include(FetchContent)

cmake_policy(PUSH)  # Save current policy settings
cmake_policy(SET CMP0091 NEW)  # Set the new policy

FetchContent_Declare(
    crc32c
    GIT_REPOSITORY https://github.com/google/crc32c.git
    GIT_TAG 1.1.2
)

set(CRC32C_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CRC32C_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(crc32c)

cmake_policy(POP)  # Restore previous policy settings

