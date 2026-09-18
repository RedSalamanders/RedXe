# Zoom (`builtin.zoom`)

A headless service that drives the **Zoom Workplace** client on this PC: join, leave, mute, video, share, record,
reactions, and chat become [actions](../actions.md#zoom--the-zoom-service) you bind to a Logicon key or a Launcher
tile. It has no tile of its own. It reaches Zoom two ways: through the **Zoom Plugin SDK** when your Zoom account
lets you authorize the RedXe app (full control and true state), and otherwise **locally**, by pressing the buttons
of the meeting toolbar itself through the accessibility interface Zoom exposes for screen readers — the path that
works on a managed (corporate) account that does not allow Marketplace apps. `mode` picks; `auto` (the default)
uses the SDK when it can and the local path when it cannot.

## What you need

1. Zoom Workplace **7.0.2 or later** installed and running on the same PC.
2. The shipped settings already name the RedXe Zoom App Marketplace app (a public client id; no secret is ever
   entered anywhere). To use your own Marketplace **General app** instead, enable the **Plugin SDK** on it, register
   the OAuth redirect URL `http://127.0.0.1:48123/redirect` (or the `redirectPort` you configure), and put its client
   id in the settings below.
3. A RedXe build with the Zoom Plugin SDK imported (see `ThirdParty/ZoomPluginSdk/README.md`). Without it the service
   logs `zoom-sdk-unavailable` once and every `zoom.*` action is refused.

## Settings

```jsonc
"services": {
  "Zoom": {
    "plugin": "builtin.zoom",
    "clientId": "sHVWQENoR4qrpuBPgsFsPw",
    "redirectPort": 48123,
    "domain": "zoom.us",
    "displayName": "",
    "autoConnect": false
  }
}
```

| Key | Type | Values | Default | Meaning |
| --- | --- | --- | --- | --- |
| `clientId` | string | 1–128 bytes | the RedXe app | The Marketplace app's public client id |
| `redirectPort` | integer | 1024–65535 | `48123` | Loopback port of the redirect URL registered in the Marketplace app |
| `domain` | string | 1–128 bytes | `zoom.us` | Zoom domain for sign-in |
| `displayName` | string | ≤ 128 bytes | `""` | Name used when joining; empty uses your Zoom profile name |
| `autoConnect` | boolean | | `false` | `true` connects to the Zoom client as soon as RedXe starts; `false` connects at the first `zoom.*` action |
| `mode` | string | `auto`, `sdk`, `local` | `auto` | `auto`: the SDK when signed in and allowed, else the local path. `sdk`: never fall back. `local`: never use the SDK (no `clientId` needed) |
| `labels` | object | see below | English names | The meeting toolbar button names RedXe reads and presses; set them when your Zoom client is not in English |

## Signing in

Bind `zoom.signIn` to a key. Pressing it opens your browser at Zoom's sign-in page; after you approve, Zoom sends the
browser back to RedXe (`http://127.0.0.1:<port>/redirect`) and RedXe stores the sign-in in Windows Credential Manager
(`RedXe/Zoom/<clientId>`) for your Windows account. Nothing is written to the settings file. `zoom.signOut` (`"now"`)
forgets it. If the sign-in was revoked, the log shows `zoom-auth-failed` and a new `zoom.signIn` is needed.

## Without a Marketplace app (corporate accounts)

On a managed Zoom account the admin decides which Marketplace apps a user may authorize; when the RedXe app is not
allowed, the SDK cannot connect (`zoom-auth-failed`, or the consent page refuses). RedXe then drives the Zoom
client the way a screen reader does: it reads the meeting toolbar through Windows' accessibility interface and
presses its buttons. No keystroke, no window switching, nothing to authorize. Set `"mode": "local"` to skip the
SDK entirely, or leave `auto` and it falls back by itself.

| Action | What RedXe presses | Notes |
| --- | --- | --- |
| `zoom.mute` | the Mute/Unmute button | `on` / `off` read the state first and press nothing when already there |
| `zoom.video` | the Start/Stop Video button | same |
| `zoom.raiseHand` | the Raise/Lower Hand button | same |
| `zoom.share` (`monitor`, `app`) | the Share button | opens Zoom's share picker; the monitor or window choice is yours there |
| `zoom.record` (`local.start`, `cloud.start`) | the Record button | |
| `zoom.leave` | the Leave button, or End for a host | Zoom then asks whether to end or leave |
| `zoom.join` | opens `zoommtg://zoom.us/join?confno=…` | the Zoom client joins; `zoom.start` needs your PMI as the target |
| `zoom.focus` | — | brings the meeting (or client) window to the front; works in every mode |

Not available this way: `zoom.audio`, `zoom.reaction`, `zoom.chat.send`, `zoom.captions`, `zoom.end`,
`zoom.participants.*`, share stop/pause/resume, record stop/pause/resume — the key logs an error and does nothing.

How the state is known: Zoom names its toolbar buttons with their state (`Unmute, currently muted, …` /
`Mute, currently unmuted, …`, `Start my video` / `Stop my video`, `Raise hand` / `Lower hand`). RedXe matches those
names, so with another Zoom language put the words your client uses under `labels` (`muted`, `unmuted`, `videoOn`,
`videoOff`, `handRaised`, `handLowered`, `share`, `record`, `leave`, `end`); a name that cannot be found makes
`on`/`off` do nothing (`zoom-state-unknown` in the log) while `toggle` keeps working.

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
`zoom-disconnected`, `zoom-local-mode` (why the local path is in use), `zoom-state-unknown` (the toolbar names
did not match `labels`), `zoom-action-failed`.
