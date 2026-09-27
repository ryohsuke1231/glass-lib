Title: How the Glass Works
Slug: concepts

# How the Glass Works

## The view draws the glass

A [class@Glass.Panel] does not draw its own glass: the [class@Glass.View]
above it does, in its snapshot. It draws its content, then — for each panel —
renders what is under the panel into a texture, blurs it and runs the glass
shader over it, then draws the panels' children on top. GTK snapshots the view
whenever anything in it changes, so the glass is always of the same frame as
the content under it: scrolling never shows a frame of lag.

What the glass captures is the content and whatever glass lies under the
panel; never a panel's own children. The text on a panel therefore cannot
feed back into its own colour.

Consequences for layouts:

- Put panels among a view's **overlay** children. Panels in the content, or
  outside any view, are frosted with CSS instead.
- Containers between a view and its panels should have **no background** of
  their own: it would cover the glass.
- The content fills the view and runs under the glass. Pad it yourself where
  it should not start hidden: [property@Glass.ToolbarView:top-bar-height],
  [property@Glass.ToolbarView:bottom-bar-height] and
  [property@Glass.SplitView:content-inset] say by how much.
- Where the content is transparent, the glass sees the view's
  [property@Glass.View:backdrop-color] (the window background by default).
- Content that changes every frame under the glass (an animation, a video)
  is rendered again for every panel's capture. Draw it with render nodes
  (`gtk_snapshot_append_linear_gradient()`, `…_color()`, textures): a cairo
  drawing (`GtkDrawingArea`) is rasterised on the CPU each time it is
  rendered, which for a window-sized drawing can take longer than a frame.

## Glass over glass

A panel inside another panel of the same view is a later **layer**: it
refracts the panel under it, and what that panel refracts. A view inside a
panel (a toolbar in a sidebar) draws its glass over the panel's glass.

Glass bodies are drawn before any panel's children, all of them. So within
one view, a panel's text is drawn over the other panels' glass. When content
with text scrolls under a bar (cards under a header), put the bar in an outer
view — [class@Glass.ToolbarView] is one — and the cards in an inner view:
the outer glass then refracts the cards and their text.

## Groups: glass that flows together

The panels in a [class@Glass.Group] are drawn as one body of glass: closer
than [property@Glass.Group:spacing] (16 px by default) they flow into each
other like drops of water; touching, they are one capsule. They share the
first panel's material and tint.

## Morphing

A panel shown while another with the same [property@Glass.Panel:morph-id]
hides takes over that panel's glass, which changes shape into its own (a
search button becoming a search field). In a group, a panel that is shown
comes out of its neighbour like a drop, and one that is hidden goes back
into it. Animations follow the "reduce motion" setting.

Morphs follow the layout frame by frame: to animate a change of size, animate
the allocation (a `GtkRevealer`, `GtkStack:interpolate-size`) and the glass
follows.

## Colours that follow the backdrop

With [property@Glass.Panel:adaptive] (the default for the `REGULAR` and
`CLEAR` materials) a panel measures what is under it and gets the style class
`.glass-light` (light glass, dark text) or `.glass-dark` (dark glass, light
text); [property@Glass.Panel:appearance] reports it. The switch is smoothed
over time and has hysteresis, so it does not flicker over busy content. It
works with the CSS fallback too: the view samples the content under its
panels at most ten times a second.

## When there is no OpenGL

Without OpenGL ES 3.0 / OpenGL 3.3, with the environment variable
`GLASS_RENDERER=fallback`, or with [property@Glass.Context:renderer] set to
`FALLBACK`, panels are frosted with CSS `backdrop-filter`: blurred, tinted,
without refraction. [method@Glass.View.get_active_renderer] says which is in
use.

## Accessibility

- **High contrast**: the glass becomes opaque with an outline.
- **Reduced transparency** ([property@Glass.Context:reduce-transparency]):
  no refraction, more blur, a stronger tint.
- **Reduced motion** (`gtk-enable-animations` off): no morphing or press
  animations, no colour transitions.
- The parts use the accessible roles of their libadwaita counterparts
  (`SWITCH`, `SLIDER`, `SEARCH_BOX`...); a panel is a `GROUP`.
