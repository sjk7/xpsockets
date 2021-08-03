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

struct read_callback_t {
    virtual auto new_data(int read_result, const std::string_view) noexcept
        -> int
        = 0;
};

template <typename F> struct rcb_t : read_callback_t {
    F&& m_f;
    auto new_data(int read_result, const std::string_view data) noexcept
        -> int override {
        return m_f(read_result, data);
    }
    rcb_t(F&& f) : m_f(std::forward<F>(f)) {}
};

class Sock;
class ServerSocket;

class SocketContext {
    public:
    virtual int on_idle(Sock* ptr) noexcept;
    virtual void on_start(Sock* sck) const noexcept;
    static void sleep(long ms) noexcept;
    virtual void run(ServerSocket* server);
    std::atomic<bool> m_should_run{true};
    bool should_run() const noexcept { return m_should_run; }

#if defined(_DEBUG) || !defined(NDEBUG)
    bool debug_info{true};
#else
    bool debug_info{false};
#endif
};

inline void sleep_ms(int ms) {
    return SocketContext::sleep(ms);
}

class Sock {
    friend class SocketContext;
    friend class ServerSocket;
    class Impl;

    protected:
    Impl* pimpl;

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
    xp::sock_handle_t fd() const noexcept;
    [[nodiscard]] uint64_t id() const noexcept;
    void id_set(uint64_t newid) noexcept;

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
    typedef char no;
    typedef char yes[2];

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

inline std::string to_string(Sock* p) {
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
    uint64_t id() const noexcept { return Sock::id(); }

    private:
    ServerSocket* m_pserver{nullptr};
};

struct ServerStats {
    size_t peak_clients;
    xp::timepoint_t peaked_when;
    uint64_t naccepts;
    uint32_t nactive_accepts;
    uint32_t npeak_active_accepts;
    uint64_t nclients_disconnected_during_read;
};

class ServerSocket : public Sock {
    friend class SocketContext;

    public:
    ServerSocket(std::string_view name, const xp::endpoint_t& listen_where,
        SocketContext* ctx = nullptr);
    ~ServerSocket() override = default;
    int listen();
    bool is_blocking() const noexcept;

    // return < 0 to immediately disconnect the client, else just return 0
    virtual int on_new_client(AcceptedSocket* a);

    auto next_id() noexcept { return ++m_id_seed; }
    AcceptedSocket* do_accept(
        xp::endpoint_t& client_endpoint, bool debug_info = false) noexcept;
    bool add_client(AcceptedSocket* client) {
        if (m_clients.size() == max_clients()) {
            return false;
        }
        m_clients.push_back(client);
        if (m_clients.size() > m_stats.peak_clients) {
            m_stats.peak_clients = m_clients.size();
            m_stats.peaked_when = xp::system_current_time_millis();
        }
        return true;
    }

    const std::vector<AcceptedSocket*>& clients() const noexcept {
        return m_clients;
    }
    virtual size_t max_clients() const noexcept { return m_max_clients; }

    SocketContext* context() noexcept;

    SocketContext* context_const() const noexcept {
        auto pthis = const_cast<ServerSocket*>(this);
        return pthis->context();
    }

    bool is_listening() const noexcept {
        if (context_const())
            return context_const()->should_run();
        else
            return false;
    }

    protected:
    xp::sock_handle_t accept(xp::endpoint_t& endpoint, bool debug_info);
    bool remove_client(AcceptedSocket* client_to_remove, const char* why);
    virtual int on_idle() noexcept { return 0; }

    // return < 0 to disconnect the client, and NOT add him to the clients()
    // collection
    virtual int on_after_accept_new_client(
        SocketContext* ctx, AcceptedSocket* a, bool debug_info) noexcept;
    // return < 0 means quit listening
    int perform_internal_accept(SocketContext* ctx, bool debug_info) noexcept;
    ServerStats stats() const noexcept { return m_stats; }

    private:
    std::vector<AcceptedSocket*> m_clients;
    uint32_t m_id_seed{0};
    size_t m_max_clients{xp::MAX_CLIENTS};
    ServerStats m_stats{};
};

} // namespace xp

#endif // XPSOCKETS_HPP
