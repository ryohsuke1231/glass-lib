// hello-glass.js — a complete glass-lib app in GJS: a header bar and a row of
// buttons floating on glass over content that scrolls under them.
//
//   meson devenv -C build -w . gjs -m examples/hello-glass.js
//
// (or plain `gjs -m hello-glass.js` with glass-lib installed)
//
// SPDX-License-Identifier: MIT

import Adw from 'gi://Adw?version=1';
import Gdk from 'gi://Gdk?version=4.0';
import GObject from 'gi://GObject';
import Gtk from 'gi://Gtk?version=4.0';
import Glass from 'gi://Glass?version=1';

// Something colourful to scroll under the glass.
function tiles() {
    let css = '.tile { border-radius: 14px; min-height: 110px; }\n';
    for (let i = 0; i < 12; i++)
        css += `.tile-${i} { background: hsl(${i * 30} 75% 58%); }\n`;
    const provider = new Gtk.CssProvider();
    provider.load_from_string(css);
    Gtk.StyleContext.add_provider_for_display(Gdk.Display.get_default(), provider,
        Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION);

    const grid = new Gtk.FlowBox({ selection_mode: Gtk.SelectionMode.NONE, homogeneous: true,
        min_children_per_line: 3, max_children_per_line: 6, row_spacing: 10, column_spacing: 10,
        valign: Gtk.Align.START, margin_start: 12, margin_end: 12, margin_bottom: 96 });
    for (let i = 0; i < 60; i++)
        grid.append(new Gtk.Box({ css_classes: ['tile', `tile-${(i * 5) % 12}`] }));
    return grid;
}

const app = new Adw.Application({ application_id: 'org.example.HelloGlass' });

app.connect('activate', () => {
    // Once, after GTK and libadwaita are up.
    Glass.init();

    // The bars of a toolbar view float on glass; the content runs under
    // them, so pad its start by the bars' height.
    const grid = tiles();
    const toolbar = new Glass.ToolbarView({ content: new Gtk.ScrolledWindow({ child: grid }) });
    toolbar.bind_property('top-bar-height', grid, 'margin-top', GObject.BindingFlags.SYNC_CREATE);

    const header = new Glass.HeaderBar();
    header.pack_end(new Gtk.Button({ icon_name: 'open-menu-symbolic', tooltip_text: 'Menu' }));
    toolbar.add_top_bar(header);

    // A row of buttons on one capsule of glass, at the bottom.
    const buttons = new Glass.ButtonGroup({ halign: Gtk.Align.CENTER, margin_bottom: 18 });
    for (const icon of ['go-previous-symbolic', 'starred-symbolic', 'user-trash-symbolic', 'go-next-symbolic'])
        buttons.append(new Gtk.Button({ icon_name: icon }));
    toolbar.add_bottom_bar(buttons);

    // Tuning: this one capsule bends what is under it more than the rest.
    // Glass.Context.get_default().set_param() would change all the glass.
    buttons.set_param('displacement-scale', 70);

    new Adw.ApplicationWindow({ application: app, title: 'Hello, Glass',
        default_width: 720, default_height: 540, content: toolbar }).present();
});

app.run([]);
