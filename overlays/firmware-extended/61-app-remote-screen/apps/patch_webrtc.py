#!/usr/bin/env python3
"""Add optional screen input to the pinned camera WebRTC implementation."""
import sys
from pathlib import Path

path = Path(sys.argv[1])
source = path.read_text()
changes = [
    ('using json = nlohmann::json;', '#include "screen_touch.h"\n\nusing json = nlohmann::json;'),
    ('    std::string id;\n', '    std::string id;\n    std::string screen_owner;\n    uint64_t screen_generation = 0;\n'),
    ('    client->id = std::to_string(++g_client_counter);',
     '    client->screen_owner = request.value("screen_owner", "");\n'
     '    client->screen_generation = request.value("screen_generation", uint64_t{0});\n'
     '    client->id = client->screen_owner.empty() ? std::to_string(++g_client_counter) :\n'
     '        client->screen_owner + "-" + std::to_string(client->screen_generation);'),
    ('log_errorf("Removed client %s\\n", c->id.c_str());',
     'screen_touch.disconnected(c->id);\n            log_errorf("Removed client %s\\n", c->id.c_str());'),
    ('client->data_channel->onMessage([weak_client](auto) {',
     'client->data_channel->onMessage([weak_client](rtc::message_variant message) {'),
    ('c->last_pong = std::chrono::steady_clock::now();',
     'c->last_pong = std::chrono::steady_clock::now();\n'
     '            if (auto text = std::get_if<std::string>(&message)) {\n'
     '                if (screen_touch.message(c->id, *text)) c->data_channel->send("touch-ok");\n'
     '            }'),
    ('        ping_clients();', '        screen_touch.expire();\n        ping_clients();'),
    ('static json handle_request(const json& request) {', '''static void close_screen_clients(const std::string& id, const std::string& owner = "") {
    std::list<std::shared_ptr<Client>> removed;
    {
        std::lock_guard<std::mutex> lock(g_clients_mutex);
        for (auto it = g_clients.begin(); it != g_clients.end();) {
            if ((!id.empty() && (*it)->id == id) || (!owner.empty() && (*it)->screen_owner == owner)) {
                auto current = it++;
                removed.splice(removed.end(), g_clients, current);
            } else {
                ++it;
            }
        }
    }
    for (const auto& client : removed) {
        screen_touch.disconnected(client->id);
        try { if (client->pc) client->pc->close(); } catch (...) {}
    }
}

static json handle_request(const json& request) {'''),
    ('    std::string type = request.value("type", "");', '''    std::string type = request.value("type", "");

    if (type == "close") {
        std::string id = request.value("id", "");
        if (id.empty()) return {{"error", "missing id"}};
        close_screen_clients(id);
        return {{"type", "ok"}};
    }

    if (type == "request" || type == "offer") {
        std::string owner = request.value("screen_owner", "");
        uint64_t generation = 0;
        if (!owner.empty()) {
            if (owner.size() != 32 || owner.find_first_not_of("0123456789abcdef") != std::string::npos ||
                !request.contains("screen_generation") || !request["screen_generation"].is_number_unsigned())
                return {{"error", "invalid screen identity"}};
            generation = request["screen_generation"].get<uint64_t>();
            if (!generation || generation > 9007199254740991ULL) return {{"error", "invalid screen generation"}};
        }
        if (type == "offer" && request.value("sdp", "").empty()) return {{"error", "missing sdp"}};
        cleanup_clients();
        if (!owner.empty()) {
            {
                std::lock_guard<std::mutex> lock(g_clients_mutex);
                for (const auto& client : g_clients)
                    if (client->screen_owner == owner && client->screen_generation >= generation)
                        return {{"error", "stale screen request"}};
            }
            // Only replace this browser page; another viewer keeps its own slot.
            close_screen_clients("", owner);
        }
    }'''),
    ('''        auto client = create_client(request);
        client->pc->setLocalDescription();

        auto desc = client->pc->localDescription();
        if (!desc) {
            return {{"error", "failed to create offer"}};
        }

        return {{"type", "offer"}, {"id", client->id}, {"sdp", std::string(*desc)}};''',
     '''        auto client = create_client(request);
        try {
            client->pc->setLocalDescription();
            auto desc = client->pc->localDescription();
            if (desc) return {{"type", "offer"}, {"id", client->id}, {"sdp", std::string(*desc)}};
        } catch (...) {
            close_screen_clients(client->id);
            throw;
        }
        close_screen_clients(client->id);
        return {{"error", "failed to create offer"}};'''),
    ('''        auto client = create_client(request);
        client->pc->setRemoteDescription(rtc::Description(sdp, rtc::Description::Type::Offer));
        client->answer_received = true;

        auto desc = client->pc->localDescription();
        if (!desc) {
            return {{"error", "failed to create answer"}};
        }

        return {{"type", "answer"}, {"id", client->id}, {"sdp", std::string(*desc)}};''',
     '''        auto client = create_client(request);
        try {
            client->pc->setRemoteDescription(rtc::Description(sdp, rtc::Description::Type::Offer));
            client->answer_received = true;
            auto desc = client->pc->localDescription();
            if (desc) return {{"type", "answer"}, {"id", client->id}, {"sdp", std::string(*desc)}};
        } catch (...) {
            close_screen_clients(client->id);
            throw;
        }
        close_screen_clients(client->id);
        return {{"error", "failed to create answer"}};'''),
]
for before, after in changes:
    if source.count(before) != 1:
        raise SystemExit(f'Pinned WebRTC source changed: {before}')
    source = source.replace(before, after)
path.write_text(source)
