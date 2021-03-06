// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it. PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// http://www.viva64.com
#pragma once
#ifndef XPSOCKETS_HPP
#define XPSOCKETS_HPP

// C++: Public include file for Steve's xpsockets (cross-platform) sockets
// library. You do not need to, nor should you, #include "xpsockets.h" in your
// project. That is an implementation file for the library. This file is the one
// to include if you are a library consumer.
#include "xpcommon.h"
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <atomic>
#include <array>
#include <memory>

namespace xp {
inline constexpr std::string_view simple_http_request
    = "GET /index.html HTTP/1.1\r\n"
      "Host: www.google.com\r\n"
      "Connection:close\r\n\r\n";

inline constexpr std::string_view simple_http_response
    = "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/html\r\n"
      "Connection: close\r\n"
      "Server: C++Test server\r\n"
      "Content-Length: xxxx\r\n\r\n";

inline constexpr std::string_view simple_http_response_no_cl
    = "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/html\r\n"
      "Connection: close\r\n"
      "Server: C++Test server\r\n\r\n";

inline constexpr std::string_view simple_http_response_no_cl_ico
    = "HTTP/1.1 200 OK\r\n"
      "Content-Type: 'image/ico'\r\n"
      "Connection: close\r\n"
      "Server: C++Test server\r\n\r\n";

inline constexpr std::string_view simple_http_response_no_cl_404
    = "HTTP/1.1 404 Not Found\r\n"
      "Content-Type: text/html\r\n"
      "Connection: close\r\n"
      "Server: C++Test server\r\n\r\n";

#ifndef PURE_VIRTUAL
#define PURE_VIRTUAL 0
#endif

struct read_callback_t {
    virtual int64_t new_data(
        int read_result, const std::string_view) noexcept = PURE_VIRTUAL;
};

template <typename F> struct rcb_t : read_callback_t {
    F&& m_f;
    int64_t new_data(
        int read_result, const std::string_view data) noexcept override {
        return m_f(read_result, data);
    }
    rcb_t(F&& f) noexcept : m_f(std::forward<F>(f)) {}
};

class Sock;
class ServerSocket;
class SocketContext {
    public:
    virtual int on_idle(Sock* ptr) noexcept;
    virtual void on_start(Sock* sck) const noexcept;
    static void sleep(long ms) noexcept;
    virtual void run(ServerSocket* server) noexcept;
    std::atomic<bool> m_should_run{true};
    bool should_run() const noexcept { return m_should_run; }

#if defined(_DEBUG) || !defined(NDEBUG)
    bool debug_info{true};
#else
    bool debug_info{true};
#endif
};

inline void sleep_ms(int ms) noexcept {
    return SocketContext::sleep(ms);
}

struct no_copy {
    no_copy() = default;
    ~no_copy() = default;
    no_copy(const no_copy&) = delete;
    no_copy& operator=(const no_copy&) = delete;
};

class Sock : public no_copy {
    friend class SocketContext;
    friend class ServerSocket;
    class Impl;

    protected:
    std::unique_ptr<Impl> pimpl;

    public:
    Sock(std::string_view name, const endpoint_t& ep,
        SocketContext* ctx = nullptr);
    Sock(xp::sock_handle_t a, std::string_view name, const endpoint_t& ep,
        SocketContext* ctx = nullptr);
    virtual ~Sock();
    [[nodiscard]] auto underlying_socket() const noexcept -> xp::sock_handle_t;
    auto send(std::string_view data) noexcept -> xp::ioresult_t;
    [[nodiscard]] bool is_blocking() const noexcept;
    bool blocking_set(bool should_blck);
    [[nodiscard]] xp::sock_handle_t fd() const noexcept;
    [[nodiscard]] uint64_t id() const noexcept;
    void id_set(uint64_t newid) noexcept;
    int close() noexcept;

    template <typename F>
    auto read_until(xp::msec_timeout_t t, std::string& data, F&& f)
        -> xp::ioresult_t {
        rcb_t rcb(std::forward<F>(f));
        return read(data, &rcb, t);
    }
    // returns:
    // ret.return_value: return whatever recv returns, or < 0 if some error
    // ** NOTE: if return_value == 0, the client disconnected and this will be
    // reflected in last_error() and last_error_string **
    // ret.bytes_transferred :obvious, innit ?!
    // ** NB: if no callback is set, read() returns after only one read attempt,
    // and it is expected that the caller will be looping read() until he gets
    // what he needs. If the callback returns anything other than 0, this will
    // return immediately
    auto read(std::string& data, read_callback_t* read_callback,
        xp::msec_timeout_t timeout = xp::msec_timeout_t{
            xp::msec_timeout_t::default_timeout}) noexcept -> xp::ioresult_t;
    [[nodiscard]] auto last_error_string() const noexcept -> const std::string&;
    [[nodiscard]] auto last_error() const noexcept -> int;
    [[nodiscard]] auto is_valid() const noexcept -> bool;
    [[nodiscard]] auto name() const noexcept -> std::string_view;
    [[nodiscard]] auto endpoint() const noexcept -> const xp::endpoint_t&;
    auto data() noexcept -> std::string&;
    [[nodiscard]] auto ms_alive() const noexcept -> xp::duration_t;
};

namespace has_insertion_operator_impl {
    using no = char;
    using yes = std::array<char, 2>;

    struct any_t {
        template <typename T> any_t(T const&);
    };

    no operator<<(std::ostream const&, any_t const&);

    yes& test(std::ostream&);
    no test(no);

    template <typename T> struct has_insertion_operator {
        static std::ostream& s;
        static T const& t;
        static bool const value = sizeof(test(s << t)) == sizeof(yes);
    };
} // namespace has_insertion_operator_impl

template <typename T>
struct has_insertion_operator
    : has_insertion_operator_impl::has_insertion_operator<T> {};

// NOTE: your type must implement operator << for this to work!
template <typename T> inline std::string to_string(const T* p) {
    static_assert(has_insertion_operator<T>::value,
        "Your type needs ostream& << operator in order to use xp::to_string()");
    return xp::concat(*p);
}

template <typename T> inline std::string to_string(const T& p) {
    static_assert(has_insertion_operator<T>::value,
        "Your type needs ostream& << operator in order to use xp::to_string()");
    return xp::concat(p);
}

inline std::string to_string(const Sock* p) {
    assert(p);
    const auto ret = concat("fd: ", to_int(p->fd()), " id: ", p->id(),
        " endpoint: ", xp::to_string(p->endpoint()),
        " ms_alive: ", xp::to_int(p->ms_alive()));
    return ret;
}

class ConnectingSocket : public Sock {
    public:
    ConnectingSocket(std::string_view name, const xp::endpoint_t& connect_where,
        msec_timeout_t timeout = xp::msec_timeout_t::default_timeout,
        SocketContext* ctx = nullptr);
    ~ConnectingSocket() override;
};

class AcceptedSocket : public Sock {
    public:
    AcceptedSocket(xp::sock_handle_t sock,
        const xp::endpoint_t& remote_endpoint, std::string_view name,
        ServerSocket* server, SocketContext* ctx = nullptr);
    ~AcceptedSocket() override = default;
    auto server() noexcept -> ServerSocket* { return m_pserver; }

    virtual void on_closed();

    private:
    ServerSocket* m_pserver{nullptr};
};

struct ServerStats {
    size_t peak_clients;
    xp::timepoint_t when_peak_clients;
    xp::timepoint_t when_last_client;
    xp::timepoint_t when_shown_last_summary;
    uint64_t naccepts;
    uint32_t nactive_accepts;
    uint32_t npeak_active_accepts;
    xp::timepoint_t when_peak_accepts;
    uint64_t nclients_disconnected_during_read;
};

class ServerSocket : public Sock {
    friend class SocketContext;
    friend class AcceptedSocket;

    public:
    ServerSocket(std::string_view name, const xp::endpoint_t& listen_where,
        SocketContext* ctx = nullptr);
    virtual ~ServerSocket() override = default;
    int listen();

    [[nodiscard]] const std::vector<Sock*>& clients() const noexcept {
        return m_clients;
    }
    [[nodiscard]] virtual size_t max_clients() const noexcept {
        return m_max_clients;
    }

    SocketContext* context() noexcept;

    [[nodiscard]] SocketContext* context_const() const noexcept {
        auto pthis = const_cast<ServerSocket*>(this);
        return pthis->context();
    }

    [[nodiscard]] bool is_listening() const noexcept {
        if (context_const()) {
            return context_const()->should_run();
        }
        // the default:
        return false;
    }

    protected:
    xp::sock_handle_t accept(xp::endpoint_t& endpoint, bool debug_info);
    bool remove_client(Sock* client_to_remove, const char* why);
    virtual int on_idle() noexcept { return 0; }

    // return a nullptr to immediately disconnect the client.
    virtual AcceptedSocket* on_new_client(std::unique_ptr<AcceptedSocket>& a);

    auto next_id() noexcept { return ++m_id_seed; }
    std::unique_ptr<AcceptedSocket> do_accept(
        xp::endpoint_t& client_endpoint, bool debug_info = false);

    Sock* add_client(std::unique_ptr<AcceptedSocket>& client) {
        if (m_clients.size() == max_clients()) {
            return nullptr;
        }
        m_clients.push_back(client.get());
        client.release(); // vector owns it now
        if (m_clients.size() > m_stats.peak_clients) {
            m_stats.peak_clients = m_clients.size();
            m_stats.when_peak_clients = xp::system_current_time_millis();
        }
        return m_clients[m_clients.size() - 1];
    }

    // return < 0 to disconnect the client, and NOT add him to the clients()
    // collection
    virtual int on_after_accept_new_client(
        SocketContext* ctx, AcceptedSocket* a, bool debug_info) noexcept;
    // return < 0 means quit listening
    int64_t perform_internal_accept(
        SocketContext* ctx, bool debug_info) noexcept;
    [[nodiscard]] ServerStats stats() const noexcept { return m_stats; }

    virtual bool on_got_request(
        Sock* client, std::string_view request, bool& keep_client) {
        (void)client;
        (void)request;
        (void)keep_client;
        return false;
    }

    virtual void on_client_closed(AcceptedSocket*) {}

    private:
    std::vector<Sock*> m_clients;
    uint32_t m_id_seed{0};
    size_t m_max_clients{xp::MAX_CLIENTS};
    ServerStats m_stats{};
};

// returns the position of the doublenewline if found,
// or if not, returns std::string::npos
inline ioresult_t read_until_found(Sock* sock,
    std::string_view find_what = xp::DOUBLE_NEWLINE,
    xp::msec_timeout_t timeout = xp::msec_timeout_t::thirty_seconds,
    bool debug = false) {

    assert(sock);
    size_t found = std::string::npos;
    xp::ioresult_t myread{};

    while (found == std::string::npos) {
        myread = sock->read_until(timeout, sock->data(),
            [&](auto& bytes_read, auto& mydata) noexcept {
                assert(bytes_read); // should only return to us when data
                                    // ready, or fail with timeout
                if (debug) {
                    printf("read: %s\n", mydata.data());
                    std::ignore = bytes_read;
                    std::ignore = mydata;
                }
                found = sock->data().find(find_what);
                if (found != std::string::npos) {
                    return found;
                }
                return size_t(0);
            });

        if (myread.return_value < 0) return myread;
    };
    return myread;
}

} // namespace xp

#endif // XPSOCKETS_HPP
