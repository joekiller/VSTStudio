// Checking for, downloading and installing a VST Studio update, off the GUI thread.  Update.h has where updates come
// from and what is checked; UpdateOs.h does the platform parts; App (AppUpdate.cpp) decides when to ask the user.
//
// One action at a time runs on a worker thread; the GUI reads status() each frame, which never blocks:
//
//   Idle --check()--> Checking --> UpToDate | Available | Failed
//   Available --download()--> Downloading --> Verifying --> Ready | Failed
//   Ready --install()--> (the installer or swap script runs; the app must quit at once)
//
// A download is checked twice before Ready: its sha256 must equal the API's digest and the release's SHA256SUMS line,
// then updateos::verifyAndStage() checks its code signature against the running copy's signer.  A file already in the
// download folder with the right sha256 isn't fetched again (Later, then Update now, after a restart).
#pragma once
#include "Update.h"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

class Updater {
public:
	enum class State { Idle, Checking, UpToDate, Available, Downloading, Verifying, Ready, Failed };
	struct Status {
		State state = State::Idle;
		update::Release release;   // from the last good check
		std::string error;         // Failed: what went wrong, in plain words
		bool manual = false;       // the action was asked for by the user (an automatic check's failure stays quiet)
		std::uint64_t got = 0, total = 0;   // Downloading
		bool canInstall = false;   // updateos::canInstall()
		std::string cantInstall;   // why not, when it can't
	};

	explicit Updater(const std::string& runningVersion);
	~Updater();   // cancels and waits for the worker
	Updater(const Updater&) = delete;
	Updater& operator=(const Updater&) = delete;

	const update::Version& running() const { return running_; }
	Status status() const;
	bool busy() const { return busy_; }

	void check(bool manual);   // ignored while busy
	void download();           // from Available (or Failed after a good check); ignored otherwise
	void cancel();             // stops a download; the state goes back to Available
	// From Ready: starts the installer (updateos::install, which says whether the app must quit now).  False leaves the
	// state as it was, with `error`.
	bool install(bool& quitNow, std::string& error);
	// --shots only: shows a made-up state (no network), so the documentation tour can capture the prompt.
	void pretend(const Status& s);

private:
	void start(void (Updater::*fn)());
	void doCheck();
	void doDownload();
	void set(State s, const std::string& error = "");

	update::Version running_;
	mutable std::mutex mu_;
	Status st_;
	std::string staged_, signer_;
	std::thread worker_;
	std::atomic<bool> busy_{ false }, cancel_{ false };
};
