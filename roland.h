#ifndef LOTR_ROLAND_H
#define LOTR_ROLAND_H

#include <SDL.h>

#if defined(_WIN32) && SDL_MAJOR_VERSION == 2
#define LOTR_ROLAND_OUTPUT 1
int roland_list_ports(void);
unsigned roland_port_count(void);
int roland_port_name(unsigned index, char name[32]);
int roland_activate(const char *port, const char *profile, int fellowship, int strict);
int roland_configure(const char *port, const char *profile, int fellowship);
int roland_enabled(void);
int roland_stop(void);
void roland_close(void);
void roland_play(const Uint8 *data, int size, int loop, int fade_in);
#endif

#endif
