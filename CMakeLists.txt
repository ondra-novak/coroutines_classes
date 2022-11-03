cmake_minimum_required(VERSION 3.2)
project (cotest)

include(library.cmake)

set (CMAKE_CXX_STANDARD 20)
LINK_LIBRARIES(pthread)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/)
add_compile_options(-Wall -Wno-noexcept-type)

add_subdirectory (src/examples)
add_subdirectory (src/cotest)


