Title: Styling
Slug: styling

# Styling

glass-lib's stylesheet is loaded at `GTK_STYLE_PROVIDER_PRIORITY_SETTINGS`
(400): above libadwaita, below the application (600), so an app can override
any of it.

## Nodes and classes

| Node | Classes | Set by |
|---|---|---|
| `glassview` | | [class@Glass.View] |
| `glasspanel` | `.glass-light`, `.glass-dark` | the adaptive colours |
| | `.glass-fallback` | when CSS draws the glass |
| | `.glass-high-contrast` | in high contrast |
| | `.glass-button`, `.button-group`, `.toggle-group`, `.search-entry` | the parts |
| `glassgroup` | | [class@Glass.Group] |

Setting a panel's classes wholesale (`css_classes: [...]` in a GJS or Python
constructor, `gtk_widget_set_css_classes()`) keeps these: the panel puts its
own back.

## Colours on glass

Panels with adaptive colours set `color` and these variables, which their
children inherit:

| Variable | Use |
|---|---|
| `--glass-fg-color` | text and icons |
| `--glass-dim-fg-color` | secondary text |
| `--glass-hover-color` | hover backgrounds |
| `--glass-active-color` | pressed backgrounds |

```css
glasspanel.glass-dark .badge { color: var(--glass-dim-fg-color); }
```

Buttons on glass have no background of their own (the glass is their
background) unless they are `.suggested-action`, `.destructive-action` or
`.opaque`. Their content takes `--glass-fg-color` directly, so a theme that
colours buttons does not hide the adaptive colour.

## The parts

- Header bar: the button capsules are `glasspanel.header-capsule` (the window
  controls also `.window-controls`).
- Toggle group: the sliding plate's tint is the `color` of its `pill` child
  node: `glasspanel.toggle-group > pill { color: rgb(255 255 255 / 30%); }`.
- Switch and slider: the track is `glassswitch > glassview > track`
  (`background-color`), the rail `glassslider > glassview > rail` and its
  `fill` (`color`); the knobs are `.switch-knob` and `.slider-knob`.
- Popovers are `popover.glass` (menus `.glass-menu`), tinted with the `color`
  of `popover.glass > backdrop`; dialogs are `dialog.glass`, tinted with
  `glassdialogsurface > backdrop`.
- Sidebars: `glasspanel.glass-sidebar`. Do not give glass the `.sidebar`
  class: themes paint it opaque.
