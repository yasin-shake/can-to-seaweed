"""Convert MF4 files written by the earlier version of this pipeline (asammdf layout, which the CSS
decoders reject) into the CANedge layout.

    python convert_legacy.py FILE.MF4 [FILE2.MF4 ...]          # writes converted copies to ./converted/
    python convert_legacy.py --in-place FILE.MF4 [...]         # replaces each file; keeps FILE.MF4.legacy

Files that are already in CANedge layout are skipped. Nothing is uploaded or downloaded by this tool.
"""
import os
import re
import shutil
import sys

from asammdf import MDF

import mf4writer


def convert(src, dst):
    m = MDF(src)
    try:
        if len(m.groups) != 1 or "CAN_DataFrame.ID" not in [c.name for c in m.groups[0].channels]:
            return "skipped (not a legacy single-group file)"
        comment = m.header.comment or ""
        serial = re.search(r'name="serial number"[^>]*>([^<]*)<', comment)
        session = re.search(r'name="session"[^>]*>\s*(\d+)\s*<', comment)
        split = re.search(r'name="split"[^>]*>\s*(\d+)\s*<', comment)
        if not (serial and session and split):
            return "skipped (header lacks serial/session/split)"
        start = m.header.start_time.timestamp()
        ids = m.get("CAN_DataFrame.ID", group=0)
        dlc = m.get("CAN_DataFrame.DLC", group=0).samples
        data = m.get("CAN_DataFrame.DataBytes", group=0).samples
        frames = [(start + float(t), int(i), bytes(d[:int(n)]))
                  for t, i, n, d in zip(ids.timestamps, ids.samples, dlc, data)]
    finally:
        m.close()
    mf4writer.write_mf4(dst, start, frames, serial.group(1).strip(), int(session.group(1)), int(split.group(1)))
    return "converted %d frames" % len(frames)


def main(argv):
    in_place = "--in-place" in argv
    files = [a for a in argv if not a.startswith("--")]
    if not files:
        print(__doc__)
        return 1
    outdir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "converted")
    for f in files:
        if in_place:
            tmp = f + ".converting"
            res = convert(f, tmp)
            if res.startswith("converted"):
                shutil.copy2(f, f + ".legacy")
                os.replace(tmp, f)
            elif os.path.exists(tmp):
                os.remove(tmp)
        else:
            os.makedirs(outdir, exist_ok=True)
            res = convert(f, os.path.join(outdir, os.path.basename(f)))
        print("%s: %s" % (os.path.basename(f), res))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
