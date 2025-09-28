
PROJECT(NTOS)

if (CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
    # Enable this again. CORE-17637
    add_compile_options(-Wunused-result)
endif()

include(ntos.cmake)

set(NTOSKRNL_SOURCE ${SOURCE})
set(NTOSKRNL_ASM_SOURCE ${ASM_SOURCE})

set(NTKRNLMP_SOURCE ${SOURCE})
set(NTKRNLMP_ASM_SOURCE ${ASM_SOURCE})

spec2def(ntoskrnl.exe ntoskrnl.spec ADD_IMPORTLIB)

# Embed RTC libs
if (STACK_PROTECTOR)
    target_sources(libntoskrnl PRIVATE $<TARGET_OBJECTS:gcc_ssp_nt>)
endif()

add_asm_files(ntoskrnl_asm ${NTOSKRNL_ASM_SOURCE})

if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    # Clang optimises strcmp calls to memcmp.
    target_sources(libntoskrnl PRIVATE $<TARGET_OBJECTS:memcmp>)
endif()

if((CMAKE_C_COMPILER_ID STREQUAL "Clang") OR (CMAKE_C_COMPILER_ID STREQUAL "GNU"))
    # Enable xsave feature
    add_compile_options(-mxsave)
endif()

list(APPEND PCH_SKIP_SOURCE
    guid.c)

add_executable(ntoskrnl
    ${ntoskrnl_asm}
    ${NTOSKRNL_SOURCE}
    ${PCH_SKIP_SOURCE}
    ntoskrnl.rc
    ${CMAKE_CURRENT_BINARY_DIR}/ntoskrnl.def)
set_module_type(ntoskrnl kernel)

# Stricter warnings for all builds (GCC/Clang only)
if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
    target_compile_options(ntoskrnl PRIVATE
        -UNDEBUG
        -Wno-static-in-inline
        $<$<COMPILE_LANGUAGE:C>:
            -Werror
            -Wextra -Wformat -Wformat-security
            -Wcast-align -Wpointer-arith -Wvla
            
            -Wno-missing-prototypes -Wno-unused-parameter -Wno-sign-compare
            -Wno-missing-field-initializers -Wno-cast-function-type
            -Wno-implicit-fallthrough -Wno-null-dereference -Wno-format-nonliteral
            -Wno-enum-conversion -Wno-absolute-value>
        $<$<COMPILE_LANGUAGE:CXX>:
            -Werror
            -Wextra -Wformat -Wformat-security -Wpointer-arith
            -Wcast-align -Wno-unused-parameter
            -Wno-sign-compare -Wno-missing-field-initializers
            -Wno-cast-function-type -Wno-implicit-fallthrough
            -Wno-null-dereference>)
endif()

source_group(TREE ${REACTOS_SOURCE_DIR}/ntoskrnl PREFIX "Source Files" FILES ${NTOSKRNL_SOURCE})

target_link_libraries(ntoskrnl cportlib csq ${PSEH_LIB} arbiter cmlib ntlsalib rtl ${ROSSYM_LIB} libcntpr wdmguid poguid ioevent)

if(STACK_PROTECTOR)
    target_sources(ntoskrnl PRIVATE $<TARGET_OBJECTS:gcc_ssp_nt>)
endif()

add_importlibs(ntoskrnl hal kdcom bootvid)
add_pch(ntoskrnl ${REACTOS_SOURCE_DIR}/ntoskrnl/include/ntoskrnl.h "${PCH_SKIP_SOURCE}")
add_dependencies(ntoskrnl psdk asm)
add_cd_file(TARGET ntoskrnl DESTINATION reactos/system32 NO_CAB FOR all)

add_subdirectory(ntkrnlmp)

# Apply the same warnings to the SMP kernel variant
if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
    target_compile_options(ntkrnlmp PRIVATE
        -UNDEBUG
        -Wno-static-in-inline
        $<$<COMPILE_LANGUAGE:C>:
            -Werror
            -Wextra -Wformat -Wformat-security
            -Wcast-align -Wpointer-arith -Wvla
            
            -Wno-missing-prototypes -Wno-unused-parameter -Wno-sign-compare
            -Wno-missing-field-initializers -Wno-cast-function-type
            -Wno-implicit-fallthrough -Wno-null-dereference -Wno-format-nonliteral
            -Wno-enum-conversion -Wno-absolute-value>
        $<$<COMPILE_LANGUAGE:CXX>:
            -Werror
            -Wextra -Wformat -Wformat-security -Wpointer-arith
            -Wcast-align -Wno-unused-parameter
            -Wno-sign-compare -Wno-missing-field-initializers
            -Wno-cast-function-type -Wno-implicit-fallthrough
            -Wno-null-dereference>)
endif()

# Enable stricter warnings per-subtree (all builds, GCC/Clang)
if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
    file(GLOB ARM3_WARN_SOURCES
        "${REACTOS_SOURCE_DIR}/ntoskrnl/mm/ARM3/*.c"
        "${REACTOS_SOURCE_DIR}/ntoskrnl/mm/ARM3/*.cpp")
    foreach(_src ${ARM3_WARN_SOURCES})
        set_source_files_properties(${_src} PROPERTIES COMPILE_OPTIONS "-Wextra")
    endforeach()
    # Also tighten for core section manager implementation
    set_source_files_properties(${REACTOS_SOURCE_DIR}/ntoskrnl/mm/section.c PROPERTIES COMPILE_OPTIONS "-Wextra")
    # Tighten fsrtl subtree
    file(GLOB FSRTL_WARN_SOURCES "${REACTOS_SOURCE_DIR}/ntoskrnl/fsrtl/*.c")
    foreach(_src ${FSRTL_WARN_SOURCES})
        set_source_files_properties(${_src} PROPERTIES COMPILE_OPTIONS "-Wextra;-Wsign-compare;-Wimplicit-fallthrough")
    endforeach()
    # Tighten io manager subtree
    file(GLOB IO_WARN_SOURCES
        "${REACTOS_SOURCE_DIR}/ntoskrnl/io/iomgr/*.c")
    foreach(_src ${IO_WARN_SOURCES})
        set_source_files_properties(${_src} PROPERTIES COMPILE_OPTIONS "-Wextra;-Wsign-compare;-Wimplicit-fallthrough")
    endforeach()
    # Tighten ex subsystem
    file(GLOB EX_WARN_SOURCES "${REACTOS_SOURCE_DIR}/ntoskrnl/ex/*.c")
    foreach(_src ${EX_WARN_SOURCES})
        set_source_files_properties(${_src} PROPERTIES COMPILE_OPTIONS "-Wextra;-Wsign-compare;-Wimplicit-fallthrough")
    endforeach()
    # Tighten dbgk subsystem
    file(GLOB DBGK_WARN_SOURCES "${REACTOS_SOURCE_DIR}/ntoskrnl/dbgk/*.c")
    foreach(_src ${DBGK_WARN_SOURCES})
        set_source_files_properties(${_src} PROPERTIES COMPILE_OPTIONS "-Wextra;-Wsign-compare;-Wimplicit-fallthrough")
    endforeach()
    # Tighten ps subsystem
    file(GLOB PS_WARN_SOURCES "${REACTOS_SOURCE_DIR}/ntoskrnl/ps/*.c")
    foreach(_src ${PS_WARN_SOURCES})
        set_source_files_properties(${_src} PROPERTIES COMPILE_OPTIONS "-Wextra;-Wsign-compare;-Wimplicit-fallthrough")
    endforeach()
    # ps/state.c uses PSEH; suppress sign-compare noise arising from macro expansions
    set_source_files_properties(${REACTOS_SOURCE_DIR}/ntoskrnl/ps/state.c PROPERTIES COMPILE_OPTIONS "-Wextra;-Wno-sign-compare;-Wimplicit-fallthrough")
    # Tighten remaining mm (non-ARM3) sources in ntoskrnl/mm
    file(GLOB MM_WARN_SOURCES
        "${REACTOS_SOURCE_DIR}/ntoskrnl/mm/*.c")
    foreach(_src ${MM_WARN_SOURCES})
        # Skip section.c which is already tightened above
        if (NOT _src MATCHES "/mm/section.c$")
            set_source_files_properties(${_src} PROPERTIES COMPILE_OPTIONS "-Wextra;-Wsign-compare;-Wimplicit-fallthrough")
        endif()
    endforeach()
    # Tighten ke (kernel executive) subsystem
    file(GLOB KE_WARN_SOURCES "${REACTOS_SOURCE_DIR}/ntoskrnl/ke/*.c")
    foreach(_src ${KE_WARN_SOURCES})
        set_source_files_properties(${_src} PROPERTIES COMPILE_OPTIONS "-Wextra;-Wsign-compare;-Wimplicit-fallthrough")
    endforeach()
    # Tighten ob (object manager) subsystem
    file(GLOB OB_WARN_SOURCES "${REACTOS_SOURCE_DIR}/ntoskrnl/ob/*.c")
    foreach(_src ${OB_WARN_SOURCES})
        set_source_files_properties(${_src} PROPERTIES COMPILE_OPTIONS "-Wextra;-Wsign-compare;-Wimplicit-fallthrough")
    endforeach()
    # ob/obwait.c uses PSEH; suppress sign-compare noise arising from macro expansions
    set_source_files_properties(${REACTOS_SOURCE_DIR}/ntoskrnl/ob/obwait.c PROPERTIES COMPILE_OPTIONS "-Wextra;-Wno-sign-compare;-Wimplicit-fallthrough")
    # Tighten se (security) subsystem
    file(GLOB SE_WARN_SOURCES "${REACTOS_SOURCE_DIR}/ntoskrnl/se/*.c")
    foreach(_src ${SE_WARN_SOURCES})
        set_source_files_properties(${_src} PROPERTIES COMPILE_OPTIONS "-Wextra;-Wsign-compare;-Wimplicit-fallthrough")
    endforeach()
    # Tighten cm (configuration manager) subsystem
    file(GLOB CM_WARN_SOURCES "${REACTOS_SOURCE_DIR}/ntoskrnl/config/*.c")
    foreach(_src ${CM_WARN_SOURCES})
        set_source_files_properties(${_src} PROPERTIES COMPILE_OPTIONS "-Wextra;-Wsign-compare;-Wimplicit-fallthrough")
    endforeach()
    # Tighten po (power) subsystem
    file(GLOB PO_WARN_SOURCES "${REACTOS_SOURCE_DIR}/ntoskrnl/po/*.c")
    foreach(_src ${PO_WARN_SOURCES})
        set_source_files_properties(${_src} PROPERTIES COMPILE_OPTIONS "-Wextra;-Wsign-compare;-Wimplicit-fallthrough")
    endforeach()
    # Tighten lpc (local procedure call) subsystem
    file(GLOB LPC_WARN_SOURCES "${REACTOS_SOURCE_DIR}/ntoskrnl/lpc/*.c")
    foreach(_src ${LPC_WARN_SOURCES})
        set_source_files_properties(${_src} PROPERTIES COMPILE_OPTIONS "-Wextra;-Wsign-compare;-Wimplicit-fallthrough")
    endforeach()
    # Tighten kd (kernel debugger) subsystem
    file(GLOB KD_WARN_SOURCES "${REACTOS_SOURCE_DIR}/ntoskrnl/kd*.c" "${REACTOS_SOURCE_DIR}/ntoskrnl/kd64/*.c" "${REACTOS_SOURCE_DIR}/ntoskrnl/kd64/*/*.c")
    foreach(_src ${KD_WARN_SOURCES})
        set_source_files_properties(${_src} PROPERTIES COMPILE_OPTIONS "-Wextra;-Wsign-compare;-Wimplicit-fallthrough")
    endforeach()
    # Tighten cc (cache manager) subsystem
    file(GLOB CC_WARN_SOURCES "${REACTOS_SOURCE_DIR}/ntoskrnl/cc/*.c")
    foreach(_src ${CC_WARN_SOURCES})
        set_source_files_properties(${_src} PROPERTIES COMPILE_OPTIONS "-Wextra;-Wsign-compare;-Wimplicit-fallthrough")
    endforeach()
endif()

add_asm_files(ntdllsys_asm ntdll.S)
add_library(ntdllsys ${ntdllsys_asm})
set_target_properties(ntdllsys PROPERTIES LINKER_LANGUAGE "C")
add_dependencies(ntdllsys asm)
