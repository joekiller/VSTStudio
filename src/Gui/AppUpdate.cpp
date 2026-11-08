// VST Studio's side of updating: when to check, when to ask, and the Updates dialog.  Update.h has where updates come
// from and what is verified; Updater.h runs the work off the GUI thread.
//
// The rules a novice can rely on:
//   * Studio asks first, every time.  A new version opens the Updates dialog once a session (never twice for the same
//     version), and "Skip this version" keeps it from asking about that one again.  Nothing downloads before "Update
//     now", and once it is pressed Studio downloads, verifies, closes, installs and opens again with nothing to click.
//   * A tracking run is never interrupted.  The dialog doesn't open by itself during a run, "Update now" is disabled
//     while one is going (the phone can start a run while the dialog is up), and an update that finishes downloading
//     after a run started waits for the run to end and asks again.
//   * It can be turned off: "Check for updates automatically" in the dialog (the rail's version line opens it).  Off,
//     Studio contacts GitHub only when "Check now" is pressed.  On, it asks GitHub's API once a day, 20 s after launch
//     at the earliest, sending nothing but its version in the User-Agent.
//   * Documentation runs (--shots) and builds that aren't releases never check by themselves.
#include "App.h"
#include "Icons.h"
#include "Media.h"
#include "Theme.h"
#include "UpdateOs.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <cstdio>
#include <ctime>

#ifndef VST_VERSION
#define VST_VERSION "dev"
#endif

using namespace theme;

namespace {

double now() { return glfwGetTime(); }

std::string megabytes(std::uint64_t bytes) {
	char b[32];
	std::snprintf(b, sizeof b, "%.0f MB", double(bytes) / (1024.0 * 1024.0));
	return b;
}

// "2026-11-08T01:39:30Z" -> "November 8, 2026" (UTC, which is near enough for a release date).
std::string dayOf(const std::string& iso) {
	static const char* const months[] = { "January", "February", "March", "April", "May", "June", "July", "August", "September",
		"October", "November", "December" };
	int y = 0, m = 0, d = 0;
	if (std::sscanf(iso.c_str(), "%4d-%2d-%2d", &y, &m, &d) != 3 || m < 1 || m > 12) return iso.substr(0, 10);
	return std::string(months[m - 1]) + " " + std::to_string(d) + ", " + std::to_string(y);
}

bool anyPopupOpen() { return ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel); }

constexpr const char* kDialog = "VST Studio updates";

}  // namespace

void App::updateTick() {
	if (screenshotMode || !persist) return;   // no network from documentation runs or tests
	if (!updater_) updater_ = std::make_unique<Updater>(VST_VERSION);
	double wall = double(std::time(nullptr));
	if (!updater_->busy() && now() > update::kFirstCheckDelaySeconds
		&& update::checkDue(prefs.updateAuto, updater_->running(), prefs.updateChecked, wall)) {
		prefs.updateChecked = wall;   // saved with the other preferences (savePrefsSoon)
		updater_->check(false);
	}
	Updater::Status st = updater_->status();
	bool run = tracker.running();
	const update::Version& latest = st.release.version;
	bool found = st.state == Updater::State::Available || st.state == Updater::State::Ready;
	// Ask once a session per version, never during a run and never over another dialog (the 2016 welcome, a report).
	if (found && !run && offeredUpdate_ != latest.str() && update::shouldOffer(updater_->running(), latest, prefs.updateSkip) && !anyPopupOpen()) {
		offeredUpdate_ = latest.str();
		openUpdate_ = true;
	}
	if (installWhenReady_ && st.state == Updater::State::Failed) installWhenReady_ = false;
	if (installWhenReady_ && st.state == Updater::State::Ready) {
		installWhenReady_ = false;
		if (run) {
			// Started from the phone while it downloaded: the run wins, and the dialog asks again once it has ended.
			offeredUpdate_.clear();
			toast("The update to " + latest.str() + " is downloaded. VST Studio will ask to install it when tracking stops.", c::accent2, ICON_DOWNLOAD);
			return;
		}
		prefs.save();   // the installer closes Studio; nothing of this session should depend on the exit path
		std::string err;
		bool quitNow = false;
		if (!updater_->install(quitNow, err)) toast(err, c::bad, ICON_WARN);
		else if (quitNow) quitRequested = true;
		else updateLaunched_ = true;   // the dialog says Windows is asking for permission
	}
}

// One line under "companion" in the rail's status block: the version, and what the updater is doing.  Clicking it opens
// the Updates dialog.
void App::updateRailLine(ImDrawList* dl, ImVec2 at, float width) {
	std::string text = std::string(ICON_ROTATE "  VST Studio ") + VST_VERSION;
	ImU32 col = c::faint;
	if (updater_) {
		Updater::Status st = updater_->status();
		std::string latest = st.release.version.str();
		switch (st.state) {
		case Updater::State::Checking: text = ICON_ROTATE "  checking for updates…"; break;
		case Updater::State::Available:
			if (update::shouldOffer(updater_->running(), st.release.version, prefs.updateSkip)) { text = ICON_DOWNLOAD "  update " + latest + " available"; col = c::accent2; }
			break;
		case Updater::State::Downloading: {
			int pct = st.total ? int(100.0 * double(st.got) / double(st.total)) : 0;
			text = ICON_DOWNLOAD "  downloading " + latest + "  " + std::to_string(pct) + "%";
			col = c::accent2;
			break;
		}
		case Updater::State::Verifying: text = ICON_CERTIFICATE "  checking the update…"; col = c::accent2; break;
		case Updater::State::Ready: text = ICON_DOWNLOAD "  update " + latest + " ready"; col = c::accent2; break;
		case Updater::State::UpToDate: text += "  " ICON_CHECK; break;
		default: break;
		}
	}
	ImGui::SetCursorScreenPos(at - ImVec2(px(4), px(2)));
	ImGui::InvisibleButton("updates", ImVec2(width, fsz(fonts.caption) + px(4)));
	bool hov = ImGui::IsItemHovered();
	if (ImGui::IsItemClicked() && updater_) openUpdate_ = true;
	if (hov && updater_) ImGui::SetTooltip("Updates: check for a new version, or turn automatic checks on or off.");
	dl->AddText(fonts.caption, fsz(fonts.caption), at, hov && updater_ ? c::text : col, text.c_str());
}

void App::updateDialog() {
	if (openUpdate_) { openUpdate_ = false; ImGui::OpenPopup(kDialog); }
	if (!updater_) return;
	ImGuiViewport* vp = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(std::min(px(600), vp->Size.x - px(40)), 0), ImGuiCond_Always);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(28), px(24)));
	bool open = ImGui::BeginPopupModal(kDialog, nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar);
	ImGui::PopStyleVar();
	if (!open) return;

	using S = Updater::State;
	Updater::Status st = updater_->status();
	const update::Version& running = updater_->running();
	const update::Release& rel = st.release;
	bool run = tracker.running();
	bool newer = rel.version.newerThan(running);
	bool closeNow = false;
	ImGui::PushTextWrapPos(0);

	ImGui::PushStyleColor(ImGuiCol_Text, v4(c::accent2));
	ImGui::PushFont(fonts.icons);
	ImGui::TextUnformatted(newer ? ICON_DOWNLOAD : ICON_ROTATE);
	ImGui::PopFont();
	ImGui::PopStyleColor();
	ImGui::PushFont(fonts.title);
	if (newer) ImGui::Text("VST Studio %s is out", rel.version.str().c_str());
	else ImGui::TextUnformatted("VST Studio updates");
	ImGui::PopFont();
	if (running.valid) dimText("You have %s.%s", running.str().c_str(), newer && !rel.published.empty() ? (" The new one came out " + dayOf(rel.published) + ".").c_str() : "");
	else dimText("This is a test build (%s), so it doesn't update itself. Releases do.", VST_VERSION);
	ImGui::Dummy(ImVec2(0, px(8)));

	// What's new, from the release's own notes.
	if (newer) {
		std::string notes = update::notesPreview(rel.notes, 12);
		if (!notes.empty()) {
			sectionLabel("WHAT'S NEW");
			ImGui::PushStyleColor(ImGuiCol_ChildBg, v4(c::bg1));
			// As tall as the notes (scrolling past about eight lines), so one change doesn't sit in an empty box.
			float pad = ImGui::GetStyle().WindowPadding.y * 2 + px(4);
			float textH = ImGui::CalcTextSize(notes.c_str(), nullptr, false, ImGui::GetContentRegionAvail().x - ImGui::GetStyle().WindowPadding.x * 2).y;
			ImGui::BeginChild("notes", ImVec2(0, std::min(px(190), textH + pad)), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders);
			ImGui::PopStyleColor();
			ImGui::TextWrapped("%s", notes.c_str());
			ImGui::EndChild();
			ImGui::Dummy(ImVec2(0, px(4)));
		}
	}

	// Where things stand.
	switch (st.state) {
	case S::Idle:
		dimText("VST Studio hasn't checked yet.");
		break;
	case S::Checking:
		ImGui::TextUnformatted(ICON_HOURGLASS "  Checking GitHub for a newer version…");
		break;
	case S::UpToDate:
		ImGui::PushStyleColor(ImGuiCol_Text, v4(c::ok));
		ImGui::Text(ICON_CHECK_CIRCLE "  You're up to date: %s is the newest release.", rel.version.str().c_str());
		ImGui::PopStyleColor();
		break;
	case S::Downloading: {
		float frac = st.total ? float(double(st.got) / double(st.total)) : 0.f;
		std::string label = megabytes(st.got) + (st.total ? " of " + megabytes(st.total) : std::string());
		ImGui::TextUnformatted(ICON_DOWNLOAD "  Downloading…");
		ImGui::ProgressBar(frac, ImVec2(-1, 0), label.c_str());
		break;
	}
	case S::Verifying:
		ImGui::TextUnformatted(ICON_CERTIFICATE "  Checking that the download is genuine…");
		break;
	case S::Failed:
		ImGui::PushStyleColor(ImGuiCol_Text, v4(c::warn));
		ImGui::TextWrapped(ICON_WARN "  %s", st.error.c_str());
		ImGui::PopStyleColor();
		if (st.manual || newer) dimText("Nothing was changed. You can try again now or later; VST Studio keeps working as it is.");
		break;
	default: break;
	}
	if (newer && (st.state == S::Available || st.state == S::Ready || st.state == S::Failed)) {
		if (!st.canInstall) {
			ImGui::PushStyleColor(ImGuiCol_Text, v4(c::dim));
			ImGui::TextWrapped(ICON_INFO "  %s", st.cantInstall.c_str());
			ImGui::PopStyleColor();
		}
		else if (st.state != S::Failed) {
			std::string size = rel.package.size ? megabytes(rel.package.size) : std::string("the new version");
			dimText("Updating takes a minute or two: VST Studio downloads %s, checks it's genuine (signed by the same developer as "
				"this copy), closes, installs it and opens again. Your settings, calibration, videos and results stay as they are.", size.c_str());
		}
		if (run && st.canInstall) {
			ImGui::PushStyleColor(ImGuiCol_Text, v4(c::warn));
			ImGui::TextWrapped(ICON_GAUGE "  Tracking is running. Updating never interrupts a run: stop tracking first, or choose Later.");
			ImGui::PopStyleColor();
		}
	}
	ImGui::Dummy(ImVec2(0, px(12)));

	// Buttons for the state.
	bool working = st.state == S::Checking || st.state == S::Downloading || st.state == S::Verifying;
	if (newer && st.canInstall && (st.state == S::Available || st.state == S::Ready || (st.state == S::Failed && rel.package.found()))) {
		if (updateLaunched_) {
			ImGui::PushStyleColor(ImGuiCol_Text, v4(c::accent2));
			ImGui::TextWrapped(ICON_LOCK "  Windows is asking for permission to update VST Studio for everyone. Once you allow it, "
				"VST Studio closes, updates and opens again. If you said no, nothing changed; Update now asks again.");
			ImGui::PopStyleColor();
			ImGui::Dummy(ImVec2(0, px(4)));
		}
		ImGui::BeginDisabled(run);
		const char* label = st.state == S::Failed ? ICON_ROTATE "  Try again" : ICON_DOWNLOAD "  Update now";
		if (primaryButton(label)) {
			installWhenReady_ = true;
			updateLaunched_ = false;
			if (st.state != S::Ready) updater_->download();
		}
		ImGui::EndDisabled();
		if (run && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Stop tracking first");
		ImGui::SameLine();
	}
	else if (newer && !st.canInstall && !working) {
		if (primaryButton(ICON_EXTERNAL "  Open the download page")) openWithSystem(rel.page.empty() ? update::kReleasesPage : rel.page);
		ImGui::SameLine();
	}
	if (st.state == S::Downloading) {
		if (ghostButton(ICON_XMARK "  Cancel")) updater_->cancel();
	}
	else if (st.state == S::Verifying || installWhenReady_) {
		ImGui::BeginDisabled();
		ghostButton("Later");
		ImGui::EndDisabled();
	}
	else {
		if (ghostButton(newer ? "Later" : "Close")) closeNow = true;
		if (newer) {
			ImGui::SameLine();
			if (ghostButton("Skip this version")) {
				prefs.updateSkip = rel.version.str();
				prefs.save();
				toast("VST Studio won't ask about " + rel.version.str() + " again. The next version will ask as usual.", c::accent2, ICON_INFO);
				closeNow = true;
			}
		}
	}

	ImGui::Dummy(ImVec2(0, px(10)));
	ImGui::Separator();
	ImGui::Dummy(ImVec2(0, px(4)));
	if (ImGui::Checkbox("Check for updates automatically", &prefs.updateAuto)) prefs.save();
	ImGui::SameLine();
	helpMarker("Once a day VST Studio asks GitHub whether there's a newer release, and asks you before installing it. "
		"Nothing is downloaded until you say so, and a tracking run is never interrupted. Off: it checks only when you press Check now.");
	ImGui::SameLine(0, px(16));
	ImGui::BeginDisabled(working || installWhenReady_ || !running.valid);
	if (ghostButton(ICON_ROTATE "  Check now")) {
		updater_->check(true);
		prefs.updateChecked = double(std::time(nullptr));
	}
	ImGui::EndDisabled();
	if (!running.valid && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Test builds don't update; install a release");

	ImGui::PopTextWrapPos();
	if (closeNow) ImGui::CloseCurrentPopup();
	ImGui::EndPopup();
}
