/*
 * Copyright (C) 2026 MALP contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <cstdint>
#include <string>

namespace yoinks {

struct HttpServerConfig {
    std::string bind_address{"127.0.0.1"};
    std::uint16_t port{8788};
    std::string bearer_token;
};

class HttpServer final {
public:
    explicit HttpServer(HttpServerConfig config);
    void run();

private:
    HttpServerConfig config_;
};

}  // namespace yoinks
