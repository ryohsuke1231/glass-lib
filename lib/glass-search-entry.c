/* glass-search-entry.c — a search field on glass (design.md §6.8).
 *
 * A capsule of glass with a magnifier, the text and a button that clears
 * it. The text is a bare GtkText, not a GtkSearchEntry: a theme paints
 * `entry` nodes (background, corner radius) from USER priority, which no
 * library CSS can undo, and that box would sit on the glass (the reason
 * buttons on glass are frameless, docs/memo.md 地雷27). GtkText has no
 * background of its own.
 *
 * SPDX-License-Identifier: MIT
 */
#include "glass-private.h"

/**
 * GlassSearchEntry:
 *
 * The glass counterpart of `GtkSearchEntry`: a capsule of glass to type a
 * search into, with a magnifier and a button that clears the text.
 *
 * It implements `GtkEditable`, and emits [signal@SearchEntry::search-changed]
 * a moment ([property@SearchEntry:search-delay]) after the text changes,
 * [signal@SearchEntry::activate] on Enter and
 * [signal@SearchEntry::stop-search] on Escape.
 *
 * Give it a [property@Panel:morph-id] shared with a search button to have
 * the button's glass become the field's (design.md §6.8).
 *
 * ## CSS nodes
 *
 * `GlassSearchEntry` is a [class@Panel] (CSS name `glasspanel`) with the
 * style class `.search-entry`, holding a `box` with an `image`, a `text`
 * and a `button`.
 *
 * ## Accessibility
 *
 * `GlassSearchEntry` uses the %GTK_ACCESSIBLE_ROLE_SEARCH_BOX role.
 */

struct _GlassSearchEntry {
  GlassPanel          parent_instance;

  GtkWidget          *text;
  GtkWidget          *clear;
  guint               delay;
  guint               delay_source;
  GtkWidget          *capture_widget;       /* weak */
  GtkEventController *capture_controller;   /* on capture_widget */
};

enum {
  PROP_0,
  PROP_PLACEHOLDER_TEXT,
  PROP_SEARCH_DELAY,
  PROP_KEY_CAPTURE_WIDGET,
  LAST_PROP
};

static GParamSpec *props[LAST_PROP];

enum {
  SEARCH_CHANGED,
  ACTIVATE,
  STOP_SEARCH,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

static void glass_search_entry_editable_init (GtkEditableInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (GlassSearchEntry, glass_search_entry, GLASS_TYPE_PANEL,
                               G_IMPLEMENT_INTERFACE (GTK_TYPE_EDITABLE, glass_search_entry_editable_init))

static GtkEditable *
get_delegate (GtkEditable *editable)
{
  return GTK_EDITABLE (GLASS_SEARCH_ENTRY (editable)->text);
}

static void
glass_search_entry_editable_init (GtkEditableInterface *iface)
{
  iface->get_delegate = get_delegate;
}

static gboolean
search_changed_timeout (gpointer data)
{
  GlassSearchEntry *self = data;

  self->delay_source = 0;
  g_signal_emit (self, signals[SEARCH_CHANGED], 0);
  return G_SOURCE_REMOVE;
}

static void
text_changed (GtkEditable      *text,
              GlassSearchEntry *self)
{
  const char *str = gtk_editable_get_text (text);

  gtk_widget_set_child_visible (self->clear, str && *str);
  g_clear_handle_id (&self->delay_source, g_source_remove);
  /* Emptied: at once, like GtkSearchEntry (the results go back at once). */
  if (self->delay == 0 || str == NULL || *str == '\0')
    g_signal_emit (self, signals[SEARCH_CHANGED], 0);
  else
    self->delay_source = g_timeout_add (self->delay, search_changed_timeout, self);
}

static void
text_activate (GtkText          *text,
               GlassSearchEntry *self)
{
  g_signal_emit (self, signals[ACTIVATE], 0);
}

static void
clear_clicked (GtkButton        *button,
               GlassSearchEntry *self)
{
  gtk_editable_set_text (GTK_EDITABLE (self->text), "");
  gtk_widget_grab_focus (self->text);
}

static gboolean
key_pressed (GtkEventControllerKey *controller,
             guint                  keyval,
             guint                  keycode,
             GdkModifierType        state,
             GlassSearchEntry      *self)
{
  if (keyval == GDK_KEY_Escape)
    {
      g_signal_emit (self, signals[STOP_SEARCH], 0);
      return GDK_EVENT_STOP;
    }
  return GDK_EVENT_PROPAGATE;
}

/* A click anywhere on the glass puts the caret in the text. */
static void
pressed (GtkGestureClick  *gesture,
         int               n_press,
         double            x,
         double            y,
         GlassSearchEntry *self)
{
  if (!gtk_widget_has_focus (self->text))
    gtk_widget_grab_focus (self->text);
}

/* Typing in the capture widget types here (GtkSearchEntry's key capture). */
static gboolean
capture_key_pressed (GtkEventControllerKey *controller,
                     guint                  keyval,
                     guint                  keycode,
                     GdkModifierType        state,
                     GlassSearchEntry      *self)
{
  g_autofree char *before = NULL;
  gunichar c;

  if (gtk_widget_has_focus (self->text) || !gtk_widget_get_mapped (GTK_WIDGET (self)) ||
      (state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SUPER_MASK)))
    return GDK_EVENT_PROPAGATE;
  c = gdk_keyval_to_unicode (keyval);
  if (c == 0 || !g_unichar_isgraph (c))
    return GDK_EVENT_PROPAGATE;

  before = g_strdup (gtk_editable_get_text (GTK_EDITABLE (self->text)));
  gtk_event_controller_key_forward (GTK_EVENT_CONTROLLER_KEY (controller), self->text);
  if (g_strcmp0 (before, gtk_editable_get_text (GTK_EDITABLE (self->text))) == 0)
    return GDK_EVENT_PROPAGATE;
  gtk_widget_grab_focus (self->text);
  gtk_editable_set_position (GTK_EDITABLE (self->text), -1);
  return GDK_EVENT_STOP;
}

static gboolean
glass_search_entry_grab_focus (GtkWidget *widget)
{
  return gtk_widget_grab_focus (GLASS_SEARCH_ENTRY (widget)->text);
}

static void
glass_search_entry_dispose (GObject *object)
{
  GlassSearchEntry *self = GLASS_SEARCH_ENTRY (object);

  glass_search_entry_set_key_capture_widget (self, NULL);
  g_clear_handle_id (&self->delay_source, g_source_remove);
  if (self->text)
    gtk_editable_finish_delegate (GTK_EDITABLE (self));
  self->text = NULL;

  G_OBJECT_CLASS (glass_search_entry_parent_class)->dispose (object);
}

static void
glass_search_entry_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GlassSearchEntry *self = GLASS_SEARCH_ENTRY (object);

  if (gtk_editable_delegate_get_property (object, prop_id, value, pspec))
    return;

  switch (prop_id)
    {
    case PROP_PLACEHOLDER_TEXT:
      g_value_set_string (value, glass_search_entry_get_placeholder_text (self));
      break;
    case PROP_SEARCH_DELAY:
      g_value_set_uint (value, self->delay);
      break;
    case PROP_KEY_CAPTURE_WIDGET:
      g_value_set_object (value, self->capture_widget);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
glass_search_entry_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GlassSearchEntry *self = GLASS_SEARCH_ENTRY (object);

  if (gtk_editable_delegate_set_property (object, prop_id, value, pspec))
    return;

  switch (prop_id)
    {
    case PROP_PLACEHOLDER_TEXT:
      glass_search_entry_set_placeholder_text (self, g_value_get_string (value));
      break;
    case PROP_SEARCH_DELAY:
      glass_search_entry_set_search_delay (self, g_value_get_uint (value));
      break;
    case PROP_KEY_CAPTURE_WIDGET:
      glass_search_entry_set_key_capture_widget (self, g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
glass_search_entry_class_init (GlassSearchEntryClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = glass_search_entry_dispose;
  object_class->get_property = glass_search_entry_get_property;
  object_class->set_property = glass_search_entry_set_property;
  widget_class->grab_focus = glass_search_entry_grab_focus;

  /**
   * GlassSearchEntry:placeholder-text:
   *
   * The text shown while the field is empty.
   */
  props[PROP_PLACEHOLDER_TEXT] =
    g_param_spec_string ("placeholder-text", NULL, NULL, NULL,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * GlassSearchEntry:search-delay:
   *
   * How long after the text changes, in ms, to emit
   * [signal@SearchEntry::search-changed].
   */
  props[PROP_SEARCH_DELAY] =
    g_param_spec_uint ("search-delay", NULL, NULL, 0, G_MAXUINT, 150,
                       G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * GlassSearchEntry:key-capture-widget:
   *
   * A widget whose typing goes into the field while it has no focus (a
   * search that starts when you type, anywhere in the window).
   */
  props[PROP_KEY_CAPTURE_WIDGET] =
    g_param_spec_object ("key-capture-widget", NULL, NULL, GTK_TYPE_WIDGET,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, LAST_PROP, props);
  gtk_editable_install_properties (object_class, LAST_PROP);

  /**
   * GlassSearchEntry::search-changed:
   *
   * The text changed, [property@SearchEntry:search-delay] ago (at once when
   * it was emptied).
   */
  signals[SEARCH_CHANGED] =
    g_signal_new ("search-changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL, G_TYPE_NONE, 0);

  /**
   * GlassSearchEntry::activate:
   *
   * Enter was pressed in the field.
   */
  signals[ACTIVATE] =
    g_signal_new ("activate", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                  0, NULL, NULL, NULL, G_TYPE_NONE, 0);

  /**
   * GlassSearchEntry::stop-search:
   *
   * Escape was pressed: the app usually ends the search.
   */
  signals[STOP_SEARCH] =
    g_signal_new ("stop-search", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                  0, NULL, NULL, NULL, G_TYPE_NONE, 0);

  gtk_widget_class_set_accessible_role (widget_class, GTK_ACCESSIBLE_ROLE_SEARCH_BOX);
}

static void
glass_search_entry_init (GlassSearchEntry *self)
{
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  GtkWidget *icon = gtk_image_new_from_icon_name ("edit-find-symbolic");
  GtkEventController *keys;
  GtkGesture *click;

  self->delay = 150;

  self->text = gtk_text_new ();
  gtk_widget_set_hexpand (self->text, TRUE);
  gtk_widget_set_valign (self->text, GTK_ALIGN_CENTER);
  gtk_editable_set_width_chars (GTK_EDITABLE (self->text), 12);

  self->clear = gtk_button_new_from_icon_name ("edit-clear-symbolic");
  gtk_button_set_has_frame (GTK_BUTTON (self->clear), FALSE);
  gtk_widget_add_css_class (self->clear, "circular");
  gtk_widget_set_valign (self->clear, GTK_ALIGN_CENTER);
  gtk_widget_set_tooltip_text (self->clear, "Clear");
  gtk_widget_set_child_visible (self->clear, FALSE);

  gtk_widget_add_css_class (icon, "dim-label");
  gtk_box_append (GTK_BOX (box), icon);
  gtk_box_append (GTK_BOX (box), self->text);
  gtk_box_append (GTK_BOX (box), self->clear);
  glass_panel_set_child (GLASS_PANEL (self), box);
  gtk_widget_add_css_class (GTK_WIDGET (self), "search-entry");

  gtk_editable_init_delegate (GTK_EDITABLE (self));
  g_signal_connect (self->text, "changed", G_CALLBACK (text_changed), self);
  g_signal_connect (self->text, "activate", G_CALLBACK (text_activate), self);
  g_signal_connect (self->clear, "clicked", G_CALLBACK (clear_clicked), self);

  keys = gtk_event_controller_key_new ();
  g_signal_connect (keys, "key-pressed", G_CALLBACK (key_pressed), self);
  gtk_widget_add_controller (GTK_WIDGET (self), keys);

  click = gtk_gesture_click_new ();
  g_signal_connect (click, "pressed", G_CALLBACK (pressed), self);
  gtk_widget_add_controller (GTK_WIDGET (self), GTK_EVENT_CONTROLLER (click));
  gtk_widget_set_cursor_from_name (GTK_WIDGET (self), "text");
}

/**
 * glass_search_entry_new:
 *
 * Returns: a new search field
 */
GtkWidget *
glass_search_entry_new (void)
{
  return g_object_new (GLASS_TYPE_SEARCH_ENTRY, NULL);
}

/**
 * glass_search_entry_get_placeholder_text:
 * @self: a search field
 *
 * Returns: (nullable): the placeholder text
 */
const char *
glass_search_entry_get_placeholder_text (GlassSearchEntry *self)
{
  g_return_val_if_fail (GLASS_IS_SEARCH_ENTRY (self), NULL);

  return gtk_text_get_placeholder_text (GTK_TEXT (self->text));
}

/**
 * glass_search_entry_set_placeholder_text:
 * @self: a search field
 * @text: (nullable): the text shown while the field is empty
 *
 * Sets the placeholder text.
 */
void
glass_search_entry_set_placeholder_text (GlassSearchEntry *self,
                                         const char       *text)
{
  g_return_if_fail (GLASS_IS_SEARCH_ENTRY (self));

  if (g_strcmp0 (text, gtk_text_get_placeholder_text (GTK_TEXT (self->text))) == 0)
    return;
  gtk_text_set_placeholder_text (GTK_TEXT (self->text), text);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_PLACEHOLDER_TEXT]);
}

/**
 * glass_search_entry_get_search_delay:
 * @self: a search field
 *
 * Returns: the delay of [signal@SearchEntry::search-changed], ms
 */
guint
glass_search_entry_get_search_delay (GlassSearchEntry *self)
{
  g_return_val_if_fail (GLASS_IS_SEARCH_ENTRY (self), 0);

  return self->delay;
}

/**
 * glass_search_entry_set_search_delay:
 * @self: a search field
 * @delay: the delay in ms
 *
 * Sets how long after the text changes to emit
 * [signal@SearchEntry::search-changed].
 */
void
glass_search_entry_set_search_delay (GlassSearchEntry *self,
                                     guint             delay)
{
  g_return_if_fail (GLASS_IS_SEARCH_ENTRY (self));

  if (self->delay == delay)
    return;
  self->delay = delay;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SEARCH_DELAY]);
}

/**
 * glass_search_entry_get_key_capture_widget:
 * @self: a search field
 *
 * Returns: (transfer none) (nullable): the key capture widget
 */
GtkWidget *
glass_search_entry_get_key_capture_widget (GlassSearchEntry *self)
{
  g_return_val_if_fail (GLASS_IS_SEARCH_ENTRY (self), NULL);

  return self->capture_widget;
}

/**
 * glass_search_entry_set_key_capture_widget:
 * @self: a search field
 * @widget: (nullable): a widget whose typing goes into the field
 *
 * Sets [property@SearchEntry:key-capture-widget].
 */
void
glass_search_entry_set_key_capture_widget (GlassSearchEntry *self,
                                           GtkWidget        *widget)
{
  g_return_if_fail (GLASS_IS_SEARCH_ENTRY (self));
  g_return_if_fail (widget == NULL || GTK_IS_WIDGET (widget));

  if (self->capture_widget == widget)
    return;
  if (self->capture_widget)
    {
      gtk_widget_remove_controller (self->capture_widget, self->capture_controller);
      self->capture_controller = NULL;
      g_clear_weak_pointer (&self->capture_widget);
    }
  if (widget)
    {
      g_set_weak_pointer (&self->capture_widget, widget);
      self->capture_controller = gtk_event_controller_key_new ();
      gtk_event_controller_set_propagation_phase (self->capture_controller, GTK_PHASE_BUBBLE);
      g_signal_connect (self->capture_controller, "key-pressed", G_CALLBACK (capture_key_pressed), self);
      gtk_widget_add_controller (widget, self->capture_controller);
    }
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_KEY_CAPTURE_WIDGET]);
}
