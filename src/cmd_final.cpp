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

struct Snippet { std::wstring keyword, group, text; bool enabled = true; };

static HINSTANCE g_inst{};
static HWND g_hwnd{}, g_keyword{}, g_group{}, g_text{}, g_list{}, g_search{}, g_status{}, g_count{};
static HWND g_mode{}, g_hotkeyEdit{}, g_hotkeyButton{}, g_hotkeyStatus{};
static std::vector<Snippet> g_snips;
static int g_editIndex = -1;
static bool g_enabled = true, g_internal = false, g_hasClipBackup = false;
static std::wstring g_buffer, g_dataPath, g_pendingText, g_clipBackup;
static int g_pendingDelete = 0;
static HWND g_pendingTarget{};
static HHOOK g_hook{};
static NOTIFYICONDATAW g_nid{};
static HFONT g_font{}, g_titleFont{}, g_boldFont{};
static HBRUSH g_bgBrush{}, g_softBrush{}, g_fieldBrush{};
static WNDPROC g_hotkeyOldProc{};
static COLORREF C_BG = RGB(225,244,255), C_SOFT = RGB(207,235,250), C_CARD = RGB(238,250,255), C_TEXT = RGB(15,45,66), C_BLUE = RGB(0,135,205);

static const UINT WM_EXPAND_SNIPPET = WM_APP + 10;
static const UINT WM_TRAYICON = WM_APP + 20;
static const UINT TIMER_RESTORE_CLIP = 77;
static const UINT TRAY_UID = 1001;

enum Mode { MODE_AUTO = 0, MODE_CTRL_SPACE = 1, MODE_CUSTOM = 2 };
static int g_modeValue = MODE_AUTO;
static bool g_customCtrl = true, g_customAlt = true, g_customShift = false;
static UINT g_customVk = 'Q';
static std::wstring g_customDisplay = L"Ctrl+Alt+Q";

enum CmdId {
    ID_SAVE=1, ID_NEW=2, ID_DELETE=3, ID_COPY=4, ID_IMPORT=5, ID_EXPORT=6,
    ID_ENABLE=10, ID_DISABLE=11, ID_MODE=12, ID_SET_HOTKEY=13,
    ID_KEYWORD=20, ID_GROUP=21, ID_TEXT=22, ID_SEARCH=30, ID_LIST=31, ID_HOTKEY=32,
    ID_TRAY_SHOW=200, ID_TRAY_ENABLE=201, ID_TRAY_DISABLE=202, ID_TRAY_EXIT=203
};

static void SaveSnippets();

static std::wstring Utf8ToWide(const std::string& s){ if(s.empty()) return L""; int n=MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0); if(n<=0){ n=MultiByteToWideChar(CP_ACP,0,s.data(),(int)s.size(),nullptr,0); std::wstring w(n,0); if(n>0) MultiByteToWideChar(CP_ACP,0,s.data(),(int)s.size(),w.data(),n); return w; } std::wstring w(n,0); MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),w.data(),n); return w; }
static std::string WideToUtf8(const std::wstring& w){ if(w.empty()) return ""; int n=WideCharToMultiByte(CP_UTF8,0,w.data(),(int)w.size(),nullptr,0,nullptr,nullptr); std::string s(n,0); WideCharToMultiByte(CP_UTF8,0,w.data(),(int)w.size(),s.data(),n,nullptr,nullptr); return s; }
static std::wstring BytesToWide(const std::string& b){ if(b.size()>=2 && (unsigned char)b[0]==0xFF && (unsigned char)b[1]==0xFE){ std::wstring w; for(size_t i=2;i+1<b.size();i+=2) w.push_back((wchar_t)((unsigned char)b[i] | ((unsigned char)b[i+1]<<8))); return w; } if(b.size()>=3 && (unsigned char)b[0]==0xEF && (unsigned char)b[1]==0xBB && (unsigned char)b[2]==0xBF) return Utf8ToWide(b.substr(3)); return Utf8ToWide(b); }
static std::wstring ExeDir(){ wchar_t p[MAX_PATH]; GetModuleFileNameW(nullptr,p,MAX_PATH); std::wstring s=p; auto i=s.find_last_of(L"\\/"); return i==std::wstring::npos?L".":s.substr(0,i); }
static std::string ReadAllBytes(const std::wstring& path){ std::ifstream f(path,std::ios::binary); if(!f) return {}; std::ostringstream ss; ss<<f.rdbuf(); return ss.str(); }
static void WriteAllBytes(const std::wstring& path,const std::string& data){ std::ofstream f(path,std::ios::binary|std::ios::trunc); f.write(data.data(),(std::streamsize)data.size()); }
static std::wstring GetText(HWND h){ int n=GetWindowTextLengthW(h); std::wstring s(n,0); GetWindowTextW(h,s.data(),n+1); return s; }
static void SetText(HWND h,const std::wstring& s){ SetWindowTextW(h,s.c_str()); }
static std::wstring Trim(std::wstring s){ auto f=[](wchar_t c){return c==L' '||c==L'\t'||c==L'\r'||c==L'\n';}; while(!s.empty()&&f(s.front()))s.erase(s.begin()); while(!s.empty()&&f(s.back()))s.pop_back(); return s; }
static std::wstring Lower(std::wstring s){ std::transform(s.begin(),s.end(),s.begin(),[](wchar_t c){return (wchar_t)towlower(c);}); return s; }
static std::wstring NormId(std::wstring s){ s=Lower(Trim(s)); if(!s.empty()&&s.front()==L'{') s.erase(s.begin()); if(!s.empty()&&s.back()==L'}') s.pop_back(); return s; }
static bool LooksUuid(const std::wstring& s){ auto n=NormId(s); return n.size()>=32 && n.find(L'-')!=std::wstring::npos; }
static void ApplyFont(HWND h, HFONT f=nullptr){ SendMessageW(h, WM_SETFONT, (WPARAM)(f?f:g_font), TRUE); }

static std::wstring JsonEscape(const std::wstring& s){ std::wstring o; for(wchar_t c:s){ if(c==L'\\')o+=L"\\\\"; else if(c==L'\"')o+=L"\\\""; else if(c==L'\n')o+=L"\\n"; else if(c==L'\r')o+=L"\\r"; else if(c==L'\t')o+=L"\\t"; else o+=c;} return o; }
static std::wstring JsonUnescape(const std::wstring& s){ std::wstring o; for(size_t i=0;i<s.size();++i){ if(s[i]==L'\\'&&i+1<s.size()){ wchar_t n=s[++i]; if(n==L'n')o+=L'\n'; else if(n==L'r')o+=L'\r'; else if(n==L't')o+=L'\t'; else if(n==L'u' && i+4<s.size()){ unsigned v=0; bool ok=true; for(int k=0;k<4;k++){ wchar_t h=s[i+1+k]; v<<=4; if(h>=L'0'&&h<=L'9') v+=h-L'0'; else if(h>=L'a'&&h<=L'f') v+=10+h-L'a'; else if(h>=L'A'&&h<=L'F') v+=10+h-L'A'; else ok=false; } if(ok){ o+=(wchar_t)v; i+=4; } } else o+=n; } else o+=s[i]; } return o; }
static bool ExtractJsonString(const std::wstring& obj,const std::vector<std::wstring>& keys,std::wstring& out){ for(auto& k:keys){ std::wstring pat=L"\""+k+L"\""; size_t p=obj.find(pat); if(p==std::wstring::npos) continue; p=obj.find(L':',p+pat.size()); if(p==std::wstring::npos) continue; p=obj.find(L'\"',p+1); if(p==std::wstring::npos) continue; ++p; std::wstring val; bool esc=false; for(;p<obj.size();++p){ wchar_t c=obj[p]; if(esc){ val+=L'\\'; val+=c; esc=false; continue; } if(c==L'\\'){ esc=true; continue; } if(c==L'\"') break; val+=c; } out=JsonUnescape(val); return true; } return false; }
static bool ExtractJsonBoolFalse(const std::wstring& obj,const std::wstring& key){ std::wstring pat=L"\""+key+L"\""; size_t p=obj.find(pat); if(p==std::wstring::npos) return false; p=obj.find(L':',p+pat.size()); if(p==std::wstring::npos) return false; return Lower(obj.substr(p+1,12)).find(L"false")!=std::wstring::npos; }
static std::vector<std::wstring> JsonObjects(const std::wstring& json){ std::vector<std::wstring> out; int depth=0; bool in=false, esc=false; size_t start=0; for(size_t i=0;i<json.size();++i){ wchar_t c=json[i]; if(in){ if(esc) esc=false; else if(c==L'\\') esc=true; else if(c==L'\"') in=false; continue; } if(c==L'\"'){ in=true; continue; } if(c==L'{'){ if(depth++==0) start=i; } else if(c==L'}'){ if(depth>0 && --depth==0) out.push_back(json.substr(start,i-start+1)); } } return out; }
static std::wstring ExtractNamedArray(const std::wstring& json,const std::wstring& name){ std::wstring pat=L"\""+name+L"\""; size_t p=json.find(pat); if(p==std::wstring::npos) return L""; p=json.find(L'[',p+pat.size()); if(p==std::wstring::npos) return L""; size_t start=p+1; int depth=1; bool in=false, esc=false; for(size_t i=p+1;i<json.size();++i){ wchar_t c=json[i]; if(in){ if(esc) esc=false; else if(c==L'\\') esc=true; else if(c==L'\"') in=false; continue; } if(c==L'\"'){ in=true; continue; } if(c==L'[') depth++; else if(c==L']'){ if(--depth==0) return json.substr(start,i-start); } } return L""; }

static std::vector<std::wstring> CsvLine(const std::wstring& line){ std::vector<std::wstring> cells; std::wstring cur; bool q=false; for(size_t i=0;i<line.size();++i){ wchar_t c=line[i]; if(c==L'\"'){ if(q && i+1<line.size() && line[i+1]==L'\"'){ cur+=L'\"'; ++i; } else q=!q; } else if(c==L',' && !q){ cells.push_back(cur); cur.clear(); } else cur+=c; } cells.push_back(cur); return cells; }
static void LoadCsv(const std::wstring& text,std::vector<Snippet>& out){ std::wistringstream ss(text); std::wstring line; int key=-1,val=-1,grp=-1; bool first=true; while(std::getline(ss,line)){ if(!line.empty()&&line.back()==L'\r') line.pop_back(); if(Trim(line).empty()) continue; auto c=CsvLine(line); if(first){ first=false; for(size_t i=0;i<c.size();++i){ auto h=Lower(Trim(c[i])); if(h==L"keyword"||h==L"shortcut"||h==L"abbreviation") key=(int)i; if(h==L"snippet"||h==L"text"||h==L"replacement"||h==L"content") val=(int)i; if(h==L"group"||h==L"topic") grp=(int)i; } if(key>=0&&val>=0) continue; key=-1; val=-1; } if(key<0||val<0){ if(c.size()>=3){ grp=0; key=1; val=2; } else if(c.size()>=2){ key=0; val=1; grp=-1; } else continue; } if((int)c.size()>key&&(int)c.size()>val){ Snippet s; s.keyword=Trim(c[key]); s.text=c[val]; s.group=(grp>=0&&(int)c.size()>grp)?Trim(c[grp]):L"CSV"; if(!s.keyword.empty()&&!s.text.empty()) out.push_back(s); } } }

static void LoadSnippetsFromText(const std::wstring& text){
    std::vector<Snippet> r;
    auto groupObjs = JsonObjects(ExtractNamedArray(text,L"groups"));
    auto comboObjs = JsonObjects(ExtractNamedArray(text,L"combos"));
    if(comboObjs.empty()) comboObjs = JsonObjects(text);
    std::map<std::wstring,std::wstring> groups;
    for(auto& obj: groupObjs){ std::wstring id,name; ExtractJsonString(obj,{L"uuid",L"id"},id); ExtractJsonString(obj,{L"name",L"title"},name); if(!id.empty()&&!name.empty()) groups[NormId(id)] = name; }
    for(auto& obj: comboObjs){ Snippet s; ExtractJsonString(obj,{L"keyword",L"key",L"trigger",L"shortcut",L"abbreviation"},s.keyword); ExtractJsonString(obj,{L"text",L"snippet",L"replacement",L"value",L"content",L"phrase"},s.text); ExtractJsonString(obj,{L"group",L"groupName",L"sheetName",L"group_uuid",L"groupUuid",L"groupId"},s.group); auto ng=NormId(s.group); if(!ng.empty()&&groups.count(ng)) s.group=groups[ng]; else if(LooksUuid(s.group)) s.group=L"Imported"; s.keyword=Trim(s.keyword); s.enabled=!ExtractJsonBoolFalse(obj,L"enabled"); if(!s.keyword.empty()&&!s.text.empty()) r.push_back(s); }
    if(r.empty()) LoadCsv(text,r);
    if(!r.empty()){ std::vector<Snippet> clean; for(auto&s:r){ bool dup=false; for(auto&x:clean) if(x.keyword==s.keyword && x.text==s.text){dup=true;break;} if(!dup) clean.push_back(s); } g_snips=clean; }
}
static void LoadSnippets(){ g_snips.clear(); std::string bytes=ReadAllBytes(g_dataPath); if(!bytes.empty()) LoadSnippetsFromText(BytesToWide(bytes)); if(g_snips.empty()){ g_snips.push_back({L";hi",L"Default",L"أهلاً بحضرتك، معاك إسلام من خدمة عملاء WE. ازاي أقدر أساعد حضرتك؟",true}); g_snips.push_back({L";thanks",L"Default",L"تحت أمرك يا فندم، سعدت بمساعدة حضرتك ونتمنالك يوم سعيد.",true}); SaveSnippets(); } }
static void SaveSnippets(){ std::wstring j=L"[\n"; for(size_t i=0;i<g_snips.size();++i){ auto&s=g_snips[i]; j+=L"  {\n    \"keyword\": \""+JsonEscape(s.keyword)+L"\",\n    \"group\": \""+JsonEscape(s.group)+L"\",\n    \"text\": \""+JsonEscape(s.text)+L"\",\n    \"enabled\": true\n  }"; if(i+1<g_snips.size()) j+=L","; j+=L"\n";} j+=L"]\n"; WriteAllBytes(g_dataPath,WideToUtf8(j)); }

static void Status(const std::wstring& s){ if(g_status) SetWindowTextW(g_status,s.c_str()); }
static std::wstring Preview(const std::wstring& text){ std::wstring t=text; std::replace(t.begin(),t.end(),L'\n',L' '); std::replace(t.begin(),t.end(),L'\r',L' '); if(t.size()>160)t=t.substr(0,160)+L"..."; return t; }
static void RefreshList(){ ListView_DeleteAllItems(g_list); std::wstring q=Lower(Trim(GetText(g_search))); int count=0; for(size_t i=0;i<g_snips.size();++i){ auto&s=g_snips[i]; std::wstring all=Lower(s.keyword+L" "+s.group+L" "+s.text); if(q.empty()||all.find(q)!=std::wstring::npos){ LVITEMW item{}; item.mask=LVIF_TEXT|LVIF_PARAM; item.iItem=count; item.pszText=(LPWSTR)s.keyword.c_str(); item.lParam=(LPARAM)i; int row=ListView_InsertItem(g_list,&item); ListView_SetItemText(g_list,row,1,(LPWSTR)(s.group.empty()?L"General":s.group.c_str())); auto p=Preview(s.text); ListView_SetItemText(g_list,row,2,(LPWSTR)p.c_str()); ++count; }} SetWindowTextW(g_count,(L"Showing "+std::to_wstring(count)+L" of "+std::to_wstring(g_snips.size())+L" snippets").c_str()); }
static int SelectedSnippetIndex(){ int sel=ListView_GetNextItem(g_list,-1,LVNI_SELECTED); if(sel<0) return -1; LVITEMW item{}; item.mask=LVIF_PARAM; item.iItem=sel; if(!ListView_GetItem(g_list,&item)) return -1; return (int)item.lParam; }
static void LoadSelected(){ int i=SelectedSnippetIndex(); if(i<0||i>=(int)g_snips.size()) return; g_editIndex=i; SetText(g_keyword,g_snips[i].keyword); SetText(g_group,g_snips[i].group); SetText(g_text,g_snips[i].text); Status(L"Editing " + g_snips[i].keyword); }
static void ClearEditor(){ g_editIndex=-1; SetText(g_keyword,L""); SetText(g_group,L""); SetText(g_text,L""); SetFocus(g_keyword); }
static int FindMatch(const std::wstring& key){ for(size_t i=0;i<g_snips.size();++i) if(g_snips[i].enabled && g_snips[i].keyword==key) return (int)i; return -1; }
static bool OwnWindowActive(){ HWND fg=GetForegroundWindow(); return fg==g_hwnd || IsChild(g_hwnd,fg); }

static std::wstring KeyName(UINT vk){ if(vk>='A'&&vk<='Z') return std::wstring(1,(wchar_t)vk); if(vk>='0'&&vk<='9') return std::wstring(1,(wchar_t)vk); if(vk==VK_SPACE) return L"Space"; if(vk==VK_TAB) return L"Tab"; if(vk==VK_RETURN) return L"Enter"; if(vk>=VK_F1&&vk<=VK_F12) return L"F"+std::to_wstring(vk-VK_F1+1); wchar_t name[64]{}; UINT scan=MapVirtualKeyW(vk,MAPVK_VK_TO_VSC); if(GetKeyNameTextW((LONG)(scan<<16),name,64)>0) return name; return L"Key"; }
static std::wstring HotkeyDisplay(UINT vk,bool ctrl,bool alt,bool shift){ std::wstring s; if(ctrl) s+=L"Ctrl+"; if(alt) s+=L"Alt+"; if(shift) s+=L"Shift+"; s+=KeyName(vk); return s; }
static UINT ParseKeyName(std::wstring k){ k=Lower(Trim(k)); if(k==L"space") return VK_SPACE; if(k==L"tab") return VK_TAB; if(k==L"enter"||k==L"return") return VK_RETURN; if(k.size()==1){ wchar_t c=(wchar_t)towupper(k[0]); if((c>=L'A'&&c<=L'Z')||(c>=L'0'&&c<=L'9')) return (UINT)c; } if(k.size()>=2 && k[0]==L'f'){ int n=_wtoi(k.substr(1).c_str()); if(n>=1&&n<=12) return VK_F1+n-1; } return 0; }
static bool ApplyHotkeyFromText(){ std::wstring t=GetText(g_hotkeyEdit); std::vector<std::wstring> parts; std::wstring cur; for(wchar_t c:t){ if(c==L'+'){ parts.push_back(cur); cur.clear(); } else cur+=c; } if(!cur.empty()) parts.push_back(cur); bool ctrl=false,alt=false,shift=false; UINT vk=0; for(auto&p:parts){ auto x=Lower(Trim(p)); if(x==L"ctrl"||x==L"control") ctrl=true; else if(x==L"alt") alt=true; else if(x==L"shift") shift=true; else vk=ParseKeyName(x); } if(!vk || (!ctrl&&!alt&&!shift)){ MessageBoxW(g_hwnd,L"Use a safe combo like Ctrl+Alt+Q or Ctrl+Shift+Space.",L"cmd Text Expander",MB_ICONWARNING); return false; } g_customCtrl=ctrl; g_customAlt=alt; g_customShift=shift; g_customVk=vk; g_customDisplay=HotkeyDisplay(vk,ctrl,alt,shift); SetText(g_hotkeyEdit,g_customDisplay); SetText(g_hotkeyStatus,L"Active hotkey: "+g_customDisplay); Status(L"Custom hotkey set: "+g_customDisplay); return true; }
static LRESULT CALLBACK HotkeyEditProc(HWND h,UINT m,WPARAM w,LPARAM l){ if(m==WM_KEYDOWN||m==WM_SYSKEYDOWN){ UINT vk=(UINT)w; if(vk==VK_CONTROL||vk==VK_SHIFT||vk==VK_MENU) return 0; bool ctrl=(GetAsyncKeyState(VK_CONTROL)&0x8000)!=0, alt=(GetAsyncKeyState(VK_MENU)&0x8000)!=0, shift=(GetAsyncKeyState(VK_SHIFT)&0x8000)!=0; if(vk==VK_BACK){ SetText(h,L""); return 0; } SetText(h,HotkeyDisplay(vk,ctrl,alt,shift)); return 0; } return CallWindowProcW(g_hotkeyOldProc,h,m,w,l); }
static void UpdateModeUI(){ g_modeValue=(int)SendMessageW(g_mode,CB_GETCURSEL,0,0); if(g_modeValue<0) g_modeValue=MODE_AUTO; bool custom=g_modeValue==MODE_CUSTOM; ShowWindow(g_hotkeyEdit, custom?SW_SHOW:SW_HIDE); ShowWindow(g_hotkeyButton, custom?SW_SHOW:SW_HIDE); if(g_modeValue==MODE_AUTO) SetText(g_hotkeyStatus,L"Mode: Automatic — keyword then Space/Enter/Tab"); else if(g_modeValue==MODE_CTRL_SPACE) SetText(g_hotkeyStatus,L"Mode: Ctrl + Space"); else SetText(g_hotkeyStatus,L"Active hotkey: "+g_customDisplay); }

static std::wstring KeyToText(KBDLLHOOKSTRUCT* k){ BYTE ks[256]; if(!GetKeyboardState(ks)) return L""; ks[k->vkCode]|=0x80; wchar_t buf[8]{}; int r=ToUnicodeEx(k->vkCode,k->scanCode,ks,buf,7,0,GetKeyboardLayout(0)); if(r>0) return std::wstring(buf,r); if(k->vkCode>='0'&&k->vkCode<='9') return std::wstring(1,(wchar_t)k->vkCode); if(k->vkCode>=VK_NUMPAD0&&k->vkCode<=VK_NUMPAD9) return std::wstring(1,(wchar_t)(L'0'+(k->vkCode-VK_NUMPAD0))); return L""; }
static void SendKey(WORD vk,bool up){ INPUT in{}; in.type=INPUT_KEYBOARD; in.ki.wVk=vk; in.ki.dwFlags=up?KEYEVENTF_KEYUP:0; SendInput(1,&in,sizeof(INPUT)); }
static void SendBackspaces(int n){ for(int i=0;i<n;i++){ SendKey(VK_BACK,false); SendKey(VK_BACK,true); } }
static void SendCtrlV(){ SendKey(VK_CONTROL,false); SendKey('V',false); SendKey('V',true); SendKey(VK_CONTROL,true); }
static bool ClipboardGet(std::wstring& out){ if(!OpenClipboard(g_hwnd)) return false; HANDLE h=GetClipboardData(CF_UNICODETEXT); if(!h){ CloseClipboard(); return false; } wchar_t* p=(wchar_t*)GlobalLock(h); if(!p){ CloseClipboard(); return false; } out=p; GlobalUnlock(h); CloseClipboard(); return true; }
static bool ClipboardSet(const std::wstring& s){ if(!OpenClipboard(g_hwnd)) return false; EmptyClipboard(); size_t bytes=(s.size()+1)*sizeof(wchar_t); HGLOBAL h=GlobalAlloc(GMEM_MOVEABLE,bytes); if(!h){ CloseClipboard(); return false; } memcpy(GlobalLock(h),s.c_str(),bytes); GlobalUnlock(h); SetClipboardData(CF_UNICODETEXT,h); CloseClipboard(); return true; }
static void ExpandNow(){ if(g_pendingTarget){ SetForegroundWindow(g_pendingTarget); Sleep(35); } SendBackspaces(g_pendingDelete); Sleep(25); g_hasClipBackup=ClipboardGet(g_clipBackup); ClipboardSet(g_pendingText); Sleep(30); SendCtrlV(); if(g_hasClipBackup) SetTimer(g_hwnd,TIMER_RESTORE_CLIP,900,nullptr); Status(L"Expanded snippet."); }
static bool IsTriggerKey(DWORD vk){ return vk==VK_SPACE || vk==VK_RETURN || vk==VK_TAB; }
static bool IsResetKey(DWORD vk){ return vk==VK_ESCAPE||vk==VK_LEFT||vk==VK_RIGHT||vk==VK_UP||vk==VK_DOWN||vk==VK_HOME||vk==VK_END||vk==VK_DELETE||vk==VK_PRIOR||vk==VK_NEXT; }
static bool CustomHotkeyPressed(UINT vk){ bool ctrl=(GetAsyncKeyState(VK_CONTROL)&0x8000)!=0, alt=(GetAsyncKeyState(VK_MENU)&0x8000)!=0, shift=(GetAsyncKeyState(VK_SHIFT)&0x8000)!=0; return vk==g_customVk && ctrl==g_customCtrl && alt==g_customAlt && shift==g_customShift; }
static void QueueExpansion(int match){ g_pendingTarget=GetForegroundWindow(); g_pendingDelete=(int)g_buffer.size(); g_pendingText=g_snips[match].text; g_buffer.clear(); PostMessageW(g_hwnd,WM_EXPAND_SNIPPET,0,0); }
static LRESULT CALLBACK HookProc(int code, WPARAM wp, LPARAM lp){ if(code<0||!g_enabled||g_internal) return CallNextHookEx(g_hook,code,wp,lp); if(wp!=WM_KEYDOWN&&wp!=WM_SYSKEYDOWN) return CallNextHookEx(g_hook,code,wp,lp); auto*k=(KBDLLHOOKSTRUCT*)lp; if((k->flags&LLKHF_INJECTED)||OwnWindowActive()) return CallNextHookEx(g_hook,code,wp,lp); bool ctrl=(GetAsyncKeyState(VK_CONTROL)&0x8000)!=0; if(g_modeValue==MODE_CTRL_SPACE && ctrl && k->vkCode==VK_SPACE){ int m=FindMatch(g_buffer); if(m>=0){ QueueExpansion(m); return 1; } }
    if(g_modeValue==MODE_CUSTOM && CustomHotkeyPressed(k->vkCode)){ int m=FindMatch(g_buffer); if(m>=0){ QueueExpansion(m); return 1; } }
    if(k->vkCode==VK_BACK){ if(!g_buffer.empty()) g_buffer.pop_back(); return CallNextHookEx(g_hook,code,wp,lp); }
    if(IsResetKey(k->vkCode)){ g_buffer.clear(); return CallNextHookEx(g_hook,code,wp,lp); }
    if(g_modeValue==MODE_AUTO && IsTriggerKey(k->vkCode)){ int m=FindMatch(g_buffer); if(m>=0){ QueueExpansion(m); return 1; } g_buffer.clear(); return CallNextHookEx(g_hook,code,wp,lp); }
    if(ctrl || (GetAsyncKeyState(VK_MENU)&0x8000)) return CallNextHookEx(g_hook,code,wp,lp);
    std::wstring t=KeyToText(k); if(!t.empty()&&t[0]>=32){ g_buffer+=t; if(g_buffer.size()>120) g_buffer.erase(0,g_buffer.size()-120); }
    return CallNextHookEx(g_hook,code,wp,lp); }
static void StartHook(){ if(!g_hook) g_hook=SetWindowsHookExW(WH_KEYBOARD_LL,HookProc,GetModuleHandleW(nullptr),0); g_enabled=g_hook!=nullptr; Status(g_enabled?L"Enabled. Hook active.":L"Hook failed. Try Run as administrator."); }
static void StopHook(){ g_enabled=false; g_buffer.clear(); Status(L"Disabled."); }
static void AddTrayIcon(){ ZeroMemory(&g_nid,sizeof(g_nid)); g_nid.cbSize=sizeof(g_nid); g_nid.hWnd=g_hwnd; g_nid.uID=TRAY_UID; g_nid.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP; g_nid.uCallbackMessage=WM_TRAYICON; g_nid.hIcon=LoadIcon(nullptr,IDI_APPLICATION); wcscpy_s(g_nid.szTip,L"cmd Text Expander"); Shell_NotifyIconW(NIM_ADD,&g_nid); }
static void RemoveTrayIcon(){ if(g_nid.cbSize) Shell_NotifyIconW(NIM_DELETE,&g_nid); }
static void ShowMain(){ ShowWindow(g_hwnd,SW_SHOW); ShowWindow(g_hwnd,SW_RESTORE); SetForegroundWindow(g_hwnd); }
static void TrayMenu(){ POINT pt; GetCursorPos(&pt); HMENU m=CreatePopupMenu(); AppendMenuW(m,MF_STRING,ID_TRAY_SHOW,L"Show"); AppendMenuW(m,MF_STRING,ID_TRAY_ENABLE,L"Enable"); AppendMenuW(m,MF_STRING,ID_TRAY_DISABLE,L"Disable"); AppendMenuW(m,MF_SEPARATOR,0,nullptr); AppendMenuW(m,MF_STRING,ID_TRAY_EXIT,L"Exit"); SetForegroundWindow(g_hwnd); TrackPopupMenu(m,TPM_RIGHTBUTTON,pt.x,pt.y,0,g_hwnd,nullptr); DestroyMenu(m); }
static HWND C(const wchar_t* cls,const wchar_t* txt,DWORD style,int x,int y,int w,int h,HWND parent,int id, DWORD ex=0){ HWND hwnd=CreateWindowExW(ex,cls,txt,style,x,y,w,h,parent,(HMENU)(INT_PTR)id,g_inst,nullptr); ApplyFont(hwnd); return hwnd; }
static HWND L(const wchar_t* txt,int x,int y,int w,int h,HFONT f=nullptr){ HWND hwnd=C(L"STATIC",txt,WS_CHILD|WS_VISIBLE,x,y,w,h,g_hwnd,0); ApplyFont(hwnd,f); return hwnd; }
static HWND B(const wchar_t* txt,int x,int y,int w,int h,int id){ HWND hwnd=C(L"BUTTON",txt,WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,x,y,w,h,g_hwnd,id); return hwnd; }
static void SaveCurrent(){ std::wstring k=Trim(GetText(g_keyword)), g=Trim(GetText(g_group)), t=GetText(g_text); if(k.empty()){ MessageBoxW(g_hwnd,L"Keyword is required.",L"cmd Text Expander",MB_ICONWARNING); SetFocus(g_keyword); return; } if(t.empty()){ MessageBoxW(g_hwnd,L"Snippet text is required.",L"cmd Text Expander",MB_ICONWARNING); SetFocus(g_text); return; } Snippet s{k,g,t,true}; if(g_editIndex>=0&&g_editIndex<(int)g_snips.size()) g_snips[g_editIndex]=s; else g_snips.push_back(s); SaveSnippets(); RefreshList(); ClearEditor(); Status(L"Saved."); }
static std::wstring PickFile(bool save){ wchar_t file[MAX_PATH]=L""; OPENFILENAMEW ofn{}; ofn.lStructSize=sizeof(ofn); ofn.hwndOwner=g_hwnd; ofn.lpstrFile=file; ofn.nMaxFile=MAX_PATH; ofn.lpstrFilter=L"Backup/CSV files (*.json;*.btbackup;*.csv)\0*.json;*.btbackup;*.csv\0All files (*.*)\0*.*\0"; ofn.Flags=OFN_EXPLORER|OFN_PATHMUSTEXIST|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST); if(save) wcscpy_s(file,L"cmd-snippets.json"); return (save?GetSaveFileNameW(&ofn):GetOpenFileNameW(&ofn))?file:L""; }
static void ImportFile(){ auto p=PickFile(false); if(p.empty()) return; auto bytes=ReadAllBytes(p); if(bytes.empty()){ MessageBoxW(g_hwnd,L"Could not read file.",L"cmd Text Expander",MB_ICONERROR); return; } std::vector<Snippet> old=g_snips; g_snips.clear(); LoadSnippetsFromText(BytesToWide(bytes)); if(g_snips.empty()){ g_snips=old; MessageBoxW(g_hwnd,L"No valid snippets found. Try Beeftext .btbackup, JSON, or CSV.",L"cmd Text Expander",MB_ICONERROR); return; } SaveSnippets(); RefreshList(); ClearEditor(); Status(L"Imported "+std::to_wstring(g_snips.size())+L" snippets."); }
static void ExportFile(){ auto p=PickFile(true); if(p.empty()) return; SaveSnippets(); WriteAllBytes(p,ReadAllBytes(g_dataPath)); Status(L"Backup exported."); }
static void Layout(HWND h){ RECT r; GetClientRect(h,&r); int W=r.right,H=r.bottom; MoveWindow(g_status,18,H-30,W-36,24,TRUE); MoveWindow(g_search,18,332,420,30,TRUE); MoveWindow(g_count,W-340,336,320,24,TRUE); MoveWindow(g_list,18,372,W-36,H-410,TRUE); ListView_SetColumnWidth(g_list,0,150); ListView_SetColumnWidth(g_list,1,160); ListView_SetColumnWidth(g_list,2,W-370); }
static void BuildList(){ g_list=CreateWindowExW(WS_EX_CLIENTEDGE,WC_LISTVIEWW,L"",WS_CHILD|WS_VISIBLE|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,18,372,1060,320,g_hwnd,(HMENU)ID_LIST,g_inst,nullptr); ApplyFont(g_list); ListView_SetExtendedListViewStyle(g_list,LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES|LVS_EX_DOUBLEBUFFER); LVCOLUMNW col{}; col.mask=LVCF_TEXT|LVCF_WIDTH; col.pszText=(LPWSTR)L"Keyword"; col.cx=150; ListView_InsertColumn(g_list,0,&col); col.pszText=(LPWSTR)L"Group"; col.cx=160; ListView_InsertColumn(g_list,1,&col); col.pszText=(LPWSTR)L"Preview"; col.cx=720; ListView_InsertColumn(g_list,2,&col); }
static void BuildUi(){
    L(L"cmd Text Expander",18,14,360,34,g_titleFont);
    L(L"Expansion Mode",650,18,160,24,g_boldFont);
    g_mode=C(WC_COMBOBOXW,L"",WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST,650,46,260,160,g_hwnd,ID_MODE); SendMessageW(g_mode,CB_ADDSTRING,0,(LPARAM)L"Automatic"); SendMessageW(g_mode,CB_ADDSTRING,0,(LPARAM)L"Ctrl + Space"); SendMessageW(g_mode,CB_ADDSTRING,0,(LPARAM)L"Custom Hotkey"); SendMessageW(g_mode,CB_SETCURSEL,0,0);
    g_hotkeyEdit=C(L"EDIT",g_customDisplay.c_str(),WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,650,82,150,28,g_hwnd,ID_HOTKEY,WS_EX_CLIENTEDGE); g_hotkeyOldProc=(WNDPROC)SetWindowLongPtrW(g_hotkeyEdit,GWLP_WNDPROC,(LONG_PTR)HotkeyEditProc);
    g_hotkeyButton=B(L"Set Hotkey",810,82,100,28,ID_SET_HOTKEY);
    g_hotkeyStatus=L(L"Mode: Automatic — keyword then Space/Enter/Tab",18,54,610,24,g_boldFont);
    B(L"Enable",930,46,78,32,ID_ENABLE); B(L"Disable",1016,46,78,32,ID_DISABLE);
    L(L"Keyword",18,102,90,22,g_boldFont); g_keyword=C(L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,112,100,260,30,g_hwnd,ID_KEYWORD,WS_EX_CLIENTEDGE);
    L(L"Group",392,102,70,22,g_boldFont); g_group=C(L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,470,100,260,30,g_hwnd,ID_GROUP,WS_EX_CLIENTEDGE);
    L(L"Snippet Text",18,144,100,22,g_boldFont); g_text=C(L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL,112,140,618,130,g_hwnd,ID_TEXT,WS_EX_CLIENTEDGE);
    B(L"Save",752,140,90,32,ID_SAVE); B(L"New",852,140,90,32,ID_NEW); B(L"Delete",952,140,90,32,ID_DELETE);
    B(L"Copy",752,180,90,32,ID_COPY); B(L"Import",852,180,90,32,ID_IMPORT); B(L"Export",952,180,90,32,ID_EXPORT);
    L(L"Search",18,306,90,22,g_boldFont); g_search=C(L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,18,332,420,30,g_hwnd,ID_SEARCH,WS_EX_CLIENTEDGE);
    g_count=L(L"",760,336,320,24,g_boldFont); BuildList(); g_status=L(L"Ready",18,710,1060,24,g_boldFont); UpdateModeUI(); }
static LRESULT CALLBACK WndProc(HWND h,UINT m,WPARAM w,LPARAM l){ switch(m){ case WM_CREATE:{ g_hwnd=h; g_dataPath=ExeDir()+L"\\snippets.json"; g_font=CreateFontW(17,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI"); g_boldFont=CreateFontW(17,0,0,0,FW_SEMIBOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI"); g_titleFont=CreateFontW(28,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI"); g_bgBrush=CreateSolidBrush(C_BG); g_softBrush=CreateSolidBrush(C_SOFT); g_fieldBrush=CreateSolidBrush(RGB(255,255,255)); LoadSnippets(); AddTrayIcon(); BuildUi(); RefreshList(); StartHook(); return 0; }
case WM_ERASEBKGND:{ RECT rc; GetClientRect(h,&rc); FillRect((HDC)w,&rc,g_bgBrush); return 1; }
case WM_CTLCOLORSTATIC:{ HDC dc=(HDC)w; SetBkMode(dc,TRANSPARENT); SetTextColor(dc,C_TEXT); return (LRESULT)g_bgBrush; }
case WM_CTLCOLOREDIT:{ HDC dc=(HDC)w; SetBkColor(dc,RGB(255,255,255)); SetTextColor(dc,RGB(20,35,45)); return (LRESULT)g_fieldBrush; }
case WM_SIZE: if(w==SIZE_MINIMIZED){ ShowWindow(h,SW_HIDE); return 0;} Layout(h); return 0;
case WM_TRAYICON: if(l==WM_LBUTTONDBLCLK) ShowMain(); else if(l==WM_RBUTTONUP) TrayMenu(); return 0;
case WM_EXPAND_SNIPPET: g_internal=true; ExpandNow(); g_internal=false; return 0;
case WM_TIMER: if(w==TIMER_RESTORE_CLIP){ KillTimer(h,TIMER_RESTORE_CLIP); if(g_hasClipBackup) ClipboardSet(g_clipBackup); g_hasClipBackup=false;} return 0;
case WM_NOTIFY:{ auto* nm=(NMHDR*)l; if(nm->idFrom==ID_LIST && nm->code==NM_DBLCLK) LoadSelected(); return 0; }
case WM_COMMAND:{ int id=LOWORD(w); if(id==ID_SAVE) SaveCurrent(); else if(id==ID_NEW) ClearEditor(); else if(id==ID_DELETE){ int i=SelectedSnippetIndex(); if(g_editIndex>=0)i=g_editIndex; if(i>=0&&i<(int)g_snips.size()){ g_snips.erase(g_snips.begin()+i); SaveSnippets(); RefreshList(); ClearEditor(); }} else if(id==ID_COPY) ClipboardSet(GetText(g_text)); else if(id==ID_IMPORT) ImportFile(); else if(id==ID_EXPORT) ExportFile(); else if(id==ID_ENABLE||id==ID_TRAY_ENABLE) StartHook(); else if(id==ID_DISABLE||id==ID_TRAY_DISABLE) StopHook(); else if(id==ID_SET_HOTKEY) ApplyHotkeyFromText(); else if(id==ID_MODE && HIWORD(w)==CBN_SELCHANGE) UpdateModeUI(); else if(id==ID_TRAY_SHOW) ShowMain(); else if(id==ID_TRAY_EXIT) DestroyWindow(h); else if(id==ID_SEARCH && HIWORD(w)==EN_CHANGE) RefreshList(); return 0; }
case WM_DESTROY: RemoveTrayIcon(); if(g_hook)UnhookWindowsHookEx(g_hook); DeleteObject(g_font); DeleteObject(g_titleFont); DeleteObject(g_boldFont); DeleteObject(g_bgBrush); DeleteObject(g_softBrush); DeleteObject(g_fieldBrush); PostQuitMessage(0); return 0; } return DefWindowProcW(h,m,w,l); }

int APIENTRY wWinMain(HINSTANCE h,HINSTANCE,LPWSTR,int n){ g_inst=h; INITCOMMONCONTROLSEX icc{sizeof(icc),ICC_LISTVIEW_CLASSES}; InitCommonControlsEx(&icc); WNDCLASSW wc{}; wc.lpfnWndProc=WndProc; wc.hInstance=h; wc.hCursor=LoadCursor(nullptr,IDC_ARROW); wc.hIcon=LoadIcon(nullptr,IDI_APPLICATION); wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1); wc.lpszClassName=L"CmdTextExpanderFinal"; RegisterClassW(&wc); HWND wnd=CreateWindowExW(0,wc.lpszClassName,L"cmd Text Expander",WS_OVERLAPPEDWINDOW,100,80,1120,760,nullptr,nullptr,h,nullptr); ShowWindow(wnd,n); UpdateWindow(wnd); MSG msg; while(GetMessageW(&msg,nullptr,0,0)){ TranslateMessage(&msg); DispatchMessageW(&msg);} return 0; }
