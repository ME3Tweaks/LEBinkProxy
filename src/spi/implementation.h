#pragma once

#include <cstring>
#include <intrin.h>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <unordered_map>
#include <Windows.h>
#include "../conf/version.h"
#include "../utils/io.h"
#include "../utils/hook.h"
#include "../utils/classutils.h"
#include "../utils/memory.h"
#include "../utils/console_broker.h"
#include "../dllstruct.h"
#include "../spi/shared_hook_manager.h"
#include "../spi/interface.h"


#define SPI_IMPL_INSTANCE_LOCK(MUTEX) const std::lock_guard<std::mutex> lock(this->MUTEX);


namespace SPI
{

    // Concrete implementation of ISharedProxyInterface.

    class SharedProxyInterface
        : public ISharedProxyInterface
        , public NonCopyMovable
    {
        // Implementation details.

        DWORD version_;
        BOOL isRelease_;

        SharedHookManager hookMngr_;
        ConsoleBroker consoleBroker_;

        std::mutex mtxGetVersion_;
        std::mutex mtxGetBuildMode_;
        std::mutex mtxGetHostGame_;
        std::mutex mtxFindPattern_;
        std::mutex mtxInstallHook_;

        struct ServiceEntry
        {
            std::uint32_t Version;
            const void* Table;
            std::wstring Provider;
        };

        std::mutex mtxServices_;
        std::unordered_map<std::string, ServiceEntry> services_;

        // Implementation methods.

        __forceinline DWORD getVersion_() const noexcept { return version_; }
        __forceinline bool getReleaseMode_() const noexcept { return isRelease_; }

        static bool isValidServiceName_(const char* name) noexcept
        {
            if (!name)
            {
                return false;
            }

            size_t const len = strnlen(name, SPI_SERVICE_NAME_MAX + 1);
            if (len == 0 || len > SPI_SERVICE_NAME_MAX)
            {
                return false;
            }

            for (size_t i = 0; i < len; i++)
            {
                if (name[i] < 0x21 || name[i] > 0x7E)
                {
                    return false;
                }
            }
            return true;
        }

        // File name of the module containing the address, for logging which plugin made a call.
        static std::wstring describeModuleAt_(void* address)
        {
            HMODULE module = nullptr;
            wchar_t path[MAX_PATH];
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    static_cast<LPCWSTR>(address), &module)
                || 0 == GetModuleFileNameW(module, path, MAX_PATH))
            {
                return L"<unknown module>";
            }

            wchar_t const* const fileName = wcsrchr(path, L'\\');
            return fileName ? fileName + 1 : path;
        }

        __declspec(noinline) bool parseCombinedPattern_(char* inPattern, BYTE* outPatternBuffer, BYTE* outMaskBuffer, size_t* outLength)
        {
            auto len = inPattern ? std::strlen(inPattern) : 0;
            if (len < 6)
            {
                return false;
            }

            char* token = nullptr;
            char* endPtr = nullptr;
            long byteValue = 0;
            size_t parsedLength = 0;
            size_t patternLength = 0;

            do
            {
                token = std::strtok(parsedLength == 0 ? inPattern : nullptr, " ");
                if (token)
                {
                    parsedLength = token + 2 - inPattern;

                    //GLogger.writeln(L"parseCombinedPattern_: token = %S, parsedLength = %llu, len = %llu, strlen(token) = %llu", token, parsedLength, len, strlen(token));
                    
                    if (strlen(token) != 2)
                    {
                        GLogger.writeln(L"parseCombinedPattern_: ERROR: parsed token was not 2 chars long: %S", token);
                        return false;
                    }

                    if (0 == strcmp(token, "??"))
                    {
                        outPatternBuffer[patternLength] = 0u;
                        outMaskBuffer[patternLength] = '?';
                        ++patternLength;
                    }
                    else
                    {
                        errno = 0;
                        if (0 == (byteValue = std::strtol(token, &endPtr, 0x10)) && errno != 0)
                        {
                            GLogger.writeln(L"parseCombinedPattern_: ERROR: strtol returned an error (errno = %d)", errno);
                            return false;
                        }
                        else
                        {
                            auto foo = (*outPatternBuffer);
                            outPatternBuffer[patternLength] = static_cast<BYTE>(byteValue);
                            outMaskBuffer[patternLength] = 'x';
                            ++patternLength;
                        }
                    }
                }
                else
                {
                    if (parsedLength != len)
                    {
                        GLogger.writeln(L"parseCombinedPattern_: ERROR: token was null but inPattern end wasn't reached (%d != %d)", parsedLength, len);
                        return false;
                    }
                }
            } while (token);

            *outLength = patternLength;
            return true;
        }

    public:
        SharedProxyInterface()
            : NonCopyMovable()
            , version_{ ASI_SPI_VERSION }
            , isRelease_{ false }
            , hookMngr_{ }
        {
#ifndef ASI_DEBUG
            isRelease_ = true;
#endif
        }

        // ISharedProxyInterface implementation.

        SPIDEFN GetVersion(unsigned long* outVersionPtr)
        {
            SPI_IMPL_INSTANCE_LOCK(mtxGetVersion_);

            if (!outVersionPtr)
            {
                return SPIReturn::FailureInvalidParam;
            }

            *outVersionPtr = this->getVersion_();
            return SPIReturn::Success;
        }

        SPIDEFN GetBuildMode(bool* outIsRelease)
        {
            SPI_IMPL_INSTANCE_LOCK(mtxGetBuildMode_);

            if (!outIsRelease)
            {
                return SPIReturn::FailureInvalidParam;
            }

            *outIsRelease = this->getReleaseMode_();
            return SPIReturn::Success;
        }

        SPIDEFN GetHostGame(SPIGameVersion* outGameVersion)
        {
            SPI_IMPL_INSTANCE_LOCK(mtxGetHostGame_);

            if (!outGameVersion)
            {
                return SPIReturn::FailureInvalidParam;
            }

            *outGameVersion = static_cast<SPIGameVersion>(GLEBinkProxy.Game);
            return SPIReturn::Success;
        }

        SPIDEFN FindPattern(void** outOffsetPtr, char* combinedPattern)
        {
            SPI_IMPL_INSTANCE_LOCK(mtxFindPattern_);

            if (!outOffsetPtr || !combinedPattern)
            {
                return SPIReturn::FailureInvalidParam;
            }
            if (strlen(combinedPattern) > 300)
            {
                return SPIReturn::FailurePatternTooLong;
            }


            // Unpack the combined pattern into pattern and a mask.

            auto inPatternCopy = _strdup(combinedPattern);

            BYTE patternBytes[100];
            BYTE maskBytes[100];
            size_t patternLength = 0;

            ZeroMemory(patternBytes, 100);
            ZeroMemory(maskBytes, 100);
            
            if (!this->parseCombinedPattern_(inPatternCopy, (BYTE*)patternBytes, (BYTE*)maskBytes, &patternLength))
            {
                return SPIReturn::FailurePatternInvalid;
            }

            //GLogger.writeln(L"Pattern length = %llu", patternLength);
            //for (size_t i = 0; i < patternLength; i++) printf(" %02x", patternBytes[i]);  printf("\n\n");
            //for (size_t i = 0; i < patternLength; i++) printf(" %02x", maskBytes[i]);     printf("\n\n");

            free(inPatternCopy);

            // Use the built-in memory scanner.

            auto offset = Utils::ScanProcess(patternBytes, maskBytes);
            if (!offset)
            {
                *outOffsetPtr = nullptr;
                return SPIReturn::FailureGeneric;
            }

            *outOffsetPtr = offset;
            return SPIReturn::Success;
        }

        SPIDEFN InstallHook(const char* name, void* target, void* detour, void** original)
        {
            SPI_IMPL_INSTANCE_LOCK(mtxInstallHook_);

            if (!name || !target || !detour || !original)
            {
                return SPIReturn::FailureInvalidParam;
            }

            if (hookMngr_.HookExists(const_cast<char*>(name)))
            {
                GLogger.writeln(L"Failed to install the hook [%S] because it already exists", name);
                return SPIReturn::FailureDuplicacy;
            }

            if (!hookMngr_.Install(target, detour, original, const_cast<char*>(name)))
            {
                GLogger.writeln(L"Failed to install the hook [%S]", name);
                return SPIReturn::FailureHooking;
            }

            return SPIReturn::Success;
        }

        SPIDEFN InstallHookDeferred(const char* name, void* target, void* detour, void** original)
        {
            SPI_IMPL_INSTANCE_LOCK(mtxInstallHook_);

            if (!name || !target || !detour || !original)
            {
                return SPIReturn::FailureInvalidParam;
            }

            if (hookMngr_.HookExists(const_cast<char*>(name)))
            {
                GLogger.writeln(L"Failed to install deferred hook [%S] because it already exists", name);
                return SPIReturn::FailureDuplicacy;
            }

            if (!hookMngr_.InstallDeferred(target, detour, original, const_cast<char*>(name)))
            {
                GLogger.writeln(L"Failed to install deferred hook [%S]", name);
                return SPIReturn::FailureHooking;
            }

            return SPIReturn::Success;
        }

        SPIDEFN EnableHook(const char* name)
        {
            SPI_IMPL_INSTANCE_LOCK(mtxInstallHook_);

            if (!name)
            {
                return SPIReturn::FailureInvalidParam;
            }

            if (!hookMngr_.Enable(const_cast<char*>(name)))
            {
                GLogger.writeln(L"Failed to enable hook [%S]", name);
                return SPIReturn::FailureHooking;
            }

            return SPIReturn::Success;
        }

        SPIDEFN UninstallHook(const char* name)
        {
            SPI_IMPL_INSTANCE_LOCK(mtxInstallHook_);

            if (!name)
            {
                return SPIReturn::FailureInvalidParam;
            }

            if (!hookMngr_.HookExists(const_cast<char*>(name)))
            {
                GLogger.writeln(L"Failed to uninstall the hook [%S] because it doesn't exist", name);
                return SPIReturn::FailureDuplicacy;
            }

            if (!hookMngr_.Uninstall(const_cast<char*>(name)))
            {
                GLogger.writeln(L"Failed to uninstall the hook [%S]", name);
                return SPIReturn::FailureHooking;
            }

            return SPIReturn::Success;
        }

        bool AcquireConsoleLease(wchar_t const* ownerName, SPIConsoleLease* outLease) override
        {
            bool const acquired = consoleBroker_.AcquireConsoleLease(ownerName, outLease);
            wchar_t const* const owner = ownerName != nullptr ? ownerName : L"<unnamed>";
            if (acquired)
            {
                GLogger.writeln(L"SPI ConsoleBroker: acquired lease %llu for %s.",
                    static_cast<unsigned long long>(*outLease), owner);
            }
            else
            {
                GLogger.writeln(L"SPI ConsoleBroker: failed to acquire a lease for %s (error = %lu).",
                    owner, GetLastError());
            }
            return acquired;
        }

        bool OpenConsoleOutput(SPIConsoleLease const lease, HANDLE* outOutputHandle) override
        {
            bool const opened = consoleBroker_.OpenConsoleOutput(lease, outOutputHandle);
            if (opened)
            {
                GLogger.writeln(L"SPI ConsoleBroker: opened console output for lease %llu.",
                    static_cast<unsigned long long>(lease));
            }
            else
            {
                GLogger.writeln(L"SPI ConsoleBroker: failed to open console output for lease %llu (error = %lu).",
                    static_cast<unsigned long long>(lease), GetLastError());
            }
            return opened;
        }

        void ReleaseConsoleLease(SPIConsoleLease const lease) override
        {
            consoleBroker_.ReleaseConsoleLease(lease);
        }

        SPIDEFN RegisterService(const char* name, std::uint32_t version, const void* table)
        {
            if (!isValidServiceName_(name) || version == 0 || !table)
            {
                return SPIReturn::FailureInvalidParam;
            }

            try
            {
                // Identify the calling plugin for the log. Registration is rare, so this is not done for queries.
                std::wstring provider = describeModuleAt_(_ReturnAddress());

                SPI_IMPL_INSTANCE_LOCK(mtxServices_);

                auto const existing = services_.find(name);
                if (existing != services_.end())
                {
                    GLogger.writeln(L"RegisterService: [%S] v%u from %s rejected, already registered as v%u by %s",
                        name, version, provider.c_str(), existing->second.Version, existing->second.Provider.c_str());
                    return SPIReturn::FailureDuplicacy;
                }

                GLogger.writeln(L"RegisterService: [%S] v%u by %s", name, version, provider.c_str());
                services_.emplace(name, ServiceEntry{ version, table, std::move(provider) });
            }
            catch (...)
            {
                // Never let an allocation failure cross into the calling plugin.
                return SPIReturn::FailureGeneric;
            }

            return SPIReturn::Success;
        }

        SPIDEFN QueryService(const char* name, std::uint32_t minVersion, const void** outTable, std::uint32_t* outVersion)
        {
            if (!outTable)
            {
                return SPIReturn::FailureInvalidParam;
            }
            *outTable = nullptr;

            if (!isValidServiceName_(name))
            {
                return SPIReturn::FailureInvalidParam;
            }

            try
            {
                // Consumers poll this until the provider registers, so it deliberately does not log.
                SPI_IMPL_INSTANCE_LOCK(mtxServices_);

                auto const found = services_.find(name);
                if (found == services_.end())
                {
                    return SPIReturn::FailureNotFound;
                }

                if (outVersion)
                {
                    *outVersion = found->second.Version;
                }
                if (found->second.Version < minVersion)
                {
                    return SPIReturn::FailureVersionTooOld;
                }

                *outTable = found->second.Table;
            }
            catch (...)
            {
                // The std::string key built from name can throw.
                return SPIReturn::FailureGeneric;
            }

            return SPIReturn::Success;
        }

        // End of ISharedProxyInterface implementation.
    };
}
