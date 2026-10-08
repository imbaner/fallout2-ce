#ifndef FALLOUT_SOUND_TEMPO_H_
#define FALLOUT_SOUND_TEMPO_H_

#include <stddef.h>
#include <stdint.h>

#include <vector>

namespace fallout {

// CE: Plays 16-bit PCM [samples] ([frames] of [channels] interleaved samples
// at [rate]) [tempo] times as fast keeping its pitch (2.0 - half as long),
// returns the new samples. Used for sounds of animations sped up by the
// combat speed (sound.cc `soundSetTempo`).
//
// WSOLA: the sound is put together from short overlapping pieces of the
// original taken at the new pace, each shifted a little to where it continues
// the previous one best, and crossfaded with it - no clicks, no pitch change.
std::vector<int16_t> soundChangeTempo(const int16_t* samples, size_t frames, int channels, int rate, double tempo);

} // namespace fallout

#endif /* FALLOUT_SOUND_TEMPO_H_ */
