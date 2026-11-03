// The bundled sample: the 2016 Locust Avenue clip VST was built on, already marked and calibrated by its own VST.cfg.
// For a newcomer it is the "see it work first" path, one click from every screen that would otherwise be empty
// (What you need, Live tracking and Results before anything is connected, and Connect the camera), so the first
// thing they see is cars getting speeds rather than five steps of setup.
//
// The sample is not the user's street, and nothing about it may pass for theirs:
//   * Its runs keep their results in a Sample folder inside the data folder, so its 2016 cars never join "All saved
//     runs" or a street report about the user's own street.
//   * It runs with its own settings once the user has marked a street of their own (sampleOwnSettings()), so trying
//     it later can't time it on their lines.  Before that their VST.cfg is a copy of the same template.
//   * It doesn't check steps 3-5 (Connect the camera, Mark your street, Calibrate): those mean "done for your
//     camera".  Its calibration still counts for running it (calibrationDone()), so it never nags.
//   * While it's on screen, Live tracking says so and offers the way on: the first setup step not done yet.
#include "App.h"
#include "Icons.h"
#include "Theme.h"
#include "UserPaths.h"
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;
using namespace theme;

bool App::isSample(const std::string& s) {
	if (s.empty()) return false;
	if (sampleCache_.empty()) sampleCache_ = findSample();
	return s == sampleCache_;
}

// The sample's own settings: the installed VST.cfg template (the file every new per-user VST.cfg is seeded from), or
// in a source checkout bin/VST.cfg beside sampleData/.  Loaded once; empty when neither is there, and then the sample
// runs with the user's VST.cfg as it always did.
static std::string sampleConfigPath(const std::string& sample) {
	if (fs::path t = userpaths::installedFile("VST.cfg"); !t.empty()) return t.string();
	std::error_code ec;
	fs::path checkout = fs::path(sample).parent_path().parent_path().parent_path().parent_path();   // .../sampleData/IPCam/<date>/x.avi
	fs::path bin = checkout / "bin" / "VST.cfg";
	return fs::is_regular_file(bin, ec) ? bin.string() : std::string();
}

bool App::sampleOwnSettings() {
	if (prefs.zonesFor.empty() || isSample(prefs.zonesFor)) return false;
	if (sampleCfg_.path().empty()) {
		std::string path = sampleConfigPath(findSample());
		if (path.empty() || !sampleCfg_.load(path)) return false;
	}
	return true;
}

Globals& App::liveG() {
	return sampleRunOwn_ && !sampleCfg_.path().empty() && isSample(source) ? sampleCfg_.g : cfg.g;
}

void App::trySample() {
	std::string sample = findSample();
	if (sample.empty() || tracker.running()) { page = Page::Track; return; }
	batch.clear();
	if (source != sample || !player.isOpen()) openSource(sample);
	page = Page::Track;
	sampleStart_ = true;
}

// Called every frame: starts the run trySample() asked for once the video has opened, and gives up quietly if
// something else was opened meanwhile (openSource() has already said why if the sample itself failed).
void App::sampleStartPump() {
	if (!sampleStart_) return;
	if (!isSample(source) || !batch.empty() || tracker.running()) { sampleStart_ = false; return; }
	if (player.opening() || !player.isOpen()) return;
	sampleStart_ = false;
	startTracking();
}

int App::firstOpenStep() {
	for (int i = 0; i < kFlowSteps; i++)
		if (!flowDone(i)) return i;
	return -1;
}

// "This is the sample" with the way on.  On Live tracking it sits above the counts; on Connect the camera it replaces
// the plain "Use the sample" row once the sample is open, offering the run instead of opening it again.
void App::sampleNote(float width, bool onLive) {
	// What you need only informs, and is where "Watch the sample" usually came from, so the way on starts at step 2.
	int next = std::max(firstOpenStep(), 1);
	beginCard(onLive ? "samplenote" : "sample", ICON_MAGIC, onLive ? "This is the sample, not your street" : nullptr, ImVec2(width, 0));
	ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
	if (onLive) {
		dimText("Locust Avenue, Charlottesville, January 2016, with the settings it was measured with. Its results are kept "
			"apart from yours, in a Sample folder, so they never show up in your street's report.");
		ImGui::PopTextWrapPos();
		ImGui::Dummy(ImVec2(0, px(4)));
		if (firstOpenStep() >= 0) {
			std::string go = std::string("Set up my street: ") + kFlow[next].name + "  " ICON_ARROW_RIGHT;
			if (primaryButton(go.c_str(), ImVec2(-FLT_MIN, 0))) goFlow(next);
		}
	}
	else {
		ImGui::TextUnformatted("The 2016 Locust Avenue sample is open. It comes marked and calibrated, so it's ready to measure.");
		ImGui::PopTextWrapPos();
		dimText("Watch it, or connect your own camera above whenever you're ready.");
		ImGui::Dummy(ImVec2(0, px(2)));
		if (primaryButton(tracker.running() ? ICON_GAUGE "  Watch it on Live tracking" : ICON_PLAY "  Watch it measure")) trySample();
	}
	endCard();
}

// Steps 4 and 5 with the sample open: one line saying what the page means here, since neither step is checked for it.
// The sample needs neither (it came marked and calibrated); the page edits your own VST.cfg, which is harmless
// practice until you've marked a street of your own, and after that would move your marks, so it says so.
void App::sampleStepNote(bool calibrate) {
	if (!isSample(source) || screenshotMode) return;
	bool yours = sampleOwnSettings();
	ImGui::PushStyleColor(ImGuiCol_Text, v4(yours ? c::warn : c::accent2));
	ImGui::PushTextWrapPos(0);
	if (yours)
		ImGui::TextWrapped(ICON_WARN "  This is the 2016 sample, which tracks with its own settings. Changes here go into your own street's %s, "
			"so connect your camera (step 3) before you change anything.", calibrate ? "calibration" : "marks");
	else
		ImGui::TextWrapped(ICON_MAGIC "  This is the 2016 sample: it came %s, so it's ready to run as it is. Look around or practise here; "
			"you'll do this for real on your own camera.", calibrate ? "calibrated" : "marked");
	ImGui::PopTextWrapPos();
	ImGui::PopStyleColor();
}

// What you need, under the title: the first thing a newcomer can do is watch it work.  The sample is already marked
// and calibrated, so one click opens it on Live tracking and starts measuring; the five steps wait below.
void App::sampleTryCard(float width) {
	if (findSample().empty() || screenshotMode) return;
	ImGui::Dummy(ImVec2(0, px(4)));
	beginCard("trysample", ICON_PLAY, "See it work first", ImVec2(width, 0));
	bool running = tracker.running() && isSample(source);
	const char* label = running ? ICON_GAUGE "  Watch it on Live tracking" : ICON_PLAY "  Watch the sample measure";
	float bw = buttonWidth(label);
	float textW = ImGui::GetContentRegionAvail().x - bw - px(20);
	ImGui::BeginGroup();
	ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + textW);
	dimText("No camera yet? Watch VST time real traffic on the 2016 Locust Avenue video it was built on. It's already set up, "
		"so there's nothing to do: one click and the cars get speeds. Come back here when you're ready to set up your own street.");
	ImGui::PopTextWrapPos();
	ImGui::EndGroup();
	ImGui::SameLine(0, px(20));
	if (primaryButton(label, ImVec2(bw, 0))) trySample();
	endCard();
	ImGui::Dummy(ImVec2(0, px(4)));
}
