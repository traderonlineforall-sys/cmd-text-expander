# cmd Text Expander v21

Independent Windows text-expander with Beeftext-like behavior.

## Why this version

This is not Beeftext and does not ship Beeftext binaries. It implements the same class of behavior independently:

- type a keyword
- press Space / Enter / Tab, or Ctrl + Space
- the keyword is deleted
- the snippet is pasted
- clipboard is restored

## v21 engine

- Reverse trie matching for large snippet sets.
- Longest match wins.
- Exact raw delete mapping, fixing cases like `2.` leaving `2`.
- Arabic-aware normalization:
  - أ / إ / آ / ٱ => ا
  - ى => ي
  - ة => ه
  - ؤ => و
  - ئ => ي
  - removes tashkeel, tatweel, bidi marks, zero-width marks
  - Arabic/Persian digits => English digits
- Uses active foreground keyboard layout for Arabic typing.
- Visible GUI window.

## Usage

Keep `snippets.json` beside `cmd.exe`.

Example:

```json
[
  { "keyword": "2.", "text": "DOT TEST" },
  { "keyword": "ابج", "text": "ARABIC TEST" }
]
```

Run `cmd.exe`, type the keyword in Notepad/Chrome/Outlook, then press Space or Ctrl + Space.

## Build

Run `build.bat` in a Visual Studio Developer Command Prompt, or use the included GitHub Actions workflow.
