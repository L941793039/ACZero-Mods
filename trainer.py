"""Local ACZero prototype controller. Attaches only to the verified Steam acz.exe build."""
from __future__ import annotations
import argparse
import configparser
import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
import os
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'tools'))
from read_runtime import Process, MemoryInfo
from inspect_install import PE

IMAGE = None
EXPECTED_HASH = '3802b0dc05c43dda06bea6d4124dce611bd2956c7e9f27f9eb322615d42d5e5a'
DLL = ROOT / 'dist' / 'ACZeroMods-v59.dll'
INI = ROOT / 'dist' / 'ACZeroMods.ini'
STATUS = ROOT / 'dist' / 'ACZeroMods.status'
FEATURES = {'DualSP': '双 SP（原武器键：MSL → SP1 → SP2）',
            'ProportionalNavigation': '比例制导（各弹系数为 0 时使用原版）',
            'GunAssist': '机炮微自瞄（已锁定目标 / 3 度，实验）',
            'InfiniteAmmo': '无限弹药', 'FastReload': '快速装填',
            'HighG': '高 G（加速＋减速＋拉杆，负 G 较温和）',
            'MissileLockRange': '导弹锁距倍率（分弹种，独立设置）',
            'MissileFlightRange': '导弹航程倍率（各导弹 / SOD母弹，逐弹可编辑）',
            'MissileSpeed': '导弹速度倍率（在配置文件中分别设置）',
            'NextTarget': 'NEXT 预告与机头附近目标切换',
            'TargetIndicatorName': '选中目标离屏方向箭头旁显示目标类型',
            'ExtendedTargetBoxes': '延长目标框显示距离（英尺配置）',
            'ExtendedTargetSelection': '远距目标选择／NEXT（武器锁距独立设置）'}


def config_set(section, key, value):
    # WritePrivateProfileString preserves concurrent command acknowledgements by the DLL.
    api = C.WinDLL('kernel32', use_last_error=True)
    api.WritePrivateProfileStringW.argtypes = [W.LPCWSTR, W.LPCWSTR, W.LPCWSTR, W.LPCWSTR]
    if not api.WritePrivateProfileStringW(section, key, str(value), str(INI)):
        raise C.WinError(C.get_last_error())


def find_pid():
    ps = C.WinDLL('psapi', use_last_error=True)
    ids = (W.DWORD * 8192)()
    used = W.DWORD()
    if not ps.EnumProcesses(ids, C.sizeof(ids), C.byref(used)):
        raise C.WinError(C.get_last_error())
    matches = []
    for pid in ids[:used.value // 4]:
        if not pid:
            continue
        try:
            process = Process(pid, IMAGE)
        except (OSError, ValueError):
            continue
        matches.append(pid)
        process.close()
    if len(matches) != 1:
        raise RuntimeError(f'需要且只能有一个已启动的 AC0，找到 {len(matches)} 个。')
    return matches[0]


def check_files():
    if IMAGE is None or IMAGE.name.casefold() != "acz.exe":
        raise ValueError("Select the standalone game acz.exe.")
    if hashlib.sha256(IMAGE.read_bytes()).hexdigest() != EXPECTED_HASH:
        raise RuntimeError('AC0 程序哈希不匹配；此原型仅适配已分析的独立版 2.0.0.1。')
    if not DLL.is_file() or PE(DLL).machine != 0x8664:
        raise RuntimeError('缺少已编译的 x64 ACZeroMods.dll。')
    if (ROOT / "SHA256SUMS.txt").is_file() and hashlib.sha256(DLL.read_bytes()).hexdigest() != "ad667a9992b6c0140ed23adf4d9ad4ac497ff82120e1a2809ed43af257db1433":
        raise ValueError("Packaged v59 DLL checksum mismatch.")


def attach(pid=None):
    check_files()
    pid = pid or find_pid()
    process = Process(pid, IMAGE)
    k = C.WinDLL('kernel32', use_last_error=True)
    ps = C.WinDLL('psapi', use_last_error=True)
    try:
        modules, needed = (W.HMODULE * 1024)(), W.DWORD()
        ps.EnumProcessModules.argtypes = [W.HANDLE, C.POINTER(W.HMODULE), W.DWORD, C.POINTER(W.DWORD)]
        ps.GetModuleFileNameExW.argtypes = [W.HANDLE, W.HMODULE, W.LPWSTR, W.DWORD]
        if not ps.EnumProcessModules(process.handle, modules, C.sizeof(modules), C.byref(needed)):
            raise C.WinError(C.get_last_error())
        module_paths = []
        for module in modules[:needed.value // C.sizeof(W.HMODULE)]:
            buf = C.create_unicode_buffer(32768)
            if ps.GetModuleFileNameExW(process.handle, module, buf, 32768):
                module_paths.append((module, Path(buf.value)))
        for _, path in module_paths:
            if path.name.casefold() == DLL.name.casefold():
                if path.resolve() != DLL.resolve():
                    raise RuntimeError('已有来自其他目录的同名 Mod，拒绝重复加载。')
                return {'pid': pid, 'result': 'already_loaded', 'dll': str(path)}
            if path.name.casefold().startswith("aczeromods") and path.suffix.casefold()==".dll":
                raise RuntimeError("Another ACZeroMods version is already loaded.")
        # Validate hook signatures before allocating or writing anything to acz.exe.
        for rva, expected in [(0x9afb0, bytes.fromhex('48 8b c4 48 89 58 08')),
                              (0x9c490, bytes.fromhex('48 8b c4 48 89 70 10')),
                              (0x9d260, bytes.fromhex('48 8b c4 48 89 58 10')),
                              (0x9e990, bytes.fromhex('48 8b c4 48 89 58 10')),
                              (0x9b730, bytes.fromhex('c5 fa 59 0d c8 90 23 00')),
                              (0x9ca33, bytes.fromhex('c5 fa 59 0d 8d 7d 23 00')),
                              (0x9d4aa, bytes.fromhex('c5 c2 59 48 14')),
                              (0x9ee09, bytes.fromhex('c5 ba 59 48 14')),
                              (0x9da10, bytes.fromhex('48 8b c4 48 89 58 08')),
                              (0x9e030, bytes.fromhex('48 8b c4 48 89 58 10')),
                              (0x9f230, bytes.fromhex('48 8b c4 48 89 58 10')),
                              (0x9f59a, bytes.fromhex('c5 ba 59 48 14')),
                              (0xc0780, bytes.fromhex('40 55 41 56 48 8d 6c 24 98')),
                              (0x13f320, bytes.fromhex('c5 fa 59 0d 88 53 19 00')),
                              (0x9db22, bytes.fromhex('c5 ca 59 48 14')),
                              (0x9e4ae, bytes.fromhex('c4 c1 7a 59 c9')),
                              (0xc1610, bytes.fromhex('48 8b c4 53 41 54 41 55')),
                              (0xb9b41, bytes.fromhex('c5 7a 10 05 4f b2 21 00')),
                              (0xb9c39, bytes.fromhex('c5 fa 10 b4 83 0c 02 00 00')),
                              (0xb9dc0, bytes.fromhex('48 8b c4 55 56 57 41 54')),
                              (0xb6c50, bytes.fromhex('48 89 5c 24 08 48 89 6c')),
                              (0xb6e00, bytes.fromhex('40 53 55 56 57 41 56 48 81 ec')),
                              (0xe9380, bytes.fromhex('40 53 48 83 ec 20 80 79 08 00')),
                              (0xb0100, bytes.fromhex('48 8b c4 48 89 58 08 48 89 70 18')),
                              (0xb05ad, bytes.fromhex('e8 4e e1 14 00')),
                              (0xafd40, bytes.fromhex('48 8b c4 48 89 58 10 4c 89 48 20')),
                              (0xbf7b0, bytes.fromhex('48 83 ec 38 44 8b 91 f0 01 00 00')),
                              (0xc1c60, bytes.fromhex('48 8b c4 48 89 58 20')),
                              (0xb9aa0, bytes.fromhex('4c 8b dc 4d 89 4b 20')),
                              (0x104100, bytes.fromhex('48 8b c4 4c 89 40 18')),
                              (0x90a60, bytes.fromhex('48 8b c4 48 89 58 08')),
                              (0x13ad20, bytes.fromhex('4c 89 44 24 18')),
                              (0x187cc0, bytes.fromhex('48 89 5c 24 08')),
                              (0x13efc3, bytes.fromhex('e8 d8 3f 0c 00')),
                              (0x12aa70, bytes.fromhex('48 8b c4 55 56 41 57')),
                              (0xdd480, bytes.fromhex('40 55 41 56 48 8d 6c 24 c8')),
                              (0x90510, bytes.fromhex('48 8b c4 c5 fa 11 58 20')),
                              (0x13f1fc, bytes.fromhex('e8 9f 3d 0c 00')),
                              (0x12c780, bytes.fromhex('48 89 74 24 20 57 48 83 ec 20')),
                              (0xeae40, bytes.fromhex('48 89 5c 24 08')),
                              (0xeba50, bytes.fromhex('48 89 5c 24 08')),
                              (0xec700, bytes.fromhex('40 53 48 83 ec 20')),
                              (0xce8c0, bytes.fromhex('48 8b c4 55 56 48 8d 6c 24 88'))]:
            if process.read(process.base + rva, len(expected)) != expected:
                raise RuntimeError(f'运行时代码签名不匹配：{rva:x}')
        # Verify complete relocated native bodies used by helpers, including
        # unhooked calls and the field offsets they read, before any injection.
        port = json.loads((ROOT/'compat/standalone-v2001.json').read_text())
        for function in port['functions']:
            rva, size = int(function['new'],16), function['size']
            if hashlib.sha256(process.read(process.base+rva,size)).hexdigest() != function['sha256']:
                raise RuntimeError(f'独立版原生函数校验不匹配：{rva:x}')
        k.GetModuleHandleW.argtypes = [W.LPCWSTR]
        k.GetModuleHandleW.restype = W.HMODULE
        k.GetProcAddress.argtypes = [W.HMODULE, C.c_char_p]
        k.GetProcAddress.restype = C.c_void_p
        local_load = k.GetProcAddress(k.GetModuleHandleW('kernel32.dll'), b'LoadLibraryW')
        k.VirtualQuery.argtypes = [C.c_void_p, C.POINTER(MemoryInfo), C.c_size_t]
        k.VirtualQuery.restype = C.c_size_t
        info = MemoryInfo()
        if not k.VirtualQuery(local_load, C.byref(info), C.sizeof(info)):
            raise C.WinError(C.get_last_error())
        k.GetModuleFileNameW.argtypes = [W.HMODULE, W.LPWSTR, W.DWORD]
        buf = C.create_unicode_buffer(32768)
        if not k.GetModuleFileNameW(info.allocation_base, buf, 32768):
            raise C.WinError(C.get_last_error())
        host = Path(buf.value).name.casefold()
        remote_base = next((base for base, path in module_paths if path.resolve() == Path(buf.value).resolve()), None)
        if remote_base is None:
            raise RuntimeError('无法定位目标进程的 Windows 加载器模块。')
        remote_load = remote_base + local_load - info.allocation_base
        # Steam's overlay hooks this documented Windows export in the game. Call the
        # existing export normally; byte equality with the controller process is invalid.
        k.VirtualQueryEx.argtypes = [W.HANDLE, C.c_void_p, C.POINTER(MemoryInfo), C.c_size_t]
        k.VirtualQueryEx.restype = C.c_size_t
        remote_info = MemoryInfo()
        if not k.VirtualQueryEx(process.handle, remote_load, C.byref(remote_info), C.sizeof(remote_info)):
            raise C.WinError(C.get_last_error())
        if remote_info.type != 0x1000000 or not (remote_info.protect & 0xf0):
            raise RuntimeError('目标 Windows 加载器地址不是可执行映像内存。')
    finally:
        process.close()
    # Separate handle: read_runtime.py always remains read-only.
    k.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
    k.OpenProcess.restype = W.HANDLE
    handle = k.OpenProcess(0x0400 | 0x0010 | 0x0008 | 0x0020 | 0x0002, False, pid)
    if not handle:
        raise C.WinError(C.get_last_error())
    k.VirtualAllocEx.argtypes = [W.HANDLE, C.c_void_p, C.c_size_t, W.DWORD, W.DWORD]
    k.VirtualAllocEx.restype = C.c_void_p
    k.VirtualFreeEx.argtypes = [W.HANDLE, C.c_void_p, C.c_size_t, W.DWORD]
    k.WriteProcessMemory.argtypes = [W.HANDLE, C.c_void_p, C.c_void_p, C.c_size_t, C.POINTER(C.c_size_t)]
    k.CreateRemoteThread.argtypes = [W.HANDLE, C.c_void_p, C.c_size_t, C.c_void_p, C.c_void_p, W.DWORD, C.POINTER(W.DWORD)]
    k.CreateRemoteThread.restype = W.HANDLE
    k.WaitForSingleObject.argtypes = [W.HANDLE, W.DWORD]
    k.GetExitCodeThread.argtypes = [W.HANDLE, C.POINTER(W.DWORD)]
    k.CloseHandle.argtypes = [W.HANDLE]
    payload = (str(DLL.resolve()) + '\0').encode('utf-16le')
    remote = None
    thread = None
    finished = False
    try:
        for command in ('Stop', 'SwitchSP', 'CheckpointSave', 'CheckpointLoad'):
            config_set('Commands', command, 0)
        remote = k.VirtualAllocEx(handle, None, len(payload), 0x3000, 4)
        if not remote:
            raise C.WinError(C.get_last_error())
        done = C.c_size_t()
        if not k.WriteProcessMemory(handle, remote, payload, len(payload), C.byref(done)) or done.value != len(payload):
            raise C.WinError(C.get_last_error())
        thread = k.CreateRemoteThread(handle, None, 0, remote_load, remote, 0, None)
        if not thread:
            raise C.WinError(C.get_last_error())
        if k.WaitForSingleObject(thread, 10000) != 0:
            raise RuntimeError('加载线程尚未退出；保留路径内存，不重复注入。')
        finished = True
        code = W.DWORD()
        if not k.GetExitCodeThread(thread, C.byref(code)) or not code.value:
            raise RuntimeError('LoadLibraryW 加载失败，请检查依赖。')
        return {'pid': pid, 'result': 'loaded', 'dll': str(DLL)}
    finally:
        if thread:
            k.CloseHandle(thread)
        if remote and (finished or not thread):
            k.VirtualFreeEx(handle, remote, 0, 0x8000)
        k.CloseHandle(handle)


def gui():
    import tkinter as tk
    from tkinter import ttk, messagebox, filedialog
    app = tk.Tk()
    app.title("ACZero Mods v59 - Steam 2.0.0.1")
    app.geometry('560x820')
    app.minsize(560,820)
    frame = ttk.Frame(app, padding=20)
    frame.pack(fill='both', expand=True)
    ttk.Label(frame, text='AC0 双 SP / 制导 / 机炮 / 修改器', font=('Microsoft YaHei UI', 15)).pack(anchor='w', pady=(0, 12))
    def choose_game():
        global IMAGE
        selected = filedialog.askopenfilename(title="Select AC0 acz.exe", filetypes=[("AC0 executable", "acz.exe")])
        if selected:
            IMAGE = Path(selected).resolve()
        return IMAGE is not None
    ttk.Button(frame, text="Select AC0 acz.exe", command=choose_game).pack(fill="x")
    def load():
        if IMAGE is None and not choose_game():
            return
        try:
            messagebox.showinfo('加载结果', json.dumps(attach(), ensure_ascii=False, indent=2))
        except Exception as exc:
            messagebox.showerror('未加载', str(exc))
    ttk.Button(frame, text='连接已正常启动的 AC0', command=load).pack(fill='x', pady=(0, 10))
    cfg = configparser.ConfigParser()
    cfg.read(INI, encoding='utf-8')
    variables = {}
    for key, label in FEATURES.items():
        value = tk.BooleanVar(value=cfg.getboolean('Features', key, fallback=False))
        variables[key] = value
        ttk.Checkbutton(frame, text=label, variable=value,
                        command=lambda k=key, v=value: config_set('Features', k, int(v.get()))).pack(anchor='w', pady=4)
    hud_variables=[]
    for key,label in [('DualSPRows','五行武器 HUD'),('HighGIndicator','高 G 提示框')]:
        value=tk.BooleanVar(value=cfg.getboolean('HUD',key,fallback=False))
        hud_variables.append((key,value))
        ttk.Checkbutton(frame,text=label,variable=value,command=lambda k=key,v=value:config_set('HUD',k,int(v.get()))).pack(anchor='w',pady=4)
    ttk.Button(frame, text='循环武器（原武器键 / F2）', command=lambda: config_set('Commands', 'SwitchSP', 1)).pack(fill='x', pady=10)
    def disable():
        for k, v in variables.items():
            v.set(False)
            config_set('Features', k, 0)
        for key,value in hud_variables:
            value.set(False)
            config_set('HUD',key,0)
    ttk.Button(frame, text='关闭全部功能', command=disable).pack(fill='x')
    ttk.Button(frame, text='编辑导弹参数：制导 / 速度 / 锁距 / 航程',
               command=lambda: os.startfile(str(INI))).pack(fill='x', pady=(10,0))
    state = tk.StringVar(value='等待连接')
    ttk.Label(frame, textvariable=state, wraplength=490).pack(anchor='w', pady=12)
    def refresh():
        try:
            lines = dict(line.split('=', 1) for line in STATUS.read_text().splitlines() if '=' in line)
            state.set(f"状态：{lines.get('state', '?')}  |  切换 {lines.get('switches', 0)} 次  |  制导 {lines.get('guided', 0)} 帧  |  辅助 {lines.get('assisted', 0)} 发")
        except (OSError, ValueError):
            pass
        app.after(500, refresh)
    refresh()
    app.mainloop()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--attach', action='store_true')
    parser.add_argument('--pid', type=int)
    parser.add_argument('--game-exe', type=Path)
    parser.add_argument('--check-files', action='store_true')
    parser.add_argument('--set', nargs=2, metavar=('FEATURE', '0_OR_1'))
    parser.add_argument('--switch', action='store_true')
    parser.add_argument('--stop', action='store_true')
    parser.add_argument('--checkpoint', choices=('save','load'), help='Experimental paused, same-phase checkpoint command')
    parser.add_argument('--status', action='store_true')
    args = parser.parse_args()
    IMAGE = args.game_exe.resolve() if args.game_exe else None
    if args.check_files:
        check_files()
        print("Steam standalone 2.0.0.1 and v59 DLL file checks passed.")
    elif args.attach:
        print(json.dumps(attach(args.pid), ensure_ascii=False))
    elif args.set:
        key, value = args.set
        if key not in FEATURES or value not in ('0', '1'):
            parser.error('功能名或数值不正确')
        config_set('Features', key, value)
    elif args.switch:
        config_set('Commands', 'SwitchSP', 1)
    elif args.stop:
        config_set('Commands', 'Stop', 1)
    elif args.checkpoint:
        config_set('Commands','CheckpointSave' if args.checkpoint=='save' else 'CheckpointLoad',1)
    elif args.status:
        print(STATUS.read_text() if STATUS.exists() else 'not_attached')
    else:
        gui()
