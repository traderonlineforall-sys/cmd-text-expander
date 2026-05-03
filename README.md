# cmd v1.6 Reliability Upgrade

## What changed

- Best suffix matching instead of exact whole-buffer matching.
- One-letter and multi-letter shortcuts work more reliably.
- Shortcuts can be Arabic, English, numbers, symbols, or mixed.
- Normalizes invisible characters and Arabic/Persian digits.
- Decodes JSON unicode escapes such as `\u0627`.
- Ctrl + Space remains the recommended stable mode.

## Usage

1. Run `cmd.exe`.
2. Import your Beeftext backup or add snippets.
3. Use `Ctrl + Space` mode.
4. Type the keyword, then press `Ctrl + Space`.

Examples:

- `ا`
- `ab`
- `55`
- `3.`
- `فى البداية`
- `;hi`

## Modes

- Ctrl + Space: most reliable for Notepad, Chrome, WhatsApp Web, Outlook.
- Automatic: keyword then Space / Enter / Tab.
- Custom trigger: keyword + your custom text, e.g. `;hi؛`.
