// cmd.cpp (final version)
//
// A native Windows text‑expander designed to handle a very large number of
// snippets and multilingual keywords, including Arabic.  This version
// implements a robust matching engine that canonicalizes Arabic letters,
// digits and diacritics, performs longest‑suffix matching and tolerates
// punctuation such as dots within keywords.  It also takes care to
// release modifier keys before erasing text and restores the clipboard
// after pasting the replacement.  The code is portable across Windows
// versions and can be compiled with MSVC.

#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#include <windows.h>
#include <winuser.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <fstream>
#include <sstream>
#include <codecvt>
#include <locale>
#include <algorithm>
#include <mutex>

// ---------------------------------------------------------------------------
// Configuration
//
// Adjust these constants to customise the behaviour.  Delimiters trigger
// automatic expansion when a keyword is followed by one of these
// characters.  The hotkey for manual expansion is Ctrl+Space by default.
// If you set a custom trigger string (e.g. "." or "-"), typing
// keyword+customTrigger will trigger expansion instead of waiting for a
// delimiter.  Note that the custom trigger string itself is canonicalised
// before matching.

// Delimiter characters that trigger automatic expansion.  You can add more
// characters here (e.g. L'.' to trigger after a dot).
static const wchar_t DELIM_CHARS[] = { L' ', L'\t', L'\r', 0 };

// Global hotkey definition (Ctrl+Space).  You may adjust HOTKEY_MOD and
// HOTKEY_VK to use another combination.  See WinUser.h for virtual key
// codes.
static const UINT HOTKEY_MOD = MOD_CONTROL;
static const UINT HOTKEY_VK  = VK_SPACE;

// Custom trigger string (empty by default).  Set this to a non‑empty
// sequence if you want expansions to be triggered by typing
// keyword+trigger instead of waiting for a delimiter.  For example, if
// g_customTrigger = L"؛" (Arabic semicolon), typing ";sig؛" will trigger
// the expansion.
static std::wstring g_customTrigger = L"";

// ---------------------------------------------------------------------------
// Globals

// Mapping from canonical keyword to replacement text.  The canonical form
// collapses Arabic letters, removes diacritics and normalises digits.
static std::unordered_map<std::wstring, std::wstring> g_snippets;

// Maximum length (in canonical characters) of any keyword in g_snippets.
static size_t g_maxKeywordLen = 0;

// Buffer of canonicalised characters recently typed.  This grows up to
// (g_maxKeywordLen + g_customTrigger.length()) before older characters are
// discarded to keep matching efficient.
static std::wstring g_typedBuffer;

// Synchronises access to g_typedBuffer across the hook thread and main
// thread.
static std::mutex g_bufferMutex;

// Keyboard hook handle
static HHOOK g_hookHandle = NULL;

// ---------------------------------------------------------------------------
// Helper functions

// UTF‑8 to UTF‑16 conversion using std::codecvt.  The project stores
// snippets in UTF‑8 JSON, so conversion is required when reading the file.
static std::wstring utf8ToWString(const std::string &str) {
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> conv;
    return conv.from_bytes(str);
}

// Read an entire file (UTF‑8) into a std::string.  Returns an empty string
// on error or if the file is empty.
static std::string readFileUtf8(const std::wstring &path) {
    std::ifstream f(path, std::ios::in | std::ios::binary);
    if (!f) return std::string();
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Determine whether a character is considered a delimiter.  Delimiters
// trigger automatic expansion when encountered immediately after a keyword.
static inline bool isDelimiter(wchar_t ch) {
    for (const wchar_t *p = DELIM_CHARS; *p; ++p) {
        if (*p == ch) return true;
    }
    return false;
}

// Canonicalise a single Unicode codepoint.  Arabic letters are mapped to
// their base forms (e.g. different alif variants become U+0627), digits
// are normalised to ASCII digits, combining marks are removed and Latin
// letters are lower‑cased.  Unhandled characters are returned unchanged.
static wchar_t canonicaliseChar(wchar_t ch) {
    // Arabic digits (U+0660–U+0669)
    if (ch >= 0x0660 && ch <= 0x0669) return L'0' + (ch - 0x0660);
    // Persian digits (U+06F0–U+06F9)
    if (ch >= 0x06F0 && ch <= 0x06F9) return L'0' + (ch - 0x06F0);
    // Remove combining marks (Arabic diacritics)
    if ((ch >= 0x064B && ch <= 0x065F) || (ch >= 0x0610 && ch <= 0x061A) ||
        (ch >= 0x06D6 && ch <= 0x06DC) || (ch >= 0x06DF && ch <= 0x06E8) ||
        (ch >= 0x06EA && ch <= 0x06ED)) {
        return 0; // skip
    }
    // Normalise Arabic letters
    switch (ch) {
    case 0x0622: case 0x0623: case 0x0625: case 0x0671: case 0x0675:
        return 0x0627; // alif
    case 0x0649: // alif maksura
        return 0x064A; // ya
    case 0x0629: // ta marbuta
        return 0x0647; // ha
    case 0x0624: // waw with hamza
        return 0x0648; // waw
    case 0x0626: // ya with hamza
        return 0x064A; // ya
    default:
        break;
    }
    // Lowercase ASCII letters
    if (ch < 0x80) return (wchar_t)tolower((int)ch);
    return ch;
}

// Canonicalise an entire string by applying canonicaliseChar to each
// character and discarding any zero results (i.e. diacritics).  The input
// may contain Arabic, Persian or Latin text.  Digits and punctuation
// remain unchanged except for normalisation of Arabic/Persian digits.
static std::wstring canonicaliseString(const std::wstring &in) {
    std::wstring out;
    out.reserve(in.size());
    for (wchar_t ch : in) {
        wchar_t c = canonicaliseChar(ch);
        if (c != 0) out.push_back(c);
    }
    return out;
}

// A minimal JSON parser for our snippet file.  The input is expected to
// be an array of objects with at least "keyword" and "text" fields.
// This parser does not handle escapes within strings but is sufficient
// for well‑formed JSON produced by typical editors.  Keywords and texts
// are canonicalised and stored in g_snippets.
static void parseSnippetsJson(const std::string &json) {
    size_t i = 0;
    auto skipWS = [&]() {
        while (i < json.size() && isspace((unsigned char)json[i])) i++;
    };
    skipWS();
    if (i >= json.size() || json[i] != '[') return;
    i++;
    while (i < json.size()) {
        skipWS();
        if (i < json.size() && json[i] == ']') break;
        if (i < json.size() && json[i] == '{') {
            i++;
            std::string rawKey;
            std::string rawText;
            bool foundKey = false;
            bool foundText = false;
            while (i < json.size()) {
                skipWS();
                if (json[i] == '}') { i++; break; }
                if (json[i] == ',') { i++; continue; }
                // Field name
                if (json[i] == '"') {
                    size_t start = ++i;
                    while (i < json.size() && json[i] != '"') i++;
                    std::string field = json.substr(start, i - start);
                    i++; // skip closing quote
                    skipWS();
                    if (i < json.size() && json[i] == ':') i++;
                    skipWS();
                    // Field value
                    if (i < json.size() && json[i] == '"') {
                        size_t vstart = ++i;
                        while (i < json.size() && json[i] != '"') i++;
                        std::string val = json.substr(vstart, i - vstart);
                        i++; // skip closing quote
                        if (field == "keyword") {
                            rawKey = val;
                            foundKey = true;
                        } else if (field == "text") {
                            rawText = val;
                            foundText = true;
                        }
                    }
                } else {
                    i++;
                }
            }
            if (foundKey && foundText) {
                std::wstring wKey  = utf8ToWString(rawKey);
                std::wstring wText = utf8ToWString(rawText);
                std::wstring cKey = canonicaliseString(wKey);
                if (!cKey.empty()) {
                    // If duplicate keyword exists, update text (latest wins)
                    auto it = g_snippets.find(cKey);
                    if (it == g_snippets.end()) {
                        g_snippets.emplace(cKey, wText);
                    } else {
                        it->second = wText;
                    }
                    g_maxKeywordLen = std::max(g_maxKeywordLen, cKey.size());
                }
            }
        } else {
            i++;
        }
    }
}

// Load snippets from `snippets.json` located in the same directory as
// the executable.  Called once at program startup.
static void loadSnippets() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    std::wstring exe(path);
    size_t pos = exe.find_last_of(L"\\/");
    std::wstring dir = (pos != std::wstring::npos) ? exe.substr(0, pos) : L".";
    std::wstring jsonPath = dir + L"\\snippets.json";
    std::string contents = readFileUtf8(jsonPath);
    if (!contents.empty()) parseSnippetsJson(contents);
}

// Perform the text expansion.  matchedKey is the canonical keyword that
// matched (possibly including the canonicalised custom trigger).  The
// original buffer may contain non‑canonical characters that were typed; we
// only need to erase as many characters as were canonicalised.  If
// triggeredByDelimiter is true, the delimiter will be re‑typed after
// pasting.  This function releases modifier keys first to avoid
// accidentally sending Ctrl+Backspace etc.
static void performExpansion(const std::wstring &matchedKey, const std::wstring &replacement,
                             bool triggeredByDelimiter) {
    // Release modifiers (Ctrl, Alt, Shift, Win) so Backspace acts normally
    std::vector<WORD> mods = { VK_CONTROL, VK_MENU, VK_SHIFT, VK_LWIN, VK_RWIN };
    INPUT releases[10];
    UINT rcount = 0;
    for (WORD vk : mods) {
        if (GetAsyncKeyState(vk) & 0x8000) {
            releases[rcount].type = INPUT_KEYBOARD;
            releases[rcount].ki.wVk = vk;
            releases[rcount].ki.dwFlags = KEYEVENTF_KEYUP;
            releases[rcount].ki.wScan = 0;
            releases[rcount].ki.dwExtraInfo = 0;
            releases[rcount].ki.time = 0;
            rcount++;
        }
    }
    if (rcount > 0) SendInput(rcount, releases, sizeof(INPUT));

    // Erase the matched canonical keyword.  We use KEYEVENTF_UNICODE to
    // delete characters reliably (equivalent to VK_BACK).  Each canonical
    // character corresponds to one typed character in the buffer.
    size_t count = matchedKey.size();
    if (count > 0) {
        std::vector<INPUT> eraseInputs;
        eraseInputs.reserve(count * 2);
        for (size_t i = 0; i < count; ++i) {
            INPUT in{};
            in.type = INPUT_KEYBOARD;
            in.ki.wVk = VK_BACK;
            in.ki.dwFlags = 0;
            eraseInputs.push_back(in);
            INPUT up = in;
            up.ki.dwFlags = KEYEVENTF_KEYUP;
            eraseInputs.push_back(up);
        }
        SendInput((UINT)eraseInputs.size(), eraseInputs.data(), sizeof(INPUT));
    }

    // Save existing clipboard content
    std::wstring prevClip;
    if (OpenClipboard(NULL)) {
        HANDLE h = GetClipboardData(CF_UNICODETEXT);
        if (h) {
            LPCWSTR p = (LPCWSTR)GlobalLock(h);
            if (p) {
                prevClip.assign(p);
                GlobalUnlock(h);
            }
        }
        CloseClipboard();
    }
    // Replace clipboard with replacement text
    if (OpenClipboard(NULL)) {
        EmptyClipboard();
        size_t bytes = (replacement.size() + 1) * sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (hMem) {
            memcpy(GlobalLock(hMem), replacement.c_str(), bytes);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
        CloseClipboard();
    }
    // Paste via Ctrl+V
    INPUT paste[4] = {};
    paste[0].type = INPUT_KEYBOARD; paste[0].ki.wVk = VK_CONTROL;
    paste[1].type = INPUT_KEYBOARD; paste[1].ki.wVk = 'V';
    paste[2] = paste[1]; paste[2].ki.dwFlags = KEYEVENTF_KEYUP;
    paste[3] = paste[0]; paste[3].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(4, paste, sizeof(INPUT));
    // Re‑type delimiter if required
    if (triggeredByDelimiter) {
        INPUT spc[2] = {};
        spc[0].type = INPUT_KEYBOARD;
        spc[0].ki.wVk = VK_SPACE;
        spc[1] = spc[0]; spc[1].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(2, spc, sizeof(INPUT));
    }
    // Restore clipboard
    if (!prevClip.empty()) {
        if (OpenClipboard(NULL)) {
            EmptyClipboard();
            size_t bytes = (prevClip.size() + 1) * sizeof(wchar_t);
            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
            if (hMem) {
                memcpy(GlobalLock(hMem), prevClip.c_str(), bytes);
                GlobalUnlock(hMem);
                SetClipboardData(CF_UNICODETEXT, hMem);
            }
            CloseClipboard();
        }
    }
}

// Attempt to expand when a delimiter is typed.  This function appends
// the canonicalised delimiter to the current buffer for matching, then
// searches for the longest matching keyword.  If a match is found, it
// clears g_typedBuffer and performs the expansion.
static void tryExpandOnDelimiter(wchar_t delim) {
    std::lock_guard<std::mutex> lock(g_bufferMutex);
    // Append canonicalised delimiter to a temporary buffer
    wchar_t cDelim = canonicaliseChar(delim);
    std::wstring tmp = g_typedBuffer;
    if (cDelim != 0) tmp.push_back(cDelim);
    size_t maxLen = std::min(g_maxKeywordLen, tmp.size());
    std::wstring matched;
    for (size_t len = maxLen; len > 0; --len) {
        std::wstring suffix = tmp.substr(tmp.size() - len, len);
        auto it = g_snippets.find(suffix);
        if (it != g_snippets.end()) {
            matched = suffix;
            break;
        }
    }
    if (!matched.empty()) {
        std::wstring replacement = g_snippets[matched];
        g_typedBuffer.clear();
        performExpansion(matched, replacement, true);
    } else {
        // No match; clear buffer so it does not grow indefinitely
        g_typedBuffer.clear();
    }
}

// Attempt to expand on a custom trigger (if configured).  When the
// canonicalised typed buffer ends with the canonicalised custom trigger,
// this function searches for the longest suffix preceding the trigger.
static void tryExpandOnCustom() {
    if (g_customTrigger.empty()) return;
    std::lock_guard<std::mutex> lock(g_bufferMutex);
    std::wstring cTrig = canonicaliseString(g_customTrigger);
    if (cTrig.empty()) return;
    size_t trigLen = cTrig.size();
    if (g_typedBuffer.size() <= trigLen) return;
    // Check if buffer ends with custom trigger
    if (g_typedBuffer.compare(g_typedBuffer.size() - trigLen, trigLen, cTrig) != 0) return;
    // Remove trigger from the end and look for matching keyword
    std::wstring prefix = g_typedBuffer.substr(0, g_typedBuffer.size() - trigLen);
    size_t maxLen = std::min(g_maxKeywordLen, prefix.size());
    std::wstring matched;
    for (size_t len = maxLen; len > 0; --len) {
        std::wstring suffix = prefix.substr(prefix.size() - len, len);
        auto it = g_snippets.find(suffix);
        if (it != g_snippets.end()) {
            matched = suffix;
            break;
        }
    }
    if (!matched.empty()) {
        std::wstring replacement = g_snippets[matched];
        // Clear typed buffer before performing expansion
        g_typedBuffer.clear();
        // Perform expansion.  The matched keyword includes the trigger
        // canonicalised portion so that the correct number of characters
        // are erased (matched + cTrig).
        performExpansion(matched + cTrig, replacement, false);
    }
}

// Low‑level keyboard hook callback.  This intercepts key presses and
// updates the typed buffer.  When the hotkey or a delimiter/custom
// trigger is detected, it searches for a matching keyword and performs
// expansion.
static LRESULT CALLBACK keyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode < 0) return CallNextHookEx(g_hookHandle, nCode, wParam, lParam);
    KBDLLHOOKSTRUCT *kbd = (KBDLLHOOKSTRUCT *)lParam;
    // Only handle keydown events
    if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
        // Check for manual hotkey (Ctrl+Space)
        if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) && kbd->vkCode == HOTKEY_VK) {
            std::lock_guard<std::mutex> lock(g_bufferMutex);
            // Find the longest matching keyword in the buffer
            if (!g_typedBuffer.empty()) {
                size_t maxLen = std::min(g_maxKeywordLen, g_typedBuffer.size());
                std::wstring matched;
                for (size_t len = maxLen; len > 0; --len) {
                    std::wstring suffix = g_typedBuffer.substr(g_typedBuffer.size() - len, len);
                    auto it = g_snippets.find(suffix);
                    if (it != g_snippets.end()) {
                        matched = suffix;
                        break;
                    }
                }
                if (!matched.empty()) {
                    std::wstring replacement = g_snippets[matched];
                    g_typedBuffer.clear();
                    performExpansion(matched, replacement, false);
                    return 1; // swallow key
                }
            }
        }
        // Translate vkCode to Unicode character
        wchar_t ch = 0;
        BYTE state[256];
        if (GetKeyboardState(state)) {
            WCHAR buf[4];
            if (ToUnicodeEx(kbd->vkCode, kbd->scanCode, state, buf, 4, 0, GetKeyboardLayout(0)) == 1) {
                ch = buf[0];
            }
        }
        if (ch != 0) {
            // If delimiter: attempt expansion
            if (isDelimiter(ch)) {
                tryExpandOnDelimiter(ch);
            } else {
                // Append canonical character
                wchar_t c = canonicaliseChar(ch);
                std::lock_guard<std::mutex> lock(g_bufferMutex);
                if (c != 0) g_typedBuffer.push_back(c);
                // Trim buffer length
                size_t maxBuffer = g_maxKeywordLen + g_customTrigger.size() + 8;
                if (g_typedBuffer.size() > maxBuffer) {
                    g_typedBuffer.erase(0, g_typedBuffer.size() - maxBuffer);
                }
                // Check custom trigger
                tryExpandOnCustom();
            }
        }
    }
    return CallNextHookEx(g_hookHandle, nCode, wParam, lParam);
}

// Program entry point.  Loads snippets, installs the keyboard hook and
// enters a message loop.  Exits when a WM_QUIT message is posted.
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    loadSnippets();
    // Install global keyboard hook
    g_hookHandle = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardHookProc, NULL, 0);
    if (!g_hookHandle) {
        MessageBoxW(NULL, L"Failed to install keyboard hook", L"Error", MB_ICONERROR);
        return -1;
    }
    // Register manual hotkey (Ctrl+Space)
    RegisterHotKey(NULL, 0xBEEF, HOTKEY_MOD, HOTKEY_VK);
    // Message loop
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (g_hookHandle) UnhookWindowsHookEx(g_hookHandle);
    return 0;
}