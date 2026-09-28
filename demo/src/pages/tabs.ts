// Tabs: a tab bar and a search field on glass over pages that scroll under
// them (design.md §6.8). The search button's glass becomes the field's
// (morph-id), and one tab needs attention.

import Adw from 'gi://Adw?version=1';
import Gtk from 'gi://Gtk?version=4.0';
import Glass from 'gi://Glass?version=1';

import { bindInset, Canvas, openImage, PhotoView, photos } from '../util.js';

const PARAGRAPHS = [
    'Glass refracts what is under it: the edge of every pane bends the content as it scrolls past.',
    'A tab bar floats over the page, one item per page, and a plate of glass slides to the one shown.',
    'The search button and the search field are one piece of glass that changes shape.',
    'Panels in a group flow into each other like drops of water, and come apart again.',
    'Menus are made of their own material: lighter than a sidebar, still easy to read.',
    'A sheet at the bottom of the window rounds only its top corners.',
    'Everything is drawn in the same frame as the content under it.',
];

export class TabsPage {
    readonly toolbar: Glass.ToolbarView;
    readonly header: Glass.HeaderBar;
    private stack: Adw.ViewStack;
    private tabs: Glass.TabBar;
    private searchGroup: Glass.Group;
    private searchButton: Glass.Button;
    private search: Glass.SearchEntry;
    private reading: Gtk.ListBox;
    private photosStack: Gtk.Stack;
    private photo: InstanceType<typeof PhotoView>;
    private photoScrolled: Gtk.ScrolledWindow;
    private backToGrid: Gtk.Button;
    private query = '';
    private insets: Gtk.Widget[] = [];

    constructor() {
        this.stack = new Adw.ViewStack({ vexpand: true, hexpand: true });

        // Photos: a grid that scrolls under the bars.
        const flow = new Gtk.FlowBox({ selection_mode: Gtk.SelectionMode.NONE, homogeneous: true,
            row_spacing: 8, column_spacing: 8, max_children_per_line: 4, min_children_per_line: 2,
            valign: Gtk.Align.START, margin_end: 8, margin_bottom: 96 });
        const textures = photos();
        for (let i = 0; i < Math.max(24, textures.length); i++)
            flow.append(new Gtk.Picture({ paintable: textures.length ? textures[i % textures.length] : null,
                content_fit: Gtk.ContentFit.COVER, height_request: 170, width_request: 170,
                css_classes: ['card'], overflow: Gtk.Overflow.HIDDEN }));
        this.insets.push(flow);
        // ... or one opened photo at the page's height, under the bars (as in Photos).
        this.photo = new PhotoView();
        this.photoScrolled = new Gtk.ScrolledWindow({ child: this.photo, hscrollbar_policy: Gtk.PolicyType.AUTOMATIC,
            vscrollbar_policy: Gtk.PolicyType.NEVER });
        this.photosStack = new Gtk.Stack({ transition_type: Gtk.StackTransitionType.CROSSFADE });
        this.photosStack.add_named(new Gtk.ScrolledWindow({ child: flow, hscrollbar_policy: Gtk.PolicyType.NEVER }), 'grid');
        this.photosStack.add_named(this.photoScrolled, 'photo');
        this.stack.add_titled_with_icon(this.photosStack, 'photos', 'Photos', 'image-x-generic-symbolic');

        // Reading: paragraphs the search filters.
        this.reading = new Gtk.ListBox({ selection_mode: Gtk.SelectionMode.NONE, css_classes: ['boxed-list'],
            valign: Gtk.Align.START, margin_end: 24, margin_bottom: 96 });
        for (let i = 0; i < 4; i++)
            for (const text of PARAGRAPHS)
                this.reading.append(new Gtk.Label({ label: text, wrap: true, xalign: 0,
                    margin_top: 14, margin_bottom: 14, margin_start: 16, margin_end: 16 }));
        this.reading.set_filter_func(row => this.query === '' ||
            ((row as Gtk.ListBoxRow).get_child() as Gtk.Label).get_label().toLowerCase().includes(this.query));
        this.insets.push(this.reading);
        const readingPage = this.stack.add_titled_with_icon(
            new Gtk.ScrolledWindow({ child: this.reading, hscrollbar_policy: Gtk.PolicyType.NEVER }),
            'reading', 'Reading', 'font-x-generic-symbolic');
        readingPage.set_needs_attention(true);
        this.stack.connect('notify::visible-child-name', () => {
            if (this.stack.get_visible_child_name() === 'reading')
                readingPage.set_needs_attention(false);
        });

        // Colours: a gradient.
        const canvas = new Canvas();
        canvas.setPattern('gradient');
        this.stack.add_titled_with_icon(canvas.widget, 'colors', 'Colours', 'preferences-color-symbolic');

        const view = new Glass.View({ content: this.stack });

        // The tab bar, at the start of the bottom edge.
        this.tabs = new Glass.TabBar({ stack: this.stack, halign: Gtk.Align.START, valign: Gtk.Align.END,
            margin_bottom: 18 });
        view.add_overlay(this.tabs);

        // At the end: a search button that becomes the search field.
        this.searchButton = Glass.Button.new_from_icon_name('edit-find-symbolic');
        this.searchButton.set_morph_id('search');
        this.searchButton.set_tooltip_text('Search');
        this.searchButton.connect('clicked', () => this.setSearching(true));
        this.search = new Glass.SearchEntry({ morph_id: 'search', visible: false, placeholder_text: 'Search the text',
            width_request: 240 });
        this.search.connect('search-changed', () => {
            this.query = this.search.get_text().toLowerCase();
            this.reading.invalidate_filter();
            if (this.query !== '')
                this.stack.set_visible_child_name('reading');
        });
        this.search.connect('stop-search', () => this.setSearching(false));
        const searchRow = new Gtk.Box();
        searchRow.append(this.searchButton);
        searchRow.append(this.search);
        this.searchGroup = new Glass.Group({ child: searchRow, halign: Gtk.Align.END, valign: Gtk.Align.END,
            margin_bottom: 22, margin_end: 18 });
        view.add_overlay(this.searchGroup);

        this.toolbar = new Glass.ToolbarView({ content: view });
        this.header = new Glass.HeaderBar();
        this.header.set_title_widget(new Gtk.Label({ label: 'Tabs', css_classes: ['heading'] }));
        const openPhoto = new Gtk.Button({ icon_name: 'document-open-symbolic', tooltip_text: 'Open a photo' });
        openPhoto.connect('clicked', () => this.openPhoto());
        this.header.pack_start(openPhoto);
        this.backToGrid = new Gtk.Button({ icon_name: 'view-grid-symbolic', tooltip_text: 'Back to the photos',
            visible: false });
        this.backToGrid.connect('clicked', () => {
            this.photosStack.set_visible_child_name('grid');
            this.backToGrid.set_visible(false);
        });
        this.header.pack_start(this.backToGrid);
        this.toolbar.add_top_bar(this.header);
        for (const widget of this.insets)
            bindInset(this.toolbar, 'top-bar-height', widget, 'margin-top', 8);
        this.search.set_key_capture_widget(this.toolbar);
    }

    // Any image file, alone in the Photos tab; the grid comes back with the
    // button next to the open one.
    private openPhoto() {
        openImage(this.toolbar, 2560, texture => {
            this.photo.setTexture(texture);
            this.photoScrolled.get_hadjustment().set_value(0);
            this.photosStack.set_visible_child_name('photo');
            this.stack.set_visible_child_name('photos');
            this.backToGrid.set_visible(true);
        });
    }

    // The button's glass becomes the field's, and back.
    private setSearching(searching: boolean) {
        if (!searching && this.search.get_text() !== '') {
            this.search.set_text('');
        }
        this.searchButton.set_visible(!searching);
        this.search.set_visible(searching);
        if (searching)
            this.search.grab_focus();
    }

    setInsetSource(split: Glass.SplitView) {
        bindInset(split, 'content-inset', this.header, 'margin-start', 0);
        bindInset(split, 'content-inset', this.tabs, 'margin-start', 18);
        for (const widget of this.insets)
            bindInset(split, 'content-inset', widget, 'margin-start', 8);
    }
}
