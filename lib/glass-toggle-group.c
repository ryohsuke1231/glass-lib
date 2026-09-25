/* glass-toggle-group.c — a segmented control on glass (design.md §6.6).
 *
 * A capsule of glass holding a row of buttons, one of them active. A light
 * rounded plate slides under the active one. The plate is not glass: glass
 * over glass needs the layered drawing of v2 (design.md §19).
 *
 * The buttons are plain GtkButtons marked with the style class `.active`,
 * not GtkToggleButtons: a theme's `button:checked` background (even one in
 * ~/.config/gtk-4.0/gtk.css, at USER priority, which nothing can override)
 * would otherwise cover the plate (docs/memo.md 地雷12).
 *
 * SPDX-License-Identifier: MIT
 */
#include "glass-private.h"

#include <adwaita.h>

/**
 * GlassToggleGroup:
 *
 * The glass counterpart of `AdwToggleGroup`: a row of toggles on a glass
 * capsule, one of them active, marked by a plate that slides to it.
 *
 * ## CSS nodes
 *
 * `GlassToggleGroup` is a [class@Panel] (CSS name `glasspanel`) with the
 * style class `.toggle-group`. The plate's colour is the `color` of its
 * never-drawn child node `pill`.
 */

struct _GlassToggleGroup {
  GlassPanel      parent_instance;

  GtkWidget      *box;
  GtkWidget      *pill_node;
  GPtrArray      *buttons;         /* GtkButton* */
  GPtrArray      *names;           /* char* */
  guint           active;

  AdwAnimation   *animation;
  double          progress;
  graphene_rect_t from;            /* the plate where the animation started */
  gboolean        have_from;
  graphene_rect_t shown;           /* the plate as last drawn */
};

enum {
  PROP_0,
  PROP_ACTIVE,
  PROP_ACTIVE_NAME,
  PROP_N_TOGGLES,
  N_PROPS
};

static GParamSpec *props[N_PROPS];

G_DEFINE_FINAL_TYPE (GlassToggleGroup, glass_toggle_group, GLASS_TYPE_PANEL)

static gboolean
active_rect (GlassToggleGroup *self, graphene_rect_t *out)
{
  GtkWidget *button;

  if (self->active >= self->buttons->len)
    return FALSE;
  button = g_ptr_array_index (self->buttons, self->active);
  return gtk_widget_compute_bounds (button, GTK_WIDGET (self), out);
}

static void
glass_toggle_group_snapshot (GtkWidget   *widget,
                             GtkSnapshot *snapshot)
{
  GlassToggleGroup *self = GLASS_TOGGLE_GROUP (widget);
  graphene_rect_t target, plate;
  GskRoundedRect rounded;
  GdkRGBA color;

  if (active_rect (self, &target))
    {
      /* Followed every frame, so the plate stays under the button through
       * relayouts; interpolated from where it was while it slides. */
      if (self->have_from && self->progress < 1.0)
        graphene_rect_interpolate (&self->from, &target, self->progress, &plate);
      else
        plate = target;
      self->shown = plate;

      gtk_widget_get_color (self->pill_node, &color);
      gsk_rounded_rect_init_from_rect (&rounded, &plate, MIN (plate.size.width, plate.size.height) / 2.0f);
      gtk_snapshot_push_rounded_clip (snapshot, &rounded);
      gtk_snapshot_append_color (snapshot, &color, &plate);
      gtk_snapshot_pop (snapshot);
    }

  /* Not the pill node: it only carries the colour. */
  gtk_widget_snapshot_child (widget, self->box, snapshot);
}

static void
glass_toggle_group_size_allocate (GtkWidget *widget,
                                  int        width,
                                  int        height,
                                  int        baseline)
{
  GlassToggleGroup *self = GLASS_TOGGLE_GROUP (widget);

  GTK_WIDGET_CLASS (glass_toggle_group_parent_class)->size_allocate (widget, width, height, baseline);
  gtk_widget_allocate (self->pill_node, 0, 0, -1, NULL);
}

static void
animation_value (double value, gpointer data)
{
  GlassToggleGroup *self = data;

  self->progress = value;
  gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
mark (GtkWidget *button, gboolean active)
{
  if (active)
    gtk_widget_add_css_class (button, "active");
  else
    gtk_widget_remove_css_class (button, "active");
  gtk_accessible_update_state (GTK_ACCESSIBLE (button),
                               GTK_ACCESSIBLE_STATE_PRESSED,
                               active ? GTK_ACCESSIBLE_TRISTATE_TRUE : GTK_ACCESSIBLE_TRISTATE_FALSE,
                               -1);
}

static void
set_active_internal (GlassToggleGroup *self, guint active, gboolean animate)
{
  if (active >= self->buttons->len || active == self->active)
    return;

  self->have_from = animate && gtk_widget_get_mapped (GTK_WIDGET (self));
  self->from = self->shown;
  if (self->active < self->buttons->len)
    mark (g_ptr_array_index (self->buttons, self->active), FALSE);
  self->active = active;
  mark (g_ptr_array_index (self->buttons, active), TRUE);

  if (self->have_from)
    {
      self->progress = 0.0;
      adw_animation_reset (self->animation);
      adw_animation_play (self->animation);
    }
  gtk_widget_queue_draw (GTK_WIDGET (self));

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ACTIVE]);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ACTIVE_NAME]);
}

static void
button_clicked (GtkButton        *button,
                GlassToggleGroup *self)
{
  guint index;

  if (g_ptr_array_find (self->buttons, button, &index))
    set_active_internal (self, index, TRUE);
}

static void
glass_toggle_group_dispose (GObject *object)
{
  GlassToggleGroup *self = GLASS_TOGGLE_GROUP (object);

  g_clear_object (&self->animation);
  g_clear_pointer (&self->pill_node, gtk_widget_unparent);

  G_OBJECT_CLASS (glass_toggle_group_parent_class)->dispose (object);
}

static void
glass_toggle_group_finalize (GObject *object)
{
  GlassToggleGroup *self = GLASS_TOGGLE_GROUP (object);

  g_ptr_array_unref (self->buttons);
  g_ptr_array_unref (self->names);

  G_OBJECT_CLASS (glass_toggle_group_parent_class)->finalize (object);
}

static void
glass_toggle_group_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  GlassToggleGroup *self = GLASS_TOGGLE_GROUP (object);

  switch (prop_id)
    {
    case PROP_ACTIVE:
      g_value_set_uint (value, self->active);
      break;
    case PROP_ACTIVE_NAME:
      g_value_set_string (value, glass_toggle_group_get_active_name (self));
      break;
    case PROP_N_TOGGLES:
      g_value_set_uint (value, self->buttons->len);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
glass_toggle_group_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  GlassToggleGroup *self = GLASS_TOGGLE_GROUP (object);

  switch (prop_id)
    {
    case PROP_ACTIVE:
      glass_toggle_group_set_active (self, g_value_get_uint (value));
      break;
    case PROP_ACTIVE_NAME:
      glass_toggle_group_set_active_name (self, g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
glass_toggle_group_class_init (GlassToggleGroupClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = glass_toggle_group_dispose;
  object_class->finalize = glass_toggle_group_finalize;
  object_class->get_property = glass_toggle_group_get_property;
  object_class->set_property = glass_toggle_group_set_property;

  widget_class->snapshot = glass_toggle_group_snapshot;
  widget_class->size_allocate = glass_toggle_group_size_allocate;

  /**
   * GlassToggleGroup:active:
   *
   * The index of the active toggle.
   */
  props[PROP_ACTIVE] =
    g_param_spec_uint ("active", NULL, NULL, 0, G_MAXUINT, 0,
                       G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * GlassToggleGroup:active-name:
   *
   * The name of the active toggle.
   */
  props[PROP_ACTIVE_NAME] =
    g_param_spec_string ("active-name", NULL, NULL, NULL,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * GlassToggleGroup:n-toggles:
   *
   * How many toggles there are.
   */
  props[PROP_N_TOGGLES] =
    g_param_spec_uint ("n-toggles", NULL, NULL, 0, G_MAXUINT, 0,
                       G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, props);
}

static void
glass_toggle_group_init (GlassToggleGroup *self)
{
  AdwAnimationTarget *target;

  self->buttons = g_ptr_array_new ();
  self->names = g_ptr_array_new_with_free_func (g_free);
  self->progress = 1.0;

  self->box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_set_homogeneous (GTK_BOX (self->box), TRUE);
  glass_panel_set_child (GLASS_PANEL (self), self->box);
  gtk_widget_add_css_class (GTK_WIDGET (self), "toggle-group");

  /* Visible (an invisible node's style is not kept up to date) but never
   * drawn: it resolves the plate's colour from CSS. */
  self->pill_node = glass_style_node_new ("pill");
  gtk_widget_set_parent (self->pill_node, GTK_WIDGET (self));

  target = adw_callback_animation_target_new (animation_value, self, NULL);
  self->animation = adw_timed_animation_new (GTK_WIDGET (self), 0.0, 1.0, 280, target);
  adw_timed_animation_set_easing (ADW_TIMED_ANIMATION (self->animation), ADW_EASE_OUT_CUBIC);
}

/**
 * glass_toggle_group_new:
 *
 * Returns: a new toggle group
 */
GtkWidget *
glass_toggle_group_new (void)
{
  return g_object_new (GLASS_TYPE_TOGGLE_GROUP, NULL);
}

/**
 * glass_toggle_group_append:
 * @self: a toggle group
 * @name: (nullable): the toggle's name
 * @label: (nullable): its text
 * @icon_name: (nullable): its icon, used when there is no @label (and as
 *   the tooltip's companion otherwise)
 *
 * Adds a toggle at the end. The first one added is active.
 */
void
glass_toggle_group_append (GlassToggleGroup *self,
                           const char       *name,
                           const char       *label,
                           const char       *icon_name)
{
  GtkWidget *button;

  g_return_if_fail (GLASS_IS_TOGGLE_GROUP (self));
  g_return_if_fail (label != NULL || icon_name != NULL);

  button = label ? gtk_button_new_with_label (label) : gtk_button_new_from_icon_name (icon_name);
  if (!label && name)
    gtk_widget_set_tooltip_text (button, name);
  gtk_widget_add_css_class (button, "toggle");
  mark (button, self->buttons->len == 0);

  g_signal_connect_object (button, "clicked", G_CALLBACK (button_clicked), self, 0);
  gtk_box_append (GTK_BOX (self->box), button);
  g_ptr_array_add (self->buttons, button);
  g_ptr_array_add (self->names, g_strdup (name));

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_N_TOGGLES]);
  if (self->buttons->len == 1)
    {
      g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ACTIVE]);
      g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ACTIVE_NAME]);
    }
}

/**
 * glass_toggle_group_get_n_toggles:
 * @self: a toggle group
 *
 * Returns: how many toggles there are
 */
guint
glass_toggle_group_get_n_toggles (GlassToggleGroup *self)
{
  g_return_val_if_fail (GLASS_IS_TOGGLE_GROUP (self), 0);

  return self->buttons->len;
}

/**
 * glass_toggle_group_get_active:
 * @self: a toggle group
 *
 * Returns: the index of the active toggle
 */
guint
glass_toggle_group_get_active (GlassToggleGroup *self)
{
  g_return_val_if_fail (GLASS_IS_TOGGLE_GROUP (self), 0);

  return self->active;
}

/**
 * glass_toggle_group_set_active:
 * @self: a toggle group
 * @active: the index of the toggle to activate
 *
 * Activates a toggle.
 */
void
glass_toggle_group_set_active (GlassToggleGroup *self,
                               guint             active)
{
  g_return_if_fail (GLASS_IS_TOGGLE_GROUP (self));

  set_active_internal (self, active, TRUE);
}

/**
 * glass_toggle_group_get_active_name:
 * @self: a toggle group
 *
 * Returns: (nullable): the name of the active toggle
 */
const char *
glass_toggle_group_get_active_name (GlassToggleGroup *self)
{
  g_return_val_if_fail (GLASS_IS_TOGGLE_GROUP (self), NULL);

  return self->active < self->names->len ? g_ptr_array_index (self->names, self->active) : NULL;
}

/**
 * glass_toggle_group_set_active_name:
 * @self: a toggle group
 * @name: the name of the toggle to activate
 *
 * Activates a toggle by name.
 */
void
glass_toggle_group_set_active_name (GlassToggleGroup *self,
                                    const char       *name)
{
  g_return_if_fail (GLASS_IS_TOGGLE_GROUP (self));

  for (guint i = 0; i < self->names->len; i++)
    if (g_strcmp0 (g_ptr_array_index (self->names, i), name) == 0)
      {
        set_active_internal (self, i, TRUE);
        return;
      }
}
