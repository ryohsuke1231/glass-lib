// Controls: the dedicated glass parts (design.md §6.7) over a background of
// your choice: switches and sliders whose knobs turn to lenses while held,
// buttons that fuse like drops of water, a glass menu and a glass dialog.

import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import Gtk from 'gi://Gtk?version=4.0';
import Glass from 'gi://Glass?version=1';

import { AnimationBar, bindInset, Canvas, PATTERNS, Pattern } from '../util.js';

function row(title: string, control: Gtk.Widget): Gtk.Box {
    const box = new Gtk.Box({ spacing: 12 });
    box.append(new Gtk.Label({ label: title, hexpand: true, xalign: 0 }));
    control.set_valign(Gtk.Align.CENTER);
    box.append(control);
    return box;
}

export class ControlsPage {
    readonly toolbar: Glass.ToolbarView;
    readonly header: Glass.HeaderBar;
    private canvas: Canvas;
    private background: Glass.ToggleGroup;
    private stage: Gtk.Box;
    private animation: AnimationBar;

    constructor() {
        this.canvas = new Canvas();
        const view = new Glass.View({ content: this.canvas.widget });

        this.stage = new Gtk.Box({ orientation: Gtk.Orientation.VERTICAL, spacing: 28,
            halign: Gtk.Align.CENTER, valign: Gtk.Align.CENTER });

        // Settings on a card: the knobs are glass over the card's glass.
        const settings = new Gtk.Box({ orientation: Gtk.Orientation.VERTICAL, spacing: 14,
            margin_top: 18, margin_bottom: 18, margin_start: 22, margin_end: 22 });
        const wifi = new Glass.Switch({ active: true });
        settings.append(row('Wi-Fi', wifi));
        settings.append(row('Bluetooth', new Glass.Switch()));
        const volume = Glass.Slider.new_with_range(0, 100, 1);
        volume.set_value(60);
        volume.set_size_request(220, -1);
        settings.append(row('Volume', volume));
        const brightness = Glass.Slider.new_with_range(0, 100, 1);
        brightness.set_value(35);
        brightness.set_size_request(220, -1);
        settings.append(row('Brightness', brightness));
        this.stage.append(new Glass.Panel({ child: settings, corner_radius: 22, width_request: 420 }));

        // Buttons in a Glass.Group: closer than its spacing, they fuse.
        const drops = new Gtk.Box({ spacing: 8, halign: Gtk.Align.CENTER });
        for (const icon of ['media-skip-backward-symbolic', 'media-playback-start-symbolic', 'media-skip-forward-symbolic'])
            drops.append(Glass.Button.new_from_icon_name(icon));
        const group = new Glass.Group({ child: drops, halign: Gtk.Align.CENTER });
        this.stage.append(group);

        const gap = Glass.Slider.new_with_range(0, 48, 1);
        gap.set_value(8);
        gap.set_size_request(260, -1);
        gap.get_adjustment().connect('value-changed', () => drops.set_spacing(Math.round(gap.get_value())));
        const gapRow = new Gtk.Box({ spacing: 12, margin_top: 8, margin_bottom: 8, margin_start: 18, margin_end: 18 });
        gapRow.append(new Gtk.Label({ label: 'Gap', css_classes: ['dim-label'] }));
        gapRow.append(gap);
        this.stage.append(new Glass.Panel({ child: gapRow, halign: Gtk.Align.CENTER }));

        // A row of buttons in one capsule: each press bulges the glass.
        const buttons = new Glass.ButtonGroup({ halign: Gtk.Align.CENTER });
        for (const icon of ['format-text-bold-symbolic', 'format-text-italic-symbolic', 'format-text-underline-symbolic'])
            buttons.append(new Gtk.Button({ icon_name: icon }));
        this.stage.append(buttons);

        view.add_overlay(this.stage);

        // Play / pause and speed of the moving background.
        this.animation = new AnimationBar(this.canvas);
        this.animation.widget.set_margin_bottom(18);
        view.add_overlay(this.animation.widget);

        this.toolbar = new Glass.ToolbarView({ content: view });
        this.header = new Glass.HeaderBar();

        const background = new Glass.ToggleGroup();
        for (const [name, , icon] of PATTERNS)
            background.append(name, null, icon);
        background.connect('notify::active-name', () =>
            this.canvas.setPattern(background.get_active_name() as Pattern));
        this.header.set_title_widget(background);
        this.background = background;

        // A glass menu: its actions pick the background too.
        const actions = new Gio.SimpleActionGroup();
        const pattern = new Gio.SimpleAction({ name: 'pattern', parameter_type: new GLib.VariantType('s') });
        pattern.connect('activate', (_action, value) => background.set_active_name(value!.unpack() as string));
        actions.add_action(pattern);
        const dialog = new Gio.SimpleAction({ name: 'dialog' });
        dialog.connect('activate', () => this.showDialog());
        actions.add_action(dialog);
        this.toolbar.insert_action_group('controls', actions);

        const menu = new Gio.Menu();
        const patterns = new Gio.Menu();
        for (const [name, title] of PATTERNS)
            patterns.append(title, `controls.pattern::${name}`);
        menu.append_submenu('Background', patterns);
        const section = new Gio.Menu();
        section.append('Show a Dialog…', 'controls.dialog');
        menu.append_section(null, section);
        const menuButton = new Glass.MenuButton({ icon_name: 'open-menu-symbolic', menu_model: menu,
            tooltip_text: 'Menu' });
        this.header.pack_end(menuButton);

        const open = new Gtk.Button({ icon_name: 'window-new-symbolic', tooltip_text: 'Show a dialog' });
        open.connect('clicked', () => this.showDialog());
        this.header.pack_end(open);

        this.toolbar.add_top_bar(this.header);
        this.toolbar.set_top_edge_style(Glass.EdgeStyle.NONE);
        bindInset(this.toolbar, 'top-bar-height', this.stage, 'margin-top', 0);

        this.canvas.setPattern('gradient');
        background.set_active_name('gradient');
    }

    setInsetSource(split: Glass.SplitView) {
        bindInset(split, 'content-inset', this.header, 'margin-start', 0);
        bindInset(split, 'content-inset', this.stage, 'margin-start', 0);
        bindInset(split, 'content-inset', this.animation.widget, 'margin-start', 0);
    }

    setPattern(pattern: Pattern) {
        this.background.set_active_name(pattern);
    }

    showDialog() {
        const box = new Gtk.Box({ orientation: Gtk.Orientation.VERTICAL, spacing: 12,
            margin_top: 28, margin_bottom: 24, margin_start: 32, margin_end: 32 });
        box.append(new Gtk.Label({ label: 'A Sheet of Glass', css_classes: ['title-2'] }));
        box.append(new Gtk.Label({ label: 'The window shows through the dialog, blurred and refracted.',
            wrap: true, justify: Gtk.Justification.CENTER }));
        const done = new Gtk.Button({ label: 'Close', halign: Gtk.Align.CENTER, margin_top: 8,
            css_classes: ['pill', 'suggested-action'] });
        box.append(done);

        const dialog = new Glass.Dialog({ content: box, content_width: 380 });
        done.connect('clicked', () => dialog.close());
        dialog.present(this.toolbar);
    }
}
