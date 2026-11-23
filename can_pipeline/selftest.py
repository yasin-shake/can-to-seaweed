"""Offline self-test (no serial port, no network): parser, DTC decoder (single + BAM + RTS), MF4 round trip."""
import os
import tempfile

import j1939
import mf4writer
import pipeline as P
from asammdf import MDF

# --- line parser on real simulator output
r = P.parse_line("Tx J1939 CANID[18FEEE00] PGN[0x00FEEE|65262] SRC[00] DST[FF] DATA[80][80][31][2D][31][2D][80][FF]")
assert r[0] == "frame" and r[1] == 0x18FEEE00 and r[2].hex() == "8080312d312d80ff" and r[3] == 0xFEEE, r
r = P.parse_line("Tx J1939 CANID[18ECFF00] PGN[0x00EC00|60416] SRC[00] DST[FF] DATA[20][11][00][03][FF][EC][FE][00]")
assert r[0] == "frame" and j1939.parse_id(r[1])[1] == r[3] == 0xEC00
assert P.parse_line("Tx J1939 CANID[18FEEE00] PGN[0x00FEEE|65262] SRC[00] DST[FF] DATA[80][80][3")[0] == "bad"
assert P.parse_line("Version 1.0.2")[0] == "other"
r = P.parse_line("Tx J1939 CANID[18FD8A00] PGN[0x00FD8A|64906] SRC[00] DST[FF] DATA[02][50][30][31][31][30][85][42][30][32][31][30][08]")
assert r == ("oversize", 0x18FD8A00, 13), r
assert j1939.parse_id(0x18FECA00) == (6, 0xFECA, 0, None)
assert j1939.parse_id(0x18EA0B03) == (6, 0xEA00, 3, 0x0B)

# --- DTC: single-frame DM1 (SPN 110 FMI 0 OC 1)
d = j1939.DtcDecoder()
spn, fmi = 110, 0
dtc = bytes([spn & 0xFF, (spn >> 8) & 0xFF, ((spn >> 16) << 5) | fmi, 1])
ev = d.feed(0x18FECA00, bytes([0x04, 0xFF]) + dtc + b"\xff\xff")
assert ev == [dict(kind="DM1", sa=0, spn=110, fmi=0, oc=1, cm=0)], ev
# no-fault DM1 -> nothing
assert d.feed(0x18FECA00, bytes([0, 0xFF, 0, 0, 0, 0, 0xFF, 0xFF])) == []
# SPN > 16 bit: 0x7F1 + (3<<16), FMI 5, CM 1, OC 2
spn = 0x307F1
dtc = bytes([spn & 0xFF, (spn >> 8) & 0xFF, ((spn >> 16) << 5) | 5, 0x80 | 2])
assert d.feed(0x18FECB05, b"\x00\xff" + dtc + b"\xff\xff")[0]["spn"] == spn
# multi-packet DM1 via BAM from SA 0x0B: 2 DTCs = 10 bytes -> 2 packets
d2 = bytes([0, 0xFF]) + bytes([100, 0, 0x01, 1]) + bytes([200, 0, 0x03, 2])
assert d.feed(0x18ECFF0B, bytes([0x20, 10, 0, 2, 0xFF, 0xCA, 0xFE, 0])) == []
assert d.feed(0x18EBFF0B, bytes([1]) + d2[:7]) == []
ev = d.feed(0x18EBFF0B, bytes([2]) + d2[7:] + b"\xff" * 4)
assert [(e["spn"], e["fmi"], e["sa"]) for e in ev] == [(100, 1, 0x0B), (200, 3, 0x0B)], ev
# non-DTC BAM (vehicle ID, PGN FEEC) is ignored
assert d.feed(0x18ECFF00, bytes([0x20, 0x11, 0, 3, 0xFF, 0xEC, 0xFE, 0])) == []
assert d.feed(0x18EBFF00, bytes([1, 0x31, 0x58, 0x4B, 0x44, 0x50, 0x34, 0x45])) == []

# --- simulator button DTCs: one DM1 single frame per press (lamp 0x10, one DTC, OC in byte 5)
for spn, fmi, oc in ((190, 0, 1), (84, 0, 2), (100, 1, 3)):
    line = "Tx J1939 CANID[18FECA00] PGN[0x00FECA|65226] SRC[00] DST[FF] DATA[10][FF][%02X][%02X][%02X][%02X][FF][FF]" % (
        spn & 0xFF, spn >> 8, ((spn >> 16) << 5) | fmi, oc)
    r = P.parse_line(line)
    assert r[0] == "frame" and r[3] == 0xFECA, r
    assert d.feed(r[1], r[2]) == [dict(kind="DM1", sa=0, spn=spn, fmi=fmi, oc=oc, cm=0)], (spn, fmi)

# --- signal decoding
import signals
acr, _d, sg = signals.decode((0, 0xFEEE, bytes.fromhex("28282022202228FF")))
v = {s["name"]: s for s in sg}
assert acr == "ET1" and v["EngineCoolantTemperature"]["value"] == 0 and abs(v["EngineOilTemperature"]["value"]) < 0.05
assert v["ThermostatOpening"]["status"] == "N/A" and v["ThermostatOpening"]["value"] is None
# EEC1: speed 1500 rpm = raw 12000 (0x2EE0), torque 50% = raw 175
sg = {s["name"]: s["value"] for s in signals.decode((0, 0xF004, bytes([0xF0, 0xFF, 175, 0xE0, 0x2E, 0xFF, 0xFF, 0xFF])))[2]}
assert sg["EngineSpeed"] == 1500.0 and sg["ActualEngineTorque"] == 50, sg
sg = {s["name"]: s["value"] for s in signals.decode((0, 0xFEF7, bytes.fromhex("7D00 2003 7004 FFFF".replace(" ", ""))))[2]}
assert sg["BatteryPotential"] == 56.8 and sg["AlternatorPotential"] == 40.0, sg   # 0x0470*0.05, 0x0320*0.05
assert signals.decode((0, 0x1234, b"\0" * 8)) is None
# VIN via BAM (17 chars + '*' = 18 bytes -> 3 packets)
asm = j1939.MessageAssembler()
vin = b"1XKDP4EX5DJ346527*"
assert asm.feed(0x18ECFF00, bytes([0x20, 18, 0, 3, 0xFF, 0xEC, 0xFE, 0])) is None
res = None
for i in range(3):
    chunk = vin[i * 7:(i + 1) * 7].ljust(7, b"\xff")
    res = asm.feed(0x18EBFF00, bytes([i + 1]) + chunk)
assert res and res[1] == 0xFEEC
assert signals.decode(res)[2][0]["value"] == "1XKDP4EX5DJ346527", signals.decode(res)
# DM6 / DM12 now count as DTC sources
assert j1939.DtcDecoder().feed(0x18FECF00, bytes.fromhex("5555E8031600FFFF"))[0]["kind"] == "DM6"

# --- DBC decoding (CSS Electronics J1939 DBC)
import json
_cfg = json.load(open(os.path.join(P.BASE, "config.json")))
n = signals.load_dbc(os.path.join(P.BASE, _cfg["dbc_path"]))
assert n > 2000, n
acr, desc, sg = signals.decode((0, 0xFEEE, bytes.fromhex("28282022202228FF")))
v = {s["name"]: s for s in sg}
assert acr == "ET1" and desc == "Engine Temperature 1"
assert v["EngineCoolantTemperature"]["value"] == 0 and v["EngineCoolantTemperature"]["spn"] == 110
assert v["EngineCoolantTemperature"]["label"] == "Engine Coolant Temperature" and v["EngineCoolantTemperature"]["unit"] == "°C"
assert v["EngnChrgArClrThrmsttOpnng"]["status"] == "N/A" and v["EngnChrgArClrThrmsttOpnng"]["value"] is None
sg = {s["name"]: s["value"] for s in signals.decode((0, 0xF004, bytes([0xF3, 0xFF, 175, 0xE0, 0x2E, 0xFF, 0xFF, 0xFF])))[2]}
assert sg["EngineSpeed"] == 1500 and sg["ActualEnginePercentTorque"] == 50, sg
assert signals.decode((0, 0xFEF7, bytes([0x7D]) + b"\xff" + bytes.fromhex("20037004FFFF")))[2][0]["status"] in ("", "N/A")
# message missing from the DBC (VIN) still decodes via the built-in text decoder; short payloads do not crash
assert signals.decode(res)[2][0]["value"] == "1XKDP4EX5DJ346527"
assert signals.decode((0, 0xFEEE, b"\x28\x28")) is not None
assert signals.decode((0, 0xFEDA, b"\x01" + b"123456789ABCDE*"))[2][0]["value"] == "123456789ABCDE"
print("DBC ok:", signals.dbc_info())

# --- MF4 round trip (CANedge layout)
import struct
frames = [(1000.0 + i * 0.012, 0x18FE0000 + i, bytes(range(i % 9))) for i in range(300)]
frames.append((1000.005, 0x18FEEE00, bytes.fromhex("28282022202228ff")))        # out of order on purpose
with tempfile.TemporaryDirectory() as t:
    p = os.path.join(t, mf4writer.file_name(7, 1000.0))
    size = mf4writer.write_mf4(p, 1000.0, frames, "4E4BB624", 7, 3)
    raw = open(p, "rb").read()
    assert len(raw) == size == mf4writer.estimated_size(301), (len(raw), size)
    assert not os.path.exists(p + ".part")
    # header: identical to the CANedge template except HD start time + serial/session/split text
    tpl = open(os.path.join(P.BASE, "templates", "canedge_template.bin"), "rb").read()
    diff = [i for i in range(len(tpl)) if raw[i] != tpl[i]]
    old_serial = mf4writer._TEMPLATE_SERIAL
    serial_spots = {j + k for j in [m_.start() for m_ in __import__('re').finditer(old_serial, tpl)] for k in range(8)}
    assert diff and all((136 <= i < 144) or (2640 <= i < 2616 + 902) or i in serial_spots for i in diff), diff[:10]
    assert old_serial not in raw and raw.count(b'4E4BB624') == len(serial_spots) // 8, 'serial not replaced everywhere'
    assert raw[len(tpl) - 24:len(tpl) - 20] == b"##DT" and raw[len(tpl)] == 66          # records start with record id
    m = MDF(p)
    ids = m.get("CAN_DataFrame.ID", group=23)
    assert len(ids.samples) == 301 and abs(float(ids.timestamps[0])) < 1e-6
    assert all(ids.timestamps[i] <= ids.timestamps[i + 1] for i in range(300)), "timestamps not monotonic"
    k = [i for i in range(301) if int(ids.samples[i]) == 0x18FE0005][0]
    assert abs(ids.timestamps[k] - 0.06) < 1e-5 and m.get("CAN_DataFrame.DLC", group=23).samples[k] == 5
    assert bytes(m.get("CAN_DataFrame.DataBytes", group=23).samples[k][:5]) == bytes(range(5))
    assert int(m.get("CAN_DataFrame.BusChannel", group=23).samples[0]) == 1 and int(m.get("CAN_DataFrame.IDE", group=23).samples[0]) == 1
    assert m.bus_logging_map["CAN"].get(1) is not None
    assert "4E4BB624" in m.header.comment and "      %d" % 7 in m.header.comment and "       3" in m.header.comment
    assert int(m.header.start_time.timestamp()) == 1000
    print("MF4 ok:", os.path.basename(p), size, "bytes,", len(diff), "header bytes patched")
    m.close()
try:
    mf4writer.check_device_id("4E4BB62")
    raise SystemExit("device id check failed")
except ValueError:
    pass
# --- S3 uploads must NOT be aws-chunked (servers that do not decode it store the framing inside the object)
class _Stop(Exception):
    pass
os.environ.setdefault("AWS_ACCESS_KEY_ID", "dummy")
os.environ.setdefault("AWS_SECRET_ACCESS_KEY", "dummy")
_cl = P.s3_client(P.Config(os.path.join(P.BASE, "config.json")))
_seen = {}
def _hook(request, **kw):
    _seen["h"] = {k.lower(): str(v) for k, v in request.headers.items()}
    raise _Stop()
_cl.meta.events.register("before-send.s3.PutObject", _hook)
try:
    _cl.put_object(Bucket="b", Key="k", Body=b"x" * 100)
except _Stop:
    pass
assert "aws-chunked" not in _seen["h"].get("content-encoding", ""), _seen["h"]
assert "x-amz-trailer" not in _seen["h"] and _seen["h"]["content-length"] == "100", _seen["h"]
print("S3 PUT is a plain (non-chunked) request")
print("selftest OK")
