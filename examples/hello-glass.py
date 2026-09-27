#!/usr/bin/env python3
# hello-glass.py — a complete glass-lib app in Python: a header bar and a row
# of buttons floating on glass over content that scrolls under them.
#
#   meson devenv -C build -w . python3 examples/hello-glass.py
#
# (or plain `python3 hello-glass.py` with glass-lib installed)
#
# SPDX-License-Identifier: MIT

import gi

gi.require_version('Gtk', '4.0')
gi.require_version('Adw', '1')
gi.require_version('Glass', '1')
from gi.repository import Adw, Gdk, GObject, Glass, Gtk


def tiles():
    """Something colourful to scroll under the glass."""
    css = '.tile { border-radius: 14px; min-height: 110px; }\n'
    css += ''.join(f'.tile-{i} {{ background: hsl({i * 30} 75% 58%); }}\n' for i in range(12))
    provider = Gtk.CssProvider()
    provider.load_from_string(css)
    Gtk.StyleContext.add_provider_for_display(Gdk.Display.get_default(), provider,
                                              Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)

    grid = Gtk.FlowBox(selection_mode=Gtk.SelectionMode.NONE, homogeneous=True,
                       min_children_per_line=3, max_children_per_line=6, row_spacing=10,
                       column_spacing=10, valign=Gtk.Align.START, margin_start=12,
                       margin_end=12, margin_bottom=96)
    for i in range(60):
        grid.append(Gtk.Box(css_classes=['tile', f'tile-{(i * 5) % 12}']))
    return grid


def activate(app):
    # Once, after GTK and libadwaita are up.
    Glass.init()

    # The bars of a toolbar view float on glass; the content runs under them,
    # so pad its start by the bars' height.
    grid = tiles()
    toolbar = Glass.ToolbarView(content=Gtk.ScrolledWindow(child=grid))
    toolbar.bind_property('top-bar-height', grid, 'margin-top', GObject.BindingFlags.SYNC_CREATE)

    header = Glass.HeaderBar()
    header.pack_end(Gtk.Button(icon_name='open-menu-symbolic', tooltip_text='Menu'))
    toolbar.add_top_bar(header)

    # A row of buttons on one capsule of glass, at the bottom.
    buttons = Glass.ButtonGroup(halign=Gtk.Align.CENTER, margin_bottom=18)
    for icon in ('go-previous-symbolic', 'starred-symbolic', 'user-trash-symbolic', 'go-next-symbolic'):
        buttons.append(Gtk.Button(icon_name=icon))
    toolbar.add_bottom_bar(buttons)

    # Tuning: this one capsule bends what is under it more than the rest.
    # Glass.Context.get_default().set_param() would change all the glass.
    buttons.set_param(Glass.PARAM_DISPLACEMENT_SCALE, 70)

    Adw.ApplicationWindow(application=app, title='Hello, Glass', default_width=720,
                          default_height=540, content=toolbar).present()


app = Adw.Application(application_id='org.example.HelloGlass')
app.connect('activate', activate)
app.run(None)
