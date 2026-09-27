// Photos: a photo grid scrolling under a glass header bar and floating
// glass buttons (design.md §14). Shows refraction in the same frame as the
// scrolling content, the scroll edge effect and the adaptive colours.

import Gdk from 'gi://Gdk?version=4.0';
import GObject from 'gi://GObject';
import Graphene from 'gi://Graphene';
import Gsk from 'gi://Gsk?version=4.0';
import Gtk from 'gi://Gtk?version=4.0';
import Glass from 'gi://Glass?version=1';

import { bindInset, openImage, photos } from '../util.js';

// One photo at the full height it is given, as wide as that makes it (a
// GtkPicture fits both ways). Width-for-height, so a horizontal scrolled
// window measures it at the viewport's height and scrolls what is wider;
// narrower photos are centred.
const PhotoView = GObject.registerClass(class PhotoView extends Gtk.Widget {
    private texture: Gdk.Texture | null = null;

    setTexture(texture: Gdk.Texture) {
        this.texture = texture;
        this.queue_resize();
    }

    vfunc_get_request_mode(): Gtk.SizeRequestMode {
        return Gtk.SizeRequestMode.WIDTH_FOR_HEIGHT;
    }

    vfunc_measure(orientation: Gtk.Orientation, forSize: number): [number, number, number, number] {
        if (orientation === Gtk.Orientation.HORIZONTAL && this.texture && forSize > 0) {
            const width = Math.ceil(forSize * this.texture.get_width() / this.texture.get_height());
            return [width, width, -1, -1];
        }
        return [0, 0, -1, -1];
    }

    vfunc_snapshot(snapshot: Gtk.Snapshot) {
        const h = this.get_height();
        if (!this.texture || h <= 0)
            return;
        const w = h * this.texture.get_width() / this.texture.get_height();
        const x = Math.max(0, (this.get_width() - w) / 2);
        snapshot.append_scaled_texture(this.texture, Gsk.ScalingFilter.TRILINEAR,
            new Graphene.Rect().init(x, 0, w, h));
    }
});

export class PhotosPage {
    readonly toolbar: Glass.ToolbarView;
    readonly header: Glass.HeaderBar;
    private flow: Gtk.FlowBox;
    private scrolled: Gtk.ScrolledWindow;
    private actions: Gtk.Box;
    private stack: Gtk.Stack;
    private photo: InstanceType<typeof PhotoView>;
    private photoScrolled: Gtk.ScrolledWindow;

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

        // One opened photo, large, under the bars.
        this.photo = new PhotoView();
        this.photoScrolled = new Gtk.ScrolledWindow({
            child: this.photo,
            hscrollbar_policy: Gtk.PolicyType.AUTOMATIC,
            vscrollbar_policy: Gtk.PolicyType.NEVER,
            hexpand: true,
            vexpand: true,
        });
        this.stack = new Gtk.Stack({ transition_type: Gtk.StackTransitionType.CROSSFADE });
        this.stack.add_named(this.scrolled, 'grid');
        this.stack.add_named(this.photoScrolled, 'photo');

        this.toolbar = new Glass.ToolbarView({ content: this.stack });

        this.header = new Glass.HeaderBar();
        const open = new Gtk.Button({ icon_name: 'folder-open-symbolic', tooltip_text: 'Open a folder of photos' });
        open.connect('clicked', () => this.openFolder());
        this.header.pack_start(open);
        const openImage = new Gtk.Button({ icon_name: 'image-x-generic-symbolic', tooltip_text: 'Open a photo' });
        openImage.connect('clicked', () => this.openImage());
        this.header.pack_start(openImage);
        this.header.pack_start(new Gtk.Button({ icon_name: 'view-refresh-symbolic', tooltip_text: 'Reload' }));

        const layout = new Glass.ToggleGroup();
        layout.append('grid', null, 'view-grid-symbolic');
        layout.append('list', null, 'view-list-symbolic');
        layout.connect('notify::active-name', () => {
            const list = layout.get_active_name() === 'list';
            this.flow.set_max_children_per_line(list ? 1 : 4);
            this.flow.set_min_children_per_line(list ? 1 : 2);
            this.stack.set_visible_child_name('grid');
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
        bindInset(split, 'content-inset', this.photo, 'margin-start', 0);
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

    // Any image file, shown alone at the page's height; the grid comes back with the
    // layout toggle or a folder.
    private openImage() {
        openImage(this.toolbar, 2560, texture => {
            this.photo.setTexture(texture);
            this.photoScrolled.get_hadjustment().set_value(0);
            this.stack.set_visible_child_name('photo');
        });
    }

    private openFolder() {
        const dialog = new Gtk.FileDialog({ title: 'Open a folder of photos' });
        dialog.select_folder(this.toolbar.get_root() as Gtk.Window, null, (_d, result) => {
            try {
                const folder = dialog.select_folder_finish(result);
                if (folder?.get_path()) {
                    this.fill(photos(folder.get_path(), 48));
                    this.stack.set_visible_child_name('grid');
                }
            } catch (e) {
                // Cancelled.
            }
        });
    }
}
