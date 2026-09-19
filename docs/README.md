# RedXe user guide

RedXe is the native Windows dashboard for the [CORSAIR XENEON EDGE](https://www.corsair.com/newsroom/press-release/corsair-launches-the-xeneon-edge-14-5%E2%80%B3-lcd-touchscreen-a-dazzling-and-expansive-display-customized-by-you): pages of widgets on a 2560×720 landscape canvas.

This folder is the **user guide**. Product requirements live in `Specs/`; those contracts win if this guide and a spec disagree.

| Section | Contents |
| --- | --- |
| [Global usage](usage.md) | Window, dock, command line, pages, navigation, raise, settings file, prompts |
| [Widgets](plugins/README.md) | One page per bundled widget: screenshot path and parameters |
| [Actions](actions.md) | Everything a Logicon key, dialpad turn, or Launcher tile can do: `page.*`, `widget.*`, `redxe.*`, `system.*`, `keys.*`, `mouse.*`, `logicon.*`, `zoom.*` |

Edit the JSON settings file while RedXe is running; a valid save applies immediately. See [Global usage](usage.md#settings-file).
