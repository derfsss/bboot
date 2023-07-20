/*
 * ub2lb -- UBoot second level bootloader.
 *
 * Copyright (C) 2006 - 2010  Giuseppe Coviello <cjg@cruxppc.org>.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 *
 * Written by: Michal Schulz <michalsc@users.sourceforge.net>
 */

#ifndef AOSLIST_H
#define AOSLIST_H

typedef struct node {
    struct node *n_succ, *n_pred;
} node_t;

typedef struct list {
    struct node *l_head, *l_tail, *l_tailpred;
} list_t;

static inline void alist_init(list_t *l)
{
    l->l_head = (node_t *)&l->l_tail;
    l->l_tail = NULL;
    l->l_tailpred = (node_t *)l;
}

static inline void alist_add_tail(list_t *l, node_t *n)
{
    n->n_succ = (node_t *)&l->l_tail;
    n->n_pred = l->l_tailpred;

    l->l_tailpred->n_succ = n;
    l->l_tailpred = n;
}

#endif
