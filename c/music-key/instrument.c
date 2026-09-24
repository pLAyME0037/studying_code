#include <assert.h>
#include <math.h>
#include <stdbool.h>

#define LERPF(a, b, t) ((1 - (t)) * (a) + (t) * (b))
#define TWO_PI         6.28318530717958647692

typedef struct instrument_t instrument_t;
struct instrument_t {
    float        (*tag)(instrument_t *this, float x);
    float          p;
    float          tremolo_freq;
    instrument_t  *instrument;
};

float func_sine(instrument_t *this, float x) {
    (void)this;
    return sinf(x*TWO_PI);
}

float func_square(instrument_t *this, float x) {
    x = x - floorf(x);
    if (x <= this->p) return 1;
    return -1;
}

float func_saw_tooth(instrument_t *this, float x) {
    if (this->p <= 0.0) this->p = 0.0001;
    if (this->p >= 1.0) this->p = 0.9999;
    x = x - floorf(x);
    if (x <= this->p && this->p > 0) {
        return LERPF(-1, 1, x / this->p);
    }
    return LERPF(1, -1, (x - this->p)/(1 - this->p));
}

float instrument_run(instrument_t *this, float x);

float func_tremolo(instrument_t *this, float x) {
    float volumn = (sinf(x * TWO_PI / this->tremolo_freq) + 1) / 2;
    return instrument_run(this->instrument, x) * volumn;
}

instrument_t instrument_sine(void) {
    return (instrument_t) {
        .tag = func_sine,
    };
}

instrument_t instrument_square(void) {
    return (instrument_t) {
        .tag = func_square,
        .p   = 0.5
    };
}

instrument_t instrument_saw_tooth(void) {
    return (instrument_t) {
        .tag = func_saw_tooth,
    };
}

instrument_t instrument_tremolo(void) {
    return (instrument_t) {
        .tag = func_tremolo,
        .tremolo_freq = 50,
    };
}
float instrument_run(instrument_t *this, float x) {
    if (!this || !this->tag) return 0.0f;
    return this->tag(this, x);
}

