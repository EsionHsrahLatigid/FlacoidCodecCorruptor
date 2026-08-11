#pragma once

#include "PluginProcessor.h"
#include <JuceHeader.h>

class CodecCorruptorAudioProcessorEditor final
    : public juce::AudioProcessorEditor {
public:
  explicit CodecCorruptorAudioProcessorEditor(CodecCorruptorAudioProcessor &);
  ~CodecCorruptorAudioProcessorEditor() override = default;

  void paint(juce::Graphics &) override;
  void resized() override;

private:
  CodecCorruptorAudioProcessor &processor;

  juce::Slider intensity, rate, duration, resync, stereo, wet, seed;
  juce::ToggleButton msMode;

  using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;
  using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

  std::unique_ptr<Attachment> intensityAtt, rateAtt, durationAtt, resyncAtt,
      stereoAtt, wetAtt, seedAtt;
  std::unique_ptr<ButtonAttachment> msAtt;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(
      CodecCorruptorAudioProcessorEditor)
};