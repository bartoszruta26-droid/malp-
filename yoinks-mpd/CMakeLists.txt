cmake_minimum_required(VERSION 3.16)
project(yoinks_mpd_core VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(yoinks_mpd_core
    src/bounded_stream_buffer.cpp
)
target_include_directories(yoinks_mpd_core PUBLIC include)
target_compile_options(yoinks_mpd_core PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>
)
find_package(Threads REQUIRED)
target_link_libraries(yoinks_mpd_core PUBLIC Threads::Threads)

option(YOINKS_MPD_BUILD_TESTS "Build Yoinks-MPD core tests" ON)
if(YOINKS_MPD_BUILD_TESTS)
    enable_testing()
    add_executable(yoinks_mpd_core_tests tests/bounded_stream_buffer_test.cpp)
    target_link_libraries(yoinks_mpd_core_tests PRIVATE yoinks_mpd_core)
    target_compile_options(yoinks_mpd_core_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>
    )
    add_test(NAME bounded_stream_buffer COMMAND yoinks_mpd_core_tests)
endif()
