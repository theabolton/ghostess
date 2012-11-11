/* ghostess - A GUI host for DSSI plugins.
 *
 * Copyright (C) 2005, 2006, 2008 Sean Bolton.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#ifndef _GUI_INTERFACE_H
#define _GUI_INTERFACE_H

#include <gtk/gtk.h>

#include <ghostess.h>

struct _plugin_strip {
    d3h_instance_t *instance;
    GtkWidget      *container;
    GtkWidget      *midi_status;
    int             previous_midi_state;
    GtkWidget      *ui_button;
    GtkObject      *pan_adjustment;
    GtkObject      *level_adjustment;
};

extern GtkWidget *main_window;
extern GtkWidget *plugin_hbox;

extern GtkWidget *ui_context_menu;
extern GtkWidget *ui_context_menu_launch;
extern GtkWidget *ui_context_menu_show;
extern GtkWidget *ui_context_menu_hide;
extern GtkWidget *ui_context_menu_exit;

extern GtkWidget *file_selection;

extern GtkWidget *about_window;
extern GtkWidget *about_label;

extern GtkWidget *notice_window;
extern GtkWidget *notice_label_1;
extern GtkWidget *notice_label_2;

plugin_strip *create_plugin_strip(GtkWidget *parent_window,
                                  d3h_instance_t *instance);
void          create_windows(const char *host_tag, int instance_count);

#endif /* _GUI_INTERFACE_H */

