/* ghostess - A GUI host for DSSI plugins.
 *
 * Copyright (C) 2005, 2006, 2008, 2010 Sean Bolton and others.
 *
 * Portions of this file may have come from the following sources:
 * - The DSSI example code, by Chris Cannam and Steve Harris (public
 *     domain).
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

#ifndef _GHOSTESS_H
#define _GHOSTESS_H

#include <stdio.h>
#include <pthread.h>
#include <jack/jack.h>
#ifdef MIDI_JACK
#include <jack/midiport.h>
#include <alsa/asoundlib.h>
#include <alsa/seq.h>
#endif /* MIDI_JACK */
#include <dssi.h>
#include <lo/lo.h>

/* ==== debugging ==== */

#define GDB_ERROR     1  /* error messages */
#define GDB_ALWAYS   GDB_ERROR
#define GDB_MAIN      2  /* main program flow */
#define GDB_MIDI      4  /* MIDI thread */
#define GDB_DSSI      8  /* DSSI plugin interface */
#define GDB_MIDI_CC  16  /* MIDI controllers (JACK thread)*/
#define GDB_UI       32  /* DSSI UI operations (launch, exit) */
#define GDB_OSC      64  /* OSC communication with UIs */
#define GDB_PROGRAM 128  /* get_program() results */
#define GDB_GUI     256  /* GUI callbacks, updating, etc. */

/* in ghostess.c: */
void ghss_debug(int type, const char *format, ...);

#define ghss_debug_rt ghss_debug  /* !FIX! */

/* ==== end of debugging ==== */

#define GHSS_MAX_CHANNELS   16  /* MIDI limit */

#define GHSS_MAX_INSTANCES  32  /* arbitrary */

typedef struct _plugin_strip plugin_strip;

typedef struct _d3h_dll_t d3h_dll_t;

struct _d3h_dll_t {
    d3h_dll_t               *next;
    char                    *name;
    char                    *directory;
    int                      is_DSSI_dll;
    DSSI_Descriptor_Function descfn;      /* if is_DSSI_dll is false, this is a LADSPA_Descriptor_Function */
};

typedef struct _d3h_plugin_t d3h_plugin_t;

struct _d3h_plugin_t {
    d3h_plugin_t          *next;
    int                    number;
    d3h_dll_t             *dll;
    char                  *label;
    int                    is_first_in_dll;
    const DSSI_Descriptor *descriptor;   /* may be a dummy, in the case of a LADSPA-only plugin */
    int                    ins;
    int                    outs;
    int                    controlIns;
    int                    controlOuts;
    int                    instances;
};

typedef struct _configure_item_t configure_item_t;

struct _configure_item_t {
    configure_item_t *next;
    char *key;
    char *value;
};

typedef struct _initial_port_set_t initial_port_set_t;

struct _initial_port_set_t {
    unsigned long     allocated;
    int               have_settings;
    unsigned long     highest_set;
    int              *set;
    LADSPA_Data      *value;
};

typedef struct _instance_template_t instance_template_t;

struct _instance_template_t {
    int                channel;
    configure_item_t  *configure_items;
    int                program_set;
    unsigned long      bank;
    unsigned long      program;
    initial_port_set_t ports;
};

typedef struct _d3h_instance_t d3h_instance_t;

#define MIDI_CONTROLLER_COUNT 128

struct _d3h_instance_t {
    int                number;
    d3h_plugin_t      *plugin;
    int                id;
    int                channel;
    d3h_instance_t    *channel_next_instance;
    char              *friendly_name;

    /* configure items */
    configure_item_t  *configure_items;

    /* programs */
    int                pluginProgramsValid;
    int                pluginProgramsAlloc;
    int                pluginProgramCount;
    DSSI_Program_Descriptor
                      *pluginPrograms;
    long               currentBank;
    long               currentProgram;
    int                pendingBankLSB;
    int                pendingBankMSB;
    int                pendingProgramChange;

    /* ports */
    int                firstControlIn;                       /* the offset to translate instance control in # to global control in # */
    int               *pluginPortControlInNumbers;           /* maps instance LADSPA port # to global control in # */
    long               controllerMap[MIDI_CONTROLLER_COUNT]; /* maps MIDI controller to global control in # */
    // initial_port_set_t initial_ports;
    int                have_initial_values;
    int               *initial_value_set;
    LADSPA_Data       *initial_value;

    /* ghostess GUI instance strip */
    plugin_strip      *strip;
    int                midi_activity_tick;

    /* plugin (G)UI interface */
    int                ui_running;               /* true if UI launched and 'exiting' not received */
    int                ui_visible;               /* true if 'show' sent */
    int                ui_initial_show_sent;
    int                uiNeedsProgramUpdate;
    lo_address         ui_osc_address;           /* non-NULL if 'update' received */
    lo_address         ui_osc_source;            /* address of 'known UI' for this instance */
    char              *ui_osc_control_path;
    char              *ui_osc_configure_path;
    char              *ui_osc_hide_path;
    char              *ui_osc_program_path;
    char              *ui_osc_quit_path;
    char              *ui_osc_rate_path;
    char              *ui_osc_show_path;
};

/* in ghostess.c: */
extern jack_client_t  *jackClient;
#ifdef MIDI_JACK
       jack_port_t    *midi_input_port;
       snd_midi_event_t *alsa_encoder;
#endif /* MIDI_JACK */

extern char           *host_name_default;
extern char           *host_name;
extern char           *host_osc_url;
extern int             host_exiting;
extern int             debug_flags;
extern char           *project_directory;
extern int             main_timeout_tick;

#define EVENT_BUFFER_SIZE 1024
extern snd_seq_event_t midiEventBuffer[EVENT_BUFFER_SIZE]; /* ring buffer */
extern int             midiEventReadIndex;
extern int             midiEventWriteIndex;
extern int             midi_thread_running;
extern pthread_mutex_t midiEventBufferMutex;

int  write_configuration(char *filename, const char *uuid);
int  write_patchlist(char *filename);
void query_programs(d3h_instance_t *instance);
void free_programs(d3h_instance_t *instance);
void ui_osc_free(d3h_instance_t *instance);
void start_ui(d3h_instance_t *instance);

#endif /* _GHOSTESS_H */

