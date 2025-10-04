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

# Display symbol configuration
if(UEFINITY_DEBUG_SYMBOLS)
    message(STATUS "Uefinity: Full debug symbols enabled")
elseif(UEFINITY_EMBED_SYMBOLS)
    message(STATUS "Uefinity: Lightweight symbol embedding enabled for backtraces")
else()
    message(STATUS "Uefinity: Symbol embedding disabled - backtraces will show raw addresses only")
endif()

# Option: keep and emit debug symbols for the UEFI image
option(UEFINITY_DEBUG_SYMBOLS "Build Uefinity with debug symbols (do not strip)" OFF)

# Option: embed lightweight symbol table for backtraces (always recommended)
option(UEFINITY_EMBED_SYMBOLS "Embed lightweight symbol table for runtime backtraces" ON)

# Include ReactOS standard directories
include_directories(BEFORE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/arch
    ${CMAKE_CURRENT_SOURCE_DIR}/include/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/include/uefi
    ${REACTOS_SOURCE_DIR}/libspng/spng
    ${REACTOS_SOURCE_DIR}/boot/environ/include
    ${REACTOS_SOURCE_DIR}/boot/environ/include/efi
    ${REACTOS_SOURCE_DIR}/ntoskrnl/include
    ${REACTOS_SOURCE_DIR}/sdk/lib/cmlib
    ${REACTOS_SOURCE_DIR}/sdk/include/reactos/libs
    ${REACTOS_SOURCE_DIR}/sdk/include/reactos/drivers
    ${REACTOS_SOURCE_DIR}/sdk/lib/drivers/arbiter)

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
    common/uefi/miscstubs.c
    common/uefi/uistubs.c
    common/uefi/uefiarc.c

    # UI helpers
    common/ui/display.c
    common/ui/draw.c
    common/ui/menu.c
    common/ui/page.c
    common/ui/page_log.c
    common/ui/page_static.c
    common/ui/pngui.c

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
    common/lib/bugcheck.c
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
    common/ntldr/cmboot.c
)

set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/common/arch/ntoskrnl_stubs.c
    PROPERTIES
        INCLUDE_DIRECTORIES "${REACTOS_SOURCE_DIR}/ntoskrnl/include"
)

# Architecture specific source files
if(ARCH STREQUAL "arm64")
    set(UEFINITY_ARCH_SOURCES
        arch/arm64/macharm64.c
        arch/arm64/platform.c
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
        arch/amd64/machamd64.c
        arch/amd64/context.c
        arch/amd64/platform.c
        arch/amd64/memory.c
        arch/amd64/uefitrap.c
        # NT Loader for AMD64
        common/ntldr/amd64/winldr.c
        arch/amd64/winload_support.c
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
target_compile_definitions(uefinity_lib PRIVATE SPNG_STATIC)

# Main bootloader entry source
set(UEFINITY_BASE_SOURCE
    include/arch/arch_interface.h
    common/uefi/uefildr.c
    common/boot/freeldr.c
    common/uefi/symlite.c
    # NOTE: include bootpool-backed malloc/free for libspng
    common/ui/uefi_malloc.c
    common/ui/uefi_stdio_stubs.c
)

# Create the main executable target
add_executable(uefinity ${UEFINITY_BASE_SOURCE})
set_target_properties(uefinity PROPERTIES SUFFIX ".efi")

target_compile_definitions(uefinity PRIVATE UEFIBOOT)

# Emit debug info when requested (both the app and the static lib)
if(UEFINITY_DEBUG_SYMBOLS)
    target_compile_options(uefinity PRIVATE -g3 -gdwarf-4)
    target_compile_options(uefinity_lib PRIVATE -g3 -gdwarf-4)
elseif(UEFINITY_EMBED_SYMBOLS)
    # Even without full debug, we need minimal symbols for backtraces
    target_compile_options(uefinity PRIVATE -g1)
    target_compile_options(uefinity_lib PRIVATE -g1)
endif()

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
    # Try to find an objcopy implementation
    if(NOT CMAKE_OBJCOPY)
        find_program(CMAKE_OBJCOPY NAMES ${CMAKE_OBJCOPY} objcopy x86_64-w64-mingw32-objcopy)
    endif()

    if(UEFINITY_DEBUG_SYMBOLS)
        # Keep symbols in the image, and also generate a separate .debug file
        if(CMAKE_OBJCOPY)
            add_custom_command(TARGET uefinity POST_BUILD
                # Extract full debug info to a separate file
                COMMAND ${CMAKE_OBJCOPY} --only-keep-debug $<TARGET_FILE:uefinity> $<TARGET_FILE:uefinity>.debug
                # Strip debug from the main EFI to keep it small for efisys.bin
                COMMAND ${CMAKE_OBJCOPY} --strip-debug $<TARGET_FILE:uefinity>
                # Link the stripped EFI with its external .debug file
                COMMAND ${CMAKE_OBJCOPY} --add-gnu-debuglink=$<TARGET_FILE:uefinity>.debug $<TARGET_FILE:uefinity>
                # Generate a lightweight symbol list for runtime backtraces using helper script
                COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/make_symlite.sh $<TARGET_FILE:uefinity> $<TARGET_FILE:uefinity>.symlite
                # Overwrite the placeholder .symlite section contents if present; otherwise add it
                COMMAND ${CMAKE_COMMAND} -E env LANG=C sh -c "${CMAKE_OBJCOPY} --update-section .symlite=$<TARGET_FILE:uefinity>.symlite $<TARGET_FILE:uefinity> || ${CMAKE_OBJCOPY} --add-section .symlite=$<TARGET_FILE:uefinity>.symlite --set-section-flags .symlite=contents,alloc,load,readonly,data $<TARGET_FILE:uefinity>"
            )
        else()
            message(WARNING "Uefinity: CMAKE_OBJCOPY not found; debug link file will not be generated.")
        endif()
    elseif(UEFINITY_EMBED_SYMBOLS)
        # Generate lightweight symbols for backtraces only
        if(CMAKE_OBJCOPY)
            add_custom_command(TARGET uefinity POST_BUILD
                # Generate a lightweight symbol list for runtime backtraces using helper script
                COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/make_symlite.sh $<TARGET_FILE:uefinity> $<TARGET_FILE:uefinity>.symlite
                # Overwrite the placeholder .symlite section contents if present; otherwise add it
                COMMAND ${CMAKE_COMMAND} -E env LANG=C sh -c "${CMAKE_OBJCOPY} --update-section .symlite=$<TARGET_FILE:uefinity>.symlite $<TARGET_FILE:uefinity> || ${CMAKE_OBJCOPY} --add-section .symlite=$<TARGET_FILE:uefinity>.symlite --set-section-flags .symlite=contents,alloc,load,readonly,data $<TARGET_FILE:uefinity>"
                # Strip remaining debug info to keep binary small
                COMMAND ${CMAKE_STRIP} --strip-debug $<TARGET_FILE:uefinity>
            )
        else()
            message(WARNING "Uefinity: CMAKE_OBJCOPY not found; symbols cannot be embedded.")
        endif()
    else()
        # No symbols at all - strip everything
        target_link_options(uefinity PRIVATE -Wl,--exclude-all-symbols,--file-alignment,0x200,--section-alignment,0x200)
        add_custom_command(TARGET uefinity
                        POST_BUILD
                        COMMAND ${CMAKE_STRIP} --remove-section=.rossym $<TARGET_FILE:uefinity>
                        COMMAND ${CMAKE_STRIP} --strip-all $<TARGET_FILE:uefinity>)
    endif()
endif()

# Ensure linker does not discard the placeholder .symlite section
target_link_options(uefinity PRIVATE -Wl,--no-gc-sections)

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
    target_link_libraries(uefinity uefinity_lib libcntpr blrtl libspng zlib)
elseif(ARCH STREQUAL "amd64")
    target_link_libraries(uefinity PRIVATE 
        -Wl,--start-group
        uefinity_lib
        cportlib
        blcmlib
        blrtl
        libcntpr
        libspng
        zlib
        -Wl,--end-group)
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

# Embed assets: generate C and header from any PNG files
# Prefer an "assets" directory; if missing, fall back to legacy "rs".
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets")
    set(UEFINITY_ASSET_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets)
    message(STATUS "Uefinity: using assets directory '${UEFINITY_ASSET_DIR}'")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/rs")
    set(UEFINITY_ASSET_DIR ${CMAKE_CURRENT_SOURCE_DIR}/rs)
    message(STATUS "Uefinity: using assets directory '${UEFINITY_ASSET_DIR}' (fallback)")
else()
    # Create a default assets directory so the generator still runs
    set(UEFINITY_ASSET_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets)
    file(MAKE_DIRECTORY ${UEFINITY_ASSET_DIR})
    message(STATUS "Uefinity: created empty assets dir '${UEFINITY_ASSET_DIR}'")
endif()
file(GLOB UEFINITY_PNG_ASSETS ${UEFINITY_ASSET_DIR}/*.png)
set(UEFINITY_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/ui)
file(MAKE_DIRECTORY ${UEFINITY_GENERATED_DIR})
set(UEFINITY_ASSETS_C ${UEFINITY_GENERATED_DIR}/assets.c)
set(UEFINITY_ASSETS_H ${UEFINITY_GENERATED_DIR}/assets.h)

add_custom_command(
    OUTPUT ${UEFINITY_ASSETS_C}
    BYPRODUCTS ${UEFINITY_ASSETS_H}
    COMMAND ${CMAKE_COMMAND} -E env PYTHONUNBUFFERED=1
            /usr/bin/python3 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/embed_assets.py
            --asset-dir ${UEFINITY_ASSET_DIR}
            --out-c ${UEFINITY_ASSETS_C}
            --out-h ${UEFINITY_ASSETS_H}
            --png2bgra ${REACTOS_BINARY_DIR}/host-tools/bin/png2bgra
    DEPENDS ${UEFINITY_PNG_ASSETS} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/embed_assets.py
    COMMENT "Embedding PNG assets into C"
)

add_custom_target(uefinity_assets ALL DEPENDS ${UEFINITY_ASSETS_C} ${UEFINITY_ASSETS_H})
add_dependencies(uefinity_lib uefinity_assets)
target_sources(uefinity_lib PRIVATE ${UEFINITY_ASSETS_C})
target_include_directories(uefinity_lib PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)

message(STATUS "Uefinity: Created target 'uefinity' as UEFI bootloader for ${ARCH}")
