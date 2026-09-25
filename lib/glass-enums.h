/* glass-enums.h — glass-lib's enumerations (design.md §6.2).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#if !defined(GLASS_INSIDE) && !defined(GLASS_COMPILATION)
#error "Only <glass.h> can be included directly."
#endif

#include <glib.h>

G_BEGIN_DECLS

/**
 * GlassRendererMode:
 * @GLASS_RENDERER_MODE_AUTO: the full renderer when OpenGL works, the fallback otherwise
 * @GLASS_RENDERER_MODE_FULL: refraction, rim light and shadow, drawn with OpenGL
 * @GLASS_RENDERER_MODE_FALLBACK: frosted glass drawn with CSS `backdrop-filter`, no refraction
 *
 * How glass is drawn.
 */
typedef enum {
  GLASS_RENDERER_MODE_AUTO,
  GLASS_RENDERER_MODE_FULL,
  GLASS_RENDERER_MODE_FALLBACK,
} GlassRendererMode;

/**
 * GlassMaterial:
 * @GLASS_MATERIAL_REGULAR: toolbars, titles and button backgrounds
 * @GLASS_MATERIAL_CLEAR: over photos and video: less blur and tint
 * @GLASS_MATERIAL_THICK: large surfaces such as sidebars: heavily frosted,
 *   tinted with the window colour, with the theme's foreground colours
 *
 * The kind of glass a [class@Panel] is made of.
 */
typedef enum {
  GLASS_MATERIAL_REGULAR,
  GLASS_MATERIAL_CLEAR,
  GLASS_MATERIAL_THICK,
} GlassMaterial;

/**
 * GlassAdaptiveMode:
 * @GLASS_ADAPTIVE_MODE_AUTO: follow what is under the glass
 * @GLASS_ADAPTIVE_MODE_PREFER_LIGHT: like @GLASS_ADAPTIVE_MODE_AUTO, but
 *   ambiguous backgrounds give a light glass (dark foreground)
 * @GLASS_ADAPTIVE_MODE_PREFER_DARK: like @GLASS_ADAPTIVE_MODE_AUTO, but
 *   ambiguous backgrounds give a dark glass (light foreground)
 * @GLASS_ADAPTIVE_MODE_OFF: never switch: the theme's colours
 *
 * How a [class@Panel] picks the colour of what is on it.
 */
typedef enum {
  GLASS_ADAPTIVE_MODE_AUTO,
  GLASS_ADAPTIVE_MODE_PREFER_LIGHT,
  GLASS_ADAPTIVE_MODE_PREFER_DARK,
  GLASS_ADAPTIVE_MODE_OFF,
} GlassAdaptiveMode;

/**
 * GlassAppearance:
 * @GLASS_APPEARANCE_UNKNOWN: not measured (yet), or adaptive colours are off
 * @GLASS_APPEARANCE_LIGHT: the glass looks light: its foreground is dark
 * @GLASS_APPEARANCE_DARK: the glass looks dark: its foreground is light
 *
 * How light a [class@Panel] looks, as measured from what is under it.
 */
typedef enum {
  GLASS_APPEARANCE_UNKNOWN,
  GLASS_APPEARANCE_LIGHT,
  GLASS_APPEARANCE_DARK,
} GlassAppearance;

/**
 * GlassEdgeStyle:
 * @GLASS_EDGE_STYLE_NONE: no effect
 * @GLASS_EDGE_STYLE_SOFT: the content under the bar is blurred and fades
 *   into the window background towards the edge
 * @GLASS_EDGE_STYLE_HARD: the content under the bar is evenly blurred and
 *   separated from the rest by a line
 *
 * The scroll edge effect under the bars of a [class@ToolbarView].
 */
typedef enum {
  GLASS_EDGE_STYLE_NONE,
  GLASS_EDGE_STYLE_SOFT,
  GLASS_EDGE_STYLE_HARD,
} GlassEdgeStyle;

G_END_DECLS
