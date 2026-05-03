# cmd v20 Final Smart Engine

Windows native text expander GUI.

## Main fixes in v20

- Uses a reverse trie matching engine for large snippet files.
- Fixes Arabic multi-letter keywords by using the active foreground keyboard layout in `ToUnicodeEx`.
- Forces current key state before `ToUnicodeEx`, which improves Arabic character capture in low-level hooks.
- Supports Arabic normalization:
  - alif variants normalize to ا
  - ى normalizes to ي
  - ة normalizes to ه
  - ؤ normalizes to و
  - ئ normalizes to ي
  - Arabic/Persian digits normalize to 0-9
  - tashkeel, tatweel, zero-width marks are ignored
- Fixes cases such as keyword `2.` leaving `2` by using at least the original keyword raw length when deleting.
- Does not clear the buffer after spaces when there is no match, so long/multi-word keywords can work.
- GUI app, not a black console.

## How to use

1. Put `snippets.json` next to `cmd.exe`.
2. Run `cmd.exe`.
3. Type a keyword, then press Space.
4. Or type a keyword, then press Ctrl + Space.

## JSON format

```json
[
  { "keyword": "2.", "text": "DOT TEST" },
  { "keyword": "اب", "text": "ARABIC TEST" }
]
```

## Build

GitHub Actions will build `publish\cmd.exe`.

Artifact name:

`cmd_v20_final_smart_engine_release`
