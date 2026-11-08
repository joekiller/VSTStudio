// Updating VST Studio from GitHub Releases (src/Gui/Update.h, Updater.h, UpdateOs.h) unit test.
//
//   update_test <tests dir> <work dir>
//
// No network.  The fixtures in tests/fixtures/update/ are the real /releases/latest answer and SHA256SUMS of v2.1.0
// (uploader fields and the like trimmed), so the parsing is held to what GitHub actually sends.  Checks:
//   - versions: only a plain x.y.z (or vx.y.z) is a release; pull-request and main builds never update or get offered one
//   - /releases/latest: the right package per platform, its sha256 from the API, SHA256SUMS found; error answers,
//     drafts and releases missing a package read as failures in plain words
//   - SHA256SUMS lookups, both sha256sum line styles, and no match on a name that is only a prefix
//   - the prompt's notes preview, when an automatic check is due, and that a skipped version isn't offered again
//   - on this OS, the calls an update makes before installing: an unsigned build (every CI build) has no signer, can't
//     install and says why, and a file that isn't signed fails verification; on macOS, the code requirement parses
//   - the Updater state machine starts idle and refuses to download or install before a check
#include "Update.h"
#include "UpdateOs.h"
#include "Updater.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using namespace update;

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

static std::string slurp(const fs::path& p) {
	std::ifstream in(p, std::ios::binary);
	std::ostringstream s;
	s << in.rdbuf();
	return s.str();
}

static void testVersions() {
	Version v = Version::parse("v2.1.0");
	CHECK(v.valid && v.major == 2 && v.minor == 1 && v.patch == 0);
	CHECK(Version::parse("2.1.0").str() == "2.1.0");
	CHECK(Version::parse("10.20.30").valid);
	// Not releases: what pull requests, main builds and self-built copies call themselves, and anything odd.
	for (const char* s : { "0.0.0~pr75", "0.0.0~abc1234", "0.0.0", "dev", "", "2.1", "2.1.0-rc1", "2.1.0.1", "v", "2..0", "a.b.c", "1234567.0.0" })
		CHECK(!Version::parse(s).valid);
	CHECK(Version::parse("2.1.0").newerThan(Version::parse("2.0.9")));
	CHECK(Version::parse("2.10.0").newerThan(Version::parse("2.9.0")));   // numbers, not text
	CHECK(Version::parse("3.0.0").newerThan(Version::parse("2.99.99")));
	CHECK(!Version::parse("2.1.0").newerThan(Version::parse("2.1.0")));
	CHECK(!Version::parse("2.0.0").newerThan(Version::parse("2.1.0")));
	// Nothing is newer than a build that isn't a release, so a test build never offers to replace itself.
	CHECK(!Version::parse("9.9.9").newerThan(Version::parse("0.0.0~pr75")));
}

static void testLatest(const fs::path& fixtures) {
	std::string json = slurp(fixtures / "latest-v2.1.0.json");
	CHECK(!json.empty());
	Release r;
	std::string err;
	CHECK(parseLatest(json, Platform::Windows, r, err));
	CHECK(r.tag == "v2.1.0" && r.version.str() == "2.1.0");
	CHECK(r.package.name == "VSTStudio-2.1.0-windows-x64-setup.exe");
	CHECK(r.package.url == "https://github.com/joekiller/VSTStudio/releases/download/v2.1.0/VSTStudio-2.1.0-windows-x64-setup.exe");
	CHECK(r.package.size == 98687624);
	CHECK(r.package.sha256 == "083f56d268945b1c7c6d6a538f55eedb671c76226654f8a5f948923b272467f9");
	CHECK(r.sums.found() && r.sums.name == "SHA256SUMS");
	CHECK(r.page == "https://github.com/joekiller/VSTStudio/releases/tag/v2.1.0");

	CHECK(parseLatest(json, Platform::Mac, r, err));
	CHECK(r.package.name == "VSTStudio-2.1.0-macos-arm64.tar.gz");   // the tarball, not the disk image
	CHECK(r.package.sha256 == "39bf1cbecfa2089a0b7fd1005715f058a4e0e88113d5d630e884a9bfee45e08d");

	CHECK(parseLatest(json, Platform::Linux, r, err));   // version and notes only
	CHECK(!r.package.found() && r.version.str() == "2.1.0");

	// GitHub's error answer (rate limit), not JSON at all, a draft, a tag that isn't a version, a release whose
	// Windows installer isn't up yet, and one without SHA256SUMS.
	CHECK(!parseLatest("{\"message\":\"API rate limit exceeded for 1.2.3.4.\"}", Platform::Windows, r, err));
	CHECK(err.find("rate limit") != std::string::npos);
	CHECK(!parseLatest("<html>proxy error</html>", Platform::Mac, r, err) && !err.empty());
	CHECK(!parseLatest("{\"tag_name\":\"v3.0.0\",\"draft\":true,\"assets\":[]}", Platform::Windows, r, err));
	CHECK(!parseLatest("{\"tag_name\":\"nightly\",\"assets\":[]}", Platform::Windows, r, err));
	std::string noExe = "{\"tag_name\":\"v3.0.0\",\"assets\":[{\"name\":\"SHA256SUMS\",\"browser_download_url\":\"https://x/SHA256SUMS\",\"size\":10}]}";
	CHECK(!parseLatest(noExe, Platform::Windows, r, err) && err.find("VSTStudio-3.0.0-windows-x64-setup.exe") != std::string::npos);
	std::string noSums = "{\"tag_name\":\"v3.0.0\",\"assets\":[{\"name\":\"VSTStudio-3.0.0-macos-arm64.tar.gz\",\"browser_download_url\":\"https://x/a\",\"size\":10}]}";
	CHECK(!parseLatest(noSums, Platform::Mac, r, err) && err.find("SHA256SUMS") != std::string::npos);
	// A digest that isn't sha256 is ignored (the download then rests on SHA256SUMS alone).
	std::string md5 = "{\"tag_name\":\"v3.0.0\",\"assets\":[{\"name\":\"SHA256SUMS\",\"browser_download_url\":\"https://x/s\"},"
		"{\"name\":\"VSTStudio-3.0.0-macos-arm64.tar.gz\",\"browser_download_url\":\"https://x/a\",\"digest\":\"md5:abc\"}]}";
	CHECK(parseLatest(md5, Platform::Mac, r, err) && r.package.sha256.empty());
}

static void testSums(const fs::path& fixtures) {
	std::string sums = slurp(fixtures / "SHA256SUMS-v2.1.0");
	CHECK(sumFor(sums, "VSTStudio-2.1.0-windows-x64-setup.exe") == "083f56d268945b1c7c6d6a538f55eedb671c76226654f8a5f948923b272467f9");
	CHECK(sumFor(sums, "VSTStudio-2.1.0-macos-arm64.tar.gz") == "39bf1cbecfa2089a0b7fd1005715f058a4e0e88113d5d630e884a9bfee45e08d");
	CHECK(sumFor(sums, "VSTStudio-2.1.0-windows-x64").empty());   // a prefix of a listed name is not that name
	CHECK(sumFor(sums, "nothing.exe").empty());
	std::string binary = std::string(64, 'A') + " *file.bin\r\n";   // sha256sum -b style, CRLF, upper case
	CHECK(sumFor(binary, "file.bin") == std::string(64, 'a'));
	CHECK(sumFor("short  file.bin\n", "file.bin").empty());
}

static void testNotesAndTiming(const fs::path& fixtures) {
	Release r;
	std::string err;
	CHECK(parseLatest(slurp(fixtures / "latest-v2.1.0.json"), Platform::Windows, r, err));
	std::string notes = notesPreview(r.notes, 10);
	CHECK(notes.find("Review false positives: leave a pass out, put it back, or delete it") != std::string::npos);
	CHECK(notes.find("What's Changed") == std::string::npos);
	CHECK(notes.find("Full Changelog") == std::string::npos);
	CHECK(notes.find("@joekiller") == std::string::npos && notes.find("https://") == std::string::npos);
	CHECK(notes.rfind("\xe2\x80\xa2 ", 0) == 0);   // bullets
	CHECK(notesPreview(r.notes, 1).find("\xe2\x80\xa6") != std::string::npos);   // cut, with an ellipsis
	CHECK(notesPreview("* see [the docs](https://x/y) for **more**", 5) == "\xe2\x80\xa2 see the docs for more");

	Version rel = Version::parse("2.0.0"), test = Version::parse("0.0.0~pr75");
	double day = kCheckEverySeconds, t = 1.8e9;
	CHECK(checkDue(true, rel, 0, t));               // never checked
	CHECK(!checkDue(true, rel, t - day / 2, t));    // checked this morning
	CHECK(checkDue(true, rel, t - day, t));         // a day ago
	CHECK(checkDue(true, rel, t + 3600, t));        // the clock went backwards
	CHECK(!checkDue(false, rel, 0, t));             // turned off
	CHECK(!checkDue(true, test, 0, t));             // not a release build

	Version latest = Version::parse("2.1.0");
	CHECK(shouldOffer(rel, latest, ""));
	CHECK(!shouldOffer(rel, latest, "2.1.0"));      // "Skip this version"
	CHECK(shouldOffer(rel, Version::parse("2.2.0"), "2.1.0"));   // ...until the next one
	CHECK(!shouldOffer(latest, latest, ""));
	CHECK(!shouldOffer(test, latest, ""));
}

// What the OS side does on a build like this one, which no CI run signs.
static void testOs(const fs::path& work) {
	CHECK(updateos::selfSigner().empty());
	std::string why;
	CHECK(!updateos::canInstall(why));
	CHECK(!why.empty());
	std::printf("canInstall: no, because: %s\n", why.c_str());
	fs::create_directories(work);
	fs::path fake = work / "VSTStudio-9.9.9-windows-x64-setup.exe";
	{ std::ofstream(fake, std::ios::binary) << "MZ not really a program"; }
	std::string staged, err;
	CHECK(!updateos::verifyAndStage(fake.u8string(), "", staged, err));
	CHECK(!err.empty());
	std::printf("verifyAndStage of an unsigned file: %s\n", err.c_str());
	CHECK(!updateos::downloadDir().empty() && fs::is_directory(fs::u8path(updateos::downloadDir())));
#ifdef __APPLE__
	std::string req = updateos::requirementText("ABCDE12345", "io.github.joekiller.vststudio");
	CHECK(req.find("ABCDE12345") != std::string::npos && req.find("notarized") != std::string::npos);
	std::string reqErr;
	CHECK(updateos::requirementParses(req, reqErr));
	if (!reqErr.empty()) std::printf("requirement: %s\n", reqErr.c_str());
	CHECK(!updateos::requirementParses("anchor apple generic and nonsense(", reqErr));
#endif
}

static void testUpdater() {
	Updater u("2.0.0");
	CHECK(u.running().str() == "2.0.0");
	CHECK(u.status().state == Updater::State::Idle);
	u.download();   // nothing found yet: ignored
	CHECK(u.status().state == Updater::State::Idle && !u.busy());
	std::string err;
	bool quitNow = true;
	CHECK(!u.install(quitNow, err) && !err.empty() && !quitNow);
}

int main(int argc, char** argv) {
	if (argc < 3) { std::printf("usage: update_test <tests dir> <work dir>\n"); return 2; }
	fs::path fixtures = fs::path(argv[1]) / "fixtures" / "update", work = argv[2];
	testVersions();
	testLatest(fixtures);
	testSums(fixtures);
	testNotesAndTiming(fixtures);
	testOs(work);
	testUpdater();
	std::printf("update_test: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
