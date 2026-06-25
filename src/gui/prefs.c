/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "gui/prefs.h"

static char *config_path(void)
{
    return g_build_filename(g_get_user_config_dir(), "usbexplorer", "settings.ini", NULL);
}

void ue_prefs_defaults(UePrefs *p)
{
    p->win_w = 1180;
    p->win_h = 720;
    p->outer_pos = 360;
    p->inner_pos = 540;
    p->log_visible = TRUE;
    p->notifications = TRUE;
    g_strlcpy(p->theme, "Default", sizeof p->theme);
    g_strlcpy(p->color_scheme, "system", sizeof p->color_scheme);
}

void ue_prefs_load(UePrefs *p)
{
    ue_prefs_defaults(p);

    char *path = config_path();
    GKeyFile *kf = g_key_file_new();
    if (g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
        GError *e = NULL;
#define GETINT(grp, key, field)                                      \
        do { int v = g_key_file_get_integer(kf, grp, key, &e);       \
             if (!e) p->field = v; else g_clear_error(&e); } while (0)
#define GETBOOL(grp, key, field)                                     \
        do { gboolean v = g_key_file_get_boolean(kf, grp, key, &e);  \
             if (!e) p->field = v; else g_clear_error(&e); } while (0)

        GETINT("window", "width", win_w);
        GETINT("window", "height", win_h);
        GETINT("window", "outer_pos", outer_pos);
        GETINT("window", "inner_pos", inner_pos);
        GETBOOL("ui", "log_visible", log_visible);
        GETBOOL("ui", "notifications", notifications);

        char *t = g_key_file_get_string(kf, "ui", "theme", NULL);
        if (t) { g_strlcpy(p->theme, t, sizeof p->theme); g_free(t); }
        char *c = g_key_file_get_string(kf, "ui", "color_scheme", NULL);
        if (c) { g_strlcpy(p->color_scheme, c, sizeof p->color_scheme); g_free(c); }
#undef GETINT
#undef GETBOOL
    }
    g_key_file_free(kf);
    g_free(path);
}

void ue_prefs_save(const UePrefs *p)
{
    GKeyFile *kf = g_key_file_new();
    g_key_file_set_integer(kf, "window", "width", p->win_w);
    g_key_file_set_integer(kf, "window", "height", p->win_h);
    g_key_file_set_integer(kf, "window", "outer_pos", p->outer_pos);
    g_key_file_set_integer(kf, "window", "inner_pos", p->inner_pos);
    g_key_file_set_boolean(kf, "ui", "log_visible", p->log_visible);
    g_key_file_set_boolean(kf, "ui", "notifications", p->notifications);
    g_key_file_set_string(kf, "ui", "theme", p->theme);
    g_key_file_set_string(kf, "ui", "color_scheme", p->color_scheme);

    char *path = config_path();
    char *dir = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0700);
    g_key_file_save_to_file(kf, path, NULL);
    g_free(dir);
    g_free(path);
    g_key_file_free(kf);
}
