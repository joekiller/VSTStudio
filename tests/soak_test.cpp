// Long-run memory soak for the embedded tracker, the way VST Studio drives it (Tracker + vst::run), to find memory that
// grows with time on a box left running for days.  Not part of ctest: a full soak takes minutes to hours.
//
// Usage: soak_test <input> <VST.cfg> <data dir> <seconds> [live|runs]
//   live  one run on <input> (a looped stream, e.g. ffmpeg -stream_loop -1 ... -f mpegts -listen 1 http://...), as a
//         camera left on for days; frames are analyzed as fast as the machine can.
//   runs  <input> (a video file) tracked again and again, each a new run, as Start/Stop and source switches do.
// Every 10 s it prints the process's resident memory (RSS) with the frame, vehicles kept and logged, so growth can be
// lined up with what the tracker holds.  Meanwhile it reads the live frame and new vehicles as the GUI and phone do.
#include "Tracker.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>

using Clock = std::chrono::steady_clock;

// Resident set size in MB from /proc (Linux only; the soak is a Linux tool).
static double rssMb() {
	std::ifstream in("/proc/self/statm");
	long pages = 0, resident = 0;
	in >> pages >> resident;
	return resident * 4096.0 / (1 << 20);
}

int main(int argc, char** argv) {
	if (argc < 5) { std::fprintf(stderr, "usage: %s <input> <VST.cfg> <data dir> <seconds> [live|runs]\n", argv[0]); return 2; }
	std::string input = argv[1], mode = argc > 5 ? argv[5] : "live";
	double seconds = std::atof(argv[4]);
	Tracker::Options opt;
	opt.configPath = argv[2];
	opt.dataDir = argv[3];
	opt.realtime = false;
	Tracker t;
	auto begin = Clock::now(), nextReport = begin;
	auto elapsed = [&] { return std::chrono::duration<double>(Clock::now() - begin).count(); };
	Tracker::Live live;
	int seen = 0, runs = 0;
	std::printf("%8s %10s %9s %8s %8s %6s\n", "secs", "frame", "rss_mb", "kept", "logged", "runs");
	while (elapsed() < seconds) {
		if (!t.running()) { t.start({ Job{ input } }, opt); runs++; }
		t.latest(live, true);                                          // the Live page's ~60 Hz copy
		for (const Vehicle& v : t.vehiclesSince(seen)) seen = v.id;    // App::poll's new-vehicle check
		if (Clock::now() >= nextReport) {
			Counts c = t.counts();
			std::printf("%8.0f %10d %9.1f %8zu %8d %6d\n", elapsed(), t.progress().frame, rssMb(), t.vehiclesSince(0).size(), c.vehicles, runs);
			std::fflush(stdout);
			nextReport += std::chrono::seconds(10);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(16));
		if (mode == "live" && !t.running() && runs > 0) { std::fprintf(stderr, "the live run ended\n"); break; }
	}
	t.stop();
	std::printf("final rss %.1f MB\n", rssMb());
	return 0;
}
