/* Windows MIDI output for the original DOS Roland sequences. */
#include "roland.h"

#ifdef LOTR_ROLAND_OUTPUT
#include "midi.h"
#include <windows.h>
#include <mmsystem.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static UINT selected_port = UINT_MAX;
static HMIDIOUT output;
static HANDLE worker, cancel, message_done;

static void
report_midi_error(MMRESULT error)
{
    char message[MAXERRORLENGTH];
    if (midiOutGetErrorTextA(error, message, sizeof(message)) == MMSYSERR_NOERROR)
        fprintf(stderr, "lotr: Roland MIDI: %s\n", message);
    else
        fprintf(stderr, "lotr: Roland MIDI error %u\n", (unsigned)error);
}

unsigned
roland_port_count(void)
{
    return midiOutGetNumDevs();
}

int
roland_port_name(unsigned index, char name[32])
{
    MIDIOUTCAPSA caps;
    if (midiOutGetDevCapsA(index, &caps, sizeof(caps)) != MMSYSERR_NOERROR)
        return 0;
    SDL_strlcpy(name, caps.szPname, 32);
    return 1;
}

int
roland_list_ports(void)
{
    MIDIOUTCAPSA caps;
    UINT count = midiOutGetNumDevs();
    for (UINT i = 0; i < count; ++i) {
        if (midiOutGetDevCapsA(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR)
            printf("%u\t%s\n", (unsigned)i, caps.szPname);
    }
    if (!count)
        puts("No MIDI output ports found.");
    return 0;
}

static UINT
find_port(const char *name)
{
    UINT result = UINT_MAX;
    UINT count = midiOutGetNumDevs();
    for (UINT i = 0; i < count; ++i) {
        MIDIOUTCAPSA caps;
        if (midiOutGetDevCapsA(i, &caps, sizeof(caps)) != MMSYSERR_NOERROR ||
            strcmp(name, caps.szPname))
            continue;
        if (result != UINT_MAX) {
            fprintf(stderr, "lotr: more than one MIDI output is named '%s'\n", name);
            return UINT_MAX;
        }
        result = i;
    }
    return result;
}

int
roland_activate(const char *name, const char *mode, int fellowship, int strict)
{
    selected_port = UINT_MAX;
    if (strlen(name) >= 32 || strpbrk(name, "\r\n")) {
        fputs("lotr: invalid MIDI output name. Use --list-midi-ports.\n", stderr);
        return 0;
    }
    if (!strcmp(mode, "off"))
        return 1;
    if (strcmp(mode, "lapc-i") && strcmp(mode, "mt-32")) {
        fputs("lotr: Roland profile must be lapc-i, mt-32 or off.\n", stderr);
        return 0;
    }
    selected_port = find_port(name);
    if (selected_port == UINT_MAX) {
        fprintf(stderr, "lotr: MIDI output '%s' unavailable. Use --list-midi-ports.\n", name);
        return !strict;
    }
    fprintf(stderr, "lotr: Roland MIDI %s -> %s\n", mode, name);
    if (!fellowship && !strcmp(mode, "mt-32"))
        fputs("lotr: The Two Towers score uses LAPC-I features.\n", stderr);
    return 1;
}

int
roland_configure(const char *port, const char *profile, int fellowship)
{
    return roland_activate(port ? port : "", profile ? profile : port ? "lapc-i" : "off",
                           fellowship, port || profile);
}

int
roland_enabled(void)
{
    return selected_port != UINT_MAX;
}

typedef struct {
    const Uint8 *data;
    const Uint8 *end;
    int repeat;
    unsigned division;
    int fade_in;
} RolandTrack;

static RolandTrack track;

static int
send_sysex(const Uint8 *payload, Uint32 length)
{
    Uint8 *message = malloc((size_t)length + 1);
    if (!message)
        return 0;
    message[0] = 0xf0;
    memcpy(message + 1, payload, length);

    MIDIHDR header = {0};
    header.lpData = (char *)message;
    header.dwBufferLength = length + 1;
    MMRESULT result = midiOutPrepareHeader(output, &header, sizeof(header));
    if (result == MMSYSERR_NOERROR) {
        result = midiOutLongMsg(output, &header, sizeof(header));
        if (result == MMSYSERR_NOERROR) {
            HANDLE events[] = {cancel, message_done};
            while (!(header.dwFlags & MHDR_DONE)) {
                if (WaitForMultipleObjects(2, events, FALSE, INFINITE) == WAIT_OBJECT_0) {
                    midiOutReset(output);
                    /* A driver may reject reset while still owning the buffer. */
                    while (!(header.dwFlags & MHDR_DONE))
                        WaitForSingleObject(message_done, INFINITE);
                    break;
                }
            }
        }
        midiOutUnprepareHeader(output, &header, sizeof(header));
    }
    if (result != MMSYSERR_NOERROR)
        report_midi_error(result);
    free(message);
    return result == MMSYSERR_NOERROR;
}

typedef struct {
    Uint64 tick;
    DWORD message;
} NoteOff;

typedef struct {
    const Uint8 *start;
    Uint64 tick;
    unsigned repeats;
} MilesLoop;

static int
send_short(DWORD message)
{
    MMRESULT result = midiOutShortMsg(output, message);
    if (result != MMSYSERR_NOERROR)
        report_midi_error(result);
    return result == MMSYSERR_NOERROR;
}

/* Init packets and channel defaults from the DOS Miles MT32MPU driver. */
static int
initialize_towers_roland(void)
{
    static const struct {
        Uint8 length;
        Uint8 data[19];
        DWORD settle_ms;
    } setup[] = {
        {10, {0x41,0x10,0x16,0x12,0x7f,0,0,1,0,0xf7}, 169},
        {18, {0x41,0x10,0x16,0x12,0x10,0,0x0d,1,2,3,4,5,6,7,8,9,0x36,0xf7}, 57},
        {18, {0x41,0x10,0x16,0x12,0x10,0,4,4,8,6,4,4,1,0,0,5,0x4c,0xf7}, 57},
        {12, {0x41,0x10,0x16,0x12,0x10,0,1,0,3,2,0x6a,0xf7}, 57}
    };

    for (size_t i = 0; i < SDL_arraysize(setup); ++i) {
        if (!send_sysex(setup[i].data, setup[i].length))
            return 0;
        Sleep(setup[i].settle_ms);
    }

    static const Uint8 controls[][2] = {{7,127}, {1,0}, {10,64}, {11,127}, {64,0}};
    for (size_t i = 0; i < SDL_arraysize(controls); ++i) {
        for (DWORD channel = 1; channel <= 9; ++channel) {
            if (!send_short((0xb0 | channel) | (DWORD)controls[i][0] << 8 |
                             (DWORD)controls[i][1] << 16))
                return 0;
        }
    }

    Sleep(142);
    static const Uint8 programs[] = {0x44,0x30,0x5f,0x4e,0x29,0x03,0x6e,0x7a};
    for (DWORD channel = 1; channel <= 9; ++channel) {
        if (!send_short((0xe0 | channel) | 0x400000))
            return 0;
        if (channel <= SDL_arraysize(programs) &&
            !send_short((0xc0 | channel) | (DWORD)programs[channel - 1] << 8))
            return 0;
    }
    return 1;
}

enum {
    TOWERS_ROLAND_CLOCK = 240,
    TOWERS_ROLAND_VOLUME = 50,
    MILES_VOLUME_TICK = 41,
    TOWERS_FADE_STEP = 120
};

typedef struct {
    Uint64 start;
    Uint64 frequency;
    unsigned channels;
    Uint8 channel_volume[16];
    unsigned gain;
} MilesClock;

static int
wait_for_xmidi_tick(const MilesClock *clock, Uint64 tick)
{
    Uint64 due = clock->start + tick * clock->frequency / TOWERS_ROLAND_CLOCK;
    Uint64 now = SDL_GetPerformanceCounter();
    DWORD delay = due > now ? (DWORD)(((due - now) * 1000 + clock->frequency - 1) /
                                      clock->frequency) : 0;
    return WaitForSingleObject(cancel, delay) == WAIT_TIMEOUT;
}

static int
update_miles_volume(MilesClock *clock, Uint64 tick)
{
    /* MT32MPU advances its fade by 41 tenths of a millisecond per service
       call. The opening ramps 50 percentage points over 600 ms. */
    while (clock->gain < TOWERS_ROLAND_VOLUME) {
        Uint64 step = ((clock->gain + 1) * TOWERS_FADE_STEP - 1) / MILES_VOLUME_TICK;
        if (step >= tick)
            break;
        ++clock->gain;
        if (!clock->channels)
            continue;
        if (!wait_for_xmidi_tick(clock, step))
            return 0;
        for (unsigned channel = 0; channel < 16; ++channel) {
            if (clock->channels & (1u << channel)) {
                DWORD value = clock->channel_volume[channel] * clock->gain / 100;
                if (!send_short((0xb0 | channel) | (7u << 8) | (value << 16)))
                    return 0;
            }
        }
    }
    return wait_for_xmidi_tick(clock, tick);
}

static int
release_xmidi_notes(NoteOff *notes, size_t slots, Uint64 through, MilesClock *clock)
{
    for (;;) {
        size_t slot = slots;
        for (size_t i = 0; i < slots; ++i) {
            if (notes[i].message && notes[i].tick <= through &&
                (slot == slots || notes[i].tick < notes[slot].tick))
                slot = i;
        }
        if (slot == slots)
            return 1;
        if (!update_miles_volume(clock, notes[slot].tick) || !send_short(notes[slot].message))
            return 0;
        notes[slot].message = 0;
    }
}

static void
play_xmidi_track(void)
{
    size_t capacity = (size_t)(track.end - track.data);
    NoteOff *notes = calloc(capacity, sizeof(*notes));
    if (!notes) {
        fputs("lotr: cannot allocate Roland note queue\n", stderr);
        return;
    }

    const Uint8 *event = track.data;
    MilesLoop loops[4];
    unsigned depth = 0;
    unsigned sustain = 0;
    size_t slots = 0;
    Uint64 tick = 0;
    MilesClock clock = {
        .start = SDL_GetPerformanceCounter(),
        .frequency = SDL_GetPerformanceFrequency(),
        .gain = track.fade_in ? 0 : TOWERS_ROLAND_VOLUME
    };

    while (event < track.end) {
        while (event < track.end && *event < 128)
            tick += *event++;
        if (!release_xmidi_notes(notes, slots, tick, &clock))
            goto done;
        if (event == track.end || !update_miles_volume(&clock, tick))
            goto done;

        Uint8 status = *event++;
        if (status == 0xff || status == 0xf0 || status == 0xf7) {
            if (status == 0xff && event == track.end)
                goto malformed;
            Uint8 type = status == 0xff ? *event++ : 0;
            Uint32 length;
            if (!midi_read_length(&event, track.end, &length) || length > (size_t)(track.end - event))
                goto malformed;
            if (status == 0xf0) {
                if (!length || event[length - 1] != 0xf7)
                    goto malformed;
                if (!send_sysex(event, length))
                    goto done;
            }
            if (status == 0xf7)
                goto malformed;

            event += length;
            if (status == 0xff && type == 0x2f) {
                for (size_t i = 0; i < slots; ++i) {
                    if (notes[i].message && notes[i].tick > tick)
                        tick = notes[i].tick;
                }
                if (!release_xmidi_notes(notes, slots, tick, &clock))
                    goto done;
                if (!track.repeat || !tick)
                    goto done;
                event = track.data;
                depth = 0;
            }
            continue;
        }

        int command = status >> 4;
        int size = command == 0xc || command == 0xd ? 1 : 2;
        if (command < 8 || command >= 15 || track.end - event < size)
            goto malformed;
        Uint8 first = *event++;
        Uint8 second = size == 2 ? *event++ : 0;
        if (first >= 128 || second >= 128)
            goto malformed;
        if (command == 0xb && first >= 110 && first <= 119) {
            /* Miles controls are interpreted here or consumed by the original
               game/driver. They are not commands to the synth itself. */
            if (first == 116) {
                if (depth == SDL_arraysize(loops))
                    goto malformed;
                loops[depth++] = (MilesLoop){event, tick, second};
            } else if (first == 117 && depth) {
                MilesLoop *loop = &loops[depth - 1];
                if (second >= 64 && tick > loop->tick && (!loop->repeats || --loop->repeats)) {
                    event = loop->start;
                    loop->tick = tick;
                } else {
                    --depth;
                }
            }
            continue;
        }
        if (command == 9) {
            Uint32 duration;
            if (!midi_read_length(&event, track.end, &duration))
                goto malformed;
            if (second) {
                /* Miles reuses the first free slot and releases ties in slot order. */
                size_t slot = 0;
                while (slot < slots && notes[slot].message)
                    ++slot;
                if (slot == capacity)
                    goto malformed;
                if (slot == slots)
                    ++slots;
                notes[slot] = (NoteOff){tick + duration,
                    (DWORD)(0x80 | (status & 15)) | (DWORD)first << 8};
            }
        }
        if (command == 0xb && first == 7) {
            unsigned channel = status & 15;
            clock.channels |= 1u << channel;
            clock.channel_volume[channel] = second;
            second = (Uint8)(second * clock.gain / 100);
        }
        if (command == 0xb && first == 64) {
            unsigned channel = 1u << (status & 15);
            if (second >= 64)
                sustain |= channel;
            else
                sustain &= ~channel;
        }
        if (!send_short(status | (DWORD)first << 8 | (DWORD)second << 16))
            goto done;
    }

malformed:
    fputs("lotr: invalid Roland XMIDI event stream\n", stderr);
done:
    /* Miles stops queued notes individually and leaves idle channels alone. */
    for (size_t i = 0; i < slots; ++i) {
        if (notes[i].message)
            send_short(notes[i].message);
    }
    for (unsigned channel = 0; sustain && channel < 16; ++channel) {
        if (sustain & (1u << channel))
            send_short((0xb0 | channel) | (64u << 8));
    }
    free(notes);
}

/* DOS v1.3 advances the score on PIT interrupts and drains one MPU byte
   per service at divisor 0x200. See the callbacks at DB8C and E601. */
enum {
    FELLOWSHIP_PIT_HZ = 1193182,
    FELLOWSHIP_SCORE_PERIOD = 0x4000,
    FELLOWSHIP_MPU_PERIOD = 0x200,
    FELLOWSHIP_TICK_DENOMINATOR = 0x6d3,
    FELLOWSHIP_STARTUP_INTERRUPTS = 4
};

typedef struct {
    Uint64 start;
    Uint64 frequency;
    Uint64 score_cycle;
    Uint64 wire_cycle;
    Sint64 credit;
    Sint64 rate;
    unsigned startup_interrupts;
} FellowshipClock;

static void
advance_fellowship_clock(FellowshipClock *clock, Uint32 delta)
{
    clock->credit -= (Sint64)delta * FELLOWSHIP_TICK_DENOMINATOR;
    if (clock->credit < 0) {
        Sint64 interrupts = (-clock->credit + clock->rate - 1) / clock->rate;
        clock->credit += interrupts * clock->rate;
        /* The original's first timed events are four callbacks ahead of
           the SysEx burst, as measured from its MPU-401 output. */
        unsigned startup = interrupts < clock->startup_interrupts ?
                           (unsigned)interrupts : clock->startup_interrupts;
        clock->startup_interrupts -= startup;
        clock->score_cycle += (Uint64)(interrupts - startup) * FELLOWSHIP_SCORE_PERIOD;
    }
}

static int
wait_for_fellowship_message(FellowshipClock *clock, Uint32 bytes)
{
    if (clock->wire_cycle < clock->score_cycle)
        clock->wire_cycle = clock->score_cycle;
    clock->wire_cycle += (Uint64)bytes * FELLOWSHIP_MPU_PERIOD;

    Uint64 whole = clock->wire_cycle / FELLOWSHIP_PIT_HZ;
    Uint64 remainder = clock->wire_cycle % FELLOWSHIP_PIT_HZ;
    Uint64 due = clock->start + whole * clock->frequency +
                 remainder * clock->frequency / FELLOWSHIP_PIT_HZ;
    Uint64 now = SDL_GetPerformanceCounter();
    DWORD delay = due > now ? (DWORD)(((due - now) * 1000 + clock->frequency - 1) /
                                      clock->frequency) : 0;
    return WaitForSingleObject(cancel, delay) == WAIT_TIMEOUT;
}

static int
send_fellowship_sysex(FellowshipClock *clock, const Uint8 *data, Uint32 length,
                      unsigned *channels)
{
    Uint32 prefix = 0;
    while (prefix < length - 1 && data[prefix] < 128)
        ++prefix;
    if (prefix == length - 1)
        return wait_for_fellowship_message(clock, length + 1) && send_sysex(data, length);

    /* Two cues interrupt a SysEx with channel data. The DOS player sends
       the bytes verbatim. WinMM needs the resulting messages separated. */
    Uint8 *packet = malloc((size_t)prefix + 1);
    if (!packet)
        return 0;
    memcpy(packet, data, prefix);
    packet[prefix] = 0xf7;
    int sent = wait_for_fellowship_message(clock, prefix + 1) &&
               send_sysex(packet, prefix + 1);
    free(packet);
    if (!sent)
        return 0;

    Uint8 status = 0;
    for (Uint32 pos = prefix; pos < length - 1;) {
        Uint32 start = pos;
        if (data[pos] >= 128)
            status = data[pos++];
        int command = status >> 4;
        unsigned size = command == 0xc || command == 0xd ? 1 : 2;
        if (command < 8 || command >= 15 || size > length - 1 - pos)
            return 0;
        Uint8 first = data[pos++];
        Uint8 second = size == 2 ? data[pos++] : 0;
        if (first >= 128 || second >= 128)
            return 0;
        *channels |= 1u << (status & 15);
        if (!wait_for_fellowship_message(clock, pos - start) ||
            !send_short(status | (DWORD)first << 8 | (DWORD)second << 16))
            return 0;
    }
    clock->wire_cycle += FELLOWSHIP_MPU_PERIOD;  /* Trailing F7. */
    return 1;
}

static unsigned
play_fellowship_track(void)
{
    const Uint8 *event = track.data;
    const Uint8 *end = track.end;
    Uint8 running_status = 0;
    unsigned channels = 0;
    FellowshipClock clock = {
        .start = SDL_GetPerformanceCounter(),
        .frequency = SDL_GetPerformanceFrequency(),
        .rate = 120 * (Sint64)track.division,
        .startup_interrupts = FELLOWSHIP_STARTUP_INTERRUPTS
    };

    /* The original Roland driver starts each cue with MIDI System Reset. */
    if (!wait_for_fellowship_message(&clock, 1) || !send_short(0xff))
        return channels;
    clock.credit = clock.rate;

    while (event < end) {
        Uint32 delta;
        if (!midi_read_length(&event, end, &delta) || event == end)
            break;
        advance_fellowship_clock(&clock, delta);

        Uint8 status = *event >= 128 ? *event++ : running_status;
        if (status == 0xff || status == 0xf0) {
            running_status = 0;
            if (status == 0xff && event == end)
                break;
            Uint8 type = status == 0xff ? *event++ : 0;
            Uint32 length;
            if (!midi_read_length(&event, end, &length) || length > (size_t)(end - event))
                break;
            if (status == 0xff && type == 0x51 && length == 3) {
                Uint32 tempo = (Uint32)event[0] << 16 | (Uint32)event[1] << 8 | event[2];
                if (!tempo)
                    break;
                clock.rate = (60000000 / tempo) * (Sint64)track.division;
                clock.credit -= clock.credit % FELLOWSHIP_TICK_DENOMINATOR;
            }
            if (status == 0xff && type == 0x2f) {
                if (!wait_for_fellowship_message(&clock, 0) ||
                    !track.repeat || !clock.score_cycle)
                    return channels;
                event = track.data;
                clock.rate = 120 * (Sint64)track.division;
                clock.credit = clock.rate;
                clock.startup_interrupts = FELLOWSHIP_STARTUP_INTERRUPTS;
                if (!wait_for_fellowship_message(&clock, 1) || !send_short(0xff))
                    return channels;
                continue;
            }
            if (status == 0xf0) {
                if (!length || event[length - 1] != 0xf7)
                    break;
                if (!send_fellowship_sysex(&clock, event, length, &channels))
                    return channels;
            }
            event += length;
            continue;
        }

        int command = status >> 4;
        int size = command == 0xc || command == 0xd ? 1 : 2;
        if (command < 8 || command >= 15 || end - event < size)
            break;
        Uint8 first = *event++;
        Uint8 second = size == 2 ? *event++ : 0;
        if (first >= 128 || second >= 128)
            break;
        running_status = status;
        channels |= 1u << (status & 15);
        if (!wait_for_fellowship_message(&clock, size + 1) ||
            !send_short(status | (DWORD)first << 8 | (DWORD)second << 16))
            return channels;
    }
    fputs("lotr: invalid Fellowship MIDI event stream\n", stderr);
    return channels;
}

static void
reset_roland_channels(unsigned channels)
{
    if (midiOutReset(output) != MMSYSERR_NOTSUPPORTED)
        return;

    /* Munt's WinMM forwarding driver does not implement reset. */
    for (unsigned channel = 0; channel < 16; ++channel) {
        if (channels & (1u << channel)) {
            send_short((0xb0 | channel) | (64u << 8));  /* Sustain off */
            send_short((0xb0 | channel) | (123u << 8)); /* All notes off */
        }
    }
}

static DWORD WINAPI
play_track(void *unused)
{
    (void)unused;
    MMRESULT timer = timeBeginPeriod(1);
    if (track.division)
        reset_roland_channels(play_fellowship_track());
    else
        play_xmidi_track();
    if (timer == TIMERR_NOERROR)
        timeEndPeriod(1);
    return 0;
}

static void
close_roland_device(void)
{
    if (output) {
        midiOutClose(output);
        output = NULL;
    }
    if (cancel)
        CloseHandle(cancel);
    if (message_done)
        CloseHandle(message_done);
    cancel = message_done = NULL;
}

int
roland_stop(void)
{
    int playing = 0;
    if (worker) {
        playing = WaitForSingleObject(worker, 0) == WAIT_TIMEOUT;
        SetEvent(cancel);
        WaitForSingleObject(worker, INFINITE);
        CloseHandle(worker);
        worker = NULL;
    }
    if (track.division)
        close_roland_device();
    return playing;
}

void
roland_close(void)
{
    roland_stop();
    close_roland_device();
}

void
roland_play(const Uint8 *data, int size, int loop, int fade_in)
{
    size_t length;
    unsigned division = 0;
    const Uint8 *events;
    if (size > 22 && !memcmp(data, "MThd\0\0\0\6\0\0\0\1", 12) &&
        !memcmp(data + 14, "MTrk", 4)) {
        division = (unsigned)data[12] << 8 | data[13];
        length = (Uint32)data[18] << 24 | (Uint32)data[19] << 16 |
                 (Uint32)data[20] << 8 | data[21];
        if (!division || division >= 0x8000 || length > (size_t)(size - 22))
            return;
        events = data + 22;
    } else {
        events = midi_find_xmidi_events(data, size, &length, 4);
    }
    if (!events || !length)
        return;
    track = (RolandTrack){events, events + length, loop, division, fade_in};
    if (!output) {
        cancel = CreateEvent(NULL, TRUE, FALSE, NULL);
        message_done = CreateEvent(NULL, FALSE, FALSE, NULL);
        if (!cancel || !message_done)
            goto failed;

        MMRESULT result = midiOutOpen(&output, selected_port, (DWORD_PTR)message_done, 0, CALLBACK_EVENT);
        if (result != MMSYSERR_NOERROR) {
            report_midi_error(result);
            goto failed;
        }

        if (!division) {
            MMRESULT timer = timeBeginPeriod(1);
            int ready = initialize_towers_roland();
            if (timer == TIMERR_NOERROR)
                timeEndPeriod(1);
            if (!ready)
                goto failed;
        }
    }

    ResetEvent(cancel);
    worker = CreateThread(NULL, 0, play_track, NULL, 0, NULL);
    if (worker)
        return;

failed:
    fputs("lotr: could not start Roland MIDI playback\n", stderr);
    close_roland_device();
}
#endif
