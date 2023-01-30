#pragma once

#include <Windows.h>
#include <shlobj.h>
#include <fstream>
#include <string>
#include <vector>
#include <utility>
#include <cwctype>
#include <iostream>
#include <map>

#include "gamever.h"
#include "../utils/io.h"
#include "_base.h"
#include "dllstruct.h"

#pragma comment(lib, "shell32.lib")


struct LaunchGameParams
{
	LEGameVersion Target;
	bool AutoTerminate;

	wchar_t* GameExePath;
	wchar_t* GameCmdLine;
	wchar_t* GameWorkDir;
};

void LaunchGameThread(LaunchGameParams launchParams)
{
	auto gameExePath = launchParams.GameExePath;
	auto gameCmdLine = launchParams.GameCmdLine;
	auto gameWorkDir = launchParams.GameWorkDir;


	STARTUPINFOW startupInfo;
	PROCESS_INFORMATION processInfo;

	ZeroMemory(&startupInfo, sizeof(startupInfo));
	ZeroMemory(&processInfo, sizeof(processInfo));
	startupInfo.cb = sizeof(startupInfo);


	GLogger.writeln(L"LaunchGameThread: lpApplicationName = %S", gameExePath);
	GLogger.writeln(L"LaunchGameThread: lpCommandLine = %S", gameCmdLine);
	GLogger.writeln(L"LaunchGameThread: lpCurrentDirectory = %S", gameWorkDir);

	DWORD flags = 0;

	//flags = CREATE_SUSPENDED;
	//GLogger.writeln(L"LaunchGameThread: WARNING! CREATING CHILD PROCESS IN SUSPENDED STATE!");

	auto rc = CreateProcessW(gameExePath, const_cast<wchar_t*>(gameCmdLine), nullptr, nullptr, false, flags, nullptr, gameWorkDir, &startupInfo, &processInfo);
	if (rc == 0)
	{
		GLogger.writeln(L"LaunchGameThread: failed to create a process (error code = %d)", GetLastError());
		return;
	}

	// If the user requested auto-termination, just kill the launcher now.
	if (launchParams.AutoTerminate)
	{
		GLogger.writeln(L"LaunchGameThread: created a process (pid = %d), terminating the launcher...", processInfo.dwProcessId);
		exit(0);
		return;  // just in case
	}

	// If not auto-terminated, wait for the process to end.
	GLogger.writeln(L"LaunchGameThread: created a process (pid = %d), waiting until it exits...", processInfo.dwProcessId);
	WaitForSingleObject(processInfo.hProcess, INFINITE);

	GLogger.writeln(L"LaunchGameThread: process exited, closing handles...");
	CloseHandle(processInfo.hProcess);
	CloseHandle(processInfo.hThread);
	return;
}


struct VoiceOverDataPair
{
	std::wstring LocalVO;
	std::wstring InternationalVO;

	VoiceOverDataPair() : LocalVO{}, InternationalVO{} {}
	VoiceOverDataPair(std::wstring local, std::wstring international) : LocalVO{ local }, InternationalVO{ international } {}
};


class LauncherArgsModule
	: public IModule
{
private:

	// Config parameters.

	// Fields.

	LEGameVersion launchTarget_ = LEGameVersion::Unsupported;
	bool autoTerminate_ = false;
	bool needsLauncherConfigParsed_ = true; // Defaults to true (turns to false if we find command line args)
	const wchar_t* bootLanguage_ = L"INT"; // The default language (English)
	const wchar_t* subtitleSize_ = L"20"; // The default subtitle size

	bool needsConfigMade_ = false;

	LaunchGameParams launchParams_;
	wchar_t cmdArgsBuffer_[4096];
	wchar_t debuggerPath_[512];

	// Methods.

	bool parseCmdLine_(wchar_t* cmdLine)
	{
		auto startCmdLine = cmdLine;

		auto endCmdLine = startCmdLine + wcslen(startCmdLine);
		auto startWCPtr = wcsstr(startCmdLine, L" -game ");

		// Detect '-game X'
		if (startWCPtr != nullptr && (size_t)(startWCPtr + 7) < (size_t)endCmdLine) // Make sure there is enough length for game id + '-game ' which is 7 chars
		{
			GLogger.writeln(L"LauncherArgsModule.parseCmdLine_: startWCPtr = %s", startWCPtr);

			auto numStrWCPtr = startWCPtr + 7;
			auto gameNum = wcstol(numStrWCPtr, nullptr, 10);

			// If the game number was parsed to be in [1;3], return it via out arg, and check for autoterminate flag.
			if (gameNum >= 1 && gameNum <= 3)
			{
				this->launchTarget_ = static_cast<LEGameVersion>(gameNum);
				ClipOutSubString(startCmdLine, wcsstr(startCmdLine, L" -game") - startCmdLine, 8);
				endCmdLine -= 8;

				// If found autoterminate flag, return it via out arg.
				if (nullptr != wcsstr(startCmdLine, L" -autoterminate"))
				{
					this->autoTerminate_ = true;
					ClipOutSubString(startCmdLine, wcsstr(startCmdLine, L" -autoterminate") - startCmdLine, 15);
					endCmdLine -= 14;
				}

				bool readSubs = false;
				bool readLang = false;

				// Look for '-Subtitles ' for subtitle size
				auto subtitlesPos = wcsstr(startCmdLine, L"-Subtitles ");
				if (subtitlesPos != nullptr && (size_t)(subtitlesPos + 11) < (size_t)endCmdLine) // This only works if subtitles size is 2 digits.
				{
					auto subSize = wcstol(subtitlesPos + 11, nullptr, 10);
					if (subSize > 0)
					{
						// I HATE CONST
						// I have almost no idea what this is doin
						auto result = new wchar_t[3];
						swprintf(result, 3, L"%d", subSize);
						wprintf(L"%ls", result);
						this->subtitleSize_ = result;
						ClipOutSubString(startCmdLine, subtitlesPos - startCmdLine, 14);
						endCmdLine -= 14;

						GLogger.writeln(L"LauncherArgsModule.parseCmdLine_: Found command line parameter -Subtitle, setting subtitle size to %ls", this->subtitleSize_);
						readSubs = true;
					}
				}
				else
				{
					GLogger.writeln(L"LauncherArgsModule.parseCmdLine_: Didn't find -Subtitles, defaulting to 20 (will read config file)");
				}

				// Look for '-OVERRIDELANGUAGE=' to set the language
				auto overrideLangPos = wcsstr(startCmdLine, L" -OVERRIDELANGUAGE="); // Length: 19

				if (overrideLangPos != nullptr)
				{
					GLogger.writeln(L"LauncherArgsModule.parseCmdLine_: Found command line parameter -OVERRIDELANGUAGE= at %x", overrideLangPos);
					auto langStartPos = overrideLangPos + 19;
					auto langEndPos = langStartPos;

					// Hackjob to find the end of string of the end of the end of the parameter
					while ((size_t)langEndPos < (size_t)endCmdLine && !std::iswspace(langEndPos[0]))
					{
						langEndPos++;
					}

					// Don't really know what I'm doing here...
					auto count = langEndPos - langStartPos;
					auto newLang = new wchar_t[count + 1]; // 1 for null terminator
					wcsncpy(newLang, langStartPos, count);
					newLang[count] = '\0';
					//newLang[count+1] = '\0';

					this->bootLanguage_ = newLang;

					ClipOutSubString(startCmdLine, overrideLangPos - startCmdLine, (int)count + 19);
					GLogger.writeln(L"LauncherArgsModule.parseCmdLine_: Found command line parameter -OVERRIDELANGUAGE=, setting lang to %ls", this->bootLanguage_);
					readLang = true;
				}
				else
				{
					// Check for 'language' parameter - M3 will send this if it is LE3 due to LE3 using this command line arg instead for some reason...
					overrideLangPos = wcsstr(startCmdLine, L" -language="); // Length: 11

					if (overrideLangPos != nullptr)
					{
						GLogger.writeln(L"LauncherArgsModule.parseCmdLine_: Found command line parameter -language= at %x", overrideLangPos);
						auto langStartPos = overrideLangPos + 11;
						auto langEndPos = langStartPos;

						// Hackjob to find the end of string of the end of the end of the parameter
						while ((size_t)langEndPos < (size_t)endCmdLine && !std::iswspace(langEndPos[0]))
						{
							langEndPos++;
						}

						// Don't really know what I'm doing here...
						auto count = langEndPos - langStartPos;
						auto newLang = new wchar_t[count + 1]; // 1 for null terminator
						wcsncpy(newLang, langStartPos, count);
						newLang[count] = '\0';
						//newLang[count+1] = '\0';

						this->bootLanguage_ = newLang;

						ClipOutSubString(startCmdLine, overrideLangPos - startCmdLine, (int)count + 11);
						GLogger.writeln(L"LauncherArgsModule.parseCmdLine_: Found command line parameter -language=, setting lang to %ls", this->bootLanguage_);
						readLang = true;
					}
					else
					{
						GLogger.writeln(L"LauncherArgsModule.parseCmdLine_: Didn't find -OVERRIDELANGUAGE= or -language, defaulting to INT (will read config file)");

					}
				}

				this->needsLauncherConfigParsed_ = !readLang || !readSubs;
				return true;
			}

			GLogger.writeln(L"LauncherArgsModule.parseCmdLine_: wcstol failed to retrieve a code in [1;3]");
			return false;
		}

		this->launchTarget_ = LEGameVersion::Unsupported;
		GLogger.writeln(L"LauncherArgsModule.parseCmdLine_: couldn't find '-game ', startWCPtr = %p", startWCPtr);
		return true;
	}

	bool parseLauncherConfig_()
	{
		// Default values.
		std::wstring subtitlesSize = L"20";
		std::wstring overrideLang = L"INT";

		// Make path to the config file.
		wchar_t documentsPath[MAX_PATH];
		HRESULT shResult = SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, documentsPath);
		if (shResult != S_OK)
		{
			GLogger.writeln(L"parseLauncherConfig_: failed to get Documents folder path, error = %lu", shResult);
			return false;
		}
		wchar_t launcherConfigPath[MAX_PATH * 2];
		swprintf(launcherConfigPath, MAX_PATH * 2, L"%s\\BioWare\\Mass Effect Legendary Edition\\LauncherConfig.cfg", documentsPath);

		GLogger.writeln(L"parseLauncherConfig_: path ?= %s", launcherConfigPath);

		// Check that the file exists and is accessible.
		auto configAttrs = GetFileAttributesW(launcherConfigPath);
		if (INVALID_FILE_ATTRIBUTES == configAttrs)
		{
			GLogger.writeln(L"parseLauncherConfig_: file has wrong attributes, error code = %d // 2 = not found, 5 = access denied", GetLastError());

			// 19.07.21 - Allow running even without launcher config!
			needsConfigMade_ = true;
			return true;

			// MSGBOX: TRY RUNNING THE LAUNCHER / GAMES NORMALLY FOR ONCE
			// return false;
		}

		// Value reading variables.
		bool readEnglishVOEnabled = false, readLanguage = false, readSubtitleSize = false;
		wchar_t cfgEnglishVOEnabled[64], cfgLanguage[64], cfgSubtitlesSize[64];

		// Actually read the damn file.
		std::ifstream configFile{ launcherConfigPath };
		if (!configFile.is_open())
		{
			GLogger.writeln(L"parseLauncherConfig_: file should have been opened but wasn't :shrug:");
			return false;
		}
		std::string configLine;
		while (std::getline(configFile, configLine))
		{
			//GLogger.writeln(L"LINE: %S", configLine.c_str());

			if (1 == std::sscanf(configLine.c_str(), "EnglishVOEnabled=%64s", cfgEnglishVOEnabled))
			{
				readEnglishVOEnabled = true;
				GLogger.writeln(L"parseLauncherConfig_: read englishVoEnabled = %S", cfgEnglishVOEnabled);
			}
			else
				if (1 == std::sscanf(configLine.c_str(), "Language=%64s", cfgLanguage))
				{
					readLanguage = true;
					GLogger.writeln(L"parseLauncherConfig_: read language = %S", cfgLanguage);
				}
				else
					if (1 == std::sscanf(configLine.c_str(), "SubtitleSize=%64s", cfgSubtitlesSize))
					{
						readSubtitleSize = true;
						GLogger.writeln(L"parseLauncherConfig_: read subtitleSize = %S", cfgSubtitlesSize);
					}
		}
		configFile.close();

		// 20.07.21 - Hack in defaults for each of the three options!
		if (!readEnglishVOEnabled)
		{
			GLogger.writeln(L"parseLauncherConfig_: filling in englishVoEnabled with defaults!");
			wsprintf(cfgEnglishVOEnabled, L"false");
			readEnglishVOEnabled = true;
		}
		if (!readLanguage)
		{
			GLogger.writeln(L"parseLauncherConfig_: filling in language with defaults!");
			wsprintf(cfgLanguage, L"en_US");
			readLanguage = true;
		}
		if (!readSubtitleSize)
		{
			GLogger.writeln(L"parseLauncherConfig_: filling in subtitleSize with defaults!");
			wsprintf(cfgSubtitlesSize, L"20");
			readSubtitleSize = true;
		}

		// Check that all 3 needed values were read.
		// 20.07.21 - Not so necessary anymore!
		if (!(readEnglishVOEnabled && readLanguage && readSubtitleSize))
		{
			// MSGBOX: TRY RUNNING THE LAUNCHER / GAMES NORMALLY FOR ONCE
			GLogger.writeln(L"parseLauncherConfig_: not all of required config lines were found!");
			return false;
		}

		// Validate the found values.
		if (wcscmp(cfgEnglishVOEnabled, L"false")
			&& wcscmp(cfgEnglishVOEnabled, L"true"))
		{
			// MSGBOX: TRY RUNNING THE LAUNCHER / GAMES NORMALLY FOR ONCE
			GLogger.writeln(L"parseLauncherConfig_: key EnglishVOEnabled has an invalid value: %s!", cfgEnglishVOEnabled);
			return false;
		}
		if (wcscmp(cfgLanguage, L"en_US")
			&& wcscmp(cfgLanguage, L"fr_FR")
			&& wcscmp(cfgLanguage, L"de_DE")
			&& wcscmp(cfgLanguage, L"it_IT")
			&& wcscmp(cfgLanguage, L"ja_JP")
			&& wcscmp(cfgLanguage, L"es_ES")
			&& wcscmp(cfgLanguage, L"ru_RU")
			&& wcscmp(cfgLanguage, L"pl_PL"))
		{
			// MSGBOX: TRY RUNNING THE LAUNCHER / GAMES NORMALLY FOR ONCE
			GLogger.writeln(L"parseLauncherConfig_: key language has an invalid value: %s!", cfgLanguage);
			return false;
		}

		const std::map<std::wstring, VoiceOverDataPair> localizationCodesLE1
		{
			{ L"en_US", VoiceOverDataPair{ L"INT", L"INT" } },
			{ L"fr_FR", VoiceOverDataPair{ L"FR",  L"FE"  } },
			{ L"de_DE", VoiceOverDataPair{ L"DE",  L"GE"  } },
			{ L"it_IT", VoiceOverDataPair{ L"IT",  L"IE"  } },
			{ L"ja_JP", VoiceOverDataPair{ L"JA",  L"JA"  } },
			{ L"es_ES", VoiceOverDataPair{ L"ES",  L"ES"  } },
			{ L"ru_RU", VoiceOverDataPair{ L"RA",  L"RU"  } },
			{ L"pl_PL", VoiceOverDataPair{ L"PLPC", L"PL" } },
		};
		const std::map<std::wstring, VoiceOverDataPair> localizationCodesLE2
		{
			{ L"en_US", VoiceOverDataPair{ L"INT", L"INT" } },
			{ L"fr_FR", VoiceOverDataPair{ L"FRA", L"FRE" } },
			{ L"de_DE", VoiceOverDataPair{ L"DEU", L"DEE" } },
			{ L"it_IT", VoiceOverDataPair{ L"ITA", L"ITE" } },
			{ L"ja_JP", VoiceOverDataPair{ L"JPN", L"JPN" } },
			{ L"es_ES", VoiceOverDataPair{ L"ESN", L"ESN" } },
			{ L"ru_RU", VoiceOverDataPair{ L"RUS", L"RUS" } },
			{ L"pl_PL", VoiceOverDataPair{ L"POL", L"POE" } },
		};
		const std::map<std::wstring, VoiceOverDataPair> localizationCodesLE3
		{
			{ L"en_US", VoiceOverDataPair{ L"INT", L"INT" } },
			{ L"fr_FR", VoiceOverDataPair{ L"FRA", L"FRE" } },
			{ L"de_DE", VoiceOverDataPair{ L"DEU", L"DEE" } },
			{ L"it_IT", VoiceOverDataPair{ L"ITA", L"ITE" } },
			{ L"ja_JP", VoiceOverDataPair{ L"JPN", L"JPN" } },
			{ L"es_ES", VoiceOverDataPair{ L"ESN", L"ESN" } },
			{ L"ru_RU", VoiceOverDataPair{ L"RUS", L"RUS" } },
			{ L"pl_PL", VoiceOverDataPair{ L"POL", L"POL" } },
		};

		subtitlesSize = cfgSubtitlesSize;
		bool useInternationalVO = !wcscmp(cfgEnglishVOEnabled, L"true");


		// Extra params.

		wchar_t* cmdLineW = GetCommandLineW();
		wchar_t extraParams[1024];
		wsprintf(extraParams, L"%s", cmdLineW);

		// End extra params parsing.


		switch (this->launchTarget_)
		{
		case LEGameVersion::LE1:
		{
			const auto& pair = localizationCodesLE1.at(cfgLanguage);
			overrideLang = (useInternationalVO ? pair.InternationalVO : pair.LocalVO).c_str();

			wsprintf(this->cmdArgsBuffer_,
				L" -NoHomeDir -SeekFreeLoadingPCConsole -locale {locale} -Subtitles %s -OVERRIDELANGUAGE=%s %s",
				subtitlesSize.c_str(), overrideLang.c_str(), extraParams);

			break;
		}
		case LEGameVersion::LE2:
		{
			const auto& pair = localizationCodesLE2.at(cfgLanguage);
			overrideLang = (useInternationalVO ? pair.InternationalVO : pair.LocalVO).c_str();

			wsprintf(this->cmdArgsBuffer_,
				L" -NoHomeDir -SeekFreeLoadingPCConsole -locale {locale} -Subtitles %s -OVERRIDELANGUAGE=%s %s",
				subtitlesSize.c_str(), overrideLang.c_str(), extraParams);

			break;
		}
		case LEGameVersion::LE3:
		{
			const auto& pair = localizationCodesLE3.at(cfgLanguage);
			overrideLang = (useInternationalVO ? pair.InternationalVO : pair.LocalVO).c_str();

			wsprintf(this->cmdArgsBuffer_,
				L" -NoHomeDir -SeekFreeLoadingPCConsole -locale {locale} -Subtitles %s -language=%s  %s",
				subtitlesSize.c_str(), overrideLang.c_str(), extraParams);

			break;
		}
		}

		needsConfigMade_ = false;
		return true;
	}
	bool overrideForDebug_()
	{
		if (0 != GetEnvironmentVariableW(L"LEBINK_DEBUGGER", debuggerPath_, 512))
		{
			GLogger.writeln(L"overrideForDebug_ env. detected, appplying the override...");

			wchar_t prependBuffer[2048];
			wsprintf(prependBuffer, L"%s %s", launchParams_.GameExePath, launchParams_.GameCmdLine);
			wsprintf(cmdArgsBuffer_, L"%s", prependBuffer);

			launchParams_.GameCmdLine = static_cast<wchar_t*>(cmdArgsBuffer_);
			launchParams_.GameExePath = static_cast<wchar_t*>(debuggerPath_);

			return true;
		}

		return false;
	}

	const wchar_t* gameToPath_(LEGameVersion version) const noexcept
	{
		switch (version)
		{
		case LEGameVersion::LE1: return L"../ME1/Binaries/Win64/MassEffect1.exe";
		case LEGameVersion::LE2: return L"../ME2/Binaries/Win64/MassEffect2.exe";
		case LEGameVersion::LE3: return L"../ME3/Binaries/Win64/MassEffect3.exe";
		default:
			return nullptr;
		}
	}
	const wchar_t* gameToWorkingDir_(LEGameVersion version) const noexcept
	{
		switch (version)
		{
		case LEGameVersion::LE1: return L"../ME1/Binaries/Win64";
		case LEGameVersion::LE2: return L"../ME2/Binaries/Win64";
		case LEGameVersion::LE3: return L"../ME3/Binaries/Win64";
		default:
			return nullptr;
		}
	}

public:

	// IModule interface.

	LauncherArgsModule()
		: IModule{ "LauncherArgs" }
	{
		ZeroMemory(cmdArgsBuffer_, 2048);
	}

	bool Activate() override
	{
		// If we don't have parameters already for language and subtitle size in the command line options set
		// Get options from command line.
		if (!this->parseCmdLine_(GLEBinkProxy.CmdLine))
		{
			GLogger.writeln(L"LauncherArgsModule.Activate: failed to parse cmd line, aborting...");
			// TODO: MessageBox here maybe?
			return false;
		}

		// Return normally if we don't need to do anything.
		if (this->launchTarget_ == LEGameVersion::Unsupported)
		{
			GLogger.writeln(L"LauncherArgsModule.Activate: options not detected, aborting (not a failure)...");
			return true;
		}

		// Report failure if we can't do wonders because of an incorrect arg.
		// This is a redundant check and it's okay.
		if (this->launchTarget_ != LEGameVersion::LE1
			&& this->launchTarget_ != LEGameVersion::LE2
			&& this->launchTarget_ != LEGameVersion::LE3)
		{
			GLogger.writeln(L"LauncherArgsModule.Activate: options detected but invalid launch target, aborting...");
			return false;
		}

		GLogger.writeln(L"LauncherArgsModule.Activate: valid autoboot target detected: Mass Effect %d",
			static_cast<int>(this->launchTarget_));

		// Pre-parse the launcher config file.
		if (this->needsLauncherConfigParsed_ && !this->parseLauncherConfig_())
		{
			GLogger.writeln(L"LauncherArgsModule.Activate: failed to parse Launcher config, aborting...");
			// TODO: MessageBox here maybe?
			return false;
		}

		// Bail out without an error if the config file doesn't exist - is this the first launch?
		if (!this->needsLauncherConfigParsed_ || this->needsConfigMade_)
		{
			if (this->needsLauncherConfigParsed_) {
				GLogger.writeln(L"LauncherArgsModule.Activate: launcher config file is missing or inaccessible, using defaults...");
				// TODO: MessageBox here maybe?
			}

			//char* extraParams = GetCommandLineA();

			switch (this->launchTarget_)
			{
			case LEGameVersion::LE1:
				wsprintf(this->cmdArgsBuffer_, L" -NoHomeDir -SeekFreeLoadingPCConsole -locale {locale} -Subtitles %ls -OVERRIDELANGUAGE=%ls  %ls", this->subtitleSize_, this->bootLanguage_, GLEBinkProxy.CmdLine);
				break;
			case LEGameVersion::LE2:
				wsprintf(this->cmdArgsBuffer_, L" -NoHomeDir -SeekFreeLoadingPCConsole -locale {locale} -Subtitles %ls -OVERRIDELANGUAGE=%ls  %ls", this->subtitleSize_, this->bootLanguage_, GLEBinkProxy.CmdLine);
				break;
			case LEGameVersion::LE3:
				wsprintf(this->cmdArgsBuffer_, L" -NoHomeDir -SeekFreeLoadingPCConsole -locale {locale} -Subtitles %ls -language=%ls  %ls", this->subtitleSize_, this->bootLanguage_, GLEBinkProxy.CmdLine);
				break;
			}
		}

		// Set up everything for the process start.
		launchParams_.Target = this->launchTarget_;
		launchParams_.AutoTerminate = this->autoTerminate_;
		launchParams_.GameExePath = const_cast<wchar_t*>(gameToPath_(this->launchTarget_));
		launchParams_.GameCmdLine = const_cast<wchar_t*>(this->cmdArgsBuffer_);
		launchParams_.GameWorkDir = const_cast<wchar_t*>(gameToWorkingDir_(this->launchTarget_));

		// Allow debugging functionality.
		this->overrideForDebug_();

		// Start the process in a new thread.
		auto rc = CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)LaunchGameThread, &(this->launchParams_), 0, nullptr);
		if (rc == nullptr)
		{
			GLogger.writeln(L"LauncherArgsModule.Activate: failed to create a thread (error code = %d)", GetLastError());
			// TODO: MessageBox here maybe?
			return false;
		}

		return true;
	}

	void Deactivate() override
	{
		return;
	}

private:

	void ClipOutSubString(wchar_t* string, int startPos, int lenToClip)
	{
		// Find end of string so we know how much data needs shifted.
		int stringEndPos = 0;
		while (string[stringEndPos] != '\0')
		{
			stringEndPos++;
		}

		stringEndPos -= lenToClip - 1; // This is the amount shifted so it will not need copied.

		// Shift all characters to the left by the len to clip.
		for (int i = startPos; i < stringEndPos; i++)
		{
			auto charToReplace = string[i];
			auto charToMove = string[i + lenToClip];
			string[i] = string[i + lenToClip];
		}

		std::cout << string;
	}

	// End of IModule interface.
};
