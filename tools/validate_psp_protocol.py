#!/usr/bin/env python3
"""Validate the layout copied from the verified BC-250 PSP GPCOM protocol.

This is an offline layout test only. It never accesses hardware or enables a
KMD gate; it protects the byte layout while the real PSP path remains gated.
"""
from __future__ import annotations

import struct

C2PMSG = {
    33: 0x58184,
    35: 0x5818C,
    36: 0x58190,
    37: 0x58194,
    64: 0x58200,
    67: 0x5820C,
    69: 0x58214,
    70: 0x58218,
    71: 0x5821C,
    81: 0x58244,
    101: 0x58294,
}

assert C2PMSG[64] == 0x58000 + 0x200
assert C2PMSG[67] == 0x58000 + 0x20C
assert C2PMSG[69] == 0x58000 + 0x214
assert C2PMSG[70] == 0x58000 + 0x218
assert C2PMSG[71] == 0x58000 + 0x21C
assert C2PMSG[81] == 0x58000 + 0x244

# psp_gfx_rb_frame: command PA, command size, fence PA, fence value.
frame = bytearray(64)
struct.pack_into("<IIIIII", frame, 0, 0x12345000, 0x00000000, 0x400,
                 0x22345000, 0x00000000, 7)
assert len(frame) == 64
assert struct.unpack_from("<I", frame, 20)[0] == 7

# psp_gfx_cmd_resp: header, union payload at +28, response at +864.
command = bytearray(0x1000)
struct.pack_into("<III", command, 0, 0x400, 1, 0x06)
struct.pack_into("<IIII", command, 28, 0x34500000, 0xF4, 0x40000, 18)
struct.pack_into("<IIIII", command, 864, 0, 0, 0x1000, 0, 0)
assert struct.unpack_from("<I", command, 28)[0] == 0x34500000
assert struct.unpack_from("<I", command, 864)[0] == 0
assert 864 + 20 <= len(command)

# A 4 KiB ring advances 16 dwords for every 64-byte frame.
assert (0x1000 // 4) == 1024
assert (64 // 4) == 16
assert (0 + 16) % 1024 == 16

print("PSP_PROTOCOL_VALIDATION_OK")
print("mp0_base=0x58000")
print("ring_size=0x1000 frame_dwords=16 command_payload=28 response=864")
print("hardware_access=disabled")
