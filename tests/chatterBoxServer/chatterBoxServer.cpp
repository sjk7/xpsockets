#include <iostream>
#include <cassert>
#include "../../xpsockets.hpp"
#include "../../http.hpp"

using namespace std;

enum class chatclient_state { error = -1, connecting = 0, connected = 1 };

struct chatclient {
    xp::Sock* psock = nullptr;
    chatclient_state state = chatclient_state::connecting;
    std::string displayname;
    std::string& data() const noexcept {
        static std::string def;
        if (!psock) {
            std::cerr << "Whoops! chatclient has no sock!" << std::endl;
            return def;
        } else {
            return psock->data();
        }
    }
};

struct myserver : xp::ServerSocket {
    myserver(
        const xp::endpoint_t& listen_where, xp::SocketContext* ctx = nullptr)
        : xp::ServerSocket("chatserver", listen_where, ctx) {

        std::string_view pants{" Pants "};
        strings::trimSV(pants);
        assert(std::string(pants) == "Pants");

        // this->blocking_set(false); // blocking is ok now we poll
        printf("Server listening on port: %d\n", (int)listen_where.port);
        this->listen();
    }

    ~myserver() override { puts("server closed"); }

    chatclient* find_client(xp::Sock* psock) {
        auto it = std::find_if(m_chatclients.begin(), m_chatclients.end(),
            [&](auto& c) { return c.psock == psock; });

        assert(it != m_chatclients.end());
        return &(*it);
    }

    bool auth_client(chatclient& client) {
        if (client.data().find("1942khz.net") != std::string::npos) {
            client.state = chatclient_state::connected;
            return true;
        }
        return false;
    }

    virtual bool on_got_request(
        Sock* client, std::string_view request, bool& keep_client) override {

        auto header = http::header(request, true);
        assert(header.is_valid());
        auto header_agn = http::header(client->data());
        assert(header_agn.is_valid());
        auto& connection_header = header.field_by_id("Connection");
        assert(!connection_header.value.empty());

        if (client) {
            assert(client->data() == request);
            auto& myclient = add_chat_client(client);
            if (auth_client(myclient)) {
                keep_client = true;
                return true;
            }
        }
        keep_client = false;
        return false;
    }

    virtual int on_after_accept_new_client(
        xp::SocketContext*, xp::AcceptedSocket* a, bool) noexcept override {

        auto pclient = find_client((xp::Sock*)a);
        assert(pclient);
        if (pclient) return 0;
        return -1;
    }

    chatclient& add_chat_client(xp::Sock* sck) {
        return m_chatclients.emplace_back(
            chatclient{sck, chatclient_state::connecting, ""});
    }
    bool remove_chat_client(const chatclient& c) {

        auto& v = m_chatclients;
        bool found = false;
        auto it = v.erase(std::remove_if(v.begin(), v.end(),
                              [&](auto& client) {
                                  bool ret = &c == &client;
                                  found = ret;
                                  return found;
                              }),
            v.end());

        return found;
    }
    std::vector<chatclient> m_chatclients;
};

void ding() {
    // NB: Does not work unless you are IN an actual console (doesnt work inside
    // QtCreator)
    std::cout << "\a";
}

int main() {
    ding();
    myserver server(xp::endpoint_t{"0.0.0.0", xp::to_port(8090)});
    return 0;
}
