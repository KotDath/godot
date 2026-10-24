#!/usr/bin/env python3
"""Supervise one argv-based command inside the Linux SDK engine.

Stdin/stdout are a private JSON-lines protocol. Secrets arrive only on stdin and
are passed in the child's environment. EOF, cancellation or an expired client
lease terminate the owned process group, including uncooperative descendants.
Only the SDK needs Python; the editor embeds this script.
"""

import codecs
import json
import os
import queue
import select
import selectors
import signal
import subprocess
import sys
import threading
import time

PROTOCOL = 1
MAX_MESSAGE = 1024 * 1024


def emit(event, **fields):
    print(json.dumps({"aurora_job": PROTOCOL, "event": event, **fields}), flush=True)


def read_messages(messages, stop):
    # Raw reads avoid CPython buffered-stdin locks during interpreter shutdown.
    # Polling also lets the owner stop and join this thread without closing the
    # client's write end first (the normal successful editor workflow).
    pending = b""
    try:
        while not stop.is_set():
            if not select.select([0], [], [], 0.1)[0]:
                continue
            chunk = os.read(0, 4096)
            if not chunk:
                messages.put({"event": "invalid-input" if pending else "eof"})
                return
            pending += chunk
            while b"\n" in pending:
                line, pending = pending.split(b"\n", 1)
                if len(line) > MAX_MESSAGE:
                    raise ValueError("Invalid protocol frame")
                value = json.loads(line)
                if not isinstance(value, dict):
                    raise ValueError("Invalid protocol message")
                messages.put(value)
            if len(pending) > MAX_MESSAGE:
                raise ValueError("Invalid protocol frame")
    except Exception:
        # Do not print malformed input: it can contain a passphrase.
        messages.put({"event": "invalid-input"})


def exited(process):
    # Keep the leader unreaped until all group signals have been sent. This
    # prevents its PID/process-group ID being reused during cancellation grace.
    return os.waitid(os.P_PID, process.pid, os.WEXITED | os.WNOHANG | os.WNOWAIT) is not None


def group_signal(process, number):
    try:
        os.killpg(process.pid, number)
    except ProcessLookupError:
        pass


def supervise(messages):
    emit("ready")
    spec = messages.get(timeout=15)
    argv = spec.get("argv")
    if not isinstance(argv, list) or not argv or not all(isinstance(item, str) and "\0" not in item for item in argv):
        raise ValueError("Expected a nonempty argv")
    secret_environment = spec.get("secret_environment", {})
    if not isinstance(secret_environment, dict) or any(
        key != "KEY_PASSPHRASE" or not isinstance(value, str) or "\0" in value
        for key, value in secret_environment.items()
    ):
        raise ValueError("Invalid secret environment")
    lease = float(spec.get("lease_seconds", 15))
    grace = float(spec.get("terminate_grace", 2))
    if not 1 <= lease <= 120 or not 0.1 <= grace <= 5:
        raise ValueError("Invalid supervision timing")
    environment = dict(os.environ)
    environment.pop("KEY_PASSPHRASE", None)
    environment.update(secret_environment)
    hide_output = bool(secret_environment)
    decoder = codecs.getincrementaldecoder("utf-8")("replace")
    reason = ""

    def request_cancel(number, frame):
        nonlocal reason
        reason = "signal"

    signal.signal(signal.SIGTERM, request_cancel)
    signal.signal(signal.SIGINT, request_cancel)
    signal.signal(signal.SIGHUP, request_cancel)
    process = subprocess.Popen(
        argv,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        env=environment,
        start_new_session=True,
    )
    last_activity = time.monotonic()
    cancelled_at = None
    parent_exited_at = None
    eof = False
    try:
        emit("running", pid=process.pid)
        os.set_blocking(process.stdout.fileno(), False)
        with selectors.DefaultSelector() as selector:
            selector.register(process.stdout, selectors.EVENT_READ)
            while True:
                now = time.monotonic()
                while True:
                    try:
                        message = messages.get_nowait()
                    except queue.Empty:
                        break
                    event = message.get("event")
                    if event == "ping":
                        last_activity = now
                    elif event in ("cancel", "eof", "invalid-input"):
                        reason = reason or event
                    else:
                        reason = reason or "invalid-input"
                if not reason and now - last_activity > lease:
                    reason = "lease-expired"
                if reason and cancelled_at is None:
                    group_signal(process, signal.SIGTERM)
                    cancelled_at = now
                if cancelled_at is not None and now - cancelled_at >= grace:
                    group_signal(process, signal.SIGKILL)
                    break
                for key, _ in selector.select(0.05):
                    chunk = os.read(key.fd, 4096)
                    if not chunk:
                        selector.unregister(key.fileobj)
                        eof = True
                    else:
                        if not hide_output:
                            output = decoder.decode(chunk)
                            if output:
                                emit("output", text=output)
                if exited(process):
                    if cancelled_at is None and eof:
                        break
                    if parent_exited_at is None:
                        parent_exited_at = now
                    if not reason and now - parent_exited_at > 3:
                        reason = "descendant-held-output"
        output = decoder.decode(b"", final=True)
        if output:
            emit("output", text=output)
        code = process.wait()
        emit("result", exit_code=code, cancelled=bool(reason), reason=reason, output_suppressed=hide_output)
        return 130 if reason else max(0, min(code, 255)) if code >= 0 else 128 - code
    finally:
        # No Popen.poll()/wait() before the last group signal: do not release
        # the leader's PID while signalling descendants.
        if process.returncode is None:
            group_signal(process, signal.SIGKILL)
            process.wait()
        process.stdout.close()


def run():
    messages = queue.Queue()
    stop = threading.Event()
    reader = threading.Thread(target=read_messages, args=(messages, stop))
    reader.start()
    try:
        return supervise(messages)
    finally:
        stop.set()
        reader.join()


if __name__ == "__main__":
    try:
        sys.exit(run())
    except BrokenPipeError:
        os._exit(1)
    except Exception as error:
        emit("error", kind=type(error).__name__)
        sys.exit(1)
