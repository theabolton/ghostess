/* ghostess configuration input/output routines
 *
 * Copyright (C) 2005, 2009 Sean Bolton and others.
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

#include <stdio.h>

#define GETARG_MAX_LENGTH  32767  /* approximate limit of OSC over UDP */

extern char *getarg_error;

void  getarg_print_possible_error(void);
void  getarg_cleanup(void);
void  getarg_init_with_command_line(int argc, char **argv);
int   getarg_init_with_file(char *filename);
char *getarg(void);

typedef struct _getarg_state getarg_state;

struct _argc_state {
    int    argc;
    char **argv;
    int    next_arg;
};

struct _file_state {
    char  *filename;
    FILE  *fh;
    long   next;
};

struct _getarg_state {
    getarg_state *up;
    int           is_file;
    union {
        struct _argc_state argv;
        struct _file_state file;
    } state;
};

