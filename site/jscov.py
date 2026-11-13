"""V8 precise coverage of the page's JavaScript in every page, Worker and
service worker the browser tests open (V1 in the plan; report: jscov/report.js).

Off unless the environment has MKB_JSCOV=<directory>. install() then wraps
Playwright's sync API: a launched Chromium also listens on a DevTools port,
and a Collector (its own CDP connection, threads, stdlib only) auto-attaches
to every target (pages, popups, dedicated Workers, the service worker),
starts Profiler.startPreciseCoverage(callCount, detailed) before the target
runs a line, and takes Profiler.takePreciseCoverage every 30 s. Coverage is also taken
before Playwright closes a page, context or browser and before goto/reload
(a navigation can move the page to a new renderer process), and before the
page terminates a Worker (Worker.prototype.terminate waits for the take).
A navigation the page starts itself (location.reload(), location.href, a
link) is caught in the old document's beforeunload: a `debugger` statement
there pauses it (the Debugger domain is on in pages), the collector takes
the coverage and resumes it.

Only the site's own top-level *.js files are kept (not v86/, iced/, lz4/).
Each take writes the functions (counts 0/1, repeats dropped) as
JSON lines to MKB_JSCOV/raw-<label>-<pid>.jsonl: {"file", "len",
"functions"} where len is the script's length in UTF-16 units, so the
report can refuse a run of a different file with the same name (or "end",
the furthest range end, when the take has no top-level function).
Beside it, raw-....trace.txt has one JSON line per attach, detach, pause,
take, answer, failed command and script a take found run (with how many of
its functions came without block coverage, which the report cannot split
into lines and branches): what to read when a number looks wrong. Chromium
runs without V8's code cache and compilation cache (see with_port()).
"""

import base64
import json
import os
import pathlib
import socket
import struct
import sys
import threading
import time
import urllib.parse
import urllib.request

SITE = pathlib.Path(__file__).resolve().parent
SCRIPT_NAMES, SITE_DIRS = set(), set()
POLL_S = 30.0
TAKE_TIMEOUT_S = 10.0
# A service worker whose context closed never answers until it detaches
# (about 20 s later); a live one answers in milliseconds. Its answer is
# recorded whenever it comes, the flush just does not wait longer.
SW_WAIT_S = 1.0
BINDING = "__mkbJscovFlush"
WAIT_TYPES = {"page", "iframe", "worker", "service_worker", "shared_worker"}

# Page init script: terminate() of a Worker takes its coverage first. The
# Worker is silenced at once (no more messages either way, as terminate
# promises), and really terminated when the take is done or after 10 s.
TERMINATE_JS = """(() => {
  if (typeof Worker === 'undefined' || typeof %(b)s !== 'function' || Worker.prototype.__mkbJscov)
    return;
  const bind = %(b)s, real = Worker.prototype.terminate, pending = new Map();
  let n = 0;
  globalThis.__mkbJscovDone = (id) => { const f = pending.get(id); pending.delete(id); if (f) f(); };
  const W = Worker;
  function Wrapped(...a) {
    const w = new W(...a);
    w.addEventListener('message', (e) => { if (w.__mkbDead) e.stopImmediatePropagation(); });
    w.addEventListener('error', (e) => { if (w.__mkbDead) e.stopImmediatePropagation(); });
    return w;
  }
  Wrapped.prototype = W.prototype;
  W.prototype.__mkbJscov = true;
  W.prototype.terminate = function () {
    if (this.__mkbDead)
      return;
    this.__mkbDead = true;
    this.postMessage = () => {};
    const id = String(++n);
    let done = false;
    const end = () => { if (!done) { done = true; real.call(this); } };
    pending.set(id, end);
    setTimeout(end, 10000);
    bind(id);
  };
  globalThis.Worker = Wrapped;
  // The collector takes the coverage while this pause lasts (Debugger.paused).
  addEventListener('beforeunload', () => { debugger; });
})();""" % {"b": BINDING}


def log(*a):
    print("jscov:", *a, file=sys.stderr, flush=True)


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class WebSocket:
    """The client side of RFC 6455, enough for the DevTools protocol."""

    def __init__(self, url):
        u = urllib.parse.urlsplit(url)
        self.sock = socket.create_connection((u.hostname, u.port), timeout=30)
        key = base64.b64encode(os.urandom(16)).decode()
        self.sock.sendall((f"GET {u.path} HTTP/1.1\r\nHost: {u.hostname}:{u.port}\r\nUpgrade: websocket\r\n"
                           f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n").encode())
        head = b""
        while b"\r\n\r\n" not in head:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError("DevTools closed the connection in the handshake")
            head += chunk
        if b" 101 " not in head.split(b"\r\n", 1)[0]:
            raise ConnectionError(f"DevTools refused the WebSocket: {head[:200]!r}")
        self.buf = head.split(b"\r\n\r\n", 1)[1]
        self.sock.settimeout(None)
        self.lock = threading.Lock()

    def _read(self, n):
        while len(self.buf) < n:
            chunk = self.sock.recv(1 << 20)
            if not chunk:
                raise ConnectionError("DevTools closed the connection")
            self.buf += chunk
        out, self.buf = self.buf[:n], self.buf[n:]
        return out

    def send(self, text, opcode=1):
        data = text.encode() if isinstance(text, str) else text
        n = len(data)
        head = bytes([0x80 | opcode])
        if n < 126:
            head += bytes([0x80 | n])
        elif n < 1 << 16:
            head += bytes([0x80 | 126]) + struct.pack(">H", n)
        else:
            head += bytes([0x80 | 127]) + struct.pack(">Q", n)
        mask = os.urandom(4)
        body = bytes(b ^ mask[i & 3] for i, b in enumerate(data)) if n < 4096 else _mask(data, mask)
        with self.lock:
            self.sock.sendall(head + mask + body)

    def recv(self):
        parts = []
        while True:
            b0, b1 = self._read(2)
            n = b1 & 0x7F
            if n == 126:
                n = struct.unpack(">H", self._read(2))[0]
            elif n == 127:
                n = struct.unpack(">Q", self._read(8))[0]
            mask = self._read(4) if b1 & 0x80 else None
            data = self._read(n)
            if mask:
                data = _mask(data, mask)
            op = b0 & 0x0F
            if op == 8:
                raise ConnectionError("DevTools closed the WebSocket")
            if op == 9:
                self.send(data, opcode=10)
                continue
            if op == 10:
                continue
            parts.append(data)
            if b0 & 0x80:
                return b"".join(parts).decode()

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def _mask(data, mask):
    n = len(data)
    m = int.from_bytes((mask * (n // 4 + 1))[:n], "big")
    return (int.from_bytes(data, "big") ^ m).to_bytes(n, "big")


def configure(site):
    """The scripts to keep: every top-level *.js of site but node tools (#!)."""
    site = pathlib.Path(site)
    SCRIPT_NAMES.clear()
    SCRIPT_NAMES.update(p.name for p in site.glob("*.js") if not p.read_text(errors="replace").startswith("#!"))
    SITE_DIRS.clear()
    SITE_DIRS.update(p.name for p in site.iterdir() if p.is_dir())


configure(SITE)


def site_file(url):
    """The site script a URL loads (app.js for .../app.js?v=...), or None."""
    try:
        path = urllib.parse.urlsplit(url).path
    except ValueError:
        return None
    seg = path.split("/")
    if len(seg) < 2 or seg[-1] not in SCRIPT_NAMES or (len(seg) >= 3 and seg[-2] in SITE_DIRS):
        return None
    return seg[-1]


class Collector:
    def __init__(self, port, outdir, label):
        self.port = port
        self.outdir = pathlib.Path(outdir)
        self.outdir.mkdir(parents=True, exist_ok=True)
        self.path = self.outdir / f"raw-{label}-{os.getpid()}-{port}.jsonl"
        self.out = open(self.path, "a")
        # What happened, in order (attach, detach, pause, take, answer, any
        # failed command): to tell later why a take lost its block coverage.
        self.trace_file = open(self.path.with_suffix(".trace.txt"), "a")
        self.t0 = time.monotonic()
        self.next_id = 0
        self.waiting = {}   # message id -> [event, result]
        self.outstanding = {}  # message id of an unanswered take -> session id
        self.after = {}     # message id of a take -> what to send once it is answered
        self.sessions = {}  # session id -> target type
        self.lens = {}      # (session, scriptId, url) -> script length
        self.seen = set()
        self.write_lock = threading.Lock()
        self.id_lock = threading.Lock()
        self.flush_lock = threading.Lock()
        self.closed = False
        self.takes = self.records = 0
        # Functions a take reported as run without their blocks (the whole
        # function counts as run, its untaken branches are not seen), by
        # target type: how much the measurement lost.
        self.blockless = {}
        self.flush_s = self.flush_max = 0.0
        url = None
        end = time.monotonic() + 30
        while time.monotonic() < end:
            try:
                with urllib.request.urlopen(f"http://127.0.0.1:{port}/json/version", timeout=5) as r:
                    url = json.load(r)["webSocketDebuggerUrl"]
                break
            except OSError:
                time.sleep(0.2)
        if not url:
            raise RuntimeError(f"jscov: no DevTools endpoint on port {port}")
        self.ws = WebSocket(url)
        threading.Thread(target=self._reader, daemon=True, name="jscov-reader").start()
        self.call("Target.setAutoAttach", {"autoAttach": True, "waitForDebuggerOnStart": True, "flatten": True})
        threading.Thread(target=self._poller, daemon=True, name="jscov-poller").start()
        log(f"collecting into {self.path} (port {port})")

    def trace(self, what, sid=None, **kw):
        line = json.dumps({"t": round(time.monotonic() - self.t0, 3), "e": what, "s": (sid or "")[:8], **kw})
        with self.write_lock:
            if not self.trace_file.closed:
                self.trace_file.write(line + "\n")

    # ---- protocol ----------------------------------------------------------

    def post(self, method, params=None, session=None):
        """Send without waiting; returns the message id."""
        with self.id_lock:
            self.next_id += 1
            i = self.next_id
        msg = {"id": i, "method": method, "params": params or {}}
        if session:
            msg["sessionId"] = session
        try:
            self.ws.send(json.dumps(msg))
        except OSError:
            pass
        return i

    def call(self, method, params=None, session=None, timeout=TAKE_TIMEOUT_S):
        ev = threading.Event()
        with self.id_lock:
            self.next_id += 1
            i = self.next_id
            self.waiting[i] = [ev, None]
        msg = {"id": i, "method": method, "params": params or {}}
        if session:
            msg["sessionId"] = session
        try:
            self.ws.send(json.dumps(msg))
        except OSError:
            self.waiting.pop(i, None)
            return None
        ev.wait(timeout)
        return self.waiting.pop(i, [None, None])[1]

    def _reader(self):
        while True:
            try:
                msg = json.loads(self.ws.recv())
            except (ConnectionError, OSError, ValueError):
                self.closed = True
                for ev, _ in list(self.waiting.values()):
                    ev.set()
                return
            try:
                self._dispatch(msg)
            except Exception as e:  # noqa: BLE001 (a collector bug must not hang the targets)
                log(f"dispatch error {type(e).__name__}: {e}")

    def _dispatch(self, msg):
        if "id" in msg:
            sid = self.outstanding.pop(msg["id"], None)
            if sid and "result" in msg:
                self.takes += 1
                self.trace("answer", sid, id=msg["id"])
                if os.environ.get("MKB_JSCOV_DEBUG"):
                    log(f"answer {msg['id']} {self.sessions.get(sid)}: {[x.get('url', '')[-30:] for x in msg['result'].get('result', [])]}")
                self._record(sid, msg["result"].get("result", []))
            elif sid:
                log(f"take {msg['id']} on a {self.sessions.get(sid)}: {msg.get('error')}")
            then = self.after.pop(msg["id"], None)
            if then:
                self.post(*then)
            w = self.waiting.pop(msg["id"], None) if sid else self.waiting.get(msg["id"])
            if w:
                w[1] = msg
                w[0].set()
            elif "error" in msg:
                self.trace("error", msg.get("sessionId"), id=msg["id"], error=str(msg["error"])[:200])
                if os.environ.get("MKB_JSCOV_DEBUG"):
                    log(f"error {msg}")
            return
        method, p, sid = msg.get("method"), msg.get("params", {}), msg.get("sessionId")
        if os.environ.get("MKB_JSCOV_DEBUG") and method and method.startswith("Target."):
            log(f"{time.monotonic():.2f} {method} {(sid or '')[:8]} {json.dumps(p)[:200]}")
        if method == "Target.attachedToTarget":
            self.trace("attach", p["sessionId"], kind=p["targetInfo"].get("type"), url=p["targetInfo"].get("url", "")[-60:],
                       waiting=p.get("waitingForDebugger"), parent=sid)
            self._attached(p["sessionId"], p["targetInfo"], p.get("waitingForDebugger"))
        elif method == "Target.detachedFromTarget":
            self.trace("detach", p.get("sessionId"))
            self.sessions.pop(p.get("sessionId"), None)
            for i, o in list(self.outstanding.items()):
                if o == p.get("sessionId"):
                    self.outstanding.pop(i, None)
                    self.after.pop(i, None)
        elif method == "Debugger.paused" and sid in self.sessions:
            self.trace("paused", sid, reason=p.get("reason"))
            # TERMINATE_JS's beforeunload pause (or any other): take, then resume.
            self._take(sid, then=("Debugger.resume", None, sid))
        elif method == "Runtime.bindingCalled" and p.get("name") == BINDING:
            threading.Thread(target=self._terminate_flush, args=(sid, p.get("executionContextId"), p.get("payload")),
                             daemon=True).start()

    def _attached(self, sid, info, waiting):
        kind = info.get("type")
        if os.environ.get("MKB_JSCOV_DEBUG"):
            log(f"attached {kind} {info.get('url', '')[:80]} waiting={waiting}")
        if kind not in WAIT_TYPES:
            if waiting:
                self.post("Runtime.runIfWaitingForDebugger", session=sid)
            self.post("Target.detachFromTarget", {"sessionId": sid})
            return
        self.sessions[sid] = kind
        # Sent in order on the target's session, so coverage starts before
        # this session lets the target run.
        self.post("Profiler.enable", session=sid)
        self.post("Profiler.startPreciseCoverage", {"callCount": True, "detailed": True}, session=sid)
        # Workers of this target; its service worker comes once, through the
        # browser's auto-attach (a second session on it never answers takes).
        self.post("Target.setAutoAttach", {"autoAttach": True, "waitForDebuggerOnStart": True, "flatten": True,
                                           "filter": [{"type": "service_worker", "exclude": True}, {}]}, session=sid)
        if kind in ("page", "iframe"):
            # The binding reaches new documents only with both enabled.
            self.post("Page.enable", session=sid)
            self.post("Runtime.enable", session=sid)
            self.post("Runtime.addBinding", {"name": BINDING}, session=sid)
            self.post("Page.addScriptToEvaluateOnNewDocument", {"source": TERMINATE_JS}, session=sid)
            self.post("Debugger.enable", session=sid)
        if waiting:
            self.post("Runtime.runIfWaitingForDebugger", session=sid)

    # ---- coverage ----------------------------------------------------------

    def _poller(self):
        while not self.closed:
            time.sleep(POLL_S)
            self.flush()

    def _terminate_flush(self, sid, ctx, payload):
        self.flush()
        if sid in self.sessions:
            self.post("Runtime.evaluate", {"expression": f"globalThis.__mkbJscovDone && __mkbJscovDone({json.dumps(payload)})",
                                           "contextId": ctx}, session=sid)

    def _take(self, sid, then=None):
        """Ask one target for its coverage without waiting (the reader
        records the answer, then sends then: (method, params, session))."""
        with self.id_lock:
            self.next_id += 1
            i = self.next_id
            self.outstanding[i] = sid
            if then:
                self.after[i] = then
        try:
            self.trace("take", sid, id=i, why="paused" if then else "")
            self.ws.send(json.dumps({"id": i, "method": "Profiler.takePreciseCoverage", "sessionId": sid}))
        except OSError:
            self.outstanding.pop(i, None)
            if then:
                self.after.pop(i, None)
                self.post(*then)

    def flush(self):
        """Take the coverage of every attached target (all at once); the
        reader records each answer, even one later than the wait here. A
        target with a take still unanswered (a stopped service worker, a
        busy page) gets no second one."""
        if self.closed:
            return
        with self.flush_lock:
            t0 = time.monotonic()
            evs = []
            for sid in list(self.sessions):
                if sid in self.outstanding.values():
                    self.trace("skip", sid)
                    continue
                ev = threading.Event()
                with self.id_lock:
                    self.next_id += 1
                    i = self.next_id
                    self.outstanding[i] = sid
                    self.waiting[i] = [ev, None]
                if os.environ.get("MKB_JSCOV_DEBUG"):
                    log(f"{time.monotonic():.2f} take {i} -> {self.sessions.get(sid)} {sid[:8]}")
                try:
                    self.trace("take", sid, id=i)
                    self.ws.send(json.dumps({"id": i, "method": "Profiler.takePreciseCoverage", "sessionId": sid}))
                except OSError:
                    pass
                evs.append((ev, SW_WAIT_S if self.sessions.get(sid) == "service_worker" else TAKE_TIMEOUT_S))
            for ev, wait in evs:
                ev.wait(max(0.0, t0 + wait - time.monotonic()))
            if self.outstanding and os.environ.get("MKB_JSCOV_DEBUG"):
                log(f"unanswered takes: {[self.sessions.get(s) for s in self.outstanding.values()]}")
            dt = time.monotonic() - t0
            self.flush_s += dt
            self.flush_max = max(self.flush_max, dt)

    def _record(self, sid, scripts):
        lines = []
        for s in scripts:
            name = site_file(s.get("url", ""))
            if not name:
                continue
            key = (sid, s.get("scriptId"), s["url"])
            for f in s["functions"]:
                r0 = f["ranges"][0]
                if r0["startOffset"] == 0 and not f["functionName"]:
                    self.lens[key] = max(self.lens.get(key, 0), r0["endOffset"])
            # No top-level function when coverage started after it ran (another
            # client resumed the target first, seen on service workers): the
            # furthest range end bounds the length instead.
            n = self.lens.get(key)
            end = max(f["ranges"][0]["endOffset"] for f in s["functions"]) if s["functions"] else 0
            ran = []
            kind = self.sessions.get(sid) or "gone"
            nb = sum(1 for f in s["functions"] if not f["isBlockCoverage"] and f["ranges"][0]["count"])
            if nb:
                self.blockless[kind] = self.blockless.get(kind, 0) + nb
            nran = sum(1 for f in s["functions"] if f["ranges"][0]["count"])
            if nran:
                self.trace("script", sid, file=name, ran=nran, blockless=nb)
            for f in s["functions"]:
                ranges = [{"startOffset": r["startOffset"], "endOffset": r["endOffset"], "count": 1 if r["count"] else 0}
                          for r in f["ranges"]]
                sig = (name, n or -1, f["functionName"], tuple((r["startOffset"], r["endOffset"], r["count"]) for r in ranges))
                if sig in self.seen:
                    continue
                self.seen.add(sig)
                ran.append({"functionName": f["functionName"], "isBlockCoverage": f["isBlockCoverage"], "ranges": ranges})
            if os.environ.get("MKB_JSCOV_DEBUG"):
                log(f"take {kind} {sid[:8]} {name}: {len(ran)} new of {len(s['functions'])}, {nb} ran without blocks: "
                    + ", ".join(f"{f['functionName'] or '(top)'} {f['ranges'][0]['count']}" for f in ran)[:300])
            if ran:
                rec = {"file": name, "len": n} if n else {"file": name, "end": end}
                if self.sessions.get(sid) == "service_worker":
                    rec["kind"] = "service_worker"
                rec["functions"] = ran
                lines.append(json.dumps(rec))
        if lines:
            with self.write_lock:
                self.out.write("\n".join(lines) + "\n")
                self.out.flush()
                self.records += len(lines)

    def close(self):
        self.flush()
        self.closed = True
        self.ws.close()
        with self.write_lock:
            self.out.close()
            self.trace_file.close()
        log(f"{self.takes} takes ({self.flush_s:.1f} s, longest flush {self.flush_max * 1000:.0f} ms), "
            f"{self.records} records in {self.path}; functions run without block coverage: "
            f"{json.dumps(self.blockless, sort_keys=True)}")


COLLECTORS = []


def flush_all():
    for c in COLLECTORS:
        if not c.closed:
            c.flush()


def install(label):
    """With MKB_JSCOV set, collect coverage in every Chromium this process launches."""
    outdir = os.environ.get("MKB_JSCOV")
    if not outdir:
        return False
    log(f"env MKB_JSCOV = {outdir}")
    from playwright.sync_api import Browser, BrowserContext, BrowserType, Page

    def with_port(kw):
        port = free_port()
        # Functions V8 did not compile itself under block coverage report
        # none (Chromium 158): --v8-cache-options=none, no code cache (the
        # site's service worker caches its scripts at install, and Chromium
        # loads those with a code cache: every function of a page reloaded
        # under it came without blocks, CI run 43509415990);
        # --no-compilation-cache, a document loaded again in the same
        # renderer does not reuse the functions compiled for the one before
        # (the same, with the Debugger domain off).
        kw["args"] = list(kw.get("args") or []) + [f"--remote-debugging-port={port}", "--v8-cache-options=none",
                                                   "--js-flags=--no-compilation-cache"]
        return port

    launch, persistent = BrowserType.launch, BrowserType.launch_persistent_context

    def launch_cov(self, *a, **kw):
        port = with_port(kw)
        browser = launch(self, *a, **kw)
        c = Collector(port, outdir, label)
        COLLECTORS.append(c)
        browser._mkb_jscov = c
        return browser

    def persistent_cov(self, *a, **kw):
        port = with_port(kw)
        ctx = persistent(self, *a, **kw)
        c = Collector(port, outdir, label)
        COLLECTORS.append(c)
        ctx._mkb_jscov = c
        return ctx

    BrowserType.launch, BrowserType.launch_persistent_context = launch_cov, persistent_cov

    def before(cls, name):
        orig = getattr(cls, name)

        def wrapped(self, *a, **kw):
            flush_all()
            return orig(self, *a, **kw)
        setattr(cls, name, wrapped)

    for name in ("goto", "reload", "go_back", "go_forward", "close"):
        before(Page, name)
    bclose, cclose = Browser.close, BrowserContext.close

    def browser_close(self, *a, **kw):
        flush_all()
        c = getattr(self, "_mkb_jscov", None)
        if c:
            c.close()
        return bclose(self, *a, **kw)

    def context_close(self, *a, **kw):
        flush_all()
        c = getattr(self, "_mkb_jscov", None)
        if c:
            c.close()
        return cclose(self, *a, **kw)

    Browser.close, BrowserContext.close = browser_close, context_close
    return True
