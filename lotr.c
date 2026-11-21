
/****************************************************************************

    lotr.c
    the main module


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



/* lotr.h must be included first */
#include "lotr.h"
#include <SDL.h>
#include "cartoon.h"
#include "init.h"
#include "game.h"
#include "gui.h"
#include "lotr_sdl.h"
#include "timing.h"
#include "utils.h"

#include "av.h"
#include "selector.h"

#include <stdlib.h>
#include <string.h>

static void
print_usage(FILE *stream, const char *program)
{
    fprintf(stream, "Usage: %s [--edition EDITION] [--data-dir DIR] [--movie-dir DIR]\n", program);
#ifdef DEMO
    fputs("Edition: demo\n", stream);
#else
    fputs("Editions: standard, enhanced-cd, two-towers\n", stream);
    fputs("--movie-dir is available for Enhanced CD.\n", stream);
#endif
#if defined(LOTR_SDL2_SELECTOR) && !defined(DEMO)
    fputs("Run without options to open the edition selector.\n", stream);
#endif
}


int
main(int argc, char *argv[])
{
    int game_num = -1;
    const char *edition = NULL;
    const char *data_dir = NULL;
    const char *movie_dir = NULL;
    for (int i = 1; i < argc; ++i) {
        const char **value;
        if (strcmp(argv[i], "--help") == 0) {
            print_usage(stdout, argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "--edition") == 0)
            value = &edition;
        else if (strcmp(argv[i], "--data-dir") == 0)
            value = &data_dir;
        else if (strcmp(argv[i], "--movie-dir") == 0)
            value = &movie_dir;
        else {
            fprintf(stderr, "lotr: unknown option '%s'\n", argv[i]);
            print_usage(stderr, argv[0]);
            return 1;
        }
        if (i + 1 == argc || strncmp(argv[i + 1], "--", 2) == 0) {
            fprintf(stderr, "lotr: missing value for %s\n", argv[i]);
            return 1;
        }
        *value = argv[++i];
    }
#if defined(LOTR_SDL2_SELECTOR) && !defined(DEMO)
    if (argc == 1) {
        int choice = lotr_select_edition();
        if (choice <= 0)
            return choice < 0 ? 1 : 0;
    } else
#endif
    if (!lotr_configure_edition(edition, data_dir, movie_dir))
        return 1;


    /* Check if game data is present */
    if (!lotr_file_exists("arts.dat")) {
        fprintf(stderr, "Cannot find file \"arts.dat\"\n");
        fprintf(stderr, "Please copy game data into the directory \"%s\"\n",
                lotr_data_directory());
        exit(1);
    }

    lotr_init();

    if (lotr_current_edition()->volume == 2)
        game_num = gui_ttt_start_dialog();

    game_new();
    if (game_num >= 0)
        game_load(game_num);


#ifndef DEBUG
    cartoon_show_credits(1);
    switch (lotr_current_edition()->intro) {
        case LOTR_INTRO_CARTOON:
            if (game_num < 0)
                cartoon_play("opening");
            break;
        case LOTR_INTRO_CD:
            av_play("sacf");
            av_play("saqf");
            break;
        case LOTR_INTRO_DEMO:
            cartoon_play("democrt0");
            break;
    }
#endif


    game_init_graphics();

    lotr_reset_timer();
    while (!lotr_key_esc()) {
        int frame_time = FRAME_TIME;
        lotr_poll_events();
        game_next_frame();

#ifndef ENABLE_CHEATS
        /* Shift key is used for fast movement while cheating */
#if !PIXEL_PRECISE
        if (lotr_key_shift())
            frame_time /= 4;
#endif
#endif

        lotr_timer(frame_time);
    }


    lotr_close();

    return 0;
}
