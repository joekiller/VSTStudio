// Passes the user reviewed and found aren't traffic: a shadow, a person, a bird or the camera shaking that VST timed as
// a vehicle ("not a vehicle"), the same car logged twice, or a real car whose reading can't be right.  The default is to
// leave such a pass out, which is a mark and can be undone; deleting its row from the stats csv is the second, permanent
// choice.  Either way the record stays honest: what was left out, why and when is kept, and so is the fact that a row
// was deleted (when, its speed, why), though not the row itself.
//
// Storage: <data>/Stats/exclusions.json, beside calibration-sets.json (a .json, so the street report's *.csv scan of
// Stats/ never reads it as a run).  One entry per pass ever reviewed, keyed like a calibration pass (report::passKey):
// the stats csv's file name and the row's Frame.  The file name and not the path, so an archive or a published dataset
// that copied the csv into its own Stats/ still matches.
//
//   { "format": "vst-exclusions", "version": 1,
//     "marks": [ { "run": "stats_camera0_20261107_201500.csv", "frame": 51234, "reason": "not-vehicle",
//                  "state": "excluded", "passed": "2026-11-07T20:31:02", "speed": 61, "direction": "L2R",
//                  "markedAt": "2026-11-07T21:02:11", "changedAt": "2026-11-07T21:02:11" } ] }
//
// state: "excluded" (left out of every number now), "restored" (put back: kept, so the record shows it was looked at),
// "deleted" (its row was taken out of the csv for good).  Only "excluded" leaves a row out; a deleted row is gone from
// the csv, so there is nothing left to leave out.
//
// Who reads it (each through Store::excluded / keys()):
//   App::rowsFromStats and showSessionResults  ->  ResultRow::excluded: Results' tiles and charts skip the row
//   report::Input::excluded                    ->  the street report leaves the row out and says how many
//   archive::write / restore                   ->  an archive carries its runs' marks in its own Stats/exclusions.json
//   App::publishSet                            ->  dataset::Point::leftOut, a column of points.csv
//   Server (GET /api/vehicles)                 ->  the phone's Passes and Stats
//
// No ImGui: tests/exclusions_test.cpp drives it.
#pragma once
#include <set>
#include <string>
#include <vector>

namespace exclusions {

// Why a pass was left out.  Stored by name; an unknown name reads as NotVehicle.
// Merged: a person on the near sidewalk covers the lane as a car comes the other way, and the two are timed as one
// (Joe, 2026-11-07: usually a fast "wrong-way" reading on a close view).
enum class Reason { NotVehicle, Twice, BadReading, Merged };
constexpr int kReasons = 4;
const char* reasonName(Reason r);    // "not-vehicle", "twice", "bad-reading", "merged": the stored value
const char* reasonLabel(Reason r);   // "Not a vehicle", "Counted twice", "Bad reading", "Person merged with a car"
const char* reasonHelp(Reason r);    // one plain sentence: what it covers
Reason reasonFrom(const std::string& name);

enum class State { Excluded, Restored, Deleted };
const char* stateName(State s);      // "excluded", "restored", "deleted"
State stateFrom(const std::string& name);

struct Mark {
	std::string run;          // the stats csv's file name
	int frame = 0;            // the row's Frame there
	Reason reason = Reason::NotVehicle;
	State state = State::Excluded;
	// What the row said, so the record still means something once the row is deleted or its run archived away.
	std::string passed;       // "yyyy-mm-ddThh:mm:ss", "" when unknown
	int speed = 0;            // mph as Results showed it when it was marked
	bool l2r = true;
	std::string markedAt;     // when it was first reviewed (ISO 8601 local)
	std::string changedAt;    // when its state last changed
};

// The key a mark and a row are matched on: report::passKey's format, "<run>|<frame>".
std::string key(const std::string& run, int frame);

struct Store {
	std::vector<Mark> marks;  // oldest first
	const Mark* find(const std::string& run, int frame) const;   // any state; nullptr when never reviewed
	bool excluded(const std::string& run, int frame) const;      // left out right now
	std::set<std::string> keys() const;                          // key() of every row left out right now
	int count(State s) const;
};

// <dataDir>/Stats/exclusions.json
std::string pathFor(const std::string& dataDir);
// Reads the file; a missing or unreadable file is an empty store (unreadable entries are skipped).
Store load(const std::string& path);
// Writes a temp file and renames it over the old one, so a crash mid-write never loses the record.  false + `error`.
bool save(const std::string& path, const Store& s, std::string& error);
std::string dump(const Store& s);

// Leaves `m` (run, frame, reason and what the row said) out: a new entry, or the pass's entry updated (a pass put back
// and left out again keeps its markedAt).  `now` becomes changedAt (and markedAt when new).
void exclude(Store& s, Mark m, const std::string& now);
// Puts a left-out pass back.  false when it isn't left out now.
bool restore(Store& s, const std::string& run, int frame, const std::string& now);
// Records that a pass's row was deleted from its csv (whatever it was before; a pass never reviewed gets an entry).
void recordDeleted(Store& s, Mark m, const std::string& now);
// The marks of `runs` alone (file names): what an archive or a published dataset carries of the data folder's file.
Store only(const Store& s, const std::set<std::string>& runs);
// Adds the marks of `from` that `into` has no entry for (an archive's marks going back into the data folder).  A pass
// `into` already has an entry for keeps its own: the data folder's record is the newer one.  Returns how many were added.
int merge(Store& into, const Store& from);

// Takes the row whose Frame is `frame` out of the stats csv at `csv`.  Every other byte stays as it was (line endings,
// spacing, the header), and the file is rewritten through a temp file and a rename.  false + `error` when the file
// can't be read or written, or has no such row.
bool deleteRow(const std::string& csv, int frame, std::string& error);

}  // namespace exclusions
