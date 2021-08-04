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
#include <tuple> // std::ignore

#ifdef _MSC_VER
#pragma warning(disable : 26467)
#endif

namespace xp {

#include <sys/timeb.h>

static constexpr auto TOO_MANY_CLIENTS = -1000;
static constexpr auto MAX_CLIENTS = 100001;

enum class timepoint_t : uint64_t { zero = 0 };
enum class duration_t : uint64_t {
    one_second = 1000,
    one_minute = one_second * 60,
    one_hour = one_minute * 60,
    one_day = one_hour * 24,
    five_seconds = one_second * 5,
    twenty_seconds = one_second * 20,
    default_timeout_duration = twenty_seconds
};

// no, we are not using chrono due to a) bloat and b) it's buggy in Windows if
// the clock changes.
inline constexpr uint64_t to_int(timepoint_t t) noexcept {
    return static_cast<uint64_t>(t);
}
inline constexpr uint64_t to_int(const duration_t d) noexcept {
    return static_cast<uint64_t>(d);
}

inline constexpr timepoint_t from_int(uint64_t i) noexcept {
    return timepoint_t{i};
}

inline duration_t duration(const timepoint_t a, const timepoint_t b) noexcept {
    const auto mya = to_int(a);
    const auto myb = to_int(b);
    return duration_t{mya - myb};
}

duration_t since(const timepoint_t& when) noexcept;

#ifdef _WIN32
struct xptimespec_t {
    uint64_t tv_sec;
    uint64_t tv_nsec;
};

int clock_gettime(int dummy, xptimespec_t* spec) noexcept;
#endif

inline timepoint_t system_current_time_millis() noexcept {

#if defined(_WIN32) || defined(_WIN64)
    struct xptimespec_t _t = {};
    const auto iret = clock_gettime(0, &_t);
    std::ignore = iret;
    assert(iret == 0);
    static constexpr auto THOUSAND = 1000;
    static constexpr auto MILLION = 1.0e6;
    uint64_t retval = _t.tv_sec * THOUSAND + ::lround(_t.tv_nsec / MILLION);
    return timepoint_t{retval};

#else
    struct timespec _t;
    int iret = clock_gettime(CLOCK_MONOTONIC, &_t);
    static bool shown_error = false;
    if (iret) {
        if (!shown_error) {
            fprintf(
                stderr, "Unexpected fatal error from clock_gettime %d\n", iret);
            shown_error = true;
        }
    }
    uint64_t retval = _t.tv_sec * 1000 + ::lround(_t.tv_nsec / 1.0e6);
    assert(iret == 0);
    return timepoint_t{retval};
#endif
}

inline timepoint_t now() noexcept {
    return system_current_time_millis();
}

inline bool operator>(const duration_t a, const duration_t b) noexcept {
    return to_int(a) > to_int(b);
}

class stopwatch {
    public:
    stopwatch(const char* id, bool silent = false)
        : m_sid(id == nullptr ? "" : id)
        , m_start(system_current_time_millis())
        , m_end(system_current_time_millis())
        , m_silent(silent) {}
    ~stopwatch() noexcept {
        m_end = xp::system_current_time_millis();
        show();
    }
    void start(const char* newid = nullptr) {
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
            printf("%s took: %ld ms.\n", m_sid.c_str(),
                (long)to_int(duration(m_end, m_start)));
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
    one_second = 1000UL,
    five_seconds = one_second * 5UL,
    ten_seconds = one_second * 10UL,
    thirty_seconds = one_second * 30UL,
    one_minute = one_second * 60UL,
    two_minutes = one_minute * 2UL,
    five_minutes = one_minute * 5,
    ten_minutes = one_minute * 10,
    default_timeout = ten_seconds
};

inline constexpr int to_int(sock_handle_t s) noexcept {
    return static_cast<int>(s);
}
inline constexpr int to_int(const msec_timeout_t t) noexcept {
    return static_cast<int>(t);
}
enum class port_type : uint32_t {
    ftp = 21,
    ssh = 22,
    http = 50,
    http_proxy = 8080,
    testing_port = 4321
};
using port_int_type = typename std::underlying_type<port_type>::type;
struct endpoint_t {
    std::string address;
    port_type port;
};
inline std::string to_string(const port_type& p) {
    return std::to_string(static_cast<uint32_t>(p));
}

inline port_int_type to_int(const port_type& port) noexcept {
    return static_cast<uint32_t>(port);
}

inline constexpr port_type to_port(const uint32_t p) noexcept {
    return port_type{p};
}

inline std::ostream& operator<<(std::ostream& os, const port_type& port) {
    os << static_cast<uint32_t>(port);
    return os;
}

struct ioresult_t {
    size_t bytes_transferred;
    // this is really a socket error, but I wanted to avoid user having to
    // #include<Winsock.h>
    int64_t return_value;
};

inline std::string to_string(const endpoint_t& ep) {
    std::stringstream ss;
    ss << ep.address << ':' << ep.port;
    return std::string(ss.str());
}

template <typename... Args> inline std::string concat(Args&&... args) {
    std::stringstream ss;
    (ss << ... << args);
    return std::string(ss.str());
}

std::string to_display_time(const xp::duration_t& dur);

} // namespace xp
#endif // XPCOMMON_H
