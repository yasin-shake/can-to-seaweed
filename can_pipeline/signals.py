"""Decode J1939 messages into named, scaled signals (SAE J1939-71 scaling).

MESSAGES: pgn -> (acronym, description, [signal, ...])
signal = (key, label, spn, start_byte, n_bytes, scale, offset, unit)
Value = raw * scale + offset.  Raw values >= 0xFB.. are 'not available' / 'error' / 'reserved'.
"""

T = 0.03125   # 1/32 degC per bit, offset -273 (2-byte temperatures)
K = 1 / 128.0

MESSAGES = {
    0xF004: ("EEC1", "Electronic Engine Controller 1", [
        ("DriversDemandEngineTorque", "Driver's demand engine torque", 512, 1, 1, 1, -125, "%"),
        ("ActualEngineTorque", "Actual engine torque", 513, 2, 1, 1, -125, "%"),
        ("EngineSpeed", "Engine speed", 190, 3, 2, 0.125, 0, "rpm")]),
    0xF003: ("EEC2", "Electronic Engine Controller 2", [
        ("AcceleratorPedalPosition", "Accelerator pedal position", 91, 1, 1, 0.4, 0, "%"),
        ("EngineLoad", "Engine percent load at current speed", 92, 2, 1, 1, 0, "%")]),
    0xFEF1: ("CCVS", "Cruise Control / Vehicle Speed", [
        ("WheelBasedVehicleSpeed", "Wheel-based vehicle speed", 84, 1, 2, 1 / 256.0, 0, "km/h"),
        ("CruiseControlSetSpeed", "Cruise control set speed", 86, 4, 1, 1, 0, "km/h")]),
    0xFEF2: ("LFE", "Fuel Economy", [
        ("EngineFuelRate", "Engine fuel rate", 183, 0, 2, 0.05, 0, "L/h"),
        ("InstantaneousFuelEconomy", "Instantaneous fuel economy", 184, 2, 2, 1 / 512.0, 0, "km/L"),
        ("AverageFuelEconomy", "Average fuel economy", 185, 4, 2, 1 / 512.0, 0, "km/L"),
        ("ThrottlePosition", "Throttle position", 51, 6, 1, 0.4, 0, "%")]),
    0xFEEE: ("ET1", "Engine Temperature 1", [
        ("EngineCoolantTemperature", "Engine coolant temperature", 110, 0, 1, 1, -40, "°C"),
        ("FuelTemperature", "Fuel temperature", 174, 1, 1, 1, -40, "°C"),
        ("EngineOilTemperature", "Engine oil temperature", 175, 2, 2, T, -273, "°C"),
        ("TurboOilTemperature", "Turbocharger oil temperature", 176, 4, 2, T, -273, "°C"),
        ("EngineIntercoolerTemperature", "Engine intercooler temperature", 52, 6, 1, 1, -40, "°C"),
        ("ThermostatOpening", "Engine intercooler thermostat opening", 1134, 7, 1, 0.4, 0, "%")]),
    0xFEEF: ("EFL_P1", "Engine Fluid Level / Pressure 1", [
        ("FuelDeliveryPressure", "Fuel delivery pressure", 94, 0, 1, 4, 0, "kPa"),
        ("CrankcaseBlowbyPressure", "Extended crankcase blow-by pressure", 22, 1, 1, 0.05, 0, "kPa"),
        ("EngineOilLevel", "Engine oil level", 98, 2, 1, 0.4, 0, "%"),
        ("EngineOilPressure", "Engine oil pressure", 100, 3, 1, 4, 0, "kPa"),
        ("CrankcasePressure", "Crankcase pressure", 101, 4, 2, K, -250, "kPa"),
        ("CoolantPressure", "Engine coolant pressure", 109, 6, 1, 2, 0, "kPa"),
        ("CoolantLevel", "Engine coolant level", 111, 7, 1, 0.4, 0, "%")]),
    0xFEF5: ("AMB", "Ambient Conditions", [
        ("BarometricPressure", "Barometric pressure", 108, 0, 1, 0.5, 0, "kPa"),
        ("CabInteriorTemperature", "Cab interior temperature", 170, 1, 2, T, -273, "°C"),
        ("AmbientAirTemperature", "Ambient air temperature", 171, 3, 2, T, -273, "°C"),
        ("AirInletTemperature", "Engine air inlet temperature", 172, 5, 1, 1, -40, "°C"),
        ("RoadSurfaceTemperature", "Road surface temperature", 79, 6, 2, T, -273, "°C")]),
    0xFEF6: ("IC1", "Inlet / Exhaust Conditions 1", [
        ("ParticulateTrapInletPressure", "Particulate trap inlet pressure", 81, 0, 1, 0.5, 0, "kPa"),
        ("BoostPressure", "Intake manifold (boost) pressure", 102, 1, 1, 2, 0, "kPa"),
        ("IntakeManifoldTemperature", "Intake manifold temperature", 105, 2, 1, 1, -40, "°C"),
        ("AirInletPressure", "Air inlet pressure", 106, 3, 1, 2, 0, "kPa"),
        ("AirFilterDifferentialPressure", "Air filter differential pressure", 107, 4, 1, 0.05, 0, "kPa"),
        ("ExhaustGasTemperature", "Exhaust gas temperature", 173, 5, 2, T, -273, "°C"),
        ("CoolantFilterDifferentialPressure", "Coolant filter differential pressure", 112, 7, 1, 0.5, 0, "kPa")]),
    0xFEF7: ("VEP1", "Vehicle Electrical Power 1", [
        ("NetBatteryCurrent", "Net battery current", 114, 0, 1, 1, -125, "A"),
        ("AlternatorCurrent", "Alternator current", 115, 1, 1, 1, 0, "A"),
        ("AlternatorPotential", "Alternator potential", 167, 2, 2, 0.05, 0, "V"),
        ("BatteryPotential", "Battery potential (power input 1)", 168, 4, 2, 0.05, 0, "V"),
        ("KeyswitchBatteryPotential", "Keyswitch battery potential", 158, 6, 2, 0.05, 0, "V")]),
    0xFEF8: ("TRF1", "Transmission Fluids 1", [
        ("ClutchPressure", "Transmission clutch pressure", 123, 0, 1, 16, 0, "kPa"),
        ("TransmissionOilLevel", "Transmission oil level", 124, 1, 1, 0.4, 0, "%"),
        ("TransmissionFilterDiffPressure", "Transmission filter differential pressure", 126, 2, 1, 2, 0, "kPa"),
        ("TransmissionOilPressure", "Transmission oil pressure", 127, 3, 1, 16, 0, "kPa"),
        ("TransmissionOilTemperature", "Transmission oil temperature", 177, 4, 2, T, -273, "°C")]),
    0xFEE5: ("HOURS", "Engine Hours, Revolutions", [
        ("EngineTotalHours", "Engine total hours of operation", 247, 0, 4, 0.05, 0, "h"),
        ("EngineTotalRevolutions", "Engine total revolutions", 249, 4, 4, 1000, 0, "rev")]),
    0xFEE0: ("VD", "Vehicle Distance", [
        ("TripDistance", "Trip distance", 244, 0, 4, 0.125, 0, "km"),
        ("TotalVehicleDistance", "Total vehicle distance", 245, 4, 4, 0.125, 0, "km")]),
    0xFEE7: ("VH", "Vehicle Hours", [
        ("TotalVehicleHours", "Total vehicle hours", 246, 0, 4, 0.05, 0, "h"),
        ("TotalPowerTakeoffHours", "Total power takeoff hours", 248, 4, 4, 0.05, 0, "h")]),
    0xFEC1: ("VDHR", "High Resolution Vehicle Distance", [
        ("HighResTotalVehicleDistance", "High resolution total vehicle distance", 917, 0, 4, 5, 0, "m"),
        ("HighResTripDistance", "High resolution trip distance", 918, 4, 4, 5, 0, "m")]),
    0xFEFC: ("DD", "Dash Display", [
        ("FuelLevel1", "Fuel level 1", 96, 1, 1, 0.4, 0, "%"),
        ("FuelFilterDiffPressure", "Fuel filter differential pressure", 95, 2, 1, 2, 0, "kPa"),
        ("OilFilterDiffPressure", "Engine oil filter differential pressure", 99, 3, 1, 0.5, 0, "kPa"),
        ("CargoAmbientTemperature", "Cargo area ambient temperature", 169, 4, 2, T, -273, "°C"),
        ("FuelLevel2", "Fuel level 2", 38, 6, 1, 0.4, 0, "%")]),
    0xFEE9: ("LFC", "Fuel Consumption (liquid)", [
        ("TripFuelUsed", "Trip fuel used", 182, 0, 4, 0.5, 0, "L"),
        ("TotalFuelUsed", "Total fuel used", 250, 4, 4, 0.5, 0, "L")]),
    0xFEF3: ("VP", "Vehicle Position", [
        ("Latitude", "Latitude", 584, 0, 4, 1e-7, -210, "°"),
        ("Longitude", "Longitude", 585, 4, 4, 1e-7, -210, "°")]),
}

# Messages decoded as text (ASCII, '*' delimited): pgn -> (acronym, description, [(key, label, spn)])
TEXT_MESSAGES = {
    0xFEEC: ("VI", "Vehicle Identification", [("VehicleIdentificationNumber", "Vehicle identification number (VIN)", 237)]),
    0xFEEB: ("CI", "Component Identification", [
        ("ComponentMake", "Component make", 586), ("ComponentModel", "Component model", 587),
        ("ComponentSerialNumber", "Component serial number", 588), ("ComponentUnitNumber", "Component unit number", 233)]),
    0xFEDA: ("SOFT", "Software Identification", []),
}

_SPECIAL_NAMES = {0xEC00: "TP.CM (transport connection management)", 0xEB00: "TP.DT (transport data)"}


def pgn_name(pgn, dtc_pgns=None):
    if pgn in MESSAGES:
        return "%s - %s" % MESSAGES[pgn][:2]
    if pgn in TEXT_MESSAGES:
        return "%s - %s" % TEXT_MESSAGES[pgn][:2]
    if dtc_pgns and pgn in dtc_pgns:
        return "%s - diagnostic trouble codes" % dtc_pgns[pgn]
    if pgn in _SPECIAL_NAMES:
        return _SPECIAL_NAMES[pgn]
    if 0xFF00 <= pgn <= 0xFFFF:
        return "Proprietary B"
    return "no definition"


def _status(raw, n):
    top = (raw >> (8 * (n - 1))) & 0xFF
    if top < 0xFB:
        return ""
    return {0xFF: "N/A", 0xFE: "error"}.get(top, "reserved")


_UNITS = {"degC": "°C", "deg": "°", "": ""}
_DBC = None


def _special(raw, bits):
    """J1939 'not available' / 'error' / 'reserved' markers for an unsigned raw value of `bits` bits."""
    if bits < 2:
        return ""
    if bits >= 8 and bits % 8 == 0:
        top = raw >> (bits - 8)
        if top < 0xFB:
            return ""
        return {0xFF: "N/A", 0xFE: "error"}.get(top, "reserved")
    full = (1 << bits) - 1
    return "N/A" if raw == full else "error" if raw == full - 1 else ""


class DbcDecoder:
    """Decodes messages with a J1939 DBC (matched by PGN; priority and source address are ignored)."""

    def __init__(self, path):
        import logging
        import os
        import canmatrix.formats as cf
        logging.getLogger("canmatrix").setLevel(logging.ERROR)
        db = list(cf.loadp(path).values())[0]
        self.name = os.path.basename(path)
        self.frames = {}
        for fr in db.frames:
            self.frames.setdefault(fr.arbitration_id.pgn, fr)

    def decode(self, msg):
        _sa, pgn, data = msg
        fr = self.frames.get(pgn)
        if fr is None:
            return None
        payload = bytes(data) if len(data) >= fr.size else bytes(data) + b"\xff" * (fr.size - len(data))
        try:
            decoded = fr.unpack(payload[:fr.size] if fr.size else payload)
        except Exception:
            return None
        out = []
        for name, v in decoded.items():
            sig = v.signal
            raw = int(v.raw_value)
            st = _special(raw, sig.size) if not sig.is_signed else ""
            comment = (sig.comment or "").strip()
            label = comment.split(": ", 1)[0] if comment else name
            spn = sig.attributes.get("SPN")
            try:
                spn = int(spn)
            except (TypeError, ValueError):
                spn = 0
            val = None
            if not st:
                val = round(float(v.phys_value), 7 if name == "Latitude" else 4)
                if val == int(val) and abs(val) < 1e15:
                    val = int(val)
            out.append({"name": name, "label": label[:80], "spn": spn, "value": val,
                        "unit": _UNITS.get(sig.unit or "", sig.unit or ""), "status": st})
        return fr.name, (fr.comment or fr.name).strip(), out


def load_dbc(path):
    """Load a J1939 DBC as the primary decoder; returns the number of messages."""
    global _DBC
    _DBC = DbcDecoder(path)
    return len(_DBC.frames)


def dbc_info():
    return {"name": _DBC.name, "messages": len(_DBC.frames)} if _DBC else None


def decode(msg):
    """msg = (sa, pgn, payload) -> (acronym, description, [signal dict]) or None if PGN has no definition."""
    sa, pgn, data = msg
    if _DBC is not None and pgn not in TEXT_MESSAGES:      # text fields (VIN etc.) are decoded below
        res = _DBC.decode(msg)
        if res is not None:
            return res
    d = MESSAGES.get(pgn)
    if d:
        out = []
        for key, label, spn, b, n, scale, off, unit in d[2]:
            if len(data) < b + n:
                continue
            raw = int.from_bytes(data[b:b + n], "little")
            st = _status(raw, n)
            val = None if st else round(raw * scale + off, 7 if key == "Latitude" else 4)
            out.append({"name": key, "label": label, "spn": spn, "value": val, "unit": unit, "status": st})
        return d[0], d[1], out
    t = TEXT_MESSAGES.get(pgn)
    if t:
        text = bytes(data).decode("ascii", errors="replace")
        out = []
        if pgn == 0xFEDA:                       # first byte = number of fields
            fields = text[1:].split("*")
            out = [{"name": "SoftwareId%d" % (i + 1), "label": "Software identification %d" % (i + 1),
                    "spn": 234, "value": f, "unit": "", "status": ""} for i, f in enumerate(fields[:data[0]]) if f]
        else:
            fields = text.split("*")
            for (key, label, spn), f in zip(t[2], fields):
                if f.strip():
                    out.append({"name": key, "label": label, "spn": spn, "value": f.strip(), "unit": "", "status": ""})
        return t[0], t[1], out
    return None


# ---- values driven by the simulator's POTs (A0/A2/A4) and switches (A1/A3/A5) ------------------
# key -> (group, label, unit, axis_min, axis_max).  Decoded from the simulator's own frame layout
# (independent of the DBC: the simulator's PGN 64976 does not follow the standard IC2 layout).
WATCH = {
    "pot_A0": ("A0", "POT A0 raw", "", 0, 1023),
    "pot_A2": ("A2", "POT A2 raw", "", 0, 1023),
    "pot_A4": ("A4", "POT A4 raw", "", 0, 1023),
    "sw_A1": ("A0", "Switch A1", "", 0, 1),
    "sw_A3": ("A2", "Switch A3", "", 0, 1),
    "sw_A5": ("A4", "Switch A5", "", 0, 1),
    "intake_kpa": ("A0", "Intake manifold pressure (SPN 3563)", "kPa", 0, 500),
    "coolant_c": ("A2", "Engine coolant temperature (SPN 110)", "°C", 0, 150),
    "fuel_kpa": ("A4", "Fuel delivery pressure (SPN 94)", "kPa", 0, 1000),
    "oil_kpa": ("A4", "Engine oil pressure (SPN 100)", "kPa", 0, 1000),
}


def watch(msg):
    """msg = (sa, pgn, payload) -> {watch key: value or None (not available)} for POT/switch-driven data."""
    _sa, pgn, d = msg
    out = {}
    if pgn == 0xFF00 and len(d) >= 8 and d[7] == 0xFF and d[6] & 0xF8 == 0xF8:
        pots = [int.from_bytes(d[i:i + 2], "little") for i in (0, 2, 4)]
        if all(v <= 1023 for v in pots):
            out.update(pot_A0=pots[0], pot_A2=pots[1], pot_A4=pots[2],
                       sw_A1=d[6] & 1, sw_A3=d[6] >> 1 & 1, sw_A5=d[6] >> 2 & 1)
    elif pgn == 0xFDD0 and len(d) >= 2:
        raw = int.from_bytes(d[0:2], "little")
        out["intake_kpa"] = None if raw >= 0xFB00 else round(raw * 0.0078125, 2)
    elif pgn == 0xFEEE and len(d) >= 1:
        out["coolant_c"] = None if d[0] >= 0xFB else d[0] - 40
    elif pgn == 0xFEEF and len(d) >= 4:
        out["fuel_kpa"] = None if d[0] >= 0xFB else d[0] * 4
        out["oil_kpa"] = None if d[3] >= 0xFB else d[3] * 4
    return out


# ---- names for SPNs / FMIs (used by the dashboard for DTCs and telemetry) -------------------------
WATCH_SPN = {"intake_kpa": 3563, "coolant_c": 110, "fuel_kpa": 94, "oil_kpa": 100}
# (SPN, FMI, alert label) for each simulator switch.
BUTTON_DTCS = {
    "A1": (3554, 0, "Engine coolant volume ratio"),
    "A3": (3107, 0, "Water in fuel"),
    "A5": (17858, 1, "Accelerator pedal 1 low idle switch"),
}
# Severity of the DTC each switch press sends, chosen on the dashboard -> DM1 lamp status byte.
# J1939-73 DM1 byte 1: bits 8-7 MIL, 6-5 red stop, 4-3 amber warning, 2-1 protect (01 = lamp on).
# Order here is the order of the dashboard drop-down.
SEVERITY_LAMP = {
    "critical": 0x10,   # red stop lamp      (simulator's default before this setting existed)
    "high": 0x04,       # amber warning lamp
    "med": 0x40,        # malfunction indicator lamp (MIL)
    "low": 0x01,        # protect lamp
}


def lamp_severity(lamp):
    """DM1 lamp status byte -> severity name from SEVERITY_LAMP, or the byte in hex if it matches none."""
    for name, b in SEVERITY_LAMP.items():
        if b == lamp:
            return name
    return "lamp 0x%02X" % lamp

FMI_TEXT = {
    0: "Data valid but above normal operational range - most severe level",
    1: "Data valid but below normal operational range - most severe level",
    2: "Data erratic, intermittent or incorrect", 3: "Voltage above normal, or shorted to high source",
    4: "Voltage below normal, or shorted to low source", 5: "Current below normal or open circuit",
    6: "Current above normal or grounded circuit", 7: "Mechanical system not responding or out of adjustment",
    8: "Abnormal frequency, pulse width or period", 9: "Abnormal update rate", 10: "Abnormal rate of change",
    11: "Root cause not known", 12: "Bad intelligent device or component", 13: "Out of calibration",
    14: "Special instructions", 15: "Data valid but above normal operating range - least severe level",
    16: "Data valid but above normal operating range - moderately severe level",
    17: "Data valid but below normal operating range - least severe level",
    18: "Data valid but below normal operating range - moderately severe level",
    19: "Received network data in error", 20: "Data drifted high", 21: "Data drifted low", 31: "Condition exists"}
_SPN_NAMES = None


def spn_name(spn):
    """Decoded signal name for an SPN (DBC name when a DBC is loaded, else the built-in table), or ''."""
    global _SPN_NAMES
    if _SPN_NAMES is None:
        names = {}
        for msg in MESSAGES.values():
            for key, _l, s, *_ in msg[2]:
                names[s] = key
        if _DBC is not None:
            for fr in _DBC.frames.values():
                for sg in fr.signals:
                    try:
                        names[int(sg.attributes.get("SPN"))] = sg.name
                    except (TypeError, ValueError):
                        pass
        _SPN_NAMES = names
    return _SPN_NAMES.get(spn, "")
