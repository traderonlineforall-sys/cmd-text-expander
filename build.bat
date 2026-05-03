@echo off
setlocal
cd /d "%~dp0"
if exist publish rmdir /s /q publish
mkdir publish

where cl >nul 2>nul
if errorlevel 1 exit /b 1
where rc >nul 2>nul
if errorlevel 1 exit /b 1

powershell -NoProfile -ExecutionPolicy Bypass -Command "$p='src\cmd.cpp'; $t=[System.IO.File]::ReadAllText($p); $t=$t -replace 'case VK_OEM_5: return L\"\\\";','case VK_OEM_5: return L\"\\\\\";'; $needle='        auto found = g_keyIndex.find(suffix);`r`n        if (found == g_keyIndex.end()) continue;'; if($t -notlike '*Smart tail-collision guard*'){ $guard=@'
        auto found = g_keyIndex.find(suffix);
        if (found == g_keyIndex.end()) continue;

        // Smart tail-collision guard:
        // Avoid expanding a short keyword that is only the tail of a longer typed keyword.
        // Examples: keyword "." inside "word." or keyword "عليكم" inside "السلام عليكم".
        size_t suffixStart = cleanBuffer.size() - len;
        if (suffixStart > 0) {
            bool tailOfLongerKeyword = false;
            for (const auto& kv : g_keyIndex) {
                const std::wstring& longerKey = kv.first;
                if (longerKey.size() <= len) continue;
                if (longerKey.compare(longerKey.size() - len, len, suffix) != 0) continue;
                wchar_t beforeTyped = cleanBuffer[suffixStart - 1];
                wchar_t beforeLonger = longerKey[longerKey.size() - len - 1];
                if (beforeTyped == beforeLonger || !iswalnum((wint_t)beforeTyped)) {
                    tailOfLongerKeyword = true;
                    break;
                }
            }
            if (tailOfLongerKeyword) continue;

            wchar_t before = cleanBuffer[suffixStart - 1];
            bool suffixStartsWithWord = !suffix.empty() && iswalnum((wint_t)suffix[0]);
            if (!suffixStartsWithWord && iswalnum((wint_t)before)) continue;
        }
'@; $t=$t.Replace($needle,$guard) }; [System.IO.File]::WriteAllText($p,$t,[System.Text.UTF8Encoding]::new($false))"
if errorlevel 1 exit /b 1

rc /nologo /fo publish\app.res src\app.rc
if errorlevel 1 exit /b 1

cl /nologo /std:c++17 /O2 /EHsc /utf-8 /DNOMINMAX src\cmd.cpp publish\app.res /link /SUBSYSTEM:WINDOWS /OUT:publish\cmd.exe user32.lib shell32.lib comdlg32.lib gdi32.lib comctl32.lib
if errorlevel 1 exit /b 1

if not exist publish\cmd.exe exit /b 1
echo Build complete
