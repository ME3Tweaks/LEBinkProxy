#pragma once

#include "utils/io.h"
#include "utils/event.h"
#include "dllstruct.h"
#include <wchar.h>
#include <Windows.h>


namespace DRM
{

    // Version 2
    // ======================================================================

    inline Utils::Event* DrmEvent = nullptr;
    inline bool GameWindowCreated = false;

    typedef HWND(WINAPI* CREATEWINDOWEXW)(DWORD dwExStyle, LPCWSTR lpClassName, LPCWSTR lpWindowName, DWORD dwStyle, int X, int Y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam);
    inline CREATEWINDOWEXW CreateWindowExW_orig = nullptr;
    inline HWND WINAPI CreateWindowExW_hooked(DWORD dwExStyle, LPCWSTR lpClassName, LPCWSTR lpWindowName, DWORD dwStyle, int X, int Y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam)
    {
        //GLogger.writeln(L"CreateWindowExW: lpWindowName = %s", lpWindowName);
        if (nullptr != lpWindowName
            && (0 == wcscmp(lpWindowName, GLEBinkProxy.WinTitle) || 0 == wcscmp(lpWindowName, L"SplashScreen")))
        {
            GLogger.writeln(L"CreateWindowExW: matched a title, signaling the event [%p]", DrmEvent);
            if (DrmEvent && !DrmEvent->Set())
            {
                auto error = GetLastError();
                GLogger.writeln(L"CreateWindowExW: event was not null but Set failed (%d)", error);
            }
        }
        return CreateWindowExW_orig(dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
    }

    inline void InitializeDRMv2()
    {
        DrmEvent = new Utils::Event(L"drm_wait");
    }
    void WaitForDRMv2();


    // Version 3
    // ======================================================================

    void WaitForDRMv3();

    void InstallDRMv4Hook(void* detour, void** original);
}
