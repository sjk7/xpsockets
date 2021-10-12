#include <iostream>
#include <cassert>
#include "../../xpsockets.hpp"

using namespace std;

enum class chatclient_state { error = -1, connecting = 0, connected = 1 };

struct chatclient {
    xp::Sock* psock = nullptr;
    chatclient_state state = chatclient_state::connecting;
    std::string displayname;
    std::string data;
};

struct myserver : xp::ServerSocket {
    myserver(
        const xp::endpoint_t& listen_where, xp::SocketContext* ctx = nullptr)
        : xp::ServerSocket("chatserver", listen_where, ctx) {

        this->blocking_set(false);
        printf("Server listening on port: %d\n", (int)listen_where.port);
        this->listen();
    }

    ~myserver() override { puts("server closed"); }

    int auth_client(chatclient& client) {
        if (client.data.find("1942khz.net")) return 0;
        return -1;
    }

    int on_after_accept_new_client(
        xp::SocketContext*, xp::AcceptedSocket* a, bool) noexcept override {
        if (!a) return -1;
        xp::Sock* client_ptr = a;

        if (client_ptr) {

            auto& myclient = add_chat_client(client_ptr);
            auto io = a->read_until(xp::msec_timeout_t::thirty_seconds,
                myclient.data,
                [&](int read_result, const std::string_view sockdata) {
                    (void)read_result;
                    myclient.data = sockdata;
                    int ret = auth_client(myclient);
                    return ret;
                });
            if (io.return_value < 0) {
                remove_chat_client(myclient);
                return -1;
            }
        } else {
            return -1;
        }

        return 0;
    }

    chatclient& add_chat_client(xp::Sock* sck) {
        return m_chatclients.emplace_back(
            chatclient{sck, chatclient_state::connecting, "", ""});
    }
    bool remove_chat_client(const chatclient& c) {

        auto it = m_chatclients.begin();
        for (const auto& client : m_chatclients) {
            if (&client == &c) {
                m_chatclients.erase(it);
                return true;
            }
            ++it;
        }
        return false;
    }
    std::vector<chatclient> m_chatclients;
};

void ding() {
    // NB: Does not work unless you are IN an actual console (doesnt work inside
    // QtCreator)
    std::cout << "\a";
}

int main() {

    myserver server(xp::endpoint_t{"0.0.0.0", xp::to_port(8090)});
    while (server.is_listening()) {
        usleep(100);
    }
    return 0;
}
