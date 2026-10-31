#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
using json = nlohmann::json;
#define log_errorf(...) ((void)0)

namespace rtc {
struct Description {
    enum class Type {Offer, Answer};
    Description(const std::string&, Type) {}
    explicit operator std::string() const { return "sdp"; }
};
struct Candidate { explicit Candidate(const std::string&) {} };
struct PeerConnection {
    enum class State {New, Closed, Failed};
    static inline int failure = 0;
    State current = State::New;
    State state() const { return current; }
    void close() { current = State::Closed; }
    void setLocalDescription() { if (failure == 2) throw std::runtime_error("SDP failed"); }
    void setRemoteDescription(const Description&) { setLocalDescription(); }
    std::optional<Description> localDescription() {
        if (failure == 1) return {};
        return Description("sdp", Description::Type::Offer);
    }
    void addRemoteCandidate(const Candidate&) {}
};
}
struct Client {
    std::string id, screen_owner;
    uint64_t screen_generation = 0;
    std::shared_ptr<rtc::PeerConnection> pc;
    std::chrono::steady_clock::time_point start_time, last_pong;
    bool answer_received = false;
    std::vector<std::string> pending_candidates;
};
struct Touch {
    std::vector<std::string> released;
    void disconnected(const std::string& id) { released.push_back(id); }
} screen_touch;
static std::mutex g_clients_mutex;
static std::list<std::shared_ptr<Client>> g_clients;
static uint64_t g_client_counter = 0;
static int g_max_clients = 2;
#include "protocol-under-test.inc"

static json open(const std::string& owner, uint64_t generation, const std::string& type = "request") {
    // HTTP JSON parsing produces unsigned positive integers, as on the actual server.
    return handle_request(json::parse(json{{"type", type}, {"screen_owner", owner},
        {"screen_generation", generation}, {"sdp", "offer"}}.dump()));
}
static void close(const std::string& id) {
    assert(handle_request({{"type", "close"}, {"id", id}}) == json({{"type", "ok"}}));
}
int main() {
    const std::string a(32, 'a'), b(32, 'b'), c(32, 'c'), d(32, 'd');
    auto a1 = open(a, 1).at("id").get<std::string>();
    auto b1 = open(b, 1).at("id").get<std::string>();
    assert(client_count() == 2);
    assert(open(c, 1).at("error") == "max clients reached");
    assert(open(a, 1).at("error") == "stale screen request");
    auto old_a = find_client(a1);
    auto a2 = open(a, 2).at("id").get<std::string>();
    assert(client_count() == 2 && find_client(b1) && !find_client(a1));
    assert(old_a->pc->state() == rtc::PeerConnection::State::Closed);
    assert(screen_touch.released.back() == a1);
    assert(open(a, 1).at("error") == "stale screen request");
    close(a1); assert(find_client(a2)); // Late old-page cleanup must not close its replacement.
    close(a2); close(a2); assert(client_count() == 1 && find_client(b1));
    auto c1 = open(c, 1).at("id").get<std::string>();
    find_client(b1)->pc->close();
    auto d1 = open(d, 1).at("id").get<std::string>();
    assert(client_count() == 2 && !find_client(b1) && find_client(c1));
    close(d1);
    rtc::PeerConnection::failure = 1;
    assert(open(a, 3).at("error") == "failed to create offer");
    assert(client_count() == 1);
    assert(open(a, 4, "offer").at("error") == "failed to create answer");
    assert(client_count() == 1);
    rtc::PeerConnection::failure = 2;
    try { open(a, 5); assert(false); } catch (const std::runtime_error&) {}
    assert(client_count() == 1);
    rtc::PeerConnection::failure = 0;
    auto a6 = open(a, 6, "offer").at("id").get<std::string>();
    assert(client_count() == 2 && find_client(a6) && find_client(c1));
    assert(handle_request({{"type", "request"}, {"screen_owner", a}}).contains("error"));
    assert(open(a, UINT64_MAX).contains("error"));
    assert(find_client(a6) && client_count() == 2);
    close(a6); close(c1); assert(client_count() == 0);
    g_client_counter = 0; // A restarted server must not reuse an old page's session id.
    auto a7 = open(a, 7).at("id").get<std::string>();
    assert(a7 != a1 && a7 != a6);
    close(a1); close(a6); assert(find_client(a7));
    close(a7); assert(client_count() == 0);
    std::cout << "Session replacement, stale generations, independent viewers, close, cleanup and SDP failure checks passed\n";
}
