"""Loopback J1939 text source using a timed trace captured by the Windows logger."""
import argparse
import asyncio
import csv
import os
import socket
import sys
import threading
from pathlib import Path

import j1939
from signals import BUTTON_DTCS, MESSAGES


TRACE = Path(__file__).with_name("simulator_trace.csv")
HOST = "127.0.0.1"
PORT = 19390

# Signal names from the logger's built-in J1939 definitions.
CHANNELS = {
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
FIELDS = {field[0]: (pgn, *field[3:7])
          for pgn, (_, _, fields) in MESSAGES.items() for field in fields}
POTS = {"A0": 0, "A2": 2, "A4": 4}
SWITCHES = {"A1": 0, "A3": 1, "A5": 2}
EXTRA_IDS = {0xF004: 0x0CF00400, 0xFEF1: 0x18FEF100}


def control_catalog():
    """Match each editable field to the active decoder by PGN and SPN."""
    import signals
    result = []
    for channel, (field, low, high) in CHANNELS.items():
        pgn, _, _, scale, _ = FIELDS[field]
        spn = next(f[2] for f in MESSAGES[pgn][2] if f[0] == field)
        decoded = signals.decode((0, pgn, bytes(8)))
        signal = next((s for s in decoded[2] if s["spn"] == spn), None)
        if signal:
            sa = 3 if channel == "TRANSOIL" else 0
            result.append(dict(channel=channel, pgn=pgn, sa=f"0x{sa:02X}", min=low, max=high,
                               step=scale, **{k: signal[k] for k in ("name", "label", "unit", "spn")}))
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
                pgn = j1939.parse_id(cid)[1]
                if cid == self.trace[0][1]:
                    for extra, extra_id in EXTRA_IDS.items():
                        if extra in self.overrides:
                            self.broadcast(extra_id, self.apply_fields(extra, b"\xff" * 8))
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
                self.broadcast(cid, self.apply_fields(pgn, data))
                due += delay
                # Resume from the current time after host sleep or suspension.
                if clock.time() - due > 0.25:
                    due = clock.time()
                await asyncio.sleep(max(0, due - clock.time()))

    def set_field(self, pgn, start, size, raw):
        self.overrides.setdefault(pgn, {})[start] = raw.to_bytes(size, "little")

    def apply_fields(self, pgn, data):
        for start, encoded in self.overrides.get(pgn, {}).items():
            data = data[:start] + encoded + data[start + len(encoded):]
        return data

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
                self.set_field(0xFDD0, 0, 2, round(fraction * 500 * 128))
            elif pin == "A2":
                self.set_field(0xFEEE, 0, 1, round(fraction * 150) + 40)
            else:
                for start in (0, 3):
                    self.set_field(0xFEEF, start, 1, round(fraction * 250))
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
            name, low, high = CHANNELS[words[1]]
            value = float(words[2])
            if not low <= value <= high:
                raise ValueError(f"{words[1]} range is {low}..{high}")
            pgn, start, size, scale, offset = FIELDS[name]
            self.set_field(pgn, start, size, round((value - offset) / scale))
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
