// Weather data from Open-Meteo (https://open-meteo.com/): the forecast and
// the city search. Free for non-commercial use, no key; the data are CC BY
// 4.0 and the app shows "Weather data by Open-Meteo.com" next to them
// (design.md §14.1; terms checked 2026-09-26).
//
// Forecasts are cached for 15 minutes, so starting the app does not call the
// API every time; without a network the last forecast (however old), or else
// sample data, is shown.

import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import Soup from 'gi://Soup?version=3.0';

import { codeFor, SkyKind } from './conditions.js';

export interface Place {
    name: string;
    region: string;          // "Hokkaido, Japan"
    latitude: number;
    longitude: number;
}

export interface Hour {
    time: string;            // local ISO time, "2026-09-26T21:00"
    temperature: number;     // °C
    code: number;
    isDay: boolean;
    precipitation: number;   // probability, %
    rain: number;            // precipitation in the hour, mm
}

export interface Day {
    date: string;            // "2026-09-27"
    code: number;
    max: number;
    min: number;
    sunrise: string;
    sunset: string;
    precipitation: number;   // probability, %
    precipitationSum: number; // mm
    uv: number;
}

export interface Forecast {
    place: Place;
    fetched: number;         // ms since the epoch
    sample: boolean;         // made up (offline)
    utcOffset: number;       // the place's offset from UTC, s (its local time)
    current: {
        time: string;
        temperature: number;
        apparent: number;
        humidity: number;
        isDay: boolean;
        code: number;
        wind: number;        // km/h
        windDirection: number;
        precipitation: number; // mm
    };
    hourly: Hour[];
    daily: Day[];
}

const USER_AGENT = 'GlassWeather/0.1 (a glass-lib demo; +https://github.com/ryohsuke1231/glass-lib)';
const FRESH_MS = 15 * 60 * 1000;
// Hourly forecast: now and the next 48 hours.
const HOURS = 49;

let session: Soup.Session | null = null;

function getJson(url: string, cancellable: Gio.Cancellable | null): Promise<any> {
    session ??= new Soup.Session({ user_agent: USER_AGENT, timeout: 20 });
    const message = Soup.Message.new('GET', url);
    return new Promise((resolve, reject) => {
        session!.send_and_read_async(message, GLib.PRIORITY_DEFAULT, cancellable, (_session, result) => {
            try {
                const bytes = session!.send_and_read_finish(result);
                if (message.get_status() !== Soup.Status.OK)
                    throw new Error(`${message.get_status()} ${message.get_reason_phrase()}`);
                resolve(JSON.parse(new TextDecoder().decode(bytes.get_data()!)));
            } catch (e) {
                reject(e);
            }
        });
    });
}

// Coordinates to four decimals: enough for weather, and one cache entry per
// place however the search rounded them.
function coord(value: number): string {
    return value.toFixed(4);
}

export async function searchPlaces(query: string, cancellable: Gio.Cancellable | null): Promise<Place[]> {
    const url = 'https://geocoding-api.open-meteo.com/v1/search?count=6&language=en&format=json&name=' +
        encodeURIComponent(query);
    const json = await getJson(url, cancellable);
    return (json.results ?? []).map((r: any) => ({
        name: r.name,
        region: [r.admin1, r.country].filter(Boolean).join(', '),
        latitude: r.latitude,
        longitude: r.longitude,
    }));
}

function cachePath(place: Place): string {
    return GLib.build_filenamev([GLib.get_user_cache_dir(), 'glass-weather',
        `forecast-${coord(place.latitude)},${coord(place.longitude)}.json`]);
}

function readCache(place: Place): Forecast | null {
    try {
        const [ok, bytes] = GLib.file_get_contents(cachePath(place));
        if (!ok)
            return null;
        const forecast = JSON.parse(new TextDecoder().decode(bytes)) as Forecast;
        // Written by an older version (no local time, fewer hours): stale.
        if (forecast.utcOffset === undefined || forecast.hourly.length < HOURS ||
            forecast.hourly[0].rain === undefined)
            return null;
        forecast.place = place;
        return forecast;
    } catch (e) {
        return null;
    }
}

function writeCache(forecast: Forecast) {
    try {
        const path = cachePath(forecast.place);
        GLib.mkdir_with_parents(GLib.path_get_dirname(path), 0o755);
        GLib.file_set_contents(path, JSON.stringify(forecast));
    } catch (e) {
        logError(e as Error, 'glass-weather: could not cache the forecast');
    }
}

function parse(place: Place, json: any): Forecast {
    const c = json.current, h = json.hourly, d = json.daily;
    return {
        place,
        fetched: Date.now(),
        sample: false,
        utcOffset: json.utc_offset_seconds ?? 0,
        current: {
            time: c.time,
            temperature: c.temperature_2m,
            apparent: c.apparent_temperature,
            humidity: c.relative_humidity_2m,
            isDay: c.is_day === 1,
            code: c.weather_code,
            wind: c.wind_speed_10m,
            windDirection: c.wind_direction_10m,
            precipitation: c.precipitation,
        },
        hourly: h.time.map((time: string, i: number) => ({
            time,
            temperature: h.temperature_2m[i],
            code: h.weather_code[i],
            isDay: h.is_day[i] === 1,
            precipitation: h.precipitation_probability[i] ?? 0,
            rain: h.precipitation[i] ?? 0,
        })),
        daily: d.time.map((date: string, i: number) => ({
            date,
            code: d.weather_code[i],
            max: d.temperature_2m_max[i],
            min: d.temperature_2m_min[i],
            sunrise: d.sunrise[i],
            sunset: d.sunset[i],
            precipitation: d.precipitation_probability_max[i] ?? 0,
            precipitationSum: d.precipitation_sum[i] ?? 0,
            uv: d.uv_index_max[i] ?? 0,
        })),
    };
}

export interface Result {
    forecast: Forecast;
    error: string | null;    // why it is not a fresh forecast
}

// The forecast for @place: the cached one while it is fresh (unless @force),
// else from the network, else the stale cache, else sample data.
export async function getForecast(place: Place, force: boolean, cancellable: Gio.Cancellable | null): Promise<Result> {
    const cached = readCache(place);
    if (cached && !force && Date.now() - cached.fetched < FRESH_MS)
        return { forecast: cached, error: null };

    const url = 'https://api.open-meteo.com/v1/forecast' +
        `?latitude=${coord(place.latitude)}&longitude=${coord(place.longitude)}` +
        '&current=temperature_2m,apparent_temperature,relative_humidity_2m,is_day,weather_code,' +
        'wind_speed_10m,wind_direction_10m,precipitation' +
        '&hourly=temperature_2m,weather_code,is_day,precipitation_probability,precipitation' +
        '&daily=weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset,' +
        'precipitation_probability_max,precipitation_sum,uv_index_max' +
        `&timezone=auto&forecast_days=10&forecast_hours=${HOURS}`;
    try {
        const forecast = parse(place, await getJson(url, cancellable));
        writeCache(forecast);
        return { forecast, error: null };
    } catch (e) {
        if ((e as GLib.Error).matches?.(Gio.IOErrorEnum, Gio.IOErrorEnum.CANCELLED))
            throw e;
        const message = (e as Error).message;
        if (cached)
            return { forecast: cached, error: message };
        return { forecast: sampleForecast(place, 'partly', true), error: message };
    }
}

// Made-up weather in the shape of a forecast: offline, and for the sky
// preview's screenshots.
export function sampleForecast(place: Place, sky: SkyKind, isDay: boolean): Forecast {
    const code = codeFor(sky);
    const today = GLib.DateTime.new_now_local();
    const pad = (n: number) => String(n).padStart(2, '0');
    const date = (d: GLib.DateTime) => `${d.get_year()}-${pad(d.get_month())}-${pad(d.get_day_of_month())}`;
    const hour0 = isDay ? 13 : 22;
    const hourly: Hour[] = [];
    for (let i = 0; i < HOURS; i++) {
        const hour = (hour0 + i) % 24;
        const t = 18 + 5 * Math.sin((hour - 9) / 24 * 2 * Math.PI);
        const day = today.add_days(Math.floor((hour0 + i) / 24))!;
        hourly.push({ time: `${date(day)}T${pad(hour)}:00`, temperature: Math.round(t * 10) / 10,
            code: i % 7 === 3 ? codeFor('cloudy') : code, isDay: hour >= 6 && hour < 18,
            precipitation: sky === 'rain' || sky === 'storm' ? 60 + (i * 7) % 30 : (i * 13) % 20,
            rain: sky === 'rain' || sky === 'storm' ? [0.4, 1.2, 2.6, 0.8, 0, 0.3][i % 6] : (i % 11 === 5 ? 0.2 : 0) });
    }
    const daily: Day[] = [];
    const codes = [code, 2, 63, 0, 3, 81, 1, 0, 45, 2];
    for (let i = 0; i < 10; i++) {
        const d = today.add_days(i)!;
        daily.push({ date: date(d), code: codes[i], max: 23 - (i % 4) + (i % 3), min: 15 - (i % 3),
            sunrise: `${date(d)}T05:${pad(30 + i)}`, sunset: `${date(d)}T17:${pad(40 - i)}`,
            precipitation: [10, 20, 80, 0, 30, 70, 10, 0, 20, 10][i], precipitationSum: [0, 0.4, 12, 0, 1, 8, 0, 0, 0.2, 0][i],
            uv: [5, 4, 2, 6, 3, 2, 5, 6, 3, 4][i] });
    }
    // Today's rain is the hours' (the made-up hours and days agree).
    daily[0].precipitationSum = Math.round(hourly.slice(0, 24).reduce((sum, h) => sum + h.rain, 0) * 10) / 10;
    return {
        place,
        fetched: Date.now(),
        sample: true,
        utcOffset: Number(today.get_utc_offset()) / 1e6,
        current: { time: hourly[0].time, temperature: hourly[0].temperature, apparent: hourly[0].temperature + 1,
            humidity: sky === 'rain' ? 92 : 58, isDay, code, wind: 12, windDirection: 225, precipitation: 0 },
        hourly,
        daily,
    };
}
