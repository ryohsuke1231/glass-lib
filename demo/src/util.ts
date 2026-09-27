// Shared helpers of Glass Gallery: photos, test patterns, bindings.

import Gdk from 'gi://Gdk?version=4.0';
import GdkPixbuf from 'gi://GdkPixbuf?version=2.0';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import GObject from 'gi://GObject';
import Graphene from 'gi://Graphene';
import Gsk from 'gi://Gsk?version=4.0';
import Gtk from 'gi://Gtk?version=4.0';
import Glass from 'gi://Glass?version=1';
import Pango from 'gi://Pango';

// Photos are not shipped (licences, design.md §14): the system wallpapers,
// downscaled once and shared by every picture that shows them. In the
// Flatpak they are the host's, under /run/host (--filesystem=host-os:ro).
const PHOTO_DIRS = ['/usr/share/backgrounds', '/usr/share/backgrounds/gnome',
    '/run/host/usr/share/backgrounds', '/run/host/usr/share/backgrounds/gnome'];
const PHOTO_EXTENSIONS = ['.png', '.jpg', '.jpeg', '.webp'];

let photoCache: Gdk.Texture[] | null = null;

export function loadTexture(path: string, maxSide: number): Gdk.Texture | null {
    try {
        const pixbuf = GdkPixbuf.Pixbuf.new_from_file_at_scale(path, maxSide, maxSide, true);
        const format = pixbuf.get_has_alpha() ? Gdk.MemoryFormat.R8G8B8A8 : Gdk.MemoryFormat.R8G8B8;
        return Gdk.MemoryTexture.new(pixbuf.get_width(), pixbuf.get_height(), format,
            pixbuf.read_pixel_bytes(), pixbuf.get_rowstride());
    } catch (e) {
        return null;
    }
}

export function photos(folder: string | null = null, max = 12): Gdk.Texture[] {
    if (folder === null && photoCache !== null)
        return photoCache;

    const textures: Gdk.Texture[] = [];
    for (const dirPath of folder ? [folder] : PHOTO_DIRS) {
        const dir = Gio.File.new_for_path(dirPath);
        let enumerator: Gio.FileEnumerator;
        try {
            enumerator = dir.enumerate_children('standard::name,standard::type', Gio.FileQueryInfoFlags.NONE, null);
        } catch (e) {
            continue;
        }
        let info: Gio.FileInfo | null;
        const names: string[] = [];
        while ((info = enumerator.next_file(null)) !== null) {
            const name = info.get_name();
            if (info.get_file_type() === Gio.FileType.REGULAR &&
                PHOTO_EXTENSIONS.some(ext => name.toLowerCase().endsWith(ext)))
                names.push(name);
        }
        names.sort();
        for (const name of names) {
            if (textures.length >= max)
                break;
            const texture = loadTexture(dir.get_child(name).get_path()!, 960);
            if (texture)
                textures.push(texture);
        }
    }

    if (folder === null)
        photoCache = textures;
    return textures;
}

export type Pattern = 'stripes' | 'checker' | 'gradient' | 'text' | 'photo' | 'animated';

export const PATTERNS: [Pattern, string, string][] = [
    ['stripes', 'Stripes', 'view-continuous-symbolic'],
    ['checker', 'Checker', 'view-grid-symbolic'],
    ['gradient', 'Gradient', 'preferences-color-symbolic'],
    ['text', 'Text', 'font-x-generic-symbolic'],
    ['photo', 'Photo', 'image-x-generic-symbolic'],
    ['animated', 'Moving', 'media-playback-start-symbolic'],
];

const TEXT = 'Glass refracts what is under it. The quick brown fox jumps over the lazy dog. ' +
    'Liquid glass, in-app, same frame as the content. 0123456789 ';

function rgb(r: number, g: number, b: number, a = 1): Gdk.RGBA {
    const c = new Gdk.RGBA();
    c.red = r;
    c.green = g;
    c.blue = b;
    c.alpha = a;
    return c;
}

function rect(x: number, y: number, w: number, h: number): Graphene.Rect {
    return new Graphene.Rect().init(x, y, w, h);
}

function stop(offset: number, color: Gdk.RGBA): Gsk.ColorStop {
    const s = new Gsk.ColorStop();
    s.offset = offset;
    s.color = color;
    return s;
}

// A widget that draws with render nodes (see Canvas).
export const NodeArea = GObject.registerClass(class NodeArea extends Gtk.Widget {
    painter: ((snapshot: Gtk.Snapshot, w: number, h: number) => void) | null = null;

    vfunc_snapshot(snapshot: Gtk.Snapshot) {
        const w = this.get_width(), h = this.get_height();
        if (w > 0 && h > 0 && this.painter)
            this.painter(snapshot, w, h);
    }
});

// A background that exercises the glass: sharp stripes and checks, smooth
// gradients, text, a photo, or blobs that move every frame (paused or
// sped up with an AnimationBar).
//
// Drawn with render nodes, which the GPU draws, not with cairo: a cairo
// node is rasterised on the CPU each time it is rendered - for the window
// and again for every capture the glass makes of it (docs/memo.md 追記17).
export class Canvas {
    readonly widget: Gtk.Stack;
    private area: InstanceType<typeof NodeArea>;
    private textLayout: Pango.Layout | null = null;
    private picture: Gtk.Picture;
    private pattern: Pattern = 'stripes';
    private tick = 0;
    private playing = true;
    private speed = 1;
    private phase = 0;              // seconds of motion shown so far
    private lastFrame = 0;          // frame clock time of the last tick, µs
    private listeners: (() => void)[] = [];

    constructor() {
        this.area = new NodeArea({ hexpand: true, vexpand: true });
        this.area.painter = (snapshot, width, height) => this.draw(snapshot, width, height);
        this.picture = new Gtk.Picture({ content_fit: Gtk.ContentFit.COVER, hexpand: true, vexpand: true });
        const textures = photos();
        if (textures.length > 0)
            this.picture.set_paintable(textures[Math.min(2, textures.length - 1)]);

        this.widget = new Gtk.Stack({ hexpand: true, vexpand: true });
        this.widget.add_named(this.area, 'pattern');
        this.widget.add_named(this.picture, 'photo');
    }

    setPattern(pattern: Pattern) {
        this.pattern = pattern;
        this.widget.set_visible_child_name(pattern === 'photo' ? 'photo' : 'pattern');
        this.updateTick();
        this.area.queue_draw();
        this.listeners.forEach(f => f());
    }

    get moving(): boolean {
        return this.pattern === 'animated';
    }

    get isPlaying(): boolean {
        return this.playing;
    }

    setPlaying(playing: boolean) {
        this.playing = playing;
        this.updateTick();
        this.listeners.forEach(f => f());
    }

    setSpeed(speed: number) {
        this.speed = speed;
    }

    // Called when the pattern or play / pause changes.
    onChanged(listener: () => void) {
        this.listeners.push(listener);
    }

    // The blobs move by frame time times the speed, so pausing and speed
    // changes do not make them jump.
    private updateTick() {
        const wanted = this.pattern === 'animated' && this.playing;
        if (wanted && !this.tick) {
            this.lastFrame = 0;
            this.tick = this.area.add_tick_callback((_area, clock) => {
                const now = clock.get_frame_time();
                if (this.lastFrame)
                    this.phase += (now - this.lastFrame) / 1e6 * this.speed;
                this.lastFrame = now;
                this.area.queue_draw();
                return GLib.SOURCE_CONTINUE;
            });
        } else if (!wanted && this.tick) {
            this.area.remove_tick_callback(this.tick);
            this.tick = 0;
        }
    }

    private draw(s: Gtk.Snapshot, width: number, height: number) {
        const light = rgb(0.97, 0.97, 0.96);
        switch (this.pattern) {
        case 'stripes':
            // A 36 px tile, repeated.
            s.push_repeat(rect(0, 0, width, height), rect(0, 0, 36, height));
            s.append_color(light, rect(0, 0, 36, height));
            s.append_color(rgb(0.07, 0.07, 0.08), rect(0, 0, 18, height));
            s.pop();
            break;
        case 'checker': {
            const c = 24;
            const dark = rgb(0.1, 0.1, 0.12);
            s.push_repeat(rect(0, 0, width, height), rect(0, 0, 2 * c, 2 * c));
            s.append_color(light, rect(0, 0, 2 * c, 2 * c));
            s.append_color(dark, rect(0, 0, c, c));
            s.append_color(dark, rect(c, c, c, c));
            s.pop();
            break;
        }
        case 'gradient':
            s.append_linear_gradient(rect(0, 0, width, height),
                new Graphene.Point().init(0, 0), new Graphene.Point().init(width, height), [
                    stop(0.0, rgb(0.95, 0.35, 0.45)),
                    stop(0.35, rgb(0.98, 0.78, 0.25)),
                    stop(0.65, rgb(0.25, 0.75, 0.65)),
                    stop(1.0, rgb(0.25, 0.35, 0.9)),
                ]);
            break;
        case 'text': {
            s.append_color(rgb(0.99, 0.99, 0.98), rect(0, 0, width, height));
            if (!this.textLayout) {
                this.textLayout = this.area.create_pango_layout(TEXT.repeat(4));
                const font = Pango.FontDescription.from_string('Sans');
                font.set_absolute_size(15 * Pango.SCALE);
                this.textLayout.set_font_description(font);
            }
            const baseline = this.textLayout.get_baseline() / Pango.SCALE;
            const ink = rgb(0.1, 0.1, 0.12);
            let line = 0;
            for (let y = 20; y < height + 20; y += 22, line++) {
                s.save();
                s.translate(new Graphene.Point().init(8 - (line * 37) % 120, y - baseline));
                s.append_layout(this.textLayout, ink);
                s.restore();
            }
            break;
        }
        case 'animated': {
            const t = this.phase;
            s.append_color(rgb(0.08, 0.09, 0.14), rect(0, 0, width, height));
            const blobs: [number, number, number, number, number][] = [
                [0.95, 0.4, 0.3, 0.13, 0.0], [0.3, 0.7, 0.95, 0.17, 1.7],
                [0.98, 0.85, 0.3, 0.11, 3.1], [0.4, 0.9, 0.5, 0.21, 4.4],
            ];
            const radius = Math.min(width, height) * 0.2;
            for (const [r, g, b, speed, phase] of blobs) {
                const x = width * (0.5 + 0.38 * Math.sin(t * speed * 6 + phase));
                const y = height * (0.5 + 0.35 * Math.cos(t * speed * 4.3 + phase * 1.3));
                const bounds = rect(x - radius, y - radius, 2 * radius, 2 * radius);
                const disc = new Gsk.RoundedRect();
                disc.init_from_rect(bounds, radius);
                s.push_rounded_clip(disc);
                s.append_color(rgb(r, g, b), bounds);
                s.pop();
            }
            break;
        }
        default:
            break;
        }
    }
}

// Play / pause and the speed of a canvas's moving background, shown while
// the background moves: on a pane of glass of its own, or as a plain row
// for a pane that is glass already. The slider is the speed's log2: 1x in
// the middle, 0.25x to 4x at the ends.
export class AnimationBar {
    readonly widget: Gtk.Widget;

    constructor(canvas: Canvas, onGlass = true) {
        const play = new Gtk.Button({ css_classes: ['flat', 'circular'], valign: Gtk.Align.CENTER });
        play.connect('clicked', () => canvas.setPlaying(!canvas.isPlaying));

        const speed = Glass.Slider.new_with_range(-2, 2, 0.05);
        speed.set_value(0);
        speed.set_size_request(180, -1);
        speed.set_tooltip_text('Speed');
        const label = new Gtk.Label({ label: '1.00×', width_chars: 5, xalign: 1, css_classes: ['numeric'] });
        speed.get_adjustment().connect('value-changed', () => {
            const value = 2 ** speed.get_value();
            canvas.setSpeed(value);
            label.set_label(`${value.toFixed(2)}×`);
        });

        const box = new Gtk.Box({ spacing: 8, margin_top: 4, margin_bottom: 4, margin_start: 6, margin_end: 16 });
        box.append(play);
        box.append(speed);
        box.append(label);
        this.widget = onGlass ? new Glass.Panel({ child: box, halign: Gtk.Align.CENTER, valign: Gtk.Align.END }) : box;

        const sync = () => {
            this.widget.set_visible(canvas.moving);
            play.set_icon_name(canvas.isPlaying ? 'media-playback-pause-symbolic' : 'media-playback-start-symbolic');
            play.set_tooltip_text(canvas.isPlaying ? 'Pause' : 'Play');
        };
        canvas.onChanged(sync);
        sync();
    }
}

// Binds a numeric property to a margin plus a constant: the glass bars
// report how much of the content they cover, the content pads itself.
export function bindInset(source: GObject.Object, property: string, target: Gtk.Widget,
    margin: 'margin-top' | 'margin-bottom' | 'margin-start' | 'margin-end', extra = 0) {
    source.bind_property_full(property, target, margin, GObject.BindingFlags.SYNC_CREATE,
        (_binding, value: number) => [true, value + extra], null);
}

// GLASS_GALLERY_SCREENSHOT=file.png: renders the window after
// GLASS_GALLERY_DELAY ms (default 2500) and quits (for comparisons).
export function maybeScreenshot(window: Gtk.Window, quit: () => void) {
    const path = GLib.getenv('GLASS_GALLERY_SCREENSHOT');
    if (!path)
        return;
    const delay = Number(GLib.getenv('GLASS_GALLERY_DELAY') ?? '2500');
    GLib.timeout_add(GLib.PRIORITY_DEFAULT, delay, () => {
        const paintable = new Gtk.WidgetPaintable({ widget: window });
        const snapshot = new Gtk.Snapshot();
        paintable.snapshot(snapshot, window.get_width(), window.get_height());
        const node = snapshot.to_node();
        if (node)
            window.get_renderer()!.render_texture(node, null).save_to_png(path);
        print(`screenshot: ${path}`);
        quit();
        return GLib.SOURCE_REMOVE;
    });
}
