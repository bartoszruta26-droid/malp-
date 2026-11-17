/*
 * Copyright (C) 2026 MALP contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <boost/asio.hpp>

#include <cstdlib>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <string>

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: mock_mpd_server <port> <command-log>\n";
        return 2;
    }
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(argv[1], &end, 10);
    if (end == argv[1] || *end != '\0' || parsed == 0 || parsed > 65535) return 2;

    try {
        std::ofstream log(argv[2], std::ios::out | std::ios::app);
        if (!log) return 2;
        asio::io_context context;
        tcp::acceptor acceptor(context,
            tcp::endpoint{asio::ip::address_v4::loopback(),
                          static_cast<unsigned short>(parsed)});
        std::uint32_t next_id = 900;
        for (;;) {
            tcp::socket socket(context);
            acceptor.accept(socket);
            asio::write(socket, asio::buffer(std::string{"OK MPD 0.23.15\n"}));
            asio::streambuf buffer;
            for (;;) {
                boost::system::error_code error;
                asio::read_until(socket, buffer, '\n', error);
                if (error) break;
                std::istream input(&buffer);
                std::string command;
                std::getline(input, command);
                log << command << '\n';
                log.flush();

                std::string reply;
                if (command == "playlistinfo") {
                    reply = "OK\n";
                } else if (command.rfind("addid ", 0) == 0) {
                    reply = "Id: " + std::to_string(next_id++) + "\nOK\n";
                } else if (command.rfind("playid ", 0) == 0
                        || command.rfind("deleteid ", 0) == 0) {
                    reply = "OK\n";
                } else {
                    reply = "ACK [5@0] {unknown} unsupported fixture command\n";
                }
                asio::write(socket, asio::buffer(reply), error);
                if (error) break;
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "mock MPD fixture failed: " << error.what() << '\n';
        return 1;
    }
}
