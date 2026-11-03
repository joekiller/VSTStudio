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
	// The May 2016 reader (neighborhoods branch) skips blank lines anywhere, and an editor may add one at the end:
	// still theirs.  Comments and extra keys are not: no 2016 reader accepted them.
	ls.erase(std::remove_if(ls.begin(), ls.end(), [](const string& l) { return trim(l).empty(); }), ls.end());
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

namespace {

// Where a folder the 2016 setup names by `prefix` (e.g. "g:\locustdata" or "g:\VSTData") is now, as accept() judges
// it: as written when that exists (Windows), else a folder with its last name beside the config or in any folder
// above it, in the home folder or Documents, or at the root of another drive or volume (a setup copied from the old
// PC, or a drive that now has another letter).
template <class Accept>
fs::path locate(const string& prefixIn, const fs::path& configDir, const fs::path& exclude, Accept accept) {
	string prefix = unquote(trim(prefixIn));
	// The 2016 comment says "Use double back-slash" and Globals.h says not to: either way it means one.
	for (size_t at; (at = prefix.find("\\\\")) != string::npos;) prefix.erase(at, 1);
	if (prefix.empty()) return {};
#if defined(_WIN32)
	if (isDir(fs::path(prefix)) && !samePath(prefix, exclude) && accept(fs::path(prefix))) return fs::path(prefix);
#else
	// "g:\locustdata" names nothing on a Mac or Linux disk; a POSIX path from a later setup might.
	if (prefix.find('\\') == string::npos && prefix.find(':') == string::npos && isDir(fs::path(prefix)) && !samePath(prefix, exclude)
		&& accept(fs::path(prefix)))
		return fs::path(prefix);
#endif
	string leaf = prefix;
	while (!leaf.empty() && (leaf.back() == '\\' || leaf.back() == '/')) leaf.pop_back();
	size_t cut = leaf.find_last_of("\\/:");
	if (cut != string::npos) leaf = leaf.substr(cut + 1);
	if (leaf.empty()) return {};
	vector<fs::path> bases;
	// The config's folder and each one above it: a clone's x64\Release is four levels under the clone, and a
	// VSTData folder copied into the clone sits at its root.
	for (fs::path d = configDir; !d.empty(); d = d.parent_path()) {
		bases.push_back(d);
		if (d == d.parent_path()) break;
	}
	bases.push_back(home());
	bases.push_back(home() / "Documents");
	for (const fs::path& v : volumes()) bases.push_back(v);
	for (const fs::path& base : bases) {
		if (base.empty()) continue;
		fs::path d = child(base, leaf);
		if (!d.empty() && isDir(d) && !samePath(d, exclude) && accept(d)) return d;
	}
	return {};
}

// How complete a found setup is: a config (2), its data (1), used (1); the creator's own street breaks a tie.
int score(const Setup& s) {
	return 2 * ((s.config.empty() ? 0 : 2) + (s.data.empty() ? 0 : 1) + (s.used() ? 1 : 0)) + (s.creator() ? 1 : 0);
}

// A folder's own name, also for "G:\VSTData\LocustData\" (whose filename() is empty).
string nameOf(fs::path d) {
	if (d.filename().empty()) d = d.parent_path();
	return d.filename().string();
}

// Fills `out` from a 2016 config: where it is, the files beside it, and the values the welcome quotes.
void takeConfig(const fs::path& cfg, Setup& out) {
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
	fs::path dir = cfg.parent_path();
	fs::path hl = child(dir, "ProcessHiLites.cfg");
	if (hl.empty()) hl = child(dir, nameOf(dir).substr(0, nameOf(dir).size() >= 4 ? nameOf(dir).size() - 4 : 0) + "HiLites.cfg");
	if (isFile(hl)) out.hiLitesConfig = hl;
	fs::path exe = child(dir, "VideoSpeedTracker.exe");
	if (isFile(exe)) out.program = exe;
}

// neighborhoods.cfg (May 2016, pfr/VideoSpeedTracker's neighborhoods branch): "dataPathPrefix = g:\VSTData #" and
// then one "Locust Avenue = Locust #" line per street.  Street <Short> keeps its data and its <Short>Data.cfg in
// <dataPathPrefix>\<Short>Data.  Returns the prefix and the short names, in the file's order (the 2016 default is 1).
bool readNeighborhoods(const fs::path& file, string& prefix, vector<string>& streets) {
	vector<string> ls = lines(file);
	if (ls.empty()) return false;
	string k, v;
	sides(ls[0], k, v);
	if (k != "dataPathPrefix") return false;
	prefix = v;
	for (size_t i = 1; i < ls.size(); i++) {
		if (trim(ls[i]).empty() || ls[i].find('=') == string::npos) continue;
		sides(ls[i], k, v);
		if (!v.empty()) streets.push_back(v);
	}
	return true;
}

// Each street folder (<Short>Data, with its own <Short>Data.cfg or 2016 data) in `root`, e.g. G:\VSTData.
vector<fs::path> streetFolders(const fs::path& root, const vector<string>& streets) {
	vector<fs::path> out;
	if (!streets.empty()) {
		for (const string& st : streets) {
			fs::path d = child(root, st + "Data");
			if (isDir(d)) out.push_back(d);
		}
		return out;
	}
	std::error_code ec;
	for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
		string n = lower(it->path().filename().string());
		if (n.size() > 4 && n.compare(n.size() - 4, 4, "data") == 0 && it->is_directory(ec)) out.push_back(it->path());
	}
	std::sort(out.begin(), out.end());
	return out;
}

}  // namespace

fs::path findDataFolder(const string& dataPathPrefix, const fs::path& configDir, const fs::path& exclude) {
	return locate(dataPathPrefix, configDir, exclude, [](const fs::path& d) { Setup scratch; return countData(d, scratch); });
}

namespace {

// inspect(), with `streets` saying whether `dir` may also be a folder of street folders (one level only: a street
// folder is never searched for streets of its own, so a home folder's AppData isn't walked).
bool inspectAt(const fs::path& dir, Setup& out, const fs::path& exclude, bool streets) {
	if (!isDir(dir)) return false;
	bool any = false;
	// The config: VST.cfg (Feb-Mar 2016, read from the folder the program ran in), or a street's own
	// <Short>Data.cfg in its data folder (May 2016, LocustData\LocustData.cfg).
	if (out.config.empty()) {
		fs::path cfg = child(dir, "VST.cfg");
		bool own = false;
		if (!isConfig(cfg)) { cfg = child(dir, nameOf(dir) + ".cfg"); own = true; }
		if (isConfig(cfg)) {
			takeConfig(cfg, out);
			if (out.data.empty()) {
				// A street's own config sits in its data folder, which the 2016 program used whatever its first
				// line said (neighborhoods.cfg's prefix wins).  Otherwise the data folder is the one it names.
				fs::path d = own ? dir : findDataFolder(out.dataPathPrefix, dir, exclude);
				if (own && samePath(d, exclude)) d.clear();
				if (!d.empty() && !countData(d, out)) out.data = d;   // the folder it names, even before any recording
			}
			any = true;
		}
	}
	// Several streets (May 2016): neighborhoods.cfg here, beside the program, or a folder of <Short>Data streets
	// such as G:\VSTData.  The 2016 program asked which street at every run, so each is a setup of its own; the most
	// complete is taken (the creator's Locust Avenue on a tie, else the first listed, the 2016 default).
	if (streets && out.config.empty() && out.data.empty()) {
		fs::path hoods = child(dir, "neighborhoods.cfg");
		string prefix;
		vector<string> names;
		fs::path root;
		if (isFile(hoods) && readNeighborhoods(hoods, prefix, names))
			root = locate(prefix, dir, exclude, [&](const fs::path& r) { return !streetFolders(r, names).empty(); });
		else if (!samePath(dir, exclude))
			root = dir;
		bool listed = !names.empty();
		Setup best;
		if (!root.empty())
			for (const fs::path& street : streetFolders(root, names)) {
				// Without neighborhoods.cfg, only a folder with its own <Short>Data.cfg counts as a street.
				if (samePath(street, exclude) || (!listed && !isConfig(child(street, nameOf(street) + ".cfg")))) continue;
				Setup s;
				if (inspectAt(street, s, exclude, false) && score(s) > score(best)) best = s;
			}
		if (best.found()) {
			fs::path exe = child(dir, "VideoSpeedTracker.exe");
			if (best.program.empty() && isFile(exe)) best.program = exe;
			out = best;
			any = true;
		}
	}
	// A download or clone of the 2016 repository: bin\ and the Visual Studio project folder (master), the Visual
	// Studio 2013 build folders and the VSTData streets (neighborhoods branch).
	if (out.config.empty()) {
		Setup best;
		for (const fs::path& sub : { fs::path("bin"), fs::path("src") / "Wintel64" / "VideoSpeedTracker",
				fs::path("VisualStudio2013_Projects") / "VideoSpeedTracker" / "x64" / "Release",
				fs::path("VisualStudio2013_Projects") / "VideoSpeedTracker" / "x64" / "Debug", fs::path("VSTData") }) {
			fs::path d = dir / sub;
			Setup s;
			if (isDir(d) && inspectAt(d, s, exclude, sub == "VSTData") && !s.config.empty() && score(s) > score(best)) best = s;
		}
		if (best.found()) { out = best; any = true; }
	}
	if (out.data.empty() && !samePath(dir, exclude) && countData(dir, out)) any = true;
	return any;
}

}  // namespace

bool inspect(const fs::path& dir, Setup& out, const fs::path& exclude) { return inspectAt(dir, out, exclude, true); }

vector<fs::path> searchFolders() {
	vector<fs::path> roots;
	std::error_code ec;
	addIfDir(roots, fs::current_path(ec));
	fs::path h = home();
	if (!h.empty()) {
		addIfDir(roots, h);
		for (const char* sub : { "Desktop", "Documents", "Downloads", "source/repos", "OneDrive/Desktop", "OneDrive/Documents",
				// GitHub for Windows (2016) and GitHub Desktop clone into Documents\GitHub; some keep a GitHub folder at home.
				"Documents/GitHub", "GitHub", "OneDrive/Documents/GitHub" })
			addIfDir(roots, h / fs::path(sub).make_preferred());
		// Visual Studio 2013/2015/...'s default project folders, where the 2016 solution would have been opened.
		for (fs::directory_iterator it(h / "Documents", ec), end; !ec && it != end; it.increment(ec))
			if (startsWith(lower(it->path().filename().string()), "visual studio")) addIfDir(roots, it->path() / "Projects");
	}
	for (const fs::path& v : volumes()) addIfDir(roots, v);
	// Inside each, one level down: the download / clone (VideoSpeedTracker, VideoSpeedTracker-master) and the data folder
	// (locustdata, or VSTData with a folder per street from May 2016).
	vector<fs::path> out = roots;
	for (const fs::path& r : roots) {
		std::error_code e;
		for (fs::directory_iterator it(r, e), end; !e && it != end; it.increment(e)) {
			string n = lower(it->path().filename().string());
			if ((startsWith(n, "videospeedtracker") || n == "locustdata" || n == "vstdata") && it->is_directory(e)) addIfDir(out, it->path());
		}
	}
	return out;
}

Setup scan(const vector<fs::path>& folders, const fs::path& exclude) {
	vector<fs::path> where = folders.empty() ? searchFolders() : folders;
	Setup best;
	for (const fs::path& d : where) {
		Setup s;
		if (!inspect(d, s, exclude)) continue;
		if (score(s) > score(best)) best = s;
		if (score(best) == 9) break;   // the creator's config with its data, used: nothing better to find
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
