Title: Debugging and Environment Variables
Slug: debugging

# Debugging and Environment Variables

| Variable | Effect |
|---|---|
| `GLASS_RENDERER=full` / `fallback` | Forces the renderer (over [property@Glass.Context:renderer]) |
| `GLASS_DEBUG=hud` | Timings in a corner of each view: captures, cache hits, ms per frame |
| `GLASS_DEBUG=gpu-time` | GPU time per glass pass, in the debug log every 2 s |
| `GLASS_DEBUG=no-cache` | Redraws the glass every frame |
| `GLASS_DEBUG=no-supersample` | One sample per pixel (for comparison) |
| `GLASS_DEBUG=estimated-footprint` | The reference shader's lens footprint (for comparison) |
| `GLASS_DEBUG=window-capture` | Captures with the window's renderer instead of a private GL one |
| `G_MESSAGES_DEBUG=glass` | The library's debug messages (the GL version, shader logs) |

Several `GLASS_DEBUG` flags are separated by commas.

GTK's own tools help too: `GTK_DEBUG=interactive` opens the inspector
(the CSS nodes and classes above), and `GSK_RENDERER=gl` or `vulkan` switches
GTK's renderer — the glass works with both.

When glass does not appear:

- Is the panel an overlay child of a view (not in its content)?
  [method@Glass.View.get_active_renderer] and the `.glass-fallback` class
  tell whether CSS draws it.
- Does a container between the view and the panel have a background?
- Is the content transparent where the glass is? Set
  [property@Glass.View:backdrop-color].
