/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (C) 2011 by Jonathan Gordon
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ****************************************************************************/

#include "config.h"
#include "system.h"
#include "lcd.h"
#include "font.h"
#include "button.h"
#include "string.h"
#include "settings.h"
#include "kernel.h"
#include "file.h"

#include "action.h"
#include "screen_access.h"
#include "list.h"
#include "scrollbar.h"
#include "lang.h"
#include "sound.h"
#include "misc.h"
#include "viewport.h"
#include "statusbar-skinned.h"
#include "skin_engine/skin_engine.h"
#include "skin_engine/skin_display.h"
#include "appevents.h"

static struct listitem_viewport_cfg *listcfg[NB_SCREENS] = {NULL};
bool skinlist_force_lines = false;
bool skinlist_letterpress = false;
static struct gui_synclist *current_list;

static int current_row;
static int current_column;

void skinlist_set_cfg(enum screen_type screen,
                      struct listitem_viewport_cfg *cfg)
{
    if (listcfg[screen] != cfg)
    {
        if (listcfg[screen])
            screens[screen].scroll_stop_viewport(&listcfg[screen]->selected_item_vp.vp);
        listcfg[screen] = cfg;
        current_list = NULL;
        current_column = -1;
        current_row = -1;
    }
}

static bool skinlist_is_configured(enum screen_type screen,
                                    struct gui_synclist *list)
{
    return (listcfg[screen] != NULL) &&
            (!list || (list && list->selected_size == 1));
}
static int current_drawing_line;
static int offset_to_item(int offset, bool wrap)
{
    int item = current_drawing_line + offset;
    if (!current_list || current_list->nb_items == 0)
        return -1;
    if (item < 0)
    {
        if (!wrap)
            return -1;
        else
            item = (item + current_list->nb_items) % current_list->nb_items;
    }
    else if (item >= current_list->nb_items && !wrap)
        return -1;
    else
        item = item % current_list->nb_items;
    return item;
}

int skinlist_get_item_number()
{
    return current_drawing_line;
}

int skinlist_get_item_row()
{
    return current_row;
}

int skinlist_get_item_column()
{
    return current_column;
}


const char* skinlist_get_item_text(int offset, bool wrap, char* buf, size_t buf_size)
{
    int item = offset_to_item(offset, wrap);
    if (item < 0 || !current_list)
        return NULL;
    const char* ret = current_list->callback_get_item_name(
                    item, current_list->data, buf, buf_size);
    return P2STR((unsigned char*)ret);
}

enum themable_icons skinlist_get_item_icon(int offset, bool wrap)
{
    int item = offset_to_item(offset, wrap);
    if (item < 0 || !current_list || current_list->callback_get_item_icon == NULL)
        return Icon_NOICON;
    return current_list->callback_get_item_icon(item, current_list->data);
}

static bool is_selected = false;
bool skinlist_is_selected_item(void)
{
    return is_selected;
}

int skinlist_get_line_count(enum screen_type screen, struct gui_synclist *list)
{
    struct viewport *parent = (list->parent[screen]);
    if (!skinlist_is_configured(screen, list))
        return -1;
    if (listcfg[screen]->tile == true)
    {
        int rows = (parent->height / listcfg[screen]->height);
        int cols = (parent->width / listcfg[screen]->width);
        return rows*cols;
    }
    else
        return  (parent->height / listcfg[screen]->height);
}

static int current_item;
static int current_nbitems;
static bool needs_scrollbar[NB_SCREENS];
bool skinlist_needs_scrollbar(enum screen_type screen)
{
    return needs_scrollbar[screen];
}

void skinlist_get_scrollbar(int* nb_item, int* first_shown, int* last_shown)
{
    if (!skinlist_is_configured(0, NULL))
    {
        *nb_item = 0;
        *first_shown = 0;
        *last_shown = 0;
    }
    else
    {
        *nb_item = current_item;
        *first_shown = 0;
        *last_shown = current_nbitems;
    }
}

bool skinlist_get_item(struct screen *display, struct gui_synclist *list, int x, int y, int *item)
{
    const int screen = display->screen_type;
    if (!skinlist_is_configured(screen, list))
        return false;

    int row = y / listcfg[screen]->height;
    int column = x / listcfg[screen]->width;
    struct viewport *parent = (list->parent[screen]);
    int cols = (parent->width / listcfg[screen]->width);
    *item = row * cols+ column;
    return true;
}

bool skinlist_draw(struct screen *display, struct gui_synclist *list)
{
    int cur_line, display_lines;
    const int screen = display->screen_type;
    struct viewport *parent = (list->parent[screen]);
    char* label = NULL;
    const int list_start_item = list->start_item[screen];
    struct gui_wps wps;
    if (!skinlist_is_configured(screen, list))
        return false;

    current_list = list;
    wps.display = display;
    wps.data = listcfg[screen]->data;
    display_lines = skinlist_get_line_count(screen, list);
    label = (char *)SKINOFFSETTOPTR(get_skin_buffer(wps.data), listcfg[screen]->label);
    if (!label)
        return false;

    display->set_viewport(parent);
    display->clear_viewport();
    /* RPod: the selected row scrolls inside the shared selected_item_vp; stop whatever line the
     * previous list left scrolling there, or it keeps drawing over this one */
    display->scroll_stop_viewport(&listcfg[screen]->selected_item_vp.vp);
    current_item = list->selected_item;
    current_nbitems = list->nb_items;
    needs_scrollbar[screen] = list->nb_items > display_lines;

    for (cur_line = 0; cur_line < display_lines; cur_line++)
    {
        struct skin_element* viewport;
        struct skin_viewport* skin_viewport = NULL;
        if (list_start_item+cur_line+1 > list->nb_items)
            break;
        current_drawing_line = list_start_item+cur_line;
        is_selected = list_start_item+cur_line == list->selected_item;
        bool first_selected_drawn = false;

        for (viewport = SKINOFFSETTOPTR(get_skin_buffer(wps.data), listcfg[screen]->data->tree);
             viewport;
             viewport = SKINOFFSETTOPTR(get_skin_buffer(wps.data), viewport->next))
        {
            int original_x, original_y;
            skin_viewport = SKINOFFSETTOPTR(get_skin_buffer(wps.data), viewport->data);
            char *viewport_label = NULL;
            if (skin_viewport)
                viewport_label = SKINOFFSETTOPTR(get_skin_buffer(wps.data), skin_viewport->label);
            if (viewport->children == 0 || !viewport_label ||
                (skin_viewport->label && strcmp(label, viewport_label))
                )
                continue;
            bool extra = false;
            if (is_selected)
            {
                /* RPod: only the row's first viewport (the text) lives in selected_item_vp, where it
                 * may scroll; the others (icon, edges) get their own copy, else each one overwrites
                 * the text's viewport and its scroll redraws in the wrong place and colours */
                static struct skin_viewport selected_extra[NB_SCREENS];
                extra = first_selected_drawn;
                first_selected_drawn = true;
                struct skin_viewport *dst = extra ? &selected_extra[screen] : &listcfg[screen]->selected_item_vp;
                memcpy(dst, skin_viewport, sizeof(struct skin_viewport));
                skin_viewport = dst;
            }
            original_x = skin_viewport->vp.x;
            original_y = skin_viewport->vp.y;
            int original_w = skin_viewport->vp.width;
            if (listcfg[screen]->tile)
            {
                int cols = (parent->width / listcfg[screen]->width);
                current_column = (cur_line)%cols;
                current_row = (cur_line)/cols;

                skin_viewport->vp.x = parent->x + listcfg[screen]->width*current_column + original_x;
                skin_viewport->vp.y = parent->y + listcfg[screen]->height*current_row + original_y;
            }
            else
            {
                current_column = 1;
                current_row = cur_line;
                skin_viewport->vp.x = parent->x + original_x;
                skin_viewport->vp.y = parent->y + original_y +
                                   (listcfg[screen]->height*cur_line);
            }
            /* RPod: with a scrollbar, rows (and the gold selector) stop 4px before it so it stands alone */
            if (needs_scrollbar[screen] && list->scrollbar == SCROLLBAR_RIGHT)
            {
                int limit = parent->x + parent->width - SCROLLBAR_WIDTH - 6 - 4;
                if (skin_viewport->vp.x + skin_viewport->vp.width > limit)
                    skin_viewport->vp.width = MAX(0, limit - skin_viewport->vp.x);
            }
            display->set_viewport(&skin_viewport->vp);
            /* Set images to not to be displayed */
            struct skin_token_list *imglist = SKINOFFSETTOPTR(get_skin_buffer(wps.data), wps.data->images);
            while (imglist)
            {
                struct wps_token *token = SKINOFFSETTOPTR(get_skin_buffer(wps.data), imglist->token);
                struct gui_img *img = NULL;
                if (token)
                    img = SKINOFFSETTOPTR(get_skin_buffer(wps.data), token->value.data);
                if (img)
                    img->display = -1;
                imglist = SKINOFFSETTOPTR(get_skin_buffer(wps.data), imglist->next);
            }
            struct skin_element** children = SKINOFFSETTOPTR(get_skin_buffer(wps.data), viewport->children);
            if (children && *children)
            {
                skinlist_force_lines = true;
                skinlist_letterpress = is_selected && !extra;
                skin_render_viewport(SKINOFFSETTOPTR(get_skin_buffer(wps.data), (intptr_t)children[0]),
                                     &wps, skin_viewport, SKIN_REFRESH_ALL);
                skinlist_force_lines = false;
                skinlist_letterpress = false;
            }
            wps_display_images(&wps, &skin_viewport->vp);
            /* force disableing scroll because it breaks later */
            if (!is_selected || extra)
            {
                display->scroll_stop_viewport(&skin_viewport->vp);
                skin_viewport->vp.x = original_x;
                skin_viewport->vp.y = original_y;
                skin_viewport->vp.width = original_w;
            }
        }
    }
    current_column = -1;
    current_row = -1;
    /* RPod: draw the built-in list scrollbar (scrollbar / scrollbar width settings) on skinned
     * lists too, only when the list is longer than the screen, like plain lists do */
    if (list->scrollbar != SCROLLBAR_OFF && list->nb_items > display_lines)
    {
        struct viewport vp = *parent;
        vp.width = SCROLLBAR_WIDTH;
        vp.height = listcfg[screen]->height * display_lines;
        if (list->scrollbar == SCROLLBAR_RIGHT)
            vp.x = parent->x + parent->width - SCROLLBAR_WIDTH - 6; /* 6px margin (rows stop 4px before it, above): right edge on x=313 with the battery, mode icons and bar */
        display->set_viewport(&vp);
#if LCD_DEPTH >= 16
        {   /* gilt: dark rim, lit top/left, gold bottom/right, dark inside; gold thumb with a lit edge */
            const unsigned low = LCD_RGBPACK(59, 42, 20), gold = LCD_RGBPACK(201, 162, 79),
                           hi = LCD_RGBPACK(240, 212, 136), inside = LCD_RGBPACK(26, 18, 11);
            const unsigned old_fg = display->get_foreground();
            int w = vp.width, h = vp.height;
            display->set_drawmode(DRMODE_SOLID);
            display->set_foreground(low);    display->drawrect(0, 0, w, h);
            display->set_foreground(gold);   display->drawrect(1, 1, w - 2, h - 2);
            display->set_foreground(hi);     display->hline(1, w - 2, 1); display->vline(1, 1, h - 2);
            display->set_foreground(inside); display->fillrect(2, 2, w - 4, h - 4);
            int track = h - 6;
            int top = 3 + track * list_start_item / list->nb_items;
            int size = track * display_lines / list->nb_items;
            if (size < 4) size = 4;
            if (top + size > 3 + track) top = 3 + track - size;
            display->set_foreground(gold);   display->fillrect(3, top, w - 6, size);
            display->set_foreground(hi);     display->hline(3, w - 4, top); display->vline(3, top, top + size - 1);
            display->set_foreground(old_fg);
        }
#else
        gui_scrollbar_draw(display, (list->scrollbar == SCROLLBAR_LEFT) ? 0 : 1, 0,
                           SCROLLBAR_WIDTH - 1, vp.height, list->nb_items,
                           list_start_item, list_start_item + display_lines, VERTICAL);
#endif
    }
    display->set_viewport(parent);
    display->update_viewport();
    current_drawing_line = list->selected_item;
    return true;
}
