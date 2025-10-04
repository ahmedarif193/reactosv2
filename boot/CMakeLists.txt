
# Determine default settings based on architecture
if(ARCH STREQUAL "amd64" OR ARCH STREQUAL "arm64")
    set(_freeldr_default OFF)
    set(_uefinity_default ON)
else()
    set(_freeldr_default ON)
    set(_uefinity_default OFF)
endif()

option(BUILD_FREELDR "Build legacy FreeLoader BIOS boot chain" ${_freeldr_default})
option(BUILD_UEFINITY "Build the UEFI-only multi-architecture bootloader" ${_uefinity_default})

if(ARCH STREQUAL "amd64" OR ARCH STREQUAL "arm64")
    add_subdirectory(environ)
endif()

if(BUILD_UEFINITY AND (ARCH STREQUAL "amd64" OR ARCH STREQUAL "arm64"))
    add_subdirectory(uefinity)
endif()

include(boot_images.cmake)

add_subdirectory(bootdata)

if(BUILD_FREELDR)
    add_subdirectory(freeldr)
endif()

add_subdirectory(rtl)
