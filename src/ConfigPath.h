#pragma once
#include <Windows.h>
#include <string>

// Environment lookup is safe in the proxy's early loader-lock initialization.
inline std::wstring FirstPersonNarrativeConfigPath() {
    const DWORD needed=GetEnvironmentVariableW(L"LOCALAPPDATA",nullptr,0);
    if(!needed)return {};
    std::wstring path(needed,L'\0');
    const DWORD written=GetEnvironmentVariableW(L"LOCALAPPDATA",path.data(),needed);
    if(!written||written>=needed)return {};
    path.resize(written);
    path+=L"\\Remedy\\AlanWake2\\fpn.ini";
    return path;
}
