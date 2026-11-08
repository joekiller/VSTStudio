// UpdateOs.h has the design; this is macOS.  Compiled with ARC (src/Gui/CMakeLists.txt).
//
// Install, in order:
//   1. verifyAndStage() unpacks the release's tarball with /usr/bin/tar into a hidden folder beside the running
//      VSTStudio.app (".VSTStudio-update" in /Applications, typically), so the swap later is a rename on one volume.
//   2. It checks the unpacked VSTStudio.app with Security.framework: every nested binary strictly valid, and the
//      requirement  anchor apple generic and certificate leaf[subject.OU] = "<our Team ID>" and identifier "<ours>"
//      and notarized,  where the Team ID and identifier are read from the running app's own signature.  So only an
//      app Apple notarized for the same developer passes; nothing about the developer is compiled in.
//   3. install() starts /bin/sh with a short script and Studio quits.  The script waits for Studio's process to end,
//      moves the old app into the hidden folder, moves the new one into its place (moving the old one back if that
//      fails), removes the folder and opens the new app.
#include "UpdateOs.h"
#import <Foundation/Foundation.h>
#include <Security/Security.h>
#include <errno.h>
#include <spawn.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <system_error>
#include <vector>

extern char** environ;

#ifndef VST_VERSION
#define VST_VERSION "dev"
#endif

namespace fs = std::filesystem;

namespace updateos {

namespace {

std::string str(NSString* s) { return s ? std::string([s UTF8String]) : std::string(); }
NSString* ns(const std::string& s) { return [NSString stringWithUTF8String:s.c_str()]; }

// The running app's bundle, "" when Studio isn't running from one (a build tree).
std::string bundlePath() {
	NSString* p = [[NSBundle mainBundle] bundlePath];
	return [p hasSuffix:@".app"] ? str(p) : std::string();
}

std::string bundleId() { return str([[NSBundle mainBundle] bundleIdentifier]); }

std::string cfError(CFErrorRef e) {
	if (!e) return "";
	NSError* err = (__bridge NSError*)e;
	return str([err localizedDescription]);
}

// Runs a program and waits; true when it exits 0.
bool run(const char* path, std::initializer_list<std::string> args, std::string& error) {
	std::vector<std::string> keep{ path };
	keep.insert(keep.end(), args.begin(), args.end());
	std::vector<char*> argv;
	for (std::string& a : keep) argv.push_back(&a[0]);
	argv.push_back(nullptr);
	pid_t pid = 0;
	int rc = posix_spawn(&pid, path, nullptr, nullptr, argv.data(), environ);
	if (rc != 0) { error = std::string("couldn't start ") + path + ": " + strerror(rc); return false; }
	int status = 0;
	while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) { error = std::string(path) + " failed"; return false; }
	return true;
}

}  // namespace

std::string requirementText(const std::string& team, const std::string& id) {
	return "anchor apple generic and certificate leaf[subject.OU] = \"" + team + "\" and identifier \"" + id + "\" and notarized";
}

bool requirementParses(const std::string& text, std::string& error) {
	@autoreleasepool {
		SecRequirementRef req = nullptr;
		CFErrorRef cfErr = nullptr;
		OSStatus s = SecRequirementCreateWithStringAndErrors((__bridge CFStringRef)ns(text), kSecCSDefaultFlags, &cfErr, &req);
		if (s != errSecSuccess) error = cfError(cfErr).empty() ? "error " + std::to_string(int(s)) : cfError(cfErr);
		if (cfErr) CFRelease(cfErr);
		if (req) CFRelease(req);
		return s == errSecSuccess;
	}
}

std::string downloadDir() {
	std::error_code ec;
	fs::path d = fs::path(str(NSTemporaryDirectory())) / "VSTStudio-update";
	fs::create_directories(d, ec);
	return d.string();
}

std::string selfSigner() {
	SecCodeRef self = nullptr;
	if (SecCodeCopySelf(kSecCSDefaultFlags, &self) != errSecSuccess || !self) return "";
	std::string team;
	CFDictionaryRef info = nullptr;
	// A SecCodeRef is also a SecStaticCodeRef for this call.
	if (SecCodeCopySigningInformation((SecStaticCodeRef)self, kSecCSSigningInformation, &info) == errSecSuccess && info) {
		NSDictionary* d = (__bridge NSDictionary*)info;
		id t = d[(__bridge NSString*)kSecCodeInfoTeamIdentifier];
		if ([t isKindOfClass:[NSString class]]) team = str(t);
		CFRelease(info);
	}
	CFRelease(self);
	return team;   // ad-hoc and unsigned builds have no Team ID
}

bool canInstall(std::string& why) {
	std::string app = bundlePath();
	if (app.empty()) { why = "This copy of VST Studio isn't running from VSTStudio.app, so there's no app to replace."; return false; }
	if (selfSigner().empty()) {
		why = "This copy of VST Studio isn't a signed release, so it can't check that an update comes from the same developer.";
		return false;
	}
	// macOS runs a downloaded app from a random read-only place ("App Translocation") until it is moved, and the disk
	// image itself is read-only: either way there's nowhere to put the new version.
	if (app.find("/AppTranslocation/") != std::string::npos || app.rfind("/Volumes/", 0) == 0) {
		why = "VST Studio is running from the disk image or the Downloads folder. Quit it, drag VSTStudio.app into Applications, "
			"open it from there, and updates can install themselves.";
		return false;
	}
	std::string parent = fs::path(app).parent_path().string();
	if (access(parent.c_str(), W_OK) != 0 || access(app.c_str(), W_OK) != 0) {
		why = "This Mac's account can't change " + parent + ". Sign in as an administrator to update, or download the new version.";
		return false;
	}
	return true;
}

bool fetch(const std::string& url, std::string* body, const std::string& toFile,
	const std::function<bool(std::uint64_t, std::uint64_t)>& progress, std::string& error) {
	@autoreleasepool {
		NSURL* u = [NSURL URLWithString:ns(url)];
		if (!u || ![[u scheme] isEqualToString:@"https"]) { error = "Only https:// addresses are used for updates"; return false; }
		NSMutableURLRequest* req = [NSMutableURLRequest requestWithURL:u cachePolicy:NSURLRequestReloadIgnoringLocalCacheData timeoutInterval:30];
		[req setValue:ns(std::string("VSTStudio/") + VST_VERSION) forHTTPHeaderField:@"User-Agent"];
		if ([[u host] isEqualToString:@"api.github.com"]) {
			[req setValue:@"application/vnd.github+json" forHTTPHeaderField:@"Accept"];
			[req setValue:@"2022-11-28" forHTTPHeaderField:@"X-GitHub-Api-Version"];
		}
		NSURLSession* session = [NSURLSession sessionWithConfiguration:[NSURLSessionConfiguration ephemeralSessionConfiguration]];
		dispatch_semaphore_t done = dispatch_semaphore_create(0);
		__block NSInteger status = 0;
		__block NSString* failure = nil;
		__block NSData* data = nil;
		__block bool moved = false;
		NSString* dest = toFile.empty() ? nil : ns(toFile);
		NSURLSessionTask* task = nil;
		if (dest) {
			task = [session downloadTaskWithRequest:req completionHandler:^(NSURL* location, NSURLResponse* resp, NSError* err) {
				if (err) failure = [err localizedDescription];
				else {
					status = [(NSHTTPURLResponse*)resp statusCode];
					// The temporary file is deleted when this handler returns, so move it now.
					NSFileManager* fm = [NSFileManager defaultManager];
					[fm removeItemAtPath:dest error:nil];
					NSError* mvErr = nil;
					moved = status == 200 && [fm moveItemAtURL:location toURL:[NSURL fileURLWithPath:dest] error:&mvErr];
					if (status == 200 && !moved) failure = [mvErr localizedDescription];
				}
				dispatch_semaphore_signal(done);
			}];
		}
		else {
			task = [session dataTaskWithRequest:req completionHandler:^(NSData* d, NSURLResponse* resp, NSError* err) {
				if (err) failure = [err localizedDescription];
				else { status = [(NSHTTPURLResponse*)resp statusCode]; data = d; }
				dispatch_semaphore_signal(done);
			}];
		}
		[task resume];
		bool cancelled = false;
		while (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 200 * NSEC_PER_MSEC)) != 0) {
			int64_t got = [task countOfBytesReceived], total = [task countOfBytesExpectedToReceive];
			if (!cancelled && progress && !progress(std::uint64_t(got > 0 ? got : 0), std::uint64_t(total > 0 ? total : 0))) {
				cancelled = true;
				[task cancel];
			}
		}
		[session finishTasksAndInvalidate];
		if (cancelled) { error = "Cancelled"; return false; }
		if (failure) { error = "Couldn't download: " + str(failure); return false; }
		if (status != 200) {
			error = str([u host]) + " answered HTTP " + std::to_string(long(status));
			if (status == 403 && [[u host] isEqualToString:@"api.github.com"]) error += " (too many checks from this network for now; it clears within an hour)";
			return false;
		}
		if (body && data) {
			if ([data length] > (4u << 20)) { error = "The answer was larger than expected"; return false; }
			body->assign(static_cast<const char*>([data bytes]), [data length]);
		}
		if (progress) {
			int64_t got = [task countOfBytesReceived];
			progress(std::uint64_t(got > 0 ? got : 0), std::uint64_t(got > 0 ? got : 0));
		}
		return dest ? moved : true;
	}
}

bool verifyAndStage(const std::string& package, const std::string& signer, std::string& staged, std::string& error) {
	@autoreleasepool {
		std::string app = bundlePath();
		if (app.empty() || signer.empty()) { error = "This copy can't install updates."; return false; }
		fs::path stage = fs::path(app).parent_path() / ".VSTStudio-update";
		std::error_code ec;
		fs::remove_all(stage, ec);   // a half-finished earlier attempt
		if (!fs::create_directories(stage, ec)) { error = "Couldn't make a folder beside VST Studio: " + ec.message(); return false; }
		std::string why;
		if (!run("/usr/bin/tar", { "-xzf", package, "-C", stage.string() }, why)) {
			fs::remove_all(stage, ec);
			error = "The download couldn't be unpacked (" + why + ").";
			return false;
		}
		// The tarball holds VSTStudio-<version>-macos-arm64/VSTStudio.app (macos.yml "Tar and disk image").
		fs::path found;
		for (const fs::directory_entry& e : fs::directory_iterator(stage, ec))
			if (e.is_directory() && fs::exists(e.path() / "VSTStudio.app")) found = e.path() / "VSTStudio.app";
		if (found.empty()) { fs::remove_all(stage, ec); error = "The download has no VSTStudio.app in it."; return false; }

		SecStaticCodeRef code = nullptr;
		if (SecStaticCodeCreateWithPath((__bridge CFURLRef)[NSURL fileURLWithPath:ns(found.string())], kSecCSDefaultFlags, &code) != errSecSuccess) {
			fs::remove_all(stage, ec);
			error = "macOS couldn't read the new app's signature.";
			return false;
		}
		SecRequirementRef req = nullptr;
		CFErrorRef cfErr = nullptr;
		NSString* reqText = ns(requirementText(signer, bundleId()));
		OSStatus s = SecRequirementCreateWithStringAndErrors((__bridge CFStringRef)reqText, kSecCSDefaultFlags, &cfErr, &req);
		if (s == errSecSuccess) {
			if (cfErr) { CFRelease(cfErr); cfErr = nullptr; }
			s = SecStaticCodeCheckValidityWithErrors(code, kSecCSCheckAllArchitectures | kSecCSCheckNestedCode | kSecCSStrictValidate, req, &cfErr);
		}
		std::string detail = cfError(cfErr);
		if (cfErr) CFRelease(cfErr);
		if (req) CFRelease(req);
		CFRelease(code);
		if (s != errSecSuccess) {
			fs::remove_all(stage, ec);
			error = "The new app isn't signed and notarized by the same developer as this one, so it wasn't installed"
				+ (detail.empty() ? std::string(".") : " (" + detail + ").");
			return false;
		}
		staged = found.string();
		return true;
	}
}

bool install(const std::string& staged, bool& quitNow, std::string& error) {
	quitNow = false;
	std::string app = bundlePath();
	if (app.empty()) { error = "This copy can't install updates."; return false; }
	std::string stage = fs::path(app).parent_path().string() + "/.VSTStudio-update";
	// $1 Studio's pid, $2 the app to replace, $3 the verified new app, $4 the staging folder.  Gives up after a minute if
	// Studio never exits, leaving the old app where it was.
	static const char* script =
		"i=0; while kill -0 \"$1\" 2>/dev/null; do sleep 0.2; i=$((i+1)); [ $i -gt 300 ] && exit 1; done\n"
		"mv \"$2\" \"$4/previous.app\" || { open \"$2\"; exit 1; }\n"
		"if mv \"$3\" \"$2\"; then rm -rf \"$4\"; else mv \"$4/previous.app\" \"$2\"; fi\n"
		"open \"$2\"\n";
	std::string pid = std::to_string(getpid());
	std::vector<std::string> keep{ "/bin/sh", "-c", script, "vst-update", pid, app, staged, stage };
	std::vector<char*> argv;
	for (std::string& a : keep) argv.push_back(&a[0]);
	argv.push_back(nullptr);
	posix_spawnattr_t attr;
	posix_spawnattr_init(&attr);
#ifdef POSIX_SPAWN_SETSID
	posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID);   // its own session, so it outlives Studio
#endif
	pid_t child = 0;
	int rc = posix_spawn(&child, "/bin/sh", nullptr, &attr, argv.data(), environ);
	posix_spawnattr_destroy(&attr);
	if (rc != 0) { error = std::string("Couldn't start the update: ") + strerror(rc); return false; }
	quitNow = true;
	return true;
}

}  // namespace updateos
