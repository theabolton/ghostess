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

#ifndef _GUI_CALLBACKS_H
#define _GUI_CALLBACKS_H

#include <gtk/gtk.h>

#include "ghostess.h"

void on_menu_open_activate(GtkMenuItem *menuitem, gpointer user_data);
void on_menu_save_activate(GtkMenuItem *menuitem, gpointer user_data);
void on_menu_patchlist_activate(GtkMenuItem *menuitem, gpointer user_data);
void on_menu_quit_activate(GtkMenuItem *menuitem, gpointer user_data);
void on_menu_about_activate(GtkMenuItem *menuitem, gpointer user_data);
gint on_delete_event_wrapper(GtkWidget *widget, GdkEvent *event,
                             gpointer data);
void on_file_selection_ok(GtkWidget *widget, gpointer data);
void on_file_selection_cancel(GtkWidget *widget, gpointer data);
void on_save_file_ok(GtkWidget *widget, gpointer data);
void on_patchlist_file_ok(GtkWidget *widget, gpointer data);
void on_about_dismiss(GtkWidget *widget, gpointer data);
void on_strip_ui_button_toggled(GtkWidget *widget, gpointer data);
gboolean on_strip_ui_button_event(GtkWidget *widget, GdkEventButton *event,
                                  gpointer data);
void on_ui_context_menu_activate(GtkWidget *widget, gpointer data);
void display_notice(char *message1, char *message2);
void on_notice_dismiss(GtkWidget *widget, gpointer data);
void update_from_exiting(d3h_instance_t *instance);
void update_eyecandy(d3h_instance_t *instance);

#endif  /* _GUI_CALLBACKS_H */

