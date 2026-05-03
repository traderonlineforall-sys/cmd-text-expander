$ErrorActionPreference = 'Stop'

$src = Join-Path $PSScriptRoot '..\src\cmd.cpp'
if (!(Test-Path $src)) {
    throw "src\cmd.cpp was not found"
}

# Fix the escaped backslash fallback safely before compiling.
$lines = [System.IO.File]::ReadAllLines($src)
for ($i = 0; $i -lt $lines.Length; $i++) {
    if ($lines[$i] -like '*case VK_OEM_5:*') {
        $lines[$i] = '    case VK_OEM_5: return L"\\";'
    }
}
[System.IO.File]::WriteAllLines($src, $lines, [System.Text.UTF8Encoding]::new($false))

$text = [System.IO.File]::ReadAllText($src)

# 1) Prevent short tail matches from stealing longer phrases.
if ($text -notlike '*Smart tail-collision guard*') {
    $needle = @'
        auto found = g_keyIndex.find(suffix);
        if (found == g_keyIndex.end()) continue;
'@

    $guard = @'
        auto found = g_keyIndex.find(suffix);
        if (found == g_keyIndex.end()) continue;

        // Smart tail-collision guard:
        // Do not expand a short keyword when it is only the tail of a longer typed phrase.
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
'@

    if (!$text.Contains($needle)) {
        throw 'Could not locate BestSuffixMatch insertion point'
    }

    $text = $text.Replace($needle, $guard)
    [System.IO.File]::WriteAllText($src, $text, [System.Text.UTF8Encoding]::new($false))
}

# 2) Mature exact punctuation behavior:
#    - A punctuation-only shortcut like "." expands when typed alone or as a standalone token.
#    - It does not steal the tail of "word.".
#    - A longer exact keyword ending with punctuation, like "word.", wins as a full token.
$text = [System.IO.File]::ReadAllText($src)
if ($text -notlike '*Smart exact punctuation expansion*') {
    $needle2 = @'
        g_buffer += t;
        if (g_buffer.size() > 512) g_buffer.erase(0, g_buffer.size() - 512);

        if (g_modeValue == MODE_CUSTOM_TEXT && !g_customTrigger.empty()) {
'@

    $replacement2 = @'
        g_buffer += t;
        if (g_buffer.size() > 512) g_buffer.erase(0, g_buffer.size() - 512);

        // Smart exact punctuation expansion:
        // This allows a shortcut like "." to work when typed alone, while avoiding
        // accidental expansion when the dot is only punctuation at the end of a word.
        if (g_modeValue == MODE_AUTO) {
            std::wstring cleanBuffer = NormalizeKey(g_buffer);
            size_t tokenStart = cleanBuffer.find_last_of(L" ");
            std::wstring currentToken = tokenStart == std::wstring::npos ? cleanBuffer : cleanBuffer.substr(tokenStart + 1);
            auto exact = g_keyIndex.find(currentToken);
            if (exact != g_keyIndex.end() && !currentToken.empty()) {
                bool tokenHasWordChar = false;
                for (wchar_t ch : currentToken) {
                    if (iswalnum((wint_t)ch)) { tokenHasWordChar = true; break; }
                }
                bool lastTypedIsWordChar = !t.empty() && iswalnum((wint_t)t[0]);
                if (!lastTypedIsWordChar && (!tokenHasWordChar || currentToken.size() > 1)) {
                    int rawDelete = 0;
                    for (int pos = (int)g_buffer.size() - 1; pos >= 0; --pos) {
                        if (g_buffer[pos] == L' ' || g_buffer[pos] == L'\t' || g_buffer[pos] == L'\r' || g_buffer[pos] == L'\n') break;
                        rawDelete++;
                    }
                    int keywordChars = (int)g_snips[exact->second].keyword.size();
                    QueueExpansion(exact->second, std::max(rawDelete, keywordChars));
                    return 1;
                }
            }
        }

        if (g_modeValue == MODE_CUSTOM_TEXT && !g_customTrigger.empty()) {
'@

    if (!$text.Contains($needle2)) {
        throw 'Could not locate normal input buffer insertion point'
    }

    $text = $text.Replace($needle2, $replacement2)
    [System.IO.File]::WriteAllText($src, $text, [System.Text.UTF8Encoding]::new($false))
}

Write-Host 'cmd.cpp source patch applied'
