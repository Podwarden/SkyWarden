#include "audio.h"
#include "audio_util.h"

/* --- tunable SFX levels / shaping (dialed by ear on-device) --- */
#define SFX_CHANNEL_VOL  1.0f     /* channel trim; per-synth volumes do the mixing */
#define BURNER_CUTOFF    900.0f   /* low-pass cutoff (Hz): noise -> soft whoosh */
#define GUN_VOL          0.15f    /* subtle */
#define EXPL_VOL         0.25f    /* restrained */
#define GUN_DECAY        0.06f    /* s */
#define GUN_RELEASE      0.03f    /* s */
#define GUN_LEN          0.05f    /* s note length */
#define EXPL_DECAY       0.40f    /* s */
#define EXPL_RELEASE     0.20f    /* s */
#define EXPL_LEN         0.40f    /* s */
#define BURNER_NOTE_HZ   220.0f   /* arbitrary; noise waveform ignores pitch */
#define GUN_NOTE_HZ      440.0f
#define EXPL_NOTE_HZ     110.0f

static PlaydateAPI* g_pd = NULL;
static PDSynth*     g_burner = NULL;
static PDSynth*     g_gun    = NULL;
static PDSynth*     g_expl   = NULL;

void audio_init(PlaydateAPI* pd) {
    g_pd = pd;
    const struct playdate_sound* snd = pd->sound;
    /* dedicated SFX channel (independent of any future music channel) */
    SoundChannel* ch = snd->channel->newChannel();
    snd->channel->setVolume(ch, SFX_CHANNEL_VOL);
    snd->addChannel(ch);

    /* burner: sustained noise note through a low-pass filter for a soft whoosh */
    g_burner = snd->synth->newSynth();
    snd->synth->setWaveform(g_burner, kWaveformNoise);
    snd->synth->setVolume(g_burner, 0.0f, 0.0f);             /* silent until cranking */
    snd->channel->addSource(ch, (SoundSource*)g_burner);
    TwoPoleFilter* lp = snd->effect->twopolefilter->newFilter();
    snd->effect->twopolefilter->setType(lp, kFilterTypeLowPass);
    snd->effect->twopolefilter->setFrequency(lp, BURNER_CUTOFF);
    snd->channel->addEffect(ch, (SoundEffect*)lp);
    snd->synth->playNote(g_burner, BURNER_NOTE_HZ, 1.0f, -1.0f, 0); /* indefinite */

    /* gun: short noise "spit" */
    g_gun = snd->synth->newSynth();
    snd->synth->setWaveform(g_gun, kWaveformNoise);
    snd->synth->setAttackTime(g_gun, 0.0f);
    snd->synth->setDecayTime(g_gun, GUN_DECAY);
    snd->synth->setSustainLevel(g_gun, 0.0f);
    snd->synth->setReleaseTime(g_gun, GUN_RELEASE);
    snd->synth->setVolume(g_gun, GUN_VOL, GUN_VOL);
    snd->channel->addSource(ch, (SoundSource*)g_gun);

    /* explosion: longer dull noise "boom" */
    g_expl = snd->synth->newSynth();
    snd->synth->setWaveform(g_expl, kWaveformNoise);
    snd->synth->setAttackTime(g_expl, 0.0f);
    snd->synth->setDecayTime(g_expl, EXPL_DECAY);
    snd->synth->setSustainLevel(g_expl, 0.0f);
    snd->synth->setReleaseTime(g_expl, EXPL_RELEASE);
    snd->synth->setVolume(g_expl, EXPL_VOL, EXPL_VOL);
    snd->channel->addSource(ch, (SoundSource*)g_expl);
}

void audio_burner(int on, float crank_speed) {
    if (!g_burner) return;
    float v = on ? audio_burner_level(crank_speed) : 0.0f;
    g_pd->sound->synth->setVolume(g_burner, v, v);
}

void audio_gun_fire(void) {
    if (!g_gun) return;
    g_pd->sound->synth->playNote(g_gun, GUN_NOTE_HZ, 1.0f, GUN_LEN, 0);
}

void audio_explosion(void) {
    if (!g_expl) return;
    g_pd->sound->synth->playNote(g_expl, EXPL_NOTE_HZ, 1.0f, EXPL_LEN, 0);
}
