/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "gui/mainwindow.h"

#include "gui/bluetooth_ui.h"
#include "gui/css.h"
#include "gui/detailpanel.h"
#include "gui/diff_ui.h"
#include "gui/logpanel.h"
#include "gui/prefs.h"
#include "gui/treeview.h"
#include "usb/enumerate.h"
#include "usb/report.h"
#include "util/i18n.h"
#include "util/notify.h"
#include "util/pciids.h"
#include "util/udev_rule.h"
#include "util/usbids.h"

#include <string.h>

typedef struct {
    GtkWidget      *window;
    UsbNode        *root;        /* owned */
    UsbNode        *selected;    /* borrowed (into root) */
    UeTree         *tree;
    UeDetail       *detail;
    UeLog          *log;
    GtkWidget      *log_pane;
    GtkWidget      *outer;       /* GtkPaned tree | inner */
    GtkWidget      *inner;       /* GtkPaned detail | log */
    GtkWidget      *status;
    GMenu          *devmenu;     /* context-menu model (owned) */
    AdwWindowTitle *title;
    UePrefs         prefs;
} Win;

/* ------------------------------------------------------------- helpers */

static void count_devices(const UsbNode *n, int *devices, int *controllers)
{
    if (n->kind == NODE_CONTROLLER)
        (*controllers)++;
    else if (n->kind == NODE_DEVICE || n->kind == NODE_HUB || n->kind == NODE_ROOT_HUB)
        (*devices)++;
    for (size_t i = 0; i < n->n_children; i++)
        count_devices(n->children[i], devices, controllers);
}

static void set_status(Win *w, const char *msg)
{
    int dev = 0, ctrl = 0;
    count_devices(w->root, &dev, &ctrl);
    char *s = g_strdup_printf(" %d devices across %d host controllers%s%s",
                              dev, ctrl, msg ? "  —  " : "", msg ? msg : "");
    gtk_label_set_text(GTK_LABEL(w->status), s);
    g_free(s);

    char *sub = g_strdup_printf("%d devices", dev);
    adw_window_title_set_subtitle(w->title, sub);
    g_free(sub);
}

static void win_refresh(Win *w)
{
    UsbNode *fresh = usb_enumerate();
    if (!fresh)
        return;
    usb_tree_free(w->root);
    w->root = fresh;
    w->selected = NULL;
    ue_tree_set_root(w->tree, fresh);
    set_status(w, NULL);
}

/* --- selection / search --- */

static void on_tree_select(UsbNode *node, gpointer user)
{
    Win *w = user;
    w->selected = node;
    ue_detail_show(w->detail, node);
}

static UsbNode *find_match(UsbNode *n, const char *q)
{
    if (n->kind != NODE_ROOT && n->kind != NODE_CONTROLLER) {
        char vidpid[16];
        g_snprintf(vidpid, sizeof vidpid, "%04x:%04x", n->vid, n->pid);
        if ((n->product[0] && strcasestr(n->product, q)) ||
            (n->manufacturer[0] && strcasestr(n->manufacturer, q)) ||
            (n->serial[0] && strcasestr(n->serial, q)) ||
            strcasestr(n->devname, q) || strcasestr(vidpid, q))
            return n;
    }
    for (size_t i = 0; i < n->n_children; i++) {
        UsbNode *r = find_match(n->children[i], q);
        if (r)
            return r;
    }
    return NULL;
}

static void on_search_changed(GtkSearchEntry *entry, gpointer user)
{
    Win *w = user;
    const char *q = gtk_editable_get_text(GTK_EDITABLE(entry));
    if (!q || !*q)
        return;
    UsbNode *m = find_match(w->root, q);
    if (m)
        ue_tree_select_node(w->tree, m);
}

/* --- hotplug --- */

static void on_hotplug(const UsbEvent *e, gpointer user)
{
    Win *w = user;
    win_refresh(w);

    if (w->prefs.notifications) {
        const char *what = e->type == USB_EV_ADD ? "USB device connected"
                                                 : "USB device disconnected";
        char *body = g_strdup_printf("%s  (%04x:%04x)", e->sysname, e->vid, e->pid);
        ue_notify_send(what, body, "drive-removable-media");
        g_free(body);
    }
}

/* ------------------------------------------------------ device actions */

static void copy_text(Win *w, const char *text)
{
    GdkClipboard *cb = gtk_widget_get_clipboard(w->window);
    gdk_clipboard_set(cb, G_TYPE_STRING, text);
}

static void act_copy_info(GSimpleAction *a, GVariant *p, gpointer user)
{
    (void)a; (void)p;
    Win *w = user;
    if (!w->selected)
        return;
    const UsbNode *n = w->selected;
    const char *vn = n->manufacturer[0] ? n->manufacturer : usbids_vendor(n->vid);
    const char *pn = n->product[0] ? n->product : usbids_product(n->vid, n->pid);
    char *s = g_strdup_printf(
        "%s — %s\nVID:PID: %04x:%04x\nSerial: %s\nSpeed: %s\nDriver path: %s\n",
        vn ? vn : "?", pn ? pn : "?", n->vid, n->pid,
        n->serial[0] ? n->serial : "(none)", usb_speed_str(n->speed), n->sysfs_path);
    copy_text(w, s);
    set_status(w, "device info copied to clipboard");
    g_free(s);
}

static void act_copy_udev(GSimpleAction *a, GVariant *p, gpointer user)
{
    (void)a; (void)p;
    Win *w = user;
    if (!w->selected)
        return;
    char *text = NULL;
    size_t len = 0;
    FILE *ms = open_memstream(&text, &len);
    if (ms) {
        udev_rule_write(ms, w->selected->vid, w->selected->pid, w->selected);
        fclose(ms);
        copy_text(w, text);
        set_status(w, "udev rule copied to clipboard");
        free(text);
    }
}

static void act_search_online(GSimpleAction *a, GVariant *p, gpointer user)
{
    (void)a; (void)p;
    Win *w = user;
    if (!w->selected)
        return;
    char *url = g_strdup_printf("https://the.usbids.com/%04x/%04x",
                                w->selected->vid, w->selected->pid);
    GtkUriLauncher *l = gtk_uri_launcher_new(url);
    gtk_uri_launcher_launch(l, GTK_WINDOW(w->window), NULL, NULL, NULL);
    g_object_unref(l);
    g_free(url);
}

static void act_open_sysfs(GSimpleAction *a, GVariant *p, gpointer user)
{
    (void)a; (void)p;
    Win *w = user;
    if (!w->selected)
        return;
    GFile *f = g_file_new_for_path(w->selected->sysfs_path);
    GtkFileLauncher *fl = gtk_file_launcher_new(f);
    gtk_file_launcher_launch(fl, GTK_WINDOW(w->window), NULL, NULL, NULL);
    g_object_unref(fl);
    g_object_unref(f);
}

/* Run a privileged action by re-invoking ourselves through pkexec.  The
 * subcommand argument is "<prefix>BUS.DEV" (prefix e.g. "on:" for toggles). */
static void run_privileged(Win *w, const char *flag, const char *prefix)
{
    if (!w->selected || w->selected->busnum == 0) {
        set_status(w, "select a device first");
        return;
    }
    char *arg = g_strdup_printf("%s%d.%d", prefix ? prefix : "",
                                w->selected->busnum, w->selected->devnum);
    char *exe = g_file_read_link("/proc/self/exe", NULL);

    GError *err = NULL;
    GSubprocess *proc = g_subprocess_new(G_SUBPROCESS_FLAGS_NONE, &err,
                                         "pkexec", exe ? exe : "usbexplorer",
                                         flag, arg, NULL);
    if (!proc) {
        char *m = g_strdup_printf("could not run pkexec: %s", err->message);
        set_status(w, m);
        g_free(m);
        g_clear_error(&err);
    } else {
        g_object_unref(proc); /* fire-and-forget; the monitor refreshes the tree */
        char *m = g_strdup_printf("requested %s via pkexec", flag + 2);
        set_status(w, m);
        g_free(m);
    }
    g_free(arg);
    g_free(exe);
}

static void act_reset(GSimpleAction *a, GVariant *p, gpointer u)
{ (void)a; (void)p; run_privileged(u, "--reset-device", NULL); }
static void act_restart(GSimpleAction *a, GVariant *p, gpointer u)
{ (void)a; (void)p; run_privileged(u, "--restart-device", NULL); }
static void act_port_cycle(GSimpleAction *a, GVariant *p, gpointer u)
{ (void)a; (void)p; run_privileged(u, "--port-cycle", NULL); }
/* Reusable scrollable, monospace text window (dmesg, latency, history, …). */
static void show_text_window(GtkWindow *parent, const char *title, const char *text)
{
    GtkWidget *win = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(win), title);
    if (parent)
        gtk_window_set_transient_for(GTK_WINDOW(win), parent);
    gtk_window_set_default_size(GTK_WINDOW(win), 680, 480);

    GtkWidget *view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(view), 8);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(view), 6);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)),
                             text ? text : "", -1);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), view);
    gtk_window_set_child(GTK_WINDOW(win), scroll);
    gtk_window_present(GTK_WINDOW(win));
}

static void act_dmesg(GSimpleAction *a, GVariant *p, gpointer user)
{
    (void)a; (void)p;
    Win *w = user;
    if (!w->selected || w->selected->busnum == 0) { set_status(w, "select a device first"); return; }
    char *txt = report_dmesg(w->selected);
    show_text_window(GTK_WINDOW(w->window), "dmesg", txt);
    free(txt);
}

static void act_latency(GSimpleAction *a, GVariant *p, gpointer user)
{
    (void)a; (void)p;
    Win *w = user;
    if (!w->selected || w->selected->busnum == 0) { set_status(w, "select a device first"); return; }
    char *txt = report_latency(w->selected);
    show_text_window(GTK_WINDOW(w->window), "Interrupt latency", txt);
    free(txt);
}

static void act_history(GSimpleAction *a, GVariant *p, gpointer user)
{
    (void)a; (void)p;
    Win *w = user;
    char *txt = report_history(0);
    show_text_window(GTK_WINDOW(w->window), "Device history", txt);
    free(txt);
}

/* Speed test runs in a worker thread (it can read hundreds of MB). */
typedef struct { GtkWindow *parent; UsbNode snap; char *result; } SpeedJob;

static gboolean speed_done(gpointer data)
{
    SpeedJob *j = data;
    show_text_window(j->parent, "Read-speed test", j->result);
    if (j->parent)
        g_object_remove_weak_pointer(G_OBJECT(j->parent), (gpointer *)&j->parent);
    free(j->result);
    g_free(j);
    return G_SOURCE_REMOVE;
}

static gpointer speed_thread(gpointer data)
{
    SpeedJob *j = data;
    j->result = report_speedtest(&j->snap, 256L << 20);
    g_idle_add(speed_done, j);
    return NULL;
}

static void act_speedtest(GSimpleAction *a, GVariant *p, gpointer user)
{
    (void)a; (void)p;
    Win *w = user;
    if (!w->selected || w->selected->busnum == 0) { set_status(w, "select a device first"); return; }
    SpeedJob *j = g_new0(SpeedJob, 1);
    /* Snapshot only the fields the test reads, so a tree refresh can't dangle. */
    g_strlcpy(j->snap.sysfs_path, w->selected->sysfs_path, sizeof j->snap.sysfs_path);
    g_strlcpy(j->snap.product, w->selected->product, sizeof j->snap.product);
    g_strlcpy(j->snap.manufacturer, w->selected->manufacturer, sizeof j->snap.manufacturer);
    j->snap.vid = w->selected->vid; j->snap.pid = w->selected->pid;
    j->snap.speed = w->selected->speed;
    j->parent = GTK_WINDOW(w->window);
    g_object_add_weak_pointer(G_OBJECT(j->parent), (gpointer *)&j->parent);
    set_status(w, "running read-speed test…");
    g_thread_unref(g_thread_new("speedtest", speed_thread, j));
}

/* Render the device-tree widget to a PNG. */
static void save_tree_png(Win *w, const char *path)
{
    GtkWidget *tw = ue_tree_widget(w->tree);
    int width = gtk_widget_get_width(tw), height = gtk_widget_get_height(tw);
    if (width < 1 || height < 1) {
        set_status(w, "tree not ready for a screenshot yet");
        return;
    }
    GdkPaintable *paint = gtk_widget_paintable_new(tw);
    GtkSnapshot *snap = gtk_snapshot_new();
    gdk_paintable_snapshot(paint, GDK_SNAPSHOT(snap), width, height);
    GskRenderNode *node = gtk_snapshot_free_to_node(snap);
    if (node) {
        GskRenderer *r = gtk_native_get_renderer(gtk_widget_get_native(w->window));
        GdkTexture *tex = gsk_renderer_render_texture(r, node, NULL);
        if (tex) {
            char msg[400];
            if (gdk_texture_save_to_png(tex, path))
                g_snprintf(msg, sizeof msg, "saved screenshot to %s", path);
            else
                g_snprintf(msg, sizeof msg, "could not write %s", path);
            set_status(w, msg);
            g_object_unref(tex);
        }
        gsk_render_node_unref(node);
    }
    g_object_unref(paint);
}

static void on_shot_saved(GObject *src, GAsyncResult *res, gpointer user)
{
    Win *w = user;
    GError *err = NULL;
    GFile *f = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(src), res, &err);
    if (f) {
        char *path = g_file_get_path(f);
        if (path)
            save_tree_png(w, path);
        g_free(path);
        g_object_unref(f);
    } else {
        g_clear_error(&err);
    }
}

static void act_screenshot(GSimpleAction *a, GVariant *p, gpointer user)
{
    (void)a; (void)p;
    Win *w = user;
    const char *env = g_getenv("USBEXPLORER_SHOT"); /* headless self-test hook */
    if (env) {
        save_tree_png(w, env);
        return;
    }
    GtkFileDialog *fd = gtk_file_dialog_new();
    gtk_file_dialog_set_initial_name(fd, "usb-tree.png");
    gtk_file_dialog_save(fd, GTK_WINDOW(w->window), NULL, on_shot_saved, w);
    g_object_unref(fd);
}

static void act_diff(GSimpleAction *a, GVariant *p, gpointer user)
{
    (void)a; (void)p;
    Win *w = user;
    ue_diff_dialog_show(GTK_WINDOW(w->window), w->root);
}

static void act_bluetooth(GSimpleAction *a, GVariant *p, gpointer user)
{
    (void)a; (void)p;
    Win *w = user;
    ue_bluetooth_dialog_show(GTK_WINDOW(w->window));
}

static void act_autosuspend_on(GSimpleAction *a, GVariant *p, gpointer u)
{ (void)a; (void)p; run_privileged(u, "--autosuspend", "on:"); }
static void act_autosuspend_off(GSimpleAction *a, GVariant *p, gpointer u)
{ (void)a; (void)p; run_privileged(u, "--autosuspend", "off:"); }
static void act_wakeup_on(GSimpleAction *a, GVariant *p, gpointer u)
{ (void)a; (void)p; run_privileged(u, "--wakeup", "on:"); }
static void act_wakeup_off(GSimpleAction *a, GVariant *p, gpointer u)
{ (void)a; (void)p; run_privileged(u, "--wakeup", "off:"); }

/* --- view / appearance state changes --- */

static void change_show_log(GSimpleAction *a, GVariant *v, gpointer user)
{
    Win *w = user;
    gboolean on = g_variant_get_boolean(v);
    g_simple_action_set_state(a, v);
    gtk_widget_set_visible(w->log_pane, on);
    w->prefs.log_visible = on;
}

static void change_notify(GSimpleAction *a, GVariant *v, gpointer user)
{
    Win *w = user;
    w->prefs.notifications = g_variant_get_boolean(v);
    g_simple_action_set_state(a, v);
}

static void change_theme(GSimpleAction *a, GVariant *v, gpointer user)
{
    Win *w = user;
    const char *name = g_variant_get_string(v, NULL);
    g_simple_action_set_state(a, v);
    ue_css_apply_theme(name);
    g_strlcpy(w->prefs.theme, name, sizeof w->prefs.theme);
}

static void change_scheme(GSimpleAction *a, GVariant *v, gpointer user)
{
    Win *w = user;
    const char *name = g_variant_get_string(v, NULL);
    g_simple_action_set_state(a, v);
    ue_css_set_color_scheme(name);
    g_strlcpy(w->prefs.color_scheme, name, sizeof w->prefs.color_scheme);
}

/* --- right-click context menu --- */

static void on_pop_closed(GtkPopover *pop, gpointer user)
{
    (void)user;
    /* Unparent on idle so it isn't destroyed mid-signal-emission. */
    gtk_widget_unparent(GTK_WIDGET(pop));
}

static void on_right_click(GtkGestureClick *g, int np, double x, double y, gpointer user)
{
    (void)np;
    Win *w = user;
    GtkWidget *listview = ue_tree_listview(w->tree);
    GtkWidget *pop = gtk_popover_menu_new_from_model(G_MENU_MODEL(w->devmenu));
    gtk_popover_set_has_arrow(GTK_POPOVER(pop), FALSE);
    gtk_widget_set_parent(pop, listview);
    gtk_popover_set_pointing_to(GTK_POPOVER(pop), &(GdkRectangle){(int)x, (int)y, 1, 1});
    g_signal_connect(pop, "closed", G_CALLBACK(on_pop_closed), NULL);
    gtk_popover_popup(GTK_POPOVER(pop));
    gtk_gesture_set_state(GTK_GESTURE(g), GTK_EVENT_SEQUENCE_CLAIMED);
}

/* ------------------------------------------------------------- menus */

static GMenu *build_device_menu(void)
{
    GMenu *menu = g_menu_new();
    GMenu *s1 = g_menu_new();
    g_menu_append(s1, _("Copy device info"), "win.copy-info");
    g_menu_append(s1, _("Copy udev rule"), "win.copy-udev");
    g_menu_append(s1, _("Search online"), "win.search-online");
    g_menu_append(s1, _("Open sysfs folder"), "win.open-sysfs");
    g_menu_append_section(menu, NULL, G_MENU_MODEL(s1));
    g_object_unref(s1);

    GMenu *sq = g_menu_new();
    g_menu_append(sq, _("Test read speed"), "win.speedtest");
    g_menu_append(sq, _("Filtered dmesg"), "win.dmesg");
    g_menu_append(sq, _("Interrupt latency"), "win.latency");
    g_menu_append_section(menu, _("Diagnostics"), G_MENU_MODEL(sq));
    g_object_unref(sq);

    GMenu *s2 = g_menu_new();
    g_menu_append(s2, _("Enable autosuspend"), "win.autosuspend-on");
    g_menu_append(s2, _("Disable autosuspend"), "win.autosuspend-off");
    g_menu_append(s2, _("Enable remote wakeup"), "win.wakeup-on");
    g_menu_append(s2, _("Disable remote wakeup"), "win.wakeup-off");
    g_menu_append_section(menu, _("Power (pkexec)"), G_MENU_MODEL(s2));
    g_object_unref(s2);

    GMenu *s3 = g_menu_new();
    g_menu_append(s3, _("Reset device"), "win.reset");
    g_menu_append(s3, _("Restart (rebind driver)"), "win.restart");
    g_menu_append(s3, _("Power-cycle port"), "win.port-cycle");
    g_menu_append_section(menu, _("Privileged (pkexec)"), G_MENU_MODEL(s3));
    g_object_unref(s3);
    return menu;
}

static GMenu *build_primary_menu(void)
{
    GMenu *menu = g_menu_new();

    GMenu *theme = g_menu_new();
    for (const char *const *t = ue_css_theme_names(); *t; t++) {
        GMenuItem *it = g_menu_item_new(*t, NULL);
        g_menu_item_set_action_and_target_value(it, "win.theme",
                                                g_variant_new_string(*t));
        g_menu_append_item(theme, it);
        g_object_unref(it);
    }
    g_menu_append_submenu(menu, _("Theme"), G_MENU_MODEL(theme));
    g_object_unref(theme);

    GMenu *scheme = g_menu_new();
    const char *schemes[][2] = {{"System", "system"}, {"Light", "light"}, {"Dark", "dark"}};
    for (int i = 0; i < 3; i++) {
        GMenuItem *it = g_menu_item_new(schemes[i][0], NULL);
        g_menu_item_set_action_and_target_value(it, "win.color-scheme",
                                                g_variant_new_string(schemes[i][1]));
        g_menu_append_item(scheme, it);
        g_object_unref(it);
    }
    g_menu_append_submenu(menu, _("Appearance"), G_MENU_MODEL(scheme));
    g_object_unref(scheme);

    GMenu *tools = g_menu_new();
    g_menu_append(tools, _("Compare devices…"), "win.diff");
    g_menu_append(tools, _("Bluetooth devices…"), "win.bluetooth");
    g_menu_append(tools, _("Device history…"), "win.history");
    g_menu_append(tools, _("Save tree as PNG…"), "win.screenshot");
    g_menu_append_section(menu, NULL, G_MENU_MODEL(tools));
    g_object_unref(tools);

    GMenu *toggles = g_menu_new();
    g_menu_append(toggles, _("Show event log"), "win.show-log");
    g_menu_append(toggles, _("Hotplug notifications"), "win.notifications");
    g_menu_append_section(menu, NULL, G_MENU_MODEL(toggles));
    g_object_unref(toggles);
    return menu;
}

static void on_refresh_clicked(GtkButton *b, gpointer user)
{ (void)b; win_refresh((Win *)user); }

/* --- teardown --- */

/* Runs while the widgets are still alive, so geometry is valid here. */
static gboolean on_close_request(GtkWindow *window, gpointer user)
{
    (void)window;
    Win *w = user;
    int ww = gtk_widget_get_width(w->window);
    int wh = gtk_widget_get_height(w->window);
    if (ww >= 200) w->prefs.win_w = ww;
    if (wh >= 200) w->prefs.win_h = wh;
    w->prefs.outer_pos = gtk_paned_get_position(GTK_PANED(w->outer));
    w->prefs.inner_pos = gtk_paned_get_position(GTK_PANED(w->inner));
    ue_prefs_save(&w->prefs);
    return GDK_EVENT_PROPAGATE; /* allow the window to close */
}

static void on_window_destroy(GtkWidget *widget, gpointer user)
{
    (void)widget;
    Win *w = user;
    ue_log_free(w->log);
    ue_tree_free(w->tree);
    ue_detail_free(w->detail);
    usb_tree_free(w->root);
    g_clear_object(&w->devmenu);
    usbids_free();
    pciids_free();
    g_free(w);
}

/* ------------------------------------------------------------- build */

GtkWidget *ue_main_window_new(AdwApplication *app)
{
    Win *w = g_new0(Win, 1);
    ue_prefs_load(&w->prefs);

    w->root = usb_enumerate();
    if (!w->root)
        w->root = g_new0(UsbNode, 1);

    w->window = adw_application_window_new(GTK_APPLICATION(app));
    gtk_window_set_title(GTK_WINDOW(w->window), "usbexplorer");
    gtk_window_set_default_size(GTK_WINDOW(w->window), w->prefs.win_w, w->prefs.win_h);

    /* Actions. */
    static const GActionEntry entries[] = {
        { "copy-info",     act_copy_info,     NULL, NULL,       NULL, {0} },
        { "copy-udev",     act_copy_udev,     NULL, NULL,       NULL, {0} },
        { "search-online", act_search_online, NULL, NULL,       NULL, {0} },
        { "open-sysfs",    act_open_sysfs,    NULL, NULL,       NULL, {0} },
        { "reset",         act_reset,         NULL, NULL,       NULL, {0} },
        { "restart",       act_restart,       NULL, NULL,       NULL, {0} },
        { "port-cycle",    act_port_cycle,    NULL, NULL,       NULL, {0} },
        { "autosuspend-on",  act_autosuspend_on,  NULL, NULL,   NULL, {0} },
        { "autosuspend-off", act_autosuspend_off, NULL, NULL,   NULL, {0} },
        { "wakeup-on",       act_wakeup_on,       NULL, NULL,   NULL, {0} },
        { "wakeup-off",      act_wakeup_off,      NULL, NULL,   NULL, {0} },
        { "diff",            act_diff,            NULL, NULL,   NULL, {0} },
        { "screenshot",      act_screenshot,      NULL, NULL,   NULL, {0} },
        { "bluetooth",       act_bluetooth,       NULL, NULL,   NULL, {0} },
        { "dmesg",           act_dmesg,           NULL, NULL,   NULL, {0} },
        { "latency",         act_latency,         NULL, NULL,   NULL, {0} },
        { "speedtest",       act_speedtest,       NULL, NULL,   NULL, {0} },
        { "history",         act_history,         NULL, NULL,   NULL, {0} },
        { "show-log",      NULL, NULL, "true",      change_show_log, {0} },
        { "notifications", NULL, NULL, "true",      change_notify,   {0} },
        { "theme",         NULL, "s", "'Default'",  change_theme,    {0} },
        { "color-scheme",  NULL, "s", "'system'",   change_scheme,   {0} },
    };
    g_action_map_add_action_entries(G_ACTION_MAP(w->window), entries,
                                    G_N_ELEMENTS(entries), w);

    /* Header bar. */
    AdwToolbarView *toolbar = ADW_TOOLBAR_VIEW(adw_toolbar_view_new());
    GtkWidget *header = adw_header_bar_new();
    w->title = ADW_WINDOW_TITLE(adw_window_title_new("usbexplorer", ""));
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), GTK_WIDGET(w->title));

    GtkWidget *refresh = gtk_button_new_from_icon_name("view-refresh-symbolic");
    gtk_widget_set_tooltip_text(refresh, _("Refresh (re-enumerate)"));
    g_signal_connect(refresh, "clicked", G_CALLBACK(on_refresh_clicked), w);
    adw_header_bar_pack_start(ADW_HEADER_BAR(header), refresh);

    GtkWidget *search = gtk_search_entry_new();
    gtk_widget_set_size_request(search, 220, -1);
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(search),
                                          _("Search VID:PID, name, serial…"));
    gtk_accessible_update_property(GTK_ACCESSIBLE(search),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   _("Search devices"), -1);
    g_signal_connect(search, "search-changed", G_CALLBACK(on_search_changed), w);
    adw_header_bar_pack_start(ADW_HEADER_BAR(header), search);

    GtkWidget *menu_btn = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menu_btn), "open-menu-symbolic");
    gtk_widget_set_tooltip_text(menu_btn, _("Main menu"));
    GMenu *primary = build_primary_menu();
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menu_btn), G_MENU_MODEL(primary));
    g_object_unref(primary);
    adw_header_bar_pack_end(ADW_HEADER_BAR(header), menu_btn);

    adw_toolbar_view_add_top_bar(toolbar, header);

    /* Panes. */
    w->tree = ue_tree_new(w->root, on_tree_select, w);
    w->detail = ue_detail_new();
    w->log = ue_log_new(on_hotplug, w);
    w->log_pane = ue_log_widget(w->log);

    gtk_accessible_update_property(GTK_ACCESSIBLE(ue_tree_widget(w->tree)),
        GTK_ACCESSIBLE_PROPERTY_LABEL, _("USB device tree"), -1);
    gtk_accessible_update_property(GTK_ACCESSIBLE(ue_detail_widget(w->detail)),
        GTK_ACCESSIBLE_PROPERTY_LABEL, _("Device details"), -1);
    gtk_accessible_update_property(GTK_ACCESSIBLE(w->log_pane),
        GTK_ACCESSIBLE_PROPERTY_LABEL, _("Event log"), -1);

    w->inner = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_start_child(GTK_PANED(w->inner), ue_detail_widget(w->detail));
    gtk_paned_set_end_child(GTK_PANED(w->inner), w->log_pane);
    gtk_paned_set_position(GTK_PANED(w->inner), w->prefs.inner_pos);
    gtk_paned_set_resize_start_child(GTK_PANED(w->inner), TRUE);

    w->outer = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_start_child(GTK_PANED(w->outer), ue_tree_widget(w->tree));
    gtk_paned_set_end_child(GTK_PANED(w->outer), w->inner);
    gtk_paned_set_position(GTK_PANED(w->outer), w->prefs.outer_pos);
    gtk_widget_set_vexpand(w->outer, TRUE);

    /* Context menu on the tree (right-click); popover built on demand. */
    w->devmenu = build_device_menu();
    GtkGesture *click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_SECONDARY);
    g_signal_connect(click, "pressed", G_CALLBACK(on_right_click), w);
    gtk_widget_add_controller(ue_tree_listview(w->tree), GTK_EVENT_CONTROLLER(click));

    /* Status bar. */
    w->status = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(w->status), 0.0f);
    gtk_widget_add_css_class(w->status, "dim-label");
    gtk_widget_set_margin_top(w->status, 3);
    gtk_widget_set_margin_bottom(w->status, 3);

    GtkWidget *body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(body), w->outer);
    gtk_box_append(GTK_BOX(body), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(body), w->status);

    adw_toolbar_view_set_content(toolbar, body);
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(w->window),
                                       GTK_WIDGET(toolbar));

    /* Apply persisted appearance + state. */
    ue_css_apply_theme(w->prefs.theme);
    ue_css_set_color_scheme(w->prefs.color_scheme);
    gtk_widget_set_visible(w->log_pane, w->prefs.log_visible);
    g_action_group_change_action_state(G_ACTION_GROUP(w->window), "theme",
                                       g_variant_new_string(w->prefs.theme));
    g_action_group_change_action_state(G_ACTION_GROUP(w->window), "color-scheme",
                                       g_variant_new_string(w->prefs.color_scheme));
    g_action_group_change_action_state(G_ACTION_GROUP(w->window), "show-log",
                                       g_variant_new_boolean(w->prefs.log_visible));
    g_action_group_change_action_state(G_ACTION_GROUP(w->window), "notifications",
                                       g_variant_new_boolean(w->prefs.notifications));

    /* The default selection (row 0) doesn't emit "notify::selected-item", so
     * prime the detail pane for it explicitly. */
    if (w->root->n_children) {
        w->selected = w->root->children[0];
        ue_detail_show(w->detail, w->selected);
    }

    set_status(w, NULL);
    g_signal_connect(w->window, "close-request", G_CALLBACK(on_close_request), w);
    g_signal_connect(w->window, "destroy", G_CALLBACK(on_window_destroy), w);
    return w->window;
}
