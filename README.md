# LEBinkProxy

A small proxy DLL which enables dev. console in Mass Effect 1, 2 and 3 (Legendary Edition). Originally written by d00telemental. Compiled versions of this dll are distributed through ME3Tweaks Mod Manager.

Additionally provides the following features:
 - Minidumps on application crash (with -enableminidumps command line argument)
 - Autoboot to a specific game when in the launcher using -game 1/2/3 and -autoterminate
 - Command line argument pass through to the game from the launcher

## Usage
ME3Tweaks Mod Manager will automatically install this dll on any mod install, or when installed via the tools menu for `Bink bypass`. 

Alternatively, you can install it manually:
   1. In your game binary directory (`Game\ME?\Binaries\Win64`), rename `bink2w64.dll` into `bink2w64_original.dll`.
   2. Copy the built proxy DLL (`Release\bink2w64.dll`) into the same folder.
   3. Launch the game.
   4. Use `~` and `Tab` to open small and big console viewports. If your console keybindings are different, use them.

If you built the proxy from source, the proxy DLL would be found in the default Visual Studio build path.
If you downloaded a built bundle, the proxy DLL would be found at `Release\bink2w64.dll`.

Should this proxy not work, make sure you are using the latest version and then check the log file at `Game\ME?\Binaries\Win64\bink2w64_proxy.log`. **You can get support for LEBinkProxy at [ME3Tweaks Discord](https://discord.gg/s8HA6dc).**

## For developers

In the original Mass Effect trilogy, certain mods required a DLL bypass to work. This led many developers to distribute a Bink proxy with their mods. Even after almost the entire scene moved to use [ME3Tweaks Mod Manager](https://github.com/ME3Tweaks/ME3TweaksModManager) (M3), which has a built-in Bink proxy installer, some developers continue(d) to ship their own DLLs, which resulted in many different versions of the tool being spread all over the Internet.

## Screenshot (LE1)

![Example of the proxy at work](https://i.imgur.com/MRgzZzg.png)

## Credits

To inject necessary functionality into the game, LEBinkProxy uses slightly modified [MinHook](https://github.com/TsudaKageyu/minhook) library by Tsuda Kageyu. The license and a statement of changes for it can be found in the proxy's source files, which are distributed to the end user.
