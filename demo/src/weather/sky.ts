// The sky behind the glass, drawn in code (no pictures to license): a
// gradient for the weather and the time of day, with a sun or a moon and
// stars, drifting clouds, rain, snow, fog or lightning. It moves every frame
// (unless animations are off), so the glass refracts something alive.
//
// Drawn with GSK render nodes (gradients and colours), which the GPU draws.
// It was cairo before: a cairo node is rasterised on the CPU every time it
// is rendered - for the window, and again for every capture the glass makes
// of it - and this sky took 11-19 ms per rasterisation at 1400x900, more
// than a frame, twice a frame (docs/memo.md 追記17).

import Gdk from 'gi://Gdk?version=4.0';
import GLib from 'gi://GLib';
import GObject from 'gi://GObject';
import Graphene from 'gi://Graphene';
import Gsk from 'gi://Gsk?version=4.0';
import Gtk from 'gi://Gtk?version=4.0';

import { SkyKind } from './conditions.js';

type RGB = [number, number, number];

function hex(color: string): RGB {
    const n = parseInt(color.slice(1), 16);
    return [(n >> 16) / 255, ((n >> 8) & 0xff) / 255, (n & 0xff) / 255];
}

function rgba(r: number, g: number, b: number, a: number): Gdk.RGBA {
    const c = new Gdk.RGBA();
    c.red = r;
    c.green = g;
    c.blue = b;
    c.alpha = a;
    return c;
}

function stop(offset: number, color: Gdk.RGBA): Gsk.ColorStop {
    const s = new Gsk.ColorStop();
    s.offset = offset;
    s.color = color;
    return s;
}

function rect(x: number, y: number, w: number, h: number): Graphene.Rect {
    return new Graphene.Rect().init(x, y, w, h);
}

function point(x: number, y: number): Graphene.Point {
    return new Graphene.Point().init(x, y);
}

// Top and bottom of the sky, by weather and time of day.
const GRADIENTS: Record<string, [string, string]> = {
    'clear-day': ['#1d5cc2', '#6ba6e4'],
    'partly-day': ['#285fae', '#7eabdb'],
    'cloudy-day': ['#5f7188', '#a2b0bf'],
    'fog-day': ['#858f9a', '#c4cad0'],
    'rain-day': ['#3f4d62', '#728598'],
    'snow-day': ['#6b809b', '#bccadb'],
    'storm-day': ['#242b3a', '#4d576b'],
    'clear-night': ['#060b20', '#22305e'],
    'partly-night': ['#0b1230', '#2b3761'],
    'cloudy-night': ['#151b27', '#353e4f'],
    'fog-night': ['#1c222b', '#3c444f'],
    'rain-night': ['#0e131c', '#29323f'],
    'snow-night': ['#182031', '#445269'],
    'storm-night': ['#0a0d15', '#252b39'],
};

// A small seeded generator, so stars and drops stay where they are.
function random(seed: number): () => number {
    return () => {
        seed = (seed + 0x6d2b79f5) | 0;
        let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
        t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
        return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
    };
}

const rand = random(1231);
const STARS = Array.from({ length: 140 }, () => [rand(), rand() * 0.75, 0.4 + rand() * 1.2, rand() * 6.3, 0.5 + rand() * 2]);
const DROPS = Array.from({ length: 160 }, () => [rand(), rand(), 0.7 + rand() * 0.6]);
const FLAKES = Array.from({ length: 110 }, () => [rand(), rand(), 1 + rand() * 2.2, rand() * 6.3]);
const CLOUDS = Array.from({ length: 7 }, (_, i) => [rand(), 0.06 + (i % 4) * 0.13 + rand() * 0.05, 0.7 + rand() * 0.6, 0.6 + rand() * 0.8]);
const PUFFS = [[0, 0, 0.5], [0.35, -0.12, 0.42], [-0.35, 0.02, 0.38], [0.1, 0.12, 0.45], [0.62, 0.06, 0.3]];

// A rain drop runs from (x, y) to (x - 4, y + 16): the drops are drawn as
// thin upright bars in a frame turned by this much (clockwise).
const RAIN_LENGTH = Math.hypot(4, 16);
const RAIN_SIN = 4 / RAIN_LENGTH;
const RAIN_COS = 16 / RAIN_LENGTH;
const RAIN_DEGREES = Math.atan2(4, 16) * 180 / Math.PI;

// The widget: asks the Sky for its nodes.
const SkyArea = GObject.registerClass(class SkyArea extends Gtk.Widget {
    painter: ((snapshot: Gtk.Snapshot, w: number, h: number) => void) | null = null;

    vfunc_snapshot(snapshot: Gtk.Snapshot) {
        const w = this.get_width(), h = this.get_height();
        if (w > 0 && h > 0 && this.painter)
            this.painter(snapshot, w, h);
    }
});

export class Sky {
    readonly widget: Gtk.Widget;
    private kind: SkyKind = 'clear';
    private day = true;
    private phase = 0;          // seconds of motion
    private last = 0;           // frame time of the last tick, µs
    private tick = 0;

    constructor() {
        const area = new SkyArea({ hexpand: true, vexpand: true });
        area.painter = (snapshot, w, h) => this.draw(snapshot, w, h);
        this.widget = area;
        this.widget.connect('map', () => this.updateTick());
        this.widget.connect('unmap', () => this.updateTick());
        const settings = Gtk.Settings.get_default();
        settings?.connect('notify::gtk-enable-animations', () => this.updateTick());
    }

    set(kind: SkyKind, day: boolean) {
        this.kind = kind;
        this.day = day;
        this.widget.queue_draw();
    }

    // Light enough that dark text would read better (for the hero's text).
    get isBright(): boolean {
        return this.day && (this.kind === 'fog' || this.kind === 'snow');
    }

    private updateTick() {
        const animate = this.widget.get_mapped() &&
            (Gtk.Settings.get_default()?.gtk_enable_animations ?? true);
        if (animate && !this.tick) {
            this.last = 0;
            this.tick = this.widget.add_tick_callback((_widget, clock) => {
                const now = clock.get_frame_time();
                if (this.last)
                    this.phase += (now - this.last) / 1e6;
                this.last = now;
                this.widget.queue_draw();
                return GLib.SOURCE_CONTINUE;
            });
        } else if (!animate && this.tick) {
            this.widget.remove_tick_callback(this.tick);
            this.tick = 0;
        }
    }

    draw(s: Gtk.Snapshot, w: number, h: number) {
        const t = this.phase;
        const key = `${this.kind}-${this.day ? 'day' : 'night'}`;
        const [top, bottom] = GRADIENTS[key].map(hex);

        s.append_linear_gradient(rect(0, 0, w, h), point(0, 0), point(0, h),
            [stop(0, rgba(...top, 1)), stop(1, rgba(...bottom, 1))]);

        const clearish = this.kind === 'clear' || this.kind === 'partly';
        if (clearish && this.day)
            this.sun(s, w, h, t);
        if (clearish && !this.day) {
            this.stars(s, w, h, t);
            this.moon(s, w, h);
        }

        switch (this.kind) {
        case 'partly':
            this.clouds(s, w, h, t, 3, this.day ? 0.6 : 0.3, 1.0);
            break;
        case 'cloudy':
            this.clouds(s, w, h, t, 7, this.day ? 0.75 : 0.3, 0.9);
            break;
        case 'fog':
            this.fog(s, w, h, t);
            break;
        case 'rain':
            this.clouds(s, w, h, t, 6, this.day ? 0.45 : 0.22, 0.7);
            this.rain(s, w, h, t, 1);
            break;
        case 'snow':
            this.clouds(s, w, h, t, 5, this.day ? 0.7 : 0.3, 0.95);
            this.snow(s, w, h, t);
            break;
        case 'storm':
            this.clouds(s, w, h, t, 7, 0.3, 0.55);
            this.rain(s, w, h, t, 1.4);
            this.lightning(s, w, h, t);
            break;
        default:
            break;
        }
    }

    // A glow over the whole sky, from the top right.
    private sun(s: Gtk.Snapshot, w: number, h: number, t: number) {
        const x = w * 0.82, y = h * 0.1, r = Math.max(w, h) * (0.55 + 0.02 * Math.sin(t * 0.8));
        s.append_radial_gradient(rect(0, 0, w, h), point(x, y), r, r, 0, 1, [
            stop(0, rgba(1, 0.97, 0.86, 0.6)),
            stop(0.12, rgba(1, 0.93, 0.72, 0.3)),
            stop(1, rgba(1, 0.9, 0.7, 0)),
        ]);
    }

    private moon(s: Gtk.Snapshot, w: number, h: number) {
        const x = w * 0.78, y = h * 0.13, r = Math.min(w, h) * 0.05;
        // The glow starts at the disc's edge and fades out at six radii.
        s.append_radial_gradient(rect(x - 6 * r, y - 6 * r, 12 * r, 12 * r), point(x, y), 6 * r, 6 * r, 1 / 6, 1, [
            stop(0, rgba(0.8, 0.85, 1, 0.25)),
            stop(1, rgba(0.8, 0.85, 1, 0)),
        ]);
        this.disc(s, x, y, r, rgba(0.96, 0.95, 0.88, 1));
    }

    private disc(s: Gtk.Snapshot, x: number, y: number, r: number, color: Gdk.RGBA) {
        const bounds = rect(x - r, y - r, 2 * r, 2 * r);
        const rounded = new Gsk.RoundedRect();
        rounded.init_from_rect(bounds, r);
        s.push_rounded_clip(rounded);
        s.append_color(color, bounds);
        s.pop();
    }

    // Soft dots: a radial gradient that is solid to 60% of the radius.
    private dot(s: Gtk.Snapshot, x: number, y: number, r: number, r0: number, g0: number, b0: number, a: number) {
        s.append_radial_gradient(rect(x - r, y - r, 2 * r, 2 * r), point(x, y), r, r, 0, 1, [
            stop(0, rgba(r0, g0, b0, a)),
            stop(0.6, rgba(r0, g0, b0, a)),
            stop(1, rgba(r0, g0, b0, 0)),
        ]);
    }

    private stars(s: Gtk.Snapshot, w: number, h: number, t: number) {
        for (const [x, y, size, phase, speed] of STARS)
            this.dot(s, x * w, y * h, size * 1.3, 1, 1, 1, 0.35 + 0.35 * Math.sin(t * speed + phase));
    }

    // Soft puffs drifting to the right, wrapping around.
    private clouds(s: Gtk.Snapshot, w: number, h: number, t: number, n: number, alpha: number, shade: number) {
        const inner = rgba(shade, shade, shade * 1.02, alpha);
        const middle = rgba(shade, shade, shade * 1.02, alpha * 0.55);
        const outer = rgba(shade, shade, shade, 0);
        const stops = [stop(0, inner), stop(0.6, middle), stop(1, outer)];
        for (const [x0, y, scale, speed] of CLOUDS.slice(0, n)) {
            const size = Math.max(w, 360) * 0.34 * scale;
            const span = w + 2 * size;
            const x = ((x0 * span + t * 9 * speed) % span) - size;
            const cy = y * h;
            for (const [dx, dy, r] of PUFFS) {
                const cx = x + dx * size, cyy = cy + dy * size, rr = r * size;
                s.append_radial_gradient(rect(cx - rr, cyy - rr, 2 * rr, 2 * rr), point(cx, cyy), rr, rr, 0, 1, stops);
            }
        }
    }

    private rain(s: Gtk.Snapshot, w: number, h: number, t: number, strength: number) {
        const color = rgba(0.8, 0.87, 1, 0.3);
        const count = Math.round(DROPS.length * Math.min(1, strength));
        s.save();
        s.rotate(RAIN_DEGREES);
        for (const [x, y0, speed] of DROPS.slice(0, count)) {
            const y = ((y0 * (h + 40) + t * 620 * speed * strength) % (h + 40)) - 20;
            const px = x * (w + 60) - 30 - (y / h) * 30;
            // The drop's top in the turned frame.
            const u = px * RAIN_COS + y * RAIN_SIN;
            const v = -px * RAIN_SIN + y * RAIN_COS;
            s.append_color(color, rect(u - 0.65, v, 1.3, RAIN_LENGTH));
        }
        s.restore();
    }

    private snow(s: Gtk.Snapshot, w: number, h: number, t: number) {
        for (const [x, y0, size, phase] of FLAKES) {
            const y = ((y0 * (h + 20) + t * 28 * size) % (h + 20)) - 10;
            const px = x * w + 14 * Math.sin(t * 0.9 + phase);
            this.dot(s, px, y, size * 1.25, 1, 1, 1, 0.8);
        }
    }

    private fog(s: Gtk.Snapshot, w: number, h: number, t: number) {
        const peak = rgba(1, 1, 1, this.day ? 0.22 : 0.08);
        const clear = rgba(1, 1, 1, 0);
        for (let i = 0; i < 5; i++) {
            const y = h * (0.2 + i * 0.17) + 12 * Math.sin(t * 0.3 + i);
            s.append_linear_gradient(rect(0, y - 60, w, 120), point(0, y - 60), point(0, y + 60),
                [stop(0, clear), stop(0.5, peak), stop(1, clear)]);
        }
    }

    // A flash every few seconds, with a flicker.
    private lightning(s: Gtk.Snapshot, w: number, h: number, t: number) {
        const p = t % 5.5;
        const flash = p < 0.12 ? 1 - p / 0.12 : p > 0.2 && p < 0.3 ? 0.6 * (1 - (p - 0.2) / 0.1) : 0;
        if (flash <= 0)
            return;
        s.append_color(rgba(0.85, 0.9, 1, 0.4 * flash), rect(0, 0, w, h));
    }
}
