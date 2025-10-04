#
# PROJECT:     ReactOS 3rd-party Libraries
# LICENSE:     BSD-2-Clause AND libpng-2.0
# PURPOSE:     Build integration for libspng
#

add_library(libspng STATIC
    ${REACTOS_SOURCE_DIR}/libspng/spng/spng.c)

# libspng relies on zlib for PNG compression handling
add_dependencies(libspng zlib)

# Propagate headers to consumers and add zlib headers for the library itself
# NOTE: spng.h expects to be included as <spng.h>
target_include_directories(libspng
    PUBLIC
        ${REACTOS_SOURCE_DIR}/libspng/spng
    PRIVATE
        ${REACTOS_SOURCE_DIR}/sdk/include/reactos/libs/zlib)

# Build in a configuration suitable for freeldr/kernel usage
# - Disable architecture-specific SIMD to avoid SSE/NEON requirements
# - Use the static API declarations expected on Windows
# - Request a freestanding-friendly build (no FILE* helpers are pulled in unless used)
target_compile_definitions(libspng
    PUBLIC
        SPNG_STATIC
    PRIVATE
        SPNG_DISABLE_OPT
        SPNG_DISABLE_FILEIO)

# Both freeldr and ntoskrnl link against libcntpr which already provides math helpers,
# so we do not explicitly add libm here.
target_link_libraries(libspng PRIVATE zlib)

# Keep optimisation flags conservative for boot-time binaries when building with GCC/Clang
if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
    target_compile_options(libspng PRIVATE
        -fno-strict-aliasing -ffunction-sections -fdata-sections
        -include
        "${CMAKE_CURRENT_SOURCE_DIR}/reactos_compat.h")
endif()
