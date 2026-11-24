"""Offline self-test (no serial port, no network): parser, DTC decoder (single + BAM + RTS), MF4 round trip."""
import json
import os
import tempfile
from math import isclose

import j1939
import local_simulator
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
for spn, fmi, oc in ((3554, 0, 1), (3107, 0, 2), (17858, 1, 3)):
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

# Hardware and simulator share a destination test, while their MF4 receipts and spool files stay separate.
from hashlib import sha256
from io import BytesIO
from pathlib import Path
from unittest.mock import Mock, patch
from botocore.exceptions import ClientError
import s3_test

with tempfile.TemporaryDirectory() as tmp, patch.object(P, "BASE", tmp), patch.object(P, "DATA_DIR", tmp):
    cfg = P.Config(os.path.join(os.path.dirname(__file__), "config.json"))
    cfg.d.update(upload_enabled=False, device_id="EC000001")
    Path(tmp, "config.json").write_text(json.dumps(cfg.snapshot()), encoding="utf-8")
    P.save_state(last_session=12, existing_field="keep")
    listing = Mock()
    listing.list_objects_v2.side_effect = [
        {"CommonPrefixes": [{"Prefix": cfg.get("prefix") + "/EC000001/00000004/"}],
         "IsTruncated": True, "NextContinuationToken": "next"},
        {"CommonPrefixes": [{"Prefix": cfg.get("prefix") + "/EC000001/00000010/"}]},
        {},
    ]
    listing.put_object.side_effect = lambda **kw: setattr(
        listing.get_object, "return_value", {"Body": BytesIO(kw["Body"])})
    with patch.object(P, "s3_client", return_value=listing), patch.object(s3_test, "check_network"):
        assert s3_test.main([]) == 0
    state = P.load_state()
    check = state["s3_session"]
    assert state["last_session"] == 12 and state["existing_field"] == "keep" and state["s3_test"]["ok"]
    assert (check["latest_session"], check["target_session"], check["target_is_empty"]) == (10, 12, True)
    assert check["next_session"] == max(13, cfg.get("first_session"))
    assert listing.list_objects_v2.call_args_list[1].kwargs["ContinuationToken"] == "next"
    assert listing.list_objects_v2.call_args.kwargs["Prefix"].endswith("/EC000001/00000012/")
    listing.list_objects_v2.side_effect = [{"CommonPrefixes": []}, {"Contents": [{"Key": "object"}]}]
    assert s3_test.record_session_check(cfg, listing)
    check = P.load_state()["s3_session"]
    assert check["latest_session"] == 12 and check["target_is_empty"] is False
    listing.list_objects_v2.side_effect = OSError("listing unavailable")
    assert not s3_test.record_session_check(cfg, listing)
    check = P.load_state()["s3_session"]
    assert check["target_is_empty"] is None and "listing unavailable" in check["error"]

    cfg_dir = Path(tmp, "local_run")
    cfg_dir.mkdir()
    simulator = {**cfg.snapshot(), "device_id": "EC000002", "first_session": 1}
    Path(cfg_dir, "config.json").write_text(json.dumps(simulator), encoding="utf-8")
    sim_cfg = P.Config(str(Path(cfg_dir, "config.json")))
    base_prefix = cfg.get("prefix")
    listing.list_objects_v2.side_effect = [
        {"CommonPrefixes": [{"Prefix": base_prefix + "/EC000002/00000001/"}]},
        {"CommonPrefixes": [{"Prefix": base_prefix + "/EC000001/00000016/"}]},
        {},
    ]
    check = P.check_s3_session(sim_cfg, listing, 1)
    assert check["latest_session"] == 1 and check["latest_profile_session"] == 16
    assert check["target_prefix_exists"] and check["target_is_empty"]
    assert check["next_session"] == 17 == P.next_session_number(sim_cfg, 4, check["latest_profile_session"])
    sim_cfg.d["prefix"] = "different-destination"
    listing.list_objects_v2.reset_mock()
    listing.list_objects_v2.side_effect = [{}]
    assert P.bucket_session_inventory(sim_cfg, listing) == (set(), None)
    listing.list_objects_v2.assert_called_once()

    s3_test.record_test(cfg, True, "probe")
    P.DATA_DIR = os.path.join(tmp, "local_run")
    assert P.s3_test_result(cfg)["ok"] and P.upload_gate(cfg) == "disabled in config"
    cfg.d["upload_enabled"] = True
    assert P.upload_gate(cfg) == ""
    prefix = cfg.get("prefix")
    cfg.d["prefix"] = "other-target"
    assert not P.s3_test_result(cfg)
    cfg.d["prefix"], cfg.d["upload_enabled"] = prefix, False
    path = Path(P.DATA_DIR, "spool", "00000003", "00000001-12345678.MF4")
    path.parent.mkdir(parents=True)
    data = b"MF4 QA test contents"
    path.write_bytes(data)
    client = Mock()
    client.get_object.return_value = {"ContentLength": len(data), "Body": BytesIO(data)}
    s3_test.upload_one(cfg, client, path)
    key = prefix + "/EC000001/00000003/00000001-12345678.MF4"
    assert client.put_object.call_args.kwargs["Key"] == key
    assert client.put_object.call_args.kwargs["IfNoneMatch"] == "*"
    client.get_object.assert_called_once_with(Bucket=cfg.get("bucket"), Key=key)
    receipt = P.read_receipt(Path(P.DATA_DIR, "s3_checks", "last_mf4.json"))
    assert receipt["sha256"] == sha256(data).hexdigest() and path.read_bytes() == data
    transfer = P.read_receipt(P.transfer_receipt_path("manual"))
    assert transfer["sent"] == "uploaded" and transfer["verified"] and transfer["uri"] == receipt["uri"]
    client.get_object.return_value = {"ContentLength": len(data), "Body": BytesIO(data[::-1])}
    try:
        P.transfer_mf4(client, cfg, str(path), "automatic")
    except ValueError as e:
        assert "SHA-256" in str(e)
    else:
        raise AssertionError("Equal-sized corrupt MF4 accepted")
    assert P.read_receipt(Path(P.DATA_DIR, "s3_checks", "last_mf4.json")) == receipt
    transfer = P.read_receipt(P.transfer_receipt_path("automatic"))
    assert transfer["sent"] == "uploaded" and not transfer["verified"] and "SHA-256" in transfer["error"]
    assert P.read_receipt(P.transfer_receipt_path("manual"))["verified"]
    missing = ClientError({"Error": {"Code": "NoSuchKey"}, "ResponseMetadata": {"HTTPStatusCode": 404}}, "GetObject")
    client.reset_mock()
    client.get_object.side_effect = [missing, {"ContentLength": len(data), "Body": BytesIO(data)}]
    blocked = PermissionError("destination temporarily open")
    blocked.winerror = 5
    replace = os.replace
    replace_results = iter([blocked])

    def replace_with_transient_error(src, dst):
        error = next(replace_results, None)
        if error:
            raise error
        replace(src, dst)

    with patch.object(P.os, "replace", side_effect=replace_with_transient_error), patch.object(P.time, "sleep"):
        retried = P.transfer_mf4(client, cfg, str(path), "automatic")
    assert retried["verified"] and client.upload_file.call_count == 1 and client.get_object.call_count == 2
    assert P.read_receipt(P.transfer_receipt_path("automatic")) == retried
    P.save_state(s3_session={"scope": P.s3_scope(cfg), "target_session": 3, "target_is_empty": False})
    upload = P.build_status(cfg, Mock(**{"current.return_value": None}))["upload"]
    assert upload["session_check"]["target_session"] == 3
    assert upload["transfers"]["automatic"] == retried and upload["transfers"]["manual"]["verified"]
    assert upload["monitor_verified_mf4"]["sha256"] == receipt["sha256"]
    cfg.d["prefix"] = "changed-target"
    upload = P.build_status(cfg, Mock(**{"current.return_value": None}))["upload"]
    assert "session_check" not in upload and not upload["transfers"] and upload["monitor_verified_mf4"] is None
print("S3 session discovery, profile sharing, MF4 transfer status and local retention OK")

# Check that live controls survive the same text parser and DTC decoder used by the logger.
sim = local_simulator.Simulator()
controls = local_simulator.control_catalog()
assert len(controls) == len(local_simulator.CHANNELS)
for control in controls:
    channel = control["channel"]
    field = local_simulator.CHANNELS[channel]
    if channel in local_simulator.LEGACY_CHANNELS:
        value = 16
    elif field.size < 8:
        value = 1
    else:
        value = (field.low + field.high) / 2
    raw = round((value - field.offset) / field.scale)
    quantized = raw * field.scale + field.offset
    sim.command(f"SET {channel} {value}", None)
    pgn, sa = control["pgn"], int(control["sa"], 16)
    payload = sim.apply_fields(pgn, sa, b"\xff" * 8)
    decoded = signals.decode((sa, pgn, payload))[2]
    actual = next(s for s in decoded if s["name"] == control["name"])
    assert isclose(actual["value"], quantized, abs_tol=0.00005), (channel, actual, quantized)
    assert actual["unit"] == control["unit"] and actual["label"] == control["label"]
    if channel == "FUEL":
        assert payload[0] == 0xFF and payload[1] == 40
sim.command("RESET", None)
sim.command("SET EnginePtoGovernorEnableSwitch 1", None)
sim.command("SET ERtPtGvPpdSpdCtSwt 0", None)
sim.command("SET PowerTakeoffSpeed 5375.875", None)
pto = sim.apply_fields(0xFEF0, 0, b"\xff" * 8)
assert pto == bytes.fromhex("ffffa7fffff1ffff")
sim.command("SET TrnsmssnOlLvl1CntdwnTmr 4", None)
sim.command("SET TrnsmssnOlLvl1MsrmntStts 3", None)
assert sim.apply_fields(0xFEF8, 3, bytes(8))[-1] == 0x34
assert sim.apply_fields(0xFEF8, 0, bytes(8)) == bytes(8)
sim.command("SET Latitude -12.3456789", None)
latitude = next(data for _, cid, data in sim.trace if j1939.parse_id(cid)[1:3] == (0xFEF3, 0x4A))
position = sim.apply_fields(0xFEF3, 0x4A, latitude)
assert position[4:] == latitude[4:]
assert next(s["value"] for s in signals.decode((0x4A, 0xFEF3, position))[2]
            if s["name"] == "Latitude") == -12.3456789
sim.command("RESET", None)
assert sim.apply_fields(0xFEF3, 0x4A, latitude) == latitude
for _, cid, data in sim.trace:
    parsed = P.parse_line(local_simulator.frame_line(cid, data).decode().strip())
    assert parsed == ("frame", cid, data, j1939.parse_id(cid)[1])
sim.command("POT A4 512", None)
sim.command("SET OILPRESS 240", None)
payload = sim.apply_fields(0xFEEF, 0, b"\xff" * 8)
assert signals.watch((0, 0xFEEF, payload)) == {"fuel_kpa": 500, "oil_kpa": 240}

captured = []
sim.broadcast = lambda cid, data: captured.append((cid, data))
sim.command("LIVE ON", object())
for value in (1, 1, 0, 1):
    sim.command("SWITCH A1 %d" % value, None)
sim.command("DTC ADD 123456 4 2", None)
sim.command("DTC ADD 110 0", None)
sim.send_dm1(sim.active_dtcs, sim.lamp)
dec = j1939.DtcDecoder()
events = [event for cid, data in captured for event in dec.feed(cid, data)]
assert [(e["spn"], e["fmi"], e["oc"]) for e in events] == [
    (3554, 0, 1), (3554, 0, 2), (123456, 4, 2), (110, 0, 1)]
sim.command("RESET", None)
assert not (sim.active_dtcs or sim.dtc_enabled or sim.overrides or sim.pots or sim.switches)
print("Local ECU trace, live overrides, switch edges and multi-fault DM1 OK")
print("selftest OK")
