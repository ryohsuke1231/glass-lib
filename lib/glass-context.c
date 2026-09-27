/* glass-context.c — library-wide settings (design.md §6.1, §11).
 *
 * SPDX-License-Identifier: MIT
 */
#include "glass-private.h"

#include <adwaita.h>
#include <math.h>
#include <string.h>

/**
 * GlassContext:
 *
 * The library-wide settings: which renderer draws the glass, whether
 * transparency is reduced, the optical parameters and the tint.
 *
 * A parameter's value comes from the first of: a value set on the panel
 * with [method@Panel.set_param], a value set here with
 * [method@Context.set_param], the value of the panel's [enum@Material],
 * the default (the GNOME Shell extension's). Setting a value here is how
 * an app (or the demo's Lab) tunes every piece of glass at once.
 *
 * The tint is a colour the glass mixes in: the `tint-strength` parameter
 * says how much, [property@Context:tint-color] which colour (by default
 * the material's). A panel's own [property@Panel:tint] takes precedence
 * over both.
 */
struct _GlassContext {
  GObject           parent_instance;

  GlassRendererMode renderer;
  GlassRendererMode env_renderer;   /* GLASS_RENDERER, AUTO if unset */
  gboolean          reduce_transparency;
  gboolean          high_contrast;
  gboolean          is_set[GLASS_N_PARAMS];
  double            values[GLASS_N_PARAMS];
  GdkRGBA           tint_color;
  gboolean          tint_color_set;
  guint             generation;
};

enum {
  PROP_0,
  PROP_RENDERER,
  PROP_REDUCE_TRANSPARENCY,
  PROP_TINT_COLOR,
  N_PROPS
};

enum {
  CHANGED,
  N_SIGNALS
};

static GParamSpec *props[N_PROPS];
static guint signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE (GlassContext, glass_context, G_TYPE_OBJECT)

static void
changed (GlassContext *self)
{
  self->generation++;
  g_signal_emit (self, signals[CHANGED], 0);
}

static void
high_contrast_changed (AdwStyleManager *manager,
                       GParamSpec      *pspec,
                       GlassContext    *self)
{
  gboolean hc = adw_style_manager_get_high_contrast (manager);

  if (hc == self->high_contrast)
    return;
  self->high_contrast = hc;
  changed (self);
}

static void
glass_context_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  GlassContext *self = GLASS_CONTEXT (object);

  switch (prop_id)
    {
    case PROP_RENDERER:
      g_value_set_enum (value, self->renderer);
      break;
    case PROP_REDUCE_TRANSPARENCY:
      g_value_set_boolean (value, self->reduce_transparency);
      break;
    case PROP_TINT_COLOR:
      g_value_set_boxed (value, self->tint_color_set ? &self->tint_color : NULL);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
glass_context_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  GlassContext *self = GLASS_CONTEXT (object);

  switch (prop_id)
    {
    case PROP_RENDERER:
      glass_context_set_renderer (self, g_value_get_enum (value));
      break;
    case PROP_REDUCE_TRANSPARENCY:
      glass_context_set_reduce_transparency (self, g_value_get_boolean (value));
      break;
    case PROP_TINT_COLOR:
      glass_context_set_tint_color (self, g_value_get_boxed (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
glass_context_class_init (GlassContextClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = glass_context_get_property;
  object_class->set_property = glass_context_set_property;

  /**
   * GlassContext:renderer:
   *
   * How glass is drawn. The environment variable `GLASS_RENDERER`
   * (`full` or `fallback`) takes precedence.
   */
  props[PROP_RENDERER] =
    g_param_spec_enum ("renderer", NULL, NULL,
                       GLASS_TYPE_RENDERER_MODE, GLASS_RENDERER_MODE_AUTO,
                       G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * GlassContext:reduce-transparency:
   *
   * Draws glass without refraction, more blurred and more opaque.
   */
  props[PROP_REDUCE_TRANSPARENCY] =
    g_param_spec_boolean ("reduce-transparency", NULL, NULL, FALSE,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * GlassContext:tint-color:
   *
   * The colour every panel's glass is tinted with, unless the panel has a
   * [property@Panel:tint] of its own; %NULL for each material's colour
   * (white, or the theme's window or popover background).
   *
   * Only red, green and blue are used: how much of it the glass mixes in
   * is the `tint-strength` parameter.
   */
  props[PROP_TINT_COLOR] =
    g_param_spec_boxed ("tint-color", NULL, NULL, GDK_TYPE_RGBA,
                        G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, props);

  /**
   * GlassContext::changed:
   *
   * Emitted when anything that affects how glass looks changes.
   */
  signals[CHANGED] = g_signal_new ("changed", G_TYPE_FROM_CLASS (klass),
                                   G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                   G_TYPE_NONE, 0);
}

static void
glass_context_init (GlassContext *self)
{
  const char *env = g_getenv ("GLASS_RENDERER");

  self->renderer = GLASS_RENDERER_MODE_AUTO;
  self->env_renderer = GLASS_RENDERER_MODE_AUTO;
  if (env && g_ascii_strcasecmp (env, "full") == 0)
    self->env_renderer = GLASS_RENDERER_MODE_FULL;
  else if (env && g_ascii_strcasecmp (env, "fallback") == 0)
    self->env_renderer = GLASS_RENDERER_MODE_FALLBACK;
  else if (env && *env)
    g_warning ("GLASS_RENDERER=%s: expected full or fallback", env);

  for (int i = 0; i < GLASS_N_PARAMS; i++)
    self->values[i] = glass_param_specs[i].default_value;

  if (adw_is_initialized ())
    {
      AdwStyleManager *manager = adw_style_manager_get_default ();

      self->high_contrast = adw_style_manager_get_high_contrast (manager);
      g_signal_connect_object (manager, "notify::high-contrast",
                               G_CALLBACK (high_contrast_changed), self, 0);
    }
}

/**
 * glass_context_get_default:
 *
 * Gets the library's settings: there is one context, shared by every
 * window and view.
 *
 * Returns: (transfer none): the context
 */
GlassContext *
glass_context_get_default (void)
{
  static GlassContext *context;

  if (g_once_init_enter_pointer (&context))
    g_once_init_leave_pointer (&context, g_object_new (GLASS_TYPE_CONTEXT, NULL));

  return context;
}

/**
 * glass_context_get_renderer:
 * @self: a context
 *
 * Gets the renderer setting.
 *
 * Returns: the renderer setting
 */
GlassRendererMode
glass_context_get_renderer (GlassContext *self)
{
  g_return_val_if_fail (GLASS_IS_CONTEXT (self), GLASS_RENDERER_MODE_AUTO);

  return self->renderer;
}

/**
 * glass_context_set_renderer:
 * @self: a context
 * @mode: the renderer
 *
 * Sets how glass is drawn.
 */
void
glass_context_set_renderer (GlassContext      *self,
                            GlassRendererMode  mode)
{
  g_return_if_fail (GLASS_IS_CONTEXT (self));

  if (self->renderer == mode)
    return;
  self->renderer = mode;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_RENDERER]);
  changed (self);
}

GlassRendererMode
glass_context_get_wanted_renderer (GlassContext *self)
{
  GlassRendererMode mode = self->env_renderer != GLASS_RENDERER_MODE_AUTO
                           ? self->env_renderer : self->renderer;

  return mode == GLASS_RENDERER_MODE_FALLBACK ? GLASS_RENDERER_MODE_FALLBACK
                                               : GLASS_RENDERER_MODE_FULL;
}

/**
 * glass_context_get_reduce_transparency:
 * @self: a context
 *
 * Gets whether transparency is reduced.
 *
 * Returns: whether transparency is reduced
 */
gboolean
glass_context_get_reduce_transparency (GlassContext *self)
{
  g_return_val_if_fail (GLASS_IS_CONTEXT (self), FALSE);

  return self->reduce_transparency;
}

/**
 * glass_context_set_reduce_transparency:
 * @self: a context
 * @reduce: whether to reduce transparency
 *
 * Draws glass without refraction, more blurred and more opaque.
 */
void
glass_context_set_reduce_transparency (GlassContext *self,
                                       gboolean      reduce)
{
  g_return_if_fail (GLASS_IS_CONTEXT (self));

  reduce = !!reduce;
  if (self->reduce_transparency == reduce)
    return;
  self->reduce_transparency = reduce;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_REDUCE_TRANSPARENCY]);
  changed (self);
}

/**
 * glass_context_get_tint_color:
 * @self: a context
 * @color: (out) (optional): the colour, if set
 *
 * Gets the tint colour set with [method@Context.set_tint_color].
 *
 * Returns: %TRUE if a tint colour is set, %FALSE if each material uses its
 *   own
 */
gboolean
glass_context_get_tint_color (GlassContext *self,
                              GdkRGBA      *color)
{
  g_return_val_if_fail (GLASS_IS_CONTEXT (self), FALSE);

  if (color && self->tint_color_set)
    *color = self->tint_color;
  return self->tint_color_set;
}

/**
 * glass_context_set_tint_color:
 * @self: a context
 * @color: (nullable): the colour (its alpha is not used), or %NULL for
 *   each material's
 *
 * Sets the colour every panel's glass is tinted with, unless the panel has
 * a [property@Panel:tint] of its own. How much of it the glass mixes in is
 * the `tint-strength` parameter.
 */
void
glass_context_set_tint_color (GlassContext  *self,
                              const GdkRGBA *color)
{
  GdkRGBA opaque;

  g_return_if_fail (GLASS_IS_CONTEXT (self));

  if (color)
    {
      opaque = *color;
      opaque.alpha = 1.0f;
    }
  if (color == NULL && !self->tint_color_set)
    return;
  if (color && self->tint_color_set && gdk_rgba_equal (&opaque, &self->tint_color))
    return;
  self->tint_color_set = color != NULL;
  if (color)
    self->tint_color = opaque;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_TINT_COLOR]);
  changed (self);
}

static int
find_param (const char *key)
{
  if (key == NULL)
    return -1;
  for (int i = 0; i < GLASS_N_PARAMS; i++)
    if (g_str_equal (glass_param_specs[i].key, key))
      return i;
  return -1;
}

int
glass_param_check (const char *func,
                   const char *key,
                   double      value,
                   double     *out)
{
  const GlassParamSpec *spec;
  int i = find_param (key);

  if (i < 0)
    {
      g_warning ("%s: no parameter %s", func, key ? key : "(null)");
      return -1;
    }
  spec = &glass_param_specs[i];

  if (!isfinite (value) || value < spec->min || value > spec->max)
    {
      g_warning ("%s: %s = %g is outside %g..%g", func, key, value, spec->min, spec->max);
      value = isfinite (value) ? CLAMP (value, spec->min, spec->max) : spec->default_value;
    }
  if (spec->values)
    {
      /* Only meaningful values (the extension's rule): snap to the nearest. */
      double best = spec->values[0];

      for (int j = 1; spec->values[j] >= 0.0; j++)
        if (fabs (spec->values[j] - value) < fabs (best - value))
          best = spec->values[j];
      value = best;
    }

  *out = value;
  return i;
}

int
glass_param_find (const char *key)
{
  return find_param (key);
}

/**
 * glass_context_set_param:
 * @self: a context
 * @key: a parameter, one of [method@Context.list_params]; the
 *   `GLASS_PARAM_…` constants name them (%GLASS_PARAM_BLUR_RADIUS …)
 * @value: its value
 *
 * Sets a parameter for every panel, whatever its material (a panel's own
 * value, [method@Panel.set_param], still wins). Values outside the
 * parameter's range are clamped, with a warning.
 *
 * Returns: %FALSE if @key is not a parameter
 */
gboolean
glass_context_set_param (GlassContext *self,
                         const char   *key,
                         double        value)
{
  int i;

  g_return_val_if_fail (GLASS_IS_CONTEXT (self), FALSE);

  i = glass_param_check ("glass_context_set_param", key, value, &value);
  if (i < 0)
    return FALSE;

  if (self->is_set[i] && self->values[i] == value)
    return TRUE;
  self->is_set[i] = TRUE;
  self->values[i] = value;
  changed (self);

  return TRUE;
}

/**
 * glass_context_set_lens:
 * @self: a context
 * @lens: the lens
 *
 * Gives every panel one of the measured lenses: sets the context's values
 * of `max-z`, `profile-shape-n` and `displacement-scale` together (a
 * panel's own values, [method@Panel.set_param] or [method@Panel.set_lens],
 * still win). [method@Context.reset_param] on those three keys goes back
 * to each material's lens.
 *
 * Since: 0.9
 */
void
glass_context_set_lens (GlassContext *self,
                        GlassLens     lens)
{
  const int keys[3] = { GLASS_PARAM_ID_MAX_Z, GLASS_PARAM_ID_PROFILE_SHAPE_N, GLASS_PARAM_ID_DISPLACEMENT_SCALE };
  double values[3];
  gboolean any = FALSE;

  g_return_if_fail (GLASS_IS_CONTEXT (self));
  g_return_if_fail ((int) lens >= 0 && (int) lens < GLASS_N_LENSES);

  values[0] = glass_lens_specs[lens].max_z;
  values[1] = glass_lens_specs[lens].profile_shape_n;
  values[2] = glass_lens_specs[lens].displacement_scale;
  for (int k = 0; k < 3; k++)
    if (!self->is_set[keys[k]] || self->values[keys[k]] != values[k])
      {
        self->is_set[keys[k]] = TRUE;
        self->values[keys[k]] = values[k];
        any = TRUE;
      }
  if (any)
    changed (self);   /* once for the three */
}

/**
 * glass_context_get_param:
 * @self: a context
 * @key: a parameter
 *
 * Gets the value set with [method@Context.set_param], or the default; NaN if
 * @key is not a parameter.
 *
 * Returns: the value set with [method@Context.set_param], or the default;
 *   NaN if @key is not a parameter
 */
double
glass_context_get_param (GlassContext *self,
                         const char   *key)
{
  int i;

  g_return_val_if_fail (GLASS_IS_CONTEXT (self), NAN);

  i = find_param (key);
  return i < 0 ? NAN : self->values[i];
}

/**
 * glass_context_is_param_set:
 * @self: a context
 * @key: a parameter
 *
 * Gets whether @key was set with [method@Context.set_param] (and so
 * overrides the materials' values).
 *
 * Returns: whether @key was set with [method@Context.set_param] (and so
 *   overrides the materials' values)
 */
gboolean
glass_context_is_param_set (GlassContext *self,
                            const char   *key)
{
  int i;

  g_return_val_if_fail (GLASS_IS_CONTEXT (self), FALSE);

  i = find_param (key);
  return i >= 0 && self->is_set[i];
}

/**
 * glass_context_get_effective_param:
 * @self: a context
 * @material: a material
 * @key: a parameter
 *
 * Gets the value panels of @material use (unless they have their own): the
 * one set with [method@Context.set_param], else the material's, else the
 * default; NaN if @key is not a parameter.
 *
 * Returns: the value panels of @material use (unless they have their own):
 *   the one set with [method@Context.set_param], else the material's, else
 *   the default; NaN if @key is not a parameter
 */
double
glass_context_get_effective_param (GlassContext  *self,
                                   GlassMaterial  material,
                                   const char    *key)
{
  double values[GLASS_N_PARAMS];
  int i;

  g_return_val_if_fail (GLASS_IS_CONTEXT (self), NAN);

  i = find_param (key);
  if (i < 0)
    return NAN;
  glass_context_resolve (self, material, values);
  return values[i];
}

/**
 * glass_context_reset_param:
 * @self: a context
 * @key: a parameter
 *
 * Returns @key to the materials' values.
 */
void
glass_context_reset_param (GlassContext *self,
                           const char   *key)
{
  int i;

  g_return_if_fail (GLASS_IS_CONTEXT (self));

  i = find_param (key);
  if (i < 0 || !self->is_set[i])
    return;
  self->is_set[i] = FALSE;
  self->values[i] = glass_param_specs[i].default_value;
  changed (self);
}

/**
 * glass_context_list_params:
 * @self: a context
 *
 * Gets the parameters' keys.
 *
 * Returns: (transfer none) (array zero-terminated=1): the parameters' keys
 */
const char * const *
glass_context_list_params (GlassContext *self)
{
  return glass_param_keys;
}

/**
 * glass_context_get_param_range:
 * @self: a context
 * @key: a parameter
 * @min: (out) (optional): the smallest value
 * @max: (out) (optional): the largest value
 * @default_value: (out) (optional): the default (the GNOME Shell extension's)
 *
 * Gets the range of a parameter's values and its default.
 *
 * Returns: %FALSE if @key is not a parameter
 */
gboolean
glass_context_get_param_range (GlassContext *self,
                               const char   *key,
                               double       *min,
                               double       *max,
                               double       *default_value)
{
  int i = find_param (key);

  if (i < 0)
    return FALSE;
  if (min)
    *min = glass_param_specs[i].min;
  if (max)
    *max = glass_param_specs[i].max;
  if (default_value)
    *default_value = glass_param_specs[i].default_value;
  return TRUE;
}

void
glass_context_resolve (GlassContext *self,
                       GlassMaterial material,
                       double        out[GLASS_N_PARAMS])
{
  const GlassMaterialSpec *m = &glass_material_specs[CLAMP ((int) material, 0, GLASS_N_MATERIALS - 1)];

  for (int i = 0; i < GLASS_N_PARAMS; i++)
    out[i] = self->is_set[i] ? self->values[i]
             : m->has[i] ? m->values[i]
             : glass_param_specs[i].default_value;
}

void
glass_reduce_transparency (double params[GLASS_N_PARAMS],
                           float  tint[4])
{
  params[GLASS_PARAM_ID_DISPLACEMENT_SCALE] = 0.0;
  params[GLASS_PARAM_ID_CHROMA_STRENGTH] = 0.0;
  params[GLASS_PARAM_ID_BLUR_RADIUS] = MIN (MAX (params[GLASS_PARAM_ID_BLUR_RADIUS] * 3.0, 8.0), 30.0);
  tint[3] = MAX (tint[3], 0.6f);
}

guint
glass_context_get_generation (GlassContext *self)
{
  return self->generation;
}

gboolean
glass_context_get_high_contrast (GlassContext *self)
{
  return self->high_contrast;
}

void
glass_context_resolve_tint (GlassContext  *self,
                            GlassMaterial  material,
                            double         strength,
                            const GdkRGBA *theme_bg,
                            float          out[4])
{
  const GlassMaterialSpec *m = &glass_material_specs[CLAMP ((int) material, 0, GLASS_N_MATERIALS - 1)];

  if (self->tint_color_set)
    {
      out[0] = self->tint_color.red;
      out[1] = self->tint_color.green;
      out[2] = self->tint_color.blue;
    }
  else if (m->tint_from_theme && theme_bg)
    {
      out[0] = theme_bg->red;
      out[1] = theme_bg->green;
      out[2] = theme_bg->blue;
    }
  else
    memcpy (out, m->tint, sizeof m->tint);
  out[3] = (float) strength;
}
