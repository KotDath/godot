#!/usr/bin/env python3
"""Check the recorded native-keyboard smoke sequence (see README)."""

import json
import sys
from pathlib import Path

records = []
for line in Path(sys.argv[1]).read_text().splitlines():
    if line.startswith("AURORA_IME "):
        line = line.removeprefix("AURORA_IME ")
    if line.startswith("{"):
        records.append(json.loads(line))

original = 'func привет():\n\tprint("Юникод 😀")\n'
replacement = "замена\n\tстроки 👩‍💻\n"
checks = [
    ("single", "single", "Привет"),
    ("single", "single", "Пока!!"),
    ("action_4", "single", "Привет"),
    ("multiline", "multiline", original),
    ("action_2", "multiline", ""),
    ("multiline", "multiline", original),
    ("multiline", "multiline", replacement),
    ("action_2", "multiline", original),
    ("single", "single", "AБ😀"),
    ("action_4", "single", "Привет"),
    ("single", "single", "Приветq"),
    ("single", "single", "Привет"),
    ("action_5", "single", "Привет"),
]
position = 0
for event, field, expected in checks:
    while position < len(records):
        record = records[position]
        position += 1
        if record["event"] == event and record[field] == expected:
            break
    else:
        raise SystemExit(f"Missing or incorrect {event}: {field}={expected!r}")
if any(row["single"] != "Привет" or row["multiline"] != original for row in records[position:]):
    raise SystemExit("Read-only input changed text")
print(f"PASS: {len(checks)} ordered native IME checkpoints")
