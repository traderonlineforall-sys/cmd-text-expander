# cmd v1.8 Enterprise Reliability

Portable Windows text expander.

## Critical fixes

- Fixes `2.` leaving `2` after expansion by releasing Ctrl/Alt/Shift before deletion.
- Indexed keyword engine for large snippet files.
- Longest-match-first behavior: if you have `ا` and `اب`, typing `اب` expands the longer keyword.
- Strong Arabic normalization:
  - أ / إ / آ / ٱ => ا
  - ى => ي
  - ؤ => و
  - ئ => ي
  - ة => ه
  - removes tashkeel / tatweel / zero-width marks
- Arabic/Persian digits normalize to English digits.
- Arabic punctuation variants normalize for matching.
- Duplicate keyword handling updates the existing keyword instead of creating conflicts.

## Recommended mode

Use **Ctrl + Space** mode for best reliability.

Examples:

- keyword `2.` then `Ctrl + Space`
- keyword `اب` then `Ctrl + Space`
- keyword `فى البداية` then `Ctrl + Space`

## Import

Supports:

- Beeftext `.btbackup`
- `.json`
- `.csv`
