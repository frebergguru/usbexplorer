/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "tui/tui.h"

#include "usb/badusb.h"
#include "util/pciids.h"
#include "util/udev_rule.h"
#include "util/usbids.h"

#include <locale.h>
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ----------------------------------------------------------- drawing utils */

static void print_clipped(int y, int x, int w, int attr, const char *text)
{
    if (w <= 0)
        return;
    char buf[1024];
    snprintf(buf, sizeof buf, "%-*.*s", w, w, text);
    if (attr)
        attron(attr);
    mvaddnstr(y, x, buf, w);
    if (attr)
        attroff(attr);
}

static void compute_layout(TuiState *s)
{
    s->tree_w = COLS * 2 / 5;
    if (s->tree_w < 24)
        s->tree_w = COLS < 24 ? COLS : 24;
    if (s->tree_w > COLS - 10)
        s->tree_w = COLS - 10;
    s->rows_h = LINES - 2;     /* title line + status line */
    if (s->rows_h < 1)
        s->rows_h = 1;
}

/* Visual attribute for a tree row (before selection highlight). */
static int row_attr(const TreeRow *r)
{
    if (r->iface_index >= 0)
        return COLOR_PAIR(CP_DIM);
    switch (r->node->kind) {
    case NODE_CONTROLLER:
        return COLOR_PAIR(CP_CONTROLLER) | A_BOLD;
    default:
        switch (badusb_worst(r->node)) {
        case FINDING_DANGER: return COLOR_PAIR(CP_DANGER);
        case FINDING_WARN:   return COLOR_PAIR(CP_WARN);
        default:             return 0;
        }
    }
}

static bool row_has_children(const TuiState *s, const TreeRow *r)
{
    if (r->iface_index >= 0)
        return false;
    const UsbNode *n = r->node;
    if (n->n_children)
        return true;
    return s->show_interfaces && n->n_interfaces &&
           (n->kind == NODE_DEVICE || n->kind == NODE_HUB || n->kind == NODE_ROOT_HUB);
}

static void row_label(const TuiState *s, const TreeRow *r, char *out, size_t sz)
{
    const char *marker = "   ";
    if (row_has_children(s, r))
        marker = tui_is_expanded(s, r->node) ? "[-]" : "[+]";

    char indent[64] = "";
    int d = r->depth * 2;
    if (d > (int)sizeof indent - 1)
        d = sizeof indent - 1;
    memset(indent, ' ', d);
    indent[d] = '\0';

    if (r->iface_index >= 0) {
        const UsbInterface *ui = &r->node->interfaces[r->iface_index];
        snprintf(out, sz, "%s    if %u: 0x%02x/0x%02x/0x%02x %s", indent,
                 ui->number, ui->cls, ui->subcls, ui->proto,
                 ui->driver[0] ? ui->driver : "");
        return;
    }

    const UsbNode *n = r->node;
    if (n->kind == NODE_CONTROLLER) {
        const char *cn = pciids_vendor(n->controller ? n->controller->pci_vendor : 0);
        snprintf(out, sz, "%s%s HC %s %s", indent, marker,
                 n->controller ? n->controller->pci_addr : n->devname, cn ? cn : "");
        return;
    }

    const char *warn = badusb_worst(n) == FINDING_DANGER ? "! "
                     : badusb_worst(n) == FINDING_WARN ? "* " : "";
    if (n->kind == NODE_ROOT_HUB) {
        snprintf(out, sz, "%s%s %s [%s]", indent, marker, n->devname,
                 usb_speed_badge(n->speed));
    } else {
        const char *name = n->product[0] ? n->product : usbids_product(n->vid, n->pid);
        snprintf(out, sz, "%s%s %s%s [%s] %04x:%04x", indent, marker, warn,
                 name ? name : "device", usb_speed_badge(n->speed), n->vid, n->pid);
    }
}

static void ensure_visible(TuiState *s)
{
    if (s->sel < s->tree_top)
        s->tree_top = s->sel;
    else if (s->sel >= s->tree_top + (size_t)s->rows_h)
        s->tree_top = s->sel - s->rows_h + 1;
}

static void draw(TuiState *s)
{
    erase();
    compute_layout(s);

    /* Title. */
    char title[256];
    snprintf(title, sizeof title, " usbexplorer  —  %zu visible nodes%s",
             s->n_rows, s->show_interfaces ? "  (interfaces shown)" : "");
    print_clipped(0, 0, COLS, COLOR_PAIR(CP_HEADER) | A_BOLD | A_REVERSE, title);

    /* Tree pane. */
    ensure_visible(s);
    for (int i = 0; i < s->rows_h; i++) {
        size_t idx = s->tree_top + (size_t)i;
        if (idx >= s->n_rows)
            break;
        char label[1024];
        row_label(s, &s->rows[idx], label, sizeof label);
        int attr = row_attr(&s->rows[idx]);
        if (idx == s->sel)
            attr = A_REVERSE | (s->focus == FOCUS_TREE ? A_BOLD : 0);
        print_clipped(1 + i, 0, s->tree_w, attr, label);
    }

    /* Separator. */
    for (int i = 0; i < s->rows_h; i++)
        mvaddch(1 + i, s->tree_w, ACS_VLINE);

    /* Detail pane. */
    int dx = s->tree_w + 2;
    int dw = COLS - dx;
    for (int i = 0; i < s->rows_h; i++) {
        size_t idx = s->detail_top + (size_t)i;
        if (idx >= s->n_detail)
            break;
        print_clipped(1 + i, dx, dw, s->detail[idx].attr, s->detail[idx].text);
    }

    /* Status line. */
    char status[512];
    snprintf(status, sizeof status,
             " %s | %s | /:search  n/N:next  ::cmd  Tab:pane  ?:help  q:quit %s%s",
             s->focus == FOCUS_TREE ? "TREE" : "DETAIL",
             s->search[0] ? s->search : "-",
             s->msg[0] ? "| " : "", s->msg);
    print_clipped(LINES - 1, 0, COLS, COLOR_PAIR(CP_HEADER) | A_REVERSE, status);

    refresh();
}

/* ------------------------------------------------------- modal line prompt */

/* Read a line at the status bar.  Returns true on Enter, false on Esc. */
static bool prompt(TuiState *s, const char *prefix, char *buf, size_t sz)
{
    size_t len = 0;
    buf[0] = '\0';
    curs_set(1);
    for (;;) {
        char line[512];
        snprintf(line, sizeof line, "%s%s", prefix, buf);
        print_clipped(LINES - 1, 0, COLS, COLOR_PAIR(CP_HEADER) | A_REVERSE, line);
        move(LINES - 1, (int)(strlen(prefix) + len));
        refresh();

        int c = getch();
        if (c == '\n' || c == KEY_ENTER) {
            curs_set(0);
            return true;
        }
        if (c == 27) { /* Esc */
            curs_set(0);
            buf[0] = '\0';
            return false;
        }
        if (c == KEY_BACKSPACE || c == 127 || c == 8) {
            if (len > 0)
                buf[--len] = '\0';
        } else if (c >= 32 && c < 127 && len + 1 < sz) {
            buf[len++] = (char)c;
            buf[len] = '\0';
        }
        (void)s;
    }
}

/* ------------------------------------------------------------- overlays */

static void show_overlay(const char *const *lines, int n)
{
    int w = 0;
    for (int i = 0; i < n; i++) {
        int l = (int)strlen(lines[i]);
        if (l > w)
            w = l;
    }
    w += 4;
    int h = n + 2;
    int y0 = (LINES - h) / 2, x0 = (COLS - w) / 2;
    if (y0 < 0) y0 = 0;
    if (x0 < 0) x0 = 0;

    WINDOW *win = newwin(h, w, y0, x0);
    box(win, 0, 0);
    for (int i = 0; i < n; i++)
        mvwaddnstr(win, 1 + i, 2, lines[i], w - 4);
    wrefresh(win);
    getch();
    delwin(win);
}

/* Scrollable pager for arbitrary report text (dmesg, latency, bluetooth, …). */
void tui_pager_show(TuiState *s, const char *title, const char *text)
{
    (void)s;
    /* Split a mutable copy into lines. */
    char *copy = strdup(text ? text : "");
    if (!copy)
        return;
    size_t cap = 64, n = 0;
    char **lines = malloc(cap * sizeof *lines);
    for (char *p = copy; p && *p;) {
        if (n == cap) { cap *= 2; lines = realloc(lines, cap * sizeof *lines); }
        lines[n++] = p;
        char *nl = strchr(p, '\n');
        if (!nl)
            break;
        *nl = '\0';
        p = nl + 1;
    }

    int top = 0;
    for (;;) {
        erase();
        char hdr[256];
        snprintf(hdr, sizeof hdr, " %s  (j/k scroll, q close) ", title);
        print_clipped(0, 0, COLS, COLOR_PAIR(CP_HEADER) | A_REVERSE | A_BOLD, hdr);
        int h = LINES - 1;
        for (int i = 0; i < h && top + i < (int)n; i++)
            print_clipped(1 + i, 0, COLS, 0, lines[top + i]);
        refresh();

        int c = getch();
        if (c == 'q' || c == 27 || c == '\n')
            break;
        else if ((c == 'j' || c == KEY_DOWN) && top + h < (int)n)
            top++;
        else if ((c == 'k' || c == KEY_UP) && top > 0)
            top--;
        else if (c == KEY_NPAGE)
            top = (top + h < (int)n) ? top + h : top;
        else if (c == KEY_PPAGE)
            top = (top - h > 0) ? top - h : 0;
        else if (c == 'g')
            top = 0;
        else if (c == 'G')
            top = (int)n - h > 0 ? (int)n - h : 0;
    }
    free(lines);
    free(copy);
}

static void show_help(void)
{
    static const char *const help[] = {
        "usbexplorer TUI — keys",
        "",
        "  j / k / Up / Down     move selection",
        "  h / l / Left / Right  collapse / expand",
        "  Enter                 toggle expand",
        "  g / G                 first / last",
        "  PgUp / PgDn           page",
        "  Tab                   switch tree <-> detail focus",
        "  /                     search (VID:PID, name, serial, driver...)",
        "  n / N                 next / previous match",
        "  i                     toggle interface rows",
        "  u                     show udev rule for selected device",
        "  : commands:",
        "      interfaces expand collapse refresh",
        "      dmesg latency speedtest bluetooth history",
        "      reset restart portcycle  autosuspend on|off  wakeup on|off",
        "      online (usb-ids.com)   open (sysfs in file manager)",
        "  ? / F1                this help        q   quit",
        "",
        "  (press any key to close)",
    };
    show_overlay(help, (int)(sizeof help / sizeof help[0]));
}

static void show_udev_rule(TuiState *s)
{
    if (s->n_rows == 0)
        return;
    const UsbNode *n = s->rows[s->sel].node;
    if (n->kind == NODE_CONTROLLER || n->kind == NODE_ROOT) {
        snprintf(s->msg, sizeof s->msg, "select a device first");
        return;
    }

    char *text = NULL;
    size_t tlen = 0;
    FILE *ms = open_memstream(&text, &tlen);
    if (!ms)
        return;
    udev_rule_write(ms, n->vid, n->pid, n);
    fclose(ms);

    /* Split into lines for the overlay. */
    const char *lines[24];
    int nl = 0;
    char *p = text;
    while (p && *p && nl < 23) {
        lines[nl++] = p;
        char *e = strchr(p, '\n');
        if (!e)
            break;
        *e = '\0';
        p = e + 1;
    }
    lines[nl++] = "(press any key to close)";
    show_overlay(lines, nl);
    free(text);
}

/* ------------------------------------------------------------ navigation */

static UsbNode *sel_node(TuiState *s)
{
    return s->n_rows ? s->rows[s->sel].node : NULL;
}

static void move_sel(TuiState *s, long delta)
{
    if (s->n_rows == 0)
        return;
    long v = (long)s->sel + delta;
    if (v < 0)
        v = 0;
    if (v >= (long)s->n_rows)
        v = (long)s->n_rows - 1;
    s->sel = (size_t)v;
}

static void scroll_detail(TuiState *s, long delta)
{
    long v = (long)s->detail_top + delta;
    long max = (long)s->n_detail - 1;
    if (max < 0)
        max = 0;
    if (v < 0)
        v = 0;
    if (v > max)
        v = max;
    s->detail_top = (size_t)v;
}

static void key_collapse(TuiState *s)
{
    const TreeRow *r = &s->rows[s->sel];
    if (r->iface_index < 0 && tui_is_expanded(s, r->node) && row_has_children(s, r)) {
        tui_set_expanded(s, r->node, false);
        tui_tree_rebuild(s);
    } else if (r->node->parent && r->node->parent->kind != NODE_ROOT) {
        size_t row = tui_row_of_node(s, r->node->parent);
        if (row != (size_t)-1)
            s->sel = row;
    }
}

static void key_expand(TuiState *s)
{
    const TreeRow *r = &s->rows[s->sel];
    if (r->iface_index < 0 && row_has_children(s, r)) {
        if (!tui_is_expanded(s, r->node)) {
            tui_set_expanded(s, r->node, true);
            tui_tree_rebuild(s);
        } else {
            move_sel(s, 1);
        }
    }
}

static void handle_mouse(TuiState *s)
{
    MEVENT me;
    if (getmouse(&me) != OK)
        return;
    if (me.bstate & (BUTTON4_PRESSED)) {           /* wheel up */
        if (s->focus == FOCUS_DETAIL) scroll_detail(s, -3); else move_sel(s, -3);
    } else if (me.bstate & (BUTTON5_PRESSED)) {    /* wheel down */
        if (s->focus == FOCUS_DETAIL) scroll_detail(s, 3); else move_sel(s, 3);
    } else if (me.bstate & BUTTON1_CLICKED) {
        if (me.x < s->tree_w && me.y >= 1 && me.y < 1 + s->rows_h) {
            size_t idx = s->tree_top + (size_t)(me.y - 1);
            if (idx < s->n_rows) {
                s->sel = idx;
                s->focus = FOCUS_TREE;
            }
        } else if (me.x > s->tree_w) {
            s->focus = FOCUS_DETAIL;
        }
    }
}

/* ----------------------------------------------------------------- loop */

static void init_colors(void)
{
    if (!has_colors())
        return;
    start_color();
    use_default_colors();
    init_pair(CP_HEADER,     COLOR_CYAN,    -1);
    init_pair(CP_SELECT,     COLOR_BLACK,   COLOR_CYAN);
    init_pair(CP_SPEED,      COLOR_GREEN,   -1);
    init_pair(CP_WARN,       COLOR_YELLOW,  -1);
    init_pair(CP_DANGER,     COLOR_RED,     -1);
    init_pair(CP_DIM,        COLOR_BLUE,    -1);
    init_pair(CP_FIELD,      COLOR_MAGENTA, -1);
    init_pair(CP_CONTROLLER, COLOR_MAGENTA, -1);
}

int tui_run(void)
{
    TuiState s = {0};
    s.root = usb_enumerate();
    if (!s.root) {
        fprintf(stderr, "usbexplorer: cannot enumerate USB devices\n");
        return 1;
    }
    tui_expand_all(&s, true);
    tui_tree_rebuild(&s);

    setlocale(LC_ALL, "");
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    set_escdelay(25);
    mousemask(BUTTON1_CLICKED | BUTTON4_PRESSED | BUTTON5_PRESSED, NULL);
    init_colors();

    const UsbNode *built_for = NULL;
    while (!s.quit) {
        UsbNode *cur = sel_node(&s);
        if (cur != built_for) {
            tui_detail_build(&s);
            built_for = cur;
        }
        draw(&s);

        int c = getch();
        s.msg[0] = '\0';
        switch (c) {
        case 'q': s.quit = true; break;
        case '\t': s.focus = (s.focus == FOCUS_TREE) ? FOCUS_DETAIL : FOCUS_TREE; break;
        case 'j': case KEY_DOWN:
            if (s.focus == FOCUS_DETAIL) scroll_detail(&s, 1); else move_sel(&s, 1);
            break;
        case 'k': case KEY_UP:
            if (s.focus == FOCUS_DETAIL) scroll_detail(&s, -1); else move_sel(&s, -1);
            break;
        case 'h': case KEY_LEFT:
            if (s.focus == FOCUS_TREE) key_collapse(&s);
            break;
        case 'l': case KEY_RIGHT:
            if (s.focus == FOCUS_TREE) key_expand(&s);
            break;
        case '\n': case KEY_ENTER:
            if (s.focus == FOCUS_TREE && s.n_rows) {
                const TreeRow *r = &s.rows[s.sel];
                if (r->iface_index < 0 && row_has_children(&s, r)) {
                    tui_set_expanded(&s, r->node, !tui_is_expanded(&s, r->node));
                    tui_tree_rebuild(&s);
                }
            }
            break;
        case 'g': case KEY_HOME:
            if (s.focus == FOCUS_DETAIL) s.detail_top = 0; else s.sel = 0;
            break;
        case 'G': case KEY_END:
            if (s.focus == FOCUS_DETAIL) scroll_detail(&s, (long)s.n_detail);
            else if (s.n_rows) s.sel = s.n_rows - 1;
            break;
        case KEY_NPAGE:
            if (s.focus == FOCUS_DETAIL) scroll_detail(&s, s.rows_h);
            else move_sel(&s, s.rows_h);
            break;
        case KEY_PPAGE:
            if (s.focus == FOCUS_DETAIL) scroll_detail(&s, -s.rows_h);
            else move_sel(&s, -s.rows_h);
            break;
        case 'i':
            tui_command_exec(&s, "interfaces");
            break;
        case 'u':
            show_udev_rule(&s);
            break;
        case '/':
            if (prompt(&s, "/", s.search, sizeof s.search) && s.search[0])
                tui_search_step(&s, +1);
            break;
        case 'n': tui_search_step(&s, +1); break;
        case 'N': tui_search_step(&s, -1); break;
        case ':': {
            char cmd[256];
            if (prompt(&s, ":", cmd, sizeof cmd) && cmd[0])
                tui_command_exec(&s, cmd);
            break;
        }
        case '?': case KEY_F(1):
            show_help();
            break;
        case KEY_RESIZE:
            break; /* re-laid-out on next draw */
        case KEY_MOUSE:
            handle_mouse(&s);
            break;
        default:
            break;
        }
    }

    endwin();

    tui_detail_free(&s);
    free(s.detail);
    free(s.rows);
    free(s.expanded);
    usb_tree_free(s.root);
    usbids_free();
    pciids_free();
    return 0;
}
