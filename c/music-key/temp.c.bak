#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "modules/raylib-6.0/include/raylib.h"

#include "instrument.c"

#define ARRAY_LEN(s) (sizeof(s)/sizeof(s[0]))

#define DA_NEW(Type, Name)                                                \
    typedef struct {                                                      \
        Type  *items;                                                     \
        size_t count;                                                     \
        size_t capacity;                                                  \
    } Name;                                                               \
    static inline Name *Name##_new(void) {                                \
        Name *da = malloc(sizeof(*da));                                   \
        assert(da != NULL);                                               \
        da->capacity = INIT_CAPACITY;                                     \
        da->items = malloc(INIT_CAPACITY * sizeof(*da->items));           \
        assert(da->items != NULL);                                        \
        da->count = 0;                                                    \
        return da;                                                        \
    }                                                                     \
    static inline void Name##_add(Name *da, Type item) {                  \
        if (da->count >= da->capacity) {                                  \
            da->capacity = da->capacity == 0 ? 16 : da->capacity * 2;     \
            da->items = realloc(da->items, da->capacity * sizeof(Type));  \
            assert((da)->items != NULL && "Ran out of memory");           \
        }                                                                 \
        da->items[da->count++] = item;                                    \
    }                                                                     \
    static inline void Name##_free(Name *da) { free(da->items); free(da); }

#define BUFFER_SIZE      (1024*2)
#define SAMPLERATE       44100
#define SAMPLESIZE       32
#define CHANNELS         1
#define ROOT_NOTE        440.0
#define BPM              120.0
#define BEAT_SECS        (60.0/BPM)
#define BAR_BEATS        4
#define BAR_SECS         (BAR_BEATS*BEAT_SECS)
#define BAR_QUANT        32
#define QUANT_SECS       (BAR_SECS/BAR_QUANT)

#define NEXT_SEMITONE    powf(2.0, 1.0/12.0)
#define INIT_CAPACITY    16

#define ATTACK_FRAME     (2205)
#define RELEASE_FRAME    (4410)

const KeyboardKey KEY_MAP[] = {
    KEY_Z, KEY_S, KEY_X, KEY_D, KEY_C, KEY_F, KEY_V, KEY_G, KEY_B, KEY_H,
    KEY_N, KEY_J, KEY_M, KEY_K, KEY_COMMA
};
#define NOTE_COUNT       ARRAY_LEN(KEY_MAP)

const char *KEY_NAMES[NOTE_COUNT] = {
    "Z", "S", "X", "D", "C", "F", "V", "G", "B", "H", "N", "J", "M", "K", ","
};

const char *NOTE_NAMES[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

const char *semitone_to_note_name(int semitone) {
    int midi = 69 + semitone;
    int note_idx = (midi % 12 + 12) % 12;
    int octave = (midi >= 0) ? (midi / 12 - 1) : ((midi - 11) / 12 - 1);
    return TextFormat("%s%d", NOTE_NAMES[note_idx], octave);
}

float clamp_f(float f, float min, float max) {
    if (f < min) return min;
    if (f > max) return max;
    return f;
}

typedef struct {
    int          timestamp;
    bool         start;
    int          key_idx;
    int          semitone;
    instrument_t instrument;
} event_t;

DA_NEW(event_t, event_da);

typedef struct {
    int          semitone;
    bool         playing;
    size_t       start_frame;
    instrument_t instrument;
} note_t;

event_t *peek(event_da *es) {
    if (es == NULL || es->count == 0) return NULL;
    return &es->items[es->count - 1];
}

note_t notes_replay[NOTE_COUNT];
note_t notes_monitor[NOTE_COUNT];

size_t frame_count = 0;

float semitone_to_freq(float semitone) {
    return ROOT_NOTE*powf(NEXT_SEMITONE, semitone);
}

float note_update(note_t *note) {
    float volumn = clamp_f(((float)frame_count - note->start_frame)/ATTACK_FRAME, 0.0f, 1.0f);
    float freq = semitone_to_freq((float)note->semitone);
    return instrument_run(note->instrument, ((float)frame_count / SAMPLERATE) * freq) * volumn;
}

typedef struct {
    size_t       stop_frame;
    float        stop_at_volumn;
    float        semitone;
    instrument_t instrument;
} note_release_t;

DA_NEW(note_release_t, note_release_da);

float note_release_update(note_release_t *note_rel) {
    float prog   = clamp_f((float)(frame_count - note_rel->stop_frame)/RELEASE_FRAME, 0.0f, 1.0f);
    float volumn = 0.5f * (1.0f + cosf(PI * prog)) * note_rel->stop_at_volumn;
    float freq   = semitone_to_freq(note_rel->semitone);
    return instrument_run(note_rel->instrument, ((float)frame_count / SAMPLERATE) * freq) * volumn;
}

void note_releases_unordered_rm_by_idx(note_release_da *nrs, size_t idx) {
    assert(idx < nrs->count);
    nrs->items[idx] = nrs->items[--nrs->count];
}

void note_press(note_t *note, int semitone, instrument_t instrument) {
    if (!note->playing) {
        note->playing     = true;
        note->start_frame = frame_count;
        note->semitone    = semitone;
        note->instrument  = instrument;
    }
}

note_release_da *g_note_releases = NULL;
void note_released(note_t *note) {
    if (note->playing) {
        note->playing = false;
        note_release_da_add(g_note_releases, (note_release_t) {
            .stop_frame     = frame_count,
            .stop_at_volumn = clamp_f((float)(frame_count - note->start_frame)/ATTACK_FRAME, 0.0f, 1.0f),
            .semitone       = (float)note->semitone,
            .instrument     = note->instrument,
        });
    }
}

bool note_released_done(note_release_t *nr) {
    return (float)(frame_count - nr->stop_frame) >= RELEASE_FRAME;
}

int main(void) {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(800, 600, "Music Key");
    InitAudioDevice();

    float buffer[BUFFER_SIZE];
    SetAudioStreamBufferSizeDefault(BUFFER_SIZE);
    AudioStream synth = LoadAudioStream(SAMPLERATE, SAMPLESIZE, CHANNELS);
    PlayAudioStream(synth);

    Sound music = LoadSound("./h-beats-clock-beat-effect-402705.mp3");

    enum { REPLAY, WAIT_FOR_EOB, RECORD } state = REPLAY;

    event_da *events = event_da_new();
    g_note_releases  = note_release_da_new();

    SetTargetFPS(60);
    SetExitKey(KEY_NULL);
    float beat_time              = 0.0f;
    int record_bar_amount        = 0;
    int quant_for_play           = 0;
    int quant_for_play_prev      = -1;
    instrument_t instrument_curr = instrument_saw_tooth();

    while (!WindowShouldClose()) {
        float beat_time_prev = beat_time;
        beat_time += GetFrameTime();
        int quant = (int)(beat_time/QUANT_SECS);

        if (fmodf(beat_time_prev, BEAT_SECS) > fmodf(beat_time, BEAT_SECS)) {
            // PlaySound(music);
        }
        switch (state) {
        case REPLAY:
            if (events->count > 0 && record_bar_amount > 0) {
                event_t *last_event = peek(events);
                record_bar_amount = (last_event->timestamp + BAR_QUANT - 1)/BAR_QUANT;
                if (record_bar_amount < 1) record_bar_amount = 1;
                quant_for_play = quant%(record_bar_amount*BAR_QUANT);
                if (quant_for_play != quant_for_play_prev) {
                    for (size_t i = 0; i < events->count; ++i) {
                        if (events->items[i].timestamp == quant_for_play) {
                            if (events->items[i].start) {
                                note_press(&notes_replay[events->items[i].key_idx], events->items[i].semitone, instrument_curr);
                            } else {
                                note_released(&notes_replay[events->items[i].key_idx]);
                            }
                        }
                    }
                    quant_for_play_prev = quant_for_play;
                }
            }
        break;
        case WAIT_FOR_EOB:
            if (fmodf(beat_time_prev, BAR_SECS) > fmodf(beat_time, BAR_SECS)) {
                state = RECORD;
                quant = 0;
                beat_time = 0.0f;
                quant_for_play_prev = -1;
                for (size_t i = 0; i < ARRAY_LEN(notes_monitor); ++i) {
                    if (notes_monitor[i].playing) {
                        event_da_add(events, (event_t) {
                            .timestamp  = 0,
                            .start      = true,
                            .key_idx    = (int)i,
                            .semitone   = notes_monitor[i].semitone,
                            .instrument = notes_monitor[i].instrument,
                        });
                    }
                }
            }
        break;
        case RECORD:
        break;
        }

        if (IsKeyPressed(KEY_ONE)) {
            instrument_curr = instrument_sine();
        }
        if (IsKeyPressed(KEY_TWO)) {
            instrument_curr = instrument_square();
        }
        if (IsKeyPressed(KEY_THREE)) {
            instrument_curr = instrument_saw_tooth();
        }
        if (IsKeyPressed(KEY_SPACE)) {
            switch (state) {
            case REPLAY:
                state = WAIT_FOR_EOB;
                events->count = 0;
                for (size_t i = 0; i < NOTE_COUNT; ++i) {
                    note_released(&notes_replay[i]);
                }
            break;
            case RECORD:
                record_bar_amount = (quant + BAR_QUANT - 1)/BAR_QUANT;
                if (record_bar_amount < 1) record_bar_amount = 1;
                beat_time = 0.0f;
                quant_for_play_prev = -1;
                state = REPLAY;
            break;
            case WAIT_FOR_EOB:
                state = REPLAY;
            break;
            }
        }

        int shift = (IsKeyDown(KEY_BACKSPACE)) ? -12 : 0;
        for (int key = 0; key < (int)NOTE_COUNT; ++key) {
            if (IsKeyDown(KEY_MAP[key]) && !notes_monitor[key].playing) {
                int pitch = key + shift;
                note_press(&notes_monitor[key], pitch, instrument_curr);
                if (state == RECORD) {
                    event_da_add(events, (event_t) {
                        .timestamp  = quant,
                        .start      = true,
                        .key_idx    = key,
                        .semitone   = pitch,
                        .instrument = instrument_curr,
                    });
                }
            } else if (!IsKeyDown(KEY_MAP[key]) && notes_monitor[key].playing) {
                if (state == RECORD) {
                    event_da_add(events, (event_t) {
                        .timestamp  = quant,
                        .start      = false,
                        .key_idx    = key,
                        .semitone   = notes_monitor[key].semitone,
                        .instrument = instrument_curr,
                    });
                }
                note_released(&notes_monitor[key]);
            }
        }

        if (IsAudioStreamProcessed(synth)) {
            memset(buffer, 0, sizeof(buffer));

            int note_playing = 0;
            for (size_t key = 0; key < NOTE_COUNT; ++key) {
                if (notes_monitor[key].playing) note_playing += 1;
                if (notes_replay[key].playing)  note_playing += 1;
            }
            note_playing += (int)g_note_releases->count;
            float amp = (note_playing > 0) ? (1.0f / (float)note_playing) : 0.0f;

            for (size_t i = 0; i < BUFFER_SIZE; ++i) {
                float sample = 0.0f;
                if (note_playing > 0) {
                    for (size_t semitone = 0; semitone < NOTE_COUNT; ++semitone) {
                        if (notes_monitor[semitone].playing) {
                            sample += note_update(&notes_monitor[semitone]) * amp;
                        }
                        if (notes_replay[semitone].playing) {
                            sample += note_update(&notes_replay[semitone]) * amp;
                        }
                    }
                    for (size_t semitone = 0; semitone < g_note_releases->count; ++semitone) {
                        sample += note_release_update(&g_note_releases->items[semitone]) * amp;
                    }
                }
                buffer[i] = clamp_f(sample, -1.0f, 1.0f);
                frame_count += 1;
            }
            for (size_t key = g_note_releases->count; key > 0; --key) {
                if (note_released_done(&g_note_releases->items[key - 1])) {
                    note_releases_unordered_rm_by_idx(g_note_releases, key - 1);
                }
            }
            UpdateAudioStream(synth, buffer, ARRAY_LEN(buffer));
        }

        BeginDrawing();
        ClearBackground(GetColor(0x121218FF));

        // State indicator
        switch (state) {
        case REPLAY:      DrawRing((Vector2){GetScreenWidth() - 50, 50}, 18.0f, 20.0f, 0, 360, 100, YELLOW); break;
        case WAIT_FOR_EOB: DrawCircleV((Vector2){GetScreenWidth() - 50, 50}, 20.0f, BLUE); break;
        case RECORD:      DrawCircleV((Vector2){GetScreenWidth() - 50, 50}, 20.0f, RED); break;
        }

        record_bar_amount = 1;
        if (events->count > 0) {
            event_t *last_event = peek(events);
            record_bar_amount = (last_event->timestamp + BAR_QUANT - 1)/BAR_QUANT;
            if (record_bar_amount < 1) record_bar_amount = 1;
        }

        float quant_len = (float)GetScreenWidth() / (BAR_QUANT * record_bar_amount);
        float semitone_height = (float)GetScreenHeight() / (float)NOTE_COUNT;

        // Draw note names and key labels per row
        for (int key = 0; key < (int)NOTE_COUNT; ++key) {
            float y = key * semitone_height;
            bool active = notes_monitor[key].playing || notes_replay[key].playing;
            int semi = active ? (notes_monitor[key].playing 
                                  ? notes_monitor[key].semitone 
                                  : notes_replay[key].semitone)
                              : (key + shift);

            const char *label = TextFormat("%s - %s", KEY_NAMES[key], semitone_to_note_name(semi));
            Color col = active ? YELLOW : Fade(LIGHTGRAY, 0.4f);
            DrawText(label, 12, (int)(y + (semitone_height - 16) / 2.0f), 16, col);
        }

        // Draw recorded note events
        for (size_t i = 0; i < events->count; ++i) {
            Vector2 pos = {
                events->items[i].timestamp * quant_len,
                events->items[i].key_idx * semitone_height + (semitone_height / 2.0f)
            };
            DrawCircleV(pos, 6, events->items[i].start ? RED : BLUE);
        }

        // Beat grid lines
        int total_beat = BAR_BEATS * record_bar_amount;
        float beat_len = (float)GetScreenWidth() / total_beat;
        for (int i = 1; i < total_beat; ++i) {
            DrawLineV((Vector2){beat_len * i, 0}, (Vector2){beat_len * i, GetScreenHeight()}, Fade(GRAY, 0.3f));
        }

        // Playhead cursor
        float total_loop_sec = BAR_SECS * record_bar_amount;
        float ph = (fmodf(beat_time, total_loop_sec) / total_loop_sec) * GetScreenWidth();
        DrawLineV((Vector2){ph, 0}, (Vector2){ph, GetScreenHeight()}, WHITE);

        EndDrawing();
    }

    note_release_da_free(g_note_releases);
    event_da_free(events);
    UnloadSound(music);
    UnloadAudioStream(synth);
    CloseAudioDevice();
    CloseWindow();

    return 0;
}
