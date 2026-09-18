#include "dllexports.h"

#include <Windows.h>
#include <filesystem>
#include <Dbghelp.h>
#include <atomic>

#include <cwchar>

#include "conf/version.h"
#include "gamever.h"
#include "utils/io.h"
#include "utils/hook.h"
#include "dllstruct.h"
#include "utils/memory.h"
#include "spi.h"
#include "modules/asi_loader.h"
#include "modules/console_enabler.h"
#include "modules/launcher_working_dir_fix.h"
#include "modules/launcher_args.h"


PVOID GhVEH = NULL;
inline HANDLE GProxyBootstrapThread = nullptr;
inline HANDLE GProxyAsyncAttachThread = nullptr;
inline HANDLE GProxyReadyEvent = nullptr;
inline DWORD GProxyBootstrapThreadId = 0;
inline DWORD GProxyAsyncAttachThreadId = 0;
inline std::atomic_bool GProxyDetachStarted{ false };
inline std::atomic_bool GProxyInitializationSucceeded{ false };
inline std::atomic_bool GProxyOutputReady{ false };
inline std::atomic<DWORD> GProxyBootstrapWorkerId{ 0 };

void SignalProxyReady(bool succeeded) noexcept
{
	GProxyInitializationSucceeded.store(succeeded, std::memory_order_release);
	if (GProxyReadyEvent != nullptr)
		SetEvent(GProxyReadyEvent);
}

bool WaitForProxyReady()
{
	// An ASI loaded by the bootstrap thread can call this readiness export while
	// the bootstrap is still constructing the proxy. Waiting on our own event
	// would deadlock, so report only the already-published success state instead
	// of claiming readiness before preload dispatch has completed.
	if (GetCurrentThreadId() == GProxyBootstrapWorkerId.load(std::memory_order_acquire))
		return GProxyInitializationSucceeded.load(std::memory_order_acquire);

	HANDLE const readyEvent = GProxyReadyEvent;
	if (readyEvent == nullptr)
		return false;

	if (WaitForSingleObject(readyEvent, INFINITE) != WAIT_OBJECT_0)
		return false;

	return GProxyInitializationSucceeded.load(std::memory_order_acquire);
}

extern "C" __declspec(dllexport) bool LEBinkProxyEnsureReady()
{
	return WaitForProxyReady();
}

LONG LEBinkProxyVectoredExceptionHandler(PEXCEPTION_POINTERS pExceptionPtrs)
{
	// If this is a debugging exception, skip.
	auto code = pExceptionPtrs->ExceptionRecord->ExceptionCode;
	if (code == CONTROL_C_EXIT
		|| code == STATUS_BREAKPOINT
		|| code == DBG_PRINTEXCEPTION_C || code == DBG_PRINTEXCEPTION_WIDE_C
		|| code == 0xE06D7363 || code == 0x406D1388)  // the stuff that gets thrown "normally"
	{
		goto return_from_handler;
	}


	// Load dbghelp and the MiniDumpWriteDump function.

	auto libDbghelp = LoadLibraryA("dbghelp");
	if (!libDbghelp)
	{
		goto return_from_handler;
	}
	auto fnMiniDumpWriteDump = (decltype(&MiniDumpWriteDump))GetProcAddress(libDbghelp, "MiniDumpWriteDump");
	if (!fnMiniDumpWriteDump)
	{
		goto return_from_handler;
	}


	// Get the base dump file name, which will be completed depending on the dump mode.

	wchar_t wstrDumpFile[MAX_PATH];
	auto dwModuleNameLen = GetModuleFileNameW(GetModuleHandleW(0), wstrDumpFile, MAX_PATH);

	MINIDUMP_TYPE dumpType;
	SYSTEMTIME time;
	GetSystemTime(&time);


	// If -killmydisk is set, dump the entire process memory.
	// Otherwise, only the crash call stack.

	// This loads before proxy handling class so we have to fetch full command line and not autoboot version.
	if (nullptr != std::wcsstr(GetCommandLineW(), L" -killmydisk"))
	{
		dumpType = MINIDUMP_TYPE(MiniDumpWithFullMemory);
		swprintf(wstrDumpFile + dwModuleNameLen - 4, MAX_PATH, L"_%4d%02d%02d_%02d%02d%02df.dmp",
			time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
	}
	else
	{
		dumpType = MINIDUMP_TYPE(MiniDumpNormal);
		swprintf(wstrDumpFile + dwModuleNameLen - 4, MAX_PATH, L"_%4d%02d%02d_%02d%02d%02dn.dmp",
			time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
	}


	// Open a dump file in the game's directory.

	auto dumpFile = CreateFileW(wstrDumpFile, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
	if (dumpFile == INVALID_HANDLE_VALUE)
	{
		goto return_from_handler;
	}

	MINIDUMP_EXCEPTION_INFORMATION exInfo;
	exInfo.ThreadId = GetCurrentThreadId();
	exInfo.ExceptionPointers = pExceptionPtrs;
	exInfo.ClientPointers = FALSE;


	// Write the dump.

	auto dumped = fnMiniDumpWriteDump(
		GetCurrentProcess(), GetCurrentProcessId(),
		dumpFile, dumpType,
		pExceptionPtrs ? &exInfo : nullptr,
		nullptr, nullptr);

	CloseHandle(dumpFile);

return_from_handler:

	return EXCEPTION_CONTINUE_SEARCH;
}
void SetVectoredExceptionHandler()
{
	GhVEH = AddVectoredExceptionHandler(1, LEBinkProxyVectoredExceptionHandler);
}
void UnsetVectoredExceptionHandler()
{
	if (GhVEH)
	{
		RemoveVectoredExceptionHandler(GhVEH);
	}
}

// Fixes bad launcher logic when not using Autoboot (sets the wrong working directory)
void SetWorkingDirectory() {
	WCHAR path[MAX_PATH];
	GetModuleFileNameW(NULL, path, MAX_PATH);
	std::filesystem::path exePath = path;
	SetCurrentDirectoryW(exePath.parent_path().c_str());
}

// Licensee 205->206 (GMinPackageVersion)
// Must be patched before the game reads it, which happens very early during game init.
// The first call is made directly from DllMain before the bootstrap worker is
// created. The function therefore cannot rely on GLEBinkProxy or the logger,
// both of which are initialized by the worker.
#define LMP_LICENSEE_VERSION_RVA 0x1755c1c
bool PatchLicenseeVersionForLMP()
{
	wchar_t executablePath[MAX_PATH]{};
	if (GetModuleFileNameW(nullptr, executablePath, _countof(executablePath)) == 0 ||
		wcsstr(executablePath, LMP_ExecutableStem) == nullptr)
	{
		return false;
	}

	auto* imageBase = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
	if (imageBase == nullptr)
		return false;

	auto* patchAddress = reinterpret_cast<int*>(imageBase + LMP_LICENSEE_VERSION_RVA);

	DWORD oldProtect;
	if (!VirtualProtect(patchAddress, sizeof(int), PAGE_READWRITE, &oldProtect))
	{
		if (GProxyOutputReady.load(std::memory_order_acquire))
			GLogger.writeln(L"PatchLicenseeVersionForLMP: ERROR: VirtualProtect (RW) failed (code = %d).", GetLastError());
		else
			OutputDebugStringW(L"LEBinkProxy: early LMP licensee patch VirtualProtect failed.\n");
		return false;
	}
	*patchAddress = 206;
	if (!VirtualProtect(patchAddress, sizeof(int), oldProtect, &oldProtect))
	{
		if (GProxyOutputReady.load(std::memory_order_acquire))
			GLogger.writeln(L"PatchLicenseeVersionForLMP: WARNING: failed to restore original page protection (code = %d).", GetLastError());
		else
			OutputDebugStringW(L"LEBinkProxy: early LMP licensee patch could not restore page protection.\n");
	}

	if (GProxyOutputReady.load(std::memory_order_acquire))
		GLogger.writeln(L"PatchLicenseeVersionForLMP: patched licensee version to LMP (206) at 0x%p.", patchAddress);
	return true;
}

void __stdcall AsyncAttach()
{
	// Handle logic depending on the attached-to exe.
	switch (GLEBinkProxy.Game)
	{
	case LEGameVersion::LE1:
	case LEGameVersion::LE2:
	case LEGameVersion::LE3:
	case LEGameVersion::LMP:
	{
		// Keep trying to find a pattern we are *almost* guaranteed to have until we find it.
		DRM::WaitForDRMv3();

		// On DRM'd exes, the image is still encrypted until DRM unwraps it, so the early patch
		// attempt in SyncAttach might fail as that area of memory is not yet decrypted. Re-check/patch here
		// now that the image is guaranteed to be in its final decrypted state.
		PatchLicenseeVersionForLMP();

		// Unlock the console.
		if (!GLEBinkProxy.ConsoleEnabler->Activate())
		{
			GLogger.writeln(L"OnAttach: ERROR: console bypass installation failed, aborting!");
			break;
		}

		// Load all native mods that declare being post-drm.
		GLEBinkProxy.AsiLoader->PostLoad(GLEBinkProxy.SPI);

		break;
	}
	case LEGameVersion::Launcher:
	{
		// Keep trying to find a pattern we are *almost* guaranteed to have until we find it.
		DRM::WaitForDRMv3();

		if (!GLEBinkProxy.LauncherArgs->Activate())
		{
			GLogger.writeln(L"OnAttach: ERROR: handling of Launcher args failed, aborting!");
		}

		// Hook CreateProcessA so we can properly set the working directory when launching a game.
		if (!GLEBinkProxy.LauncherFixer->Activate())
		{
			GLogger.writeln(L"OnAttach: ERROR: error fixing launcher working directory issue!");
		}

		// Load all native mods that declare being post-drm.
		GLEBinkProxy.AsiLoader->PostLoad(GLEBinkProxy.SPI);

		break;
	}
	default:
	{
		GLogger.writeln(L"OnAttach: unsupported game, bye!");
		return;
	}
	}

	return;
}

DWORD WINAPI AsyncAttachThreadProc(LPVOID)
{
	AsyncAttach();
	return 0;
}

using t_PostDRM_hook = void*(void*, char, size_t, void*);
t_PostDRM_hook* PostDRM_hook_orig = nullptr;
void* PostDRM_hook(void* param_1, char param_2, size_t param_3, void* param_4) 
{
	void* ret = PostDRM_hook_orig(param_1, param_2, param_3, param_4);
	// Unlock the console.
	if (!GLEBinkProxy.ConsoleEnabler->Activate())
	{
		GLogger.writeln(L"OnAttach: ERROR: console bypass installation failed");
	}
	GLEBinkProxy.AsiLoader->PostLoad(GLEBinkProxy.SPI);
	return ret;
}

bool SyncAttach()
{
	// Open console or log.
	Utils::SetupOutput();
	GProxyOutputReady.store(true, std::memory_order_release);

	struct ReadySignal final
	{
		bool succeeded = false;
		~ReadySignal() { SignalProxyReady(succeeded); }
	} readySignal;

	GLogger.writeln(L"Attached...\n"
		L"LEBinkProxy by d00telemental/ME3Tweaks\n"
		L"Version=\"" LEBINKPROXY_VERSION L"\", built=\"" LEBINKPROXY_BUILDTM L"\", config=\"" LEBINKPROXY_BUILDMD L"\"\n"
		L"Only trust distributions installed by ME3Tweaks Mod Manager 7.0+.");

	if (nullptr != std::wcsstr(GetCommandLineW(), L" -enableminidumps")) {
		// Register exception handler for memory dumps.
		// Removed on DETACH.
		GLogger.writeln(L"Minidumps are enabled for this session");
		SetVectoredExceptionHandler();
	}

	// Initialize MinHook.
	MH_STATUS mhStatus = MH_Initialize();
	if (mhStatus != MH_OK)
	{
		GLogger.writeln(L"OnAttach: ERROR: failed to initialize the hooking library (code = %d).", mhStatus);
		return false;
	}

	// Initialize global settings.
	GLEBinkProxy.Initialize();

	// Reapply the LMP patch after proxy identity initialization. DllMain already
	// attempted the earliest patch; this second call is also useful after DRM
	// has replaced encrypted image pages.
	PatchLicenseeVersionForLMP();

	// Register modules (console enabler, launcher arg handler, asi loader).
	GLEBinkProxy.AsiLoader = new AsiLoaderModule;
	GLEBinkProxy.ConsoleEnabler = new ConsoleEnablerModule;
	if (GLEBinkProxy.Game == LEGameVersion::Launcher)
	{
		GLEBinkProxy.LauncherArgs = new LauncherArgsModule;
		GLEBinkProxy.LauncherFixer = new LauncherProcessLaunchWorkingDirFixModule;
	}

	// Spawn the SPI implementation.
	GLEBinkProxy.SPI = new SPI::SharedProxyInterface();
	GLogger.writeln(L"OnAttach: instanced the SPI! (ver = %d)", ASI_SPI_VERSION);
	GLogger.writeln(L"OnAttach: SPI v4 console broker is sticky until process termination; legacy ASIs that call FreeConsole can still disrupt output.");

	// Find all native mods and iteratively call LoadLibrary().
	if (!GLEBinkProxy.AsiLoader->Activate())
	{
		GLogger.writeln(L"OnAttach: ERROR: loading of one or more ASI plugins failed!");
	}

	GLogger.writeln(L"OnAttach: got to preload / 0x%p", GLEBinkProxy);

	// Load all native mods that declare being pre-drm.
	// Post-drm mods are loaded in the switch below.
	GLEBinkProxy.AsiLoader->PreLoad(GLEBinkProxy.SPI);

	//if (GLEBinkProxy.Game == LEGameVersion::LE1 
	// || GLEBinkProxy.Game == LEGameVersion::LE2 
	// || GLEBinkProxy.Game == LEGameVersion::LE3)
	//{
	//	if (DRM::InstallDRMv4Hook(PostDRM_hook, reinterpret_cast<void**>(&PostDRM_hook_orig)))
	//	{
	//		return true;
	//	}
	//	//fallback to older method if the hook fails
	//}

	GProxyAsyncAttachThread = CreateThread(nullptr, 0, AsyncAttachThreadProc, nullptr, 0, &GProxyAsyncAttachThreadId);
	if (GProxyAsyncAttachThread == nullptr)
	{
		GLogger.writeln(L"OnAttach: ERROR: failed to create the tracked post-DRM worker (code = %d).", GetLastError());
	}

	readySignal.succeeded = true;
	return true;
}

DWORD WINAPI ProxyBootstrapThreadProc(LPVOID)
{
	GProxyBootstrapWorkerId.store(GetCurrentThreadId(), std::memory_order_release);
	SyncAttach();
	GProxyBootstrapWorkerId.store(0, std::memory_order_release);
	return 0;
}

void __stdcall OnDetach()
{
	if (GProxyDetachStarted.exchange(true))
		return;

	// This function is called only from the explicit exported shutdown entry
	// point. Waiting here is safe because it is outside loader lock; the
	// DLL_PROCESS_DETACH path below intentionally does not call it.
	if (GProxyBootstrapThread != nullptr && GetCurrentThreadId() != GProxyBootstrapThreadId)
	{
		WaitForSingleObject(GProxyBootstrapThread, INFINITE);
		CloseHandle(GProxyBootstrapThread);
		GProxyBootstrapThread = nullptr;
		GProxyBootstrapThreadId = 0;
	}
	if (GProxyAsyncAttachThread != nullptr && GetCurrentThreadId() != GProxyAsyncAttachThreadId)
	{
		WaitForSingleObject(GProxyAsyncAttachThread, INFINITE);
		CloseHandle(GProxyAsyncAttachThread);
		GProxyAsyncAttachThread = nullptr;
		GProxyAsyncAttachThreadId = 0;
	}

	// A bootstrap thread creation failure occurs before SetupOutput, so there
	// is no logger or module state to tear down on this path.
	if (!GProxyOutputReady.load(std::memory_order_acquire))
	{
		if (GProxyReadyEvent != nullptr)
		{
			CloseHandle(GProxyReadyEvent);
			GProxyReadyEvent = nullptr;
		}
		return;
	}

	GLogger.writeln(L"OnDetach: entered...");

	// Unload the DLLs.
	if (GLEBinkProxy.AsiLoader)       GLEBinkProxy.AsiLoader->Deactivate();

	// No-ops
	if (GLEBinkProxy.LauncherArgs)    GLEBinkProxy.LauncherArgs->Deactivate();
	if (GLEBinkProxy.ConsoleEnabler)  GLEBinkProxy.ConsoleEnabler->Deactivate();

	GLogger.writeln(L"OnDetach: goodbye, I thought we were friends :(");
	Utils::TeardownOutput();

	// Remove the VEH handler we set in OnAttach.
	UnsetVectoredExceptionHandler();

	if (GProxyReadyEvent != nullptr)
	{
		CloseHandle(GProxyReadyEvent);
		GProxyReadyEvent = nullptr;
	}

}

// Call this before FreeLibrary when an embedding host explicitly unloads the
// proxy. DLL_PROCESS_DETACH cannot perform this work because the loader lock is
// held and ASI callbacks may wait on arbitrary process state.
extern "C" __declspec(dllexport) bool LEBinkProxyShutdown()
{
	OnDetach();
	return true;
}

BOOL WINAPI DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpReserved) {
	switch (dwReason) {
	case DLL_PROCESS_ATTACH:
		DisableThreadLibraryCalls(hModule);
		// DllMain must not load ASIs, acquire broker locks, or call ASI code.
		// The event gives explicit consumers a readiness contract without forcing
		// DllMain to wait under loader lock.
		// This patch is intentionally independent of GLEBinkProxy and logging:
		// DRM's LMP value must be changed before the game reaches its entry point.
		PatchLicenseeVersionForLMP();
		GProxyReadyEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (GProxyReadyEvent == nullptr)
		{
			OutputDebugStringW(L"LEBinkProxy: failed to create the initialization-ready event.\n");
			return TRUE;
		}
		GProxyBootstrapThread = CreateThread(nullptr, 0,
			ProxyBootstrapThreadProc, nullptr, 0, &GProxyBootstrapThreadId);
		if (GProxyBootstrapThread == nullptr)
		{
			SignalProxyReady(false);
			OutputDebugStringW(L"LEBinkProxy: failed to create the bootstrap worker.\n");
		}
		return TRUE;

	case DLL_PROCESS_DETACH:
		// DLL_PROCESS_DETACH with a non-null lpReserved is process termination.
		// The loader lock is held here, so ASI callbacks, waits, joins, and hook
		// teardown are unsafe and can keep the game alive indefinitely. The OS
		// will reclaim the proxy and injected-module state as the process exits.
		if (lpReserved == nullptr)
			OutputDebugStringW(L"LEBinkProxy: explicit unload requires LEBinkProxyShutdown before FreeLibrary.\n");
		return TRUE;

	default:
		return TRUE;
	}
}
