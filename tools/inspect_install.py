"""Read-only inventory of the Steam AC Zero executable and archive headers."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
from pathlib import Path


class PE:
    def __init__(self, path: Path):
        self.data = path.read_bytes()
        self.pe = struct.unpack_from('<I', self.data, 0x3c)[0]
        if self.data[self.pe:self.pe + 4] != b'PE\0\0':
            raise ValueError('Not a PE executable')
        self.machine, count = struct.unpack_from('<HH', self.data, self.pe + 4)
        opt_size = struct.unpack_from('<H', self.data, self.pe + 20)[0]
        self.opt = self.pe + 24
        self.magic = struct.unpack_from('<H', self.data, self.opt)[0]
        if self.magic != 0x20b:
            raise ValueError('Expected PE32+')
        self.base = struct.unpack_from('<Q', self.data, self.opt + 24)[0]
        self.entry = struct.unpack_from('<I', self.data, self.opt + 16)[0]
        self.sections = []
        for i in range(count):
            pos = self.opt + opt_size + 40 * i
            name, size, rva, raw_size, raw = struct.unpack_from('<8sIIII', self.data, pos)
            self.sections.append(dict(name=name.rstrip(b'\0').decode(), rva=rva,
                                      virtual_size=size, raw_offset=raw, raw_size=raw_size))

    def offset(self, rva):
        for sec in self.sections:
            if sec['rva'] <= rva < sec['rva'] + max(sec['virtual_size'], sec['raw_size']):
                return rva - sec['rva'] + sec['raw_offset']
        if rva < self.sections[0]['rva']:
            return rva
        raise ValueError(f'Unmapped RVA {rva:x}')

    def rva(self, off):
        for sec in self.sections:
            if sec['raw_offset'] <= off < sec['raw_offset'] + sec['raw_size']:
                return off - sec['raw_offset'] + sec['rva']
        return off

    def cstr(self, off):
        return self.data[off:self.data.index(b'\0', off)].decode('ascii', errors='replace')

    def imports(self):
        import_rva = struct.unpack_from('<I', self.data, self.opt + 112 + 8)[0]
        out = {}
        if not import_rva:
            return out
        pos = self.offset(import_rva)
        while True:
            orig, _, _, name, first = struct.unpack_from('<IIIII', self.data, pos)
            if not name:
                break
            dll = self.cstr(self.offset(name))
            cursor = self.offset(orig or first)
            names = []
            while True:
                thunk = struct.unpack_from('<Q', self.data, cursor)[0]
                if not thunk:
                    break
                names.append(f'ordinal:{thunk & 0xffff}' if thunk >> 63
                             else self.cstr(self.offset(thunk) + 2))
                cursor += 8
            out[dll] = names
            pos += 20
        return out

