$ErrorActionPreference = 'Stop'

$src = Join-Path $PSScriptRoot '..\src\cmd.cpp'
if (!(Test-Path $src)) {
    throw "src\cmd.cpp was not found"
}

$lines = [System.IO.File]::ReadAllLines($src)
for ($i = 0; $i -lt $lines.Length; $i++) {
    if ($lines[$i] -like '*case VK_OEM_5:*') {
        $lines[$i] = '    case VK_OEM_5: return L"\\";'
    }
}
[System.IO.File]::WriteAllLines($src, $lines, [System.Text.UTF8Encoding]::new($false))

$text = [System.IO.File]::ReadAllText($src)
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
'@

    if (!$text.Contains($needle)) {
        throw 'Could not locate BestSuffixMatch insertion point'
    }

    $text = $text.Replace($needle, $guard)
    [System.IO.File]::WriteAllText($src, $text, [System.Text.UTF8Encoding]::new($false))
}

Write-Host 'cmd.cpp source patch applied'
