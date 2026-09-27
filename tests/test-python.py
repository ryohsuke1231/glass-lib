#!/usr/bin/env python3
# test-python.py — glass-lib from Python through GObject Introspection
# (design.md §16): the public API as PyGObject sees it (out parameters,
# properties, signals, interfaces, subclassing), then every part in one
# window. Needs PyGObject and a display; skipped (77) without them.
#
# SPDX-License-Identifier: MIT

import math
import sys

try:
    import gi
    gi.require_version('Gtk', '4.0')
    gi.require_version('Adw', '1')
    gi.require_version('Glass', '1')
    from gi.repository import Adw, Gdk, Gio, GLib, Glass, Gtk
except (ImportError, ValueError) as e:
    print(f'skipped: {e}')
    sys.exit(77)

if not Gtk.init_check():
    print('skipped: no display')
    sys.exit(77)

failures = []


def check(name, fn):
    try:
        fn()
        print(f'ok {name}')
    except Exception as e:
        failures.append(name)
        print(f'not ok {name}: {e!r}')


def expect(cond, what):
    if not cond:
        raise AssertionError(what)


Glass.init()
ctx = Glass.Context.get_default()


def context():
    keys = ctx.list_params()
    expect('max-z' in keys and 'blur-radius' in keys, f'list_params: {keys}')
    ok, lo, hi, default = ctx.get_param_range('shadow-intensity')
    expect(ok and (lo, hi, default) == (0.0, 1.0, 0.55), f'get_param_range: {ok} {lo} {hi} {default}')
    expect(ctx.get_effective_param(Glass.Material.REGULAR, 'max-z') == 35.0, 'get_effective_param')
    expect(abs(ctx.get_effective_param(Glass.Material.THICK, 'tint-strength') - 0.55) < 1e-9, 'tint-strength')
    pink = Gdk.RGBA()
    pink.parse('#ff66aa')
    ctx.props.tint_color = pink
    ok, got = ctx.get_tint_color()
    expect(ok and abs(got.green - 0.4) < 1e-6, f'get_tint_color: {ok} {got}')
    ctx.set_tint_color(None)
    expect(ctx.props.tint_color is None and not ctx.get_tint_color()[0], 'tint-color back to the materials')
    changed = []
    handler = ctx.connect('changed', lambda *_: changed.append(1))
    expect(ctx.set_param('max-z', 30.0), 'set_param')
    expect(ctx.is_param_set('max-z') and ctx.get_param('max-z') == 30.0, 'is_param_set / get_param')
    ctx.reset_param('max-z')
    ctx.disconnect(handler)
    expect(not ctx.is_param_set('max-z') and len(changed) == 2, f'reset_param / changed: {changed}')
    ctx.set_renderer(Glass.RendererMode.AUTO)
    expect(ctx.get_renderer() == Glass.RendererMode.AUTO, 'renderer')
    ctx.props.reduce_transparency = False


def enums():
    expect(int(Glass.Material.MENU) == 3, 'Material.MENU')
    expect(Glass.EdgeStyle.SOFT != Glass.EdgeStyle.NONE, 'EdgeStyle')
    expect(int(Glass.Appearance.UNKNOWN) == 0, 'Appearance')


def panel():
    p = Glass.Panel(material=Glass.Material.CLEAR, corner_radius=16, has_shadow=False)
    p.set_child(Gtk.Label(label='Hello'))
    expect(isinstance(p.get_child(), Gtk.Label), 'child')
    tint = Gdk.RGBA()
    tint.parse('rgba(255, 0, 0, 0.3)')
    p.set_tint(tint)
    ok, got = p.get_tint()
    expect(ok and abs(got.alpha - 0.3) < 1e-6, f'get_tint: {ok} {got}')
    p.set_tint(None)
    expect(p.props.tint is None, 'tint back to the material')
    ok, got = p.get_tint()
    expect(not ok and abs(got.alpha - 0.04) < 1e-6, f'get_tint (CLEAR): {ok} {got}')
    expect(Glass.PARAM_DISPLACEMENT_SCALE == 'displacement-scale', 'PARAM_ constants')
    expect(p.set_param(Glass.PARAM_DISPLACEMENT_SCALE, 90.0), 'panel set_param')
    expect(p.is_param_set('displacement-scale') and p.get_param('displacement-scale') == 90.0, 'panel get_param')
    expect(p.get_effective_param('displacement-scale') == 90.0, 'panel get_effective_param')
    p.reset_param('displacement-scale')
    expect(p.get_effective_param('displacement-scale') == 45.0, 'panel reset_param')
    expect(math.isnan(p.get_param('displacement-scale')), 'panel get_param when not set')
    p.set_corner_radii(24, -1, 0, -1)
    expect(p.get_corner_radii() == (24.0, -1.0, 0.0, -1.0), f'get_corner_radii: {p.get_corner_radii()}')
    expect(p.props.top_left_radius == 24.0, 'top-left-radius')
    p.props.morph_id = 'hello'
    expect(p.get_morph_id() == 'hello', 'morph-id')
    expect(p.get_appearance() == Glass.Appearance.UNKNOWN, 'appearance before drawing')


class Badge(Glass.Panel):
    """A panel subclassed in Python."""
    __gtype_name__ = 'GlassTestBadge'

    def __init__(self, text):
        super().__init__(corner_radius=10)
        self.set_child(Gtk.Label(label=text))


def subclass():
    b = Badge('new')
    expect(isinstance(b, Glass.Panel) and b.get_corner_radius() == 10.0, 'subclass')


def toggle_group_edit():
    g = Glass.ToggleGroup()
    for name in 'abc':
        g.append(name, name.upper(), None)
    g.set_tooltip(1, 'Bee')
    expect(g.get_tooltip(1) == 'Bee', 'set_tooltip')
    g.set_active_name('b')
    g.remove(1)
    expect(g.props.n_toggles == 2 and g.get_active_name() == 'c', f'remove: {g.get_active_name()}')
    g.remove_all()
    expect(g.props.n_toggles == 0, 'remove_all')


def actions():
    group = Gio.SimpleActionGroup()
    flag = Gio.SimpleAction.new_stateful('flag', None, GLib.Variant.new_boolean(False))
    group.add_action(flag)
    box = Gtk.Box()
    box.insert_action_group('test', group)
    sw = Glass.Switch(action_name='test.flag')
    box.append(sw)
    flag.set_state(GLib.Variant.new_boolean(True))
    expect(sw.props.active, 'switch follows the action')
    sw.props.active = False
    expect(not flag.get_state().get_boolean(), 'switch changes the action')
    mb = Glass.MenuButton(action_name='test.flag')
    expect(isinstance(mb, Gtk.Actionable) and mb.get_action_name() == 'test.flag', 'menu button actionable')


check('context', context)
check('toggle group edit', toggle_group_edit)
check('actions', actions)
check('enums', enums)
check('panel', panel)
check('subclass', subclass)

app = Adw.Application(application_id='io.github.ryohsuke1231.GlassPythonTest',
                      flags=Gio.ApplicationFlags.NON_UNIQUE)
parts = {}


def build(app):
    window = Adw.ApplicationWindow(application=app, default_width=800, default_height=600)
    stack = Adw.ViewStack()
    stack.add_titled_with_icon(Gtk.Label(label='one'), 'one', 'One', 'go-home-symbolic')
    stack.add_titled_with_icon(Gtk.Label(label='two'), 'two', 'Two', 'go-next-symbolic')

    view = Glass.View(content=stack)
    column = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=12,
                     halign=Gtk.Align.CENTER, valign=Gtk.Align.CENTER)
    toggles = Glass.ToggleGroup()
    toggles.append('a', 'Alpha', None)
    toggles.append('b', 'Beta', None)
    toggles.set_active_name('b')
    column.append(toggles)
    row = Gtk.Box(spacing=8)
    row.append(Glass.Switch(active=True))
    slider = Glass.Slider.new_with_range(0, 10, 1)
    row.append(slider)
    column.append(Glass.Panel(child=row, corner_radius=18))
    drops = Gtk.Box(spacing=6)
    drops.append(Glass.Button.new_from_icon_name('go-previous-symbolic'))
    drops.append(Glass.Button.new_with_label('Label'))
    column.append(Glass.Group(child=drops))
    buttons = Glass.ButtonGroup()
    buttons.append(Gtk.Button(icon_name='go-up-symbolic'))
    column.append(buttons)
    search = Glass.SearchEntry(placeholder_text='Search', morph_id='search')
    column.append(search)
    column.append(Badge('Python'))
    view.add_overlay(column)
    tabs = Glass.TabBar(stack=stack, valign=Gtk.Align.END, halign=Gtk.Align.CENTER)
    view.add_overlay(tabs)

    toolbar = Glass.ToolbarView(content=view)
    header = Glass.HeaderBar()
    menu = Gio.Menu()
    menu.append('Item', 'app.item')
    header.pack_end(Glass.MenuButton(icon_name='open-menu-symbolic', menu_model=menu))
    toolbar.add_top_bar(header)
    sidebar = Glass.ToolbarView(content=Gtk.Label(label='Sidebar'))
    split = Glass.SplitView(sidebar=sidebar, content=toolbar)
    window.set_content(split)
    window.present()
    parts.update(window=window, stack=stack, tabs=tabs, search=search, toggles=toggles,
                 slider=slider, split=split, toolbar=toolbar, view=view)


def parts_together():
    stack, group = parts['stack'], parts['tabs'].get_first_child()
    stack.set_visible_child_name('two')
    expect(group.get_active() == 1, 'the tab bar follows the stack')
    group.set_active(0)
    expect(stack.get_visible_child_name() == 'one', 'the stack follows the tab bar')

    search, got = parts['search'], []
    search.connect('search-changed', lambda *_: got.append(search.get_text()))
    search.set_search_delay(0)
    search.set_text('glass')           # GtkEditable, through the delegate
    expect(got and got[-1] == 'glass', f'search-changed: {got}')

    expect(parts['toggles'].get_active_name() == 'b', 'toggle group')
    expect(parts['slider'].get_adjustment().get_upper() == 10, 'slider adjustment')
    expect(parts['toolbar'].get_top_bar_height() > 0, 'top-bar-height')
    expect(parts['split'].get_content_inset() > 0, 'content-inset')
    expect(parts['view'].get_active_renderer() in (Glass.RendererMode.FULL, Glass.RendererMode.FALLBACK),
           'active renderer')

    d = Glass.Dialog(content=Gtk.Label(label='Dialog'), content_width=240)
    expect(isinstance(d.get_content(), Gtk.Label), 'dialog content')
    expect(isinstance(Glass.Popover.new_from_model(menu_model()), Gtk.Popover), 'popover from a model')


def menu_model():
    menu = Gio.Menu()
    menu.append('One', 'app.one')
    return menu


def after_start():
    check('parts together', parts_together)
    app.quit()
    return GLib.SOURCE_REMOVE


app.connect('activate', build)
GLib.timeout_add(1200, after_start)
app.run([])

print(f'{len(failures)} failed: {failures}' if failures else 'all passed')
sys.exit(1 if failures else 0)
