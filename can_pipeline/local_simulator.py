"""Loopback J1939 text source using a timed trace captured by the Windows logger."""
import argparse
import asyncio
import csv
import os
import socket
import sys
import threading
from pathlib import Path
from typing import NamedTuple

import j1939
from signals import BUTTON_DTCS, MESSAGES


TRACE = Path(__file__).with_name("simulator_trace.csv")
HOST = "127.0.0.1"
PORT = 19390

# Signal names from the logger's built-in J1939 definitions.
LEGACY_CHANNELS = {
    "COOLANT": ("EngineCoolantTemperature", -40, 210),
    "OILPRESS": ("EngineOilPressure", 0, 1000),
    "BOOST": ("BoostPressure", 0, 500),
    "VOLT": ("BatteryPotential", 0, 32),
    "TRANSOIL": ("TransmissionOilTemperature", -40, 210),
    "FUEL": ("FuelLevel1", 0, 100),
    "AMBIENT": ("AmbientAirTemperature", -40, 210),
    "RPM": ("EngineSpeed", 0, 8000),
    "SPEED": ("WheelBasedVehicleSpeed", 0, 250),
}


class Control(NamedTuple):
    name: str
    pgn: int
    spn: int
    start: int
    size: int
    scale: float
    offset: float
    low: float
    high: float


def legacy_control(field, low, high):
    for pgn, (_, _, fields) in MESSAGES.items():
        for name, _, spn, byte, length, scale, offset, _ in fields:
            if name == field:
                return Control(name, pgn, spn, byte * 8, length * 8, scale, offset, low, high)
    raise ValueError(f"No J1939 definition for {field}")


# Selected bgt.platform.telemetry.parameters fields from the bundled J1939 DBC.
# Keep the layouts here so native and WSL producers both run with the standard library.
# Name, PGN, SPN, start bit, bit length, scale, offset, minimum, maximum.
TELEMETRY_CONTROLS = (
    ("EngnIntkMnfld1AsltPrssrHghRsltn", 0xFDD0, 4817, 40, 16, 0.1, 0, 0, 6425.5),
    ("TtlVhlDstnHghRsltn", 0xFEC1, 917, 0, 32, 5, 0, 0, 21055406075),
    ("TripDistanceHighResolution", 0xFEC1, 918, 32, 32, 5, 0, 0, 21055406075),
    ("TotalVehicleDistance", 0xFEE0, 245, 32, 32, 0.125, 0, 0, 526385151.875),
    ("EngineTotalHoursOfOperation", 0xFEE5, 247, 0, 32, 0.05, 0, 0, 210554060.75),
    ("TotalVehicleHours", 0xFEE7, 246, 0, 32, 0.05, 0, 0, 210554060.75),
    ("TotalPowerTakeoffHours", 0xFEE7, 248, 32, 32, 0.05, 0, 0, 210554060.75),
    ("EngineFuel1Temperature1", 0xFEEE, 174, 8, 8, 1, -40, -40, 210),
    ("EngineOilTemperature1", 0xFEEE, 175, 16, 16, 0.03125, -273, -273, 1734.96875),
    ("EngineIntercoolerTemperature", 0xFEEE, 52, 48, 8, 1, -40, -40, 210),
    ("EngnExtnddCrnksBlwBPrssr", 0xFEEF, 22, 8, 8, 0.05, 0, 0, 12.5),
    ("PowerTakeoffSpeed", 0xFEF0, 186, 8, 16, 0.125, 0, 0, 8031.875),
    ("PowerTakeoffSetSpeed", 0xFEF0, 187, 24, 16, 0.125, 0, 0, 8031.875),
    ("EnginePtoGovernorEnableSwitch", 0xFEF0, 980, 40, 2, 1, 0, 0, 3),
    ("ERtPtGvPpdSpdCtSwt", 0xFEF0, 979, 42, 2, 1, 0, 0, 3),
    ("EnginePtoGovernorResumeSwitch", 0xFEF0, 982, 52, 2, 1, 0, 0, 3),
    ("OprtrEngnPtGvrnrMmrSltSwth", 0xFEF0, 2897, 56, 2, 1, 0, 0, 3),
    ("RtPtGvPpdSpdCtSwt2", 0xFEF0, 3447, 58, 2, 1, 0, 0, 3),
    ("EnginePtoGovernorDisableSwitch", 0xFEF0, 8639, 62, 2, 1, 0, 0, 3),
    ("CruiseControlStates", 0xFEF1, 527, 53, 3, 1, 0, 0, 7),
    ("Latitude", 0xFEF3, 584, 0, 32, 1e-7, -210, -90, 90),
    ("CabInteriorTemperature", 0xFEF5, 170, 8, 16, 0.03125, -273, -273, 1734.96875),
    ("EngineIntake1AirTemperature", 0xFEF5, 172, 40, 8, 1, -40, -40, 210),
    ("EngineIntakeAirPressure", 0xFEF6, 106, 24, 8, 2, 0, 0, 500),
    ("EngnArFltr1DffrntlPrssr", 0xFEF6, 107, 32, 8, 0.05, 0, 0, 12.5),
    ("EngineExhaustTemperature", 0xFEF6, 173, 40, 16, 0.03125, -273, -273, 1734.96875),
    ("EngnClntFltrDffrntlPrssr", 0xFEF6, 112, 56, 8, 0.5, 0, 0, 125),
    ("SliBattery1NetCurrent", 0xFEF7, 114, 0, 8, 1, -125, -125, 125),
    ("AlternatorCurrent", 0xFEF7, 115, 8, 8, 1, 0, 0, 250),
    ("ChargingSystemPotentialVoltage", 0xFEF7, 167, 16, 16, 0.05, 0, 0, 3212.75),
    ("KeySwitchBatteryPotential", 0xFEF7, 158, 48, 16, 0.05, 0, 0, 3212.75),
    ("TransmissionClutch1Pressure", 0xFEF8, 123, 0, 8, 16, 0, 0, 4000),
    ("TrnsmssnFltrDffrntlPrssr", 0xFEF8, 126, 16, 8, 2, 0, 0, 500),
    ("Transmission1OilPressure", 0xFEF8, 127, 24, 8, 16, 0, 0, 4000),
    ("TransmissionOilLevel1HighLow", 0xFEF8, 3027, 48, 8, 0.5, -62.5, -62.5, 62.5),
    ("TrnsmssnOlLvl1CntdwnTmr", 0xFEF8, 3028, 56, 4, 1, 0, 0, 13),
    ("TrnsmssnOlLvl1MsrmntStts", 0xFEF8, 3026, 60, 4, 1, 0, 0, 15),
    ("WasherFluidLevel", 0xFEFC, 80, 0, 8, 0.4, 0, 0, 100),
    ("EngnOlFltrDffrntlPrssr", 0xFEFC, 99, 24, 8, 0.5, 0, 0, 125),
    ("CargoAmbientTemperature", 0xFEFC, 169, 32, 16, 0.03125, -273, -273, 1734.96875),
)
CHANNELS = {channel: legacy_control(*args) for channel, args in LEGACY_CHANNELS.items()}
CHANNELS.update({field[0].upper(): Control(*field) for field in TELEMETRY_CONTROLS})

POTS = {"A0": 0, "A2": 2, "A4": 4}
SWITCHES = {"A1": 0, "A3": 1, "A5": 2}
CONTROL_SOURCES = {0xFEF3: 0x4A, 0xFEF8: 0x03}
EXTRA_IDS = {0xF004: 0x0CF00400, 0xFEF0: 0x18FEF000, 0xFEF1: 0x18FEF100}


def control_catalog():
    """Match each editable field to the active decoder by PGN and SPN."""
    import signals
    result = []
    for channel, field in CHANNELS.items():
        sa = CONTROL_SOURCES.get(field.pgn, 0)
        decoded = signals.decode((sa, field.pgn, bytes(8)))
        if decoded is None:
            continue
        signal = next((s for s in decoded[2] if s["name"] == field.name or s["spn"] == field.spn), None)
        if signal:
            result.append(dict(channel=channel, pgn=field.pgn, sa=f"0x{sa:02X}", min=field.low,
                               max=field.high, step=field.scale,
                               **{k: signal[k] for k in ("name", "label", "unit", "spn")}))
    return result


def dtc_bytes(args):
    spn, fmi = map(int, args[:2])
    oc = int(args[2]) if len(args) == 3 else 1
    if not (1 <= spn < 524287 and 0 <= fmi <= 31 and 0 <= oc <= 126):
        raise ValueError("use SPN 1..524286, FMI 0..31, OC 0..126")
    return bytes((spn & 255, (spn >> 8) & 255, ((spn >> 16) << 5) | fmi, oc))


def send_command(port, command):
    if not command or len(command) > 128 or not command.isascii() or not command.isprintable():
        raise ValueError("expected one ASCII command")
    with socket.create_connection((HOST, port), timeout=3) as conn:
        conn.sendall((command + "\n").encode("ascii"))
        with conn.makefile("rb") as stream:
            return stream.readline(1024).decode("ascii").strip()


def frame_line(cid, data):
    _, pgn, sa, da = j1939.parse_id(cid)
    payload = "".join(f"[{b:02X}]" for b in data)
    return (f"Tx J1939 CANID[{cid:08X}] PGN[0x{pgn:06X}|{pgn}] "
            f"SRC[{sa:02X}] DST[{0xff if da is None else da:02X}] DATA{payload}\n").encode("ascii")


class Simulator:
    def __init__(self):
        with TRACE.open(encoding="ascii", newline="") as src:
            self.trace = [(int(r["delay_us"]) / 1e6, int(r["can_id"], 16), bytes.fromhex(r["data_hex"]))
                          for r in csv.DictReader(src)]
        self.subscribers = set()
        self.overrides = {}
        self.pots = {}
        self.switches = {}
        self.presses = dict.fromkeys(SWITCHES, 0)
        self.active_dtcs = []
        self.dtc_enabled = False
        self.lamp = 0

    def broadcast(self, cid, data):
        line = frame_line(cid, data)
        for client in self.subscribers.copy():
            if client.is_closing() or client.transport.get_write_buffer_size() > 65536:
                self.subscribers.discard(client)
                client.close()
            else:
                client.write(line)

    async def produce(self):
        clock = asyncio.get_running_loop()
        due = clock.time()
        while True:
            for delay, cid, data in self.trace:
                _, pgn, sa, _ = j1939.parse_id(cid)
                if cid == self.trace[0][1]:
                    for extra, extra_id in EXTRA_IDS.items():
                        if extra in self.overrides:
                            self.broadcast(extra_id, self.apply_fields(extra, 0, b"\xff" * 8))
                    if self.dtc_enabled:
                        self.send_dm1(self.active_dtcs, self.lamp)
                if pgn == 0xFF00:
                    status = bytearray(data)
                    for pin, raw in self.pots.items():
                        start = POTS[pin]
                        status[start:start + 2] = raw.to_bytes(2, "little")
                    for pin, value in self.switches.items():
                        mask = 1 << SWITCHES[pin]
                        status[6] = (status[6] & ~mask) | (value << SWITCHES[pin])
                    data = bytes(status)
                self.broadcast(cid, self.apply_fields(pgn, sa, data))
                due += delay
                # Resume from the current time after host sleep or suspension.
                if clock.time() - due > 0.25:
                    due = clock.time()
                await asyncio.sleep(max(0, due - clock.time()))

    def set_field(self, pgn, start, size, raw):
        self.overrides.setdefault(pgn, {})[start, size] = raw

    def apply_fields(self, pgn, sa, data):
        fields = self.overrides.get(pgn)
        if not fields or sa != CONTROL_SOURCES.get(pgn, 0):
            return data
        payload = int.from_bytes(data.ljust(8, b"\xff"), "little")
        for (start, size), raw in fields.items():
            mask = ((1 << size) - 1) << start
            payload = (payload & ~mask) | (raw << start)
        return payload.to_bytes(8, "little")

    def send_dm1(self, codes, lamp):
        payload = bytes((lamp, 0xFF)) + (b"".join(codes) or bytes(4))
        if len(payload) <= 8:
            self.broadcast(0x18FECA00, payload.ljust(8, b"\xff"))
            return
        # Emit at a trace cycle boundary, outside any recorded transport session.
        count = (len(payload) + 6) // 7
        self.broadcast(0x18ECFF00, bytes((0x20, len(payload), 0, count, 0xFF, 0xCA, 0xFE, 0)))
        for index in range(count):
            chunk = payload[index * 7:(index + 1) * 7]
            self.broadcast(0x18EBFF00, bytes((index + 1,)) + chunk.ljust(7, b"\xff"))

    def fault(self, code):
        if not self.subscribers:
            raise ValueError("no live receiver for one-shot fault")
        self.send_dm1([code], 0x10)

    def command(self, line, writer):
        words = line.upper().split()
        if words == ["LIVE", "ON"]:
            self.subscribers.add(writer)
        elif words == ["LIVE", "OFF"]:
            self.subscribers.discard(writer)
        elif words == ["RESET"]:
            self.overrides.clear()
            self.pots.clear()
            self.switches.clear()
            self.presses = dict.fromkeys(SWITCHES, 0)
            self.active_dtcs.clear()
            self.dtc_enabled = False
            self.lamp = 0
        elif len(words) == 3 and words[0] == "POT" and words[1] in POTS:
            pin, raw = words[1], int(words[2])
            if not 0 <= raw <= 1023:
                raise ValueError("POT range is 0..1023")
            self.pots[pin] = raw
            fraction = raw / 1023
            if pin == "A0":
                self.set_field(0xFDD0, 0, 16, round(fraction * 500 * 128))
            elif pin == "A2":
                self.set_field(0xFEEE, 0, 8, round(fraction * 150) + 40)
            else:
                for start in (0, 24):
                    self.set_field(0xFEEF, start, 8, round(fraction * 250))
        elif len(words) == 3 and words[0] == "SWITCH" and words[1] in SWITCHES:
            pin, value = words[1], int(words[2])
            if value not in (0, 1):
                raise ValueError("SWITCH range is 0 or 1")
            if value and not self.switches.get(pin):
                spn, fmi, _ = BUTTON_DTCS[pin]
                oc = min(self.presses[pin] + 1, 126)
                self.fault(dtc_bytes([spn, fmi, oc]))
                self.presses[pin] = oc
            self.switches[pin] = value
        elif len(words) == 3 and words[0] == "SET" and words[1] in CHANNELS:
            field = CHANNELS[words[1]]
            value = float(words[2])
            if not field.low <= value <= field.high:
                raise ValueError(f"{words[1]} range is {field.low}..{field.high}")
            raw = round((value - field.offset) / field.scale)
            self.set_field(field.pgn, field.start, field.size, raw)
        elif words[:1] == ["FAULT"] and len(words) in (3, 4):
            self.fault(dtc_bytes(words[1:]))
        elif words[:2] == ["DTC", "ADD"] and len(words) in (4, 5):
            if len(self.active_dtcs) >= 8:
                raise ValueError("active DTC list is full")
            self.active_dtcs.append(dtc_bytes(words[2:]))
            self.dtc_enabled = True
        elif words == ["DTC", "CLEAR"]:
            self.active_dtcs.clear()
        elif words in (["DTC", "ON"], ["DTC", "OFF"]):
            self.dtc_enabled = words[1] == "ON"
        elif words[:2] == ["DTC", "LAMP"] and len(words) == 3:
            lamp = int(words[2])
            if not 0 <= lamp <= 255:
                raise ValueError("lamp byte must be 0..255")
            self.lamp = lamp
        else:
            raise ValueError("use LIVE ON/OFF, POT, SWITCH, SET, FAULT, DTC, or RESET")
        return "ACK " + " ".join(words)

    async def handle(self, reader, writer):
        try:
            while raw := await reader.readline():
                try:
                    reply = self.command(raw.decode("ascii"), writer)
                except ValueError as e:
                    reply = "ERR " + str(e)
                writer.write((reply + "\n").encode("ascii", errors="replace"))
                await writer.drain()
        except (ConnectionError, ValueError):
            pass
        finally:
            self.subscribers.discard(writer)
            writer.close()


async def serve(port, managed):
    sim = Simulator()
    server = await asyncio.start_server(sim.handle, HOST, port, limit=1024)
    stop = asyncio.Event()
    loop = asyncio.get_running_loop()
    if managed:
        def watch_parent():
            os.read(sys.stdin.fileno(), 1)
            loop.call_soon_threadsafe(stop.set)
        threading.Thread(target=watch_parent, daemon=True).start()
    async with server:
        print(f"Listening on {HOST}:{server.sockets[0].getsockname()[1]}", flush=True)
        producer = asyncio.create_task(sim.produce())
        try:
            await stop.wait()
        finally:
            producer.cancel()
            await asyncio.gather(producer, return_exceptions=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=PORT)
    parser.add_argument("--managed", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--send", nargs="+", metavar="WORD", help="send a command to the running source")
    args = parser.parse_args()
    try:
        if args.send:
            reply = send_command(args.port, " ".join(args.send))
            print(reply)
            return 0 if reply.startswith("ACK ") else 1
        asyncio.run(serve(args.port, args.managed))
    except KeyboardInterrupt:
        pass
    except (OSError, ValueError) as e:
        parser.exit(1, f"Simulator: {e}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
