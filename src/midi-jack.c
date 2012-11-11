/* ghostess - A GUI host for DSSI plugins.
 *
 * Copyright (C) 2005, 2006, 2009, 2012 Sean Bolton and others.
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

#include <jack/jack.h>
#include <jack/midiport.h>

#include "ghostess.h"

int
midi_open(void)
{
    midi_input_port = jack_port_register (jackClient, "midi_in",
                                          JACK_DEFAULT_MIDI_TYPE,
                                          JackPortIsInput, 0);
    if (!midi_input_port) {
        ghss_debug(GDB_ERROR, " midi_open: Failed to create JACK MIDI port!");
	return 0;
    }

    /* Set up ALSA snd_seq_event_t encoder */
    if (!alsa_encoder) {
        if (snd_midi_event_new(3, &alsa_encoder)) {
            ghss_debug(GDB_ERROR, " midi_open: Failed to initialize ALSA MIDI encoder!");
            return 0;
        }
    }

    snd_midi_event_reset_encode(alsa_encoder);

    ghss_debug(GDB_ALWAYS, ": listening using JACK MIDI");

    return 1;
}

