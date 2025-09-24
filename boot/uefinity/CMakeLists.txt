##############################################################################
# Uefinity - Multi-Architecture UEFI Bootloader
# Copyright (C) 2024 Ahmed ARIF (Arif193@gmail.com)
#
# ReactOS Integration Build System
##############################################################################

# Build for ARM64 and AMD64 UEFI systems
if(NOT (ARCH STREQUAL "arm64" OR ARCH STREQUAL "amd64"))
    message(STATUS "Uefinity: Skipping build for ${ARCH} - ARM64/AMD64 only")
    return()
endif()

message(STATUS "Building Uefinity UEFI Bootloader for ${ARCH}")

# Include ReactOS standard directories
include_directories(BEFORE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/arch
    ${CMAKE_CURRENT_SOURCE_DIR}/include/uefi
    ${REACTOS_SOURCE_DIR}/boot/environ/include/efi
    ${REACTOS_SOURCE_DIR}/ntoskrnl/include
    ${REACTOS_SOURCE_DIR}/sdk/lib/cmlib
    ${REACTOS_SOURCE_DIR}/sdk/include/reactos/libs)

# Use ReactOS definitions
add_definitions(-D_NTHAL_ -D_BLDR_ -D_NTSYSTEM_ -DUEFIBOOT -DUEFI_ONLY)

# Exclude unnecessary components for UEFI-only build
add_definitions(-DNO_ELF_SUPPORT -DNO_BIOS_SUPPORT -DNO_MULTIBOOT_SUPPORT)

# Architecture specific definitions
if(ARCH STREQUAL "arm64")
    add_definitions(-D_ARM64_ -DARM64 -D_M_ARM64)
elseif(ARCH STREQUAL "amd64")
    add_definitions(-D_AMD64_ -DAMD64 -D_M_AMD64)
endif()

# Common source files (architecture-independent)
set(UEFINITY_COMMON_SOURCES
    # Boot management
    common/boot/bootmgr.c
    common/boot/oslist.c
    common/boot/custom.c
    common/boot/options.c
    common/boot/settings.c
    common/boot/arcname.c

    # UEFI services
    common/uefi/ueficon.c
    common/uefi/uefidisk.c
    common/uefi/uefimem.c
    common/uefi/uefivid.c
    common/uefi/uefihw.c
    common/uefi/uefiutil.c
    common/uefi/uefisetup.c
    common/uefi/uefidebug.c
    common/uefi/uefibacktrace.c
    common/uefi/uefiserial.c
    common/uefi/efiapp.c
    common/uefi/stubs.c
    common/uefi/uistubs.c
    common/uefi/uefiarc.c

    # Filesystem drivers
    common/fs/fat.c
    common/fs/ntfs.c
    common/fs/ext.c
    common/fs/btrfs.c
    common/fs/iso.c
    common/fs/fs.c

    # Libraries
    common/lib/peloader.c
    common/lib/inifile.c
    common/lib/ini_init.c
    common/lib/parse.c
    common/lib/mm.c
    common/lib/heap.c
    common/lib/meminit.c
    common/lib/arcsupp.c
    common/lib/debug.c
    common/lib/libsupp.c
    common/lib/cache/blocklist.c
    common/lib/cache/cache.c
    $<$<NOT:$<STREQUAL:${ARCH},arm64>>:common/lib/comm/rs232.c>

    # Disk support
    common/disk/disk.c
    common/disk/partition.c
    common/disk/ramdisk.c

    # Architecture support
    common/arch/arcemul.c
    common/arch/archwsup.c
    common/arch/vgafont.c

    # NT Loader (Windows boot support)
    common/ntldr/winldr.c
    common/ntldr/wlmemory.c
    common/ntldr/wlregistry.c
    common/ntldr/conversion.c
    common/ntldr/registry.c
    common/ntldr/ntldropts.c
)

# Architecture specific source files
if(ARCH STREQUAL "arm64")
    set(UEFINITY_ARCH_SOURCES
        arch/arm64/macharm64.c
        arch/arm64/trap.c
        arch/arm64/mmu_v2.c
        arch/arm64/gic.c
        arch/arm64/timer.c
        arch/arm64/stubs.c
    )

    set(UEFINITY_ARCH_ASM_SOURCES
        arch/arm64/entry.S
        arch/arm64/except.S
        arch/arm64/misc.S
        arch/arm64/cache_v2.S
        arch/uefi/arm64/uefiasm.S
    )
elseif(ARCH STREQUAL "amd64")
    set(UEFINITY_ARCH_SOURCES
        arch/amd64/uefitrap.c
        # NT Loader for AMD64
        common/ntldr/amd64/winldr.c
    )

    set(UEFINITY_ARCH_ASM_SOURCES
        arch/amd64/misc.S
        arch/amd64/uefiasm.S
        arch/amd64/uefitrap.S
    )
endif()

# Assemble ASM files
add_asm_files(uefinity_asm ${UEFINITY_ARCH_ASM_SOURCES})

# Create the bootloader library
add_library(uefinity_lib
    ${uefinity_asm}
    ${UEFINITY_COMMON_SOURCES}
    ${UEFINITY_ARCH_SOURCES}
)

target_compile_definitions(uefinity_lib PRIVATE UEFIBOOT)

# Main bootloader entry source
set(UEFINITY_BASE_SOURCE
    include/arch/arch_interface.h
    common/uefi/uefildr.c
    common/boot/freeldr.c
)

# Create the main executable target
add_executable(uefinity ${UEFINITY_BASE_SOURCE})
set_target_properties(uefinity PROPERTIES SUFFIX ".efi")

target_compile_definitions(uefinity PRIVATE UEFIBOOT)

# Architecture specific image base
if(ARCH STREQUAL "arm64" OR ARCH STREQUAL "amd64")
    set_image_base(uefinity 0x10000)
endif()

# Linker options based on compiler
if(MSVC)
    if(NOT ARCH STREQUAL "arm")
        target_link_options(uefinity PRIVATE /DYNAMICBASE:NO)
    endif()
    target_link_options(uefinity PRIVATE /NXCOMPAT:NO /ignore:4078 /ignore:4254 /DRIVER)
    remove_target_compile_option(uefinity "/hotpatch")
else()
    target_link_options(uefinity PRIVATE -Wl,--exclude-all-symbols,--file-alignment,0x200,--section-alignment,0x200)
    # Strip everything, including rossym data
    add_custom_command(TARGET uefinity
                    POST_BUILD
                    COMMAND ${CMAKE_STRIP} --remove-section=.rossym $<TARGET_FILE:uefinity>
                    COMMAND ${CMAKE_STRIP} --strip-all $<TARGET_FILE:uefinity>)
endif()

# Set subsystem
if(MSVC)
    set_subsystem(uefinity EFI_APPLICATION)
else()
    set_subsystem(uefinity 10)
endif()

# Set entry point
set_entrypoint(uefinity EfiEntry)

# Link against ReactOS libraries
if(ARCH STREQUAL "arm64")
    target_link_libraries(uefinity uefinity_lib libcntpr blrtl)
elseif(ARCH STREQUAL "amd64")
    target_link_libraries(uefinity uefinity_lib cportlib blcmlib blrtl libcntpr)
endif()

# Dynamic analysis switches (from ReactOS)
if(STACK_PROTECTOR)
    target_sources(uefinity PRIVATE $<TARGET_OBJECTS:gcc_ssp_nt>)
endif()

if(RUNTIME_CHECKS)
    target_link_libraries(uefinity runtmchk)
endif()

# Add dependencies
add_dependencies(uefinity xdk)

message(STATUS "Uefinity: Created target 'uefinity' as UEFI bootloader for ${ARCH}")
