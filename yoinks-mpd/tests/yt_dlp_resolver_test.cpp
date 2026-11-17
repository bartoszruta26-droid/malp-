/*
 * Copyright (C) 2026 MALP contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "yoinks/yt_dlp_resolver.hpp"

#include <cerrno>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

class TemporaryExecutable final {
public:
    explicit TemporaryExecutable(const std::string& script) {
        char path[] = "/tmp/yoinks-ytdlp-test-XXXXXX";
        const int fd = mkstemp(path);
        if (fd < 0) throw std::runtime_error("mkstemp failed");
        path_ = path;
        std::size_t offset = 0;
        while (offset < script.size()) {
            const ssize_t count = write(fd, script.data() + offset, script.size() - offset);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) { close(fd); unlink(path_.c_str()); throw std::runtime_error("write script failed"); }
            offset += static_cast<std::size_t>(count);
        }
        if (close(fd) != 0 || chmod(path_.c_str(), 0700) != 0) {
            unlink(path_.c_str());
            throw std::runtime_error("prepare script failed");
        }
    }
    ~TemporaryExecutable() { if (!path_.empty()) unlink(path_.c_str()); }
    const std::string& path() const { return path_; }
private:
    std::string path_;
};

void testMetadataPlaylistIsParsedWithoutMediaDownload() {
    TemporaryExecutable executable(
        "#!/bin/sh\n"
        "for arg in \"$@\"; do\n"
        "  case \"$arg\" in --dump-single-json) mode=metadata;; --skip-download) skip=1;; --no-cache-dir) cache=1;; --ignore-config) config=1;; esac\n"
        "done\n"
        "[ \"${mode:-}\" = metadata ] && [ \"${skip:-}\" = 1 ] && [ \"${cache:-}\" = 1 ] && [ \"${config:-}\" = 1 ] || exit 64\n"
        "printf '%s\n' '{\"entries\":[{\"id\":\"qqAYW7uxErI\",\"title\":\"Fixture one\",\"duration\":12},{\"id\":\"dQw4w9WgXcQ\",\"title\":\"Fixture two\"}]}'\n");
    yoinks::YtDlpResolverConfig config;
    config.executable = executable.path();
    yoinks::YtDlpResolver resolver(config);
    const auto result = resolver.resolveMetadata("https://www.youtube.com/watch?v=qqAYW7uxErI");
    require(result.success, "metadata resolver should accept valid fixture JSON");
    require(result.entries.size() == 2, "playlist entries should be returned in source order");
    require(result.entries[0].video_id == "qqAYW7uxErI", "first video ID should be preserved");
    require(result.entries[1].title == "Fixture two", "second title should be parsed");
    require(result.entries[0].duration_seconds == 12.0, "duration should be parsed");
}

void testAudioUrlMustBeAllowedCdnUrl() {
    TemporaryExecutable executable(
        "#!/bin/sh\n"
        "for arg in \"$@\"; do\n"
        "  case \"$arg\" in --get-url) mode=url;; --skip-download) skip=1;; --no-cache-dir) cache=1;; --ignore-config) config=1;; esac\n"
        "done\n"
        "[ \"${mode:-}\" = url ] && [ \"${skip:-}\" = 1 ] && [ \"${cache:-}\" = 1 ] && [ \"${config:-}\" = 1 ] || exit 64\n"
        "printf '%s\n' 'https://r1---sn.googlevideo.com/videoplayback?expire=123'\n");
    yoinks::YtDlpResolverConfig config;
    config.executable = executable.path();
    yoinks::YtDlpResolver resolver(config);
    std::string url;
    require(resolver.resolveAudioUrl("qqAYW7uxErI", url),
            "valid googlevideo URL should be returned to trusted C++ caller");
    require(url.find("googlevideo.com") != std::string::npos,
            "resolved URL should remain in memory and satisfy CDN allowlist");
    require(!resolver.resolveAudioUrl("bad-id", url), "invalid video IDs must be rejected");
    require(url.empty(), "failed resolution must clear output URL");
}

void testInvalidUserUrlIsRejectedBeforeSpawn() {
    TemporaryExecutable executable("#!/bin/sh\nexit 0\n");
    yoinks::YtDlpResolverConfig config;
    config.executable = executable.path();
    yoinks::YtDlpResolver resolver(config);
    const auto result = resolver.resolveMetadata("https://youtube.com.evil.example/watch?v=qqAYW7uxErI");
    require(!result.success && result.error_code == "invalid_youtube_url",
            "lookalike YouTube host must fail before spawning yt-dlp");
}


void testClosedStandardDescriptorsDoNotBreakSpawn() {
    TemporaryExecutable executable(
        "#!/bin/sh\n"
        "printf '%s\n' '{\"id\":\"qqAYW7uxErI\",\"title\":\"closed-fd fixture\"}'\n");
    const pid_t child = fork();
    require(child >= 0, "fork should succeed for closed-descriptor test");
    if (child == 0) {
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
        yoinks::YtDlpResolverConfig config;
        config.executable = executable.path();
        yoinks::YtDlpResolver resolver(config);
        const auto result = resolver.resolveMetadata(
                "https://www.youtube.com/watch?v=qqAYW7uxErI");
        _exit(result.success && result.entries.size() == 1 ? 0 : 1);
    }
    int status = 0;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    require(WIFEXITED(status) && WEXITSTATUS(status) == 0,
            "resolver spawn must work when parent standard descriptors are closed");
}
void testProcessTimeoutIsEnforced() {
    TemporaryExecutable executable("#!/bin/sh\nsleep 2\n");
    yoinks::YtDlpResolverConfig config;
    config.executable = executable.path();
    config.timeout_milliseconds = 100;
    yoinks::YtDlpResolver resolver(config);
    const auto result = resolver.resolveMetadata("https://www.youtube.com/watch?v=qqAYW7uxErI");
    require(!result.success && result.error_code == "process_timeout",
            "slow yt-dlp process must be terminated at configured timeout");
}


void testCancellationTerminatesProcessGroup() {
    TemporaryExecutable executable("#!/bin/sh\nsleep 5\n");
    yoinks::YtDlpResolverConfig config;
    config.executable = executable.path();
    config.timeout_milliseconds = 10000;
    yoinks::YtDlpResolver resolver(config);
    std::atomic_bool cancelled{false};
    std::thread trigger([&cancelled]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        cancelled.store(true, std::memory_order_relaxed);
    });
    const auto started = std::chrono::steady_clock::now();
    const auto result = resolver.resolveMetadata(
            "https://www.youtube.com/watch?v=qqAYW7uxErI", &cancelled);
    trigger.join();
    const auto elapsed = std::chrono::steady_clock::now() - started;
    require(!result.success && result.error_code == "process_cancelled",
            "caller cancellation must terminate the resolver process");
    require(elapsed < std::chrono::seconds(2),
            "cancellation must not wait for the process timeout");
}
void testAudioUrlCancellationTerminatesProcessGroup() {
    TemporaryExecutable executable("#!/bin/sh\nsleep 5\nprintf '%s\n' 'https://r1---sn.googlevideo.com/videoplayback?expire=123'\n");
    yoinks::YtDlpResolverConfig config;
    config.executable = executable.path();
    config.timeout_milliseconds = 10000;
    yoinks::YtDlpResolver resolver(config);
    std::atomic_bool cancelled{false};
    std::thread trigger([&cancelled]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        cancelled.store(true, std::memory_order_relaxed);
    });
    std::string url;
    const auto started = std::chrono::steady_clock::now();
    const bool resolved = resolver.resolveAudioUrl("qqAYW7uxErI", url, &cancelled);
    trigger.join();
    const auto elapsed = std::chrono::steady_clock::now() - started;
    require(!resolved, "cancelled audio URL resolution must fail");
    require(url.empty(), "cancelled audio URL resolution must not expose a partial URL");
    require(elapsed < std::chrono::seconds(2),
            "audio URL resolver cancellation must not wait for process timeout");
}
void testStdoutLimitIsEnforced() {
    TemporaryExecutable executable("#!/bin/sh\nprintf '%02048d' 0\n");
    yoinks::YtDlpResolverConfig config;
    config.executable = executable.path();
    config.maximum_stdout_bytes = 1024;
    yoinks::YtDlpResolver resolver(config);
    const auto result = resolver.resolveMetadata("https://www.youtube.com/watch?v=qqAYW7uxErI");
    require(!result.success && result.error_code == "stdout_limit_exceeded",
            "oversized resolver output must be terminated");
}

}  // namespace

int main() {
    try {
        testMetadataPlaylistIsParsedWithoutMediaDownload();
        testAudioUrlMustBeAllowedCdnUrl();
        testInvalidUserUrlIsRejectedBeforeSpawn();
        testClosedStandardDescriptorsDoNotBreakSpawn();
        testProcessTimeoutIsEnforced();
        testCancellationTerminatesProcessGroup();
        testAudioUrlCancellationTerminatesProcessGroup();
        testStdoutLimitIsEnforced();
        std::cout << "All yt-dlp resolver tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
}
