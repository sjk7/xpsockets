// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it. PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// http://www.viva64.com
#pragma once

#ifndef XPSOCKETS_H
#define XPSOCKETS_H

#include "xpcommon.h"

// Private implementation file for xpsockets.
// Do not include this in your project, it is a library file only
#ifndef _WIN32
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#else

#define VC_EXTRALEAN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <WinSock2.h>
#include <Windows.h>
#include <ws2tcpip.h>
#include <mmsystem.h>
#undef NOMINMAX

#endif

#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winmm.lib")
#endif

namespace xp {

static constexpr auto ms_in_sec = 1000;

enum class errors_t : int32_t {
    NONE = 0,
#ifdef _WIN32
    TIMED_OUT = WSAETIMEDOUT,
    NOT_CONN = WSAENOTCONN,
    IS_CONN = WSAEISCONN,
    WOULD_BLOCK = WSAEWOULDBLOCK,
    CONN_ABORTED = WSAECONNABORTED,
    UNEXPECTED = -1000

#else
    TIMED_OUT = ETIMEDOUT,
    NOT_CONN = ENOTCONN,
    IS_CONN = EISCONN,
    WOULD_BLOCK = EAGAIN,
    CONN_ABORTED = ECONNABORTED,
    BAD_FILE_DESCRIPTOR = EBADF,
    UNEXPECTED = -1000
#endif

};

inline constexpr auto to_int(const errors_t e) noexcept {
    return static_cast<int32_t>(e);
}

inline constexpr auto error_can_continue(const errors_t e) noexcept {
    return e == errors_t::WOULD_BLOCK;
}

inline auto error_can_continue(const int64_t e) noexcept {
    int myint{(int)e};
    myint = abs(myint);
    const errors_t err{myint};
    return err == errors_t::WOULD_BLOCK;
}

#ifdef _WIN32
inline int init_sockets() noexcept {
    int iResult = 0;
    WSADATA wsaData = {};

    // Initialize Winsock
    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        printf("WSAStartup failed: %d\n", iResult);
        return 1;
    }
    return iResult;
}
#else
inline int init_sockets() {
    return 0;
}
#endif

#ifdef _WIN32
using socklen_t = int;
#else
using socklen_t = ::socklen_t;
#endif

inline auto socket_error() noexcept {
#ifdef _WIN32
    return WSAGetLastError();
#else
    if (errno == 9) {
        // puts("quoi?");
        // EBADF
    }
    return errno;
#endif
}

inline auto socket_error_string(int err = -1) -> std::string {
    if (err == to_int(errors_t::UNEXPECTED)) {
        return std::string("Unexpected error");
    }
#ifdef _WIN32
    if (err == -1) {
        err = WSAGetLastError();
    }
    char* s = nullptr;
    const size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER
            | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&s, 0,
        nullptr);

    if (s == nullptr) {
        fprintf(stderr, "Bad socket error string\n");
        return std::string();
    }
    std::string ret(s, size);
    LocalFree(s);
    return ret;
#else
    if (err == -1) err = errno;
    auto s = strerror(err);
    if (s != nullptr) {
        return strerror(err);
    }

    fprintf(stderr, "Bad socket error string\n");
    return std::string();

#endif
}

inline auto to_string(const errors_t e) -> std::string {
    return socket_error_string(to_int(e));
}
inline void sleep(int ms) noexcept {
#ifdef _WIN32
    ::Sleep(ms);
#else
    ::usleep(ms * 1000);
#endif
}

#ifdef _WIN32
using native_socket_type = SOCKET;
#else
using native_socket_type = int;
#endif

auto constexpr to_native(const xp::sock_handle_t h) noexcept {
    return static_cast<native_socket_type>(h);
}
[[maybe_unused]] static inline constexpr auto invalid_handle
    = xp::sock_handle_t::invalid;

#include <fcntl.h>

/** Returns true on success, or false if there was an error */
inline bool sock_set_blocking(native_socket_type fd, bool blocking) noexcept {
    if (native_socket_type{fd} < 0) { //-V547
        return false;
    }

#ifdef _WIN32
    unsigned long mode = blocking ? 0 : 1;
    return ioctlsocket(fd, FIONBIO, &mode) == 0;
#else
    int on = blocking ? 0 : 1;
    const auto rc = ioctl(fd, FIONBIO, (char*)&on) == 0;
    assert(rc == true);
    return rc;

#endif
}
struct addrinfo_wrapper {
    addrinfo_wrapper(const xp::endpoint_t& ep) : m_ep(ep) {
        if (resolve() != 0) {
            throw std::runtime_error(
                concat("Unable to resolve ", to_string(ep), '\n', m_serror));
        }
    }

    ~addrinfo_wrapper() {
        if (m_paddr != nullptr) {
            ::freeaddrinfo(m_paddr);
        }
    }
    addrinfo_wrapper(const addrinfo_wrapper&) = delete;
    auto operator=(const addrinfo_wrapper& rhs) -> addrinfo_wrapper& = delete;
    addrinfo m_hints = {};
    addrinfo* m_paddr = nullptr;
    int m_error{0};
    std::string m_serror;
    xp::endpoint_t m_ep{};

    int resolve() {
        m_hints.ai_family = PF_UNSPEC;
        m_hints.ai_socktype = SOCK_STREAM;

        // m_hints.sin_port = htons(PORT);
        // htons(PORT);
        m_error = getaddrinfo(m_ep.address.data(),
            xp::to_string(m_ep.port).c_str(), &m_hints, &m_paddr);
        if (m_error != 0) {
#ifdef _WIN32
            m_serror = gai_strerrorA(m_error);
#else
            m_serror = gai_strerror(m_error);
#endif
            return m_error;
        }
        return 0;
    }
};

inline int sock_close(sock_handle_t h) noexcept {
    int ret = 0;
    // printf("Closing socket ... %d\n", to_int(h));
    // stopwatch sw(concat("Closing socket ", to_int(h)).c_str());
#ifdef _WIN32

    ret = ::closesocket(static_cast<SOCKET>(h));
#else
    ret = ::close(static_cast<int>(h));
#endif
    return ret;
}

inline sock_handle_t sock_create(
    int dom = PF_INET, int ty = SOCK_STREAM, int proto = 0) noexcept {
    return sock_handle_t{::socket(dom, ty, proto)};
}
enum class shutdown_how { RX, TX, BOTH };

inline int sock_shutdown(native_socket_type s, shutdown_how how) noexcept {
    const int myhow = static_cast<int>(how);
    return ::shutdown(s, myhow);
}

inline int get_error_on_socket_handle(const xp::sock_handle_t s) noexcept {
    int ret = 0;
    xp::socklen_t retlen = sizeof(ret);
    const auto x = ::getsockopt(
        to_native(s), SOL_SOCKET, SO_ERROR, (char*)&ret, &retlen);
    std::ignore = x;
    assert(x == 0);
    return ret;
}

inline auto sock_connect(const xp::sock_handle_t sock, const xp::endpoint_t& ep,
    const xp::msec_timeout_t t = xp::msec_timeout_t::default_timeout) -> int {
    addrinfo_wrapper addr(ep);

    int took = 0;
    int ret = 0;
    while (took < to_int(t)) {
        ret = ::connect(to_native(sock), addr.m_paddr->ai_addr,
            (int)addr.m_paddr->ai_addrlen);

        if (ret == 0) {
            break;
        }

        const auto err = xp::socket_error();
        // printf("Connect err: %d\n", err);
        static constexpr const auto server_went_away = 22;
        if (err == server_went_away) {
            // I see this if the server goes away
            const auto e = get_error_on_socket_handle(sock);
            if (e) {
                fprintf(stderr, "Connect error ON SOCKET: %s\n",
                    xp::socket_error_string(err).c_str());
            }
            fprintf(stderr, "Connect error: %s\n",
                xp::socket_error_string(err).c_str());
            return err;
        }
        if (err == EISCONN || err == to_int(xp::errors_t::IS_CONN)) {
            ret = 0;
            break;
        }

        xp::sleep(1);
        ++took;
    }

    if (took >= to_int(t)) {
        ret = to_int(xp::errors_t::TIMED_OUT);
    } else {
        // printf("Connected to host in %d ms.\n", took);
    }
    if (ret != 0) {
        perror("connect");
    }
    return ret;
}
} // namespace xp

#endif
