"""J1939 serial -> MF4 -> S3 pipeline with a local web dashboard.

Run:  python pipeline.py            (stop with Ctrl+C)
Open: http://127.0.0.1:8080
"""
import argparse
import collections
import glob
import hashlib
import json
import logging
import os
import queue
import re
import shutil
import sys
import threading
import tempfile
import time
from contextlib import closing
from pathlib import Path
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from logging.handlers import RotatingFileHandler
from urllib.parse import parse_qs, urlparse

import serial

import j1939
import mf4writer
import signals

BASE = os.path.dirname(os.path.abspath(__file__))
DATA_DIR = BASE


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
    "button_dtc_severity": ("enum", tuple(signals.SEVERITY_LAMP)),
}
# values used when config.json does not have the key yet
DEFAULTS = {"button_dtc_severity": "critical"}


# ----------------------------------------------------------------------------- config / state
class Config:
    def __init__(self, path):
        self.path, self.lock = path, threading.Lock()
        with open(path, encoding="utf-8") as f:
            self.d = json.load(f)
        for k, v in DEFAULTS.items():
            self.d.setdefault(k, v)

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
    return os.path.join(DATA_DIR, "state.json")


def load_state():
    try:
        with open(state_path(), encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def save_state(**kw):
    folder = os.path.join(DATA_DIR, "s3_checks")
    os.makedirs(folder, exist_ok=True)
    # The logger and s3_test are separate processes sharing this read-modify-write.
    with open(os.path.join(folder, "state.lock"), "a+b") as lock:
        if os.name == "nt":
            import msvcrt
            msvcrt.locking(lock.fileno(), msvcrt.LK_LOCK, 1)
        else:
            import fcntl
            fcntl.flock(lock, fcntl.LOCK_EX)
        s = load_state()
        s.update(kw)
        write_receipt(state_path(), s)


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
    os.makedirs(os.path.join(DATA_DIR, "logs"), exist_ok=True)
    fmt = logging.Formatter("%(asctime)s %(levelname)-7s %(message)s")
    fh = RotatingFileHandler(os.path.join(DATA_DIR, "logs", "pipeline.log"), maxBytes=5_000_000,
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
        self.btn_lamp_ack = None                 # lamp byte the simulator confirmed (ACK BTN LAMP), None = not yet
        self.btn_lamp_unsupported = False        # simulator answered "ERR Unknown command: BTN LAMP ..."
        self.last_btn_dtc = None                 # {"severity", "lamp", "spn", "fmi", "t"} of the last DM1 from the simulator

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
        os.makedirs(os.path.join(DATA_DIR, "spool", "_journal"), exist_ok=True)

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
        self.jpath = os.path.join(DATA_DIR, "spool", "_journal", "%08d-%08d.csv" % (self.session, self.split))
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
    for p in sorted(glob.glob(os.path.join(DATA_DIR, "spool", "_journal", "*.csv"))):
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
        d = os.path.join(DATA_DIR, "spool", "%08d" % job["session"])
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


def list_bucket_sessions(cfg, client, device_id):
    """Numeric session prefixes under one device."""
    prefix = "%s/%s/" % (cfg.get("prefix"), device_id)
    sessions, token = set(), None
    while True:
        kw = dict(Bucket=cfg.get("bucket"), Prefix=prefix, Delimiter="/")
        if token:
            kw["ContinuationToken"] = token
        r = client.list_objects_v2(**kw)
        for cp in r.get("CommonPrefixes", []):
            seg = cp["Prefix"][len(prefix):].strip("/")
            if re.fullmatch(r"[0-9]+", seg):
                sessions.add(int(seg))
        if not r.get("IsTruncated"):
            return sessions
        token = r.get("NextContinuationToken")


def s3_timestamp():
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def s3_scope(cfg):
    return {"destination": Path(s3_test_path(cfg)).stem, "device_id": cfg.get("device_id")}


def bucket_session_inventory(cfg, client):
    device_id = cfg.get("device_id")
    sessions = list_bucket_sessions(cfg, client, device_id)
    all_sessions = sessions.copy()
    destination = s3_scope(cfg)["destination"]
    # The hardware and simulator profiles share a session floor for the same destination.
    for path in (Path(BASE, "config.json"), Path(BASE, "local_run", "config.json")):
        if path.resolve() == Path(cfg.path).resolve() or not path.is_file():
            continue
        peer = Config(str(path))
        scope = s3_scope(peer)
        if scope["destination"] == destination and scope["device_id"] != device_id:
            all_sessions.update(list_bucket_sessions(cfg, client, scope["device_id"]))
    return sessions, max(all_sessions, default=None)


def next_session_number(cfg, current, latest=None):
    return max((latest or 0) + 1, (current or 0) + 1, int(cfg.get("first_session")))


def check_s3_session(cfg, client, session, inventory=None):
    sessions, latest = bucket_session_inventory(cfg, client) if inventory is None else inventory
    is_empty = None
    if session is not None:
        prefix = "%s/%s/%08d/" % (cfg.get("prefix"), cfg.get("device_id"), session)
        page = client.list_objects_v2(Bucket=cfg.get("bucket"), Prefix=prefix, MaxKeys=1)
        is_empty = not bool(page.get("Contents"))
        if not is_empty:
            sessions = sessions | {session}
            latest = max(latest or 0, session)
    return {"latest_session": max(sessions, default=None), "latest_profile_session": latest,
            "target_prefix_exists": session in sessions if session is not None else None,
            "target_is_empty": is_empty,
            "next_session": next_session_number(cfg, session, latest)}


def s3_test_path(cfg):
    target = [cfg.get(k) for k in ("endpoint_url", "region", "bucket", "prefix")]
    scope = hashlib.sha256(json.dumps(target).encode()).hexdigest()
    root = BASE if os.path.abspath(DATA_DIR) == os.path.join(BASE, "local_run") else DATA_DIR
    return os.path.join(root, "s3_checks", scope + ".json")


def read_receipt(path):
    try:
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def write_receipt(path, receipt):
    folder = os.path.dirname(path)
    os.makedirs(folder, exist_ok=True)
    f = tempfile.NamedTemporaryFile(mode="w", dir=folder, encoding="utf-8", delete=False)
    try:
        with f:
            json.dump(receipt, f, indent=2)
        # A dashboard poll can briefly hold the destination open on Windows.
        for attempt in range(6):
            try:
                os.replace(f.name, path)
                break
            except PermissionError as e:
                if getattr(e, "winerror", None) not in (5, 32) or attempt == 5:
                    raise
                time.sleep(0.05 * (attempt + 1))
    finally:
        Path(f.name).unlink(missing_ok=True)


def s3_test_result(cfg):
    receipt_path = Path(s3_test_path(cfg))
    result = read_receipt(receipt_path)
    if result:
        return result
    # Older installations stored the result alongside this profile's session counter.
    state = load_state()
    if state.get("s3_test_ok") and not any(receipt_path.parent.glob("?" * 64 + ".json")):
        return {"ok": True, "key": state.get("s3_test_key"), "legacy": True}
    return {}


def mf4_key(cfg, path):
    path = Path(path)
    return "%s/%s/%s/%s" % (cfg.get("prefix"), cfg.get("device_id"), path.parent.name, path.name)


def stream_sha256(stream):
    digest = hashlib.sha256()
    for chunk in iter(lambda: stream.read(1024 * 1024), b""):
        digest.update(chunk)
    return digest.hexdigest()


def verify_mf4(client, cfg, path):
    from botocore.exceptions import ClientError

    bucket, key = cfg.get("bucket"), mf4_key(cfg, path)
    size = os.path.getsize(path)
    # Allow the endpoint a short window to expose an acknowledged write.
    delays = (0.0, 0.25, 0.75, 1.5)
    for attempt, delay in enumerate(delays):
        if delay:
            time.sleep(delay)
        try:
            remote = client.get_object(Bucket=bucket, Key=key)
            break
        except ClientError as e:
            code = e.response.get("Error", {}).get("Code")
            if code not in ("NoSuchKey", "404") or attempt == len(delays) - 1:
                raise
    with closing(remote["Body"]) as body:
        if remote["ContentLength"] != size:
            raise ValueError("S3 length differs from the local MF4 (%d vs %d bytes)" % (remote["ContentLength"], size))
        remote_digest = stream_sha256(body)
    with open(path, "rb") as f:
        if stream_sha256(f) != remote_digest:
            raise ValueError("S3 SHA-256 differs from the local MF4")
    receipt = {"scope": s3_scope(cfg), "uri": "s3://%s/%s" % (bucket, key), "bytes": size, "sha256": remote_digest,
               "verified_at": s3_timestamp()}
    write_receipt(os.path.join(DATA_DIR, "s3_checks", "last_mf4.json"), receipt)
    return receipt


def transfer_receipt_path(source):
    return os.path.join(DATA_DIR, "s3_checks", "transfer_" + source + ".json")


def transfer_mf4(client, cfg, path, source):
    """Send an MF4 and record the outcome of its S3 read-back."""
    from botocore.exceptions import ClientError

    bucket, key = cfg.get("bucket"), mf4_key(cfg, path)
    transfer = {"scope": s3_scope(cfg), "uri": "s3://%s/%s" % (bucket, key),
                "sent": None, "verified": False}
    try:
        if source == "manual":
            with open(path, "rb") as f:
                try:
                    client.put_object(Bucket=bucket, Key=key, Body=f, IfNoneMatch="*")
                    transfer["sent"] = "uploaded"
                except ClientError as e:
                    if e.response.get("ResponseMetadata", {}).get("HTTPStatusCode") != 412:
                        raise
                    transfer["sent"] = "already_exists"
        else:
            client.upload_file(path, bucket, key)
            transfer["sent"] = "uploaded"
        receipt = verify_mf4(client, cfg, path)
        transfer.update(receipt, verified=True)
    except Exception as e:
        transfer["error"] = classify_error(e)
        raise
    finally:
        transfer["checked_at"] = s3_timestamp()
        write_receipt(transfer_receipt_path(source), transfer)
    return transfer


def upload_gate(cfg):
    """Why uploading is not allowed right now ('' if it is)."""
    if not cfg.get("upload_enabled"):
        return "disabled in config"
    if not creds_present():
        return "AWS_ACCESS_KEY_ID / AWS_SECRET_ACCESS_KEY not set"
    if not s3_test_result(cfg).get("ok"):
        return "S3 connection test has not passed for this destination"
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
            if stop.is_set() or upload_gate(cfg):
                break
            session = os.path.basename(os.path.dirname(p))
            key = mf4_key(cfg, p)
            try:
                if client is None:
                    client = s3_client(cfg)
                size = os.path.getsize(p)
                transfer_mf4(client, cfg, p, "automatic")
            except Exception as e:
                failures += 1
                msg = classify_error(e)
                delay = min(cfg.get("upload_retry_max_seconds"), 5 * 2 ** min(failures - 1, 10))
                next_try = time.time() + delay
                client = None
                log.error("Transfer failed for s3://%s/%s: %s (attempt %d, retry in %ds, file kept in spool)",
                          cfg.get("bucket"), key, msg, failures, delay)
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
                ad = os.path.join(DATA_DIR, cfg.get("archive_dir"), session)
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
    return sorted(p for p in glob.glob(os.path.join(DATA_DIR, "spool", "*", "*.MF4"))
                  if not p.lower().endswith(".part.mf4") and "_journal" not in p)


# ----------------------------------------------------------------------------- serial reader
class SerialReader(threading.Thread):
    def __init__(self, cfg, rec, stop):
        super().__init__(daemon=True)
        self.cfg, self.rec, self.stop_ev = cfg, rec, stop
        self.dec = j1939.DtcDecoder(cfg.snapshot().get("extra_dtc_pgns", []))
        self.asm = j1939.MessageAssembler()
        self.oversize_seen = set()
        self.sent_lamp = None                    # lamp byte last sent with BTN LAMP on this connection

    def _sync_button_lamp(self, ser):
        """Send BTN LAMP when the severity chosen on the dashboard differs from what this connection last sent.
        Called from the read loop, so a reconnect (which resets the Arduino) re-sends it automatically."""
        lamp = signals.SEVERITY_LAMP[self.cfg.get("button_dtc_severity")]
        if lamp == self.sent_lamp:
            return
        ser.write(b"BTN LAMP %d\n" % lamp)
        ser.flush()
        self.sent_lamp = lamp
        with STATS.lock:
            STATS.btn_lamp_ack = None
        log.info("Switch DTC severity -> %s: sent BTN LAMP %d (0x%02X) to the simulator",
                 self.cfg.get("button_dtc_severity"), lamp, lamp)

    def _handle_reply(self, text):
        """Log the simulator's answer to BTN LAMP."""
        if text.startswith("ACK BTN LAMP"):
            try:
                v = int(text.split()[-1])
            except ValueError:
                return
            with STATS.lock:
                STATS.btn_lamp_ack, STATS.btn_lamp_unsupported = v, False
            log.info("Simulator confirmed switch DTC severity %s (lamp 0x%02X)", signals.lamp_severity(v), v)
        elif text.startswith("ERR") and "BTN LAMP" in text.upper():
            with STATS.lock:
                first = not STATS.btn_lamp_unsupported
                STATS.btn_lamp_unsupported = True
            if first:
                log.warning("Simulator firmware does not know BTN LAMP: flash the updated "
                            "J1708J1939Simulator_live_v3.ino to change the switch DTC severity")

    def run(self):
        port, baud = self.cfg.get("serial_port"), self.cfg.get("baud")
        tcp = port.startswith("socket://")
        last_err = None
        while not self.stop_ev.is_set():
            try:
                if tcp:
                    ser = serial.serial_for_url(port, timeout=1)
                else:
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
            log.info("Input connected: %s", port)
            self.asm = j1939.MessageAssembler()
            # Newer simulator firmware only prints "Tx J1939 ..." lines after a LIVE ON command (older firmware
            # ignores it). Opening the port resets an Arduino, so wait for its bootloader before sending.
            if not tcp:
                self.stop_ev.wait(2.5)
            try:
                ser.write(b"LIVE ON\n")
                ser.flush()
            except Exception as e:
                log.warning("Could not send LIVE ON to the simulator: %s", e)
            self.sent_lamp = None                # the Arduino reset on open: send the severity again
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
                # Store the final decoded reading in each second, including unavailable markers.
                if "history" not in e and isinstance(s["value"], (int, float)):
                    e["history"] = collections.deque(maxlen=301)
                if "history" in e:
                    history = e["history"]
                    point = (round(now, 3), s["value"])
                    if history and int(history[-1][0]) == int(point[0]):
                        history[-1] = point
                    else:
                        history.append(point)

    def _pump(self, ser):
        last_warn = 0.0
        while not self.stop_ev.is_set():
            self._sync_button_lamp(ser)
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
                self._handle_reply(text)
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
                if events and msg[1] == j1939.PGN_DM1 and msg[2]:
                    lamp = msg[2][0]
                    STATS.last_btn_dtc = {"severity": signals.lamp_severity(lamp), "lamp": "0x%02X" % lamp,
                                          "spn": events[0]["spn"], "fmi": events[0]["fmi"],
                                          "t": time.strftime("%H:%M:%S", time.localtime(now))}
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
    tcp = c["serial_port"].startswith("socket://")
    baud_bytes = 0 if tcp else c["baud"] / 10.0
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
                            "numeric": "history" in e, "count": e["count"], "age": round(now - e["last"], 1)}
                           for e in STATS.signals.values()),
                          key=lambda x: (x["msg"], x["sa"]))
        undec = sorted(({"pgn": k[0], "sa": "0x%02X" % k[1], "name": signals.pgn_name(k[0], self_dtc_pgns(cfg)),
                         "count": v["count"], "length": v["length"], "data": v["data"],
                         "age": round(now - v["last"], 1)} for k, v in STATS.undecoded.items()),
                       key=lambda x: x["pgn"])
        s = {
            "signals": sig_list, "undecoded": undec, "dbc": signals.dbc_info(),
            "simulator_controls": c.get("simulator_controls", False),
            "header_bytes": mf4writer.BASE_BYTES,
            "uptime": int(time.time() - STATS.started), "session": STATS.session, "device_id": c["device_id"],
            "serial": {"port": c["serial_port"], "transport": "tcp" if tcp else "serial",
                       "baud": None if tcp else c["baud"], "connected": STATS.serial_connected,
                       "error": STATS.serial_error, "lines": STATS.lines, "bad_lines": STATS.bad_lines,
                       "other_lines": STATS.other_lines, "oversize_lines": STATS.oversize_lines, "pgn_mismatch": STATS.pgn_mismatch,
                       "bytes_per_sec": round(by), "utilization_pct": None if tcp else round(util, 1)},
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
    s["upload"]["test"] = s3_test_result(cfg)
    s["upload"]["verified_mf4"] = read_receipt(os.path.join(DATA_DIR, "s3_checks", "last_mf4.json"))
    scope = s3_scope(cfg)
    s["upload"]["monitor_verified_mf4"] = (
        s["upload"]["verified_mf4"] if s["upload"]["verified_mf4"].get("scope") == scope else None)
    session_check = load_state().get("s3_session", {})
    if session_check.get("scope") == scope:
        s["upload"]["session_check"] = session_check
    s["upload"]["transfers"] = {}
    for source in ("automatic", "manual"):
        transfer = read_receipt(transfer_receipt_path(source))
        if transfer.get("scope") == scope:
            s["upload"]["transfers"][source] = transfer
    s["limits"] = {k: list(v[1:]) if v[0] is int else None for k, v in EDITABLE.items()}
    return s


def make_handler(cfg, rec):
    from local_simulator import control_catalog
    assets = {}
    for filename, mime in (("index.html", "text/html"), ("dashboard.css", "text/css"),
                           ("dashboard.js", "text/javascript")):
        with open(os.path.join(BASE, "web", filename), "rb") as f:
            assets["/" + filename] = (f.read(), mime + "; charset=utf-8")
    assets["/"] = assets["/index.html"]

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
            if u.path in assets:
                return self._send(200, *assets[u.path])
            if u.path == "/api/controls":
                return self._send(200, control_catalog() if cfg.snapshot().get("simulator_controls") else [])
            if u.path == "/api/trends":
                params = parse_qs(u.query)
                try:
                    window = int(params.get("seconds", ["60"])[0])
                    if window not in (30, 60, 300):
                        raise ValueError
                    keys = {}
                    for key in params.get("signal", [])[:12]:
                        sa, pgn, name = key.split("/", 2)
                        keys[key] = (int(sa, 16), int(pgn), name)
                except ValueError:
                    return self._send(400, {"error": "Invalid chart window or signal"})
                now = time.time()
                with STATS.lock:
                    series = {key: [pt for pt in STATS.signals.get(k, {}).get("history", ())
                                    if now - window <= pt[0] <= now] for key, k in keys.items()}
                return self._send(200, {"now": now, "series": series})
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
                with STATS.lock:
                    ack = STATS.btn_lamp_ack
                    sev = {"selected": cfg.get("button_dtc_severity"), "options": list(signals.SEVERITY_LAMP),
                           "lamps": {k: "0x%02X" % v for k, v in signals.SEVERITY_LAMP.items()},
                           "confirmed": None if ack is None else signals.lamp_severity(ack),
                           "unsupported": STATS.btn_lamp_unsupported,
                           "last_received": STATS.last_btn_dtc}
                return self._send(200, {"now": round(time.time(), 3), "series": ser, "dtcs": dtcs,
                                        "severity": sev})
            if u.path == "/api/logs":
                since = int(parse_qs(u.query).get("since", ["0"])[0] or 0)
                return self._send(200, MEM.since(since))
            self._send(404, {"error": "not found"})

        def do_POST(self):
            path = urlparse(self.path).path
            if path not in ("/api/config", "/api/simulator") or not self._host_ok():
                return self._send(404, {"error": "not found"})
            origin = self.headers.get("Origin")
            if origin and urlparse(origin).hostname not in ("127.0.0.1", "localhost", "::1"):
                return self._send(403, {"error": "bad origin"})
            try:
                body = json.loads(self.rfile.read(int(self.headers.get("Content-Length", 0))) or b"{}")
            except ValueError:
                return self._send(400, {"error": "invalid JSON"})
            if not isinstance(body, dict):
                return self._send(400, {"error": "expected a JSON object"})
            if path == "/api/simulator":
                if not cfg.snapshot().get("simulator_controls"):
                    return self._send(404, {"error": "simulator controls are disabled"})
                from local_simulator import HOST, PORT, send_command
                if cfg.get("serial_port") != f"socket://{HOST}:{PORT}":
                    return self._send(400, {"error": "input must match the local simulator endpoint"})
                cmd = body.get("command")
                verb = cmd.strip().partition(" ")[0].upper() if isinstance(cmd, str) else ""
                if verb not in {"SET", "POT", "SWITCH", "FAULT", "DTC", "RESET"}:
                    return self._send(400, {"error": "invalid simulator command"})
                try:
                    reply = send_command(PORT, cmd)
                except ValueError as e:
                    return self._send(400, {"error": str(e)})
                except OSError as e:
                    return self._send(503, {"error": str(e)})
                if not reply.startswith("ACK "):
                    return self._send(400, {"error": reply or "source disconnected"})
                return self._send(200, {"reply": reply})
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
def main(argv=None):
    global DATA_DIR
    ap = argparse.ArgumentParser()
    ap.add_argument("--config", default=os.path.join(BASE, "config.json"))
    ap.add_argument("--data-dir", default=BASE, help="directory for logs, spool, archive and state")
    args = ap.parse_args(argv)
    DATA_DIR = os.path.abspath(args.data_dir)
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
    last_session = int(state.get("last_session", 0))
    session = next_session_number(cfg, last_session)
    session_check = None
    if cfg.get("session_from_bucket") and creds_present():
        try:
            client = s3_client(cfg)
            inventory = bucket_session_inventory(cfg, client)
            b = inventory[1]
            selected = next_session_number(cfg, last_session, b)
            if selected > session:
                log.info("Matching S3 profiles have sessions up to %08d, starting at %08d", b, selected)
            session = selected
            session_check = check_s3_session(cfg, client, session, inventory)
        except Exception as e:
            log.error("Could not read existing sessions from bucket: %s", classify_error(e))
    elif cfg.get("session_from_bucket"):
        log.warning("Session number chosen locally (%08d) because S3 credentials are unavailable; "
                    "it may overlap sessions already in the bucket for device %s", session, cfg.get("device_id"))
    rec = Recorder(cfg, session, jobs)
    try:
        srv = ThreadingHTTPServer((cfg.get("web_host"), cfg.get("web_port")), make_handler(cfg, rec))
    except OSError as e:
        log.error("Cannot start web server on %s:%s: %s", cfg.get("web_host"), cfg.get("web_port"), e)
        return 1
    changes = {"last_session": session}
    if session_check is not None:
        changes["s3_session"] = dict(session_check, scope=s3_scope(cfg), target_session=session,
                                     checked_at=s3_timestamp())
    save_state(**changes)
    STATS.session = session
    log.info("Pipeline starting: device %s, session %08d, input %s", cfg.get("device_id"), session,
             cfg.get("serial_port"))

    dbc = cfg.snapshot().get("dbc_path")
    if dbc:
        dbc_file = dbc if os.path.isabs(dbc) else os.path.join(BASE, dbc)
        try:
            n = signals.load_dbc(dbc_file)
            log.info("Signal decoding: DBC %s loaded (%d messages)", os.path.basename(dbc_file), n)
        except Exception as e:
            log.error("Could not load DBC %s: %s. Falling back to the built-in signal table.", dbc_file, e)

    recover_journals(cfg, jobs)
    writer_stop = threading.Event()
    wt = threading.Thread(target=writer_loop, args=(cfg, jobs, writer_stop), daemon=True)
    wt.start()
    threading.Thread(target=uploader_loop, args=(cfg, stop), daemon=True).start()
    reader = SerialReader(cfg, rec, stop)
    reader.start()

    def ticker():
        while not stop.wait(1):
            rec.tick()
    threading.Thread(target=ticker, daemon=True).start()

    threading.Thread(target=srv.serve_forever, daemon=True).start()
    log.info("Dashboard: http://%s:%s  (Ctrl+C to stop)", cfg.get("web_host"), cfg.get("web_port"))
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        log.info("Stopping: finishing current file ...")
    stop.set()
    reader.join()
    rec.close()
    writer_stop.set()
    wt.join(timeout=30)
    srv.shutdown()
    srv.server_close()
    log.info("Stopped. %d file(s) in spool.", len(pending_files()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
