/* ghostess universal DSSI user interface
 *
 * Copyright (C) 2005, 2006, 2008 Sean Bolton and others.
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

#ifndef _UNIVERSAL_GUI_H
#define _UNIVERSAL_GUI_H

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gtk/gtk.h>

/* ==== debugging ==== */

/* DSSP_DEBUG bits */
#define GDB_MAIN    16   /* GUI main program flow */
#define GDB_OSC     32   /* GUI OSC handling */
#define GDB_PLUGIN  64   /* DSSI/LADSPA plugin handling */
#define GDB_GUI    128   /* GUI GUI callbacks, updating, etc. */

/* If you want debug information, define DSSP_DEBUG to the XDB_* bits you're
 * interested in getting debug information about, bitwise-ORed together.
 * Otherwise, leave it undefined. */
// #define DSSP_DEBUG (1+8+16+32+64)

#ifdef DSSP_DEBUG

#include <stdio.h>
#define DSSP_DEBUG_INIT(x)
#define GDB_MESSAGE(type, fmt...) { if (DSSP_DEBUG & type) fprintf(stderr, "ghostess uniGUI" fmt); }

#else  /* !DSSP_DEBUG */

#define GDB_MESSAGE(type, fmt...)
#define DSSP_DEBUG_INIT(x)

#endif  /* DSSP_DEBUG */

/* ==== end of debugging ==== */

#define PORT_AUDIO_OUTPUT              0
#define PORT_AUDIO_INPUT               1
#define PORT_CONTROL_OUTPUT            2
#define PORT_CONTROL_INPUT_TOGGLED     3
#define PORT_CONTROL_INPUT_INTEGER     4
#define PORT_CONTROL_INPUT_LOGARITHMIC 5
#define PORT_CONTROL_INPUT_LINEAR      6

typedef struct _port_data_t port_data_t;

struct _port_data_t {
    int         type;  /* one of the PORT_* values above */
    int         bounded;
    int         by_sample_rate;
    LADSPA_Data LowerBound;
    LADSPA_Data UpperBound;
    GtkObject  *adjustment;
    GtkWidget  *widget;
    GtkWidget  *lower_label;
    GtkWidget  *upper_label;
};

#endif /* _UNIVERSAL_GUI_H */

