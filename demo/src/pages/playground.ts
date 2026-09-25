// Playground: a pane of glass you can drag over backgrounds that make the
// glass work hard, with its material, shape and shadow (design.md §14).

import Graphene from 'gi://Graphene';
import Gtk from 'gi://Gtk?version=4.0';
import Glass from 'gi://Glass?version=1';

import { AnimationBar, bindInset, Canvas, PATTERNS, Pattern } from '../util.js';

export class PlaygroundPage {
    readonly toolbar: Glass.ToolbarView;
    readonly header: Glass.HeaderBar;
    private canvas: Canvas;
    private panel: Glass.Panel;
    private stage: Glass.View;
    private background: Glass.ToggleGroup;
    private controls: Gtk.Box;
    private animation: AnimationBar;

    constructor() {
        this.canvas = new Canvas();

        // A view of its own: the draggable pane is glass over the canvas.
        this.stage = new Glass.View({ content: this.canvas.widget });

        const label = new Gtk.Box({ orientation: Gtk.Orientation.VERTICAL, spacing: 4,
            margin_top: 14, margin_bottom: 14, margin_start: 22, margin_end: 22 });
        label.append(new Gtk.Label({ label: 'Drag me', css_classes: ['title-3'] }));
        label.append(new Gtk.Label({ label: 'glass over the background', css_classes: ['dim-label'] }));
        this.panel = new Glass.Panel({
            child: label,
            halign: Gtk.Align.START,
            valign: Gtk.Align.START,
            margin_start: 340,
            margin_top: 140,
            width_request: 260,
        });
        this.makeDraggable(this.panel);
        this.stage.add_overlay(this.panel);

        const small = new Glass.Panel({
            child: new Gtk.Image({ icon_name: 'starred-symbolic', pixel_size: 22, margin_top: 12,
                margin_bottom: 12, margin_start: 12, margin_end: 12 }),
            halign: Gtk.Align.START,
            valign: Gtk.Align.START,
            margin_start: 380,
            margin_top: 320,
        });
        this.makeDraggable(small);
        this.stage.add_overlay(small);

        this.toolbar = new Glass.ToolbarView({ content: this.stage });
        this.header = new Glass.HeaderBar();
        const background = new Glass.ToggleGroup();
        for (const [name, , icon] of PATTERNS)
            background.append(name, null, icon);
        background.connect('notify::active-name', () =>
            this.canvas.setPattern(background.get_active_name() as Pattern));
        this.header.set_title_widget(background);
        this.background = background;
        this.toolbar.add_top_bar(this.header);

        // Material, shape and shadow, each a glass control of its own.
        const controls = new Gtk.Box({ spacing: 10, halign: Gtk.Align.CENTER, margin_bottom: 18, margin_top: 6 });
        this.controls = controls;
        const material = new Glass.ToggleGroup();
        material.append('regular', 'Regular', null);
        material.append('clear', 'Clear', null);
        material.append('thick', 'Thick', null);
        material.connect('notify::active', () =>
            [this.panel, small].forEach(p => p.set_material(material.get_active() as Glass.Material)));
        controls.append(material);

        const shape = new Glass.ToggleGroup();
        shape.append('capsule', 'Capsule', null);
        shape.append('rounded', 'Rounded', null);
        shape.connect('notify::active-name', () =>
            this.panel.set_corner_radius(shape.get_active_name() === 'rounded' ? 18 : -1));
        controls.append(shape);

        const shadow = new Glass.ToggleGroup();
        shadow.append('shadow', 'Shadow', null);
        shadow.append('none', 'No shadow', null);
        shadow.connect('notify::active-name', () =>
            [this.panel, small].forEach(p => p.set_has_shadow(shadow.get_active_name() === 'shadow')));
        controls.append(shadow);
        this.toolbar.add_bottom_bar(controls);

        this.toolbar.set_top_edge_style(Glass.EdgeStyle.NONE);
        this.toolbar.set_bottom_edge_style(Glass.EdgeStyle.NONE);

        // Play / pause and speed of the moving background, above the bar.
        this.animation = new AnimationBar(this.canvas);
        this.stage.add_overlay(this.animation.widget);
        bindInset(this.toolbar, 'bottom-bar-height', this.animation.widget, 'margin-bottom', 12);

        this.canvas.setPattern('stripes');
    }

    setInsetSource(split: Glass.SplitView) {
        bindInset(split, 'content-inset', this.header, 'margin-start', 0);
        bindInset(split, 'content-inset', this.controls, 'margin-start', 0);
        bindInset(split, 'content-inset', this.animation.widget, 'margin-start', 0);
    }

    setPattern(pattern: Pattern) {
        this.background.set_active_name(pattern);
    }

    // Moves the pane with its margins: the view places it by them. The
    // pointer is followed in the stage's coordinates: the drag's own offsets
    // are in the pane's, which move with the pane, so an event that comes
    // before the pane is laid out anew and one that comes after disagree,
    // and the pane jumps between two places at a fraction of the speed.
    private makeDraggable(panel: Glass.Panel) {
        const drag = new Gtk.GestureDrag();
        let x0 = 0, y0 = 0, px0 = 0, py0 = 0;
        const onStage = (x: number, y: number): [number, number] => {
            const [ok, p] = panel.compute_point(this.stage, new Graphene.Point({ x, y }));
            return ok ? [p.x, p.y] : [x, y];
        };
        drag.connect('drag-begin', (_gesture, x: number, y: number) => {
            x0 = panel.get_margin_start();
            y0 = panel.get_margin_top();
            [px0, py0] = onStage(x, y);
        });
        drag.connect('drag-update', () => {
            const [ok, x, y] = drag.get_point(null);
            if (!ok)
                return;
            const [px, py] = onStage(x, y);
            panel.set_margin_start(Math.max(0, Math.round(x0 + px - px0)));
            panel.set_margin_top(Math.max(0, Math.round(y0 + py - py0)));
        });
        panel.add_controller(drag);
        panel.set_cursor_from_name('grab');
    }
}
