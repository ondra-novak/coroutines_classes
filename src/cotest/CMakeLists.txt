cmake_minimum_required(VERSION 3.2) 

set(COCLS_DEFINE_SET_CORO_NAME OFF CACHE BOOL "Enable COCLS_SET_CORO_NAME in Release (for cotest)")
if (COCLS_DEFINE_SET_CORO_NAME) 
    add_compile_definitions(COCLS_DEFINE_SET_CORO_NAME)
endif()

add_executable (cotest  main.cpp)
