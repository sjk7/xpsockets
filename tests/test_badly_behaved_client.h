// test_badly_behaved_client.h

#include "../xpsockets.hpp"
#include <iostream>
#include <thread>
#include <atomic>

static inline std::atomic<bool> we_run{true};
static inline std::atomic<bool> g_wait{true};
static inline std::atomic<bool> client_thread_finished{true};

struct simple_server : xp::ServerSocket {
    simple_server()
        : xp::ServerSocket("Dumb server", xp::endpoint_t{"0.0.0.0", 8080}) {
        g_wait = false;
    }
    int on_idle() noexcept override {
        if (g_wait) g_wait = false;
        if (!we_run) {
            return -1;
        }
        return 0;
    }

    virtual int on_new_client(xp::AcceptedSocket* a) override {
        // std::cout << "Server accepted client: " << xp::to_string(a)
        //          << std::endl;
        return xp::ServerSocket::on_new_client(a);
    }

    virtual ~simple_server() = default;
};

struct my_client : xp::ConnectingSocket {
    my_client()
        : xp::ConnectingSocket("bad client", xp::endpoint_t{"127.0.0.1", 8080},
            xp::msec_timeout_t::ten_minutes) {}
    virtual ~my_client() = default;
};

inline void badly_behaved_client() {
    // so bad client connects, but never sends any data
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
    mystruct s;
    xp::to_string(s);
    (void)s;
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
        xp::SocketContext::sleep(500); // make sure server is up and running

        int i = 0;

        while (i++ < 10) {
            my_client c;
            auto sent = c.send(xp::simple_http_request);
            (void)sent;
            assert(sent.bytes_transferred == xp::simple_http_request.length());
            auto rx = c.read_until(
                xp::msec_timeout_t{20000}, c.data(), [&](auto, auto) {
                    auto f = c.data().find("\r\n\r\n");
                    if (f != std::string::npos) {
                        printf("Good reply #%d\r", i);
                        fflush(stdout);
                        return -1;
                    }
                    return 0;
                });

            assert(rx.return_value == -1);
            (void)rx;

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
