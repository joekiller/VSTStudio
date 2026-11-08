// UpdateOs.h has the design.  Windows and Linux here; macOS is UpdateMac.mm.
//
// Paths are UTF-8 strings everywhere in Studio; on Windows they go through u8path/wide strings, so a user name with
// accents (C:\Users\José\AppData\Local\Temp) works.
#include "UpdateOs.h"
#include "Update.h"
#include <cstdio>
#include <filesystem>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <softpub.h>
#include <cstdio>
#include <cwchar>
#include <vector>
#endif

#ifndef VST_VERSION
#define VST_VERSION "dev"
#endif

namespace fs = std::filesystem;

namespace updateos {

#if !defined(__APPLE__)
std::string downloadDir() {
	std::error_code ec;
	fs::path d = fs::temp_directory_path(ec) / "VSTStudio-update";
	fs::create_directories(d, ec);
	return d.u8string();
}
#endif

#if !defined(__APPLE__)
std::string requirementText(const std::string&, const std::string&) { return ""; }
bool requirementParses(const std::string&, std::string&) { return true; }
#endif

#if defined(_WIN32)

namespace {

std::wstring wide(const std::string& s) {
	if (s.empty()) return std::wstring();
	int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
	std::wstring w(size_t(n), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), &w[0], n);
	return w;
}

std::string narrow(const std::wstring& w) {
	if (w.empty()) return std::string();
	int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
	std::string s(size_t(n), '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), &s[0], n, nullptr, nullptr);
	return s;
}

std::string lastError(const char* what) {
	DWORD e = GetLastError();
	wchar_t* msg = nullptr;
	// WinHTTP's own codes (12xxx) have their text in winhttp.dll.
	DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_SYSTEM;
	HMODULE mod = nullptr;
	if (e >= 12000 && e < 13000 && (mod = GetModuleHandleW(L"winhttp.dll"))) flags |= FORMAT_MESSAGE_FROM_HMODULE;
	FormatMessageW(flags, mod, e, 0, reinterpret_cast<LPWSTR>(&msg), 0, nullptr);
	std::string text = msg ? narrow(msg) : std::string();
	if (msg) LocalFree(msg);
	while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' || text.back() == '.')) text.pop_back();
	return std::string(what) + ": " + (text.empty() ? "error " + std::to_string(e) : text);
}

fs::path selfPath() {
	std::vector<wchar_t> buf(MAX_PATH);
	for (;;) {
		DWORD n = GetModuleFileNameW(nullptr, buf.data(), DWORD(buf.size()));
		if (n < buf.size()) return fs::path(std::wstring(buf.data(), n));
		buf.resize(buf.size() * 2);
	}
}

// The Authenticode signer of a file, as its subject's X.500 name, when the signature is valid and chains to a root this
// PC trusts; "" otherwise, with `why`.  `revocation` checks every certificate in the chain against its issuer's
// revocation list; when the lists can't be reached (offline, a strict proxy) it falls back to not checking rather than
// refusing every update, but a certificate the lists say is revoked always fails.
std::string signerOf(const fs::path& file, bool revocation, std::string& why) {
	std::wstring path = file.wstring();
	WINTRUST_FILE_INFO fi{};
	fi.cbStruct = sizeof fi;
	fi.pcwszFilePath = path.c_str();
	GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
	auto run = [&](DWORD revoke, std::string& subject) -> LONG {
		WINTRUST_DATA wd{};
		wd.cbStruct = sizeof wd;
		wd.dwUIChoice = WTD_UI_NONE;
		wd.fdwRevocationChecks = revoke;
		wd.dwUnionChoice = WTD_CHOICE_FILE;
		wd.pFile = &fi;
		wd.dwStateAction = WTD_STATEACTION_VERIFY;
		wd.dwProvFlags = revoke == WTD_REVOKE_NONE ? 0 : WTD_REVOCATION_CHECK_CHAIN;
		LONG r = WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &wd);
		if (r == ERROR_SUCCESS) {
			CRYPT_PROVIDER_DATA* pd = WTHelperProvDataFromStateData(wd.hWVTStateData);
			CRYPT_PROVIDER_SGNR* sg = pd ? WTHelperGetProvSignerFromChain(pd, 0, FALSE, 0) : nullptr;
			PCCERT_CONTEXT cert = sg && sg->csCertChain > 0 ? sg->pasCertChain[0].pCert : nullptr;
			if (cert) {
				DWORD n = CertNameToStrW(X509_ASN_ENCODING, &cert->pCertInfo->Subject, CERT_X500_NAME_STR, nullptr, 0);
				std::wstring name(n, L'\0');
				CertNameToStrW(X509_ASN_ENCODING, &cert->pCertInfo->Subject, CERT_X500_NAME_STR, &name[0], n);
				while (!name.empty() && name.back() == L'\0') name.pop_back();
				subject = narrow(name);
			}
		}
		wd.dwStateAction = WTD_STATEACTION_CLOSE;
		WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &wd);
		return r;
	};
	std::string subject;
	LONG r = run(revocation ? WTD_REVOKE_WHOLECHAIN : WTD_REVOKE_NONE, subject);
	if (revocation && (r == LONG(CERT_E_REVOCATION_FAILURE) || r == LONG(CRYPT_E_REVOCATION_OFFLINE))) r = run(WTD_REVOKE_NONE, subject);
	switch (r) {
	case ERROR_SUCCESS:
		if (subject.empty()) why = "the signature has no signer certificate";
		return subject;
	case LONG(TRUST_E_NOSIGNATURE): case LONG(TRUST_E_SUBJECT_FORM_UNKNOWN): case LONG(TRUST_E_PROVIDER_UNKNOWN):
		why = "it isn't signed"; break;
	case LONG(CRYPT_E_REVOKED): why = "its signing certificate has been revoked"; break;
	case LONG(TRUST_E_BAD_DIGEST): why = "it was changed after it was signed"; break;
	case LONG(CERT_E_UNTRUSTEDROOT): case LONG(CERT_E_CHAINING): why = "its signature doesn't chain to a certificate Windows trusts"; break;
	case LONG(CERT_E_EXPIRED): why = "its signature has expired and carries no timestamp"; break;
	default: { char b[16]; std::snprintf(b, sizeof b, "0x%08lx", static_cast<unsigned long>(r)); why = std::string("Windows rejected its signature (") + b + ")"; }
	}
	return "";
}

// How the running copy was installed: Inno Setup's uninstall entry (packaging/windows/vst-setup.iss AppId) under
// HKLM for everyone, HKCU for this user.  Its InstallLocation must be this exe's folder, so an unrelated install
// elsewhere doesn't count.  0: not installed (unpacked from the zip), 1: this user, 2: everyone.
int installMode(const fs::path& dir) {
	const wchar_t* key = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{6F1C2A57-93D8-4B0E-A7C4-5E2D9B81F340}_is1";
	auto matches = [&](HKEY root, REGSAM view) {
		HKEY k = nullptr;
		if (RegOpenKeyExW(root, key, 0, KEY_READ | view, &k) != ERROR_SUCCESS) return false;
		wchar_t buf[1024] = {};
		DWORD size = sizeof buf - sizeof(wchar_t), type = 0;
		bool ok = RegQueryValueExW(k, L"InstallLocation", nullptr, &type, reinterpret_cast<BYTE*>(buf), &size) == ERROR_SUCCESS && type == REG_SZ;
		RegCloseKey(k);
		if (!ok) return false;
		std::error_code ec;
		return fs::equivalent(fs::path(buf), dir, ec);
	};
	if (matches(HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY)) return 2;
	if (matches(HKEY_CURRENT_USER, 0)) return 1;
	return 0;
}

}  // namespace

std::string selfSigner() {
	std::string why;
	return signerOf(selfPath(), false, why);
}

bool canInstall(std::string& why) {
	std::string whyNot;
	if (signerOf(selfPath(), false, whyNot).empty()) {
		why = "This copy of VST Studio isn't a signed release (" + whyNot + "), so it can't check that an update comes from the same place.";
		return false;
	}
	if (installMode(selfPath().parent_path()) == 0) {
		why = "This copy was unpacked from the zip rather than installed, so there's no installer to update it. Download the new version "
			"instead; the setup program installs it so later updates are automatic.";
		return false;
	}
	return true;
}

bool fetch(const std::string& url, std::string* body, const std::string& toFile,
	const std::function<bool(std::uint64_t, std::uint64_t)>& progress, std::string& error) {
	std::wstring wurl = wide(url);
	URL_COMPONENTS uc{};
	uc.dwStructSize = sizeof uc;
	uc.dwSchemeLength = uc.dwHostNameLength = uc.dwUrlPathLength = uc.dwExtraInfoLength = DWORD(-1);
	if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) { error = lastError("Bad address"); return false; }
	if (uc.nScheme != INTERNET_SCHEME_HTTPS) { error = "Only https:// addresses are used for updates"; return false; }
	std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
	std::wstring path(uc.lpszUrlPath, uc.dwUrlPathLength);
	if (uc.lpszExtraInfo) path += std::wstring(uc.lpszExtraInfo, uc.dwExtraInfoLength);

	std::wstring agent = L"VSTStudio/" + wide(VST_VERSION);
	// The automatic proxy (WPAD/PAC, as Edge uses) needs Windows 8.1; older proxy settings come from the registry.
	HINTERNET session = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!session) session = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!session) { error = lastError("Couldn't start a download"); return false; }
	WinHttpSetTimeouts(session, 15000, 15000, 30000, 60000);
	HINTERNET conn = WinHttpConnect(session, host.c_str(), uc.nPort, 0);
	HINTERNET req = conn ? WinHttpOpenRequest(conn, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
	auto close = [&] {
		if (req) WinHttpCloseHandle(req);
		if (conn) WinHttpCloseHandle(conn);
		WinHttpCloseHandle(session);
	};
	if (!req) { error = lastError("Couldn't reach " + narrow(host)); close(); return false; }
	const wchar_t* headers = host == L"api.github.com" ? L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n" : WINHTTP_NO_ADDITIONAL_HEADERS;
	if (!WinHttpSendRequest(req, headers, DWORD(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(req, nullptr)) {
		error = lastError("Couldn't reach " + narrow(host));
		close();
		return false;
	}
	DWORD status = 0, len = sizeof status;
	WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX);
	if (status != 200) {
		error = narrow(host) + " answered HTTP " + std::to_string(status);
		if (status == 403 && host == L"api.github.com") error += " (too many checks from this network for now; it clears within an hour)";
		close();
		return false;
	}
	wchar_t lenBuf[32] = {};
	DWORD lenSize = sizeof lenBuf;
	std::uint64_t total = 0;
	if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, lenBuf, &lenSize, WINHTTP_NO_HEADER_INDEX))
		total = std::wcstoull(lenBuf, nullptr, 10);

	FILE* out = nullptr;
	if (!toFile.empty() && _wfopen_s(&out, fs::u8path(toFile).wstring().c_str(), L"wb") != 0) out = nullptr;
	if (!toFile.empty() && !out) { error = "Couldn't write " + toFile; close(); return false; }
	std::uint64_t got = 0;
	bool ok = true;
	std::vector<char> buf(64 * 1024);
	for (;;) {
		DWORD n = 0;
		if (!WinHttpReadData(req, buf.data(), DWORD(buf.size()), &n)) { error = lastError("The download stopped"); ok = false; break; }
		if (n == 0) break;
		got += n;
		if (out) {
			if (std::fwrite(buf.data(), 1, n, out) != n) { error = "Couldn't write " + toFile + " (is the disk full?)"; ok = false; break; }
		}
		else if (body) {
			if (body->size() + n > 4u << 20) { error = "The answer was larger than expected"; ok = false; break; }
			body->append(buf.data(), n);
		}
		if (progress && !progress(got, total)) { error = "Cancelled"; ok = false; break; }
	}
	if (out && std::fclose(out) != 0 && ok) { error = "Couldn't write " + toFile; ok = false; }
	if (ok && total && got != total) { error = "The download was cut short"; ok = false; }
	close();
	return ok;
}

bool verifyAndStage(const std::string& package, const std::string& signer, std::string& staged, std::string& error) {
	std::string why;
	std::string who = signerOf(fs::u8path(package), true, why);
	if (who.empty()) { error = "The installer can't be trusted: " + why + "."; return false; }
	if (signer.empty() || who != signer) {
		error = "The installer is signed by someone else (" + who + "), so it wasn't run.";
		return false;
	}
	staged = package;
	return true;
}

bool install(const std::string& staged, bool& quitNow, std::string& error) {
	quitNow = false;
	fs::path dir = selfPath().parent_path();
	int mode = installMode(dir);
	if (mode == 0) { error = "This copy wasn't installed with the setup program."; return false; }
	// /SILENT shows only the progress bar: no questions, nothing to click.  /CLOSEAPPLICATIONS lets Setup close this
	// Studio if it hasn't quit yet; /relaunch=1 is vst-setup.iss's own switch to open VST Studio again when it's done
	// (as the user, not as administrator).  /ALLUSERS asks Windows for administrator rights (the UAC prompt) because
	// the copy in Program Files is everyone's; /CURRENTUSER never does.
	std::wstring log = (fs::u8path(downloadDir()) / "setup.log").wstring();
	std::wstring args = L"/SILENT /SUPPRESSMSGBOXES /NORESTART /CLOSEAPPLICATIONS /relaunch=1 ";
	args += mode == 2 ? L"/ALLUSERS" : L"/CURRENTUSER";
	args += L" \"/DIR=" + dir.wstring() + L"\" \"/LOG=" + log + L"\"";
	std::wstring file = fs::u8path(staged).wstring();
	SHELLEXECUTEINFOW sei{};
	sei.cbSize = sizeof sei;
	sei.fMask = SEE_MASK_NOASYNC;
	sei.lpVerb = L"open";
	sei.lpFile = file.c_str();
	sei.lpParameters = args.c_str();
	sei.nShow = SW_SHOWNORMAL;
	if (!ShellExecuteExW(&sei)) {
		error = lastError("Couldn't start the installer");
		return false;
	}
	quitNow = mode != 2;
	return true;
}

#elif !defined(__APPLE__)

// Linux: the .deb is installed by hand (or with a package manager pointed at a file), so Studio only says what's new.
bool canInstall(std::string& why) {
	why = "On Linux, VST Studio is installed from a .deb package. Download the new one for your system and open it, "
		"or install it with: sudo apt install ./<the file>.deb";
	return false;
}
std::string selfSigner() { return ""; }
// Only the check runs on Linux (the release's version and notes), through curl, which every desktop install has; the
// address is Studio's own constant API URL, never one from a download, so nothing from the network reaches the shell.
bool fetch(const std::string& url, std::string* body, const std::string& toFile, const std::function<bool(std::uint64_t, std::uint64_t)>&,
	std::string& error) {
	const std::string api = std::string("https://api.github.com/repos/") + update::kRepo + "/";
	if (!body || !toFile.empty() || url.rfind(api, 0) != 0 || url.find_first_of("'\"\\ $`") != std::string::npos) {
		error = "Studio doesn't download updates on Linux";
		return false;
	}
	std::string cmd = "curl -fsSL --max-time 30 -H 'Accept: application/vnd.github+json' -A 'VSTStudio/" VST_VERSION "' '" + url + "' 2>&1";
	FILE* p = popen(cmd.c_str(), "r");
	if (!p) { error = "couldn't run curl"; return false; }
	char buf[8192];
	size_t n;
	while ((n = std::fread(buf, 1, sizeof buf, p)) > 0 && body->size() < (4u << 20)) body->append(buf, n);
	int rc = pclose(p);
	if (rc != 0) {
		error = rc == 127 * 256 || body->find("not found") != std::string::npos ? "curl isn't installed" : "GitHub couldn't be reached";
		body->clear();
		return false;
	}
	return true;
}
bool verifyAndStage(const std::string&, const std::string&, std::string&, std::string& error) {
	error = "Studio doesn't install updates on Linux";
	return false;
}
bool install(const std::string&, bool& quitNow, std::string& error) {
	quitNow = false;
	error = "Studio doesn't install updates on Linux";
	return false;
}

#endif

}  // namespace updateos
