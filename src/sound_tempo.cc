#include "sound_tempo.h"

#include <algorithm>
#include <cmath>

namespace fallout {

namespace {

    // Piece length: short enough for shots and hits to stay sharp, long
    // enough for the pitch to hold.
    constexpr int kPieceMs = 30;

    // How far a piece may move from its place to continue the previous one
    // best.
    constexpr int kSearchMs = 10;

    // Tempos this close to 1 aren't worth it.
    constexpr double kTempoEpsilon = 0.01;

    // Attacks (shots of a burst, hits): loudness measured in 5 ms windows;
    // an attack is a window over 4 times as loud as the 20 ms before it, at
    // most 30 dB quieter than the loudest one, at least 40 ms after the
    // previous attack.
    constexpr int kAttackWindowMs = 5;
    constexpr int kAttackHistoryWindows = 4;
    constexpr double kAttackRise = 4.0;
    constexpr double kAttackFloor = 0.001;
    constexpr int kAttackGapMs = 40;

    // A part cut short fades out over this long into the next attack.
    constexpr int kFadeOutMs = 4;

    struct Pcm {
        const int16_t* samples;
        const float* mono;
        int channels;
        int rate;
    };

    // WSOLA over frames [from, to) of [pcm] at [tempo]: the first piece as
    // it is, then pieces taken at the new pace, each at the place near it
    // where its beginning is most like the previous piece's own continuation,
    // crossfaded with it. Too short to have pieces - as it is.
    std::vector<float> stretch(const Pcm& pcm, size_t from, size_t to, double tempo)
    {
        int channels = pcm.channels;
        size_t frames = to - from;
        size_t piece = std::max<size_t>(static_cast<size_t>(pcm.rate) * kPieceMs / 1000, 16);
        size_t overlap = piece / 2;
        size_t hop = piece - overlap;
        size_t search = static_cast<size_t>(pcm.rate) * kSearchMs / 1000;

        std::vector<float> result;
        auto append = [&](size_t start, size_t end) {
            for (size_t index = (from + start) * channels; index < (from + end) * channels; index++) {
                result.push_back(pcm.samples[index]);
            }
        };

        if (frames < piece + search * 2) {
            append(0, frames);
            return result;
        }

        const float* mono = pcm.mono + from;

        // Normalized correlation of the overlap at [start] with the one at
        // [continuation], every [step]-th sample.
        auto similarity = [&](size_t start, size_t continuation, size_t step) {
            double correlation = 0.0;
            double energy = 1.0;
            for (size_t offset = 0; offset < overlap; offset += step) {
                float value = mono[start + offset];
                correlation += value * mono[continuation + offset];
                energy += value * value;
            }
            return correlation / std::sqrt(energy);
        };

        result.reserve((static_cast<size_t>(frames / tempo) + piece) * channels);
        append(0, piece);

        size_t previous = 0;
        double inputHop = hop * tempo;
        size_t lastStart = frames - piece;
        for (size_t index = 1;; index++) {
            double ideal = index * inputHop;
            if (ideal > static_cast<double>(lastStart)) {
                break;
            }

            // What the next piece should look like where they overlap (the
            // result ends with its start).
            size_t continuation = previous + hop;

            size_t center = static_cast<size_t>(ideal);
            size_t first = center > search ? center - search : 0;
            size_t last = std::min(center + search, lastStart);

            // Every other start on every other sample, then the neighbours
            // of the best one on all of them.
            size_t best = center;
            double bestScore = -1.0e30;
            for (size_t start = first; start <= last; start += 2) {
                double score = similarity(start, continuation, 2);
                if (score > bestScore) {
                    bestScore = score;
                    best = start;
                }
            }

            size_t coarse = best;
            bestScore = similarity(coarse, continuation, 1);
            for (size_t start = coarse > first ? coarse - 1 : first; start <= std::min(coarse + 1, last); start++) {
                double score = similarity(start, continuation, 1);
                if (score > bestScore) {
                    bestScore = score;
                    best = start;
                }
            }

            size_t fadeStart = result.size() / channels - overlap;
            for (size_t offset = 0; offset < overlap; offset++) {
                float weight = 0.5f - 0.5f * std::cos(static_cast<float>(M_PI) * (offset + 0.5f) / overlap);
                for (int channel = 0; channel < channels; channel++) {
                    float& out = result[(fadeStart + offset) * channels + channel];
                    out = out * (1.0f - weight) + pcm.samples[(from + best + offset) * channels + channel] * weight;
                }
            }
            append(best + overlap, best + piece);

            previous = best;
        }

        // The rest after the last piece (the fade out) continues it.
        append(previous + piece, frames);

        return result;
    }

    // Frames where attacks begin, the first is 0.
    std::vector<size_t> findAttacks(const Pcm& pcm, size_t frames)
    {
        std::vector<size_t> attacks = { 0 };

        size_t window = std::max<size_t>(static_cast<size_t>(pcm.rate) * kAttackWindowMs / 1000, 1);
        size_t gap = static_cast<size_t>(pcm.rate) * kAttackGapMs / 1000;

        std::vector<double> energies(frames / window);
        double loudest = 0.0;
        for (size_t index = 0; index < energies.size(); index++) {
            double energy = 0.0;
            for (size_t frame = index * window; frame < (index + 1) * window; frame++) {
                energy += static_cast<double>(pcm.mono[frame]) * pcm.mono[frame];
            }
            energies[index] = energy;
            loudest = std::max(loudest, energy);
        }

        for (size_t index = kAttackHistoryWindows; index < energies.size(); index++) {
            double before = 0.0;
            for (size_t previous = index - kAttackHistoryWindows; previous < index; previous++) {
                before += energies[previous];
            }
            before /= kAttackHistoryWindows;

            size_t frame = index * window;
            if (energies[index] > before * kAttackRise
                && energies[index] > loudest * kAttackFloor
                && frame >= attacks.back() + gap) {
                attacks.push_back(frame);
            }
        }

        return attacks;
    }

} // namespace

std::vector<int16_t> soundChangeTempo(const int16_t* samples, size_t frames, int channels, int rate, double tempo)
{
    if (samples == nullptr || frames == 0 || channels <= 0 || rate <= 0) {
        return std::vector<int16_t>();
    }

    if (tempo <= 0.0 || std::fabs(tempo - 1.0) < kTempoEpsilon) {
        return std::vector<int16_t>(samples, samples + frames * channels);
    }

    // Parts are matched on the channels mixed together.
    std::vector<float> mono(frames);
    for (size_t frame = 0; frame < frames; frame++) {
        float sum = 0.0f;
        for (int channel = 0; channel < channels; channel++) {
            sum += samples[frame * channels + channel];
        }
        mono[frame] = sum / channels;
    }

    Pcm pcm = { samples, mono.data(), channels, rate };

    // Every attack starts at its place in the new time, as it is: only the
    // parts between attacks are shortened (stretched, cut short with a fade
    // out where the next attack comes sooner). So no shot of a burst is
    // lost, doubled or blurred. A sound without attacks is one part.
    std::vector<size_t> attacks = findAttacks(pcm, frames);
    size_t fade = std::max<size_t>(static_cast<size_t>(rate) * kFadeOutMs / 1000, 1);

    std::vector<float> mixed;
    for (size_t index = 0; index < attacks.size(); index++) {
        bool lastPart = index + 1 == attacks.size();
        size_t from = attacks[index];
        size_t to = lastPart ? frames : attacks[index + 1];
        size_t start = static_cast<size_t>(std::lround(from / tempo));

        std::vector<float> part = stretch(pcm, from, to, tempo);
        size_t partFrames = part.size() / channels;

        // Cut short before the next attack (fading out under it).
        if (!lastPart) {
            size_t next = static_cast<size_t>(std::lround(to / tempo));
            size_t room = next - start + fade;
            if (partFrames > room) {
                partFrames = room;
                for (size_t offset = 0; offset < fade; offset++) {
                    float weight = static_cast<float>(fade - offset) / (fade + 1);
                    for (int channel = 0; channel < channels; channel++) {
                        part[(partFrames - fade + offset) * channels + channel] *= weight;
                    }
                }
            }
        }

        size_t end = start + partFrames;
        if (mixed.size() < end * channels) {
            mixed.resize(end * channels, 0.0f);
        }

        for (size_t sample = 0; sample < partFrames * channels; sample++) {
            mixed[start * channels + sample] += part[sample];
        }
    }

    std::vector<int16_t> result(mixed.size());
    for (size_t index = 0; index < mixed.size(); index++) {
        result[index] = static_cast<int16_t>(std::clamp(std::lround(mixed[index]), -32768L, 32767L));
    }

    return result;
}

} // namespace fallout
