# Done: raised widget overlay

Status: `COMPLETE`
Created: 2026-09-02
Completed: 2026-09-02
Owner: dashboard overlay chrome, plugin raised extent ABI, and host input

## Goal

Let a user double-click or double-tap a widget that is not already full-client so it raises into a host overlay with
a drop shadow, plugin-chosen 1/4, 1/3, 1/2, or 1/1 size, and a top-right close control that returns to standard
layout.

Owning contracts: [`../../Plugins/Plugins_API.md`](../../Plugins/Plugins_API.md),
[`../../UI/UI_Dashboard.md`](../../UI/UI_Dashboard.md), and
[`../../Core/Core_PerformanceAndResources.md`](../../Core/Core_PerformanceAndResources.md).

## Selected product

- New sibling `IRedXeRaisedWidget` (`GetRaisedExtent`, `SetRaised`). The host asks; it does not guess.
- Overlay HWND exists only while raised. GDI paints dim, chrome, close, and shadow. The window region punches a hole
  over plugin content.
- GPU widgets render only the raised instance at the content viewport. Window widgets move their container into that
  rectangle.
- Escape, the close control, resize, DPI change, settings reload, and shutdown dismiss. Dim taps do not. Page swipe is
  blocked while raised.

Durable requirements live in the owning domain specs. This file is historical sequencing only.
