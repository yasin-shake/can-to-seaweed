"""Standalone S3 connectivity test. Uploads ONE small test object, then (only on success) unlocks uploading.

    python s3_test.py

Reads AWS_ACCESS_KEY_ID / AWS_SECRET_ACCESS_KEY from the environment. Never prints or stores them.
Reports the failing stage precisely: DNS, TCP, TLS, credentials, HTTP status (403 etc.).
"""
import socket
import ssl
import sys
import time
from urllib.parse import urlparse

import pipeline as P


def main():
    cfg = P.Config(P.os.path.join(P.BASE, "config.json"))
    u = urlparse(cfg.get("endpoint_url"))
    host, port = u.hostname, u.port or 443
    print("Endpoint: %s  region: %s  bucket: %s  prefix: %s" % (cfg.get("endpoint_url"), cfg.get("region"),
                                                              cfg.get("bucket"), cfg.get("prefix")))

    try:
        ips = sorted({a[4][0] for a in socket.getaddrinfo(host, port, proto=socket.IPPROTO_TCP)})
        print("[ok]   DNS    %s -> %s" % (host, ", ".join(ips)))
    except OSError as e:
        print("[FAIL] DNS    %s: %s" % (host, e))
        return 1
    try:
        s = socket.create_connection((host, port), timeout=10)
        print("[ok]   TCP    connected to port %d" % port)
    except OSError as e:
        print("[FAIL] TCP    %s:%d: %s" % (host, port, e))
        return 1
    try:
        t = ssl.create_default_context().wrap_socket(s, server_hostname=host)
        print("[ok]   TLS    %s, certificate verified" % t.version())
        t.close()
    except ssl.SSLError as e:
        print("[FAIL] TLS    %s" % e)
        return 1

    if not P.creds_present():
        print("[FAIL] CREDS  AWS_ACCESS_KEY_ID / AWS_SECRET_ACCESS_KEY are not set in this environment")
        return 1
    print("[ok]   CREDS  both environment variables present (values not shown)")

    key = "%s/_test/connection-test-%s.txt" % (cfg.get("prefix"), time.strftime("%Y%m%dT%H%M%SZ", time.gmtime()))
    body = bytes(range(256)) * 4 + b"\r\n0\r\nconnection test\n"        # binary, includes CR/LF bytes
    try:
        cl = P.s3_client(cfg)
        cl.put_object(Bucket=cfg.get("bucket"), Key=key, Body=body)
    except Exception as e:
        print("[FAIL] PUT    s3://%s/%s\n       %s" % (cfg.get("bucket"), key, P.classify_error(e)))
        P.save_state(s3_test_ok=False)
        return 1
    print("[ok]   PUT    s3://%s/%s" % (cfg.get("bucket"), key))
    try:
        back = cl.get_object(Bucket=cfg.get("bucket"), Key=key)["Body"].read()
    except Exception as e:
        print("[FAIL] GET    could not read the test object back: %s" % P.classify_error(e))
        P.save_state(s3_test_ok=False)
        return 1
    if back != body:
        print("[FAIL] ROUND TRIP  sent %d bytes, stored %d bytes; the server altered the upload (first bytes %r)"
              % (len(body), len(back), back[:24]))
        P.save_state(s3_test_ok=False)
        return 1
    print("[ok]   ROUND TRIP  downloaded bytes are identical to what was sent (%d bytes)" % len(body))
    P.save_state(s3_test_ok=True, s3_test_key=key)
    print("S3 test passed. Uploading can now be enabled (config.json 'upload_enabled' or the dashboard).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
