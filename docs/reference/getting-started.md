Title: Getting Started
Slug: getting-started

# Getting Started

glass-lib draws refracting glass — in the style of Apple's Liquid Glass — over
a GTK 4 / libadwaita application's own content. The glass bends, blurs and
tints what is under it, casts a soft shadow, and the text on it turns dark or
light with what it is over. Everything is drawn inside the window, in the same
frame as the content, so it works on any compositor, Wayland or X11.

It needs GTK ≥ 4.22 and libadwaita ≥ 1.9 (GNOME 50), and OpenGL ES 3.0 or
OpenGL 3.3 for the full renderer; without OpenGL the glass is frosted with CSS.

## Building

```sh
meson setup build
meson compile -C build
meson test -C build
meson install -C build        # libglass-lib-1.so, glass-lib-1.pc, Glass-1.typelib
```

## The first window

Call [func@Glass.init] once, after GTK and libadwaita are initialized (in
`GApplication::activate` or `startup`). Then use the glass counterparts of
libadwaita's widgets: [class@Glass.ToolbarView] for `AdwToolbarView`,
[class@Glass.HeaderBar] for `AdwHeaderBar`. The content runs under the bars,
which float on glass.

In GJS (JavaScript or TypeScript):

```js
import Adw from 'gi://Adw?version=1';
import Gtk from 'gi://Gtk?version=4.0';
import Glass from 'gi://Glass?version=1';

const app = new Adw.Application({ application_id: 'org.example.Hello' });
app.connect('activate', () => {
    Glass.init();

    const list = new Gtk.ListBox();          // anything that scrolls
    const scrolled = new Gtk.ScrolledWindow({ child: list });

    const toolbar = new Glass.ToolbarView({ content: scrolled });
    const header = new Glass.HeaderBar();
    header.pack_end(new Gtk.Button({ icon_name: 'open-menu-symbolic' }));
    toolbar.add_top_bar(header);

    // The content starts under the bar: pad it by the bar's height.
    toolbar.bind_property('top-bar-height', list, 'margin-top', 2 /* SYNC_CREATE */);

    new Adw.ApplicationWindow({ application: app, content: toolbar }).present();
});
app.run([]);
```

In Python:

```python
import gi
gi.require_version('Adw', '1')
gi.require_version('Glass', '1')
from gi.repository import Adw, Glass, Gtk

def activate(app):
    Glass.init()
    scrolled = Gtk.ScrolledWindow(child=Gtk.ListBox())
    toolbar = Glass.ToolbarView(content=scrolled)
    toolbar.add_top_bar(Glass.HeaderBar())
    Adw.ApplicationWindow(application=app, content=toolbar).present()

app = Adw.Application(application_id='org.example.Hello')
app.connect('activate', activate)
app.run()
```

In C, with `pkg-config --cflags --libs glass-lib-1`:

```c
#include <adwaita.h>
#include <glass.h>

static void
activate (GtkApplication *app)
{
  GtkWidget *window, *toolbar;

  glass_init ();
  toolbar = glass_toolbar_view_new ();
  glass_toolbar_view_set_content (GLASS_TOOLBAR_VIEW (toolbar),
                                  gtk_scrolled_window_new ());
  glass_toolbar_view_add_top_bar (GLASS_TOOLBAR_VIEW (toolbar), glass_header_bar_new ());

  window = adw_application_window_new (app);
  adw_application_window_set_content (ADW_APPLICATION_WINDOW (window), toolbar);
  gtk_window_present (GTK_WINDOW (window));
}
```

Complete, runnable versions of these are in the repository's `examples/`.

## Glass anywhere over the content

The general form is a [class@Glass.View]: its `content` fills it, and its
overlay children are placed over the content like in `GtkOverlay` (by their
`halign`, `valign` and margins). Every [class@Glass.Panel] among the overlay
children, at any depth, is drawn as glass over the content:

```js
const view = new Glass.View({ content: scrolled });
const bar = new Gtk.Box({ spacing: 4 });
for (const icon of ['go-previous-symbolic', 'starred-symbolic', 'user-trash-symbolic'])
    bar.append(new Gtk.Button({ icon_name: icon }));
view.add_overlay(new Glass.Panel({
    child: bar, halign: Gtk.Align.CENTER, valign: Gtk.Align.END, margin_bottom: 16,
}));
```

A panel inside the content (not an overlay) is frosted with CSS instead, as is
one outside any view: glass belongs over the content, not in it.

## The parts

| glass-lib | libadwaita / GTK | What it is |
|---|---|---|
| [class@Glass.View] | `GtkOverlay` | Glass over its content |
| [class@Glass.Panel] | — | A pane of glass with one child |
| [class@Glass.ToolbarView] | `AdwToolbarView` | Bars floating over the content, with a scroll edge effect |
| [class@Glass.HeaderBar] | `AdwHeaderBar` | Buttons on glass capsules, window controls |
| [class@Glass.SplitView] | `AdwOverlaySplitView` | A sidebar of thick glass floating in the window |
| [class@Glass.ToggleGroup] | `AdwToggleGroup` | A segmented control whose plate slides |
| [class@Glass.TabBar] | `AdwViewSwitcherBar` | A tab bar over an `AdwViewStack` |
| [class@Glass.Button] | `GtkButton` | A button that is glass |
| [class@Glass.ButtonGroup] | `.linked` buttons | Buttons on one capsule |
| [class@Glass.Group] | — | Panels whose glass flows together |
| [class@Glass.Switch], [class@Glass.Slider] | `GtkSwitch`, `GtkScale` | Knobs that turn to lenses while held |
| [class@Glass.SearchEntry] | `GtkSearchEntry` | A search field on glass |
| [class@Glass.MenuButton], [class@Glass.Popover] | `GtkMenuButton`, `GtkPopoverMenu` | Menus of glass |
| [class@Glass.Dialog] | `AdwDialog` | A dialog of glass |

Next: [How the glass works](concepts.html), [Tuning the glass](tuning.html).
