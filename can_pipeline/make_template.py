"""Build templates/canedge_template.bin from a real CANedge MF4 file.

    python make_template.py ../samples/00000002-6A7C4D12.MF4

A CANedge file is: <big header with 73 channel groups> + <empty DT block header> + raw 20-byte records.
The template is everything up to and including that DT block header, so mf4writer.py can append records
and produce files with exactly the structure the CSS Electronics decoders (mdf_iter, mdf2parquet) expect.
Run once; the result is committed next to the code.
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def find_blocks(b):
    seen = {}

    def blk(off):
        if off in seen or off == 0 or b[off:off + 2] != b"##":
            return
        typ = b[off + 2:off + 4].decode()
        ln, nl = struct.unpack_from("<QQ", b, off + 8)
        links = struct.unpack_from("<%dQ" % nl, b, off + 24)
        seen[off] = (typ, ln, links)
        for link in links:
            blk(link)

    blk(64)
    return seen


def main(sample):
    b = open(sample, "rb").read()
    blocks = find_blocks(b)
    dts = [(o, v) for o, v in blocks.items() if v[0] == "DT"]
    assert len(dts) == 1, "expected exactly one DT block, got %d" % len(dts)
    off, (_t, ln, _l) = dts[0]
    assert ln == 24, "DT block is not an unfinalized (header-only) DT; length=%d" % ln
    end = off + 24
    assert (len(b) - end) % 20 == 0, "record area is not a multiple of 20 bytes"
    # the data group must be a single DG with 1-byte record ids
    dgs = [(o, v) for o, v in blocks.items() if v[0] == "DG"]
    assert len(dgs) == 1 and b[dgs[0][0] + 24 + 32] == 1, "expected one DG with record id size 1"
    # the data CG is the one whose record id is 66 and has the most cycles; report the raw fields
    cgs = [(o, struct.unpack_from("<QQ", b, o + 24 + 6 * 8)) for o, v in blocks.items() if v[0] == "CG"]
    for o, (rid, cycles) in cgs:
        if rid == 66:
            print("data CG at %d: record id %d, raw cycle count in header = %d" % (o, rid, cycles))
    os.makedirs(os.path.join(HERE, "templates"), exist_ok=True)
    out = os.path.join(HERE, "templates", "canedge_template.bin")
    with open(out, "wb") as f:
        f.write(b[:end])
    print("template: %d bytes (DT header at %d) from %s -> %s" % (end, off, os.path.basename(sample), out))
    print("HD start time ns:", struct.unpack_from("<Q", b, 64 + 72)[0], "| HD comment MD block at", blocks[64][2][5])


if __name__ == "__main__":
    main(sys.argv[1])
