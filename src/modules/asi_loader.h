#pragma once

#include <vector>
#include <string>
#include <algorithm>
#include <mutex>
#include <utility>
#include <wchar.h>
#include <Windows.h>
#include "../utils/io.h"
#include "_base.h"
#include "../spi/interface.h"


typedef void(* AsiSpiSupportType)(wchar_t** name, wchar_t** author, wchar_t** version, int* gameIndex, int* spiMinVersion);
typedef bool(* AsiSpiShouldPreloadType)(void);
typedef bool(* AsiSpiShouldSpawnThreadType)(void);
typedef bool(* AsiOnAttachType)(ISharedProxyInterface* InterfacePtr);
typedef bool(* AsiOnDetachType)(ISharedProxyInterface* InterfacePtr);

struct AsiPluginLoadInfo;

struct AsiAsyncDispatchInfo
{
    ISharedProxyInterface* InterfacePtr;
    AsiOnAttachType FunctionPtr;
    AsiPluginLoadInfo* LoadInfo;
    std::mutex* CompletionMutex;
    std::vector<AsiPluginLoadInfo*>* SuccessfulAttachments;
    bool AttachSucceeded;
};
DWORD WINAPI AsiAsyncDispatchThread(LPVOID lpParameter)
{
    auto infoPtr = reinterpret_cast<AsiAsyncDispatchInfo*>(lpParameter);
    infoPtr->AttachSucceeded = infoPtr->FunctionPtr(infoPtr->InterfacePtr);
    if (infoPtr->AttachSucceeded)
    {
        // This is deliberately recorded by the worker at callback completion,
        // not by the shutdown thread. The mutex makes the ledger safe while
        // multiple async SPI callbacks finish concurrently, and the append
        // order is their actual completion order.
        std::lock_guard<std::mutex> lock{ *infoPtr->CompletionMutex };
        infoPtr->SuccessfulAttachments->push_back(infoPtr->LoadInfo);
    }
    return 0;
}


struct AsiPluginLoadInfo
{
private:
    bool shouldPreloadFetched_;
    bool shouldPreloadResolved_;
    bool shouldSpawnThreadFetched_;
    bool shouldSpawnThreadResolved_;
    bool allSpiProcsLoaded_;

public:
    std::wstring FileName;
    HINSTANCE LibInstance;

    AsiSpiSupportType SpiSupport;
    AsiSpiShouldPreloadType DoPreload;
    AsiSpiShouldSpawnThreadType DoSpawnThread;
    AsiOnAttachType OnAttach;
    AsiOnDetachType OnDetach;

    wchar_t* PluginName;
    wchar_t* PluginVersion;
    wchar_t* PluginAuthor;
    int SupportedGamesBitset;
    int MinInterfaceVersion;
    bool IsAsyncAttachMode;
    bool AttachDispatched;
    bool AttachSucceeded;

    AsiPluginLoadInfo(const std::wstring& fileName, HINSTANCE libInstance)
        : FileName{ fileName }
        , LibInstance{ libInstance }
        , PluginName{ nullptr }
        , PluginVersion{ nullptr }
        , PluginAuthor{ nullptr }
        , SupportedGamesBitset{ 0 }
        , MinInterfaceVersion { 0 }
        , IsAsyncAttachMode { false }
        , AttachDispatched{ false }
        , AttachSucceeded{ false }
        , SpiSupport{ nullptr }
        , DoPreload{ nullptr }
        , DoSpawnThread{ nullptr }
        , OnAttach{ nullptr }
        , OnDetach{ nullptr }
        , shouldPreloadFetched_{ false }
        , shouldPreloadResolved_{ false }
        , shouldSpawnThreadFetched_{ false }
        , shouldSpawnThreadResolved_{ false }
        , allSpiProcsLoaded_{ true }  // set to false on first error
    {

    }

    __forceinline void MissingProc() { allSpiProcsLoaded_ = false; }

    [[nodiscard]] __forceinline bool SupportsSPI() const noexcept { return SpiSupport != nullptr && allSpiProcsLoaded_; }
    [[nodiscard]] __forceinline bool ShouldPreload()
    {
        if (!SupportsSPI())
        {
            return false;
        }

        if (!shouldPreloadResolved_ && DoPreload)
        {
            shouldPreloadFetched_ = DoPreload();
            shouldPreloadResolved_ = true;
        }

        if (!shouldPreloadResolved_)
        {
            GLogger.writeln(L"ShouldPreload: fell through the call check, most likely DoPreload was NULL");
            return false;
        }

        return shouldPreloadFetched_;
    }
    [[nodiscard]] __forceinline bool ShouldPostload()
    {
        if (!SupportsSPI())
        {
            return false;
        }

        return !ShouldPreload();
    }
    [[nodiscard]] __forceinline bool ShouldSpawnThread()
    {
        if (!SupportsSPI())
        {
            return false;
        }

        if (!shouldSpawnThreadResolved_ && DoSpawnThread)
        {
            shouldSpawnThreadFetched_ = DoSpawnThread();
            shouldSpawnThreadResolved_ = true;
        }

        if (!shouldSpawnThreadResolved_)
        {
            GLogger.writeln(L"ShouldPostload: fell through the call check, most likely DoSpawnThread was NULL");
            return false;
        }

        return shouldSpawnThreadFetched_;
    }

    [[nodiscard]] bool HasCorrectVersionFor(int proxyVer) const noexcept { return !(MinInterfaceVersion < 2 || MinInterfaceVersion > proxyVer); }
    [[nodiscard]] bool HasCorrectFlagFor(LEGameVersion gameVer) const
    {
        switch (gameVer)
        {
        case LEGameVersion::Launcher:
            if (SupportedGamesBitset & SPI_GAME_LEL) return true;
            return false;
        case LEGameVersion::LE1:
            if (SupportedGamesBitset & SPI_GAME_LE1) return true;
            return false;
        case LEGameVersion::LE2:
            if (SupportedGamesBitset & SPI_GAME_LE2) return true;
            return false;
        case LEGameVersion::LE3:
            if (SupportedGamesBitset & SPI_GAME_LE3) return true;
            return false;
        default:
            return false;
        }
    }

    void LoadConditionalProcs()
    {
        DoPreload = (AsiSpiShouldPreloadType)GetProcAddress(LibInstance, "SpiShouldPreload");
        if (DoPreload == NULL)
        {
            allSpiProcsLoaded_ = false;
            GLogger.writeln(L"LoadConditionalProcs: failed to find SpiShouldPreload (last error = %d)", GetLastError());
        }

        DoSpawnThread = (AsiSpiShouldSpawnThreadType)GetProcAddress(LibInstance, "SpiShouldSpawnThread");
        if (DoSpawnThread == NULL)
        {
            allSpiProcsLoaded_ = false;
            GLogger.writeln(L"LoadConditionalProcs: failed to find SpiShouldSpawnThread (last error = %d)", GetLastError());
        }

        OnAttach = (AsiOnAttachType)GetProcAddress(LibInstance, "SpiOnAttach");
        if (OnAttach == NULL)
        {
            allSpiProcsLoaded_ = false;
            GLogger.writeln(L"LoadConditionalProcs: failed to find SpiOnAttach (last error = %d)", GetLastError());
        }

        OnDetach = (AsiOnDetachType)GetProcAddress(LibInstance, "SpiOnDetach");
        if (OnDetach == NULL)
        {
            allSpiProcsLoaded_ = false;
            GLogger.writeln(L"LoadConditionalProcs: failed to find SpiOnDetach (last error = %d)", GetLastError());
        }
    }
};
typedef std::vector<AsiPluginLoadInfo> AsiInfoList;


class AsiLoaderModule
    : public IModule
{
private:
    typedef wchar_t* wstr;
    typedef const wchar_t* wcstr;

    // Config parameters.

    static const int MAX_FILES = 128;       // Max. number of ASI plugins in the directory.
    static const bool TRY_LOAD_ALL = true;  // Attempt to load further ASIs after an error on loading one.

    // Fields.

    std::vector<std::wstring> fileNames_;
    wchar_t asiRoot_[512];
    AsiInfoList pluginLoadInfos_;
    std::vector<std::pair<HANDLE, AsiAsyncDispatchInfo*>> asyncAttachThreads_;
    std::vector<AsiPluginLoadInfo*> successfulAttachments_;
    std::mutex completionMutex_;
    DWORD lastErrorCode_ = 0;
	bool activatedPreloads_ = false;
	bool activatedPostloads_ = false;
	bool deactivated_ = false;

    // Methods.

    bool makeAsiRoot_()
    {
        wcscpy_s(asiRoot_, 512, GLEBinkProxy.ExePath);

        for (size_t i = wcsnlen_s(asiRoot_, 512) - 5; i > 0; i--)
        {
            if (asiRoot_[i] == L'\\' || asiRoot_[i] == L'/')
            {
                asiRoot_[i] = L'\0';
                wcscpy_s(&asiRoot_[i], 5, L"\\ASI");
                return true;
            }
        }

        return false;
    }

    bool directoryExists_() const
    {
        GLogger.writeln(L"directoryExists_: entered. Searching %s...", asiRoot_);
        auto attributes = GetFileAttributesW(asiRoot_);
        if (attributes == INVALID_FILE_ATTRIBUTES)
        {
            GLogger.writeln(L"directoryExists_: false");
            return false;
        }
        GLogger.writeln(L"directoryExists_: %S", (static_cast<bool>(attributes & FILE_ATTRIBUTE_DIRECTORY) ? "true" : "false"));
        return static_cast<bool>(attributes & FILE_ATTRIBUTE_DIRECTORY);
    }

    bool findPluginFiles_()
    {
        wchar_t pattern[512];
        swprintf_s(pattern, 512, L"%s\\*.asi", asiRoot_);

        WIN32_FIND_DATA fd;
        HANDLE findHandle = ::FindFirstFile(pattern, &fd);
        if (findHandle == INVALID_HANDLE_VALUE)
        {
            return (this->lastErrorCode_ = GetLastError()) == ERROR_FILE_NOT_FOUND;  // It's not a true error if we can't find anything.
        }

        do
        {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            {
                this->fileNames_.push_back(fd.cFileName);
                GLogger.writeln(L"Found ASI: %s", fd.cFileName);
            }
        } while (::FindNextFile(findHandle, &fd));
        ::FindClose(findHandle);

        return true;
    }

    bool registerLoadInfo_(HINSTANCE dllModuleInstance, const std::wstring& fileName)
    {
        AsiPluginLoadInfo loadInfo{ fileName, dllModuleInstance };

        loadInfo.SpiSupport = (AsiSpiSupportType)GetProcAddress(dllModuleInstance, "SpiSupportDecl");
        if (loadInfo.SpiSupport == NULL)
        {
            loadInfo.MissingProc();
            GLogger.writeln(L"registerLoadInfo_: failed to find SpiSupportDecl (last error = %d). Likely, SPI is just not supported.", GetLastError());
            // not an error
        }

        // If the plugins declares SPI support, attempt to load other required procedures.
        if (loadInfo.SupportsSPI())
        {
            loadInfo.LoadConditionalProcs();
            if (!loadInfo.SupportsSPI())
            {
                GLogger.writeln(L"registerLoadInfo_: SpiSupportDecl was found but some procs are missing:");
                GLogger.writeln(L"registerLoadInfo_: SpiSupportDecl = %p, SpiShouldPreload = %p, SpiOnAttach = %p, SpiOnDetach = %p",
                    loadInfo.SpiSupport, loadInfo.DoPreload, loadInfo.OnAttach, loadInfo.OnDetach);
                return false;
            }
            GLogger.writeln(L"registerLoadInfo_: all SPI procs were found!");

            // Get SPI-required info from the plugin via SpiSupportDecl.
            loadInfo.SpiSupport(&loadInfo.PluginName, &loadInfo.PluginAuthor, &loadInfo.PluginVersion, &loadInfo.SupportedGamesBitset, &loadInfo.MinInterfaceVersion);
            GLogger.writeln(L"registerLoadInfo_: provided info: '%s' (ver %s) by '%s', supported games (bitset) = %d, min ver = %d",
                loadInfo.PluginName, loadInfo.PluginVersion, loadInfo.PluginAuthor, loadInfo.SupportedGamesBitset, loadInfo.MinInterfaceVersion);

            // Ensure that the plugin's declared min SPI version is valid and less or equal to our version.
            if (!loadInfo.HasCorrectVersionFor(ASI_SPI_VERSION))
            {
                GLogger.writeln(L"registerLoadInfo_: filtering out because the min version is higher than the build's one (%d)!", loadInfo.MinInterfaceVersion);
                return false;
            }

            // Ensure that the plugin's declared game targets match the game we (the proxy) are attached to.
            if (!loadInfo.HasCorrectFlagFor(GLEBinkProxy.Game))
            {
                GLogger.writeln(L"registerLoadInfo_: filtering out because the plugin was not designed for this game (need to have %d)!", GLEBinkProxy.Game);
                return false;
            }
        }

        pluginLoadInfos_.push_back(loadInfo);
        return true;
    }

    bool dispatchAttach_(ISharedProxyInterface* interfacePtr, AsiPluginLoadInfo* loadInfo)
    {
        if (!loadInfo || !interfacePtr)
        {
            GLogger.writeln(L"dispatchAttach_: ERROR: one or both parameters were NULL");
            return false;
        }

        loadInfo->IsAsyncAttachMode = loadInfo->ShouldSpawnThread();
        GLogger.writeln(L"dispatchAttach_: got IsAsyncAttachMode (= %d)", loadInfo->IsAsyncAttachMode);
        loadInfo->AttachDispatched = true;

        if (!loadInfo->IsAsyncAttachMode)  // seq
        {
            loadInfo->AttachSucceeded = loadInfo->OnAttach(interfacePtr);
            if (loadInfo->AttachSucceeded)
            {
                std::lock_guard<std::mutex> lock{ completionMutex_ };
                successfulAttachments_.push_back(loadInfo);
            }
            return loadInfo->AttachSucceeded;
        }
        else if (loadInfo->IsAsyncAttachMode)  // async
        {
            auto dispatchInfo = new AsiAsyncDispatchInfo{
                interfacePtr,
                loadInfo->OnAttach,
                loadInfo,
                &completionMutex_,
                &successfulAttachments_,
                false
            };
            HANDLE const threadHandle = CreateThread(nullptr, 0,
                AsiAsyncDispatchThread, dispatchInfo, 0, nullptr);
            if (threadHandle == NULL)
            {
                GLogger.writeln(L"dispatchAttach_: ERROR: CreateThread failed, error code = %d", GetLastError());
                delete dispatchInfo;
                loadInfo->AttachDispatched = false;
                return false;
            }
            try
            {
                asyncAttachThreads_.emplace_back(threadHandle, dispatchInfo);
            }
            catch (...)
            {
                // The worker owns no independent lifetime. If retaining its
                // handle fails, join it immediately before releasing its
                // dispatch record so the callback cannot access freed state.
                GLogger.writeln(L"dispatchAttach_: ERROR: failed to retain async worker handle.");
                WaitForSingleObject(threadHandle, INFINITE);
                CloseHandle(threadHandle);
                delete dispatchInfo;
                loadInfo->AttachDispatched = false;
                return false;
            }
            return true;
        }
        
        return false;
    }

    void waitForAsyncAttaches_()
    {
        // Called by Deactivate from the explicit shutdown route, after the
        // post-DRM manager has been joined and immediately before reverse
        // detach. PostLoad intentionally never calls this helper.
        for (auto const& [threadHandle, dispatchInfo] : asyncAttachThreads_)
        {
            DWORD const waitResult = WaitForSingleObject(threadHandle, INFINITE);
            if (waitResult != WAIT_OBJECT_0)
            {
                GLogger.writeln(L"AsiLoaderModule: ERROR: waiting for async worker failed (code = %d).", GetLastError());
            }
            dispatchInfo->LoadInfo->AttachSucceeded = dispatchInfo->AttachSucceeded;
            CloseHandle(threadHandle);
            delete dispatchInfo;
        }
        asyncAttachThreads_.clear();
    }

    // Comparison function for std::sort
    // Returns true if s1 is lexicographically less than s2
    bool compare_wstrings(const wchar_t* s1, const wchar_t* s2) {
        return wcscmp(s1, s2) < 0;
    }

public:
    AsiLoaderModule()
        : IModule{ "AsiLoader" }
        , pluginLoadInfos_{}
    {
        active_ = true;
    }

    bool Activate() override
    {
        this->fileNames_.clear();
        this->fileNames_.reserve(MAX_FILES);
        this->asyncAttachThreads_.reserve(MAX_FILES);
        this->successfulAttachments_.reserve(MAX_FILES);

        // Build the root path for ASI plugins.
        if (!makeAsiRoot_())
        {
            GLogger.writeln(L"AsiLoaderModule.Activate: aborting after makeAsiRoot_ (error code = %d).", this->lastErrorCode_);
            return false;
        }

        // Bail out early if the directory doesn't even exist.
        if (!this->directoryExists_())
        {
            return true;
        }

        // Report an error if the file search fails (no matches is not a failure).
        if (!this->findPluginFiles_())
        {
            GLogger.writeln(L"AsiLoaderModule.Activate: aborting after findPluginFiles_ (error code = %d).", this->lastErrorCode_);
            return false;
        }

        // Sort the fileNames_ vector alphabetically (case-insensitive)
        std::sort(this->fileNames_.begin(), this->fileNames_.end(), [](const std::wstring& a, const std::wstring& b) {
            return _wcsicmp(a.c_str(), b.c_str()) < 0;
            });

        // For each found files, load the library and register SPI info.
        wchar_t fileNameBuffer[MAX_PATH];
        HINSTANCE lastModule = nullptr;
        for (int f = 0; f < this->fileNames_.size(); f++)
        {
            swprintf_s(fileNameBuffer, MAX_PATH, L"ASI/%s", this->fileNames_[f].c_str());

            GLogger.writeln(L"AsiLoaderModule.Activate: loading %s...", this->fileNames_[f].c_str());

            // Load the DLL file.
            // DllMain() code will be executed here, SPI will be executed later, from dllmain.cpp:OnAttach().
            if (NULL == (lastModule = LoadLibraryW(fileNameBuffer)))
            {
                GLogger.writeln(L"AsiLoaderModule.Activate:   failed with error code = %d.", (this->lastErrorCode_ = GetLastError()));
                if (TRY_LOAD_ALL) continue;
                return false;
            }

            // Get SPI info from the plugins.
            // This will populate pluginLoadInfos_ with data needed for executing plugins' attach points.
            if (!this->registerLoadInfo_(lastModule, this->fileNames_[f]))
            {
                GLogger.writeln(L"AsiLoaderModule.Activate:   registerLoadInfo_ failed.");
                if (TRY_LOAD_ALL) continue;
                return false;
            }

            // DEBUG THING
            GLogger.writeln(L"AsiLoaderModule.Activate:   successfully registered the load info.");
            // END DEBUG THING
        }

        return true;
    }
    void Deactivate() override
    {
        if (deactivated_)
            return;

        waitForAsyncAttaches_();

        // Detach only ASIs whose attach callback actually returned success. The
        // ledger reflects the real preload/postload completion order rather than
        // the registration order of the DLL directory.
        GLogger.writeln(L"AsiLoaderModule.Deactivate: %zu successful attachment(s):", successfulAttachments_.size());
        for (auto Iter = successfulAttachments_.rbegin(); Iter != successfulAttachments_.rend(); ++Iter)
        {
            auto& loadInfo = **Iter;
            GLogger.writeln(L"AsiLoaderModule.Deactivate:   - [%p] {%s} %s",
                loadInfo.LibInstance, (loadInfo.SupportsSPI() ? L"SPI" : L"RAW"), loadInfo.FileName.c_str());

            if (loadInfo.SupportsSPI() && !loadInfo.OnDetach(GLEBinkProxy.SPI))
            {
                GLogger.writeln(L"AsiLoaderModule.Deactivate:   ERROR: detach reported a failure, continuing...");
            }
        }

        successfulAttachments_.clear();
        deactivated_ = true;
    }

    bool PreLoad(ISharedProxyInterface* interfacePtr)
    {
		if (activatedPreloads_)
		{
			GLogger.writeln(L"PreLoad: already activated, skipping...");
			return true;
		}
        activatedPreloads_ = true;
        for (auto& loadInfo : pluginLoadInfos_)
        {
            if (loadInfo.ShouldPreload())
            {
                if (!dispatchAttach_(interfacePtr, &loadInfo))
                {
                    GLogger.writeln(L"PreLoad: OnAttach dispatch returned an error [%s]", loadInfo.FileName.c_str());
                    continue;
                }
                GLogger.writeln(L"PreLoad: OnAttach dispatch succeeded [%s] (mode = %d)", loadInfo.FileName.c_str(), loadInfo.IsAsyncAttachMode);
            }
        }

        return true;
    }
    bool PostLoad(ISharedProxyInterface* interfacePtr)
    {
        if (activatedPostloads_)
        {
            GLogger.writeln(L"PostLoad: already activated, skipping...");
            return true;
        }
        activatedPostloads_ = true;

        for (auto& loadInfo : pluginLoadInfos_)
        {
            if (loadInfo.ShouldPostload())
            {
                if (!dispatchAttach_(interfacePtr, &loadInfo))
                {
                    GLogger.writeln(L"PostLoad: OnAttach dispatch returned an error [%s]", loadInfo.FileName.c_str());
                    continue;
                }
                GLogger.writeln(L"PostLoad: OnAttach dispatch succeeded [%s] (mode = %d)", loadInfo.FileName.c_str(), loadInfo.IsAsyncAttachMode);
            }
        }

        return true;
    }
};
