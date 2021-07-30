// This is an independent project of an individual developer. Dear PVS-Studio,
// please check it. PVS-Studio Static Code Analyzer for C, C++, C#, and Java:
// http://www.viva64.com
#pragma once

#ifndef XPCOMMON_H

#include <cstdint>
#include <sstream>
#include <string>
#include <cassert>
#include <cmath>
#define __STDC_FORMAT_MACROS
#include <cinttypes>

namespace xp {

#include <sys/timeb.h>

static constexpr auto TOO_MANY_CLIENTS = -1000;
static constexpr auto MAX_CLIENTS = 100001;

enum class timepoint_t : uint64_t {};
enum class duration_t : uint64_t {};

// no, we are not using chrono due to a) bloat and b) it's buggy in Windows if
// the clock changes.
inline auto to_int(timepoint_t t) -> uint64_t {
    return static_cast<uint64_t>(t);
}
inline auto to_int(const duration_t d) -> uint64_t {
    return static_cast<uint64_t>(d);
}

inline auto from_int(uint64_t i) -> timepoint_t {
    return timepoint_t{i};
}

inline auto duration(const timepoint_t a, const timepoint_t b) -> duration_t {
    const auto mya = to_int(a);
    const auto myb = to_int(b);
    return duration_t{mya - myb};
}

#ifdef _WIN32
struct xptimespec_t {
    uint64_t tv_sec;
    uint64_t tv_nsec;
};

auto clock_gettime(int dummy, xptimespec_t* spec) -> int;
#endif

inline auto system_current_time_millis() -> timepoint_t {

#if defined(_WIN32) || defined(_WIN64)
    struct xptimespec_t _t = {};
    const auto iret = clock_gettime(0, &_t);
    assert(iret == 0);
    static constexpr auto THOUSAND = 1000;
    static constexpr auto MILLION = 1.0e6;
    uint64_t retval = _t.tv_sec * THOUSAND + ::lround(_t.tv_nsec / MILLION);
    return timepoint_t{retval};

#else
    struct timespec _t;
    int iret = clock_gettime(CLOCK_MONOTONIC, &_t);
    uint64_t retval = _t.tv_sec * 1000 + ::lround(_t.tv_nsec / 1.0e6);
    assert(iret == 0);
    return timepoint_t{retval};
#endif
}

inline bool operator>(const duration_t a, const duration_t b) {
    return to_int(a) > to_int(b);
}

class stopwatch {
    public:
    stopwatch(const char* id, bool silent = false) noexcept
        : m_sid(id == nullptr ? "" : id)
        , m_start(system_current_time_millis())
        , m_end(system_current_time_millis())
        , m_silent(silent) {}
    ~stopwatch() noexcept {
        m_end = xp::system_current_time_millis();
        show();
    }
    void start(const char* newid = nullptr) noexcept {
        if (newid != nullptr) {
            m_sid = newid;
        }

        m_start = xp::system_current_time_millis();
    }
    void stop() noexcept {
        m_end = xp::system_current_time_millis();
        show();
    }
    void show() noexcept {
        if (!m_silent) {
            printf("%s took%" PRIu64 " ms.\n", m_sid.c_str(),
                to_int(duration(m_end, m_start)));
        }
        m_silent = true;
    }

    void restart() noexcept {
        m_start = xp::system_current_time_millis();
        m_end = m_start;
    }

    [[nodiscard]] auto elapsed() const noexcept -> duration_t {
        return xp::duration(xp::system_current_time_millis(), this->m_start);
    }

    [[nodiscard]] auto elapsed_ms() const noexcept -> uint64_t {
        return to_int(elapsed());
    }

    void id_set(std::string_view sid) { m_sid = sid; }

    private:
    std::string m_sid;
    timepoint_t m_start;
    timepoint_t m_end;
    bool m_silent{false};
};

#ifdef _WIN32

enum class sock_handle_t : unsigned long long {
    invalid = static_cast<unsigned long long>(-1)
};
#else
enum class sock_handle_t : int { invalid = -1 };
#endif
enum class msec_timeout_t : uint64_t {
    infinite = (uint64_t)-1,
    default_timeout = 10000
};

inline auto to_int(sock_handle_t s) -> int {
    return static_cast<int>(s);
}
inline auto to_int(const msec_timeout_t t) -> int {
    return static_cast<int>(t);
}
struct endpoint_t {
    std::string address;
    uint32_t port;
};

struct ioresult_t {
    size_t bytes_transferred;
    // this is really a socket error, but I wanted to avoid user having to
    // #include<Winsock.h>
    int32_t return_value;
};

inline auto to_string(const endpoint_t& ep) -> std::string {
    std::stringstream ss;
    ss << ep.address << ':' << ep.port;
    return std::string(ss.str());
}

template <typename... Args> inline auto concat(Args&&... args) -> std::string {
    std::stringstream ss;
    (ss << ... << args);
    return std::string(ss.str());
}
} // namespace xp
#endif // XPCOMMON_H
