"""J1939 helpers: 29-bit ID parsing, TP (BAM / RTS-CTS) reassembly and DTC (DM1, DM2, ...) decoding."""

PGN_TP_CM = 0xEC00
PGN_TP_DT = 0xEB00
# PGNs whose payload is: 2 lamp bytes + N x 4-byte DTCs
DTC_PGNS = {
    0xFECA: "DM1",    # active DTCs
    0xFECB: "DM2",    # previously active DTCs
    0xFECF: "DM6",    # pending emission-related DTCs
    0xFED4: "DM12",   # emission-related active DTCs
    0xFDB5: "DM23",   # previously MIL-on DTCs
    0xFD82: "DM27",   # all pending DTCs
    0xFD80: "DM28",   # permanent DTCs
}
PGN_DM1, PGN_DM2 = 0xFECA, 0xFECB


def parse_id(can_id):
    """Return (priority, pgn, source_address, destination_address or None)."""
    prio = (can_id >> 26) & 0x7
    edp = (can_id >> 25) & 1
    dp = (can_id >> 24) & 1
    pf = (can_id >> 16) & 0xFF
    ps = (can_id >> 8) & 0xFF
    sa = can_id & 0xFF
    if pf >= 240:                       # PDU2: PS is a group extension, part of the PGN
        pgn = (edp << 17) | (dp << 16) | (pf << 8) | ps
        da = None
    else:                               # PDU1: PS is the destination address
        pgn = (edp << 17) | (dp << 16) | (pf << 8)
        da = ps
    return prio, pgn, sa, da


def decode_dtcs(payload):
    """Decode the DTC list of a DM payload (2 lamp bytes + N x 4-byte DTCs).

    A DTC with SPN 0 and FMI 0 (or all 0xFF) means 'no fault' and is skipped."""
    out = []
    for i in range(2, len(payload) - 3, 4):
        b0, b1, b2, b3 = payload[i:i + 4]
        if (b0, b1, b2, b3) == (0xFF, 0xFF, 0xFF, 0xFF):
            continue
        spn = b0 | (b1 << 8) | (((b2 >> 5) & 0x7) << 16)
        fmi = b2 & 0x1F
        if spn == 0 and fmi == 0:
            continue
        if spn == 0x7FFFF and fmi == 31:        # "not available"
            continue
        out.append({"spn": spn, "fmi": fmi, "oc": b3 & 0x7F, "cm": b3 >> 7})
    return out


class MessageAssembler:
    """feed() each CAN frame; returns (sa, pgn, payload) for every complete J1939 message.

    Single frames are returned as-is; TP.BAM and TP.RTS/CTS multi-packet messages are returned once
    all packets arrived. The TP.CM / TP.DT frames themselves return None."""

    def __init__(self):
        self._tp = {}   # (sa, da) -> dict(pgn, size, packets, parts)

    def feed(self, can_id, data):
        _prio, pgn, sa, da = parse_id(can_id)
        if pgn == PGN_TP_CM:
            if len(data) >= 8:
                ctrl = data[0]
                if ctrl in (0x20, 0x10):        # BAM or RTS
                    key = (sa, da if ctrl == 0x10 else 0xFF)
                    self._tp[key] = {"pgn": data[5] | (data[6] << 8) | (data[7] << 16),
                                     "size": data[1] | (data[2] << 8), "packets": data[3], "parts": {}}
                elif ctrl == 0xFF:              # connection abort
                    self._tp.pop((sa, da), None)
                    self._tp.pop((sa, 0xFF), None)
            return None
        if pgn == PGN_TP_DT:
            if len(data) >= 2:
                for key in ((sa, da), (sa, 0xFF)):
                    s = self._tp.get(key)
                    if not s:
                        continue
                    s["parts"][data[0]] = bytes(data[1:8])
                    if len(s["parts"]) >= s["packets"]:
                        raw = b"".join(s["parts"][k] for k in sorted(s["parts"]))[:s["size"]]
                        del self._tp[key]
                        return sa, s["pgn"], raw
                    break
            return None
        return sa, pgn, bytes(data)


class DtcDecoder:
    """Turns complete messages into DTC events (dicts) for DM1/DM2/... PGNs."""

    def __init__(self, extra_pgns=()):
        self.pgns = dict(DTC_PGNS)
        for p in extra_pgns:
            self.pgns.setdefault(int(p), "DM(0x%X)" % int(p))
        self.asm = MessageAssembler()

    def feed(self, can_id, data):
        return self.events(self.asm.feed(can_id, data))

    def events(self, msg):
        if not msg:
            return []
        sa, pgn, payload = msg
        kind = self.pgns.get(pgn)
        if not kind:
            return []
        return [dict(kind=kind, sa=sa, **d) for d in decode_dtcs(payload)]
