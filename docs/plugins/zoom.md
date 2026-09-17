# Zoom (`builtin.zoom`)

A headless service that drives the **Zoom Workplace** client on this PC through the Zoom Plugin SDK: join, leave,
mute, video, share, record, reactions, and chat become [actions](../actions.md#zoom--the-zoom-service) you bind to
a Logicon key or a Launcher tile. It has no tile of its own.

## What you need

1. Zoom Workplace **7.0.2 or later** installed and running on the same PC.
2. A Zoom App Marketplace **General app** with the **Plugin SDK** enabled, whose OAuth redirect URL is
   `http://127.0.0.1:48123/redirect` (or the `redirectPort` you configure). Its client id goes into the settings
   below; its secret is never entered anywhere.
3. A RedXe build with the Zoom Plugin SDK imported (see `ThirdParty/ZoomPluginSdk/README.md`). Without it the service
   logs `zoom-sdk-unavailable` once and every `zoom.*` action is refused.

## Settings

```jsonc
"services": {
  "Zoom": {
    "plugin": "builtin.zoom",
    "clientId": "your-marketplace-client-id",
    "redirectPort": 48123,
    "domain": "zoom.us",
    "displayName": "",
    "autoConnect": false
  }
}
```

| Key | Type | Values | Default | Meaning |
| --- | --- | --- | --- | --- |
| `clientId` | string | 1–128 bytes | required | The Marketplace app's client id |
| `redirectPort` | integer | 1024–65535 | `48123` | Loopback port of the redirect URL registered in the Marketplace app |
| `domain` | string | 1–128 bytes | `zoom.us` | Zoom domain for sign-in |
| `displayName` | string | ≤ 128 bytes | `""` | Name used when joining; empty uses your Zoom profile name |
| `autoConnect` | boolean | | `false` | `true` connects to the Zoom client as soon as RedXe starts; `false` connects at the first `zoom.*` action |

## Signing in

Bind `zoom.signIn` to a key. Pressing it opens your browser at Zoom's sign-in page; after you approve, Zoom sends the
browser back to RedXe (`http://127.0.0.1:<port>/redirect`) and RedXe stores the sign-in in Windows Credential Manager
(`RedXe/Zoom/<clientId>`) for your Windows account. Nothing is written to the settings file. `zoom.signOut` (`"now"`)
forgets it. If the sign-in was revoked, the log shows `zoom-auth-failed` and a new `zoom.signIn` is needed.

## Example bindings

```jsonc
"keys": [
  { "slot": 0, "action": "zoom.mute",      "target": "toggle", "label": "Mic",   "icon": "Microphone" },
  { "slot": 1, "action": "zoom.video",     "target": "toggle", "label": "Cam",   "icon": "Camera" },
  { "slot": 2, "action": "zoom.raiseHand", "target": "toggle", "label": "Hand",  "icon": "Emoji" },
  { "slot": 3, "action": "zoom.join",      "target": "https://zoom.us/j/1234567890?pwd=abc", "label": "Standup", "icon": "Video" },
  { "slot": 8, "action": "zoom.leave",     "target": "now",    "label": "Leave", "icon": "Cancel", "color": "#7A1E1E" }
]
```

## Behavior

- `on` / `off` variants use the state Zoom reports, so a press never guesses; `toggle` flips it.
- Host-only actions (`zoom.end`, `zoom.participants.*`) are refused unless you are the host.
- An action pressed while the service is still connecting waits up to 15 seconds for the connection, then gives up.
- If the Zoom client restarts, RedXe reconnects after 1, 2, 4, and 8 seconds, then at the next action.

## Diagnostics (JSONL log)

`zoom-sdk-unavailable`, `zoom-signed-out`, `zoom-sign-in-started`, `zoom-sign-in-complete`, `zoom-sign-in-failed`,
`zoom-listener-failed` (the redirect port is in use — change `redirectPort`), `zoom-auth-failed`, `zoom-connected`,
`zoom-disconnected`, `zoom-action-failed`.
