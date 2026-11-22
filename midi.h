/****************************************************************************

    midi.h
    midi music support
    inspiration taken from exult


    Lord of the Rings game engine

    Copyright (C) 2003  Michal Benes

    Lord of the Rings game engine is free software;
    you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This code is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this code; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*****************************************************************************/


#ifndef _MIDI_H
#define _MIDI_H

#include "lotr.h"
#include <stdio.h>

/* controls if midi is disabled */
extern int midi_disabled;

/* stops music return nonzero if music was not playing */
extern int stop_midi(void);

const Uint8 *midi_find_xmidi_events(const Uint8 *data, size_t size, size_t *length, int depth);
int midi_read_length(const Uint8 **data, const Uint8 *end, Uint32 *length);

/* Convert a single XMIDI sequence to a Standard MIDI File. */
extern Uint8 *convert_xmidi(const Uint8 *data, int size, int *midi_size);

/* start music */
extern void play_midi(Uint8 *data, int size, int loop, int fade_in);
/* Takes ownership of the file. */
extern void play_music_file(FILE *file, int loop);
extern int pause_music_for_movie(void);
extern void resume_music_after_movie(int paused);



#endif /* _MIDI_H */
