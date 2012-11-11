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

#define _BSD_SOURCE    1
#define _SVID_SOURCE   1
#define _ISOC99_SOURCE 1

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

#include "ghostess.h"
#include "eyecandy.h"
#include "gui_callbacks.h"
#include "gui_interface.h"

GtkWidget *main_window;
GtkWidget *plugin_hbox;

GtkWidget *ui_context_menu;
GtkWidget *ui_context_menu_launch;
GtkWidget *ui_context_menu_show;
GtkWidget *ui_context_menu_hide;
GtkWidget *ui_context_menu_exit;

GtkWidget *file_selection;

GtkWidget *about_window;
GtkWidget *about_label;

GtkWidget *notice_window;
GtkWidget *notice_label_1;
GtkWidget *notice_label_2;

void
create_main_window (const char *tag, int instance_count)
{
  GtkWidget *vbox1;
  GtkWidget *menubar1;
  GtkWidget *file1;
  GtkWidget *file1_menu;
  GtkWidget *menu_save;
  GtkWidget *menu_patchlist;
  GtkWidget *separator1;
  GtkWidget *menu_quit;
  GtkWidget *help1;
  GtkWidget *help1_menu;
  GtkWidget *menu_about;
  GtkWidget *scrolledwindow1;
  GtkWidget *viewport1;
  GtkAccelGroup *accel_group;
  GdkPixbuf *icon;

  if ((icon = gtk_icon_theme_load_icon(gtk_icon_theme_get_default(),
                                       "ghostess", 32, 0, NULL)) != NULL) {
      gtk_window_set_default_icon(icon);
      g_object_unref(icon);
  }

  accel_group = gtk_accel_group_new ();

  main_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_object_set_data (GTK_OBJECT (main_window), "main_window", main_window);
  gtk_window_set_title (GTK_WINDOW (main_window), tag);
  /* gtk_widget_realize(main_window);  // window must be realized for create_logo_pixmap() */

  vbox1 = gtk_vbox_new (FALSE, 0);
  gtk_widget_ref (vbox1);
  gtk_object_set_data_full (GTK_OBJECT (main_window), "vbox1", vbox1,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (vbox1);
  gtk_container_add (GTK_CONTAINER (main_window), vbox1);

  menubar1 = gtk_menu_bar_new ();
  gtk_widget_ref (menubar1);
  gtk_object_set_data_full (GTK_OBJECT (main_window), "menubar1", menubar1,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (menubar1);
  gtk_box_pack_start (GTK_BOX (vbox1), menubar1, FALSE, FALSE, 0);

  file1 = gtk_menu_item_new_with_label ("File");
  gtk_widget_ref (file1);
  gtk_object_set_data_full (GTK_OBJECT (main_window), "file1", file1,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (file1);
  gtk_container_add (GTK_CONTAINER (menubar1), file1);

  file1_menu = gtk_menu_new ();
  gtk_widget_ref (file1_menu);
  gtk_object_set_data_full (GTK_OBJECT (main_window), "file1_menu", file1_menu,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (file1), file1_menu);

  menu_save = gtk_menu_item_new_with_label ("Save Configuration...");
  gtk_widget_ref (menu_save);
  gtk_object_set_data_full (GTK_OBJECT (main_window), "menu_save", menu_save,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (menu_save);
  gtk_container_add (GTK_CONTAINER (file1_menu), menu_save);
  gtk_widget_add_accelerator (menu_save, "activate", accel_group,
                              GDK_S, GDK_CONTROL_MASK,
                              GTK_ACCEL_VISIBLE);

  menu_patchlist = gtk_menu_item_new_with_label ("Patchlist Export for Freewheeling...");
  gtk_widget_ref (menu_patchlist);
  gtk_object_set_data_full (GTK_OBJECT (main_window), "menu_patchlist", menu_patchlist,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (menu_patchlist);
  gtk_container_add (GTK_CONTAINER (file1_menu), menu_patchlist);
  gtk_widget_add_accelerator (menu_patchlist, "activate", accel_group,
                              GDK_P, GDK_CONTROL_MASK,
                              GTK_ACCEL_VISIBLE);

  separator1 = gtk_menu_item_new ();
  gtk_widget_ref (separator1);
  gtk_object_set_data_full (GTK_OBJECT (main_window), "separator1", separator1,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (separator1);
  gtk_container_add (GTK_CONTAINER (file1_menu), separator1);
  gtk_widget_set_sensitive (separator1, FALSE);

  menu_quit = gtk_menu_item_new_with_label ("Quit");
  gtk_widget_ref (menu_quit);
  gtk_object_set_data_full (GTK_OBJECT (main_window), "menu_quit", menu_quit,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (menu_quit);
  gtk_container_add (GTK_CONTAINER (file1_menu), menu_quit);
  gtk_widget_add_accelerator (menu_quit, "activate", accel_group,
                              GDK_Q, GDK_CONTROL_MASK,
                              GTK_ACCEL_VISIBLE);

  help1 = gtk_menu_item_new_with_label ("About");
  gtk_widget_ref (help1);
  gtk_object_set_data_full (GTK_OBJECT (main_window), "help1", help1,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (help1);
  gtk_container_add (GTK_CONTAINER (menubar1), help1);
  gtk_menu_item_right_justify (GTK_MENU_ITEM (help1));

  help1_menu = gtk_menu_new ();
  gtk_widget_ref (help1_menu);
  gtk_object_set_data_full (GTK_OBJECT (main_window), "help1_menu", help1_menu,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (help1), help1_menu);

  menu_about = gtk_menu_item_new_with_label ("About ghostess");
  gtk_widget_ref (menu_about);
  gtk_object_set_data_full (GTK_OBJECT (main_window), "menu_about", menu_about,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (menu_about);
  gtk_container_add (GTK_CONTAINER (help1_menu), menu_about);

    plugin_hbox = gtk_hbox_new (FALSE, 0);
    gtk_widget_ref (plugin_hbox);
    gtk_object_set_data_full (GTK_OBJECT (main_window), "plugin_hbox", plugin_hbox,
                              (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (plugin_hbox);

    if (instance_count > 5) {
        scrolledwindow1 = gtk_scrolled_window_new (NULL, NULL);
        gtk_widget_ref (scrolledwindow1);
        gtk_object_set_data_full (GTK_OBJECT (main_window), "scrolledwindow1", scrolledwindow1,
                                  (GtkDestroyNotify) gtk_widget_unref);
        gtk_widget_show (scrolledwindow1);
        gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolledwindow1), GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
        gtk_range_set_update_policy (GTK_RANGE (GTK_SCROLLED_WINDOW (scrolledwindow1)->vscrollbar), GTK_POLICY_AUTOMATIC);
        gtk_box_pack_start (GTK_BOX (vbox1), scrolledwindow1, TRUE, TRUE, 0);

        gtk_widget_set_size_request(GTK_WIDGET(scrolledwindow1), 400, -1);

        viewport1 = gtk_viewport_new (NULL, NULL);
        gtk_widget_ref (viewport1);
        gtk_object_set_data_full (GTK_OBJECT (main_window), "viewport1", viewport1,
                                  (GtkDestroyNotify) gtk_widget_unref);
        gtk_widget_show (viewport1);
        gtk_container_add (GTK_CONTAINER (scrolledwindow1), viewport1);

        gtk_container_add (GTK_CONTAINER (viewport1), plugin_hbox);
    } else {
        gtk_box_pack_start (GTK_BOX (vbox1), plugin_hbox, TRUE, TRUE, 0);
    }

    gtk_signal_connect(GTK_OBJECT(main_window), "destroy",
                       GTK_SIGNAL_FUNC(gtk_main_quit), NULL);
    gtk_signal_connect (GTK_OBJECT (main_window), "delete_event",
                        (GtkSignalFunc)on_delete_event_wrapper,
                        (gpointer)on_menu_quit_activate);

  gtk_signal_connect (GTK_OBJECT (menu_save), "activate",
                      GTK_SIGNAL_FUNC (on_menu_save_activate),
                      NULL);
  gtk_signal_connect (GTK_OBJECT (menu_patchlist), "activate",
                      GTK_SIGNAL_FUNC (on_menu_patchlist_activate),
                      NULL);
  gtk_signal_connect (GTK_OBJECT (menu_quit), "activate",
                      GTK_SIGNAL_FUNC (on_menu_quit_activate),
                      NULL);
  gtk_signal_connect (GTK_OBJECT (menu_about), "activate",
                      GTK_SIGNAL_FUNC (on_menu_about_activate),
                      NULL);

    gtk_window_add_accel_group (GTK_WINDOW (main_window), accel_group);
}

void
create_ui_context_menu(GtkWidget *parent_window)
{
    ui_context_menu = gtk_menu_new();
    gtk_widget_ref (ui_context_menu);
    gtk_object_set_data_full (GTK_OBJECT (parent_window), "ui_context_menu", ui_context_menu,
                              (GtkDestroyNotify) gtk_widget_unref);
    ui_context_menu_launch = gtk_menu_item_new_with_label ("Launch UI");
    gtk_widget_show (ui_context_menu_launch);
    gtk_menu_append (GTK_MENU (ui_context_menu), ui_context_menu_launch);
    ui_context_menu_show = gtk_menu_item_new_with_label ("Show UI");
    gtk_widget_show (ui_context_menu_show);
    gtk_menu_append (GTK_MENU (ui_context_menu), ui_context_menu_show);
    ui_context_menu_hide = gtk_menu_item_new_with_label ("Hide UI");
    gtk_widget_show (ui_context_menu_hide);
    gtk_menu_append (GTK_MENU (ui_context_menu), ui_context_menu_hide);
    ui_context_menu_exit = gtk_menu_item_new_with_label ("Exit UI");
    gtk_widget_show (ui_context_menu_exit);
    gtk_menu_append (GTK_MENU (ui_context_menu), ui_context_menu_exit);

    gtk_signal_connect (GTK_OBJECT (ui_context_menu_launch), "activate",
                        GTK_SIGNAL_FUNC (on_ui_context_menu_activate),
                        (gpointer)0);
    gtk_signal_connect (GTK_OBJECT (ui_context_menu_show), "activate",
                        GTK_SIGNAL_FUNC (on_ui_context_menu_activate),
                        (gpointer)1);
    gtk_signal_connect (GTK_OBJECT (ui_context_menu_hide), "activate",
                        GTK_SIGNAL_FUNC (on_ui_context_menu_activate),
                        (gpointer)2);
    gtk_signal_connect (GTK_OBJECT (ui_context_menu_exit), "activate",
                        GTK_SIGNAL_FUNC (on_ui_context_menu_activate),
                        (gpointer)3);
}

void
create_file_selection(const char *tag)
{
  char      *title;
  GtkWidget *ok_button;
  GtkWidget *cancel_button;

    title = (char *)malloc(strlen(tag) + 22);
    sprintf(title, "%s - File Selection", tag);
    file_selection = gtk_file_selection_new (title);
    free(title);
  gtk_object_set_data (GTK_OBJECT (file_selection), "file_selection", file_selection);
  gtk_container_set_border_width (GTK_CONTAINER (file_selection), 10);
  GTK_WINDOW (file_selection)->type = GTK_WINDOW_TOPLEVEL;

  ok_button = GTK_FILE_SELECTION (file_selection)->ok_button;
  gtk_object_set_data (GTK_OBJECT (file_selection), "ok_button", ok_button);
  gtk_widget_show (ok_button);
  GTK_WIDGET_SET_FLAGS (ok_button, GTK_CAN_DEFAULT);

  cancel_button = GTK_FILE_SELECTION (file_selection)->cancel_button;
  gtk_object_set_data (GTK_OBJECT (file_selection), "cancel_button", cancel_button);
  gtk_widget_show (cancel_button);
  GTK_WIDGET_SET_FLAGS (cancel_button, GTK_CAN_DEFAULT);

    gtk_signal_connect (GTK_OBJECT (file_selection), "destroy",
                        GTK_SIGNAL_FUNC(gtk_main_quit), NULL);
    gtk_signal_connect (GTK_OBJECT (file_selection), "delete_event",
                        (GtkSignalFunc)on_delete_event_wrapper,
                        (gpointer)on_file_selection_cancel);
    gtk_signal_connect (GTK_OBJECT (GTK_FILE_SELECTION (file_selection)->ok_button),
                        "clicked", (GtkSignalFunc)on_file_selection_ok,
                        NULL);
    gtk_signal_connect (GTK_OBJECT (GTK_FILE_SELECTION (file_selection)->cancel_button),
                        "clicked", (GtkSignalFunc)on_file_selection_cancel,
                        NULL);
}

void
create_about_window(const char *tag)
{
    GtkWidget *vbox2;
    GtkWidget *closeabout;

    about_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_object_set_data (GTK_OBJECT (about_window), "about_window", about_window);
    gtk_window_set_title (GTK_WINDOW (about_window), "About ghostess");

    vbox2 = gtk_vbox_new (FALSE, 0);
    gtk_widget_ref (vbox2);
    gtk_object_set_data_full (GTK_OBJECT (about_window), "vbox2", vbox2,
                              (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (vbox2);
    gtk_container_add (GTK_CONTAINER (about_window), vbox2);

    about_label = gtk_label_new ("Some message\ngoes here");
    gtk_widget_ref (about_label);
    gtk_object_set_data_full (GTK_OBJECT (about_window), "about_label", about_label,
                              (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (about_label);
    gtk_box_pack_start (GTK_BOX (vbox2), about_label, FALSE, FALSE, 0);
    gtk_label_set_line_wrap (GTK_LABEL (about_label), TRUE);
    gtk_label_set_justify (GTK_LABEL (about_label), GTK_JUSTIFY_CENTER);
    gtk_misc_set_padding (GTK_MISC (about_label), 5, 5);

    closeabout = gtk_button_new_with_label ("Dismiss");
    gtk_widget_ref (closeabout);
    gtk_object_set_data_full (GTK_OBJECT (about_window), "closeabout", closeabout,
                              (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (closeabout);
    gtk_box_pack_start (GTK_BOX (vbox2), closeabout, FALSE, FALSE, 0);

    gtk_signal_connect (GTK_OBJECT (about_window), "destroy",
                        GTK_SIGNAL_FUNC(gtk_main_quit), NULL);
    gtk_signal_connect (GTK_OBJECT (about_window), "delete_event",
                        GTK_SIGNAL_FUNC (on_delete_event_wrapper),
                        (gpointer)on_about_dismiss);
    gtk_signal_connect (GTK_OBJECT (closeabout), "clicked",
                        GTK_SIGNAL_FUNC (on_about_dismiss),
                        NULL);
}

void
create_notice_window(const char *tag)
{
  char      *title;
  GtkWidget *vbox3;
  GtkWidget *hbox1;
  GtkWidget *notice_dismiss;

    notice_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_object_set_data (GTK_OBJECT (notice_window), "notice_window", notice_window);
    title = (char *)malloc(strlen(tag) + 8);
    sprintf(title, "%s Notice", tag);
    gtk_window_set_title (GTK_WINDOW (notice_window), title);
    free(title);
    gtk_window_set_position (GTK_WINDOW (notice_window), GTK_WIN_POS_MOUSE);
    gtk_window_set_modal (GTK_WINDOW (notice_window), TRUE);

  vbox3 = gtk_vbox_new (FALSE, 0);
  gtk_widget_ref (vbox3);
  gtk_object_set_data_full (GTK_OBJECT (notice_window), "vbox3", vbox3,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (vbox3);
  gtk_container_add (GTK_CONTAINER (notice_window), vbox3);

  notice_label_1 = gtk_label_new ("Some message\ngoes here");
  gtk_widget_ref (notice_label_1);
  gtk_object_set_data_full (GTK_OBJECT (notice_window), "notice_label_1", notice_label_1,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (notice_label_1);
  gtk_box_pack_start (GTK_BOX (vbox3), notice_label_1, TRUE, TRUE, 0);
  gtk_label_set_line_wrap (GTK_LABEL (notice_label_1), TRUE);
  gtk_misc_set_padding (GTK_MISC (notice_label_1), 10, 5);

  notice_label_2 = gtk_label_new ("more text\ngoes here");
  gtk_widget_ref (notice_label_2);
  gtk_object_set_data_full (GTK_OBJECT (notice_window), "notice_label_2", notice_label_2,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (notice_label_2);
  gtk_box_pack_start (GTK_BOX (vbox3), notice_label_2, FALSE, FALSE, 0);
  gtk_label_set_line_wrap (GTK_LABEL (notice_label_2), TRUE);
  gtk_misc_set_padding (GTK_MISC (notice_label_2), 10, 5);

  hbox1 = gtk_hbox_new (FALSE, 0);
  gtk_widget_ref (hbox1);
  gtk_object_set_data_full (GTK_OBJECT (notice_window), "hbox1", hbox1,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (hbox1);
  gtk_box_pack_start (GTK_BOX (vbox3), hbox1, FALSE, FALSE, 0);

  notice_dismiss = gtk_button_new_with_label ("Dismiss");
  gtk_widget_ref (notice_dismiss);
  gtk_object_set_data_full (GTK_OBJECT (notice_window), "notice_dismiss", notice_dismiss,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (notice_dismiss);
  gtk_box_pack_start (GTK_BOX (hbox1), notice_dismiss, TRUE, FALSE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (notice_dismiss), 7);

    gtk_signal_connect (GTK_OBJECT (notice_window), "destroy",
                        GTK_SIGNAL_FUNC(gtk_main_quit), NULL);
    gtk_signal_connect (GTK_OBJECT (notice_window), "delete_event",
                        GTK_SIGNAL_FUNC (on_delete_event_wrapper),
                        (gpointer)on_notice_dismiss);
    gtk_signal_connect (GTK_OBJECT (notice_dismiss), "clicked",
                        GTK_SIGNAL_FUNC (on_notice_dismiss),
                        NULL);
}

plugin_strip *
create_plugin_strip(GtkWidget *parent_window, d3h_instance_t *instance)
{
    plugin_strip *ps = (plugin_strip *)calloc(1, sizeof(plugin_strip));
    GtkWidget *vbox1;
    char buf[12];
    GtkWidget *hbox1;
    GtkWidget *striplabel1;
    GtkWidget *striplabel2;
    GtkWidget *hbox2;
#if 0
    GtkWidget *config_button;
#endif
#if 0
    GtkWidget *hscale1;
    GtkWidget *vscale1;
#endif
    ps->instance = instance;

  ps->container = gtk_frame_new (NULL);
  gtk_widget_ref (ps->container);
  gtk_object_set_data_full (GTK_OBJECT (parent_window), "frame1", ps->container,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (ps->container);
  gtk_container_set_border_width (GTK_CONTAINER (ps->container), 2);
  gtk_frame_set_shadow_type (GTK_FRAME (ps->container), GTK_SHADOW_OUT);

  vbox1 = gtk_vbox_new (FALSE, 0);
  gtk_widget_ref (vbox1);
  gtk_object_set_data_full (GTK_OBJECT (parent_window), "vbox1", vbox1,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (vbox1);
  gtk_container_add (GTK_CONTAINER (ps->container), vbox1);

    hbox1 = gtk_hbox_new (FALSE, 0);
    gtk_widget_ref (hbox1);
    gtk_object_set_data_full (GTK_OBJECT (parent_window), "hbox1", hbox1,
                              (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (hbox1);
    gtk_box_pack_start (GTK_BOX (vbox1), hbox1, FALSE, FALSE, 0);

    ps->midi_status = blinky_new(0);
    gtk_widget_ref (ps->midi_status);
    gtk_object_set_data_full (GTK_OBJECT (parent_window), "midi_status", ps->midi_status,
                              (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (ps->midi_status);
    gtk_box_pack_start (GTK_BOX (hbox1), ps->midi_status, FALSE, FALSE, 1);

    ps->previous_midi_state = 0;

    snprintf(buf, 12, "Inst %d", instance->id);
    striplabel1 = gtk_label_new (buf);
    gtk_widget_ref (striplabel1);
    gtk_object_set_data_full (GTK_OBJECT (parent_window), "striplabel1", striplabel1,
                              (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (striplabel1);
    gtk_box_pack_start (GTK_BOX (hbox1), striplabel1, FALSE, FALSE, 1);
    /* gtk_misc_set_alignment (GTK_MISC (striplabel1), 0, 0.5);   -FIX- */
    /* gtk_misc_set_padding (GTK_MISC (striplabel1), 3, 0);   -FIX- */

    snprintf(buf, 12, "%s", instance->plugin->label);
    striplabel2 = gtk_label_new (buf);
    gtk_widget_ref (striplabel2);
    gtk_object_set_data_full (GTK_OBJECT (parent_window), "striplabel2", striplabel2,
                              (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (striplabel2);
    gtk_box_pack_start (GTK_BOX (vbox1), striplabel2, FALSE, FALSE, 0);
    gtk_misc_set_alignment (GTK_MISC (striplabel2), 0, 0.5);
    gtk_misc_set_padding (GTK_MISC (striplabel2), 3, 0);

  hbox2 = gtk_hbox_new (TRUE, 0);
  gtk_widget_ref (hbox2);
  gtk_object_set_data_full (GTK_OBJECT (parent_window), "hbox2", hbox2,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (hbox2);
  gtk_box_pack_start (GTK_BOX (vbox1), hbox2, FALSE, FALSE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (hbox2), 3);

#if 0
  config_button = gtk_button_new_with_label ("Cfg");
  gtk_widget_ref (config_button);
  gtk_object_set_data_full (GTK_OBJECT (parent_window), "config_button", config_button,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (config_button);
  gtk_box_pack_start (GTK_BOX (hbox2), config_button, FALSE, FALSE, 0);
#endif

  ps->ui_button = gtk_toggle_button_new_with_label ("UI");
  gtk_widget_ref (ps->ui_button);
  gtk_object_set_data_full (GTK_OBJECT (parent_window), "ui_button", ps->ui_button,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (ps->ui_button);
  gtk_box_pack_end (GTK_BOX (hbox2), ps->ui_button, FALSE, FALSE, 0);
    gtk_signal_connect (GTK_OBJECT (ps->ui_button), "toggled",
                        GTK_SIGNAL_FUNC (on_strip_ui_button_toggled),
                        (gpointer)ps);
    gtk_signal_connect (GTK_OBJECT (ps->ui_button), "button_press_event",
                        GTK_SIGNAL_FUNC (on_strip_ui_button_event),
                        (gpointer)ps);

#if 0
    ps->pan_adjustment = gtk_adjustment_new (48, 0, 100, 1, 10, 0);
  hscale1 = gtk_hscale_new (GTK_ADJUSTMENT (ps->pan_adjustment));
  gtk_widget_ref (hscale1);
  gtk_object_set_data_full (GTK_OBJECT (parent_window), "hscale1", hscale1,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (hscale1);
  gtk_box_pack_start (GTK_BOX (vbox1), hscale1, FALSE, FALSE, 0);
  gtk_scale_set_digits (GTK_SCALE (hscale1), 0);

    ps->level_adjustment = gtk_adjustment_new (-120, -120, 10, -1, -6, -1);
  vscale1 = gtk_vscale_new (GTK_ADJUSTMENT (ps->level_adjustment));
  gtk_widget_ref (vscale1);
  gtk_object_set_data_full (GTK_OBJECT (parent_window), "vscale1", vscale1,
                            (GtkDestroyNotify) gtk_widget_unref);
  gtk_widget_show (vscale1);
  gtk_box_pack_start (GTK_BOX (vbox1), vscale1, TRUE, TRUE, 0);
#endif

    return ps;
}

void
create_windows(const char *host_tag, int instance_count)
{
    char tag[64];

    /* build a nice identifier string for the window titles */
    if (strlen(host_tag) == 0) {
        strcpy(tag, host_name_default);
    } else if (strstr(host_tag, host_name_default)) {
        if (strlen(host_tag) > 63) {
            snprintf(tag, 64, "...%s", host_tag + strlen(host_tag) - 60); /* hope the unique info is at the end */
        } else {
            strcpy(tag, host_tag);
        }
    } else {
        if (strlen(host_name_default) + strlen(host_tag) > 62) {
            snprintf(tag, 64, "%s ...%s", host_name_default, host_tag + strlen(host_tag) + strlen(host_name_default) - 59);
        } else {
            snprintf(tag, 64, "%s %s", host_name_default, host_tag);
        }
    }

    create_main_window(tag, instance_count);
    create_ui_context_menu(main_window);
    create_file_selection(tag);
    create_about_window(tag);
    create_notice_window(tag);
}

