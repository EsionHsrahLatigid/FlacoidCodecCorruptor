#include "PluginEditor.h"

static void setupSlider(juce::Slider &s, const juce::String &name) {
  s.setName(name);
  ehl::juce_design::styleSlider(s);
}

static void setupLabel(juce::Label &label, const juce::String &text) {
  label.setText(text, juce::dontSendNotification);
  ehl::juce_design::styleLabel(label);
  label.setJustificationType(juce::Justification::centred);
}

CodecCorruptorAudioProcessorEditor::CodecCorruptorAudioProcessorEditor(
    CodecCorruptorAudioProcessor &p)
    : AudioProcessorEditor(&p), audioProcessor(p) {
  setLookAndFeel(&lookAndFeel);
  setupSlider(intensity, "Intensity");
  setupSlider(rate, "Rate");
  setupSlider(duration, "Duration");
  setupSlider(resync, "Resync");
  setupSlider(stereo, "StereoDamage");
  setupSlider(wet, "Wet");
  setupSlider(seed, "Seed");

  msMode.setButtonText("M/S");
  ehl::juce_design::styleToggle(msMode);

  setupLabel(intensityLabel, "INTENSITY");
  setupLabel(rateLabel, "RATE");
  setupLabel(durationLabel, "DURATION");
  setupLabel(resyncLabel, "RESYNC");
  setupLabel(stereoLabel, "STEREO");
  setupLabel(wetLabel, "WET");
  setupLabel(seedLabel, "SEED");

  addAndMakeVisible(intensity);
  addAndMakeVisible(rate);
  addAndMakeVisible(duration);
  addAndMakeVisible(resync);
  addAndMakeVisible(stereo);
  addAndMakeVisible(wet);
  addAndMakeVisible(seed);
  addAndMakeVisible(msMode);
  addAndMakeVisible(display);
  addAndMakeVisible(intensityLabel);
  addAndMakeVisible(rateLabel);
  addAndMakeVisible(durationLabel);
  addAndMakeVisible(resyncLabel);
  addAndMakeVisible(stereoLabel);
  addAndMakeVisible(wetLabel);
  addAndMakeVisible(seedLabel);

  auto &apvts = audioProcessor.apvts;
  intensityAtt = std::make_unique<Attachment>(apvts, "intensity", intensity);
  rateAtt = std::make_unique<Attachment>(apvts, "rate", rate);
  durationAtt = std::make_unique<Attachment>(apvts, "duration", duration);
  resyncAtt = std::make_unique<Attachment>(apvts, "resync", resync);
  stereoAtt = std::make_unique<Attachment>(apvts, "stereo", stereo);
  wetAtt = std::make_unique<Attachment>(apvts, "wet", wet);
  seedAtt = std::make_unique<Attachment>(apvts, "seed", seed);
  msAtt = std::make_unique<ButtonAttachment>(apvts, "ms", msMode);

  setResizable(true, true);
  setResizeLimits(ehl::juce_design::Metrics::minimumWidth,
                  ehl::juce_design::Metrics::minimumHeight,
                  ehl::juce_design::Metrics::maximumWidth,
                  ehl::juce_design::Metrics::maximumHeight);
  setSize(ehl::juce_design::Metrics::defaultWidth,
          ehl::juce_design::Metrics::defaultHeight);
  updateDisplay();
  startTimerHz(15);
}

CodecCorruptorAudioProcessorEditor::~CodecCorruptorAudioProcessorEditor() {
  stopTimer();
  setLookAndFeel(nullptr);
}

void CodecCorruptorAudioProcessorEditor::paint(juce::Graphics &g) {
  ehl::juce_design::paintEditorChrome(
      g, getLocalBounds(), "FlacoidCodecCorruptor",
      "frame predictor / residual corruption");
}

void CodecCorruptorAudioProcessorEditor::resized() {
  display.setBounds(ehl::juce_design::parameterDisplayArea(getLocalBounds()));
  ehl::juce_design::layoutLabelledControl(
      intensityLabel, intensity, ehl::juce_design::controlCell(getLocalBounds(), 0));
  ehl::juce_design::layoutLabelledControl(
      rateLabel, rate, ehl::juce_design::controlCell(getLocalBounds(), 1));
  ehl::juce_design::layoutLabelledControl(
      durationLabel, duration, ehl::juce_design::controlCell(getLocalBounds(), 2));
  ehl::juce_design::layoutLabelledControl(
      resyncLabel, resync, ehl::juce_design::controlCell(getLocalBounds(), 3));
  ehl::juce_design::layoutLabelledControl(
      stereoLabel, stereo, ehl::juce_design::controlCell(getLocalBounds(), 4));
  ehl::juce_design::layoutLabelledControl(
      wetLabel, wet, ehl::juce_design::controlCell(getLocalBounds(), 5));
  ehl::juce_design::layoutLabelledControl(
      seedLabel, seed, ehl::juce_design::controlCell(getLocalBounds(), 6));
  msMode.setBounds(ehl::juce_design::controlCell(getLocalBounds(), 7).reduced(8, 24));
}

void CodecCorruptorAudioProcessorEditor::timerCallback() { updateDisplay(); }

void CodecCorruptorAudioProcessorEditor::updateDisplay() {
  const auto normalized = [this](const char *id) {
    if (auto *parameter = audioProcessor.apvts.getParameter(id))
      return parameter->getValue();
    return 0.0f;
  };
  display.setValues({normalized("intensity"), normalized("rate"),
                     normalized("duration"), normalized("resync")});
}
