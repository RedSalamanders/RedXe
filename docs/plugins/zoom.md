# Zoom (`builtin.zoom`)

The Zoom actions open the Zoom web join page or a meeting link in your default browser. They do not install or
connect to Zoom Workplace, request a Zoom Marketplace application, store credentials, or call Zoom APIs. Zoom has no
dashboard tile and no settings: bind its actions to a Logicon key or Launcher shortcut, and they work. You do not need
a `services` entry.

A settings file from an earlier RedXe still carries a `services` entry for Zoom, such as
`"Zoom": { "plugin": "builtin.zoom" }`, and the one from RedXe 1.0.102 also carries the desktop-client settings
`clientId`, `redirectPort`, and `autoConnect` (and possibly `domain`, `displayName`, `mode`, or `labels`). It keeps
loading: RedXe ignores the whole entry and logs one warning each time it loads the file, so you can delete the entry
when convenient. A Logicon key bound to a removed desktop control, such as `zoom.mute` or `zoom.signIn`, shows a red
`!` and does nothing (a Launcher shortcut shows a warning tile); bind it to `zoom.open` or `zoom.join` instead.

| Action | Target | Effect |
| --- | --- | --- |
| `zoom.open` | none (a `target` is ignored) | Open `https://app.zoom.us/wc`, where you can enter a meeting ID. |
| `zoom.join` | A complete HTTPS meeting link such as `https://team.zoom.us/j/1234567890?pwd=...` | Open that link unchanged in the browser. |

`zoom.join` accepts only `zoom.us` and its subdomains, with a 9 to 11 digit meeting ID in an invite link
(`/j/<id>`) or in a browser-join link (`/wc/<id>/join` or `/wc/join/<id>`). A link with a user name, a port, or any
other site is refused, and so is a bare meeting ID. A key whose link does not fit shows a red `!` (a Launcher shortcut
shows a warning tile) and does nothing. Use the full link so its passcode stays intact. For a meeting ID and separate
passcode, use `zoom.open` and enter them in the browser.

Browser joining depends on the meeting host or corporate account enabling Zoom's **Join from your browser** link. An
invite link opens Zoom's launch page first; if it presents an app prompt, dismiss it and choose that browser link. To
skip that step, copy the address the browser link opens (it contains `/wc/` and ends in `/join`, followed by the
passcode) and bind that instead. RedXe cannot override an account policy that requires the desktop client or blocks
guest web access. The meeting's microphone, camera, share, chat, and other controls stay in the Zoom web app.

Example Logicon bindings:

```jsonc
"keys": [
  { "slot": 0, "action": "zoom.open", "label": "Zoom web", "icon": "Video" },
  { "slot": 1, "action": "zoom.join", "target": "https://team.zoom.us/j/1234567890?pwd=abc", "label": "Standup", "icon": "Video" },
  { "slot": 2, "action": "zoom.join", "target": "https://app.zoom.us/wc/1234567890/join?fromPWA=1&pwd=abc", "label": "Review", "icon": "Video" }
]
```
