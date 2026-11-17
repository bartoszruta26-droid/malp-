/*
 * Copyright (C) 2026 MALP contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "yoinks/yt_dlp_resolver.hpp"

#include "yoinks/http_upstream_stream.hpp"
#include "yoinks/youtube_url.hpp"

#include <nlohmann/json.hpp>

#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <utility>

extern char** environ;

namespace yoinks {
namespace {

using Json = nlohmann::json;

struct ProcessResult {
    bool success{false};
    std::string output;
    std::string error_code;
};

bool setCloseOnExec(int fd) {
    const int flags = fcntl(fd, F_GETFD);
    return flags >= 0 && fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == 0;
}

bool setNonBlocking(int fd) {
    const int flags = fcntl(fd, F_GETFL);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

void closeFd(int& fd) {
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}

void killAndReap(pid_t child) {
    if (child <= 0) return;
    (void)kill(-child, SIGKILL);
    int status = 0;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
}

ProcessResult runBoundedProcess(const std::string& executable,
                                const std::vector<std::string>& arguments,
                                const std::uint32_t timeout_milliseconds,
                                const std::size_t maximum_stdout_bytes,
                                const std::size_t maximum_stderr_bytes) {
    ProcessResult result;
    int stdout_pipe[2]{-1, -1};
    int stderr_pipe[2]{-1, -1};
    if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
        closeFd(stdout_pipe[0]); closeFd(stdout_pipe[1]);
        closeFd(stderr_pipe[0]); closeFd(stderr_pipe[1]);
        result.error_code = "pipe_creation_failed";
        return result;
    }

    for (const int fd : {stdout_pipe[0], stdout_pipe[1], stderr_pipe[0], stderr_pipe[1]}) {
        if (!setCloseOnExec(fd)) {
            closeFd(stdout_pipe[0]); closeFd(stdout_pipe[1]);
            closeFd(stderr_pipe[0]); closeFd(stderr_pipe[1]);
            result.error_code = "pipe_configuration_failed";
            return result;
        }
    }
    if (!setNonBlocking(stdout_pipe[0]) || !setNonBlocking(stderr_pipe[0])) {
        closeFd(stdout_pipe[0]); closeFd(stdout_pipe[1]);
        closeFd(stderr_pipe[0]); closeFd(stderr_pipe[1]);
        result.error_code = "pipe_configuration_failed";
        return result;
    }

    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    if (posix_spawn_file_actions_init(&actions) != 0) {
        closeFd(stdout_pipe[0]); closeFd(stdout_pipe[1]);
        closeFd(stderr_pipe[0]); closeFd(stderr_pipe[1]);
        result.error_code = "spawn_setup_failed";
        return result;
    }
    if (posix_spawnattr_init(&attributes) != 0) {
        posix_spawn_file_actions_destroy(&actions);
        closeFd(stdout_pipe[0]); closeFd(stdout_pipe[1]);
        closeFd(stderr_pipe[0]); closeFd(stderr_pipe[1]);
        result.error_code = "spawn_setup_failed";
        return result;
    }

    int setup_error = posix_spawn_file_actions_addopen(&actions, STDIN_FILENO,
            "/dev/null", O_RDONLY, 0);
    if (setup_error == 0) setup_error = posix_spawn_file_actions_adddup2(
            &actions, stdout_pipe[1], STDOUT_FILENO);
    if (setup_error == 0) setup_error = posix_spawn_file_actions_adddup2(
            &actions, stderr_pipe[1], STDERR_FILENO);
    if (setup_error == 0) setup_error = posix_spawn_file_actions_addclose(
            &actions, stdout_pipe[0]);
    if (setup_error == 0) setup_error = posix_spawn_file_actions_addclose(
            &actions, stderr_pipe[0]);
    if (setup_error == 0) setup_error = posix_spawn_file_actions_addclose(
            &actions, stdout_pipe[1]);
    if (setup_error == 0) setup_error = posix_spawn_file_actions_addclose(
            &actions, stderr_pipe[1]);
    short flags = POSIX_SPAWN_SETPGROUP;
    if (setup_error == 0) setup_error = posix_spawnattr_setflags(&attributes, flags);
    if (setup_error == 0) setup_error = posix_spawnattr_setpgroup(&attributes, 0);

    std::vector<char*> argv;
    argv.reserve(arguments.size() + 2);
    argv.push_back(const_cast<char*>(executable.c_str()));
    for (const auto& argument : arguments) {
        argv.push_back(const_cast<char*>(argument.c_str()));
    }
    argv.push_back(nullptr);

    pid_t child = -1;
    int spawn_error = setup_error;
    if (spawn_error == 0) {
        spawn_error = posix_spawn(&child, executable.c_str(), &actions, &attributes,
                                  argv.data(), environ);
    }
    posix_spawnattr_destroy(&attributes);
    posix_spawn_file_actions_destroy(&actions);
    closeFd(stdout_pipe[1]);
    closeFd(stderr_pipe[1]);
    if (spawn_error != 0) {
        closeFd(stdout_pipe[0]); closeFd(stderr_pipe[0]);
        result.error_code = "spawn_failed";
        return result;
    }

    std::size_t stderr_bytes = 0;
    bool child_exited = false;
    int child_status = 0;
    bool abort_child = false;
    const auto deadline = std::chrono::steady_clock::now()
            + std::chrono::milliseconds(timeout_milliseconds);

    auto drain = [&](int& fd, bool capture_stdout) {
        std::array<char, 8192> buffer{};
        for (;;) {
            const ssize_t count = read(fd, buffer.data(), buffer.size());
            if (count > 0) {
                const std::size_t amount = static_cast<std::size_t>(count);
                if (capture_stdout) {
                    if (amount > maximum_stdout_bytes
                            || result.output.size() > maximum_stdout_bytes - amount) {
                        result.error_code = "stdout_limit_exceeded";
                        abort_child = true;
                        return;
                    }
                    result.output.append(buffer.data(), amount);
                } else {
                    if (amount > maximum_stderr_bytes
                            || stderr_bytes > maximum_stderr_bytes - amount) {
                        result.error_code = "stderr_limit_exceeded";
                        abort_child = true;
                        return;
                    }
                    stderr_bytes += amount;
                }
                continue;
            }
            if (count == 0) { closeFd(fd); return; }
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            result.error_code = "pipe_read_failed";
            abort_child = true;
            return;
        }
    };

    while (!child_exited || stdout_pipe[0] >= 0 || stderr_pipe[0] >= 0) {
        if (std::chrono::steady_clock::now() >= deadline) {
            result.error_code = "process_timeout";
            abort_child = true;
        }
        if (abort_child) {
            killAndReap(child);
            child_exited = true;
            break;
        }

        std::array<pollfd, 2> descriptors{};
        nfds_t descriptor_count = 0;
        if (stdout_pipe[0] >= 0) descriptors[descriptor_count++] = {stdout_pipe[0], POLLIN | POLLHUP, 0};
        if (stderr_pipe[0] >= 0) descriptors[descriptor_count++] = {stderr_pipe[0], POLLIN | POLLHUP, 0};
        const int poll_result = poll(descriptors.data(), descriptor_count, 100);
        if (poll_result < 0 && errno != EINTR) {
            result.error_code = "pipe_poll_failed";
            abort_child = true;
            continue;
        }
        if (stdout_pipe[0] >= 0) drain(stdout_pipe[0], true);
        if (stderr_pipe[0] >= 0) drain(stderr_pipe[0], false);
        if (waitpid(child, &child_status, WNOHANG) == child) child_exited = true;
    }

    closeFd(stdout_pipe[0]);
    closeFd(stderr_pipe[0]);
    if (!child_exited) {
        while (waitpid(child, &child_status, 0) < 0 && errno == EINTR) {}
    }
    if (!result.error_code.empty()) return result;
    if (!WIFEXITED(child_status) || WEXITSTATUS(child_status) != 0) {
        result.error_code = "resolver_process_failed";
        return result;
    }
    result.success = true;
    return result;
}

bool validTextField(const Json& object, const char* key, std::string& output,
                    const std::size_t maximum_bytes) {
    const auto found = object.find(key);
    if (found == object.end() || !found->is_string()) return false;
    output = found->get<std::string>();
    return !output.empty() && output.size() <= maximum_bytes;
}

}  // namespace

YtDlpResolver::YtDlpResolver(YtDlpResolverConfig config) : config_(std::move(config)) {
    if (config_.executable.empty() || config_.executable.front() != '/'
            || config_.timeout_milliseconds == 0
            || config_.maximum_stdout_bytes < 1024
            || config_.maximum_stderr_bytes < 1024
            || config_.maximum_playlist_entries == 0
            || config_.maximum_playlist_entries > 200) {
        throw std::invalid_argument("invalid yt-dlp resolver configuration");
    }
}

bool YtDlpResolver::isValidVideoId(const std::string& video_id) noexcept {
    if (video_id.size() != 11) return false;
    for (const unsigned char character : video_id) {
        if (!((character >= 'a' && character <= 'z')
                || (character >= 'A' && character <= 'Z')
                || (character >= '0' && character <= '9')
                || character == '_' || character == '-')) return false;
    }
    return true;
}

MetadataResult YtDlpResolver::resolveMetadata(const std::string& youtube_url) const {
    MetadataResult result;
    if (!isAllowedYouTubeUrl(youtube_url)) {
        result.error_code = "invalid_youtube_url";
        return result;
    }
    const std::vector<std::string> arguments{
        "--ignore-config", "--no-cache-dir", "--no-warnings", "--no-progress",
        "--socket-timeout", "20", "--retries", "1", "--extractor-retries", "1",
        "--skip-download", "--flat-playlist", "--playlist-end",
        std::to_string(config_.maximum_playlist_entries), "--dump-single-json",
        "--", youtube_url
    };
    const ProcessResult process = runBoundedProcess(config_.executable, arguments,
            config_.timeout_milliseconds, config_.maximum_stdout_bytes,
            config_.maximum_stderr_bytes);
    if (!process.success) { result.error_code = process.error_code; return result; }

    Json root;
    try { root = Json::parse(process.output); }
    catch (const Json::exception&) { result.error_code = "invalid_resolver_json"; return result; }

    const Json* entries = &root;
    if (root.is_object() && root.contains("entries")) {
        if (!root["entries"].is_array()) { result.error_code = "invalid_playlist_metadata"; return result; }
        entries = &root["entries"];
    }
    if (entries->is_array()) {
        if (entries->empty() || entries->size() > config_.maximum_playlist_entries) {
            result.error_code = "playlist_limit_or_empty";
            return result;
        }
        for (const auto& entry : *entries) {
            if (!entry.is_object()) continue;
            ResolvedMediaEntry media;
            if (!validTextField(entry, "id", media.video_id, 11)
                    || !isValidVideoId(media.video_id)) continue;
            if (!validTextField(entry, "title", media.title, 512)) {
                media.title = media.video_id;
            }
            const auto duration = entry.find("duration");
            if (duration != entry.end() && duration->is_number()) {
                const double value = duration->get<double>();
                if (std::isfinite(value) && value >= 0.0 && value <= 86400.0) {
                    media.duration_seconds = value;
                }
            }
            result.entries.push_back(std::move(media));
        }
    } else if (entries->is_object()) {
        ResolvedMediaEntry media;
        if (validTextField(*entries, "id", media.video_id, 11)
                && isValidVideoId(media.video_id)) {
            if (!validTextField(*entries, "title", media.title, 512)) {
                media.title = media.video_id;
            }
            const auto duration = entries->find("duration");
            if (duration != entries->end() && duration->is_number()) {
                const double value = duration->get<double>();
                if (std::isfinite(value) && value >= 0.0 && value <= 86400.0) {
                    media.duration_seconds = value;
                }
            }
            result.entries.push_back(std::move(media));
        }
    }
    if (result.entries.empty()) { result.error_code = "no_supported_entries"; return result; }
    result.success = true;
    return result;
}

bool YtDlpResolver::resolveAudioUrl(const std::string& video_id,
                                    std::string& output_url) const {
    output_url.clear();
    if (!isValidVideoId(video_id)) return false;
    const std::string watch_url = "https://www.youtube.com/watch?v=" + video_id;
    const std::vector<std::string> arguments{
        "--ignore-config", "--no-cache-dir", "--no-warnings", "--no-progress",
        "--socket-timeout", "20", "--retries", "1", "--extractor-retries", "1",
        "--no-playlist", "--skip-download", "--format", "bestaudio/best",
        "--get-url", "--", watch_url
    };
    const ProcessResult process = runBoundedProcess(config_.executable, arguments,
            config_.timeout_milliseconds, 16 * 1024, config_.maximum_stderr_bytes);
    if (!process.success) return false;

    std::string candidate = process.output;
    while (!candidate.empty() && (candidate.back() == '\\n' || candidate.back() == '\\r')) {
        candidate.pop_back();
    }
    if (candidate.empty() || candidate.find_first_of("\\r\\n") != std::string::npos
            || !HttpUpstreamStream::isAllowedResolvedUrl(candidate)) {
        return false;
    }
    output_url = std::move(candidate);
    return true;
}

}  // namespace yoinks
