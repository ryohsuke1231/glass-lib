Title: Tuning the Glass
Slug: tuning

# Tuning the Glass

## Materials

Each panel has a [enum@Glass.Material], which sets how it looks:

- `REGULAR` (the default): toolbars, buttons, cards. Light blur, a faint
  white tint, colours that follow the backdrop.
- `CLEAR`: over photos and video. Less blur and tint, and a thicker lens
  (like the desktop widgets of macOS; the others have the Dock's).
- `THICK`: large panes such as sidebars. Strong blur, tinted with the window
  colour, the theme's text colours.
- `MENU`: popovers and menus. Between `REGULAR` and `THICK`.

Set a panel's shape with [property@Glass.Panel:corner-radius] (negative, the
default: a capsule) or per corner with [method@Glass.Panel.set_corner_radii],
and its shadow with [property@Glass.Panel:has-shadow].

## Parameters

The optical parameters — lens depth, refraction, rim light, shadow, blur,
tint strength — are listed with their ranges and each material's values in
[Parameters and Materials](parameters.html). A panel's glass takes each value
from the first of:

1. the panel: [method@Glass.Panel.set_param]
2. the whole app: [method@Glass.Context.set_param]
3. the panel's material
4. the default

So a value set on the [class@Glass.Context] tunes every piece of glass at
once (the demo's Lab page does this), and a value set on a panel singles one
out:

```js
const context = Glass.Context.get_default();
context.set_param(Glass.PARAM_BLUR_RADIUS, 3);   // all glass a little blurrier

const hero = new Glass.Panel({ child: card, corner_radius: 28 });
hero.set_param(Glass.PARAM_DISPLACEMENT_SCALE, 26);  // this one bends more
hero.set_param(Glass.PARAM_SHADOW_INTENSITY, 0.15);
```

The lens acts in a band about 22 px wide along the edge, whatever the corner
radius; on glass thinner than twice that, the whole lens is scaled down with
it. `displacement-scale`, `max-z` and `profile-shape-n` shape the lens inside
the band.

Each key has a constant, `Glass.PARAM_BLUR_RADIUS` for `'blur-radius'` and so
on (`GLASS_PARAM_BLUR_RADIUS` in C): a misspelt constant is an error at once,
where a misspelt string is only a warning when it runs. The strings work too.

Values out of range are clamped, with a warning; `blur-downscale` snaps to 1,
2 or 4. [method@Glass.Context.reset_param] and [method@Glass.Panel.reset_param]
go back to the next value in the list; [method@Glass.Panel.get_effective_param]
tells what a panel uses.

## Tint

The glass mixes in a colour. How much is the `tint-strength` parameter (above);
which colour is the first of:

1. the panel's [property@Glass.Panel:tint] (it sets the strength too, as its
   alpha)
2. [property@Glass.Context:tint-color]
3. the material's colour: white, or the theme's window (`THICK`) or popover
   (`MENU`) background

```js
const pink = new Gdk.RGBA();
pink.parse('#ff5a8c');
Glass.Context.get_default().set_tint_color(pink);        // every panel
Glass.Context.get_default().set_param(Glass.PARAM_TINT_STRENGTH, 0.2);
```

## Trying values without changing code

Glass Gallery's **Lab** page has a slider for every parameter, the tint colour
and strength, the renderer and reduced transparency, over test patterns and
moving backgrounds; the values apply to the whole app, as
[method@Glass.Context.set_param] does.
