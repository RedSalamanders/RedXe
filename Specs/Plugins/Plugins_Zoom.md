# Zoom web actions

Status: normative. Owner: `Plugins/Actions/Zoom` (`zoom.action.dll`), the retired `builtin.zoom` services entry,
`Tests/ZoomTests`, and the Zoom rows of [`Plugins_Actions.md`](Plugins_Actions.md). User guidance is
[`docs/plugins/zoom.md`](../../docs/plugins/zoom.md).

`zoom.action.dll` is a **dedicated action DLL** (`Plugins_Actions.md` "Publication ABI") for plugin id
`builtin.zoom`. It publishes the `zoom` namespace and nothing else: no service, widget, settings contract, device
worker, network worker, OAuth listener, credential store, Zoom Plugin SDK, or dependency on an installed Zoom
Workplace client. It delegates browser opening to the host's `system.launch` action, which already enforces the
automated-host device-access policy. Execution runs on the UI thread, and no background work or persistent connection
exists between actions.

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
(`https://evil.example#.zoom.us/...`). Its path MUST be `/j/<id>` (an invite), or `/wc/join/<id>` or `/wc/<id>/join`
(the web-client links that Zoom's **Join from your browser** opens, which skip the desktop-app prompt), where `<id>` is
9 to 11 digits, followed by the end, `?`, or `#`. The whole link MUST be printable ASCII without `"`, `<`, `>`, or `\`,
and at most 512 bytes. The query and fragment, including an opaque `pwd` token, pass unchanged. A separate meeting ID
and passcode are entered at the web join page; the plugin MUST NOT manufacture a `pwd` token from a plaintext
passcode, and the `<id>[:<passcode>]` form of `v1.0.102` is not a Meeting target. A `zoom.join` binding whose target
fails the grammar is an invalid binding: drawn as Logicon's red `!` face or Launcher's `Warning` tile and never
dispatched.

Both actions carry `RedXeActionFlagDeferred`: the host drains the queued `system.launch` request after `Execute`
returns. `Execute` therefore returns `S_FALSE` when the request is queued or coalesced with an identical pending one,
never `S_OK`, and passes a `RequestAction` failure through (`ERROR_BUSY` for a full ring, `E_UNEXPECTED` after
shutdown). An unpublished name, or a `zoom.join` target that fails the grammar, returns `E_INVALIDARG` without
requesting a host action.

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
(`ZoomSettings.cpp`, compiled into the host): the entry MAY carry the seven retired members `clientId`,
`redirectPort`, `domain`, `displayName`, `autoConnect`, `mode`, and `labels` with any JSON value, and any other
member, including a retired name in another case, or a second `builtin.zoom` entry MUST reject the document. A valid
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
execution performs bounded link validation and copies no meeting link; the host action ring owns its own bounded copy.
The obsolete SDK import, OAuth, synthetic session, local MSAA path, and their binaries are absent from this product.

`ZoomTests` loads the shipped DLL with a fake host and proves the actions-only metadata, the absent settings contract,
the two-action contract with its Meeting and None kinds and Deferred flags, that `IRedXeService` is refused and the
pack takes no settings, `S_FALSE` for a queued or coalesced launch and a passed-through ring failure, `zoom.open`
ignoring an authored target, unchanged invite and web-client link forwarding, and refusal without a host request. It
proves `ParseMeeting` against accepted links (`/j/`, both `/wc/` forms, an uppercase host, an 11-digit id, a fragment,
the 512-byte bound) and rejected ones (`http`, an uppercase scheme, other hosts, `@`, `:`, `#`, `?`, and `\` in the
authority, a port, an empty label, short, long, and non-digit ids, other paths, a trailing space or control, a quote,
the bare `<id>:<passcode>` form), and that the retired entry's model accepts the retired members with any value and
rejects any other member or a non-object. `SettingsTests` proves the two shipped templates carry no retired entry,
loads the exact `v1.0.102` Release and Debug templates (retired entry, retired members, and removed-verb bindings
included) without a fallback or backup, records the retired entry with and without its members, rejects any other
member and a duplicate entry, and checks the schema's deprecated variant and properties. `HostPluginTests` proves the
publisher is registered, the removed verbs are unknown, `zoom.open` ignores a target, `zoom.join` validates the
accepted links and refuses malformed and spoofed ones, and both actions execute through the dedicated executor without
any service and drain as `system.launch`. `BuildProcessTests` keeps stale SDK binaries out of packages.
