"""Write CANedge-compatible MF4 files.

A CANedge log file is a fixed header (73 channel groups; only CAN1_Rx_IDE is used here), an empty
"unfinalized" DT block header, then raw 20-byte records until end of file:

    [record id 0x42][timestamp: 6 bytes, microseconds LE][CAN id: 4 bytes LE][DLC: 1 byte][data: 8 bytes]

BusChannel (1), IDE (1) and Dir (0) are constants stored in the header, not in the records.
The header comes from templates/canedge_template.bin (built from a real CANedge file by make_template.py)
and only the start time, serial number, session and split fields are patched, in place and at the same length.
This is the structure the CSS Electronics decoders (mdf_iter, mdf2parquet_decode) require; files written
by asammdf (float timestamps, no record ids) are readable by asammdf but rejected by them.
"""
import os
import re
import struct

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
with open(os.path.join(HERE, "templates", "canedge_template.bin"), "rb") as _f:
    _TEMPLATE = _f.read()

RECORD_BYTES = 20
BASE_BYTES = len(_TEMPLATE)          # fixed header size (about 105 KB)
RECORD_ID = 66                       # record id of the CAN1_Rx_IDE group in the template
_HD_START_NS = 64 + 72               # HD block at 64: 24-byte header + 6 links, then hd_start_time_ns
_MD_OFFSET = 2616                    # HD comment (device / file information XML)

assert _TEMPLATE[64:68] == b"##HD" and _TEMPLATE[_MD_OFFSET:_MD_OFFSET + 4] == b"##MD"
assert _TEMPLATE[-24:-20] == b"##DT" and struct.unpack_from("<Q", _TEMPLATE, len(_TEMPLATE) - 16)[0] == 24


def estimated_size(n_frames):
    return BASE_BYTES + n_frames * RECORD_BYTES


def file_name(split, start_epoch):
    return "%08d-%08X.MF4" % (split, int(start_epoch))


def check_device_id(device_id):
    if not re.fullmatch(r"[0-9A-Fa-f]{8}", device_id or ""):
        raise ValueError("device_id must be exactly 8 hex characters (like 4E4BB624), got %r" % (device_id,))


def _patch_header(device_id, session, split, start_epoch):
    h = bytearray(_TEMPLATE)
    struct.pack_into("<Q", h, _HD_START_NS, int(start_epoch * 1e6) * 1000)
    end = _MD_OFFSET + struct.unpack_from("<Q", h, _MD_OFFSET + 8)[0]
    md = bytes(h[_MD_OFFSET + 24:end])

    def sub(pattern, value, text):
        new, n = re.subn(pattern, lambda m: m.group(1) + value + m.group(3), text)
        assert n == 1 and len(new) == len(text), "header template field not found: %s" % pattern
        return new

    md = sub(rb'(<e name="serial number"[^>]*>)([^<]*)(</e>)', device_id.upper().encode(), md)
    md = sub(rb'(<e name="session"[^>]*>)([^<]*)(</e>)', b"%8d" % session, md)
    md = sub(rb'(<e name="split"[^>]*>)([^<]*)(</e>)', b"%8d" % split, md)
    h[_MD_OFFSET + 24:end] = md
    return bytes(h)


def write_mf4(path, start_epoch, frames, device_id, session, split):
    """frames: list of (epoch_ts, can_id, data_bytes<=8). Writes atomically (tmp + rename); returns size."""
    check_device_id(device_id)
    n = len(frames)
    rec = np.zeros((n, RECORD_BYTES), dtype=np.uint8)
    ts = np.empty(n, dtype=np.int64)
    ids = np.empty(n, dtype="<u4")
    for i, (t, cid, data) in enumerate(frames):
        ts[i] = int(round((t - start_epoch) * 1e6))
        ids[i] = cid & 0x1FFFFFFF
        d = bytes(data[:8])
        rec[i, 11] = len(d)
        rec[i, 12:12 + len(d)] = np.frombuffer(d, dtype=np.uint8)
    ts = np.clip(ts, 0, (1 << 48) - 1)
    order = np.argsort(ts, kind="stable")                       # timestamps must be monotonic
    rec[:, 0] = RECORD_ID
    rec[:, 1:7] = ts.astype("<u8").view(np.uint8).reshape(n, 8)[:, :6]
    rec[:, 7:11] = ids.view(np.uint8).reshape(n, 4)
    rec = rec[order]
    tmp = path + ".part"                                         # not *.MF4, so the uploader ignores it
    with open(tmp, "wb") as f:
        f.write(_patch_header(device_id, session, split, start_epoch))
        f.write(rec.tobytes())
    os.replace(tmp, path)
    return BASE_BYTES + n * RECORD_BYTES
