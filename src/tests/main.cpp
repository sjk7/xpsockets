// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it. PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// http://www.viva64.com
#include "tests/test_badly_behaved_client.h"
#include "xpsockets.hpp"
#include "simple_http_server.cpp"
#include <cassert>
#include <iostream>

using namespace std;
struct myserver : xp::ServerSocket {
    myserver(std::string_view name, const xp::endpoint_t& listen_where)
        : xp::ServerSocket(name, listen_where) {}

    ~myserver() override = default;
};

void test_server() {
    myserver server("test echo server",
        xp::endpoint_t{"0.0.0.0", xp::port_type::testing_port});
    server.listen();
}

int main() {

    // test_server();

    {
        try {

            // example with lambda, using read2()
            xp::ConnectingSocket consock("my connecting socket",
                xp::endpoint_t{"google.com", xp::port_type::http},
                xp::msec_timeout_t::default_timeout);
            cout << "Sending the following request: " << xp::simple_http_request
                 << endl;
            const xp::ioresult_t send_result
                = consock.send(xp::simple_http_request);
            std::ignore = send_result;
            assert(send_result.bytes_transferred
                == xp::simple_http_request.size());
            std::string data;

            int64_t my_result = 0;

            const auto read_result
                = consock.read_until(xp::msec_timeout_t::default_timeout, data,
                    [&](auto read_result, auto& d) {
                        std::ignore = d;
                        if (read_result > 0) {
                            const auto f = (data.find("\r\n\r\n"));
                            my_result = f;
                            return -my_result;
                        }
                        return int64_t{0};
                    });
            std::ignore = read_result;

            assert(!consock.is_valid()); // should be closed
            assert(read_result.return_value == -my_result);
            const std::string_view sv(data.data(), data.size());
            cout << "Server responded, with header: \n" << sv << endl << endl;
            cout << endl;

        } catch (const std::exception& e) {
            cerr << e.what() << endl << endl;
        }

        struct header_reader : xp::read_callback_t {
            int64_t new_data(int read_result,
                const std::string_view data) noexcept override {
                if (read_result == 0) {
                    const auto f = (data.find("\r\n\r\n"));
                    return f;
                }
                return 0;
            }
        };

        try {
            // example with using a callback class. for read()
            xp::ConnectingSocket consock("my connecting socket",
                xp::endpoint_t{"google.com", xp::port_type::http});
            cout << "Sending the following request: " << xp::simple_http_request
                 << endl;
            const auto send_result = consock.send(xp::simple_http_request);
            std::ignore = send_result;
            assert(send_result.bytes_transferred
                == xp::simple_http_request.length());
            std::string data;
            header_reader my_reader;
            const auto read_result = consock.read(data, &my_reader);
            std::ignore = read_result;
            assert(read_result.bytes_transferred > 0);
            const std::string_view sv(data.data());
            cout << "Server responded, with header: \n" << sv << endl << endl;
            cout << endl;

        } catch (const std::exception& e) {
            cerr << "Caught an exception: " << e.what() << endl;
            exit(-1);
        }
    }

    test_badly_behaved_client(3);

    cout.flush();
    fflush(stdout);

    // run_file_server();

    return 0;
}
