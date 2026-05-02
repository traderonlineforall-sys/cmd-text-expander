# cmd Text Expander

Portable Windows text expander inspired by Beeftext.

## What it does

`cmd.exe` runs in the background and expands saved shortcuts into full canned responses anywhere you can type in Windows.

Example:

```text
;hi + Space
```

becomes:

```text
أهلاً بحضرتك، معاك إسلام من خدمة عملاء WE. ازاي أقدر أساعد حضرتك؟
```

## Features

- Portable Windows EXE named `cmd.exe`
- No installer
- No admin required for normal apps
- Add / edit / delete canned responses
- Search snippets
- Export / import JSON backup
- Runs in the tray when minimized
- Enable / Disable expander
- Expands shortcuts after Space, Enter, or Tab
- Saves data beside the EXE in `snippets.json`

## Download

1. Open the repository on GitHub.
2. Go to **Actions**.
3. Open the latest successful run named **Build cmd Text Expander**.
4. Download the artifact named **cmd_text_expander_final**.
5. Extract the ZIP.
6. Run `cmd.exe`.

## Usage

1. Run `cmd.exe`.
2. Add a keyword, for example `;hi`.
3. Add the response text.
4. Click Save.
5. Open any normal typing field in Chrome, Outlook, Notepad, WhatsApp Web, CRM, etc.
6. Type the keyword, then press Space, Enter, or Tab.
7. The keyword will be replaced with the saved response.

## Notes

- For programs running as Administrator, run `cmd.exe` as Administrator too.
- Some secured apps may block simulated paste.
- Do not place `cmd.exe` in `System32` or Windows PATH because Windows already has a command prompt named `cmd`.

## Build locally on Windows

```bat
build.bat
```

Output:

```text
publish\cmd.exe
```
