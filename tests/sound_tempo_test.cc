// Tempo change test (src/sound_tempo.cc): length, pitch, loudness and
// channels stay right, every shot of a burst stays, short sounds and tempo 1
// are left as they are.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <algorithm>
#include <vector>

#include "sound_tempo.h"

using fallout::soundChangeTempo;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "FAIL: line %d: %s\n", __LINE__, #condition);     \
            exit(EXIT_FAILURE);                                                \
        }                                                                      \
    } while (false)

namespace {

constexpr int kRate = 22050;

// Sines of [frequencies] (one per channel), [seconds] long.
std::vector<int16_t> sines(const std::vector<double>& frequencies, double seconds)
{
    int channels = static_cast<int>(frequencies.size());
    size_t frames = static_cast<size_t>(seconds * kRate);
    std::vector<int16_t> samples(frames * channels);
    for (size_t frame = 0; frame < frames; frame++) {
        for (int channel = 0; channel < channels; channel++) {
            samples[frame * channels + channel] = static_cast<int16_t>(10000.0 * sin(2.0 * M_PI * frequencies[channel] * frame / kRate));
        }
    }
    return samples;
}

// A burst: [shots] noise bursts [interval] s apart, each dying out in 15 ms.
std::vector<int16_t> burst(int shots, double interval)
{
    size_t frames = static_cast<size_t>((shots * interval + 0.2) * kRate);
    std::vector<int16_t> samples(frames);
    unsigned int noise = 12345;
    for (int shot = 0; shot < shots; shot++) {
        size_t start = static_cast<size_t>(shot * interval * kRate);
        for (size_t offset = 0; offset < static_cast<size_t>(0.05 * kRate); offset++) {
            noise = noise * 1103515245 + 12345;
            double value = (static_cast<int>((noise >> 16) % 2001) - 1000) * 20.0 * exp(-(offset / (0.015 * kRate)) * 3.0);
            samples[start + offset] = static_cast<int16_t>(value);
        }
    }
    return samples;
}

// Sign changes of [channel] per second.
double crossingsPerSecond(const std::vector<int16_t>& samples, int channels, int channel)
{
    size_t frames = samples.size() / channels;
    int crossings = 0;
    for (size_t frame = 1; frame < frames; frame++) {
        bool before = samples[(frame - 1) * channels + channel] >= 0;
        bool now = samples[frame * channels + channel] >= 0;
        if (before != now) {
            crossings++;
        }
    }
    return crossings * static_cast<double>(kRate) / frames;
}

double rms(const std::vector<int16_t>& samples)
{
    double sum = 0.0;
    for (int16_t sample : samples) {
        sum += static_cast<double>(sample) * sample;
    }
    return sqrt(sum / samples.size());
}

// Shots: rises of the 5 ms loudness over half the loudest.
int onsets(const std::vector<int16_t>& samples)
{
    size_t window = kRate / 200;
    std::vector<int> envelope;
    for (size_t start = 0; start + window <= samples.size(); start += window) {
        int peak = 0;
        for (size_t index = start; index < start + window; index++) {
            peak = std::max(peak, abs(samples[index]));
        }
        envelope.push_back(peak);
    }

    int loudest = 0;
    for (int value : envelope) {
        loudest = std::max(loudest, value);
    }

    int count = 0;
    bool loud = false;
    for (int value : envelope) {
        if (!loud && value > loudest / 2) {
            count++;
            loud = true;
        } else if (loud && value < loudest / 8) {
            loud = false;
        }
    }
    return count;
}

} // namespace

int main()
{
    // Tempo 1: unchanged.
    std::vector<int16_t> tone = sines({ 440.0 }, 1.0);
    CHECK(soundChangeTempo(tone.data(), tone.size(), 1, kRate, 1.0) == tone);

    // Twice as fast: half as long, the same pitch and loudness.
    std::vector<int16_t> fast = soundChangeTempo(tone.data(), tone.size(), 1, kRate, 2.0);
    CHECK(fabs(fast.size() - tone.size() / 2.0) < 0.05 * kRate);
    CHECK(fabs(crossingsPerSecond(fast, 1, 0) - crossingsPerSecond(tone, 1, 0)) < 0.02 * crossingsPerSecond(tone, 1, 0));
    CHECK(fabs(rms(fast) - rms(tone)) < 0.1 * rms(tone));

    // 1.5 times as fast.
    std::vector<int16_t> faster = soundChangeTempo(tone.data(), tone.size(), 1, kRate, 1.5);
    CHECK(fabs(faster.size() - tone.size() / 1.5) < 0.05 * kRate);

    // Stereo: each channel keeps its own pitch.
    std::vector<int16_t> stereo = sines({ 300.0, 600.0 }, 1.0);
    std::vector<int16_t> fastStereo = soundChangeTempo(stereo.data(), stereo.size() / 2, 2, kRate, 2.0);
    CHECK(fastStereo.size() % 2 == 0);
    CHECK(fabs(crossingsPerSecond(fastStereo, 2, 0) - 600.0) < 15.0);
    CHECK(fabs(crossingsPerSecond(fastStereo, 2, 1) - 1200.0) < 25.0);

    // A burst keeps every shot, twice as close.
    std::vector<int16_t> shots = burst(10, 0.1);
    CHECK(onsets(shots) == 10);
    std::vector<int16_t> fastShots = soundChangeTempo(shots.data(), shots.size(), 1, kRate, 2.0);
    CHECK(onsets(fastShots) == 10);
    CHECK(fastShots.size() < shots.size() * 0.6);

    // Too short to change.
    std::vector<int16_t> click = sines({ 1000.0 }, 0.02);
    CHECK(soundChangeTempo(click.data(), click.size(), 1, kRate, 2.0) == click);

    printf("PASS: sound tempo\n");
    return EXIT_SUCCESS;
}
