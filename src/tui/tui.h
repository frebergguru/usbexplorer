/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef USBEXPLORER_TUI_TUI_H
#define USBEXPLORER_TUI_TUI_H

#include <stdbool.h>
#include <stddef.h>

#include "usb/enumerate.h"

/*
 * Shared state for the ncurses TUI.  One TuiState is created by tui_run() and
 * threaded through the tree / detail / search / command modules.  The tree
 * module flattens the device tree into a list of visible TreeRow entries each
 * redraw; the detail module renders the selected node into scrollable lines.
 */

/* Curses color pairs. */
enum {
    CP_DEFAULT = 0,
    CP_HEADER,        /* pane titles / status bar      */
    CP_SELECT,        /* selected row                  */
    CP_SPEED,         /* speed badge                   */
    CP_WARN,          /* warning-level finding         */
    CP_DANGER,        /* danger-level finding          */
    CP_DIM,           /* secondary text                */
    CP_FIELD,         /* detail field name             */
    CP_CONTROLLER,    /* host controller rows          */
};

typedef struct {
    UsbNode *node;
    int  depth;
    int  iface_index;   /* >= 0 => this row is interface N of node, else -1 */
} TreeRow;

typedef struct {
    char *text;
    int   attr;         /* a COLOR_PAIR(...) | A_* attribute, or 0 */
} TuiLine;

typedef enum { FOCUS_TREE, FOCUS_DETAIL } TuiFocus;

typedef struct {
    UsbNode *root;          /* owned: enumerated by tui_run()         */

    TreeRow *rows;          /* flattened visible tree                 */
    size_t   n_rows, cap_rows;
    size_t   sel;           /* selected row index                     */
    size_t   tree_top;      /* first visible tree row (scroll)        */

    const UsbNode **expanded;  /* set of expanded nodes               */
    size_t   n_expanded, cap_expanded;

    TuiLine *detail;        /* detail lines for the selected node     */
    size_t   n_detail, cap_detail;
    size_t   detail_top;    /* scroll offset                          */
    const UsbNode *detail_for; /* node the detail buffer was built for */

    TuiFocus focus;
    bool     show_interfaces;
    bool     quit;

    char     search[128];   /* last search query                      */
    char     msg[256];      /* status/command-line message            */
    int      tree_w;        /* tree pane width (columns)              */
    int      rows_h;        /* visible content height                 */
} TuiState;

/* Entry point: enumerate, run the UI, free.  Returns a process exit status. */
int tui_run(void);

/* --- tree module --- */
void   tui_tree_rebuild(TuiState *s);
bool   tui_is_expanded(const TuiState *s, const UsbNode *n);
void   tui_set_expanded(TuiState *s, const UsbNode *n, bool on);
void   tui_expand_all(TuiState *s, bool on);
void   tui_expand_ancestors(TuiState *s, const UsbNode *n);
size_t tui_row_of_node(const TuiState *s, const UsbNode *n);

/* --- detail module --- */
void tui_detail_build(TuiState *s);  /* (re)build for the selected node */
void tui_detail_free(TuiState *s);

/* --- search module --- */
bool tui_node_matches(const UsbNode *n, const char *query);
/* Move selection to the next (dir=+1) / previous (dir=-1) match of s->search,
 * expanding ancestors so it is visible.  Returns true if a match was found. */
bool tui_search_step(TuiState *s, int dir);

/* --- command module --- */
void tui_command_exec(TuiState *s, const char *cmd);

/* Show scrollable text in a full-screen pager (implemented in tui_main). */
void tui_pager_show(TuiState *s, const char *title, const char *text);

#endif /* USBEXPLORER_TUI_TUI_H */
