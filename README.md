# cmd Beeftext Logic Package

This package uses the original Beeftext portable runtime supplied in `cmd.zip`, renamed to `cmd.exe`, with the user's `Beeftext.btbackup` preloaded as `comboList.json` in the portable data locations.

## Run locally

Open:

```text
app\cmd.exe
```

Keep the program running while typing.

## GitHub Actions

Upload the repository contents, then run the workflow. The artifact will be:

```text
cmd_beeftext_logic_release
```

Inside it, run:

```text
cmd.exe
```

## Notes

- The app uses Beeftext's real expansion engine, not the experimental custom hook engine.
- Your original backup is included as `Beeftext.btbackup`.
- If the combo list is not loaded automatically on a specific machine, import `Beeftext.btbackup` from the app's Import option.
