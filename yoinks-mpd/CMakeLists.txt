cmake_minimum_required(VERSION 3.16)
project(yoinks_mpd_core VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(yoinks_mpd_core
    src/bounded_stream_buffer.cpp
    src/http_server.cpp
)
target_include_directories(yoinks_mpd_core PUBLIC include)
target_compile_options(yoinks_mpd_core PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>
)
find_package(Threads REQUIRED)
find_package(Boost 1.74 REQUIRED COMPONENTS system)
find_package(nlohmann_json 3.2.0 REQUIRED)
target_link_libraries(yoinks_mpd_core PUBLIC
    Threads::Threads
    Boost::system
    nlohmann_json::nlohmann_json
)

add_executable(yoinks-mpd src/main.cpp)
target_link_libraries(yoinks-mpd PRIVATE yoinks_mpd_core)
target_compile_options(yoinks-mpd PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>
)

option(YOINKS_MPD_BUILD_TESTS "Build Yoinks-MPD core tests" ON)
if(YOINKS_MPD_BUILD_TESTS)
    enable_testing()
    add_executable(yoinks_mpd_core_tests tests/bounded_stream_buffer_test.cpp)
    target_link_libraries(yoinks_mpd_core_tests PRIVATE yoinks_mpd_core)
    target_compile_options(yoinks_mpd_core_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>
    )
    add_test(NAME bounded_stream_buffer COMMAND yoinks_mpd_core_tests)
    add_test(NAME http_api_integration
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/http_api_test.sh $<TARGET_FILE:yoinks-mpd>)
endif()
