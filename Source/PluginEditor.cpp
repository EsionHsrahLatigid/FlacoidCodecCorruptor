#include "PluginEditor.h"

static void setupSlider(juce::Slider &s, const juce::String &name) {
  s.setName(name);
  s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
  s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 18);
}

CodecCorruptorAudioProcessorEditor::CodecCorruptorAudioProcessorEditor(
    CodecCorruptorAudioProcessor &p)
    : AudioProcessorEditor(&p), processor(p) {
  setupSlider(intensity, "Intensity");
  setupSlider(rate, "Rate");
  setupSlider(duration, "Duration");
  setupSlider(resync, "Resync");
  setupSlider(stereo, "StereoDamage");
  setupSlider(wet, "Wet");
  setupSlider(seed, "Seed");

  msMode.setButtonText("M/S");

  addAndMakeVisible(intensity);
  addAndMakeVisible(rate);
  addAndMakeVisible(duration);
  addAndMakeVisible(resync);
  addAndMakeVisible(stereo);
  addAndMakeVisible(wet);
  addAndMakeVisible(seed);
  addAndMakeVisible(msMode);

  auto &apvts = processor.apvts;
  intensityAtt = std::make_unique<Attachment>(apvts, "intensity", intensity);
  rateAtt = std::make_unique<Attachment>(apvts, "rate", rate);
  durationAtt = std::make_unique<Attachment>(apvts, "duration", duration);
  resyncAtt = std::make_unique<Attachment>(apvts, "resync", resync);
  stereoAtt = std::make_unique<Attachment>(apvts, "stereo", stereo);
  wetAtt = std::make_unique<Attachment>(apvts, "wet", wet);
  seedAtt = std::make_unique<Attachment>(apvts, "seed", seed);
  msAtt = std::make_unique<ButtonAttachment>(apvts, "ms", msMode);

  setSize(620, 220);
}

void CodecCorruptorAudioProcessorEditor::paint(juce::Graphics &g) {
  g.fillAll(juce::Colours::black);
  g.setColour(juce::Colours::white);
  g.setFont(14.0f);
  g.drawText("Codec Corruptor (frame-based predictor/residual corruption)", 10,
             10, getWidth() - 20, 20, juce::Justification::left);
}

void CodecCorruptorAudioProcessorEditor::resized() {
  const int pad = 10;
  const int top = 40;
  const int w = 90;
  const int h = 140;
  const int gap = 10;

  int x = pad;

  intensity.setBounds(x, top, w, h);
  x += w + gap;
  rate.setBounds(x, top, w, h);
  x += w + gap;
  duration.setBounds(x, top, w, h);
  x += w + gap;
  resync.setBounds(x, top, w, h);
  x += w + gap;
  stereo.setBounds(x, top, w, h);
  x += w + gap;
  wet.setBounds(x, top, w, h);
  x += w + gap;
  seed.setBounds(x, top, w, h);
  x += w + gap;

  msMode.setBounds(pad, 190, 60, 20);
}