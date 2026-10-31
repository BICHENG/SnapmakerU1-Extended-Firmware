#!/usr/bin/env python3
"""Add optional screen input to the pinned camera WebRTC implementation."""
import sys
from pathlib import Path

path = Path(sys.argv[1])
source = path.read_text()
changes = [
    ('using json = nlohmann::json;', '#include "screen_touch.h"\n\nusing json = nlohmann::json;'),
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
]
for before, after in changes:
    if source.count(before) != 1:
        raise SystemExit(f'Pinned WebRTC source changed: {before}')
    source = source.replace(before, after)
path.write_text(source)
