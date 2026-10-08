# Zoom web actions

Status: normative. Owner: `Plugins/Actions/Zoom` (`zoom.action.dll`), the retired `builtin.zoom` services entry,
`Tests/ZoomTests`, and the Zoom rows of [`Plugins_Actions.md`](Plugins_Actions.md). User guidance is
[`docs/plugins/zoom.md`](../../docs/plugins/zoom.md).

`zoom.action.dll` is a **dedicated action DLL** (`Plugins_Actions.md` "Publication ABI") for plugin id
`builtin.zoom`. It publishes the `zoom` namespace and nothing else: no service, widget, settings contract, device
worker, network worker, OAuth listener, credential store, Zoom Plugin SDK, or dependency on an installed Zoom
Workplace client. It delegates browser opening to the host's `system.launch` action, which already enforces the
automated-host device-access policy and performs the shell call on the host's launch worker, never the UI thread.
`Execute` runs on the UI thread, and the DLL owns no background work or persistent connection between actions.

## Publication

The DLL advertises only `RedXePluginCapabilityActions` and exports `RedXeCreate`, `RedXeEnumeratePlugins`, and
`RedXeGetActionContract`; it exports no settings contract. `RedXeCreate` answers only `IID_IRedXeActionPack`, with
the empty `{"plugin":{},"instance":{}}` envelope the host passes (any other envelope is `ERROR_INVALID_DATA`, any
other interface `E_NOINTERFACE`). The host maps the DLL when a binding in the `zoom` namespace is first validated,
creates the one executor on the first `zoom.*` execution, and releases it at process teardown, so `zoom.*` bindings
work in every document without a `services` entry. The DLL publishes exactly:

| Action | Target | Result |
| --- | --- | --- |
| `zoom.open` | None (an authored target is ignored) | Queue `system.launch` for `https://app.zoom.us/wc`. |
| `zoom.join` | Meeting: a complete HTTPS Zoom link | Queue `system.launch` for the link unchanged. |

The Meeting grammar is `RedXeActions::ParseMeeting` (`Common/Actions`), which host binding validation and the pack
share, so validation and execution agree by construction. A link MUST start with `https://`; its authority MUST be
exactly `zoom.us` or a subdomain (ASCII case-insensitive) and MUST NOT contain `@`, `:`, `#`, or `?`, so a link
carries no credentials or port and cannot name another host that a browser would open
(`https://evil.example#.zoom.us/...`). The authority MUST also be a DNS host name, checked before the `zoom.us`
suffix: dot-separated labels of 1 to 63 ASCII letters, digits, and hyphens, none starting or ending with a hyphen, at
most 253 characters in all. So an empty label (`a..zoom.us`, a leading or trailing dot), `_` or another character
(`-team.zoom.us`, `te_am.zoom.us`), and percent-encoding, which a browser decodes first (`a%2e.zoom.us` is
`a..zoom.us`), are refused; an internationalized subdomain passes in its `xn--` form. Its path MUST be `/j/<id>` (an
invite), or `/wc/join/<id>` or `/wc/<id>/join` (the web-client links that Zoom's **Join from your browser** opens, which
skip the desktop-app prompt), where `<id>` is 9 to 11 digits, followed by the end, `?`, or `#`. The whole link MUST be
printable ASCII without `"`, `<`, `>`, or `\`, and at most 512 bytes. The query and fragment, including an opaque `pwd`
token, pass unchanged. A separate meeting ID and passcode are entered at the web join page; the plugin MUST NOT
manufacture a `pwd` token from a plaintext passcode, and the `<id>[:<passcode>]` form of `v1.0.102` is not a Meeting
target. A `zoom.join` binding whose target fails the grammar is an invalid binding: drawn as Logicon's red `!` face or
Launcher's `Warning` tile and never dispatched.

Both actions carry `RedXeActionFlagDeferred`: the host drains the queued `system.launch` request after `Execute`
returns and opens the browser on its launch worker ([`Plugins_Actions.md`](Plugins_Actions.md) "Launch worker").
`Execute` therefore returns `S_FALSE` once `RequestAction` accepts the request, whether queued or coalesced with an
identical pending one, never `S_OK`, and passes a `RequestAction` failure through (`ERROR_BUSY` for a full ring,
`E_UNEXPECTED` after shutdown). A launch worker that is full at drain time (`action-failed`, a Debug record that Release
drops) and a browser that fails to open (`launch-failed`, a Warning) both happen after the pack has returned, so they
are never this action's result. An unpublished name, or a `zoom.join` target that fails the grammar, returns `E_INVALIDARG` without requesting
a host action.

The namespace does not publish desktop or SDK controls (`zoom.signIn`, `zoom.signOut`, `zoom.start`, `zoom.leave`,
`zoom.end`, `zoom.audio`, `zoom.mute`, `zoom.video`, `zoom.share`, `zoom.record`, `zoom.raiseHand`, `zoom.reaction`,
`zoom.chat.send`, `zoom.captions`, `zoom.participants.*`, `zoom.focus`), and neither shipped template binds them.
Because document validation does not check a published namespace's verbs (`Plugins_Actions.md`), an existing binding
to one of those names still loads; binding validation reports it as an unknown verb (`ERROR_NOT_FOUND`), so it is an
invalid binding as well.

## Retired services entry

Earlier releases started `builtin.zoom` as a headless service from a `services` entry, and the `v1.0.102` templates
wrote one with Zoom SDK members. Such a file MUST still load (`Core_Settings.md` "Version 5 document" and
"Services"). `builtin.zoom` is listed in `kRedXeRetiredServices` (`RedXe/BundledPlugins.h`), not in
`kRedXeBundledServices`. The host validates a `services` entry naming it with `Zoom::ParseSettings`
(`ZoomSettings.cpp`, compiled into the host) on the members as authored, never on a defaults merge, which would drop
a `null` member: the entry MAY carry the seven retired members `clientId`, `redirectPort`, `domain`, `displayName`,
`autoConnect`, `mode`, and `labels` with any JSON value, `null` included, and any other member, whatever its value
and including a retired name in another case, or a second `builtin.zoom` entry MUST reject the document. A valid
entry is recorded in `AppSettings::retiredServices` and otherwise ignored: the host never creates or starts anything
for it and passes nothing from it to the DLL, MUST NOT rewrite the file for it, and logs one Warning record
`service-retired-settings-ignored` from `builtin.zoom` per load or live apply of a document that carries it. The
schema marks the entry and its members deprecated. Neither shipped template carries it. No secret or token is read or
stored.

## Browser boundary

Opening a link requires no Zoom Marketplace application registration and no desktop installation. A Zoom meeting
host or corporate administrator may disable the browser join option or require authentication. RedXe neither
bypasses that policy nor claims that opening a link joined the meeting. Meeting controls belong to the web app.
Zoom's official browser-join help describes the host settings:
<https://support.zoom.com/hc/en/article?id=zm_kb&sysparm_article=KB0059553>.

## Resource and validation contract

The executor allocates no queue, timer, worker, socket, or GPU object and owns nothing between executions. Action
execution performs bounded link validation and copies no meeting link; the host action ring, and then a launch worker
slot, own their own bounded copies. The obsolete SDK import, OAuth, synthetic session, local MSAA path, and their binaries are absent from this product.

`ZoomTests` loads the shipped DLL with a fake host and proves the actions-only metadata, the absent settings contract,
the two-action contract with its Meeting and None kinds and Deferred flags, that `IRedXeService` is refused and the
pack takes no settings, `S_FALSE` for a queued or coalesced launch and a passed-through ring failure, `zoom.open`
ignoring an authored target, unchanged invite and web-client link forwarding, and refusal without a host request. It
proves `ParseMeeting` against accepted links (`/j/`, both `/wc/` forms, an uppercase host, an 11-digit id, a fragment,
the 512-byte bound, a 63-character label, a 253-character host) and rejected ones (`http`, an uppercase scheme, other
hosts, `@`, `:`, `#`, `?`, `%`, and `\` in the authority, a port, an empty leading, inner, or trailing label, a label
with `_` or a hyphen at either end, a 64-character label, a 254-character host, short, long, and non-digit ids, other
paths, a trailing space or control, a quote, the bare `<id>:<passcode>` form), and that the retired entry's model
accepts the retired members with any value and rejects any other member, also set to `null`, or a non-object.
`SettingsTests` proves the two shipped templates carry no retired entry, loads the exact `v1.0.102` Release and Debug
templates (retired entry, retired members, and removed-verb bindings included) without a fallback or backup, records the
retired entry with and without its members and with a sole retired member set to `null`, rejects any other member (also
set to `null`) and a duplicate entry, and checks the schema's deprecated variant and properties. `HostPluginTests`
proves the publisher is registered, the removed verbs are unknown, `zoom.open` ignores a target, `zoom.join` validates
the accepted links and refuses malformed, spoofed, and empty-label ones, both actions execute through the dedicated
executor without any service and drain as `system.launch`, and the retired entry warns once per load, also when its only
retired member is `null`. `BuildProcessTests` keeps stale SDK binaries out of packages.
