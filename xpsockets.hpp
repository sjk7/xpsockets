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
    virtual void on_idle(Sock* ptr) noexcept;
    virtual void on_start(Sock* sck) const noexcept;
    static void sleep(long ms) noexcept;
    virtual void run(ServerSocket* server);
    bool should_run{true};

#if defined(_DEBUG) || !defined(NDEBUG)
    bool debug_info{false};
#else
    bool debug_info{false};
#endif
};

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
    [[nodiscard]] bool blocking() const noexcept;
    bool blocking_set(bool should_blck);

    template <typename F>
    auto read2(xp::msec_timeout_t t, std::string& data, F&& f)
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

class ConnectingSocket : public Sock {
    public:
    ConnectingSocket(std::string_view name, const xp::endpoint_t& connect_where,
        msec_timeout_t timeout = xp::msec_timeout_t::default_timeout,
        SocketContext* ctx = nullptr);
    ~ConnectingSocket() override;
};

class ServerSocket;
class AcceptedSocket : public Sock {
    public:
    AcceptedSocket(xp::sock_handle_t sock,
        const xp::endpoint_t& remote_endpoint, std::string_view name,
        ServerSocket* server, SocketContext* ctx = nullptr);
    ~AcceptedSocket() override = default;
    auto server() noexcept -> ServerSocket* { return m_pserver; }
    [[nodiscard]] auto id() const noexcept -> uint64_t;
    void id_set(uint64_t newid) noexcept;
    [[nodiscard]] auto to_string() const noexcept -> std::string;

    private:
    ServerSocket* m_pserver{nullptr};
};

class ServerSocket : public Sock {
    friend class SocketContext;

    public:
    ServerSocket(std::string_view name, const xp::endpoint_t& listen_where,
        SocketContext* ctx = nullptr);
    ~ServerSocket() override = default;
    auto listen() -> int;
    [[nodiscard]] auto is_blocking() const noexcept -> bool;

    // return < 0 to immediately disconnect the client, else just return 0
    virtual auto on_new_client(AcceptedSocket* a) -> int;

    auto next_id() noexcept -> uint32_t { return ++m_id_seed; }
    auto do_accept(xp::endpoint_t& client_endpoint,
        bool debug_info = false) noexcept -> AcceptedSocket*;
    auto add_client(AcceptedSocket* client) -> bool {
        if (m_clients.size() == max_clients()) {
            return false;
        }
        m_clients.push_back(client);
        if (m_clients.size() > m_peak_clients) {
            m_peak_clients = m_clients.size();
            m_peaked_when = xp::system_current_time_millis();
        }
        return true;
    }
    [[nodiscard]] virtual auto max_clients() const noexcept -> size_t {
        return m_max_clients;
    }
    [[nodiscard]] auto peaked_when() const noexcept -> xp::timepoint_t {
        return m_peaked_when;
    }
    [[nodiscard]] auto peak_num_clients() const noexcept -> size_t {
        return m_peak_clients;
    }

    protected:
    auto accept(xp::endpoint_t& endpoint, bool debug_info) -> xp::sock_handle_t;
    auto remove_client(AcceptedSocket* client_to_remove, const char* why)
        -> bool;

    // return < 0 to disconnect the client
    virtual auto on_after_accept_new_client(
        SocketContext* ctx, AcceptedSocket* a, bool debug_info) noexcept -> int;
    // return true if an accept took place
    auto perform_internal_accept(SocketContext* ctx, bool debug_info) noexcept
        -> bool;

    private:
    std::vector<AcceptedSocket*> m_clients;
    uint32_t m_id_seed{0};
    size_t m_max_clients{xp::MAX_CLIENTS};
    size_t m_peak_clients{0};
    xp::timepoint_t m_peaked_when{0};
    uint64_t m_naccepts{0};
    uint32_t m_nactive_accepts{0};
    uint32_t m_npeak_active_accepts{0};
    uint64_t m_nclients_disconnected_during_read{0};
};

} // namespace xp

#endif // XPSOCKETS_HPP
