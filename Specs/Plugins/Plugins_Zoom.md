# Zoom web actions

Status: normative. Owner: `Plugins/Actions/Zoom` (`zoom.action.dll`), the `builtin.zoom` service settings entry,
`Tests/ZoomTests`, and the Zoom rows of [`Plugins_Actions.md`](Plugins_Actions.md). User guidance is
[`docs/plugins/zoom.md`](../../docs/plugins/zoom.md).

`builtin.zoom` is a headless service with `IRedXeService` and `IRedXeActionPack` on one COM identity. It publishes
the `zoom` namespace. The service has **no device worker**, network worker, OAuth listener, credential store, Zoom
Plugin SDK, or dependency on an installed Zoom Workplace client. It delegates browser opening to the host's
`system.launch` action, which already enforces the automated-host device-access policy. Start, settings apply,
action execution, host-state delivery, and Stop run on the UI thread. No background work or persistent connection
exists between actions.

## Settings and publication

The flattened service entry is `{ "plugin": "builtin.zoom" }`. Its private settings contract is a closed empty
object (`{}` defaults); legacy `clientId`, `redirectPort`, `domain`, `displayName`, `autoConnect`, `mode`, and `labels`
are rejected. No secret or token is read or stored. The DLL advertises `Service | Actions` and publishes exactly:

| Action | Target | Result |
| --- | --- | --- |
| `zoom.open` | None | Enqueue `system.launch` for `https://app.zoom.us/wc`. |
| `zoom.join` | Text containing a complete meeting invite URL | Enqueue `system.launch` for the URL unchanged. |

Both actions are deferred because the host drains the `system.launch` request after the pack returns. `zoom.join`
validates the URL again in the pack: HTTPS only, exact `zoom.us` or a subdomain, path `/j/` followed by 9–11 digits,
and no controls, spaces, backslashes, credentials, or port in the authority. It preserves the query, including an
opaque `pwd` token. A separate meeting ID and passcode are entered at the web join page; the plugin MUST NOT
manufacture a `pwd` token from a plaintext passcode. An invalid URL returns `E_INVALIDARG` without requesting a
host action. A stopped service returns `E_NOT_VALID_STATE`.

The namespace does not publish desktop or SDK controls (`zoom.signIn`, `zoom.signOut`, `zoom.start`, `zoom.leave`,
`zoom.end`, `zoom.audio`, `zoom.mute`, `zoom.video`, `zoom.share`, `zoom.record`, `zoom.raiseHand`, `zoom.reaction`,
`zoom.chat.send`, `zoom.captions`, `zoom.participants.*`, `zoom.focus`). Existing bindings to those names fail
settings validation as unknown actions; migration removes them from both shipped templates.

## Browser boundary

Opening a link requires no Zoom Marketplace application registration and no desktop installation. A Zoom meeting
host or corporate administrator may disable the browser join option or require authentication. RedXe neither
bypasses that policy nor claims that opening a link joined the meeting. Meeting controls belong to the web app.
Zoom's official browser-join help describes the host settings:
<https://support.zoom.com/hc/en/article?id=zm_kb&sysparm_article=KB0059553>.

## Resource and validation contract

The service allocates no queue, timer, worker, socket, or GPU object. Action execution performs bounded URL
validation and copies no meeting URL; the host action ring owns its own bounded copy. The obsolete SDK import,
OAuth, synthetic session, local MSAA path, and their binaries are absent from this product.

`ZoomTests` loads the shipped DLL with a fake host and proves the two-action contract, empty settings, URL allowlist,
unchanged invite forwarding, refusal without a host request, and stopped-service behavior. `SettingsTests` proves
the two shipped templates and rejects legacy private members. `HostPluginTests` proves the publisher is registered
and the removed verbs are unknown. `BuildProcessTests` keeps stale SDK binaries out of packages.
