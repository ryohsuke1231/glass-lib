# glass-lib

A GTK4 / libadwaita library that lets applications put refracting, Liquid Glass–style panels on top of their own content.

> [!NOTE]
> **Status: early development.** The library, the window widgets and a first version of the demo work; the API may still change. The design (in Japanese) is in [`docs/design.md`](docs/design.md).

> [!NOTE]
> This is an unofficial, community-driven project and is not affiliated with, endorsed by, or connected to Apple Inc. in any way.

## What it provides

- **`GlassView`**: a container whose content scrolls underneath floating glass.
- **`GlassPanel`**: a piece of glass that can carry icons, labels and flat buttons.
- Window widgets built on them, as drop-in counterparts of libadwaita's:
  **`GlassToolbarView`** (with a scroll edge effect), **`GlassHeaderBar`** (buttons on glass capsules),
  **`GlassSplitView`** (a floating glass sidebar), **`GlassToggleGroup`** and **`GlassButton`**.
- The same optics as the [Liquid Glass GNOME Shell extension](https://github.com/ryohsuke1231/liquid-glass): refraction, chromatic aberration, rim light, drop shadow and inner shading, tuned for small in-app glass.
- Automatic light/dark switching of the foreground, based on what is behind the glass.
- A CSS `backdrop-filter` fallback when OpenGL is unavailable.
- Bindings for GJS (JavaScript/TypeScript) and Python through GObject Introspection (namespace `Glass`).
- A demo application, **Glass Gallery**.

The library draws everything inside the application window, so it does not depend on the compositor.

## Requirements

- GTK ≥ 4.22, libadwaita ≥ 1.9 (GNOME 50 or later), libepoxy
- OpenGL ES 3.0 or OpenGL 3.3 (for the full renderer)

## Building

```bash
meson setup build
meson compile -C build
meson test -C build

# the demo (TypeScript, needs Node.js)
cd demo && npm install && npm run build && cd ..
meson devenv -C build -w . gjs -m demo/dist/main.js
```

## Using it (GJS)

```js
import Glass from 'gi://Glass?version=1';

Glass.init();
const view = new Glass.ToolbarView({ content: myScrolledWindow });
const header = new Glass.HeaderBar();
header.pack_start(new Gtk.Button({ icon_name: 'go-previous-symbolic' }));
view.add_top_bar(header);
window.set_content(view);
```

## License

MIT. See [LICENSE](LICENSE).
