// Lab: every optical parameter as a slider, the tint, the renderer switch,
// reduced transparency, over a background of your choice (design.md §14).
// The inspector is itself a pane of thick glass. Everything here is set on
// Glass.Context, so it tunes every piece of glass in the app at once.

import Gdk from 'gi://Gdk?version=4.0';
import GLib from 'gi://GLib';
import Gtk from 'gi://Gtk?version=4.0';
import Glass from 'gi://Glass?version=1';

import { AnimationBar, bindInset, Canvas, PATTERNS, Pattern } from '../util.js';

// Group headings for the parameter list, in spec/params.json order.
const GROUPS: [string, string[]][] = [
    ['Lens', ['max-z', 'displacement-scale', 'edge-smoothing', 'profile-shape-n', 'ior', 'chroma-strength']],
    ['Light', ['specular-intensity', 'shininess', 'rim-width', 'rim-intensity', 'rim-directional-power',
        'rim-power', 'rim-light-color-intensity', 'sheen-intensity', 'light-angle-deg']],
    ['Shadow', ['shadow-radius', 'shadow-intensity', 'ao-intensity', 'ao-radius']],
    ['Tint', ['tint-strength']],
    ['Blur', ['blur-radius', 'blur-downscale']],
];

// The tint colour (Glass.Context:tint-color): unset, each material keeps its
// own (white, or the theme's window and popover colours).
class TintColorRow {
    readonly widget: Gtk.Box;
    private button: Gtk.ColorDialogButton;
    private reset: Gtk.Button;
    private updating = false;

    constructor(private context: Glass.Context) {
        this.button = new Gtk.ColorDialogButton({
            dialog: new Gtk.ColorDialog({ title: 'Tint', with_alpha: false }),
            valign: Gtk.Align.CENTER,
        });
        this.button.connect('notify::rgba', () => {
            if (!this.updating)
                context.set_tint_color(this.button.get_rgba());
        });

        this.reset = new Gtk.Button({ icon_name: 'edit-undo-symbolic', tooltip_text: 'Back to the materials\' colours',
            css_classes: ['flat', 'circular'], valign: Gtk.Align.CENTER });
        this.reset.connect('clicked', () => context.set_tint_color(null));

        this.widget = new Gtk.Box({ spacing: 4, margin_start: 12, margin_end: 6 });
        this.widget.append(new Gtk.Label({ label: 'tint-color', xalign: 0, hexpand: true, css_classes: ['caption'] }));
        this.widget.append(this.button);
        this.widget.append(this.reset);
        this.sync();
    }

    sync() {
        const [set, color] = this.context.get_tint_color();
        const shown = new Gdk.RGBA();
        shown.parse('white');
        this.updating = true;
        this.button.set_rgba(set ? color! : shown);
        this.updating = false;
        this.reset.set_sensitive(set);
    }
}

class ParamRow {
    readonly widget: Gtk.Box;
    private scale: Gtk.Scale;
    private reset: Gtk.Button;
    private updating = false;

    constructor(private context: Glass.Context, private key: string) {
        const [, min, max] = context.get_param_range(key);
        const step = max - min > 50 ? 1 : max - min > 5 ? 0.1 : 0.01;

        this.scale = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, min, max, step);
        this.scale.set_draw_value(true);
        this.scale.set_value_pos(Gtk.PositionType.RIGHT);
        this.scale.set_digits(step >= 1 ? 0 : step >= 0.1 ? 1 : 2);
        this.scale.set_hexpand(true);
        this.scale.connect('value-changed', () => {
            if (!this.updating)
                context.set_param(key, this.scale.get_value());
        });

        this.reset = new Gtk.Button({ icon_name: 'edit-undo-symbolic', tooltip_text: 'Back to the material\'s value',
            css_classes: ['flat', 'circular'], valign: Gtk.Align.CENTER });
        this.reset.connect('clicked', () => context.reset_param(key));

        const title = new Gtk.Label({ label: key, xalign: 0, css_classes: ['caption'] });
        const row = new Gtk.Box({ spacing: 4 });
        row.append(this.scale);
        row.append(this.reset);

        this.widget = new Gtk.Box({ orientation: Gtk.Orientation.VERTICAL, margin_start: 12, margin_end: 6 });
        this.widget.append(title);
        this.widget.append(row);
        this.sync();
    }

    // The slider shows what a REGULAR panel uses: the set value or the
    // material's.
    sync() {
        this.updating = true;
        this.scale.set_value(this.context.get_effective_param(Glass.Material.REGULAR, this.key));
        this.updating = false;
        this.reset.set_sensitive(this.context.is_param_set(this.key));
    }
}

export class LabPage {
    readonly toolbar: Glass.ToolbarView;
    readonly header: Glass.HeaderBar;
    private canvas: Canvas;
    private specimens: Glass.Panel[] = [];
    private animation: AnimationBar;
    private background: Glass.ToggleGroup;

    constructor() {
        const context = Glass.Context.get_default();
        this.canvas = new Canvas();
        const stage = new Glass.View({ content: this.canvas.widget });

        // Specimens: a capsule toolbar and a rounded card.
        const capsule = new Glass.ButtonGroup({ halign: Gtk.Align.START, valign: Gtk.Align.START, margin_top: 110 });
        for (const icon of ['go-previous-symbolic', 'media-playback-start-symbolic', 'go-next-symbolic'])
            capsule.append(new Gtk.Button({ icon_name: icon }));
        this.specimens.push(capsule);
        const card = new Gtk.Box({ orientation: Gtk.Orientation.VERTICAL, spacing: 6, margin_top: 18,
            margin_bottom: 18, margin_start: 22, margin_end: 22 });
        card.append(new Gtk.Label({ label: '21°', css_classes: ['title-1'], xalign: 0 }));
        card.append(new Gtk.Label({ label: 'Partly cloudy', xalign: 0 }));
        this.specimens.push(new Glass.Panel({ child: card, corner_radius: 22, width_request: 220,
            halign: Gtk.Align.START, valign: Gtk.Align.START, margin_top: 200 }));
        // The button that confirms: tinted with the accent (Glass.Material.PROMINENT).
        this.specimens.push(new Glass.Button({ label: 'Done', material: Glass.Material.PROMINENT,
            halign: Gtk.Align.START, valign: Gtk.Align.START, margin_top: 330 }));
        this.specimens.forEach(p => stage.add_overlay(p));

        // The inspector: thick glass over the same background.
        const rows: ParamRow[] = [];
        const list = new Gtk.Box({ orientation: Gtk.Orientation.VERTICAL, spacing: 8, margin_top: 12, margin_bottom: 12 });

        const renderer = new Glass.ToggleGroup({ halign: Gtk.Align.CENTER });
        renderer.append('full', 'Full', null);
        renderer.append('fallback', 'CSS fallback', null);
        renderer.connect('notify::active-name', () => context.set_renderer(
            renderer.get_active_name() === 'fallback' ? Glass.RendererMode.FALLBACK : Glass.RendererMode.AUTO));
        list.append(renderer);

        const reduce = new Glass.Switch({ valign: Gtk.Align.CENTER });
        reduce.connect('notify::active', () => context.set_reduce_transparency(reduce.get_active()));
        const reduceRow = new Gtk.Box({ spacing: 8, margin_start: 12, margin_end: 12 });
        reduceRow.append(new Gtk.Label({ label: 'Reduce transparency', hexpand: true, xalign: 0 }));
        reduceRow.append(reduce);
        list.append(reduceRow);

        const background = new Glass.ToggleGroup({ halign: Gtk.Align.CENTER });
        for (const [name, , icon] of PATTERNS)
            background.append(name, null, icon);
        background.connect('notify::active-name', () =>
            this.canvas.setPattern(background.get_active_name() as Pattern));
        list.append(background);
        this.background = background;

        // Play / pause and speed of the moving background, on the inspector.
        this.animation = new AnimationBar(this.canvas, false);
        this.animation.widget.set_halign(Gtk.Align.CENTER);
        list.append(this.animation.widget);

        // The two lenses measured on macOS (Glass.Lens), or each material's own.
        const lens = new Glass.ToggleGroup({ halign: Gtk.Align.CENTER, margin_top: 4 });
        lens.append('material', 'Material\'s', null);
        lens.append('thin', 'Thin', null);
        lens.append('thick', 'Thick', null);
        lens.connect('notify::active-name', () => {
            const name = lens.get_active_name();
            if (name === 'thin')
                context.set_lens(Glass.Lens.THIN);
            else if (name === 'thick')
                context.set_lens(Glass.Lens.THICK);
            else
                for (const key of [Glass.PARAM_MAX_Z, Glass.PARAM_PROFILE_SHAPE_N, Glass.PARAM_DISPLACEMENT_SCALE])
                    context.reset_param(key);
        });

        const tintColor = new TintColorRow(context);
        for (const [group, keys] of GROUPS) {
            list.append(new Gtk.Label({ label: group, xalign: 0, css_classes: ['heading'], margin_start: 12, margin_top: 8 }));
            if (group === 'Lens')
                list.append(lens);
            if (group === 'Tint')
                list.append(tintColor.widget);
            for (const key of keys) {
                const row = new ParamRow(context, key);
                rows.push(row);
                list.append(row.widget);
            }
        }
        const resetAll = new Gtk.Button({ label: 'Reset all', halign: Gtk.Align.CENTER, margin_top: 8 });
        resetAll.connect('clicked', () => {
            for (const key of context.list_params())
                context.reset_param(key);
            context.set_tint_color(null);
        });
        list.append(resetAll);
        context.connect('changed', () => {
            rows.forEach(r => r.sync());
            tintColor.sync();
        });

        const inspector = new Glass.Panel({
            child: new Gtk.ScrolledWindow({ child: list, hscrollbar_policy: Gtk.PolicyType.NEVER, propagate_natural_height: false }),
            material: Glass.Material.THICK,
            corner_radius: 18,
            width_request: 340,
            halign: Gtk.Align.END,
            valign: Gtk.Align.FILL,
            margin_end: 12,
            margin_bottom: 12,
        });
        inspector.add_css_class('glass-sidebar');
        stage.add_overlay(inspector);

        this.toolbar = new Glass.ToolbarView({ content: stage });
        this.header = new Glass.HeaderBar();
        const hud = new Gtk.Label({ label: GLib.getenv('GLASS_DEBUG')?.includes('hud') ? 'HUD on' : 'GLASS_DEBUG=hud for timings',
            css_classes: ['dim-label', 'caption'] });
        this.header.set_title_widget(hud);
        this.toolbar.add_top_bar(this.header);
        bindInset(this.toolbar, 'top-bar-height', inspector, 'margin-top', 0);

        this.canvas.setPattern('photo');
        background.set_active_name('photo');
    }

    setPattern(pattern: Pattern) {
        this.background.set_active_name(pattern);
    }

    setInsetSource(split: Glass.SplitView) {
        bindInset(split, 'content-inset', this.header, 'margin-start', 0);
        this.specimens.forEach(p => bindInset(split, 'content-inset', p, 'margin-start', 40));
    }
}
