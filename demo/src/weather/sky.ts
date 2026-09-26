// The sky behind the glass, drawn in code (no pictures to license): a
// gradient for the weather and the time of day, with a sun or a moon and
// stars, drifting clouds, rain, snow, fog or lightning. It moves every frame
// (unless animations are off), so the glass refracts something alive.

import GLib from 'gi://GLib';
import Gtk from 'gi://Gtk?version=4.0';

import cairo from 'cairo';

import { SkyKind } from './conditions.js';

type RGB = [number, number, number];

function hex(color: string): RGB {
    const n = parseInt(color.slice(1), 16);
    return [(n >> 16) / 255, ((n >> 8) & 0xff) / 255, (n & 0xff) / 255];
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

export class Sky {
    readonly widget: Gtk.DrawingArea;
    private kind: SkyKind = 'clear';
    private day = true;
    private phase = 0;          // seconds of motion
    private last = 0;           // frame time of the last tick, µs
    private tick = 0;

    constructor() {
        this.widget = new Gtk.DrawingArea({ hexpand: true, vexpand: true });
        this.widget.set_draw_func((_area, cr, width, height) => {
            this.draw(cr, width, height);
            cr.$dispose();
        });
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

    private draw(cr: cairo.Context, w: number, h: number) {
        const t = this.phase;
        const key = `${this.kind}-${this.day ? 'day' : 'night'}`;
        const [top, bottom] = GRADIENTS[key].map(hex);

        const g = new cairo.LinearGradient(0, 0, 0, h);
        g.addColorStopRGB(0, ...top);
        g.addColorStopRGB(1, ...bottom);
        cr.setSource(g);
        cr.paint();

        const clearish = this.kind === 'clear' || this.kind === 'partly';
        if (clearish && this.day)
            this.sun(cr, w, h, t);
        if (clearish && !this.day) {
            this.stars(cr, w, h, t);
            this.moon(cr, w, h);
        }

        switch (this.kind) {
        case 'partly':
            this.clouds(cr, w, h, t, 3, this.day ? 0.6 : 0.3, 1.0);
            break;
        case 'cloudy':
            this.clouds(cr, w, h, t, 7, this.day ? 0.75 : 0.3, 0.9);
            break;
        case 'fog':
            this.fog(cr, w, h, t);
            break;
        case 'rain':
            this.clouds(cr, w, h, t, 6, this.day ? 0.45 : 0.22, 0.7);
            this.rain(cr, w, h, t, 1);
            break;
        case 'snow':
            this.clouds(cr, w, h, t, 5, this.day ? 0.7 : 0.3, 0.95);
            this.snow(cr, w, h, t);
            break;
        case 'storm':
            this.clouds(cr, w, h, t, 7, 0.3, 0.55);
            this.rain(cr, w, h, t, 1.4);
            this.lightning(cr, t);
            break;
        default:
            break;
        }
    }

    private sun(cr: cairo.Context, w: number, h: number, t: number) {
        const x = w * 0.82, y = h * 0.1, r = Math.max(w, h) * (0.55 + 0.02 * Math.sin(t * 0.8));
        const glow = new cairo.RadialGradient(x, y, 0, x, y, r);
        glow.addColorStopRGBA(0, 1, 0.97, 0.86, 0.6);
        glow.addColorStopRGBA(0.12, 1, 0.93, 0.72, 0.3);
        glow.addColorStopRGBA(1, 1, 0.9, 0.7, 0);
        cr.setSource(glow);
        cr.paint();
    }

    private moon(cr: cairo.Context, w: number, h: number) {
        const x = w * 0.78, y = h * 0.13, r = Math.min(w, h) * 0.05;
        const glow = new cairo.RadialGradient(x, y, r, x, y, r * 6);
        glow.addColorStopRGBA(0, 0.8, 0.85, 1, 0.25);
        glow.addColorStopRGBA(1, 0.8, 0.85, 1, 0);
        cr.setSource(glow);
        cr.paint();
        cr.setSourceRGBA(0.96, 0.95, 0.88, 1);
        cr.arc(x, y, r, 0, 2 * Math.PI);
        cr.fill();
    }

    private stars(cr: cairo.Context, w: number, h: number, t: number) {
        for (const [x, y, size, phase, speed] of STARS) {
            cr.setSourceRGBA(1, 1, 1, 0.35 + 0.35 * Math.sin(t * speed + phase));
            cr.arc(x * w, y * h, size, 0, 2 * Math.PI);
            cr.fill();
        }
    }

    // Soft puffs drifting to the right, wrapping around.
    private clouds(cr: cairo.Context, w: number, h: number, t: number, n: number, alpha: number, shade: number) {
        for (const [x0, y, scale, speed] of CLOUDS.slice(0, n)) {
            const size = Math.max(w, 360) * 0.34 * scale;
            const span = w + 2 * size;
            const x = ((x0 * span + t * 9 * speed) % span) - size;
            const cy = y * h;
            for (const [dx, dy, r] of [[0, 0, 0.5], [0.35, -0.12, 0.42], [-0.35, 0.02, 0.38], [0.1, 0.12, 0.45], [0.62, 0.06, 0.3]]) {
                const cx = x + dx * size, cyy = cy + dy * size, rr = r * size;
                const puff = new cairo.RadialGradient(cx, cyy, 0, cx, cyy, rr);
                puff.addColorStopRGBA(0, shade, shade, shade * 1.02, alpha);
                puff.addColorStopRGBA(0.6, shade, shade, shade * 1.02, alpha * 0.55);
                puff.addColorStopRGBA(1, shade, shade, shade, 0);
                cr.setSource(puff);
                cr.arc(cx, cyy, rr, 0, 2 * Math.PI);
                cr.fill();
            }
        }
    }

    private rain(cr: cairo.Context, w: number, h: number, t: number, strength: number) {
        cr.setSourceRGBA(0.8, 0.87, 1, 0.3);
        cr.setLineWidth(1.3);
        const count = Math.round(DROPS.length * Math.min(1, strength));
        for (const [x, y0, speed] of DROPS.slice(0, count)) {
            const y = ((y0 * (h + 40) + t * 620 * speed * strength) % (h + 40)) - 20;
            const px = x * (w + 60) - 30 - (y / h) * 30;
            cr.moveTo(px, y);
            cr.lineTo(px - 4, y + 16);
        }
        cr.stroke();
    }

    private snow(cr: cairo.Context, w: number, h: number, t: number) {
        cr.setSourceRGBA(1, 1, 1, 0.8);
        for (const [x, y0, size, phase] of FLAKES) {
            const y = ((y0 * (h + 20) + t * 28 * size) % (h + 20)) - 10;
            const px = x * w + 14 * Math.sin(t * 0.9 + phase);
            cr.arc(px, y, size, 0, 2 * Math.PI);
            cr.fill();
        }
    }

    private fog(cr: cairo.Context, w: number, h: number, t: number) {
        for (let i = 0; i < 5; i++) {
            const y = h * (0.2 + i * 0.17) + 12 * Math.sin(t * 0.3 + i);
            const band = new cairo.LinearGradient(0, y - 60, 0, y + 60);
            band.addColorStopRGBA(0, 1, 1, 1, 0);
            band.addColorStopRGBA(0.5, 1, 1, 1, this.day ? 0.22 : 0.08);
            band.addColorStopRGBA(1, 1, 1, 1, 0);
            cr.setSource(band);
            cr.rectangle(0, y - 60, w, 120);
            cr.fill();
        }
    }

    // A flash every few seconds, with a flicker.
    private lightning(cr: cairo.Context, t: number) {
        const s = t % 5.5;
        const flash = s < 0.12 ? 1 - s / 0.12 : s > 0.2 && s < 0.3 ? 0.6 * (1 - (s - 0.2) / 0.1) : 0;
        if (flash <= 0)
            return;
        cr.setSourceRGBA(0.85, 0.9, 1, 0.4 * flash);
        cr.paint();
    }
}
