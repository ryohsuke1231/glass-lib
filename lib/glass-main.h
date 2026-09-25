/* glass-main.h — library initialisation.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#if !defined(GLASS_INSIDE) && !defined(GLASS_COMPILATION)
#error "Only <glass.h> can be included directly."
#endif

#include <glib.h>

G_BEGIN_DECLS

void     glass_init           (void);
gboolean glass_is_initialized (void);

G_END_DECLS
