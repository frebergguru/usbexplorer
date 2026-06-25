/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef USBEXPLORER_UTIL_I18N_H
#define USBEXPLORER_UTIL_I18N_H

#include <libintl.h>

/*
 * gettext convenience macros.  _() marks a string for translation and looks it
 * up at run time; N_() marks a string for extraction but defers translation
 * (e.g. for static initialisers).  Call ue_i18n_init() once at start-up.
 */
#define _(String)  gettext(String)
#define N_(String) (String)

void ue_i18n_init(void);

#endif /* USBEXPLORER_UTIL_I18N_H */
