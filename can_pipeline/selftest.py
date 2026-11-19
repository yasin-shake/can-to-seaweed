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

# --- MF4 round trip
frames = [(1000.0 + i * 0.012, 0x18FE0000 + i, bytes(range(i % 9))) for i in range(300)]
with tempfile.TemporaryDirectory() as t:
    p = os.path.join(t, mf4writer.file_name(1, 1000.0))
    size = mf4writer.write_mf4(p, 1000.0, frames, "4E4BB624", 7, 1)
    m = MDF(p)
    ids = m.get("CAN_DataFrame.ID", group=0)
    assert len(ids.samples) == 300 and int(ids.samples[5]) == 0x18FE0005
    assert abs(ids.timestamps[10] - 0.12) < 1e-3
    assert m.get("CAN_DataFrame.DLC", group=0).samples[5] == 5
    assert bytes(m.get("CAN_DataFrame.DataBytes", group=0).samples[7][:7]) == bytes(range(7))
    assert 1 in m.bus_logging_map["CAN"] and "4E4BB624" in m.header.comment and "session" in m.header.comment
    print("MF4 ok:", os.path.basename(p), size, "bytes,", m.header.start_time)
    m.close()
print("selftest OK")
