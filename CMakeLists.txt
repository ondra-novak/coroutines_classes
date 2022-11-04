cmake_minimum_required(VERSION 3.2)
project (cotest)

include(library.cmake)

set (CMAKE_CXX_STANDARD 20)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/)
if(MSVC)
add_compile_options(/EHsc /DNOMINMAX)
else()
add_compile_options(-Wall -Wno-noexcept-type)
LINK_LIBRARIES(pthread)
endif()

add_subdirectory (src/examples)
add_subdirectory (src/cotest)


