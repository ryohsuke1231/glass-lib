// Glass Weather's window, laid out like macOS's Weather. From the back:
//
//   Glass.SplitView              the places, on a sidebar of thick glass
//   └ Glass.ToolbarView          the header floats over the weather
//     └ Glass.View               the cards are clear glass over the sky
//       ├ content: Sky           drawn in code, moving
//       └ overlay: ScrolledWindow  the hero text and the cards
//
// Two views for the weather, because a view draws every glass body before
// any foreground: the header's glass is in the outer view, so it refracts
// the cards and their text as they scroll under it (design.md §6.7, §7.6).
//
//   ┌─────────┬──────────────────────────────────────────────┐
//   │ search  │              (🔍) (⟳) (⋯)                     │
//   │ Tokyo   │             Tokyo  18°  Drizzle               │
//   │ London  │ [ hourly forecast, 48 hours, scrolls sideways ]│
//   │ …       │ [ 10 days ] [ feels like ] [ wind          ]  │
//   │         │ [         ] [ rain       ] [ sunrise       ]  │
//   └─────────┴──────────────────────────────────────────────┘

import Adw from 'gi://Adw?version=1';
import Gdk from 'gi://Gdk?version=4.0';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import GObject from 'gi://GObject';
import Graphene from 'gi://Graphene?version=1.0';
import Gsk from 'gi://Gsk?version=4.0';
import Gtk from 'gi://Gtk?version=4.0';
import Glass from 'gi://Glass?version=1';

import cairo from 'cairo';

import { Day, Forecast, getForecast, Place, Result, sampleForecast, searchPlaces } from './api.js';
import { condition, iconFor, SkyKind } from './conditions.js';
import { Sky } from './sky.js';

const APP_ID = 'io.github.ryohsuke1231.GlassWeather';
const REFRESH_MS = 15 * 60 * 1000;
const DEFAULT_PLACES: Place[] = [
    { name: 'Tokyo', region: 'Tokyo, Japan', latitude: 35.6895, longitude: 139.6917 },
    { name: 'London', region: 'England, United Kingdom', latitude: 51.5085, longitude: -0.1257 },
    { name: 'New York', region: 'New York, United States', latitude: 40.7143, longitude: -74.006 },
];

const CSS = `
.weather-hero { color: white; }
.weather-hero label { text-shadow: 0 1px 6px rgb(0 0 0 / 38%); }
.weather-hero.on-bright { color: #17212e; }
.weather-hero.on-bright label { text-shadow: none; }
.weather-hero .city { font-size: 30px; }
.weather-hero .temperature { font-size: 92px; font-weight: 200; margin: -10px 0 -8px 18px; }
.weather-hero .condition { font-size: 19px; font-weight: 600; }
.weather-hero .hilo { font-size: 17px; font-weight: 600; }
.weather-hero .notice { font-size: 12px; opacity: 0.85; margin-top: 4px; }
.weather-card .card-heading { font-size: 11px; font-weight: 800; letter-spacing: 0.05em; opacity: 0.7; }
.weather-card .dim { opacity: 0.7; }
.weather-card separator { background: alpha(currentColor, 0.18); }
.weather-card .precip { color: #a6d8ff; font-size: 11px; font-weight: 800; }
glasspanel.glass-light.weather-card .precip { color: #1463b0; }
.weather-card .value { font-size: 40px; font-weight: 300; }
.weather-card .detail { font-size: 15px; opacity: 0.8; }
.weather-card .rain-hour { font-size: 12px; opacity: 0.7; }
.weather-hour .temp { font-weight: 700; }
.weather-day .day { font-weight: 700; }
.weather-day .temp { font-weight: 700; }
.weather-attribution { color: white; font-size: 12px; }
.weather-attribution.on-bright { color: #17212e; }
.weather-place .place-time { font-size: 12px; opacity: 0.7; }
.weather-place .place-temp { font-size: 24px; font-weight: 300; }
.weather-result .region { font-size: 12px; opacity: 0.7; }
`;

interface State {
    place: Place;
    places: Place[];
    fahrenheit: boolean;
}

const placeKey = (p: Place) => `${p.latitude.toFixed(4)},${p.longitude.toFixed(4)}`;

function statePath(): string {
    return GLib.build_filenamev([GLib.get_user_config_dir(), 'glass-weather', 'state.json']);
}

function loadState(): State {
    try {
        const [, bytes] = GLib.file_get_contents(statePath());
        const saved = JSON.parse(new TextDecoder().decode(bytes));
        if (saved.place) {
            // Before the sidebar the places were "recents".
            const places: Place[] = saved.places ?? saved.recents ?? [];
            if (!places.some(p => placeKey(p) === placeKey(saved.place)))
                places.unshift(saved.place);
            return { place: saved.place, places, fahrenheit: !!saved.fahrenheit };
        }
    } catch (e) {
        // First start.
    }
    return { place: DEFAULT_PLACES[0], places: [...DEFAULT_PLACES], fahrenheit: false };
}

function saveState(state: State) {
    try {
        GLib.mkdir_with_parents(GLib.path_get_dirname(statePath()), 0o755);
        GLib.file_set_contents(statePath(), JSON.stringify(state));
    } catch (e) {
        logError(e as Error, 'glass-weather: could not save the state');
    }
}

// Blue when cold, through green and yellow, to red when hot (°C).
const TEMP_COLORS: [number, number, number, number][] = [
    [-10, 0.38, 0.62, 1.0], [5, 0.35, 0.78, 0.98], [15, 0.39, 0.82, 0.6],
    [22, 1.0, 0.84, 0.04], [30, 1.0, 0.62, 0.04], [38, 1.0, 0.27, 0.23],
];

function tempColor(t: number): [number, number, number] {
    if (t <= TEMP_COLORS[0][0])
        return TEMP_COLORS[0].slice(1) as [number, number, number];
    for (let i = 1; i < TEMP_COLORS.length; i++) {
        const [t1, ...c1] = TEMP_COLORS[i];
        const [t0, ...c0] = TEMP_COLORS[i - 1];
        if (t <= t1) {
            const k = (t - t0) / (t1 - t0);
            return c0.map((c, j) => c + (c1[j] - c) * k) as [number, number, number];
        }
    }
    return TEMP_COLORS[TEMP_COLORS.length - 1].slice(1) as [number, number, number];
}

function roundedBar(cr: cairo.Context, x: number, y: number, w: number, h: number) {
    const r = h / 2;
    cr.newSubPath();
    cr.arc(x + w - r, y + r, r, -Math.PI / 2, Math.PI / 2);
    cr.arc(x + r, y + r, r, Math.PI / 2, 3 * Math.PI / 2);
    cr.closePath();
}

function roundedColumn(cr: cairo.Context, x: number, y: number, w: number, h: number) {
    const r = Math.min(w / 2, h / 2);
    cr.newSubPath();
    cr.arc(x + w - r, y + r, r, -Math.PI / 2, 0);
    cr.arc(x + w - r, y + h - r, r, 0, Math.PI / 2);
    cr.arc(x + r, y + h - r, r, Math.PI / 2, Math.PI);
    cr.arc(x + r, y + r, r, Math.PI, 3 * Math.PI / 2);
    cr.closePath();
}

const COMPASS = ['N', 'NE', 'E', 'SE', 'S', 'SW', 'W', 'NW'];
const WEEKDAYS = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];
const pad = (n: number) => String(n).padStart(2, '0');

function clockTime(iso: string): string {
    return iso.slice(11, 16);
}

// The time now where the place is.
function localTime(utcOffset: number): string {
    const d = new Date(Date.now() + utcOffset * 1000);
    return `${pad(d.getUTCHours())}:${pad(d.getUTCMinutes())}`;
}

function weekday(date: string, index: number): string {
    if (index === 0)
        return 'Today';
    const [y, m, d] = date.split('-').map(Number);
    return WEEKDAYS[new Date(Date.UTC(y, m - 1, d)).getUTCDay()];
}

function removeAll(box: Gtk.Box | Gtk.ListBox) {
    for (let child = box.get_first_child(); child; child = box.get_first_child())
        box.remove(child);
}

// A card: a pane of clear glass with a small heading.
function card(icon: string, heading: string, body: Gtk.Widget): Glass.Panel {
    const box = new Gtk.Box({ orientation: Gtk.Orientation.VERTICAL, spacing: 8,
        margin_top: 12, margin_bottom: 12, margin_start: 16, margin_end: 16 });
    const title = new Gtk.Box({ spacing: 6, css_classes: ['card-heading'] });
    title.append(new Gtk.Image({ icon_name: icon, pixel_size: 12 }));
    title.append(new Gtk.Label({ label: heading.toUpperCase(), xalign: 0 }));
    box.append(title);
    box.append(body);
    return new Glass.Panel({ child: box, corner_radius: 22, material: Glass.Material.CLEAR,
        css_classes: ['weather-card'] });
}

// Two children side by side, the first on a third of the width and the
// second on two thirds, or one above the other when the first would not fit
// on a third. Its minimum width is the stacked layout's, so the window can
// always get narrower (a homogeneous GtkGrid would demand three times the
// widest child).
const GAP = 12;
const WIDE_MIN = 720;

const ThirdsLayout = GObject.registerClass(class ThirdsLayout extends Gtk.Widget {
    private first: Gtk.Widget;
    private second: Gtk.Widget;

    constructor(first: Gtk.Widget, second: Gtk.Widget) {
        super();
        this.first = first;
        this.second = second;
        first.set_parent(this);
        second.set_parent(this);
    }

    private split(width: number): [number, number] | null {
        const firstWidth = Math.floor((width - GAP) / 3);
        if (width < WIDE_MIN || firstWidth < this.first.measure(Gtk.Orientation.HORIZONTAL, -1)[0])
            return null;
        return [firstWidth, width - GAP - firstWidth];
    }

    vfunc_get_request_mode(): Gtk.SizeRequestMode {
        return Gtk.SizeRequestMode.HEIGHT_FOR_WIDTH;
    }

    vfunc_measure(orientation: Gtk.Orientation, forSize: number): [number, number, number, number] {
        const [a, b] = [this.first, this.second];
        if (orientation === Gtk.Orientation.HORIZONTAL) {
            const [amin, anat] = a.measure(orientation, -1);
            const [bmin, bnat] = b.measure(orientation, -1);
            return [Math.max(amin, bmin), anat + GAP + bnat, -1, -1];
        }
        const split = forSize > 0 ? this.split(forSize) : null;
        if (split) {
            const [amin, anat] = a.measure(orientation, split[0]);
            const [bmin, bnat] = b.measure(orientation, split[1]);
            return [Math.max(amin, bmin), Math.max(anat, bnat), -1, -1];
        }
        const [amin, anat] = a.measure(orientation, forSize);
        const [bmin, bnat] = b.measure(orientation, forSize);
        return [amin + GAP + bmin, anat + GAP + bnat, -1, -1];
    }

    vfunc_size_allocate(width: number, height: number, _baseline: number) {
        const split = this.split(width);
        if (split) {
            this.first.allocate(split[0], height, -1, null);
            this.second.allocate(split[1], height, -1,
                new Gsk.Transform().translate(new Graphene.Point({ x: split[0] + GAP, y: 0 })));
        } else {
            const firstHeight = this.first.measure(Gtk.Orientation.VERTICAL, width)[1];
            this.first.allocate(width, firstHeight, -1, null);
            this.second.allocate(width, Math.max(height - firstHeight - GAP, 0), -1,
                new Gsk.Transform().translate(new Graphene.Point({ x: 0, y: firstHeight + GAP })));
        }
    }

    vfunc_dispose() {
        this.first?.unparent();
        this.second?.unparent();
        super.vfunc_dispose();
    }
});

// A tile: a heading, a big value, a line under it, and a picture that
// takes the rest of the room.
class Tile {
    readonly panel: Glass.Panel;
    readonly value = new Gtk.Label({ xalign: 0, css_classes: ['value'] });
    readonly detail = new Gtk.Label({ xalign: 0, wrap: true, css_classes: ['detail'] });

    constructor(icon: string, heading: string, visual: Gtk.Widget) {
        const body = new Gtk.Box({ orientation: Gtk.Orientation.VERTICAL, spacing: 4, vexpand: true });
        body.append(this.value);
        body.append(this.detail);
        visual.set_vexpand(true);
        visual.set_margin_top(6);
        body.append(visual);
        this.panel = card(icon, heading, body);
    }
}

// A strip that scrolls sideways, with its scrollbar under it (when the
// strip does not fit) as a widget of its own, so that it has room: a
// scrolled window's natural height does not count a scrollbar that may
// show, which then covered the strip's last row.
function sidewaysStrip(child: Gtk.Widget): Gtk.Box {
    const scroller = new Gtk.ScrolledWindow({ child, vscrollbar_policy: Gtk.PolicyType.NEVER,
        hscrollbar_policy: Gtk.PolicyType.EXTERNAL, propagate_natural_height: true });
    const adjustment = scroller.get_hadjustment();
    const scrollbar = new Gtk.Scrollbar({ orientation: Gtk.Orientation.HORIZONTAL, adjustment, visible: false });
    adjustment.connect('changed', () =>
        scrollbar.set_visible(adjustment.get_upper() - adjustment.get_page_size() > 1));
    const box = new Gtk.Box({ orientation: Gtk.Orientation.VERTICAL, spacing: 6 });
    box.append(scroller);
    box.append(scrollbar);
    return box;
}

// Minutes since midnight of a local ISO time ("2026-09-27T05:32").
function minutes(iso: string): number {
    return Number(iso.slice(11, 13)) * 60 + Number(iso.slice(14, 16));
}

// A drawing in the text colour (it follows the glass's light / dark).
function drawing(draw: (cr: cairo.Context, w: number, h: number, c: Gdk.RGBA) => void): Gtk.DrawingArea {
    const area = new Gtk.DrawingArea({ hexpand: true });
    area.set_draw_func((a, cr, w, h) => {
        draw(cr, w, h, a.get_color());
        cr.$dispose();
    });
    return area;
}

// A place in the sidebar: its name and local time, its temperature, and a
// button to remove it that shows on hover.
class PlaceRow {
    readonly row: Gtk.ListBoxRow;
    private time = new Gtk.Label({ xalign: 0, css_classes: ['place-time'] });
    private temp = new Gtk.Label({ valign: Gtk.Align.CENTER, css_classes: ['place-temp'] });
    private remove: Gtk.Button;

    constructor(readonly place: Place, onRemove: (place: Place) => void) {
        const text = new Gtk.Box({ orientation: Gtk.Orientation.VERTICAL, hexpand: true, valign: Gtk.Align.CENTER });
        text.append(new Gtk.Label({ label: place.name, xalign: 0, ellipsize: 3, css_classes: ['heading'] }));
        text.append(this.time);
        this.remove = new Gtk.Button({ icon_name: 'window-close-symbolic', valign: Gtk.Align.CENTER,
            tooltip_text: `Remove ${place.name}`, opacity: 0, can_target: false, css_classes: ['flat', 'circular'] });
        this.remove.connect('clicked', () => onRemove(place));

        const box = new Gtk.Box({ spacing: 8, margin_top: 6, margin_bottom: 6, margin_start: 4,
            css_classes: ['weather-place'] });
        box.append(text);
        box.append(this.temp);
        box.append(this.remove);
        this.row = new Gtk.ListBoxRow({ child: box });

        const motion = new Gtk.EventControllerMotion();
        motion.connect('enter', () => this.setRemovable(true));
        motion.connect('leave', () => this.setRemovable(false));
        this.row.add_controller(motion);
    }

    private setRemovable(shown: boolean) {
        this.remove.set_opacity(shown ? 1 : 0);
        this.remove.set_can_target(shown);
    }

    update(forecast: Forecast | undefined, temp: (celsius: number) => string) {
        this.time.set_label(forecast ? localTime(forecast.utcOffset) : '—');
        this.temp.set_label(forecast ? temp(forecast.current.temperature) : '');
    }
}

export class WeatherWindow {
    readonly window: Adw.ApplicationWindow;
    private state = loadState();
    private results = new Map<string, Result>();
    private preview: string = GLib.getenv('GLASS_WEATHER_SKY') ?? 'live';
    private offline = !!GLib.getenv('GLASS_WEATHER_OFFLINE');
    private loading = new Map<string, Gio.Cancellable>();
    private searching: Gio.Cancellable | null = null;

    private sky = new Sky();
    private split: Glass.SplitView;
    private toolbar: Glass.ToolbarView;
    private hero!: Gtk.Box;
    private city = new Gtk.Label({ css_classes: ['city'], ellipsize: 3 });
    private temperature = new Gtk.Label({ css_classes: ['temperature'] });
    private conditionLabel = new Gtk.Label({ css_classes: ['condition'] });
    private hilo = new Gtk.Label({ css_classes: ['hilo'] });
    private notice = new Gtk.Label({ css_classes: ['notice'], wrap: true, justify: Gtk.Justification.CENTER });
    private spinner = new Adw.Spinner({ width_request: 32, height_request: 32 });
    private hours = new Gtk.Box({ homogeneous: true, hexpand: true });
    private days = new Gtk.Box({ orientation: Gtk.Orientation.VERTICAL });
    private tiles: Record<string, Tile> = {};
    private rainHours = new Gtk.Box({ homogeneous: true, hexpand: true });
    private humidity = 0;           // %
    private windFrom = 0;           // degrees
    private sun = { rise: 360, set: 1080, now: 720 };   // minutes since midnight

    // The wind: a compass with an arrow the way the wind blows.
    private windDial = drawing((cr, w, h, c) => {
        const r = Math.min(w, h) / 2 - 12, cx = w / 2, cy = h / 2;
        if (r < 12)
            return;
        cr.setSourceRGBA(c.red, c.green, c.blue, 0.25);
        cr.setLineWidth(1.5);
        cr.arc(cx, cy, r, 0, 2 * Math.PI);
        cr.stroke();
        for (let i = 0; i < 36; i++) {
            const a = i * Math.PI / 18, long = i % 9 === 0;
            cr.setSourceRGBA(c.red, c.green, c.blue, long ? 0.8 : 0.3);
            cr.moveTo(cx + Math.sin(a) * (r - (long ? 9 : 5)), cy - Math.cos(a) * (r - (long ? 9 : 5)));
            cr.lineTo(cx + Math.sin(a) * r, cy - Math.cos(a) * r);
            cr.stroke();
        }
        cr.setSourceRGBA(c.red, c.green, c.blue, 0.7);
        cr.setFontSize(11);
        for (const [label, a] of [['N', 0], ['E', 90], ['S', 180], ['W', 270]] as [string, number][]) {
            const ext = cr.textExtents(label);
            const rad = a * Math.PI / 180, d = r - 20;
            cr.moveTo(cx + Math.sin(rad) * d - ext.width / 2, cy - Math.cos(rad) * d + ext.height / 2);
            cr.showText(label);
        }
        // From where it comes, to where it goes.
        const to = (this.windFrom + 180) * Math.PI / 180, len = r - 26;
        const tipX = cx + Math.sin(to) * len, tipY = cy - Math.cos(to) * len;
        cr.setSourceRGBA(c.red, c.green, c.blue, 0.95);
        cr.setLineWidth(3);
        cr.moveTo(cx - Math.sin(to) * len, cy + Math.cos(to) * len);
        cr.lineTo(tipX, tipY);
        cr.stroke();
        cr.moveTo(tipX, tipY);
        cr.lineTo(tipX - Math.sin(to - 0.5) * 12, tipY + Math.cos(to - 0.5) * 12);
        cr.lineTo(tipX - Math.sin(to + 0.5) * 12, tipY + Math.cos(to + 0.5) * 12);
        cr.closePath();
        cr.fill();
    });

    // The sun: its path from sunrise to sunset over the horizon, with the
    // part it has done drawn solid and the sun where it is now.
    private sunArc = drawing((cr, w, h, c) => {
        const pad = 14, horizon = h - 18, top = 10, x0 = pad, x1 = w - pad;
        if (horizon - top < 20)
            return;
        const point = (t: number): [number, number] =>
            [x0 + (x1 - x0) * t, horizon - (horizon - top) * Math.sin(Math.PI * t)];
        const { rise, set, now } = this.sun;
        const t = Math.min(Math.max((now - rise) / Math.max(set - rise, 1), 0), 1);
        const up = now > rise && now < set;

        cr.setSourceRGBA(c.red, c.green, c.blue, 0.3);
        cr.setLineWidth(1);
        cr.moveTo(0, horizon);
        cr.lineTo(w, horizon);
        cr.stroke();
        cr.setDash([4, 4], 0);
        cr.setLineWidth(2);
        for (let i = 0; i <= 48; i++)
            cr.lineTo(...point(i / 48));
        cr.stroke();
        cr.setDash([], 0);
        if (t > 0) {
            cr.setSourceRGBA(1, 0.78, 0.25, 0.9);
            cr.setLineWidth(3);
            for (let i = 0; i <= 48; i++)
                cr.lineTo(...point(t * i / 48));
            cr.stroke();
        }
        const [sx, sy] = point(t);
        const glow = new cairo.RadialGradient(sx, sy, 0, sx, sy, 18);
        glow.addColorStopRGBA(0, 1, 0.85, 0.35, up ? 0.7 : 0.2);
        glow.addColorStopRGBA(1, 1, 0.85, 0.35, 0);
        cr.setSource(glow);
        cr.arc(sx, sy, 18, 0, 2 * Math.PI);
        cr.fill();
        cr.setSourceRGBA(1, 0.82, 0.3, up ? 1 : 0.45);
        cr.arc(sx, sy, 7, 0, 2 * Math.PI);
        cr.fill();
    });

    // Humidity as a gauge under the "feels like".
    private humidityGauge(): Gtk.Widget {
        const box = new Gtk.Box({ orientation: Gtk.Orientation.VERTICAL, spacing: 6, valign: Gtk.Align.END });
        this.humidityLabel = new Gtk.Label({ xalign: 0, css_classes: ['detail'] });
        box.append(this.humidityLabel);
        const bar = drawing((cr, w, h, c) => {
            cr.setSourceRGBA(c.red, c.green, c.blue, 0.16);
            roundedBar(cr, 0, 0, w, h);
            cr.fill();
            const gradient = new cairo.LinearGradient(0, 0, w, 0);
            gradient.addColorStopRGB(0, 0.55, 0.8, 1);
            gradient.addColorStopRGB(1, 0.15, 0.45, 0.95);
            cr.setSource(gradient);
            roundedBar(cr, 0, 0, Math.max(w * this.humidity / 100, h), h);
            cr.fill();
        });
        bar.set_content_height(8);
        bar.set_vexpand(false);
        box.append(bar);
        this.humidityBar = bar;
        return box;
    }

    private humidityLabel!: Gtk.Label;
    private humidityBar!: Gtk.DrawingArea;
    private attribution!: Gtk.Label;
    private units!: Glass.ToggleGroup;

    // The sidebar: the search field, then the places (or what it found).
    private sidebarSearch!: Glass.SearchEntry;
    private sidebarStack = new Gtk.Stack({ vexpand: true });
    private placeList = new Gtk.ListBox({ css_classes: ['navigation-sidebar'] });
    private foundList = new Gtk.ListBox({ css_classes: ['navigation-sidebar'], selection_mode: Gtk.SelectionMode.NONE });
    private rows = new Map<string, PlaceRow>();

    // The header's search: a button whose glass becomes a field, and what
    // it finds in a glass popover under it.
    private searchButton!: Glass.Button;
    private headerSearch!: Glass.SearchEntry;
    private foundPopover!: Glass.Popover;
    private foundBox = new Gtk.Box({ orientation: Gtk.Orientation.VERTICAL, css_classes: ['glass-menu'] });
    private found: Place[] = [];

    constructor(app: Adw.Application) {
        const provider = new Gtk.CssProvider();
        provider.load_from_string(CSS);
        Gtk.StyleContext.add_provider_for_display(Gdk.Display.get_default()!, provider,
            Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION);

        this.window = new Adw.ApplicationWindow({ application: app, title: 'Glass Weather',
            default_width: 1180, default_height: 800, width_request: 360, height_request: 560 });

        this.toolbar = this.buildWeather();
        const sidebar = this.buildSidebar();
        this.split = new Glass.SplitView({ sidebar, content: this.toolbar });
        this.window.set_content(this.split);
        this.buildHeaders();

        this.addActions();
        this.fillPlaces();
        this.refresh(false);
        this.show();
        GLib.timeout_add(GLib.PRIORITY_DEFAULT, REFRESH_MS, () => {
            this.refresh(false);
            return GLib.SOURCE_CONTINUE;
        });
        // The places' local times.
        GLib.timeout_add_seconds(GLib.PRIORITY_DEFAULT, 20, () => {
            this.updateRows();
            return GLib.SOURCE_CONTINUE;
        });
    }

    // ── Building ──

    private buildWeather(): Glass.ToolbarView {
        // The hero: the place and the weather now, straight on the sky.
        this.hero = new Gtk.Box({ orientation: Gtk.Orientation.VERTICAL, halign: Gtk.Align.CENTER,
            margin_top: 18, margin_bottom: 18, css_classes: ['weather-hero'] });
        for (const w of [this.city, this.spinner, this.temperature, this.conditionLabel, this.hilo, this.notice])
            this.hero.append(w);

        // Hourly: the whole width; a scrollbar when the hours do not fit.
        const hourBody = new Gtk.Box({ orientation: Gtk.Orientation.VERTICAL, spacing: 8 });
        hourBody.append(new Gtk.Separator());
        hourBody.append(sidewaysStrip(this.hours));
        const hourly = card('document-open-recent-symbolic', 'Hourly Forecast', hourBody);

        const daily = card('x-office-calendar-symbolic', '10-Day Forecast', this.days);

        // Four tiles, two by two, each with a picture.
        const tileGrid = new Gtk.Grid({ column_spacing: 12, row_spacing: 12, column_homogeneous: true,
            row_homogeneous: true });
        const rain = new Gtk.Box({ orientation: Gtk.Orientation.VERTICAL, valign: Gtk.Align.END });
        rain.append(sidewaysStrip(this.rainHours));
        const TILES: [string, string, string, Gtk.Widget][] = [
            ['feels', 'temperature-symbolic', 'Feels Like', this.humidityGauge()],
            ['wind', 'weather-windy-symbolic', 'Wind', this.windDial],
            ['rain', 'weather-showers-symbolic', 'Precipitation', rain],
            ['sun', 'daytime-sunset-symbolic', 'Sunrise & Sunset', this.sunArc],
        ];
        TILES.forEach(([key, icon, heading, visual], i) => {
            this.tiles[key] = new Tile(icon, heading, visual);
            tileGrid.attach(this.tiles[key].panel, i % 2, Math.floor(i / 2), 1, 1);
        });

        // The 10 days on the left third, the tiles on the right two thirds;
        // one above the other when there is no room.
        const grid = new ThirdsLayout(daily, tileGrid);

        // Open-Meteo's licence asks for this next to the data. A plain label
        // that opens the site: a theme colours links, which on the sky would
        // be blue on blue.
        this.attribution = new Gtk.Label({ use_markup: true, css_classes: ['weather-attribution'],
            label: '<u>Weather data by Open-Meteo.com</u>', cursor: Gdk.Cursor.new_from_name('pointer', null),
            tooltip_text: 'https://open-meteo.com/' });
        const click = new Gtk.GestureClick();
        click.connect('released', () => new Gtk.UriLauncher({ uri: 'https://open-meteo.com/' })
            .launch(this.window, null, null));
        this.attribution.add_controller(click);

        const column = new Gtk.Box({ orientation: Gtk.Orientation.VERTICAL, spacing: 12,
            margin_start: 16, margin_end: 16, margin_bottom: 24 });
        for (const w of [this.hero, hourly, grid, this.attribution])
            column.append(w);

        const scroller = new Gtk.ScrolledWindow({ hscrollbar_policy: Gtk.PolicyType.NEVER, child: column });
        const view = new Glass.View({ content: this.sky.widget });
        view.add_overlay(scroller);

        const toolbar = new Glass.ToolbarView({ content: view });
        toolbar.set_top_edge_style(Glass.EdgeStyle.NONE);
        toolbar.bind_property_full('top-bar-height', column, 'margin-top', GObject.BindingFlags.SYNC_CREATE,
            (_binding, value: number) => [true, value + 4], null);
        this.column = column;
        return toolbar;
    }

    private column!: Gtk.Box;

    private buildSidebar(): Gtk.Widget {
        // At the top, a search field already open.
        this.sidebarSearch = new Glass.SearchEntry({ placeholder_text: 'Search for a city',
            margin_start: 10, margin_end: 10, margin_top: 2, margin_bottom: 8 });
        this.sidebarSearch.connect('search-changed', () => this.search(this.sidebarSearch.get_text()));
        this.sidebarSearch.connect('activate', () => {
            if (this.found.length > 0)
                this.choose(this.found[0]);
        });
        this.sidebarSearch.connect('stop-search', () => this.sidebarSearch.set_text(''));

        this.placeList.connect('row-activated', (_list, row: Gtk.ListBoxRow) => {
            for (const r of this.rows.values())
                if (r.row === row)
                    this.choose(r.place);
        });
        this.foundList.connect('row-activated', (_list, row: Gtk.ListBoxRow) => {
            const place = this.found[row.get_index()];
            if (place)
                this.choose(place);
        });
        this.sidebarStack.add_named(new Gtk.ScrolledWindow({ child: this.placeList,
            hscrollbar_policy: Gtk.PolicyType.NEVER }), 'places');
        this.sidebarStack.add_named(new Gtk.ScrolledWindow({ child: this.foundList,
            hscrollbar_policy: Gtk.PolicyType.NEVER }), 'found');

        const box = new Gtk.Box({ orientation: Gtk.Orientation.VERTICAL });
        box.append(this.sidebarSearch);
        box.append(this.sidebarStack);

        const sidebar = new Glass.ToolbarView({ content: box });
        sidebar.set_top_edge_style(Glass.EdgeStyle.NONE);
        sidebar.bind_property('top-bar-height', box, 'margin-top', GObject.BindingFlags.SYNC_CREATE);
        this.sidebarToolbar = sidebar;
        return sidebar;
    }

    private sidebarToolbar!: Glass.ToolbarView;

    // The two header bars, and what shows or hides with the sidebar.
    private buildHeaders() {
        const sidebarHeader = new Glass.HeaderBar({ show_title: false, show_end_title_buttons: false });
        const hide = new Gtk.Button({ icon_name: 'sidebar-show-symbolic', tooltip_text: 'Hide the places' });
        hide.connect('clicked', () => this.split.set_show_sidebar(false));
        sidebarHeader.pack_end(hide);
        this.sidebarToolbar.add_top_bar(sidebarHeader);

        const header = new Glass.HeaderBar();
        const show = new Gtk.Button({ icon_name: 'sidebar-show-symbolic', tooltip_text: 'Show the places' });
        show.connect('clicked', () => this.split.set_show_sidebar(true));
        header.pack_start(show);
        this.split.bind_property('show-sidebar', show, 'visible',
            GObject.BindingFlags.SYNC_CREATE | GObject.BindingFlags.INVERT_BOOLEAN);
        this.split.bind_property('show-sidebar', header, 'show-start-title-buttons',
            GObject.BindingFlags.SYNC_CREATE | GObject.BindingFlags.INVERT_BOOLEAN);
        for (const widget of [header as Gtk.Widget, this.column])
            this.split.bind_property_full('content-inset', widget, 'margin-start', GObject.BindingFlags.SYNC_CREATE,
                (_binding, value: number) => [true, value + (widget === this.column ? 16 : 0)], null);

        // Units in the middle.
        this.units = new Glass.ToggleGroup();
        this.units.append('c', '°C', null);
        this.units.append('f', '°F', null);
        this.units.set_active_name(this.state.fahrenheit ? 'f' : 'c');
        this.units.connect('notify::active-name', () => {
            this.state.fahrenheit = this.units.get_active_name() === 'f';
            saveState(this.state);
            this.show();
            this.updateRows();
        });
        header.set_title_widget(this.units);

        // At the end: the menu, and to its left the search, closed: a glass
        // button whose glass becomes the field's (morph-id).
        header.pack_end(new Glass.MenuButton({ icon_name: 'open-menu-symbolic', menu_model: this.menu(),
            tooltip_text: 'Menu' }));
        // Refresh: a piece of glass of its own, left of the menu.
        const refresh = Glass.Button.new_from_icon_name('view-refresh-symbolic');
        refresh.set_tooltip_text('Refresh');
        refresh.set_action_name('win.refresh');
        header.pack_end(refresh);
        this.searchButton = Glass.Button.new_from_icon_name('edit-find-symbolic');
        this.searchButton.set_morph_id('header-search');
        this.searchButton.set_tooltip_text('Search for a city');
        this.searchButton.connect('clicked', () => this.setHeaderSearching(true));
        this.headerSearch = new Glass.SearchEntry({ morph_id: 'header-search', visible: false, width_request: 260,
            placeholder_text: 'Search for a city' });
        this.headerSearch.connect('search-changed', () => this.search(this.headerSearch.get_text()));
        this.headerSearch.connect('activate', () => {
            if (this.found.length > 0)
                this.choose(this.found[0]);
        });
        this.headerSearch.connect('stop-search', () => this.setHeaderSearching(false));
        const focus = new Gtk.EventControllerFocus();
        focus.connect('leave', () => {
            if (this.headerSearch.get_text() === '')
                this.setHeaderSearching(false);
        });
        this.headerSearch.add_controller(focus);
        const searchRow = new Gtk.Box();
        searchRow.append(this.searchButton);
        searchRow.append(this.headerSearch);
        header.pack_end(new Glass.Group({ child: searchRow }));

        // What the header's search finds: a glass popover under the field
        // that does not take the keyboard from it.
        this.foundPopover = new Glass.Popover({ autohide: false, position: Gtk.PositionType.BOTTOM,
            halign: Gtk.Align.END });
        this.foundPopover.add_css_class('menu');
        this.foundPopover.set_offset(0, 4);
        this.foundPopover.set_child(this.foundBox);
        this.foundPopover.set_parent(this.headerSearch);

        this.toolbar.add_top_bar(header);
    }

    private menu(): Gio.Menu {
        const menu = new Gio.Menu();
        const skies = new Gio.Menu();
        const SKIES: [string, string][] = [
            ['live', 'Live Weather'], ['clear-day', 'Clear'], ['clear-night', 'Clear Night'],
            ['partly-day', 'Partly Cloudy'], ['cloudy-day', 'Cloudy'], ['fog-day', 'Fog'],
            ['rain-day', 'Rain'], ['rain-night', 'Rainy Night'], ['snow-day', 'Snow'], ['storm-night', 'Thunderstorm'],
        ];
        for (const [name, title] of SKIES)
            skies.append(title, `win.sky::${name}`);
        menu.append_submenu('Preview Sky', skies);
        const about = new Gio.Menu();
        about.append('About Glass Weather', 'win.about');
        menu.append_section(null, about);
        return menu;
    }

    private addActions() {
        const refresh = new Gio.SimpleAction({ name: 'refresh' });
        refresh.connect('activate', () => this.refresh(true));
        this.window.add_action(refresh);

        const sky = new Gio.SimpleAction({ name: 'sky', parameter_type: new GLib.VariantType('s') });
        sky.connect('activate', (_action, value) => {
            this.preview = value!.unpack() as string;
            this.show();
        });
        this.window.add_action(sky);

        const about = new Gio.SimpleAction({ name: 'about' });
        about.connect('activate', () => this.about());
        this.window.add_action(about);
    }

    private about() {
        const dialog = new Adw.AboutDialog({
            application_name: 'Glass Weather',
            application_icon: APP_ID,
            developer_name: 'The glass-lib authors',
            version: '0.0.1',
            website: 'https://github.com/ryohsuke1231/glass-lib',
            license_type: Gtk.License.MIT_X11,
            comments: 'A showcase of glass-lib: refracting, Liquid Glass–style glass for GTK 4 and libadwaita. ' +
                'The sky is drawn in code; the cards are glass over it.',
        });
        dialog.add_legal_section('Weather data', 'Open-Meteo.com', Gtk.License.CUSTOM,
            'Weather data by <a href="https://open-meteo.com/">Open-Meteo.com</a>, licensed under ' +
            '<a href="https://creativecommons.org/licenses/by/4.0/">CC BY 4.0</a>. Shown as provided.');
        dialog.present(this.window);
    }

    // ── Places ──

    private fillPlaces() {
        removeAll(this.placeList);
        this.rows.clear();
        for (const place of this.state.places) {
            const row = new PlaceRow(place, p => this.removePlace(p));
            this.rows.set(placeKey(place), row);
            this.placeList.append(row.row);
        }
        this.selectRow();
        this.updateRows();
    }

    private selectRow() {
        const row = this.rows.get(placeKey(this.state.place));
        if (row)
            this.placeList.select_row(row.row);
    }

    private updateRows() {
        for (const [key, row] of this.rows)
            row.update(this.results.get(key)?.forecast, c => this.temp(c));
    }

    private removePlace(place: Place) {
        if (this.state.places.length <= 1)
            return;
        const key = placeKey(place);
        this.state.places = this.state.places.filter(p => placeKey(p) !== key);
        const row = this.rows.get(key);
        if (row)
            this.placeList.remove(row.row);
        this.rows.delete(key);
        if (placeKey(this.state.place) === key)
            this.choose(this.state.places[0]);
        saveState(this.state);
    }

    private choose(place: Place) {
        const key = placeKey(place);
        if (!this.state.places.some(p => placeKey(p) === key)) {
            this.state.places.push(place);
            const row = new PlaceRow(place, p => this.removePlace(p));
            this.rows.set(key, row);
            this.placeList.append(row.row);
            this.load(place, false);
        }
        this.state.place = place;
        saveState(this.state);
        this.sidebarSearch.set_text('');
        this.setHeaderSearching(false);
        this.selectRow();
        this.preview = 'live';
        this.show();
    }

    // ── Data ──

    private refresh(force: boolean) {
        for (const place of this.state.places)
            this.load(place, force);
    }

    private async load(place: Place, force: boolean) {
        const key = placeKey(place);
        this.loading.get(key)?.cancel();
        const cancellable = new Gio.Cancellable();
        this.loading.set(key, cancellable);
        this.updateSpinner();
        try {
            const result = this.offline
                ? { forecast: sampleForecast(place, 'partly', true), error: 'GLASS_WEATHER_OFFLINE is set' }
                : await getForecast(place, force, cancellable);
            this.results.set(key, result);
            this.rows.get(key)?.update(result.forecast, c => this.temp(c));
            if (key === placeKey(this.state.place))
                this.show();
        } catch (e) {
            if (!(e as GLib.Error).matches?.(Gio.IOErrorEnum, Gio.IOErrorEnum.CANCELLED))
                logError(e as Error);
        } finally {
            if (this.loading.get(key) === cancellable)
                this.loading.delete(key);
            this.updateSpinner();
        }
    }

    private updateSpinner() {
        const key = placeKey(this.state.place);
        this.spinner.set_visible(this.loading.has(key) && !this.results.has(key));
    }

    private temp(celsius: number): string {
        return `${Math.round(this.state.fahrenheit ? celsius * 9 / 5 + 32 : celsius)}°`;
    }

    // ── Showing the forecast ──

    private show() {
        this.updateSpinner();
        const result = this.results.get(placeKey(this.state.place));
        this.city.set_label(this.state.place.name);
        this.city.set_tooltip_text(this.state.place.region);
        if (!result) {
            for (const w of [this.temperature, this.conditionLabel, this.hilo, this.notice])
                w.set_label('');
            return;
        }
        let f = result.forecast;

        // The preview replaces the weather with made-up weather under the
        // chosen sky, to see the glass (and its colours) over every sky.
        let kind: SkyKind = condition(f.current.code).sky;
        let isDay = f.current.isDay;
        if (this.preview !== 'live') {
            const [previewKind, time] = this.preview.split('-') as [SkyKind, string];
            kind = previewKind;
            isDay = time !== 'night';
            f = { ...sampleForecast(f.place, kind, isDay), fetched: f.fetched };
        }
        this.sky.set(kind, isDay);
        for (const w of [this.hero, this.attribution]) {
            if (this.sky.isBright)
                w.add_css_class('on-bright');
            else
                w.remove_css_class('on-bright');
        }

        const today = f.daily[0];
        this.temperature.set_label(this.temp(f.current.temperature));
        this.conditionLabel.set_label(condition(f.current.code).text);
        this.hilo.set_label(`H:${this.temp(today.max)}  L:${this.temp(today.min)}`);
        const fetched = GLib.DateTime.new_from_unix_local(Math.floor(f.fetched / 1000)).format('%H:%M');
        if (this.preview !== 'live')
            this.notice.set_label('Preview — made-up weather under this sky');
        else if (f.sample)
            this.notice.set_label(`Sample data — the forecast could not be loaded (${result.error})`);
        else if (result.error)
            this.notice.set_label(`Offline — the forecast from ${fetched}`);
        else
            this.notice.set_label(`Updated ${fetched}`);

        this.showHours(f);
        this.showDays(f);
        this.showTiles(f);
    }

    private showHours(f: Forecast) {
        removeAll(this.hours);
        f.hourly.forEach((hour, i) => {
            const column = new Gtk.Box({ orientation: Gtk.Orientation.VERTICAL, spacing: 6, width_request: 56,
                css_classes: ['weather-hour'] });
            column.append(new Gtk.Label({ label: i === 0 ? 'Now' : clockTime(hour.time).slice(0, 2),
                css_classes: i === 0 ? ['temp'] : [] }));
            column.append(new Gtk.Image({ icon_name: iconFor(hour.code, hour.isDay), pixel_size: 22 }));
            column.append(new Gtk.Label({ label: hour.precipitation >= 20 ? `${hour.precipitation}%` : ' ',
                css_classes: ['precip'] }));
            column.append(new Gtk.Label({ label: this.temp(hour.temperature), css_classes: ['temp'] }));
            this.hours.append(column);
        });
    }

    private showDays(f: Forecast) {
        removeAll(this.days);
        const low = Math.min(...f.daily.map(d => d.min));
        const high = Math.max(...f.daily.map(d => d.max));
        f.daily.forEach((day, i) => {
            if (i > 0)
                this.days.append(new Gtk.Separator({ margin_top: 2, margin_bottom: 2 }));
            this.days.append(this.dayRow(day, i, low, high, i === 0 ? f.current.temperature : null));
        });
    }

    private dayRow(day: Day, index: number, low: number, high: number, now: number | null): Gtk.Widget {
        const row = new Gtk.Box({ spacing: 8, margin_top: 6, margin_bottom: 6, css_classes: ['weather-day'] });
        row.append(new Gtk.Label({ label: weekday(day.date, index), xalign: 0, width_chars: 5, css_classes: ['day'] }));
        const icon = new Gtk.Box({ orientation: Gtk.Orientation.VERTICAL, width_request: 32, valign: Gtk.Align.CENTER });
        icon.append(new Gtk.Image({ icon_name: iconFor(day.code, true), pixel_size: 20 }));
        if (day.precipitation >= 20)
            icon.append(new Gtk.Label({ label: `${day.precipitation}%`, css_classes: ['precip'] }));
        row.append(icon);
        row.append(new Gtk.Label({ label: this.temp(day.min), width_chars: 4, xalign: 1, css_classes: ['dim', 'temp'] }));

        // The day's range on the scale of the whole ten days.
        const bar = new Gtk.DrawingArea({ content_height: 6, content_width: 40, hexpand: true, valign: Gtk.Align.CENTER });
        bar.set_draw_func((area, cr, w, h) => {
            const color = area.get_color();
            const x = (t: number) => (t - low) / Math.max(high - low, 1) * w;
            cr.setSourceRGBA(color.red, color.green, color.blue, 0.16);
            roundedBar(cr, 0, 0, w, h);
            cr.fill();
            const x0 = x(day.min), x1 = Math.max(x(day.max), x0 + h);
            const gradient = new cairo.LinearGradient(x0, 0, x1, 0);
            gradient.addColorStopRGB(0, ...tempColor(day.min));
            gradient.addColorStopRGB(1, ...tempColor(day.max));
            cr.setSource(gradient);
            roundedBar(cr, x0, 0, x1 - x0, h);
            cr.fill();
            if (now !== null) {
                cr.setSourceRGBA(1, 1, 1, 1);
                cr.arc(Math.min(Math.max(x(now), h / 2), w - h / 2), h / 2, h / 2 + 1, 0, 2 * Math.PI);
                cr.fillPreserve();
                cr.setSourceRGBA(0, 0, 0, 0.35);
                cr.setLineWidth(1);
                cr.stroke();
            }
            cr.$dispose();
        });
        row.append(bar);
        row.append(new Gtk.Label({ label: this.temp(day.max), width_chars: 4, xalign: 1, css_classes: ['temp'] }));
        return row;
    }

    private showTiles(f: Forecast) {
        const c = f.current, today = f.daily[0];
        const t = this.tiles;
        t.feels.value.set_label(this.temp(c.apparent));
        t.feels.detail.set_label(Math.abs(c.apparent - c.temperature) < 1.5 ? 'Close to the actual temperature.'
            : c.apparent > c.temperature ? 'Humidity makes it feel warmer.' : 'Wind makes it feel colder.');
        this.humidity = c.humidity;
        this.humidityLabel.set_label(`Humidity ${Math.round(c.humidity)}%`);
        this.humidityBar.queue_draw();

        const wind = this.state.fahrenheit ? `${Math.round(c.wind / 1.609)} mph` : `${Math.round(c.wind)} km/h`;
        t.wind.value.set_label(wind);
        t.wind.detail.set_label(`From the ${COMPASS[Math.round(c.windDirection / 45) % 8]}`);
        this.windFrom = c.windDirection;
        this.windDial.queue_draw();

        t.rain.value.set_label(`${today.precipitationSum.toFixed(today.precipitationSum < 10 ? 1 : 0)} mm today`);
        t.rain.detail.set_label(`${today.precipitation}% chance · the next 48 hours:`);
        this.showRain(f);

        t.sun.value.set_label(`↑ ${clockTime(today.sunrise)}`);
        t.sun.detail.set_label(`↓ Sunset ${clockTime(today.sunset)}`);
        this.sun = { rise: minutes(today.sunrise), set: minutes(today.sunset), now: minutes(c.time) };
        this.sunArc.queue_draw();
    }

    // The rain, hour by hour: a bar for each hour (full at 4 mm, or at the
    // wettest hour), the amount over it.
    private showRain(f: Forecast) {
        removeAll(this.rainHours);
        const most = Math.max(4, ...f.hourly.map(h => h.rain));
        f.hourly.forEach((hour, i) => {
            const column = new Gtk.Box({ orientation: Gtk.Orientation.VERTICAL, spacing: 3, width_request: 40 });
            column.append(new Gtk.Label({ label: hour.rain >= 0.1 ? hour.rain.toFixed(1) : ' ',
                css_classes: ['precip'] }));
            const bar = drawing((cr, w, h, c) => {
                const bw = 10, x = (w - bw) / 2;
                cr.setSourceRGBA(c.red, c.green, c.blue, 0.12);
                roundedColumn(cr, x, 0, bw, h);
                cr.fill();
                const fill = Math.max(h * Math.min(hour.rain / most, 1), hour.rain > 0 ? bw : 0);
                if (fill > 0) {
                    cr.setSourceRGBA(0.3, 0.62, 1, 0.95);
                    roundedColumn(cr, x, h - fill, bw, fill);
                    cr.fill();
                }
            });
            bar.set_content_height(44);
            column.append(bar);
            column.append(new Gtk.Label({ label: i === 0 ? 'Now' : clockTime(hour.time).slice(0, 2),
                css_classes: ['rain-hour'] }));
            this.rainHours.append(column);
        });
    }

    // ── Search ──

    // The header's search opens (the button's glass becomes the field's) or
    // closes; what it found goes with it.
    private setHeaderSearching(searching: boolean) {
        if (this.headerSearch.get_visible() === searching)
            return;
        this.searching?.cancel();
        this.foundPopover.popdown();
        if (!searching)
            this.headerSearch.set_text('');
        this.searchButton.set_visible(!searching);
        this.headerSearch.set_visible(searching);
        if (searching)
            this.headerSearch.grab_focus();
    }

    private async search(text: string) {
        const query = text.trim();
        this.searching?.cancel();
        if (query === '') {
            this.showFound([], null);
            return;
        }
        const cancellable = new Gio.Cancellable();
        this.searching = cancellable;
        try {
            const places = await searchPlaces(query, cancellable);
            this.showFound(places, places.length ? null : 'No places found');
        } catch (e) {
            if (!(e as GLib.Error).matches?.(Gio.IOErrorEnum, Gio.IOErrorEnum.CANCELLED))
                this.showFound([], `Cannot search: ${(e as Error).message}`);
        }
    }

    // What the search found: in the sidebar in place of the places, or
    // under the header's field.
    private showFound(places: Place[], message: string | null) {
        this.found = places;
        const inHeader = this.headerSearch.get_visible();
        removeAll(this.foundList);
        removeAll(this.foundBox);
        for (const place of places) {
            const text = new Gtk.Box({ orientation: Gtk.Orientation.VERTICAL, css_classes: ['weather-result'] });
            text.append(new Gtk.Label({ label: place.name, xalign: 0, css_classes: ['heading'] }));
            text.append(new Gtk.Label({ label: place.region, xalign: 0, css_classes: ['region'], ellipsize: 3 }));
            if (inHeader) {
                // (css_classes replaces the .flat that has_frame: false adds)
                const button = new Gtk.Button({ child: text, can_focus: false,
                    css_classes: ['flat', 'glass-menu-item'] });
                button.connect('clicked', () => this.choose(place));
                this.foundBox.append(button);
            } else {
                this.foundList.append(new Gtk.ListBoxRow({ child: text }));
            }
        }
        if (message) {
            const label = new Gtk.Label({ label: message, wrap: true, xalign: 0, margin_top: 8, margin_bottom: 8,
                margin_start: 12, margin_end: 12, css_classes: ['dim-label'] });
            if (inHeader)
                this.foundBox.append(label);
            else
                this.foundList.append(new Gtk.ListBoxRow({ child: label, activatable: false }));
        }
        const any = places.length > 0 || message !== null;
        if (inHeader) {
            if (any)
                this.foundPopover.popup();
            else
                this.foundPopover.popdown();
        } else {
            this.sidebarStack.set_visible_child_name(any ? 'found' : 'places');
        }
    }
}
