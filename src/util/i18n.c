/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "util/i18n.h"
#include "config.h"

#include <locale.h>

void ue_i18n_init(void)
{
    setlocale(LC_ALL, "");
    bindtextdomain(GETTEXT_PACKAGE, LOCALEDIR);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);
}
