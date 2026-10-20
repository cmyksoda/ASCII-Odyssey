// SPDX-License-Identifier: GPL-3.0-only
#include "player.h"

#include "mathx.h"

#include <string.h>

#define TAIL_SECONDS 6
#define GHOST_FADE 0.05f

enum { DRY_L, DRY_R, DUCK_L, DUCK_R, SEND_REVERB, SEND_DELAY, HALF_L, HALF_R, BUSES };

int player_init(Player *p, const Song *s, int dry) {
    memset(p, 0, sizeof *p);
    p->song = s;
    p->dry = dry;
    p->frames_per_tick = AUDIO_RATE * 2.5 / s->bpm;
    for (int c = 0; c < SONG_CHANNELS; c++) {
        p->ch[c].volume = 1;
        p->ch[c].pan = -2;
    }
    p->tail_frames = TAIL_SECONDS * AUDIO_RATE;
    if (dry) return 0;
    const MixSettings *m = &s->mix;
    if (plate_init(&p->plate) != 0) return -1;
    p->plate.decay = m->reverb_size > 0 ? m->reverb_size : 0.72f;
    p->plate.damping = m->reverb_damping > 0 ? m->reverb_damping : 0.35f;
    float beat = 60.0f / (float)s->bpm;
    if (pingpong_init(&p->delay, (m->delay_beats > 0 ? m->delay_beats : 0.75f) * beat) != 0) return -1;
    p->delay.feedback = m->delay_feedback > 0 ? m->delay_feedback : 0.4f;
    compressor_init(&p->glue, -14, 2.0f, 12, 160);
    limiter_init(&p->limiter, 0.89f);
    p->duck_release = mexp(-1.0f / ((m->duck_release > 0 ? m->duck_release : 0.12f) * AUDIO_RATE));
    p->master_knob = 1;
    p->master_level = s->level && s->automation_rows ? s->level[0] : 1;
    svf_reset(&p->master_l);
    svf_reset(&p->master_r);
    return 0;
}

void player_free(Player *p) {
    plate_free(&p->plate);
    pingpong_free(&p->delay);
}

double player_seconds(const Player *p) {
    return (double)p->frame / AUDIO_RATE;
}

// The global row being played: order * 64 + row.
static int player_row(const Player *p) {
    return p->order * p->song->pattern_rows + p->row;
}

static const Sample *pick_sample(const Instrument *ins, int note) {
    for (int k = 0; k < ins->sample_count; k++)
        if (note <= ins->sample_top[k]) return &ins->samples[k];
    return &ins->samples[ins->sample_count - 1];
}

static Voice *ghost_slot(Player *p) {
    Voice *best = NULL;
    float quietest = 1e9f;
    for (int i = SONG_CHANNELS; i < PLAYER_VOICES; i++) {
        Voice *v = &p->voices[i];
        if (!v->active) return v;
        float level = (v->target_l + v->target_r) * v->fade;
        if (level < quietest) {
            quietest = level;
            best = v;
        }
    }
    return best;
}

// The old note keeps ringing on a ghost voice while the channel moves on, instead of being cut off.
static void release_to_ghost(Player *p, Channel *c) {
    if (!c->voice || !c->voice->active) return;
    Voice *g = ghost_slot(p);
    if (g) {
        *g = *c->voice;
        g->owner = -1;
        g->released = 1;
    }
    c->voice->active = 0;
}

static void start_note(Player *p, int channel, int note, int offset) {
    Channel *c = &p->ch[channel];
    const Song *s = p->song;
    if (c->instrument < 1 || c->instrument > s->instrument_count) return;
    const Instrument *ins = &s->instruments[c->instrument - 1];
    if (ins->sample_count == 0) return;
    release_to_ghost(p, c);
    Voice *v = &p->voices[channel];
    memset(v, 0, sizeof *v);
    v->ins = ins;
    v->smp = pick_sample(ins, note);
    v->pos = (double)offset * 256;
    if (v->pos >= v->smp->length) v->pos = 0;
    v->active = 1;
    v->owner = channel;
    v->fade = 1;
    v->volume = c->volume;
    v->velocity = 1;
    v->cutoff = c->cutoff > 0 ? c->cutoff : ins->cutoff;
    v->cutoff_env = ins->cutoff_env;
    v->filtered = v->cutoff > 0 && v->cutoff < 0.999f;
    svf_reset(&v->svf);
    svf_reset(&v->vowel[0]);
    svf_reset(&v->vowel[1]);
    c->voice = v;
    c->note = note;
    c->pitch = c->target_pitch = (float)note;
    c->vibrato_phase = 0;
    if (ins->role == ROLE_KICK) p->duck = 1;
    if (p->listener) p->listener(p->listener_context, player_seconds(p), channel, note, c->volume, ins);
}

static float envelope_level(const Envelope *e, int tick) {
    if (e->count <= 1) return e->count ? e->level[0] : 1;
    for (int i = 0; i + 1 < e->count; i++)
        if (tick < e->tick[i + 1]) {
            float span = (float)(e->tick[i + 1] - e->tick[i]);
            float t = span > 0 ? (float)(tick - e->tick[i]) / span : 1;
            return e->level[i] + (e->level[i + 1] - e->level[i]) * t;
        }
    return e->level[e->count - 1];
}

// Volumes, pans, pitches and filters for the coming tick.
static void update_voice(Player *p, Voice *v, const Channel *c) {
    const Instrument *ins = v->ins;
    const Envelope *e = &ins->volume_env;
    float level = envelope_level(e, v->env_tick);
    int held = !v->released && e->sustain >= 0 && v->env_tick >= e->tick[e->sustain];
    if (!held) v->env_tick++;
    if (v->released) {
        v->fade -= v->owner < 0 && !v->smp->loop_length ? GHOST_FADE : ins->fadeout;
        if (v->fade <= 0) v->active = 0;
    }
    if (e->count > 1 && v->env_tick >= e->tick[e->count - 1] && e->level[e->count - 1] <= 0) v->active = 0;
    float pan = c && c->pan > -1.5f ? c->pan : ins->pan;
    float volume = c ? c->volume : v->volume;
    float amp = ins->volume * volume * level * (v->fade > 0 ? v->fade : 0) * v->velocity;
    float angle = (pan + 1) * (PI_F / 4);
    v->target_l = amp * mcos(angle);
    v->target_r = amp * msin(angle);
    if (!v->active) v->target_l = v->target_r = 0;
    float pitch = c ? c->pitch : v->pitch;
    v->pitch = pitch;
    if (c) v->wobble = c->wobble;
    float vib = 0;
    if (c && c->vibrato_depth) vib += msin(c->vibrato_phase) * (float)c->vibrato_depth * 0.0625f;
    if (ins->vibrato_depth > 0) {
        float onset = smoothstep(0.0f, 0.4f, (float)v->env_tick * 2.5f / (float)p->song->bpm);
        v->vibrato_phase += TAU_F * ins->vibrato_rate * 2.5f / (float)p->song->bpm;
        vib += msin(v->vibrato_phase) * ins->vibrato_depth * onset;
    }
    v->step = mexp2((pitch + vib - v->smp->root_note) / 12.0f);
    if (v->cutoff_env > 0 && ins->cutoff_decay > 0)
        v->cutoff_env *= mexp(-2.5f / ((float)p->song->bpm * ins->cutoff_decay));
}

static void row_effects(Player *p, int channel) {
    Channel *c = &p->ch[channel];
    const PatternCell *cell = &c->cell;
    int x = cell->param >> 4, y = cell->param & 15;
    c->arpeggio = 0;
    c->vibrato_depth = 0;
    c->retrigger = 0;
    switch (cell->effect) {
    case FX_ARPEGGIO: c->arpeggio = cell->param; break;
    case FX_PORTA_UP:
    case FX_PORTA_DOWN:
    case FX_TONE_PORTA:
        if (cell->param) c->porta = cell->param;
        break;
    case FX_VIBRATO:
        if (x) c->vibrato_speed = (uint8_t)x;
        c->vibrato_depth = (uint8_t)(y ? y : 4);
        break;
    case FX_VOLUME_SLIDE:
        if (cell->param) c->slide = cell->param;
        break;
    case FX_SET_PAN: c->pan = (float)cell->param / 127.5f - 1; break;
    case FX_RETRIGGER: c->retrigger = cell->param; break;
    case FX_WOBBLE: c->wobble = (float)cell->param / 4.0f; break;
    case FX_FILTER:
        c->cutoff = (float)cell->param / 255.0f;
        if (c->voice && c->voice->active) {
            c->voice->cutoff = c->cutoff;
            c->voice->filtered = c->cutoff < 0.999f;
        }
        break;
    default: break;
    }
}

static void trigger_row(Player *p, int channel) {
    Channel *c = &p->ch[channel];
    const PatternCell *cell = &c->cell;
    if (cell->instrument) {
        c->instrument = cell->instrument;
        c->volume = 1;
    }
    if (cell->volume) c->volume = (float)(cell->volume - 1) / 64.0f;
    row_effects(p, channel);
    if (cell->effect == FX_NOTE_DELAY && (cell->param & 15)) return;
    if (cell->note == NOTE_OFF) {
        if (c->voice) c->voice->released = 1;
    } else if (cell->note == NOTE_CUT) {
        if (c->voice) c->voice->fade = 0, c->voice->released = 1;
    } else if (cell->note >= 1 && cell->note <= 120) {
        if (cell->effect == FX_TONE_PORTA && c->voice && c->voice->active)
            c->target_pitch = (float)cell->note;
        else
            start_note(p, channel, cell->note, cell->effect == FX_SAMPLE_OFFSET ? cell->param : 0);
    }
}

static void tick_effects(Player *p, int channel, int tick) {
    Channel *c = &p->ch[channel];
    const PatternCell *cell = &c->cell;
    int x = c->slide >> 4, y = c->slide & 15;
    switch (cell->effect) {
    case FX_PORTA_UP: c->pitch += (float)c->porta / 16.0f; break;
    case FX_PORTA_DOWN: c->pitch -= (float)c->porta / 16.0f; break;
    case FX_TONE_PORTA: {
        float d = c->target_pitch - c->pitch, s = (float)c->porta / 16.0f;
        c->pitch = mabs(d) <= s ? c->target_pitch : c->pitch + (d > 0 ? s : -s);
        break;
    }
    case FX_VOLUME_SLIDE: c->volume = clampf(c->volume + (float)x / 64.0f - (float)y / 64.0f, 0, 1); break;
    case FX_NOTE_CUT:
        if (tick == (cell->param & 15) && c->voice) c->volume = 0;
        break;
    case FX_NOTE_DELAY:
        if (tick == (cell->param & 15) && cell->note >= 1 && cell->note <= 120)
            start_note(p, channel, cell->note, 0);
        break;
    default: break;
    }
    if (c->retrigger && tick % c->retrigger == 0 && c->note) start_note(p, channel, c->note, 0);
}

static void process_tick(Player *p) {
    const Song *s = p->song;
    if (!p->ended && p->tick == 0) {
        if (p->order >= s->order_length) {
            p->ended = 1;
            for (int c = 0; c < SONG_CHANNELS; c++)
                if (p->ch[c].voice) p->ch[c].voice->released = 1;
        } else {
            const Pattern *pat = &s->patterns[s->order[p->order]];
            for (int c = 0; c < s->channels; c++) {
                p->ch[c].cell = pat->cells[p->row][c];
                trigger_row(p, c);
            }
        }
    } else if (!p->ended) {
        for (int c = 0; c < s->channels; c++) tick_effects(p, c, p->tick);
    }
    for (int c = 0; c < s->channels; c++) {
        Channel *ch = &p->ch[c];
        float arp = 0;
        if (ch->arpeggio) {
            int phase = p->tick % 3;
            arp = phase == 1 ? (float)(ch->arpeggio >> 4) : phase == 2 ? (float)(ch->arpeggio & 15) : 0;
        }
        if (ch->vibrato_depth) ch->vibrato_phase += (float)ch->vibrato_speed * TAU_F / 64.0f;
        if (ch->voice && ch->voice->active) {
            ch->pitch += arp;
            update_voice(p, ch->voice, ch);
            ch->pitch -= arp;
        }
    }
    for (int i = SONG_CHANNELS; i < PLAYER_VOICES; i++)
        if (p->voices[i].active) update_voice(p, &p->voices[i], NULL);
    if (!p->ended && ++p->tick >= s->speed) {
        p->tick = 0;
        if (++p->row >= s->pattern_rows) {
            p->row = 0;
            p->order++;
        }
    }
}

static float frame_at(const Sample *s, int i) {
    if (i < 0) return 0;
    if (i >= s->length) {
        if (!s->loop_length) return 0;
        i = s->loop_start + (i - s->loop_start) % s->loop_length;
    }
    return s->frames[i];
}

static void mix_voice(Player *p, Voice *v, int n) {
    const Sample *s = v->smp;
    const Instrument *ins = v->ins;
    int bus = ins->ducked == 2 ? HALF_L : ins->ducked ? DUCK_L : DRY_L;
    float *out_l = p->bus[bus], *out_r = p->bus[bus + 1];
    float *rev = p->bus[SEND_REVERB], *del = p->bus[SEND_DELAY];
    float dl = (v->target_l - v->gain_l) / (float)n, dr = (v->target_r - v->gain_r) / (float)n;
    int end = s->loop_length ? s->loop_start + s->loop_length : s->length;
    float knob = v->cutoff + v->cutoff_env, lfo = 0;

    if (ins->wobble_depth > 0 && v->wobble > 0) {
        // The LFO runs on the song's own beat, read at the middle of this block.
        double beat = ((double)p->frame + n * 0.5) * (double)p->song->bpm / (60.0 * AUDIO_RATE);
        float phase = (float)(beat * v->wobble - (double)(long long)(beat * v->wobble));
        lfo = ins->wobble_shape == 1   ? 1 - phase
              : ins->wobble_shape == 2 ? 1 - mabs(2 * phase - 1)
                                       : 0.5f - 0.5f * mcos(phase * TAU_F);
        knob += ins->wobble_depth * lfo;
    }

    if (v->filtered) svf_set(&v->svf, knob_hz(knob), 0.707f + ins->resonance);
    float growl = ins->growl, drive = 1 + 3 * growl;
    if (growl > 0) {
        // From "o" with the filter shut to "ee" wide open, so every swing of the wobble says "yoi".
        svf_set(&v->vowel[0], 450 - 150 * lfo, 4.5f);
        svf_set(&v->vowel[1], 800 * mpow(2.875f, lfo), 7);
    }

    for (int i = 0; i < n; i++) {
        int idx = (int)v->pos;
        float f = (float)(v->pos - (double)idx), s0, s1, s2, s3;
        if (idx >= 1 && idx + 2 < end) {
            const float *d = s->frames + idx;
            s0 = d[-1];
            s1 = d[0];
            s2 = d[1];
            s3 = d[2];
        } else {
            s0 = frame_at(s, idx - 1);
            s1 = frame_at(s, idx);
            s2 = frame_at(s, idx + 1);
            s3 = frame_at(s, idx + 2);
        }

        float c1 = 0.5f * (s2 - s0), c2 = s0 - 2.5f * s1 + 2 * s2 - 0.5f * s3, c3 = 0.5f * (s3 - s0) + 1.5f * (s1 - s2);
        float x = ((c3 * f + c2) * f + c1) * f + s1;
        if (v->filtered) x = svf_low(&v->svf, x);
        if (growl > 0) {
            float d = mtanh(x * drive);
            float talk = v->vowel[0].k * svf_band(&v->vowel[0], d) + 0.7f * v->vowel[1].k * svf_band(&v->vowel[1], d);
            // Added on top of the whole sound, so the body stays, and the drive's overtones reach the mix only
            // through the formants, or they'd fizz over the top.
            x += growl * 7 * talk;
        }

        v->gain_l += dl;
        v->gain_r += dr;
        float l = x * v->gain_l, r = x * v->gain_r, m = (l + r) * 0.5f;
        out_l[i] += l;
        out_r[i] += r;
        rev[i] += m * ins->send_reverb;
        del[i] += m * ins->send_delay;

        v->pos += v->step;
        if (v->pos >= end) {
            if (s->loop_length) {
                v->pos -= s->loop_length;
            } else {
                v->active = 0;
                break;
            }
        }
    }

    v->gain_l = v->target_l;
    v->gain_r = v->target_r;
}

static float automation(const float *lane, const Song *s, int row, float fallback) {
    if (!lane || row < 0 || row >= s->automation_rows) return fallback;
    return lane[row];
}

static void master(Player *p, float *out, int n) {
    const Song *s = p->song;
    const MixSettings *m = &s->mix;
    int row = player_row(p);
    float depth = automation(s->duck_amount, s, row, m->duck_depth);
    float knob_target = automation(s->master_cutoff, s, row, 1);
    float level_target = automation(s->level, s, row, 1);
    float gain = m->master_gain > 0 ? m->master_gain : 1, pre = m->pre_gain > 0 ? m->pre_gain : 1;
    for (int i = 0; i < n; i++) {
        p->duck *= p->duck_release;
        float duck = 1 - depth * p->duck;
        float half = 1 - depth * 0.5f * p->duck;
        float l = p->bus[DRY_L][i] + p->bus[DUCK_L][i] * duck + p->bus[HALF_L][i] * half;
        float r = p->bus[DRY_R][i] + p->bus[DUCK_R][i] * duck + p->bus[HALF_R][i] * half;
        float rl, rr, dl, dr;
        plate_process(&p->plate, p->bus[SEND_REVERB][i], &rl, &rr);
        pingpong_process(&p->delay, p->bus[SEND_DELAY][i], &dl, &dr);
        float rv = m->reverb_mix > 0 ? m->reverb_mix : 0.8f, dv = m->delay_mix > 0 ? m->delay_mix : 0.6f;
        l = (l + rl * rv * duck + dl * dv) * pre;
        r = (r + rr * rv * duck + dr * dv) * pre;
        if ((i & 31) == 0) {
            // About 25 ms to settle: quick enough for a drop to land on its downbeat, slow enough not to click.
            p->master_level += (level_target - p->master_level) * 0.06f;
            p->master_knob += (knob_target - p->master_knob) * 0.08f;
            if (p->master_knob < 0.995f) {
                svf_set(&p->master_l, knob_hz(p->master_knob), 0.9f);
                p->master_r = (Svf){p->master_r.ic1, p->master_r.ic2, p->master_l.a1, p->master_l.a2, p->master_l.a3,
                                    p->master_l.k};
            }
        }
        if (p->master_knob < 0.995f) {
            l = svf_low(&p->master_l, l);
            r = svf_low(&p->master_r, r);
        }
        // A 20 Hz high-pass keeps sub-sonic drift from eating headroom.
        p->dc_l = 0.9974f * (p->dc_l + l - p->dc_in_l);
        p->dc_in_l = l;
        p->dc_r = 0.9974f * (p->dc_r + r - p->dc_in_r);
        p->dc_in_r = r;
        l = p->dc_l;
        r = p->dc_r;
        // The section level comes after the glue, so the compressor can't even out a song's quiet and loud parts.
        float g = compressor_gain(&p->glue, mmax(mabs(l), mabs(r))) * 1.2f * p->master_level;
        l *= g;
        r *= g;
        if (m->drive > 0) {
            l = soft_clip(l * (1 + m->drive)) / (1 + m->drive * 0.5f);
            r = soft_clip(r * (1 + m->drive)) / (1 + m->drive * 0.5f);
        }
        limiter_process(&p->limiter, &l, &r);
        out[i * 2] = l * gain;
        out[i * 2 + 1] = r * gain;
    }
}

static int any_voice(const Player *p) {
    for (int i = 0; i < PLAYER_VOICES; i++)
        if (p->voices[i].active) return 1;
    return 0;
}

int player_render(Player *p, float *out, int frames) {
    int done = 0;
    while (done < frames) {
        if (p->ended && (p->tail_frames <= 0 || (p->dry && !any_voice(p)))) break;
        if (p->frames_left <= 0) {
            process_tick(p);
            p->frames_left += p->frames_per_tick;
        }
        int n = frames - done;
        if (n > PLAYER_BLOCK) n = PLAYER_BLOCK;
        int left = (int)p->frames_left + (p->frames_left > (int)p->frames_left ? 1 : 0);
        if (n > left) n = left;
        if (p->ended) {
            if (n > p->tail_frames) n = p->tail_frames;
            p->tail_frames -= n;
        }
        if (!p->dry) {
            for (int b = 0; b < BUSES; b++) memset(p->bus[b], 0, sizeof(float) * (size_t)n);
            int active = 0;
            for (int i = 0; i < PLAYER_VOICES; i++)
                if (p->voices[i].active) {
                    mix_voice(p, &p->voices[i], n);
                    active++;
                }
            if (active > p->peak_voices) p->peak_voices = active;
            master(p, out + done * 2, n);
        }
        p->frames_left -= n;
        p->frame += (uint64_t)n;
        done += n;
    }
    return done;
}
