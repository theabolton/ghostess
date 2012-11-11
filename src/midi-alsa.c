/* ghostess - A GUI host for DSSI plugins.
 *
 * Copyright (C) 2005, 2006, 2008, 2012 Sean Bolton and others.
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

#include <stdlib.h>
#include <errno.h>
#include <pthread.h>

#include <alsa/asoundlib.h>

#include "ghostess.h"

static snd_seq_t *alsaClient;
static int        alsaClient_npfd;
struct pollfd    *alsaClient_pfd;

int alsa_client_id;
int alsa_port_id;

int
midi_open(void)
{
    if (snd_seq_open(&alsaClient, "hw", SND_SEQ_OPEN_DUPLEX, 0) < 0) {
        ghss_debug(GDB_ERROR, ": failed to open ALSA sequencer interface");
	return 0;
    }

    snd_seq_set_client_name(alsaClient, host_name);

    alsa_client_id = snd_seq_client_id(alsaClient);

    if ((alsa_port_id = snd_seq_create_simple_port
	 (alsaClient, "input",
	  SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE, SND_SEQ_PORT_TYPE_APPLICATION)) < 0) {
        ghss_debug(GDB_ERROR, ": failed to create ALSA sequencer port");
	return 0;
    }

    alsaClient_npfd = snd_seq_poll_descriptors_count(alsaClient, POLLIN);
    alsaClient_pfd = (struct pollfd *)calloc(1, alsaClient_npfd * sizeof(struct pollfd));
    snd_seq_poll_descriptors(alsaClient, alsaClient_pfd, alsaClient_npfd, POLLIN);

    ghss_debug(GDB_ALWAYS, ": listening using ALSA MIDI");

    return 1;
}

void *
midi_thread_function(void *arg)
{
    int rc;
    struct sched_param rtparam;
    snd_seq_event_t *ev = 0;

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

    midi_thread_running = 1;

    do { /* while(!host_exiting) */

        rc = poll(alsaClient_pfd, alsaClient_npfd, 500);
        if (rc <= 0) {
            if (rc < 0 && rc != EINTR) {
                ghss_debug_rt(GDB_MIDI, " midi thread: poll error: %s", strerror(errno));
                usleep(500);
            }
            continue;
        }

        pthread_mutex_lock(&midiEventBufferMutex);

        do {
        
            if (snd_seq_event_input(alsaClient, &ev) > 0) {

                if (midiEventReadIndex == midiEventWriteIndex + 1) {
                    ghss_debug_rt(GDB_MIDI, " midi thread: MIDI event buffer overflow!");
                    continue;
                }

                midiEventBuffer[midiEventWriteIndex] = *ev;

                ev = &midiEventBuffer[midiEventWriteIndex];

                /* We don't need to handle EVENT_NOTE here, because ALSA
                   won't ever deliver them on the sequencer queue -- it
                   unbundles them into NOTE_ON and NOTE_OFF when they're
                   dispatched.  We would only need worry about them when
                   retrieving MIDI events from some other source. */

                if (ev->type == SND_SEQ_EVENT_NOTEON && ev->data.note.velocity == 0) {
                    ev->type =  SND_SEQ_EVENT_NOTEOFF;
                }

                /* fprintf(stderr, "midi: flags %02x, tick %u, sec %u nsec %u\n",
                 *         ev->flags, ev->time.tick, ev->time.time.tv_sec, ev->time.time.tv_nsec);
                 * fflush(stderr); */

                /* -FIX- Ideally, we would use ev->time to figure out how long ago
                 * this event was generated, and adjust accordingly. Instead, we
                 * take the easy-and-fast route of just restamping the event with
                 * the JACK rolling frame time at its arrival, which seems to work
                 * pretty well.... */
                /* -FIX- Rosegarden has example of setting up input queue, see
                 * /t/src/example/rosegarden-CVS-20050109/sound/AlsaDriver.cpp */
                /* -FIX- snd_seq_ioctl_get_queue_status, aka snd_seq_MUMBLE_get_queue_status()
                 * should return current queue time, subtract event time from that to get offset
                 * into past that event arrived? */
                ev->time.tick = jack_frame_time(jackClient);

                /* fprintf(stderr, "midi: %u\n", ev->time.tick); fflush(stderr); */

                ev->dest.client = 0;  /* flag as from MIDI thread */

                midiEventWriteIndex = (midiEventWriteIndex + 1) % EVENT_BUFFER_SIZE;
            }
        
        } while (snd_seq_event_input_pending(alsaClient, 0) > 0);

        pthread_mutex_unlock(&midiEventBufferMutex);

    } while(!host_exiting);

    midi_thread_running = 0;

    return NULL;
}

