
if(ARCH STREQUAL "arm64")
    add_library(cportlib cport_stub.c)
elseif(SARCH STREQUAL "pc98")
    add_library(cportlib cport_pc98.c)
else()
    add_library(cportlib cport.c)
endif()
add_dependencies(cportlib xdk)
