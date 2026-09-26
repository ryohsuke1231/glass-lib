// Shared helpers of Glass Gallery: photos, test patterns, bindings.

import Gdk from 'gi://Gdk?version=4.0';
import GdkPixbuf from 'gi://GdkPixbuf?version=2.0';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import GObject from 'gi://GObject';
import Gtk from 'gi://Gtk?version=4.0';
import Glass from 'gi://Glass?version=1';

import cairo from 'cairo';

// Photos are not shipped (licences, design.md §14): the system wallpapers,
// downscaled once and shared by every picture that shows them. In the
// Flatpak they are the host's, under /run/host (--filesystem=host-os:ro).
const PHOTO_DIRS = ['/usr/share/backgrounds', '/usr/share/backgrounds/gnome',
    '/run/host/usr/share/backgrounds', '/run/host/usr/share/backgrounds/gnome'];
const PHOTO_EXTENSIONS = ['.png', '.jpg', '.jpeg', '.webp'];

let photoCache: Gdk.Texture[] | null = null;

function loadTexture(path: string, maxSide: number): Gdk.Texture | null {
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

// A background that exercises the glass: sharp stripes and checks, smooth
// gradients, text, a photo, or blobs that move every frame (paused or
// sped up with an AnimationBar).
export class Canvas {
    readonly widget: Gtk.Stack;
    private area: Gtk.DrawingArea;
    private picture: Gtk.Picture;
    private pattern: Pattern = 'stripes';
    private tick = 0;
    private playing = true;
    private speed = 1;
    private phase = 0;              // seconds of motion shown so far
    private lastFrame = 0;          // frame clock time of the last tick, µs
    private listeners: (() => void)[] = [];

    constructor() {
        this.area = new Gtk.DrawingArea({ hexpand: true, vexpand: true });
        this.area.set_draw_func((_area, cr, width, height) => {
            this.draw(cr, width, height);
            cr.$dispose();
        });
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

    private draw(cr: cairo.Context, width: number, height: number) {
        switch (this.pattern) {
        case 'stripes':
            cr.setSourceRGB(0.97, 0.97, 0.96);
            cr.paint();
            cr.setSourceRGB(0.07, 0.07, 0.08);
            for (let x = 0; x < width; x += 36)
                cr.rectangle(x, 0, 18, height);
            cr.fill();
            break;
        case 'checker': {
            const s = 24;
            cr.setSourceRGB(0.97, 0.97, 0.96);
            cr.paint();
            cr.setSourceRGB(0.1, 0.1, 0.12);
            for (let y = 0; y < height; y += s)
                for (let x = (y / s) % 2 === 0 ? 0 : s; x < width; x += 2 * s)
                    cr.rectangle(x, y, s, s);
            cr.fill();
            break;
        }
        case 'gradient': {
            const g = new cairo.LinearGradient(0, 0, width, height);
            g.addColorStopRGB(0.0, 0.95, 0.35, 0.45);
            g.addColorStopRGB(0.35, 0.98, 0.78, 0.25);
            g.addColorStopRGB(0.65, 0.25, 0.75, 0.65);
            g.addColorStopRGB(1.0, 0.25, 0.35, 0.9);
            cr.setSource(g);
            cr.paint();
            break;
        }
        case 'text': {
            cr.setSourceRGB(0.99, 0.99, 0.98);
            cr.paint();
            cr.setSourceRGB(0.1, 0.1, 0.12);
            cr.selectFontFace('Sans', 0, 0);
            cr.setFontSize(15);
            let line = 0;
            for (let y = 20; y < height + 20; y += 22, line++) {
                cr.moveTo(8 - (line * 37) % 120, y);
                cr.showText(TEXT.repeat(4));
            }
            break;
        }
        case 'animated': {
            const t = this.phase;
            cr.setSourceRGB(0.08, 0.09, 0.14);
            cr.paint();
            const blobs: [number, number, number, number, number][] = [
                [0.95, 0.4, 0.3, 0.13, 0.0], [0.3, 0.7, 0.95, 0.17, 1.7],
                [0.98, 0.85, 0.3, 0.11, 3.1], [0.4, 0.9, 0.5, 0.21, 4.4],
            ];
            for (const [r, g, b, speed, phase] of blobs) {
                const x = width * (0.5 + 0.38 * Math.sin(t * speed * 6 + phase));
                const y = height * (0.5 + 0.35 * Math.cos(t * speed * 4.3 + phase * 1.3));
                cr.setSourceRGB(r, g, b);
                cr.arc(x, y, Math.min(width, height) * 0.2, 0, 2 * Math.PI);
                cr.fill();
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
