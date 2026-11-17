/*
 * Copyright (C) 2026 MALP contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "yoinks/yt_dlp_resolver.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
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
        "for arg in \"$@\"; do [ \"$arg\" = \"--dump-single-json\" ] && mode=metadata; done\n"
        "[ \"${mode:-}\" = metadata ] || exit 64\n"
        "printf '%s\\n' '{\"entries\":[{\"id\":\"qqAYW7uxErI\",\"title\":\"Fixture one\",\"duration\":12},{\"id\":\"dQw4w9WgXcQ\",\"title\":\"Fixture two\"}]}'\n");
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
        "for arg in \"$@\"; do [ \"$arg\" = \"--get-url\" ] && mode=url; done\n"
        "[ \"${mode:-}\" = url ] || exit 64\n"
        "printf '%s\\n' 'https://r1---sn.googlevideo.com/videoplayback?expire=123'\n");
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
        testProcessTimeoutIsEnforced();
        testStdoutLimitIsEnforced();
        std::cout << "All yt-dlp resolver tests passed\\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\\n';
        return 1;
    }
}
