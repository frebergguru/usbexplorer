/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef USBEXPLORER_GUI_CSS_H
#define USBEXPLORER_GUI_CSS_H

#include <stddef.h>

/* Install the application's CSS provider on the default display (once). */
void ue_css_init(void);

/* Apply a named built-in palette theme ("Default" clears overrides). */
void ue_css_apply_theme(const char *name);

/* NULL-terminated list of built-in theme names. */
const char *const *ue_css_theme_names(void);

/* Set the libadwaita colour scheme: "system", "light", or "dark". */
void ue_css_set_color_scheme(const char *scheme);

#endif /* USBEXPLORER_GUI_CSS_H */
