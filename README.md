cmd – Native Text Expander (Final Version)
========================================

This project provides a **portable text‑expander** for Windows that behaves similarly to
popular tools like Beeftext.  It reads your canned responses (snippets) from a
`snippets.json` file and monitors keyboard input at the system level.  When
you type a defined keyword and then press a delimiter (space, tab, or enter),
the program automatically erases the keyword and pastes the corresponding
replacement text.  A global hotkey (Ctrl + Space by default) can trigger
expansion manually, and a custom trigger string can also be configured.

Key Features
------------

* **Portable and lightweight** – written in modern C++ without any
  dependencies.  The compiled binary runs as a standalone executable.
* **Robust matching engine** – canonicalizes Arabic and Latin characters,
  removes diacritics, normalizes digits and performs longest‑suffix matching
  against a large library of keywords.  This allows multi‑character Arabic
  keywords to be recognized even when many snippets are loaded.
* **Triggers** – expansions are triggered by space/tab/enter, by Ctrl + Space,
  or by an optional custom string.  You can configure the trigger characters at
  the top of `cmd.cpp`.
* **Clipboard preservation** – the program temporarily replaces the clipboard
  contents when pasting your snippet and restores the original clipboard
  afterwards.
* **Unicode support** – snippets and keywords can contain Arabic, Persian and
  Latin text.  All matching is case‑insensitive for Latin letters and
  tolerant of different Arabic forms (e.g. various alif and ya variants).

How It Works
------------

The program installs a low‑level keyboard hook (`WH_KEYBOARD_LL`) to observe
every key press.  Characters are normalized and appended to an internal
buffer.  When you press a delimiter or the hotkey, the buffer’s suffix is
checked against all canonicalized keywords.  If a match is found, the
program:

1. Releases modifier keys (Ctrl/Shift/Alt/Win) to prevent sending accidental
   shortcuts while deleting text.
2. Erases the keyword (and custom trigger if used) by sending backspaces.
3. Temporarily replaces the clipboard with the snippet text.
4. Pastes it with Ctrl + V.
5. Restores the previous clipboard contents.
6. Optionally re‑types the delimiter if expansion was triggered by a delimiter.

Installation / Usage
--------------------

1. Copy your canned responses into a `snippets.json` file located next to
   the executable.  The file must contain an array of objects with
   `keyword` and `text` fields, for example:

   ```json
   [
     { "keyword": ";hi", "text": "Hello!" },
     { "keyword": ";sig", "text": "Best regards,\nYour Name" }
   ]
   ```

2. Build the project on a Windows machine with Microsoft Visual C++.  Use
   the provided `build.bat` script (see below) or integrate into your own
   build system.  You can also compile this code via GitHub Actions on
   `windows-latest` to produce a `cmd.exe` suitable for distribution.

3. Run the resulting `cmd.exe`.  The program will read `snippets.json` and
   remain active in the background.  Type a keyword followed by a space,
   tab or enter to perform an automatic expansion.  Alternatively, type
   your keyword and press Ctrl + Space to trigger expansion manually.

4. To exit the program, terminate its process via Task Manager.

Building on Windows
-------------------

This repository contains a simple batch file that uses the Visual C++
toolchain to compile the program.  Open a **Developer Command Prompt for
VS** and run:

```
cd cmd_final
build.bat
```

This will produce the binary under the `publish` folder.  The batch script
uses `/O2` optimizations and links against the standard Windows libraries.

Repository Contents
-------------------

* `src/cmd.cpp` – Main application source.  Contains the keyword matching
  engine, keyboard hook, canonicalization functions and clipboard logic.
* `build.bat` – Batch file to compile the program using `cl.exe` (MSVC).
* `snippets.json` – Sample snippet file; populate this with your own
  shortcuts and replacement texts.
* `.github/workflows/build-windows-exe.yml` – GitHub Actions workflow used
  to build the executable on a Windows runner and publish an artifact.

Limitations
-----------

* Only runs on Windows because it relies on Windows APIs for keyboard hooks
  and clipboard operations.  There is no support for macOS or Linux.
* Requires compilation on a Windows environment or via GitHub Actions with
  `runs-on: windows-latest`.  Cross‑compiling from Linux is not supported.
* Automatic expansion is limited to delimiter characters and a single
  custom trigger.  Advanced features like context menus, GUI or tray icons
  would require additional code.

License
-------

This code is provided for educational purposes.  You may use, modify and
distribute it freely.