## Process this file with automake to produce Makefile.in

bin_PROGRAMS = ghostess ghostess_universal_gui

if DARWIN
if MIDI_JACK
MIDI_SRCS = midi-jack.c
DARWIN_LD_FLAGS = -framework CoreFoundation
else
MIDI_SRCS = midi-coremidi.c
DARWIN_LD_FLAGS = -framework CoreFoundation -framework CoreMIDI
endif
DARWIN_LD_ADD = -lmx
else
if MIDI_JACK
MIDI_SRCS = midi-jack.c
else
MIDI_SRCS = midi-alsa.c
endif
DARWIN_LD_FLAGS =
DARWIN_LD_ADD =
endif

ghostess_SOURCES = \
	ghostess.c \
	ghostess.h \
	eyecandy.c \
	eyecandy.h \
	getarg.c \
	getarg.h \
	gui_callbacks.c \
	gui_callbacks.h \
	gui_interface.c \
	gui_interface.h \
	midi.h \
	$(MIDI_SRCS)

ghostess_CFLAGS = @GTK_CFLAGS@ $(JACK_CFLAGS) $(AM_CFLAGS)

ghostess_LDFLAGS = $(DARWIN_LD_FLAGS)
ghostess_LDADD = @GTK_LIBS@ $(JACK_LIBS) $(AM_LDFLAGS) -ldl -lm $(DARWIN_LD_ADD)

ghostess_universal_gui_SOURCES = \
	universal_gui.c \
	universal_gui.h \
	gtkknob.c \
	gtkknob.h

ghostess_universal_gui_CFLAGS = @GTK_CFLAGS@ $(AM_CFLAGS)

ghostess_universal_gui_LDFLAGS = $(DARWIN_LD_FLAGS)
ghostess_universal_gui_LDADD = @GTK_LIBS@ $(AM_LDFLAGS) -ldl -lm $(DARWIN_LD_ADD)

