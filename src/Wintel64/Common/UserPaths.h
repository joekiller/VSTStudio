// Per-user file locations shared by VST Studio, the VideoSpeedTracker engine and ProcessHiLites.
//
// Installed copies of VST.cfg / ProcessHiLites.cfg are read-only templates (a .deb puts them in
// /usr/share, a Store/MSIX package or .app bundle can't be written at all), so each user gets their
// own config and data folders in the place their OS expects:
//
//            config                                  data (IPCam, HiLites, Stats, Trace)
//   Linux    $XDG_CONFIG_HOME/vststudio              $XDG_DATA_HOME/vststudio
//            (default ~/.config/vststudio)           (default ~/.local/share/vststudio)
//   Windows  %APPDATA%\VSTStudio                     %LOCALAPPDATA%\VSTStudio
//   macOS    ~/Library/Application Support/VSTStudio  ~/Library/Application Support/VSTStudio/Data
//
// Upgrades from v1.x (named VideoSpeedTracker): while a new-name folder doesn't exist and the v1 folder of the
// same kind does (videospeedtracker on Linux, VideoSpeedTracker on Windows and macOS), the v1 folder is used as
// it is, so absolute paths saved in VST.cfg and vst-studio.ini keep working.  Nothing is moved or copied.
//
// Windows uses the Known Folder API and macOS uses $HOME, so both follow MSIX and App Sandbox redirection.

#pragma once
#include <filesystem>
#include <string>

namespace userpaths {

std::filesystem::path configDir();
std::filesystem::path dataDir();
std::filesystem::path executableDir();

// Creates the IPCam, HiLites/forPosting, Stats and Trace folders under d (existing ones are left alone).
void makeDataDirs(const std::filesystem::path& d);

// Folder a config's dataPathPrefix names: the value itself, or dataDir() (with its subfolders created)
// when it is blank, which is how the shipped templates leave it.
std::filesystem::path dataPath(const std::string& dataPathPrefix);

// A file installed with the program (rel may contain '/'), looked for next to the executable, in
// ../share/vststudio (FHS installs, e.g. the .deb) and in ../Resources (macOS bundle).
// Empty if not found, e.g. when running from a build tree.
std::filesystem::path installedFile(const std::string& rel);

// The 2016 Locust Avenue clip that release packages bundle under sample/; empty if it isn't installed.
// The VST.cfg template's geometry and calibration are the ones that clip was recorded with.
std::filesystem::path sampleVideo();

// Windows: if the program got a console of its own (started by double-clicking the exe), wait for
// Enter before the window closes at exit, so its messages can be read.  No-op elsewhere.
void holdConsoleOnExit();

// Picks the config file to read when --config wasn't given, in this order:
//  1. <name> in the working directory (the original 2016 workflow, so existing setups keep working)
//  2. <configDir>/<name>
//  3. a fresh copy of the template installed with the program, written to (2) with dataPathPrefix
//     pointed at dataDir(), which is created with the IPCam, HiLites/forPosting, Stats and Trace folders
// Prints which file it chose. Returns an empty string, after printing why, if none can be found or made.
std::string resolveConfig(const std::string& name);

}
