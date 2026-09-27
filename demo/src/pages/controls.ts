// Controls: the dedicated glass parts (design.md §6.7) over a background of
// your choice: switches and sliders whose knobs turn to lenses while held,
// buttons that fuse like drops of water, a glass menu, and a dialog whose
// Close button is the glass that confirms (Glass.Material.PROMINENT).

import Adw from 'gi://Adw?version=1';
import Gdk from 'gi://Gdk?version=4.0';
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

        // Buttons in a Glass.Group (design.md §6.7, §6.8). The first three
        // touch, so their glass is one capsule; the two after it stand apart,
        // further than the group's spacing, as drops of their own. "More"
        // shows them: they come out of the capsule like drops, and go back
        // into it when hidden.
        //
        //   [ ⏮ ⏵ ⋯ ]  ( ☆ )  ( 🗑 )
        //
        // The row keeps the room for the drops, so the capsule stays put
        // while they come and go (centred, it would jump by half of them).
        const BUTTON = 44, GAP = 12;
        const capsule = new Gtk.Box();
        for (const icon of ['media-skip-backward-symbolic', 'media-playback-start-symbolic'])
            capsule.append(Glass.Button.new_from_icon_name(icon));
        const more = Glass.Button.new_from_icon_name('view-more-symbolic');
        more.set_tooltip_text('More');
        capsule.append(more);
        const drops = new Gtk.Box({ spacing: GAP });
        drops.append(capsule);
        const extras = ['starred-symbolic', 'user-trash-symbolic'].map(icon => {
            const button = Glass.Button.new_from_icon_name(icon);
            button.set_visible(false);
            drops.append(button);
            return button;
        });
        more.connect('clicked', () => extras.forEach(b => b.set_visible(!b.get_visible())));
        this.stage.append(new Glass.Group({ child: drops, halign: Gtk.Align.CENTER,
            width_request: 3 * BUTTON + extras.length * (GAP + BUTTON) }));

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

    // An ordinary dialog: the glass is only its Close button, the button that
    // confirms (Glass.Material.PROMINENT) - here a vivid blue instead of the
    // theme's accent, strongly frosted.
    showDialog() {
        const text = new Gtk.Box({ orientation: Gtk.Orientation.VERTICAL, spacing: 12,
            margin_top: 28, margin_bottom: 24, margin_start: 32, margin_end: 32 });
        text.append(new Gtk.Label({ label: 'A Glass Button', css_classes: ['title-2'] }));
        text.append(new Gtk.Label({ label: 'The dialog is plain; its Close button is glass, tinted and frosted.',
            wrap: true, justify: Gtk.Justification.CENTER }));
        // Room for the button, which is on the glass layer of the view.
        text.append(new Gtk.Box({ height_request: 44, margin_top: 8 }));

        const blue = new Gdk.RGBA();
        blue.parse('rgba(10, 132, 255, 0.92)');
        const done = new Glass.Button({ label: 'Close', material: Glass.Material.PROMINENT,
            halign: Gtk.Align.CENTER, valign: Gtk.Align.END, margin_bottom: 24, width_request: 120 });
        done.set_tint(blue);
        done.set_param(Glass.PARAM_BLUR_RADIUS, 10);

        const view = new Glass.View({ content: text });
        view.add_overlay(done);

        const dialog = new Adw.Dialog({ child: view, content_width: 380 });
        done.connect('clicked', () => dialog.close());
        dialog.present(this.toolbar);
    }
}
