// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it. PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// http://www.viva64.com
#include "xpsockets.hpp"
#include "xpsockets.h"

#include <array>
#include <cassert>
#include <iostream>
#include <string>
#include <string_view>
#include <algorithm> // remove_if

#ifdef _MSC_VER
#ifdef assert
#undef assert
#define assert _ASSERTE // makes assert()s resumable in VS
#endif
#endif

using namespace std;
using namespace xp;

inline auto default_context_instance() -> SocketContext& {
    static SocketContext ctx;
    return ctx;
}
enum class sockstate_t { none, in_recv, in_send };

struct sockstate_wrapper_t {
    sockstate_wrapper_t(sockstate_t& state, sockstate_t this_state)
        : m_state(state) {
        m_state = this_state;
    }
    ~sockstate_wrapper_t() { m_state = sockstate_t::none; }

    [[nodiscard]] auto state() const noexcept { return m_state; }

    private:
    sockstate_t& m_state;
};

static inline const uint64_t longest_alive{0};
static inline std::string longest_alive_info;

inline auto longest_alive_data() noexcept -> const auto& {
    return longest_alive_info;
}

template <typename CRTP> class SocketBase {
    static inline bool socks_init = false;

    public:
    // constructor for server clients, or for any case
    // where you have a valid socket handle already
    SocketBase(xp::sock_handle_t s, Sock* sck, std::string_view name,
        SocketContext* ctx, endpoint_t endpoint)
        : m_fd(s)
        , m_name(name)
        , m_ctx(ctx)
        , m_pcrtp(sck)
        , m_endpoint(std::move(endpoint)) {
        const auto bl = sock_set_blocking(to_native(m_fd), false);
        if (!bl) {
            throw std::runtime_error(
                concat("Unable to set blocking mode, for: ", m_name, "\n",
                    to_string(m_endpoint), xp::socket_error(), ":",
                    xp::socket_error_string()));
        }

        struct linger sl = {};
        xp::socklen_t optlen = sizeof(sl);
        int ffs = getsockopt(
            to_native(this->m_fd), SOL_SOCKET, SO_LINGER, (char*)&sl, &optlen);
        assert(
            sl.l_linger == 0 && sl.l_onoff == 0); // at least the case in 'doze
        sl.l_onoff = 1;
        sl.l_linger = 2;

        ffs = setsockopt(
            to_native(m_fd), SOL_SOCKET, SO_LINGER, (const char*)&sl, optlen);
        assert(ffs == 0);

        m_ms_create = xp::system_current_time_millis();
    }

    SocketBase(Sock* sck, std::string_view name, SocketContext* ctx,
        endpoint_t endpoint)
        : m_name(name)
        , m_ctx(ctx)
        , m_pcrtp(sck)
        , m_endpoint(std::move(endpoint)) {
        if (m_ctx == nullptr) {
            m_ctx = &default_context_instance();
        }
#ifndef _WIN32
        signal(SIGPIPE, SIG_IGN);
#else
        if (!socks_init) {
            socks_init = true;
            int i = xp::init_sockets();
            if (i != 0) {
                throw std::runtime_error(xp::concat(
                    "Unable to init sockets library, returned error: ", i));
            }
        }
#endif
        m_fd = xp::sock_create();
        if (m_fd == xp::invalid_handle) {
            throw std::runtime_error("No system resources to create socket");
        }

        const auto bl = sock_set_blocking(to_native(m_fd), false);
        if (!bl) {
            throw std::runtime_error(
                concat("Unable to set blocking mode, for: ", m_name, "\n",
                    to_string(m_endpoint), xp::socket_error(), ":",
                    xp::socket_error_string()));
        }
        m_ms_create = xp::system_current_time_millis();
    }
    virtual ~SocketBase() {
        if (context() && context()->debug_info) {

            const auto ms = to_int(ms_alive());
            if (ms > longest_alive) {
                const auto s = to_string(endpoint());
                const auto summary = xp::concat("socket, with id: ", this->id(),
                    " and endpoint: ", s, " was alive for ", ms, " ms.");
                printf("%s\n", summary.c_str());
                longest_alive_info = summary;
            }
        }

        close();
    }

    [[nodiscard]] auto id() const noexcept -> uint64_t { return m_id; }
    void id_set(uint64_t newid) noexcept { m_id = newid; }

    [[nodiscard]] auto state() const noexcept -> sockstate_t { return m_state; }

    [[nodiscard]] auto ms_alive() const noexcept -> xp::duration_t {
        return xp::duration(xp::system_current_time_millis(), m_ms_create);
    }

    auto close() -> int {

        assert(m_state
            == sockstate_t::none); // why are we being closed whilst active?
        int ret = 0;
        xp::sock_close(m_fd);
        m_fd = xp::invalid_handle;
        return ret;
    }
    auto shutdown(xp::shutdown_how how, const char* why) -> int {
        if (!is_valid()) {
            return 0;
        } // I forgive u
        if (this->context()) {
            if (this->context()->debug_info) {
                printf("called shutdown() on fd: %d , with id %lld becoz %s\n",
                    static_cast<int>(m_fd), this->m_id, why);
            }
        }

        return xp::sock_shutdown(xp::to_native(m_fd), how);
    }
    [[nodiscard]] auto fd() const { return m_fd; }
    [[nodiscard]] auto name() const -> const std::string& { return m_name; }

    auto send(std::string_view& sv) -> xp::ioresult_t {
        sockstate_wrapper_t w(this->m_state, sockstate_t::in_send);
        int remain = (int)sv.size();
        auto sck = xp::to_native(m_fd);
        char* ptr = (char*)sv.data();
        xp::ioresult_t retval{};

        while (remain > 0) {
            const auto sz = remain > 512 ? 512 : remain;
            int sent = ::send(sck, ptr, sz, 0);
            retval.bytes_transferred
                += sent >= 0 ? static_cast<size_t>(sent) : 0;

            if (sent < 0) {
                perror("send");
                const int e = xp::socket_error();

                if (e == EAGAIN || e == to_int(xp::errors_t::WOULD_BLOCK)) {
                    this->m_ctx->on_idle(crtp());
                } else {
                    this->m_last_error = e;
                    this->m_slast_error = xp::concat(
                        "error in send(): ", xp::socket_error_string(e));
                    retval.return_value = e;
                    return retval;
                }

            } else {
                ptr += static_cast<std::ptrdiff_t>(sent);
            }
            remain -= sz;
        };
        assert(retval.return_value == 0);
        return retval;
    }

    auto read(std::string& data, read_callback_t* read_callback,
        xp::msec_timeout_t timeout) -> xp::ioresult_t {

        sockstate_wrapper_t w(this->m_state, sockstate_t::in_recv);
        xp::ioresult_t ret{0, 0};
        auto sck = xp::to_native(m_fd);
        static constexpr size_t BUFSIZE = 512;
        const auto time_start = xp::system_current_time_millis();
        assert(this->m_fd != xp::invalid_handle);

        while (true) {
            std::array<char, BUFSIZE> tmp = {};
            // WHO is closing us when we are in recv, ffs?
            assert(this->m_fd != xp::invalid_handle);
            int read = ::recv(sck, tmp.data(), BUFSIZE, 0);
            ret.return_value = read;

            if (read == 0) {
                m_last_error = to_int(xp::errors_t::NOT_CONN);
                m_slast_error = concat("Client closed socket during read(): ",
                    xp::socket_error_string(m_last_error));
                // callers should watch for this: it means the remote end was
                // closed during recv
                assert(ret.return_value != -1);
                return ret;
            }

            if (read < 0) {
                m_last_error = xp::socket_error();
                assert(m_last_error); // why? it returned -1
                const auto dur = xp::duration(
                    xp::system_current_time_millis(), time_start);
                if (static_cast<int>(to_int(dur)) > to_int(timeout)) {
                    m_last_error = to_int(xp::errors_t::TIMED_OUT);
                    m_slast_error = "Timed out in read() waiting for data";
                    ret.return_value = -to_int(xp::errors_t::TIMED_OUT);
                    ret.bytes_transferred = 0;
                    assert(ret.return_value != -1);
                    return ret;
                }
                if (m_last_error == EAGAIN
                    || m_last_error == to_int(xp::errors_t::WOULD_BLOCK)) {
                    // m_slast_error = xp::socket_error_string();
                    // ^^ save some cpu, we prolly don't need to do this as
                    // failure is almost guaranteed: it's how sockets work!
                    this->m_ctx->on_idle(crtp());
                    ret.return_value = -m_last_error;
                    return ret;
                }
                m_slast_error
                    = xp::concat("unexpected return value of: ", m_last_error,
                        socket_error_string(m_last_error));
                ret.return_value = -m_last_error;
                assert(ret.return_value != -1);
                return ret;
            }
            {
                if (read > 0) {
                    ret.bytes_transferred += static_cast<size_t>(read);
                    const auto old_size = data.size();
                    data.resize(data.size() + static_cast<size_t>(read));
                    memcpy(data.data() + old_size, tmp.data(),
                        static_cast<size_t>(read));
                    assert(ret.return_value != -1);
                }
                if (read_callback != nullptr) {
                    const auto cbret = read_callback->new_data(read, data);
                    // DO NOT make ret == cbret, as it may confuse users.
                    // Just return immediately if callback returns non-zero.
                    if (cbret != 0) {
                        assert(ret.return_value != -1);
                        return ret;
                    }
                }
            }

            if (read_callback == nullptr) {
                assert(ret.return_value != -1);
                return ret; // no callback, don't loop, assume user is doing so
                            // instead.
            }
        };

        assert(ret.return_value != -1);
        return ret;
    }
    [[nodiscard]] auto last_error_string() const noexcept
        -> const std::string& {
        return m_slast_error;
    }
    [[nodiscard]] auto last_error() const noexcept -> int {
        return this->m_last_error;
    }
    [[nodiscard]] auto is_valid() const noexcept -> bool {
        return this->m_fd != xp::sock_handle_t::invalid;
    }
    [[nodiscard]] auto endpoint() const noexcept -> const endpoint_t& {
        return m_endpoint;
    }
    [[nodiscard]] auto context() const noexcept -> SocketContext* {
        return m_ctx;
    }
    [[nodiscard]] auto is_blocking() const noexcept -> bool {
        return m_blocking;
    }
    auto data() noexcept -> std::string& { return m_data; }

    private:
    sock_handle_t m_fd = xp::invalid_handle;
    string m_name;
    SocketContext* m_ctx{nullptr};
    string m_slast_error;
    int m_last_error = {0};
    CRTP* m_pcrtp{nullptr};
    auto crtp() -> CRTP* { return m_pcrtp; }
    endpoint_t m_endpoint;
    bool m_blocking{false};
    std::string m_data;
    xp::timepoint_t m_ms_create{0};
    sockstate_t m_state{sockstate_t::none};
    uint64_t m_id{0};
};

class Sock::Impl : public SocketBase<Sock> {
    // friend class ConnectingSocket;
    // friend class SocketBase;

    public:
    Impl(Sock* sck, string_view name, const endpoint_t& ep,
        SocketContext* ctx = nullptr)
        : SocketBase(sck, name, ctx, ep){

        };

    Impl(xp::sock_handle_t s, Sock* sck, string_view name, const endpoint_t& ep,
        SocketContext* ctx = nullptr)
        : SocketBase(s, sck, name, ctx, ep){

        };

    ~Impl() override = default;

    private:
};

// Constructor connected with our Impl structure
Sock::Sock(string_view name, const endpoint_t& ep, SocketContext* ctx)
    : pimpl(new Impl(this, name, ep, ctx)) {
    assert(pimpl->fd() != xp::invalid_handle);
}

Sock::Sock(xp::sock_handle_t a, string_view name, const endpoint_t& ep,
    SocketContext* ctx)
    : pimpl(new Impl(a, this, name, ep, ctx)) {
    assert(pimpl->fd() != xp::invalid_handle);
}
Sock::~Sock() {
    delete pimpl;
    pimpl = nullptr;
}

auto Sock::underlying_socket() const noexcept -> sock_handle_t {
    return pimpl->fd();
}

auto Sock::send(std::string_view data) noexcept -> xp::ioresult_t {
    return pimpl->send(data);
}

auto Sock::read(std::string& data, read_callback_t* read_callback,
    xp::msec_timeout_t timeout) noexcept -> xp::ioresult_t {
    return pimpl->read(data, read_callback, timeout);
}

auto Sock::last_error_string() const noexcept -> const string& {
    return pimpl->last_error_string();
}

auto Sock::last_error() const noexcept -> int {
    return pimpl->last_error();
}

auto Sock::is_valid() const noexcept -> bool {
    return pimpl->is_valid();
}

auto Sock::name() const noexcept -> string_view {
    return pimpl->name();
}

auto Sock::endpoint() const noexcept -> const xp::endpoint_t& {
    return pimpl->endpoint();
}

auto Sock::data() noexcept -> string& {
    return pimpl->data();
}

auto Sock::ms_alive() const noexcept -> xp::duration_t {
    return pimpl->ms_alive();
}

ConnectingSocket::ConnectingSocket(std::string_view name,
    const xp::endpoint_t& connect_where, xp::msec_timeout_t timeout,
    SocketContext* ctx)
    : Sock(name, connect_where, ctx) {
    auto conn = xp::sock_connect(this->pimpl->fd(), connect_where, timeout);
    if (conn != 0) {
        throw std::runtime_error(
            xp::concat("Unable to connect to: ", to_string(connect_where),
                socket_error(), ":", socket_error_string()));
    }
}

ConnectingSocket::~ConnectingSocket() {
    puts("Connecting socket destructor called");
}

void SocketContext::on_idle(Sock* ptr) noexcept {
    assert(ptr);
    auto pa = dynamic_cast<AcceptedSocket*>(ptr);
    static timepoint_t last_accept_time = xp::system_current_time_millis();
    const duration_t accept_interval{1000};
    static auto constexpr max_concurrency = 200;

    if (pa != nullptr) {
        auto server = pa->server();

        if (server != nullptr) {
            if (!server->is_blocking()) {
                if (server->m_nactive_accepts >= max_concurrency) {
                    // avoid stack overflow??
                    xp::sleep(1);
                    return;
                }
                auto dur = xp::duration(
                    xp::system_current_time_millis(), last_accept_time);
// on MAC, without this, recv keeps returning -1 and would_block
// for an insane amount of time (some seconds) when we recurse.
// I can only guess he's pissed off with hitting ::accept() too
// much?
#ifdef __apple__
                xp::sleep(1);
                return;
#endif

                if (server->perform_internal_accept(this, this->debug_info)) {
                    last_accept_time = xp::system_current_time_millis();
                    if (this->debug_info) {
                        puts("*****************on_idle caused another socket "
                             "to be "
                             "::accept()ed****************\n\n");
                    }
                } else {
                    xp::sleep(1);
                }
            }
        } else {
            xp::sleep(1);
        }
    } else {
        xp::sleep(1);
    }
}

void SocketContext::on_start(Sock* s) const noexcept {

    auto& ep = s->endpoint();
    printf("Server running, listening on %s\n", to_string(ep).c_str());
}

void SocketContext::sleep(long ms) noexcept {
    xp::sleep(ms);
}

static inline auto get_client_endpoint(struct sockaddr_in addr_in)
    -> xp::endpoint_t {
    std::array<char, INET6_ADDRSTRLEN + 1> client_ip = {};
    const auto ntop_ret = ::inet_ntop(
        AF_INET, &addr_in.sin_addr, client_ip.data(), INET6_ADDRSTRLEN + 1);

    assert(ntop_ret != nullptr);
    if (ntop_ret == nullptr) {
        throw std::runtime_error("ntop_ret failed. Bad address?");
    }
    int client_port = ntohs(addr_in.sin_port);
    xp::endpoint_t ep{client_ip.data(), (uint32_t)client_port};
    return ep;
}

auto ServerSocket::accept(xp::endpoint_t& client_endpoint, bool debug_info)
    -> xp::sock_handle_t {
    struct sockaddr_in addr_in = {};
    const auto fd = xp::to_native(this->underlying_socket());
    xp::socklen_t addrlen = sizeof(addr_in);
    const auto acc = ::accept(fd, (struct sockaddr*)&addr_in, &addrlen);

    if (acc == -1) {
        const auto e = socket_error();

        if (e == to_int(xp::errors_t::WOULD_BLOCK) || e == EAGAIN) {
            return xp::sock_handle_t::invalid;
        }
        {
            if (debug_info) {
                fprintf(stderr, "Weird Accept error: %d:%s", xp::socket_error(),
                    xp::socket_error_string().c_str());
                fprintf(stderr, "\n");
            }
            return xp::invalid_handle;
        }
    } else {
        // accepted!
        client_endpoint = get_client_endpoint(addr_in);
        if (debug_info) {
            printf("ServerSocket '%s' asked to accept a client\n",
                this->name().data());

            printf("client ip: %s ::accept()ed\n",
                xp::to_string(client_endpoint).c_str());
        }
        return static_cast<xp::sock_handle_t>(acc);
    }

    return xp::sock_handle_t::invalid;
}

auto ServerSocket::do_accept(xp::endpoint_t& client_endpoint,
    bool debug_info) noexcept -> AcceptedSocket* {
    xp::sock_handle_t s = accept(client_endpoint, debug_info);
    if (s == xp::sock_handle_t::invalid) {
        return nullptr;
    }
    {
        auto a = new AcceptedSocket(s, client_endpoint,
            xp::concat(
                "Server client, ", name(), " on ", to_string(client_endpoint)),
            this, pimpl->context());
        a->id_set(next_id());
        a->pimpl->id_set(a->id());
        return a;
    }
}

// return < 0 to disconnect the client
auto ServerSocket::on_after_accept_new_client(
    SocketContext* ctx, AcceptedSocket* a, bool debug_info) noexcept -> int {

    (void)ctx;
    xp::stopwatch write_timer("writing response: ", !ctx->debug_info);
    std::string hdr(xp::simple_http_response);
    auto find = std::string_view{"xxxx"};
    hdr.replace(
        hdr.find(find), find.length(), std::to_string(a->data().size()));

    auto sent = a->send(hdr);
    if (debug_info) {
        printf("sent returned(when sending header) : %d, and "
               "bytes_transferred=%zu\n",
            sent.return_value, sent.bytes_transferred);
    }
    if (sent.return_value != 0) {
        printf("send failed for fd: %d, with id %lld\n",
            static_cast<int>(a->pimpl->fd()), a->pimpl->id());
    }
    assert(sent.return_value == 0 && sent.bytes_transferred == hdr.length());
    sent = a->send(a->data());
    if (debug_info) {
        printf("sent returned(when sending data) : %d, and "
               "bytes_transferred=%zu\n",
            sent.return_value, sent.bytes_transferred);
    }
    return -1; // to signify client socket should go away now
}

// return true if an accept took place
auto ServerSocket::perform_internal_accept(
    SocketContext* ctx, bool debug_info) noexcept -> bool {
    bool retval = false;
    assert(ctx);
    xp::endpoint_t client_endpoint{};
    AcceptedSocket* a = do_accept(client_endpoint, debug_info);
    if (a != nullptr) {
        m_nactive_accepts++;
        if (m_nactive_accepts > m_npeak_active_accepts) {
            m_npeak_active_accepts = m_nactive_accepts;
        }

        m_naccepts++;
        retval = true;
        int should_accept = on_new_client(a);

        if (should_accept == 0) {
            const auto oaa = on_after_accept_new_client(ctx, a, debug_info);
            assert(oaa == -1);
            if (oaa < 0) {
                remove_client(
                    a, concat("on_after_accept returned: ", oaa).c_str());
            }
        } else {
            if (debug_info) {
                printf("Server %s rejected client %s because should_accept "
                       "returned %d\n",
                    this->name().data(), to_string(client_endpoint).data(),
                    should_accept);
            }
            remove_client(a,
                concat("should_accept returned a value: ", should_accept)
                    .c_str());
            retval = false;
        }
        m_nactive_accepts--;
    }

    return retval;
}

void SocketContext::run(ServerSocket* server) {
    assert(server);
    assert(!server->is_blocking());
    this->on_start(server);

    while (this->should_run) {
        if (!server->perform_internal_accept(this, this->debug_info)) {
            auto ctx = server->pimpl->context();
            if (ctx != nullptr) {
                ctx->on_idle(server); // on_idle calls sleep if he can
            }
        }
    };
}

ServerSocket::ServerSocket(
    std::string_view name, const endpoint_t& listen_where, SocketContext* ctx)
    : Sock(name, listen_where, ctx) {
    int on = 1;
    auto rc = ::setsockopt(to_native(this->underlying_socket()), SOL_SOCKET,
        SO_REUSEADDR, (char*)&on, sizeof(on));
    if (rc < 0) {
        throw std::runtime_error(
            concat("Server cannot set required socket properties:\n ",
                to_string(listen_where), xp::socket_error(), ":",
                xp::socket_error_string()));
    }
}

auto ServerSocket::listen() -> int {
    int rc = 0;
    struct sockaddr_in serverSa = {};
    serverSa.sin_family = AF_INET;
    serverSa.sin_addr.s_addr = htonl(INADDR_ANY);
    const auto& ep = this->pimpl->endpoint();
    if (ep.address.empty()) {
        int x = inet_pton(AF_INET, ep.address.data(), &(serverSa.sin_addr));
        if (x != 1) {
            throw std::runtime_error(
                concat("Unable to resolve server address: ",
                    to_string(pimpl->endpoint()), xp::socket_error(), ":",
                    xp::socket_error_string()));
        }
    }
    serverSa.sin_port = htons(this->pimpl->endpoint().port);
    const auto sock = to_native(this->pimpl->fd());
    rc = ::bind(sock, (struct sockaddr*)&serverSa, sizeof(serverSa));
    if (rc < 0) {
        throw std::runtime_error(
            concat("Unable to bind to: ", to_string(pimpl->endpoint()), " ",
                xp::socket_error(), ": ", xp::socket_error_string()));
    }
    // if this is set low, ab complains about: apr_socket_recv: Connection reset
    // by peer (104)
    const auto max_conn = SOMAXCONN;
    rc = ::listen(sock, max_conn);
    if (rc < 0) {
        throw std::runtime_error(
            concat("Unable to listen on: ", to_string(pimpl->endpoint()),
                xp::socket_error(), ":", xp::socket_error_string()));
    }

    auto ctx = this->pimpl->context();
    if (ctx != nullptr) {
        ctx->run(this);
    }
    return 0;
}

auto ServerSocket::is_blocking() const noexcept -> bool {
    return this->pimpl->is_blocking();
}

// return 0 on success
auto ServerSocket::on_new_client(AcceptedSocket* a) -> int {
    // m_clients.push_back(a);
    if (!add_client(a)) {
        // too many clients!
        fprintf(stderr, "too many clients! The maximum is: %zu\n",
            this->max_clients());
        return xp::TOO_MANY_CLIENTS;
    }
    auto ctx = this->pimpl->context();
    (void)ctx;
    size_t found = std::string::npos;
    const bool debug
        = (this->pimpl->context() != nullptr) && pimpl->context()->debug_info;
    static constexpr int timeout_ms = 20'000;
    stopwatch sw("", !debug);
    if (debug) {
        const auto astr = a->to_string();
        sw.id_set(astr);
    }

    auto attempts = 0;
    bool warned = false;

    while (found == std::string::npos) {

        attempts++;
        if (sw.elapsed_ms() >= timeout_ms) {
            return to_int(xp::errors_t::TIMED_OUT);
        }
        auto myread = a->read(a->data(), nullptr);

        auto retval = myread.return_value;
        if (myread.bytes_transferred > 0) {
            // printf("have data: %s\n", a->data().c_str());
            found = a->data().find("\r\n\r\n");
        }

        // return this first if it happens,
        // as you won't be able to do any more with this socket,
        // even if you read what you wanted.
        if (retval == 0) {
            fprintf(stderr,
                "After %d attempts (%d ms), client: remote disconnected during "
                "read:\n%s\nhaving read: %zu bytes\n",
                attempts, (int)sw.elapsed_ms(), a->to_string().c_str(),
                a->data().length());
            return to_int(errors_t::NOT_CONN);
        }
        if (retval < 0) {
            // some other error:
            retval = -retval;

            if (retval == to_int(errors_t::WOULD_BLOCK)) {
                continue;
            }
            if (retval == ETIMEDOUT || retval == to_int(xp::errors_t::TIMED_OUT)
                || retval == to_int(xp::errors_t::CONN_ABORTED)
                || retval == ECONNABORTED) {
                // if (ctx->debug_info) {
                fprintf(stderr,
                    "Got an expected error code in on_new_client (from "
                    "read) "
                    "%d:%s\n",
                    retval, xp::socket_error_string(retval).c_str());
                //}
                assert("checkme" == nullptr);
                return -1; // make sure he's disconnected
            }
            fprintf(stderr,
                "Unknown error code in on_new_client (from read) %d:%s\n",
                retval, xp::socket_error_string(retval).c_str());
            assert("checkme" == nullptr);
            return retval;
        }

        if (found != std::string::npos) {
            static const uint64_t REASONABLE_TIME = 1000;
            if (!warned && sw.elapsed_ms() > REASONABLE_TIME) {
                warned = true;
                fprintf(stderr, "Took ages for: %s\n", a->to_string().c_str());
            }
            return 0;
        }
    }; // while (found != std::string::npos)
    // how are we getting here?
    return -1;
}

auto ServerSocket::remove_client(
    AcceptedSocket* client_to_remove, const char* why) -> bool {

    const auto server = client_to_remove->server();
    const auto ctx = server->pimpl->context();
    bool debug = false;
    if (ctx != nullptr) {
        debug = ctx->debug_info;
    }
    auto it = std::remove_if(
        m_clients.begin(), m_clients.end(), [&](const AcceptedSocket* client) {
            auto b = client_to_remove == client;
            if (b) {
                if (debug) {

                    printf("removing client, then calling shutdown, with fd:%d "
                           "and id "
                           "%llu\n",
                        static_cast<int>(client_to_remove->pimpl->fd()),
                        client_to_remove->id());
                    printf("reason %s\n", why);
                }
                client->pimpl->shutdown(xp::shutdown_how::TX, why);
                if (b) {
                    client = nullptr;
                }
            }
            return b;
        });
    bool ret = it != m_clients.end();
    assert(ret);

    if (it != m_clients.end()) {
        m_clients.erase(it);
    }

    auto s = client_to_remove->server();

    if (s != nullptr) {

        if (debug) {
            printf("\n\nCurrent number of connected clients: %d\n",
                (int)m_clients.size());
            printf("Peak number of connected clients: %zd, at timestamp %llu\n",
                this->peak_num_clients(), to_int(this->peaked_when()));
            const auto ms_ago = xp::duration(
                xp::system_current_time_millis(), this->peaked_when());
            printf(
                "This was: %lld seconds ago\n", to_int(ms_ago) / xp::ms_in_sec);
        }
        delete client_to_remove;
        if (debug) {

            printf("Current number of clients: %zu\n", m_clients.size());
            if (m_clients.empty()) {
                printf(
                    "Longest alive socket: %s\n", longest_alive_data().c_str());
                printf("\n");
            }
        }
    }

    return ret;
}

AcceptedSocket::AcceptedSocket(xp::sock_handle_t s,
    const endpoint_t& remote_endpoint, std::string_view name,
    ServerSocket* pserver, SocketContext* ctx)
    : Sock(s, name, remote_endpoint, ctx), m_pserver(pserver) {}

auto xp::AcceptedSocket::id() const noexcept -> uint64_t {
    return pimpl->id();
}

void xp::AcceptedSocket::id_set(uint64_t newid) noexcept {
    pimpl->id_set(newid);
}

auto xp::AcceptedSocket::to_string() const noexcept -> std::string {
    const auto ret = concat("fd: ", to_int(this->underlying_socket()),
        " id: ", this->id(), " endpoint: ", xp::to_string(this->endpoint()),
        " ms_alive: ", xp::to_int(this->ms_alive()));
    return ret;
}

#ifdef _WIN32
auto xp::clock_gettime(int unused, timespec_t* spec) -> int {
    (void)unused;
    __int64 wintime;
    GetSystemTimeAsFileTime((FILETIME*)&wintime);
    static constexpr int64_t seconds = 10000000I64;
    static constexpr int64_t HUNDRED = 100;
    static constexpr int64_t DATE_MAGIC
        = 116444736000000000I64; // 1jan1601 to 1jan1970;
    wintime -= DATE_MAGIC;
    spec->tv_sec = wintime / seconds; // seconds
    spec->tv_nsec = wintime % seconds * HUNDRED; // nano-seconds
    return 0;
}
#endif
