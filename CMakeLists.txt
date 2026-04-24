cmake_minimum_required(VERSION 3.16)
project(granda VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_DEBUG   "-g -O0 -Wall -Wextra -Wpedantic")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_C_FLAGS_DEBUG     "-g -O0 -Wall")
set(CMAKE_C_FLAGS_RELEASE   "-O2 -DNDEBUG")

add_executable(granda
    src/main.cpp
    src/lexer.cpp
    src/parser.cpp
    src/type_checker.cpp
    src/codegen.cpp
)

target_include_directories(granda PRIVATE src)

# Copy runtime files next to the compiler so it can find them at compile time
add_custom_command(TARGET granda POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_SOURCE_DIR}/runtime/granda_rt.h
        ${CMAKE_SOURCE_DIR}/runtime/granda_rt.c
        $<TARGET_FILE_DIR:granda>
    COMMENT "Copying Granda runtime to output directory"
)

install(TARGETS granda DESTINATION bin)
install(FILES runtime/granda_rt.h runtime/granda_rt.c
        DESTINATION share/granda/runtime)
