/* glass-search-entry.h — a search field on glass (design.md §6.8).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#if !defined(GLASS_INSIDE) && !defined(GLASS_COMPILATION)
#error "Only <glass.h> can be included directly."
#endif

#include "glass-panel.h"

G_BEGIN_DECLS

#define GLASS_TYPE_SEARCH_ENTRY (glass_search_entry_get_type ())
G_DECLARE_FINAL_TYPE (GlassSearchEntry, glass_search_entry, GLASS, SEARCH_ENTRY, GlassPanel)

GtkWidget  *glass_search_entry_new                    (void);

const char *glass_search_entry_get_placeholder_text   (GlassSearchEntry *self);
void        glass_search_entry_set_placeholder_text   (GlassSearchEntry *self,
                                                       const char       *text);

guint       glass_search_entry_get_search_delay       (GlassSearchEntry *self);
void        glass_search_entry_set_search_delay       (GlassSearchEntry *self,
                                                       guint             delay);

GtkWidget  *glass_search_entry_get_key_capture_widget (GlassSearchEntry *self);
void        glass_search_entry_set_key_capture_widget (GlassSearchEntry *self,
                                                       GtkWidget        *widget);

G_END_DECLS
