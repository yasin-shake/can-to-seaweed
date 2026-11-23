"""J1939 serial -> MF4 -> S3 pipeline with a local web dashboard.

Run:  python pipeline.py            (stop with Ctrl+C)
Open: http://127.0.0.1:8080
"""
import argparse
import collections
import glob
import json
import logging
import os
import queue
import re
import shutil
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from logging.handlers import RotatingFileHandler
from urllib.parse import parse_qs, urlparse

import serial

import j1939
import mf4writer
import signals

BASE = os.path.dirname(os.path.abspath(__file__))


def load_dotenv(path=os.path.join(BASE, ".env")):
    """Load KEY=VALUE lines from .env into os.environ (existing variables win). Values are never logged."""
    try:
        with open(path, encoding="utf-8-sig") as f:
            for ln in f:
                ln = ln.strip()
                if not ln or ln.startswith("#") or "=" not in ln:
                    continue
                k, v = ln.split("=", 1)
                k, v = k.strip(), v.strip().strip('"').strip("'")
                if k in ("AWS_ACCESS_KEY_ID", "AWS_SECRET_ACCESS_KEY") and k not in os.environ:
                    os.environ[k] = v
    except OSError:
        pass


load_dotenv()
DTC_LEVEL = 35
logging.addLevelName(DTC_LEVEL, "DTC")
log = logging.getLogger("pipeline")

# keys editable live from the web UI: name -> (type, min, max) / allowed values
EDITABLE = {
    "file_seconds": (int, 5, 86400),
    "file_max_kb": (int, 4, 102400),     # KB of logged frame data per file (the ~104 KB header is extra)
    "upload_enabled": (bool,),
    "after_upload": ("enum", ("delete", "archive")),
    "upload_retry_max_seconds": (int, 10, 3600),
}


# ----------------------------------------------------------------------------- config / state
class Config:
    def __init__(self, path):
        self.path, self.lock = path, threading.Lock()
        with open(path, encoding="utf-8") as f:
            self.d = json.load(f)

    def get(self, k):
        with self.lock:
            return self.d[k]

    def snapshot(self):
        with self.lock:
            return dict(self.d)

    def update(self, changes):
        with self.lock:
            self.d.update(changes)
            tmp = self.path + ".tmp"
            with open(tmp, "w", encoding="utf-8") as f:
                json.dump(self.d, f, indent=2)
            os.replace(tmp, self.path)


def state_path():
    return os.path.join(BASE, "state.json")


def load_state():
    try:
        with open(state_path(), encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def save_state(**kw):
    s = load_state()
    s.update(kw)
    tmp = state_path() + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(s, f, indent=2)
    os.replace(tmp, state_path())


# ----------------------------------------------------------------------------- logging
class MemHandler(logging.Handler):
    def __init__(self, n=600):
        super().__init__()
        self.buf, self.n, self.seq, self.mlock = collections.deque(maxlen=n), n, 0, threading.Lock()

    def emit(self, r):
        with self.mlock:
            self.seq += 1
            self.buf.append({"id": self.seq, "t": time.strftime("%H:%M:%S", time.localtime(r.created)),
                             "level": r.levelname, "msg": r.getMessage()})

    def since(self, i):
        with self.mlock:
            return [x for x in self.buf if x["id"] > i]


MEM = MemHandler()


def setup_logging(cfg):
    os.makedirs(os.path.join(BASE, "logs"), exist_ok=True)
    fmt = logging.Formatter("%(asctime)s %(levelname)-7s %(message)s")
    fh = RotatingFileHandler(os.path.join(BASE, "logs", "pipeline.log"), maxBytes=5_000_000,
                             backupCount=5, encoding="utf-8")
    fh.setFormatter(fmt)
    sh = logging.StreamHandler(sys.stdout)
    sh.setFormatter(fmt)
    log.setLevel(logging.INFO)
    for h in (fh, sh, MEM):
        log.addHandler(h)


# ----------------------------------------------------------------------------- stats
class Stats:
    def __init__(self):
        self.lock = threading.Lock()
        self.started = time.time()
        self.session = 0
        self.serial_connected = False
        self.serial_error = ""
        self.lines = self.other_lines = self.bad_lines = self.pgn_mismatch = self.oversize_lines = 0
        self.bytes = 0
        self.frames_total = 0
        self.frames_session = 0
        self.files_written = self.files_uploaded = 0
        self.bytes_uploaded = 0
        self.upload_state = "disabled"
        self.upload_error = ""
        self.upload_failures = 0
        self.last_upload = ""
        self.dtc_total = 0
        self.last_dtc = None
        self.dtc_seen = {}                       # (kind, sa, spn, fmi) -> [count, last_time, oc]
        self.signals = {}                        # (sa, pgn, name) -> latest decoded signal
        self.undecoded = {}                      # (pgn, sa) -> {count, last, data, length}
        self.per_sec = collections.deque(maxlen=60)   # [sec, frames, bytes]
        self.recent = collections.deque(maxlen=25)
        self.watch_hist = {k: collections.deque(maxlen=3000) for k in signals.WATCH}   # key -> (time, value)
        self.buffer_warn = ""

    def tick_second(self, frames, nbytes):
        sec = int(time.time())
        with self.lock:
            if self.per_sec and self.per_sec[-1][0] == sec:
                self.per_sec[-1][1] += frames
                self.per_sec[-1][2] += nbytes
            else:
                self.per_sec.append([sec, frames, nbytes])

    def rates(self, window=10):
        now = int(time.time())
        with self.lock:
            rows = [r for r in self.per_sec if now - r[0] < window]
        fr = sum(r[1] for r in rows) / window
        by = sum(r[2] for r in rows) / window
        return fr, by


STATS = Stats()


# ----------------------------------------------------------------------------- serial parsing
FRAME_RE = re.compile(
    r"^(?:Tx|Rx)\s+J1939\s+CANID\[([0-9A-Fa-f]{1,8})\](?:\s+PGN\[0x([0-9A-Fa-f]+)\|\d+\])?.*?DATA((?:\[[0-9A-Fa-f]{2}\])*)\s*$")
BYTE_RE = re.compile(r"\[([0-9A-Fa-f]{2})\]")


def parse_line(line):
    """-> ('frame', can_id, data, pgn_printed) | ('bad',) | ('other',)"""
    if "CANID[" not in line:
        return ("other",)
    m = FRAME_RE.match(line)
    if not m:
        return ("bad",)
    data = bytes(int(b, 16) for b in BYTE_RE.findall(m.group(3)))
    if len(data) > 8:                    # not representable as a classic CAN frame
        return ("oversize", int(m.group(1), 16) & 0x1FFFFFFF, len(data))
    return ("frame", int(m.group(1), 16) & 0x1FFFFFFF, data, int(m.group(2), 16) if m.group(2) else None)


# ----------------------------------------------------------------------------- recorder (rotation + journal)
class Recorder:
    def __init__(self, cfg, session, jobs):
        self.cfg, self.session, self.jobs = cfg, session, jobs
        self.lock = threading.Lock()
        self.frames, self.start, self.split, self.dtcs = [], None, 0, []
        self.jfile, self.jpath, self.last_flush = None, None, time.time()
        os.makedirs(os.path.join(BASE, "spool", "_journal"), exist_ok=True)

    def current(self):
        with self.lock:
            if self.start is None:
                return None
            n = len(self.frames)
            return {"name": mf4writer.file_name(self.split, self.start), "frames": n,
                    "est_bytes": mf4writer.estimated_size(n), "elapsed": time.time() - self.start,
                    "dtc": len(self.dtcs)}

    def add(self, ts, cid, data, events):
        with self.lock:
            if self.start is None:
                self._open(ts)
            self.frames.append((ts, cid, data))
            self.jfile.write("%.4f,%X,%s\n" % (ts, cid, data.hex()))
            self.dtcs.extend(events)
            if time.time() - self.last_flush > 0.5:
                self.jfile.flush()
                self.last_flush = time.time()
            if (ts - self.start >= self.cfg.get("file_seconds")
                    or len(self.frames) * mf4writer.RECORD_BYTES >= self.cfg.get("file_max_kb") * 1024):
                self._rotate()

    def tick(self):
        with self.lock:
            if self.start is not None and time.time() - self.start >= self.cfg.get("file_seconds"):
                self._rotate()

    def close(self):
        with self.lock:
            if self.start is not None:
                self._rotate()

    def _open(self, ts):
        self.split += 1
        self.start = ts
        self.jpath = os.path.join(BASE, "spool", "_journal", "%08d-%08d.csv" % (self.session, self.split))
        self.jfile = open(self.jpath, "w", encoding="ascii")
        self.jfile.write("#%d,%d,%.4f\n" % (self.session, self.split, ts))
        log.info("Started file %s (session %d)", mf4writer.file_name(self.split, ts), self.session)

    def _rotate(self):
        self.jfile.close()
        self.jobs.put({"session": self.session, "split": self.split, "start": self.start,
                       "frames": self.frames, "dtcs": self.dtcs, "journal": self.jpath})
        self.frames, self.start, self.dtcs, self.jfile = [], None, [], None


def recover_journals(cfg, jobs):
    """Turn journals left by a crash / hard stop into MF4 files (nothing is lost)."""
    for p in sorted(glob.glob(os.path.join(BASE, "spool", "_journal", "*.csv"))):
        try:
            if os.path.getsize(p) == 0:          # left behind by a hard kill before anything was flushed
                os.remove(p)
                continue
            with open(p, encoding="ascii") as f:
                head = f.readline().strip().lstrip("#").split(",")
                session, split, start = int(head[0]), int(head[1]), float(head[2])
                frames = []
                for ln in f:
                    parts = ln.strip().split(",")
                    if len(parts) == 3:
                        frames.append((float(parts[0]), int(parts[1], 16), bytes.fromhex(parts[2])))
            if frames:
                log.warning("Recovering %d frames from unfinished journal %s", len(frames), os.path.basename(p))
                jobs.put({"session": session, "split": split, "start": start, "frames": frames,
                          "dtcs": [], "journal": p, "recovered": True})
            else:
                os.remove(p)
        except Exception as e:
            log.error("Could not recover journal %s: %s", p, e)


def writer_loop(cfg, jobs, stop):
    while not (stop.is_set() and jobs.empty()):
        try:
            job = jobs.get(timeout=0.5)
        except queue.Empty:
            continue
        name = mf4writer.file_name(job["split"], job["start"])
        d = os.path.join(BASE, "spool", "%08d" % job["session"])
        os.makedirs(d, exist_ok=True)
        path = os.path.join(d, name)
        try:
            size = mf4writer.write_mf4(path, job["start"], job["frames"], cfg.get("device_id"),
                                       job["session"], job["split"])
            summary = {}
            for e in job["dtcs"]:
                k = "%s SPN %d / FMI %d (SA 0x%02X)" % (e["kind"], e["spn"], e["fmi"], e["sa"])
                summary[k] = summary.get(k, 0) + 1
            with open(path + ".meta.json", "w", encoding="utf-8") as f:
                json.dump({"frames": len(job["frames"]), "dtc_count": len(job["dtcs"]), "dtc": summary}, f)
            try:
                os.remove(job["journal"])
            except OSError:
                pass
            with STATS.lock:
                STATS.files_written += 1
            log.info("Wrote %s: %d frames, %d bytes, %d DTC message(s)", name, len(job["frames"]), size,
                     len(job["dtcs"]))
        except Exception as e:
            log.error("Failed to write %s: %s (journal kept for recovery)", name, e)


# ----------------------------------------------------------------------------- S3 upload
def s3_client(cfg):
    import boto3
    from botocore.config import Config as BConfig
    return boto3.client(
        "s3", endpoint_url=cfg.get("endpoint_url"), region_name=cfg.get("region"),
        config=BConfig(s3={"addressing_style": "path"}, signature_version="s3v4",
                       retries={"max_attempts": 2, "mode": "standard"},
                       connect_timeout=10, read_timeout=30,
                       # boto3>=1.36 otherwise uploads as 'aws-chunked' with a trailing CRC32. S3-compatible
                       # servers that do not decode that (SeaweedFS etc.) store the chunk framing INSIDE the
                       # object, which corrupts every file ("Unable to load block header for type <hex size>").
                       request_checksum_calculation="when_required",
                       response_checksum_validation="when_required"))


def creds_present():
    return bool(os.environ.get("AWS_ACCESS_KEY_ID") and os.environ.get("AWS_SECRET_ACCESS_KEY"))


def classify_error(e):
    """Short, specific description of an S3/network failure (never includes credentials)."""
    name = type(e).__name__
    try:
        from botocore.exceptions import (ClientError, ConnectTimeoutError, EndpointConnectionError,
                                         NoCredentialsError, ReadTimeoutError, SSLError)
        if isinstance(e, ClientError):
            err = e.response.get("Error", {})
            st = e.response.get("ResponseMetadata", {}).get("HTTPStatusCode")
            return "HTTP %s %s: %s" % (st, err.get("Code"), err.get("Message"))
        if isinstance(e, SSLError):
            return "TLS/SSL error: %s" % e
        if isinstance(e, EndpointConnectionError):
            return "cannot connect to endpoint (DNS/socket): %s" % e
        if isinstance(e, (ConnectTimeoutError, ReadTimeoutError)):
            return "timeout: %s" % e
        if isinstance(e, NoCredentialsError):
            return "no AWS credentials found in environment"
    except ImportError:
        pass
    return "%s: %s" % (name, e)


def discover_bucket_session(cfg):
    """Highest numeric session folder under <prefix>/<device_id>/ (read-only listing), or None."""
    cl = s3_client(cfg)
    prefix = "%s/%s/" % (cfg.get("prefix"), cfg.get("device_id"))
    best, token = None, None
    while True:
        kw = dict(Bucket=cfg.get("bucket"), Prefix=prefix, Delimiter="/")
        if token:
            kw["ContinuationToken"] = token
        r = cl.list_objects_v2(**kw)
        for cp in r.get("CommonPrefixes", []):
            seg = cp["Prefix"][len(prefix):].strip("/")
            if seg.isdigit():
                best = max(best or 0, int(seg))
        if not r.get("IsTruncated"):
            return best
        token = r.get("NextContinuationToken")


def upload_gate(cfg):
    """Why uploading is not allowed right now ('' if it is)."""
    if not cfg.get("upload_enabled"):
        return "disabled in config"
    if not creds_present():
        return "AWS_ACCESS_KEY_ID / AWS_SECRET_ACCESS_KEY not set"
    if not load_state().get("s3_test_ok"):
        return "S3 connection test (s3_test.py) has not passed yet"
    return ""


def uploader_loop(cfg, stop):
    client, failures, next_try = None, 0, 0.0
    while not stop.is_set():
        stop.wait(2)
        reason = upload_gate(cfg)
        if reason:
            with STATS.lock:
                STATS.upload_state = "paused: " + reason
            continue
        files = pending_files()
        if not files:
            with STATS.lock:
                STATS.upload_state = "idle (nothing pending)"
            continue
        if time.time() < next_try:
            continue
        with STATS.lock:
            STATS.upload_state = "uploading"
        for p in files:
            session = os.path.basename(os.path.dirname(p))
            key = "%s/%s/%s/%s" % (cfg.get("prefix"), cfg.get("device_id"), session, os.path.basename(p))
            try:
                if client is None:
                    client = s3_client(cfg)
                size = os.path.getsize(p)
                client.upload_file(p, cfg.get("bucket"), key)
                stored = client.head_object(Bucket=cfg.get("bucket"), Key=key)["ContentLength"]
                if stored != size:      # never delete the local copy of a corrupted upload
                    raise RuntimeError("stored object is %d bytes but local file is %d bytes (server altered the "
                                       "upload, e.g. aws-chunked framing)" % (stored, size))
            except Exception as e:
                failures += 1
                msg = classify_error(e)
                delay = min(cfg.get("upload_retry_max_seconds"), 5 * 2 ** min(failures - 1, 10))
                next_try = time.time() + delay
                client = None
                log.error("Upload FAILED for %s: %s (attempt %d, retry in %ds, file kept in spool)",
                          os.path.basename(p), msg, failures, delay)
                with STATS.lock:
                    STATS.upload_error = msg
                    STATS.upload_failures = failures
                    STATS.upload_state = "retrying in %ds" % delay
                break
            failures = 0
            meta = {}
            try:
                with open(p + ".meta.json", encoding="utf-8") as f:
                    meta = json.load(f)
            except (OSError, ValueError):
                pass
            log.info("Uploaded s3://%s/%s (%d bytes)", cfg.get("bucket"), key, size)
            if meta.get("dtc_count"):
                log.log(DTC_LEVEL, "DTC PUSHED to bucket in %s: %d DTC message(s): %s", os.path.basename(p),
                        meta["dtc_count"], "; ".join("%s x%d" % kv for kv in meta["dtc"].items()))
            if cfg.get("after_upload") == "archive":
                ad = os.path.join(BASE, cfg.get("archive_dir"), session)
                os.makedirs(ad, exist_ok=True)
                shutil.move(p, os.path.join(ad, os.path.basename(p)))
            else:
                os.remove(p)
            for extra in (p + ".meta.json",):
                try:
                    os.remove(extra)
                except OSError:
                    pass
            with STATS.lock:
                STATS.files_uploaded += 1
                STATS.bytes_uploaded += size
                STATS.upload_error = ""
                STATS.upload_failures = 0
                STATS.last_upload = "%s %s" % (time.strftime("%H:%M:%S"), os.path.basename(p))


def pending_files():
    return sorted(p for p in glob.glob(os.path.join(BASE, "spool", "*", "*.MF4"))
                  if not p.lower().endswith(".part.mf4") and "_journal" not in p)


# ----------------------------------------------------------------------------- serial reader
class SerialReader(threading.Thread):
    def __init__(self, cfg, rec, stop):
        super().__init__(daemon=True)
        self.cfg, self.rec, self.stop_ev = cfg, rec, stop
        self.dec = j1939.DtcDecoder(cfg.snapshot().get("extra_dtc_pgns", []))
        self.asm = j1939.MessageAssembler()
        self.oversize_seen = set()

    def run(self):
        port, baud = self.cfg.get("serial_port"), self.cfg.get("baud")
        last_err = None
        while not self.stop_ev.is_set():
            try:
                ser = serial.Serial(port, baud, timeout=1)
                try:
                    ser.set_buffer_size(rx_size=262144)
                except Exception:
                    pass
            except Exception as e:
                msg = "Cannot open %s: %s" % (port, e)
                with STATS.lock:
                    STATS.serial_connected, STATS.serial_error = False, msg
                if msg != last_err:
                    log.error("Serial: %s (retrying every 2s)", msg)
                    last_err = msg
                self.stop_ev.wait(2)
                continue
            with STATS.lock:
                STATS.serial_connected, STATS.serial_error = True, ""
            log.info("Serial port %s opened at %d baud", port, baud)
            # Newer simulator firmware only prints "Tx J1939 ..." lines after a LIVE ON command (older firmware
            # ignores it). Opening the port resets an Arduino, so wait for its bootloader before sending.
            self.stop_ev.wait(2.5)
            try:
                ser.write(b"LIVE ON\n")
                ser.flush()
            except Exception as e:
                log.warning("Could not send LIVE ON to the simulator: %s", e)
            last_err = None
            try:
                self._pump(ser)
            except Exception as e:
                log.error("Serial connection lost: %s", e)
            finally:
                with STATS.lock:
                    STATS.serial_connected = False
                try:
                    ser.close()
                except Exception:
                    pass

    def _update_signals(self, msg, now):
        if not msg:
            return
        sa, pgn, payload = msg
        res = signals.decode(msg)
        with STATS.lock:
            for k, v in signals.watch(msg).items():
                STATS.watch_hist[k].append((round(now, 3), v))
            if res is None:
                if pgn not in self.dec.pgns:
                    u = STATS.undecoded.setdefault((pgn, sa), {"count": 0})
                    u.update(count=u["count"] + 1, last=now, data=payload[:16].hex(" ").upper(), length=len(payload))
                return
            acr, desc, sigs = res
            for s in sigs:
                k = (sa, pgn, s["name"])
                e = STATS.signals.setdefault(k, {"count": 0})
                e.update(msg=acr, msg_desc=desc, sa=sa, pgn=pgn, count=e["count"] + 1, last=now, **s)

    def _pump(self, ser):
        last_warn = 0.0
        while not self.stop_ev.is_set():
            raw = ser.readline()
            if not raw:
                continue
            now = time.time()
            text = raw.decode("ascii", errors="replace").strip()
            res = parse_line(text)
            with STATS.lock:
                STATS.lines += 1
                STATS.bytes += len(raw)
            if res[0] == "other":
                with STATS.lock:
                    STATS.other_lines += 1
                STATS.tick_second(0, len(raw))
                continue
            if res[0] == "oversize":
                with STATS.lock:
                    STATS.oversize_lines += 1
                STATS.tick_second(0, len(raw))
                if res[1] not in self.oversize_seen:
                    self.oversize_seen.add(res[1])
                    log.warning("Frame %08X has %d data bytes (classic CAN max is 8): NOT recorded to MF4. "
                                "Further lines with this ID are counted but not logged.", res[1], res[2])
                continue
            if res[0] == "bad":
                with STATS.lock:
                    STATS.bad_lines += 1
                STATS.tick_second(0, len(raw))
                log.warning("Malformed/truncated frame line (possible dropped data): %r", text[:120])
                continue
            _, cid, data, pgn_printed = res
            prio, pgn, sa, da = j1939.parse_id(cid)
            if pgn_printed is not None and pgn_printed != pgn:
                with STATS.lock:
                    STATS.pgn_mismatch += 1
            msg = self.asm.feed(cid, data)          # complete J1939 message (single frame or reassembled TP)
            events = self.dec.events(msg)
            self.rec.add(now, cid, data, events)
            self._update_signals(msg, now)
            with STATS.lock:
                STATS.frames_total += 1
                STATS.frames_session += 1
                STATS.recent.append({"t": time.strftime("%H:%M:%S", time.localtime(now)) + ".%03d" % (now % 1 * 1000),
                                     "id": "%08X" % cid, "prio": prio, "pgn": pgn, "sa": sa,
                                     "data": data.hex(" ").upper()})
                for e in events:
                    STATS.dtc_total += 1
                    k = (e["kind"], e["sa"], e["spn"], e["fmi"])
                    c = STATS.dtc_seen.setdefault(k, [0, "", 0])
                    c[0] += 1
                    c[1], c[2] = time.strftime("%H:%M:%S"), e["oc"]
                    STATS.last_dtc = "%s SPN %d FMI %d SA 0x%02X @ %s" % (e["kind"], e["spn"], e["fmi"], e["sa"], c[1])
            for e in events:
                log.log(DTC_LEVEL, "DTC %s captured: SA=0x%02X SPN=%d FMI=%d OC=%d CM=%d", e["kind"], e["sa"],
                        e["spn"], e["fmi"], e["oc"], e["cm"])
            STATS.tick_second(1, len(raw))
            try:
                waiting = ser.in_waiting
                if waiting > 100000 and now - last_warn > 30:
                    last_warn = now
                    log.warning("Serial receive backlog %d bytes: PC is not keeping up with the link", waiting)
            except Exception:
                pass


# ----------------------------------------------------------------------------- web
def self_dtc_pgns(cfg):
    d = dict(j1939.DTC_PGNS)
    for p in cfg.snapshot().get("extra_dtc_pgns", []):
        d.setdefault(int(p), "DM(0x%X)" % int(p))
    return d


def build_status(cfg, rec):
    c = cfg.snapshot()
    fr, by = STATS.rates()
    baud_bytes = c["baud"] / 10.0
    util = by / baud_bytes * 100 if baud_bytes else 0
    cur = rec.current()
    with STATS.lock:
        dtc_list = sorted(({"kind": k[0], "sa": "0x%02X" % k[1], "spn": k[2], "fmi": k[3], "count": v[0],
                            "last": v[1], "oc": v[2], "name": signals.spn_name(k[2]),
                            "fmi_text": signals.FMI_TEXT.get(k[3], "")} for k, v in STATS.dtc_seen.items()),
                          key=lambda x: x["last"], reverse=True)[:20]
        now = time.time()
        sig_list = sorted(({"msg": e["msg"], "msg_desc": e["msg_desc"], "sa": "0x%02X" % e["sa"],
                            "pgn": e["pgn"], "name": e["name"], "label": e["label"], "spn": e["spn"],
                            "value": e["value"], "unit": e["unit"], "status": e["status"],
                            "count": e["count"], "age": round(now - e["last"], 1)} for e in STATS.signals.values()),
                          key=lambda x: (x["msg"], x["sa"]))
        undec = sorted(({"pgn": k[0], "sa": "0x%02X" % k[1], "name": signals.pgn_name(k[0], self_dtc_pgns(cfg)),
                         "count": v["count"], "length": v["length"], "data": v["data"],
                         "age": round(now - v["last"], 1)} for k, v in STATS.undecoded.items()),
                       key=lambda x: x["pgn"])
        s = {
            "signals": sig_list, "undecoded": undec, "dbc": signals.dbc_info(),
            "header_bytes": mf4writer.BASE_BYTES,
            "uptime": int(time.time() - STATS.started), "session": STATS.session, "device_id": c["device_id"],
            "serial": {"port": c["serial_port"], "baud": c["baud"], "connected": STATS.serial_connected,
                       "error": STATS.serial_error, "lines": STATS.lines, "bad_lines": STATS.bad_lines,
                       "other_lines": STATS.other_lines, "oversize_lines": STATS.oversize_lines, "pgn_mismatch": STATS.pgn_mismatch,
                       "bytes_per_sec": round(by), "utilization_pct": round(util, 1)},
            "frames": {"total": STATS.frames_total, "session": STATS.frames_session, "rate": round(fr, 1)},
            "files": {"written": STATS.files_written, "uploaded": STATS.files_uploaded,
                      "pending": len(pending_files()), "bytes_uploaded": STATS.bytes_uploaded},
            "current_file": cur,
            "upload": {"state": STATS.upload_state, "error": STATS.upload_error, "failures": STATS.upload_failures,
                       "last": STATS.last_upload, "gate": upload_gate(cfg),
                       "target": "s3://%s/%s/%s/%08d/" % (c["bucket"], c["prefix"], c["device_id"], STATS.session)},
            "dtc": {"total": STATS.dtc_total, "last": STATS.last_dtc, "seen": dtc_list},
            "recent": list(STATS.recent)[-20:][::-1],
        }
    s["config"] = {k: c[k] for k in EDITABLE}
    s["limits"] = {k: list(v[1:]) if v[0] is int else None for k, v in EDITABLE.items()}
    return s


def make_handler(cfg, rec):
    page = open(os.path.join(BASE, "web", "index.html"), "rb").read()

    class H(BaseHTTPRequestHandler):
        def log_message(self, *a):
            pass

        def _send(self, code, body, ctype="application/json"):
            if not isinstance(body, bytes):
                body = json.dumps(body).encode()
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def _host_ok(self):
            host = (self.headers.get("Host") or "").split(":")[0]
            return host in ("127.0.0.1", "localhost", "[::1]", cfg.get("web_host"))

        def do_GET(self):
            u = urlparse(self.path)
            if not self._host_ok():
                return self._send(403, {"error": "bad host"})
            if u.path in ("/", "/index.html"):
                return self._send(200, page, "text/html; charset=utf-8")
            if u.path == "/api/status":
                return self._send(200, build_status(cfg, rec))
            if u.path == "/api/watch":
                since = float(parse_qs(u.query).get("since", ["0"])[0] or 0)
                with STATS.lock:
                    ser = {k: {"group": m[0], "label": m[1], "unit": m[2], "min": m[3], "max": m[4],
                               "name": signals.spn_name(signals.WATCH_SPN.get(k, 0)),
                               "points": [[t, v] for t, v in STATS.watch_hist[k] if t > since]}
                           for k, m in signals.WATCH.items()}
                dtcs = [{"switch": switch, "spn": spn, "fmi": fmi, "name": name,
                         "fmi_text": signals.FMI_TEXT.get(fmi, "")}
                        for switch, (spn, fmi, name) in signals.BUTTON_DTCS.items()]
                return self._send(200, {"now": round(time.time(), 3), "series": ser, "dtcs": dtcs})
            if u.path == "/api/logs":
                since = int(parse_qs(u.query).get("since", ["0"])[0] or 0)
                return self._send(200, MEM.since(since))
            self._send(404, {"error": "not found"})

        def do_POST(self):
            if urlparse(self.path).path != "/api/config" or not self._host_ok():
                return self._send(404, {"error": "not found"})
            origin = self.headers.get("Origin")
            if origin and urlparse(origin).hostname not in ("127.0.0.1", "localhost", "::1"):
                return self._send(403, {"error": "bad origin"})
            try:
                body = json.loads(self.rfile.read(int(self.headers.get("Content-Length", 0))) or b"{}")
            except ValueError:
                return self._send(400, {"error": "invalid JSON"})
            changes = {}
            for k, v in body.items():
                spec = EDITABLE.get(k)
                if not spec:
                    return self._send(400, {"error": "setting not editable: %s" % k})
                try:
                    if spec[0] is int:
                        v = int(v)
                        if not spec[1] <= v <= spec[2]:
                            return self._send(400, {"error": "%s must be %d..%d" % (k, spec[1], spec[2])})
                    elif spec[0] is bool:
                        if not isinstance(v, bool):
                            raise ValueError
                    elif v not in spec[1]:
                        raise ValueError
                except (ValueError, TypeError):
                    return self._send(400, {"error": "invalid value for %s" % k})
                changes[k] = v
            if changes.get("upload_enabled"):
                tmp = dict(cfg.snapshot(), **changes)
                probe = Config.__new__(Config)
                probe.d, probe.lock = tmp, threading.Lock()
                reason = upload_gate(probe)
                if reason and "disabled" not in reason:
                    return self._send(400, {"error": "cannot enable upload: " + reason})
            cfg.update(changes)
            log.info("Config changed from web UI: %s", ", ".join("%s=%s" % kv for kv in changes.items()))
            self._send(200, {"ok": True})

    return H


# ----------------------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--config", default=os.path.join(BASE, "config.json"))
    args = ap.parse_args()
    cfg = Config(args.config)
    setup_logging(cfg)
    try:
        mf4writer.check_device_id(cfg.get("device_id"))
    except ValueError as e:
        log.error("%s", e)
        return 1
    stop = threading.Event()
    jobs = queue.Queue()

    state = load_state()
    session = max(int(state.get("last_session", 0)) + 1, int(cfg.get("first_session")))
    if cfg.get("session_from_bucket") and not upload_gate(cfg):
        try:
            b = discover_bucket_session(cfg)
            if b is not None and b + 1 > session:
                log.info("Bucket already has sessions up to %08d, starting at %08d", b, b + 1)
                session = b + 1
        except Exception as e:
            log.error("Could not read existing sessions from bucket: %s", classify_error(e))
    elif cfg.get("session_from_bucket"):
        log.warning("Session number chosen locally (%08d) because bucket access is not enabled yet; "
                    "it may overlap sessions already in the bucket for device %s", session, cfg.get("device_id"))
    save_state(last_session=session)
    STATS.session = session
    log.info("Pipeline starting: device %s, session %08d, port %s @ %d baud", cfg.get("device_id"), session,
             cfg.get("serial_port"), cfg.get("baud"))

    dbc = cfg.snapshot().get("dbc_path")
    if dbc:
        dbc_file = dbc if os.path.isabs(dbc) else os.path.join(BASE, dbc)
        try:
            n = signals.load_dbc(dbc_file)
            log.info("Signal decoding: DBC %s loaded (%d messages)", os.path.basename(dbc_file), n)
        except Exception as e:
            log.error("Could not load DBC %s: %s. Falling back to the built-in signal table.", dbc_file, e)

    rec = Recorder(cfg, session, jobs)
    recover_journals(cfg, jobs)
    wt = threading.Thread(target=writer_loop, args=(cfg, jobs, stop), daemon=True)
    wt.start()
    threading.Thread(target=uploader_loop, args=(cfg, stop), daemon=True).start()
    SerialReader(cfg, rec, stop).start()

    def ticker():
        while not stop.is_set():
            stop.wait(1)
            rec.tick()
    threading.Thread(target=ticker, daemon=True).start()

    try:
        srv = ThreadingHTTPServer((cfg.get("web_host"), cfg.get("web_port")), make_handler(cfg, rec))
    except OSError as e:
        log.error("Cannot start web server on %s:%s: %s", cfg.get("web_host"), cfg.get("web_port"), e)
        return 1
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    log.info("Dashboard: http://%s:%s  (Ctrl+C to stop)", cfg.get("web_host"), cfg.get("web_port"))
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        log.info("Stopping: finishing current file ...")
    stop.set()
    rec.close()
    wt.join(timeout=30)
    srv.shutdown()
    log.info("Stopped. %d file(s) left in spool will be uploaded on next start.", len(pending_files()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
