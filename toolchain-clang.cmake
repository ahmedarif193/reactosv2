
if(DEFINED ENV{_ROSBE_ROSSCRIPTDIR})
    set(CMAKE_SYSROOT $ENV{_ROSBE_ROSSCRIPTDIR}/$ENV{ROS_ARCH})
endif()

# pass variables necessary for the toolchain (needed for try_compile)
set(CMAKE_TRY_COMPILE_PLATFORM_VARIABLES ARCH CLANG_VERSION)

# The name of the target operating system
set(CMAKE_SYSTEM_NAME Windows)
# The processor we are targeting
if (ARCH STREQUAL "i386")
    set(CMAKE_SYSTEM_PROCESSOR i686)
elseif (ARCH STREQUAL "amd64")
    set(CMAKE_SYSTEM_PROCESSOR x86_64)
elseif(ARCH STREQUAL "arm")
    set(CMAKE_SYSTEM_PROCESSOR arm)
elseif(ARCH STREQUAL "arm64")
    set(CMAKE_SYSTEM_PROCESSOR aarch64)
else()
    message(FATAL_ERROR "Unsupported ARCH: ${ARCH}")
endif()

if (DEFINED CLANG_VERSION)
    set(CLANG_SUFFIX "-${CLANG_VERSION}")
else()
    set(CLANG_SUFFIX "")
endif()

# Which tools to use
set(triplet ${CMAKE_SYSTEM_PROCESSOR}-w64-mingw32)
if (CMAKE_HOST_WIN32)
    set(GCC_TOOLCHAIN_PREFIX "")
else()
    set(GCC_TOOLCHAIN_PREFIX "${triplet}-")
endif()

set(_toolchain_prefix_path "")
if(DEFINED TOOLCHAIN_PATH AND NOT TOOLCHAIN_PATH STREQUAL "")
    set(_candidate "${TOOLCHAIN_PATH}/${GCC_TOOLCHAIN_PREFIX}gcc")
    if(EXISTS "${_candidate}")
        set(_toolchain_prefix_path "${TOOLCHAIN_PATH}/")
    endif()
endif()

if(DEFINED TOOLCHAIN_PATH AND NOT TOOLCHAIN_PATH STREQUAL "")
    # Ensure toolchain binaries are found when invoking helper tools
    list(PREPEND CMAKE_PROGRAM_PATH "${TOOLCHAIN_PATH}")
    if(DEFINED ENV{PATH})
        set(ENV{PATH} "${TOOLCHAIN_PATH}:$ENV{PATH}")
    else()
        set(ENV{PATH} "${TOOLCHAIN_PATH}")
    endif()

    get_filename_component(_toolchain_root "${TOOLCHAIN_PATH}" DIRECTORY)

    set(_toolchain_lib_dirs
        "${_toolchain_root}/lib"
        "${_toolchain_root}/${triplet}/lib"
        "${_toolchain_root}/${triplet}/sysroot/lib"
        "${_toolchain_root}/${triplet}/sysroot/usr/lib"
        "${_toolchain_root}/${triplet}/sysroot/usr/${triplet}/lib")
    set(_toolchain_include_dirs
        "${_toolchain_root}/include"
        "${_toolchain_root}/${triplet}/include"
        "${_toolchain_root}/${triplet}/sysroot/usr/include"
        "${_toolchain_root}/${triplet}/sysroot/usr/${triplet}/include")

    # Ask the GCC toolchain for the libgcc location so we can reuse its directory.
    if(EXISTS "${_toolchain_prefix_path}${GCC_TOOLCHAIN_PREFIX}gcc")
        execute_process(
            COMMAND "${_toolchain_prefix_path}${GCC_TOOLCHAIN_PREFIX}gcc" -print-libgcc-file-name
            OUTPUT_VARIABLE _libgcc_path
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        if(_libgcc_path)
            get_filename_component(_libgcc_dir "${_libgcc_path}" DIRECTORY)
            get_filename_component(_gcc_version "${_libgcc_dir}" NAME)
            list(APPEND _toolchain_lib_dirs "${_libgcc_dir}")
        endif()
    endif()

    foreach(_dir IN LISTS _toolchain_lib_dirs)
        if(EXISTS "${_dir}")
            list(PREPEND CMAKE_LIBRARY_PATH "${_dir}")
            if(DEFINED ENV{LIBRARY_PATH} AND NOT ENV{LIBRARY_PATH} STREQUAL "")
                set(ENV{LIBRARY_PATH} "${_dir}:$ENV{LIBRARY_PATH}")
            else()
                set(ENV{LIBRARY_PATH} "${_dir}")
            endif()
        endif()
    endforeach()

    foreach(_idir IN LISTS _toolchain_include_dirs)
        if(EXISTS "${_idir}")
            list(APPEND CMAKE_C_STANDARD_INCLUDE_DIRECTORIES "${_idir}")
            list(APPEND CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES "${_idir}")
            if(DEFINED ENV{CPATH} AND NOT ENV{CPATH} STREQUAL "")
                set(ENV{CPATH} "${_idir}:$ENV{CPATH}")
            else()
                set(ENV{CPATH} "${_idir}")
            endif()
        endif()
    endforeach()

    if(_gcc_version)
        set(_cxx_include_base "${_toolchain_root}/${triplet}/include/c++/${_gcc_version}")
        if(EXISTS "${_cxx_include_base}")
            set(_cxx_include_dirs
                "${_cxx_include_base}"
                "${_cxx_include_base}/${triplet}"
                "${_cxx_include_base}/backward")

            foreach(_idir IN LISTS _cxx_include_dirs)
                if(EXISTS "${_idir}")
                    list(APPEND CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES "${_idir}")
                    if(DEFINED ENV{CPLUS_INCLUDE_PATH} AND NOT ENV{CPLUS_INCLUDE_PATH} STREQUAL "")
                        set(ENV{CPLUS_INCLUDE_PATH} "${_idir}:$ENV{CPLUS_INCLUDE_PATH}")
                    else()
                        set(ENV{CPLUS_INCLUDE_PATH} "${_idir}")
                    endif()
                endif()
            endforeach()
        endif()
    endif()
endif()

set(CMAKE_C_COMPILER clang${CLANG_SUFFIX})
set(CMAKE_C_COMPILER_TARGET ${triplet})
set(CMAKE_CXX_COMPILER clang++${CLANG_SUFFIX})
set(CMAKE_CXX_COMPILER_TARGET ${triplet})
set(CMAKE_ASM_COMPILER "${_toolchain_prefix_path}${GCC_TOOLCHAIN_PREFIX}gcc")
set(CMAKE_ASM_COMPILER_ID GNU)
set(CMAKE_MC_COMPILER "${_toolchain_prefix_path}${GCC_TOOLCHAIN_PREFIX}windmc")
set(CMAKE_RC_COMPILER "${_toolchain_prefix_path}${GCC_TOOLCHAIN_PREFIX}windres")
set(CMAKE_DLLTOOL "${_toolchain_prefix_path}${GCC_TOOLCHAIN_PREFIX}dlltool")
set(CMAKE_AR "${_toolchain_prefix_path}${GCC_TOOLCHAIN_PREFIX}ar")
set(CMAKE_RANLIB "${_toolchain_prefix_path}${GCC_TOOLCHAIN_PREFIX}ranlib")

add_compile_options(-fno-builtin)

# This allows to have CMake test the compiler without linking
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_CREATE_STATIC_LIBRARY "<CMAKE_AR> crs <TARGET> <LINK_FLAGS> <OBJECTS>")
set(CMAKE_CXX_CREATE_STATIC_LIBRARY ${CMAKE_C_CREATE_STATIC_LIBRARY})
set(CMAKE_ASM_CREATE_STATIC_LIBRARY ${CMAKE_C_CREATE_STATIC_LIBRARY})

set(CMAKE_C_STANDARD_LIBRARIES "-lmingwex -lgcc" CACHE STRING "Standard C Libraries")
set(CMAKE_CXX_STANDARD_LIBRARIES "-lmingwex -lgcc" CACHE STRING "Standard C++ Libraries")

find_program (LD_EXECUTABLE ${GCC_TOOLCHAIN_PREFIX}ld)
message(STATUS "Using linker ${LD_EXECUTABLE}")

set(CMAKE_SHARED_LINKER_FLAGS_INIT "-nostdlib -Wl,--enable-auto-image-base,--disable-auto-import -fuse-ld=${LD_EXECUTABLE}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "-nostdlib -Wl,--enable-auto-image-base,--disable-auto-import -fuse-ld=${LD_EXECUTABLE}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-nostdlib -Wl,--enable-auto-image-base,--disable-auto-import -fuse-ld=${LD_EXECUTABLE}")

foreach(_dir IN LISTS _toolchain_lib_dirs)
    if(EXISTS "${_dir}")
        set(CMAKE_SHARED_LINKER_FLAGS_INIT "${CMAKE_SHARED_LINKER_FLAGS_INIT} -L${_dir}")
        set(CMAKE_MODULE_LINKER_FLAGS_INIT "${CMAKE_MODULE_LINKER_FLAGS_INIT} -L${_dir}")
        set(CMAKE_EXE_LINKER_FLAGS_INIT "${CMAKE_EXE_LINKER_FLAGS_INIT} -L${_dir}")
    endif()
endforeach()

set(CMAKE_USER_MAKE_RULES_OVERRIDE "${CMAKE_CURRENT_LIST_DIR}/overrides-gcc.cmake")
