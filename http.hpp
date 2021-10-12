#pragma once
// HTTP_HPP

#include <vector>
#include "strings.hpp"
#include <string_view>
#include <cassert>

namespace http {

struct header;
struct name_value_pair_type {
    std::string name;
    std::string value;
};

struct request_line_type {
    friend struct header;

    private:
    std::string m_method; // GET
    std::string m_path; // /index.html
    std::string m_protocol; // HTTP/1.0
    std::vector<std::string_view> m_request_line_components{
        m_method, m_path, m_protocol};

    public:
    const std::string& method() const noexcept { return m_method; }
    const std::string& path() const noexcept { return m_path; }
    const std::string& protocol() const noexcept { return m_protocol; }

    const auto& request_line_components() const noexcept {
        return m_request_line_components;
    }

    void clear() {
        m_method.clear();
        m_path.clear();
        m_protocol.clear();
        m_last_error.clear();
    }

    bool parse(std::string_view data) {
        clear();
        auto splut_space = strings::splitSV(data, strings::SPACE);
        if (splut_space.size() != 3) {
            m_last_error
                = "request line does not consist of 3 components, as expected";
            return false;
        }
        m_method = splut_space[0];
        m_path = splut_space[1];
        m_protocol = splut_space[2];

        m_request_line_components[0] = m_method;
        m_request_line_components[1] = m_path;
        m_request_line_components[2] = m_protocol;

        return true;
    }

    private:
    std::string m_last_error;
};

struct header {
    header(std::string_view data, bool have_dbl_newline_already = false) {
        parse(data, have_dbl_newline_already);
    }

    bool parse(std::string_view data, bool have_dbl_newline_already = false) {
        clear();
        auto nlfound = have_dbl_newline_already
            ? data.size()
            : data.find(strings::DBLNEWLINE);
        if (nlfound == std::string::npos) {
            m_last_error = "Double newline not found";
            m_is_valid = false;
            return false;
        }

        std::string_view header_data{data};
        if (nlfound != data.size()) {
            auto splutbydblnewline
                = strings::splitSV(data, strings::DBLNEWLINE);
            if (splutbydblnewline.empty()) {
                m_is_valid = false;
                m_last_error = "no double newline found";
            } else {
                header_data = splutbydblnewline[0];
                if (splutbydblnewline.size() > 1) {
                    this->m_data_after_header = splutbydblnewline[1];
                }
            }
        }

        if (header_data.empty()) {
            m_last_error = "header data empty";
            m_is_valid = false;
        } else {
            auto splutnewline = strings::splitSV(header_data, strings::NEWLINE);
            if (splutnewline.empty()) {

                splutnewline
                    = strings::splitSV(header_data, strings::UNIX_NEWLINE);
                if (splutnewline.size() > 1) {
                    m_last_error = "Possibly bad header, no proper new lines "
                                   "found, only unix newlines";
                } else {
                    m_last_error = "No new lines in header";
                    m_is_valid = false;
                }
            } else {
                bool ok = this->m_request_line.parse(splutnewline[0]);
                if (!ok) {
                    m_last_error
                        = "Unable to parse request line into 3 components";
                    m_is_valid = false;
                } else {
                    m_is_valid = true;
                    add_fields(splutnewline);
                }
            }
        }

        return m_is_valid;
    }

    bool is_valid() const noexcept { return m_is_valid; }
    const std::string& last_error() const noexcept { return m_last_error; }
    const request_line_type& request_line() const noexcept {
        return m_request_line;
    }
    const std::string& data_after_header() const noexcept {
        return m_data_after_header;
    }
    const auto& fields() const noexcept { return m_fields; }

    const auto& field_by_id(std::string_view id) const {
        auto& v = m_fields;
        static name_value_pair_type nvp = name_value_pair_type{};
        auto it = std::find_if(
            v.begin(), v.end(), [&](auto& f) { return f.name == id; });
        if (it != v.end()) {
            return *it;
        }
        return nvp;
    }

    private:
    request_line_type m_request_line;
    std::vector<name_value_pair_type> m_fields;
    std::string m_data_after_header;
    bool m_is_valid = false;
    std::string m_last_error;
    void add_fields(const std::vector<std::string_view>& v) {
        m_fields.clear();
        if (v.size() == 0) return;

        m_fields.reserve(v.size() - 1);
        int i = 0;

        for (const auto& s : v) {
            if (++i > 0) {
                if (s.find_first_of(strings::COLON) != std::string::npos) {
                    auto splutcolon = strings::splitSV(s, strings::COLON);
                    if (splutcolon.size() == 2) {
                        strings::trimSV(splutcolon[0]);
                        strings::trimSV(splutcolon[1]);
                        m_fields.emplace_back(
                            name_value_pair_type{std::string{splutcolon[0]},
                                std::string{splutcolon[1]}});
                    }
                }
            }
        }
    }
    void clear() {
        this->m_fields.clear();
        this->m_data_after_header.clear();
        this->m_last_error.clear();
        this->m_request_line.clear();
        this->m_is_valid = false;
    }
};

} // namespace http
