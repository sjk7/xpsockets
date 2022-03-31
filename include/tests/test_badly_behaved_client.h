// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it. PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// http://www.viva64.com
// test_badly_behaved_client.h

#include "../xpsockets.hpp"
#include <iostream>
#include <thread>
#include <atomic>

static inline std::atomic<bool> we_run{true};
static inline std::atomic<bool> g_wait{true};
static inline std::atomic<bool> client_thread_finished{true};
static inline xp::port_type my_port = xp::port_type::testing_port;

struct simple_server : xp::ServerSocket {
    simple_server()
        : xp::ServerSocket("Dumb server", xp::endpoint_t{"0.0.0.0", my_port}) {
        g_wait = false;
    }
    int on_idle() noexcept override {
        if (g_wait) {
            g_wait = false;
        }
        if (!we_run) {
            return -1;
        }
        return 0;
    }
    ~simple_server() override = default;
};

struct my_client : xp::ConnectingSocket {
    my_client()
        : xp::ConnectingSocket("bad client",
            xp::endpoint_t{"127.0.0.1", my_port},
            xp::msec_timeout_t::ten_minutes) {}
    ~my_client() override = default;
};

inline void badly_behaved_client() {
    // so bad client connects, but never sends any data
    // this tests that the server is capable of processing concurrent clients
    try {
        my_client c;
        while (we_run) {
            xp::SocketContext::sleep(1);
        }
    } catch (const std::exception& e) {

        assert("Connect itself should be successful, even though we don't send "
               "any data"
            == nullptr);
        throw(e);
    }
    client_thread_finished = true;
    return;
}
struct mystruct {
    int test{1};
    friend std::ostream& operator<<(std::ostream& os, const mystruct& s) {
        os << s.test;
        return os;
    }
};
inline void server_thread() {

    simple_server serv;
    serv.listen();
    const mystruct s;
    xp::to_string(s);
    std::ignore = s;
    std::thread bad_client_thread = std::thread{badly_behaved_client};

    bad_client_thread.join();
    while (serv.is_listening()) {
        xp::SocketContext::sleep(1);
    }
    assert(!serv.is_listening());
    return;
}

inline int test_badly_behaved_client(int ctr = 2) {

    while (ctr-- > 0) {
        we_run = true;
        g_wait = true;
        std::thread t{server_thread};
        while (g_wait) {
            xp::SocketContext::sleep(1);
        }

        // even thou a bad_client has connected but sent no data, we should be
        // fine still:

        int i = 0;
        static constexpr auto ENOUGH_TO_DETECT_RACES = 10;

        while (i++ < ENOUGH_TO_DETECT_RACES) {
            my_client c;
            const auto sent = c.send(xp::simple_http_request);
            std::ignore = sent;
            assert(sent.bytes_transferred == xp::simple_http_request.length());
            const auto tt = xp::msec_timeout_t{xp::msec_timeout_t::ten_seconds};
            const auto rx = c.read_until(tt, c.data(),
                [&](const auto& tmpdata, auto bytes_read) noexcept {
                    (void)tmpdata;
                    (void)bytes_read;
                    const auto f = c.data().find("\r\n\r\n");
                    if (f != std::string::npos) {
                        printf("Good reply #%d\r", i);
                        fflush(stdout);
                        return -1;
                    }
                    return 0;
                });

            printf("Return value: %lld: %s\n", rx.return_value,
                c.last_error_string().c_str());
            assert(rx.return_value == 10057 || // doze
                rx.return_value == 107 // Ubuntu
                || rx.return_value == 57); // mac
                
            std::ignore = rx;

        }; // while()
        we_run = false;
        std::cout << std::endl;
        while (!client_thread_finished) {
            xp::SocketContext::sleep(1);
        }
        t.join();
    }
    std::cout << "\n\n"
              << "badly behaved client test complete success!" << std::endl;
    return 0;
}
