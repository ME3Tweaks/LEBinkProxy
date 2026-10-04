#pragma once

#include <cstdint>
#include <Windows.h>

#pragma region Plugin-side utilities

/// Game versions for SpiSupportDecl.
/// Not convertible with LEGameVersion or SPIGameVersion!!!

#define SPI_GAME_LEL (1 << 0)
#define SPI_GAME_LE1 (1 << 1)
#define SPI_GAME_LE2 (1 << 2)
#define SPI_GAME_LE3 (1 << 3)


/// SPI version macros.
/// Duplicate the stuff in version.h!!!

#define SPI_VERSION_V3      3
#define SPI_VERSION_V4      4
#define SPI_VERSION_ANY     SPI_VERSION_V3
#define SPI_VERSION_LATEST  SPI_VERSION_V4

using SPIConsoleLease = std::uint64_t;

/// Max. length of a service name passed to RegisterService / QueryService, excluding the terminator.
#define SPI_SERVICE_NAME_MAX 63

/// Plugin-side definition which marks the dll as supporting SPI.
#define SPI_PLUGINSIDE_SUPPORT(NAME,AUTHOR,VERSION,GAME_FLAGS,SPIMINVER) \
extern "C" __declspec(dllexport) void SpiSupportDecl(wchar_t** name, wchar_t** author, wchar_t** version, int* gameIndexFlags, int* spiMinVersion) \
{ *name = NAME;  *author = AUTHOR; *version = VERSION; *gameIndexFlags = GAME_FLAGS;  *spiMinVersion = SPIMINVER; }

/// Plugin-side definition which marks the dll for being loaded
/// at the earliest possible moment.
#define SPI_PLUGINSIDE_PRELOAD extern "C" __declspec(dllexport) bool SpiShouldPreload(void) { return true; }
/// Plugin-side definition which marks the dll for being loaded
/// only after DRM has finished decrypting the game.
#define SPI_PLUGINSIDE_POSTLOAD extern "C" __declspec(dllexport) bool SpiShouldPreload(void) { return false; }

/// Plugin-side definition which marks the dll for running
/// its attach point sequentially during game startup.
#define SPI_PLUGINSIDE_SEQATTACH extern "C" __declspec(dllexport) bool SpiShouldSpawnThread(void) { return false; }
/// Plugin-side definition which marks the dll for running
/// its attach point asynchronously during game startup.
#define SPI_PLUGINSIDE_ASYNCATTACH extern "C" __declspec(dllexport) bool SpiShouldSpawnThread(void) { return true; }

/// Plugin-side boilerplate macro for defining the plugin attach point.
/// This is run when the plugin is loaded by SPI (!), not when the DLL itself is loaded.
#define SPI_IMPLEMENT_ATTACH  extern "C" __declspec(dllexport) bool SpiOnAttach(ISharedProxyInterface* InterfacePtr)
/// Plugin-side boilerplate macro for defining the plugin detach point.
/// This *should* run when the plugin is unloaded by SPI (!), not when the DLL itself is unloaded.
/// WARNING: DO NOT RELY ON THIS BEING RUN
#define SPI_IMPLEMENT_DETACH  extern "C" __declspec(dllexport) bool SpiOnDetach(ISharedProxyInterface* InterfacePtr)

#pragma endregion


#pragma region Error codes

/// <summary>
/// Return value for all public SPI functions.
///   - 0 is illegal to return,
///   - 1 means success,
///   - [10; 100) mean failures,
///   - [100; ...) mean that normal operation is no longer possible.
/// </summary>
enum class SPIReturn : short
{
    // Adding stuff here requires adding stuff to MakeReturnCodeText() !!!
    Undefined = 0,
    Success = 1,
    FailureGeneric = 10,
    FailureDuplicacy = 11,
    FailureHooking = 12,
    FailureInvalidParam = 13,
    FailureUnsupportedYet = 14,
    FailureDeprecated = 15,
    FailurePatternInvalid = 16,
    FailurePatternTooLong = 17,
    FailureNotFound = 18,
    FailureVersionTooOld = 19,
    ErrorFatal = 100,
    ErrorWinApi = 115,
};

/// <summary>
/// Get a string describing an SPIReturn code.
/// </summary>
/// <returns>A const wide string</returns>
inline const wchar_t* SPIReturnToString(SPIReturn code)
{
    switch (code)
    {
    case SPIReturn::Undefined:                     return L"Undefined - illegal return code";
    case SPIReturn::Success:                       return L"Success - yay!";
    case SPIReturn::FailureGeneric:                return L"FailureGeneric - unspecified error";
    case SPIReturn::FailureDuplicacy:              return L"FailureDuplicacy - something unique was not unique, or something that should have existed didn't";
    case SPIReturn::FailureHooking:                return L"FailureHooking - injection code returned an error";
    case SPIReturn::FailureInvalidParam:           return L"FailureInvalidParam - illegal parameter passed to an SPI method";
    case SPIReturn::FailureUnsupportedYet:         return L"FailureUnsupportedYet - feature is defined in SPI but not yet provided";
    case SPIReturn::FailureDeprecated:             return L"FailureDeprecated - feature is defined in SPI but must not be used";
    case SPIReturn::FailurePatternInvalid:         return L"FailurePatternInvalid - provided pattern was ill-formed";
    case SPIReturn::FailurePatternTooLong:         return L"FailurePatternTooLong - provided pattern is too long";
    case SPIReturn::FailureNotFound:               return L"FailureNotFound - nothing is registered under that name (yet)";
    case SPIReturn::FailureVersionTooOld:          return L"FailureVersionTooOld - the registered version is lower than requested";
    case SPIReturn::ErrorFatal:                    return L"ErrorFatal - unspecified error AFTER WHICH EXECUTION CANNOT CONTINUE";
    case SPIReturn::ErrorWinApi:                   return L"ErrorWinApi - a call to Win API function failed, check GetLastError()";
    default:                                       return L"UNRECOGNIZED RETURN CODE - CONTACT DEVELOPERS";
    }
}

#pragma endregion


#pragma region The public-facing interface

#define SPIDECL [[nodiscard]] __declspec(noinline) virtual SPIReturn
#define SPIDEFN virtual SPIReturn

/// Game versions for GetHostGame.
/// Not interchangable with LEGameVersion!!!
enum class SPIGameVersion
{
    Launcher = 0,
    LE1 = 1,
    LE2 = 2,
    LE3 = 3
};

/// <summary>
/// SPI declaration for use in ASI mods.
/// </summary>
class ISharedProxyInterface
{
public:
    /// <summary>
    /// Get version of the SPI implementation provided by the host proxy.
    /// </summary>
    /// <param name="outVersionPtr">Output value for the version.</param>
    /// <returns>An appropriate <see cref="SPIReturn"/> code.</returns>
    SPIDECL GetVersion(unsigned long* outVersionPtr) = 0;
    /// <summary>
    /// Get if the host proxy was built in debug or release mode.
    /// </summary>
    /// <param name="outIsRelease">Output value for the build mode.</param>
    /// <returns>An appropriate <see cref="SPIReturn"/> code.</returns>
    SPIDECL GetBuildMode(bool* outIsRelease) = 0;
    /// <summary>
    /// Get the game this host proxy is attached to (1 - 3 or Launcher).
    /// </summary>
    /// <param name="outGameVersion">Output value for the game.</param>
    /// <returns>An appropriate <see cref="SPIReturn"/> code.</returns>
    SPIDECL GetHostGame(SPIGameVersion* outGameVersion) = 0;

    /// <summary>
    /// Search the main game module for a PEiD-style pattern.
    /// </summary>
    /// <param name="outOffsetPtr">Output value for the offset, set to NULL if not found.</param>
    /// <param name="combinedPattern">PEiD-style pattern specifying 100 bytes (299 chars + \0) at most.</param>
    /// <returns>An appropriate <see cref="SPIReturn"/> code.</returns>
    SPIDECL FindPattern(void** outOffsetPtr, char* combinedPattern) = 0;

    /// <summary>
    /// Use bink proxy's built-in injection library to detour a procedure.
    /// </summary>
    /// <param name="name">Name of the hook used for logging purposes.</param>
    /// <param name="target">Pointer to detour.</param>
    /// <param name="detour">Pointer to what to detour the target with.</param>
    /// <param name="original">Pointer to where to write out the original procedure.</param>
    /// <returns>An appropriate <see cref="SPIReturn"/> code.</returns>
    SPIDECL InstallHook(const char* name, void* target, void* detour, void** original) = 0;
    /// <summary>
    /// Remove a hook installed by <see cref="ISharedProxyInterface::InstallHook"/>.
    /// </summary>
    /// <param name="name">Name of the hook to remove.</param>
    /// <returns>An appropriate <see cref="SPIReturn"/> code.</returns>
    SPIDECL UninstallHook(const char* name) = 0;

    /// Acquire a named, opaque lease for the process console.
    /// The proxy copies ownerName for diagnostics and does not retain the caller's pointer.
    virtual bool AcquireConsoleLease(wchar_t const* ownerName, SPIConsoleLease* outLease) = 0;

    /// Open a fresh caller-owned handle to CONOUT$ for a valid lease.
    virtual bool OpenConsoleOutput(SPIConsoleLease lease, HANDLE* outOutputHandle) = 0;

    /// Release a lease. This is idempotent and never detaches the process console.
    virtual void ReleaseConsoleLease(SPIConsoleLease lease) = 0;

    /// Create a hook and its trampoline without enabling it. The caller must
    /// subsequently call EnableHook after publishing the returned original.
    SPIDECL InstallHookDeferred(const char* name, void* target, void* detour, void** original) = 0;

    /// Enable a hook previously created by InstallHookDeferred.
    SPIDECL EnableHook(const char* name) = 0;

    /// <summary>
    /// Publish a named table of functions for other plugins to look up with <see cref="ISharedProxyInterface::QueryService"/>.
    /// The table must stay valid until process exit; there is no unregistration. After the provider
    /// detaches, the table's functions should fail gracefully rather than become invalid.
    /// </summary>
    /// <param name="name">Printable ASCII without spaces, at most SPI_SERVICE_NAME_MAX chars, case-sensitive.
    /// By convention "Owner.Feature", e.g. "LMP.Lobby".</param>
    /// <param name="version">1 or higher. Bump it when appending functions to the table;
    /// changing an existing entry requires a new name instead.</param>
    /// <param name="table">Pointer to the provider's table, typically a static struct of function pointers.</param>
    /// <returns>An appropriate <see cref="SPIReturn"/> code; FailureDuplicacy if the name is taken.</returns>
    SPIDECL RegisterService(const char* name, std::uint32_t version, const void* table) = 0;

    /// <summary>
    /// Look up a table published with <see cref="ISharedProxyInterface::RegisterService"/>.
    /// Attach order between plugins is not guaranteed, so query lazily and retry on FailureNotFound.
    /// </summary>
    /// <param name="name">Name the table was registered under.</param>
    /// <param name="minVersion">Lowest table version the caller can use.</param>
    /// <param name="outTable">Output value for the table, set to NULL on failure.</param>
    /// <param name="outVersion">Optional output value for the registered version, also set on FailureVersionTooOld.</param>
    /// <returns>An appropriate <see cref="SPIReturn"/> code; FailureNotFound or FailureVersionTooOld on a miss.</returns>
    SPIDECL QueryService(const char* name, std::uint32_t minVersion, const void** outTable, std::uint32_t* outVersion) = 0;
};

#pragma endregion
