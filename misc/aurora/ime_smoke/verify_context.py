#!/usr/bin/env python3
"""Check a WAYLAND_DEBUG=client trace produced by the IME smoke fixture."""

import gzip
import json
import re
import sys
from pathlib import Path

path = Path(sys.argv[1])
trace = gzip.open(path, "rt").read() if path.suffix == ".gz" else path.read_text()
requests = re.compile(r'set_surrounding_text\("(.*?)", (\d+), (\d+)\)', re.S)
records = re.compile(r"^AURORA_IME (\{[^\n]*\})$", re.M)
events = [(match.start(), "context", match.groups()) for match in requests.finditer(trace)]
events += [(match.start(), "state", json.loads(match[1])) for match in records.finditer(trace)]
secret = False
count = 0
unicode_seen = False
password_seen = False
multiline_seen = False
for _, kind, data in sorted(events):
    if kind == "state":
        if "secret" in data:
            secret = data["secret"]
        elif data["event"] == "action_6":
            secret = not secret
        continue
    text, cursor, anchor = data
    encoded = text.encode("utf-8")
    assert len(encoded) <= 4000, "Surrounding text exceeds the Wayland message limit"
    for offset in (int(cursor), int(anchor)):
        assert 0 <= offset <= len(encoded), "Cursor or anchor is outside the advertised text"
        encoded[:offset].decode("utf-8")  # Reject offsets inside a multibyte character.
    if secret:
        assert not text and cursor == anchor == "0", "Password text or positions reached the IME"
        password_seen = True
    unicode_seen |= not text.isascii()
    multiline_seen |= "\n" in text
    count += 1
assert count and unicode_seen and password_seen and multiline_seen, "Missing required smoke scenarios"
assert "set_content_type(192, 8)" in trace, "Missing hidden/sensitive password purpose"
print(f"PASS: {count} bounded contexts; Unicode boundaries, multiline and password privacy")
