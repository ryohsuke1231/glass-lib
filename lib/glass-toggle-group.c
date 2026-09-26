/* glass-toggle-group.c — a segmented control on glass (design.md §6.6).
 *
 * A capsule of glass holding a row of buttons, one of them active. Under the
 * active one lies a plate that is glass on the glass (a nested GlassPanel,
 * drawn by the view in a later layer, design.md §6.7): it slides to the
 * toggle that becomes active, and swells like a lens while the group is
 * pressed.
 *
 * The buttons are plain GtkButtons marked with the style class `.active`,
 * not GtkToggleButtons: a theme's `button:checked` background (even one in
 * ~/.config/gtk-4.0/gtk.css, at USER priority, which nothing can override)
 * would otherwise cover the plate (docs/memo.md 地雷12). Each is drawn
 * inside a pill (GlassPillBox), so its hover background is round.
 *
 * SPDX-License-Identifier: MIT
 */
#include "glass-private.h"

#include <adwaita.h>

/**
 * GlassToggleGroup:
 *
 * The glass counterpart of `AdwToggleGroup`: a row of toggles on a glass
 * capsule, one of them active, marked by a plate of glass that slides to
 * it and swells while the group is pressed.
 *
 * ## CSS nodes
 *
 * `GlassToggleGroup` is a [class@Panel] (CSS name `glasspanel`) with the
 * style class `.toggle-group`. The plate is a [class@Panel] with the style
 * class `.toggle-plate`; its tint is the `color` of the never-drawn child
 * node `pill`.
 */

struct _GlassToggleGroup {
  GlassPanel      parent_instance;

  GtkWidget      *box;
  GtkWidget      *plate;           /* GlassPanel, glass on our glass */
  GtkWidget      *pill_node;       /* resolves the plate's tint from CSS */
  GPtrArray      *buttons;         /* GtkButton* */
  GPtrArray      *names;           /* char* */
  guint           active;

  AdwAnimation   *animation;
  double          progress;
  graphene_rect_t from;            /* the plate where the animation started */
  gboolean        have_from;
  graphene_rect_t shown;           /* the plate as last allocated */
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

static void append_button (GlassToggleGroup *self, const char *name, GtkWidget *button);

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
update_plate_tint (GlassToggleGroup *self)
{
  GdkRGBA color;

  gtk_widget_get_color (self->pill_node, &color);
  glass_panel_set_tint (GLASS_PANEL (self->plate), &color);
}

static void
glass_toggle_group_size_allocate (GtkWidget *widget,
                                  int        width,
                                  int        height,
                                  int        baseline)
{
  GlassToggleGroup *self = GLASS_TOGGLE_GROUP (widget);
  graphene_rect_t target, plate;

  GTK_WIDGET_CLASS (glass_toggle_group_parent_class)->size_allocate (widget, width, height, baseline);
  gtk_widget_allocate (self->pill_node, 0, 0, -1, NULL);

  /* The buttons are placed now; the plate follows the active one, from
   * where it was while it slides. */
  if (!active_rect (self, &target))
    {
      gtk_widget_set_child_visible (self->plate, FALSE);
      return;
    }
  if (self->have_from && self->progress < 1.0)
    graphene_rect_interpolate (&self->from, &target, self->progress, &plate);
  else
    plate = target;
  self->shown = plate;

  gtk_widget_set_child_visible (self->plate, TRUE);
  gtk_widget_size_allocate (self->plate,
                            &(GtkAllocation) { (int) roundf (plate.origin.x), (int) roundf (plate.origin.y),
                                               (int) roundf (plate.size.width), (int) roundf (plate.size.height) },
                            -1);
}

static void
glass_toggle_group_css_changed (GtkWidget         *widget,
                                GtkCssStyleChange *change)
{
  GTK_WIDGET_CLASS (glass_toggle_group_parent_class)->css_changed (widget, change);
  update_plate_tint (GLASS_TOGGLE_GROUP (widget));
}

static void
glass_toggle_group_root (GtkWidget *widget)
{
  GTK_WIDGET_CLASS (glass_toggle_group_parent_class)->root (widget);
  update_plate_tint (GLASS_TOGGLE_GROUP (widget));
}

static void
animation_value (double value, gpointer data)
{
  GlassToggleGroup *self = data;

  self->progress = value;
  gtk_widget_queue_allocate (GTK_WIDGET (self));
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
  gtk_widget_queue_allocate (GTK_WIDGET (self));

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

/* The plate lies under the buttons, so it never sees the press: the group
 * watches it (in the capture phase, without taking it) for the plate. */
static gboolean
press_event (GtkEventControllerLegacy *controller,
             GdkEvent                 *event,
             GlassToggleGroup         *self)
{
  switch ((int) gdk_event_get_event_type (event))
    {
    case GDK_BUTTON_PRESS:
    case GDK_TOUCH_BEGIN:
      glass_panel_set_pressed (GLASS_PANEL (self->plate), TRUE);
      break;
    case GDK_BUTTON_RELEASE:
    case GDK_TOUCH_END:
    case GDK_TOUCH_CANCEL:
    case GDK_GRAB_BROKEN:
      glass_panel_set_pressed (GLASS_PANEL (self->plate), FALSE);
      break;
    default:
      break;
    }
  return GDK_EVENT_PROPAGATE;
}

static void
glass_toggle_group_dispose (GObject *object)
{
  GlassToggleGroup *self = GLASS_TOGGLE_GROUP (object);

  g_clear_object (&self->animation);
  g_clear_pointer (&self->pill_node, gtk_widget_unparent);
  g_clear_pointer (&self->plate, gtk_widget_unparent);

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

  widget_class->size_allocate = glass_toggle_group_size_allocate;
  widget_class->css_changed = glass_toggle_group_css_changed;
  widget_class->root = glass_toggle_group_root;

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
  GtkEventController *press;

  self->buttons = g_ptr_array_new ();
  self->names = g_ptr_array_new_with_free_func (g_free);
  self->progress = 1.0;

  /* Each toggle is drawn inside a pill, like the plate under it. */
  self->box = glass_pill_box_new ();
  glass_pill_box_set_homogeneous (GLASS_PILL_BOX (self->box), TRUE);
  glass_panel_set_child (GLASS_PANEL (self), self->box);
  glass_panel_add_own_class (GLASS_PANEL (self), "toggle-group");

  /* The plate: glass on our glass, under the buttons (drawn first). */
  self->plate = glass_panel_new ();
  gtk_widget_add_css_class (self->plate, "toggle-plate");
  glass_panel_set_has_shadow (GLASS_PANEL (self->plate), FALSE);
  glass_panel_set_adaptive (GLASS_PANEL (self->plate), GLASS_ADAPTIVE_MODE_OFF);
  glass_panel_set_press_grow (GLASS_PANEL (self->plate), 12.0, 0.2);
  gtk_widget_set_can_target (self->plate, FALSE);
  gtk_widget_insert_before (self->plate, GTK_WIDGET (self), self->box);

  /* Visible (an invisible node's style is not kept up to date) but never
   * drawn: it resolves the plate's tint from CSS. */
  self->pill_node = glass_style_node_new ("pill");
  gtk_widget_set_parent (self->pill_node, GTK_WIDGET (self));

  press = gtk_event_controller_legacy_new ();
  gtk_event_controller_set_propagation_phase (press, GTK_PHASE_CAPTURE);
  g_signal_connect (press, "event", G_CALLBACK (press_event), self);
  gtk_widget_add_controller (GTK_WIDGET (self), press);

  target = adw_callback_animation_target_new (animation_value, self, NULL);
  self->animation = adw_timed_animation_new (GTK_WIDGET (self), 0.0, 1.0, 280, target);
  adw_timed_animation_set_easing (ADW_TIMED_ANIMATION (self->animation), ADW_EASE_OUT_CUBIC);
}

/**
 * glass_toggle_group_new:
 *
 * Creates a new toggle group.
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
 * @icon_name: (nullable): its icon, used when there is no @label
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
  append_button (self, name, button);
}

/* Private (glass-private.h): a toggle showing @content (a tab's icon and
 * title). */
GtkWidget *
glass_toggle_group_append_item (GlassToggleGroup *self,
                                const char       *name,
                                GtkWidget        *content)
{
  GtkWidget *button = gtk_button_new ();

  gtk_button_set_child (GTK_BUTTON (button), content);
  append_button (self, name, button);
  return button;
}

/* Private: removes every toggle. */
void
glass_toggle_group_remove_all (GlassToggleGroup *self)
{
  while (self->buttons->len > 0)
    {
      GtkWidget *button = g_ptr_array_steal_index (self->buttons, self->buttons->len - 1);

      glass_pill_box_remove (GLASS_PILL_BOX (self->box), button);
      g_ptr_array_remove_index (self->names, self->names->len - 1);
    }
  self->active = 0;
  self->have_from = FALSE;
  gtk_widget_queue_allocate (GTK_WIDGET (self));
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_N_TOGGLES]);
}

static void
append_button (GlassToggleGroup *self,
               const char       *name,
               GtkWidget        *button)
{
  /* Frameless: a theme can paint framed buttons from USER priority, which
   * would put a pill under every toggle at rest (docs/memo.md 地雷27). */
  gtk_button_set_has_frame (GTK_BUTTON (button), FALSE);
  gtk_widget_add_css_class (button, "toggle");
  mark (button, self->buttons->len == 0);

  g_signal_connect_object (button, "clicked", G_CALLBACK (button_clicked), self, 0);
  glass_pill_box_append (GLASS_PILL_BOX (self->box), button);
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
 * Gets how many toggles there are.
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
 * Gets the index of the active toggle.
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
 * Gets the name of the active toggle.
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
