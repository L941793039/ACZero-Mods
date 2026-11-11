#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef ACZERO_PORTABLE_V59
#define MOD_VERSION "59"
#define MOD_VERSION_W L"59"
#define GAME_VERSION "2.0.0.1"
#define GAME_VERSION_W L"2.0.0.1"
static constexpr DWORD gameVersionMS=MAKELONG(0,2),gameVersionLS=MAKELONG(1,0);
static constexpr int extraRows=66;
#else
#define MOD_VERSION "41"
#define MOD_VERSION_W L"41"
#define GAME_VERSION "1.0.2.1"
#define GAME_VERSION_W L"1.0.2.1"
static constexpr DWORD gameVersionMS=MAKELONG(0,1),gameVersionLS=MAKELONG(1,2);
static constexpr int extraRows=0;
static const char* dllHash="d6c5055485ce8b58b273126e586846e4e2e9df32c8ff014932bc1c2feeff131a";
#endif
static const wchar_t* dllName=L"ACZeroMods-v" MOD_VERSION_W L".dll";
static std::wstring root,ini,dll,statusFile;
static HWND mainWindow,statusLabel,slotCombo;
static HFONT font,titleFont;
static DWORD connectedPid;
static int dpi=96;
struct Handle { HANDLE value=nullptr; ~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);} };
static std::wstring widen(const char* text) {
    int n=MultiByteToWideChar(CP_UTF8,0,text,-1,nullptr,0);
    std::wstring out(n,0); MultiByteToWideChar(CP_UTF8,0,text,-1,out.data(),n); out.pop_back(); return out;
}
static std::string utf8(const std::wstring& text) {
    int n=WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),nullptr,0,nullptr,nullptr);
    std::string out(n,0); WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),out.data(),n,nullptr,nullptr); return out;
}
static void fail(const char* what) { throw std::runtime_error(what); }
static std::string hexDigest(const std::array<UCHAR,32>& digest) {
    std::string out;const char* digits="0123456789abcdef";
    for(auto byte:digest) {out+=digits[byte>>4];out+=digits[byte&15];}
    return out;
}
template<class Feed> static std::string sha256(Feed feed) {
    BCRYPT_ALG_HANDLE algorithm=nullptr; BCRYPT_HASH_HANDLE hash=nullptr;
    DWORD size=0,used=0;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) fail("无法启动 SHA256 校验。");
    if(BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&size),sizeof(size),&used,0)<0) { BCryptCloseAlgorithmProvider(algorithm,0); fail("无法读取 SHA256 参数。"); }
    std::vector<UCHAR> object(size); std::array<UCHAR,32> result{};
    bool ok=BCryptCreateHash(algorithm,&hash,object.data(),size,nullptr,0,0)>=0;
    if(ok) ok=feed(hash);
    if(ok) ok=BCryptFinishHash(hash,result.data(),DWORD(result.size()),0)>=0;
    if(hash) BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(algorithm,0);
    if(!ok) fail("文件 SHA256 校验失败。");
    return hexDigest(result);
}
static std::string hashFile(const std::wstring& path) {
    Handle file{CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr)};
    if(file.value==INVALID_HANDLE_VALUE) fail("无法读取文件，请完整解压压缩包，并确认文件存在。");
    std::vector<UCHAR> buffer(65536);
    return sha256([&](BCRYPT_HASH_HANDLE hash){
        DWORD count=0;
        while(ReadFile(file.value,buffer.data(),DWORD(buffer.size()),&count,nullptr)) {
            if(!count) return true;
            if(BCryptHashData(hash,buffer.data(),count,0)<0) return false;
        }
        return false;
    });
}
struct Signature {uintptr_t rva;std::vector<unsigned char> bytes;};
#ifdef ACZERO_PORTABLE_V59
struct NativeBody {uintptr_t rva;DWORD size;const char* sha256;};
#include "portable_v59_profile.h"
static std::string hashBytes(std::vector<UCHAR>& bytes) {
    return sha256([&](BCRYPT_HASH_HANDLE hash){
        return BCryptHashData(hash,bytes.data(),ULONG(bytes.size()),0)>=0;
    });
}
#endif
static std::wstring get(const wchar_t* section,const wchar_t* key,const wchar_t* fallback=L"0",const std::wstring* path=nullptr) {
    wchar_t value[256]; GetPrivateProfileStringW(section,key,fallback,value,256,(path?*path:ini).c_str()); return value;
}
static void set(const wchar_t* section,const wchar_t* key,const std::wstring& value) {
    if(!WritePrivateProfileStringW(section,key,value.c_str(),ini.c_str())) fail("无法保存配置。请把整包解压到可写的普通文件夹。");
}
static bool enabled(const wchar_t* section,const wchar_t* key) { return get(section,key)==L"1"; }
static void verifyPackage() {
    if(hashFile(dll)!=dllHash) fail("DLL 缺失或校验不符：请重新完整解压 v" MOD_VERSION " 压缩包。");
    if(GetFileAttributesW(ini.c_str())==INVALID_FILE_ATTRIBUTES) fail("找不到 ACZeroMods.ini，请完整解压整包。");
    BOOL replaced=FALSE;
    if(!WideCharToMultiByte(CP_ACP,0,dll.c_str(),-1,nullptr,0,nullptr,&replaced)||replaced)
        fail("路径含当前系统无法表示的字符，请移到 C:\\ACZeroMods 等普通路径。");
}
struct Game { DWORD pid; std::wstring path; };
static bool supportedVersion(const VS_FIXEDFILEINFO& version) {
    return version.dwSignature==VS_FFI_SIGNATURE && version.dwFileVersionMS==gameVersionMS && version.dwFileVersionLS==gameVersionLS;
}
static void verifyGameFile(const std::wstring& path) {
    DWORD ignored=0,size=GetFileVersionInfoSizeW(path.c_str(),&ignored);
    if(!size||size>16*1024*1024) fail("无法读取 AC0 版本资源。需要 " GAME_VERSION " 的 acz.exe。");
    std::vector<unsigned char> data(size); VS_FIXEDFILEINFO* version=nullptr; UINT length=0;
    if(!GetFileVersionInfoW(path.c_str(),0,size,data.data())||
       !VerQueryValueW(data.data(),L"\\",reinterpret_cast<void**>(&version),&length)||!version||length<sizeof(*version))
        fail("AC0 文件版本资源无效，未进行注入。");
    if(!supportedVersion(*version)) {
        char message[220];snprintf(message,sizeof(message),"AC0 文件版本为 %u.%u.%u.%u，本包需要 " GAME_VERSION "。",
            HIWORD(version->dwFileVersionMS),LOWORD(version->dwFileVersionMS),HIWORD(version->dwFileVersionLS),LOWORD(version->dwFileVersionLS));
        fail(message);
    }
    Handle file{CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)};
    IMAGE_DOS_HEADER dos{};DWORD read=0;LARGE_INTEGER fileSize{};
    if(file.value==INVALID_HANDLE_VALUE||!GetFileSizeEx(file.value,&fileSize)||
       !ReadFile(file.value,&dos,sizeof(dos),&read,nullptr)||read!=sizeof(dos)||dos.e_magic!=IMAGE_DOS_SIGNATURE||
       dos.e_lfanew<LONG(sizeof(dos))||fileSize.QuadPart<LONGLONG(dos.e_lfanew)+sizeof(IMAGE_NT_HEADERS64))
        fail("AC0 PE 文件头无效，未进行注入。");
    LARGE_INTEGER offset{};offset.QuadPart=dos.e_lfanew;IMAGE_NT_HEADERS64 nt{};
    if(!SetFilePointerEx(file.value,offset,nullptr,FILE_BEGIN)||!ReadFile(file.value,&nt,sizeof(nt),&read,nullptr)||read!=sizeof(nt)||
       nt.Signature!=IMAGE_NT_SIGNATURE||nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        fail("需要 x64 AC0 程序，未进行注入。");
#ifdef ACZERO_PORTABLE_V59
    if(hashFile(path)!=gameHash) fail("游戏文件与已验证的独立版 2.0.0.1 不匹配。");
#endif
}
static Game findGame() {
    Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0)};
    if(snapshot.value==INVALID_HANDLE_VALUE) fail("无法枚举进程。");
    PROCESSENTRY32W entry{}; entry.dwSize=sizeof(entry); std::vector<Game> matches;
    if(Process32FirstW(snapshot.value,&entry)) do {
        if(_wcsicmp(entry.szExeFile,L"acz.exe")) continue;
        Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,entry.th32ProcessID)};
        wchar_t path[32768]; DWORD n=32768;
        if(process.value&&QueryFullProcessImageNameW(process.value,0,path,&n)) matches.push_back({entry.th32ProcessID,path});
    } while(Process32NextW(snapshot.value,&entry));
    if(matches.size()!=1) fail("请先启动 AC0，并进入飞行任务。只能同时运行一个 acz.exe。");
    verifyGameFile(matches[0].path);
    return matches[0];
}
struct Module { std::wstring name,path; uintptr_t base; };
static std::vector<Module> modules(DWORD pid) {
    Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid)};
    if(snapshot.value==INVALID_HANDLE_VALUE) fail("无法读取游戏模块。若游戏以管理员身份运行，启动器也需相同权限。");
    MODULEENTRY32W entry{}; entry.dwSize=sizeof(entry); std::vector<Module> result;
    if(Module32FirstW(snapshot.value,&entry)) do { result.push_back({entry.szModule,entry.szExePath,reinterpret_cast<uintptr_t>(entry.modBaseAddr)}); } while(Module32NextW(snapshot.value,&entry));
    if(result.empty()) fail("游戏模块列表为空。"); return result;
}
static bool isMod(const Module& module) { return _wcsnicmp(module.name.c_str(),L"ACZeroMods",10)==0&&module.name.size()>4&&_wcsicmp(module.name.c_str()+module.name.size()-4,L".dll")==0; }
#ifndef ACZERO_PORTABLE_V59
static const std::array<Signature,25>& gameSignatures() {
    static const std::array<Signature,25> signatures={{
        {0x1040c0,{0x48,0x8b,0xc4,0x4c,0x89,0x40,0x18}}, {0x90a20,{0x48,0x8b,0xc4,0x48,0x89,0x58,0x08}},
        {0x13ace0,{0x4c,0x89,0x44,0x24,0x18}}, {0x187c80,{0x48,0x89,0x5c,0x24,0x08}},
        {0x13ef83,{0xe8,0x98,0x39,0x0c,0x00}}, {0x12aa30,{0x48,0x8b,0xc4,0x55,0x56,0x41,0x57}},
        {0xdd440,{0x40,0x55,0x41,0x56,0x48,0x8d,0x6c,0x24,0xc8}},
        {0x904d0,{0x48,0x8b,0xc4,0xc5,0xfa,0x11,0x58,0x20}}, {0x13f1bc,{0xe8,0x5f,0x37,0x0c,0x00}},
        {0xc15d0,{0x48,0x8b,0xc4,0x53,0x41,0x54,0x41,0x55}},
        {0xc1c20,{0x48,0x8b,0xc4,0x48,0x89,0x58,0x20}},
        {0xb9a60,{0x4c,0x8b,0xdc,0x4d,0x89,0x4b,0x20}},
        {0xb9b01,{0xc5,0x7a,0x10,0x05,0x57,0x90,0x21,0x00}},
        {0xb9bf9,{0xc5,0xfa,0x10,0xb4,0x83,0x0c,0x02,0x00,0x00}},
        {0xb9d80,{0x48,0x8b,0xc4,0x55,0x56,0x57,0x41,0x54}},
        {0xb6c10,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c}},
        {0xb00c0,{0x48,0x8b,0xc4,0x48,0x89,0x58,0x08,0x48,0x89,0x70,0x18}},
        {0xb056d,{0xe8,0x0e,0xdb,0x14,0x00}},
        {0xafd00,{0x48,0x8b,0xc4,0x48,0x89,0x58,0x10,0x4c,0x89,0x48,0x20}},
        {0xbf770,{0x48,0x83,0xec,0x38,0x44,0x8b,0x91,0xf0,0x01,0x00,0x00}},
        {0x12c740,{0x48,0x89,0x74,0x24,0x20,0x57,0x48,0x83,0xec,0x20}},
        {0xeae00,{0x48,0x89,0x5c,0x24,0x08}},
        {0xeba10,{0x48,0x89,0x5c,0x24,0x08}},
        {0xec6c0,{0x40,0x53,0x48,0x83,0xec,0x20}},
        {0xce880,{0x48,0x8b,0xc4,0x55,0x56,0x48,0x8d,0x6c,0x24,0x88}}}};
    return signatures;
}
#endif
static void verifySignatures(HANDLE process,uintptr_t base) {
    for(auto& signature:gameSignatures()) {
        unsigned char bytes[16]; SIZE_T read=0;
        if(!ReadProcessMemory(process,reinterpret_cast<void*>(base+signature.rva),bytes,signature.bytes.size(),&read)||read!=signature.bytes.size()||memcmp(bytes,signature.bytes.data(),read))
        {
            char message[240];snprintf(message,sizeof(message),"AC0 关键代码不匹配（RVA 0x%llx），未进行注入。请进入飞行任务并关闭其他修改器；若仍不符，当前程序布局与本包不兼容。",static_cast<unsigned long long>(signature.rva));
            fail(message);
        }
    }
}
static void verifyRuntime(HANDLE process,uintptr_t base) {
    verifySignatures(process,base);
#ifdef ACZERO_PORTABLE_V59
    for(const auto& body:nativeBodies) {
        std::vector<UCHAR> bytes(body.size);SIZE_T read=0;
        if(!ReadProcessMemory(process,reinterpret_cast<void*>(base+body.rva),bytes.data(),bytes.size(),&read)||
           read!=bytes.size()||hashBytes(bytes)!=body.sha256) {
            char message[220];snprintf(message,sizeof(message),"AC0 原生函数校验不匹配（RVA 0x%llx）。",static_cast<unsigned long long>(body.rva));
            fail(message);
        }
    }
#endif
}
static void attachGame() {
    verifyPackage(); Game game=findGame(); auto list=modules(game.pid);
    for(auto& module:list) if(isMod(module)) fail("当前游戏进程已经加载过 ACZeroMods，请使用原来的面板，或重启 AC0 后连接此包。");
    uintptr_t base=0; for(auto& module:list) if(!_wcsicmp(module.name.c_str(),L"acz.exe")) base=module.base;
    if(!base) fail("无法定位 AC0 主模块。");
    Handle reader{OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,game.pid)};
    if(!reader.value) fail("无法读取 AC0。请检查启动权限。"); verifyRuntime(reader.value,base);
    auto localLoad=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW"); MEMORY_BASIC_INFORMATION info{};
    if(!localLoad||!VirtualQuery(reinterpret_cast<void*>(localLoad),&info,sizeof(info))) fail("无法定位 Windows 加载器。");
    wchar_t host[32768]; if(!GetModuleFileNameW(static_cast<HMODULE>(info.AllocationBase),host,32768)) fail("无法读取 Windows 加载器路径。");
    auto hostName=wcsrchr(host,L'\\'); hostName=hostName?hostName+1:host;
    uintptr_t remoteLoad=0;
    for(auto& module:list) if(!_wcsicmp(module.name.c_str(),hostName)) remoteLoad=module.base+reinterpret_cast<uintptr_t>(localLoad)-reinterpret_cast<uintptr_t>(info.AllocationBase);
    MEMORY_BASIC_INFORMATION remoteInfo{};
    if(!remoteLoad||!VirtualQueryEx(reader.value,reinterpret_cast<void*>(remoteLoad),&remoteInfo,sizeof(remoteInfo))||remoteInfo.Type!=MEM_IMAGE||!(remoteInfo.Protect&0xf0)) fail("目标 Windows 加载器不可用。");
    Handle writer{OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ|PROCESS_VM_WRITE|PROCESS_VM_OPERATION|PROCESS_CREATE_THREAD,FALSE,game.pid)};
    if(!writer.value) fail("无法连接 AC0。若游戏以管理员身份运行，启动器也需相同权限。");
    for(auto key:{L"Stop",L"SwitchSP",L"CheckpointSave",L"CheckpointLoad"})set(L"Commands",key,L"0");
    SIZE_T size=(dll.size()+1)*sizeof(wchar_t),written=0;
    void* remote=VirtualAllocEx(writer.value,nullptr,size,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!remote) fail("无法分配 DLL 路径内存。");
    if(!WriteProcessMemory(writer.value,remote,dll.c_str(),size,&written)||written!=size) {VirtualFreeEx(writer.value,remote,0,MEM_RELEASE);fail("DLL 路径写入失败。");}
    Handle thread{CreateRemoteThread(writer.value,nullptr,0,reinterpret_cast<LPTHREAD_START_ROUTINE>(remoteLoad),remote,0,nullptr)};
    if(!thread.value) {VirtualFreeEx(writer.value,remote,0,MEM_RELEASE);fail("无法创建 AC0 加载线程。");}
    if(WaitForSingleObject(thread.value,10000)!=WAIT_OBJECT_0) fail("加载线程仍在运行，暂不重复连接。DLL 路径内存保留至游戏退出。");
    DWORD code=0; bool loaded=GetExitCodeThread(thread.value,&code)&&code;
    VirtualFreeEx(writer.value,remote,0,MEM_RELEASE); if(!loaded) fail("DLL 加载失败，请确认整包完整且解压路径可读。");
    connectedPid=game.pid;
    auto end=GetTickCount64()+4000;
    do {
        Sleep(50);
        Handle state{CreateFileW(statusFile.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr)};
        if(state.value==INVALID_HANDLE_VALUE) continue;
        char text[8192]{}; DWORD n=0; if(!ReadFile(state.value,text,sizeof(text)-1,&n,nullptr)) continue;
        std::string content(text,n),id="pid="+std::to_string(game.pid)+"\n";
        content.erase(std::remove(content.begin(),content.end(),'\r'),content.end());
        if(content.find(id)==std::string::npos) continue;
        if(content.find("state=active\n")!=std::string::npos&&content.find("version=" MOD_VERSION "\n")!=std::string::npos) return;
        if(content.find("state=")!=std::string::npos) fail("DLL 已加载但功能未启动，请查看 ACZeroMods.status 和日志，重启游戏后再试。");
    } while(GetTickCount64()<end);
    fail("DLL 已加载，尚未收到启动完成状态。请查看日志，暂不重复连接。");
}

struct Toggle {const wchar_t* section;const wchar_t* key;const wchar_t* label;HWND window;};
static Toggle toggles[]={
    {L"Features",L"DualSP",L"双 SP：原武器键 / F2 三段循环"},
    {L"Features",L"ProportionalNavigation",L"比例制导（下方可分别修改）"},
    {L"Features",L"GunAssist",L"机炮微辅助：仅已锁定目标"},
    {L"Features",L"InfiniteAmmo",L"无限弹药"},{L"Features",L"FastReload",L"快速装填"},
    {L"Features",L"HighG",L"高 G：加速＋减速＋拉杆"},{L"Features",L"MissileSpeed",L"导弹速度倍率"},
    {L"HUD",L"DualSPRows",L"五行武器 HUD"},{L"HUD",L"HighGIndicator",L"HIGH G 提示框"},
    {L"Features",L"NextTarget",L"NEXT 预告与机头优先切换"},
    {L"Features",L"ExtendedTargetBoxes",L"远距目标框（当前 30000 英尺）"},
    {L"Features",L"ExtendedTargetSelection",L"远距选择（保留武器锁定范围）"},
    {L"Features",L"TargetIndicatorName",L"方向标类型与冠名"},
#ifdef ACZERO_PORTABLE_V59
    {L"Features",L"MissileLockRange",L"导弹锁距倍率（见完整配置）"},
    {L"Features",L"MissileFlightRange",L"导弹航程倍率（见完整配置）"},
    {L"Checkpoint",L"AutoSave",L"任务阶段自动保存 CP"},
#endif
};
struct Input {const wchar_t* section;const wchar_t* key;const wchar_t* fallback;double low,high;HWND window;};
static const wchar_t* weapons[]={L"MSL",L"QAAM",L"XAAM",L"XLAA",L"SAAM",L"LASM",L"LAGM",L"XAGM",L"SOD",L"Other"};
static std::vector<Input> inputs;
static int px(int n) {return MulDiv(n,dpi,96);}
static HWND control(const wchar_t* cls,const wchar_t* text,DWORD style,int x,int y,int width,int height,int id=0) {
    HWND w=CreateWindowExW(!wcscmp(cls,L"EDIT")?WS_EX_CLIENTEDGE:0,cls,text,WS_CHILD|WS_VISIBLE|style,px(x),px(y),px(width),px(height),mainWindow,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
    SendMessageW(w,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE); return w;
}
static void label(const wchar_t* text,int x,int y,int width,int height=23) {control(L"STATIC",text,SS_LEFT,x,y,width,height);}
static void input(const wchar_t* section,const wchar_t* key,const wchar_t* fallback,double low,double high,int x,int y,int width=90) {
    inputs.push_back({section,key,fallback,low,high,control(L"EDIT",get(section,key,fallback).c_str(),ES_AUTOHSCROLL|WS_TABSTOP,x,y,width,23)});
}
static bool parse(const std::wstring& value,double low,double high,double& out) {
    wchar_t* end=nullptr; out=wcstod(value.c_str(),&end);
    if(end==value.c_str()||!std::isfinite(out)||out<low||out>high) return false;
    while(*end==L' '||*end==L'\t')++end; return !*end;
}
static void apply() {
    std::vector<std::wstring> values;
    for(auto& item:inputs) {
        wchar_t value[128];GetWindowTextW(item.window,value,128);double n;
        if(!parse(value,item.low,item.high,n)) {std::wstring message=std::wstring(item.section)+L" / "+item.key+L" 数值无效，请按面板范围填写。";throw std::runtime_error(utf8(message));}
        values.emplace_back(value);
    }
    int selection=int(SendMessageW(slotCombo,CB_GETCURSEL,0,0)); if(selection<0||selection>3) fail("请选择自动选择或 SP2 槽位 0、1、2。");
    int slot=selection-1;
    for(size_t i=0;i<inputs.size();i++)set(inputs[i].section,inputs[i].key,values[i]);
    bool changed=get(L"DualSP",L"SecondarySlot",L"2")!=std::to_wstring(slot);
    set(L"DualSP",L"SecondarySlot",std::to_wstring(slot));
    SetWindowTextW(statusLabel,changed?L"参数已保存。SP2 请在取消双 SP 后恢复飞行一小段，再勾选；或下次进入任务生效。":L"参数已保存，制导、速度、高 G 参数约 0.2 秒后读取。");
}
static void buildUi() {
    auto heading=control(L"STATIC",L"ACZero Mods v" MOD_VERSION_W L" · 便携版",SS_LEFT,20,12,900,30);SendMessageW(heading,WM_SETFONT,reinterpret_cast<WPARAM>(titleFont),TRUE);
    label(L"AC0 " GAME_VERSION_W L" ｜ 先进入飞行任务，再点连接 ｜ 无需安装 Python",20,44,940);
    control(L"BUTTON",L"功能开关",BS_GROUPBOX,16,73,324,322+extraRows);
    for(size_t i=0;i<std::size(toggles);i++) {auto& item=toggles[i];item.window=control(L"BUTTON",item.label,BS_AUTOCHECKBOX|WS_TABSTOP,30,99+int(i)*22,296,22,100+int(i));SendMessageW(item.window,BM_SETCHECK,enabled(item.section,item.key)?BST_CHECKED:BST_UNCHECKED,0);}
    control(L"BUTTON",L"双 SP 选择",BS_GROUPBOX,16,407+extraRows,324,119);
    label(L"SP2 使用机体原有的武器槽：",30,432+extraRows,285);
    slotCombo=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,30,460+extraRows,280,130);
    const wchar_t* slots[]={L"自动选择不同于 SP1 的槽位",L"槽位 0（机体第一种 SP）",L"槽位 1（机体第二种 SP）",L"槽位 2（机体第三种 SP）"};
    for(auto text:slots)SendMessageW(slotCombo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));
    int slot=_wtoi(get(L"DualSP",L"SecondarySlot",L"-1").c_str());SendMessageW(slotCombo,CB_SETCURSEL,std::clamp(slot,-1,2)+1,0);
    label(L"与 SP1 相同会回退到下一槽；点应用保存。",30,495+extraRows,296,23);
    control(L"BUTTON",L"连接已启动的 AC0",BS_PUSHBUTTON|WS_TABSTOP,20,540+extraRows,316,34,1);
    control(L"BUTTON",L"关闭面板开关",BS_PUSHBUTTON|WS_TABSTOP,20,583+extraRows,150,32,2);
    control(L"BUTTON",L"打开完整配置",BS_PUSHBUTTON|WS_TABSTOP,184,583+extraRows,152,32,3);
    control(L"BUTTON",L"每种导弹参数",BS_GROUPBOX,356,73,590,338);
    label(L"弹种",376,98,80);label(L"PN 系数：0～20",475,98,180);label(L"速度倍率：0.25～3",690,98,230);
    for(int i=0;i<10;i++) {
        int y=126+i*26;label(weapons[i],376,y+2,80);
        input(L"ProportionalNavigation",weapons[i],L"0",0,20,475,y,160);
        if(i<8)input(L"MissileSpeed",weapons[i],L"1",.25,3,690,y,160);
        else label(L"不适用",690,y+2,160);
    }
    label(L"PN=0 使用原版制导；速度=1 使用原速。XAAM 对应游戏 XMAA。",373,385,563,22);
    control(L"BUTTON",L"高 G 参数",BS_GROUPBOX,356,425,590,116);
    label(L"正 G 倍率 1～3",374,453,150);input(L"HighG",L"PositiveMultiplier",L"2.2",1,3,526,450,90);
    label(L"负 G 倍率 1～2",636,453,150);input(L"HighG",L"NegativeMultiplier",L"1.2",1,2,807,450,90);
    label(L"备用速度 km/h",374,492,150);input(L"HighG",L"MinSpeedKmh",L"550",300,1800,526,489,90);
    label(L"默认按机体失速退出；机炮参数见完整配置。",636,492,294,38);
    label(L"CP：暂停 → CHECKPOINT → SAVE / LOAD；死亡菜单可 LOAD。",356,548,590,25);
    control(L"BUTTON",L"应用参数",BS_DEFPUSHBUTTON|WS_TABSTOP,356,583,285,32,4);
    control(L"BUTTON",L"打开使用说明",BS_PUSHBUTTON|WS_TABSTOP,655,583,291,32,5);
    statusLabel=control(L"STATIC",L"等待连接。CP 仅当前出击有效；关闭面板不会停用已加载功能。",SS_LEFT,20,632+extraRows,926,42);
    SetTimer(mainWindow,1,700,nullptr);
}
static void report(const std::string& text) {
    Handle file{CreateFileW((root+L"\\检查结果.txt").c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr)};
    if(file.value==INVALID_HANDLE_VALUE)return;DWORD n;const unsigned char bom[]={0xef,0xbb,0xbf};WriteFile(file.value,bom,3,&n,nullptr);WriteFile(file.value,text.data(),DWORD(text.size()),&n,nullptr);
}
static LRESULT CALLBACK windowProc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if(message==WM_COMMAND)try {
        int id=LOWORD(wp);
        if(id>=100&&id<100+int(std::size(toggles))&&HIWORD(wp)==BN_CLICKED) {auto& item=toggles[id-100];set(item.section,item.key,SendMessageW(item.window,BM_GETCHECK,0,0)==BST_CHECKED?L"1":L"0");}
        else if(id==1) {apply();SetWindowTextW(statusLabel,L"正在检查游戏版本并连接……");UpdateWindow(window);attachGame();SetWindowTextW(statusLabel,L"已连接 AC0，功能按面板开关生效。退出游戏后请重新连接。");}
        else if(id==2) {for(auto& item:toggles){set(item.section,item.key,L"0");SendMessageW(item.window,BM_SETCHECK,BST_UNCHECKED,0);}SetWindowTextW(statusLabel,L"已关闭面板内增强和显示开关；游戏 CHECKPOINT 菜单仍可使用。");}
        else if(id==3)ShellExecuteW(window,L"open",ini.c_str(),nullptr,root.c_str(),SW_SHOWNORMAL);
        else if(id==4)apply();
        else if(id==5)ShellExecuteW(window,L"open",(root+L"\\README-zh-CN.txt").c_str(),nullptr,root.c_str(),SW_SHOWNORMAL);
    }catch(const std::exception& error){MessageBoxW(window,widen(error.what()).c_str(),L"未完成",MB_OK|MB_ICONINFORMATION);}
    else if(message==WM_TIMER&&connectedPid) {
        Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,connectedPid)};DWORD code=0;
        if(!process.value||!GetExitCodeProcess(process.value,&code)||code!=STILL_ACTIVE) {connectedPid=0;SetWindowTextW(statusLabel,L"AC0 已退出。下次进入任务后，重新点击连接。");}
    }
    else if(message==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcW(window,message,wp,lp);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show) {
    wchar_t path[32768];GetModuleFileNameW(nullptr,path,32768);root=path;root.resize(root.find_last_of(L'\\'));ini=root+L"\\ACZeroMods.ini";dll=root+L"\\"+dllName;statusFile=root+L"\\ACZeroMods.status";
    int argc=0;LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    bool selfTest=argc>1&&!wcscmp(argv[1],L"--self-test"),checkGame=argc>1&&!wcscmp(argv[1],L"--check-game");
    bool uiTest=argc>1&&!wcscmp(argv[1],L"--ui-self-test");
    bool checkFile=argc>1&&!wcscmp(argv[1],L"--check-file");std::wstring fileToCheck=checkFile&&argc>2?argv[2]:L"";LocalFree(argv);
    if(checkFile)try {
        if(fileToCheck.empty())fail("--check-file 需要 EXE 路径。");
        verifyGameFile(fileToCheck);
#ifdef ACZERO_PORTABLE_V59
        report("PASS: file version " GAME_VERSION " / x64 PE / game EXE SHA256.\r\n");
#else
        report("PASS: file version " GAME_VERSION " / x64 PE. Complete EXE hash is not checked.\r\n");
#endif
        return 0;
    }catch(const std::exception& error){report(std::string("FAIL: ")+error.what()+"\r\n");return 2;}
    if(selfTest||checkGame)try {
        verifyPackage();double n=0;
        if(!parse(get(L"HighG",L"PositiveMultiplier",L"2.2"),1,3,n))fail("HighG 默认参数无效。");
        for(auto key:weapons)if(!parse(get(L"ProportionalNavigation",key,L"0"),0,20,n))fail("PN 默认参数无效。");
        for(int i=0;i<8;i++)if(!parse(get(L"MissileSpeed",weapons[i],L"1"),.25,3,n))fail("导弹速度默认参数无效。");
        if(parse(L"NaN",0,20,n)||parse(L"1oops",0,20,n)||parse(L"3.01",.25,3,n)||!parse(L"1.5",.25,3,n))fail("数值校验自检失败。");
        VS_FIXEDFILEINFO test{};test.dwSignature=VS_FFI_SIGNATURE;test.dwFileVersionMS=gameVersionMS;test.dwFileVersionLS=gameVersionLS;
        if(!supportedVersion(test))fail("版本匹配自检失败。");
        test.dwFileVersionLS=gameVersionLS+1;if(supportedVersion(test))fail("版本拒绝自检失败。");
        test.dwFileVersionLS=gameVersionLS;test.dwSignature=0;if(supportedVersion(test))fail("版本资源签名自检失败。");
        size_t fixtureSize=0;
        for(const auto& s:gameSignatures())fixtureSize=std::max(fixtureSize,s.rva+s.bytes.size());
        std::vector<unsigned char> fixture(fixtureSize);
        for(auto& s:gameSignatures())memcpy(fixture.data()+s.rva,s.bytes.data(),s.bytes.size());
        verifySignatures(GetCurrentProcess(),reinterpret_cast<uintptr_t>(fixture.data()));
        for(auto& s:gameSignatures()) {
            fixture[s.rva]^=1;bool rejected=false;
            try {verifySignatures(GetCurrentProcess(),reinterpret_cast<uintptr_t>(fixture.data()));}catch(const std::exception&){rejected=true;}
            fixture[s.rva]^=1;if(!rejected)fail("关键代码签名拒绝自检失败。");
        }
        std::string text="PASS: DLL SHA256 / portable paths / configuration / numeric validation / version policy / all "+std::to_string(gameSignatures().size())+" signature guards.\r\n";
#ifdef ACZERO_PORTABLE_V59
        std::vector<UCHAR> sample{'a','b','c'};
        if(hashBytes(sample)!="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")fail("SHA256 自检失败。");
        text+="Game EXE SHA256 is required. Native body profile: "+std::to_string(nativeBodies.size())+" hashes.\r\n";
#else
        text+="Game EXE whole-file SHA256 is not required.\r\n";
#endif
        if(checkGame) {
            auto game=findGame();auto list=modules(game.pid);bool loaded=false;uintptr_t base=0;
            for(auto& m:list){loaded|=isMod(m);if(!_wcsicmp(m.name.c_str(),L"acz.exe"))base=m.base;}
            text+="AC0 process name / file version " GAME_VERSION " / x64 PE: PASS\r\nPID="+std::to_string(game.pid)+"\r\n"+utf8(game.path)+"\r\nExisting mod="+(loaded?"yes":"no")+"\r\n";
            if(loaded)text+="Runtime signature check skipped: an ACZeroMods DLL is already loaded.\r\n";
            else {
                Handle reader{OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,game.pid)};
                if(!reader.value||!base)fail("只读代码检查无法打开 AC0。");
                verifyRuntime(reader.value,base);text+="Runtime code checks: PASS\r\n";
            }
            text+="Read-only check; no injection performed.\r\n";
        }
        report(text);return 0;
    }catch(const std::exception& e){report(std::string("FAIL: ")+e.what()+"\r\n");return 1;}
    SetProcessDPIAware();HDC dc=GetDC(nullptr);dpi=GetDeviceCaps(dc,LOGPIXELSY);ReleaseDC(nullptr,dc);
    font=CreateFontW(-px(14),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
    titleFont=CreateFontW(-px(20),0,0,0,FW_MEDIUM,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
    WNDCLASSW cls{};cls.lpfnWndProc=windowProc;cls.hInstance=instance;cls.lpszClassName=L"ACZeroPortableLauncher" MOD_VERSION_W;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);RegisterClassW(&cls);
    RECT bounds{0,0,px(966),px(686+extraRows)};AdjustWindowRect(&bounds,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,FALSE);
    mainWindow=CreateWindowW(cls.lpszClassName,L"ACZero Mods v" MOD_VERSION_W,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,bounds.right-bounds.left,bounds.bottom-bounds.top,nullptr,nullptr,instance,nullptr);
    if(!mainWindow)return 1;buildUi();
    if(uiTest){
        bool ok=std::size(toggles)==13+extraRows/22;RECT client{};GetClientRect(mainWindow,&client);
        for(HWND child=GetWindow(mainWindow,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){
            RECT rect{};GetWindowRect(child,&rect);MapWindowPoints(nullptr,mainWindow,reinterpret_cast<POINT*>(&rect),2);
            if(rect.left<0||rect.top<0||rect.right>client.right||rect.bottom>client.bottom)ok=false;
        }
        for(auto& item:toggles)if(!item.window||(SendMessageW(item.window,BM_GETCHECK,0,0)==BST_CHECKED)!=enabled(item.section,item.key))ok=false;
        report(ok?"PASS: hidden GUI created; all "+std::to_string(std::size(toggles))+" toggles match configuration; controls fit client area. No game opened or injected.\r\n":"FAIL: hidden GUI layout/configuration check.\r\n");
        DestroyWindow(mainWindow);DeleteObject(font);DeleteObject(titleFont);return ok?0:1;
    }
    ShowWindow(mainWindow,show);UpdateWindow(mainWindow);
    MSG message;while(GetMessageW(&message,nullptr,0,0)>0){if(!IsDialogMessageW(mainWindow,&message)){TranslateMessage(&message);DispatchMessageW(&message);}}
    DeleteObject(font);DeleteObject(titleFont);return 0;
}
