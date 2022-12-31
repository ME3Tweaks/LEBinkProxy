#pragma once
#include <string>
#include <Windows.h>

#include "conf/patterns.h"
#include "gamever.h"
#include "utils/io.h"
#include "spi/interface.h"


// Forward-declare these to avoid a cyclical header dependency.

class AsiLoaderModule;
class ConsoleEnablerModule;
class LauncherArgsModule;
class LauncherProcessLaunchWorkingDirFixModule;


struct LEBinkProxy
{
public:
    wchar_t ExePath[MAX_PATH];
    wchar_t ExeName[MAX_PATH];
    wchar_t WinTitle[MAX_PATH];
    wchar_t* CmdLine;

    LEGameVersion Game;

    AsiLoaderModule*       AsiLoader;
    ConsoleEnablerModule* ConsoleEnabler;
    LauncherProcessLaunchWorkingDirFixModule* LauncherFixer;
    LauncherArgsModule*    LauncherArgs;

    ISharedProxyInterface* SPI;

private:
    __forceinline
    void stripExecutableName_(wchar_t* path, wchar_t* newPath)
    {
        auto selectionStart = path;
        while (*path != L'\0')
        {
            if (*path == L'\\')
            {
                selectionStart = path;
            }
            path++;
        }

        wcscpy(newPath, selectionStart + 1);
    }

    __forceinline
    void associateWindowTitle_(wchar_t* exeName, wchar_t* winTitle)
    {
        if (0 == wcscmp(exeName, LEL_ExecutableName))
        {
            wcscpy(winTitle, LEL_WindowTitle);
            Game = LEGameVersion::Launcher;
        }
        else if (0 == wcscmp(exeName, LE1_ExecutableName))
        {
            wcscpy(winTitle, LE1_WindowTitle);
            Game = LEGameVersion::LE1;
        }
        else if (0 == wcscmp(exeName, LE2_ExecutableName))
        {
            wcscpy(winTitle, LE2_WindowTitle);
            Game = LEGameVersion::LE2;
        }
        else if (0 == wcscmp(exeName, LE3_ExecutableName))
        {
            wcscpy(winTitle, LE3_WindowTitle);
            Game = LEGameVersion::LE3;
        }
        else
        {
            GLogger.writeln(L"..AssociateWindowTitle: UNSUPPORTED EXE NAME %s", exeName);
            Game = LEGameVersion::Unsupported;
            exit(-1);
        }
    }

    /**
 * \brief Removes a value from HKEY_CURRENT_USER. Does not do error handling.
 * \param regSubKey
 * \param regValue
 * \return
 */
    static LSTATUS DeleteValueFromHKCU(const std::wstring& regSubKey, const std::wstring& regValue)
    {
        HKEY hKey = NULL;
        long lReturn = RegOpenKeyEx(HKEY_CURRENT_USER,
            regSubKey.c_str(),
            0L,
            KEY_SET_VALUE,
            &hKey);
        if (lReturn == ERROR_SUCCESS)
        {
            lReturn = RegDeleteValue(hKey, regValue.c_str());
            RegCloseKey(hKey);
        }

        return lReturn;
    }

    /*! \brief                          Returns a value from HKLM as string.
    \exception  std::runtime_error  Replace with your error handling.
    */
    static std::wstring GetStringValueFromHKCU(const std::wstring& regSubKey, const std::wstring& regValue)
    {
        size_t bufferSize = 0xFFF; // If too small, will be resized down below.
        std::wstring valueBuf; // Contiguous buffer since C++11.
        valueBuf.resize(bufferSize);
        auto cbData = static_cast<DWORD>(bufferSize * sizeof(wchar_t));
        auto rc = RegGetValueW(
            HKEY_CURRENT_USER,
            regSubKey.c_str(),
            regValue.c_str(),
            RRF_RT_REG_SZ,
            nullptr,
            static_cast<void*>(valueBuf.data()),
            &cbData
        );
        while (rc == ERROR_MORE_DATA)
        {
            // Get a buffer that is big enough.
            cbData /= sizeof(wchar_t);
            if (cbData > static_cast<DWORD>(bufferSize))
            {
                bufferSize = static_cast<size_t>(cbData);
            }
            else
            {
                bufferSize *= 2;
                cbData = static_cast<DWORD>(bufferSize * sizeof(wchar_t));
            }
            valueBuf.resize(bufferSize);
            rc = RegGetValueW(
                HKEY_CURRENT_USER,
                regSubKey.c_str(),
                regValue.c_str(),
                RRF_RT_REG_SZ,
                nullptr,
                static_cast<void*>(valueBuf.data()),
                &cbData
            );
        }
        if (rc == ERROR_SUCCESS)
        {
            cbData /= sizeof(wchar_t);
            valueBuf.resize(static_cast<size_t>(cbData - 1)); // remove end null character
            return valueBuf;
        }

    	// else 
        return L""; // Return nothing.
    }

    /**
     * \brief Gets command line from actual command line (if any) or from ME3Tweaks Registry Key (if none - EA app boot)
     */
    __forceinline
    void getUsedCommandLine_()
    {
        CmdLine = GetCommandLineW();
        auto leargs = GetStringValueFromHKCU(L"SOFTWARE\\ME3Tweaks", L"LEAutobootArgs");
        if (leargs.length() > 0)
        {
            CmdLine = new wchar_t[leargs.length()];
            wcscpy(CmdLine, leargs.data());
            DeleteValueFromHKCU(L"SOFTWARE\\ME3Tweaks", L"LEAutobootArgs");
        }
    }

public:
    __forceinline
    void Initialize()
    {
        getUsedCommandLine_();


        GetModuleFileNameW(NULL, ExePath, MAX_PATH);
        stripExecutableName_(ExePath, ExeName);
        associateWindowTitle_(ExeName, WinTitle);

        GLogger.writeln(L"..Initialize: cmd line = %s", CmdLine);
        GLogger.writeln(L"..Initialize: exe path = %s", ExePath);
        GLogger.writeln(L"..Initialize: exe name = %s", ExeName);
        GLogger.writeln(L"..Initialize: win title = %s", WinTitle);
    }
};

// Global instance.

static LEBinkProxy GLEBinkProxy;
