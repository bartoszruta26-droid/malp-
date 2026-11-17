/*
 * Copyright (C) 2026 MALP contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <string>

namespace yoinks {

inline bool isAllowedYouTubeUrl(const std::string& value) {
    constexpr char scheme[] = "https://";
    if (value.compare(0, sizeof(scheme) - 1, scheme) != 0
            || value.find('#') != std::string::npos
            || value.find('@') != std::string::npos) return false;
    const std::size_t authority_start = sizeof(scheme) - 1;
    const std::size_t path_start = value.find('/', authority_start);
    if (path_start == std::string::npos) return false;
    const std::string host = value.substr(authority_start, path_start - authority_start);
    if (host != "youtube.com" && host != "www.youtube.com"
            && host != "m.youtube.com" && host != "music.youtube.com"
            && host != "youtu.be") return false;

    const std::string path_and_query = value.substr(path_start);
    const std::size_t query_start = path_and_query.find('?');
    const std::string path = path_and_query.substr(0, query_start);
    const std::string query = query_start == std::string::npos
            ? "" : path_and_query.substr(query_start + 1);

    auto valid_id = [](const std::string& id, std::size_t minimum, std::size_t maximum) {
        if (id.size() < minimum || id.size() > maximum) return false;
        for (const unsigned char character : id) {
            if (!((character >= 'a' && character <= 'z')
                    || (character >= 'A' && character <= 'Z')
                    || (character >= '0' && character <= '9')
                    || character == '_' || character == '-')) return false;
        }
        return true;
    };

    if (host == "youtu.be") {
        return path.size() == 12 && valid_id(path.substr(1), 11, 11);
    }
    if (path == "/watch") {
        const std::string key = "v=";
        const std::size_t position = query.find(key);
        if (position == std::string::npos
                || (position != 0 && query[position - 1] != '&')) return false;
        const std::size_t start = position + key.size();
        const std::size_t end = query.find('&', start);
        return valid_id(query.substr(start, end == std::string::npos
                ? std::string::npos : end - start), 11, 11);
    }
    if (path == "/playlist") {
        const std::string key = "list=";
        const std::size_t position = query.find(key);
        if (position == std::string::npos
                || (position != 0 && query[position - 1] != '&')) return false;
        const std::size_t start = position + key.size();
        const std::size_t end = query.find('&', start);
        return valid_id(query.substr(start, end == std::string::npos
                ? std::string::npos : end - start), 10, 128);
    }
    for (const std::string prefix : {"/shorts/", "/live/", "/embed/"}) {
        if (path.compare(0, prefix.size(), prefix) == 0) {
            return path.size() == prefix.size() + 11
                    && valid_id(path.substr(prefix.size()), 11, 11);
        }
    }
    return false;
}

}  // namespace yoinks
