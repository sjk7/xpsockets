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

#ifndef _WIN32
#include <poll.h>
#endif

#ifdef _MSC_VER
#ifdef assert
#undef assert
#define assert _ASSERTE // makes assert()s resumable in VS
#endif
#endif

using namespace std;
using namespace xp;

static inline auto mypoll(const xp::native_socket_type fd) {
    short find_events = POLLIN;
    short found_events = 0;
    struct pollfd pollfds[1] = {{fd, find_events, found_events}};

    const auto n = poll(pollfds, 1, 10);

    return n;
}

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
        const auto bl = sock_set_blocking(to_native(m_fd), true);
        m_blocking = true;
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

        const auto bl = sock_set_blocking(to_native(m_fd), true);
        m_blocking = true;
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
                printf("called shutdown() on fd: %d , with id %" PRIu64
                       "becoz %s\n",
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
            retval.return_value = sent;

            if (sent < 0) {
                const int e = xp::socket_error();
                if (e == to_int(xp::errors_t::WOULD_BLOCK)) {
                    this->m_ctx->on_idle(crtp());
                } else {
                    this->m_last_error = e;
                    this->m_slast_error = xp::concat(
                        "error in send(): ", xp::socket_error_string(e));
                    fprintf(stderr, "send error: %s\n", m_slast_error.c_str());
                    retval.return_value = e;
                    return retval;
                }

            } else {
                ptr += static_cast<std::ptrdiff_t>(sent);
            }
            remain -= sz;
        };

        assert(retval.bytes_transferred == sv.size());
        return retval;
    }

    static constexpr auto CONTINGENCY = 4;
    static constexpr auto DEFAULT_BUFFER_SIZE = 8192;
    static constexpr auto BUFSIZE = DEFAULT_BUFFER_SIZE;

    auto read(std::string& data, read_callback_t* read_callback,
        xp::msec_timeout_t timeout) -> xp::ioresult_t {

        xp::ioresult_t ret{0, 0};
        sockstate_wrapper_t w(this->m_state, sockstate_t::in_recv);
        int pollres = 0;
        auto polldur = xp::duration_t{timeout};
        stopwatch sw("read timer", true);

        if (this->is_blocking()) {
            while (pollres <= 0) {
                pollres = mypoll(to_native(this->m_fd));
                if (pollres < 0) {
                    auto e = errno;
                    if (e == EINTR) {
                        continue;
                    }
                }
                if (pollres <= 0) {
                    // nothing to read
                    m_ctx->on_idle(crtp());
                }
                if (sw.elapsed() >= polldur) {
                    ret.return_value = to_int(xp::errors_t::TIMED_OUT);
                    return ret;
                }
            };
        }

        if (m_tmp.empty()) {
            // ok so this means a heap allocation once per client, but:
            // not using a stack buffer to avoid stack overflow during ab tests
            // and so on. (heavy traffic)
            m_tmp.resize(DEFAULT_BUFFER_SIZE + CONTINGENCY);
        }

        auto sck = xp::to_native(m_fd);

        const auto time_start = xp::system_current_time_millis();
        assert(this->m_fd != xp::invalid_handle);
        auto minus1err = 0;

        while (true) {

            // WHO is closing us when we are in recv, ffs?
            assert(this->m_fd != xp::invalid_handle);
            int read = recv(sck, m_tmp.data(), BUFSIZE, 0);

            // grab the errors straight away coz of concurrency
            if (read < 0) {
                minus1err = xp::socket_error();
                assert(minus1err);
                ret.return_value = minus1err;
                // m_last_error = xp::socket_error();
                // assert(m_last_error); // why? it returned -1
            }
            if (read == 0) {
                ret.return_value = to_int(xp::errors_t::NOT_CONN);
                m_last_error = to_int(xp::errors_t::NOT_CONN);
                m_slast_error = concat("Client closed socket during read(): ",
                    xp::socket_error_string(m_last_error));
                // callers should watch for this: it means the remote end was
                // closed during recv
                assert(ret.return_value != -1);
                return ret;
            }

            if (read < 0) {
                assert(minus1err);
                m_last_error = minus1err;
                const auto dur = xp::duration(
                    xp::system_current_time_millis(), time_start);
                if (static_cast<int>(to_int(dur)) > to_int(timeout)) {
                    m_last_error = to_int(xp::errors_t::TIMED_OUT);
                    m_slast_error = "Timed out in read() waiting for data";
                    ret.return_value = to_int(xp::errors_t::TIMED_OUT);
                    ret.bytes_transferred = 0;
                    assert(ret.return_value != -1);
                    return ret;
                }
                if (m_last_error == to_int(xp::errors_t::WOULD_BLOCK)) {
                    // m_slast_error = xp::socket_error_string();
                    // ^^ save some cpu, we prolly don't need to do this as
                    // failure is almost guaranteed: it's how sockets work!
                    this->m_ctx->on_idle(crtp());
                    ret.return_value = m_last_error;
                    return ret;
                }
                m_slast_error
                    = xp::concat("unexpected return value of: ", m_last_error,
                        socket_error_string(m_last_error));
                assert(ret.return_value != -1);
                return ret;
            }

            if (read > 0) {
                ret.return_value = 0;
                // printf("Got data: %s\n", m_tmp.data());
                const auto old_size = data.size();
                data.resize(data.size() + static_cast<size_t>(read));
                memcpy(data.data() + old_size, m_tmp.data(),
                    static_cast<size_t>(read));
                // printf("Got cumulative data: %s\n", data.c_str());
                ret.bytes_transferred += static_cast<size_t>(read);
                assert(ret.return_value != -1);
            }
            if (read_callback != nullptr) {
                const auto cbret = read_callback->new_data(
                    read, std::string_view{m_tmp.data(), size_t(read)});
                if (cbret != 0) {
                    ret.return_value = cbret;
                    return ret;
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
    std::vector<char> m_tmp;
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

    static auto constexpr max_concurrency = 500;

    if (pa != nullptr) {
        auto server = pa->server();

        if (server != nullptr) {
            if (true) {
                if (server->m_nactive_accepts >= max_concurrency) {
                    // avoid stack overflow??
                    xp::sleep(1);
                    return;
                }

                if (server->perform_internal_accept(this, this->debug_info)) {
                    if (this->debug_info) {
                        puts("*****************on_idle caused another "
                             "socket "
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

static inline void get_client_endpoint(
    struct sockaddr_in addr_in, xp::endpoint_t& result) {
    std::array<char, INET6_ADDRSTRLEN + 1> client_ip = {};
    const auto ntop_ret = ::inet_ntop(
        AF_INET, &addr_in.sin_addr, client_ip.data(), INET6_ADDRSTRLEN + 1);

    assert(ntop_ret != nullptr);
    if (ntop_ret == nullptr) {
        throw std::runtime_error("ntop_ret failed. Bad address?");
    }
    int client_port = ntohs(addr_in.sin_port);
    result.address = client_ip.data();
    result.port = (uint32_t)client_port;
    return;
}

#ifdef _WIN32
auto poll() -> int {
    int ret = 0;
    WSAPOLLFD fds[1] = {};

    // ret = ::WSAPoll(fds, nfds, 1);
    return -1;
}
#endif

static inline auto native_accept(xp::native_socket_type fd, sockaddr_in& addr)
    -> xp::native_socket_type {

    const auto empty_ret = to_native(xp::invalid_handle);

    const auto n = mypoll(fd);
    if (n >= 1) {
        auto len = xp::socklen_t{sizeof(addr)};
        const auto accept_fd = accept(fd, (sockaddr*)&addr, &len);
        assert(accept_fd != to_native(xp::invalid_handle));
        return xp::native_socket_type(accept_fd);
    }
    return empty_ret;
}

auto ServerSocket::accept(xp::endpoint_t& client_endpoint, bool debug_info)
    -> xp::sock_handle_t {
    struct sockaddr_in addr_in = {};
    const auto fd = xp::to_native(this->underlying_socket());

    if (m_nactive_accepts > 100) {
        xp::sleep(10); // anti-ddos
    }
    const auto acc = native_accept(fd, addr_in);
    // xp::socklen_t addrlen = sizeof(addr_in);
    // const auto acc = ::accept(fd, (struct sockaddr*)&addr_in, &addrlen);
    if (acc == to_native(xp::invalid_handle)) {
        const auto e = socket_error();

        if (e == to_int(xp::errors_t::WOULD_BLOCK)
            || e == to_int(xp::errors_t::TIMED_OUT)
            || e == to_int(xp::errors_t::NONE)) {
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
        endpoint_t ep;
        get_client_endpoint(addr_in, ep);
        if (debug_info) {
            printf("ServerSocket '%s' asked to accept a client\n",
                this->name().data());

            printf("Allocated fd %ld to client ip: %s ::accept()ed\n",
                long(acc), xp::to_string(client_endpoint).c_str());
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
            // printf("peak active accepts: %ld\n\n", m_npeak_active_accepts);
        }

        m_naccepts++;
        retval = true;
        int should_accept = on_new_client(a);
        m_nactive_accepts--;

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
    }

    return retval;
}

void SocketContext::run(ServerSocket* server) {
    assert(server);
    // assert(!server->is_blocking());
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
    rc = ::listen(sock, SOMAXCONN);
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
    xp::msec_timeout_t t = xp::msec_timeout_t::default_timeout;
    xp::duration_t dur{30000};
    bool good = false;
    const auto hdr = std::string_view{xp::simple_http_response_no_cl};

    while (found == std::string::npos) {

        auto myread
            = a->read2(t, a->data(), [&](auto& bytes_read, auto& mydata) {
                  assert(bytes_read); // should only return to us when data
                                      // ready, or fail with timeout

                  const auto found = a->data().find("\r\n\r\n");
                  if (found != std::string::npos) {
                      good = true;
                      return 1;
                  }
                  sw.restart();

                  return 0;
              });
        if (good) {
            auto sent = a->send(hdr);
            if (sent.bytes_transferred != hdr.size()) {
                fprintf(stderr,
                    "****Did not send all header data: sent = %d, serr =%s\n",
                    sent.return_value, a->last_error_string().c_str());
            }

            sent = a->send(a->data());
            if (sent.bytes_transferred != a->data().size()) {
                fprintf(stderr,
                    "****Did not send all body data: sent = %d, serr =%s\n",
                    sent.return_value, a->last_error_string().c_str());
            }
            return -1;
        }
        if (myread.return_value == 0) {
            this->m_nclients_disconnected_during_read++;
            return -1;
        }
        if (!xp::error_can_continue(myread.return_value)) {
            return myread.return_value;
        }

        if (sw.elapsed() > dur) {
            return to_int(xp::errors_t::TIMED_OUT);
        }
    }

    assert(0); // dunno how we got here!

    /*/
    while (found == std::string::npos) {

        attempts++;
        if (sw.elapsed_ms() >= timeout_ms) {
            return to_int(xp::errors_t::TIMED_OUT);
        }

        auto retval = myread.return_value;
        if (myread.bytes_transferred > 0) {
            printf("have data: %s\n", a->data().c_str());
            found = a->data().find("\r\n\r\n");
            if (found == std::string::npos) {
                found = a->data().find("\n\n");
            }
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
            fprintf(stderr,
                "Note: current active/pending accepts: %" PRIu32
                ", max_concurrent clients: %" PRIu32
                ", current clients: %zu and peak "
                "clients: %zu\n",
                this->m_nactive_accepts, this->m_npeak_active_accepts,
                m_clients.size(), this->m_peak_clients);

            m_nclients_disconnected_during_read++;
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

                return -1; // make sure he's disconnected
            }
            fprintf(stderr,
                "Unknown error code in on_new_client (from read) %d:%s\n",
                retval, xp::socket_error_string(retval).c_str());
            assert("checkme" == nullptr);
            return retval;
        }
        static const uint64_t REASONABLE_TIME = 1000;
        if (!warned) {
            if (sw.elapsed_ms() > REASONABLE_TIME) {

                warned = true;
                fprintf(stderr, "Took ages for: %s\n", a->to_string().c_str());
                fprintf(stderr, "When took ages, client buffer has:%s\n",
                    a->data().c_str());
            }
        }
        if (found != std::string::npos) {
            if (warned) {
                fprintf(stderr, "Slow client accepted, finally: %s\n",
                    a->to_string().c_str());
                fprintf(stderr, "\n");
            }
            return 0;
        }
    }; // while (found != std::string::npos)
    // how are we getting here?
    return -1;
    /*/
}

auto ServerSocket::remove_client(
    AcceptedSocket* client_to_remove, const char* why) -> bool {

    const auto ctx = this->pimpl->context();
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
                           "%" PRIu64 "\n",
                        static_cast<int>(client_to_remove->pimpl->fd()),
                        client_to_remove->id());
                    printf("reason %s\n", why);
                }
                client->pimpl->shutdown(xp::shutdown_how::TX, why);
                client->pimpl->close();

                if (b) {
                    client = nullptr;
                }
            }
            return true;
        });
    bool ret = it != m_clients.end();
    assert(ret);

    if (it != m_clients.end()) {
        m_clients.erase(it);
    }

    if (debug) {
        printf("\n\nCurrent number of connected clients: %d\n",
            (int)m_clients.size());
        printf("Peak number of connected clients: %zd,"
               "\n",
            this->peak_num_clients());
        const auto ms_ago = xp::duration(
            xp::system_current_time_millis(), this->peaked_when());
        printf("Peak of clients was: %" PRIu64 " seconds ago\n",
            to_int(ms_ago) / xp::ms_in_sec);
    }
    delete client_to_remove;
    if (debug) {

        printf("Current number of clients: %zu\n", m_clients.size());
        if (m_clients.empty()) {
            printf("Longest alive socket: %s\n", longest_alive_data().c_str());
            printf("\n");
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

static auto constexpr exp7 = 10000000I64;
static auto constexpr exp9 = 1000000000I64; // 1E+9
static auto constexpr w2ux = 116444736000000000I64; // 1.jan1601 to 1.jan1970
static auto constexpr HUNDRED = 100;
void unix_time(struct xp::xptimespec_t* spec) {
    __int64 wintime;
    GetSystemTimeAsFileTime((FILETIME*)&wintime);
    wintime -= w2ux;
    spec->tv_sec = wintime / exp7;
    spec->tv_nsec = wintime % exp7 * HUNDRED;
}

auto xp::clock_gettime(int dummy, xptimespec_t* spec) -> int {
    (void)dummy;
    static struct xptimespec_t startspec;
    static double ticks2nano = 0;
    static __int64 startticks = 0;
    static __int64 tps = -1;
    __int64 tmp = 0;
    __int64 curticks = 0;
    QueryPerformanceFrequency((LARGE_INTEGER*)&tmp); // some strange system can
    if (tps != tmp) {
        tps = tmp; // init ~~ONCE         //possibly change freq ?
        QueryPerformanceCounter((LARGE_INTEGER*)&startticks);
        unix_time(&startspec);
        ticks2nano = (double)exp9 / tps;
    }
    QueryPerformanceCounter((LARGE_INTEGER*)&curticks);
    curticks -= startticks;
    spec->tv_sec = startspec.tv_sec + (curticks / tps);
    spec->tv_nsec = static_cast<uint64_t>(
        startspec.tv_nsec + (double)(curticks % tps) * ticks2nano);

    if (!(spec->tv_nsec < exp9)) {
        spec->tv_sec++;
        spec->tv_nsec -= exp9;
    }
    return 0;
}

#endif
