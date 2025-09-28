
add_library(comsupp comsupp.cpp)
target_link_libraries(comsupp PRIVATE cppstl)
set_target_cpp_properties(comsupp WITH_EXCEPTIONS)

add_dependencies(comsupp psdk)

# Suppress noisy C++ class conversion warnings from comutil headers in Debug
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(comsupp PRIVATE -Wno-class-conversion)
endif()
