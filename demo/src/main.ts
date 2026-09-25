// Glass Gallery — the demo and test harness of glass-lib (design.md §14).
//
//   cd demo && npm run build
//   meson devenv -C build gjs -m demo/dist/main.js
//
// Environment (for comparisons and screenshots):
//   GLASS_GALLERY_PAGE=photos|playground|lab   the page to start on
//   GLASS_GALLERY_SCROLL=PX                    the Photos page's scroll position
//   GLASS_GALLERY_PATTERN=stripes|checker|...  the Playground's background
//   GLASS_GALLERY_AUTOSCROLL=1                 scroll the photos forever (measurements)
//   GLASS_GALLERY_SCREENSHOT=file.png          render the window, then quit
//   GLASS_DEBUG=hud                            timings in each view's corner

import Adw from 'gi://Adw?version=1';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import GObject from 'gi://GObject';
import Gtk from 'gi://Gtk?version=4.0';
import Glass from 'gi://Glass?version=1';
import System from 'system';

import { LabPage } from './pages/lab.js';
import { PhotosPage } from './pages/photos.js';
import { PlaygroundPage } from './pages/playground.js';
import { maybeScreenshot, Pattern } from './util.js';

const PAGES: [string, string, string][] = [
    ['photos', 'Photos', 'image-x-generic-symbolic'],
    ['playground', 'Playground', 'input-mouse-symbolic'],
    ['lab', 'Lab', 'preferences-other-symbolic'],
];

function buildWindow(app: Adw.Application) {
    Glass.init();

    const window = new Adw.ApplicationWindow({ application: app, title: 'Glass Gallery',
        default_width: 1120, default_height: 780 });

    const photos = new PhotosPage();
    const playground = new PlaygroundPage();
    const lab = new LabPage();
    const pages = { photos, playground, lab };

    const stack = new Gtk.Stack({ transition_type: Gtk.StackTransitionType.CROSSFADE, hexpand: true, vexpand: true });
    for (const [name, title] of PAGES)
        stack.add_titled(pages[name as keyof typeof pages].toolbar, name, title);

    // The sidebar: a list of pages under its own header bar, on thick glass.
    const list = new Gtk.ListBox({ css_classes: ['navigation-sidebar'], selection_mode: Gtk.SelectionMode.SINGLE });
    for (const [name, title, icon] of PAGES) {
        const box = new Gtk.Box({ spacing: 10, margin_top: 6, margin_bottom: 6, margin_start: 4 });
        box.append(new Gtk.Image({ icon_name: icon }));
        box.append(new Gtk.Label({ label: title, xalign: 0 }));
        const row = new Gtk.ListBoxRow({ child: box, name });
        list.append(row);
    }
    list.connect('row-selected', (_list, row: Gtk.ListBoxRow | null) => {
        if (row)
            stack.set_visible_child_name(row.get_name());
    });

    const sidebar = new Glass.ToolbarView({ content: list });
    sidebar.set_top_edge_style(Glass.EdgeStyle.NONE);
    const sidebarHeader = new Glass.HeaderBar({ show_title: false, show_end_title_buttons: false });
    sidebar.add_top_bar(sidebarHeader);
    sidebar.bind_property('top-bar-height', list, 'margin-top', GObject.BindingFlags.SYNC_CREATE);

    const split = new Glass.SplitView({ sidebar, content: stack });
    const hide = new Gtk.Button({ icon_name: 'sidebar-show-symbolic', tooltip_text: 'Hide the sidebar' });
    hide.connect('clicked', () => split.set_show_sidebar(false));
    sidebarHeader.pack_end(hide);

    // Each page's header: its window controls and a "show sidebar" button
    // appear when the sidebar (which has the controls) slides away.
    for (const page of [photos, playground, lab]) {
        page.setInsetSource(split);
        const show = new Gtk.Button({ icon_name: 'sidebar-show-symbolic', tooltip_text: 'Show the sidebar' });
        show.connect('clicked', () => split.set_show_sidebar(true));
        page.header.pack_start(show);
        split.bind_property('show-sidebar', show, 'visible',
            GObject.BindingFlags.SYNC_CREATE | GObject.BindingFlags.INVERT_BOOLEAN);
        split.bind_property('show-sidebar', page.header, 'show-start-title-buttons',
            GObject.BindingFlags.SYNC_CREATE | GObject.BindingFlags.INVERT_BOOLEAN);
    }

    window.set_content(split);

    const start = GLib.getenv('GLASS_GALLERY_PAGE') ?? 'photos';
    const index = Math.max(0, PAGES.findIndex(([name]) => name === start));
    list.select_row(list.get_row_at_index(index));

    const pattern = GLib.getenv('GLASS_GALLERY_PATTERN');
    if (pattern)
        playground.setPattern(pattern as Pattern);

    window.present();

    // GLASS_GALLERY_AUTOSCROLL=1: the photos scroll up and down forever
    // (measurements while scrolling, design.md §8.8).
    if (GLib.getenv('GLASS_GALLERY_AUTOSCROLL')) {
        let direction = 1;
        window.add_tick_callback(() => {
            const adj = photos.adjustment();
            const next = adj.get_value() + direction * 15;
            if (next <= adj.get_lower() || next >= adj.get_upper() - adj.get_page_size())
                direction = -direction;
            adj.set_value(next);
            return GLib.SOURCE_CONTINUE;
        });
    }

    const scroll = GLib.getenv('GLASS_GALLERY_SCROLL');
    if (scroll)
        GLib.timeout_add(GLib.PRIORITY_DEFAULT, 800, () => {
            photos.scrollTo(Number(scroll));
            return GLib.SOURCE_REMOVE;
        });

    maybeScreenshot(window, () => app.quit());
}

const app = new Adw.Application({
    application_id: 'io.github.ryohsuke1231.GlassGallery',
    flags: Gio.ApplicationFlags.NON_UNIQUE,
});
app.connect('activate', () => buildWindow(app));
app.run([System.programInvocationName, ...ARGV]);
