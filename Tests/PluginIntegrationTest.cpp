#include "PluginProcessor.h"

#include <cmath>
#include <iostream>

namespace
{
bool check(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "[FAIL] " << message << '\n';
    return condition;
}

void setFloatParameter(CodecCorruptorAudioProcessor& processor, const char* parameterID, float value)
{
    if (auto* parameter = processor.apvts.getParameter(parameterID))
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

bool samplesAreSilent(const juce::AudioBuffer<float>& audio, int startSample, int numSamples)
{
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        for (int sample = startSample; sample < startSample + numSamples; ++sample)
            if (std::abs(audio.getSample(channel, sample)) > 1.0e-7f)
                return false;

    return true;
}

bool samplesAreNotDry(const juce::AudioBuffer<float>& audio, int startSample, int numSamples, float leftDryValue, float rightDryValue)
{
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
    {
        const auto dryValue = channel == 0 ? leftDryValue : rightDryValue;
        for (int sample = startSample; sample < startSample + numSamples; ++sample)
            if (std::abs(audio.getSample(channel, sample) - dryValue) <= 1.0e-7f)
                return false;
    }

    return true;
}

int findFirstAudibleSample(CodecCorruptorAudioProcessor& processor, const int* blockSizes, int numBlocks)
{
    int firstAudible = -1;
    int globalSample = 0;
    bool impulsePending = true;

    for (int blockIndex = 0; blockIndex < numBlocks; ++blockIndex)
    {
        juce::AudioBuffer<float> audio(2, blockSizes[blockIndex]);
        audio.clear();

        if (impulsePending)
        {
            audio.setSample(0, 0, 0.5f);
            audio.setSample(1, 0, -0.5f);
            impulsePending = false;
        }

        juce::MidiBuffer midi;
        processor.processBlock(audio, midi);

        for (int sample = 0; sample < audio.getNumSamples(); ++sample)
        {
            if (std::abs(audio.getSample(0, sample)) > 1.0e-5f
                || std::abs(audio.getSample(1, sample)) > 1.0e-5f)
            {
                firstAudible = globalSample + sample;
                break;
            }
        }

        if (firstAudible >= 0)
            break;

        globalSample += audio.getNumSamples();
    }

    return firstAudible;
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI initialiseJuce;
    CodecCorruptorAudioProcessor processor;
    bool passed = true;

    passed &= check(processor.getName() == "FlacoidCodecCorruptor", "product name should match EHL identity");
    passed &= check(!processor.acceptsMidi(), "processor should not accept MIDI");
    passed &= check(!processor.isMidiEffect(), "processor should be an audio effect");

    juce::AudioProcessor::BusesLayout stereo;
    stereo.inputBuses.add(juce::AudioChannelSet::stereo());
    stereo.outputBuses.add(juce::AudioChannelSet::stereo());
    passed &= check(processor.isBusesLayoutSupported(stereo), "stereo layout should be supported");

    auto* wet = processor.apvts.getParameter("wet");
    passed &= check(wet != nullptr, "wet parameter should exist");
    if (wet != nullptr)
    {
        wet->setValueNotifyingHost(wet->convertTo0to1(0.25f));
        juce::MemoryBlock state;
        processor.getStateInformation(state);
        wet->setValueNotifyingHost(wet->convertTo0to1(0.75f));
        processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        passed &= check(std::abs(processor.apvts.getRawParameterValue("wet")->load() - 0.25f) < 0.001f,
                        "APVTS state should round-trip");
    }

    constexpr double sampleRate = 44100.0;

    {
        CodecCorruptorAudioProcessor latencyProcessor;
        setFloatParameter(latencyProcessor, "wet", 1.0f);
        latencyProcessor.prepareToPlay(sampleRate, 512);

        juce::AudioBuffer<float> audio(2, 128);
        audio.clear();
        for (int channel = 0; channel < audio.getNumChannels(); ++channel)
            for (int sample = 0; sample < audio.getNumSamples(); ++sample)
                audio.setSample(channel, sample, channel == 0 ? 0.375f : -0.375f);

        juce::MidiBuffer midi;
        latencyProcessor.processBlock(audio, midi);

        passed &= check(samplesAreSilent(audio, 0, audio.getNumSamples()),
                        "wet variable-block cold start should output latency silence, not current dry input");
    }

    {
        CodecCorruptorAudioProcessor latencyProcessor;
        setFloatParameter(latencyProcessor, "wet", 1.0f);
        latencyProcessor.prepareToPlay(sampleRate, 512);

        juce::AudioBuffer<float> audio(2, 768);
        audio.clear();
        for (int channel = 0; channel < audio.getNumChannels(); ++channel)
            for (int sample = 0; sample < audio.getNumSamples(); ++sample)
                audio.setSample(channel, sample, channel == 0 ? 0.25f : -0.25f);

        juce::MidiBuffer midi;
        latencyProcessor.processBlock(audio, midi);

        passed &= check(samplesAreSilent(audio, 0, 512),
                        "wet variable-block oversized cold start should output one frame of latency silence");
        passed &= check(samplesAreNotDry(audio, 512, 256, 0.25f, -0.25f),
                        "wet variable-block delayed output should not leave current dry input after latency silence");
    }

    {
        const float wetValues[] { 0.0f, 0.5f, 1.0f };
        const int exactBlocks[] { 512, 512, 512 };
        const int irregularBlocks[] { 128, 384, 257, 255, 128 };

        for (const auto wetValue : wetValues)
        {
            CodecCorruptorAudioProcessor exactProcessor;
            setFloatParameter(exactProcessor, "wet", wetValue);
            setFloatParameter(exactProcessor, "intensity", 0.0f);
            exactProcessor.prepareToPlay(sampleRate, 512);
            passed &= check(findFirstAudibleSample(exactProcessor, exactBlocks, 3) == 512,
                            "exact frame blocks should make an input impulse first audible after reported latency");

            CodecCorruptorAudioProcessor irregularProcessor;
            setFloatParameter(irregularProcessor, "wet", wetValue);
            setFloatParameter(irregularProcessor, "intensity", 0.0f);
            irregularProcessor.prepareToPlay(sampleRate, 512);
            passed &= check(findFirstAudibleSample(irregularProcessor, irregularBlocks, 5) == 512,
                            "irregular variable blocks should make an input impulse first audible after reported latency");
        }
    }

    processor.prepareToPlay(sampleRate, 512);
    int generatedSamples = 0;
    const int blockSizes[] { 32, 128, 512, 1024, 256, 64, 2048, 17, 511, 513 };

    for (const auto blockSize : blockSizes)
    {
        juce::AudioBuffer<float> audio(2, blockSize);
        for (int sample = 0; sample < blockSize; ++sample)
        {
            const auto value = static_cast<float>(0.2 * std::sin(2.0 * juce::MathConstants<double>::pi
                                                                 * 220.0 * generatedSamples / sampleRate));
            audio.setSample(0, sample, value);
            audio.setSample(1, sample, value);
            ++generatedSamples;
        }

        juce::MidiBuffer midi;
        processor.processBlock(audio, midi);

        for (int channel = 0; channel < audio.getNumChannels(); ++channel)
            for (int sample = 0; sample < audio.getNumSamples(); ++sample)
                passed &= check(std::isfinite(audio.getSample(channel, sample)), "processed audio should remain finite");
    }

    for (int cycle = 0; cycle < 12; ++cycle)
    {
        const int blockSize = (cycle % 2 == 0 ? 37 : 1009);
        juce::AudioBuffer<float> audio(2, blockSize);
        for (int sample = 0; sample < blockSize; ++sample)
        {
            const auto value = static_cast<float>(0.15 * std::sin(2.0 * juce::MathConstants<double>::pi
                                                                  * 330.0 * generatedSamples / sampleRate));
            audio.setSample(0, sample, value);
            audio.setSample(1, sample, -value);
            ++generatedSamples;
        }

        juce::MidiBuffer midi;
        processor.processBlock(audio, midi);

        for (int channel = 0; channel < audio.getNumChannels(); ++channel)
            for (int sample = 0; sample < audio.getNumSamples(); ++sample)
                passed &= check(std::isfinite(audio.getSample(channel, sample)),
                                "variable-size processed audio should remain finite");
    }

    if (passed)
        std::cout << "FlacoidCodecCorruptor integration checks passed\n";
    return passed ? 0 : 1;
}
