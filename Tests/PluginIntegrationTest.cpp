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
    processor.prepareToPlay(sampleRate, 512);
    int generatedSamples = 0;
    const int blockSizes[] { 32, 128, 512, 1024, 256, 64 };

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

    if (passed)
        std::cout << "FlacoidCodecCorruptor integration checks passed\n";
    return passed ? 0 : 1;
}
