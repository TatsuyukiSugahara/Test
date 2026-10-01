#!/usr/bin/env python3
"""
エディタ連携(LinkService)の評価用の最小クライアント。標準ライブラリだけで動く。

ランタイムを -editor-port=<port> 付きで起動してから使う。

  1 命令を送って応答を表示する:
    python link_client.py <port> ping
    python link_client.py <port> level.load "{\"path\":\"Assets/Levels/Stage1.level.json\"}"
  生の 1 行を送る(壊れた JSON の確認用):
    python link_client.py <port> raw "{not json"
  対話モード(1 行に「cmd [json-params]」。":raw <text>" で生の行、":quit" で終了):
    python link_client.py <port> interactive

  評価シナリオ(結果を PASS / FAIL で表示する):
    discard  実行前の破棄(hold → inc → 切断 → 別接続で stats / counter を確認)
    hello    hello と応答の取り違え(C → A → B の世代の手順)
    flood    ping を大量に(既定 10 万件)応答を読みながら送る
    noread   ping を送り続けて一切読まない(--hold-open で接続を開いたまま待つ)
    ping100  ping を 100 回連続で送り、往復時間を記録する
"""

import argparse
import json
import select
import socket
import statistics
import sys
import threading
import time

HOST = "127.0.0.1"
HOLD_TIMEOUT_MS = 5000


class LinkError(Exception):
    pass


class LinkClient:
    """改行区切り JSON の 1 接続"""

    def __init__(self, port, host=HOST, timeout=10.0, name=""):
        self.name = name
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        # 送信は別スレッドから sendall することがある(flood)。受信の待ち時間で送信がタイムアウトしないよう、
        # ソケット自体はブロッキングにして、受信の待ちは select で行う
        self.sock.settimeout(None)
        self.buffer = b""
        self.next_id = 1
        self.pending = []   # 読んだがまだ取り出していないメッセージ
        self.closed = False

    # ---- 送信 ----

    def send_line(self, text):
        self.sock.sendall(text.encode("utf-8") + b"\n")

    def send_request(self, cmd, params=None, request_id=None):
        if request_id is None:
            request_id = self.next_id
            self.next_id += 1
        message = {"type": "request", "id": request_id, "cmd": cmd}
        if params is not None:
            message["params"] = params
        self.send_line(json.dumps(message, separators=(",", ":")))
        return request_id

    # ---- 受信 ----

    def read_message(self, timeout):
        """1 メッセージ読む。timeout 秒で何も来なければ None。切断されたら LinkError"""
        if self.pending:
            return self.pending.pop(0)
        deadline = time.monotonic() + timeout
        while True:
            newline = self.buffer.find(b"\n")
            if newline >= 0:
                line = self.buffer[:newline]
                self.buffer = self.buffer[newline + 1:]
                if not line.strip():
                    continue
                return json.loads(line.decode("utf-8"))
            remain = deadline - time.monotonic()
            if remain <= 0:
                return None
            readable, _, _ = select.select([self.sock], [], [], remain)
            if not readable:
                return None
            try:
                chunk = self.sock.recv(65536)
            except (ConnectionResetError, ConnectionAbortedError) as e:
                self.closed = True
                raise LinkError("%s: connection reset (%s)" % (self.name, e))
            if not chunk:
                self.closed = True
                raise LinkError("%s: connection closed by runtime" % self.name)
            self.buffer += chunk

    def wait_for(self, predicate, timeout, seen=None):
        """predicate が真になるメッセージまで読む。途中のメッセージは seen に積む"""
        deadline = time.monotonic() + timeout
        while True:
            remain = deadline - time.monotonic()
            if remain <= 0:
                return None
            message = self.read_message(remain)
            if message is None:
                return None
            if predicate(message):
                return message
            if seen is not None:
                seen.append(message)

    def wait_event(self, name, timeout=10.0, seen=None):
        return self.wait_for(lambda m: m.get("type") == "event" and m.get("event") == name, timeout, seen)

    def wait_response(self, request_id, timeout=10.0, seen=None):
        return self.wait_for(lambda m: m.get("type") == "response" and m.get("id") == request_id, timeout, seen)

    def call(self, cmd, params=None, timeout=10.0, seen=None):
        request_id = self.send_request(cmd, params)
        response = self.wait_response(request_id, timeout, seen)
        if response is None:
            raise LinkError("%s: no response to %s within %.1f s" % (self.name, cmd, timeout))
        return response

    def result(self, cmd, params=None, timeout=10.0, seen=None):
        response = self.call(cmd, params, timeout, seen)
        if not response.get("ok"):
            raise LinkError("%s: %s failed: %s" % (self.name, cmd, response.get("error")))
        return response.get("result", {})

    def close(self):
        if self.closed:
            return
        self.closed = True
        try:
            self.sock.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        self.sock.close()


def dump(message):
    print(json.dumps(message, ensure_ascii=False))


def connect_accepted(port, name, timeout=10.0, settle=0.3, retries=50):
    """
    接続してランタイムに受け付けられるまで再試行する。
    前の接続の切断がまだ検出されていないと busy で拒否されるので、そのときは少し待って繋ぎ直す。
    settle 秒のあいだ busy が来なければ受け付けられたとみなす(メインが止まっていて hello が来ない場合用)
    """
    for _ in range(retries):
        client = LinkClient(port, timeout=timeout, name=name)
        try:
            message = client.read_message(settle)
        except LinkError:
            message = {"type": "event", "event": "error", "data": {"code": "busy"}}
        if message is not None and message.get("type") == "event" and message.get("event") == "error":
            client.close()
            time.sleep(0.1)
            continue
        if message is not None:
            client.pending.append(message)
        return client
    raise LinkError("%s: could not connect (runtime kept answering busy)" % name)


def connect_with_hello(port, name, timeout=10.0):
    client = connect_accepted(port, name, timeout)
    hello = client.wait_event("hello", timeout)
    if hello is None:
        raise LinkError("%s: no hello within %.1f s" % (name, timeout))
    return client, hello["data"]


class Verdict:
    def __init__(self, title):
        self.title = title
        self.failures = []

    def check(self, condition, message):
        print("  [%s] %s" % ("ok" if condition else "NG", message))
        if not condition:
            self.failures.append(message)

    def finish(self):
        if self.failures:
            print("FAIL: %s" % self.title)
            for failure in self.failures:
                print("  - " + failure)
            return 1
        print("PASS: %s" % self.title)
        return 0


# ---------------------------------------------------------------------------
# 単発・対話
# ---------------------------------------------------------------------------

def run_single(port, cmd, params_text, timeout):
    params = json.loads(params_text) if params_text else None
    client, hello = connect_with_hello(port, "client", timeout)
    dump({"type": "event", "event": "hello", "data": hello})
    seen = []
    response = client.call(cmd, params, timeout, seen)
    for message in seen:
        dump(message)
    dump(response)
    # 応答の後に届いたイベントも短く拾って表示する
    while True:
        message = client.read_message(0.2)
        if message is None:
            break
        dump(message)
    client.close()
    return 0 if response.get("ok") else 1


def run_raw(port, text, timeout):
    client, hello = connect_with_hello(port, "client", timeout)
    dump({"type": "event", "event": "hello", "data": hello})
    client.send_line(text)
    message = client.read_message(timeout)
    if message is None:
        print("(no reply within %.1f s)" % timeout)
        client.close()
        return 1
    dump(message)
    # 壊れた行の後も接続が維持されているか確かめる
    try:
        alive = client.call("ping", None, timeout)
        print("(connection still alive: ping ok=%s)" % alive.get("ok"))
    except LinkError as e:
        print("(connection lost: %s)" % e)
    client.close()
    return 0


def run_interactive(port, timeout):
    client = LinkClient(port, timeout=timeout, name="client")
    stop = threading.Event()

    def reader():
        while not stop.is_set():
            try:
                message = client.read_message(0.2)
            except LinkError as e:
                print("<< %s" % e)
                stop.set()
                return
            except ValueError as e:
                print("<< (unparsable line: %s)" % e)
                continue
            if message is not None:
                print("<< " + json.dumps(message, ensure_ascii=False))

    thread = threading.Thread(target=reader, daemon=True)
    thread.start()
    print("connected. type 'cmd [json-params]', ':raw <text>' or ':quit'")
    try:
        for line in sys.stdin:
            if stop.is_set():
                break
            line = line.strip()
            if not line:
                continue
            if line in (":quit", ":q"):
                break
            if line.startswith(":raw "):
                client.send_line(line[5:])
                continue
            cmd, _, params_text = line.partition(" ")
            try:
                params = json.loads(params_text) if params_text.strip() else None
            except ValueError as e:
                print("!! invalid params JSON: %s" % e)
                continue
            request_id = client.send_request(cmd, params)
            print(">> id=%d %s" % (request_id, cmd))
    except KeyboardInterrupt:
        pass
    stop.set()
    client.close()
    return 0


# ---------------------------------------------------------------------------
# 評価シナリオ
# ---------------------------------------------------------------------------

def scenario_discard(port, timeout):
    """
    実行前の破棄:
      1. A が接続し、hello の connectionId を控える
      2. A が debug.holdUntilDisconnected を送り、debug.holdStarted を受け取るまで待つ
      3. A が debug.counter inc を送ってから切断する
      4. B が接続し、debug.stats と debug.counter get を送る
    """
    verdict = Verdict("discard (request of a disconnected client is not executed)")

    a, hello_a = connect_with_hello(port, "A", timeout)
    print("A connected: connectionId=%s" % hello_a.get("connectionId"))
    stats0 = a.result("debug.stats")
    counter0 = a.result("debug.counter", {"op": "get"})["value"]
    print("baseline: stats=%s counter=%s" % (stats0, counter0))

    a.send_request("debug.holdUntilDisconnected", {"timeoutMs": HOLD_TIMEOUT_MS})
    started = a.wait_event("debug.holdStarted", timeout)
    verdict.check(started is not None, "A received debug.holdStarted")
    if started is None:
        a.close()
        return verdict.finish()

    a.send_request("debug.counter", {"op": "inc"})
    a.close()
    print("A sent debug.counter inc and disconnected")

    b, hello_b = connect_with_hello(port, "B", timeout + HOLD_TIMEOUT_MS / 1000.0)
    print("B connected: connectionId=%s" % hello_b.get("connectionId"))
    stats1 = b.result("debug.stats")
    counter1 = b.result("debug.counter", {"op": "get"})["value"]
    print("after: stats=%s counter=%s" % (stats1, counter1))
    b.close()

    verdict.check(stats1["holdTimeouts"] == stats0["holdTimeouts"],
                  "holdTimeouts did not increase (hold was released by the disconnect): %s -> %s"
                  % (stats0["holdTimeouts"], stats1["holdTimeouts"]))
    verdict.check(counter1 == counter0,
                  "counter unchanged (inc was not executed): %s -> %s" % (counter0, counter1))
    verdict.check(stats1["discardedBeforeExec"] == stats0["discardedBeforeExec"] + 1,
                  "discardedBeforeExec increased by 1: %s -> %s"
                  % (stats0["discardedBeforeExec"], stats1["discardedBeforeExec"]))
    return verdict.finish()


def scenario_hello(port, timeout):
    """
    hello と応答の取り違え:
      1. C が接続し、hello の connectionId を c とする
      2. C が debug.holdUntilGeneration {generation: c+2} を送り、holdStarted を受け取ってから切断する
      3. A が接続し(世代 c+1)、すぐ切断する
      4. B が接続する(世代 c+2)。ここで hold が解ける
    """
    verdict = Verdict("hello (no mix-up of hello / responses across generations)")

    c_client, hello_c = connect_with_hello(port, "C", timeout)
    c = int(hello_c["connectionId"])
    print("C connected: connectionId=%d" % c)
    stats0 = c_client.result("debug.stats")
    print("baseline: stats=%s" % stats0)

    # B の id と重ならないように C の id を大きく取る(B に C 宛ての応答が来たら分かるように)
    c_client.next_id = 900000
    c_client.send_request("debug.holdUntilGeneration", {"generation": c + 2, "timeoutMs": HOLD_TIMEOUT_MS})
    started = c_client.wait_event("debug.holdStarted", timeout)
    verdict.check(started is not None, "C received debug.holdStarted")
    if started is None:
        c_client.close()
        return verdict.finish()
    c_client.close()
    print("C disconnected")
    time.sleep(0.2)   # C の切断が検出されるのを待つ(間に合わなければ busy で再試行する)

    a = connect_accepted(port, "A", timeout)
    a.close()
    print("A connected (expected generation %d) and disconnected" % (c + 1))
    time.sleep(0.2)

    b = connect_accepted(port, "B", timeout)
    print("B connected (expected generation %d)" % (c + 2))
    received = []
    hello_b = b.wait_event("hello", timeout + HOLD_TIMEOUT_MS / 1000.0, received)
    if hello_b is not None:
        received.append(hello_b)
    stats_id = b.send_request("debug.stats")
    stats_response = b.wait_response(stats_id, timeout, received)
    # 遅れて届く 2 つ目の hello が無いか、もう 1 往復して確かめる
    ping_id = b.send_request("ping")
    b.wait_response(ping_id, timeout, received)
    while True:
        message = b.read_message(0.3)
        if message is None:
            break
        received.append(message)
    b.close()

    hellos = [m for m in received if m.get("type") == "event" and m.get("event") == "hello"]
    foreign = [m for m in received if m.get("type") == "response" and m.get("id") not in (stats_id, ping_id)]
    verdict.check(len(hellos) == 1, "B received exactly one hello (got %d)" % len(hellos))
    if hellos:
        verdict.check(int(hellos[0]["data"]["connectionId"]) == c + 2,
                      "hello connectionId is c+2 = %d (got %s)" % (c + 2, hellos[0]["data"]["connectionId"]))
    verdict.check(not foreign, "B received no responses meant for A / C (got %s)" % foreign)
    verdict.check(stats_response is not None and stats_response.get("ok"), "debug.stats answered")
    if stats_response is not None and stats_response.get("ok"):
        stats1 = stats_response["result"]
        print("after: stats=%s" % stats1)
        verdict.check(stats1["droppedStaleOutbox"] >= stats0["droppedStaleOutbox"] + 2,
                      "droppedStaleOutbox increased by >= 2 (hello for A, hold response for C): %s -> %s"
                      % (stats0["droppedStaleOutbox"], stats1["droppedStaleOutbox"]))
        verdict.check(stats1["holdTimeouts"] == stats0["holdTimeouts"],
                      "holdTimeouts did not increase: %s -> %s" % (stats0["holdTimeouts"], stats1["holdTimeouts"]))
    return verdict.finish()


def scenario_flood(port, timeout, count):
    """ping を count 件、応答を読みながら一気に送る"""
    verdict = Verdict("flood (%d pings while reading responses)" % count)
    client, _ = connect_with_hello(port, "flood", timeout)
    info0 = client.result("runtime.info")
    first_id = 1000
    sender_error = []

    def sender():
        try:
            batch = []
            for i in range(count):
                batch.append('{"type":"request","id":%d,"cmd":"ping"}\n' % (first_id + i))
                if len(batch) >= 1000:
                    client.sock.sendall("".join(batch).encode("ascii"))
                    batch = []
            if batch:
                client.sock.sendall("".join(batch).encode("ascii"))
        except OSError as e:
            sender_error.append(e)

    start = time.monotonic()
    thread = threading.Thread(target=sender, daemon=True)
    thread.start()

    received = 0
    per_frame = {}    # ping の time(フレームの総経過秒)ごとの応答数
    max_gap = 0.0
    last = time.monotonic()
    while received < count:
        message = client.read_message(timeout)
        now = time.monotonic()
        if message is None:
            break
        if message.get("type") != "response":
            continue
        max_gap = max(max_gap, now - last)
        last = now
        received += 1
        if message.get("ok"):
            key = message["result"]["time"]
            per_frame[key] = per_frame.get(key, 0) + 1
    elapsed = time.monotonic() - start
    thread.join(1.0)
    info1 = client.result("runtime.info")
    client.close()

    frames = len(per_frame)
    max_per_frame = max(per_frame.values()) if per_frame else 0
    print("elapsed %.2f s, %d responses, %.0f req/s" % (elapsed, received, received / elapsed if elapsed > 0 else 0))
    print("frames that processed pings: %d, max per frame: %d, avg per frame: %.1f"
          % (frames, max_per_frame, received / frames if frames else 0))
    print("longest gap between responses: %.1f ms" % (max_gap * 1000.0))
    print("fps before %.1f / after %.1f (frame time impact: check the in-game profiler)" % (info0["fps"], info1["fps"]))
    print("check the runtime log for '[link] inbox full, receive paused' and '[link] receive resumed'")
    verdict.check(not sender_error, "sender finished without error %s" % sender_error)
    verdict.check(received == count, "all %d responses received (got %d)" % (count, received))
    verdict.check(max_per_frame <= 256, "at most 256 commands per frame (max %d)" % max_per_frame)
    return verdict.finish()


def scenario_noread(port, timeout, seconds, hold_open):
    """接続して ping を送り続け、一切読まない"""
    verdict = Verdict("noread (client that never reads)")
    client = LinkClient(port, timeout=timeout, name="noread")
    client.sock.setblocking(False)
    line = b'{"type":"request","id":1,"cmd":"ping"}\n'
    batch = line * 1000
    sent_bytes = 0
    blocked = 0
    closed_by_runtime = None
    start = time.monotonic()
    pending = b""
    while time.monotonic() - start < seconds:
        data = pending or batch
        try:
            written = client.sock.send(data)
            sent_bytes += written
            pending = data[written:]
        except BlockingIOError:
            blocked += 1
            time.sleep(0.01)
        except OSError as e:
            closed_by_runtime = e
            break
    print("sent %.1f MiB (%d pings) in %.1f s, send blocked %d times"
          % (sent_bytes / 1048576.0, sent_bytes // len(line), time.monotonic() - start, blocked))
    if closed_by_runtime is not None:
        print("runtime closed the connection: %s (expected only when the send limit was exceeded)" % closed_by_runtime)

    if hold_open and closed_by_runtime is None:
        print("keeping the connection open without reading.")
        print("now close the runtime window: it must exit within 1 s with exit code 0. Ctrl+C to stop.")
        try:
            while True:
                time.sleep(0.5)
                try:
                    if client.sock.recv(1, socket.MSG_PEEK) == b"":
                        print("runtime closed the connection")
                        break
                except BlockingIOError:
                    pass
                except OSError as e:
                    print("runtime closed the connection: %s" % e)
                    break
        except KeyboardInterrupt:
            pass
        client.close()
        return 0

    client.sock.setblocking(True)
    client.close()

    # ランタイムが止まっていない(Tick が回り続けている)ことを、別の接続の往復で確かめる
    try:
        checker, _ = connect_with_hello(port, "checker", timeout)
        t0 = time.monotonic()
        checker.result("ping", None, timeout)
        rtt = (time.monotonic() - t0) * 1000.0
        checker.close()
        verdict.check(True, "runtime still answers after the flood (ping %.1f ms)" % rtt)
    except (LinkError, OSError) as e:
        verdict.check(False, "runtime still answers after the flood (%s)" % e)
    return verdict.finish()


def scenario_ping100(port, timeout):
    verdict = Verdict("ping100 (round-trip time)")
    client, _ = connect_with_hello(port, "ping100", timeout)
    rtts = []
    for _ in range(100):
        t0 = time.monotonic()
        response = client.call("ping", None, timeout)
        rtts.append((time.monotonic() - t0) * 1000.0)
        if not response.get("ok"):
            verdict.check(False, "ping failed: %s" % response)
            break
    client.close()
    if rtts:
        ordered = sorted(rtts)
        p95 = ordered[min(len(ordered) - 1, int(len(ordered) * 0.95))]
        print("rtt ms: min %.1f / avg %.1f / median %.1f / p95 %.1f / max %.1f"
              % (ordered[0], statistics.mean(rtts), statistics.median(rtts), p95, ordered[-1]))
    verdict.check(len(rtts) == 100, "100 pings answered (got %d)" % len(rtts))
    return verdict.finish()


# ---------------------------------------------------------------------------

SCENARIOS = ("discard", "hello", "flood", "noread", "ping100")


def main():
    parser = argparse.ArgumentParser(description="LinkService test client (newline-delimited JSON over TCP)")
    parser.add_argument("port", type=int)
    parser.add_argument("cmd", help="runtime command, 'raw', 'interactive', or a scenario: " + ", ".join(SCENARIOS))
    parser.add_argument("params", nargs="?", default=None, help="JSON params (or the raw line for 'raw')")
    parser.add_argument("--timeout", type=float, default=10.0, help="seconds to wait for each reply")
    parser.add_argument("--count", type=int, default=100000, help="flood: number of pings")
    parser.add_argument("--seconds", type=float, default=10.0, help="noread: seconds to keep sending")
    parser.add_argument("--hold-open", action="store_true", help="noread: keep the connection open without reading")
    args = parser.parse_args()

    try:
        if args.cmd == "interactive":
            return run_interactive(args.port, args.timeout)
        if args.cmd == "raw":
            return run_raw(args.port, args.params or "", args.timeout)
        if args.cmd == "discard":
            return scenario_discard(args.port, args.timeout)
        if args.cmd == "hello":
            return scenario_hello(args.port, args.timeout)
        if args.cmd == "flood":
            return scenario_flood(args.port, args.timeout, args.count)
        if args.cmd == "noread":
            return scenario_noread(args.port, args.timeout, args.seconds, args.hold_open)
        if args.cmd == "ping100":
            return scenario_ping100(args.port, args.timeout)
        return run_single(args.port, args.cmd, args.params, args.timeout)
    except (LinkError, OSError, ValueError) as e:
        print("FAIL: %s" % e)
        return 1


if __name__ == "__main__":
    sys.exit(main())
