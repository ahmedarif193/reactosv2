spec2def(hal.dll ../hal.spec ADD_IMPORTLIB)

add_definitions(
    -D_NTHALDLL_
    -D_NTHAL_)

include_directories(
    ${REACTOS_SOURCE_DIR}/ntoskrnl/include)

list(APPEND HAL_SOURCE
    halarm64.c
    portio.c)

add_asm_files(hal_asm)

add_library(hal MODULE
    ${HAL_SOURCE}
    ${hal_asm}
    hal.rc)

target_link_libraries(hal libcntpr)

if(NOT MSVC)
    # No special link options needed on GCC/MinGW
else()
    target_link_options(hal PRIVATE /WHOLEARCHIVE:libcntpr)
endif()

add_pch(hal ${REACTOS_SOURCE_DIR}/ntoskrnl/include/internal/ntoskrnl.h HAL_SOURCE)
add_dependencies(hal psdk bugcodes asm)

set_module_type(hal kerneldll ENTRYPOINT HalInitSystem)
add_importlibs(hal ntoskrnl)
add_cd_file(TARGET hal DESTINATION reactos/system32 NO_CAB FOR all)
