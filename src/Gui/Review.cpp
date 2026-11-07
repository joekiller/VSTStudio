// VST Studio: passes reviewed by hand (Exclusions.h).  Joe's case: a shadow (or a person, or the camera shaking) timed as
// a 60 mph "vehicle".  Once someone has looked at it, it shouldn't count.
//
// Flow:  Results' detail card (reviewCard) offers "Not a vehicle? Leave it out" on any pass that counts, and "Put it
// back" on one that is left out; both reach the "Leave this pass out" and "Delete this pass" dialogs (reviewDialogs)
//  ->  reviewPass() does the work for Studio and the companion API alike:
//        LeaveOut  exclusions::exclude in the csv's exclusions.json; Results greys the row and drops it from the tiles
//                  and charts; the street report, archives, the phone and anything published from now on leave it out
//        PutBack   exclusions::restore: it counts again (the record keeps that it was looked at)
//        Delete    exclusions::deleteRow takes its row out of the csv (refused while a published dataset holds it, while
//                  its run is recording, or when the file isn't VST Studio's to change), its clip goes, and the record
//                  keeps one line saying a pass was deleted (when, its speed, why), never the row
//  ->  a published dataset holding the pass can't change, so dataset::recordLater notes the review in its status.json:
//      its cover then says its numbers still count a car found not to be traffic, and offers a corrected copy.
//
// Leaving out is the default because it can be undone and keeps the evidence; deleting is offered second, for test
// passes and junk nobody wants to see again.
#include "App.h"
#include "Icons.h"
#include "Theme.h"
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <filesystem>

namespace fs = std::filesystem;
using namespace theme;

namespace {
bool sameDir(const fs::path& a, const fs::path& b) {
	std::error_code ec;
	if (fs::equivalent(a, b, ec)) return true;
	return a.lexically_normal() == b.lexically_normal();
}
// "2026-11-07T21:02:11" -> "Nov 7 at 21:02" for a sentence ("" stays "").
std::string whenWords(const std::string& iso) {
	static const char* months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	if (iso.size() < 16 || iso[4] != '-' || iso[10] != 'T') return iso;
	int m = std::atoi(iso.substr(5, 2).c_str()), d = std::atoi(iso.substr(8, 2).c_str());
	if (m < 1 || m > 12) return iso;
	return std::string(months[m - 1]) + " " + std::to_string(d) + " at " + iso.substr(11, 5);
}
// The reason in the middle of a sentence: "not a vehicle", "counted twice", "a bad reading".
const char* reasonWords(exclusions::Reason r) {
	switch (r) {
	case exclusions::Reason::Twice: return "counted twice";
	case exclusions::Reason::BadReading: return "a bad reading";
	case exclusions::Reason::Merged: return "a person merged with a car";
	default: return "not a vehicle";
	}
}
}  // namespace

// ---- the record ----------------------------------------------------------------------------------------------------

std::string App::exclusionsPath(const std::string& statsPath) {
	fs::path data(dataDir());
	fs::path folder = fs::path(statsPath).parent_path();
	// Beside the csv only in VST Studio's own Stats folders: the data folder's and the sample's.  A csv opened from
	// anywhere else (a 2016 folder, an archive, a published dataset) is never written to or beside, so its marks go in
	// the data folder's record, keyed by its file name like every mark.
	if (!statsPath.empty() && (sameDir(folder, data / "Stats") || sameDir(folder, data / "Sample" / "Stats")))
		return (folder / "exclusions.json").string();
	return exclusions::pathFor(data.string());
}

const exclusions::Store& App::exclusionsFor(const std::string& statsPath) {
	std::string path = exclusionsPath(statsPath);
	auto it = excl_.find(path);
	if (it == excl_.end()) {
		// The documentation tour (--shots) starts from nothing, as it never reads the user's files.
		it = excl_.emplace(path, screenshotMode ? exclusions::Store() : exclusions::load(path)).first;
		exclKeysChanged();
	}
	return it->second;
}

void App::exclKeysChanged() {
	std::map<std::string, exclusions::Reason> keys;
	for (const auto& kv : excl_)
		for (const exclusions::Mark& m : kv.second.marks)
			if (m.state == exclusions::State::Excluded) keys[exclusions::key(m.run, m.frame)] = m.reason;
	std::lock_guard<std::mutex> l(exclMu_);
	exclKeys_.swap(keys);
}

bool App::leftOutAnyThread(const std::string& run, int frame, exclusions::Reason* why) const {
	std::lock_guard<std::mutex> l(exclMu_);
	auto it = exclKeys_.find(exclusions::key(run, frame));
	if (it == exclKeys_.end()) return false;
	if (why) *why = it->second;
	return true;
}

std::string App::statsPathForRun(const std::string& run) {
	// A plain file name of a saved run, never a path: the phone can only name runs VST Studio itself listed.
	if (run.empty() || run.find_first_of("/\\:") != std::string::npos || run == "." || run == ".." || run.rfind("stats_", 0) != 0
		|| fs::path(run).extension() != ".csv")
		return "";
	fs::path p = fs::path(dataDir()) / "Stats" / run;
	std::error_code ec;
	return fs::is_regular_file(p, ec) ? p.string() : "";
}

bool App::rowFor(const std::string& statsPath, int frame, ResultRow& out) {
	std::vector<StatsRow> stats;
	std::string err;
	if (statsPath.empty() || !readStatsCsv(statsPath, stats, err)) return false;
	stats.erase(std::remove_if(stats.begin(), stats.end(), [frame](const StatsRow& s) { return s.frame != frame; }), stats.end());
	if (stats.empty()) return false;
	std::vector<ResultRow> rs;
	rowsFromStats(stats, calsets::readRun(statsPath), statsPath, fs::path(statsPath).filename().string(), rs);
	syncSets();
	retimeRows(rs);
	out = rs.front();
	return true;
}

// Results' rows for that pass, marked or unmarked in place (the selection stays where it is).
void App::markRowsReviewed(const std::string& run, int frame, bool excluded, exclusions::Reason why, const std::string& at) {
	for (ResultRow& r : rows)
		if (r.frame == frame && !r.statsPath.empty() && fs::path(r.statsPath).filename().string() == run) {
			r.excluded = excluded;
			r.leftOutWhy = why;
			r.leftOutAt = excluded ? at : std::string();
		}
}

std::string App::whyCantDelete(const std::string& statsPath, int frame) {
	if (statsPath.empty()) return "This pass isn't in a results file yet, so there's no row to delete. You can still leave it out.";
	fs::path data(dataDir()), folder = fs::path(statsPath).parent_path();
	if (!sameDir(folder, data / "Stats") && !sameDir(folder, data / "Sample" / "Stats"))
		return "Its results file isn't one VST Studio keeps in your data folder (an opened file, an archive or a 2016 folder), "
			"so VST Studio won't change it. Leaving the pass out works on any file.";
	if (tracker.running() && sameDir(fs::path(tracker.runningStatsPath()), fs::path(statsPath)))
		return "Its run is still recording into that file. Leave it out now; you can delete it once tracking has stopped.";
	// A published dataset must stay exactly as it was published, and its points name their run and frame.
	syncSets();
	std::string run = fs::path(statsPath).filename().string();
	if (const calsets::Set* s = sets_.ofRun(run))
		for (const std::string& rel : s->published) {
			dataset::Info d;
			std::string err;
			if (!dataset::read((data / fs::path(rel)).string(), d, err)) continue;
			for (const dataset::Point& p : d.points)
				if (p.run == run && p.frame == frame)
					return "It's in your published dataset \"" + d.name + "\", which has to stay as it was published. Leave it out instead: "
						"the dataset will say so, and can be corrected.";
		}
	return "";
}

App::ReviewResult App::reviewPass(const std::string& statsPath, int frame, ReviewAction what, exclusions::Reason why) {
	ReviewResult res;
	if (statsPath.empty()) { res.message = "This pass isn't in a results file yet. Try again once it has been logged."; return res; }
	const std::string run = fs::path(statsPath).filename().string();
	const std::string path = exclusionsPath(statsPath);
	exclusionsFor(statsPath);   // loaded into excl_
	exclusions::Store& store = excl_[path];
	const std::string now = calib::nowIso();

	// What the row says, so the record still means something after a delete or once its run is archived away.
	exclusions::Mark m;
	m.run = run;
	m.frame = frame;
	m.reason = why;
	ResultRow shownRow;
	bool known = false;
	for (const ResultRow& r : rows)
		if (r.frame == frame && r.statsPath == statsPath) { shownRow = r; known = true; break; }
	if (!known) known = rowFor(statsPath, frame, shownRow);
	if (known) { m.passed = shownRow.time; m.speed = shownRow.speed; m.l2r = shownRow.l2r; }
	if (known && shownRow.calKind) {
		res.message = "This is your own calibration drive, which is already left out of the street's numbers.";
		return res;
	}

	exclusions::Store before = store;
	std::string err;
	if (what == ReviewAction::Delete) {
		std::string no = whyCantDelete(statsPath, frame);
		if (!no.empty()) { res.message = no; return res; }
		if (!exclusions::deleteRow(statsPath, frame, err)) { res.message = err; return res; }
		exclusions::recordDeleted(store, m, now);
		// This session's copy of it goes too: its row, its snapshot and its clip.
		std::string clip = tracker.forgetPass(statsPath, frame);
		std::error_code ec;
		if (!clip.empty()) fs::remove(fs::path(clip), ec);
		rows.erase(std::remove_if(rows.begin(), rows.end(), [&](const ResultRow& r) { return r.frame == frame && r.statsPath == statsPath; }), rows.end());
		selectedRow_ = -1;
		reviewRow_ = -1;
		res.message = "Deleted for good. The record in exclusions.json keeps one line saying a pass was deleted, and why.";
	}
	else if (what == ReviewAction::LeaveOut) {
		exclusions::exclude(store, m, now);
		markRowsReviewed(run, frame, true, why, now);
		res.message = std::string("Left out as \"") + exclusions::reasonLabel(why) + "\". It stays in the table, greyed; put it back any time.";
	}
	else {
		if (!exclusions::restore(store, run, frame, now)) { res.message = "That pass isn't left out."; return res; }
		markRowsReviewed(run, frame, false, why, now);
		res.message = "Put back: it counts again.";
	}
	if (!screenshotMode && !exclusions::save(path, store, err)) {
		store = before;   // as it was, so the screen never claims what the file doesn't say
		if (what != ReviewAction::Delete) {
			const exclusions::Mark* was = store.find(run, frame);
			bool out = was && was->state == exclusions::State::Excluded;
			markRowsReviewed(run, frame, out, was ? was->reason : why, was ? was->changedAt : std::string());
		}
		res.message = err;
		if (what == ReviewAction::Delete) { res.ok = true; res.message = "Deleted, but the record couldn't be saved: " + err; }
		exclKeysChanged();
		return res;
	}
	exclKeysChanged();
	res.ok = true;

	// A published dataset holding this pass can't change; its status.json records the review instead.
	if (what != ReviewAction::Delete && !screenshotMode) {
		syncSets();
		if (const calsets::Set* s = sets_.ofRun(run))
			for (const std::string& rel : s->published) {
				dataset::Info d;
				std::string derr;
				if (!dataset::read((fs::path(dataDir()) / fs::path(rel)).string(), d, derr)) continue;
				if (dataset::recordLater(d, run, frame, exclusions::reasonName(why), what == ReviewAction::LeaveOut, now, derr)) res.published.push_back(d.name);
				else if (!derr.empty()) toast(derr, c::warn, ICON_WARN);
			}
		if (!res.published.empty())
			res.message += " Your published dataset \"" + res.published.front() + "\"" + (res.published.size() > 1 ? " and others" : "")
				+ (what == ReviewAction::LeaveOut ? " now says its numbers count this pass, and offers a corrected copy." : " no longer flags it.");
	}
	return res;
}

// ---- Results ---------------------------------------------------------------------------------------------------------

void App::reviewCard(const ResultRow& r, int row) {
	if (r.calKind) return;   // the user's own drive is left out already, for its own reason
	ImGui::Dummy(ImVec2(0, px(4)));
	ImGui::PushTextWrapPos(0);
	if (r.excluded) {
		std::string at = r.leftOutAt.empty() ? std::string() : " on " + whenWords(r.leftOutAt);
		dimText("%s left out as %s%s. It isn't in the totals, the charts, the street report or anything published from it.",
			reviewing() ? "It was" : "You", reasonWords(r.leftOutWhy), at.c_str());
	}
	ImGui::PopTextWrapPos();
	if (reviewing()) {
		// An archive or a published dataset is read-only here: what was decided shows, nothing changes.
		if (!r.excluded) return;
		ImGui::PushTextWrapPos(0);
		dimText("Put its archive back to change that.");
		ImGui::PopTextWrapPos();
		return;
	}
	if (r.statsPath.empty()) return;
	if (r.excluded) {
		if (ghostButton(ICON_UNDO "  Put it back", ImVec2(-FLT_MIN, 0))) {
			ReviewResult res = reviewPass(r.statsPath, r.frame, ReviewAction::PutBack, r.leftOutWhy);
			toast(res.message, res.ok ? c::ok : c::bad, res.ok ? ICON_CHECK : ICON_WARN);
		}
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("It was traffic after all: count it again.");
		if (ghostButton(ICON_TRASH "  Delete it for good…", ImVec2(-FLT_MIN, 0))) { reviewRow_ = row; openDeletePass_ = true; }
		return;
	}
	if (ghostButton(ICON_EYE_SLASH "  Not a vehicle? Leave it out…", ImVec2(-FLT_MIN, 0))) {
		reviewRow_ = row;
		leaveReason_ = 0;
		reviewError_.clear();
		openLeaveOut_ = true;
	}
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("A shadow, a person, the camera shaking, or the same car twice?\nLeave it out of the numbers. You can put it back.");
}

void App::reviewDialogs() {
	if (openLeaveOut_) { openLeaveOut_ = false; ImGui::OpenPopup("Leave this pass out"); }
	// Why the open delete dialog's pass can't be deleted, worked out once when it opens (it reads published datasets).
	static std::string deleteNo;
	const bool valid = reviewRow_ >= 0 && reviewRow_ < int(rows.size());
	if (openDeletePass_) {
		openDeletePass_ = false;
		reviewError_.clear();
		deleteNo = valid ? whyCantDelete(rows[reviewRow_].statsPath, rows[reviewRow_].frame) : std::string();
		ImGui::OpenPopup("Delete this pass");
	}

	ImGui::SetNextWindowSize(ImVec2(px(560), 0), ImGuiCond_Always);
	if (ImGui::BeginPopupModal("Leave this pass out", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
		if (!valid) { ImGui::CloseCurrentPopup(); ImGui::EndPopup(); return; }
		const ResultRow r = rows[reviewRow_];   // a copy: the buttons below change rows
		ImGui::PushTextWrapPos(0);
		std::string when = r.time.empty() ? std::string() : " at " + whenWords(r.time);
		ImGui::Text("%d mph, %s%s.", r.speed, r.l2r ? "left to right" : "right to left", when.c_str());
		dimText("Leaving it out takes it out of the totals, the charts, the street report, the phone's Stats and anything you publish "
			"from now on. It stays in the table, greyed, so you can always see it and put it back.");
		ImGui::Dummy(ImVec2(0, px(4)));
		sectionLabel("WHY");
		for (int k = 0; k < exclusions::kReasons; k++) {
			auto reason = exclusions::Reason(k);
			ImGui::PushID(k);
			if (ImGui::RadioButton(exclusions::reasonLabel(reason), leaveReason_ == k)) leaveReason_ = k;
			ImGui::Indent(px(28));
			dimText("%s", exclusions::reasonHelp(reason));
			ImGui::Unindent(px(28));
			ImGui::PopID();
		}
		// Say beforehand what happens to a published dataset, so nobody is surprised.
		if (const calsets::Set* s = setOfRow(r); s && !s->published.empty()) {
			ImGui::Dummy(ImVec2(0, px(4)));
			dimText(ICON_CERTIFICATE "  Its calibration set is published. A published dataset never changes, so if this pass is in it, its "
				"cover will say so and offer a corrected copy.");
		}
		if (!reviewError_.empty()) { ImGui::PushStyleColor(ImGuiCol_Text, v4(c::bad)); ImGui::TextWrapped("%s", reviewError_.c_str()); ImGui::PopStyleColor(); }
		ImGui::PopTextWrapPos();
		ImGui::Dummy(ImVec2(0, px(6)));
		if (primaryButton(ICON_EYE_SLASH "  Leave it out")) {
			ReviewResult res = reviewPass(r.statsPath, r.frame, ReviewAction::LeaveOut, exclusions::Reason(leaveReason_));
			if (res.ok) { toast(res.message, c::ok, ICON_CHECK); ImGui::CloseCurrentPopup(); }
			else reviewError_ = res.message;
		}
		ImGui::SameLine();
		if (ghostButton("Cancel")) ImGui::CloseCurrentPopup();
		ImGui::SameLine();
		// The permanent choice, kept to the side and behind its own confirmation.
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, ImGui::GetContentRegionAvail().x - buttonWidth(ICON_TRASH "  Delete it for good…")));
		if (ghostButton(ICON_TRASH "  Delete it for good…")) { ImGui::CloseCurrentPopup(); openDeletePass_ = true; }
		ImGui::EndPopup();
	}

	ImGui::SetNextWindowSize(ImVec2(px(560), 0), ImGuiCond_Always);
	if (ImGui::BeginPopupModal("Delete this pass", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
		if (!valid) { ImGui::CloseCurrentPopup(); ImGui::EndPopup(); return; }
		const ResultRow r = rows[reviewRow_];
		const std::string no = deleteNo;
		ImGui::PushTextWrapPos(0);
		std::string when = r.time.empty() ? std::string() : " at " + whenWords(r.time);
		ImGui::Text("%d mph, %s%s.", r.speed, r.l2r ? "left to right" : "right to left", when.c_str());
		if (!no.empty()) dimText("%s", no.c_str());
		else {
			ImGui::PushStyleColor(ImGuiCol_Text, v4(c::bad));
			ImGui::TextWrapped("This can't be undone. Its row is taken out of %s%s.", fs::path(r.statsPath).filename().string().c_str(),
				r.vehicleId ? ", with its snapshot and clip" : "");
			ImGui::PopStyleColor();
			dimText("Leaving it out does the same for every number and can be undone, so it's usually the better choice. "
				"Delete is for test passes and junk you never want to see again. A line in exclusions.json still records "
				"that a pass was deleted, when, and why, so your data stays honest.");
		}
		if (!reviewError_.empty()) { ImGui::PushStyleColor(ImGuiCol_Text, v4(c::bad)); ImGui::TextWrapped("%s", reviewError_.c_str()); ImGui::PopStyleColor(); }
		ImGui::PopTextWrapPos();
		ImGui::Dummy(ImVec2(0, px(6)));
		if (no.empty()) {
			if (primaryButton(ICON_TRASH "  Delete for good", ImVec2(0, 0), c::bad)) {
				ReviewResult res = reviewPass(r.statsPath, r.frame, ReviewAction::Delete, r.excluded ? r.leftOutWhy : exclusions::Reason(leaveReason_));
				if (res.ok) { toast(res.message, c::ok, ICON_TRASH); ImGui::CloseCurrentPopup(); }
				else reviewError_ = res.message;
			}
			ImGui::SameLine();
		}
		if (!r.excluded && ghostButton(no.empty() ? ICON_EYE_SLASH "  Leave it out instead" : ICON_EYE_SLASH "  Leave it out")) {
			ImGui::CloseCurrentPopup();
			openLeaveOut_ = true;
		}
		if (!r.excluded) ImGui::SameLine();
		if (ghostButton("Cancel")) ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
}
