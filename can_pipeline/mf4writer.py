"""Write CANedge-style bus-logging MF4 files (one CAN1_Rx_IDE group of CAN_DataFrame records)."""
import os
from datetime import datetime, timezone

import numpy as np
from asammdf import MDF, Signal
from asammdf.blocks import v4_constants as v4c
from asammdf.blocks.source_utils import Source

RECORD_BYTES = 19          # Timestamp(6) + CAN_DataFrame(13), as in the CANedge sample files
BASE_BYTES = 3000          # approximate fixed overhead of a one-group file

_DT = np.dtype([
    ("CAN_DataFrame.BusChannel", "<u1"),
    ("CAN_DataFrame.ID", "<u4"),
    ("CAN_DataFrame.IDE", "<u1"),
    ("CAN_DataFrame.DLC", "<u1"),
    ("CAN_DataFrame.DataLength", "<u1"),
    ("CAN_DataFrame.DataBytes", "u1", (8,)),
    ("CAN_DataFrame.Dir", "<u1"),
])


def estimated_size(n_frames):
    return BASE_BYTES + n_frames * RECORD_BYTES


def file_name(split, start_epoch):
    return "%08d-%08X.MF4" % (split, int(start_epoch))


def _comment(device_id, session, split):
    return (
        "<HDcomment>\n<TX/>\n<common_properties>\n"
        '<tree name="Device Information">\n'
        '<e name="serial number">%s</e>\n'
        '<e name="device type">PC-SERIAL-LOGGER</e>\n'
        "</tree>\n"
        '<tree name="File Information">\n'
        '<e name="session">%8d</e>\n<e name="split">%8d</e>\n'
        "</tree>\n</common_properties>\n</HDcomment>" % (device_id, session, split)
    )


def write_mf4(path, start_epoch, frames, device_id, session, split):
    """frames: list of (epoch_ts, can_id, data_bytes). Writes atomically (tmp + rename)."""
    n = len(frames)
    rec = np.zeros(n, dtype=_DT)
    ts = np.empty(n, dtype=np.float64)
    for i, (t, cid, data) in enumerate(frames):
        ts[i] = max(0.0, round(t - start_epoch, 4))
        rec["CAN_DataFrame.ID"][i] = cid & 0x1FFFFFFF
        d = bytes(data[:8])
        rec["CAN_DataFrame.DLC"][i] = len(d)
        rec["CAN_DataFrame.DataLength"][i] = len(d)
        rec["CAN_DataFrame.DataBytes"][i, :len(d)] = np.frombuffer(d, dtype=np.uint8)
    rec["CAN_DataFrame.BusChannel"] = 1
    rec["CAN_DataFrame.IDE"] = 1
    rec["CAN_DataFrame.Dir"] = 0
    order = np.argsort(ts, kind="stable")        # timestamps must be monotonic
    rec, ts = rec[order], ts[order]

    m = MDF(version="4.11")
    m.header.start_time = datetime.fromtimestamp(start_epoch, tz=timezone.utc)
    m.header.comment = _comment(device_id, session, split)
    src = Source(name="CAN1", path="CAN1", comment="",
                 source_type=v4c.SOURCE_BUS, bus_type=v4c.BUS_TYPE_CAN)
    m.append(Signal(samples=rec, timestamps=ts, name="CAN_DataFrame"),
             acq_name="CAN1_Rx_IDE", acq_source=src, comment="")
    g = m.groups[0]
    g.channel_group.flags = v4c.FLAG_CG_BUS_EVENT | v4c.FLAG_CG_PLAIN_BUS_EVENT
    g.channels[0].name = "Timestamp"
    tmp = path[:-4] + ".part.mf4"      # asammdf forces a .mf4 suffix; pending_files() ignores *.part.mf4
    m.save(tmp, overwrite=True)
    m.close()
    os.replace(tmp, path)
    return os.path.getsize(path)
