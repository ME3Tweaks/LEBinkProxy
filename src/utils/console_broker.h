#pragma once

#include <Windows.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

#include "../spi/interface.h"

namespace SPI
{
    /**
     * Proxy-owned sticky console broker.
     *
     * The broker counts only participating SPI clients. Releasing the last known
     * lease deliberately does not call FreeConsole because legacy injected DLLs
     * may still be using the process console.
     */
    class ConsoleBroker final
    {
    public:
        bool AcquireConsoleLease(wchar_t const* ownerName, SPIConsoleLease* outLease) noexcept
        {
            if (outLease == nullptr)
            {
                SetLastError(ERROR_INVALID_PARAMETER);
                return false;
            }

            *outLease = 0;

            std::lock_guard<std::mutex> lock{ mutex_ };
            if (!HasAttachedConsole_())
            {
                if (!AllocConsole())
                {
                    // Some console hosts (notably ConPTY/Windows Terminal
                    // scenarios) do not satisfy the probes above even though
                    // the process already has a usable console connection.
                    // AllocConsole reports ERROR_ACCESS_DENIED for that case.
                    // Keep going and let CONOUT$ be the authoritative check.
                    if (GetLastError() != ERROR_ACCESS_DENIED)
                    {
                        ++failedAcquires_;
                        return false;
                    }
                }
                else
                {
                    proxyAllocatedConsole_ = true;
                }
            }

            SPIConsoleLease const lease = NextLease_();
            try
            {
                leases_.emplace(lease, ownerName != nullptr ? ownerName : L"<unnamed>");
            }
            catch (...)
            {
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                ++failedAcquires_;
                return false;
            }
            ++successfulAcquires_;
            *outLease = lease;
            return true;
        }

        bool OpenConsoleOutput(SPIConsoleLease const lease, HANDLE* outOutputHandle) noexcept
        {
            if (outOutputHandle == nullptr)
            {
                SetLastError(ERROR_INVALID_PARAMETER);
                return false;
            }

            *outOutputHandle = INVALID_HANDLE_VALUE;

            // Keep validation and handle creation under the same lock so a
            // release cannot race a successful open for an already-released
            // lease.
            std::lock_guard<std::mutex> lock{ mutex_ };
            if (leases_.find(lease) == leases_.end())
            {
                SetLastError(ERROR_INVALID_HANDLE);
                ++invalidTokens_;
                return false;
            }

            // A freshly allocated console can report success from AllocConsole
            // before CONOUT$ is ready to open. This is most visible when the
            // first lease is acquired by an asynchronously attached ASI in a
            // Release proxy. Keep the retry short and bounded; this path is not
            // used while holding the loader lock.
            DWORD const openAttempts = proxyAllocatedConsole_ ? 8u : 1u;
            HANDLE output = INVALID_HANDLE_VALUE;
            DWORD lastError = ERROR_SUCCESS;
            for (DWORD attempt = 0; attempt < openAttempts; ++attempt)
            {
                output = CreateFileW(
                    L"CONOUT$",
                    GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    nullptr,
                    OPEN_EXISTING,
                    0,
                    nullptr);
                if (output != INVALID_HANDLE_VALUE)
                    break;

                lastError = GetLastError();
                if (attempt + 1 < openAttempts)
                    Sleep(1);
            }
            if (output == INVALID_HANDLE_VALUE)
            {
                SetLastError(lastError);
                ++failedOpens_;
                return false;
            }

            ++successfulOpens_;
            *outOutputHandle = output;
            return true;
        }

        void ReleaseConsoleLease(SPIConsoleLease const lease) noexcept
        {
            if (lease == 0)
                return;

            std::lock_guard<std::mutex> lock{ mutex_ };
            auto const erased = leases_.erase(lease);
            if (erased != 0)
                ++successfulReleases_;
            else
                ++invalidTokens_;
        }

        [[nodiscard]] std::size_t LeaseCount() const noexcept
        {
            std::lock_guard<std::mutex> lock{ mutex_ };
            return leases_.size();
        }

    private:
        [[nodiscard]] static bool HasAttachedConsole_() noexcept
        {
            if (GetConsoleWindow() != nullptr || GetConsoleCP() != 0 || GetConsoleOutputCP() != 0)
                return true;

            HANDLE const output = GetStdHandle(STD_OUTPUT_HANDLE);
            DWORD mode = 0;
            return output != nullptr && output != INVALID_HANDLE_VALUE && GetConsoleMode(output, &mode) != FALSE;
        }

        [[nodiscard]] SPIConsoleLease NextLease_() noexcept
        {
            // Zero is reserved as the invalid token. Skip wrapped values and any
            // still-live token rather than accidentally handing out a duplicate.
            for (;;)
            {
                SPIConsoleLease const candidate = nextLease_++;
                if (nextLease_ == 0)
                    nextLease_ = 1;

                if (candidate != 0 && leases_.find(candidate) == leases_.end())
                    return candidate;
            }
        }

        mutable std::mutex mutex_;
        SPIConsoleLease nextLease_{ 1 };
        std::unordered_map<SPIConsoleLease, std::wstring> leases_;
        bool proxyAllocatedConsole_{ false };

        std::uint64_t successfulAcquires_{ 0 };
        std::uint64_t successfulOpens_{ 0 };
        std::uint64_t successfulReleases_{ 0 };
        std::uint64_t failedAcquires_{ 0 };
        std::uint64_t failedOpens_{ 0 };
        std::uint64_t invalidTokens_{ 0 };
    };
}
