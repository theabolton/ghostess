/* ghostess - A GUI host for DSSI plugins.
 *
 * Copyright (C) 2005, 2006, 2008, 2010, 2012 Sean Bolton.
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
#include <inttypes.h>
#include <errno.h>

#include <glib.h>
#include <gtk/gtk.h>
#include "ghostess.h"
#include "eyecandy.h"
#include "gui_callbacks.h"
#include "gui_interface.h"
#ifdef MIDI_ALSA
#include "midi.h"
#endif

static void (*file_selection_handler)(GtkWidget *widget, gpointer data);

static gchar *last_save_filename      = NULL;
static gchar *last_patchlist_filename = NULL;

static d3h_instance_t *ui_context_menu_instance;

void
file_selection_set_path(gchar *filename)
{
    if (filename) {
        gtk_file_selection_set_filename(GTK_FILE_SELECTION(file_selection),
                                        filename);
    } else if (project_directory && strlen(project_directory)) {
        if (project_directory[strlen(project_directory) - 1] != '/') {
            char *buffer = g_strdup_printf("%s/", project_directory);
            gtk_file_selection_set_filename(GTK_FILE_SELECTION(file_selection),
                                            buffer);
            g_free(buffer);
        } else {
            gtk_file_selection_set_filename(GTK_FILE_SELECTION(file_selection),
                                            project_directory);
        }
    }
}

void
on_menu_save_activate                  (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
    file_selection_set_path(last_save_filename);
    gtk_window_set_title(GTK_WINDOW(file_selection), "ghostess - Save Configuration");
    file_selection_handler = on_save_file_ok;
    gtk_widget_show(file_selection);
}


void
on_menu_patchlist_activate             (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
    file_selection_set_path(last_patchlist_filename);
    gtk_window_set_title(GTK_WINDOW(file_selection), "ghostess - Export Patchlist for Freewheeling");
    file_selection_handler = on_patchlist_file_ok;
    gtk_widget_show(file_selection);
}


void
on_menu_quit_activate                  (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
    gtk_main_quit();
    host_exiting = 1;
}


void
on_menu_about_activate                 (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
    char buf[1024];
    int maxlen = 1024,
        len;
#ifdef HAVE_CONFIG_H
    snprintf(buf, maxlen, "%s " VERSION "\n"
#else
    snprintf(buf, maxlen, "%s\n"
#endif
                       "Copyright (C) 2012 by Sean Bolton and others.\n\n"
                       "This is sloppy, hurried HACKWARE -- please do not "
                       "consider this exemplary of the authors' skills or "
                       "preferences, nor of good DSSI or general programming "
                       "practices.  (In particular, I don't want anyone "
                       "attributing my mess to Chris or Steve ;-)\n\n"
                       "%s comes with ABSOLUTELY NO WARRANTY. "
                       "It is free software, and you are welcome to "
                       "redistribute it under certain conditions; see "
                       "the file COPYING for details.\n\n",
             host_name_default, host_name_default);
    len = strlen(buf);
    snprintf(buf + len, maxlen - len, "Host OSC URL: %s\n", host_osc_url);
#ifdef MIDI_ALSA
    len = strlen(buf);
    snprintf(buf + len, maxlen - len, "ALSA MIDI input port: %d:%d\n",
             alsa_client_id, alsa_port_id);
#endif
#ifdef MIDI_JACK
    len = strlen(buf);
    snprintf(buf + len, maxlen - len, "Using >experimental< JACK MIDI input\n");
#endif
#ifdef MIDI_COREMIDI
    len = strlen(buf);
    snprintf(buf + len, maxlen - len, "Using >experimental< CoreMIDI input\n");
#endif
    gtk_label_set_text (GTK_LABEL (about_label), buf);
    gtk_widget_show(about_window);
}

gint
on_delete_event_wrapper( GtkWidget *widget, GdkEvent *event, gpointer data )
{
    void (*handler)(GtkWidget *, gpointer) = (void (*)(GtkWidget *, gpointer))data;

    /* call our 'dismiss' or 'cancel' callback (which must not need the user data) */
    (*handler)(widget, NULL);

    /* tell GTK+ to NOT emit 'destroy' */
    return TRUE;
}

void
on_file_selection_ok( GtkWidget *widget, gpointer data )
{
    gtk_widget_hide(file_selection);
    (*file_selection_handler)(widget, data);
}

void
on_file_selection_cancel( GtkWidget *widget, gpointer data )
{
    ghss_debug(GDB_GUI, ": on_save_file_cancel called");
    gtk_widget_hide(file_selection);
}

void
on_save_file_ok( GtkWidget *widget, gpointer data )
{
    if (last_save_filename) free(last_save_filename);
    last_save_filename = (gchar *)g_strdup(gtk_file_selection_get_filename(
                             GTK_FILE_SELECTION(file_selection)));

    ghss_debug(GDB_GUI, " on_save_file_ok: file '%s' selected",
               last_save_filename);

    if (!write_configuration(last_save_filename, NULL)) {
        display_notice("Save Configuration failed:", strerror(errno));
    } else {
        display_notice("Configuration Saved.", "");
    }
}

void
on_patchlist_file_ok( GtkWidget *widget, gpointer data )
{
    if (last_patchlist_filename) free(last_patchlist_filename);
    last_patchlist_filename = (gchar *)g_strdup(gtk_file_selection_get_filename(
                                  GTK_FILE_SELECTION(file_selection)));
    /* -FIX- add '.xml' to filename if it's not already there? */

    ghss_debug(GDB_GUI, " on_patchlist_file_ok: file '%s' selected",
               last_patchlist_filename);

    if (!write_patchlist(last_patchlist_filename)) {
        display_notice("Patchlist export failed:", strerror(errno));
    } else {
        display_notice("Patchlist exported.", "");
    }
}

void
on_about_dismiss( GtkWidget *widget, gpointer data )
{
    gtk_widget_hide(about_window);
}

void
on_strip_ui_button_toggled(GtkWidget *widget, gpointer data)
{
    d3h_instance_t *instance = ((plugin_strip *)data)->instance;
    int state = GTK_TOGGLE_BUTTON (widget)->active;

    ghss_debug(GDB_GUI, " on_strip_ui_button_toggled: instance %d button changed to %s",
               instance->number, (state ? "on" : "off"));

    if (instance->ui_osc_address) {
        if (state) {
            lo_send(instance->ui_osc_address, instance->ui_osc_show_path, "");
            instance->ui_visible = 1;
        } else {
            lo_send(instance->ui_osc_address, instance->ui_osc_hide_path, "");
            instance->ui_visible = 0;
        }
    } else if (state && !instance->ui_running) {
        instance->ui_visible = 1;
        start_ui(instance);
    }
}

gboolean
on_strip_ui_button_event(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    d3h_instance_t *instance = ((plugin_strip *)data)->instance;

    if (event->button != 3)  /* we're only interested in third button clicks */
        return FALSE;  /* propagate to other handlers, if any */

    ghss_debug(GDB_GUI, " on_strip_ui_button_event: third button click on instance %d",
               instance->number);

    /* set menu item sensitivity according to assumed UI state */
    gtk_widget_set_sensitive (ui_context_menu_launch, !instance->ui_running);
    gtk_widget_set_sensitive (ui_context_menu_show, instance->ui_running && !instance->ui_visible);
    gtk_widget_set_sensitive (ui_context_menu_hide, instance->ui_running && instance->ui_visible);
    gtk_widget_set_sensitive (ui_context_menu_exit, instance->ui_running);

    ui_context_menu_instance = instance;

    gtk_menu_popup (GTK_MENU(ui_context_menu), NULL, NULL, NULL, NULL,
                    event->button, event->time);

    return TRUE;
}

void
update_ui_button_internal(d3h_instance_t *instance, int value)
{
    /* -FIX- should be g_signal_handlers_block_by_func if we're GTK+ 2.x only */
    gtk_signal_handler_block_by_func(GTK_OBJECT(instance->strip->ui_button),
                                     GTK_SIGNAL_FUNC(on_strip_ui_button_toggled),
                                     (gpointer)instance->strip);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(instance->strip->ui_button),
                                 value);
    gtk_signal_handler_unblock_by_func(GTK_OBJECT(instance->strip->ui_button),
                                       GTK_SIGNAL_FUNC(on_strip_ui_button_toggled),
                                       (gpointer)instance->strip);
}

void
on_ui_context_menu_activate(GtkWidget *widget, gpointer data)
{
    int mode = (intptr_t)data;
    d3h_instance_t *instance = ui_context_menu_instance;

    ghss_debug(GDB_GUI, " on_ui_context_menu_activate: menu mode %d selected", mode);

    switch (mode) {
      default:
        break;

      case 0: /* launch */
        if (instance->ui_osc_address == NULL) {
            instance->ui_visible = 1;
            start_ui(instance);
            update_ui_button_internal(instance, TRUE);
        }
        break;

      case 1: /* show */
        if (instance->ui_osc_address) {
            lo_send(instance->ui_osc_address, instance->ui_osc_show_path, "");
            instance->ui_visible = 1;
            update_ui_button_internal(instance, TRUE);
        }
        break;

      case 2: /* hide */
        if (instance->ui_osc_address) {
            lo_send(instance->ui_osc_address, instance->ui_osc_hide_path, "");
            instance->ui_visible = 0;
            update_ui_button_internal(instance, FALSE);
        }
        break;

      case 3: /* exit */
        if (instance->ui_osc_address) {
            lo_send(instance->ui_osc_address, instance->ui_osc_quit_path, "");
            ui_osc_free(instance);
        }

        instance->ui_running = 0;
        instance->ui_visible = 0;
        update_ui_button_internal(instance, FALSE);

        break;
    }
}

void
display_notice(char *message1, char *message2)
{
    gtk_label_set_text (GTK_LABEL (notice_label_1), message1);
    gtk_label_set_text (GTK_LABEL (notice_label_2), message2);
    gtk_widget_show(notice_window);
}

void
on_notice_dismiss( GtkWidget *widget, gpointer data )
{
    gtk_widget_hide(notice_window);
}

void
update_from_exiting(d3h_instance_t *instance)
{
    update_ui_button_internal(instance, FALSE);
}

void
update_eyecandy(d3h_instance_t *instance)
{
    int state;

    state = (main_timeout_tick - instance->midi_activity_tick <= 1);
    if (instance->strip->previous_midi_state != state) {
        blinky_set_state(BLINKY(instance->strip->midi_status), state);
        instance->strip->previous_midi_state = state;
    }
}

