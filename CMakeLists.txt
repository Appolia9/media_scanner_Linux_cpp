cmake_minimum_required(VERSION 3.16)
project(media_scanner_Linux VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Опции сборки
option(BUILD_TESTS "Build tests" OFF)
option(BUILD_DEBUG_INFO "Build with debug symbols" OFF)

# Предупреждения
add_compile_options(-Wall -Wextra -Wpedantic)
add_compile_options(-Wshadow -Wconversion -Wsign-conversion)

if(BUILD_DEBUG_INFO)
    add_compile_options(-g -O0 -DDEBUG)
else()
    add_compile_options(-O2 -DNDEBUG)
endif()

# Определение платформы
if(UNIX AND NOT APPLE)
    add_compile_definitions(_GNU_SOURCE)
endif()

# Release по умолчанию
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_executable(media_scanner_Linux src/main.cpp)

# std::filesystem требует линковки с libstdc++fs на GCC < 9
target_link_libraries(media_scanner_Linux PRIVATE
    $<$<AND:$<CXX_COMPILER_ID:GNU>,$<VERSION_LESS:$<CXX_COMPILER_VERSION>,9.0>>:stdc++fs>
    pthread
)

install(TARGETS media_scanner_Linux DESTINATION bin)

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "/usr/local" CACHE PATH "Install path" FORCE)
endif()
