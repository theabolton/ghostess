/* ghostess - A GUI host for DSSI plugins.
 *
 * Copyright (C) 2005, 2006, 2008, 2010, 2012 Sean Bolton and others.
 *
 * This is sloppy, hurried HACKWARE -- please do not consider
 * this exemplary of the authors' skills or preferences, nor of
 * good DSSI or general programming practices.  (In particular,
 * I don't want anyone attributing my mess to Chris or Steve ;-)
 *
 * Portions of this file may have come from the following sources:
 * - The DSSI example code, by Chris Cannam and Steve Harris (public
 *     domain).
 * - XML patch list export code contributed by JP Mercury.
 * - Rosegarden-4, copyright (C) 2000-2004 Guillaume Laurent,
 *     Chris Cannam, and Richard Bown.
 * - midisine.c, copyright (C) 2004 Ian Esten.
 *
 * This is a host for DSSI plugins.  It listens for MIDI events on
 * an ALSA sequencer port, JACK MIDI port, or CoreMIDI port, then
 * delivers them to DSSI synths and outputs the result via JACK.
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
#include <config.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <dlfcn.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <dirent.h>
#include <pthread.h>
#include <math.h>

#include <glib.h>
#include <gtk/gtk.h>

#include <ladspa.h>
#include <dssi.h>
#include <alsa/asoundlib.h>
#include <alsa/seq.h>
#include <jack/jack.h>
#ifdef MIDI_JACK
#include <jack/midiport.h>
#endif /* MIDI_JACK */
#ifdef JACK_SESSION
#include <jack/session.h>
#endif /* JACK_SESSION */
#include <lo/lo.h>

#include "ghostess.h"
#include "getarg.h"
#include "gui_interface.h"
#include "gui_callbacks.h"
#include "midi.h"

       jack_client_t *jackClient;
static jack_port_t  **inputPorts, **outputPorts;
#ifdef MIDI_JACK
       jack_port_t   *midi_input_port;
       snd_midi_event_t *alsa_encoder;
#endif /* MIDI_JACK */
char  *jack_session_uuid = NULL;
static float          sample_rate;

static d3h_dll_t     *dlls;

static d3h_plugin_t  *plugins;
static int            plugin_count = 0;

static d3h_instance_t instances[GHSS_MAX_INSTANCES];
static int            instance_count = 0;

static LADSPA_Handle    *instanceHandles;
static snd_seq_event_t **instanceEventBuffers;
static unsigned long    *instanceEventCounts;

static int insTotal, outsTotal;
static float **pluginInputBuffers, **pluginOutputBuffers;

static int controlInsTotal, controlOutsTotal;
static float *pluginControlIns, *pluginControlOuts;
static d3h_instance_t *channel2instances[GHSS_MAX_CHANNELS]; /* maps MIDI channel to instances */
static d3h_instance_t **pluginAudioInInstances;              /* maps global audio in # to instance */
static unsigned long *pluginAudioInPortNumbers;              /* maps global audio in # to instance LADSPA port # */
static d3h_instance_t **pluginControlInInstances;            /* maps global control in # to instance */
static unsigned long *pluginControlInPortNumbers;            /* maps global control in # to instance LADSPA port # */
static int *pluginPortUpdated;                               /* indexed by global control in # */

lo_server_thread serverThread;

static sigset_t _signals;

int   host_exiting = 0;

char *host_name_default = "ghostess";
char *host_name;
char *host_osc_url = NULL;
char *host_argv0;

int   debug_flags = GDB_ERROR;  /* default is errors only */
int   autoconnect = 1;

char *dssi_path = NULL;
char *ladspa_path = NULL;
char *project_directory = NULL;

int   main_timeout_tick = 0;

snd_seq_event_t midiEventBuffer[EVENT_BUFFER_SIZE]; /* ring buffer */
int midiEventReadIndex = 0, midiEventWriteIndex = 0;

#ifndef MIDI_JACK
static pthread_t midi_thread;
int              midi_thread_running = 0;
pthread_mutex_t  midiEventBufferMutex = PTHREAD_MUTEX_INITIALIZER;
#endif /* MIDI_JACK */

LADSPA_Data get_port_default(const LADSPA_Descriptor *plugin, int port);

void osc_error(int num, const char *m, const char *path);

int osc_message_handler(const char *path, const char *types, lo_arg **argv, int
		      argc, void *data, void *user_data) ;
int osc_debug_handler(const char *path, const char *types, lo_arg **argv, int
		      argc, void *data, void *user_data) ;

void
ghss_debug(int type, const char *format, ...)
{
    va_list args;

    va_start(args, format);
    if (debug_flags & type) {
        fputs(host_name, stderr);
        vfprintf(stderr, format, args);
        fputs("\n", stderr);
    }
    va_end(args);
}

void
signalHandler(int sig)

{
    ghss_debug(GDB_MAIN, ": signal caught, trying to clean up and exit");
    host_exiting = 1;
}

void
setControl(d3h_instance_t *instance, long controlIn, snd_seq_event_t *event)
{
    long port = pluginControlInPortNumbers[controlIn];

    const LADSPA_Descriptor *p = instance->plugin->descriptor->LADSPA_Plugin;

    LADSPA_PortRangeHintDescriptor d = p->PortRangeHints[port].HintDescriptor;

    LADSPA_Data lb = p->PortRangeHints[port].LowerBound;

    LADSPA_Data ub = p->PortRangeHints[port].UpperBound;

    float value = (float)event->data.control.value;

    if (LADSPA_IS_HINT_SAMPLE_RATE(d)) {
        lb *= sample_rate;
        ub *= sample_rate;
    }

    if (!LADSPA_IS_HINT_BOUNDED_BELOW(d)) {
	if (!LADSPA_IS_HINT_BOUNDED_ABOVE(d)) {
	    /* unbounded: might as well leave the value alone. */
            return;
	} else {
	    /* bounded above only. just shift the range. */
	    value = ub - 127.0f + value;
	}
    } else {
	if (!LADSPA_IS_HINT_BOUNDED_ABOVE(d)) {
	    /* bounded below only. just shift the range. */
	    value = lb + value;
	} else {
	    /* bounded both ends.  more interesting. */
            if (LADSPA_IS_HINT_LOGARITHMIC(d) && lb > 0.0f && ub > 0.0f) {
                lb = logf(lb);
                ub = logf(ub);
                value = lb + ((ub - lb) * value / 127.0f);
                value = expf(value);
            } else {
                value = lb + ((ub - lb) * value / 127.0f);
            }
	}
    }
    if (LADSPA_IS_HINT_INTEGER(d)) {
        value = lrintf(value);
    }

    ghss_debug_rt(GDB_MIDI_CC, ": %s MIDI controller %d=%d -> control in %ld=%f",
                  instance->friendly_name, event->data.control.param,
                  event->data.control.value, controlIn, value);

    pluginControlIns[controlIn] = value;
    pluginPortUpdated[controlIn] = 1;
}

int
audio_callback(jack_nframes_t nframes, void *arg)
{
    int i;
    jack_nframes_t last_frame_time = jack_last_frame_time(jackClient);
    unsigned int last_tick_offset = 0;
#ifdef MIDI_JACK
    void* midi_port_buf = jack_port_get_buffer(midi_input_port, nframes);
    jack_midi_event_t jack_midi_event;
    jack_nframes_t jack_midi_event_index = 0;
    jack_nframes_t jack_midi_event_count = jack_midi_get_event_count(midi_port_buf);
    static snd_seq_event_t jack_seq_event_holder[3];
    snd_seq_event_t *jack_seq_event = NULL, *osc_seq_event = NULL;
    int had_midi_overflow = 0;
#else /* MIDI_JACK */
    int have_full_midi_buffer = 0;
#endif /* MIDI_JACK */
    d3h_instance_t *instance;

    /* Not especially pretty or efficient */

    for (i = 0; i < instance_count; i++) {
        instanceEventCounts[i] = 0;
    }

#ifdef MIDI_JACK

    /* Merge MIDI events arriving via JACK and OSC */
    while (1) {
	snd_seq_event_t *ev;

        /* MIDI events from JACK */
        if (jack_seq_event == NULL && (jack_midi_event_index < jack_midi_event_count)) {

            int count;

            jack_midi_event_get(&jack_midi_event, midi_port_buf, jack_midi_event_index);
            jack_midi_event_index++;

            jack_seq_event = jack_seq_event_holder;
            count = snd_midi_event_encode(alsa_encoder, jack_midi_event.buffer,
                                          jack_midi_event.size, jack_seq_event);
            if (count) {
                jack_seq_event->time.tick = jack_midi_event.time;
                jack_seq_event->dest.client = 0;  /* flag as from MIDI thread */
            } else
                jack_seq_event = NULL;

        } else {
            jack_seq_event = NULL;
        }

        /* MIDI events from OSC */
        if (osc_seq_event == NULL && (midiEventReadIndex != midiEventWriteIndex)) {

            osc_seq_event = &midiEventBuffer[midiEventReadIndex];

            /* de-jitter */
            if (osc_seq_event->time.tick < last_frame_time) {
                osc_seq_event->time.tick = 0;
            } else {
                osc_seq_event->time.tick -= last_frame_time;
                if (osc_seq_event->time.tick > nframes - 1) {
                    if (osc_seq_event->time.tick < nframes * 2) {
                        osc_seq_event->time.tick += last_frame_time;
                        /* !FIX! this is horribly inefficient: */
                        osc_seq_event = NULL;  /* leave this and following events for the next process cycle */
                    } else {
                        osc_seq_event->time.tick = nframes - 1;  /* so we don't block if things get weird */
                    }
                }
            }
            if (osc_seq_event) {
                midiEventReadIndex = (midiEventReadIndex + 1) % EVENT_BUFFER_SIZE;
                if (osc_seq_event->time.tick < last_tick_offset) {
                    osc_seq_event->time.tick = last_tick_offset; /* assure monotonicity */
                } else {
                    last_tick_offset = osc_seq_event->time.tick;
                }
            }

        } else {
            osc_seq_event = NULL;
        }

        if (jack_seq_event && osc_seq_event) {
            if (jack_seq_event->time.tick < jack_seq_event->time.tick) {
                ev = jack_seq_event;
                jack_seq_event = NULL;
            } else {
                ev = osc_seq_event;
                osc_seq_event = NULL;
            }
        } else if (jack_seq_event) {
            ev = jack_seq_event;
            jack_seq_event = NULL;
        } else if (osc_seq_event) {
            ev = osc_seq_event;
            osc_seq_event = NULL;
        } else {
            break;  /* no more events to process */
        }

        if (!snd_seq_ev_is_channel_type(ev)) {
            /* discard non-channel oriented messages */
            continue;
        }

        if (ev->dest.client == 0 &&
            channel2instances[ev->data.note.channel] == NULL) {
            /* discard messages intended for channels we aren't using */
            continue;
        }

        if (ev->dest.client) {
            /* instance-addressed event from OSC message */
            instance = &instances[ev->dest.port];
        } else {
            /* channel-addressed event from MIDI thread */
            instance = channel2instances[ev->data.note.channel];
        }
        while (instance) {
            i = instance->number;

            if (ev->type == SND_SEQ_EVENT_CONTROLLER) {

                int controller = ev->data.control.param;

                ghss_debug_rt(GDB_MIDI_CC, ": %s MIDI CC %d(0x%02x) = %d",
                              instance->friendly_name, controller, controller,
                              ev->data.control.value);

                if (controller == 0) { // bank select MSB

                    instance->pendingBankMSB = ev->data.control.value;

                } else if (controller == 32) { // bank select LSB

                    instance->pendingBankLSB = ev->data.control.value;

                } else if (controller > 0 && controller < MIDI_CONTROLLER_COUNT) {

                    long controlIn = instance->controllerMap[controller];
                    if (controlIn >= 0) {

                        /* controller is mapped to LADSPA port, update the port */
                        setControl(instance, controlIn, ev);

                    } else {

                        /* controller is not mapped, so pass the event through to plugin */
                        if (instanceEventCounts[i] < EVENT_BUFFER_SIZE) {
                            instanceEventBuffers[i][instanceEventCounts[i]] = *ev;
                            instanceEventCounts[i]++;
                        } else
                            had_midi_overflow = 1;
                    }
                }

            } else if (ev->type == SND_SEQ_EVENT_PGMCHANGE) {
            
                instance->pendingProgramChange = ev->data.control.value;
                instance->uiNeedsProgramUpdate = 1;

            } else {

                if (instanceEventCounts[i] < EVENT_BUFFER_SIZE) {
                    instanceEventBuffers[i][instanceEventCounts[i]] = *ev;
                    instanceEventCounts[i]++;
                } else
                    had_midi_overflow = 1;
            }

            instance->midi_activity_tick = main_timeout_tick;

            if (!ev->dest.client)
                instance = instance->channel_next_instance; /* repeat for next instance on this channel, if any */
            else
                break;  /* event is just for this instance */
        }
    }

    if (had_midi_overflow) {
        ghss_debug_rt(GDB_MIDI, " audio_callback: MIDI overflow");
    }
#else /* MIDI_JACK */
    for ( ; midiEventReadIndex != midiEventWriteIndex;
         midiEventReadIndex = (midiEventReadIndex + 1) % EVENT_BUFFER_SIZE) {

	snd_seq_event_t *ev = &midiEventBuffer[midiEventReadIndex];
        jack_nframes_t previous_frame_time;

        if (!snd_seq_ev_is_channel_type(ev)) {
            /* discard non-channel oriented messages */
            continue;
        }

        if (ev->dest.client == 0 &&
            channel2instances[ev->data.note.channel] == NULL) {
            /* discard messages intended for channels we aren't using */
            continue;
        }

        /* MIDI event de-jittering:
         * The MIDI thread sets ev->time.tick to the JACK rolling frame time
         * of the event's arrival, and subtracting (jack_last_frame_time() -
         * nframes) from that gives an approximate frame offset relative to
         * the current cycle.  With some clipping, we use that as this cycle's
         * frame offset. */
        /* !FIX! test this with recent JACK SVN! */
        previous_frame_time = last_frame_time - nframes;
        if (ev->time.tick < previous_frame_time) {
            ev->time.tick = 0;
        } else {
            ev->time.tick -= previous_frame_time;
            if (ev->time.tick > nframes - 1) {
                if (ev->time.tick < nframes * 2) {
                    ev->time.tick += previous_frame_time;
                    break;  /* leave this and following events for the next process cycle */
                } else {
                    ev->time.tick = nframes - 1;  /* so we don't block if things get weird */
                }
            }
        }
        if (ev->time.tick < last_tick_offset) {
            ev->time.tick = last_tick_offset; /* assure monotonicity */
        } else {
            last_tick_offset = ev->time.tick;
        }

        if (ev->dest.client) {
            /* instance-addressed event from OSC message */
            instance = &instances[ev->dest.port];
        } else {
            /* channel-addressed event from MIDI thread */
            instance = channel2instances[ev->data.note.channel];
        }
        while (instance) {
            i = instance->number;

            if (ev->type == SND_SEQ_EVENT_CONTROLLER) {

                int controller = ev->data.control.param;

                ghss_debug_rt(GDB_MIDI_CC, ": %s MIDI CC %d(0x%02x) = %d",
                              instance->friendly_name, controller, controller,
                              ev->data.control.value);

                if (controller == 0) { // bank select MSB

                    instance->pendingBankMSB = ev->data.control.value;

                } else if (controller == 32) { // bank select LSB

                    instance->pendingBankLSB = ev->data.control.value;

                } else if (controller > 0 && controller < MIDI_CONTROLLER_COUNT) {

                    long controlIn = instance->controllerMap[controller];
                    if (controlIn >= 0) {

                        /* controller is mapped to LADSPA port, update the port */
                        setControl(instance, controlIn, ev);

                    } else {

                        /* controller is not mapped, so pass the event through to plugin */
                        instanceEventBuffers[i][instanceEventCounts[i]] = *ev;
                        instanceEventCounts[i]++;
                    }
                }

            } else if (ev->type == SND_SEQ_EVENT_PGMCHANGE) {
            
                instance->pendingProgramChange = ev->data.control.value;
                instance->uiNeedsProgramUpdate = 1;

            } else {

                instanceEventBuffers[i][instanceEventCounts[i]] = *ev;
                instanceEventCounts[i]++;
            }

            if (instanceEventCounts[i] == EVENT_BUFFER_SIZE)
                have_full_midi_buffer = 1;

            instance->midi_activity_tick = main_timeout_tick;

            if (!ev->dest.client)
                instance = instance->channel_next_instance; /* repeat for next instance on this channel, if any */
            else
                break;  /* event is just for this instance */
        }

        /* stop processing incoming MIDI if an instance's event buffer
         * became full. */
        if (have_full_midi_buffer)
            break;
    }
#endif /* MIDI_JACK */

    /* process pending program changes */
    for (i = 0; i < instance_count; i++) {
        instance = &instances[i];

        if (instance->pendingProgramChange >= 0) {

            int pc = instance->pendingProgramChange;
            int msb = instance->pendingBankMSB;
            int lsb = instance->pendingBankLSB;

            //!!! gosh, I don't know this -- need to check with the specs:
            // if you only send one of MSB/LSB controllers, should the
            // other go to zero or remain as it was?  Assume it remains as
            // it was, for now.

            if (lsb >= 0) {
                if (msb >= 0) {
                    instance->currentBank = lsb + 128 * msb;
                } else {
                    instance->currentBank = lsb + 128 * (instance->currentBank / 128);
                }
            } else if (msb >= 0) {
                instance->currentBank = (instance->currentBank % 128) + 128 * msb;
            }

            instance->currentProgram = pc;

            instance->pendingProgramChange = -1;
            instance->pendingBankMSB = -1;
            instance->pendingBankLSB = -1;

            if (instance->plugin->descriptor->select_program) {
                instance->plugin->descriptor->
                    select_program(instanceHandles[instance->number],
                                   instance->currentBank,
                                   instance->currentProgram);
            }
        }
    }

    /* connect input port buffers */
    for (i = 0; i < insTotal; i++) {

	jack_default_audio_sample_t *buffer =
	    jack_port_get_buffer(inputPorts[i], nframes);

        if (buffer != pluginInputBuffers[i]) {
            pluginInputBuffers[i] = buffer;
            instance = pluginAudioInInstances[i];
            instance->plugin->descriptor->LADSPA_Plugin->connect_port
                (instanceHandles[instance->number], pluginAudioInPortNumbers[i],
                 buffer);
        }
    }

    /* call run_multiple_synths(), run_synth() or run() for all instances */
    i = 0;
    while (i < instance_count) {
        instance = &instances[i];
        if (instance->plugin->descriptor->run_multiple_synths) {
            instance->plugin->descriptor->run_multiple_synths
                (instance->plugin->instances,
                 instanceHandles + i,
                 nframes,
                 instanceEventBuffers + i,
                 instanceEventCounts + i);
            i += instance->plugin->instances;
        } else if (instance->plugin->descriptor->run_synth) {
            instance->plugin->descriptor->run_synth(instanceHandles[i],
                                                    nframes,
                                                    instanceEventBuffers[i],
                                                    instanceEventCounts[i]);
            i++;
        } else if (instance->plugin->descriptor->LADSPA_Plugin->run) {
            instance->plugin->descriptor->LADSPA_Plugin->run(instanceHandles[i],
                                                             nframes);
            i++;
        } /* -FIX- else silence buffer? */
    }

    for (i = 0; i < outsTotal; ++i) {

	jack_default_audio_sample_t *buffer =
	    jack_port_get_buffer(outputPorts[i], nframes);

	/* -FIX- this memcpy could be avoided for anything this host is not
         * doing post-plugin processing on */
	memcpy(buffer, pluginOutputBuffers[i], nframes * sizeof(LADSPA_Data));
    }

    return 0;
}

#ifdef JACK_SESSION
int 
session_gui_idle_callback( void *arg )
{
    char *filename;
    char *command;
    jack_session_event_t *session_event = (jack_session_event_t *) arg;

    filename = g_strdup_printf( "%sghostess.cfg", session_event->session_dir );
    command = "/bin/sh ${SESSION_DIR}ghostess.cfg";

    ghss_debug(GDB_MAIN | GDB_GUI, " session_gui_idle_callback: %s to '%s'",
               (session_event->type == JackSessionSaveAndQuit ? "save-and-quit" : "save"),
               filename);

    /* -FIX- is there no way for a client to report a save failure back to JACK? */
    /* int rc = */ write_configuration( filename, session_event->client_uuid );

    g_free(filename);

    session_event->command_line = g_strdup( command );

    jack_session_reply( jackClient, session_event );

    if (session_event->type == JackSessionSaveAndQuit)
	host_exiting = TRUE;

    jack_session_event_free (session_event);

    return 0; /* remove this source */
}

void session_callback( jack_session_event_t *event, void *arg )
{
    g_idle_add( session_gui_idle_callback, event );
}
#endif

#ifndef RTLD_LOCAL
#define RTLD_LOCAL  (0)
#endif

static char *
search_path(const char *paths, const char *name, void **dll)
{
    gchar **elem;
    int i;
    char *path, *file;
    void *handle;
    const char *message;

    elem = g_strsplit(paths, ":", 0);

    for (i = 0; elem[i]; i++) {
        path = elem[i];

        if (strlen(path) == 0)
            continue;
        if (!g_path_is_absolute(path)) {
            ghss_debug(GDB_DSSI, ": ignoring plugin search path relative element '%s'", path);
            continue;
        }

        file = g_build_filename(path, name, NULL);

        handle = dlopen(file, RTLD_NOW |    /* real-time programs should not use RTLD_LAZY */
                              RTLD_LOCAL);  /* do not share symbols across plugins */
        if (handle) {
            ghss_debug(GDB_DSSI, ": '%s' found at '%s'", name, file);
            *dll = handle;
            path = g_strdup(path);
            g_free(file);
            g_strfreev(elem);
            return path;
        }
        
        message = dlerror();
        if (message) {
            ghss_debug(GDB_DSSI, ": dlopen of '%s' failed: %s", file, message);
        } else {
            ghss_debug(GDB_DSSI, ": dlopen of '%s' failed", file);
        }
        
        g_free(file);
    }
    
    g_strfreev(elem);
    return NULL;
}

char *
load(const char *dllName, void **dll) /* returns directory where dll found */
{
    char *path;
    const char *message;

    if (g_path_is_absolute(dllName)) {
	if ((*dll = dlopen(dllName, RTLD_NOW |       /* real-time programs should not use RTLD_LAZY */
                                    RTLD_LOCAL))) {  /* incredibly, some systems default to RTLD_GLOBAL **cough**Apple** */
	    return g_path_get_dirname(dllName);
	} else {
            message = dlerror();
            if (message) {
                ghss_debug(GDB_DSSI, ": dlopen of '%s' failed: %s", dllName, message);
            } else {
                ghss_debug(GDB_DSSI, ": dlopen of '%s' failed", dllName);
            }
	    return NULL;
	}
    }

    if (!dssi_path && !(dssi_path = getenv("DSSI_PATH"))) {
        dssi_path = "/usr/local/lib/dssi:/usr/lib/dssi";
        ghss_debug(GDB_DSSI, " warning: DSSI_PATH not set, defaulting to '%s'", dssi_path);
    }
    if ((path = search_path(dssi_path, dllName, dll)) != NULL)
        return path;

    if (!ladspa_path && !(ladspa_path = getenv("LADSPA_PATH"))) {
        ladspa_path = "/usr/local/lib/ladspa:/usr/lib/ladspa";
        ghss_debug(GDB_DSSI, " warning: LADSPA_PATH not set, defaulting to '%s'", ladspa_path);
    }
    return search_path(ladspa_path, dllName, dll);
}

static void
copy_initial_port_set(instance_template_t *temp, d3h_instance_t *inst)
{
    unsigned long port_count = inst->plugin->descriptor->LADSPA_Plugin->PortCount;
    unsigned long number_set = temp->ports.highest_set + 1;

    inst->have_initial_values = 1;
    inst->initial_value_set = (int *)calloc(sizeof(int), port_count);
    inst->initial_value = (LADSPA_Data *)malloc(sizeof(LADSPA_Data) * port_count);
    memcpy(inst->initial_value_set, temp->ports.set, sizeof(int) * number_set);
    memcpy(inst->initial_value, temp->ports.value, sizeof(LADSPA_Data) * number_set);
}

static instance_template_t *
new_instance_template(void)
{
    instance_template_t *t = (instance_template_t *)calloc(1, sizeof(instance_template_t));
    return t;
}

static void
reset_instance_template(instance_template_t *t)
{
    configure_item_t *c;
    int i;

    /* leave t->channel alone */
    while((c = t->configure_items)) {
        t->configure_items = c->next;
        free(c->key);
        free(c->value);
        free(c);
    }
    t->program_set = 0;
    t->bank = 0;
    t->program = 0;
    t->ports.have_settings = 0;
    t->ports.highest_set = 0;
    for (i = 0; i < t->ports.allocated; i++)
        t->ports.set[i] = 0;
}

static void
free_instance_template(instance_template_t *t)
{
    reset_instance_template(t); /* free configure items */
    if (t->ports.allocated) {
        free(t->ports.set);
        free(t->ports.value);
    }
    free(t);
}

static void
instance_template_set_port(instance_template_t *t, unsigned long port,
                           LADSPA_Data value)
{
    unsigned long n;

    if (port >= t->ports.allocated) {
        n = 256;
        while (n <= port) n <<= 1;
        if (t->ports.allocated) {
            t->ports.set = (int *)realloc(t->ports.set, sizeof(int) * n);
            memset(&t->ports.set[t->ports.allocated], 0, sizeof(int) * (n - t->ports.allocated));
            t->ports.value = (LADSPA_Data *)realloc(t->ports.value, sizeof(LADSPA_Data) * n);
        } else {
            t->ports.set = (int *)calloc(sizeof(int), n);
            t->ports.value = (LADSPA_Data *)malloc(sizeof(LADSPA_Data) * n);
        }
        t->ports.allocated = n;
    }

    t->ports.have_settings = 1;
    if (t->ports.highest_set < port)
        t->ports.highest_set = port;
    t->ports.set[port] = 1;
    t->ports.value[port] = value;
}

void
set_initial_port_settings(d3h_instance_t *instance)
{
    int i, in;

    for (i = 0; i < instance->plugin->descriptor->LADSPA_Plugin->PortCount; i++) {
        if (instance->initial_value_set[i]) {
            in = instance->pluginPortControlInNumbers[i];
            /* ghss_debug_rt(GDB_MAIN, ": %s setting control %lu (in %d) to %f", instance->friendly_name, i, in, instance->initial_value[i]); */
            pluginControlIns[in] = instance->initial_value[i];
            pluginPortUpdated[in] = 1;
        }
    }
}

void
add_configure_item(configure_item_t **head, char *key, char *value)
{
    configure_item_t *item = *head;

    while(item) {
        if (!strcmp(key, item->key))
            break;
        item = item->next;
    }
    if (item) {
        free(item->value);
    } else {
        item = (configure_item_t *)malloc(sizeof(configure_item_t));
        item->next = *head;
        *head = item;
        item->key = strdup(key);
    }
    item->value = strdup(value);
}

void
copy_configure_items(instance_template_t *temp, d3h_instance_t *inst)
{
    configure_item_t *item = temp->configure_items,
                     *newitem;

    while(item) {
        newitem = (configure_item_t *)malloc(sizeof(configure_item_t));
        newitem->next = inst->configure_items;
        inst->configure_items = newitem;
        newitem->key = strdup(item->key);
        newitem->value = strdup(item->value);
        item = item->next;
    }
}

void
escape_for_shell(char **p, const char *text)
{
    int len, i, c;

    if (*p) free(*p);

    if (!text || (len = strlen(text)) == 0) {
        *p = strdup("''");
        return;
    }

    for (i = 0, c = 0; i < len; i++)
        if (text[i] == '\'') c++;

    if (c == 0) {
        *p = (char *)malloc(len + 3);
        sprintf(*p, "'%s'", text);
        return;
    }

    *p = (char *)malloc(len + c * 3 + 3);
    c = 0;
    if (text[0] != '\'') {
        (*p)[c++] = '\'';
    }
    for (i = 0; i < len; i++) {
        if (text[i] == '\'') {
            if (i != 0) (*p)[c++] = '\'';
            (*p)[c++] = '\\';
            (*p)[c++] = '\'';
            if (i != len - 1) (*p)[c++] = '\'';
        } else {
            (*p)[c++] = text[i];
        }
    }
    if (text[len - 1] != '\'') {
        (*p)[c++] = '\'';
    }
    (*p)[c++] = '\0';
}

int
write_configuration(char *filename, const char *uuid)
{
    FILE *fp = NULL;
    int rc = 0;
    int id, instno, i, in, port;
    d3h_instance_t *instance;
    char *arg1 = NULL,
         *arg2 = NULL;
    configure_item_t *item;

    if ((fp = fopen(filename, "w")) == NULL) goto error;
    if (fprintf(fp, "#!/bin/sh\n") < 0) goto error;
    /* -FIX- this shouldn't really be saved in the .ghss: */
    if (dssi_path) {
        escape_for_shell(&arg1, dssi_path);
        if (fprintf(fp, "DSSI_PATH=%s\nexport DSSI_PATH\n", arg1) < 0) goto error;
    }
    if (ladspa_path) {
        escape_for_shell(&arg1, ladspa_path);
        if (fprintf(fp, "LADSPA_PATH=%s\nexport LADSPA_PATH\n", arg1) < 0) goto error;
    }
    if (fprintf(fp, "exec %s \\\n", host_argv0) < 0) goto error;
    if (strcmp(host_name, host_name_default)) {
        escape_for_shell(&arg1, host_name);
        if (fprintf(fp, " -hostname %s \\\n", arg1) < 0) goto error;
    }
    if (project_directory) {
        escape_for_shell(&arg1, project_directory);
        if (fprintf(fp, " -projdir %s \\\n", arg1) < 0) goto error;
    }
    if (uuid) {
        escape_for_shell(&arg1, uuid);
        if (fprintf(fp, " -uuid %s \\\n", arg1) < 0) goto error;
    }
    if (!autoconnect || uuid) {
        if (fprintf(fp, " -noauto \\\n") < 0) goto error;
    }
    for (id = 0; id < instance_count; id++) {
        for (instno = 0; instances[instno].id != id; instno++);
        instance = &instances[instno];

        escape_for_shell(&arg1, instance->friendly_name);
        if (fprintf(fp, "-comment %s \\\n", arg1) < 0) goto error;

        /* chan */
        if (fprintf(fp, " -chan %d \\\n", instance->channel) < 0) goto error;

        /* conf */
        for (item = instance->configure_items; item; item = item->next) {
            if (strcmp(item->key, DSSI_PROJECT_DIRECTORY_KEY)) {  /* skip project directory */
                escape_for_shell(&arg1, item->key);
                escape_for_shell(&arg2, item->value);
                if (fprintf(fp, " -conf %s %s \\\n", arg1, arg2) < 0) goto error;
            }
        }

        /* prog */
        if (instance->plugin->descriptor->select_program) {
            if (fprintf(fp, " -prog %lu %lu \\\n", instance->currentBank, instance->currentProgram) < 0) goto error;
        }

        /* port */
        for (i = 0; i < instance->plugin->controlIns; i++) {
            char buf[G_ASCII_DTOSTR_BUF_SIZE];

            in = i + instance->firstControlIn;
	    port = pluginControlInPortNumbers[in];

            /* always use "." as decimal point */
            g_ascii_formatd(buf, sizeof(buf), "%.6g", pluginControlIns[in]);
            if (fprintf(fp, " -port %d %s \\\n", port, buf) < 0) goto error;
        }

        /* soname:label */
        escape_for_shell(&arg1, instance->plugin->dll->name);
        escape_for_shell(&arg2, instance->plugin->label);
        if (fprintf(fp, " %s:%s \\\n", arg1, arg2) < 0) goto error;
    }
    if (fprintf(fp, "\n") < 0) goto error;
    rc = 1;

  error:
    if (arg1) free(arg1);
    if (arg2) free(arg2);
    if (fp)   fclose(fp);

    return rc;
}

int
write_patchlist(char *filename)
{
    FILE *fp = NULL;
    int rc = 0;
    int id, instno, i, j;
    d3h_instance_t *instance;
    char tmp[255];

    if ((fp = fopen(filename, "w")) == NULL) goto error;
    fprintf(fp, "<patchlist>\n");

    for (id = 0; id < instance_count; id++) {
        for (instno = 0; instances[instno].id != id; instno++);
        instance = &instances[instno];

        if (!instance->pluginProgramsValid)
            query_programs(instance);

        for (i = 0; i < instance->pluginProgramCount; i++) {
            /* Replace <, >, and " characters in name (bad XML) */
            strncpy(tmp, instance->pluginPrograms[i].Name, 254);
            tmp[254] = '\0';
            for (j = 0; tmp[j]; j++) 
                if (tmp[j] == '<' || tmp[j] == '>')
                    tmp[j] = ' ';
                else if (tmp[j] =='"')
                    tmp[j] = '\'';
 
            fprintf(fp, "<patch channel=\"%d\" "
                    "name=\"%s\" bank=\"%d\" program=\"%d\"/>\n",
                    instance->channel,
                    tmp,
                    (int)instance->pluginPrograms[i].Bank,
                    (int)instance->pluginPrograms[i].Program);
        }
    }
    if (fprintf(fp, "</patchlist>\n") < 0) goto error;
    rc = 1;

  error:
    if (fp)   fclose(fp);

    return rc;
}

static int
instance_sort_cmp(const void *a, const void *b)
{
    d3h_instance_t *ia = (d3h_instance_t *)a;
    d3h_instance_t *ib = (d3h_instance_t *)b;

    if (ia->plugin->number != ib->plugin->number) {
        return ia->plugin->number - ib->plugin->number;
    } else {
        return ia->id - ib->id;
    }
}

void
ui_osc_free(d3h_instance_t *instance)
{
    lo_address_free(instance->ui_osc_address);
    if (instance->ui_osc_source)
        lo_address_free(instance->ui_osc_source);
    free(instance->ui_osc_control_path);
    free(instance->ui_osc_configure_path);
    free(instance->ui_osc_hide_path);
    free(instance->ui_osc_program_path);
    free(instance->ui_osc_quit_path);
    free(instance->ui_osc_rate_path);
    free(instance->ui_osc_show_path);
    instance->ui_osc_address = NULL;
    instance->ui_osc_source = NULL;
    instance->ui_osc_control_path = NULL;
    instance->ui_osc_configure_path = NULL;
    instance->ui_osc_hide_path = NULL;
    instance->ui_osc_program_path = NULL;
    instance->ui_osc_quit_path = NULL;
    instance->ui_osc_rate_path = NULL;
    instance->ui_osc_show_path = NULL;
}

void
start_ui(d3h_instance_t *instance)
{
    const char *directory = instance->plugin->dll->directory;
    const char *dllName = instance->plugin->dll->name;
    const char *label = instance->plugin->descriptor->LADSPA_Plugin->Label;
    struct dirent *entry;
    char *dllBase = strdup(dllName);
    char *subpath, *path, *origPath;
    DIR *subdir;
    char *filename;
    struct stat buf;
    char *osc_url;
    char tag[12];
    int fuzzy;
#if !(defined(__MACH__) && defined(__APPLE__))
    pid_t pid;
#endif

    if (strlen(dllBase) > 3 &&
        !strcasecmp(dllBase + strlen(dllBase) - 3, ".so")) {
        dllBase[strlen(dllBase) - 3] = '\0';
    }

    if (*dllBase == '/') {
        subpath = dllBase;
        dllBase = strdup(strrchr(subpath, '/') + 1);
    } else {
        subpath = (char *)malloc(strlen(directory) + strlen(dllBase) + 2);
        sprintf(subpath, "%s/%s", directory, dllBase);
    }

    for (fuzzy = 0; fuzzy <= 1; ++fuzzy) {

        if (!(subdir = opendir(subpath))) {
            ghss_debug(GDB_UI, " warning: can't open plugin UI directory '%s'", subpath);
            break;
        }

        while ((entry = readdir(subdir))) {
            
            if (entry->d_name[0] == '.') continue;
            if (!strchr(entry->d_name, '_')) continue;

            if (fuzzy) {
                ghss_debug(GDB_UI, ": checking %s against %s", entry->d_name, dllBase);
                if (strlen(entry->d_name) <= strlen(dllBase) ||
                    strncmp(entry->d_name, dllBase, strlen(dllBase)) ||
                    entry->d_name[strlen(dllBase)] != '_')
                    continue;
            } else {
                ghss_debug(GDB_UI, ": checking %s against %s", entry->d_name, label);
                if (strlen(entry->d_name) <= strlen(label) ||
                    strncmp(entry->d_name, label, strlen(label)) ||
                    entry->d_name[strlen(label)] != '_')
                    continue;
            }

            filename = (char *)malloc(strlen(subpath) + strlen(entry->d_name) + 2);
            sprintf(filename, "%s/%s", subpath, entry->d_name);
            
            if (stat(filename, &buf)) {
                ghss_debug(GDB_UI, ": stat of UI %s failed: %s", strerror(errno));
                free(filename);
                continue;
            }
            
            if ((S_ISREG(buf.st_mode) || S_ISLNK(buf.st_mode)) &&
                (buf.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))) {
                
                osc_url = (char *)malloc(strlen(host_osc_url) +
                                         strlen(instance->friendly_name) + 7);
                sprintf(osc_url, "%sdssi/%s", host_osc_url, instance->friendly_name);
                snprintf(tag, 12, "Inst %d", instance->id);

                ghss_debug(GDB_UI, ": trying to execute UI '%s', URL to host will be '%s'",
                           filename, osc_url);

#if !(defined(__MACH__) && defined(__APPLE__))
                /* !FIX! On Darwin, this results in ghostess dying with a SIGPIPE.  Need to
                 * figure out which fds to close before the exec.... */
                if ((pid = fork()) == 0) {
                    if (fork() == 0) {
                        execlp(filename, filename, osc_url, dllName, label, tag, NULL);
                        ghss_debug(GDB_ERROR, ": exec of UI failed: %s", strerror(errno));
                    }
                    _exit(1);
                } else if (pid > 0) {
                    waitpid(pid, NULL, 0);
                }
#else
		if (fork() == 0) {
		    execlp(filename, filename, osc_url, dllName, label, tag, NULL);
                    ghss_debug(GDB_ERROR, ": exec of UI failed: %s", strerror(errno));
		    exit(1);
		}
#endif /* Darwin */

                instance->ui_running = 1;
                instance->ui_initial_show_sent = 0;
                
                free(osc_url);
                free(filename);
                closedir(subdir);
                free(subpath);
                free(dllBase);
                return;
            }
            
            free(filename);
        }

        closedir(subdir);
    }   

    free(subpath);
    free(dllBase);

    /* try universal GUI */
    if ((origPath = getenv("PATH"))) {
        path = strdup(origPath);
        origPath = path;

        while ((subpath = strtok(path, ":")) != 0) {

            path = 0;

            if (subpath[0] != '/') {
                ghss_debug(GDB_UI, ": ignoring PATH relative subpath '%s'", subpath);
                continue;
            }

            filename = (char *)malloc(strlen(subpath) + 24);
            sprintf(filename, "%s/ghostess_universal_gui", subpath);

            if (stat(filename, &buf)) {
                free(filename);
                continue;
            }
            
            if ((S_ISREG(buf.st_mode) || S_ISLNK(buf.st_mode)) &&
                (buf.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))) {
                
                osc_url = (char *)malloc(strlen(host_osc_url) +
                                         strlen(instance->friendly_name) + 7);
                sprintf(osc_url, "%sdssi/%s", host_osc_url, instance->friendly_name);
                snprintf(tag, 12, "Inst %d", instance->id);

                ghss_debug(GDB_UI, ": trying to execute universal GUI '%s', URL to host will be '%s'",
                           filename, osc_url);

#if !(defined(__MACH__) && defined(__APPLE__))
                /* !FIX! On Darwin, this results in ghostess dying with a SIGPIPE.  Need to
                 * figure out which fds to close before the exec.... */
                if ((pid = fork()) == 0) {
                    if (fork() == 0) {
                        execlp(filename, filename, osc_url, dllName, label, tag, NULL);
                        ghss_debug(GDB_ERROR, ": exec of universal GUI failed: %s", strerror(errno));
                    }
                    exit(1);
                } else if (pid > 0) {
                    waitpid(pid, NULL, 0);
                }
#else
		if (fork() == 0) {
		    execlp(filename, filename, osc_url, dllName, label, tag, NULL);
                    ghss_debug(GDB_ERROR, ": exec of universal GUI failed: %s", strerror(errno));
		    exit(1);
		}
#endif

                instance->ui_running = 1;
                instance->ui_initial_show_sent = 0;
                
                free(osc_url);
                free(filename);
                return;
            }

            free(filename);
        }

        free(origPath);
    }

    ghss_debug(GDB_UI, ": no UI found for plugin '%s'", label);
}

void
query_programs(d3h_instance_t *instance)
{
    int i;
    const DSSI_Program_Descriptor *descriptor;

    if (instance->pluginProgramsValid)
        return;

    if (!instance->plugin->descriptor->get_program) {
        instance->pluginProgramsValid = 1;
        instance->pluginProgramCount = 0;
        return;
    }

    /* free old lot */
    for (i = 0; i < instance->pluginProgramCount; i++)
        free((void *)instance->pluginPrograms[i].Name);
    instance->pluginProgramCount = 0;

    /* get new */
    i = 0;
    while ((descriptor = instance->plugin->descriptor->
                get_program(instanceHandles[instance->number], i)) != NULL) {

        if (instance->pluginProgramsAlloc <= i) {
            instance->pluginProgramsAlloc += 128;
            instance->pluginPrograms = (DSSI_Program_Descriptor *)
                realloc(instance->pluginPrograms,
                        instance->pluginProgramsAlloc * sizeof(DSSI_Program_Descriptor));
        }

        instance->pluginPrograms[i].Bank = descriptor->Bank;
        instance->pluginPrograms[i].Program = descriptor->Program;
        instance->pluginPrograms[i].Name = strdup(descriptor->Name);
        ghss_debug(GDB_PROGRAM, " %s: program %d is MIDI bank %lu program %lu, named '%s'",
                   instance->friendly_name, i,
                   instance->pluginPrograms[i].Bank,
                   instance->pluginPrograms[i].Program,
                   instance->pluginPrograms[i].Name);
        instance->pluginProgramCount++;
        i++;
    }
    instance->pluginProgramsValid = 1;
}

void free_programs(d3h_instance_t *instance)
{
    int i;

    if (instance->pluginPrograms) {
        for (i = 0; i < instance->pluginProgramCount; i++)
            free((void *)instance->pluginPrograms[i].Name);
        free((char *)instance->pluginPrograms);
    }
    instance->pluginProgramsValid = 0;
    instance->pluginProgramsAlloc = 0;
    instance->pluginProgramCount = 0;
    instance->pluginPrograms = NULL;
}

gint
gtk_timeout_callback(gpointer data)
{
    int i;
    d3h_instance_t *instance;

    /* Race conditions here, because the programs and ports are
       updated from the audio thread.  We at least try to minimise
       trouble by copying out before the expensive OSC call */

    for (i = 0; i < instance_count; i++) {
        instance = &instances[i];
        if (instance->uiNeedsProgramUpdate && instance->pendingProgramChange < 0) {
            int bank = instance->currentBank;
            int program = instance->currentProgram;
            instance->uiNeedsProgramUpdate = 0;
            if (instance->ui_osc_address) {
                lo_send(instance->ui_osc_address, instance->ui_osc_program_path, "ii", bank, program);
            }
        }
    }

    for (i = 0; i < controlInsTotal; ++i) {
        if (pluginPortUpdated[i]) {
            int port = pluginControlInPortNumbers[i];
            float value = pluginControlIns[i];
            instance = pluginControlInInstances[i];
            pluginPortUpdated[i] = 0;
            if (instance->ui_osc_address) {
                lo_send(instance->ui_osc_address, instance->ui_osc_control_path, "if", port, value);
            }
        }
    }

    for (i = 0; i < instance_count; i++)
        update_eyecandy(&instances[i]);

    main_timeout_tick++;

    if (host_exiting) {
        gtk_main_quit();
        return FALSE;
    } else {
        return TRUE;
    }
}

int
main(int argc, char **argv)
{
    gint gtk_timeout_tag;

    d3h_dll_t *dll;
    d3h_plugin_t *plugin;
    instance_template_t *itemplate;
    d3h_instance_t *instance;
    void *pluginObject;
    char *dllName;
    char *label;
    const char **ports;
    char *tmp, *arg0, *arg1;
    int i, reps, j;
    int in, out, controlIn, controlOut;
    jack_status_t status;

    gtk_init(&argc, &argv);

    setsid();
    sigemptyset (&_signals);
    sigaddset(&_signals, SIGHUP);
    sigaddset(&_signals, SIGINT);
    sigaddset(&_signals, SIGQUIT);
    sigaddset(&_signals, SIGPIPE);
    sigaddset(&_signals, SIGTERM);
    sigaddset(&_signals, SIGUSR1);
    sigaddset(&_signals, SIGUSR2);
    pthread_sigmask(SIG_BLOCK, &_signals, 0);

    host_name = host_name_default;

    insTotal = outsTotal = controlInsTotal = controlOutsTotal = 0;

    /* Parse args and report usage */

    if (argc < 2) {
#ifdef HAVE_CONFIG_H
	fprintf(stderr, "%s %s\n", host_name, VERSION);
#else
	fprintf(stderr, "%s\n", host_name);
#endif
	fprintf(stderr, "Copyright (C) 2006-2012 Sean Bolton and others.\n");
	fprintf(stderr, "%s comes with ABSOLUTELY NO WARRANTY. This is free software, and you are\n", host_name);
        fprintf(stderr, "welcome to redistribute it under certain conditions; see the file COPYING for details.\n");
#ifdef JACK_SESSION
	fprintf(stderr, "Usage: %s [-debug <level>] [-hostname <hostname>] [-projdir <projdir>] [-uuid <uuid>] [-noauto] [-f <cfgfile>]\n", argv[0]);
#else
	fprintf(stderr, "Usage: %s [-debug <level>] [-hostname <hostname>] [-projdir <projdir>] [-noauto] [-f <cfgfile>]\n", argv[0]);
#endif
        fprintf(stderr, "       [-<n>] [-chan <c>] [-conf <k> <v>] [-prog <b> <p>] [-port <p> <f>] <soname>[:<label>] [...]\n\n");
        fprintf(stderr, "  <level>    Debug information flags, bitfield, 1 = errors only, -1 = all\n");
        fprintf(stderr, "  <hostname> JACK and ALSA client name to use, default \"ghostess\"\n");
        fprintf(stderr, "  <projdir>  DSSI project directory, default none\n");
#ifdef JACK_SESSION
        fprintf(stderr, "  <uuid>     JACK session management UUID, default none\n");
#endif
        fprintf(stderr, "  <cfgfile>  File containing more configuration; same format as command line\n");
        fprintf(stderr, "  <n>        Number of instances of the following plugin to create, default 1\n");
        fprintf(stderr, "  <c>        MIDI channel for following instance, numbered from 0\n");
        fprintf(stderr, "  <k> <v>    Configure item key and value for following instance (repeatable for different keys)\n");
        fprintf(stderr, "  <b> <p>    Bank and program number for following instance\n");
        fprintf(stderr, "  <p> <f>    Port number and value for following instance (repeatable for different ports)\n");
        fprintf(stderr, "  <soname>   Name of DSSI plugin library (*.so) to load\n");
        fprintf(stderr, "  <label>    Name of DSSI plugin within library to instantiate, default first\n");
        /* -FIX- add example? */
	return 2;
    }

    host_argv0 = argv[0];

    ghss_debug(GDB_ALWAYS, ": ghostess starting...");
    
    getarg_init_with_command_line(argc - 1, &argv[1]);
    itemplate = new_instance_template();
    reps = 1;
    while ((arg0 = getarg())) {

        if (!strcmp(arg0, "-debug")) {
            arg0 = getarg();
            if (!arg0 || !strlen(arg0)) {
                getarg_print_possible_error();
                ghss_debug(GDB_ERROR, ": debug level expected after '-debug'");
                return 2;
            }
            debug_flags = atoi(arg0);
            ghss_debug(GDB_MAIN, ": debug level now %08x", debug_flags);
            continue;
        }

        if (!strcmp(arg0, "-hostname")) {
            arg0 = getarg();
            if (!arg0 || !strlen(arg0)) {
                getarg_print_possible_error();
                ghss_debug(GDB_ERROR, ": host name base expected after '-hostname'");
                return 2;
            }
            /* -FIX- check new host name for length, valid characters? */
            if (host_name != host_name_default) free(host_name);
            host_name = strdup(arg0);
            continue;
        }

        if (!strcmp(arg0, "-projdir") || !strcmp(arg0, "-p")) {
            arg0 = getarg();
            if (!arg0 || !strlen(arg0)) {
                getarg_print_possible_error();
                ghss_debug(GDB_ERROR, ": project directory expected after '-projdir'");
                return 2;
            }
            if (project_directory) free(project_directory);
            project_directory = strdup(arg0);
            continue;
        }

        if (!strcmp(arg0, "-uuid")) {
            arg0 = getarg();
            if (!arg0 || !strlen(arg0)) {
                getarg_print_possible_error();
                ghss_debug(GDB_ERROR, ": uuid expected after '-uuid'");
                return 2;
            }
            if (jack_session_uuid) free(jack_session_uuid);
            jack_session_uuid = strdup(arg0);
            continue;
        }

        if (!strcmp(arg0, "-noauto") || !strcmp(arg0, "-c")) {
            autoconnect = 0;
            continue;
        }

        if (instance_count >= GHSS_MAX_INSTANCES) {
            ghss_debug(GDB_ERROR, ": too many plugin instances specified (limit is %d)", GHSS_MAX_INSTANCES);
            return 2;
        }

        /* parse repetition count */
        if (arg0[0] == '-' && strlen(arg0) > 1) {
            j = strtol(&arg0[1], &tmp, 10);
            if (*tmp == '\0' && j > 0) {
                reps = j;
                continue;
            }
        }

        /* MIDI channel */
        if (!strcmp(arg0, "-chan")) {
            arg0 = getarg();
            if (!arg0 || !strlen(arg0)) {
                getarg_print_possible_error();
                ghss_debug(GDB_ERROR, ": MIDI channel expected after '-chan'");
                return 2;
            }
            itemplate->channel = strtol(arg0, &tmp, 10);
            if (*tmp != '\0' || itemplate->channel < 0 || itemplate->channel > 15) {
                ghss_debug(GDB_ERROR, ": bad MIDI channel '%s'", arg0);
                return 2;
            }
            continue;
        }

        /* configure item */
        if (!strcmp(arg0, "-conf")) {
            arg0 = getarg();
            if (!arg0 || !strlen(arg0)) {
                getarg_print_possible_error();
                ghss_debug(GDB_ERROR, ": key and value expected after '-conf'");
                return 2;
            }
            arg0 = strdup(arg0);
            arg1 = getarg();
            if (!arg1) {
                getarg_print_possible_error();
                ghss_debug(GDB_ERROR, ": key and value expected after '-conf'");
                free(arg0);
                return 2;
            }
            add_configure_item(&itemplate->configure_items, arg0, arg1);
            free(arg0);
            continue;
        }

        /* bank and program number */
        if (!strcmp(arg0, "-prog")) {
            arg0 = getarg();
            if (!arg0 || !strlen(arg0)) {
                getarg_print_possible_error();
                ghss_debug(GDB_ERROR, ": bank and program number expected after '-prog'");
                return 2;
            }
            arg0 = strdup(arg0);
            arg1 = getarg();
            if (!arg1 || !strlen(arg1)) {
                getarg_print_possible_error();
                ghss_debug(GDB_ERROR, ": bank and program number expected after '-prog'");
                free(arg0);
                return 2;
            }
            itemplate->bank = strtoul(arg0, &tmp, 10);
            if (*tmp != '\0') {
                ghss_debug(GDB_ERROR, ": bad bank number '%s'", arg0);
                free(arg0);
                return 2;
            }
            free(arg0);
            itemplate->program = strtoul(arg1, &tmp, 10);
            if (*tmp != '\0') {
                ghss_debug(GDB_ERROR, ": bad program number '%s'", arg1);
                return 2;
            }
            itemplate->program_set = 1;
            continue;
        }

        /* port setting */
        if (!strcmp(arg0, "-port")) {
            unsigned long port;
            LADSPA_Data value;

            arg0 = getarg();
            if (!arg0 || !strlen(arg0)) {
                getarg_print_possible_error();
                ghss_debug(GDB_ERROR, ": port number and value expected after '-port'");
                return 2;
            }
            arg0 = strdup(arg0);
            arg1 = getarg();
            if (!arg1 || !strlen(arg1)) {
                getarg_print_possible_error();
                ghss_debug(GDB_ERROR, ": port number and value expected after '-port'");
                free(arg0);
                return 2;
            }
            port = strtoul(arg0, &tmp, 10);
            if (*tmp != '\0') {
                ghss_debug(GDB_ERROR, ": bad port number '%s'", arg0);
                free(arg0);
                return 2;
            }
            free(arg0);
            value = g_strtod(arg1, &tmp); /* will try native and "C" locale decimal points */
            if (*tmp != '\0') {
                ghss_debug(GDB_ERROR, ": bad port value '%s'", arg1);
                return 2;
            }
            instance_template_set_port(itemplate, port, value);
            continue;
        }

        /* comment */
        if (!strcmp(arg0, "-comment")) {
            arg0 = getarg();
            if (!arg0) {
                getarg_print_possible_error();
                ghss_debug(GDB_ERROR, ": comment expected after '-comment'");
                return 2;
            }
            continue;
        }

        /* parse dll name, plus a label if supplied */
        tmp = strchr(arg0, ':');
        if (tmp) {
            dllName = calloc(1, tmp - arg0 + 1);
            strncpy(dllName, arg0, tmp - arg0);
            label = strdup(tmp + 1);
        } else {
            dllName = strdup(arg0);
            label = NULL;
        }

        /* check if we've seen this plugin before */
        for (plugin = plugins; plugin; plugin = plugin->next) {
            if (label) {
                if (!strcmp(dllName, plugin->dll->name) &&
                    !strcmp(label,   plugin->label))
                    break;
            } else {
               if (!strcmp(dllName, plugin->dll->name) &&
                   plugin->is_first_in_dll)
                   break;
            }
        }

        if (plugin) {
            /* have already seen this plugin */

            free(dllName);
            free(label);

        } else {
            /* this is a new plugin */

            plugin = (d3h_plugin_t *)calloc(1, sizeof(d3h_plugin_t));
            plugin->number = plugin_count;
            plugin->label = label;

            /* check if we've seen this dll before */
            for (dll = dlls; dll; dll = dll->next) {
                if (!strcmp(dllName, dll->name))
                    break;
            }
            if (!dll) {
                /* this is a new dll */
                dll = (d3h_dll_t *)calloc(1, sizeof(d3h_dll_t));
                dll->name = dllName;

                dll->directory = load(dllName, &pluginObject);
                if (!dll->directory || !pluginObject) {
                    ghss_debug(GDB_ERROR, ": failed to load plugin library %s", dllName);
                    return 1;
                }
                
                dll->descfn = (DSSI_Descriptor_Function)dlsym(pluginObject,
                                                              "dssi_descriptor");
                if (dll->descfn) {
                    dll->is_DSSI_dll = 1;
                } else {
                    dll->descfn = (DSSI_Descriptor_Function)dlsym(pluginObject,
                                                                  "ladspa_descriptor");
                    if (!dll->descfn) {
                        ghss_debug(GDB_ERROR, ": %s is not a DSSI or LADSPA plugin library", dllName);
                        return 1;
                    }
                    dll->is_DSSI_dll = 0;
                }

                dll->next = dlls;
                dlls = dll;
            }
            plugin->dll = dll;

            /* get the plugin descriptor */
            j = 0;
            if (dll->is_DSSI_dll) {
                const DSSI_Descriptor *desc;

                while ((desc = dll->descfn(j++))) {
                    if (!plugin->label ||
                        !strcmp(desc->LADSPA_Plugin->Label, plugin->label)) {
                        plugin->descriptor = desc;
                        break;
                    }
                }
            } else { /* LADSPA plugin; create and use a dummy DSSI descriptor */
                LADSPA_Descriptor *desc;

                plugin->descriptor = (const DSSI_Descriptor *)calloc(1, sizeof(DSSI_Descriptor));
                ((DSSI_Descriptor *)plugin->descriptor)->DSSI_API_Version = 1;

                while ((desc = (LADSPA_Descriptor *)dll->descfn(j++))) {
                    if (!plugin->label ||
                        !strcmp(desc->Label, plugin->label)) {
                        ((DSSI_Descriptor *)plugin->descriptor)->LADSPA_Plugin = desc;
                        break;
                    }
                }
                if (!plugin->descriptor->LADSPA_Plugin) {
                    free((void *)plugin->descriptor);
                    plugin->descriptor = NULL;
                }
            }
            if (!plugin->descriptor) {
                ghss_debug(GDB_ERROR, ": plugin label '%s' not found in library '%s'",
                           plugin->label ? plugin->label : "(none)", dllName);
                return 1;
            }
            plugin->is_first_in_dll = (j = 1);
            if (!plugin->label) {
                plugin->label = strdup(plugin->descriptor->LADSPA_Plugin->Label);
            }

            /* Count number of i/o buffers and ports required */
            plugin->ins = 0;
            plugin->outs = 0;
            plugin->controlIns = 0;
            plugin->controlOuts = 0;
 
            for (j = 0; j < plugin->descriptor->LADSPA_Plugin->PortCount; j++) {

                LADSPA_PortDescriptor pod =
                    plugin->descriptor->LADSPA_Plugin->PortDescriptors[j];

                if (LADSPA_IS_PORT_AUDIO(pod)) {

                    if (LADSPA_IS_PORT_INPUT(pod)) ++plugin->ins;
                    else if (LADSPA_IS_PORT_OUTPUT(pod)) ++plugin->outs;

                } else if (LADSPA_IS_PORT_CONTROL(pod)) {

                    if (LADSPA_IS_PORT_INPUT(pod)) ++plugin->controlIns;
                    else if (LADSPA_IS_PORT_OUTPUT(pod)) ++plugin->controlOuts;
                }
            }

            /* finish up new plugin */
            plugin->instances = 0;
            plugin->next = plugins;
            plugins = plugin;
            plugin_count++;
        }

        /* !FIX! condition initial values here */
#if 0 /* !FIX! */
                    if (instance->have_initial_values &&
                        j <= instance->initial_ports.highest_set &&
                        instance->initial_ports.set[j]) {
                        /* -FIX- port values set should be conditioned to bounds before we get here (like just after plugin is loaded) */
                        const LADSPA_PortRangeHint *prh = &plugin->descriptor->LADSPA_Plugin->PortRangeHints[j];
                        LADSPA_Data lb = prh->LowerBound;
                        LADSPA_Data ub = prh->UpperBound;
                        if (LADSPA_IS_HINT_SAMPLE_RATE(prh->HintDescriptor)) {
                            lb *= sample_rate;
                            ub *= sample_rate;
                        }
                        if (instance->initial_ports.value[j] < lb) {
                            ghss_debug(GDB_ERROR, " %s warning: port %d setting %f adjusted to lower bound %f",
                                       instance->friendly_name, j, instance->initial_ports.value[j], lb);
                            instance->initial_ports.value[j] = lb;
                        } else if (instance->initial_ports.value[j] > ub) {
                            ghss_debug(GDB_ERROR, " %s warning: port %d setting %f adjusted to upper bound %f",
                                       instance->friendly_name, j, instance->initial_ports.value[j], ub);
                            instance->initial_ports.value[j] = ub;
                        }
                        /* -FIX- this should further validate using LADSPA_IS_HINT_INTEGER */
                    }
#endif /* 0 */

        /* set up instances */
        for (j = 0; j < reps; j++) {
            if (instance_count < GHSS_MAX_INSTANCES) {
                instance = &instances[instance_count];

                instance->plugin = plugin;
                instance->id = instance_count;
                instance->channel = itemplate->channel;
                instance->channel_next_instance = NULL;
                tmp = (char *)malloc(strlen(plugin->dll->name) +
                                     strlen(plugin->label) + 9);
                instance->friendly_name = tmp;
                if (strrchr(plugin->dll->name, '/')) {
                    strcpy(tmp, strrchr(plugin->dll->name, '/') + 1);
                } else {
                    strcpy(tmp, plugin->dll->name);
                }
                if (strlen(tmp) > 3 &&
                    !strcasecmp(tmp + strlen(tmp) - 3, ".so")) {
                    tmp = tmp + strlen(tmp) - 3;
                } else {
                    tmp = tmp + strlen(tmp);
                }
                sprintf(tmp, "/%s/inst%02d", plugin->label, instance->id);
                instance->configure_items = NULL;
                copy_configure_items(itemplate, instance);
                instance->pluginProgramsValid = 0;
                instance->pluginProgramsAlloc = 0;
                instance->pluginProgramCount = 0;
                instance->pluginPrograms = NULL;
                if (itemplate->program_set) {
                    instance->currentBank = itemplate->bank;
                    instance->currentProgram = itemplate->program;
                    instance->pendingProgramChange = 0;
                } else {
                    instance->currentBank = 0;
                    instance->currentProgram = 0;
                    instance->pendingProgramChange = -1;
                }
                instance->pendingBankLSB = -1;
                instance->pendingBankMSB = -1;
                if (itemplate->ports.have_settings) {
                    /* -FIX- this should also be conditioned earlier */
                    if (itemplate->ports.highest_set >= plugin->descriptor->LADSPA_Plugin->PortCount) {
                        ghss_debug(GDB_ERROR, ": out-of-range port number %lu given for %s (instance %d)",
                                   itemplate->ports.highest_set, plugin->label, instance_count);
                        return 2;
                    }
                    copy_initial_port_set(itemplate, instance);
                } else {
                    instance->have_initial_values = 0;
                }
                instance->midi_activity_tick = -2;
                instance->ui_running = 0;
                instance->ui_visible = 0;
                instance->ui_initial_show_sent = 0;
                instance->uiNeedsProgramUpdate = 0;
                instance->ui_osc_address = NULL;
                instance->ui_osc_source = NULL;
                instance->ui_osc_configure_path = NULL;
                instance->ui_osc_control_path = NULL;
                instance->ui_osc_hide_path = NULL;
                instance->ui_osc_program_path = NULL;
                instance->ui_osc_quit_path = NULL;
                instance->ui_osc_rate_path = NULL;
                instance->ui_osc_show_path = NULL;

                insTotal += plugin->ins;
                outsTotal += plugin->outs;
                controlInsTotal += plugin->controlIns;
                controlOutsTotal += plugin->controlOuts;

                itemplate->channel = (itemplate->channel + 1) & 15;
                plugin->instances++;
                instance_count++;
            } else {
                ghss_debug(GDB_ERROR, ": too many plugin instances specified");
                return 2;
            }
        }
        reset_instance_template(itemplate);
        reps = 1;
    }
    free_instance_template(itemplate);
    if (getarg_error) {
        getarg_print_possible_error();
        ghss_debug(GDB_ERROR, ": aborting.");
        return 2;
    }
    getarg_cleanup();

    if (!instance_count) {
        ghss_debug(GDB_ERROR, ": no plugin instances specified");
        return 2;
    }

    /* sort array of instances to group them by plugin */
    if (instance_count > 1) {
        qsort(instances, instance_count, sizeof(d3h_instance_t), instance_sort_cmp);
    }

    /* build channel2instances[] while showing what our instances are */
    for (i = 0; i < GHSS_MAX_CHANNELS; i++)
        channel2instances[i] = NULL;
    for (i = 0; i < instance_count; i++) {
        instance = &instances[i];
        instance->number = i;
        if (channel2instances[instance->channel]) {
            instance->channel_next_instance = channel2instances[instance->channel];
        }
        channel2instances[instance->channel] = instance;
        fprintf(stderr, "%s: instance %2d on channel %2d, plugin %2d is '%s'\n",
                host_name, i, instance->channel, instance->plugin->number,
                instance->friendly_name);
    }

    /* Create buffers and JACK client and ports */

#ifdef JACK_SESSION
    jackClient = jack_client_open(host_name, JackSessionID, &status, jack_session_uuid);
#else
    jackClient = jack_client_open(host_name, 0, &status);
#endif
    if (jackClient == 0) {
        fprintf(stderr, "%s: Error: Failed to connect to JACK server\n", host_name);
        return 1;
    }
    if (status & JackNameNotUnique) {
        if (host_name != host_name_default) free(host_name);
        host_name = strdup(jack_get_client_name(jackClient));
    }
    assert(sizeof(jack_default_audio_sample_t) == sizeof(float));
    assert(sizeof(jack_default_audio_sample_t) == sizeof(LADSPA_Data));

    sample_rate = jack_get_sample_rate(jackClient);

    inputPorts = (jack_port_t **)malloc(insTotal * sizeof(jack_port_t *));
    pluginInputBuffers = (float **)calloc(insTotal, sizeof(float *));
    pluginAudioInInstances =
        (d3h_instance_t **)malloc(insTotal * sizeof(d3h_instance_t *));
    pluginAudioInPortNumbers =
        (unsigned long *)malloc(insTotal * sizeof(unsigned long));
    pluginControlIns = (float *)calloc(controlInsTotal, sizeof(float));
    pluginControlInInstances =
        (d3h_instance_t **)malloc(controlInsTotal * sizeof(d3h_instance_t *));
    pluginControlInPortNumbers =
        (unsigned long *)malloc(controlInsTotal * sizeof(unsigned long));
    pluginPortUpdated = (int *)malloc(controlInsTotal * sizeof(int));

    outputPorts = (jack_port_t **)malloc(outsTotal * sizeof(jack_port_t *));
    pluginOutputBuffers = (float **)malloc(outsTotal * sizeof(float *));
    pluginControlOuts = (float *)calloc(controlOutsTotal, sizeof(float));

    instanceHandles = (LADSPA_Handle *)malloc(instance_count *
                                              sizeof(LADSPA_Handle));
    instanceEventBuffers = (snd_seq_event_t **)malloc(instance_count *
                                                      sizeof(snd_seq_event_t *));
    instanceEventCounts = (unsigned long *)malloc(instance_count *
                                                  sizeof(unsigned long));

    for (i = 0; i < instance_count; i++) {
        instanceEventBuffers[i] = (snd_seq_event_t *)malloc(EVENT_BUFFER_SIZE *
                                                            sizeof(snd_seq_event_t));
        instances[i].pluginPortControlInNumbers =
            (int *)malloc(instances[i].plugin->descriptor->LADSPA_Plugin->PortCount *
                          sizeof(int));
    }

    in = 0;
    out = 0;
    for (i = 0; i < instance_count; i++) {
        int inst_in = 0, inst_out = 0;

        instance = &instances[i];
        plugin = instance->plugin;

        for (j = 0; j < plugin->descriptor->LADSPA_Plugin->PortCount; j++) {

            LADSPA_PortDescriptor pod =
                plugin->descriptor->LADSPA_Plugin->PortDescriptors[j];

            if (LADSPA_IS_PORT_AUDIO(pod) && LADSPA_IS_PORT_INPUT(pod)) {

                char portname[65];
                snprintf(portname, 65, "inst%02d %s %s",
                         instance->id, plugin->label,
                         plugin->descriptor->LADSPA_Plugin->PortNames[j]);
                inputPorts[in] = jack_port_register(jackClient, portname,
                                                    JACK_DEFAULT_AUDIO_TYPE,
                                                    JackPortIsInput, 0);
                if (!inputPorts[in]) {
                    snprintf(portname, 65, "inst%02d %s in %d %s",
                             instance->id, plugin->label, inst_in,
                             plugin->descriptor->LADSPA_Plugin->PortNames[j]);
                    inputPorts[in] = jack_port_register(jackClient, portname,
                                                    JACK_DEFAULT_AUDIO_TYPE,
                                                    JackPortIsInput, 0);
                }
                if (!inputPorts[in]) {
                    fprintf(stderr, "%s: Error: Could not create instance '%s' input port '%s'\n",
                            host_name, instance->friendly_name, portname);
                    return 1;
                }
                inst_in++;

                /* JACK port buffers are used directly as the audio input buffers */
                in++;

            } else if (LADSPA_IS_PORT_AUDIO(pod) && LADSPA_IS_PORT_OUTPUT(pod)) {

                char portname[65];
                snprintf(portname, 65, "inst%02d %s %s",
                         instance->id, plugin->label,
                         plugin->descriptor->LADSPA_Plugin->PortNames[j]);
                outputPorts[out] = jack_port_register(jackClient, portname,
                                                      JACK_DEFAULT_AUDIO_TYPE,
                                                      JackPortIsOutput, 0);
                if (!outputPorts[out]) {
                    snprintf(portname, 65, "inst%02d %s out %d %s",
                             instance->id, plugin->label, inst_out,
                             plugin->descriptor->LADSPA_Plugin->PortNames[j]);
                    outputPorts[out] = jack_port_register(jackClient, portname,
                                                          JACK_DEFAULT_AUDIO_TYPE,
                                                          JackPortIsOutput, 0);
                }
                if (!outputPorts[out]) {
                    ghss_debug(GDB_ERROR, " error: Could not create instance '%s' output port '%s'",
                               instance->friendly_name, portname);
                    return 1;
                }
                inst_out++;

                pluginOutputBuffers[out] = 
                    (float *)calloc(jack_get_buffer_size(jackClient), sizeof(float));
                out++;
            }
        }
    }

    jack_set_process_callback(jackClient, audio_callback, 0);
#ifdef JACK_SESSION
    if (jack_set_session_callback) {
        ghss_debug(GDB_MAIN, ": setting JACK session callback");
	jack_set_session_callback(jackClient, session_callback, 0);
    }
#endif

    /* Instantiate plugins */

    for (i = 0; i < instance_count; i++) {
        plugin = instances[i].plugin;
        instanceHandles[i] = plugin->descriptor->LADSPA_Plugin->instantiate
            (plugin->descriptor->LADSPA_Plugin, sample_rate);

        if (!instanceHandles[i]) {
            ghss_debug(GDB_ERROR, " error: Failed to instantiate instance %d, plugin '%s'!",
                       i, plugin->label);
            return 1;
        }
    }

    /* Create OSC thread */

    serverThread = lo_server_thread_new(NULL, osc_error);
    host_osc_url = lo_server_thread_get_url(serverThread);
    ghss_debug(GDB_OSC, ": host OSC URL is %s", host_osc_url);
    lo_server_thread_add_method(serverThread, NULL, NULL, osc_message_handler,
				NULL);
    lo_server_thread_start(serverThread);

    /* set up GTK+ */
    create_windows(host_name, instance_count);

    /* Connect plugins, and build a GUI strip for each */

    for (in = 0; in < controlInsTotal; in++) {
        pluginPortUpdated[in] = 0;
    }

    in = out = controlIn = controlOut = 0;

    for (i = 0; i < instance_count; i++) {   /* i is instance number */
        instance = &instances[i];

        instance->firstControlIn = controlIn;
        for (j = 0; j < MIDI_CONTROLLER_COUNT; j++) {
            instance->controllerMap[j] = -1;
        }

        plugin = instance->plugin;
        for (j = 0; j < plugin->descriptor->LADSPA_Plugin->PortCount; j++) {  /* j is LADSPA port number */

            LADSPA_PortDescriptor pod =
                plugin->descriptor->LADSPA_Plugin->PortDescriptors[j];

            instance->pluginPortControlInNumbers[j] = -1;

            if (LADSPA_IS_PORT_AUDIO(pod)) {

                /* !FIX! also should be conditioned before here */
                if (instance->have_initial_values &&
                    instance->initial_value_set[j]) {
                    ghss_debug(GDB_ERROR, " %s error: port setting given for audio port %d",
                               instance->friendly_name, j);
                    return 2;
                }
                if (LADSPA_IS_PORT_INPUT(pod)) {
                    /* audio input buffers are connect_port()'ed on-the-fly */
                    pluginAudioInInstances[in] = instance;
                    pluginAudioInPortNumbers[in++] = j;
                } else if (LADSPA_IS_PORT_OUTPUT(pod)) {
                    plugin->descriptor->LADSPA_Plugin->connect_port
                        (instanceHandles[i], j, pluginOutputBuffers[out++]);
                }

            } else if (LADSPA_IS_PORT_CONTROL(pod)) {

                if (LADSPA_IS_PORT_INPUT(pod)) {

                    if (plugin->descriptor->get_midi_controller_for_port) {

                        int controller = plugin->descriptor->
                            get_midi_controller_for_port(instanceHandles[i], j);

                        if (controller == 0) {
                            ghss_debug(GDB_ERROR, " error: buggy plugin %s:%s wants mapping for bank MSB",
                                       plugin->dll->name, plugin->label);
                        } else if (controller == 32) {
                            ghss_debug(GDB_ERROR, " error: buggy plugin %s:%s wants mapping for bank LSB",
                                       plugin->dll->name, plugin->label);
                        } else if (DSSI_IS_CC(controller)) {
                            instance->controllerMap[DSSI_CC_NUMBER(controller)]
                                = controlIn;
                        }
                    }

                    pluginControlInInstances[controlIn] = instance;
                    pluginControlInPortNumbers[controlIn] = j;
                    instance->pluginPortControlInNumbers[j] = controlIn;

                    pluginControlIns[controlIn] = get_port_default
                        (plugin->descriptor->LADSPA_Plugin, j);

                    plugin->descriptor->LADSPA_Plugin->connect_port
                        (instanceHandles[i], j, &pluginControlIns[controlIn++]);

                } else if (LADSPA_IS_PORT_OUTPUT(pod)) {
                    /* !FIX! also should be conditioned before here */
                    if (instance->have_initial_values &&
                        instance->initial_value_set[j]) {
                        ghss_debug(GDB_ERROR, ": port setting given for control out port %d, plugin %s, instance %d",
                                   j, plugin->label, instance_count);
                        return 2;
                    }
                    plugin->descriptor->LADSPA_Plugin->connect_port
                        (instanceHandles[i], j, &pluginControlOuts[controlOut++]);
                }
            }
        }  /* 'for (j...'  LADSPA port number */

        /* build GUI strip for plugin */
        instance->strip = create_plugin_strip(main_window, instance);
        gtk_box_pack_start (GTK_BOX (plugin_hbox), instance->strip->container, TRUE, TRUE, 0);
                  
    } /* 'for (i...' instance number */
    assert(in == insTotal);
    assert(out == outsTotal);
    assert(controlIn == controlInsTotal);
    assert(controlOut == controlOutsTotal);

    /* Create MIDI client and port */

    if (!midi_open()) {
        return 1;
    }

    /* Configure and activate plugins */

    for (i = 0; i < instance_count; i++) {
        instance = &instances[i];
        plugin = instance->plugin;

        /* send configure items, if any */
        if (plugin->descriptor->configure) {
            configure_item_t *item;

            if (project_directory) {
                add_configure_item(&instance->configure_items,
                                   DSSI_PROJECT_DIRECTORY_KEY, project_directory);
            }

            for (item = instance->configure_items; item; item = item->next) {
                tmp = plugin->descriptor->configure(instanceHandles[i],
                                                    item->key, item->value);
                if (tmp) {
                    ghss_debug(GDB_DSSI, ": on configure '%s' '%s', plugin '%s' returned '%s'",
                               item->key, item->value, instance->friendly_name, tmp);
                    free(tmp);
                }
            }
        } else {
            if (instance->configure_items) {
                ghss_debug(GDB_ALWAYS, " %s warning: configure items specified for plugin without configure()",
                           instance->friendly_name);
            }
        }

        /* select program */
        if (plugin->descriptor->select_program) {
            if (instance->pendingProgramChange < 0 &&  /* no bank/program specified on command line */
                instance->plugin->descriptor->get_program) {

                const DSSI_Program_Descriptor *descriptor;

                if ((descriptor = instance->plugin->descriptor->
                        get_program(instanceHandles[instance->number], 0)) != NULL) {
        	    /* select program at index 0 */
                    instance->currentBank = descriptor->Bank;
                    instance->currentProgram = descriptor->Program;
                }
            }
            plugin->descriptor->select_program(instanceHandles[i],
                                               instance->currentBank,
                                               instance->currentProgram);
	    instance->uiNeedsProgramUpdate = 1;
        } else {
            if (instance->pendingProgramChange >= 0) {
                ghss_debug(GDB_ALWAYS, " %s warning: program specified for plugin without select_program()",
                           instance->friendly_name);
            }
        }
        instance->pendingProgramChange = -1;

        /* set port values specified on the command line */
        if (instance->have_initial_values) {
            set_initial_port_settings(instance);
            instance->have_initial_values = 0;
        }

        /* activate the instance */
        if (plugin->descriptor->LADSPA_Plugin->activate) {
            plugin->descriptor->LADSPA_Plugin->activate(instanceHandles[i]);
        }
    }

    /* activate JACK and connect ports */

    if (jack_activate(jackClient)) {
        ghss_debug(GDB_ERROR, ": cannot activate JACK client");
        return 1;
    }

    if (autoconnect) {
        /* !FIX! this to more intelligently connect ports: */
        ports = jack_get_ports(jackClient, NULL, "^" JACK_DEFAULT_AUDIO_TYPE "$",
                               JackPortIsPhysical|JackPortIsInput);
        if (ports && ports[0]) {
            for (i = 0, j = 0; i < outsTotal; ++i) {
                if (jack_connect(jackClient, jack_port_name(outputPorts[i]),
                                 ports[j])) {
                    fprintf (stderr, "cannot connect output port %d\n", i);
                }
                if (!ports[++j]) j = 0;
            }
            free(ports);
        }
    }

    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    signal(SIGHUP, signalHandler);
    signal(SIGQUIT, signalHandler);
    pthread_sigmask(SIG_UNBLOCK, &_signals, 0);

#ifndef MIDI_JACK
    /* start MIDI thread */
    if (pthread_create(&midi_thread, NULL, midi_thread_function, NULL)) {
        ghss_debug(GDB_ERROR, ": could not create MIDI thread");
        return 1;
    }
#endif /* MIDI_JACK */

    /* add GTK+ timeout function for GUI updates and OSC output code*/
    gtk_timeout_tag = gtk_timeout_add(50, gtk_timeout_callback, NULL);

    fprintf(stderr, "%s ready\n", host_name);

    /* let GTK+ take it from here */
    gtk_widget_show(main_window);
    host_exiting = 0;
    gtk_main();

    jack_client_close(jackClient);

    /* GTK+ cleanup */
    gtk_timeout_remove(gtk_timeout_tag);

    /* cleanup plugins */
    for (i = 0; i < instance_count; i++) {
        instance = &instances[i];

        if (instance->ui_osc_address) {
            instance->ui_running = 0;
            lo_send(instance->ui_osc_address, instance->ui_osc_quit_path, "");
            ui_osc_free(instance);
        }

        if (instance->plugin->descriptor->LADSPA_Plugin->deactivate) {
            instance->plugin->descriptor->LADSPA_Plugin->deactivate
		(instanceHandles[i]);
	}

        if (instance->plugin->descriptor->LADSPA_Plugin->cleanup) {
            instance->plugin->descriptor->LADSPA_Plugin->cleanup
		(instanceHandles[i]);
	}

        free_programs(instance);
    }

#ifndef MIDI_JACK
    /* clean up MIDI thread */
    /* !FIX! this should become a midi_cleanup() or something.... */
    do {
        sleep(1);  /* this also gives the UIs some time to exit */
        ghss_debug(GDB_MAIN, ": waiting for midi thread to finish");
    } while (midi_thread_running);
    pthread_join(midi_thread, NULL);
#endif /* MIDI_JACK */

    if (host_name != host_name_default) free(host_name);
    if (host_osc_url) free(host_osc_url);

    /* -FIX- jack-dssi-host sends a SIGHUP here to everything in the process
     * group except the main thread, which is clearly overkill.  If we've
     * terminated even somewhat cleanly here, we don't need to do that, but
     * I've seen some lingering processes after crashes -- maybe we might
     * want to SIGHUP just the other threads of this process? */

    return 0;
}

LADSPA_Data get_port_default(const LADSPA_Descriptor *plugin, int port)
{
    LADSPA_Data value;

    LADSPA_PortRangeHint hint = plugin->PortRangeHints[port];
    float lower = hint.LowerBound *
	(LADSPA_IS_HINT_SAMPLE_RATE(hint.HintDescriptor) ? sample_rate : 1.0f);
    float upper = hint.UpperBound *
	(LADSPA_IS_HINT_SAMPLE_RATE(hint.HintDescriptor) ? sample_rate : 1.0f);

    if (!LADSPA_IS_HINT_HAS_DEFAULT(hint.HintDescriptor)) {
	if (!LADSPA_IS_HINT_BOUNDED_BELOW(hint.HintDescriptor) ||
	    !LADSPA_IS_HINT_BOUNDED_ABOVE(hint.HintDescriptor)) {
	    /* No hint, its not bounded, wild guess */
	    return 0.0f;
	}

	if (lower <= 0.0f && upper >= 0.0f) {
	    /* It spans 0.0, 0.0 is often a good guess */
	    return 0.0f;
	}

	/* No clues, return minimum */
	return lower;
    }

    /* Try all the easy ones */
    
    if (LADSPA_IS_HINT_DEFAULT_0(hint.HintDescriptor)) {
	return 0.0f;
    } else if (LADSPA_IS_HINT_DEFAULT_1(hint.HintDescriptor)) {
	return 1.0f;
    } else if (LADSPA_IS_HINT_DEFAULT_100(hint.HintDescriptor)) {
	return 100.0f;
    } else if (LADSPA_IS_HINT_DEFAULT_440(hint.HintDescriptor)) {
	return 440.0f;
    }

    /* All the others require some bounds */

    value = 0.0f;  /* fallback if nothing else matches */
    if (LADSPA_IS_HINT_BOUNDED_BELOW(hint.HintDescriptor)) {
	if (LADSPA_IS_HINT_DEFAULT_MINIMUM(hint.HintDescriptor)) {
	    value = lower;
	}
    }
    if (LADSPA_IS_HINT_BOUNDED_ABOVE(hint.HintDescriptor)) {
	if (LADSPA_IS_HINT_DEFAULT_MAXIMUM(hint.HintDescriptor)) {
	    value = upper;
	}
	if (LADSPA_IS_HINT_BOUNDED_BELOW(hint.HintDescriptor)) {
            if (LADSPA_IS_HINT_LOGARITHMIC(hint.HintDescriptor) &&
                lower > 0.0f && upper > 0.0f) {
                if (LADSPA_IS_HINT_DEFAULT_LOW(hint.HintDescriptor)) {
                    value = expf(logf(lower) * 0.75f + logf(upper) * 0.25f);
                } else if (LADSPA_IS_HINT_DEFAULT_MIDDLE(hint.HintDescriptor)) {
                    value = expf(logf(lower) * 0.5f + logf(upper) * 0.5f);
                } else if (LADSPA_IS_HINT_DEFAULT_HIGH(hint.HintDescriptor)) {
                    value = expf(logf(lower) * 0.25f + logf(upper) * 0.75f);
                }
            } else {
                if (LADSPA_IS_HINT_DEFAULT_LOW(hint.HintDescriptor)) {
                    value = lower * 0.75f + upper * 0.25f;
                } else if (LADSPA_IS_HINT_DEFAULT_MIDDLE(hint.HintDescriptor)) {
                    value = lower * 0.5f + upper * 0.5f;
                } else if (LADSPA_IS_HINT_DEFAULT_HIGH(hint.HintDescriptor)) {
                    value = lower * 0.25f + upper * 0.75f;
                }
            }
	}
    }

    if (LADSPA_IS_HINT_INTEGER(hint.HintDescriptor)) {
        value = lrintf(value);
    }
    return value;
}

void osc_error(int num, const char *msg, const char *path)
{
    ghss_debug(GDB_OSC, ": liblo server error %d in path %s: %s", num, path, msg);
}

int
osc_exiting_handler(d3h_instance_t *instance, lo_arg **argv)
{
    ghss_debug(GDB_OSC, " OSC: got exiting notification from %s",
               instance->friendly_name);

    instance->ui_running = 0;

    if (instance->ui_osc_address) {
        ui_osc_free(instance);
    }

    update_from_exiting(instance);

    return 0;
}

int
osc_midi_handler(d3h_instance_t *instance, lo_arg **argv)
{
    static snd_midi_event_t *alsaCoder = NULL;
    static snd_seq_event_t alsaEncodeBuffer[10];
    long count;
    snd_seq_event_t *ev = &alsaEncodeBuffer[0];

    ghss_debug(GDB_OSC, " OSC: got midi request for %s (%02x %02x %02x %02x)",
           instance->friendly_name, argv[0]->m[0], argv[0]->m[1], argv[0]->m[2], argv[0]->m[3]);

    if (!alsaCoder) {
        if (snd_midi_event_new(10, &alsaCoder)) {
            ghss_debug(GDB_OSC, " OSC midi handler: Failed to initialise ALSA MIDI coder!");
            return 0;
        }
    }

    snd_midi_event_reset_encode(alsaCoder);

    count = snd_midi_event_encode
	(alsaCoder, (argv[0]->m) + 1, 3, alsaEncodeBuffer); /* ignore OSC "port id" in argv[0]->m[0] */

    if (!count || !snd_seq_ev_is_channel_type(ev)) {
        return 0;
    }

    /* flag event as for this instance only */
    ev->dest.client = 1;
    ev->dest.port = instance->number;
    ev->time.tick = 0;
    
    if (ev->type == SND_SEQ_EVENT_NOTEON && ev->data.note.velocity == 0) {
        ev->type =  SND_SEQ_EVENT_NOTEOFF;
    }
        
#ifndef MIDI_JACK
    pthread_mutex_lock(&midiEventBufferMutex);
#endif /* MIDI_JACK */

    if (midiEventReadIndex == midiEventWriteIndex + 1) {

        ghss_debug(GDB_OSC, " OSC midi handler warning: MIDI event buffer overflow!");

    } else if (ev->type == SND_SEQ_EVENT_CONTROLLER &&
               (ev->data.control.param == 0 || ev->data.control.param == 32)) {

        ghss_debug(GDB_OSC, " OSC midi handler warning: %s UI sent bank select controller (should use /program OSC call), ignoring",
                   instance->friendly_name);

    } else if (ev->type == SND_SEQ_EVENT_PGMCHANGE) {

        ghss_debug(GDB_OSC, " OSC midi handler warning: %s UI sent program change (should use /program OSC call), ignoring",
                   instance->friendly_name);

    } else {

        midiEventBuffer[midiEventWriteIndex] = *ev;
        midiEventWriteIndex = (midiEventWriteIndex + 1) % EVENT_BUFFER_SIZE;

    }

#ifndef MIDI_JACK
    pthread_mutex_unlock(&midiEventBufferMutex);
#endif /* MIDI_JACK */

    return 0;
}

int
osc_control_handler(d3h_instance_t *instance, lo_arg **argv)
{
    int port = argv[0]->i;
    LADSPA_Data value = argv[1]->f;

    if (port < 0 || port > instance->plugin->descriptor->LADSPA_Plugin->PortCount) {
	ghss_debug(GDB_OSC, " OSC control handler: %s port number (%d) is out of range",
                   instance->friendly_name, port);
	return 0;
    }
    if (instance->pluginPortControlInNumbers[port] == -1) {
	ghss_debug(GDB_OSC, " OSC control handler: %s port %d is not a control in",
                   instance->friendly_name, port);
	return 0;
    }
    pluginControlIns[instance->pluginPortControlInNumbers[port]] = value;
    ghss_debug(GDB_OSC, " OSC control handler: %s port %d = %f",
               instance->friendly_name, port, value);
    
    return 0;
}

int
osc_program_handler(d3h_instance_t *instance, lo_arg **argv)
{
    int bank = argv[0]->i;
    int program = argv[1]->i;
    int i;
    int found = 0;

    if (debug_flags & GDB_OSC) {
        if (!instance->pluginProgramsValid)
            query_programs(instance);

        for (i = 0; i < instance->pluginProgramCount; ++i) {
            if (instance->pluginPrograms[i].Bank == bank &&
                instance->pluginPrograms[i].Program == program) {
                ghss_debug(GDB_OSC, " OSC program handler: %s setting bank %d, program %d, name %s",
                       instance->friendly_name, bank, program,
                       instance->pluginPrograms[i].Name);
                found = 1;
                break;
            }
        }

        if (!found) {
            ghss_debug(GDB_OSC, " OSC program handler: %s UI requested unknown program: bank %d, program %d: sending to plugin anyway (plugin should ignore it)",
                    instance->friendly_name, bank, program);
        }
    }

    instance->pendingBankMSB = bank / 128;
    instance->pendingBankLSB = bank % 128;
    instance->pendingProgramChange = program;

    return 0;
}

int
osc_configure_handler(d3h_instance_t *instance, lo_arg **argv)
{
    const char *key = (const char *)&argv[0]->s;
    const char *value = (const char *)&argv[1]->s;
    char *message;
    int i;
    d3h_instance_t *inst;

    ghss_debug(GDB_OSC, " osc_configure_handler: UI for '%s' sent '%s', '%s'",
               instance->friendly_name, key, value);

    if (instance->plugin->instances == 1 ||
        strncmp(key, DSSI_GLOBAL_CONFIGURE_PREFIX,
                strlen(DSSI_GLOBAL_CONFIGURE_PREFIX))) { /* single instance */

        if (instance->plugin->descriptor->configure) {

            add_configure_item(&instance->configure_items, (char *)key, (char *)value);

            message = instance->plugin->descriptor->configure
                          (instanceHandles[instance->number], key, value);
            if (message) {
                ghss_debug(GDB_DSSI, ": on configure '%s' '%s', plugin '%s' returned '%s'",
                       key, value, instance->friendly_name, message);
                free(message);
            }
        }

        /* configure invalidates bank and program information */
        instance->pluginProgramsValid = 0;

        return 0;
    }

    /* handle global key for multiple instances */
    i = 0;
    while (instances[i].plugin != instance->plugin) /* find first instance of this plugin */
         i += instances[i].plugin->instances;
    for ( ; instances[i].plugin == instance->plugin; i++) {
        inst = &instances[i];

        if (inst->plugin->descriptor->configure) {

            add_configure_item(&inst->configure_items, (char *)key, (char *)value);

            message = inst->plugin->descriptor->configure(instanceHandles[i],
                                                          key, value);
            if (message) {
                ghss_debug(GDB_DSSI, ": on configure '%s' '%s', plugin '%s' returned '%s'",
                       key, value, inst->friendly_name, message);
                free(message);
            }
        }

        /* configure invalidates bank and program information */
        instance->pluginProgramsValid = 0;

        /* also send to UIs of other instances of this plugin */
        if (i != instance->number && inst->ui_osc_address) {
            lo_send(inst->ui_osc_address, inst->ui_osc_configure_path, "ss",
                    key, value);
        }
    }

    return 0;
}

int
osc_update_handler(d3h_instance_t *instance, lo_arg **argv, lo_address source)
{
    const char *url = (char *)&argv[0]->s;
    const char *path;
    unsigned int i;
    char *host, *port;
    configure_item_t *item;

    printf("%s: OSC: got update request from <%s>\n", host_name, url);

    if (instance->ui_osc_address) lo_address_free(instance->ui_osc_address);
    host = lo_url_get_hostname(url);
    port = lo_url_get_port(url);
    instance->ui_osc_address = lo_address_new(host, port);
    free(host);
    free(port);

    if (instance->ui_osc_source) lo_address_free(instance->ui_osc_source);
    host = (char *)lo_address_get_hostname(source);
    port = (char *)lo_address_get_port(source);
    instance->ui_osc_source = lo_address_new(host, port);

    path = lo_url_get_path(url);

    if (instance->ui_osc_configure_path) free(instance->ui_osc_configure_path);
    instance->ui_osc_configure_path = (char *)malloc(strlen(path) + 11);
    sprintf(instance->ui_osc_configure_path, "%s/configure", path);

    if (instance->ui_osc_control_path) free(instance->ui_osc_control_path);
    instance->ui_osc_control_path = (char *)malloc(strlen(path) + 10);
    sprintf(instance->ui_osc_control_path, "%s/control", path);

    if (instance->ui_osc_hide_path) free(instance->ui_osc_hide_path);
    instance->ui_osc_hide_path = (char *)malloc(strlen(path) + 10);
    sprintf(instance->ui_osc_hide_path, "%s/hide", path);

    if (instance->ui_osc_program_path) free(instance->ui_osc_program_path);
    instance->ui_osc_program_path = (char *)malloc(strlen(path) + 10);
    sprintf(instance->ui_osc_program_path, "%s/program", path);

    if (instance->ui_osc_rate_path) free(instance->ui_osc_rate_path);
    instance->ui_osc_rate_path = (char *)malloc(strlen(path) + 13);
    sprintf(instance->ui_osc_rate_path, "%s/sample-rate", path);

    if (instance->ui_osc_show_path) free(instance->ui_osc_show_path);
    instance->ui_osc_show_path = (char *)malloc(strlen(path) + 10);
    sprintf(instance->ui_osc_show_path, "%s/show", path);

    if (instance->ui_osc_quit_path) free(instance->ui_osc_quit_path);
    instance->ui_osc_quit_path = (char *)malloc(strlen(path) + 10);
    sprintf(instance->ui_osc_quit_path, "%s/quit", path);

    free((char *)path);

    /* Send sample rate */
    lo_send(instance->ui_osc_address, instance->ui_osc_rate_path, "i", lrintf(sample_rate));

    /* Send current configure items */
    for (item = instance->configure_items; item; item = item->next) {
        lo_send(instance->ui_osc_address, instance->ui_osc_configure_path, "ss",
                item->key, item->value);
        /* ghss_debug(GDB_OSC, " OSC: sending %s configure '%s' '%s'", instance->friendly_name, item->key, item->value); */
    }

    /* Send current bank/program  (-FIX- another race...) */
    if (instance->plugin->descriptor->select_program &&
        instance->pendingProgramChange < 0) {
        unsigned long bank = instance->currentBank;
        unsigned long program = instance->currentProgram;
        instance->uiNeedsProgramUpdate = 0;
        lo_send(instance->ui_osc_address, instance->ui_osc_program_path, "ii", bank, program);
    }

    /* Send current control values (-FIX- should send control outs as well) */
    for (i = 0; i < instance->plugin->controlIns; i++) {
        int in = i + instance->firstControlIn;
	int port = pluginControlInPortNumbers[in];
	lo_send(instance->ui_osc_address, instance->ui_osc_control_path, "if", port,
                pluginControlIns[in]);
        /* Avoid overloading the GUI if there are lots and lots of ports */
        if (i % 50 == 49) usleep(300000);
    }

    if (instance->ui_visible && !instance->ui_initial_show_sent) {
        lo_send(instance->ui_osc_address, instance->ui_osc_show_path, "");
        instance->ui_initial_show_sent = 1;
    }

    return 0;
}

int osc_debug_handler(const char *path, const char *types, lo_arg **argv,
                      int argc, void *data, void *user_data)
{
    int i;

    ghss_debug(GDB_OSC, ": got unhandled OSC message:");
    ghss_debug(GDB_OSC, ": path: <%s>", path);
    fflush(stderr);
    for (i=0; i<argc; i++) {
        printf("%s: arg %d '%c' ", host_name, i, types[i]);
        lo_arg_pp(types[i], argv[i]);
        printf("\n");
    }
    fflush(stdout);
    ghss_debug(GDB_OSC, ":");

    return 1;
}

int osc_message_handler(const char *path, const char *types, lo_arg **argv,
                        int argc, void *data, void *user_data)
{
    int i;
    d3h_instance_t *instance = NULL;
    const char *method;
    unsigned int flen = 0;
    lo_address source;
    int send_to_ui = 0;

    if (strncmp(path, "/dssi/", 6))
        return osc_debug_handler(path, types, argv, argc, data, user_data);

    for (i = 0; i < instance_count; i++) {
        flen = strlen(instances[i].friendly_name);
        if (!strncmp(path + 6, instances[i].friendly_name, flen) &&
            *(path + 6 + flen) == '/') { /* avoid matching prefix only */
            instance = &instances[i];
            break;
        }
    }
    if (!instance)
        return osc_debug_handler(path, types, argv, argc, data, user_data);

    method = path + 6 + flen;
    if (*method != '/' || *(method + 1) == 0)
        return osc_debug_handler(path, types, argv, argc, data, user_data);
    method++;

    source = lo_message_get_source((lo_message)data);

    if (instance->ui_osc_source && instance->ui_osc_address) {
        if (strcmp(lo_address_get_hostname(source),
                   lo_address_get_hostname(instance->ui_osc_source)) ||
            strcmp(lo_address_get_port(source),
                   lo_address_get_port(instance->ui_osc_source))) {
            /* This didn't come from our known UI for this plugin,
               so send an update to that as well */
            send_to_ui = 1;
        }
    }

    if (!strcmp(method, "configure") && argc == 2 && !strcmp(types, "ss")) {

        if (send_to_ui) {
            lo_send(instance->ui_osc_address, instance->ui_osc_configure_path, "ss",
                    &argv[0]->s, &argv[1]->s);
        }

        return osc_configure_handler(instance, argv);

    } else if (!strcmp(method, "control") && argc == 2 && !strcmp(types, "if")) {

        if (send_to_ui) {
            lo_send(instance->ui_osc_address, instance->ui_osc_control_path, "if",
                    argv[0]->i, argv[1]->f);
        }

        return osc_control_handler(instance, argv);

    } else if (!strcmp(method, "exiting") && argc == 0) {

        return osc_exiting_handler(instance, argv);

    } else if (!strcmp(method, "midi") && argc == 1 && !strcmp(types, "m")) {

        return osc_midi_handler(instance, argv);

    } else if (!strcmp(method, "program") && argc == 2 && !strcmp(types, "ii")) {

        if (send_to_ui) {
            lo_send(instance->ui_osc_address, instance->ui_osc_program_path, "ii",
                    argv[0]->i, argv[1]->i);
        }
        
        return osc_program_handler(instance, argv);

    } else if (!strcmp(method, "update") && argc == 1 && !strcmp(types, "s")) {

        return osc_update_handler(instance, argv, source);

    }
    return osc_debug_handler(path, types, argv, argc, data, user_data);
}

