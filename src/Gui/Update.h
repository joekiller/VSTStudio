// Updating VST Studio from GitHub Releases: the decisions, with no network, UI or OS code (UpdateOs.h does those).
//
// Where an update comes from.  A release is a tag push (release.yml); GitHub's immutable releases lock its files
// once it is published, and the release is published only after every platform's package is up and SHA256SUMS is
// signed.  So https://api.github.com/repos/<repo>/releases/latest, which never returns a draft or a pre-release, names a
// finished release, and each asset in it carries GitHub's own sha256 of the stored file ("digest": "sha256:...").
//
// What Studio downloads, one file per platform:
//   Windows  VSTStudio-<ver>-windows-x64-setup.exe   the signed Inno Setup installer, run silently in place
//   macOS    VSTStudio-<ver>-macos-arm64.tar.gz      the signed, notarized VSTStudio.app (the disk image's twin)
// Linux packages are .deb files installed by hand, so Linux only hears that a new version exists.
//
// What has to hold before anything is installed (Updater.cpp runs these in order; any failure stops it):
//   1. the file's size and sha256 equal the API's digest for it, and its line in the release's SHA256SUMS;
//   2. the operating system says the file is signed by the same developer as the copy that is running (UpdateOs.h):
//      Authenticode on Windows, Developer ID + notarization on macOS.  A build that isn't signed (pull requests,
//      a self-built copy) can tell you about a new version but never installs one.
// (1) proves the bytes are the ones the release holds; (2) is the part a GitHub account compromise can't fake, since
// the signing keys live in Azure and Apple, not in the repository.
#pragma once
#include <cstdint>
#include <string>

namespace update {

constexpr const char* kRepo = "joekiller/VSTStudio";
constexpr const char* kReleasesPage = "https://github.com/joekiller/VSTStudio/releases/latest";
// A day between automatic checks: a release is at most a few a week, and GitHub allows 60 unsigned API calls an hour.
constexpr double kCheckEverySeconds = 24 * 3600;
// The first automatic check waits this long after launch, so it never competes with the camera reconnecting.
constexpr double kFirstCheckDelaySeconds = 20;

// A release version: "2.1.0" or "v2.1.0".  Anything else (a pull request's "0.0.0~pr75", a main build's
// "0.0.0~abc1234", "dev") is not a release: valid stays false and nothing compares newer than it or to it.
struct Version {
	int major = 0, minor = 0, patch = 0;
	bool valid = false;
	static Version parse(const std::string& text);
	std::string str() const;   // "2.1.0"
	bool newerThan(const Version& o) const;
};

enum class Platform { Windows, Mac, Linux };
Platform thisPlatform();

struct Asset {
	std::string name, url;      // url: browser_download_url (github.com, which redirects to its file store)
	std::uint64_t size = 0;
	std::string sha256;         // lowercase hex from the API's "digest"; "" when the API gave none
	bool found() const { return !url.empty(); }
};

// What /releases/latest says, cut down to what Studio needs.
struct Release {
	Version version;
	std::string tag, page, notes, published;   // page: the release's web page; notes: its description (Markdown)
	Asset package;   // this platform's file (none on Linux)
	Asset sums;      // SHA256SUMS
};

// The package file name a release has for a platform ("" on Linux).
std::string packageName(Platform p, const Version& v);

// Reads the JSON of /releases/latest.  False with `error` in plain words when it isn't a usable release: not JSON, no
// tag, a tag that isn't a version, a draft or pre-release (the endpoint shouldn't return those), or, for Windows and
// macOS, no package for this platform or no SHA256SUMS.
bool parseLatest(const std::string& json, Platform p, Release& out, std::string& error);

// The sha256 a SHA256SUMS file lists for `name` (lowercase hex), "" if it isn't listed.  Accepts both "hash  name" and
// sha256sum's binary-mode "hash *name".
std::string sumFor(const std::string& sha256sums, const std::string& name);

// The release notes as plain lines for the prompt: headings and GitHub's "Full Changelog" footer and "by @who in <pull
// request>" tails dropped, bullets and links simplified, at most `maxLines` lines (an ellipsis line when cut).
std::string notesPreview(const std::string& markdown, int maxLines);

// Whether an automatic check is due: never for a build that isn't a release, never when turned off, otherwise when
// the last one was kCheckEverySeconds ago (or the clock went backwards).
bool checkDue(bool autoCheck, const Version& running, double lastCheckEpoch, double nowEpoch);

// Whether to ask the user about `latest`: it must be newer than what runs, and not the version they chose to skip.
bool shouldOffer(const Version& running, const Version& latest, const std::string& skipped);

}  // namespace update
