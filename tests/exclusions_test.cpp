// Passes reviewed by hand (src/Gui/Exclusions.cpp) unit test.
//
//   exclusions_test <work dir>
//
// Joe (2026-11-07): "I got a false positive that upon review should be removed from history or at least marked and
// excluded from stats."  These checks pin:
//   - leave out, put back and leave out again: one entry per pass, markedAt kept, only "excluded" counts as left out
//   - the file round trip, a hand-broken entry skipped, a duplicate keeping the later one
//   - deleteRow(): exactly that row goes, every other byte (header, CRLF endings, spacing) stays, a second delete fails
//     in plain words, and the csv still reads back through readStatsCsv
//   - recordDeleted() keeps the reason a left-out pass had, only() and merge() for archives
//   - the street report leaves a left-out pass out of every number and says how many
//   - an archive carries its runs' marks, its rows' leftOut, and its put-back merges them into the data folder's record
#include "Archive.h"
#include "Exclusions.h"
#include "Report.h"
#include "Results.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using namespace exclusions;

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

static std::string slurp(const fs::path& p) { std::ifstream in(p, std::ios::binary); std::ostringstream s; s << in.rdbuf(); return s.str(); }
static void spit(const fs::path& p, const std::string& text) { fs::create_directories(p.parent_path()); std::ofstream(p, std::ios::binary) << text; }

static Mark mark(const std::string& run, int frame, int speed, Reason why = Reason::NotVehicle) {
	Mark m;
	m.run = run;
	m.frame = frame;
	m.speed = speed;
	m.reason = why;
	m.passed = "2026-11-07T20:31:02";
	return m;
}

static void testStore(const fs::path& work) {
	Store s;
	exclude(s, mark("stats_a.csv", 120, 61), "2026-11-07T21:00:00");
	CHECK(s.excluded("stats_a.csv", 120) && !s.excluded("stats_a.csv", 121) && !s.excluded("stats_b.csv", 120));
	CHECK(s.keys().count("stats_a.csv|120") == 1 && s.count(State::Excluded) == 1);
	// Put back: kept (it was looked at) but it counts again.
	CHECK(restore(s, "stats_a.csv", 120, "2026-11-07T21:05:00"));
	CHECK(!s.excluded("stats_a.csv", 120) && s.find("stats_a.csv", 120)->state == State::Restored && s.keys().empty());
	CHECK(!restore(s, "stats_a.csv", 120, "2026-11-07T21:06:00"));   // not left out now
	// Left out again, for another reason: still one entry, first marked time kept.
	exclude(s, mark("stats_a.csv", 120, 61, Reason::Twice), "2026-11-07T21:10:00");
	CHECK(s.marks.size() == 1 && s.marks[0].reason == Reason::Twice && s.marks[0].markedAt == "2026-11-07T21:00:00"
		&& s.marks[0].changedAt == "2026-11-07T21:10:00" && s.excluded("stats_a.csv", 120));

	// Deleted: the reason it was left out for stays with the record.
	exclude(s, mark("stats_a.csv", 300, 44, Reason::BadReading), "2026-11-07T21:20:00");
	Mark d = mark("stats_a.csv", 300, 44, Reason::BadReading);
	recordDeleted(s, d, "2026-11-07T21:21:00");
	CHECK(s.find("stats_a.csv", 300)->state == State::Deleted && s.find("stats_a.csv", 300)->reason == Reason::BadReading
		&& !s.excluded("stats_a.csv", 300) && s.count(State::Deleted) == 1);
	// A pass deleted without being left out first gets its own entry.
	recordDeleted(s, mark("stats_b.csv", 7, 33), "2026-11-07T21:22:00");
	CHECK(s.marks.size() == 3 && s.find("stats_b.csv", 7)->markedAt == "2026-11-07T21:22:00");

	// Round trip through the file.
	std::string path = pathFor((work / "data").string()), err;
	CHECK(fs::path(path).filename() == "exclusions.json" && fs::path(path).parent_path().filename() == "Stats");
	CHECK(save(path, s, err));
	CHECK(!fs::exists(path + ".tmp"));
	Store back = load(path);
	CHECK(back.marks.size() == 3 && back.excluded("stats_a.csv", 120) && back.find("stats_a.csv", 120)->reason == Reason::Twice
		&& back.find("stats_a.csv", 120)->speed == 61 && back.find("stats_a.csv", 120)->passed == "2026-11-07T20:31:02"
		&& back.find("stats_b.csv", 7)->state == State::Deleted);
	CHECK(slurp(path).find("\"format\": \"vst-exclusions\"") != std::string::npos);

	// A hand-edited file: a broken entry is skipped, a duplicate keeps the later one, the rest still reads.
	spit(work / "edited.json", R"({"format":"vst-exclusions","version":1,"marks":[
		{"run":"stats_a.csv","frame":"twelve"},
		{"run":"stats_a.csv","frame":5,"reason":"twice","state":"excluded"},
		{"run":"stats_a.csv","frame":5,"reason":"bad-reading","state":"restored"},
		{"run":"stats_a.csv","frame":6,"reason":"something new","state":"excluded"}]})");
	Store e = load((work / "edited.json").string());
	CHECK(e.marks.size() == 2 && !e.excluded("stats_a.csv", 5) && e.find("stats_a.csv", 5)->reason == Reason::BadReading);
	CHECK(e.excluded("stats_a.csv", 6) && e.find("stats_a.csv", 6)->reason == Reason::NotVehicle);   // unknown reason: not a vehicle
	CHECK(load((work / "missing.json").string()).marks.empty());
	spit(work / "junk.json", "not json at all");
	CHECK(load((work / "junk.json").string()).marks.empty());

	// only() and merge(): an archive's share, and putting it back without overriding the data folder's newer record.
	Store mine = only(s, { "stats_b.csv" });
	CHECK(mine.marks.size() == 1 && mine.marks[0].run == "stats_b.csv");
	Store data;
	exclude(data, mark("stats_b.csv", 7, 33), "2026-11-08T09:00:00");   // the data folder says left out, the archive deleted
	Store arch;
	exclude(arch, mark("stats_c.csv", 9, 50), "2026-11-07T22:00:00");
	recordDeleted(arch, mark("stats_b.csv", 7, 33), "2026-11-07T22:00:00");
	CHECK(merge(data, arch) == 1 && data.excluded("stats_b.csv", 7) && data.excluded("stats_c.csv", 9));
	CHECK(merge(data, arch) == 0);   // twice adds nothing

	CHECK(std::string(reasonName(Reason::Twice)) == "twice" && reasonFrom("bad-reading") == Reason::BadReading
		&& std::string(reasonLabel(Reason::NotVehicle)) == "Not a vehicle" && stateFrom("deleted") == State::Deleted);
}

static void testDeleteRow(const fs::path& work) {
	// The tracker's format with CRLF endings (a csv carried from Windows), and the 2016 one's spacing.
	const std::string head = "Date, Time, Frame, Direction, StartFrame, EndFrame, # Frames, StartPix, EndPix, DeltaPix, VehicleArea, , estSpeed, Flag, Timestamp\r\n";
	const std::string a = "20261107, 203100, 1200, NE, 1170, 1200, 30, 100, 900, 800, 60, , 25, , 2026-11-07T20:31:00-05:00\r\n";
	const std::string b = "20261107, 203102, 1262, NE, 1250, 1262, 12, 100, 900, 800, 41, , 61, , 2026-11-07T20:31:02-05:00\r\n";
	const std::string c = "20261107, 203110, 1500, SW, 1468, 1500, 32, 900, 100, -800, 70, , 23, , 2026-11-07T20:31:10-05:00";   // no final newline
	fs::path csv = work / "data" / "Stats" / "stats_camera0_20261107_203000.csv";
	spit(csv, head + a + b + c);
	std::string err;
	CHECK(deleteRow(csv.string(), 1262, err));
	CHECK(slurp(csv) == head + a + c);   // byte for byte, but for that row
	CHECK(!fs::exists(csv.string() + ".tmp"));
	CHECK(!deleteRow(csv.string(), 1262, err) && err.find("no row for frame 1262") != std::string::npos);
	CHECK(deleteRow(csv.string(), 1500, err) && slurp(csv) == head + a);   // the last line, without a newline
	std::vector<StatsRow> rows;
	CHECK(readStatsCsv(csv.string(), rows, err) && rows.size() == 1 && rows[0].frame == 1200);
	// The header is never taken for a row, whatever is asked.
	CHECK(!deleteRow(csv.string(), 0, err));
	CHECK(!deleteRow((work / "nope.csv").string(), 1, err) && err.find("Can't open") != std::string::npos);
}

// The street report leaves a left-out pass out of every number and says how many; an archive carries the marks.
static void testReportAndArchive(const fs::path& work) {
	fs::path data = work / "study";
	const std::string head = "Date, Time, Frame, Direction, StartFrame, EndFrame, # Frames, StartPix, EndPix, DeltaPix, VehicleArea, , estSpeed, Flag, Timestamp\n";
	std::string body;
	for (int i = 0; i < 9; i++)
		body += "20261107, 2031" + std::to_string(10 + i) + ", " + std::to_string(1000 + 100 * i) + ", NE, 0, 30, 30, 100, 900, 800, 60, , 24, , 2026-11-07T20:31:" + std::to_string(10 + i) + "-05:00\n";
	body += "20261107, 203130, 9000, NE, 0, 9, 9, 100, 900, 800, 60, , 81, , 2026-11-07T20:31:30-05:00\n";   // the shadow
	const std::string run = "stats_camera0_20261107_203000.csv";
	spit(data / "Stats" / run, head + body);

	report::Input in;
	in.dataDir = data.string();
	report::Settings set;
	set.speedLimit = 25;
	report::Study before = report::analyze(in, set);
	CHECK(before.all.n == 10 && before.all.max == 81);
	in.excluded = { report::passKey(run, 9000) };
	report::Study after = report::analyze(in, set);
	CHECK(after.all.n == 9 && after.all.max == 24 && after.runs.size() == 1 && after.runs[0].leftOut == 1);
	bool said = false;
	for (const std::string& c : after.caveats) said |= c.find("1 detection was reviewed by hand and left out") != std::string::npos;
	CHECK(said);
	std::string html = report::renderHtml(after);
	CHECK(html.find("1 left out by hand") != std::string::npos && html.find("marked as not traffic") != std::string::npos);

	// Archive it: the marks of its runs only, its rows' leftOut, vehicles.csv and review.html say so.
	Store store;
	exclude(store, mark(run, 9000, 81), "2026-11-07T21:00:00");
	exclude(store, mark("stats_other.csv", 5, 40), "2026-11-07T21:00:00");
	archive::Request req;
	req.dataDir = data.string();
	req.move = true;
	req.info.name = "Week of Nov 7";
	req.info.created = "2026-11-08T10:00:00";
	req.statsCsvs = { (data / "Stats" / run).string() };
	req.exclusions = store;
	for (int i = 0; i < 2; i++) {
		archive::Source s;
		s.row.run = run;
		s.row.frame = i ? 9000 : 1000;
		s.row.speed = i ? 81 : 24;
		s.row.leftOut = i ? "not-vehicle" : "";
		req.vehicles.push_back(s);
	}
	std::string dir, err;
	CHECK(archive::write(req, dir, err));
	archive::Info a;
	CHECK(archive::read(dir, a, err));
	CHECK(a.exclusions.marks.size() == 1 && a.exclusions.excluded(run, 9000));
	CHECK(a.vehicles.size() == 2 && a.vehicles[1].leftOut == "not-vehicle" && a.vehicles[0].leftOut.empty());
	CHECK(slurp(fs::path(dir) / "vehicles.csv").find(",Left out\n") != std::string::npos
		&& slurp(fs::path(dir) / "vehicles.csv").find(",Not a vehicle\n") != std::string::npos);
	CHECK(slurp(fs::path(dir) / "review.html").find("v.leftOut") != std::string::npos);
	// The report made from the archive leaves it out too (App::archiveReportInput passes these keys).
	report::Input ain;
	ain.dataDir = dir;
	ain.excluded = a.exclusions.keys();
	CHECK(report::analyze(ain, set).all.n == 9);
	// Moved away, then put back: the mark goes back into the data folder's record (merged, not copied over it).
	CHECK(!fs::exists(data / "Stats" / run));
	Store there;
	exclude(there, mark("stats_kept.csv", 1, 30), "2026-11-08T11:00:00");
	CHECK(save(pathFor(data.string()), there, err));
	CHECK(archive::restore(dir, data.string(), err));
	CHECK(err.find("exclusions.json") == std::string::npos);   // never "already there, left as it is"
	Store merged = load(pathFor(data.string()));
	CHECK(merged.excluded("stats_kept.csv", 1) && merged.excluded(run, 9000) && merged.marks.size() == 2);
}

int main(int argc, char** argv) {
	fs::path work = argc > 1 ? fs::path(argv[1]) : fs::temp_directory_path() / "exclusions-test";
	fs::remove_all(work);
	fs::create_directories(work);
	testStore(work);
	testDeleteRow(work);
	testReportAndArchive(work);
	std::printf("%d of %d checks passed\n", checks - failures, checks);
	return failures ? 1 : 0;
}
