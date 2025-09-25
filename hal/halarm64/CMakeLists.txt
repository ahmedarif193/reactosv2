spec2def(hal.dll ../hal.spec ADD_IMPORTLIB)

add_definitions(
    -D_NTHALDLL_
    -D_NTHAL_)

include_directories(
    ${REACTOS_SOURCE_DIR}/ntoskrnl/include
    include)

list(APPEND HAL_SOURCE
    halarm64.c
    portio.c
    stubs.c
    acpi/halacpi.c
    generic/halinit.c
    generic/cache.c
    generic/interrupt.c
    generic/timer.c
    generic/dma.c
    generic/iommu.c
    generic/power.c
    generic/processor.c
    generic/sysinfo.c
    gic/gic.c)

add_asm_files(hal_asm)

add_library(hal MODULE
    ${HAL_SOURCE}
    ${hal_asm}
    hal.rc
    ${CMAKE_CURRENT_BINARY_DIR}/hal.def
    ${CMAKE_CURRENT_BINARY_DIR}/hal_stubs.c)

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
