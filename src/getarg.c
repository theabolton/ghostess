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

#define _BSD_SOURCE    1
#define _SVID_SOURCE   1
#define _ISOC99_SOURCE 1

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "ghostess.h"
#include "getarg.h"

char *getarg_error = NULL;

static getarg_state *state_list = NULL;
static char         *argbuf = NULL;

/* ==== getarg command line and configuration file reading ==== */

/*
 * getarg_print_possible_error_to_stderr
 */
void
getarg_print_possible_error(void)
{
    if (getarg_error)
        ghss_debug(GDB_ERROR, " configuration error: %s", getarg_error);
}

/*
 * getarg_pop_state
 */
static void
getarg_pop_state(void)
{
    getarg_state *state;

    if (state_list) {
        state = state_list;
        state_list = state->up;
        if (state->is_file) {
            free(state->state.file.filename);
            fclose(state->state.file.fh);
        }
        free(state);
    }

    if (!state_list && argbuf) {
        free(argbuf);
        argbuf = NULL;
    }
}

/*
 * getarg_cleanup
 */
void
getarg_cleanup(void)
{
    while (state_list)
        getarg_pop_state();
}

/*
 * getarg_init_with_command_line
 *
 * argc must be greater than zero
 */
void
getarg_init_with_command_line(int argc, char **argv)
{
    getarg_state *state;

    getarg_cleanup();  /* free any previous state */

    state = (getarg_state *)calloc(1, sizeof(getarg_state));
    state->up = NULL;
    state->is_file = 0;
    state->state.argv.argc = argc;
    state->state.argv.argv = argv;
    state->state.argv.next_arg = 0;

    state_list = state;

    if (!argbuf)
        argbuf = (char *)malloc(GETARG_MAX_LENGTH + 1);

    getarg_error = NULL;
}

/*
 * getarg_push_file
 */
static int
getarg_push_file(char *filename)
{
    getarg_state *state;
    char *fn = strdup(filename);
    FILE *fh = fopen(filename, "r");

    if (!fh) {
        snprintf(argbuf, 2048, "could not open configuration file '%s': %s",
                 fn, strerror(errno));
        getarg_error = argbuf;
        free(fn);
        return 0;
    }

    state = (getarg_state *)calloc(1, sizeof(getarg_state));
    state->up = state_list;
    state->is_file = 1;
    state->state.file.filename = fn;
    state->state.file.fh = fh;
    state->state.file.next = 0;

    state_list = state;

    if (!argbuf)
        argbuf = (char *)malloc(GETARG_MAX_LENGTH + 1);

    return 1;
}

/*
 * getarg_init_with_file
 */
int
getarg_init_with_file(char *filename)
{
    getarg_cleanup();  /* free any previous state */
    getarg_error = NULL;

    return getarg_push_file(filename);
}

/*
 * getarg_read_file_arg
 */
static int
getarg_read_file_arg(getarg_state *state)
{
    char c;
    int len, done, parse_state;

    if (fseek(state->state.file.fh, state->state.file.next, SEEK_SET) < 0) {
        snprintf(argbuf, 2048, "seek error on configuration file '%s': %s",
                 state->state.file.filename, strerror(errno));
        getarg_error = argbuf;
        return 0;
    }

    len = done = parse_state = 0;
    while (!done) {
        if (len >= GETARG_MAX_LENGTH) {
            snprintf(argbuf, 2048, "argument too long reading configuration file '%s'",
                     state->state.file.filename);
            getarg_error = argbuf;
            return 0;
        }

        c = fgetc(state->state.file.fh);
        if (c == EOF) {
            if (ferror(state->state.file.fh)) {
                snprintf(argbuf, 2048, "error reading configuration file '%s': %s",
                         state->state.file.filename, strerror(errno));
                getarg_error = argbuf;
                return 0;
            } else { /* end of file */
                if (len)
                   break;
                return 0;
            }
        }
        state->state.file.next++;

/* parser states:
 * 0 - between args, no escape pending
 * 1 - between args, escape pending
 * 2 - in arg, not within single quotes or escape pending
 * 3 - in arg, within single quotes
 * 4 - in arg, not within single quotes, with escape pending
 */
        switch (parse_state) {
          default:
          case 0:  /* between args, no escape pending */
            if (c == '\\') {
                parse_state = 1;
                continue;
            } else if (c == ' ' || c == '\t' || c == '\n') {
                continue;
            } else if (c == '\'') {
                parse_state = 3;
                continue;
            }
            parse_state = 2;
            argbuf[len++] = c;
            break;

          case 1:  /* between args, escape pending */
            if (c == ' ' || c == '\t' || c == '\n') {
                parse_state = 0;
                continue;
            }
            parse_state = 2;
            argbuf[len++] = c;
            break;

          case 2:  /* in arg, no escape pending, not within single quotes */
            if (c == ' ' || c == '\t' || c == '\n') {
                done = 1;
                break;
            } else if (c == '\\') {
                parse_state = 4;
                continue;
            } else if (c == '\'') {
                parse_state = 3;
                continue;
            }
            argbuf[len++] = c;
            break;

          case 3:  /* in arg, within single quotes */
            if (c == '\'') {
                parse_state = 2;
                continue;
            }
            argbuf[len++] = c;
            break;

          case 4:  /* in arg, not within single quotes, with escape pending */
            parse_state = 2;
            argbuf[len++] = c;
            break;
        }
    }
    argbuf[len] = '\0';
    return 1;
}

/*
 * getarg_internal
 */
static char *
getarg_internal(void)
{
    getarg_state *state;
    char *arg;

  again:
    state = state_list;
    if (state == NULL)
        return NULL;

    if (state->is_file) {

        if (getarg_read_file_arg(state)) {
            return argbuf;
        } else {
            if (getarg_error) {
                return NULL;
            } else { /* end of file */
                getarg_pop_state();
                goto again;
            }
        }

    } else { /* command line args */

        if (state->state.argv.next_arg < state->state.argv.argc) {
            arg = state->state.argv.argv[state->state.argv.next_arg];
            state->state.argv.next_arg++;
            return arg;
        } else {
            getarg_pop_state();
            goto again;
        }
    }
}

/*
 * getarg
 */
char *
getarg(void)
{
    char *arg;

  again:
    arg = getarg_internal();

    if (!arg || strcmp(arg, "-f"))
        return arg;

    arg = getarg_internal();
    if (!arg || !strlen(arg)) {
        getarg_error = "configuration file name expected after '-f'";
        return NULL;
    }

    if (getarg_push_file(arg))
        goto again;

    return NULL;
}

