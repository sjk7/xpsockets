// test_badly_behaved_client.h

#include "../xpsockets.hpp"
#include <iostream>
#include <thread>
#include <atomic>

static inline std::atomic<bool> we_run{true};
static inline std::atomic<bool> g_wait{true};
static inline std::atomic<bool> client_thread_finished{true};

struct socket_context : xp::SocketContext {
    int on_idle(xp::Sock* psock) noexcept override {
        if (g_wait) g_wait = false;
        if (!we_run) {
            return -1;
        }
        return xp::SocketContext::on_idle(psock);
    }
    virtual void run(xp::ServerSocket* server) override {
        return xp::SocketContext::run(server);
    }
};

inline socket_context my_context;

struct simple_server : xp::ServerSocket {
    simple_server()
        : xp::ServerSocket(
            "Dumb server", xp::endpoint_t{"0.0.0.0", 8080}, &my_context) {
        g_wait = false;
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
inline void server_thread() {

    simple_server serv;
    serv.listen();
    std::thread bad_client_thread = std::thread{badly_behaved_client};
    while (we_run) {
        xp::SocketContext::sleep(1);
    }
    bad_client_thread.join();
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
                    // std::cout << "a: " << a << std::endl << "b: " << b <<
                    // std::endl;
                    if (f != std::string::npos) {
                        // std::cout << "Got reply from server: "
                        //          << xp::to_string(c.endpoint()) << "\n\n"
                        //          << c.data() << std::endl;
                        printf("Good reply #%d\r", i);
                        fflush(stdout);
                        return -1;
                    }
                    return 0;
                });

            assert(rx.return_value == -1);
            (void)rx;
        }
        we_run = false;
        std::cout << std::endl;
        t.join();
        while (!client_thread_finished) {
            xp::SocketContext::sleep(1);
        }
    }
    std::cout << "\n\n"
              << "badly behaved client test complete success!" << std::endl;
    return 0;
}
