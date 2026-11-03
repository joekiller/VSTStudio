#include "Legacy2016.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;
using std::string;
using std::vector;

namespace legacy2016 {

const char* const kStatsHeader = ", , Frame, Direction, StartFrame, EndFrame, # Frames, StartPix, EndPix, DeltaPix, VehicleArea, , estSpeed";

const vector<string>& keys() {
	// Globals::readConfig()'s lhsString[23] in pfr/VideoSpeedTracker, verbatim.
	static const vector<string> k = {
		"dataPathPrefix", "L2RDirection", "R2LDirection", "obstruction", "AnalysisBoxLeft", "AnalysisBoxTop",
		"AnalysisBoxWidth", "AnalysisBoxHeight", "speedLineLeft", "speedLineRight", "maxL2RDistOnEntry",
		"maxR2LDistOnEntry", "entryLookBack", "obstruction_extent", "largeVehicleArea", "CalibrationFramesL2R",
		"CalibrationFramesR2L", "SENSITIVITY_VALUE", "BLUR_SIZE", "SLOP", "R2LStreetY", "L2RStreetY", "nextHeight",
	};
	return k;
}

namespace {

string lower(string s) {
	for (char& c : s) c = (char)std::tolower((unsigned char)c);
	return s;
}

string trim(const string& s) {
	size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
	return a == string::npos ? string() : s.substr(a, b - a + 1);
}

bool allDigits(const string& s) {
	return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

bool startsWith(const string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

bool isDir(const fs::path& p) {
	std::error_code ec;
	return !p.empty() && fs::is_directory(p, ec);
}

bool isFile(const fs::path& p) {
	std::error_code ec;
	return !p.empty() && fs::is_regular_file(p, ec);
}

// Windows paths are case-blind and the 2016 program mixed "stats" with "Stats"; a copy on a Mac or Linux disk keeps
// whichever case it had.  The entry of `dir` named `name` in any case, or "" if there is none.
fs::path child(const fs::path& dir, const string& name) {
	fs::path exact = dir / name;
	if (fs::exists(exact)) return exact;
	std::error_code ec;
	string want = lower(name);
	for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
		if (lower(it->path().filename().string()) == want) return it->path();
	return {};
}

// Every line of a small text file, CRs removed; empty when it can't be read.
vector<string> lines(const fs::path& file) {
	vector<string> out;
	std::ifstream in(file, std::ios::binary);
	for (string l; std::getline(in, l);) {
		if (!l.empty() && l.back() == '\r') l.pop_back();
		out.push_back(l);
	}
	return out;
}

string firstLine(const fs::path& file) {
	std::ifstream in(file, std::ios::binary);
	string l;
	std::getline(in, l);
	return l;
}

// The 2016 reader's split: key before '=', value between '=' and '#', both trimmed.
void sides(const string& line, string& key, string& value) {
	size_t eq = line.find('=');
	key = trim(line.substr(0, eq));
	value = eq == string::npos ? string() : trim(line.substr(eq + 1, line.find('#', eq + 1) == string::npos ? string::npos : line.find('#', eq + 1) - eq - 1));
}

string unquote(const string& v) {
	return v.size() >= 2 && v.front() == '"' && v.back() == '"' ? v.substr(1, v.size() - 2) : v;
}

int toInt(const string& v) {
	try { return std::stoi(v); }
	catch (...) { return 0; }
}

// Counts what a 2016 data folder holds into `s`.  True when it has the 2016 program's fingerprints: its folder
// listings, 2016-header stats, or Foscam recordings filed IPCam\<yyyymmdd>\manual_*.avi.
bool countData(const fs::path& dir, Setup& s) {
	Setup c;
	std::error_code ec;
	fs::path ipcam = child(dir, "IPCam");
	if (isDir(ipcam)) {
		c.listings = isFile(child(ipcam, "directories.txt"));
		vector<string> dayNames;
		for (fs::directory_iterator it(ipcam, ec), end; !ec && it != end; it.increment(ec)) {
			string day = it->path().filename().string();
			if (day.size() != 8 || !allDigits(day) || !it->is_directory(ec)) continue;
			if (isFile(child(it->path(), "files.txt"))) c.listings = true;
			int n = 0;
			fs::path latest;
			std::error_code dec;
			for (fs::directory_iterator f(it->path(), dec), fend; !dec && f != fend; f.increment(dec)) {
				string name = f->path().filename().string();
				if (lower(f->path().extension().string()) != ".avi" || !f->is_regular_file(dec)) continue;
				n++;
				if (latest.empty() || name > latest.filename().string()) latest = f->path();
			}
			if (n == 0) continue;
			c.recordings += n;
			c.days++;
			dayNames.push_back(day);
			if (c.latestRecording.empty() || day >= c.lastDay) c.latestRecording = latest;
			if (c.lastDay.empty() || day > c.lastDay) c.lastDay = day;
			if (c.firstDay.empty() || day < c.firstDay) c.firstDay = day;
		}
	}
	fs::path stats = child(dir, "Stats");
	if (isDir(stats)) {
		for (fs::directory_iterator it(stats, ec), end; !ec && it != end; it.increment(ec)) {
			string name = it->path().filename().string();
			if (!startsWith(lower(name), "stats_") || lower(it->path().extension().string()) != ".csv" || !isStatsCsv(it->path())) continue;
			c.statsFiles.push_back(it->path());
			for (const string& l : lines(it->path())) c.statsRows += trim(l).empty() ? 0 : 1;
			c.statsRows--;   // the header
		}
		std::sort(c.statsFiles.begin(), c.statsFiles.end());
	}
	auto countIn = [&](const char* sub, const char* ext) {
		int n = 0;
		fs::path d = child(dir, sub);
		std::error_code e;
		if (isDir(d))
			for (fs::directory_iterator it(d, e), end; !e && it != end; it.increment(e)) n += lower(it->path().extension().string()) == ext;
		return n;
	};
	c.highlights = countIn("HiLites", ".avi");
	c.traces = countIn("Trace", ".txt");
	if (!c.listings && c.statsFiles.empty() && c.recordings == 0) return false;
	s.data = dir;
	s.recordings = c.recordings; s.days = c.days; s.firstDay = c.firstDay; s.lastDay = c.lastDay;
	s.latestRecording = c.latestRecording; s.statsFiles = c.statsFiles; s.statsRows = std::max(0, c.statsRows);
	s.highlights = c.highlights; s.traces = c.traces; s.listings = c.listings;
	return true;
}

bool samePath(const fs::path& a, const fs::path& b) {
	if (a.empty() || b.empty()) return false;
	std::error_code ec;
	return lower(fs::weakly_canonical(a, ec).generic_string()) == lower(fs::weakly_canonical(b, ec).generic_string());
}

fs::path home() {
#if defined(_WIN32)
	const char* v = std::getenv("USERPROFILE");
#else
	const char* v = std::getenv("HOME");
#endif
	return v && *v ? fs::path(v) : fs::path();
}

// Roots another disk can appear under: every drive letter on Windows (but CD drives and network shares, which can
// hang a background scan for a long time), /Volumes/* on macOS, /media/<user>/*, /run/media/<user>/* and /mnt/* on Linux.
vector<fs::path> volumes() {
	vector<fs::path> out;
#if defined(_WIN32)
	// An empty card reader or floppy would otherwise pop up "There is no disk in the drive".
	DWORD old = 0;
	SetThreadErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX, &old);
	DWORD mask = GetLogicalDrives();
	for (int i = 2; i < 26; i++) {   // C: onwards
		if (!(mask & (1u << i))) continue;
		wchar_t root[4] = { wchar_t(L'A' + i), L':', L'\\', 0 };
		UINT type = GetDriveTypeW(root);
		if (type == DRIVE_FIXED || type == DRIVE_REMOVABLE || type == DRIVE_RAMDISK) out.emplace_back(root);
	}
	SetThreadErrorMode(old, nullptr);
#else
	vector<fs::path> parents = { "/Volumes", "/mnt" };
	if (const char* u = std::getenv("USER")) {
		parents.push_back(fs::path("/media") / u);
		parents.push_back(fs::path("/run/media") / u);
	}
	for (const fs::path& p : parents) {
		std::error_code ec;
		for (fs::directory_iterator it(p, ec), end; !ec && it != end; it.increment(ec))
			if (it->is_directory(ec)) out.push_back(it->path());
	}
#endif
	return out;
}

void addIfDir(vector<fs::path>& v, const fs::path& p) {
	if (!isDir(p)) return;
	for (const fs::path& q : v) if (samePath(p, q)) return;
	v.push_back(p);
}

}  // namespace

bool isConfigText(const string& text) {
	std::istringstream in(text);
	vector<string> ls;
	for (string l; std::getline(in, l);) {
		if (!l.empty() && l.back() == '\r') l.pop_back();
		ls.push_back(l);
	}
	while (!ls.empty() && trim(ls.back()).empty()) ls.pop_back();
	if (ls.size() != keys().size()) return false;
	for (size_t i = 0; i < ls.size(); i++) {
		string k, v;
		sides(ls[i], k, v);
		if (k != keys()[i] || ls[i].find('=') == string::npos) return false;
	}
	return true;
}

bool isConfig(const fs::path& file) {
	std::error_code ec;
	if (!isFile(file) || fs::file_size(file, ec) > 64 * 1024) return false;
	std::ifstream in(file, std::ios::binary);
	std::stringstream ss;
	ss << in.rdbuf();
	return isConfigText(ss.str());
}

bool isStatsCsv(const fs::path& file) {
	auto squash = [](const string& s) { string o; for (char c : s) if (c != ' ' && c != '\r' && c != '\t') o += c; return o; };
	return isFile(file) && squash(firstLine(file)) == squash(kStatsHeader);
}

bool Setup::locustAvenue() const {
	return (lower(l2r) == "se" && lower(r2l) == "nw") || lower(dataPathPrefix).find("locustdata") != string::npos
		|| (!data.empty() && lower(data.filename().string()) == "locustdata");
}

string Setup::key() const {
	std::error_code ec;
	auto norm = [&](const fs::path& p) { return p.empty() ? string() : lower(fs::weakly_canonical(p, ec).generic_string()); };
	return norm(config) + "|" + norm(data);
}

fs::path findDataFolder(const string& dataPathPrefix, const fs::path& configDir, const fs::path& exclude) {
	string prefix = trim(dataPathPrefix);
	// The 2016 comment says "Use double back-slash" and Globals.h says not to: either way it means one.
	for (size_t at; (at = prefix.find("\\\\")) != string::npos;) prefix.erase(at, 1);
	if (prefix.empty()) return {};
	Setup scratch;
#if defined(_WIN32)
	if (isDir(fs::path(prefix)) && !samePath(prefix, exclude)) return fs::path(prefix);
#else
	// "g:\locustdata" names nothing on a Mac or Linux disk; a POSIX path from a later setup might.
	if (prefix.find('\\') == string::npos && prefix.find(':') == string::npos && isDir(fs::path(prefix)) && !samePath(prefix, exclude))
		return fs::path(prefix);
#endif
	string leaf = prefix;
	while (!leaf.empty() && (leaf.back() == '\\' || leaf.back() == '/')) leaf.pop_back();
	size_t cut = leaf.find_last_of("\\/:");
	if (cut != string::npos) leaf = leaf.substr(cut + 1);
	if (leaf.empty()) return {};
	vector<fs::path> bases;
	if (!configDir.empty()) {
		bases.push_back(configDir);
		bases.push_back(configDir.parent_path());
		bases.push_back(configDir.parent_path().parent_path());
	}
	bases.push_back(home());
	bases.push_back(home() / "Documents");
	for (const fs::path& v : volumes()) bases.push_back(v);
	for (const fs::path& b : bases) {
		if (b.empty()) continue;
		fs::path d = child(b, leaf);
		if (!d.empty() && isDir(d) && !samePath(d, exclude) && countData(d, scratch)) return d;
	}
	return {};
}

bool inspect(const fs::path& dir, Setup& out, const fs::path& exclude) {
	if (!isDir(dir)) return false;
	bool any = false;
	fs::path cfg = child(dir, "VST.cfg");
	if (out.config.empty() && isConfig(cfg)) {
		out.config = cfg;
		for (const string& l : lines(cfg)) {
			string k, v;
			sides(l, k, v);
			if (k == "dataPathPrefix") out.dataPathPrefix = v;
			else if (k == "L2RDirection") out.l2r = unquote(v);
			else if (k == "R2LDirection") out.r2l = unquote(v);
			else if (k == "CalibrationFramesL2R") out.calL2R = toInt(v);
			else if (k == "CalibrationFramesR2L") out.calR2L = toInt(v);
		}
		fs::path hl = child(dir, "ProcessHiLites.cfg");
		if (isFile(hl)) out.hiLitesConfig = hl;
		fs::path exe = child(dir, "VideoSpeedTracker.exe");
		if (isFile(exe)) out.program = exe;
		if (out.data.empty()) {
			fs::path d = findDataFolder(out.dataPathPrefix, dir, exclude);
			if (!d.empty() && !countData(d, out)) out.data = d;   // the folder it names, even before any recording
		}
		any = true;
	}
	// A download or clone of the 2016 repository: its bin\ and the Visual Studio project folder.
	if (out.config.empty()) {
		for (const fs::path& sub : { fs::path("bin"), fs::path("src") / "Wintel64" / "VideoSpeedTracker" }) {
			fs::path d = dir / sub;
			if (isDir(d) && isConfig(child(d, "VST.cfg"))) { any = inspect(d, out, exclude) || any; break; }
		}
	}
	if (out.data.empty() && !samePath(dir, exclude) && countData(dir, out)) any = true;
	return any;
}

vector<fs::path> searchFolders() {
	vector<fs::path> roots;
	std::error_code ec;
	addIfDir(roots, fs::current_path(ec));
	fs::path h = home();
	if (!h.empty()) {
		addIfDir(roots, h);
		for (const char* sub : { "Desktop", "Documents", "Downloads", "source/repos", "OneDrive/Desktop", "OneDrive/Documents" })
			addIfDir(roots, h / fs::path(sub).make_preferred());
		// Visual Studio 2013/2015/...'s default project folders, where the 2016 solution would have been opened.
		for (fs::directory_iterator it(h / "Documents", ec), end; !ec && it != end; it.increment(ec))
			if (startsWith(lower(it->path().filename().string()), "visual studio")) addIfDir(roots, it->path() / "Projects");
	}
	for (const fs::path& v : volumes()) addIfDir(roots, v);
	// Inside each, one level down: the download / clone (VideoSpeedTracker, VideoSpeedTracker-master) and the data folder.
	vector<fs::path> out = roots;
	for (const fs::path& r : roots) {
		std::error_code e;
		for (fs::directory_iterator it(r, e), end; !e && it != end; it.increment(e)) {
			string n = lower(it->path().filename().string());
			if ((startsWith(n, "videospeedtracker") || n == "locustdata") && it->is_directory(e)) addIfDir(out, it->path());
		}
	}
	return out;
}

Setup scan(const vector<fs::path>& folders, const fs::path& exclude) {
	vector<fs::path> where = folders.empty() ? searchFolders() : folders;
	Setup best;
	auto score = [](const Setup& s) { return (s.config.empty() ? 0 : 2) + (s.data.empty() ? 0 : 1) + (s.used() ? 1 : 0); };
	for (const fs::path& d : where) {
		Setup s;
		if (!inspect(d, s, exclude)) continue;
		if (score(s) > score(best)) best = s;
		if (score(best) == 4) break;   // a config with its data, used: nothing better to find
	}
	return best;
}

bool copyConfig(const Setup& s, const fs::path& to, string& error, const string& when) {
	error.clear();
	if (s.config.empty()) { error = "No 2016 VST.cfg to copy"; return false; }
	if (isFile(to)) return true;   // made earlier: it may have been changed in VST Studio since, so keep it
	vector<string> src = lines(s.config);
	if (src.empty()) { error = "Can't read " + s.config.string(); return false; }
	std::ostringstream out;
	out << "# VST Studio's copy of your 2016 VideoSpeedTracker settings" << (when.empty() ? "" : ", made " + when) << ", from\n"
		<< "#   " << s.config.string() << "\n"
		<< "# That file is left exactly as it was, so the 2016 program keeps working.  VST Studio changes only this copy.\n";
	if (!s.dataPathPrefix.empty())
		out << "# Your 2016 recordings and results stay in " << (s.data.empty() ? s.dataPathPrefix : s.data.string())
			<< "; new results go to VST Studio's own data folder.\n";
	for (const string& l : src) {
		if (trim(l).empty()) continue;
		string k, v;
		sides(l, k, v);
		if (k == "dataPathPrefix") out << "dataPathPrefix =                # was " << s.dataPathPrefix << ".  Blank: VST Studio's own data folder.\n";
		else out << l << "\n";
	}
	out << "# What the 2016 program assumed: the calibration car drove at the 25 mph limit, filmed at 30 fps.\n"
		<< "CalibrationSpeed = 25\n"
		<< "CalibrationFPS = 30\n";
	std::error_code ec;
	fs::create_directories(to.parent_path(), ec);
	fs::path tmp = to;
	tmp += ".tmp";
	{
		std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
		f << out.str();
		if (!f) { f.close(); fs::remove(tmp, ec); error = "Can't write " + to.string(); return false; }
	}
	fs::rename(tmp, to, ec);
	if (ec) { fs::remove(tmp, ec); error = "Can't write " + to.string(); return false; }
	return true;
}

int copyStats(const Setup& s, const fs::path& dataDir, string& error) {
	error.clear();
	int copied = 0;
	std::error_code ec;
	fs::path stats = dataDir / "Stats";
	fs::create_directories(stats, ec);
	for (const fs::path& src : s.statsFiles) {
		string stem = src.stem().string();
		if (startsWith(lower(stem), "stats_")) stem = stem.substr(6);
		fs::path dst = stats / ("stats_2016_" + stem + ".csv");
		std::error_code e;
		if (isFile(dst) && fs::file_size(dst, e) == fs::file_size(src, e)) continue;   // copied before
		if (!fs::copy_file(src, dst, fs::copy_options::overwrite_existing, e)) {
			if (error.empty()) error = "Couldn't copy " + src.string() + ": " + e.message();
			continue;
		}
		auto t = fs::last_write_time(src, e);
		if (!e) fs::last_write_time(dst, t, e);
		copied++;
	}
	return copied;
}

string rerunStem(const fs::path& statsDir, const string& stem) {
	return isStatsCsv(statsDir / ("stats_" + stem + ".csv")) ? stem + "_rerun" : stem;
}

string dayText(const string& d) {
	static const char* months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	if (d.size() != 8 || !allDigits(d)) return d;
	int m = toInt(d.substr(4, 2)), day = toInt(d.substr(6, 2));
	if (m < 1 || m > 12) return d;
	return string(months[m - 1]) + " " + std::to_string(day) + ", " + d.substr(0, 4);
}

}  // namespace legacy2016
