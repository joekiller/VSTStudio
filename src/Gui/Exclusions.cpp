#include "Exclusions.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace exclusions {

const char* reasonName(Reason r) {
	switch (r) {
	case Reason::Twice: return "twice";
	case Reason::BadReading: return "bad-reading";
	case Reason::Merged: return "merged";
	default: return "not-vehicle";
	}
}
const char* reasonLabel(Reason r) {
	switch (r) {
	case Reason::Twice: return "Counted twice";
	case Reason::BadReading: return "Bad reading";
	case Reason::Merged: return "Person merged with a car";
	default: return "Not a vehicle";
	}
}
const char* reasonHelp(Reason r) {
	switch (r) {
	case Reason::Twice: return "The same vehicle was logged twice; this is the extra one.";
	case Reason::BadReading: return "A real vehicle, but the speed can't be right: it was hidden, or merged with another.";
	case Reason::Merged: return "Someone on the near sidewalk covered the lane as a car came the other way, and VST timed them as one: "
		"often a fast wrong-way reading.";
	default: return "A shadow, a person or animal, headlights, rain, or the camera moving.";
	}
}
Reason reasonFrom(const std::string& n) {
	return n == "twice" ? Reason::Twice : n == "bad-reading" ? Reason::BadReading : n == "merged" ? Reason::Merged : Reason::NotVehicle;
}

const char* stateName(State s) { return s == State::Restored ? "restored" : s == State::Deleted ? "deleted" : "excluded"; }
State stateFrom(const std::string& n) { return n == "restored" ? State::Restored : n == "deleted" ? State::Deleted : State::Excluded; }

std::string key(const std::string& run, int frame) { return run + "|" + std::to_string(frame); }

const Mark* Store::find(const std::string& run, int frame) const {
	for (const Mark& m : marks) if (m.frame == frame && m.run == run) return &m;
	return nullptr;
}
bool Store::excluded(const std::string& run, int frame) const {
	const Mark* m = find(run, frame);
	return m && m->state == State::Excluded;
}
std::set<std::string> Store::keys() const {
	std::set<std::string> out;
	for (const Mark& m : marks) if (m.state == State::Excluded) out.insert(key(m.run, m.frame));
	return out;
}
int Store::count(State s) const { return int(std::count_if(marks.begin(), marks.end(), [s](const Mark& m) { return m.state == s; })); }

std::string pathFor(const std::string& dataDir) { return (fs::path(dataDir) / "Stats" / "exclusions.json").string(); }

std::string dump(const Store& s) {
	json marks = json::array();
	for (const Mark& m : s.marks)
		marks.push_back({ { "run", m.run }, { "frame", m.frame }, { "reason", reasonName(m.reason) }, { "state", stateName(m.state) },
			{ "passed", m.passed }, { "speed", m.speed }, { "direction", m.l2r ? "L2R" : "R2L" },
			{ "markedAt", m.markedAt }, { "changedAt", m.changedAt } });
	return json({ { "format", "vst-exclusions" }, { "version", 1 }, { "marks", marks } }).dump(1, '\t') + "\n";
}

Store load(const std::string& path) {
	Store s;
	std::ifstream in(fs::path(path), std::ios::binary);
	if (!in) return s;
	json j = json::parse(in, nullptr, false);
	if (!j.is_object() || !j.contains("marks") || !j["marks"].is_array()) return s;
	// Field by field with type checks, so a hand-edited file loses only the entries it broke.
	auto str = [](const json& o, const char* k) { return o.contains(k) && o[k].is_string() ? o[k].get<std::string>() : std::string(); };
	auto num = [](const json& o, const char* k) { return o.contains(k) && o[k].is_number() ? o[k].get<double>() : 0.0; };
	for (const json& o : j["marks"]) {
		if (!o.is_object()) continue;
		Mark m;
		m.run = str(o, "run");
		if (m.run.empty() || !o.contains("frame") || !o["frame"].is_number()) continue;
		m.frame = int(num(o, "frame"));
		m.reason = reasonFrom(str(o, "reason"));
		m.state = stateFrom(str(o, "state"));
		m.passed = str(o, "passed");
		m.speed = int(num(o, "speed"));
		m.l2r = str(o, "direction") != "R2L";
		m.markedAt = str(o, "markedAt");
		m.changedAt = str(o, "changedAt");
		// One entry per pass: a duplicate (two files merged by hand) keeps the later one.
		s.marks.erase(std::remove_if(s.marks.begin(), s.marks.end(), [&](const Mark& x) { return x.run == m.run && x.frame == m.frame; }), s.marks.end());
		s.marks.push_back(m);
	}
	return s;
}

bool save(const std::string& path, const Store& s, std::string& error) {
	fs::path p(path), tmp = fs::path(path + ".tmp");
	std::error_code ec;
	fs::create_directories(p.parent_path(), ec);
	{
		std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
		out << dump(s);
		if (!out) { error = "Couldn't write " + tmp.string(); return false; }
	}
	fs::rename(tmp, p, ec);
	if (ec) { error = "Couldn't save " + p.string() + ": " + ec.message(); fs::remove(tmp, ec); return false; }
	return true;
}

// The entry for (run, frame), made when there is none.
static Mark& entry(Store& s, const Mark& m, const std::string& now, bool* fresh) {
	for (Mark& x : s.marks)
		if (x.run == m.run && x.frame == m.frame) { *fresh = false; return x; }
	s.marks.push_back(m);
	s.marks.back().markedAt = now;
	*fresh = true;
	return s.marks.back();
}

void exclude(Store& s, Mark m, const std::string& now) {
	bool fresh;
	Mark& e = entry(s, m, now, &fresh);
	std::string marked = e.markedAt;
	e = m;
	e.markedAt = fresh || marked.empty() ? now : marked;
	e.state = State::Excluded;
	e.changedAt = now;
}

bool restore(Store& s, const std::string& run, int frame, const std::string& now) {
	for (Mark& m : s.marks)
		if (m.run == run && m.frame == frame && m.state == State::Excluded) {
			m.state = State::Restored;
			m.changedAt = now;
			return true;
		}
	return false;
}

void recordDeleted(Store& s, Mark m, const std::string& now) {
	bool fresh;
	Mark& e = entry(s, m, now, &fresh);
	// A pass left out first keeps the reason it was left out for, unless the delete gives one of its own.
	if (!fresh) { e.passed = m.passed.empty() ? e.passed : m.passed; e.speed = m.speed ? m.speed : e.speed; e.l2r = m.l2r; e.reason = m.reason; }
	e.state = State::Deleted;
	e.changedAt = now;
}

Store only(const Store& s, const std::set<std::string>& runs) {
	Store out;
	for (const Mark& m : s.marks) if (runs.count(m.run)) out.marks.push_back(m);
	return out;
}

int merge(Store& into, const Store& from) {
	int added = 0;
	for (const Mark& m : from.marks)
		if (!into.find(m.run, m.frame)) { into.marks.push_back(m); added++; }
	return added;
}

// The Frame of a stats csv line (its third comma-separated field), or false for the header and anything unreadable.
static bool frameOf(const std::string& line, int& frame) {
	// Date, Time, Frame, ...: the field after the second comma.
	size_t a = line.find(',');
	if (a != std::string::npos) a = line.find(',', a + 1);
	if (a == std::string::npos) return false;
	size_t b = line.find(',', a + 1);
	if (b == std::string::npos) return false;
	std::string f = line.substr(a + 1, b - a - 1);
	size_t i = f.find_first_not_of(" \t"), k = f.find_last_not_of(" \t");
	if (i == std::string::npos) return false;
	f = f.substr(i, k - i + 1);
	if (f.empty() || f.find_first_not_of("0123456789") != std::string::npos) return false;
	try { frame = std::stoi(f); } catch (...) { return false; }
	return true;
}

bool deleteRow(const std::string& csv, int frame, std::string& error) {
	fs::path p(csv);
	std::string text;
	{
		std::ifstream in(p, std::ios::binary);
		if (!in) { error = "Can't open " + p.filename().string(); return false; }
		std::ostringstream ss;
		ss << in.rdbuf();
		text = ss.str();
	}
	// Line by line, each kept with its own ending ("\n" or "\r\n"), so everything but the deleted row is byte for byte
	// the same.  Every row with that Frame goes (a run has one per frame).
	std::string out;
	out.reserve(text.size());
	int gone = 0;
	size_t pos = 0;
	while (pos < text.size()) {
		size_t nl = text.find('\n', pos);
		size_t end = nl == std::string::npos ? text.size() : nl + 1;
		std::string line = text.substr(pos, end - pos);
		int f;
		if (frameOf(line, f) && f == frame) gone++;
		else out += line;
		pos = end;
	}
	if (!gone) { error = p.filename().string() + " has no row for frame " + std::to_string(frame) + "; it may have been deleted already."; return false; }
	fs::path tmp = fs::path(csv + ".tmp");
	{
		std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
		o << out;
		if (!o) { error = "Couldn't write " + tmp.string(); return false; }
	}
	std::error_code ec;
	fs::rename(tmp, p, ec);
	if (ec) { error = "Couldn't replace " + p.filename().string() + ": " + ec.message(); fs::remove(tmp, ec); return false; }
	return true;
}

}  // namespace exclusions
