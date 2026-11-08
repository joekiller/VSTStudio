// KeepAwake.h has the design.  One platform block each; every one of them only ever holds off *idle* sleep.
#include "KeepAwake.h"
#include <chrono>

#if defined(_WIN32)
#include <windows.h>
#include <powerbase.h>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/pwr_mgt/IOPMLib.h>
#else
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace {
double seconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
}

void KeepAwake::want(bool on) {
	if (on && state_ == Off) start();
	else if (!on && state_ != Off) stop();
	else if (on) poll();
}

const char* KeepAwake::settingsHint() {
#if defined(_WIN32)
	return "Settings > System > Power & battery > Screen and sleep, \"put my device to sleep\": Never (when plugged in)";
#elif defined(__APPLE__)
	return "System Settings > Energy (or Battery > Options): \"Prevent automatic sleeping when the display is off\"";
#else
	return "your desktop's Power settings: Automatic Suspend off (when plugged in)";
#endif
}

#if defined(_WIN32)
void KeepAwake::start() {
	// On a Modern Standby PC (most laptops and many new mini PCs) turning the screen off on idle *is* going to sleep, so
	// there the screen has to stay on too.  On the rest the screen can still turn off.
	SYSTEM_POWER_CAPABILITIES caps{};
	bool modernStandby = CallNtPowerInformation(SystemPowerCapabilities, nullptr, 0, &caps, sizeof caps) == 0 && caps.AoAc;
	EXECUTION_STATE flags = ES_CONTINUOUS | ES_SYSTEM_REQUIRED | (modernStandby ? ES_DISPLAY_REQUIRED : 0);
	if (SetThreadExecutionState(flags)) { state_ = Held; why_ = modernStandby ? "screen stays on too, since on this PC a dark screen means sleep" : ""; }
	else { state_ = Failed; why_ = "Windows didn't take the request"; }
}
void KeepAwake::stop() {
	SetThreadExecutionState(ES_CONTINUOUS);   // back to the system's own idle timers
	state_ = Off;
	why_.clear();
}
void KeepAwake::poll() {}

#elif defined(__APPLE__)
void KeepAwake::start() {
	IOPMAssertionID id = 0;
	IOReturn r = IOPMAssertionCreateWithName(kIOPMAssertionTypePreventUserIdleSystemSleep, kIOPMAssertionLevelOn,
		CFSTR("VST Studio is timing traffic on a live camera"), &id);
	if (r == kIOReturnSuccess) { assertion_ = id; state_ = Held; why_.clear(); }
	else { state_ = Failed; why_ = "macOS didn't take the power assertion"; }
}
void KeepAwake::stop() {
	if (state_ == Held) IOPMAssertionRelease(IOPMAssertionID(assertion_));
	assertion_ = 0;
	state_ = Off;
	why_.clear();
}
void KeepAwake::poll() {}

#else
void KeepAwake::start() {
	int fds[2];
	if (pipe2(fds, O_CLOEXEC) != 0) { state_ = Failed; why_ = std::string("couldn't make a pipe: ") + std::strerror(errno); return; }
	posix_spawn_file_actions_t fa;
	posix_spawn_file_actions_init(&fa);
	posix_spawn_file_actions_adddup2(&fa, fds[0], 0);   // cat reads the pipe: EOF when this end closes, however VST Studio ends
	posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
	posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
	char a0[] = "systemd-inhibit", a1[] = "--what=sleep:idle", a2[] = "--who=VST Studio",
		a3[] = "--why=Timing traffic on a live camera", a4[] = "--mode=block", a5[] = "cat";
	char* argv[] = { a0, a1, a2, a3, a4, a5, nullptr };
	pid_t pid = -1;
	int rc = posix_spawnp(&pid, "systemd-inhibit", &fa, nullptr, argv, environ);
	posix_spawn_file_actions_destroy(&fa);
	close(fds[0]);
	if (rc != 0) {
		close(fds[1]);
		state_ = Failed;
		why_ = rc == ENOENT ? "systemd-inhibit isn't installed" : std::string("couldn't start systemd-inhibit: ") + std::strerror(rc);
		return;
	}
	pid_ = pid;
	pipe_ = fds[1];
	state_ = Starting;
	startedAt_ = seconds();
}
void KeepAwake::stop() {
	if (pipe_ >= 0) close(pipe_);
	pipe_ = -1;
	if (pid_ > 0) {
		kill(pid_, SIGTERM);
		waitpid(pid_, nullptr, 0);
	}
	pid_ = -1;
	state_ = Off;
	why_.clear();
}
// systemd-inhibit ends at once when logind refuses (no systemd, no D-Bus): still running after a second means it holds.
void KeepAwake::poll() {
	if (pid_ <= 0 || state_ == Failed) return;
	int status = 0;
	if (waitpid(pid_, &status, WNOHANG) == pid_) {
		pid_ = -1;
		if (pipe_ >= 0) close(pipe_);
		pipe_ = -1;
		state_ = Failed;
		why_ = "this system's login manager (logind) wouldn't hold off sleep";
		return;
	}
	if (state_ == Starting && seconds() - startedAt_ >= 1) state_ = Held;
}
#endif
