/* ghostess - A GUI host for DSSI plugins.
 *
 * Copyright (C) 2005, 2006, 2008, 2012 Sean Bolton and others.
 *
 * This CoreMIDI driver naively follows the Echo.cpp example provided
 * with the OS X Developer Tools.  It works, but it just blindly
 * connects to all available source ports, and doesn't do whatever is
 * needed to appear in the connection dialogs of other CoreMIDI tools.
 * If anyone can help write a better CoreMIDI driver for ghostess,
 * please do!
 *
 * Portions of this file may have come from the following sources:
 * - The DSSI example code, by Chris Cannam and Steve Harris (public
 *     domain).
 * - Ardour, copyright (C) 1999-2002 Paul Davis.
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

#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include <alsa/asoundlib.h>
#include <alsa/seq.h>
#include <jack/jack.h>

#include <CoreMIDI/MIDIServices.h>
#include <CoreFoundation/CFRunLoop.h>

#include "ghostess.h"
#include "midi.h"

static snd_midi_event_t *alsa_encoder = NULL;

static void midi_read_proc(const MIDIPacketList *pktlist, void *refCon, void *connRefCon)
{
    const MIDIPacket *packet;
    static snd_seq_event_t alsa_encode_buffer[10];
    snd_seq_event_t *ev;
    unsigned int i, j, count;

    pthread_mutex_lock(&midiEventBufferMutex);

    packet = pktlist->packet;
    for (j = 0; j < pktlist->numPackets; packet = MIDIPacketNext(packet), ++j) {
        if (!packet->length)
            continue;

        /* -FIX- if (!packet->length > ??) skip it */

        if (debug_flags & GDB_MIDI) {
            switch(packet->length) {
              case 1:
                ghss_debug_rt(GDB_MIDI, " midi_read_proc: got midi message %02x", packet->data[0]);
                break;
              case 2:
                ghss_debug_rt(GDB_MIDI, " midi_read_proc: got midi message %02x %02x", packet->data[0], packet->data[1]);
                break;
              case 3:
                ghss_debug_rt(GDB_MIDI, " midi_read_proc: got midi message %02x %02x %02x", packet->data[0], packet->data[1], packet->data[2]);
                break;
              default:
                ghss_debug_rt(GDB_MIDI, " midi_read_proc: got midi message %02x %02x %02x ...", packet->data[0], packet->data[1], packet->data[2]);
                break;
            }
        }

        count = snd_midi_event_encode(alsa_encoder, packet->data,
                                      packet->length, alsa_encode_buffer);

        for (i = 0; i < count; i++) {
            ev = &alsa_encode_buffer[i];

            if (!snd_seq_ev_is_channel_type(ev))
                continue;

            if (ev->type == SND_SEQ_EVENT_NOTEON && ev->data.note.velocity == 0)
                ev->type =  SND_SEQ_EVENT_NOTEOFF;

            /* -FIX- Ideally, we should probably use packet->timeStamp to figure
             * out how long ago this event was generated, and adjust accordingly.
             * Instead, we take the easy-and-fast route of just restamping the
             * event with the JACK rolling frame time at its arrival, which seems
             * to work pretty well.... */
            ev->time.tick = jack_frame_time(jackClient);

            ev->dest.client = 0;  /* flag as from MIDI thread */

            if (midiEventReadIndex == midiEventWriteIndex + 1) {
                ghss_debug_rt(GDB_MIDI, " midi_read_proc warning: MIDI event buffer overflow!");
                continue;
            }

            midiEventBuffer[midiEventWriteIndex] = *ev;
            midiEventWriteIndex = (midiEventWriteIndex + 1) % EVENT_BUFFER_SIZE;
        }
    }

    pthread_mutex_unlock(&midiEventBufferMutex);
}

int
midi_open(void)
{
    MIDIClientRef client = NULL;
    MIDIPortRef inPort = NULL;
    int i, n;

    /* create client and port */
    // Note: this does create an extra thread!:
    /* !FIX! no error handling? */
    // if (!
    MIDIClientCreate(CFSTR("ghostess"), NULL, NULL, &client); /* !FIX! use host_name something?! */
    // ) {
    //    ghss_debug(GDB_ERROR, ": failed to create CoreMIDI client");
    //    return 0;
    //   }
    // if (!
    MIDIInputPortCreate(client, CFSTR("Input port"), midi_read_proc, NULL, &inPort);
    // ) {
    //    ghss_debug(GDB_ERROR, ": failed to create CoreMIDI input port");
    //    return 0;
    //   }

    /* open connections from all sources */
    // !FIX! this shouldn't just connect to all sources! but if we don't
    // !FIX! connect to something, we use huge CPU!
    n = MIDIGetNumberOfSources();
    ghss_debug(GDB_MIDI, " midi_open: found %d sources\n", n);
    for (i = 0; i < n; ++i) {
        MIDIEndpointRef src = MIDIGetSource(i);
        MIDIPortConnectSource(inPort, src, NULL);
    }
        
    /* Set up ALSA snd_seq_event_t encoder */
    if (!alsa_encoder) {
        if (snd_midi_event_new(10, &alsa_encoder)) {
            ghss_debug(GDB_MIDI, " midi_open: Failed to initialize ALSA MIDI encoder!");
            return 0;
        }
    }

    snd_midi_event_reset_encode(alsa_encoder);

    ghss_debug(GDB_ALWAYS, ": listening using CoreMIDI");

    return 1;
}

void *
midi_thread_function(void *arg)
{
#if 0
    int rc;
    struct sched_param rtparam;

    /* try to get low-priority real-time scheduling */
    memset (&rtparam, 0, sizeof (rtparam));
    rtparam.sched_priority = 1; /* just above SCHED_OTHER */
    if ((rc = pthread_setschedparam (pthread_self(), SCHED_FIFO, &rtparam)) != 0) {
        if (rc == EPERM) {
            ghss_debug_rt(GDB_MIDI, " midi thread: no permission for SCHED_FIFO, continuing...");
        } else {
            ghss_debug_rt(GDB_MIDI, " midi thread: error getting SCHED_FIFO, continuing...");
        }
    }
#endif

    midi_thread_running = 1;

    do { /* while(!host_exiting) */

	/* CFRunLoopRun();  -FIX- does not return, and we don't seem to need it - ???? */
        sleep(1);

    } while(!host_exiting);

    midi_thread_running = 0;

    return NULL;
}

