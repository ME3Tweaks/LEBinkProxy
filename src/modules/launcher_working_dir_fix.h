#pragma once

#include <filesystem>
#include <Windows.h>
#include "../utils/io.h"
#include "../utils/hook.h"
#include "../dllstruct.h"
#include "_base.h"
#include "drm.h"
#include "ue_types.h"

// These have to list outside the class or the actual hooking signature won't be correct
typedef int (WINAPI* CREATEPROCESSA)(LPCSTR, LPSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES,
	BOOL, DWORD, LPVOID, LPCSTR, LPSTARTUPINFOA, LPPROCESS_INFORMATION);
CREATEPROCESSA CreateProcessA_orig = NULL;
int WINAPI CreateProcessA_hook(LPCSTR lpApplicationName, LPSTR lpCommandLine,
	LPSECURITY_ATTRIBUTES lpProcessAttributes,
	LPSECURITY_ATTRIBUTES lpThreadAttributes,
	BOOL bInheritHandles,
	DWORD dwCreationFlags,
	LPVOID lpEnvironment,
	LPCSTR lpCurrentDirectory,
	LPSTARTUPINFOA lpStartupInfo,
	LPPROCESS_INFORMATION lpProcessInformation) {

	auto exePath = std::filesystem::absolute(lpApplicationName);
	auto fixedDir = std::filesystem::canonical(lpApplicationName).parent_path();
	GLogger.writeln(L"LauncherFix: Using working directory: %s", fixedDir.c_str());

	std::string lpCmdLine = std::string(lpCommandLine);
	auto wideLpCmdLine = std::wstring(lpCmdLine.begin(), lpCmdLine.end());

	_STARTUPINFOW si;
	ZeroMemory(&si, sizeof(si)); // We don't care. The original game doesn't set anything here either

	auto result = CreateProcessW(exePath.wstring().c_str(), const_cast<LPWSTR>(wideLpCmdLine.c_str()), lpProcessAttributes, lpThreadAttributes, bInheritHandles, dwCreationFlags, lpEnvironment, fixedDir.wstring().c_str(), &si, lpProcessInformation);
	return result;
}

class LauncherProcessLaunchWorkingDirFixModule
	: public IModule
{
private:
	// Config parameters.

	// Fields.

	// Methods.

	bool detourOffsets_()
	{
		GLogger.writeln(L"detourOffsets_: hooking CreateProcessA %p into %p, preserving into %p",
			CreateProcessA_hook, CreateProcessA, reinterpret_cast<LPVOID*>(&CreateProcessA_orig));
		return GHookManager.Install(CreateProcessA, CreateProcessA_hook, reinterpret_cast<LPVOID*>(&CreateProcessA_orig), "CreateProcessA");
	}

public:
	LauncherProcessLaunchWorkingDirFixModule() : IModule{ "LauncherProcessLaunchWorkingDirFix" }
	{
		active_ = true;
	}

	bool Activate() override
	{

		if (!this->detourOffsets_())
		{
			return false;
		}

		return true;
	}

	void Deactivate() override
	{

	}
};
