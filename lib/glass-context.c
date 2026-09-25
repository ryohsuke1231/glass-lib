/* glass-context.c — library-wide settings (design.md §6.1, §11).
 *
 * SPDX-License-Identifier: MIT
 */
#include "glass-private.h"

#include <adwaita.h>
#include <math.h>

/**
 * GlassContext:
 *
 * The library-wide settings: which renderer draws the glass, whether
 * transparency is reduced, and the optical parameters.
 *
 * A parameter's value comes from the first of: a value set with
 * [method@Context.set_param], the value of the panel's [enum@Material],
 * the default (the GNOME Shell extension's).
 */
struct _GlassContext {
  GObject           parent_instance;

  GlassRendererMode renderer;
  GlassRendererMode env_renderer;   /* GLASS_RENDERER, AUTO if unset */
  gboolean          reduce_transparency;
  gboolean          high_contrast;
  gboolean          is_set[GLASS_N_PARAMS];
  double            values[GLASS_N_PARAMS];
  guint             generation;
};

enum {
  PROP_0,
  PROP_RENDERER,
  PROP_REDUCE_TRANSPARENCY,
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

/**
 * glass_context_set_param:
 * @self: a context
 * @key: a parameter, one of [method@Context.list_params]
 * @value: its value
 *
 * Sets an optical parameter for every panel, whatever its material. Values
 * outside the parameter's range are clamped, with a warning.
 *
 * Returns: %FALSE if @key is not a parameter
 */
gboolean
glass_context_set_param (GlassContext *self,
                         const char   *key,
                         double        value)
{
  const GlassParamSpec *spec;
  int i;

  g_return_val_if_fail (GLASS_IS_CONTEXT (self), FALSE);

  i = find_param (key);
  if (i < 0)
    {
      g_warning ("glass_context_set_param: no parameter %s", key ? key : "(null)");
      return FALSE;
    }
  spec = &glass_param_specs[i];

  if (!isfinite (value) || value < spec->min || value > spec->max)
    {
      g_warning ("glass_context_set_param: %s = %g is outside %g..%g", key, value, spec->min, spec->max);
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

  if (self->is_set[i] && self->values[i] == value)
    return TRUE;
  self->is_set[i] = TRUE;
  self->values[i] = value;
  changed (self);

  return TRUE;
}

/**
 * glass_context_get_param:
 * @self: a context
 * @key: a parameter
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
 * Returns: the value panels of @material use: the one set with
 *   [method@Context.set_param], else the material's, else the default;
 *   NaN if @key is not a parameter
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
