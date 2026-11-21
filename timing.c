/****************************************************************************

    timing.c
    several time functions


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


#include "lotr.h"
#include "timing.h"
#include <SDL.h>



static Uint32 lotr_ticks;

/*
  returns number of second since time 0
 */




/*
  sleep specified number of miliseconds
*/

void
lotr_sleep(int delay)
{
    SDL_Delay(delay);
}


/*
  resets timer
*/

void
lotr_reset_timer(void)
{
    lotr_ticks = SDL_GetTicks();
}


/*
  wait delay milliseconds from the last lotr_timer call
*/

void
lotr_timer(int delay)
{
    Uint32 timer;

    timer = SDL_GetTicks();

    if (!SDL_TICKS_PASSED(timer, lotr_ticks + delay)) {
        /* we wait some time */
        SDL_Delay(lotr_ticks + delay - timer);
        lotr_ticks = lotr_ticks + delay;
    } else {
        lotr_ticks = timer;
    }

}

void
lotr_timer_steady(int delay)
{
    lotr_ticks += delay;
    Uint32 now = SDL_GetTicks();
    if (!SDL_TICKS_PASSED(now, lotr_ticks))
        SDL_Delay(lotr_ticks - now);
}

int
lotr_timer_late(int tolerance)
{
    return SDL_TICKS_PASSED(SDL_GetTicks(), lotr_ticks + tolerance);
}
