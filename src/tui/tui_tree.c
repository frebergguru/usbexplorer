/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "tui/tui.h"

#include <stdlib.h>

/* ---- expanded-node set (small; linear scan is fine) ---- */

bool tui_is_expanded(const TuiState *s, const UsbNode *n)
{
    for (size_t i = 0; i < s->n_expanded; i++)
        if (s->expanded[i] == n)
            return true;
    return false;
}

void tui_set_expanded(TuiState *s, const UsbNode *n, bool on)
{
    for (size_t i = 0; i < s->n_expanded; i++) {
        if (s->expanded[i] == n) {
            if (!on) /* remove by swapping with last */
                s->expanded[i] = s->expanded[--s->n_expanded];
            return;
        }
    }
    if (!on)
        return;
    if (s->n_expanded == s->cap_expanded) {
        size_t nc = s->cap_expanded ? s->cap_expanded * 2 : 64;
        const UsbNode **g = realloc(s->expanded, nc * sizeof *g);
        if (!g)
            return;
        s->expanded = g;
        s->cap_expanded = nc;
    }
    s->expanded[s->n_expanded++] = n;
}

static void expand_all_rec(TuiState *s, const UsbNode *n, bool on)
{
    tui_set_expanded(s, n, on);
    for (size_t i = 0; i < n->n_children; i++)
        expand_all_rec(s, n->children[i], on);
}

void tui_expand_all(TuiState *s, bool on)
{
    if (!on)
        s->n_expanded = 0;
    else
        for (size_t i = 0; i < s->root->n_children; i++)
            expand_all_rec(s, s->root->children[i], on);
}

void tui_expand_ancestors(TuiState *s, const UsbNode *n)
{
    for (UsbNode *p = n->parent; p && p->kind != NODE_ROOT; p = p->parent)
        tui_set_expanded(s, p, true);
}

/* ---- flatten the tree into visible rows ---- */

static void push_row(TuiState *s, UsbNode *node, int depth, int iface)
{
    if (s->n_rows == s->cap_rows) {
        size_t nc = s->cap_rows ? s->cap_rows * 2 : 128;
        TreeRow *g = realloc(s->rows, nc * sizeof *g);
        if (!g)
            return;
        s->rows = g;
        s->cap_rows = nc;
    }
    s->rows[s->n_rows++] = (TreeRow){ .node = node, .depth = depth,
                                      .iface_index = iface };
}

static bool node_is_deviceish(const UsbNode *n)
{
    return n->kind == NODE_DEVICE || n->kind == NODE_HUB ||
           n->kind == NODE_ROOT_HUB;
}

static void walk(TuiState *s, UsbNode *n, int depth)
{
    push_row(s, n, depth, -1);
    if (!tui_is_expanded(s, n))
        return;

    if (s->show_interfaces && node_is_deviceish(n))
        for (size_t i = 0; i < n->n_interfaces; i++)
            push_row(s, n, depth + 1, (int)i);

    for (size_t i = 0; i < n->n_children; i++)
        walk(s, n->children[i], depth + 1);
}

void tui_tree_rebuild(TuiState *s)
{
    s->n_rows = 0;
    for (size_t i = 0; i < s->root->n_children; i++)
        walk(s, s->root->children[i], 0);

    if (s->sel >= s->n_rows)
        s->sel = s->n_rows ? s->n_rows - 1 : 0;
}

size_t tui_row_of_node(const TuiState *s, const UsbNode *n)
{
    for (size_t i = 0; i < s->n_rows; i++)
        if (s->rows[i].node == n && s->rows[i].iface_index < 0)
            return i;
    return (size_t)-1;
}
