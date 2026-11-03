// Working on top of a 2016 VideoSpeedTracker setup (src/Wintel64/Common/Legacy2016.h, src/Gui/Legacy.cpp).
//
//   legacy_test <repo root> <work dir> [VideoSpeedTracker exe]
//
// Builds the setup the 2016 program left behind, as its creator would have it: bin\ with the shipped VST.cfg (the
// fixture is pfr/VideoSpeedTracker's bin/VST.cfg byte for byte: CRLF, no newline at the end), ProcessHiLites.cfg and
// VideoSpeedTracker.exe, and beside it the locustdata folder with IPCam day folders, the dir /b listings, a 2016 stats
// csv, a highlights reel and a trace.  Then checks that:
//   - the 2016 VST.cfg is told apart from today's templates, and its setup is found and counted from bin\, from the
//     download's root, and from the data folder alone; "g:\locustdata" is found on this disk by its name
//   - it is the creator's (Locust Avenue, used); someone else's used setup and a download never run are not
//   - picking it up (copyConfig, copyStats) gives a config today's tracker reads with the same values, results that
//     keep their dates, and never touches a byte or a time stamp of the 2016 files
//   - Studio's config document opens the 2016 VST.cfg read only and carries edits on in the copy
//   - a re-run never replaces a 2016 stats csv, in Studio (rerunStem) or the command-line tool (--stats onto one)
#include "Legacy2016.h"
#include "Resume.h"
#include "Settings.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using namespace legacy2016;

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

static std::string slurp(const fs::path& p) {
	std::ifstream in(p, std::ios::binary);
	std::stringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

static void put(const fs::path& p, const std::string& text) {
	fs::create_directories(p.parent_path());
	std::ofstream(p, std::ios::binary) << text;
}

// Every file under `dir` with its bytes and time stamp, to prove nothing in the 2016 setup changed.
using Snapshot = std::map<std::string, std::pair<std::string, fs::file_time_type::rep>>;
static Snapshot snapshot(const fs::path& dir) {
	Snapshot s;
	for (auto& e : fs::recursive_directory_iterator(dir))
		if (e.is_regular_file()) s[e.path().generic_string()] = { slurp(e.path()), fs::last_write_time(e.path()).time_since_epoch().count() };
		else s[e.path().generic_string() + "/"] = { "", 0 };
	return s;
}

static const char* kRows2016 =
	", , Frame, Direction, StartFrame, EndFrame, # Frames, StartPix, EndPix, DeltaPix, VehicleArea, , estSpeed\r\n"
	"20160130, 172251, 120, SE, 85, 117, 32, 12, 1250, 1238, 61, , 27\r\n"
	"20160130, 172251, 410, NW, 368, 405, 37, 1260, 15, -1245, 70, , 27\r\n"
	"20160130, 172251, 900, SE, 872, 899, 27, 10, 1255, 1245, 140, , 32\r\n";

// The 2016 setup on "drive G:": G/bin and G/locustdata.
static fs::path make2016(const fs::path& root, const fs::path& repo) {
	fs::path g = root / "G";
	fs::remove_all(g);
	fs::path bin = g / "bin";
	fs::create_directories(bin);
	fs::copy_file(repo / "tests/fixtures/legacy2016/VST.cfg", bin / "VST.cfg");
	fs::copy_file(repo / "tests/fixtures/legacy2016/ProcessHiLites.cfg", bin / "ProcessHiLites.cfg");
	put(bin / "VideoSpeedTracker.exe", "MZ");
	fs::path data = g / "locustdata";
	put(data / "IPCam/directories.txt", "20160114\r\n20160130\r\n");
	put(data / "IPCam/20160114/manual_20160114100000.avi", "avi");
	put(data / "IPCam/20160114/manual_20160114110000.avi", "avi");
	put(data / "IPCam/20160114/files.txt", "manual_20160114100000.avi\r\nmanual_20160114110000.avi\r\n");
	put(data / "IPCam/20160130/manual_20160130172251.avi", "avi");
	put(data / "IPCam/20160130/files.txt", "manual_20160130172251.avi\r\n");
	put(data / "stats/stats_20160130_172251.csv", kRows2016);
	put(data / "trace/trace_20160130_172251.txt", "<1> trace\r\n");
	put(data / "HiLites/Hilites_20160130_172251.avi", "avi");
	fs::create_directories(data / "HiLites/forPosting");
	// Give the 2016 files 2016 time stamps, so keeping them is checkable.
	auto old = fs::last_write_time(data / "stats/stats_20160130_172251.csv") - std::chrono::hours(24 * 365 * 10);
	for (auto& e : fs::recursive_directory_iterator(g)) if (e.is_regular_file()) fs::last_write_time(e.path(), old);
	return g;
}

static void testFormat(const fs::path& repo) {
	CHECK(keys().size() == 23);
	CHECK(isConfig(repo / "tests/fixtures/legacy2016/VST.cfg"));
	// Today's templates are not 2016 files: comments, extra keys, a blank dataPathPrefix.
	CHECK(!isConfig(repo / "bin/VST.cfg"));
	CHECK(!isConfig(repo / "src/Wintel64/VideoSpeedTracker/VST.cfg"));
	std::string text = slurp(repo / "tests/fixtures/legacy2016/VST.cfg");
	CHECK(isConfigText(text + "\r\n"));                        // an editor's newline at the end: still theirs
	CHECK(!isConfigText(text + "\r\nCalibrationFPS = 30"));    // a 24th line: today's format
	CHECK(!isConfigText("# note\r\n" + text));                 // a comment line: the 2016 reader would refuse it
	std::string swapped = text;
	size_t a = swapped.find("L2RDirection"), b = swapped.find("R2LDirection");
	swapped.replace(b, 12, "L2RDirection");
	swapped.replace(a, 12, "R2LDirection");
	CHECK(!isConfigText(swapped));                             // out of order
	CHECK(dayText("20160130") == "Jan 30, 2016");
	CHECK(dayText("2016") == "2016");
}

static void testFind(const fs::path& work, const fs::path& repo) {
	fs::path g = make2016(work, repo);
	Snapshot before = snapshot(g);

	// From bin\ (where it ran): the config, and g:\locustdata found here by its name.
	Setup s = scan({ g / "bin" });
	CHECK(s.config == g / "bin/VST.cfg");
	CHECK(s.hiLitesConfig == g / "bin/ProcessHiLites.cfg");
	CHECK(s.program == g / "bin/VideoSpeedTracker.exe");
	CHECK(s.dataPathPrefix == "g:\\locustdata");
	CHECK(s.data == g / "locustdata");
	CHECK(s.l2r == "SE" && s.r2l == "NW");
	CHECK(s.calL2R == 35 && s.calR2L == 40);
	CHECK(s.recordings == 3 && s.days == 2);
	CHECK(s.firstDay == "20160114" && s.lastDay == "20160130");
	CHECK(s.latestRecording.filename() == "manual_20160130172251.avi");
	CHECK(s.statsFiles.size() == 1 && s.statsRows == 3);
	CHECK(s.highlights == 1 && s.traces == 1 && s.listings);
	CHECK(s.used() && s.locustAvenue() && s.creator());
	// From the download's root (bin\ inside it), and from the data folder alone.
	Setup fromRoot = scan({ g });
	CHECK(fromRoot.config == s.config && fromRoot.data == s.data);
	Setup dataOnly;
	CHECK(inspect(g / "locustdata", dataOnly));
	CHECK(dataOnly.config.empty() && dataOnly.recordings == 3 && dataOnly.creator());
	// VST Studio's own data folder is never taken for a 2016 one.
	Setup excluded;
	CHECK(!inspect(g / "locustdata", excluded, g / "locustdata"));
	// The scan prefers the setup with a config and data over data alone, whatever order it meets them in.
	Setup both = scan({ g / "locustdata", g / "bin" });
	CHECK(both.config == s.config && both.data == s.data);
	// Same key whichever way it was found (what the welcome remembers).
	CHECK(both.key() == s.key() && !s.key().empty());

	// Someone else's street, used: welcomed back, not called Professor.
	fs::path other = work / "Other";
	fs::remove_all(other);
	std::string cfg = slurp(g / "bin/VST.cfg");
	cfg.replace(cfg.find("g:\\locustdata"), 13, "c:\\vstdata");
	cfg.replace(cfg.find("\"SE\""), 4, "\"E\"");
	cfg.replace(cfg.find("\"NW\""), 4, "\"W\"");
	put(other / "bin/VST.cfg", cfg);
	put(other / "vstdata/stats/stats_20170401.csv", kRows2016);
	Setup o = scan({ other / "bin" });
	CHECK(o.config == other / "bin/VST.cfg" && o.data == other / "vstdata");
	CHECK(o.used() && !o.locustAvenue() && !o.creator());
	// The 2016 download never run: its config, no data anywhere.
	fs::path dl = work / "Download";
	fs::remove_all(dl);
	fs::create_directories(dl / "VideoSpeedTracker-master/bin");
	fs::copy_file(repo / "tests/fixtures/legacy2016/VST.cfg", dl / "VideoSpeedTracker-master/bin/VST.cfg");
	Setup d = scan({ dl / "VideoSpeedTracker-master" });
	CHECK(d.found() && !d.config.empty() && d.data.empty() && !d.used() && !d.creator());
	// Nothing 2016 about a modern data folder (Studio's own layout, today's stats header).
	fs::path modern = work / "Modern";
	fs::remove_all(modern);
	put(modern / "Stats/stats_camera0_20261101_133012.csv", "Date, Time, Frame, Direction, StartFrame\n");
	fs::create_directories(modern / "IPCam");
	Setup m;
	CHECK(!inspect(modern, m) && !m.found());

	CHECK(snapshot(g) == before);   // looking never writes
}

static void testPickUp(const fs::path& work, const fs::path& repo) {
	fs::path g = make2016(work, repo);
	Snapshot before = snapshot(g);
	Setup s = scan({ g / "bin" });

	// The settings: a copy today's tracker reads, with the 2016 values, writing to VST Studio's own data folder.
	fs::path copy = work / "config/VST-2016.cfg";
	fs::remove_all(work / "config");
	std::string err;
	CHECK(copyConfig(s, copy, err, "2026-11-02"));
	CHECK(err.empty());
	CHECK(!isConfig(copy));   // today's format now
	std::string text = slurp(copy);
	CHECK(text.find(s.config.string()) != std::string::npos);   // says where it came from
	CHECK(text.find('\r') == std::string::npos);
	Globals mine;
	CHECK(mine.readConfig(copy.string()));
	Globals theirs;
	CHECK(theirs.readConfig((g / "bin/VST.cfg").string()));    // today's tracker reads the 2016 file as it is, too
	CHECK(mine.dataPathPrefix.empty() && theirs.dataPathPrefix == "g:\\locustdata");
	CHECK(mine.AnalysisBoxTop == 220 && mine.AnalysisBoxTop == theirs.AnalysisBoxTop);
	CHECK(mine.speedLineLeft == theirs.speedLineLeft && mine.speedLineRight == theirs.speedLineRight);
	CHECK(mine.CalibrationFramesL2R == 35 && mine.CalibrationFramesR2L == 40);
	CHECK(mine.obstruction[0] == 251 && mine.obstruction[1] == 311);
	CHECK(mine.L2RDirection == "SE" && mine.R2LDirection == "NW");
	CHECK(mine.CalibrationSpeed == 25 && mine.CalibrationFPS == 30);
	CHECK(mine.problems().empty());
	// A second pick-up keeps the copy as it is (it may have been edited in Studio since).
	put(copy, text + "# edited in Studio\n");
	CHECK(copyConfig(s, copy, err));
	CHECK(slurp(copy).find("# edited in Studio") != std::string::npos);

	// The results: copied with their dates, once.
	fs::path data = work / "data";
	fs::remove_all(data);
	CHECK(copyStats(s, data, err) == 1 && err.empty());
	fs::path csv = data / "Stats/stats_2016_20160130_172251.csv";
	CHECK(fs::exists(csv) && slurp(csv) == kRows2016);
	CHECK(fs::last_write_time(csv) == fs::last_write_time(s.statsFiles[0]));
	CHECK(copyStats(s, data, err) == 0);
	// Results lists them, labelled as the 2016 program's.
	auto runs = resume::savedRuns(data.string());
	CHECK(runs.size() == 1 && runs[0].name == "2016_20160130_172251");
	CHECK(resume::runLabel("2016_20160130_172251") == "2016 VideoSpeedTracker  ·  2016-01-30 17:22");
	CHECK(resume::runLabel("2016_20160130") == "2016 VideoSpeedTracker  ·  2016-01-30");

	// Studio's config document: the 2016 file opens read only, an edit goes to the copy.
	ConfigDoc doc;
	CHECK(doc.load((g / "bin/VST.cfg").string()));
	CHECK(doc.legacy2016());
	doc.g.speedLineLeft = 360;
	CHECK(doc.dirty());
	CHECK(!doc.save());
	CHECK(!doc.error.empty());
	fs::path copy2 = work / "config/VST-2016-2.cfg";
	CHECK(copyConfig(s, copy2, err));
	CHECK(doc.moveTo(copy2.string()));
	CHECK(!doc.legacy2016() && doc.path() == copy2.string());
	doc.g.dataPathPrefix.clear();
	CHECK(doc.save());
	Globals moved;
	CHECK(moved.readConfig(copy2.string()));
	CHECK(moved.speedLineLeft == 360 && moved.dataPathPrefix.empty() && moved.speedLineRight == 909);

	// Re-runs: Studio and the tracker pick a name beside the 2016 results, never theirs.
	CHECK(rerunStem(g / "locustdata/stats", "20160130_172251") == "20160130_172251_rerun");
	CHECK(rerunStem(g / "locustdata/stats", "20160114") == "20160114");
	CHECK(rerunStem(data / "Stats", "camera0_20261101_133012") == "camera0_20261101_133012");

	CHECK(snapshot(g) == before);   // not a byte, not a time stamp
}

// The command-line tool given --stats onto a 2016 csv, with the 2016 VST.cfg as it is: it runs on that config and
// writes <name>_rerun.csv beside the 2016 file.
static void testCommandLine(const fs::path& work, const fs::path& repo, const std::string& exe) {
	fs::path g = make2016(work, repo);
	Snapshot before = snapshot(g);
	fs::path csv = g / "locustdata/stats/stats_20160130_172251.csv";
	std::string cmd = "\"" + exe + "\" --headless --input \"" + (repo / "sampleData/IPCam/20160130/manual_20160130172251.avi").string()
		+ "\" --config \"" + (g / "bin/VST.cfg").string() + "\" --stats \"" + csv.string() + "\" --start-frame 15000 > \""
		+ (work / "cli.log").string() + "\" 2>&1";
#ifdef _WIN32
	cmd = "\"" + cmd + "\"";   // cmd.exe drops the outer quotes of a line that starts with one
#endif
	int rc = std::system(cmd.c_str());
	CHECK(rc == 0);
	fs::path kept = g / "locustdata/stats/stats_20160130_172251_rerun.csv";
	CHECK(fs::exists(kept));
	CHECK(slurp(kept).rfind("Date, Time, Frame", 0) == 0);
	fs::remove(kept);
	std::error_code ec;
	fs::remove(g / "locustdata/stats/run_20160130_172251_rerun.json", ec);
	CHECK(snapshot(g) == before);
	if (failures) std::printf("%s", slurp(work / "cli.log").c_str());
}

int main(int argc, char** argv) {
	if (argc < 3) { std::printf("usage: legacy_test <repo root> <work dir> [VideoSpeedTracker]\n"); return 2; }
	fs::path repo = argv[1], work = argv[2];
	fs::create_directories(work);
	testFormat(repo);
	testFind(work, repo);
	testPickUp(work, repo);
	if (argc > 3) testCommandLine(work, repo, argv[3]);
	std::printf("%d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
