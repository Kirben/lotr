/* Force-included only in objects linked into the native media test. */
#include <SDL.h>
#ifdef HAVE_SDL_MIXER
#include <SDL_mixer.h>
Mix_Chunk *media_load_sample(Uint8 *data, Uint32 size);
int media_play_channel(int channel, Mix_Chunk *chunk, int loops);
void media_channel_finished(void (SDLCALL *callback)(int));
#define Mix_QuickLoad_RAW media_load_sample
#define Mix_PlayChannel media_play_channel
#define Mix_ChannelFinished media_channel_finished
#endif

int media_poll_event(SDL_Event *event);
void media_delay(Uint32 duration);
void media_present(SDL_Renderer *renderer);
SDL_Window *media_create_window(const char *title, int x, int y, int width, int height, Uint32 flags);
void media_play_av(const char *name);
void media_play_cartoon(char *name);
int media_towers_menu(void);
void media_game_frame(void);
void media_init(void);

#define SDL_PollEvent media_poll_event
#define SDL_Delay media_delay
#define SDL_RenderPresent media_present
#define SDL_CreateWindow media_create_window
#define cartoon_play media_play_cartoon
#define gui_ttt_start_dialog media_towers_menu
#define game_next_frame media_game_frame
#ifdef INTRO_TRACK_AV
#if defined(_WIN32) && defined(HAVE_SDL_MIXER)
#include <windows.h>
#include <mmsystem.h>
MMRESULT media_waveout_write(HWAVEOUT device, WAVEHDR *header, UINT size);
MMRESULT media_waveout_position(HWAVEOUT device, MMTIME *position, UINT size);
#define waveOutWrite media_waveout_write
#define waveOutGetPosition media_waveout_position
#endif
void media_timer_steady(int delay);
#define lotr_timer_steady media_timer_steady
#else
#define av_play media_play_av
#endif
#define lotr_init media_init
