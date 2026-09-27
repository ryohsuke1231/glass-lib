// Photos: a photo grid scrolling under a glass header bar and floating
// glass buttons (design.md §14). Shows refraction in the same frame as the
// scrolling content, the scroll edge effect and the adaptive colours.

import Gdk from 'gi://Gdk?version=4.0';
import Gio from 'gi://Gio';
import Gtk from 'gi://Gtk?version=4.0';
import Glass from 'gi://Glass?version=1';

import { bindInset, loadTexture, photos } from '../util.js';

export class PhotosPage {
    readonly toolbar: Glass.ToolbarView;
    readonly header: Glass.HeaderBar;
    private flow: Gtk.FlowBox;
    private scrolled: Gtk.ScrolledWindow;
    private actions: Gtk.Box;

    constructor() {
        this.flow = new Gtk.FlowBox({
            selection_mode: Gtk.SelectionMode.NONE,
            homogeneous: true,
            row_spacing: 8,
            column_spacing: 8,
            max_children_per_line: 4,
            min_children_per_line: 2,
            valign: Gtk.Align.START,
        });
        this.fill(photos());

        this.scrolled = new Gtk.ScrolledWindow({
            child: this.flow,
            hscrollbar_policy: Gtk.PolicyType.NEVER,
            hexpand: true,
            vexpand: true,
        });

        this.toolbar = new Glass.ToolbarView({ content: this.scrolled });

        this.header = new Glass.HeaderBar();
        const open = new Gtk.Button({ icon_name: 'folder-open-symbolic', tooltip_text: 'Open a folder of photos' });
        open.connect('clicked', () => this.openFolder());
        this.header.pack_start(open);
        const openImages = new Gtk.Button({ icon_name: 'image-x-generic-symbolic', tooltip_text: 'Open photos' });
        openImages.connect('clicked', () => this.openImages());
        this.header.pack_start(openImages);
        this.header.pack_start(new Gtk.Button({ icon_name: 'view-refresh-symbolic', tooltip_text: 'Reload' }));

        const layout = new Glass.ToggleGroup();
        layout.append('grid', null, 'view-grid-symbolic');
        layout.append('list', null, 'view-list-symbolic');
        layout.connect('notify::active-name', () => {
            const list = layout.get_active_name() === 'list';
            this.flow.set_max_children_per_line(list ? 1 : 4);
            this.flow.set_min_children_per_line(list ? 1 : 2);
        });
        this.header.set_title_widget(layout);

        this.header.pack_end(new Gtk.Button({ icon_name: 'open-menu-symbolic', tooltip_text: 'Menu' }));
        this.header.pack_end(new Gtk.Button({ icon_name: 'system-search-symbolic', tooltip_text: 'Search' }));
        this.toolbar.add_top_bar(this.header);

        const actions = new Gtk.Box({ spacing: 12, halign: Gtk.Align.CENTER, margin_bottom: 18, margin_top: 6 });
        this.actions = actions;
        actions.append(Glass.Button.new_from_icon_name('starred-symbolic'));
        actions.append(Glass.Button.new_with_label('Share'));
        actions.append(Glass.Button.new_from_icon_name('user-trash-symbolic'));
        this.toolbar.add_bottom_bar(actions);

        // The content starts below the bars and ends above them; it still
        // scrolls under both.
        bindInset(this.toolbar, 'top-bar-height', this.flow, 'margin-top', 4);
        bindInset(this.toolbar, 'bottom-bar-height', this.flow, 'margin-bottom', 4);
        this.flow.set_margin_end(8);
    }

    // The split view's sidebar covers the start of the page.
    setInsetSource(split: Glass.SplitView) {
        bindInset(split, 'content-inset', this.flow, 'margin-start', 0);
        bindInset(split, 'content-inset', this.header, 'margin-start', 0);
        bindInset(split, 'content-inset', this.actions, 'margin-start', 0);
    }

    adjustment(): Gtk.Adjustment {
        return this.scrolled.get_vadjustment();
    }

    scrollTo(value: number) {
        this.scrolled.get_vadjustment().set_value(value);
    }

    private fill(textures: ReturnType<typeof photos>) {
        let child;
        while ((child = this.flow.get_first_child()) !== null)
            this.flow.remove(child);
        // Enough rows to scroll through, even with few photos.
        for (let i = 0; i < Math.max(24, textures.length); i++) {
            const picture = new Gtk.Picture({
                paintable: textures.length ? textures[i % textures.length] : null,
                content_fit: Gtk.ContentFit.COVER,
                height_request: 190,
                width_request: 180,
                css_classes: ['card'],
                overflow: Gtk.Overflow.HIDDEN,
            });
            this.flow.append(picture);
        }
    }

    // Any image files, one or several.
    private openImages() {
        const images = new Gtk.FileFilter({ name: 'Images' });
        images.add_mime_type('image/*');
        const filters = new Gio.ListStore({ item_type: Gtk.FileFilter.$gtype });
        filters.append(images);
        const dialog = new Gtk.FileDialog({ title: 'Open photos', filters, default_filter: images });
        dialog.open_multiple(this.toolbar.get_root() as Gtk.Window, null, (_d, result) => {
            let files: Gio.ListModel | null = null;
            try {
                files = dialog.open_multiple_finish(result);
            } catch (e) {
                return;   // Cancelled.
            }
            const textures: Gdk.Texture[] = [];
            for (let i = 0; files && i < files.get_n_items(); i++) {
                const path = (files.get_item(i) as Gio.File).get_path();
                const texture = path ? loadTexture(path, 960) : null;
                if (texture)
                    textures.push(texture);
            }
            if (textures.length)
                this.fill(textures);
        });
    }

    private openFolder() {
        const dialog = new Gtk.FileDialog({ title: 'Open a folder of photos' });
        dialog.select_folder(this.toolbar.get_root() as Gtk.Window, null, (_d, result) => {
            try {
                const folder = dialog.select_folder_finish(result);
                if (folder?.get_path())
                    this.fill(photos(folder.get_path(), 48));
            } catch (e) {
                // Cancelled.
            }
        });
    }
}
