/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "tui/tui.h"

#include "usb/actions.h"
#include "usb/report.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool eq(const char *a, const char *b) { return strcmp(a, b) == 0; }

static const UsbNode *sel_device(TuiState *s)
{
    if (!s->n_rows)
        return NULL;
    const UsbNode *n = s->rows[s->sel].node;
    return (n->kind == NODE_DEVICE || n->kind == NODE_HUB ||
            n->kind == NODE_ROOT_HUB) ? n : NULL;
}

/* Re-enumerate, discarding UI state tied to the old tree. */
static void refresh(TuiState *s)
{
    UsbNode *fresh = usb_enumerate();
    if (!fresh) {
        snprintf(s->msg, sizeof s->msg, "refresh failed: cannot enumerate");
        return;
    }
    usb_tree_free(s->root);
    s->root = fresh;
    s->n_expanded = 0;
    s->detail_for = NULL;
    s->sel = 0;
    s->tree_top = 0;
    tui_expand_all(s, true);
    tui_tree_rebuild(s);
    snprintf(s->msg, sizeof s->msg, "refreshed");
}

/* Open a URI (or path) in the desktop default handler, detached. */
static void open_uri(const char *uri)
{
    pid_t pid = fork();
    if (pid == 0) {
        int null = open("/dev/null", O_WRONLY);
        if (null >= 0) { dup2(null, 1); dup2(null, 2); }
        execlp("xdg-open", "xdg-open", uri, (char *)NULL);
        _exit(127);
    }
}

/* Run a privileged device action on the selected device. */
static void do_action(TuiState *s, UsbActionType act)
{
    const UsbNode *n = sel_device(s);
    if (!n) {
        snprintf(s->msg, sizeof s->msg, "select a device first");
        return;
    }
    char err[200];
    if (usb_action_perform(act, n->busnum, n->devnum, err, sizeof err) == 0) {
        snprintf(s->msg, sizeof s->msg, "%s ok (bus %d dev %d)",
                 usb_action_name(act), n->busnum, n->devnum);
        refresh(s);
    } else {
        snprintf(s->msg, sizeof s->msg, "%s", err);
    }
}

/* Show a report string for the selected device in the pager. */
static void show_report(TuiState *s, char *(*fn)(const UsbNode *), const char *title)
{
    const UsbNode *n = sel_device(s);
    if (!n) {
        snprintf(s->msg, sizeof s->msg, "select a device first");
        return;
    }
    char *txt = fn(n);
    if (txt) {
        tui_pager_show(s, title, txt);
        free(txt);
    }
}

static char *speed_256(const UsbNode *n) { return report_speedtest(n, 256L << 20); }

void tui_command_exec(TuiState *s, const char *cmd)
{
    while (*cmd == ' ')
        cmd++;

    /* Split into verb + optional argument. */
    char verb[32] = "";
    const char *arg = "";
    size_t i = 0;
    while (cmd[i] && cmd[i] != ' ' && i < sizeof verb - 1) {
        verb[i] = cmd[i];
        i++;
    }
    verb[i] = '\0';
    while (cmd[i] == ' ')
        i++;
    arg = cmd + i;
    if (!verb[0])
        return;

    if (eq(verb, "q") || eq(verb, "quit") || eq(verb, "x")) {
        s->quit = true;
    } else if (eq(verb, "interfaces") || eq(verb, "if")) {
        s->show_interfaces = !s->show_interfaces;
        tui_tree_rebuild(s);
        snprintf(s->msg, sizeof s->msg, "interfaces %s",
                 s->show_interfaces ? "shown" : "hidden");
    } else if (eq(verb, "expand")) {
        tui_expand_all(s, true); tui_tree_rebuild(s);
    } else if (eq(verb, "collapse")) {
        tui_expand_all(s, false); tui_tree_rebuild(s); s->sel = 0;
    } else if (eq(verb, "refresh") || eq(verb, "r")) {
        refresh(s);
    } else if (eq(verb, "help") || eq(verb, "h")) {
        snprintf(s->msg, sizeof s->msg, "press ? for the keybinding help screen");
    }
    /* --- per-device reports (shared with CLI/GUI via usb/report) --- */
    else if (eq(verb, "dmesg")) {
        show_report(s, report_dmesg, "dmesg");
    } else if (eq(verb, "latency")) {
        show_report(s, report_latency, "Interrupt latency");
    } else if (eq(verb, "speedtest") || eq(verb, "speed")) {
        show_report(s, speed_256, "Read-speed test");
    } else if (eq(verb, "bluetooth") || eq(verb, "bt")) {
        char *txt = report_bluetooth();
        if (txt) { tui_pager_show(s, "Bluetooth", txt); free(txt); }
    } else if (eq(verb, "history")) {
        char *txt = report_history(0);
        if (txt) { tui_pager_show(s, "Device history", txt); free(txt); }
    }
    /* --- privileged device actions --- */
    else if (eq(verb, "reset")) {
        do_action(s, ACTION_RESET);
    } else if (eq(verb, "restart")) {
        do_action(s, ACTION_RESTART);
    } else if (eq(verb, "portcycle") || eq(verb, "port-cycle")) {
        do_action(s, ACTION_PORT_CYCLE);
    } else if (eq(verb, "autosuspend")) {
        do_action(s, eq(arg, "off") ? ACTION_AUTOSUSPEND_OFF : ACTION_AUTOSUSPEND_ON);
    } else if (eq(verb, "wakeup")) {
        do_action(s, eq(arg, "off") ? ACTION_WAKEUP_OFF : ACTION_WAKEUP_ON);
    }
    /* --- open helpers --- */
    else if (eq(verb, "online")) {
        const UsbNode *n = sel_device(s);
        if (n) {
            char url[64];
            snprintf(url, sizeof url, "https://the.usbids.com/%04x/%04x", n->vid, n->pid);
            open_uri(url);
            snprintf(s->msg, sizeof s->msg, "opened %s", url);
        }
    } else if (eq(verb, "open")) {
        const UsbNode *n = sel_device(s);
        if (n) { open_uri(n->sysfs_path); snprintf(s->msg, sizeof s->msg, "opened sysfs path"); }
    } else {
        snprintf(s->msg, sizeof s->msg, "unknown command: %s", verb);
    }
}
