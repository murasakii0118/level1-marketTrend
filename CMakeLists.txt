cmake_minimum_required(VERSION 3.16)
project(level1-marketTrend CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(marketTrend
    src/main.cpp
    src/ws.cpp
    src/snappy.cpp
    src/json.cpp
    src/config.cpp
    src/session.cpp
    src/store.cpp
)

target_compile_definitions(marketTrend PRIVATE
    _WIN32_WINNT=0x0A00
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(marketTrend PRIVATE -O2 -Wall -Wextra -Wno-unused-parameter)
endif()

target_link_libraries(marketTrend PRIVATE winhttp)

set_target_properties(marketTrend PROPERTIES
    OUTPUT_NAME "marketTrend"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# 把 settings.json 复制到可执行文件旁边
add_custom_command(TARGET marketTrend POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/settings.json"
            "$<TARGET_FILE_DIR:marketTrend>/settings.json"
)
