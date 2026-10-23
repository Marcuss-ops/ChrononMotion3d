#include "chrononmotion/motion/OfflineBaking.hpp"
#include "OfflineBakingInternal.hpp"

#include "chrononmotion/motion/Layer.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace chrononmotion::motion {
using detail::addVectorKey;
using detail::kMaximumBakedFrames;
using detail::kMaximumBakedWork;
using detail::validateFrameRange;
using detail::validateFrameTimes;

float AudioFeatureSample::value(AudioFeature feature) const noexcept {
    switch (feature) {
        case AudioFeature::Rms: return rms;
        case AudioFeature::Bass: return bass;
        case AudioFeature::Mid: return mid;
        case AudioFeature::Treble: return treble;
        case AudioFeature::Onset: return onset;
    }
    return 0.f;
}

std::vector<AudioFeatureSample> analyzePcmAudio(std::span<const float> pcm,
                                                const AudioAnalysisSpec& spec) {
    if (spec.sampleRate == 0 || spec.channels == 0 || spec.windowSize < 8 ||
        spec.windowSize > 8192 || spec.hopSize == 0 || spec.hopSize > spec.windowSize ||
        !std::isfinite(spec.bassBelowHz) || !std::isfinite(spec.midBelowHz) ||
        spec.bassBelowHz <= 0.f || spec.midBelowHz <= spec.bassBelowHz ||
        static_cast<double>(spec.midBelowHz) >= static_cast<double>(spec.sampleRate) * 0.5 ||
        spec.channels > 64 ||
        !std::isfinite(spec.onsetThreshold) || spec.onsetThreshold < 0.f ||
        !std::isfinite(spec.rmsThreshold) || spec.rmsThreshold < 0.f || spec.rmsThreshold > 1.f ||
        pcm.size() % spec.channels != 0)
        throw std::invalid_argument("analyzePcmAudio: invalid PCM format or analysis parameters");
    for (float sample : pcm)
        if (!std::isfinite(sample) || sample < -1.f || sample > 1.f)
            throw std::invalid_argument("analyzePcmAudio: normalized PCM samples must be finite and in [-1, 1]");
    const std::size_t frameCount = pcm.size() / spec.channels;
    if (frameCount == 0) return {};
    const std::size_t windowCount = 1 + (frameCount - 1) / spec.hopSize;
    if (windowCount > kMaximumBakedFrames)
        throw std::invalid_argument("analyzePcmAudio: PCM exceeds the offline window limit");
    const std::size_t windowCost = spec.windowSize * spec.windowSize;
    const std::size_t dftBinCount = spec.windowSize / 2;
    if (windowCost == 0 || windowCount > kMaximumBakedWork / windowCost ||
        (dftBinCount != 0 && windowCount > kMaximumBakedWork / spec.windowSize / dftBinCount))
        throw std::invalid_argument("analyzePcmAudio: direct DFT exceeds the deterministic offline operation budget");
    std::vector<AudioFeatureSample> output;
    output.reserve(windowCount);
    float previousRms = 0.f;
    for (std::size_t start = 0; start < frameCount; start += spec.hopSize) {
        double squareSum = 0.0;
        const std::size_t count = std::min(spec.windowSize, frameCount - start);
        std::vector<float> mono(count);
        for (std::size_t i = 0; i < count; ++i) {
            double value = 0.0;
            for (std::size_t channel = 0; channel < spec.channels; ++channel)
                value += pcm[(start + i) * spec.channels + channel];
            mono[i] = static_cast<float>(value / spec.channels);
            squareSum += static_cast<double>(mono[i]) * mono[i];
        }
        const float rms = static_cast<float>(std::sqrt(squareSum / count));
        double bandPower[3]{0.0, 0.0, 0.0};
        std::size_t bandBins[3]{0, 0, 0};
        for (std::size_t bin = 1; bin <= count / 2; ++bin) {
            const float frequency = static_cast<float>(bin) * static_cast<float>(spec.sampleRate) /
                                    static_cast<float>(count);
            unsigned band = frequency < spec.bassBelowHz ? 0u : frequency < spec.midBelowHz ? 1u : 2u;
            double real = 0.0, imaginary = 0.0;
            for (std::size_t sample = 0; sample < count; ++sample) {
                const float window = count > 1 ? 0.5f - 0.5f * std::cos(math::TWO_PI *
                    static_cast<float>(sample) / static_cast<float>(count - 1)) : 1.f;
                const double phase = math::TWO_PI * static_cast<double>(bin * sample) / static_cast<double>(count);
                const double weighted = static_cast<double>(mono[sample] * window);
                real += weighted * std::cos(phase);
                imaginary -= weighted * std::sin(phase);
            }
            bandPower[band] += real * real + imaginary * imaginary;
            ++bandBins[band];
        }
        AudioFeatureSample feature;
        feature.seconds = static_cast<float>(start) / static_cast<float>(spec.sampleRate);
        feature.rms = rms;
        float* bands[] = {&feature.bass, &feature.mid, &feature.treble};
        for (unsigned band = 0; band < 3; ++band) {
            const double denominator = static_cast<double>(count) * static_cast<double>(count) *
                                       static_cast<double>(std::max<std::size_t>(1, bandBins[band]));
            *bands[band] = static_cast<float>(std::sqrt(bandPower[band] / denominator) * 2.0);
        }
        const float rise = rms / std::max(previousRms, 1e-5f);
        feature.onset = previousRms <= spec.rmsThreshold && rms > spec.rmsThreshold &&
                        rise >= spec.onsetThreshold ? rms - previousRms : 0.f;
        if (!std::isfinite(feature.seconds) || !std::isfinite(feature.rms) ||
            !std::isfinite(feature.bass) || !std::isfinite(feature.mid) ||
            !std::isfinite(feature.treble) || !std::isfinite(feature.onset))
            throw std::invalid_argument("analyzePcmAudio: analysis overflowed finite output range");
        output.push_back(feature);
        previousRms = rms;
        if (start + spec.hopSize >= frameCount) break;
    }
    return output;
}

Track<float> bakeAudioDriverTrack(std::span<const AudioFeatureSample> features,
                                   const AudioDriverSpec& driver, float fps,
                                   int firstFrame, int lastFrame) {
    validateFrameRange(firstFrame, lastFrame);
    if (features.empty() || !std::isfinite(fps) || fps <= 0.f ||
        !std::isfinite(static_cast<float>(lastFrame) / fps) || !std::isfinite(1.f / fps) ||
        static_cast<unsigned>(driver.feature) > static_cast<unsigned>(AudioFeature::Onset) ||
        !std::isfinite(driver.multiplier) || !std::isfinite(driver.bias) ||
        !std::isfinite(driver.smoothing) || driver.smoothing < 0.f)
        throw std::invalid_argument("bakeAudioDriverTrack: features, fps and driver parameters must be valid");
    if (features.size() > kMaximumBakedFrames)
        throw std::invalid_argument("bakeAudioDriverTrack: feature count exceeds the offline sample budget");
    for (std::size_t i = 0; i < features.size(); ++i) {
        if (!std::isfinite(features[i].seconds) || features[i].seconds < 0.f ||
            (i && features[i].seconds <= features[i - 1].seconds) ||
            !std::isfinite(features[i].rms) || features[i].rms < 0.f ||
            !std::isfinite(features[i].bass) || features[i].bass < 0.f ||
            !std::isfinite(features[i].mid) || features[i].mid < 0.f ||
            !std::isfinite(features[i].treble) || features[i].treble < 0.f ||
            !std::isfinite(features[i].onset) || features[i].onset < 0.f)
            throw std::invalid_argument("bakeAudioDriverTrack: feature samples must be finite and strictly ordered");
    }
    Track<float> track;
    float smoothed = features.front().value(driver.feature);
    std::size_t upper = 0;
    const float dt = 1.f / fps;
    const std::size_t frameCount = static_cast<std::size_t>(lastFrame - firstFrame) + 1;
    validateFrameTimes(fps, firstFrame, lastFrame, "bakeAudioDriverTrack");
    for (std::size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        const int frame = firstFrame + static_cast<int>(frameIndex);
        const float time = static_cast<float>(frame) / fps;
        while (upper + 1 < features.size() && features[upper + 1].seconds < time) ++upper;
        float value = features[upper].value(driver.feature);
        if (upper + 1 < features.size()) {
            const auto& a = features[upper];
            const auto& b = features[upper + 1];
            const float amount = std::clamp((time - a.seconds) / (b.seconds - a.seconds), 0.f, 1.f);
            value = a.value(driver.feature) + (b.value(driver.feature) - a.value(driver.feature)) * amount;
        }
        if (driver.smoothing > 0.f) {
            const float alpha = 1.f - std::exp(-dt / driver.smoothing);
            smoothed += (value - smoothed) * alpha;
            value = smoothed;
        } else {
            smoothed = value;
        }
        const float driven = driver.bias + driver.multiplier * value;
        if (!std::isfinite(time) || !std::isfinite(value) || !std::isfinite(smoothed) || !std::isfinite(driven))
            throw std::invalid_argument("bakeAudioDriverTrack: output key is non-finite");
        track.add(time, driven);
    }
    return track;
}

void bakeAudioDriverToLayer(Layer& layer, std::span<const AudioFeatureSample> features,
                            const AudioDriverSpec& driver, AudioDriverProperty property,
                            const Vector3& axis, float fps, int firstFrame, int lastFrame) {
    if (!std::isfinite(axis.x) || !std::isfinite(axis.y) || !std::isfinite(axis.z))
        throw std::invalid_argument("bakeAudioDriverToLayer: axis must be finite");
    const auto scalar = bakeAudioDriverTrack(features, driver, fps, firstFrame, lastFrame);
    Track<Vector3>* vectorTrack = nullptr;
    Track<float>* opacityTrack = nullptr;
    switch (property) {
        case AudioDriverProperty::Position: vectorTrack = &layer.tracks.position; break;
        case AudioDriverProperty::Rotation: vectorTrack = &layer.tracks.rotation; break;
        case AudioDriverProperty::Scale: vectorTrack = &layer.tracks.scale; break;
        case AudioDriverProperty::Opacity: opacityTrack = &layer.tracks.opacity; break;
        default: throw std::invalid_argument("bakeAudioDriverToLayer: unsupported target property");
    }
    if (vectorTrack) {
        Track<Vector3> baked;
        for (const auto& key : scalar.keys()) {
            Vector3 base = property == AudioDriverProperty::Position ? layer.transform.position :
                           property == AudioDriverProperty::Rotation ? layer.transform.rotation : layer.transform.scale;
            if (!vectorTrack->empty()) base = vectorTrack->sample(key.time);
            Vector3 value;
            if (property == AudioDriverProperty::Scale) {
                value.set(base.x * (1.f + axis.x * key.value),
                          base.y * (1.f + axis.y * key.value),
                          base.z * (1.f + axis.z * key.value));
            } else {
                value.set(base.x + axis.x * key.value,
                          base.y + axis.y * key.value,
                          base.z + axis.z * key.value);
            }
            if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z))
                throw std::invalid_argument("bakeAudioDriverToLayer: vector output is non-finite");
            baked.add(key.time, value);
        }
        *vectorTrack = std::move(baked);
    } else if (opacityTrack) {
        Track<float> baked;
        for (const auto& key : scalar.keys()) {
            const float base = opacityTrack->empty() ? layer.transform.opacity : opacityTrack->sample(key.time);
            const float value = base + key.value;
            if (!std::isfinite(base) || !std::isfinite(value))
                throw std::invalid_argument("bakeAudioDriverToLayer: opacity output is non-finite");
            baked.add(key.time, std::clamp(value, 0.f, 1.f));
        }
        *opacityTrack = std::move(baked);
    }
}

} // namespace chrononmotion::motion
