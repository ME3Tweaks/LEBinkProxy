#include "drm.h"
#include <utils/memory.h>
#include <cwchar>

void DRM::WaitForDRMv2()
{
    GLogger.writeln(L"WaitForDRMv2: waiting for DRM...");

    if (!DrmEvent->InError())
    {
        auto rc = DrmEvent->WaitForIt(10000);  // 10 seconds timeout should be more than enough
        switch (rc)
        {
        case Utils::EventWaitValue::Signaled:
            GLogger.writeln(L"WaitForDRMv2: event signaled!");
            delete DrmEvent;
            DrmEvent = nullptr;
            break;
        default:
            GLogger.writeln(L"WaitForDRMv2: event wait failed (EventWaitValue = %d)", (int)rc);
        }
    }
}

void DRM::WaitForDRMv3()
{
    int iterations = 0;
    bool foundPattern = false;
    BYTE* offset = nullptr;

    do
    {
        switch (GLEBinkProxy.Game)
        {
        case LEGameVersion::LE1:
            foundPattern = nullptr != Utils::ScanProcess(LE1_UFunctionBind_Pattern, LE1_UFunctionBind_Mask);
            break;
        case LEGameVersion::LE2:
            foundPattern = nullptr != Utils::ScanProcess(LE2_UFunctionBind_Pattern, LE2_UFunctionBind_Mask);
            break;
        case LEGameVersion::LE3:
            foundPattern = nullptr != Utils::ScanProcess(LE3_UFunctionBind_Pattern, LE3_UFunctionBind_Mask);
            break;
        case LEGameVersion::Launcher:
            foundPattern = nullptr != Utils::ScanProcess(LEL_DRMTest_Pattern, LEL_DRMTest_Mask);
            break;
        default:
            foundPattern = true;
        }

        iterations++;

    } while (!foundPattern && iterations < 20000);

    if (!foundPattern)
    {
        GLogger.writeln(L"WaitForDRMv3 - FAILED TO FIND THE PATTERN in %d iteration(s), but still stopping the poll because YOLO.", iterations);
        return;
    }

    GLogger.writeln(L"WaitForDRMv3 - found the pattern in %d iteration(s), stopping the poll.", iterations);
}

BYTE* GetActivatorModuleBase() {
    DWORD const CurrentProcess = GetCurrentProcessId();
    HANDLE const ProcessSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, CurrentProcess);

    BYTE* BaseAddress = nullptr;

    if (ProcessSnapshot != INVALID_HANDLE_VALUE) {
        MODULEENTRY32 ModuleEntry{};
        ModuleEntry.dwSize = sizeof ModuleEntry;

        if (Module32First(ProcessSnapshot, &ModuleEntry)) {
            do {
                auto const* const ModuleName = ModuleEntry.szModule;
                if (ModuleName == std::wcsstr(ModuleName, L"Activation64")) {
                    BaseAddress = ModuleEntry.modBaseAddr;
                    break;
                }
            } while (Module32Next(ProcessSnapshot, &ModuleEntry));
        }

        CloseHandle(ProcessSnapshot);
    }

    return BaseAddress;
}

bool DRM::InstallDRMv4Hook(void* detour, void** original)
{
	auto const ActivatorBase = GetActivatorModuleBase();
	if (!ActivatorBase) {
		GLogger.writeln(L"InstallDRMv4Hook: ERROR: failed to find Activation64.dll base address.");
		return false;
	}
	auto const Offset = ActivatorBase + 0x11480;
	auto spiReturn = GLEBinkProxy.SPI->InstallHook("PostDRM_hook", Offset, detour, original);
    if (spiReturn != SPIReturn::Success)
    {
		GLogger.writeln(L"InstallDRMv4Hook: ERROR: failed to install post-drm hook (code = %d).", static_cast<int>(spiReturn));
		return false;
    }
	GLogger.writeln(L"InstallDRMv4Hook: successfully installed hook on Activation64.dll.");
	return true;
}