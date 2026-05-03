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
#include <vector>

// chrome Text Expander v21
// Independent implementation of Beeftext-like behavior:
// - global keyboard hook
// - reverse trie matching for large snippet sets
// - longest keyword wins
// - Arabic-aware normalization
// - exact raw-character deletion using event mapping
// - clipboard paste with restoration
// - visible GUI

struct Snippet {
    std::wstring keyword;
    std::wstring keyNorm;
    std::wstring text;
};

struct KeyEvent {
    std::wstring raw;
    std::wstring norm;
};

struct TrieNode {
    int snippetIndex = -1;
    std::unordered_map<wchar_t, int> next;
};

struct Match {
    int index = -1;
    int rawDelete = 0;
};

static HINSTANCE gInst{};
static HWND gWnd{}, gStatus{}, gCount{}, gList{};
static HHOOK gHook{};
static bool gEnabled = true;
static bool gExpanding = false;

static std::vector<Snippet> gSnips;
static std::vector<TrieNode> gTrie;
static std::vector<KeyEvent> gEvents;
static std::wstring gNormBuffer;

static const size_t HARD_EVENT_LIMIT = 2048;
static size_t gMaxEvents = 512;

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
    std::string out(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &out[0], n, nullptr, nullptr);
    return out;
}

static std::string ReadFileBytes(const std::wstring& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void WriteFileBytes(const std::wstring& path, const std::string& bytes) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return;
    f.write(bytes.data(), (std::streamsize)bytes.size());
}

static std::wstring ExeDir() {
    wchar_t p[MAX_PATH]{};
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    std::wstring s = p;
    size_t pos = s.find_last_of(L"\\/");
    return pos == std::wstring::npos ? L"." : s.substr(0, pos);
}

static std::wstring Trim(std::wstring s) {
    auto sp = [](wchar_t c) {
        return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n';
    };
    while (!s.empty() && sp(s.front())) s.erase(s.begin());
    while (!s.empty() && sp(s.back())) s.pop_back();
    return s;
}

static wchar_t NormChar(wchar_t c) {
    // Remove invisible marks, bidi marks, tatweel, and Arabic diacritics.
    if (c == 0xFEFF || c == 0x200B || c == 0x200C || c == 0x200D || c == 0x200E || c == 0x200F) return 0;
    if (c == 0x0640 || c == 0x0670 || (c >= 0x064B && c <= 0x065F) || (c >= 0x0610 && c <= 0x061A)) return 0;
    if ((c >= 0x06D6 && c <= 0x06DC) || (c >= 0x06DF && c <= 0x06E8) || (c >= 0x06EA && c <= 0x06ED)) return 0;

    // Arabic/Persian digits -> ASCII digits.
    if (c >= 0x0660 && c <= 0x0669) return L'0' + (c - 0x0660);
    if (c >= 0x06F0 && c <= 0x06F9) return L'0' + (c - 0x06F0);

    // Arabic letter normalization without Arabic source-code literals.
    if (c == 0x0622 || c == 0x0623 || c == 0x0625 || c == 0x0671 || c == 0x0675) return 0x0627; // alif variants -> alif
    if (c == 0x0649) return 0x064A; // alif maksura -> ya
    if (c == 0x0624) return 0x0648; // waw hamza -> waw
    if (c == 0x0626) return 0x064A; // ya hamza -> ya
    if (c == 0x0629) return 0x0647; // ta marbuta -> ha

    // Punctuation normalization.
    if (c == 0x066B) return L'.'; // Arabic decimal separator
    if (c == 0x066C) return L','; // Arabic thousands separator
    if (c == 0x061B) return L';'; // Arabic semicolon
    if (c == 0x060C) return L','; // Arabic comma

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

                    // Preserve surrogate pair if present.
                    if (first >= 0xD800 && first <= 0xDBFF && i + 6 < s.size() && s[i + 1] == L'\\' && s[i + 2] == L'u') {
                        int e = Hex(s[i + 3]), f = Hex(s[i + 4]), g = Hex(s[i + 5]), h = Hex(s[i + 6]);
                        if (e >= 0 && f >= 0 && g >= 0 && h >= 0) {
                            wchar_t second = (wchar_t)((e << 12) | (f << 8) | (g << 4) | h);
                            out.push_back(first);
                            out.push_back(second);
                            i += 6;
                        } else {
                            out.push_back(first);
                        }
                    } else {
                        out.push_back(first);
                    }
                }
            } else {
                out.push_back(n);
            }
        } else {
            out.push_back(s[i]);
        }
    }

    return out;
}

static std::wstring JsonEscape(const std::wstring& s) {
    std::wstring out;
    for (wchar_t c : s) {
        if (c == L'\\') out += L"\\\\";
        else if (c == L'"') out += L"\\\"";
        else if (c == L'\n') out += L"\\n";
        else if (c == L'\r') out += L"\\r";
        else if (c == L'\t') out += L"\\t";
        else out.push_back(c);
    }
    return out;
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

        if (c == L'"') {
            in = true;
            continue;
        }

        if (c == L'{') {
            if (depth++ == 0) start = i;
        } else if (c == L'}') {
            if (depth > 0 && --depth == 0) out.push_back(j.substr(start, i - start + 1));
        }
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

            if (esc) {
                raw.push_back(L'\\');
                raw.push_back(c);
                esc = false;
                continue;
            }

            if (c == L'\\') {
                esc = true;
                continue;
            }

            if (c == L'"') break;
            raw.push_back(c);
        }

        out = JsonUnescape(raw);
        return true;
    }

    return false;
}

static void BuildReverseTrie() {
    gTrie.clear();
    gTrie.emplace_back();
    gMaxEvents = 512;

    for (size_t i = 0; i < gSnips.size(); ++i) {
        const std::wstring& key = gSnips[i].keyNorm;
        if (key.empty()) continue;

        int node = 0;

        for (auto it = key.rbegin(); it != key.rend(); ++it) {
            wchar_t c = *it;
            auto found = gTrie[node].next.find(c);

            if (found == gTrie[node].next.end()) {
                int newIndex = (int)gTrie.size();
                gTrie[node].next[c] = newIndex;
                gTrie.emplace_back();
                node = newIndex;
            } else {
                node = found->second;
            }
        }

        // Latest duplicate wins, like many text expander imports.
        gTrie[node].snippetIndex = (int)i;
        gMaxEvents = std::max<size_t>(gMaxEvents, gSnips[i].keyword.size() + 64);
    }

    if (gMaxEvents > HARD_EVENT_LIMIT) gMaxEvents = HARD_EVENT_LIMIT;
}

static void LoadSnippets() {
    gSnips.clear();

    std::wstring path = ExeDir() + L"\\snippets.json";
    std::wstring content = Utf8ToWide(ReadFileBytes(path));

    for (const auto& obj : Objects(content)) {
        std::wstring key, text;

        GetJsonString(obj, {L"keyword", L"key", L"shortcut", L"abbreviation", L"trigger"}, key);
        GetJsonString(obj, {L"text", L"snippet", L"replacement", L"replace", L"value", L"content", L"phrase"}, text);

        key = Trim(key);
        if (key.empty() || text.empty()) continue;

        Snippet s{key, Normalize(key), text};
        if (!s.keyNorm.empty()) gSnips.push_back(s);
    }

    if (gSnips.empty()) {
        gSnips.push_back({L";hi", Normalize(L";hi"), L"Hello"});
        gSnips.push_back({L"ab", Normalize(L"ab"), L"Arabic-like test"});
        gSnips.push_back({L"2.", Normalize(L"2."), L"DOT TEST"});
    }

    BuildReverseTrie();
}

static Match FindMatch() {
    Match m;
    if (gEvents.empty() || gTrie.empty()) return m;

    int node = 0;
    int bestIndex = -1;
    int bestRawDelete = 0;
    int rawConsumed = 0;

    for (int e = (int)gEvents.size() - 1; e >= 0; --e) {
        const KeyEvent& ev = gEvents[e];
        rawConsumed += (int)ev.raw.size();

        // Empty normalized event: ignored character, but if it follows a keyword
        // it should still be deleted with the keyword.
        if (ev.norm.empty()) {
            if (gTrie[node].snippetIndex >= 0) {
                bestIndex = gTrie[node].snippetIndex;
                bestRawDelete = rawConsumed;
            }
            continue;
        }

        for (int j = (int)ev.norm.size() - 1; j >= 0; --j) {
            wchar_t c = ev.norm[j];

            auto found = gTrie[node].next.find(c);
            if (found == gTrie[node].next.end()) {
                if (bestIndex >= 0) {
                    m.index = bestIndex;
                    m.rawDelete = bestRawDelete;
                }
                return m;
            }

            node = found->second;

            if (gTrie[node].snippetIndex >= 0) {
                bestIndex = gTrie[node].snippetIndex;
                bestRawDelete = rawConsumed;
            }
        }
    }

    if (bestIndex >= 0) {
        m.index = bestIndex;
        m.rawDelete = bestRawDelete;
    }

    return m;
}

static void SetStatus(const std::wstring& s) {
    if (gStatus) SetWindowTextW(gStatus, s.c_str());
}

static void RefreshUi() {
    if (gCount) {
        SetWindowTextW(gCount, (L"Loaded snippets: " + std::to_wstring(gSnips.size())).c_str());
    }

    if (gList) {
        SendMessageW(gList, LB_RESETCONTENT, 0, 0);
        int count = (int)std::min<size_t>(gSnips.size(), 200);

        for (int i = 0; i < count; ++i) {
            SendMessageW(gList, LB_ADDSTRING, 0, (LPARAM)gSnips[i].keyword.c_str());
        }
    }
}

static void ReleaseMods() {
    WORD keys[] = {VK_CONTROL, VK_MENU, VK_SHIFT, VK_LCONTROL, VK_RCONTROL, VK_LMENU, VK_RMENU, VK_LSHIFT, VK_RSHIFT, VK_LWIN, VK_RWIN};

    for (WORD vk : keys) {
        INPUT in{};
        in.type = INPUT_KEYBOARD;
        in.ki.wVk = vk;
        in.ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(1, &in, sizeof(INPUT));
    }

    Sleep(80);
}

static void Key(WORD vk, bool up) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
    SendInput(1, &in, sizeof(INPUT));
}

static bool OpenClip() {
    for (int i = 0; i < 30; ++i) {
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
    if (!h) {
        CloseClipboard();
        return true;
    }

    wchar_t* p = (wchar_t*)GlobalLock(h);
    if (!p) {
        CloseClipboard();
        return true;
    }

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
    if (!h) {
        CloseClipboard();
        return false;
    }

    void* p = GlobalLock(h);
    if (!p) {
        GlobalFree(h);
        CloseClipboard();
        return false;
    }

    memcpy(p, s.c_str(), bytes);
    GlobalUnlock(h);

    SetClipboardData(CF_UNICODETEXT, h);
    CloseClipboard();
    return true;
}

static void ClearClip() {
    if (OpenClip()) {
        EmptyClipboard();
        CloseClipboard();
    }
}

static void DoExpand() {
    gExpanding = true;

    if (gTarget) {
        SetForegroundWindow(gTarget);
        Sleep(60);
    }

    ReleaseMods();

    for (int i = 0; i < gDeleteCount; ++i) {
        Key(VK_BACK, false);
        Key(VK_BACK, true);
        Sleep(1);
    }

    std::wstring old;
    bool hadText = false;
    bool couldReadClipboard = GetClip(old, hadText);

    if (!SetClip(gPasteText)) {
        SetStatus(L"Clipboard busy - expansion failed");
        gExpanding = false;
        return;
    }

    Sleep(40);
    Key(VK_CONTROL, false);
    Key('V', false);
    Key('V', true);
    Key(VK_CONTROL, true);

    if (gDelimiterToSend) {
        Sleep(25);
        Key(gDelimiterToSend, false);
        Key(gDelimiterToSend, true);
    }

    Sleep(120);

    if (couldReadClipboard) {
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

static void ClearBuffer() {
    gEvents.clear();
    gNormBuffer.clear();
}

static void RebuildNormBuffer() {
    gNormBuffer.clear();
    for (const auto& ev : gEvents) {
        gNormBuffer += ev.norm;
    }
}

static void AddToBuffer(const std::wstring& raw) {
    if (raw.empty()) return;

    KeyEvent ev{raw, Normalize(raw)};
    gEvents.push_back(ev);

    if (gEvents.size() > gMaxEvents) {
        size_t excess = gEvents.size() - gMaxEvents;
        gEvents.erase(gEvents.begin(), gEvents.begin() + (ptrdiff_t)excess);
    }

    RebuildNormBuffer();
}

static std::wstring KeyText(KBDLLHOOKSTRUCT* k) {
    BYTE st[256];
    if (!GetKeyboardState(st)) ZeroMemory(st, sizeof(st));

    // Low-level keyboard hooks run before the target app updates its key state.
    // Force the current key and live modifier states.
    st[k->vkCode] |= 0x80;

    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) st[VK_SHIFT] |= 0x80;
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) st[VK_CONTROL] |= 0x80;
    if (GetAsyncKeyState(VK_MENU) & 0x8000) st[VK_MENU] |= 0x80;

    HWND fg = GetForegroundWindow();
    DWORD tid = fg ? GetWindowThreadProcessId(fg, nullptr) : GetCurrentThreadId();
    HKL hkl = GetKeyboardLayout(tid);

    wchar_t buf[8]{};
    int r = ToUnicodeEx(k->vkCode, k->scanCode, st, buf, 7, 0, hkl);

    if (r > 0) return std::wstring(buf, r);

    // Punctuation/numpad fallbacks. This fixes common cases like 2. when
    // the layout fails to emit punctuation through ToUnicodeEx.
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

    if (k->vkCode >= '0' && k->vkCode <= '9') return std::wstring(1, (wchar_t)k->vkCode);
    if (k->vkCode >= 'A' && k->vkCode <= 'Z') return std::wstring(1, (wchar_t)towlower((wchar_t)k->vkCode));
    if (k->vkCode >= VK_NUMPAD0 && k->vkCode <= VK_NUMPAD9) return std::wstring(1, (wchar_t)(L'0' + (k->vkCode - VK_NUMPAD0)));

    return L"";
}

static bool IsDelimiterVk(DWORD vk, WORD& outVk, std::wstring& rawDelimiter) {
    if (vk == VK_SPACE) {
        outVk = VK_SPACE;
        rawDelimiter = L" ";
        return true;
    }

    if (vk == VK_RETURN) {
        outVk = VK_RETURN;
        rawDelimiter = L"\n";
        return true;
    }

    if (vk == VK_TAB) {
        outVk = VK_TAB;
        rawDelimiter = L"\t";
        return true;
    }

    return false;
}

static void StartExpansion(const Match& m, WORD delimiterToSend) {
    gTarget = GetForegroundWindow();
    gDeleteCount = m.rawDelete;
    gDelimiterToSend = delimiterToSend;
    gPasteText = gSnips[m.index].text;

    ClearBuffer();
    PostMessageW(gWnd, WM_DO_EXPAND, 0, 0);
}

static LRESULT CALLBACK HookProc(int code, WPARAM wp, LPARAM lp) {
    if (code < 0 || !gEnabled || gExpanding) return CallNextHookEx(gHook, code, wp, lp);
    if (wp != WM_KEYDOWN && wp != WM_SYSKEYDOWN) return CallNextHookEx(gHook, code, wp, lp);

    auto* k = (KBDLLHOOKSTRUCT*)lp;
    if ((k->flags & LLKHF_INJECTED) || OwnWindow()) return CallNextHookEx(gHook, code, wp, lp);

    bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

    if (ctrl && k->vkCode == VK_SPACE) {
        Match m = FindMatch();

        if (m.index >= 0) {
            StartExpansion(m, 0);
            return 1;
        }
    }

    if (!ctrl && !alt) {
        WORD delimVk = 0;
        std::wstring rawDelimiter;

        if (IsDelimiterVk(k->vkCode, delimVk, rawDelimiter)) {
            Match m = FindMatch();

            if (m.index >= 0) {
                StartExpansion(m, delimVk);
                return 1;
            }

            // Keep delimiters in the buffer to support multi-word keywords.
            AddToBuffer(rawDelimiter);
            return CallNextHookEx(gHook, code, wp, lp);
        }
    }

    if (k->vkCode == VK_BACK) {
        if (!gEvents.empty()) gEvents.pop_back();
        RebuildNormBuffer();
        return CallNextHookEx(gHook, code, wp, lp);
    }

    if (k->vkCode == VK_ESCAPE || k->vkCode == VK_LEFT || k->vkCode == VK_RIGHT ||
        k->vkCode == VK_UP || k->vkCode == VK_DOWN || k->vkCode == VK_DELETE ||
        k->vkCode == VK_HOME || k->vkCode == VK_END) {
        ClearBuffer();
        return CallNextHookEx(gHook, code, wp, lp);
    }

    if (ctrl || alt) return CallNextHookEx(gHook, code, wp, lp);

    std::wstring t = KeyText(k);

    if (!t.empty() && t[0] >= 32) {
        AddToBuffer(t);
    }

    return CallNextHookEx(gHook, code, wp, lp);
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE:
        CreateWindowW(L"STATIC", L"chrome Text Expander v21", WS_CHILD | WS_VISIBLE, 18, 14, 360, 28, h, 0, gInst, 0);
        gCount = CreateWindowW(L"STATIC", L"Loaded snippets: 0", WS_CHILD | WS_VISIBLE, 18, 50, 360, 22, h, 0, gInst, 0);
        gStatus = CreateWindowW(L"STATIC", L"Running - keyword + Space or Ctrl + Space", WS_CHILD | WS_VISIBLE, 18, 78, 520, 22, h, 0, gInst, 0);
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
    wc.lpszClassName = L"chromeTextExpanderGuiV21";

    RegisterClassW(&wc);

    gWnd = CreateWindowW(
        wc.lpszClassName,
        L"chrome Text Expander",
        WS_OVERLAPPEDWINDOW,
        100,
        100,
        600,
        500,
        nullptr,
        nullptr,
        h,
        nullptr
    );

    ShowWindow(gWnd, show);
    UpdateWindow(gWnd);

    gHook = SetWindowsHookExW(WH_KEYBOARD_LL, HookProc, GetModuleHandleW(nullptr), 0);

    if (!gHook) {
        MessageBoxW(gWnd, L"Keyboard hook failed. Try Run as administrator.", L"chrome Text Expander", MB_ICONERROR);
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (gHook) UnhookWindowsHookEx(gHook);
    return 0;
}
