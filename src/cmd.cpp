#define UNICODE
#define _UNICODE
#define _WIN32_WINNT 0x0601

#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <map>
#include <cwctype>
#include <cstring>

struct Snippet {
    std::wstring keyword;
    std::wstring group;
    std::wstring text;
    bool enabled = true;
};

static HINSTANCE g_inst{};
static HWND g_hwnd{}, g_keyword{}, g_group{}, g_text{}, g_list{}, g_search{}, g_status{}, g_count{};
static HWND g_mode{}, g_customBox{}, g_customButton{}, g_modeStatus{};

static std::vector<Snippet> g_snips;

// v1.8 performance index:
// - map: normalized keyword -> snippet index
// - lengths: unique keyword lengths, sorted desc, so longest match always wins
static std::map<std::wstring, int> g_keyIndex;
static std::vector<size_t> g_keyLengths;

static int g_editIndex = -1;

static bool g_enabled = true;
static bool g_internalPaste = false;
static bool g_hasClipBackup = false;

static std::wstring g_buffer;
static std::wstring g_dataPath;
static std::wstring g_pendingText;
static std::wstring g_clipBackup;
static std::wstring g_customTrigger = L"؛";

static int g_pendingDelete = 0;
static HWND g_pendingTarget{};
static HWND g_lastForeground{};
static ULONGLONG g_lastKeyTick = 0;

static HHOOK g_hook{};
static NOTIFYICONDATAW g_tray{};

static HFONT g_font{}, g_titleFont{}, g_boldFont{};
static HBRUSH g_bgBrush{}, g_fieldBrush{};

static const UINT WM_EXPAND = WM_APP + 10;
static const UINT WM_TRAY = WM_APP + 20;
static const UINT TIMER_CLIP = 77;
static const UINT TRAY_UID = 1001;

enum Mode {
    MODE_CTRL_SPACE = 0,
    MODE_AUTO = 1,
    MODE_CUSTOM_TEXT = 2
};

static int g_modeValue = MODE_CTRL_SPACE;

enum CmdId {
    ID_SAVE = 1,
    ID_NEW = 2,
    ID_DELETE = 3,
    ID_COPY = 4,
    ID_IMPORT = 5,
    ID_EXPORT = 6,
    ID_ENABLE = 10,
    ID_DISABLE = 11,
    ID_MODE = 12,
    ID_SET_CUSTOM = 13,
    ID_KEYWORD = 20,
    ID_GROUP = 21,
    ID_TEXT = 22,
    ID_SEARCH = 30,
    ID_LIST = 31,
    ID_CUSTOM = 32,
    ID_TRAY_SHOW = 200,
    ID_TRAY_ENABLE = 201,
    ID_TRAY_DISABLE = 202,
    ID_TRAY_EXIT = 203
};

static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    UINT cp = CP_UTF8;
    if (n <= 0) {
        cp = CP_ACP;
        n = MultiByteToWideChar(cp, 0, s.data(), (int)s.size(), nullptr, 0);
    }
    if (n <= 0) return L"";
    std::wstring w(n, 0);
    MultiByteToWideChar(cp, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

static std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

static std::wstring BytesToWide(const std::string& b) {
    if (b.size() >= 2 && (unsigned char)b[0] == 0xFF && (unsigned char)b[1] == 0xFE) {
        std::wstring w;
        for (size_t i = 2; i + 1 < b.size(); i += 2) {
            w.push_back((wchar_t)((unsigned char)b[i] | ((unsigned char)b[i + 1] << 8)));
        }
        return w;
    }
    if (b.size() >= 3 && (unsigned char)b[0] == 0xEF && (unsigned char)b[1] == 0xBB && (unsigned char)b[2] == 0xBF) {
        return Utf8ToWide(b.substr(3));
    }
    return Utf8ToWide(b);
}

static std::wstring ExeDir() {
    wchar_t p[MAX_PATH]{};
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    std::wstring s = p;
    auto i = s.find_last_of(L"\\/");
    return i == std::wstring::npos ? L"." : s.substr(0, i);
}

static std::string ReadBytes(const std::wstring& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void WriteBytes(const std::wstring& path, const std::string& data) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(data.data(), (std::streamsize)data.size());
}

static std::wstring GetTextW2(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring s(n, 0);
    GetWindowTextW(h, s.data(), n + 1);
    return s;
}

static void SetTextW2(HWND h, const std::wstring& s) {
    SetWindowTextW(h, s.c_str());
}

static std::wstring Trim(std::wstring s) {
    auto sp = [](wchar_t c) {
        return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n';
    };
    while (!s.empty() && sp(s.front())) s.erase(s.begin());
    while (!s.empty() && sp(s.back())) s.pop_back();
    return s;
}

static std::wstring Lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });
    return s;
}

// Stronger Arabic/English/numeric normalization for reliable matching.
// It intentionally affects ONLY keyword matching, not the actual snippet text.
static std::wstring NormalizeKey(std::wstring s) {
    std::wstring out;
    bool lastWasSpace = false;

    for (wchar_t c : s) {
        // Remove BOM, zero-width marks, direction marks, Arabic tatweel, and diacritics.
        if (c == 0xFEFF || c == 0x200B || c == 0x200C || c == 0x200D || c == 0x200E || c == 0x200F) continue;
        if (c == 0x0640 || c == 0x0670 || (c >= 0x064B && c <= 0x065F)) continue;

        // Normalize whitespace to a single space.
        if (c == L'\t' || c == L'\r' || c == L'\n' || c == L' ') {
            if (!lastWasSpace && !out.empty()) {
                out.push_back(L' ');
                lastWasSpace = true;
            }
            continue;
        }

        lastWasSpace = false;

        // Normalize Arabic/Persian digits to ASCII.
        if (c >= 0x0660 && c <= 0x0669) c = L'0' + (c - 0x0660);
        else if (c >= 0x06F0 && c <= 0x06F9) c = L'0' + (c - 0x06F0);

        // Normalize common Arabic letter variants.
        else if (c == L'أ' || c == L'إ' || c == L'آ' || c == L'ٱ') c = L'ا';
        else if (c == L'ى') c = L'ي';
        else if (c == L'ؤ') c = L'و';
        else if (c == L'ئ') c = L'ي';
        else if (c == L'ة') c = L'ه';

        // Normalize common Arabic punctuation variants.
        else if (c == L'٫') c = L'.';
        else if (c == L'٬') c = L',';
        else if (c == L'؛') c = L';';

        out.push_back((wchar_t)towlower(c));
    }

    return Trim(out);
}

static std::wstring NormalizeId(std::wstring s) {
    s = Lower(Trim(s));
    if (!s.empty() && s.front() == L'{') s.erase(s.begin());
    if (!s.empty() && s.back() == L'}') s.pop_back();
    return s;
}

static bool LooksUuid(const std::wstring& s) {
    auto n = NormalizeId(s);
    return n.size() >= 32 && n.find(L'-') != std::wstring::npos;
}

static void RebuildKeyIndex() {
    g_keyIndex.clear();
    g_keyLengths.clear();

    for (size_t i = 0; i < g_snips.size(); ++i) {
        if (!g_snips[i].enabled) continue;

        std::wstring key = NormalizeKey(g_snips[i].keyword);
        if (key.empty()) continue;

        // First keyword wins. Duplicate saves update the existing one before reaching this stage.
        if (!g_keyIndex.count(key)) {
            g_keyIndex[key] = (int)i;
            if (std::find(g_keyLengths.begin(), g_keyLengths.end(), key.size()) == g_keyLengths.end()) {
                g_keyLengths.push_back(key.size());
            }
        }
    }

    std::sort(g_keyLengths.begin(), g_keyLengths.end(), [](size_t a, size_t b) { return a > b; });
}

static std::wstring JsonEscape(const std::wstring& s) {
    std::wstring o;
    for (wchar_t c : s) {
        if (c == L'\\') o += L"\\\\";
        else if (c == L'"') o += L"\\\"";
        else if (c == L'\n') o += L"\\n";
        else if (c == L'\r') o += L"\\r";
        else if (c == L'\t') o += L"\\t";
        else o += c;
    }
    return o;
}

static int HexVal(wchar_t h) {
    if (h >= L'0' && h <= L'9') return h - L'0';
    if (h >= L'a' && h <= L'f') return 10 + h - L'a';
    if (h >= L'A' && h <= L'F') return 10 + h - L'A';
    return -1;
}

static std::wstring JsonUnescape(const std::wstring& s) {
    std::wstring o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'\\' && i + 1 < s.size()) {
            wchar_t n = s[++i];
            if (n == L'n') o += L'\n';
            else if (n == L'r') o += L'\r';
            else if (n == L't') o += L'\t';
            else if (n == L'u' && i + 4 < s.size()) {
                int a = HexVal(s[i + 1]), b = HexVal(s[i + 2]), c = HexVal(s[i + 3]), d = HexVal(s[i + 4]);
                if (a >= 0 && b >= 0 && c >= 0 && d >= 0) {
                    wchar_t v = (wchar_t)((a << 12) | (b << 8) | (c << 4) | d);
                    o += v;
                    i += 4;
                }
            } else {
                o += n;
            }
        } else {
            o += s[i];
        }
    }
    return o;
}

static bool ExtractJsonString(const std::wstring& obj, const std::vector<std::wstring>& keys, std::wstring& out) {
    for (auto& key : keys) {
        auto p = obj.find(L"\"" + key + L"\"");
        if (p == std::wstring::npos) continue;
        p = obj.find(L':', p);
        if (p == std::wstring::npos) continue;
        p = obj.find(L'"', p + 1);
        if (p == std::wstring::npos) continue;

        ++p;
        std::wstring val;
        bool esc = false;

        for (; p < obj.size(); ++p) {
            wchar_t c = obj[p];
            if (esc) {
                val += L'\\';
                val += c;
                esc = false;
                continue;
            }
            if (c == L'\\') {
                esc = true;
                continue;
            }
            if (c == L'"') break;
            val += c;
        }

        out = JsonUnescape(val);
        return true;
    }
    return false;
}

static bool ExtractFalse(const std::wstring& obj, const std::wstring& key) {
    auto p = obj.find(L"\"" + key + L"\"");
    if (p == std::wstring::npos) return false;
    p = obj.find(L':', p);
    if (p == std::wstring::npos) return false;
    return Lower(obj.substr(p + 1, 12)).find(L"false") != std::wstring::npos;
}

static std::vector<std::wstring> JsonObjects(const std::wstring& json) {
    std::vector<std::wstring> out;
    int depth = 0;
    bool in = false, esc = false;
    size_t start = 0;

    for (size_t i = 0; i < json.size(); ++i) {
        wchar_t c = json[i];
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
            if (depth > 0 && --depth == 0) out.push_back(json.substr(start, i - start + 1));
        }
    }

    return out;
}

static std::wstring NamedArray(const std::wstring& json, const std::wstring& name) {
    auto p = json.find(L"\"" + name + L"\"");
    if (p == std::wstring::npos) return L"";
    p = json.find(L'[', p);
    if (p == std::wstring::npos) return L"";

    size_t start = p + 1;
    int depth = 1;
    bool in = false, esc = false;

    for (size_t i = p + 1; i < json.size(); ++i) {
        wchar_t c = json[i];
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
        if (c == L'[') depth++;
        else if (c == L']') {
            if (--depth == 0) return json.substr(start, i - start);
        }
    }

    return L"";
}

static std::vector<std::wstring> CsvCells(const std::wstring& line) {
    std::vector<std::wstring> c;
    std::wstring cur;
    bool q = false;

    for (size_t i = 0; i < line.size(); ++i) {
        wchar_t ch = line[i];
        if (ch == L'"') {
            if (q && i + 1 < line.size() && line[i + 1] == L'"') {
                cur += L'"';
                ++i;
            } else q = !q;
        } else if (ch == L',' && !q) {
            c.push_back(cur);
            cur.clear();
        } else {
            cur += ch;
        }
    }

    c.push_back(cur);
    return c;
}

static void LoadCsv(const std::wstring& text, std::vector<Snippet>& out) {
    std::wistringstream ss(text);
    std::wstring line;
    int key = -1, val = -1, grp = -1;
    bool first = true;

    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (Trim(line).empty()) continue;

        auto c = CsvCells(line);

        if (first) {
            first = false;
            for (size_t i = 0; i < c.size(); ++i) {
                auto h = Lower(Trim(c[i]));
                if (h == L"keyword" || h == L"shortcut" || h == L"abbreviation") key = (int)i;
                if (h == L"snippet" || h == L"text" || h == L"replacement" || h == L"content") val = (int)i;
                if (h == L"group" || h == L"topic") grp = (int)i;
            }
            if (key >= 0 && val >= 0) continue;
            key = -1;
            val = -1;
        }

        if (key < 0 || val < 0) {
            if (c.size() >= 3) {
                grp = 0; key = 1; val = 2;
            } else if (c.size() >= 2) {
                key = 0; val = 1; grp = -1;
            } else {
                continue;
            }
        }

        if ((int)c.size() > key && (int)c.size() > val) {
            Snippet s{ Trim(c[key]), grp >= 0 && (int)c.size() > grp ? Trim(c[grp]) : L"CSV", c[val], true };
            if (!s.keyword.empty() && !s.text.empty()) out.push_back(s);
        }
    }
}

static void LoadFromText(const std::wstring& text) {
    std::vector<Snippet> r;

    auto groupObjs = JsonObjects(NamedArray(text, L"groups"));
    auto comboObjs = JsonObjects(NamedArray(text, L"combos"));
    if (comboObjs.empty()) comboObjs = JsonObjects(text);

    std::map<std::wstring, std::wstring> groups;

    for (auto& obj : groupObjs) {
        std::wstring id, name;
        ExtractJsonString(obj, { L"uuid", L"id" }, id);
        ExtractJsonString(obj, { L"name", L"title" }, name);
        if (!id.empty() && !name.empty()) groups[NormalizeId(id)] = name;
    }

    for (auto& obj : comboObjs) {
        Snippet s;
        ExtractJsonString(obj, { L"keyword", L"key", L"trigger", L"shortcut", L"abbreviation" }, s.keyword);
        ExtractJsonString(obj, { L"text", L"snippet", L"replacement", L"value", L"content", L"phrase" }, s.text);
        ExtractJsonString(obj, { L"group", L"groupName", L"sheetName", L"group_uuid", L"groupUuid", L"groupId" }, s.group);

        auto ng = NormalizeId(s.group);
        if (!ng.empty() && groups.count(ng)) s.group = groups[ng];
        else if (LooksUuid(s.group)) s.group = L"Imported";

        s.keyword = Trim(s.keyword);
        s.enabled = !ExtractFalse(obj, L"enabled");

        if (!s.keyword.empty() && !s.text.empty()) r.push_back(s);
    }

    if (r.empty()) LoadCsv(text, r);

    if (!r.empty()) {
        std::vector<Snippet> clean;
        std::map<std::wstring, int> seen;

        for (auto& s : r) {
            std::wstring nk = NormalizeKey(s.keyword);
            if (nk.empty()) continue;

            // Same keyword: keep the latest imported definition.
            if (seen.count(nk)) {
                clean[seen[nk]] = s;
            } else {
                seen[nk] = (int)clean.size();
                clean.push_back(s);
            }
        }

        g_snips = clean;
    }
}

static void SaveSnippets() {
    std::wstring j = L"[\n";

    for (size_t i = 0; i < g_snips.size(); ++i) {
        auto& s = g_snips[i];
        j += L"  {\n";
        j += L"    \"keyword\": \"" + JsonEscape(s.keyword) + L"\",\n";
        j += L"    \"group\": \"" + JsonEscape(s.group) + L"\",\n";
        j += L"    \"text\": \"" + JsonEscape(s.text) + L"\",\n";
        j += L"    \"enabled\": true\n";
        j += L"  }";
        if (i + 1 < g_snips.size()) j += L",";
        j += L"\n";
    }

    j += L"]\n";
    WriteBytes(g_dataPath, WideToUtf8(j));
}

static void LoadSnippets() {
    g_snips.clear();

    auto b = ReadBytes(g_dataPath);
    if (!b.empty()) LoadFromText(BytesToWide(b));

    if (g_snips.empty()) {
        g_snips.push_back({ L";hi", L"Default", L"أهلاً بحضرتك، معاك إسلام من خدمة عملاء WE. ازاي أقدر أساعد حضرتك؟", true });
        g_snips.push_back({ L";thanks", L"Default", L"تحت أمرك يا فندم، سعدت بمساعدة حضرتك ونتمنالك يوم سعيد.", true });
        SaveSnippets();
    }

    RebuildKeyIndex();
}

static void Status(const std::wstring& s) {
    if (g_status) SetTextW2(g_status, s);
}

static std::wstring Preview(std::wstring t) {
    std::replace(t.begin(), t.end(), L'\n', L' ');
    std::replace(t.begin(), t.end(), L'\r', L' ');
    if (t.size() > 160) t = t.substr(0, 160) + L"...";
    return t;
}

static void RefreshList() {
    ListView_DeleteAllItems(g_list);

    std::wstring q = Lower(Trim(GetTextW2(g_search)));
    int count = 0;

    for (size_t i = 0; i < g_snips.size(); ++i) {
        auto& s = g_snips[i];
        auto all = Lower(s.keyword + L" " + s.group + L" " + s.text);

        if (q.empty() || all.find(q) != std::wstring::npos) {
            LVITEMW item{};
            item.mask = LVIF_TEXT | LVIF_PARAM;
            item.iItem = count;
            item.pszText = (LPWSTR)s.keyword.c_str();
            item.lParam = (LPARAM)i;

            int row = ListView_InsertItem(g_list, &item);
            ListView_SetItemText(g_list, row, 1, (LPWSTR)(s.group.empty() ? L"General" : s.group.c_str()));
            auto p = Preview(s.text);
            ListView_SetItemText(g_list, row, 2, (LPWSTR)p.c_str());
            ++count;
        }
    }

    SetTextW2(g_count, L"Showing " + std::to_wstring(count) + L" of " + std::to_wstring(g_snips.size()) + L" snippets");
}

static int SelectedIndex() {
    int s = ListView_GetNextItem(g_list, -1, LVNI_SELECTED);
    if (s < 0) return -1;

    LVITEMW item{};
    item.mask = LVIF_PARAM;
    item.iItem = s;

    return ListView_GetItem(g_list, &item) ? (int)item.lParam : -1;
}

static void LoadSelected() {
    int i = SelectedIndex();
    if (i < 0 || i >= (int)g_snips.size()) return;

    g_editIndex = i;
    SetTextW2(g_keyword, g_snips[i].keyword);
    SetTextW2(g_group, g_snips[i].group);
    SetTextW2(g_text, g_snips[i].text);
    Status(L"Editing " + g_snips[i].keyword);
}

static void ClearEditor() {
    g_editIndex = -1;
    SetTextW2(g_keyword, L"");
    SetTextW2(g_group, L"");
    SetTextW2(g_text, L"");
    SetFocus(g_keyword);
}

struct MatchResult {
    int index = -1;
    int deleteCount = 0;
};

static MatchResult BestSuffixMatch(const std::wstring& rawBuffer) {
    MatchResult best;

    std::wstring cleanBuffer = NormalizeKey(rawBuffer);
    if (cleanBuffer.empty() || g_keyIndex.empty()) return best;

    // Only test existing keyword lengths, longest first.
    for (size_t len : g_keyLengths) {
        if (cleanBuffer.size() < len) continue;

        std::wstring suffix = cleanBuffer.substr(cleanBuffer.size() - len);
        auto it = g_keyIndex.find(suffix);
        if (it == g_keyIndex.end()) continue;

        int index = it->second;
        std::wstring key = suffix;

        int rawDelete = 0;
        std::wstring rebuilt;

        for (int pos = (int)rawBuffer.size() - 1; pos >= 0; --pos) {
            rebuilt.insert(rebuilt.begin(), rawBuffer[pos]);
            rawDelete++;
            if (NormalizeKey(rebuilt) == key) break;
        }

        best.index = index;
        best.deleteCount = rawDelete > 0 ? rawDelete : (int)g_snips[index].keyword.size();
        return best;
    }

    return best;
}

static bool OwnWindowActive() {
    HWND fg = GetForegroundWindow();
    return fg == g_hwnd || IsChild(g_hwnd, fg);
}

static std::wstring KeyText(KBDLLHOOKSTRUCT* k) {
    BYTE ks[256];
    if (!GetKeyboardState(ks)) return L"";

    ks[k->vkCode] |= 0x80;

    wchar_t buf[8]{};
    int r = ToUnicodeEx(k->vkCode, k->scanCode, ks, buf, 7, 0, GetKeyboardLayout(0));
    if (r > 0) return std::wstring(buf, r);

    if (k->vkCode >= '0' && k->vkCode <= '9') return std::wstring(1, (wchar_t)k->vkCode);
    if (k->vkCode >= VK_NUMPAD0 && k->vkCode <= VK_NUMPAD9) return std::wstring(1, (wchar_t)(L'0' + (k->vkCode - VK_NUMPAD0)));
    if (k->vkCode == VK_SPACE) return L" ";

    return L"";
}

static void SendKey(WORD vk, bool up) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
    SendInput(1, &in, sizeof(INPUT));
}

static void ReleaseModifiers() {
    // Critical v1.8 fix:
    // Without this, Ctrl may remain logically down while backspacing after Ctrl+Space.
    // That can turn Backspace into Ctrl+Backspace and leave part of the keyword behind
    // such as keyword "2." leaving "2".
    SendKey(VK_CONTROL, true);
    SendKey(VK_MENU, true);
    SendKey(VK_SHIFT, true);
    Sleep(20);
}

static void SendBackspaces(int n) {
    for (int i = 0; i < n; i++) {
        SendKey(VK_BACK, false);
        SendKey(VK_BACK, true);
        Sleep(1);
    }
}

static void SendCtrlV() {
    SendKey(VK_CONTROL, false);
    SendKey('V', false);
    SendKey('V', true);
    SendKey(VK_CONTROL, true);
}

static bool OpenClipboardRetry() {
    for (int i = 0; i < 16; i++) {
        if (OpenClipboard(g_hwnd)) return true;
        Sleep(12);
    }
    return false;
}

static bool ClipboardGet(std::wstring& out) {
    if (!OpenClipboardRetry()) return false;

    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (!h) {
        CloseClipboard();
        return false;
    }

    wchar_t* p = (wchar_t*)GlobalLock(h);
    if (!p) {
        CloseClipboard();
        return false;
    }

    out = p;
    GlobalUnlock(h);
    CloseClipboard();
    return true;
}

static bool ClipboardSet(const std::wstring& s) {
    if (!OpenClipboardRetry()) return false;

    EmptyClipboard();

    size_t bytes = (s.size() + 1) * sizeof(wchar_t);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!h) {
        CloseClipboard();
        return false;
    }

    memcpy(GlobalLock(h), s.c_str(), bytes);
    GlobalUnlock(h);

    SetClipboardData(CF_UNICODETEXT, h);
    CloseClipboard();
    return true;
}

static void FocusTarget(HWND target) {
    if (!target) return;

    DWORD cur = GetCurrentThreadId();
    DWORD tgt = GetWindowThreadProcessId(target, nullptr);

    AttachThreadInput(cur, tgt, TRUE);
    SetForegroundWindow(target);
    SetFocus(target);
    AttachThreadInput(cur, tgt, FALSE);

    Sleep(45);
}

static void ExpandNow() {
    FocusTarget(g_pendingTarget);
    ReleaseModifiers();

    SendBackspaces(g_pendingDelete);
    Sleep(35);

    g_hasClipBackup = ClipboardGet(g_clipBackup);

    if (!ClipboardSet(g_pendingText)) {
        Status(L"Clipboard busy. Try again.");
        return;
    }

    Sleep(35);
    SendCtrlV();

    if (g_hasClipBackup) SetTimer(g_hwnd, TIMER_CLIP, 1200, nullptr);
    Status(L"Expanded snippet.");
}

static void QueueExpansion(int index, int deleteCount) {
    g_pendingTarget = GetForegroundWindow();
    g_pendingDelete = deleteCount;
    g_pendingText = g_snips[index].text;
    g_buffer.clear();
    PostMessageW(g_hwnd, WM_EXPAND, 0, 0);
}

static bool TriggerKey(DWORD vk) {
    return vk == VK_SPACE || vk == VK_RETURN || vk == VK_TAB;
}

static bool ResetKey(DWORD vk) {
    return vk == VK_ESCAPE || vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN ||
           vk == VK_HOME || vk == VK_END || vk == VK_DELETE || vk == VK_PRIOR || vk == VK_NEXT;
}

static LRESULT CALLBACK HookProc(int code, WPARAM wp, LPARAM lp) {
    if (code < 0 || !g_enabled || g_internalPaste) return CallNextHookEx(g_hook, code, wp, lp);
    if (wp != WM_KEYDOWN && wp != WM_SYSKEYDOWN) return CallNextHookEx(g_hook, code, wp, lp);

    auto* k = (KBDLLHOOKSTRUCT*)lp;

    if ((k->flags & LLKHF_INJECTED) || OwnWindowActive()) return CallNextHookEx(g_hook, code, wp, lp);

    HWND fg = GetForegroundWindow();
    auto now = GetTickCount64();

    if (fg != g_lastForeground || now - g_lastKeyTick > 60000) {
        g_buffer.clear();
        g_lastForeground = fg;
    }
    g_lastKeyTick = now;

    bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

    if (g_modeValue == MODE_CTRL_SPACE && ctrl && k->vkCode == VK_SPACE) {
        auto m = BestSuffixMatch(g_buffer);
        if (m.index >= 0) {
            QueueExpansion(m.index, m.deleteCount);
            return 1;
        }
    }

    if (k->vkCode == VK_BACK) {
        if (!g_buffer.empty()) g_buffer.pop_back();
        return CallNextHookEx(g_hook, code, wp, lp);
    }

    if (ResetKey(k->vkCode)) {
        g_buffer.clear();
        return CallNextHookEx(g_hook, code, wp, lp);
    }

    if (g_modeValue == MODE_AUTO && TriggerKey(k->vkCode)) {
        auto m = BestSuffixMatch(g_buffer);
        if (m.index >= 0) {
            QueueExpansion(m.index, m.deleteCount);
            return 1;
        }

        std::wstring t = KeyText(k);
        if (!t.empty()) {
            g_buffer += t;
            if (g_buffer.size() > 512) g_buffer.erase(0, g_buffer.size() - 512);
        }

        return CallNextHookEx(g_hook, code, wp, lp);
    }

    if (ctrl || alt) return CallNextHookEx(g_hook, code, wp, lp);

    std::wstring t = KeyText(k);
    if (!t.empty() && t[0] >= 32) {
        g_buffer += t;

        if (g_buffer.size() > 512) g_buffer.erase(0, g_buffer.size() - 512);

        if (g_modeValue == MODE_CUSTOM_TEXT && !g_customTrigger.empty()) {
            std::wstring cleanBuffer = NormalizeKey(g_buffer);
            std::wstring cleanTrigger = NormalizeKey(g_customTrigger);

            if (cleanBuffer.size() >= cleanTrigger.size() &&
                cleanBuffer.compare(cleanBuffer.size() - cleanTrigger.size(), cleanTrigger.size(), cleanTrigger) == 0) {

                std::wstring beforeTrigger = g_buffer;
                if (beforeTrigger.size() >= g_customTrigger.size()) {
                    beforeTrigger.erase(beforeTrigger.size() - g_customTrigger.size());
                }

                auto m = BestSuffixMatch(beforeTrigger);
                if (m.index >= 0) {
                    QueueExpansion(m.index, m.deleteCount + (int)g_customTrigger.size());
                    return CallNextHookEx(g_hook, code, wp, lp);
                }
            }
        }
    }

    return CallNextHookEx(g_hook, code, wp, lp);
}

static void StartHook() {
    if (!g_hook) g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, HookProc, GetModuleHandleW(nullptr), 0);
    g_enabled = g_hook != nullptr;
    Status(g_enabled ? L"cmd v1.8 enabled. Ctrl+Space recommended." : L"Hook failed. Try Run as administrator.");
}

static void StopHook() {
    g_enabled = false;
    g_buffer.clear();
    Status(L"cmd disabled.");
}

static void AddTray() {
    ZeroMemory(&g_tray, sizeof(g_tray));
    g_tray.cbSize = sizeof(g_tray);
    g_tray.hWnd = g_hwnd;
    g_tray.uID = TRAY_UID;
    g_tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_tray.uCallbackMessage = WM_TRAY;
    g_tray.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wcscpy_s(g_tray.szTip, L"cmd");
    Shell_NotifyIconW(NIM_ADD, &g_tray);
}

static void RemoveTray() {
    if (g_tray.cbSize) Shell_NotifyIconW(NIM_DELETE, &g_tray);
}

static void ShowMain() {
    ShowWindow(g_hwnd, SW_SHOW);
    ShowWindow(g_hwnd, SW_RESTORE);
    SetForegroundWindow(g_hwnd);
}

static void TrayMenu() {
    POINT pt;
    GetCursorPos(&pt);

    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, ID_TRAY_SHOW, L"Show cmd");
    AppendMenuW(m, MF_STRING, ID_TRAY_ENABLE, L"Enable");
    AppendMenuW(m, MF_STRING, ID_TRAY_DISABLE, L"Disable");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_TRAY_EXIT, L"Exit");

    SetForegroundWindow(g_hwnd);
    TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hwnd, nullptr);
    DestroyMenu(m);
}

static void UpdateMode() {
    g_modeValue = (int)SendMessageW(g_mode, CB_GETCURSEL, 0, 0);
    if (g_modeValue < 0) g_modeValue = MODE_CTRL_SPACE;

    BOOL show = g_modeValue == MODE_CUSTOM_TEXT;
    ShowWindow(g_customBox, show ? SW_SHOW : SW_HIDE);
    ShowWindow(g_customButton, show ? SW_SHOW : SW_HIDE);

    g_buffer.clear();

    if (g_modeValue == MODE_CTRL_SPACE) {
        SetTextW2(g_modeStatus, L"Mode: Ctrl + Space — stable for one-letter, Arabic, numeric, and punctuation shortcuts");
    } else if (g_modeValue == MODE_AUTO) {
        SetTextW2(g_modeStatus, L"Mode: Automatic — expands after Space / Enter / Tab");
    } else {
        SetTextW2(g_modeStatus, L"Custom trigger: " + g_customTrigger);
    }
}

static void SetCustomTrigger() {
    auto v = GetTextW2(g_customBox);

    if (Trim(v).empty()) {
        MessageBoxW(g_hwnd, L"اكتب رمز أو حرف أو كلمة للتفعيل. مثال: ؛ أو ب أو ##", L"cmd", MB_ICONWARNING);
        return;
    }

    g_customTrigger = v;
    SetTextW2(g_customBox, g_customTrigger);
    UpdateMode();
    Status(L"Custom trigger set: " + g_customTrigger);
}

static void ApplyFont(HWND h, HFONT f = nullptr) {
    SendMessageW(h, WM_SETFONT, (WPARAM)(f ? f : g_font), TRUE);
}

static HWND Ctl(const wchar_t* cls, const wchar_t* txt, DWORD style, int x, int y, int w, int h, int id, DWORD ex = 0) {
    HWND r = CreateWindowExW(ex, cls, txt, style, x, y, w, h, g_hwnd, (HMENU)(INT_PTR)id, g_inst, nullptr);
    ApplyFont(r);
    return r;
}

static HWND Label(const wchar_t* txt, int x, int y, int w, int h, HFONT f = nullptr) {
    HWND r = Ctl(L"STATIC", txt, WS_CHILD | WS_VISIBLE, x, y, w, h, 0);
    ApplyFont(r, f);
    return r;
}

static HWND Btn(const wchar_t* txt, int x, int y, int w, int h, int id) {
    return Ctl(L"BUTTON", txt, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, x, y, w, h, id);
}

static void SaveCurrent() {
    std::wstring k = Trim(GetTextW2(g_keyword));
    std::wstring g = Trim(GetTextW2(g_group));
    std::wstring t = GetTextW2(g_text);

    if (k.empty()) {
        MessageBoxW(g_hwnd, L"Keyword is required.", L"cmd", MB_ICONWARNING);
        SetFocus(g_keyword);
        return;
    }

    if (t.empty()) {
        MessageBoxW(g_hwnd, L"Snippet text is required.", L"cmd", MB_ICONWARNING);
        SetFocus(g_text);
        return;
    }

    Snippet s{ k, g, t, true };

    int duplicateIndex = -1;
    std::wstring nk = NormalizeKey(k);

    for (size_t i = 0; i < g_snips.size(); ++i) {
        if ((int)i == g_editIndex) continue;
        if (NormalizeKey(g_snips[i].keyword) == nk) {
            duplicateIndex = (int)i;
            break;
        }
    }

    if (g_editIndex >= 0 && g_editIndex < (int)g_snips.size()) {
        g_snips[g_editIndex] = s;
    } else if (duplicateIndex >= 0) {
        g_snips[duplicateIndex] = s;
    } else {
        g_snips.push_back(s);
    }

    RebuildKeyIndex();
    SaveSnippets();
    RefreshList();
    ClearEditor();

    Status(duplicateIndex >= 0 ? L"Updated existing keyword." : L"Saved.");
}

static std::wstring PickFile(bool save) {
    wchar_t file[MAX_PATH] = L"";

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"Backup/CSV files (*.json;*.btbackup;*.csv)\0*.json;*.btbackup;*.csv\0All files (*.*)\0*.*\0";
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);

    if (save) wcscpy_s(file, L"cmd-snippets.json");

    return (save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn)) ? file : L"";
}

static void ImportFile() {
    auto p = PickFile(false);
    if (p.empty()) return;

    auto b = ReadBytes(p);
    if (b.empty()) {
        MessageBoxW(g_hwnd, L"Could not read file.", L"cmd", MB_ICONERROR);
        return;
    }

    auto old = g_snips;
    g_snips.clear();

    LoadFromText(BytesToWide(b));

    if (g_snips.empty()) {
        g_snips = old;
        MessageBoxW(g_hwnd, L"No valid snippets found. Try Beeftext .btbackup, JSON, or CSV.", L"cmd", MB_ICONERROR);
        return;
    }

    RebuildKeyIndex();
    SaveSnippets();
    RefreshList();
    ClearEditor();

    Status(L"Imported " + std::to_wstring(g_snips.size()) + L" snippets. Index ready.");
}

static void ExportFile() {
    auto p = PickFile(true);
    if (p.empty()) return;

    SaveSnippets();
    WriteBytes(p, ReadBytes(g_dataPath));
    Status(L"Backup exported.");
}

static void BuildList() {
    g_list = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        WC_LISTVIEWW,
        L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        18, 372, 1060, 320,
        g_hwnd,
        (HMENU)ID_LIST,
        g_inst,
        nullptr
    );

    ApplyFont(g_list);
    ListView_SetExtendedListViewStyle(g_list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

    LVCOLUMNW c{};
    c.mask = LVCF_TEXT | LVCF_WIDTH;
    c.pszText = (LPWSTR)L"Keyword"; c.cx = 150; ListView_InsertColumn(g_list, 0, &c);
    c.pszText = (LPWSTR)L"Group"; c.cx = 160; ListView_InsertColumn(g_list, 1, &c);
    c.pszText = (LPWSTR)L"Preview"; c.cx = 720; ListView_InsertColumn(g_list, 2, &c);
}

static void Layout() {
    RECT r;
    GetClientRect(g_hwnd, &r);

    int W = r.right;
    int H = r.bottom;

    MoveWindow(g_status, 18, H - 30, W - 36, 24, TRUE);
    MoveWindow(g_search, 18, 332, 420, 30, TRUE);
    MoveWindow(g_count, W - 340, 336, 320, 24, TRUE);
    MoveWindow(g_list, 18, 372, W - 36, H - 410, TRUE);

    ListView_SetColumnWidth(g_list, 0, 150);
    ListView_SetColumnWidth(g_list, 1, 160);
    ListView_SetColumnWidth(g_list, 2, W - 370);
}

static void BuildUI() {
    Label(L"cmd", 18, 14, 220, 34, g_titleFont);

    Label(L"Expansion Mode", 650, 18, 150, 24, g_boldFont);
    g_mode = Ctl(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 650, 46, 205, 160, ID_MODE);

    SendMessageW(g_mode, CB_ADDSTRING, 0, (LPARAM)L"Ctrl + Space");
    SendMessageW(g_mode, CB_ADDSTRING, 0, (LPARAM)L"Automatic");
    SendMessageW(g_mode, CB_ADDSTRING, 0, (LPARAM)L"Custom trigger");
    SendMessageW(g_mode, CB_SETCURSEL, 0, 0);

    g_customBox = Ctl(L"EDIT", g_customTrigger.c_str(), WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 865, 46, 90, 28, ID_CUSTOM, WS_EX_CLIENTEDGE);
    g_customButton = Btn(L"Set", 965, 46, 58, 28, ID_SET_CUSTOM);

    Btn(L"Enable", 930, 82, 78, 32, ID_ENABLE);
    Btn(L"Disable", 1016, 82, 78, 32, ID_DISABLE);

    g_modeStatus = Label(L"", 18, 54, 610, 24, g_boldFont);

    Label(L"Keyword", 18, 102, 90, 22, g_boldFont);
    g_keyword = Ctl(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 112, 100, 260, 30, ID_KEYWORD, WS_EX_CLIENTEDGE);

    Label(L"Group", 392, 102, 70, 22, g_boldFont);
    g_group = Ctl(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 470, 100, 260, 30, ID_GROUP, WS_EX_CLIENTEDGE);

    Label(L"Snippet Text", 18, 144, 100, 22, g_boldFont);
    g_text = Ctl(L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL, 112, 140, 618, 130, ID_TEXT, WS_EX_CLIENTEDGE);

    Btn(L"Save", 752, 140, 90, 32, ID_SAVE);
    Btn(L"New", 852, 140, 90, 32, ID_NEW);
    Btn(L"Delete", 952, 140, 90, 32, ID_DELETE);
    Btn(L"Copy", 752, 180, 90, 32, ID_COPY);
    Btn(L"Import", 852, 180, 90, 32, ID_IMPORT);
    Btn(L"Export", 952, 180, 90, 32, ID_EXPORT);

    Label(L"Search", 18, 306, 90, 22, g_boldFont);
    g_search = Ctl(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 18, 332, 420, 30, ID_SEARCH, WS_EX_CLIENTEDGE);

    g_count = Label(L"", 760, 336, 320, 24, g_boldFont);

    BuildList();

    g_status = Label(L"Ready", 18, 710, 1060, 24, g_boldFont);
    UpdateMode();
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE:
        g_hwnd = h;
        g_dataPath = ExeDir() + L"\\snippets.json";

        g_font = CreateFontW(17, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        g_boldFont = CreateFontW(17, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        g_titleFont = CreateFontW(28, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");

        g_bgBrush = CreateSolidBrush(RGB(222, 244, 255));
        g_fieldBrush = CreateSolidBrush(RGB(255, 255, 255));

        LoadSnippets();
        AddTray();
        BuildUI();
        RefreshList();
        StartHook();
        return 0;

    case WM_ERASEBKGND: {
        RECT r;
        GetClientRect(h, &r);
        FillRect((HDC)w, &r, g_bgBrush);
        return 1;
    }

    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)w, TRANSPARENT);
        SetTextColor((HDC)w, RGB(12, 47, 70));
        return (LRESULT)g_bgBrush;

    case WM_CTLCOLOREDIT:
        SetBkColor((HDC)w, RGB(255, 255, 255));
        SetTextColor((HDC)w, RGB(15, 35, 45));
        return (LRESULT)g_fieldBrush;

    case WM_SIZE:
        if (w == SIZE_MINIMIZED) {
            ShowWindow(h, SW_HIDE);
            return 0;
        }
        Layout();
        return 0;

    case WM_TRAY:
        if (l == WM_LBUTTONDBLCLK) ShowMain();
        else if (l == WM_RBUTTONUP) TrayMenu();
        return 0;

    case WM_EXPAND:
        g_internalPaste = true;
        ExpandNow();
        g_internalPaste = false;
        return 0;

    case WM_TIMER:
        if (w == TIMER_CLIP) {
            KillTimer(h, TIMER_CLIP);
            if (g_hasClipBackup) ClipboardSet(g_clipBackup);
            g_hasClipBackup = false;
        }
        return 0;

    case WM_NOTIFY: {
        auto* nm = (NMHDR*)l;
        if (nm->idFrom == ID_LIST && nm->code == NM_DBLCLK) LoadSelected();
        return 0;
    }

    case WM_COMMAND: {
        int id = LOWORD(w);

        if (id == ID_SAVE) SaveCurrent();
        else if (id == ID_NEW) ClearEditor();
        else if (id == ID_DELETE) {
            int i = SelectedIndex();
            if (g_editIndex >= 0) i = g_editIndex;
            if (i >= 0 && i < (int)g_snips.size()) {
                g_snips.erase(g_snips.begin() + i);
                RebuildKeyIndex();
                SaveSnippets();
                RefreshList();
                ClearEditor();
            }
        } else if (id == ID_COPY) {
            ClipboardSet(GetTextW2(g_text));
        } else if (id == ID_IMPORT) {
            ImportFile();
        } else if (id == ID_EXPORT) {
            ExportFile();
        } else if (id == ID_ENABLE || id == ID_TRAY_ENABLE) {
            StartHook();
        } else if (id == ID_DISABLE || id == ID_TRAY_DISABLE) {
            StopHook();
        } else if (id == ID_MODE && HIWORD(w) == CBN_SELCHANGE) {
            UpdateMode();
        } else if (id == ID_SET_CUSTOM) {
            SetCustomTrigger();
        } else if (id == ID_TRAY_SHOW) {
            ShowMain();
        } else if (id == ID_TRAY_EXIT) {
            DestroyWindow(h);
        } else if (id == ID_SEARCH && HIWORD(w) == EN_CHANGE) {
            RefreshList();
        }

        return 0;
    }

    case WM_DESTROY:
        RemoveTray();
        if (g_hook) UnhookWindowsHookEx(g_hook);
        DeleteObject(g_font);
        DeleteObject(g_titleFont);
        DeleteObject(g_boldFont);
        DeleteObject(g_bgBrush);
        DeleteObject(g_fieldBrush);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(h, m, w, l);
}

int APIENTRY wWinMain(HINSTANCE h, HINSTANCE, LPWSTR, int n) {
    g_inst = h;

    INITCOMMONCONTROLSEX ic{ sizeof(ic), ICC_LISTVIEW_CLASSES };
    InitCommonControlsEx(&ic);

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = h;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"cmdV18EnterpriseReliable";

    RegisterClassW(&wc);

    HWND wnd = CreateWindowExW(
        0,
        wc.lpszClassName,
        L"cmd",
        WS_OVERLAPPEDWINDOW,
        100,
        80,
        1120,
        760,
        nullptr,
        nullptr,
        h,
        nullptr
    );

    ShowWindow(wnd, n);
    UpdateWindow(wnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return 0;
}
