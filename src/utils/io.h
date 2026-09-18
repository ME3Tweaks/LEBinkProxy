#pragma once

#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>

#include <cstdio>
#include <cstring>
#include <string>

#include <mutex>
#include <thread>

#include "console_broker.h"


#define ASI_LOG_FNAME "bink2w64_proxy.log"

#define ASI_IO_LOCK(MUTEX) const std::lock_guard<std::mutex> lock(MUTEX);

namespace Utils
{
	inline FILE* FGLog = nullptr;
	inline SPI::ConsoleBroker GConsoleBroker;
	inline SPIConsoleLease GConsoleLease = 0;
	inline HANDLE GConsoleOutput = INVALID_HANDLE_VALUE;
	inline std::mutex GWriteLineMtx;

    // Acquire a proxy diagnostic lease and retain a private CONOUT$ handle in
    // debug builds. The proxy never redirects CRT streams and never tears down
    // the process console when this lease ends.
    inline void SetupOutput()
    {
#ifdef ASI_DEBUG
		if (!GConsoleBroker.AcquireConsoleLease(L"LEBinkProxy", &GConsoleLease) ||
			!GConsoleBroker.OpenConsoleOutput(GConsoleLease, &GConsoleOutput))
		{
			GConsoleBroker.ReleaseConsoleLease(GConsoleLease);
			GConsoleLease = 0;
			GConsoleOutput = INVALID_HANDLE_VALUE;
			OutputDebugStringW(L"LEBinkProxy: failed to acquire broker console output.\n");
		}
#else
        // Ensure Logs directory exists (ignore error if it already exists)
        CreateDirectoryA("Logs", NULL);

        // Build path: Logs\<filename>
        char logPath[MAX_PATH];
        _snprintf_s(logPath, MAX_PATH, _TRUNCATE, "Logs\\%s", ASI_LOG_FNAME);

        FGLog = fopen(logPath, "w");
        if (FGLog == NULL)
        {
            wchar_t errorBuffer[512];
            wsprintf(errorBuffer, L"Failed to create / open a log file.\r\nError code: %d.\r\nIs your game directory writable? Do not run modding tools as administrator unless instructed to by a developer. Tools must run in the same elevation context as the game.", GetLastError());
            MessageBoxW(NULL, errorBuffer, L"Bink proxy error", MB_OK | MB_ICONERROR | MB_TOPMOST);
        }
#endif
    }
    // Close the console window.
    inline void TeardownOutput()
    {
#ifdef ASI_DEBUG
		if (GConsoleOutput != INVALID_HANDLE_VALUE)
		{
			CloseHandle(GConsoleOutput);
			GConsoleOutput = INVALID_HANDLE_VALUE;
		}
		GConsoleBroker.ReleaseConsoleLease(GConsoleLease);
		GConsoleLease = 0;
#else
        if (FGLog != nullptr)
        {
            fclose(FGLog);
			FGLog = nullptr;
        }
#endif
    }

	struct RuntimeLogger
	{
	private:
		bool initialized = false;
		SYSTEMTIME localTime;

		int writeInternal(wchar_t const* line, wchar_t const* postfix)
		{
			GetLocalTime(&localTime);
			wchar_t buffer[1200]{};
			int const length = _snwprintf_s(buffer, _countof(buffer), _TRUNCATE,
				L"%02d:%02d:%02d.%03d  %s%s",
				localTime.wHour, localTime.wMinute, localTime.wSecond, localTime.wMilliseconds,
				line, postfix ? postfix : L"");
			if (length < 0)
				return -1;

#ifdef ASI_DEBUG
			if (GConsoleOutput == INVALID_HANDLE_VALUE)
			{
				OutputDebugStringW(buffer);
				return length;
			}

			DWORD written = 0;
			DWORD consoleMode = 0;
			if (GetConsoleMode(GConsoleOutput, &consoleMode) != FALSE &&
				WriteConsoleW(GConsoleOutput, buffer, static_cast<DWORD>(length), &written, nullptr) != FALSE)
			{
				return static_cast<int>(written);
			}

			int const utf8Length = WideCharToMultiByte(CP_UTF8, 0, buffer, length, nullptr, 0, nullptr, nullptr);
			if (utf8Length <= 0)
				return -1;
			std::string utf8(static_cast<size_t>(utf8Length), '\0');
			WideCharToMultiByte(CP_UTF8, 0, buffer, length, utf8.data(), utf8Length, nullptr, nullptr);
			if (WriteFile(GConsoleOutput, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr) == FALSE)
				return -1;
			return static_cast<int>(written);
#else
			return fwprintf(FGLog, L"%s", buffer);
#endif
		}

		int writeAndFlush(wchar_t const* line, wchar_t const* postfix)
		{
			int rv = writeInternal(line, postfix);
#ifdef ASI_DEBUG
			if (GConsoleOutput != INVALID_HANDLE_VALUE)
				FlushFileBuffers(GConsoleOutput);
#else
			fflush(FGLog);
#endif
			return rv;
		}

	public:
		int writeln(wchar_t const* fmt_line, ...)
		{
			ASI_IO_LOCK(GWriteLineMtx);

			int rc;
			wchar_t buffer[1024];

			va_list args;
			va_start(args, fmt_line);
			rc = _vsnwprintf(buffer, 1024, fmt_line, args);
			va_end(args);

			if (rc < 0)
			{
				writeAndFlush(L"writeln: vsnprintf encoding error", L"\n");
				return -1;
			}
			else if (rc == 0)
			{
				writeAndFlush(L"writeln: vsnprintf runtime constraint violation", L"\n");
				return -1;
			}

			return writeAndFlush(buffer, L"\n");
		}
	};
}

// Global instance.
inline Utils::RuntimeLogger GLogger;
