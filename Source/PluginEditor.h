#pragma once

#include "PluginProcessor.h"
#include <JuceHeader.h>
#include <ehl/juce_design/EhlDesign.h>

class CodecCorruptorAudioProcessorEditor final
    : public juce::AudioProcessorEditor,
      private juce::Timer {
public:
  explicit CodecCorruptorAudioProcessorEditor(CodecCorruptorAudioProcessor &);
  ~CodecCorruptorAudioProcessorEditor() override;

  void paint(juce::Graphics &) override;
  void resized() override;

private:
  void timerCallback() override;
  void updateDisplay();

  CodecCorruptorAudioProcessor &audioProcessor;

  ehl::juce_design::LookAndFeel lookAndFeel;
  ehl::juce_design::ParameterDisplay display{ehl::juce_design::DisplayKind::bitcrusher};

  juce::Slider intensity, rate, duration, resync, stereo, wet, seed;
  juce::ToggleButton msMode;
  juce::Label intensityLabel, rateLabel, durationLabel, resyncLabel,
      stereoLabel, wetLabel, seedLabel;

  using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;
  using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

  std::unique_ptr<Attachment> intensityAtt, rateAtt, durationAtt, resyncAtt,
      stereoAtt, wetAtt, seedAtt;
  std::unique_ptr<ButtonAttachment> msAtt;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(
      CodecCorruptorAudioProcessorEditor)
};
