// Keeps the computer from going to sleep on its own while VST Studio is timing a live camera or stream.
//
// A sleeping computer times nothing, and the engine can't measure a car across the gap (videoSpeedTracker.cpp,
// kGapSeconds), so an unattended camera that dozes off every 20 minutes misses most of the street.  While held, the
// system's idle sleep is held off the way a video player or a download does it; the screen may still turn off, and
// sleeping on purpose (the lid, the power button, the Sleep menu) still works.
//
//   Windows  SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED) on the GUI thread, which calls want().
//   macOS    an IOKit power assertion, kIOPMAssertionTypePreventUserIdleSystemSleep.
//   Linux    a logind "sleep:idle" block, held by a `systemd-inhibit ... cat` child whose stdin is a pipe from here: if
//            VST Studio exits or crashes the pipe closes, cat ends and the block goes with it.  Desktops without logind
//            (no systemd) can't be held this way; the user is told to turn sleep off instead.
//
// want(true/false) is cheap and meant to be called every frame; it only acts on a change, and on Linux checks that the
// child is still there.
#pragma once
#include <string>

class KeepAwake {
public:
	enum State {
		Off,        // not asked to
		Starting,   // asked; Linux: the child hasn't proved itself yet (about a second)
		Held,       // idle sleep is held off
		Failed,     // asked, but this system wouldn't do it (why())
	};
	KeepAwake() = default;
	~KeepAwake() { want(false); }
	KeepAwake(const KeepAwake&) = delete;
	KeepAwake& operator=(const KeepAwake&) = delete;

	void want(bool on);
	State state() const { return state_; }
	// Why it failed, in plain words, when state() is Failed.
	const std::string& why() const { return why_; }
	// Where this system's own setting is, for "turn sleep off" advice: "Settings > System > Power".
	static const char* settingsHint();

private:
	void start();
	void stop();
	void poll();
	State state_ = Off;
	std::string why_;
	double startedAt_ = 0;
#if defined(__APPLE__)
	unsigned assertion_ = 0;
#elif !defined(_WIN32)
	int pid_ = -1, pipe_ = -1;
#endif
};
