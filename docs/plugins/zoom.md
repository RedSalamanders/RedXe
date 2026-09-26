# Zoom (`builtin.zoom`)

The Zoom service opens the Zoom web join page or a meeting invite in your default browser. It does not install or
connect to Zoom Workplace, request a Zoom Marketplace application, store credentials, or call Zoom APIs. It has no
dashboard tile of its own; bind its actions to a Logicon key or Launcher shortcut.

The shipped service entry needs no settings:

```jsonc
"services": {
  "Zoom": { "plugin": "builtin.zoom" }
}
```

| Action | Target | Effect |
| --- | --- | --- |
| `zoom.open` | none | Open `https://app.zoom.us/wc`, where you can enter a meeting ID. |
| `zoom.join` | Complete HTTPS invite URL such as `https://team.zoom.us/j/1234567890?pwd=...` | Open that invite unchanged in the browser. |

`zoom.join` accepts only `zoom.us` and its subdomains, with a 9 to 11 digit meeting ID in `/j/`. Use the full
invitation link so its passcode stays intact. For a meeting ID and separate passcode, use `zoom.open` and enter them
in the browser.

Browser joining depends on the meeting host or corporate account enabling Zoom's **Join from your browser** link.
If Zoom presents an app prompt, dismiss it and choose that browser link. RedXe cannot override an account policy that
requires the desktop client or blocks guest web access. The meeting's microphone, camera, share, chat, and other
controls stay in the Zoom web app.

Example Logicon bindings:

```jsonc
"keys": [
  { "slot": 0, "action": "zoom.open", "label": "Zoom web", "icon": "Video" },
  { "slot": 1, "action": "zoom.join", "target": "https://team.zoom.us/j/1234567890?pwd=abc", "label": "Standup", "icon": "Video" }
]
```
