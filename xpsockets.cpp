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

#ifdef _WIN32
static inline auto poll(struct pollfd* fds, ULONG nfds, INT timeout_ms) -> int {
    int ret = 0;
    ret = ::WSAPoll(fds, nfds, timeout_ms);
    return ret;
}
#endif
enum class pollevents_t {

    // The POLLOUT flag is defined as the same as the POLLWRNORM flag value.
    poll_write = POLLWRNORM,
    // The POLLIN flag is defined as the combination of POLLRDNORM and
    // POLLRDBAND flag values
    poll_read = POLLIN,
    // Priority data may be read without  blocking.This flag is not supported by
    // the Microsoft Winsock provider
    poll_pri = POLLPRI,
    // Priority band(out - of - band) data may be read without blocking.
    poll_rdband = POLLRDBAND,
    // Normal data may be read without blocking.
    poll_rd_norm = POLLRDNORM,
    poll_write_normal
    = POLLWRNORM // Normal data may be written without blocking.
};

static inline auto mypoll(const xp::native_socket_type fd,
    pollevents_t what_for = pollevents_t::poll_read,
    xp::msec_timeout_t t = xp::msec_timeout_t{1}) {

    auto find_events = short(what_for);
    short found_events = 0;
    struct pollfd pollfd = {fd, find_events, found_events};

    const auto n = poll(&pollfd, 1, to_int(t));
    if (n > 0) {
        if (pollfd.revents & POLLIN) {
            if (what_for == pollevents_t::poll_read) {
                return n;
            }
        }

        if (pollfd.revents & POLLOUT) {
            if (what_for == pollevents_t::poll_write) {
                return n;
            }
        }

        if (pollfd.revents & POLLHUP) {
            return -POLLHUP;
        }

        return -1;
        // assert(0); // got someting you were not expecting?
    }
    return n;
}

inline auto default_context_instance() -> SocketContext& {
    static SocketContext ctx;
    return ctx;
}
enum class sockstate_t { none, in_recv, in_send, about_to_close };

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

            static constexpr UINT TARGET_RESOLUTION
                = 1; // 1-millisecond target resolution

            TIMECAPS tc;
            UINT wTimerRes;

            if (timeGetDevCaps(&tc, sizeof(TIMECAPS)) != TIMERR_NOERROR) {
                fprintf(stderr,
                    "Cannot get timer resolution: not fatal, but "
                    "unexpected.\n");
            }

            wTimerRes = std::min(
                std::max(tc.wPeriodMin, TARGET_RESOLUTION), tc.wPeriodMax);
            timeBeginPeriod(wTimerRes);
        }
#endif

        blocking_set(true);

        // linger makes things very very slow in windows for lots of concurrent
        /*/
        struct linger sl = {};
        xp::socklen_t optlen = sizeof(sl);
        int ffs = getsockopt(
            to_native(this->m_fd), SOL_SOCKET, SO_LINGER, (char*)&sl, &optlen);
        assert(
            sl.l_linger == 0 && sl.l_onoff == 0); // at least the case in 'doze
        sl.l_onoff = 1;
        sl.l_linger = 1;

        ffs = setsockopt(
            to_native(m_fd), SOL_SOCKET, SO_LINGER, (const char*)&sl, optlen);
        assert(ffs == 0);
        /*/
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

        blocking_set(true);
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

    bool blocking_set(bool should_block) {
        const auto bl = sock_set_blocking(to_native(m_fd), should_block);
        if (!bl) {
            throw std::runtime_error(
                concat("Unable to set blocking mode, for: ", m_name, "\n",
                    to_string(m_endpoint), xp::socket_error(), ":",
                    xp::socket_error_string()));
        }
        m_blocking = should_block;
        return bl;
    }

    [[nodiscard]] auto id() const noexcept -> uint64_t { return m_id; }
    void id_set(uint64_t newid) noexcept { m_id = newid; }

    [[nodiscard]] auto state() const noexcept -> sockstate_t { return m_state; }

    [[nodiscard]] auto ms_alive() const noexcept -> xp::duration_t {
        return xp::duration(xp::system_current_time_millis(), m_ms_create);
    }

    auto close() -> int {

        assert(m_state == sockstate_t::none
            || m_state == sockstate_t::about_to_close); // why are we being
                                                        // closed whilst active?
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
                printf("called shutdown() on fd: %d , with id: %" PRIu64
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
        xp::ioresult_t retval{0, 0};
        stopwatch sw("TXTimer", true);

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

        while (remain > 0) {
            const auto sz = remain > 512 ? 512 : remain;
            const auto ready_ret = wait_for_ready(sw, this,
                pollevents_t::poll_write, "Waiting for client to send data");

            if (ready_ret != to_int(xp::errors_t::NONE)) {
                return retval;
            }

            int sent = ::send(sck, ptr, sz, MSG_NOSIGNAL);
            // puts("Blocking send complete\n");
            retval.bytes_transferred
                += sent >= 0 ? static_cast<size_t>(sent) : 0;
            retval.return_value = sent;

            if (sent < 0) {
                const int e = xp::socket_error();
                if (e == to_int(xp::errors_t::WOULD_BLOCK)) {
                    this->m_ctx->on_idle(this->crtp());
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

        // printf("Sent: %s\n", sv.data());
        assert(retval.bytes_transferred == sv.size());
        return retval;
    }

    [[nodiscard]] xp::native_socket_type native_handle() const noexcept {
        return static_cast<xp::native_socket_type>(m_fd);
    }

    template <typename SOCKBASE>
    static inline int wait_for_ready(xp::stopwatch& sw, SOCKBASE* psock,
        const pollevents_t events, const char* why,
        xp::duration_t timeout = xp::duration_t{20000}) {
        assert(psock);
        if (!psock->is_blocking()) {
            return 0;
        }

        int pollres = 0;

        while (pollres <= 0) {
            pollres = mypoll(psock->native_handle(), events);
            if (pollres > 0) {
                return to_int(xp::errors_t::NONE);
            }
            if (sw.elapsed() > timeout) {
                psock->m_last_error = to_int(xp::errors_t::TIMED_OUT);
                psock->m_slast_error
                    = concat("Timed out in wait_for_ready: ", why, " after ",
                        to_int(timeout), " ms.");
                return psock->m_last_error;
            }
            if (pollres & POLLHUP) {
                psock->m_slast_error = "Remote client hung up";
                psock->m_last_error = to_int(xp::errors_t::CONN_ABORTED);
                return -POLLHUP;
            }

            if (pollres < 0) {
                auto e = errno;
                if (e == EINTR) {
                    continue;
                }
                return pollres;
            }
            // nothing to read
            psock->on_idle();
        };
        return 0;
    }

    static constexpr auto CONTINGENCY = 4;
    static constexpr auto DEFAULT_BUFFER_SIZE = 8192;
    static constexpr auto BUFSIZE = DEFAULT_BUFFER_SIZE;

    int on_idle() noexcept {
        assert(m_ctx);
        return this->m_ctx->on_idle(crtp());
    }

    void prepare_tmp_buffer() {

        if (m_tmp.empty()) {
            // ok so this means a heap allocation once per client, but:
            // not using a stack buffer to avoid stack overflow during ab tests
            // and so on. (heavy traffic)
            m_tmp.resize(DEFAULT_BUFFER_SIZE + CONTINGENCY);
        }
    }

    ioresult_t handle_remote_closed(const ioresult_t& io, const char* why) {
        assert(why);
        ioresult_t ret{io.bytes_transferred, to_int(xp::errors_t::NOT_CONN)};
        ret.return_value = m_last_error = to_int(xp::errors_t::NOT_CONN);
        m_slast_error = concat(why, ' ', xp::socket_error_string(m_last_error));
        m_state = sockstate_t::about_to_close;
        this->blocking_set(false);
        close();
        return ret;
    }

    static inline xp::ioresult_t handle_successful_read(int bytes_read,
        std::string& data, const std::vector<char>& tmp,
        read_callback_t* read_callback) {
        xp::ioresult_t ret{};
        ret.return_value = to_int(xp::errors_t::NONE);
        data.append(tmp.data(), bytes_read);
        ret.bytes_transferred += static_cast<size_t>(bytes_read);

        if (read_callback != nullptr) {
            const auto cbret = read_callback->new_data(
                bytes_read, std::string_view{tmp.data(), size_t(bytes_read)});
            if (cbret != 0) {
                ret.return_value = cbret;
                return ret;
            }
        }
        return ret;
    }

    int prepare_read(xp::stopwatch& sw, const xp::msec_timeout_t timeout) {

        const auto poll_timeout
            = xp::duration_t{static_cast<xp::duration_t>(timeout)};
        const auto ready_ret = wait_for_ready(sw, this, pollevents_t::poll_read,
            "Waiting for client to send data", poll_timeout);

        if (ready_ret != to_int(xp::errors_t::NONE)) {
            if (ready_ret < 0) {
                if (ready_ret & POLLHUP) {
                    m_state = sockstate_t::none;
                    if (m_ctx && m_ctx->debug_info) {
                        fprintf(stderr, "Client hung up: %s\n",
                            to_string(this).c_str());
                    }
                    close();
                }
            }
            return ready_ret;
        }

        prepare_tmp_buffer();
        return 0; // noerror
    }

    int handle_failed_read(const xp::timepoint_t time_start,
        const xp::msec_timeout_t timeout) noexcept {
        int ret = 0;
        assert(!is_blocking());
        m_last_error = xp::socket_error();
        ret = -m_last_error;
        const auto dur
            = xp::duration(xp::system_current_time_millis(), time_start);
        if (static_cast<int>(to_int(dur)) > to_int(timeout)) {
            m_last_error = to_int(xp::errors_t::TIMED_OUT);
            m_slast_error = "Timed out in read() waiting for data";
            ret = -to_int(xp::errors_t::TIMED_OUT);
            assert(ret != -1);
            return ret;
        }
        if (m_last_error == to_int(xp::errors_t::WOULD_BLOCK)) {
            this->m_ctx->on_idle(crtp());
            ret = m_last_error;
            return ret;
        }
        m_slast_error = xp::concat("unexpected return value of: ", m_last_error,
            socket_error_string(m_last_error));
        assert(ret != -1);
        return ret;
    }

    auto read(std::string& data, read_callback_t* read_callback,
        xp::msec_timeout_t timeout) -> xp::ioresult_t {

        assert(this->m_fd != xp::invalid_handle);
        xp::ioresult_t ret{0, 0};
        stopwatch read_timer("RXTimer", true);

        sockstate_wrapper_t w(this->m_state, sockstate_t::in_recv);

        const auto time_start = xp::system_current_time_millis();
        while (true) {
            assert(m_fd != xp::invalid_handle
                && "Someone closed the socket during read()" != nullptr);

            ret.return_value = prepare_read(read_timer, timeout);
            if (ret.return_value != to_int(xp::errors_t::NONE)) {
                return ret;
            }
            int read = recv(xp::to_native(m_fd), m_tmp.data(), BUFSIZE, 0);

            if (read > 0) {
                ret = handle_successful_read(read, data, m_tmp, read_callback);
                if (ret.return_value < 0) {
                    m_state = sockstate_t::about_to_close;
                    close();
                    return ret;
                }
                if ((ret.return_value != 0) || (read_callback == nullptr)) {
                    return ret; // callback wants us to return.
                }
            }
            if (read == 0) {
                ret = handle_remote_closed(
                    ret, "Client closed socket during read()");
                // loop does not complain
                return ret;
            }

            if (read < 0) {
                ret.return_value = handle_failed_read(time_start, timeout);
                if (xp::error_can_continue(ret.return_value)) {
                    assert(!is_blocking());
                    continue;
                }
                if (ret.return_value < 0) {
                    return ret;
                }
            }
        };
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

bool xp::Sock::is_blocking() const noexcept {
    return pimpl->is_blocking();
}

bool xp::Sock::blocking_set(bool should_blck) {
    return pimpl->blocking_set(should_blck);
}

xp::sock_handle_t xp::Sock::fd() const noexcept {
    return pimpl->fd();
}

uint64_t xp::Sock::id() const noexcept {
    return pimpl->id();
}

void xp::Sock::id_set(uint64_t newid) noexcept {
    pimpl->id_set(newid);
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

    blocking_set(false);
    assert(!this->is_blocking());
    auto conn = xp::sock_connect(this->pimpl->fd(), connect_where, timeout);
    if (conn != 0) {
        throw std::runtime_error(
            xp::concat("Unable to connect to: ", xp::to_string(connect_where),
                " ", socket_error(), ":", socket_error_string()));
    }
}

ConnectingSocket::~ConnectingSocket() {
    if (pimpl->context() && pimpl->context()->debug_info) {
        puts("Connecting socket destructor called");
    }
}

int SocketContext::on_idle(Sock* ptr) noexcept {

    assert(ptr);
    auto pa = dynamic_cast<AcceptedSocket*>(ptr);

    static auto last_time = xp::system_current_time_millis();
    static uint64_t longest_interval{0};
    static constexpr uint64_t very_long_interval = 1000;
    static constexpr auto busy_accept_attempts = 10;

    const auto this_interval
        = to_int(xp::system_current_time_millis()) - to_int(last_time);
    if (this_interval > longest_interval) {
        longest_interval = this_interval;
        printf("Idle interval = %d ms.\n", (int)longest_interval);
        if (longest_interval > very_long_interval) {

            if (pa != nullptr && pa->server() != nullptr
                && pa->server()->m_nactive_accepts < busy_accept_attempts) {
                printf("Warn: long time between event loop being called: (%ld"
                       "ms). Try not to "
                       "block the thread\n",
                    (long)this_interval);
            }
        }
    }
    last_time = xp::system_current_time_millis();

    if (pa != nullptr) {
        auto server = pa->server();

        if (server != nullptr) {
            static auto constexpr max_concurrency = 100;
            if (server->m_nactive_accepts >= max_concurrency) {
                // avoid stack overflow??
                xp::sleep(1);
                return 0;
            }

            if (server->perform_internal_accept(this, this->debug_info)) {
                if (this->debug_info) {
                    puts("*****************on_idle caused another "
                         "socket "
                         "to be "
                         "::accept()ed****************\n\n");
                }
            } else {
                if (!server->is_blocking()) {
                    xp::sleep(1);
                }
            }

        } else {
            if (!ptr->is_blocking()) {
                xp::sleep(1);
            }
        }
    } else {
        if (ptr != nullptr && !ptr->is_blocking()) {
            xp::sleep(1);
        }
    }

    return 0;
}

void SocketContext::on_start(Sock* s) const noexcept {

    auto& ep = s->endpoint();
    std::cout << "Server running, listening on " << to_string(ep) << std::endl;
    return;
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

static inline auto native_accept(xp::native_socket_type fd, sockaddr_in& addr)
    -> xp::native_socket_type {

    const auto empty_ret = to_native(xp::invalid_handle);

    const auto n = mypoll(fd);
    if (n >= 1) {
        auto len = xp::socklen_t{sizeof(addr)};
        const auto accept_fd = ::accept(fd, (sockaddr*)&addr, &len);
        assert(accept_fd != to_native(xp::invalid_handle));
        return xp::native_socket_type(accept_fd);
    }
    return empty_ret;
}

auto ServerSocket::accept(xp::endpoint_t& client_endpoint, bool debug_info)
    -> xp::sock_handle_t {
    struct sockaddr_in addr_in = {};
    const auto fd = xp::to_native(this->underlying_socket());

    static constexpr auto ddos_threshold = 100;
    if (m_nactive_accepts > ddos_threshold) {
        xp::sleep(1); // anti-ddos
    }

    xp::native_socket_type acc;
    if (!this->is_blocking()) {
        xp::socklen_t addrlen = sizeof(addr_in);
        acc = ::accept(fd, (struct sockaddr*)&addr_in, &addrlen);

    } else {
        acc = native_accept(fd, addr_in);
    }

    if (acc == to_native(xp::invalid_handle)) {
        const auto e = socket_error();

        if (e == to_int(xp::errors_t::WOULD_BLOCK)
            || e == to_int(xp::errors_t::TIMED_OUT)
            || e == to_int(xp::errors_t::NONE)
            || e == to_int(xp::errors_t::NONE)) {
            if (pimpl->context()) {
                if (pimpl->on_idle() < 0) {
                    return xp::sock_handle_t::invalid;
                }
            }
            return xp::sock_handle_t::invalid;
        }
        assert("a non-accept should always lead to idle(), but somehow we "
               "missed it"
            == nullptr);
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

        get_client_endpoint(addr_in, client_endpoint);
        if (debug_info) {
            // printf("ServerSocket '%s' asked to accept a client\n",
            //    this->name().data());

            // printf("Allocated fd %ld to client ip: %s ::accept()ed\n",
            //    long(acc), xp::to_string(client_endpoint).c_str());
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
    try {
        auto a = new AcceptedSocket(s, client_endpoint,
            xp::concat("Server client, ", name(), " on ",
                xp::to_string(client_endpoint)),
            this, pimpl->context());
        a->id_set(next_id());
        a->pimpl->id_set(a->id());

        return a;
    } catch (const std::exception& e) {
        fprintf(stderr, "%s\n",
            xp::concat("Could not create new client socket: ",
                std::string_view(e.what()), "\n")
                .c_str());
        return nullptr;
    }
}

// return < 0 to disconnect the client
auto ServerSocket::on_after_accept_new_client(
    SocketContext* ctx, AcceptedSocket* a, bool debug_info) noexcept -> int {
    (void)ctx;
    (void)a;
    (void)debug_info;
    return -1; // to signify client socket should go away now
}

// return true if an accept took place
int ServerSocket::perform_internal_accept(
    SocketContext* ctx, bool debug_info) noexcept {
    int retval = false;
    assert(ctx);
    xp::endpoint_t client_endpoint{};
    AcceptedSocket* a = do_accept(client_endpoint, debug_info);
    if (a == nullptr) {
        if (this->pimpl->context()) {
            const auto idl = this->pimpl->on_idle();
            if (idl < 0) {
                return idl;
            }
        }
    }

    if (a != nullptr) {
        m_nactive_accepts++;
        if (m_nactive_accepts > m_npeak_active_accepts) {
            m_npeak_active_accepts = m_nactive_accepts;
            // printf("peak active accepts: %ld\n\n", m_npeak_active_accepts);
        }

        m_naccepts++;
        retval = 0;
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
                    this->name().data(), xp::to_string(client_endpoint).data(),
                    should_accept);
            }
            remove_client(a,
                concat("should_accept returned a value: ", should_accept)
                    .c_str());
            return should_accept;
        }
    }

    return retval;
}

void SocketContext::run(ServerSocket* server) {
    assert(server);
    this->m_should_run = true;
    this->on_start(server);

    while (this->m_should_run) {
        const auto rv = server->perform_internal_accept(this, this->debug_info);
        if (rv < 0) {
            printf(
                "Server exiting because perform_internal_accept returned %d\n",
                rv);
            return;
        }
        auto ctx = server->pimpl->context();
        if (ctx != nullptr) {
            if (ctx->on_idle(server) < 0) {
                // on_idle calls sleep if he can
                printf("When idle() returns < 0, server stops running\n");
                this->m_should_run = false;
            }
        }
    };
}

ServerSocket::ServerSocket(
    std::string_view name, const endpoint_t& listen_where, SocketContext* ctx)
    : Sock(name, listen_where, ctx) {

    //#ifndef __APPLE__
    // // blocking_set(
    //    false); // non-blocking mode is much faster, but does not work
    //    properly
    // on a single thread in macos (recv always returns E_WOULDBLOCK
    // for more than one concurrnet client)
    //#endif
    int on = 1;
    auto rc = ::setsockopt(to_native(this->underlying_socket()), SOL_SOCKET,
        SO_REUSEADDR, (char*)&on, sizeof(on));
    if (rc < 0) {
        throw std::runtime_error(
            concat("Server cannot set required socket properties:\n ",
                xp::to_string(listen_where), xp::socket_error(), ":",
                xp::socket_error_string()));
    }
}

static inline int check_max_conn(int system_max_conn) {
#ifdef __APPLE__
    FILE* fp = nullptr;
    char path[1035];

    /* Open the command for reading. */
    fp = popen("sysctl -a | grep somaxconn", "r");
    if (fp) {
        while (fgets(path, sizeof(path), fp) != NULL) {
            std::string_view sv(path);
            const auto found = sv.find_last_of(':');
            if (found != std::string::npos) {
                std::string_view sn = sv.data() + found + 2;
                std::string_view num = sn.substr(0, sv.size());
                path[strlen(path) - 1] = '\0';

                int n = std::stoi(num.data());
                if (n > 0) {
                    return n;
                }
            }
        }
        pclose(fp);
    }
#endif
    return system_max_conn;
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
                    xp::to_string(pimpl->endpoint()), xp::socket_error(), ":",
                    xp::socket_error_string()));
        }
    }
    serverSa.sin_port = htons(this->pimpl->endpoint().port);
    const auto sock = to_native(this->pimpl->fd());
    rc = ::bind(sock, (struct sockaddr*)&serverSa, sizeof(serverSa));
    if (rc < 0) {
        throw std::runtime_error(
            concat("Unable to bind to: ", xp::to_string(pimpl->endpoint()), " ",
                xp::socket_error(), ": ", xp::socket_error_string()));
    }

    static constexpr auto LOWMAXCONN = 128;
    // if this is set low, ab complains about: apr_socket_recv: Connection reset
    // by peer (104)
    int system_max_conn = SOMAXCONN;
    if (SOMAXCONN > 0 && SOMAXCONN <= LOWMAXCONN) {
        system_max_conn = check_max_conn(system_max_conn);
    }

    if (system_max_conn <= LOWMAXCONN && system_max_conn > 0) {
        fprintf(stderr,
            "Warn: Some Oses have a low maximum of concurrent "
            "connections,\n"
            "and it looks like yours is one of them.\nYour server may be "
            "easily flooded. Max = %d\n",
            (int)system_max_conn);
#ifdef __APPLE__
        fprintf(stderr,
            "Try this on Mac BigSur and above: sudo sysctl "
            "kern.ipc.somaxconn=64000\n\n");
#endif
    }
    static constexpr int reasonable_max = 20000;
    auto actual_max
        = system_max_conn > reasonable_max ? reasonable_max : system_max_conn;
#ifdef _WIN32
    actual_max = SOMAXCONN;
#endif

    rc = ::listen(sock,
        actual_max); // not system_max_conn: on mac it polls forever
    if (rc < 0) {
        throw std::runtime_error(
            concat("Unable to listen on: ", xp::to_string(pimpl->endpoint()),
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

    stopwatch sw("", !debug);
    if (debug) {
        const auto astr = to_string(a->pimpl);
        sw.id_set(astr);
    }

    xp::duration_t dur{duration_t::default_timeout_duration};
    xp::msec_timeout_t t{to_int(dur)};
    bool good = false;
    const auto hdr = std::string_view{xp::simple_http_response_no_cl};

    while (found == std::string::npos) {

        auto myread
            = a->read_until(t, a->data(), [&](auto& bytes_read, auto& mydata) {
                  assert(bytes_read); // should only return to us when data
                                      // ready, or fail with timeout
                  (void)bytes_read;
                  (void)mydata;
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
            return 0;
        }
        if (myread.return_value == 0) {
            this->m_nclients_disconnected_during_read++;
            return 0;
        }
        if (!xp::error_can_continue(myread.return_value)) {
            return myread.return_value;
        }

        if (sw.elapsed() > dur) {
            return to_int(xp::errors_t::TIMED_OUT);
        }
    }

    assert(0); // dunno how we got here!
    return to_int(xp::errors_t::UNEXPECTED);
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
                if (client->pimpl->is_valid()) {

                    client->pimpl->blocking_set(false);
                    client->pimpl->shutdown(xp::shutdown_how::TX, why);
                    client->pimpl->close();
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
