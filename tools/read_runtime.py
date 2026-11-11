"""Read only the running acz.exe process. No writes, injection, or privilege changes."""
import argparse
import ctypes as C
from ctypes import wintypes as W
import json
import struct
from pathlib import Path

from inspect_install import PE

K32 = C.WinDLL('kernel32', use_last_error=True)
PSAPI = C.WinDLL('psapi', use_last_error=True)
K32.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
K32.OpenProcess.restype = W.HANDLE
K32.CloseHandle.argtypes = [W.HANDLE]
K32.ReadProcessMemory.argtypes = [W.HANDLE, C.c_void_p, C.c_void_p, C.c_size_t, C.POINTER(C.c_size_t)]
K32.ReadProcessMemory.restype = W.BOOL
K32.QueryFullProcessImageNameW.argtypes = [W.HANDLE, W.DWORD, W.LPWSTR, C.POINTER(W.DWORD)]
K32.QueryFullProcessImageNameW.restype = W.BOOL
PSAPI.EnumProcessModules.argtypes = [W.HANDLE, C.POINTER(W.HMODULE), W.DWORD, C.POINTER(W.DWORD)]
PSAPI.EnumProcessModules.restype = W.BOOL


class MemoryInfo(C.Structure):
    _fields_ = [('base', C.c_void_p), ('allocation_base', C.c_void_p),
                ('allocation_protect', W.DWORD), ('partition_id', W.WORD),
                ('region_size', C.c_size_t), ('state', W.DWORD),
                ('protect', W.DWORD), ('type', W.DWORD)]


K32.VirtualQueryEx.argtypes = [W.HANDLE, C.c_void_p, C.POINTER(MemoryInfo), C.c_size_t]
K32.VirtualQueryEx.restype = C.c_size_t


class Process:
    def __init__(self, pid, expected_image):
        self.handle = K32.OpenProcess(0x0400 | 0x0010, False, pid)
        if not self.handle:
            raise C.WinError(C.get_last_error())
        try:
            buf, size = C.create_unicode_buffer(32768), W.DWORD(32768)
            if not K32.QueryFullProcessImageNameW(self.handle, 0, buf, C.byref(size)):
                raise C.WinError(C.get_last_error())
            self.path = Path(buf.value)
            if self.path.name.lower() != 'acz.exe' or self.path.resolve() != expected_image.resolve():
                raise ValueError(f'Unexpected process image: {self.path}')
            modules, needed = (W.HMODULE * 1024)(), W.DWORD()
            if not PSAPI.EnumProcessModules(self.handle, modules, C.sizeof(modules), C.byref(needed)):
                raise C.WinError(C.get_last_error())
            self.base = modules[0]
        except BaseException:
            self.close()
            raise

    def read(self, address, size):
        if not 0 < size <= 64 * 1024 * 1024:
            raise ValueError('Read size out of bounds')
        buf, done = C.create_string_buffer(size), C.c_size_t()
        if not K32.ReadProcessMemory(self.handle, address, buf, size, C.byref(done)) or done.value != size:
            raise C.WinError(C.get_last_error())
        return buf.raw

    def close(self):
        if self.handle:
            K32.CloseHandle(self.handle)
            self.handle = None

