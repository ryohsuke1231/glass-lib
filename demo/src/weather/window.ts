// Glass Weather's window. The layout, from the back:
//
//   Glass.ToolbarView            the header and the search float over it all
//   └ Glass.View                 the cards are glass over the sky
//     ├ content: Sky             drawn in code, moving
//     └ overlay: ScrolledWindow  the hero text and the cards (Glass.Panel)
//
// Two views, because a view draws every glass body before any foreground:
// the header's glass is in the outer view, so it refracts the cards and
// their text as they scroll under it (design.md §6.7, §7.6).

import Adw from 'gi://Adw?version=1';
import Gdk from 'gi://Gdk?version=4.0';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import GObject from 'gi://GObject';
import Gtk from 'gi://Gtk?version=4.0';
import Glass from 'gi://Glass?version=1';

import cairo from 'cairo';

import { Day, Forecast, getForecast, Place, sampleForecast, searchPlaces } from './api.js';
import { condition, iconFor, SkyKind } from './conditions.js';
import { Sky } from './sky.js';

const APP_ID = 'io.github.ryohsuke1231.GlassWeather';
const REFRESH_MS = 15 * 60 * 1000;
const TOKYO: Place = { name: 'Tokyo', region: 'Tokyo, Japan', latitude: 35.6895, longitude: 139.6917 };

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
.weather-card .value { font-size: 28px; }
.weather-hour .temp { font-weight: 700; }
.weather-day .day { font-weight: 700; }
.weather-day .temp { font-weight: 700; }
.weather-attribution { color: white; font-size: 12px; }
.weather-attribution.on-bright { color: #17212e; }
.weather-result .region { font-size: 12px; opacity: 0.7; }
`;

interface State {
    place: Place;
    recents: Place[];
    fahrenheit: boolean;
}

function statePath(): string {
    return GLib.build_filenamev([GLib.get_user_config_dir(), 'glass-weather', 'state.json']);
}

function loadState(): State {
    try {
        const [, bytes] = GLib.file_get_contents(statePath());
        const state = JSON.parse(new TextDecoder().decode(bytes)) as State;
        if (state.place)
            return { place: state.place, recents: state.recents ?? [], fahrenheit: !!state.fahrenheit };
    } catch (e) {
        // First start.
    }
    return { place: TOKYO, recents: [TOKYO], fahrenheit: false };
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

const COMPASS = ['N', 'NE', 'E', 'SE', 'S', 'SW', 'W', 'NW'];
const WEEKDAYS = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];

function clockTime(iso: string): string {
    return iso.slice(11, 16);
}

function weekday(date: string, index: number): string {
    if (index === 0)
        return 'Today';
    const [y, m, d] = date.split('-').map(Number);
    return WEEKDAYS[new Date(Date.UTC(y, m - 1, d)).getUTCDay()];
}

function uvText(uv: number): string {
    return uv < 3 ? 'Low' : uv < 6 ? 'Moderate' : uv < 8 ? 'High' : uv < 11 ? 'Very High' : 'Extreme';
}

// A card: a pane of glass with a small heading.
function card(icon: string, heading: string, body: Gtk.Widget, extraClass?: string): Glass.Panel {
    const box = new Gtk.Box({ orientation: Gtk.Orientation.VERTICAL, spacing: 8,
        margin_top: 12, margin_bottom: 12, margin_start: 16, margin_end: 16 });
    const title = new Gtk.Box({ spacing: 6, css_classes: ['card-heading'] });
    title.append(new Gtk.Image({ icon_name: icon, pixel_size: 12 }));
    title.append(new Gtk.Label({ label: heading.toUpperCase(), xalign: 0 }));
    box.append(title);
    box.append(body);
    const panel = new Glass.Panel({ child: box, corner_radius: 22, css_classes: ['weather-card'] });
    if (extraClass)
        panel.add_css_class(extraClass);
    return panel;
}

// A small square card: a heading, a value and a line under it.
class Tile {
    readonly panel: Glass.Panel;
    readonly value = new Gtk.Label({ xalign: 0, css_classes: ['value'] });
    readonly detail = new Gtk.Label({ xalign: 0, wrap: true, css_classes: ['dim'] });

    constructor(icon: string, heading: string) {
        const body = new Gtk.Box({ orientation: Gtk.Orientation.VERTICAL, spacing: 4, vexpand: true });
        body.append(this.value);
        body.append(this.detail);
        this.panel = card(icon, heading, body, 'weather-tile');
        this.panel.set_hexpand(true);
        this.panel.set_size_request(-1, 128);
    }
}

export class WeatherWindow {
    readonly window: Adw.ApplicationWindow;
    private state = loadState();
    private forecast: Forecast | null = null;
    private error: string | null = null;
    private preview: string = GLib.getenv('GLASS_WEATHER_SKY') ?? 'live';
    private offline = !!GLib.getenv('GLASS_WEATHER_OFFLINE');
    private loading: Gio.Cancellable | null = null;
    private searching: Gio.Cancellable | null = null;

    private sky = new Sky();
    private toolbar: Glass.ToolbarView;
    private hero: Gtk.Box;
    private city = new Gtk.Label({ css_classes: ['city'], ellipsize: 3 });
    private temperature = new Gtk.Label({ css_classes: ['temperature'] });
    private conditionLabel = new Gtk.Label({ css_classes: ['condition'] });
    private hilo = new Gtk.Label({ css_classes: ['hilo'] });
    private notice = new Gtk.Label({ css_classes: ['notice'], wrap: true, justify: Gtk.Justification.CENTER });
    private spinner = new Adw.Spinner({ width_request: 32, height_request: 32 });
    private hours = new Gtk.Box({ spacing: 4 });
    private days = new Gtk.Box({ orientation: Gtk.Orientation.VERTICAL });
    private tiles: Record<string, Tile> = {};
    private attribution: Gtk.Label;
    private units: Glass.ToggleGroup;

    private searchButton: Glass.Button;
    private searchEntry: Glass.SearchEntry;
    private results: Glass.Panel;
    private resultList = new Gtk.Box({ orientation: Gtk.Orientation.VERTICAL });
    private shownPlaces: Place[] = [];

    constructor(app: Adw.Application) {
        const provider = new Gtk.CssProvider();
        provider.load_from_string(CSS);
        Gtk.StyleContext.add_provider_for_display(Gdk.Display.get_default()!, provider,
            Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION);

        this.window = new Adw.ApplicationWindow({ application: app, title: 'Glass Weather',
            default_width: 470, default_height: 880, width_request: 360, height_request: 560 });

        // The hero: the place and the weather now, straight on the sky.
        this.hero = new Gtk.Box({ orientation: Gtk.Orientation.VERTICAL, halign: Gtk.Align.CENTER,
            margin_top: 18, margin_bottom: 18, css_classes: ['weather-hero'] });
        for (const w of [this.city, this.spinner, this.temperature, this.conditionLabel, this.hilo, this.notice])
            this.hero.append(w);

        // Hourly: a strip that scrolls sideways inside its glass.
        const hourScroller = new Gtk.ScrolledWindow({ child: this.hours, vscrollbar_policy: Gtk.PolicyType.NEVER,
            hscrollbar_policy: Gtk.PolicyType.EXTERNAL, propagate_natural_height: true });
        const hourBody = new Gtk.Box({ orientation: Gtk.Orientation.VERTICAL, spacing: 8 });
        hourBody.append(new Gtk.Separator());
        hourBody.append(hourScroller);
        const hourly = card('document-open-recent-symbolic', 'Hourly Forecast', hourBody);

        const dayBody = new Gtk.Box({ orientation: Gtk.Orientation.VERTICAL });
        dayBody.append(this.days);
        const daily = card('x-office-calendar-symbolic', '10-Day Forecast', dayBody);

        // Small cards, two to a row.
        const grid = new Gtk.Grid({ column_spacing: 12, row_spacing: 12, column_homogeneous: true });
        const TILES: [string, string, string][] = [
            ['feels', 'temperature-symbolic', 'Feels Like'],
            ['humidity', 'weather-fog-symbolic', 'Humidity'],
            ['wind', 'weather-windy-symbolic', 'Wind'],
            ['uv', 'weather-clear-symbolic', 'UV Index'],
            ['sun', 'daytime-sunset-symbolic', 'Sunrise'],
            ['rain', 'weather-showers-symbolic', 'Precipitation'],
        ];
        TILES.forEach(([key, icon, heading], i) => {
            this.tiles[key] = new Tile(icon, heading);
            grid.attach(this.tiles[key].panel, i % 2, Math.floor(i / 2), 1, 1);
        });

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
            margin_start: 16, margin_end: 16, margin_bottom: 96 });
        for (const w of [this.hero, hourly, daily, grid, this.attribution])
            column.append(w);
        const scroller = new Gtk.ScrolledWindow({ hscrollbar_policy: Gtk.PolicyType.NEVER,
            child: new Adw.Clamp({ maximum_size: 560, tightening_threshold: 420, child: column }) });

        const view = new Glass.View({ content: this.sky.widget });
        view.add_overlay(scroller);

        // The header: units in the middle, the menu at the end.
        const header = new Glass.HeaderBar();
        this.units = new Glass.ToggleGroup();
        this.units.append('c', '°C', null);
        this.units.append('f', '°F', null);
        this.units.set_active_name(this.state.fahrenheit ? 'f' : 'c');
        this.units.connect('notify::active-name', () => {
            this.state.fahrenheit = this.units.get_active_name() === 'f';
            saveState(this.state);
            this.show();
        });
        header.set_title_widget(this.units);
        header.pack_end(new Glass.MenuButton({ icon_name: 'open-menu-symbolic', menu_model: this.menu(),
            tooltip_text: 'Menu' }));

        // The search at the bottom: the button's glass becomes the field's
        // (morph-id), and the list of places comes out of the field.
        this.searchButton = Glass.Button.new_from_icon_name('edit-find-symbolic');
        this.searchButton.set_morph_id('search');
        this.searchButton.set_tooltip_text('Search for a City');
        this.searchButton.connect('clicked', () => this.setSearching(true));
        // Frosted like the list above it (a group's panels share the first
        // one's material), so the text reads over the cards.
        this.searchEntry = new Glass.SearchEntry({ morph_id: 'search', visible: false, width_request: 300,
            placeholder_text: 'Search for a city', material: Glass.Material.MENU });
        this.searchEntry.connect('search-changed', () => this.search());
        this.searchEntry.connect('activate', () => {
            if (this.shownPlaces.length > 0)
                this.choose(this.shownPlaces[0]);
        });
        this.searchEntry.connect('stop-search', () => this.setSearching(false));
        // A list over busy cards: the menu material, frosted enough to read.
        this.results = new Glass.Panel({ child: this.resultList, corner_radius: 20, visible: false,
            width_request: 300, material: Glass.Material.MENU, css_classes: ['weather-card'] });
        this.resultList.set_margin_top(6);
        this.resultList.set_margin_bottom(6);
        const searchRow = new Gtk.Box({ halign: Gtk.Align.CENTER });
        searchRow.append(this.searchButton);
        searchRow.append(this.searchEntry);
        const bottom = new Gtk.Box({ orientation: Gtk.Orientation.VERTICAL, spacing: 12, halign: Gtk.Align.CENTER,
            margin_bottom: 18 });
        bottom.append(this.results);
        bottom.append(searchRow);

        this.toolbar = new Glass.ToolbarView({ content: view });
        this.toolbar.add_top_bar(header);
        this.toolbar.add_bottom_bar(new Glass.Group({ child: bottom, halign: Gtk.Align.CENTER }));
        this.toolbar.set_top_edge_style(Glass.EdgeStyle.NONE);
        this.toolbar.set_bottom_edge_style(Glass.EdgeStyle.NONE);
        this.toolbar.bind_property_full('top-bar-height', column, 'margin-top', GObject.BindingFlags.SYNC_CREATE,
            (_binding, value: number) => [true, value + 4], null);
        this.window.set_content(this.toolbar);

        this.addActions();
        this.load(false);
        GLib.timeout_add(GLib.PRIORITY_DEFAULT, REFRESH_MS, () => {
            this.load(false);
            return GLib.SOURCE_CONTINUE;
        });
    }

    private menu(): Gio.Menu {
        const menu = new Gio.Menu();
        menu.append('Refresh', 'win.refresh');
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
        refresh.connect('activate', () => this.load(true));
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

    // ── Data ──

    private async load(force: boolean) {
        this.loading?.cancel();
        const cancellable = new Gio.Cancellable();
        this.loading = cancellable;
        this.spinner.set_visible(this.forecast === null);
        const place = this.state.place;
        try {
            if (this.offline) {
                this.forecast = sampleForecast(place, 'partly', true);
                this.error = 'GLASS_WEATHER_OFFLINE is set';
            } else {
                const result = await getForecast(place, force, cancellable);
                this.forecast = result.forecast;
                this.error = result.error;
            }
            this.show();
        } catch (e) {
            if (!(e as GLib.Error).matches?.(Gio.IOErrorEnum, Gio.IOErrorEnum.CANCELLED))
                logError(e as Error);
        } finally {
            if (this.loading === cancellable) {
                this.loading = null;
                this.spinner.set_visible(false);
            }
        }
    }

    private temp(celsius: number): string {
        return `${Math.round(this.state.fahrenheit ? celsius * 9 / 5 + 32 : celsius)}°`;
    }

    // ── Showing the forecast ──

    private show() {
        let f = this.forecast;
        if (!f)
            return;

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
        this.city.set_label(f.place.name);
        this.city.set_tooltip_text(f.place.region);
        this.temperature.set_label(this.temp(f.current.temperature));
        this.conditionLabel.set_label(condition(f.current.code).text);
        this.hilo.set_label(`H:${this.temp(today.max)}  L:${this.temp(today.min)}`);
        const fetched = GLib.DateTime.new_from_unix_local(Math.floor(f.fetched / 1000)).format('%H:%M');
        if (this.preview !== 'live')
            this.notice.set_label('Preview — made-up weather under this sky');
        else if (f.sample)
            this.notice.set_label(`Sample data — the forecast could not be loaded (${this.error})`);
        else if (this.error)
            this.notice.set_label(`Offline — the forecast from ${fetched}`);
        else
            this.notice.set_label(`Updated ${fetched}`);

        this.showHours(f);
        this.showDays(f);
        this.showTiles(f);
    }

    private showHours(f: Forecast) {
        for (let child = this.hours.get_first_child(); child; child = this.hours.get_first_child())
            this.hours.remove(child);
        f.hourly.forEach((hour, i) => {
            const column = new Gtk.Box({ orientation: Gtk.Orientation.VERTICAL, spacing: 6, width_request: 50,
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
        for (let child = this.days.get_first_child(); child; child = this.days.get_first_child())
            this.days.remove(child);
        const low = Math.min(...f.daily.map(d => d.min));
        const high = Math.max(...f.daily.map(d => d.max));
        f.daily.forEach((day, i) => {
            if (i > 0)
                this.days.append(new Gtk.Separator({ margin_top: 2, margin_bottom: 2 }));
            this.days.append(this.dayRow(day, i, low, high, i === 0 ? f.current.temperature : null));
        });
    }

    private dayRow(day: Day, index: number, low: number, high: number, now: number | null): Gtk.Widget {
        const row = new Gtk.Box({ spacing: 10, margin_top: 6, margin_bottom: 6, css_classes: ['weather-day'] });
        row.append(new Gtk.Label({ label: weekday(day.date, index), xalign: 0, width_chars: 6, css_classes: ['day'] }));
        const icon = new Gtk.Box({ orientation: Gtk.Orientation.VERTICAL, width_request: 36, valign: Gtk.Align.CENTER });
        icon.append(new Gtk.Image({ icon_name: iconFor(day.code, true), pixel_size: 20 }));
        if (day.precipitation >= 20)
            icon.append(new Gtk.Label({ label: `${day.precipitation}%`, css_classes: ['precip'] }));
        row.append(icon);
        row.append(new Gtk.Label({ label: this.temp(day.min), width_chars: 4, xalign: 1, css_classes: ['dim', 'temp'] }));

        // The day's range on the scale of the whole ten days.
        const bar = new Gtk.DrawingArea({ content_height: 6, hexpand: true, valign: Gtk.Align.CENTER });
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
        t.humidity.value.set_label(`${Math.round(c.humidity)}%`);
        t.humidity.detail.set_label(c.humidity > 80 ? 'Very humid.' : c.humidity < 30 ? 'Dry air.' : 'Comfortable.');
        const wind = this.state.fahrenheit ? `${Math.round(c.wind / 1.609)} mph` : `${Math.round(c.wind)} km/h`;
        t.wind.value.set_label(wind);
        t.wind.detail.set_label(`From the ${COMPASS[Math.round(c.windDirection / 45) % 8]}.`);
        t.uv.value.set_label(`${Math.round(today.uv)}`);
        t.uv.detail.set_label(uvText(today.uv));
        t.sun.value.set_label(clockTime(today.sunrise));
        t.sun.detail.set_label(`Sunset ${clockTime(today.sunset)}`);
        t.rain.value.set_label(`${today.precipitationSum.toFixed(today.precipitationSum < 10 ? 1 : 0)} mm`);
        t.rain.detail.set_label(`Today, with a ${today.precipitation}% chance.`);
    }

    // ── Search ──

    private setSearching(searching: boolean) {
        this.searching?.cancel();
        this.searchButton.set_visible(!searching);
        this.searchEntry.set_visible(searching);
        this.results.set_visible(false);
        if (searching) {
            this.searchEntry.set_text('');
            this.searchEntry.grab_focus();
            // The recent places come out of the field once it has formed.
            GLib.timeout_add(GLib.PRIORITY_DEFAULT, 320, () => {
                if (this.searchEntry.get_visible() && this.searchEntry.get_text() === '')
                    this.showPlaces(this.state.recents, null);
                return GLib.SOURCE_REMOVE;
            });
        }
    }

    private async search() {
        const query = this.searchEntry.get_text().trim();
        this.searching?.cancel();
        if (query === '') {
            this.showPlaces(this.state.recents, null);
            return;
        }
        const cancellable = new Gio.Cancellable();
        this.searching = cancellable;
        try {
            const places = await searchPlaces(query, cancellable);
            this.showPlaces(places, places.length ? null : 'No places found');
        } catch (e) {
            if (!(e as GLib.Error).matches?.(Gio.IOErrorEnum, Gio.IOErrorEnum.CANCELLED))
                this.showPlaces([], `Cannot search: ${(e as Error).message}`);
        }
    }

    private showPlaces(places: Place[], message: string | null) {
        for (let child = this.resultList.get_first_child(); child; child = this.resultList.get_first_child())
            this.resultList.remove(child);
        this.shownPlaces = places;
        for (const place of places) {
            const box = new Gtk.Box({ orientation: Gtk.Orientation.VERTICAL, css_classes: ['weather-result'] });
            box.append(new Gtk.Label({ label: place.name, xalign: 0, css_classes: ['heading'] }));
            box.append(new Gtk.Label({ label: place.region, xalign: 0, css_classes: ['region'], ellipsize: 3 }));
            const button = new Gtk.Button({ child: box, has_frame: false, margin_start: 6, margin_end: 6 });
            button.connect('clicked', () => this.choose(place));
            this.resultList.append(button);
        }
        if (message)
            this.resultList.append(new Gtk.Label({ label: message, wrap: true, margin_top: 8, margin_bottom: 8,
                margin_start: 16, margin_end: 16, css_classes: ['dim'] }));
        this.results.set_visible(this.searchEntry.get_visible() && (places.length > 0 || message !== null));
    }

    private choose(place: Place) {
        const same = (a: Place, b: Place) => a.latitude === b.latitude && a.longitude === b.longitude;
        this.state.place = place;
        this.state.recents = [place, ...this.state.recents.filter(p => !same(p, place))].slice(0, 5);
        saveState(this.state);
        this.setSearching(false);
        this.preview = 'live';
        this.forecast = null;
        this.load(false);
    }
}
