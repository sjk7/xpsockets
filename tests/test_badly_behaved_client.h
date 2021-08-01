// test_badly_behaved_client.h

#include "../xpsockets.hpp"
#include <iostream>
#include <thread>
#include <atomic>

static inline std::atomic<bool> we_run{true};
static inline std::atomic<bool> wait{true};

struct socket_context : xp::SocketContext {
    int on_idle(xp::Sock* psock) noexcept override {
        if (wait) wait = false;
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
        wait = false;
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

inline void server_thread() {

    simple_server serv;
    serv.listen();
    while (we_run) {
        if (my_context.on_idle(&serv) < 0) {
            return;
        }
    }
    return;
}

inline void badly_behaved_client() {
    // so bad client connects, but never sends any data
    my_client c;
    while (we_run) {
        xp::SocketContext::sleep(1);
    }

    return;
}

inline int test_badly_behaved_client() {

    std::thread t{server_thread};
    while (wait) {
        xp::SocketContext::sleep(1);
    }
    std::thread bad_client_thread = std::thread{badly_behaved_client};

    // even thou a bad_client has connected but sent no data, we should be fine
    // still:
    xp::SocketContext::sleep(500); // make sure server is up and running

    int i = 0;
    while (i++ < 1000) {

        my_client c;

        auto sent = c.send(xp::simple_http_request);
        assert(sent.bytes_transferred == xp::simple_http_request.length());
        auto rx = c.read_until(
            xp::msec_timeout_t{20000}, c.data(), [&](auto a, auto b) {
                auto f = c.data().find("\r\n\r\n");
                // std::cout << "a: " << a << std::endl << "b: " << b <<
                // std::endl;
                if (f != std::string::npos) {
                    // std::cout << "Got reply from server: "
                    //          << xp::to_string(c.endpoint()) << "\n\n"
                    //          << c.data() << std::endl;
                    std::cout << "Good reply #" << i << "\r";
                    return -1;
                }
                return 0;
            });

        assert(rx.return_value == -1);
    }
    we_run = false;
    std::cout << std::endl;
    t.join();
    bad_client_thread.join();
    std::cout << "\n\n"
              << "badly behaved client test complete success!" << std::endl;
    return 0;
}
