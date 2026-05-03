#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#define UNICODE
#define _UNICODE
#define _WIN32_WINNT 0x0601

#include <windows.h>
#include <commctrl.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <algorithm>
#include <cwctype>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct Snippet {
    std::wstring keyword;
    std::wstring keyNorm;
    std::wstring text;
};

struct MatchResult {
    int index = -1;
    int rawDelete = 0;
};

static HINSTANCE gInst{};
static HWND gWnd{}, gStatus{}, gCount{}, gList{};
static HHOOK gHook{};
static bool gEnabled = true;
static bool gExpanding = false;

static std::vector<Snippet> gSnips;
static std::unordered_map<std::wstring, std::vector<int>> gExactIndex;
static std::unordered_map<std::wstring, std::vector<int>> gNormIndex;
static std::unordered_set<std::wstring> gNormPrefixes;
static size_t gMaxRawBuffer = 512;

static std::wstring gRawBuffer;
static std::wstring gNormBuffer;

static const UINT WM_DO_EXPAND = WM_APP + 10;
static HWND gTarget{};
static int gDeleteCount = 0;
static WORD gDelimiterToSend = 0;
static std::wstring gPasteText;

static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0);
    UINT cp = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    if (n <= 0) {
        cp = CP_ACP;
        flags = 0;
        n = MultiByteToWideChar(cp, flags, s.data(), (int)s.size(), nullptr, 0);
    }
    if (n <= 0) return L"";
    std::wstring out(n, 0);
    MultiByteToWideChar(cp, flags, s.data(), (int)s.size(), &out[0], n);
    return out;
}

static std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

static std::string ReadFileBytes(const std::wstring& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static std::wstring ExeDir() {
    wchar_t p[MAX_PATH]{};
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    std::wstring s = p;
    size_t pos = s.find_last_of(L"\\/");
    return pos == std::wstring::npos ? L"." : s.substr(0, pos);
}

static std::wstring Trim(std::wstring s) {
    auto sp = [](wchar_t c) { return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n'; };
    while (!s.empty() && sp(s.front())) s.erase(s.begin());
    while (!s.empty() && sp(s.back())) s.pop_back();
    return s;
}

static wchar_t NormChar(wchar_t c) {
    if (c == 0xFEFF || c == 0x200B || c == 0x200C || c == 0x200D || c == 0x200E || c == 0x200F) return 0;
    if (c == 0x0640 || c == 0x0670 || (c >= 0x064B && c <= 0x065F) || (c >= 0x0610 && c <= 0x061A)) return 0;
    if ((c >= 0x06D6 && c <= 0x06DC) || (c >= 0x06DF && c <= 0x06E8) || (c >= 0x06EA && c <= 0x06ED)) return 0;

    if (c >= 0x0660 && c <= 0x0669) return L'0' + (c - 0x0660);
    if (c >= 0x06F0 && c <= 0x06F9) return L'0' + (c - 0x06F0);

    if (c == 0x0622 || c == 0x0623 || c == 0x0625 || c == 0x0671 || c == 0x0675) return 0x0627;
    if (c == 0x0649) return 0x064A;
    if (c == 0x0624) return 0x0648;
    if (c == 0x0626) return 0x064A;
    if (c == 0x0629) return 0x0647;

    if (c == 0x066B) return L'.';
    if (c == 0x066C) return L',';
    if (c == 0x061B) return L';';

    return (wchar_t)towlower(c);
}

static std::wstring Normalize(const std::wstring& in) {
    std::wstring out;
    out.reserve(in.size());
    bool lastSpace = false;
    for (wchar_t c : in) {
        wchar_t n = NormChar(c);
        if (!n) continue;
        if (n == L'\t' || n == L'\r' || n == L'\n') n = L' ';
        if (n == L' ') {
            if (!lastSpace && !out.empty()) {
                out.push_back(L' ');
                lastSpace = true;
            }
            continue;
        }
        out.push_back(n);
        lastSpace = false;
    }
    return Trim(out);
}

static int Hex(wchar_t c) {
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'a' && c <= L'f') return 10 + c - L'a';
    if (c >= L'A' && c <= L'F') return 10 + c - L'A';
    return -1;
}

static std::wstring JsonUnescape(const std::wstring& s) {
    std::wstring out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'\\' && i + 1 < s.size()) {
            wchar_t n = s[++i];
            if (n == L'n') out.push_back(L'\n');
            else if (n == L'r') out.push_back(L'\r');
            else if (n == L't') out.push_back(L'\t');
            else if (n == L'\\') out.push_back(L'\\');
            else if (n == L'"') out.push_back(L'"');
            else if (n == L'u' && i + 4 < s.size()) {
                int a = Hex(s[i + 1]), b = Hex(s[i + 2]), c = Hex(s[i + 3]), d = Hex(s[i + 4]);
                if (a >= 0 && b >= 0 && c >= 0 && d >= 0) {
                    wchar_t first = (wchar_t)((a << 12) | (b << 8) | (c << 4) | d);
                    i += 4;
                    if (first >= 0xD800 && first <= 0xDBFF && i + 6 < s.size() && s[i + 1] == L'\\' && s[i + 2] == L'u') {
                        int e = Hex(s[i + 3]), f = Hex(s[i + 4]), g = Hex(s[i + 5]), h = Hex(s[i + 6]);
                        if (e >= 0 && f >= 0 && g >= 0 && h >= 0) {
                            out.push_back(first);
                            out.push_back((wchar_t)((e << 12) | (f << 8) | (g << 4) | h));
                            i += 6;
                        } else out.push_back(first);
                    } else out.push_back(first);
                }
            } else out.push_back(n);
        } else out.push_back(s[i]);
    }
    return out;
}

static std::wstring BytesToWideText(const std::string& bytes) {
    if (bytes.size() >= 3 && (unsigned char)bytes[0] == 0xEF && (unsigned char)bytes[1] == 0xBB && (unsigned char)bytes[2] == 0xBF)
        return Utf8ToWide(bytes.substr(3));
    return Utf8ToWide(bytes);
}

static std::vector<std::wstring> Objects(const std::wstring& j) {
    std::vector<std::wstring> out;
    int depth = 0;
    bool in = false, esc = false;
    size_t start = 0;
    for (size_t i = 0; i < j.size(); ++i) {
        wchar_t c = j[i];
        if (in) {
            if (esc) esc = false;
            else if (c == L'\\') esc = true;
            else if (c == L'"') in = false;
            continue;
        }
        if (c == L'"') { in = true; continue; }
        if (c == L'{') { if (depth++ == 0) start = i; }
        else if (c == L'}') { if (depth > 0 && --depth == 0) out.push_back(j.substr(start, i - start + 1)); }
    }
    return out;
}

static bool GetJsonString(const std::wstring& obj, const std::vector<std::wstring>& keys, std::wstring& out) {
    for (const auto& k : keys) {
        size_t p = obj.find(L"\"" + k + L"\"");
        if (p == std::wstring::npos) continue;
        p = obj.find(L':', p);
        if (p == std::wstring::npos) continue;
        p = obj.find(L'"', p + 1);
        if (p == std::wstring::npos) continue;
        ++p;
        std::wstring raw;
        bool esc = false;
        for (; p < obj.size(); ++p) {
            wchar_t c = obj[p];
            if (esc) { raw.push_back(L'\\'); raw.push_back(c); esc = false; continue; }
            if (c == L'\\') { esc = true; continue; }
            if (c == L'"') break;
            raw.push_back(c);
        }
        out = JsonUnescape(raw);
        return true;
    }
    return false;
}

static bool GetJsonBoolFalse(const std::wstring& obj, const std::wstring& key) {
    size_t p = obj.find(L"\"" + key + L"\"");
    if (p == std::wstring::npos) return false;
    p = obj.find(L':', p);
    if (p == std::wstring::npos) return false;
    std::wstring tail = obj.substr(p + 1, 16);
    std::transform(tail.begin(), tail.end(), tail.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });
    return tail.find(L"false") != std::wstring::npos;
}

static void AddSnippet(const std::wstring& key, const std::wstring& text) {
    std::wstring k = Trim(key);
    if (k.empty() || text.empty()) return;
    Snippet s{k, Normalize(k), text};
    if (s.keyNorm.empty()) return;
    gSnips.push_back(s);
}

static void BuildIndexes() {
    gExactIndex.clear();
    gNormIndex.clear();
    gNormPrefixes.clear();
    gMaxRawBuffer = 512;
    for (size_t i = 0; i < gSnips.size(); ++i) {
        gExactIndex[gSnips[i].keyword].push_back((int)i);
        gNormIndex[gSnips[i].keyNorm].push_back((int)i);
        gMaxRawBuffer = std::max<size_t>(gMaxRawBuffer, gSnips[i].keyword.size() + 64);
        for (size_t n = 1; n < gSnips[i].keyNorm.size(); ++n) gNormPrefixes.insert(gSnips[i].keyNorm.substr(0, n));
    }
}

static void LoadSnippetsFromText(const std::wstring& content) {
    for (const auto& obj : Objects(content)) {
        if (GetJsonBoolFalse(obj, L"enabled")) continue;
        std::wstring key, text;
        GetJsonString(obj, {L"keyword", L"key", L"shortcut", L"abbreviation", L"trigger", L"comboText"}, key);
        GetJsonString(obj, {L"snippet", L"text", L"replacement", L"replace", L"value", L"content", L"phrase", L"substitutionText"}, text);
        AddSnippet(key, text);
    }
}

static void LoadSnippets() {
    gSnips.clear();
    std::wstring dir = ExeDir();
    std::wstring snippets = BytesToWideText(ReadFileBytes(dir + L"\\snippets.json"));
    if (!snippets.empty()) LoadSnippetsFromText(snippets);
    if (gSnips.empty()) {
        std::wstring backup = BytesToWideText(ReadFileBytes(dir + L"\\Beeftext.btbackup"));
        if (!backup.empty()) LoadSnippetsFromText(backup);
    }
    if (gSnips.empty()) {
        AddSnippet(L";hi", L"Hello");
        AddSnippet(L"2.", L"DOT TEST");
        AddSnippet(std::wstring({0x0627, 0x0628, 0x062C}), L"ARABIC TEST");
    }
    BuildIndexes();
}

static bool EndsWith(const std::wstring& text, const std::wstring& suffix) {
    return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static int RawDeleteForNormSuffix(const std::wstring& normSuffix) {
    std::wstring rebuilt;
    int raw = 0;
    for (int p = (int)gRawBuffer.size() - 1; p >= 0; --p) {
        rebuilt.insert(rebuilt.begin(), gRawBuffer[p]);
        ++raw;
        if (Normalize(rebuilt) == normSuffix) return raw;
    }
    return (int)normSuffix.size();
}

static MatchResult FindMatch() {
    MatchResult m;
    if (gRawBuffer.empty()) return m;

    // Beeftext-style strict exact keyword comparison first.
    for (const auto& kv : gExactIndex) {
        const std::wstring& key = kv.first;
        if (EndsWith(gRawBuffer, key)) {
            m.index = kv.second.empty() ? -1 : kv.second.front();
            m.rawDelete = (int)key.size();
            return m;
        }
    }

    // Normalized fallback for Arabic variants and Arabic/Persian digits.
    if (gNormBuffer.empty()) return m;
    size_t maxLen = std::min<size_t>(gNormBuffer.size(), gMaxRawBuffer);
    for (size_t len = maxLen; len > 0; --len) {
        std::wstring suffix = gNormBuffer.substr(gNormBuffer.size() - len);
        auto it = gNormIndex.find(suffix);
        if (it != gNormIndex.end() && !it->second.empty()) {
            m.index = it->second.front();
            m.rawDelete = std::max<int>(RawDeleteForNormSuffix(suffix), (int)gSnips[m.index].keyword.size());
            return m;
        }
    }
    return m;
}

static bool IsPunctuationTriggerChar(const std::wstring& t) {
    if (t.size() != 1) return false;
    wchar_t c = NormChar(t[0]);
    return c == L'.' || c == L',' || c == L';' || c == L':' || c == L'!' || c == L'?';
}

static bool HasLongerPrefix(const std::wstring& norm) {
    return gNormPrefixes.find(norm) != gNormPrefixes.end();
}

static void SetStatus(const std::wstring& s) {
    if (gStatus) SetWindowTextW(gStatus, s.c_str());
}

static void RefreshUi() {
    if (gCount) SetWindowTextW(gCount, (L"Loaded snippets: " + std::to_wstring(gSnips.size())).c_str());
    if (gList) {
        SendMessageW(gList, LB_RESETCONTENT, 0, 0);
        int count = (int)std::min<size_t>(gSnips.size(), 300);
        for (int i = 0; i < count; ++i) SendMessageW(gList, LB_ADDSTRING, 0, (LPARAM)gSnips[i].keyword.c_str());
    }
}

static void ReleaseMods() {
    INPUT in[12]{};
    int n = 0;
    WORD keys[] = {VK_CONTROL, VK_MENU, VK_SHIFT, VK_LCONTROL, VK_RCONTROL, VK_LMENU, VK_RMENU, VK_LSHIFT, VK_RSHIFT, VK_LWIN, VK_RWIN};
    for (WORD vk : keys) {
        in[n].type = INPUT_KEYBOARD;
        in[n].ki.wVk = vk;
        in[n].ki.dwFlags = KEYEVENTF_KEYUP;
        ++n;
    }
    SendInput(n, in, sizeof(INPUT));
    Sleep(35);
}

static void Key(WORD vk, bool up) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
    SendInput(1, &in, sizeof(INPUT));
}

static bool OpenClip() {
    for (int i = 0; i < 25; ++i) {
        if (OpenClipboard(gWnd)) return true;
        Sleep(10);
    }
    return false;
}

static bool GetClip(std::wstring& out, bool& hadText) {
    hadText = false;
    out.clear();
    if (!OpenClip()) return false;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (!h) { CloseClipboard(); return true; }
    wchar_t* p = (wchar_t*)GlobalLock(h);
    if (!p) { CloseClipboard(); return true; }
    out = p;
    hadText = true;
    GlobalUnlock(h);
    CloseClipboard();
    return true;
}

static bool SetClip(const std::wstring& s) {
    if (!OpenClip()) return false;
    EmptyClipboard();
    size_t bytes = (s.size() + 1) * sizeof(wchar_t);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!h) { CloseClipboard(); return false; }
    void* p = GlobalLock(h);
    memcpy(p, s.c_str(), bytes);
    GlobalUnlock(h);
    SetClipboardData(CF_UNICODETEXT, h);
    CloseClipboard();
    return true;
}

static void ClearClip() {
    if (OpenClip()) { EmptyClipboard(); CloseClipboard(); }
}

static void ClearBuffer() {
    gRawBuffer.clear();
    gNormBuffer.clear();
}

static void DoExpand() {
    gExpanding = true;
    if (gTarget) { SetForegroundWindow(gTarget); Sleep(50); }
    ReleaseMods();

    for (int i = 0; i < gDeleteCount; ++i) {
        Key(VK_BACK, false);
        Key(VK_BACK, true);
        Sleep(1);
    }

    std::wstring old;
    bool hadText = false;
    bool readClip = GetClip(old, hadText);
    if (!SetClip(gPasteText)) {
        SetStatus(L"Clipboard busy - expansion failed");
        gExpanding = false;
        return;
    }

    Sleep(35);
    Key(VK_CONTROL, false);
    Key('V', false);
    Key('V', true);
    Key(VK_CONTROL, true);

    if (gDelimiterToSend) {
        Sleep(25);
        Key(gDelimiterToSend, false);
        Key(gDelimiterToSend, true);
    }

    Sleep(100);
    if (readClip) {
        if (hadText) SetClip(old);
        else ClearClip();
    }
    gExpanding = false;
    SetStatus(L"Expanded snippet successfully");
}

static bool OwnWindow() {
    HWND f = GetForegroundWindow();
    return f == gWnd || IsChild(gWnd, f);
}

static void AddTextToBuffer(const std::wstring& text) {
    gRawBuffer += text;
    if (gRawBuffer.size() > gMaxRawBuffer) gRawBuffer.erase(0, gRawBuffer.size() - gMaxRawBuffer);
    gNormBuffer = Normalize(gRawBuffer);
}

static BYTE KeyStateByte(int vk) {
    SHORT s = GetKeyState(vk);
    BYTE b = 0;
    if (s & 0x8000) b |= 0x80;
    if (s & 0x0001) b |= 0x01;
    return b;
}

static std::wstring KeyText(KBDLLHOOKSTRUCT* k) {
    BYTE st[256]{};
    int keys[] = {VK_SHIFT, VK_LSHIFT, VK_RSHIFT, VK_CONTROL, VK_LCONTROL, VK_RCONTROL, VK_MENU, VK_LMENU, VK_RMENU, VK_RWIN, VK_LWIN, VK_CAPITAL};
    for (int vk : keys) st[vk] = KeyStateByte(vk);

    HWND fg = GetForegroundWindow();
    DWORD tid = fg ? GetWindowThreadProcessId(fg, nullptr) : GetCurrentThreadId();
    HKL hkl = GetKeyboardLayout(tid);

    wchar_t buf[16]{};
    int r = ToUnicodeEx(k->vkCode, k->scanCode, st, buf, 15, 1 << 2, hkl);
    if (r > 0) return std::wstring(buf, r);

    if (r == 0) {
        switch (k->vkCode) {
        case VK_DECIMAL: return L".";
        case VK_OEM_PERIOD: return L".";
        case VK_OEM_COMMA: return L",";
        case VK_OEM_MINUS: return L"-";
        case VK_OEM_PLUS: return L"=";
        case VK_OEM_1: return L";";
        case VK_OEM_2: return L"/";
        case VK_OEM_3: return L"`";
        case VK_OEM_4: return L"[";
        case VK_OEM_5: return L"\\";
        case VK_OEM_6: return L"]";
        case VK_OEM_7: return L"'";
        default: break;
        }
    }

    if (k->vkCode >= '0' && k->vkCode <= '9') return std::wstring(1, (wchar_t)k->vkCode);
    if (k->vkCode >= VK_NUMPAD0 && k->vkCode <= VK_NUMPAD9) return std::wstring(1, (wchar_t)(L'0' + (k->vkCode - VK_NUMPAD0)));
    return L"";
}

static bool IsDelimiterVk(DWORD vk, WORD& outVk) {
    if (vk == VK_SPACE) { outVk = VK_SPACE; return true; }
    if (vk == VK_RETURN) { outVk = VK_RETURN; return true; }
    if (vk == VK_TAB) { outVk = VK_TAB; return true; }
    return false;
}

static void QueueExpansion(const MatchResult& m, int deleteAdjust, WORD delimiter) {
    if (m.index < 0) return;
    gTarget = GetForegroundWindow();
    gDeleteCount = std::max(0, m.rawDelete - deleteAdjust);
    gDelimiterToSend = delimiter;
    gPasteText = gSnips[m.index].text;
    ClearBuffer();
    PostMessageW(gWnd, WM_DO_EXPAND, 0, 0);
}

static LRESULT CALLBACK HookProc(int code, WPARAM wp, LPARAM lp) {
    if (code < 0 || !gEnabled || gExpanding) return CallNextHookEx(gHook, code, wp, lp);
    if (wp != WM_KEYDOWN && wp != WM_SYSKEYDOWN) return CallNextHookEx(gHook, code, wp, lp);

    auto* k = (KBDLLHOOKSTRUCT*)lp;
    if ((k->flags & LLKHF_INJECTED) || OwnWindow()) return CallNextHookEx(gHook, code, wp, lp);

    if (k->vkCode == VK_LSHIFT || k->vkCode == VK_RSHIFT || k->vkCode == VK_SHIFT || k->vkCode == VK_CAPITAL)
        return CallNextHookEx(gHook, code, wp, lp);

    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_LCONTROL) & 0x8000) || (GetKeyState(VK_RCONTROL) & 0x8000);
    bool alt = (GetKeyState(VK_MENU) & 0x8000) || (GetKeyState(VK_LMENU) & 0x8000) || (GetKeyState(VK_RMENU) & 0x8000);
    bool win = (GetKeyState(VK_LWIN) & 0x8000) || (GetKeyState(VK_RWIN) & 0x8000);

    if (ctrl && !alt && !win && k->vkCode == VK_SPACE) {
        MatchResult m = FindMatch();
        if (m.index >= 0) {
            QueueExpansion(m, 0, 0);
            return 1;
        }
    }

    WORD delimVk = 0;
    if (!ctrl && !alt && !win && IsDelimiterVk(k->vkCode, delimVk)) {
        MatchResult m = FindMatch();
        if (m.index >= 0) {
            QueueExpansion(m, 0, delimVk);
            return 1;
        }
        ClearBuffer();
        return CallNextHookEx(gHook, code, wp, lp);
    }

    if (k->vkCode == VK_BACK) {
        if (!gRawBuffer.empty()) gRawBuffer.pop_back();
        gNormBuffer = Normalize(gRawBuffer);
        return CallNextHookEx(gHook, code, wp, lp);
    }

    if (k->vkCode == VK_ESCAPE || k->vkCode == VK_LEFT || k->vkCode == VK_RIGHT || k->vkCode == VK_UP || k->vkCode == VK_DOWN ||
        k->vkCode == VK_DELETE || k->vkCode == VK_HOME || k->vkCode == VK_END || k->vkCode == VK_PRIOR || k->vkCode == VK_NEXT) {
        ClearBuffer();
        return CallNextHookEx(gHook, code, wp, lp);
    }

    std::wstring text = KeyText(k);
    if (text.empty()) return CallNextHookEx(gHook, code, wp, lp);

    bool hadCtrlAltText = (ctrl || alt || win) && text.empty();
    if (hadCtrlAltText) return CallNextHookEx(gHook, code, wp, lp);

    AddTextToBuffer(text);

    // Beeftext-like protection for multi-character keywords:
    // Only expand immediately for punctuation-terminated keywords that are not prefixes of longer keywords.
    if (IsPunctuationTriggerChar(text)) {
        MatchResult m = FindMatch();
        if (m.index >= 0 && !HasLongerPrefix(gSnips[m.index].keyNorm)) {
            QueueExpansion(m, (int)text.size(), 0);
            return 1;
        }
    }

    return CallNextHookEx(gHook, code, wp, lp);
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE:
        CreateWindowW(L"STATIC", L"chrome Text Expander v22", WS_CHILD | WS_VISIBLE, 18, 14, 400, 28, h, 0, gInst, 0);
        gCount = CreateWindowW(L"STATIC", L"Loaded snippets: 0", WS_CHILD | WS_VISIBLE, 18, 50, 420, 22, h, 0, gInst, 0);
        gStatus = CreateWindowW(L"STATIC", L"Running - Beeftext-style keyword recognition", WS_CHILD | WS_VISIBLE, 18, 78, 540, 22, h, 0, gInst, 0);
        CreateWindowW(L"BUTTON", L"Reload snippets", WS_CHILD | WS_VISIBLE, 18, 112, 140, 32, h, (HMENU)1, gInst, 0);
        CreateWindowW(L"BUTTON", L"Enable", WS_CHILD | WS_VISIBLE, 170, 112, 90, 32, h, (HMENU)2, gInst, 0);
        CreateWindowW(L"BUTTON", L"Disable", WS_CHILD | WS_VISIBLE, 270, 112, 90, 32, h, (HMENU)3, gInst, 0);
        gList = CreateWindowW(L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL, 18, 158, 540, 260, h, 0, gInst, 0);
        RefreshUi();
        return 0;

    case WM_COMMAND:
        if (LOWORD(w) == 1) {
            LoadSnippets();
            RefreshUi();
            ClearBuffer();
            SetStatus(L"Snippets reloaded");
        } else if (LOWORD(w) == 2) {
            gEnabled = true;
            SetStatus(L"Enabled");
        } else if (LOWORD(w) == 3) {
            gEnabled = false;
            ClearBuffer();
            SetStatus(L"Disabled");
        }
        return 0;

    case WM_DO_EXPAND:
        DoExpand();
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE h, HINSTANCE, PWSTR, int show) {
    gInst = h;
    LoadSnippets();

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = h;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.lpszClassName = L"chromeTextExpanderV22";
    RegisterClassW(&wc);

    gWnd = CreateWindowW(wc.lpszClassName, L"chrome Text Expander", WS_OVERLAPPEDWINDOW, 100, 100, 600, 500, nullptr, nullptr, h, nullptr);
    ShowWindow(gWnd, show);
    UpdateWindow(gWnd);

    gHook = SetWindowsHookExW(WH_KEYBOARD_LL, HookProc, GetModuleHandleW(nullptr), 0);
    if (!gHook) MessageBoxW(gWnd, L"Keyboard hook failed. Try Run as administrator.", L"chrome Text Expander", MB_ICONERROR);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (gHook) UnhookWindowsHookEx(gHook);
    return 0;
}
