// The operating-system half of updating (Update.h has the design): HTTPS downloads, code-signature checks, and handing
// over to the installer.  Every function blocks and is called from Updater's worker thread, never the GUI thread.
//
//   Windows (UpdateOs.cpp)   WinHTTP for downloads (the system's proxy settings and certificate store), WinVerifyTrust
//                            for Authenticode, and the Inno Setup installer run with /SILENT in the install's own mode
//                            and folder, which closes nothing but Studio and starts it again afterwards.
//   macOS (UpdateMac.mm)     NSURLSession, Security.framework's static code checks with a requirement naming the
//                            running copy's Team ID and notarization, and a small script that waits for Studio to exit,
//                            swaps the new VSTStudio.app in where the old one was, and opens it.
//   Linux (UpdateOs.cpp)     none of it: .deb packages are installed by hand, so Studio only links to the release.
#pragma once
#include <cstdint>
#include <functional>
#include <string>

namespace updateos {

// Whether this copy can install an update by itself.  False with `why` in plain words otherwise: Linux; a copy that
// isn't signed by the developer (pull-request and self-built copies); on Windows a copy unpacked from the zip rather
// than installed; on macOS a copy run from the disk image or from Downloads before it was moved to Applications.
bool canInstall(std::string& why);

// A stable name for who signed the program that is running ("CN=Joe Lawson, O=Joe Lawson, ..." on Windows, the Team ID
// on macOS), "" when it isn't signed by a developer.
std::string selfSigner();

// GET over HTTPS.  With `toFile` empty the body goes to `body` (capped at 4 MB); otherwise it streams to that file.
// `progress(received, total)` is called as data arrives (total 0 when unknown) and returning false cancels.
// Redirects are followed (release downloads go from github.com to its file store).  False with `error` on any failure,
// including an HTTP status other than 200.
bool fetch(const std::string& url, std::string* body, const std::string& toFile,
	const std::function<bool(std::uint64_t, std::uint64_t)>& progress, std::string& error);

// Checks the downloaded package is signed by `signer` (selfSigner()'s value) and gets it ready to install.  Windows:
// the installer's Authenticode signature, chained to a trusted root, by the same subject.  macOS: unpacks the tarball
// next to the running app (same volume, so the swap is a rename), then checks the VSTStudio.app inside with strict
// validation of all nested code, the same Team ID, the same bundle identifier and Apple's notarization.
// `staged` is what install() takes: the installer's path, or the unpacked app's.
bool verifyAndStage(const std::string& package, const std::string& signer, std::string& staged, std::string& error);

// Starts the installer (Windows) or the swap script (macOS) and returns at once.  VST Studio opens again by itself when it
// is done.  `quitNow` says whether the caller must quit straight away: true on macOS and for a Windows install just for
// this user.  For a Windows install for everyone it is false: Setup first asks Windows for administrator rights (the UAC
// prompt), and only once they're given does it close Studio itself (/CLOSEAPPLICATIONS); if they aren't, Setup ends and
// Studio carries on as if nothing happened, rather than having closed for an update that never came.
bool install(const std::string& staged, bool& quitNow, std::string& error);

// macOS: the code requirement verifyAndStage() holds the new app to, for a Team ID and bundle identifier, and whether
// Security.framework accepts it (tests/update_test.cpp checks the syntax on the macOS CI runner).  Elsewhere "" / true.
std::string requirementText(const std::string& team, const std::string& bundleId);
bool requirementParses(const std::string& text, std::string& error);

// Where downloads go: a folder of their own under the system's temp folder, created if needed.
std::string downloadDir();

}  // namespace updateos
