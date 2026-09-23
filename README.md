# glass-lib

A GTK4 / libadwaita library that lets applications put refracting, Liquid Glass–style panels on top of their own content.

> [!NOTE]
> **Status: design phase.** Nothing is implemented yet. The design (in Japanese) is in [`docs/design.md`](docs/design.md).

> [!NOTE]
> This is an unofficial, community-driven project and is not affiliated with, endorsed by, or connected to Apple Inc. in any way.

## What it will provide (v1)

- **`GlassView`**: a container whose content scrolls underneath floating glass.
- **`GlassPanel`**: a piece of glass that can carry icons, labels and flat buttons.
- The same optics as the [Liquid Glass GNOME Shell extension](https://github.com/ryohsuke1231/liquid-glass): refraction, chromatic aberration, rim light, drop shadow and inner shading.
- Automatic light/dark switching of the foreground, based on what is behind the glass.
- A CSS `backdrop-filter` fallback when OpenGL is unavailable.
- Bindings for GJS (JavaScript/TypeScript) and Python through GObject Introspection (namespace `Glass`).
- A demo application, **Glass Gallery**.

The library draws everything inside the application window, so it does not depend on the compositor.

## Requirements (planned)

- GTK ≥ 4.22, libadwaita ≥ 1.9 (GNOME 50 or later)
- OpenGL ES 3.0 or OpenGL 3.3 (for the full renderer)

## License

MIT. See [LICENSE](LICENSE).
