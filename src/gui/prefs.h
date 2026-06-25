/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef USBEXPLORER_GUI_PREFS_H
#define USBEXPLORER_GUI_PREFS_H

#include <glib.h>

/* Persisted GUI preferences / session state (XDG config: usbexplorer/settings.ini). */
typedef struct {
    int      win_w, win_h;
    int      outer_pos, inner_pos;   /* GtkPaned positions */
    gboolean log_visible;
    gboolean notifications;
    char     theme[64];              /* css palette name        */
    char     color_scheme[16];       /* "system"/"light"/"dark" */
} UePrefs;

void ue_prefs_defaults(UePrefs *p);
void ue_prefs_load(UePrefs *p);            /* defaults + overrides from disk */
void ue_prefs_save(const UePrefs *p);

#endif /* USBEXPLORER_GUI_PREFS_H */
