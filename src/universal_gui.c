/* ghostess universal DSSI user interface
 *
 * Copyright (C) 2005, 2006, 2008, 2010 Sean Bolton and others.
 *
 * Portions of this file may have come from Chris Cannam and Steve
 * Harris's public domain DSSI example code.
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _BSD_SOURCE    1
#define _SVID_SOURCE   1
#define _ISOC99_SOURCE 1

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#include <sys/types.h>
#include <inttypes.h>
#include <unistd.h>
#include <math.h>

#include <gtk/gtk.h>
#include "gtkknob.h"
#include <lo/lo.h>
#include <dssi.h>

#include "universal_gui.h"

/* ==== global variables ==== */

char *     osc_host_url;
char *     osc_self_url;
lo_address osc_host_address;
char *     osc_configure_path;
char *     osc_control_path;
char *     osc_exiting_path;
char *     osc_hide_path;
char *     osc_midi_path;
char *     osc_program_path;
char *     osc_quit_path;
char *     osc_rate_path;
char *     osc_show_path;
char *     osc_update_path;

gint update_request_timeout_tag;
int  update_request_timeout_active = 0;
int  update_request_awaiting_program_change = 0;

GtkWidget *main_window;
GtkObject *bank_spin_adj;
GtkObject *program_spin_adj;
GtkWidget *test_note_button;
GtkWidget *test_note_toggle;

unsigned char test_note_noteon_key = 60;
int           test_note_noteoff_key = -1;
unsigned char test_note_velocity = 96;

int host_requested_quit = 0;

unsigned long sample_rate;

void *       plugin_dlh;
char *       plugin_path;
DSSI_Descriptor_Function
             plugin_descfn;
int          plugin_is_DSSI_so;
const DSSI_Descriptor *
             plugin_descriptor;
port_data_t *port_data;
           

/* forward: */
void schedule_update_request(void);
void update_from_program_select(int bank, int program);
void update_port_widget(int port, float value);
void update_for_sample_rate(void);

/* ==== OSC handling ==== */

static char *
osc_build_path(char *base_path, char *method)
{
    char buffer[256];
    char *full_path;

    snprintf(buffer, 256, "%s%s", base_path, method);
    if (!(full_path = strdup(buffer))) {
        GDB_MESSAGE(GDB_OSC, ": out of memory!\n");
        exit(1);
    }
    return full_path;
}

static void
osc_error(int num, const char *msg, const char *path)
{
    GDB_MESSAGE(GDB_OSC, " error: liblo server error %d in path \"%s\": %s\n",
            num, (path ? path : "(null)"), msg);
}

int
osc_debug_handler(const char *path, const char *types, lo_arg **argv,
                  int argc, lo_message msg, void *user_data)
{
    int i;

    GDB_MESSAGE(GDB_OSC, " warning: unhandled OSC message to <%s>:\n", path);

    for (i = 0; i < argc; ++i) {
        fprintf(stderr, "arg %d: type '%c': ", i, types[i]);
fflush(stderr);
        lo_arg_pp((lo_type)types[i], argv[i]);  /* -FIX- Ack, mixing stderr and stdout... */
        fprintf(stdout, "\n");
fflush(stdout);
    }

    return 1;  /* try any other handlers */
}

int
osc_action_handler(const char *path, const char *types, lo_arg **argv,
                  int argc, lo_message msg, void *user_data)
{
    if (!strcmp(user_data, "show")) {

        /* GDB_MESSAGE(GDB_OSC, " osc_action_handler: received 'show' message\n"); */
        if (!GTK_WIDGET_MAPPED(main_window))
            gtk_widget_show(main_window);
        else
            gdk_window_raise(main_window->window);

    } else if (!strcmp(user_data, "hide")) {

        /* GDB_MESSAGE(GDB_OSC, " osc_action_handler: received 'hide' message\n"); */
        gtk_widget_hide(main_window);

    } else if (!strcmp(user_data, "quit")) {

        /* GDB_MESSAGE(GDB_OSC, " osc_action_handler: received 'quit' message\n"); */
        host_requested_quit = 1;
        gtk_main_quit();

    } else if (!strcmp(user_data, "sample-rate")) {

        /* GDB_MESSAGE(GDB_OSC, " osc_action_handler: received 'sample-rate' message, rate = %d\n", argv[0]->i); */
        sample_rate = argv[0]->i;
        update_for_sample_rate();

    } else {

        return osc_debug_handler(path, types, argv, argc, msg, user_data);

    }
    return 0;
}

int
osc_configure_handler(const char *path, const char *types, lo_arg **argv,
                  int argc, lo_message msg, void *user_data)
{
    char *key, *value;

    if (argc < 2) {
        GDB_MESSAGE(GDB_OSC, " error: too few arguments to osc_configure_handler\n");
        return 1;
    }

    key   = &argv[0]->s;
    value = &argv[1]->s;

    GDB_MESSAGE(GDB_OSC, ": received configure with key '%s' and value '%s'\n", key, value);

    return 0;
}

int
osc_control_handler(const char *path, const char *types, lo_arg **argv,
                  int argc, lo_message msg, void *user_data)
{
    int port;
    float value;

    if (argc < 2) {
        GDB_MESSAGE(GDB_OSC, " error: too few arguments to osc_control_handler\n");
        return 1;
    }

    port = argv[0]->i;
    value = argv[1]->f;

    GDB_MESSAGE(GDB_OSC, " osc_control_handler: control %d now %f\n", port, value);

    update_port_widget(port, value);

    return 0;
}

int
osc_program_handler(const char *path, const char *types, lo_arg **argv,
                  int argc, lo_message msg, void *user_data)
{
    int bank, program;

    if (argc < 2) {
        GDB_MESSAGE(GDB_OSC, " error: too few arguments to osc_program_handler\n");
        return 1;
    }

    if (!plugin_descriptor->select_program) {
        GDB_MESSAGE(GDB_OSC, " osc_program_handler: received program change; plugin has no select_program()!\n");
        return 0;
    }

    bank = argv[0]->i;
    program = argv[1]->i;

    GDB_MESSAGE(GDB_OSC, " osc_program_handler: received program change, bank %d, program %d\n", bank, program);

    update_from_program_select(bank, program);

    if (update_request_awaiting_program_change) {
        update_request_awaiting_program_change = 0;
    } else {
        /* assume this isn't part of an update response, so request another
         * update to get current port values */
        schedule_update_request();
    }

    return 0;
}

void
osc_data_on_socket_callback(gpointer data, gint source,
                            GdkInputCondition condition)
{
    lo_server server = (lo_server)data;

    lo_server_recv_noblock(server, 0);
}

gint
update_request_timeout_callback(gpointer data)
{
    /* send our update request */
    lo_send(osc_host_address, osc_update_path, "s", osc_self_url);

    update_request_timeout_active = 0;

    return FALSE;  /* don't need to do this again */
}

void
schedule_update_request(void)
{
    if (!update_request_timeout_active) {
        update_request_timeout_tag = gtk_timeout_add(100,
                                                     update_request_timeout_callback,
                                                     NULL);
        update_request_timeout_active = 1;
        update_request_awaiting_program_change = 1;
    }
}

/* ==== GTK+ widget callbacks ==== */

gint
on_delete_event_wrapper( GtkWidget *widget, GdkEvent *event, gpointer data )
{
    void (*handler)(GtkWidget *, gpointer) = (void (*)(GtkWidget *, gpointer))data;

    /* call our 'close', 'dismiss' or 'cancel' callback (which must not need the user data) */
    (*handler)(widget, NULL);

    /* tell GTK+ to NOT emit 'destroy' */
    return TRUE;
}

void
on_program_spin_changed(GtkWidget *widget, gpointer data)
{
    unsigned long bank    = lrintf(GTK_ADJUSTMENT(bank_spin_adj)->value);
    unsigned long program = lrintf(GTK_ADJUSTMENT(program_spin_adj)->value);

    GDB_MESSAGE(GDB_GUI, " on_program_spin_changed: bank %lu program %lu selected\n", bank, program);

    lo_send(osc_host_address, osc_program_path, "ii", bank, program);

    /* select_program() may change the ports, so we need to request another update */
    schedule_update_request();
}

void
on_test_note_mode_toggled(GtkWidget *widget, gpointer data)
{
    int state = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));

    if (state) {
        gtk_widget_hide(test_note_button);
        gtk_widget_show(test_note_toggle);
    } else {
        gtk_widget_show(test_note_button);
        gtk_widget_hide(test_note_toggle);
    }
}

void
on_test_note_slider_change(GtkWidget *widget, gpointer data)
{
    unsigned char value = lrintf(GTK_ADJUSTMENT(widget)->value);

    if ((intptr_t)data == 0) {  /* key */

        test_note_noteon_key = value;
        GDB_MESSAGE(GDB_GUI, " on_test_note_slider_change: new test note key %d\n", test_note_noteon_key);

    } else {  /* velocity */

        test_note_velocity = value;
        GDB_MESSAGE(GDB_GUI, " on_test_note_slider_change: new test note velocity %d\n", test_note_velocity);

    }
}

static void
send_midi(unsigned char b0, unsigned char b1, unsigned char b2)
{
    unsigned char midi[4];

    midi[0] = 0;
    midi[1] = b0;
    midi[2] = b1;
    midi[3] = b2;
    lo_send(osc_host_address, osc_midi_path, "m", midi);
}

void
release_test_note(void)
{
    if (test_note_noteoff_key >= 0) {
        send_midi(0x80, test_note_noteoff_key, 0x40);
        test_note_noteoff_key = -1;
    }
}

void
on_test_note_button_press(GtkWidget *widget, gpointer data)
{
    /* here we just set the state of the test note toggle button, which may
     * cause a call to on_test_note_toggle_toggled() below, which will send
     * the actual MIDI message. */
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(test_note_toggle), (intptr_t)data != 0);
}

void
on_test_note_toggle_toggled(GtkWidget *widget, gpointer data)
{
    int state = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(test_note_toggle));

    GDB_MESSAGE(GDB_GUI, " on_test_note_toggle_toggled: state is now %s\n",
                state ? "active" : "inactive");

    if (state) {  /* button pressed */

        if (test_note_noteoff_key < 0) {
            send_midi(0x90, test_note_noteon_key, test_note_velocity);
            test_note_noteoff_key = test_note_noteon_key;
        }

    } else { /* button released */

        release_test_note();

    }
}

void
on_port_button_toggled( GtkWidget *widget, gpointer data )
{
    int port = (intptr_t)data;
    int state = GTK_TOGGLE_BUTTON (widget)->active;

    GDB_MESSAGE(GDB_GUI, " on_port_button_toggled: port %d changed to %s\n",
                port, (state ? "on" : "off"));

    lo_send(osc_host_address, osc_control_path, "if", port, (state ? 1.0f : 0.0f));
}

void
on_port_spin_changed(GtkWidget *widget, gpointer data)
{
    int port = (intptr_t)data;
    int value = lrintf(GTK_ADJUSTMENT(widget)->value);

    GDB_MESSAGE(GDB_GUI, " on_port_spin_changed: port %d changed to %d\n", port, value);

    lo_send(osc_host_address, osc_control_path, "if", port, (float)value);
}

void
on_port_knob_changed(GtkWidget *widget, gpointer data)
{
    int port = (intptr_t)data;
    float lval = GTK_ADJUSTMENT(widget)->value;
    float cval;

    if (port_data[port].type == PORT_CONTROL_INPUT_LOGARITHMIC) {
        float plb = port_data[port].LowerBound,
              pub = port_data[port].UpperBound;

        cval = (lval - plb) / (pub - plb);
        cval = (1.0f - cval) * logf(plb) + cval * logf(pub);
        cval = expf(cval);
    } else {
        cval = lval;
    }

    GDB_MESSAGE(GDB_GUI, " on_port_knob_changed: port %d changed to %f => %f\n", port, lval, cval);

    lo_send(osc_host_address, osc_control_path, "if", port, cval);
}

void
update_from_program_select(int bank, int program)
{
    /* -FIX- should be g_signal_handlers_block_by_func if we're GTK+ 2.x only */
    gtk_signal_handler_block_by_func(GTK_OBJECT(bank_spin_adj),
                                     GTK_SIGNAL_FUNC(on_program_spin_changed),
                                     (gpointer)0);
    gtk_signal_handler_block_by_func(GTK_OBJECT(program_spin_adj),
                                     GTK_SIGNAL_FUNC(on_program_spin_changed),
                                     (gpointer)1);

    GTK_ADJUSTMENT(bank_spin_adj)->value = (float)bank;
    GTK_ADJUSTMENT(program_spin_adj)->value = (float)program;
    gtk_signal_emit_by_name (GTK_OBJECT (bank_spin_adj), "value_changed");
    gtk_signal_emit_by_name (GTK_OBJECT (program_spin_adj), "value_changed");

    gtk_signal_handler_unblock_by_func(GTK_OBJECT(bank_spin_adj),
                                       GTK_SIGNAL_FUNC(on_program_spin_changed),
                                       (gpointer)0);
    gtk_signal_handler_unblock_by_func(GTK_OBJECT(program_spin_adj),
                                       GTK_SIGNAL_FUNC(on_program_spin_changed),
                                       (gpointer)1);
}

void
update_port_widget(int port, float value)
{
    GtkAdjustment *adj;
    GtkWidget *widget;
    float bounded_value, fval;
    int ival;
    float plb = port_data[port].LowerBound,
          pub = port_data[port].UpperBound;

    if (port < 0 || port > plugin_descriptor->LADSPA_Plugin->PortCount - 1 ||
        port_data[port].type == PORT_AUDIO_OUTPUT ||
        port_data[port].type == PORT_AUDIO_INPUT) {
        return;
    }

    bounded_value = value;
    if (port_data[port].bounded) {
        if (bounded_value < plb)
            bounded_value = plb;
        else if (bounded_value > pub)
            bounded_value = pub;
    }

    switch (port_data[port].type) {

      case PORT_CONTROL_INPUT_TOGGLED:
        ival = (value > 0.0001f ? 1 : 0);
        /* GDB_MESSAGE(GDB_GUI, " update_port_widget: change of '%s' to %f => %d\n", plugin_descriptor->LADSPA_Plugin->PortNames[port], value, ival); */
        widget = port_data[port].widget;
        gtk_signal_handler_block_by_func(GTK_OBJECT(widget),
                                         GTK_SIGNAL_FUNC(on_port_button_toggled),
                                         (gpointer)(intptr_t)port);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), ival);
        gtk_signal_handler_unblock_by_func(GTK_OBJECT(widget),
                                           GTK_SIGNAL_FUNC(on_port_button_toggled),
                                           (gpointer)(intptr_t)port);
        break;

      case PORT_CONTROL_INPUT_INTEGER:
        ival = lrintf(bounded_value);
        /* GDB_MESSAGE(GDB_GUI, " update_port_widget: change of '%s' to %f => %d\n", plugin_descriptor->LADSPA_Plugin->PortNames[port], value, ival); */
        adj = GTK_ADJUSTMENT(port_data[port].adjustment);
        adj->value = (float)ival;
        gtk_signal_handler_block_by_func(GTK_OBJECT(adj),
                                         GTK_SIGNAL_FUNC(on_port_spin_changed),
                                         (gpointer)(intptr_t)port);
        gtk_signal_emit_by_name (GTK_OBJECT (adj), "value_changed");
        gtk_signal_handler_unblock_by_func(GTK_OBJECT(adj),
                                           GTK_SIGNAL_FUNC(on_port_spin_changed),
                                           (gpointer)(intptr_t)port);
        break;

      case PORT_CONTROL_INPUT_LOGARITHMIC:
        fval = logf(plb);
        fval = (logf(bounded_value) - fval) / (logf(pub) - fval);
        fval = plb + fval * (pub - plb);
        /* GDB_MESSAGE(GDB_GUI, " update_port_widget: change of '%s' to %f => %f\n", plugin_descriptor->LADSPA_Plugin->PortNames[port], value, fval); */
        adj = GTK_ADJUSTMENT(port_data[port].adjustment);
        adj->value = fval;
        gtk_signal_handler_block_by_func(GTK_OBJECT(adj),
                                         GTK_SIGNAL_FUNC(on_port_knob_changed),
                                         (gpointer)(intptr_t)port);
        gtk_signal_emit_by_name (GTK_OBJECT (adj), "value_changed");
        gtk_signal_handler_unblock_by_func(GTK_OBJECT(adj),
                                           GTK_SIGNAL_FUNC(on_port_knob_changed),
                                           (gpointer)(intptr_t)port);
        break;

      case PORT_CONTROL_INPUT_LINEAR:
        /* GDB_MESSAGE(GDB_GUI, " update_port_widget: change of '%s' to %f => %f\n", plugin_descriptor->LADSPA_Plugin->PortNames[port], value, bounded_value); */
        adj = GTK_ADJUSTMENT(port_data[port].adjustment);
        adj->value = bounded_value;
        gtk_signal_handler_block_by_func(GTK_OBJECT(adj),
                                         GTK_SIGNAL_FUNC(on_port_knob_changed),
                                         (gpointer)(intptr_t)port);
        gtk_signal_emit_by_name (GTK_OBJECT (adj), "value_changed");
        gtk_signal_handler_unblock_by_func(GTK_OBJECT(adj),
                                           GTK_SIGNAL_FUNC(on_port_knob_changed),
                                           (gpointer)(intptr_t)port);
        break;

      case PORT_CONTROL_OUTPUT:
        /* GDB_MESSAGE(GDB_GUI, " update_port_widget: change of '%s' to %f\n", plugin_descriptor->LADSPA_Plugin->PortNames[port], value); */
        {
            char buf[16];
            snprintf(buf, 16, "%.6g", value);
            gtk_label_set_text (GTK_LABEL (port_data[port].widget), buf);
        }
        break;

      default:
        break;
    }
}

void
update_for_sample_rate(void)
{
    unsigned long portcount = plugin_descriptor->LADSPA_Plugin->PortCount;
    int port;
    LADSPA_PortRangeHintDescriptor prh;
    LADSPA_Data plb, pub;

    GDB_MESSAGE(GDB_GUI, " update_for_sample_rate: new rate %ld\n", sample_rate);

    for (port = 0; port < portcount; port++) {
        if (!(port_data[port].by_sample_rate &&
              (port_data[port].type == PORT_CONTROL_INPUT_INTEGER ||
               port_data[port].type == PORT_CONTROL_INPUT_LOGARITHMIC ||
               port_data[port].type == PORT_CONTROL_INPUT_LINEAR)))
            continue;

        prh = plugin_descriptor->LADSPA_Plugin->PortRangeHints[port].HintDescriptor;

        if (LADSPA_IS_HINT_BOUNDED_BELOW(prh) &&
            LADSPA_IS_HINT_BOUNDED_ABOVE(prh)) {
            plb = plugin_descriptor->LADSPA_Plugin->PortRangeHints[port].LowerBound;
            pub = plugin_descriptor->LADSPA_Plugin->PortRangeHints[port].UpperBound;
        } else if (LADSPA_IS_HINT_BOUNDED_BELOW(prh)) {
            plb = plugin_descriptor->LADSPA_Plugin->PortRangeHints[port].LowerBound;
            if (plb < 1.0f)
                pub = 1.0f;
            else
                pub = plb + 1.0f;
        } else { /* LADSPA_IS_HINT_BOUNDED_ABOVE(prh) */
            pub = plugin_descriptor->LADSPA_Plugin->PortRangeHints[port].UpperBound;
            if (pub > 0.0f)
                plb = 0.0f;
            else
                plb = pub - 1.0f;
        }
        plb *= sample_rate;
        pub *= sample_rate;
        port_data[port].LowerBound = plb;
        port_data[port].UpperBound = pub;

        /* block signals */
        if (port_data[port].type == PORT_CONTROL_INPUT_INTEGER)
            g_signal_handlers_block_by_func(G_OBJECT(port_data[port].adjustment),
                                            (gpointer)on_port_spin_changed,
                                            (gpointer)(intptr_t)port);
        else
            g_signal_handlers_block_by_func(G_OBJECT(port_data[port].adjustment),
                                            (gpointer)on_port_knob_changed,
                                            (gpointer)(intptr_t)port);

        /* update bounds */
#if GTK_CHECK_VERSION(2, 14, 0)
        gtk_adjustment_set_lower(GTK_ADJUSTMENT(port_data[port].adjustment), plb);
        gtk_adjustment_set_upper(GTK_ADJUSTMENT(port_data[port].adjustment), pub);
#else
        GTK_ADJUSTMENT(port_data[port].adjustment)->lower = plb;
        GTK_ADJUSTMENT(port_data[port].adjustment)->upper = pub;
        gtk_adjustment_changed(GTK_ADJUSTMENT(port_data[port].adjustment));
#endif

        /* update labels */
        if (port_data[port].bounded) {
            char buf[32];
            sprintf(buf, "%.5g", plb);
            gtk_label_set_text(GTK_LABEL(port_data[port].lower_label), buf);
            sprintf(buf, "%.5g", pub);
            gtk_label_set_text(GTK_LABEL(port_data[port].upper_label), buf);
        }

        /* make widget sensitive */
        gtk_widget_set_sensitive (port_data[port].widget, TRUE);

        /* unblock signals */
        if (port_data[port].type == PORT_CONTROL_INPUT_INTEGER)
            g_signal_handlers_unblock_by_func(G_OBJECT(port_data[port].adjustment),
                                              (gpointer)on_port_spin_changed,
                                              (gpointer)(intptr_t)port);
        else
            g_signal_handlers_unblock_by_func(G_OBJECT(port_data[port].adjustment),
                                              (gpointer)on_port_knob_changed,
                                              (gpointer)(intptr_t)port);
    }
}

/* ==== GTK+ widget creation ==== */

void
create_main_window (const char *tag, const char *soname, const char *label)
{
    GtkWidget *vbox4;
    char buf[256], *tmp;
    GtkWidget *main_label;
    GtkWidget *separator;
    GtkWidget *program_hbox;
    GtkWidget *bank_label;
    GtkWidget *bank_spin;
    GtkWidget *program_label;
    GtkWidget *program_spin;
    unsigned long portcount = plugin_descriptor->LADSPA_Plugin->PortCount;
    int port, x, y;
    GtkWidget *table2;
    GtkWidget *scrolledwindow1;
    GtkWidget *viewport1;
    GtkWidget *test_note_frame;
    GtkWidget *test_note_table;
    GtkWidget *test_note_label_key;
    GtkWidget *test_note_label_velocity;
    GtkWidget *test_note_key;
    GtkWidget *test_note_velocity;
    GtkWidget *test_note_mode_button;
    GtkWidget *port_frame;
    GtkWidget *port_table;
    GtkWidget *port_label;
    LADSPA_PortDescriptor pod;
    LADSPA_PortRangeHintDescriptor prh;
    LADSPA_Data plb, pub;
    GtkObject *adjustment;
    GtkWidget *widget;

    main_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_object_set_data (GTK_OBJECT (main_window), "main_window", main_window);
    gtk_window_set_title (GTK_WINDOW (main_window), tag);

    /* connect main window */
    gtk_signal_connect(GTK_OBJECT(main_window), "destroy",
                       GTK_SIGNAL_FUNC(gtk_main_quit), NULL);
    gtk_signal_connect (GTK_OBJECT (main_window), "delete_event",
                        (GtkSignalFunc)on_delete_event_wrapper,
                        (gpointer)gtk_main_quit);

    vbox4 = gtk_vbox_new (FALSE, 0);
    gtk_widget_ref (vbox4);
    gtk_object_set_data_full (GTK_OBJECT (main_window), "vbox4", vbox4,
                              (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (vbox4);
    gtk_container_add (GTK_CONTAINER (main_window), vbox4);

    if (plugin_is_DSSI_so)
        snprintf(buf, 256, "DSSI plugin %s:%s", soname, label);
    else
        snprintf(buf, 256, "LADSPA plugin %s:%s", soname, label);
    main_label = gtk_label_new (buf);
    gtk_widget_ref (main_label);
    gtk_object_set_data_full (GTK_OBJECT (main_window), "main_label plugin", main_label,
                              (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (main_label);
    gtk_box_pack_start (GTK_BOX (vbox4), main_label, FALSE, FALSE, 2);
    gtk_misc_set_alignment (GTK_MISC (main_label), 0, 0.5);
    gtk_misc_set_padding (GTK_MISC (main_label), 5, 0);
    /* gtk_label_set_line_wrap (GTK_LABEL (main_label), TRUE); */
    
    snprintf(buf, 256, "Name: %s", plugin_descriptor->LADSPA_Plugin->Name);
    main_label = gtk_label_new (buf);
    gtk_widget_ref (main_label);
    gtk_object_set_data_full (GTK_OBJECT (main_window), "main_label Name", main_label,
                              (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (main_label);
    gtk_box_pack_start (GTK_BOX (vbox4), main_label, FALSE, FALSE, 2);
    gtk_misc_set_alignment (GTK_MISC (main_label), 0, 0.5);
    gtk_misc_set_padding (GTK_MISC (main_label), 5, 0);
    gtk_label_set_line_wrap (GTK_LABEL (main_label), TRUE);

    snprintf(buf, 256, "Maker: %s", plugin_descriptor->LADSPA_Plugin->Maker);
    main_label = gtk_label_new (buf);
    gtk_widget_ref (main_label);
    gtk_object_set_data_full (GTK_OBJECT (main_window), "main_label Maker", main_label,
                              (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (main_label);
    gtk_box_pack_start (GTK_BOX (vbox4), main_label, FALSE, FALSE, 2);
    gtk_misc_set_alignment (GTK_MISC (main_label), 0, 0.5);
    gtk_misc_set_padding (GTK_MISC (main_label), 5, 0);
    gtk_label_set_line_wrap (GTK_LABEL (main_label), TRUE);

    snprintf(buf, 256, "Copyright: %s", plugin_descriptor->LADSPA_Plugin->Copyright);
    main_label = gtk_label_new (buf);
    gtk_widget_ref (main_label);
    gtk_object_set_data_full (GTK_OBJECT (main_window), "main_label Copyright", main_label,
                              (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (main_label);
    gtk_box_pack_start (GTK_BOX (vbox4), main_label, FALSE, FALSE, 2);
    gtk_misc_set_alignment (GTK_MISC (main_label), 0, 0.5);
    gtk_misc_set_padding (GTK_MISC (main_label), 5, 0);
    gtk_label_set_line_wrap (GTK_LABEL (main_label), TRUE);

    /* program widgets */
    if (plugin_descriptor->select_program) {

        separator = gtk_hseparator_new ();
        gtk_widget_ref (separator);
        gtk_object_set_data_full (GTK_OBJECT (main_window), "separator1", separator,
                                  (GtkDestroyNotify) gtk_widget_unref);
        gtk_widget_show (separator);
        gtk_box_pack_start (GTK_BOX (vbox4), separator, FALSE, FALSE, 2);

        program_hbox = gtk_hbox_new (FALSE, 10);
        gtk_widget_ref (program_hbox);
        gtk_object_set_data_full (GTK_OBJECT (main_window), "program_hbox", program_hbox,
                                  (GtkDestroyNotify) gtk_widget_unref);
        gtk_widget_show (program_hbox);
        gtk_box_pack_start (GTK_BOX (vbox4), program_hbox, FALSE, FALSE, 2);

        bank_label = gtk_label_new ("Bank:");
        gtk_widget_ref (bank_label);
        gtk_object_set_data_full (GTK_OBJECT (main_window), "bank_label", bank_label,
                                  (GtkDestroyNotify) gtk_widget_unref);
        gtk_widget_show (bank_label);
        gtk_box_pack_start (GTK_BOX (program_hbox), bank_label, FALSE, FALSE, 2);
        gtk_misc_set_alignment (GTK_MISC (bank_label), 0, 0.5);

        bank_spin_adj = gtk_adjustment_new (0, 0, G_MAXLONG, 1, 1, 0);
        bank_spin = gtk_spin_button_new (GTK_ADJUSTMENT (bank_spin_adj), 1, 0);
        gtk_widget_ref (bank_spin);
        gtk_object_set_data_full (GTK_OBJECT (main_window), "bank_spin", bank_spin,
                                  (GtkDestroyNotify) gtk_widget_unref);
        gtk_widget_show (bank_spin);
        gtk_box_pack_start (GTK_BOX (program_hbox), bank_spin, FALSE, FALSE, 0);
        gtk_spin_button_set_numeric (GTK_SPIN_BUTTON (bank_spin), TRUE);
        gtk_spin_button_set_update_policy (GTK_SPIN_BUTTON (bank_spin), GTK_UPDATE_IF_VALID);
        gtk_spin_button_set_snap_to_ticks (GTK_SPIN_BUTTON (bank_spin), TRUE);
        gtk_spin_button_set_wrap (GTK_SPIN_BUTTON (bank_spin), TRUE);
        gtk_signal_connect (GTK_OBJECT (bank_spin_adj), "value_changed",
                            GTK_SIGNAL_FUNC (on_program_spin_changed),
                            (gpointer)0);

        program_label = gtk_label_new ("Program:");
        gtk_widget_ref (program_label);
        gtk_object_set_data_full (GTK_OBJECT (main_window), "program_label", program_label,
                                  (GtkDestroyNotify) gtk_widget_unref);
        gtk_widget_show (program_label);
        gtk_box_pack_start (GTK_BOX (program_hbox), program_label, FALSE, FALSE, 2);
        gtk_misc_set_alignment (GTK_MISC (program_label), 0, 0.5);

        program_spin_adj = gtk_adjustment_new (0, 0, G_MAXLONG, 1, 1, 0);
        program_spin = gtk_spin_button_new (GTK_ADJUSTMENT (program_spin_adj), 1, 0);
        gtk_widget_ref (program_spin);
        gtk_object_set_data_full (GTK_OBJECT (main_window), "program_spin", program_spin,
                                  (GtkDestroyNotify) gtk_widget_unref);
        gtk_widget_show (program_spin);
        gtk_box_pack_start (GTK_BOX (program_hbox), program_spin, FALSE, FALSE, 0);
        gtk_spin_button_set_numeric (GTK_SPIN_BUTTON (program_spin), TRUE);
        gtk_spin_button_set_update_policy (GTK_SPIN_BUTTON (program_spin), GTK_UPDATE_IF_VALID);
        gtk_spin_button_set_snap_to_ticks (GTK_SPIN_BUTTON (program_spin), TRUE);
        gtk_spin_button_set_wrap (GTK_SPIN_BUTTON (program_spin), TRUE);
        gtk_signal_connect (GTK_OBJECT (program_spin_adj), "value_changed",
                            GTK_SIGNAL_FUNC (on_program_spin_changed),
                            (gpointer)1);
    }

    /* port widget table and scrolledwindow */
    separator = gtk_hseparator_new ();
    gtk_widget_ref (separator);
    gtk_object_set_data_full (GTK_OBJECT (main_window), "separator2", separator,
                              (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (separator);
    gtk_box_pack_start (GTK_BOX (vbox4), separator, FALSE, FALSE, 2);

    table2 = gtk_table_new (5, (portcount + 4) / 5, FALSE);
    gtk_widget_ref (table2);
    gtk_object_set_data_full (GTK_OBJECT (main_window), "table2", table2,
                              (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (table2);
    gtk_container_set_border_width (GTK_CONTAINER (table2), 4);
    gtk_table_set_col_spacings (GTK_TABLE (table2), 2);

    if (portcount < 21) {
        gtk_box_pack_start (GTK_BOX (vbox4), table2, TRUE, TRUE, 0);
    } else {
        scrolledwindow1 = gtk_scrolled_window_new (NULL, NULL);
        gtk_widget_ref (scrolledwindow1);
        gtk_object_set_data_full (GTK_OBJECT (main_window), "scrolledwindow1", scrolledwindow1,
                                  (GtkDestroyNotify) gtk_widget_unref);
        gtk_widget_show (scrolledwindow1);
        gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolledwindow1), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        gtk_range_set_update_policy (GTK_RANGE (GTK_SCROLLED_WINDOW (scrolledwindow1)->vscrollbar), GTK_UPDATE_CONTINUOUS);
        gtk_box_pack_start (GTK_BOX (vbox4), scrolledwindow1, TRUE, TRUE, 0);

        gtk_widget_set_size_request(GTK_WIDGET(scrolledwindow1), -1, 300);

        viewport1 = gtk_viewport_new (NULL, NULL);
        gtk_widget_ref (viewport1);
        gtk_object_set_data_full (GTK_OBJECT (main_window), "viewport1", viewport1,
                                  (GtkDestroyNotify) gtk_widget_unref);
        gtk_widget_show (viewport1);
        gtk_container_add (GTK_CONTAINER (scrolledwindow1), viewport1);

        gtk_container_add (GTK_CONTAINER (viewport1), table2);
    }

    /* test note widgets */
    if (plugin_descriptor->run_synth ||
        plugin_descriptor->run_synth_adding ||
        plugin_descriptor->run_multiple_synths ||
        plugin_descriptor->run_multiple_synths_adding) {

        separator = gtk_hseparator_new ();
        gtk_widget_ref (separator);
        gtk_object_set_data_full (GTK_OBJECT (main_window), "separator3", separator,
                                  (GtkDestroyNotify) gtk_widget_unref);
        gtk_widget_show (separator);
        gtk_box_pack_start (GTK_BOX (vbox4), separator, FALSE, FALSE, 2);

        test_note_frame = gtk_frame_new ("Test Note");
        gtk_widget_ref (test_note_frame);
        gtk_object_set_data_full (GTK_OBJECT (main_window), "test_note_frame", test_note_frame,
                                  (GtkDestroyNotify) gtk_widget_unref);
        gtk_widget_show (test_note_frame);
        gtk_container_set_border_width (GTK_CONTAINER (test_note_frame), 5);

        test_note_table = gtk_table_new (4, 2, FALSE);
        gtk_widget_show (test_note_table);
        gtk_container_add (GTK_CONTAINER (test_note_frame), test_note_table);
        gtk_container_set_border_width (GTK_CONTAINER (test_note_table), 2);
        gtk_table_set_row_spacings (GTK_TABLE (test_note_table), 1);
        gtk_table_set_col_spacings (GTK_TABLE (test_note_table), 5);

        test_note_label_key = gtk_label_new ("key");
        gtk_widget_ref (test_note_label_key);
        gtk_object_set_data_full (GTK_OBJECT (main_window), "test_note_label_key", test_note_label_key,
                                  (GtkDestroyNotify) gtk_widget_unref);
        gtk_widget_show (test_note_label_key);
        gtk_table_attach (GTK_TABLE (test_note_table), test_note_label_key, 0, 1, 0, 1,
                          (GtkAttachOptions) (GTK_FILL),
                          (GtkAttachOptions) (0), 0, 0);
        gtk_misc_set_alignment (GTK_MISC (test_note_label_key), 0, 0.5);

        test_note_label_velocity = gtk_label_new ("velocity");
        gtk_widget_ref (test_note_label_velocity);
        gtk_object_set_data_full (GTK_OBJECT (main_window), "test_note_label_velocity", test_note_label_velocity,
                                  (GtkDestroyNotify) gtk_widget_unref);
        gtk_widget_show (test_note_label_velocity);
        gtk_table_attach (GTK_TABLE (test_note_table), test_note_label_velocity, 0, 1, 1, 2,
                          (GtkAttachOptions) (GTK_FILL),
                          (GtkAttachOptions) (0), 0, 0);
        gtk_misc_set_alignment (GTK_MISC (test_note_label_velocity), 0, 0.5);

        test_note_mode_button = gtk_check_button_new ();
        gtk_widget_show (test_note_mode_button);
        gtk_table_attach (GTK_TABLE (test_note_table), test_note_mode_button, 2, 3, 0, 2,
                          (GtkAttachOptions) (0),
                          (GtkAttachOptions) (0), 4, 0);
        gtk_signal_connect (GTK_OBJECT (test_note_mode_button), "toggled",
                            GTK_SIGNAL_FUNC (on_test_note_mode_toggled),
                            NULL);

        test_note_button = gtk_button_new_with_label ("Send Test Note");
        gtk_widget_ref (test_note_button);
        gtk_object_set_data_full (GTK_OBJECT (main_window), "test_note_button", test_note_button,
                                  (GtkDestroyNotify) gtk_widget_unref);
        gtk_widget_show (test_note_button);
        gtk_table_attach (GTK_TABLE (test_note_table), test_note_button, 3, 4, 0, 2,
                          (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
                          (GtkAttachOptions) (0), 4, 0);
        gtk_signal_connect (GTK_OBJECT (test_note_button), "pressed",
                            GTK_SIGNAL_FUNC (on_test_note_button_press),
                            (gpointer)1);
        gtk_signal_connect (GTK_OBJECT (test_note_button), "released",
                            GTK_SIGNAL_FUNC (on_test_note_button_press),
                            (gpointer)0);

        test_note_toggle = gtk_toggle_button_new_with_label ("Toggle Test Note");
        /* gtk_widget_show (test_note_toggle);  -- initially hidden */
        gtk_table_attach (GTK_TABLE (test_note_table), test_note_toggle, 3, 4, 0, 2,
                          (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
                          (GtkAttachOptions) (0), 4, 0);
        gtk_signal_connect (GTK_OBJECT (test_note_toggle), "toggled",
                            GTK_SIGNAL_FUNC (on_test_note_toggle_toggled),
                            NULL);

        test_note_key = gtk_hscale_new (GTK_ADJUSTMENT (gtk_adjustment_new (60, 12, 120, 1, 12, 12)));
        gtk_widget_ref (test_note_key);
        gtk_object_set_data_full (GTK_OBJECT (main_window), "test_note_key", test_note_key,
                                  (GtkDestroyNotify) gtk_widget_unref);
        gtk_widget_show (test_note_key);
        gtk_table_attach (GTK_TABLE (test_note_table), test_note_key, 1, 2, 0, 1,
                          (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
                          (GtkAttachOptions) (0), 0, 0);
        gtk_scale_set_value_pos (GTK_SCALE (test_note_key), GTK_POS_RIGHT);
        gtk_scale_set_digits (GTK_SCALE (test_note_key), 0);
        gtk_range_set_update_policy (GTK_RANGE (test_note_key), GTK_UPDATE_DELAYED);
        gtk_signal_connect (GTK_OBJECT (gtk_range_get_adjustment (GTK_RANGE (test_note_key))),
                            "value_changed", GTK_SIGNAL_FUNC(on_test_note_slider_change),
                            (gpointer)0);

        test_note_velocity = gtk_hscale_new (GTK_ADJUSTMENT (gtk_adjustment_new (96, 1, 137, 1, 10, 10)));
        gtk_widget_ref (test_note_velocity);
        gtk_object_set_data_full (GTK_OBJECT (main_window), "test_note_velocity", test_note_velocity,
                                  (GtkDestroyNotify) gtk_widget_unref);
        gtk_widget_show (test_note_velocity);
        gtk_table_attach (GTK_TABLE (test_note_table), test_note_velocity, 1, 2, 1, 2,
                          (GtkAttachOptions) (GTK_FILL),
                          (GtkAttachOptions) (0), 0, 0);
        gtk_scale_set_value_pos (GTK_SCALE (test_note_velocity), GTK_POS_RIGHT);
        gtk_scale_set_digits (GTK_SCALE (test_note_velocity), 0);
        gtk_range_set_update_policy (GTK_RANGE (test_note_velocity), GTK_UPDATE_DELAYED);
        gtk_signal_connect (GTK_OBJECT (gtk_range_get_adjustment (GTK_RANGE (test_note_velocity))),
                            "value_changed", GTK_SIGNAL_FUNC(on_test_note_slider_change),
                            (gpointer)1);

        gtk_box_pack_start (GTK_BOX (vbox4), test_note_frame, FALSE, FALSE, 0);
    }

    /* port widgets */
    port_data = (port_data_t *)calloc(portcount, sizeof(port_data_t));

    for (port = 0; port < portcount; port++) {
        port_frame = gtk_frame_new (NULL);
        gtk_widget_ref (port_frame);
        gtk_object_set_data_full (GTK_OBJECT (main_window), "port_frame", port_frame,
                                  (GtkDestroyNotify) gtk_widget_unref);
        gtk_widget_show (port_frame);
        x = port % 5;
        y = port / 5;
        gtk_table_attach (GTK_TABLE (table2), port_frame, x, x + 1, y, y + 1,
                          (GtkAttachOptions) (GTK_FILL),
                          (GtkAttachOptions) (GTK_FILL), 0, 0);

        port_table = gtk_table_new (3, 3, FALSE);
        gtk_widget_ref (port_table);
        gtk_object_set_data_full (GTK_OBJECT (main_window), "port_table", port_table,
                                  (GtkDestroyNotify) gtk_widget_unref);
        gtk_widget_show (port_table);
        gtk_container_set_border_width (GTK_CONTAINER (port_table), 4);
        gtk_table_set_row_spacings (GTK_TABLE (port_table), 0);
        gtk_table_set_col_spacings (GTK_TABLE (port_table), 0);
        gtk_container_add (GTK_CONTAINER (port_frame), port_table);

        tmp = (char *)plugin_descriptor->LADSPA_Plugin->PortNames[port];
        if (strlen(tmp) <= 21) {
            strcpy(buf, tmp);
        } else {
            for (x = 0, y = 0; x <= strlen(tmp) && y < 255; x++, y++) {
                buf[y] = tmp[x];
                if (tmp[x] == 0)
                    break;
                if (x % 20 == 19)
                    buf[++y] = '\n';
            }
        }
        port_label = gtk_label_new (buf);
        gtk_widget_ref (port_label);
        gtk_object_set_data_full (GTK_OBJECT (main_window), "port_label", port_label,
                                  (GtkDestroyNotify) gtk_widget_unref);
        gtk_widget_show (port_label);
        gtk_misc_set_alignment (GTK_MISC (port_label), 0, 0.5);
        gtk_misc_set_padding (GTK_MISC (port_label), 2, 2);
        gtk_label_set_line_wrap (GTK_LABEL (port_label), TRUE);
        gtk_table_attach (GTK_TABLE (port_table), port_label, 0, 3, 0, 1,
                                  (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
                                  (GtkAttachOptions) (0), 2, 2);

        pod = plugin_descriptor->LADSPA_Plugin->PortDescriptors[port];
        prh = plugin_descriptor->LADSPA_Plugin->PortRangeHints[port].HintDescriptor;
        port_data[port].bounded = 0;
        port_data[port].by_sample_rate = 0;
        if (LADSPA_IS_HINT_BOUNDED_BELOW(prh) &&
            LADSPA_IS_HINT_BOUNDED_ABOVE(prh)) {
            plb = plugin_descriptor->LADSPA_Plugin->PortRangeHints[port].LowerBound;
            pub = plugin_descriptor->LADSPA_Plugin->PortRangeHints[port].UpperBound;
            port_data[port].bounded = 1;
            if (LADSPA_IS_HINT_SAMPLE_RATE(prh))
                port_data[port].by_sample_rate = 1;
        } else if (LADSPA_IS_HINT_BOUNDED_BELOW(prh)) {
            plb = plugin_descriptor->LADSPA_Plugin->PortRangeHints[port].LowerBound;
            pub = plb + 1.0f;
            if (LADSPA_IS_HINT_SAMPLE_RATE(prh))
                port_data[port].by_sample_rate = 1;
        } else if (LADSPA_IS_HINT_BOUNDED_ABOVE(prh)) {
            pub = plugin_descriptor->LADSPA_Plugin->PortRangeHints[port].UpperBound;
            plb = pub - 1.0f;
            if (LADSPA_IS_HINT_SAMPLE_RATE(prh))
                port_data[port].by_sample_rate = 1;
        } else {
            plb = 0.0f; pub = 1.0f;
        }
        port_data[port].LowerBound = plb;
        port_data[port].UpperBound = pub;

        if (LADSPA_IS_PORT_AUDIO(pod)) {

            if (LADSPA_IS_PORT_INPUT(pod)) {
                port_data[port].type = PORT_AUDIO_INPUT;

                widget = gtk_label_new ("(audio input)");
            } else {
                port_data[port].type = PORT_AUDIO_OUTPUT;

                widget = gtk_label_new ("(audio output)");
            }
            port_data[port].widget = widget;
            gtk_widget_ref (widget);
            gtk_object_set_data_full (GTK_OBJECT (main_window), "audio label", widget,
                                      (GtkDestroyNotify) gtk_widget_unref);
            gtk_widget_show (widget);
            gtk_misc_set_alignment (GTK_MISC (widget), 0.5, 0.5);
            gtk_misc_set_padding (GTK_MISC (widget), 2, 2);
            gtk_table_attach (GTK_TABLE (port_table), widget, 0, 3, 1, 3,
                                      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
                                      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL), 5, 5);

        } else if (!LADSPA_IS_PORT_CONTROL(pod) ||
                   !LADSPA_IS_PORT_INPUT(pod)) {  /* assume control output */

            port_data[port].type = PORT_CONTROL_OUTPUT;

            widget = gtk_label_new ("?");
            port_data[port].widget = widget;
            gtk_widget_ref (widget);
            gtk_object_set_data_full (GTK_OBJECT (main_window), "ctlout label", widget,
                                      (GtkDestroyNotify) gtk_widget_unref);
            gtk_widget_show (widget);
            gtk_misc_set_alignment (GTK_MISC (widget), 0.5, 0.5);
            gtk_misc_set_padding (GTK_MISC (widget), 2, 2);
            gtk_table_attach (GTK_TABLE (port_table), widget, 0, 3, 1, 3,
                              (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
                              (GtkAttachOptions) (GTK_EXPAND | GTK_FILL), 5, 5);

        /* from here on should be control inputs */

        } else if (LADSPA_IS_HINT_TOGGLED(prh)) {

            port_data[port].type = PORT_CONTROL_INPUT_TOGGLED;

            widget = gtk_check_button_new ();
            port_data[port].widget = widget;
            gtk_widget_ref (widget);
            gtk_object_set_data_full (GTK_OBJECT (main_window), "port toggle", widget,
                                      (GtkDestroyNotify) gtk_widget_unref);
            gtk_widget_show (widget);
            gtk_table_attach (GTK_TABLE (port_table), widget, 0, 3, 1, 3,
                                      (GtkAttachOptions) (0),
                                      (GtkAttachOptions) (0), 5, 5);
            gtk_signal_connect (GTK_OBJECT (widget), "toggled",
                        GTK_SIGNAL_FUNC (on_port_button_toggled),
                        (gpointer)(intptr_t)port);

        } else if (LADSPA_IS_HINT_INTEGER(prh)) {
            /* -FIX- would be nice to implement LADSPA_HINT_SWITCHED */

            port_data[port].type = PORT_CONTROL_INPUT_INTEGER;

            adjustment = gtk_adjustment_new (plb, plb, pub, 1, 1, 0);
            port_data[port].adjustment = adjustment;
            widget = gtk_spin_button_new (GTK_ADJUSTMENT (adjustment), 1, 0);
            port_data[port].widget = widget;
            gtk_widget_ref (widget);
            gtk_object_set_data_full (GTK_OBJECT (main_window), "port spin", widget,
                                      (GtkDestroyNotify) gtk_widget_unref);
            if (port_data[port].by_sample_rate)
                gtk_widget_set_sensitive (widget, FALSE);
            gtk_widget_show (widget);
            gtk_spin_button_set_numeric (GTK_SPIN_BUTTON (widget), TRUE);
            gtk_spin_button_set_update_policy (GTK_SPIN_BUTTON (widget), GTK_UPDATE_IF_VALID);
            gtk_spin_button_set_snap_to_ticks (GTK_SPIN_BUTTON (widget), TRUE);
            gtk_spin_button_set_wrap (GTK_SPIN_BUTTON (widget), TRUE);
            gtk_table_attach (GTK_TABLE (port_table), widget, 0, 3, 1, 3,
                                      (GtkAttachOptions) (0),
                                      (GtkAttachOptions) (0), 5, 5);
            gtk_signal_connect (GTK_OBJECT (adjustment), "value_changed",
                                GTK_SIGNAL_FUNC (on_port_spin_changed),
                                (gpointer)(intptr_t)port);

        } else { /* continuous control input */

            if (LADSPA_IS_HINT_LOGARITHMIC(prh) && plb > 0.0f && pub > plb)
                port_data[port].type = PORT_CONTROL_INPUT_LOGARITHMIC;
            else
                port_data[port].type = PORT_CONTROL_INPUT_LINEAR;

            adjustment = gtk_adjustment_new (plb, plb, pub, (pub - plb) / 1000.0f, 1, 0);
            port_data[port].adjustment = adjustment;

            widget = gtk_knob_new (GTK_ADJUSTMENT (adjustment));
            port_data[port].widget = widget;
            gtk_widget_ref (widget);
            gtk_object_set_data_full (GTK_OBJECT (main_window), "port knob", widget,
                                      (GtkDestroyNotify) gtk_widget_unref);
            if (port_data[port].by_sample_rate)
                gtk_widget_set_sensitive (widget, FALSE);
            gtk_widget_show (widget);
            gtk_table_attach (GTK_TABLE (port_table), widget, 1, 2, 1, 2,
                                      (GtkAttachOptions) (0),
                                      (GtkAttachOptions) (0), 0, 0);
            gtk_signal_connect (GTK_OBJECT (adjustment), "value_changed",
                                GTK_SIGNAL_FUNC (on_port_knob_changed),
                                (gpointer)(intptr_t)port);

            if (port_data[port].bounded) {
                GtkWidget *lb_label, *ub_label;

                if (port_data[port].by_sample_rate) {
                    sprintf(buf, "?");
                } else {
                    sprintf(buf, "%.5g", plb);
                }
                lb_label = gtk_label_new (buf);
                port_data[port].lower_label = lb_label;
                gtk_widget_ref (lb_label);
                gtk_object_set_data_full (GTK_OBJECT (main_window), "port lb_label", lb_label,
                                          (GtkDestroyNotify) gtk_widget_unref);
                gtk_widget_show (lb_label);
                gtk_misc_set_alignment (GTK_MISC (lb_label), 1, 0.5);
                gtk_misc_set_padding (GTK_MISC (lb_label), 1, 0);
                gtk_table_attach (GTK_TABLE (port_table), lb_label, 0, 1, 2, 3,
                                  (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
                                  (GtkAttachOptions) (0), 0, 0);
                if (port_data[port].by_sample_rate) {
                    sprintf(buf, "?");
                } else {
                    sprintf(buf, "%.5g", pub);
                }
                ub_label = gtk_label_new (buf);
                port_data[port].upper_label = ub_label;
                gtk_widget_ref (ub_label);
                gtk_object_set_data_full (GTK_OBJECT (main_window), "port ub_label", ub_label,
                                          (GtkDestroyNotify) gtk_widget_unref);
                gtk_widget_show (ub_label);
                gtk_misc_set_alignment (GTK_MISC (ub_label), 0, 0.5);
                gtk_misc_set_padding (GTK_MISC (ub_label), 1, 0);
                gtk_table_attach (GTK_TABLE (port_table), ub_label, 2, 3, 2, 3,
                                  (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
                                  (GtkAttachOptions) (0), 0, 0);
            }
        }
    }
}

void
create_windows(const char *instance_tag, const char *soname, const char *label)
{
    char tag[60];

    /* build a nice identifier string for the window titles */
    if (strlen(instance_tag) == 0) {
        strcpy(tag, "ghostess universal DSSI plugin GUI");
    } else {
        if (strlen(instance_tag) > 43) {
            snprintf(tag, 60, "ghostess uniGUI ...%s", instance_tag + strlen(instance_tag) - 40);
        } else {
            snprintf(tag, 60, "ghostess uniGUI %s", instance_tag);
        }
    }

    create_main_window(tag, soname, label);
}

/* ==== DSSI/LADSPA plugin handling ==== */

int
load_so(char *soname, char *label)
{
    char *path, *origPath, *element, *filePath;
    const char *message;
    int i;

    plugin_dlh = NULL;

    if (soname[0] == '/') {  /* absolute path */
        if ((plugin_dlh = dlopen(soname, RTLD_LAZY))) {
            plugin_path = strdup(soname);
        } else {
            message = dlerror();
            if (message) {
                GDB_MESSAGE(GDB_PLUGIN, ": dlopen of '%s' failed: %s\n", soname, message);
            } else {
                GDB_MESSAGE(GDB_PLUGIN, ": dlopen of '%s' failed\n", soname);
            }
            return 0;
        }
    } else {  /* relative path */

        if (!(path = getenv("DSSI_PATH"))) {
            path = "/usr/local/lib/dssi:/usr/lib/dssi";
            GDB_MESSAGE(GDB_PLUGIN, " warning: DSSI_PATH not set, defaulting to '%s'\n", path);
        }
        origPath = strdup(path);
        path = origPath;

        while ((element = strtok(path, ":")) != 0) {

            path = 0;

            if (element[0] != '/') {
                GDB_MESSAGE(GDB_PLUGIN, ": ignoring DSSI_PATH relative element '%s'\n", element);
                continue;
            }

            filePath = (char *)malloc(strlen(element) + strlen(soname) + 2);
            sprintf(filePath, "%s/%s", element, soname);

            if ((plugin_dlh = dlopen(filePath, RTLD_LAZY))) {
                plugin_path = filePath;
                break;
            }

            message = dlerror();
            if (message) {
                GDB_MESSAGE(GDB_PLUGIN, ": dlopen of '%s' failed: %s\n", filePath, message);
            } else {
                GDB_MESSAGE(GDB_PLUGIN, ": dlopen of '%s' failed\n", filePath);
            }

            free(filePath);
        }

        free(origPath);

        if (!plugin_dlh) {
            GDB_MESSAGE(GDB_PLUGIN, ": plugin '%s' not found\n", soname);
            return 0;
        }
    }
    GDB_MESSAGE(GDB_PLUGIN, ": '%s' found at '%s'\n", soname, plugin_path);

    plugin_descfn = (DSSI_Descriptor_Function)dlsym(plugin_dlh,
                                                    "dssi_descriptor");
    if (plugin_descfn) {
        plugin_is_DSSI_so = 1;
    } else {
        plugin_descfn = (DSSI_Descriptor_Function)dlsym(plugin_dlh,
                                                        "ladspa_descriptor");
        if (!plugin_descfn) {
            GDB_MESSAGE(GDB_PLUGIN, ": %s is not a DSSI or LADSPA plugin library\n", soname);
            return 0;
        }
        plugin_is_DSSI_so = 0;
    }

    /* get the plugin descriptor */
    i = 0;
    if (plugin_is_DSSI_so) {
        const DSSI_Descriptor *desc;

        while ((desc = plugin_descfn(i++))) {
            if (!strcmp(desc->LADSPA_Plugin->Label, label)) {
                plugin_descriptor = desc;
                break;
            }
        }
    } else { /* LADSPA plugin; create and use a dummy DSSI descriptor */
        LADSPA_Descriptor *desc;

        plugin_descriptor = (const DSSI_Descriptor *)calloc(1, sizeof(DSSI_Descriptor));
        ((DSSI_Descriptor *)plugin_descriptor)->DSSI_API_Version = 1;

        while ((desc = (LADSPA_Descriptor *)plugin_descfn(i++))) {
            if (!strcmp(desc->Label, label)) {
                ((DSSI_Descriptor *)plugin_descriptor)->LADSPA_Plugin = desc;
                break;
            }
        }
        if (!plugin_descriptor->LADSPA_Plugin) {
            free((void *)plugin_descriptor);
            plugin_descriptor = NULL;
        }
    }
    if (!plugin_descriptor) {
        GDB_MESSAGE(GDB_PLUGIN, ": plugin label '%s' not found in library '%s'\n",
                    label, soname);
        return 0;
    }

    return 1;
}

/* ==== main ==== */

int
main(int argc, char *argv[])
{
    char *host, *port, *path, *tmp_url;
    lo_server osc_server;
    gint osc_server_socket_tag;

    DSSP_DEBUG_INIT("ghostess uniGUI");

#ifdef DSSP_DEBUG
    GDB_MESSAGE(GDB_MAIN, " starting (pid %d)...\n", getpid());
#else
    fprintf(stderr, "ghostess uniGUI starting (pid %d)...\n", getpid());
#endif
    /* { int i; fprintf(stderr, "args:\n"); for(i=0; i<argc; i++) printf("%d: %s\n", i, argv[i]); } // debug */
    
    gtk_init(&argc, &argv);

    if (argc != 5) {
        fprintf(stderr, "usage: %s <osc url> <plugin dllname> <plugin label> <user-friendly id>\n", argv[0]);
        exit(1);
    }

    /* load and analyze plugin */
    if (!load_so(argv[2], argv[3])) {
        fprintf(stderr, "ghostess uniGUI fatal: can't load plugin %s:%s\n", argv[2], argv[3]);
        exit(1);
    }
    /* GDB_MESSAGE(GDB_PLUGIN, ": plugin has %lu ports\n", plugin_descriptor->LADSPA_Plugin->PortCount); */

    /* set up OSC support */
    osc_host_url = argv[1];
    host = lo_url_get_hostname(osc_host_url);
    port = lo_url_get_port(osc_host_url);
    path = lo_url_get_path(osc_host_url);
    osc_host_address = lo_address_new(host, port);
    osc_configure_path = osc_build_path(path, "/configure");
    osc_control_path   = osc_build_path(path, "/control");
    osc_exiting_path   = osc_build_path(path, "/exiting");
    osc_hide_path      = osc_build_path(path, "/hide");
    osc_midi_path      = osc_build_path(path, "/midi");
    osc_program_path   = osc_build_path(path, "/program");
    osc_quit_path      = osc_build_path(path, "/quit");
    osc_rate_path      = osc_build_path(path, "/sample-rate");
    osc_show_path      = osc_build_path(path, "/show");
    osc_update_path    = osc_build_path(path, "/update");

    osc_server = lo_server_new(NULL, osc_error);
    lo_server_add_method(osc_server, osc_configure_path, "ss", osc_configure_handler, NULL);
    lo_server_add_method(osc_server, osc_control_path, "if", osc_control_handler, NULL);
    lo_server_add_method(osc_server, osc_hide_path, "", osc_action_handler, "hide");
    lo_server_add_method(osc_server, osc_program_path, "ii", osc_program_handler, NULL);
    lo_server_add_method(osc_server, osc_quit_path, "", osc_action_handler, "quit");
    lo_server_add_method(osc_server, osc_rate_path, "i", osc_action_handler, "sample-rate");
    lo_server_add_method(osc_server, osc_show_path, "", osc_action_handler, "show");
    lo_server_add_method(osc_server, NULL, NULL, osc_debug_handler, NULL);

    tmp_url = lo_server_get_url(osc_server);
    osc_self_url = osc_build_path(tmp_url, (strlen(path) > 1 ? path + 1 : path));
    free(tmp_url);
    GDB_MESSAGE(GDB_OSC, ": listening at %s\n", osc_self_url);

    /* set up GTK+ */
    create_windows(argv[4], argv[2], argv[3]);

    /* add OSC server socket to GTK+'s watched I/O */
    if (lo_server_get_socket_fd(osc_server) < 0) {
        fprintf(stderr, "ghostess uniGUI fatal: OSC transport does not support exposing socket fd\n");
        exit(1);
    }
    osc_server_socket_tag = gdk_input_add(lo_server_get_socket_fd(osc_server),
                                          GDK_INPUT_READ,
                                          osc_data_on_socket_callback,
                                          osc_server);

    /* schedule our initial update request */
    schedule_update_request();

    /* let GTK+ take it from here */
    gtk_main();

    /* clean up and exit */
    GDB_MESSAGE(GDB_MAIN, ": yep, we got to the cleanup!\n");

    /* release test note, if playing */
    release_test_note();

    /* GTK+ cleanup */
    gtk_timeout_remove(update_request_timeout_tag);
    gdk_input_remove(osc_server_socket_tag);

    /* say bye-bye */
    if (!host_requested_quit) {
        lo_send(osc_host_address, osc_exiting_path, "");
    }

    /* clean up OSC support */
    lo_server_free(osc_server);
    free(host);
    free(port);
    free(path);
    free(osc_configure_path);
    free(osc_control_path);
    free(osc_exiting_path);
    free(osc_hide_path);
    free(osc_midi_path);
    free(osc_program_path);
    free(osc_quit_path);
    free(osc_rate_path);
    free(osc_show_path);
    free(osc_update_path);
    free(osc_self_url);

    /* clean up plugin */
    dlclose(plugin_dlh);
    free(plugin_path);
    if (!plugin_is_DSSI_so) {
        free((void *)plugin_descriptor);
    }
    free(port_data);

    return 0;
}

