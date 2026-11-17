cmake_minimum_required(VERSION 3.16)
project(yoinks_mpd_core VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
find_package(CURL REQUIRED)
find_package(Boost 1.74 REQUIRED)
find_package(nlohmann_json 3.2.0 REQUIRED)

add_library(yoinks_mpd_core
    src/bounded_stream_buffer.cpp
    src/http_server.cpp
    src/http_upstream_stream.cpp
    src/lazy_stream_session_registry.cpp
    src/mpd_queue_manager.cpp
    src/stream_session_registry.cpp
    src/yt_dlp_resolver.cpp
    src/youtube_queue_service.cpp
)
target_include_directories(yoinks_mpd_core PUBLIC include ${Boost_INCLUDE_DIRS})
target_compile_definitions(yoinks_mpd_core PUBLIC BOOST_ERROR_CODE_HEADER_ONLY)
target_compile_options(yoinks_mpd_core PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>
)
target_link_libraries(yoinks_mpd_core PUBLIC
    Threads::Threads
    nlohmann_json::nlohmann_json
    CURL::libcurl
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
    add_executable(yoinks_mpd_upstream_tests tests/http_upstream_stream_test.cpp)
    target_link_libraries(yoinks_mpd_upstream_tests PRIVATE yoinks_mpd_core)
    target_compile_options(yoinks_mpd_upstream_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>
    )
    add_test(NAME http_upstream_validation COMMAND yoinks_mpd_upstream_tests)
    add_executable(yoinks_mpd_session_registry_tests tests/stream_session_registry_test.cpp)
    target_link_libraries(yoinks_mpd_session_registry_tests PRIVATE yoinks_mpd_core)
    target_compile_options(yoinks_mpd_session_registry_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>
    )
    add_test(NAME stream_session_registry COMMAND yoinks_mpd_session_registry_tests)
    add_executable(yoinks_mpd_chunked_stream_tests tests/chunked_stream_writer_test.cpp)
    target_link_libraries(yoinks_mpd_chunked_stream_tests PRIVATE yoinks_mpd_core)
    target_compile_options(yoinks_mpd_chunked_stream_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>
    )
    add_test(NAME chunked_stream_writer COMMAND yoinks_mpd_chunked_stream_tests)
    add_executable(yoinks_mpd_ytdlp_resolver_tests tests/yt_dlp_resolver_test.cpp)
    target_link_libraries(yoinks_mpd_ytdlp_resolver_tests PRIVATE yoinks_mpd_core)
    target_compile_options(yoinks_mpd_ytdlp_resolver_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>
    )
    add_test(NAME yt_dlp_resolver COMMAND yoinks_mpd_ytdlp_resolver_tests)
    add_executable(yoinks_mpd_queue_manager_tests tests/mpd_queue_manager_test.cpp)
    target_link_libraries(yoinks_mpd_queue_manager_tests PRIVATE yoinks_mpd_core)
    target_compile_options(yoinks_mpd_queue_manager_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>
    )
    add_test(NAME mpd_queue_manager COMMAND yoinks_mpd_queue_manager_tests)
    add_executable(yoinks_mpd_lazy_session_tests tests/lazy_stream_session_registry_test.cpp)
    target_link_libraries(yoinks_mpd_lazy_session_tests PRIVATE yoinks_mpd_core)
    target_compile_options(yoinks_mpd_lazy_session_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>
    )
    add_test(NAME lazy_stream_session_registry COMMAND yoinks_mpd_lazy_session_tests)
    add_executable(yoinks_mpd_youtube_queue_service_tests tests/youtube_queue_service_test.cpp)
    target_link_libraries(yoinks_mpd_youtube_queue_service_tests PRIVATE yoinks_mpd_core)
    target_compile_options(yoinks_mpd_youtube_queue_service_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>
    )
    add_test(NAME youtube_queue_service COMMAND yoinks_mpd_youtube_queue_service_tests)
    add_test(NAME http_api_integration
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/http_api_test.sh $<TARGET_FILE:yoinks-mpd>)
endif()
