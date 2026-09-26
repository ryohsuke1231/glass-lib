// Glass Weather — the showcase demo of glass-lib (design.md §14.1): the
// weather over a sky drawn in code, on cards of glass.
//
//   cd demo && npm run build
//   meson devenv -C build -w . gjs -m demo/dist/weather/main.js
//
// Environment (for comparisons and screenshots):
//   GLASS_WEATHER_SKY=clear-day|rain-night|...   preview a sky (made-up weather)
//   GLASS_WEATHER_OFFLINE=1                      sample data, no network
//   GLASS_GALLERY_SCREENSHOT=file.png            render the window, then quit

import Adw from 'gi://Adw?version=1';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import Glass from 'gi://Glass?version=1';
import System from 'system';

import { maybeScreenshot } from '../util.js';
import { WeatherWindow } from './window.js';

const app = new Adw.Application({
    application_id: 'io.github.ryohsuke1231.GlassWeather',
    flags: Gio.ApplicationFlags.DEFAULT_FLAGS,
});
app.connect('activate', () => {
    const active = app.get_active_window();
    if (active) {
        active.present();
        return;
    }
    Glass.init();
    const weather = new WeatherWindow(app);
    // Screenshots at the default size, whatever the window manager does
    // with new windows.
    if (GLib.getenv('GLASS_GALLERY_SCREENSHOT'))
        weather.window.set_resizable(false);
    weather.window.present();
    maybeScreenshot(weather.window, () => app.quit());
});
app.run([System.programInvocationName, ...ARGV]);
