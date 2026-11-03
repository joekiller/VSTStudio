// Recognizing a setup made with the original 2016 VideoSpeedTracker, and working on top of it without touching it.
//
// Paul Reynolds' 2016 program (github.com/pfr/VideoSpeedTracker) kept everything in two places, and nothing else
// (no registry, no per-user folder, no preferences file; speed limit, trace and highlights were asked at each run):
//
//   VST.cfg               read from the working directory, i.e. the folder the program ran in (bin\ of the download, or
//                         src\Wintel64\VideoSpeedTracker\ when run from Visual Studio).  Exactly 23 "key = value # note"
//                         lines in a fixed order (keys() below), CRLF, no blank or comment lines and no newline after
//                         the last one: the 2016 reader stops with "Just read LHS doesn't match anything", "Too many
//                         lines" or "Unexpected error in config file" on anything else.  ProcessHiLites.cfg sits beside
//                         it (dataPathPrefix, CropLeft, CropTop).
//   <dataPathPrefix>      the data folder, shipped as g:\locustdata:
//                           IPCam\directories.txt                 `dir /b` of IPCam, written by every run
//                           IPCam\<yyyymmdd>\manual_<yyyymmddhhmmss>.avi   the Foscam's recordings, a folder a day
//                           IPCam\<yyyymmdd>\files.txt            `dir /b *.avi` of that day, written by every run
//                           stats\stats_<yyyymmdd>.csv            one day ("*" at the file prompt), or
//                           stats\stats_<yyyymmdd>_<hhmmss>.csv   one recording; header kStatsHeader
//                           trace\trace_<same>.txt, HiLites\Hilites_<same>.avi, HiLites\forPosting\
//
// The last 2016 version (May 2016, the repository's neighborhoods branch, built from VisualStudio2013_Projects\...\
// x64\Release) adds streets: neighborhoods.cfg beside the program ("dataPathPrefix = g:\VSTData #", then one
// "Locust Avenue = Locust #" line per street) and, per street, <prefix>\<Short>Data\ holding the same folders plus
// its own <Short>Data.cfg (the 23 lines; its reader also skips blank lines), <Short>HiLites.cfg and
// stats\hiLiteStats_*.csv.  Its stats rows may end ", HiLite".
//
// Both versions write the stats Direction column as VST.cfg's value with its quotes (and any tabs before the '#'),
// e.g. "SE": the 2016 trim() removed spaces only.  readStatsCsv() takes the quotes off.
//
// So "recognizing the environment" means finding a VST.cfg in exactly that 2016 shape and/or a data folder with
// those fingerprints (the dir /b listings and 2016-header stats files are only ever written by the 2016 program).
// scan() looks in the places such a setup would be; Setup says what it found, in numbers a greeting can quote.
//
// The import never writes inside the 2016 setup.  copyConfig() makes an editable copy elsewhere, copyStats() copies
// the results into VST Studio's own data folder, and the recordings are opened where they are (reading a video
// changes nothing).  rerunStem() keeps a re-run from replacing a 2016 stats file of the same name.
#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace legacy2016 {

// The 23 keys of the 2016 VST.cfg, in the order its reader demands.
const std::vector<std::string>& keys();

// The header line the 2016 program wrote at the top of every stats csv (the date and time columns had no names).
extern const char* const kStatsHeader;

// Is this the text of a 2016-format VST.cfg: its 23 keys in order and nothing else?  Blank lines are allowed (the May
// 2016 reader skips them; an editor may add one at the end).  Comments and extra keys are not.
bool isConfigText(const std::string& text);
bool isConfig(const std::filesystem::path& file);

// A stats csv the 2016 program wrote: its first line is kStatsHeader (spaces and a CR aside).
bool isStatsCsv(const std::filesystem::path& file);

// What scan()/inspect() found.  Every path is one that exists; numbers are what was counted on disk.
struct Setup {
	std::filesystem::path config;         // the 2016 VST.cfg ("" = none found, only a data folder)
	std::filesystem::path hiLitesConfig;  // ProcessHiLites.cfg beside it ("" = none)
	std::filesystem::path program;        // VideoSpeedTracker.exe beside it ("" = none)
	std::filesystem::path data;           // the data folder (IPCam, stats, ...) ("" = not found or not reachable)
	std::string dataPathPrefix;           // as VST.cfg spells it, e.g. "g:\locustdata"
	std::string l2r, r2l;                 // compass headings, e.g. "SE" and "NW" (quotes removed)
	int calL2R = 0, calR2L = 0;           // CalibrationFramesL2R / R2L
	int recordings = 0;                   // IPCam\<day>\*.avi
	int days = 0;                         // IPCam\<yyyymmdd> folders with recordings
	std::string firstDay, lastDay;        // "yyyymmdd" of the first and last such folder
	std::filesystem::path latestRecording;
	std::vector<std::filesystem::path> statsFiles;   // 2016-format stats csvs, oldest name first
	int statsRows = 0;                    // vehicles in them
	int highlights = 0, traces = 0;       // HiLites\*.avi, trace\*.txt
	bool listings = false;                // IPCam\directories.txt or a day's files.txt: the 2016 program ran here

	bool found() const { return !config.empty() || !data.empty(); }
	// The program was used here, not just downloaded: recordings, results or its own folder listings.
	bool used() const { return recordings > 0 || !statsFiles.empty() || listings; }
	// The Locust Avenue setup itself: the street's headings (SE / NW) or the locustdata folder.
	bool locustAvenue() const;
	// Its creator's own: Locust Avenue and used.  Only this gets "Hello, Professor."
	bool creator() const { return locustAvenue() && used(); }
	// Identifies this setup across launches (what the greeting was shown for), e.g. "g:/vst/bin/vst.cfg|g:/locustdata".
	std::string key() const;
};

// Looks at one folder as each thing a 2016 setup could be, and fills `out` with what it finds (keeping what is
// already there, so a config found first gets its data folder added).  `dir` may be:
//   a folder holding a 2016 VST.cfg (bin\, or the Visual Studio project folder)
//   a download or clone of the 2016 repository (looks in bin\, src\Wintel64\VideoSpeedTracker\,
//     VisualStudio2013_Projects\VideoSpeedTracker\x64\Release and Debug, and VSTData\)
//   a folder holding neighborhoods.cfg, or a folder of street folders each with its own <Short>Data.cfg (G:\VSTData):
//     the most complete street is taken, the creator's Locust Avenue on a tie
//   a street's data folder with its own <Short>Data.cfg (G:\VSTData\LocustData)
//   a data folder (IPCam / stats with 2016 fingerprints), e.g. g:\locustdata
// Returns whether anything 2016 was found there.  `exclude` is never taken as a data folder (VST Studio's own).
bool inspect(const std::filesystem::path& dir, Setup& out, const std::filesystem::path& exclude = {});

// Where a 2016 data folder named by `dataPathPrefix` is now: as written when that exists, else a folder with its last
// name ("locustdata") beside the config, in any folder above it, in the home folder or Documents, or on another drive or volume (a setup
// copied from the old PC).  "" if none of those has 2016 data.
std::filesystem::path findDataFolder(const std::string& dataPathPrefix, const std::filesystem::path& configDir,
	const std::filesystem::path& exclude = {});

// The folders scan() inspects: the working directory, the program's folder, the home folder and its Desktop,
// Documents, Downloads, source\repos, Documents\GitHub and Visual Studio project folders, the root of every drive
// (Windows) or mounted volume (macOS /Volumes, Linux /media and /mnt), and inside each one any folder named
// VideoSpeedTracker*, locustdata or VSTData.  Only folders that exist are returned.
std::vector<std::filesystem::path> searchFolders();

// The first 2016 setup found in `folders` (searchFolders() when empty), preferring one with a VST.cfg and data, then
// one with a VST.cfg, then data alone.  Read only; reads a few directory listings and the first line of each csv.
Setup scan(const std::vector<std::filesystem::path>& folders = {}, const std::filesystem::path& exclude = {});

// Writes `to`, an editable copy of the 2016 VST.cfg for VST Studio: a header saying where it came from, the 23 lines
// with their comments, native line endings, and dataPathPrefix left blank so new results go to VST Studio's own data
// folder.  CalibrationSpeed and CalibrationFPS are added with the values the 2016 program assumed (25 mph, 30 fps),
// so a camera at another frame rate still measures right.  `when` (e.g. "2026-11-02") goes in the header.  An existing
// `to` is left alone and counts as done: it may have been changed in VST Studio since.
bool copyConfig(const Setup& s, const std::filesystem::path& to, std::string& error, const std::string& when = "");

// Copies each 2016 stats csv into <dataDir>/Stats as stats_2016_<name>.csv (keeping its date, so it sorts as old),
// skipping ones already copied.  Returns how many were copied now; `error` names the first that failed.
int copyStats(const Setup& s, const std::filesystem::path& dataDir, std::string& error);

// The run name a re-run should write under: `stem` itself, or "<stem>_rerun" when <statsDir>/stats_<stem>.csv is a
// 2016 file, which must not be replaced (re-running a 2016 recording gives the same name).
std::string rerunStem(const std::filesystem::path& statsDir, const std::string& stem);

// "20160130" -> "Jan 30, 2016"; anything else unchanged.
std::string dayText(const std::string& yyyymmdd);

}  // namespace legacy2016
