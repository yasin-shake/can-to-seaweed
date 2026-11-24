"""Test the configured S3 destination, or upload and verify one completed MF4."""
import argparse
from contextlib import closing
from pathlib import Path
import re
import socket
import ssl
import sys
from urllib.parse import urlparse
from uuid import uuid4

import pipeline as P


def record_test(cfg, ok, key=None, error=None, sent=None):
    result = {"ok": ok, "key": key, "error": error, "sent": sent, "checked_at": P.s3_timestamp()}
    P.write_receipt(P.s3_test_path(cfg), result)
    P.save_state(s3_test=result)


def check_network(cfg):
    url = urlparse(cfg.get("endpoint_url"))
    host = url.hostname
    port = url.port or (443 if url.scheme == "https" else 80)
    ips = sorted({a[4][0] for a in socket.getaddrinfo(host, port, proto=socket.IPPROTO_TCP)})
    print("[ok] DNS", host, ", ".join(ips))
    with socket.create_connection((host, port), timeout=10) as sock:
        print("[ok] TCP port", port)
        if url.scheme == "https":
            with ssl.create_default_context().wrap_socket(sock, server_hostname=host) as tls:
                print("[ok] TLS", tls.version())


def probe(cfg, client):
    key = "%s/_test/connection-test-%s.txt" % (cfg.get("prefix"), uuid4().hex)
    body = bytes(range(256)) * 4 + b"\r\n0\r\nconnection test\n"
    sent = False
    try:
        check_network(cfg)
        client.put_object(Bucket=cfg.get("bucket"), Key=key, Body=body)
        sent = True
        print("[ok] PUT s3://%s/%s" % (cfg.get("bucket"), key))
        with closing(client.get_object(Bucket=cfg.get("bucket"), Key=key)["Body"]) as stream:
            if stream.read() != body:
                raise ValueError("The downloaded probe differs from the original bytes")
    except Exception as e:
        record_test(cfg, False, key, P.classify_error(e), sent)
        raise
    record_test(cfg, True, key, sent=True)
    print("[ok] GET matched all %d probe bytes" % len(body))


def upload_one(cfg, client, path):
    key = P.mf4_key(cfg, path)
    target = "s3://%s/%s" % (cfg.get("bucket"), key)
    print("Selected", path, "->", target, flush=True)
    result = P.transfer_mf4(client, cfg, path, "manual")
    if result["sent"] == "uploaded":
        record_test(cfg, True, key, sent=True)
    else:
        print("Key already exists in S3.")
    print("[ok] Verified %s bytes, SHA-256 %s" % (result["bytes"], result["sha256"]))
    print("Local copy retained:", path)


def record_session_check(cfg, client, error=None):
    session = P.load_state().get("last_session")
    session = int(session) if session is not None else None
    result = {"scope": P.s3_scope(cfg), "target_session": session,
              "latest_session": None, "latest_profile_session": None, "target_prefix_exists": None,
              "target_is_empty": None, "next_session": None}
    if error is None:
        try:
            result.update(P.check_s3_session(cfg, client, session))
        except Exception as e:
            error = P.classify_error(e)
    result.update(error=error, checked_at=P.s3_timestamp())
    P.save_state(s3_session=result)
    if error:
        print("[FAIL] S3 session listing:", error)
        return False
    latest = result["latest_session"]
    print("[ok] Latest S3 session for this device:", "%08d" % latest if latest is not None else "None")
    latest_profile = result["latest_profile_session"]
    print("[ok] Latest S3 session across matching profiles:",
          "%08d" % latest_profile if latest_profile is not None else "None")
    if session is None:
        print("Current target: No local session selected")
    else:
        if not result["target_is_empty"]:
            occupancy = "contains objects in S3"
        elif result["target_prefix_exists"]:
            occupancy = "already listed in S3, with no objects returned"
        else:
            occupancy = "new prefix at check time"
        print("Current target: %08d (%s)" % (session, occupancy))
    print("Next available session at check time: %08d" % result["next_session"])
    return True


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--local", action="store_true", help="use the run_local.py simulator profile")
    parser.add_argument("--verify-file", metavar="SESSION/FILENAME.MF4",
                        help="upload only this closed spooled file, verify it and keep the local copy")
    args = parser.parse_args(argv)
    root = Path(P.BASE) / "local_run" if args.local else Path(P.BASE)
    if not (root / "config.json").is_file():
        parser.error("Profile config missing: %s. Start the corresponding logger once." % (root / "config.json"))
    P.DATA_DIR = str(root)
    cfg = P.Config(str(root / "config.json"))
    print("Config:", cfg.path)
    print("Destination: %s  s3://%s/%s  device %s" % (
        cfg.get("endpoint_url"), cfg.get("bucket"), cfg.get("prefix"), cfg.get("device_id")))
    if args.verify_file:
        spool = (root / "spool").resolve()
        path = (spool / args.verify_file).resolve()
        if (path.parent.parent != spool or not re.fullmatch(r"[0-9]{8}", path.parent.name)
                or not re.fullmatch(r"[0-9]{8}-[0-9A-Fa-f]{8}\.MF4", path.name) or not path.is_file()):
            parser.error("Select a completed MF4 in this profile's spool/<session>/ folder")
    client = None
    transfer_error = None
    try:
        if not P.creds_present():
            raise ValueError("AWS credentials are missing")
        client = P.s3_client(cfg)
        if args.verify_file:
            upload_one(cfg, client, path)
        else:
            probe(cfg, client)
    except Exception as e:
        transfer_error = P.classify_error(e)
        if client is None and not args.verify_file:
            record_test(cfg, False, error=transfer_error, sent=False)
        print("[FAIL]", transfer_error)
    session_ok = record_session_check(cfg, client, transfer_error if client is None else None)
    print("Bulk upload setting for this profile:", "On" if cfg.get("upload_enabled") else "Off")
    return 1 if transfer_error or not session_ok else 0


if __name__ == "__main__":
    sys.exit(main())
