// simple_http_server.cpp
#include "xpsockets.hpp"
#include <fstream>

// very dumb and insecure file server.
// If you base anything on this, you need to restrict
// file requests to a jailed folder, for a start!
// This can read any file, anywhere !!!
// (A browser may not permit this, but any other client may be
// used to hack you)
class fileserver : public xp::ServerSocket {
    private:
    static void send_404(xp::Sock* client) {
        client->send(xp::simple_http_response_no_cl_404);
    }

    static void send_file(xp::Sock* client, std::string_view filepath) {
#ifndef _WIN32
#define _getcwd getcwd
#endif
        char cwd_buf[512] = {};
        _getcwd(cwd_buf, 512);
        bool want_ico = false;
        (void)(want_ico);
        if (filepath.find("favicon") != std::string::npos) {
            want_ico = true;
        }
        printf("server looking for file %s,\nrelative to: %s\n\n",
            filepath.data(), cwd_buf);
        std::ifstream f(filepath.data(), std::ios::binary);
        bool sent_header = false;
        size_t total_sent = 0;
        bool is_icon = filepath.find(".ico") != std::string::npos;
        if (!f) {
            send_404(client);
        } else {
            char buf[1024] = {};
            while (!f.eof()) {
                f.read(buf, 1024);
                auto bytes_read = (size_t)f.gcount();
                std::string_view sv(buf, bytes_read);
                if (!sent_header) {
                    if (is_icon) {
                        auto hsent
                            = client->send(xp::simple_http_response_no_cl_404);
                        assert(hsent.bytes_transferred > 0);
                    } else {
                        auto hsent
                            = client->send(xp::simple_http_response_no_cl);
                        assert(hsent.bytes_transferred > 0);
                    }

                    sent_header = true;
                }
                auto sent = client->send(sv);
                total_sent += sent.bytes_transferred;
                printf("Total bytes sent to client: %ld\n", (long)total_sent);
                if (sent.bytes_transferred != bytes_read) {
                    assert("you may want to handle this" == nullptr);
                    return;
                }
                client->send("\r\n\r\n");
            }
        }
    }

    public:
    fileserver()
        : xp::ServerSocket("simple_http_server",
            xp::endpoint_t{"0.0.0.0", xp::port_type{8080}}) {}

    protected:
    int on_idle() noexcept override { return 0; }
    virtual bool on_got_request(
        Sock* client, std::string_view request) override {
        // be a totally insecure, dumb file server
        const auto found = request.find("\r\n");
        if (found == std::string::npos) {
            send_404(client);
        }
        const auto first_line = request.substr(0, found);
        const auto verb = request.substr(0, 3);
        if (verb == std::string_view("GET")) {
            const auto slash_pos = first_line.find_first_of('/');
            if (slash_pos == std::string::npos) return false;

            const auto where_space_after_slash
                = first_line.find_first_of(' ', slash_pos);
            const auto file = first_line.substr(
                slash_pos, where_space_after_slash - slash_pos);
            std::string sfile(file);
            if (sfile == "/") {
                sfile = "index.html";
            } else {
                sfile = sfile.substr(1);
            }
            send_file(client, sfile);
            return true;
        }
        return false;
    }
};

inline void run_file_server() {
    fileserver fs;
    fs.listen();
}
