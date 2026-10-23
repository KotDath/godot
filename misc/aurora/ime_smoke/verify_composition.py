#!/usr/bin/env python3
"""Check the empty-LineEdit native preedit → background → Undo scenario."""

import json
import sys
from pathlib import Path

records = [json.loads(line) for line in Path(sys.argv[1]).read_text().splitlines() if line.startswith("{")]
preedit = ""
committed = False
cleared = False
for record in records:
    kind, data = record["kind"], record["data"]
    if kind == "preedit":
        if not committed:
            preedit = data["text"]
        elif not data["text"]:
            cleared = True
    elif kind == "state":
        if not committed and preedit and data["event"] == "single":
            if data["single"] != preedit:
                raise SystemExit("Committed fallback differs from this test handler's actual preedit")
            committed = True
        elif committed and cleared and data["event"] == "action_4" and not data["single"]:
            print("PASS: native preedit, matching focus-loss fallback, clear and one-step Undo")
            break
else:
    raise SystemExit("Missing native preedit → fallback → clear → Undo sequence")
