/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "gui/css.h"

#include <adwaita.h>
#include <string.h>

static GtkCssProvider *g_provider;

typedef struct {
    const char *name;
    const char *css;   /* palette overrides; "" = none */
} Theme;

/* A handful of palettes.  Each tints the main surfaces; libadwaita still
 * supplies widget styling underneath.  "Default" applies nothing. */
static const Theme THEMES[] = {
    { "Default", "" },
    { "Dark",
      "window, textview, textview text, listview { background-color:#1e1e1e; color:#e0e0e0; }" },
    { "Nord",
      "window, listview { background-color:#2e3440; color:#d8dee9; }"
      "textview, textview text { background-color:#2e3440; color:#d8dee9; }"
      "headerbar { background-color:#3b4252; color:#eceff4; }" },
    { "Solarized Dark",
      "window, listview { background-color:#002b36; color:#93a1a1; }"
      "textview, textview text { background-color:#073642; color:#93a1a1; }"
      "headerbar { background-color:#073642; color:#eee8d5; }" },
    { "Catppuccin",
      "window, listview { background-color:#1e1e2e; color:#cdd6f4; }"
      "textview, textview text { background-color:#181825; color:#cdd6f4; }"
      "headerbar { background-color:#313244; color:#cdd6f4; }" },
    { "High Contrast",
      "window, listview, textview, textview text { background-color:#000000; color:#ffffff; }"
      "headerbar { background-color:#000000; color:#ffffff; }"
      "* { outline-color:#ffff00; }" },
};
#define N_THEMES (sizeof THEMES / sizeof THEMES[0])

void ue_css_init(void)
{
    if (g_provider)
        return;
    g_provider = gtk_css_provider_new();
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(), GTK_STYLE_PROVIDER(g_provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

void ue_css_apply_theme(const char *name)
{
    if (!g_provider)
        ue_css_init();
    for (size_t i = 0; i < N_THEMES; i++) {
        if (strcmp(THEMES[i].name, name) == 0) {
            gtk_css_provider_load_from_string(g_provider, THEMES[i].css);
            return;
        }
    }
    gtk_css_provider_load_from_string(g_provider, "");
}

const char *const *ue_css_theme_names(void)
{
    static const char *names[N_THEMES + 1];
    if (!names[0])
        for (size_t i = 0; i < N_THEMES; i++)
            names[i] = THEMES[i].name;
    return names;
}

void ue_css_set_color_scheme(const char *scheme)
{
    AdwStyleManager *sm = adw_style_manager_get_default();
    AdwColorScheme cs = ADW_COLOR_SCHEME_DEFAULT;
    if (scheme && strcmp(scheme, "light") == 0)
        cs = ADW_COLOR_SCHEME_FORCE_LIGHT;
    else if (scheme && strcmp(scheme, "dark") == 0)
        cs = ADW_COLOR_SCHEME_FORCE_DARK;
    adw_style_manager_set_color_scheme(sm, cs);
}
