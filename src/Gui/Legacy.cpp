// Working on top of a 2016 VideoSpeedTracker setup (Legacy2016.h): the background scan, the welcome, picking up
// where it left off, and keeping its VST.cfg exactly as it is.
//
// The flow:
//   init()            starts legacy2016::scan() on a thread (a few directory listings, never a write)
//   legacyPump()      each frame, picks the result up once; a setup whose welcome hasn't been answered opens it
//   welcome           "Hello, Professor." for the creator's own Locust Avenue setup (Setup::creator), "Welcome back."
//                     for anyone else's used setup, "Found a 2016 VideoSpeedTracker" for a download never run; what was
//                     found, in numbers; the read-only promise; Pick up where I left off / Not now
//   importLegacy()    copies the VST.cfg (legacy2016::copyConfig) and the stats csvs (copyStats) into VST Studio's own
//                     folders, honours the 2016 calibration and zones, opens the latest recording where it is, and shows
//                     every saved run in Results, 2016 ones included
//   legacyCopyOnWrite()  a 2016 VST.cfg Studio opened (from the working directory, say) is read only; the first edit
//                     carries on in the copy, so the 2016 program, whose reader refuses any line it doesn't expect,
//                     keeps working
// portable-file-dialogs pulls in the Windows headers; they must be parsed before Globals.h's "using namespace std".
#include <portable-file-dialogs.h>
#include "App.h"
#include "Icons.h"
#include "Theme.h"
#include "UserPaths.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace theme;

namespace {

const char* kWelcome = "##legacy-welcome";

// 2140 -> "2,140"
std::string thousands(long long n) {
	std::string s = std::to_string(n);
	for (int i = (int)s.size() - 3; i > 0; i -= 3) s.insert((size_t)i, ",");
	return s;
}

std::string plural(long long n, const char* one, const char* many) { return thousands(n) + " " + (n == 1 ? one : many); }

bool samePath(const std::string& a, const std::string& b) {
	if (a.empty() || b.empty()) return false;
	std::error_code ec;
	return fs::weakly_canonical(a, ec) == fs::weakly_canonical(b, ec);
}

// The second line of a copy made by copyConfig() names the 2016 file it came from.
bool copiedFrom(const fs::path& copy, const fs::path& original) {
	std::ifstream in(copy);
	std::string l1, l2;
	std::getline(in, l1);
	std::getline(in, l2);
	return l2.find(original.string()) != std::string::npos;
}

// One icon row of the welcome's "what we found": a bold head and a dim detail under it.
void foundRow(const char* icon, ImU32 col, const std::string& head, const std::string& detail) {
	float x0 = ImGui::GetCursorPosX();
	ImGui::PushStyleColor(ImGuiCol_Text, v4(col));
	ImGui::TextUnformatted(icon);
	ImGui::PopStyleColor();
	ImGui::SameLine(x0 + px(30));
	ImGui::BeginGroup();
	ImGui::PushFont(fonts.strong);
	ImGui::TextWrapped("%s", head.c_str());
	ImGui::PopFont();
	if (!detail.empty()) dimText("%s", detail.c_str());
	ImGui::EndGroup();
	ImGui::Dummy(ImVec2(0, px(2)));
}

}  // namespace

std::string App::legacyCopyPath() const {
	// One copy per 2016 VST.cfg: VST-2016.cfg, or VST-2016-2.cfg, ... when an earlier one came from another file.
	fs::path dir = userpaths::configDir();
	for (int n = 1; n < 100; n++) {
		fs::path p = dir / (n == 1 ? std::string("VST-2016.cfg") : "VST-2016-" + std::to_string(n) + ".cfg");
		std::error_code ec;
		if (!fs::exists(p, ec) || legacy.config.empty() || copiedFrom(p, legacy.config)) return p.string();
	}
	return (dir / "VST-2016.cfg").string();
}

void App::legacyPump() {
	if (legacyScanned_ || !legacyScan_.valid()) return;
	if (legacyScan_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
	legacy = legacyScan_.get();
	legacyScanned_ = true;
	if (legacy.found() && legacy.key() != prefs.legacySeen && legacy.key() != prefs.legacyImported) legacyWelcome_ = true;
}

void App::legacyCopyOnWrite() {
	// The setup this file belongs to: the scan's when it found this very file, else looked at again from its folder.
	legacy2016::Setup s = legacy;
	if (s.config.empty() || !samePath(s.config.string(), cfg.path())) {
		s = legacy2016::Setup();
		legacy2016::inspect(fs::path(cfg.path()).parent_path(), s, userpaths::dataDir());
		if (legacy.config.empty()) legacy = s;
	}
	std::string original = cfg.path();
	std::string to = legacyCopyPath();
	std::string err;
	if (!legacy2016::copyConfig(s, to, err, calib::nowIso().substr(0, 10)) || !cfg.moveTo(to)) {
		toast(err.empty() ? cfg.error : err, c::bad, ICON_WARN);
		autosaveAt_ = 0;
		return;
	}
	// Results go to VST Studio's own data folder, as the copy says, unless the user picked a folder themselves.
	if (cfg.g.dataPathPrefix == s.dataPathPrefix) cfg.g.dataPathPrefix.clear();
	prefs.configPath = to;
	if (prefs.calibratedFor == original) prefs.calibratedFor = to;
	if (cfg.save()) savedAt_ = glfwGetTime();   // the clock App.cpp's now() reads
	toast("Your 2016 VST.cfg stays exactly as it was, so the 2016 program keeps working. Your change is saved in a copy, "
		+ fs::path(to).filename().string() + ", which VST Studio uses from now on.", c::ok, ICON_LOCK);
}

bool App::importLegacy() {
	if (tracker.running()) { toast("Stop tracking first, then pick up your 2016 setup", c::warn, ICON_WARN); return false; }
	legacyError_.clear();
	std::string err;
	if (!legacy.config.empty()) {
		if (cfg.legacy2016() && samePath(cfg.path(), legacy.config.string())) {
			legacyCopyOnWrite();   // Studio already has it open (read only): carry on in the copy, edits and all
			if (cfg.legacy2016()) { legacyError_ = "Couldn't make the copy of your settings"; return false; }
		}
		else {
			std::string to = legacyCopyPath();
			if (!legacy2016::copyConfig(legacy, to, err, calib::nowIso().substr(0, 10))) { legacyError_ = err; return false; }
			if (!loadConfig(to)) { legacyError_ = cfg.error; return false; }
		}
		// The 2016 calibration was counted on this very street: it stands, as "calibrated, not checked".
		prefs.calibratedFor = cfg.path();
		prefs.calibratedLines = speedLinesSig();
	}
	int copied = 0;
	if (!legacy.statsFiles.empty()) {
		copied = legacy2016::copyStats(legacy, dataDir(), err);
		if (!err.empty()) toast(err, c::warn, ICON_WARN);
	}
	if (!legacy.latestRecording.empty()) {
		std::string rec = legacy.latestRecording.string();
		// The zones in the 2016 VST.cfg were drawn on this camera's picture, so Mark your street counts as done for it.
		if (!legacy.config.empty()) { prefs.zonesFor = rec; prefs.zonesFrame = "1280x720"; }
		if (source.empty()) openSource(rec);
		else prefs.addRecent(rec);
	}
	prefs.legacyImported = prefs.legacySeen = legacy.key();
	prefs.legacyData = legacy.data.string();
	if (legacy.creator() && prefs.siteName == "My street") prefs.siteName = "Locust Avenue";
	syncSets(true);
	showSavedRuns();
	page = Page::Results;
	std::string what = legacy.config.empty() ? std::string() : "your settings";
	if (!legacy.statsFiles.empty()) what += std::string(what.empty() ? "" : " and ") + "your 2016 results";
	toast((what.empty() ? std::string("Picked up your 2016 setup") : "Picked up " + what)
		+ (copied ? " (" + plural(copied, "file", "files") + " copied)" : "") + ". Nothing in the 2016 setup was changed.", c::ok, ICON_CHECK_CIRCLE);
	return true;
}

void App::legacyWelcomeModal() {
	if (legacyWelcome_) { ImGui::OpenPopup(kWelcome); legacyWelcome_ = false; }
	ImGuiViewport* vp = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(std::min(px(640), vp->Size.x - px(40)), 0), ImGuiCond_Always);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(28), px(24)));
	bool open = ImGui::BeginPopupModal(kWelcome, nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar);
	ImGui::PopStyleVar();
	if (!open) return;
	const legacy2016::Setup& s = legacy;
	bool picked = !prefs.legacyImported.empty() && prefs.legacyImported == s.key();
	ImGui::PushTextWrapPos(0);

	// The greeting: the creator is recognized by his street and his data; anyone else who ran it is welcomed back.
	const char* hello = s.creator() ? "Hello, Professor." : s.used() ? "Welcome back." : "Found a 2016 VideoSpeedTracker";
	const char* sub = s.creator() ? "VST Studio recognized your Locust Avenue setup from 2016."
		: s.used() ? "VST Studio found your setup from the 2016 VideoSpeedTracker."
		: "VST Studio found the original 2016 program and its settings on this computer.";
	ImGui::PushStyleColor(ImGuiCol_Text, v4(c::accent2));
	ImGui::PushFont(fonts.icons);
	ImGui::TextUnformatted(ICON_GAUGE);
	ImGui::PopFont();
	ImGui::PopStyleColor();
	ImGui::PushFont(fonts.title);
	ImGui::TextUnformatted(hello);
	ImGui::PopFont();
	ImGui::TextUnformatted(sub);
	ImGui::Dummy(ImVec2(0, px(6)));
	if (s.creator())
		dimText("VideoSpeedTracker began with your camera on Locust Avenue, and VST Studio is built on it: the predictive "
			"bumper tracker, the calibration by frames, the stats files. Thank you for making it, and for sharing it.");
	else
		dimText("VST Studio grew out of Paul Reynolds' 2016 VideoSpeedTracker: the same tracker and the same calibration, "
			"with a guided setup, live cameras, a phone companion and street reports added.");
	ImGui::Dummy(ImVec2(0, px(10)));

	sectionLabel("WHAT'S HERE");
	if (!s.config.empty()) {
		std::string head = "Your settings";
		if (!s.l2r.empty() && !s.r2l.empty()) head += ": cars heading " + s.l2r + " and " + s.r2l;
		std::string detail = s.config.string();
		if (s.calL2R > 0 && s.calR2L > 0)
			detail = "Calibrated at " + std::to_string(s.calL2R) + " and " + std::to_string(s.calR2L) + " frames.  " + detail;
		foundRow(ICON_SLIDERS, c::accent, head, detail);
	}
	if (s.recordings > 0) {
		std::string when = s.firstDay == s.lastDay ? legacy2016::dayText(s.firstDay)
			: legacy2016::dayText(s.firstDay) + " to " + legacy2016::dayText(s.lastDay);
		foundRow(ICON_FILM, c::accent, plural(s.recordings, "recording", "recordings") + " over " + plural(s.days, "day", "days") + ", " + when,
			(s.data / "IPCam").string());
	}
	if (!s.statsFiles.empty())
		foundRow(ICON_TABLE, c::accent, plural((long long)s.statsFiles.size(), "results file", "results files") + ", "
			+ plural(s.statsRows, "vehicle", "vehicles") + " measured", s.statsFiles.front().parent_path().string());
	if (s.highlights > 0) foundRow(ICON_BOLT, c::accent, plural(s.highlights, "highlights reel", "highlights reels"), "");
	if (!s.config.empty() && s.data.empty() && !s.dataPathPrefix.empty())
		foundRow(ICON_WARN, c::warn, "Your data folder, " + s.dataPathPrefix + ", isn't here right now",
			"If it's on another drive, plug it in and use Find my 2016 setup on What you need. Your settings can be picked up without it.");
	ImGui::Dummy(ImVec2(0, px(6)));

	// The promise, in the colour Studio uses for "all good".
	ImGui::PushStyleColor(ImGuiCol_Text, v4(c::ok));
	ImGui::TextWrapped(ICON_LOCK "  Read only. VST Studio never changes, moves or deletes anything in your 2016 setup, "
		"and the 2016 program keeps working exactly as before.");
	ImGui::PopStyleColor();
	ImGui::Dummy(ImVec2(0, px(4)));
	if (!picked) {
		std::string how = "Picking up ";
		std::vector<std::string> parts;
		if (!s.config.empty()) parts.push_back("copies your settings to " + fs::path(legacyCopyPath()).filename().string() + " for VST Studio to use");
		if (!s.statsFiles.empty()) parts.push_back("copies your results into VST Studio's data folder");
		if (!s.latestRecording.empty()) parts.push_back("opens your latest recording from where it is");
		for (size_t i = 0; i < parts.size(); i++) how += (i == 0 ? "" : i + 1 == parts.size() ? ", and " : ", ") + parts[i];
		dimText("%s.", how.c_str());
	}
	if (!legacyError_.empty()) {
		ImGui::PushStyleColor(ImGuiCol_Text, v4(c::bad));
		ImGui::TextWrapped("%s", legacyError_.c_str());
		ImGui::PopStyleColor();
	}
	ImGui::PopTextWrapPos();
	ImGui::Dummy(ImVec2(0, px(12)));

	if (picked) {
		if (primaryButton(ICON_TABLE "  Show my results")) { showSavedRuns(); page = Page::Results; ImGui::CloseCurrentPopup(); }
	}
	else {
		ImGui::BeginDisabled(tracker.running());
		if (primaryButton(ICON_FILE_IMPORT "  Pick up where I left off") && importLegacy()) ImGui::CloseCurrentPopup();
		ImGui::EndDisabled();
		if (tracker.running() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Stop tracking first");
	}
	ImGui::SameLine();
	if (ghostButton(picked ? "Close" : "Not now")) {
		prefs.legacySeen = s.key();   // don't open by itself again; What you need keeps a card for it
		ImGui::CloseCurrentPopup();
	}
	if (!picked) {
		ImGui::SameLine();
		dimText("You can do this later from What you need.");
	}
	ImGui::EndPopup();
}

void App::legacyCard(float width) {
	if (screenshotMode) return;
	const legacy2016::Setup& s = legacy;
	if (!s.found()) {
		// Not found by itself (another drive, an unusual folder): let the user point at it.
		if (!legacyScanned_) return;
		ImGui::Dummy(ImVec2(0, px(4)));
		ImGui::AlignTextToFramePadding();
		dimText(ICON_INFO "  Used the original 2016 VideoSpeedTracker? VST Studio can work on top of that setup without changing it.");
		ImGui::SameLine();
		if (ghostButton(ICON_FOLDER_OPEN "  Find my 2016 setup")) {
			std::string dir = pfd::select_folder("The folder with your 2016 VST.cfg, or your data folder (IPCam, stats)").result();
			if (!dir.empty()) {
				legacy2016::Setup found;
				if (legacy2016::inspect(dir, found, userpaths::dataDir())) { legacy = found; legacyWelcome_ = true; }
				else toast("No 2016 VideoSpeedTracker setup in " + dir + ". Pick the folder that holds VST.cfg, or your data folder (the one with IPCam and stats).", c::warn, ICON_WARN);
			}
		}
		return;
	}
	bool picked = !prefs.legacyImported.empty() && prefs.legacyImported == s.key();
	beginCard("legacy", ICON_FILE_IMPORT, s.creator() ? "Your Locust Avenue setup from 2016" : "Your 2016 VideoSpeedTracker setup", ImVec2(width, 0));
	ImGui::PushTextWrapPos(0);
	std::vector<std::string> bits;
	if (!s.config.empty()) bits.push_back("settings");
	if (s.recordings > 0) bits.push_back(plural(s.recordings, "recording", "recordings"));
	if (!s.statsFiles.empty()) bits.push_back(plural(s.statsRows, "vehicle", "vehicles") + " measured");
	std::string line;
	for (size_t i = 0; i < bits.size(); i++) line += (i ? ", " : "") + bits[i];
	if (picked) linePill(ICON_CHECK "  Picked up", c::ok, alpha(c::ok, .15f));
	else linePill(ICON_LOCK "  Read only", c::accent2, alpha(c::accent2, .15f));
	ImGui::SameLine();
	ImGui::AlignTextToFramePadding();
	std::string text = line.empty() ? std::string("Found") : line;
	text[0] = (char)std::toupper((unsigned char)text[0]);
	dimText("%s.  %s", text.c_str(), picked ? "VST Studio uses a copy; the 2016 files are as they were." : "Nothing there is changed by VST Studio.");
	ImGui::PopTextWrapPos();
	ImGui::Dummy(ImVec2(0, px(2)));
	if (!picked) {
		ImGui::BeginDisabled(tracker.running());
		if (primaryButton(ICON_FILE_IMPORT "  Pick up where I left off")) importLegacy();
		ImGui::EndDisabled();
		ImGui::SameLine();
	}
	else {
		if (primaryButton(ICON_TABLE "  Show my results")) { showSavedRuns(); page = Page::Results; }
		ImGui::SameLine();
	}
	if (ghostButton(ICON_INFO "  What's there")) legacyWelcome_ = true;
	if (!s.data.empty()) {
		ImGui::SameLine();
		if (ghostButton(ICON_FOLDER_OPEN "  Open the 2016 data folder")) openWithSystem(s.data.string());
	}
	endCard();
	ImGui::Dummy(ImVec2(0, px(8)));
}
