// WMO weather codes, as Open-Meteo reports them: what to call the weather,
// which icon to show and which sky to draw behind the glass.

export type SkyKind = 'clear' | 'partly' | 'cloudy' | 'fog' | 'rain' | 'snow' | 'storm';

export interface Condition {
    text: string;
    sky: SkyKind;
    icon: string;
    nightIcon?: string;
}

const CLEAR: Condition = { text: 'Clear', sky: 'clear',
    icon: 'weather-clear-symbolic', nightIcon: 'weather-clear-night-symbolic' };
const FEW = { sky: 'partly' as SkyKind,
    icon: 'weather-few-clouds-symbolic', nightIcon: 'weather-few-clouds-night-symbolic' };
const DRIZZLE = { sky: 'rain' as SkyKind, icon: 'weather-showers-scattered-symbolic' };
const RAIN = { sky: 'rain' as SkyKind, icon: 'weather-showers-symbolic' };
const SNOW = { sky: 'snow' as SkyKind, icon: 'weather-snow-symbolic' };
const STORM = { sky: 'storm' as SkyKind, icon: 'weather-storm-symbolic' };

const CONDITIONS: Record<number, Condition> = {
    0: CLEAR,
    1: { text: 'Mostly Clear', ...FEW },
    2: { text: 'Partly Cloudy', ...FEW },
    3: { text: 'Cloudy', sky: 'cloudy', icon: 'weather-overcast-symbolic' },
    45: { text: 'Fog', sky: 'fog', icon: 'weather-fog-symbolic' },
    48: { text: 'Rime Fog', sky: 'fog', icon: 'weather-fog-symbolic' },
    51: { text: 'Light Drizzle', ...DRIZZLE },
    53: { text: 'Drizzle', ...DRIZZLE },
    55: { text: 'Heavy Drizzle', ...DRIZZLE },
    56: { text: 'Freezing Drizzle', ...DRIZZLE },
    57: { text: 'Freezing Drizzle', ...DRIZZLE },
    61: { text: 'Light Rain', ...DRIZZLE },
    63: { text: 'Rain', ...RAIN },
    65: { text: 'Heavy Rain', ...RAIN },
    66: { text: 'Freezing Rain', ...RAIN },
    67: { text: 'Freezing Rain', ...RAIN },
    71: { text: 'Light Snow', ...SNOW },
    73: { text: 'Snow', ...SNOW },
    75: { text: 'Heavy Snow', ...SNOW },
    77: { text: 'Snow Grains', ...SNOW },
    80: { text: 'Rain Showers', ...DRIZZLE },
    81: { text: 'Rain Showers', ...RAIN },
    82: { text: 'Violent Showers', ...RAIN },
    85: { text: 'Snow Showers', ...SNOW },
    86: { text: 'Heavy Snow Showers', ...SNOW },
    95: { text: 'Thunderstorm', ...STORM },
    96: { text: 'Thunderstorm, Hail', ...STORM },
    99: { text: 'Thunderstorm, Hail', ...STORM },
};

export function condition(code: number): Condition {
    return CONDITIONS[code] ?? { text: 'Unknown', sky: 'cloudy', icon: 'weather-severe-alert-symbolic' };
}

export function iconFor(code: number, isDay: boolean): string {
    const c = condition(code);
    return !isDay && c.nightIcon ? c.nightIcon : c.icon;
}

// A code that shows the given sky (for the sky preview).
export function codeFor(sky: SkyKind): number {
    return { clear: 0, partly: 2, cloudy: 3, fog: 45, rain: 63, snow: 73, storm: 95 }[sky];
}
