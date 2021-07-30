// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it. PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// http://www.viva64.com
#include "xpsockets.hpp"
#include <cassert>
#include <iostream>

using namespace std;
struct myserver : xp::ServerSocket {
    myserver(std::string_view name, const xp::endpoint_t& listen_where)
        : xp::ServerSocket(name, listen_where) {}

    virtual ~myserver() = default;
};

void test_server() {
    myserver server("test echo server", xp::endpoint_t{"0.0.0.0", 8080});
    server.listen();
}

int main() {
    test_server();
    struct MySockContext : xp::SocketContext {
        mutable int ctr = 0;
        virtual void on_idle(xp::Sock* /*sck*/) noexcept {
            // cout << "on_idle called for sock: " << sck->name() << endl;
            xp::SocketContext::sleep(1);
            ctr++;
        }
    };

    {
        try {
            MySockContext myctx;
            // example with lambda, using read2()
            xp::ConnectingSocket consock("my connecting socket",
                xp::endpoint_t{"google.com", 80},
                xp::msec_timeout_t::default_timeout, &myctx);
            cout << "Sending the following request: " << xp::simple_http_request
                 << endl;
            xp::ioresult_t send_result = consock.send(xp::simple_http_request);
            (void)send_result;
            assert(send_result.bytes_transferred
                == xp::simple_http_request.size());
            std::string data;

            int my_result = 0;

            auto read_result
                = consock.read2(xp::msec_timeout_t::default_timeout, data,
                    [&](auto read_result, auto& d) {
                        if (read_result == 0) {
                            const auto f = (d.find("\r\n\r\n"));
                            my_result = (int)f;
                            return (int)f;
                        }
                        return 0;
                    });
            cout << "Reading data from remote server took: " << myctx.ctr
                 << " ms." << endl;
            assert(!consock.is_valid()); // should be closed
            assert(read_result.return_value == my_result);
            std::string_view sv(data.data(), read_result.bytes_transferred);
            cout << "Server responded, with header: \n" << sv << endl << endl;
            cout << endl;
            assert(consock.last_error() == 0);
            assert(consock.last_error_string().empty());

        } catch (const std::exception& e) {
            cerr << e.what() << endl << endl;
        }

        struct header_reader : xp::read_callback_t {
            int new_data(int read_result,
                const std::string_view data) noexcept override {
                if (read_result == 0) {
                    const auto f = (data.find("\r\n\r\n"));
                    return (int)f;
                }
                return 0;
            }
        };

        try {
            // example with using a callback class. for read()
            xp::ConnectingSocket consock(
                "my connecting socket", xp::endpoint_t{"google.com", 80});
            cout << "Sending the following request: " << xp::simple_http_request
                 << endl;
            auto send_result = consock.send(xp::simple_http_request);
            (void)send_result;
            assert(send_result.bytes_transferred
                == xp::simple_http_request.length());
            std::string data;
            header_reader my_reader;
            auto read_result = consock.read(data, &my_reader);
            assert(read_result.bytes_transferred > 0);
            std::string_view sv(data.data(), read_result.bytes_transferred);
            cout << "Server responded, with header: \n" << sv << endl << endl;
            cout << endl;

        } catch (const std::exception& e) {
            cerr << "Caught an exception: " << e.what() << endl;
        }
    }

    cout.flush();
    fflush(stdout);

    return 0;
}
