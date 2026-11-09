#include "MainComponent.h"

//==============================================================================
void LayoutView::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat().reduced (24.0f);
    const float side = std::min (area.getWidth(), area.getHeight());
    auto room = area.withSizeKeepingCentre (side, side);

    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.fillRect (room);
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.drawRect (room, 1.0f);
    g.setFont (11.0f);
    g.drawText ("FRONT", room.withHeight (16.0f).translated (0, -18.0f), juce::Justification::centred);

    // listener
    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (room.getCentre()));

    auto toScreen = [&] (const hoa::CubePosition& p) {
        return juce::Point<float> (room.getCentreX() + (float) p.x * side * 0.5f,
                                   room.getCentreY() - (float) p.y * side * 0.5f);
    };

    for (const auto& s : speakers)
    {
        if (s.isLFE)
            continue;
        const auto c = toScreen (s.cube);
        const float r = 5.0f + 5.0f * (1.0f - (float) s.cube.z * 0.6f);
        const auto colour = s.isBed ? juce::Colour (0xff4fc3f7)
                                    : juce::Colour (0xffffb74d).interpolatedWith (juce::Colour (0xffe57373), (float) s.cube.z);
        g.setColour (colour.withAlpha (0.9f));
        g.fillEllipse (juce::Rectangle<float> (r * 2, r * 2).withCentre (c));
        g.setColour (juce::Colours::white);
        g.setFont (10.0f);
        g.drawText (s.name, juce::Rectangle<float> (60.0f, 12.0f).withCentre (c.translated (0, r + 8.0f)),
                    juce::Justification::centred);
    }

    g.setFont (11.0f);
    g.setColour (juce::Colour (0xff4fc3f7));
    g.drawText ("bed", getLocalBounds().removeFromBottom (18).removeFromLeft (60), juce::Justification::centred);
    g.setColour (juce::Colour (0xffffb74d));
    g.drawText ("object (ear)", getLocalBounds().removeFromBottom (18).withTrimmedLeft (60).removeFromLeft (90), juce::Justification::centred);
    g.setColour (juce::Colour (0xffe57373));
    g.drawText ("object (top)", getLocalBounds().removeFromBottom (18).withTrimmedLeft (150).removeFromLeft (90), juce::Justification::centred);
}

//==============================================================================
class MainComponent::Worker : public juce::Thread
{
public:
    Worker (MainComponent& o, hoa::ConvertOptions opts) : juce::Thread ("hoa2adm"), owner (o), options (std::move (opts)) {}

    void run() override
    {
        juce::Component::SafePointer<MainComponent> safe (&owner);
        result = hoa::convert (
            options,
            [this] (double p, const juce::String&) {
                owner.progress = p;
                return ! threadShouldExit();
            },
            [safe] (const juce::String& line) {
                juce::MessageManager::callAsync ([safe, line] {
                    if (safe != nullptr)
                        safe->appendLog (line);
                });
            });
        finished = true;
    }

    MainComponent& owner;
    hoa::ConvertOptions options;
    hoa::ConvertResult result;
    std::atomic<bool> finished { false };
};

//==============================================================================
MainComponent::MainComponent()
{
    addAndMakeVisible (chooseButton);
    chooseButton.onClick = [this] { chooseInput(); };
    addAndMakeVisible (inputLabel);
    inputLabel.setText ("Drop a 64-ch (7th order) or smaller AmbiX WAV rendered from Reaper at 48 kHz", juce::dontSendNotification);
    inputLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);

    for (auto* l : { &layoutLabel, &normLabel, &orderLabel, &yawLabel, &lfeLabel })
        addAndMakeVisible (l);

    layoutBox.addItem ("7.1.4  (7.1 bed + 4 height objects, 12 tracks)", 1);
    layoutBox.addItem ("Extended  (+ wides, rear C, top ring, zenith, 20 tracks)", 2);
    layoutBox.addItem ("Dense  (+ fill-ins, mid & upper rings, 37 tracks)", 3);
    layoutBox.setSelectedId (2);
    layoutBox.onChange = [this] { updateLayoutView(); };
    addAndMakeVisible (layoutBox);

    normBox.addItem ("AmbiX (ACN / SN3D)", 1);
    normBox.addItem ("ACN / N3D", 2);
    normBox.setSelectedId (1);
    addAndMakeVisible (normBox);

    orderBox.addItem ("Auto", 100);
    for (int n = 1; n <= hoa::kMaxOrder; ++n)
        orderBox.addItem (juce::String (n), n);
    orderBox.setSelectedId (100);
    addAndMakeVisible (orderBox);

    bedToggle.setToggleState (true, juce::dontSendNotification);
    bedToggle.onClick = [this] { updateLayoutView(); lfeToggle.setEnabled (bedToggle.getToggleState()); };
    maxReToggle.setToggleState (true, juce::dontSendNotification);
    foldToggle.setToggleState (true, juce::dontSendNotification);
    trimToggle.setToggleState (true, juce::dontSendNotification);
    for (auto* t : { &bedToggle, &maxReToggle, &foldToggle, &lfeToggle, &trimToggle, &stemsToggle })
        addAndMakeVisible (t);

    yawSlider.setRange (-180.0, 180.0, 1.0);
    yawSlider.setValue (0.0);
    yawSlider.setTextValueSuffix (" deg");
    yawSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
    addAndMakeVisible (yawSlider);

    lfeSlider.setRange (-30.0, 0.0, 0.5);
    lfeSlider.setValue (-10.0);
    lfeSlider.setTextValueSuffix (" dB");
    lfeSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
    addAndMakeVisible (lfeSlider);

    convertButton.setEnabled (false);
    convertButton.onClick = [this] { startConvert(); };
    addAndMakeVisible (convertButton);
    addAndMakeVisible (progressBar);

    logView.setMultiLine (true);
    logView.setReadOnly (true);
    logView.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
    addAndMakeVisible (logView);
    addAndMakeVisible (layoutView);

    updateLayoutView();
    setSize (980, 640);
}

MainComponent::~MainComponent()
{
    if (worker != nullptr)
        worker->stopThread (10000);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto top = r.removeFromTop (28);
    chooseButton.setBounds (top.removeFromLeft (200));
    top.removeFromLeft (10);
    inputLabel.setBounds (top);
    r.removeFromTop (10);

    auto right = r.removeFromRight (380);
    layoutView.setBounds (right.removeFromTop (380));
    right.removeFromTop (8);

    auto left = r;
    auto row = [&] (juce::Label& l, juce::Component& c) {
        auto rr = left.removeFromTop (26);
        l.setBounds (rr.removeFromLeft (70));
        c.setBounds (rr.removeFromLeft (420));
        left.removeFromTop (6);
    };
    row (layoutLabel, layoutBox);
    row (normLabel, normBox);
    row (orderLabel, orderBox);
    row (yawLabel, yawSlider);

    auto toggles = left.removeFromTop (26 * 3 + 8);
    auto colA = toggles.removeFromLeft (250), colB = toggles;
    for (auto* t : { &bedToggle, &maxReToggle, &foldToggle })
        t->setBounds (colA.removeFromTop (28));
    for (auto* t : { &lfeToggle, &trimToggle, &stemsToggle })
        t->setBounds (colB.removeFromTop (28));
    row (lfeLabel, lfeSlider);

    auto conv = left.removeFromTop (32);
    convertButton.setBounds (conv.removeFromLeft (200));
    conv.removeFromLeft (10);
    progressBar.setBounds (conv);
    left.removeFromTop (8);
    // log spans the full width below whichever column ends lower
    logView.setBounds (getLocalBounds().reduced (12).withTop (std::max (left.getY(), right.getY())));
}

void MainComponent::filesDropped (const juce::StringArray& files, int, int)
{
    if (! files.isEmpty())
        loadInput (juce::File (files[0]));
}

void MainComponent::chooseInput()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose an ambisonic (AmbiX) file", inputFile, "*.wav;*.wave;*.rf64;*.aif;*.aiff;*.caf");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc) {
                              if (fc.getResult() != juce::File())
                                  loadInput (fc.getResult());
                          });
}

void MainComponent::loadInput (const juce::File& f)
{
    inputInfo = hoa::probeInput (f);
    if (! inputInfo.ok)
    {
        inputLabel.setText (inputInfo.error, juce::dontSendNotification);
        convertButton.setEnabled (false);
        return;
    }
    inputFile = f;
    juce::String s;
    s << f.getFileName() << "  -  " << inputInfo.numChannels << " ch, order " << inputInfo.order << ", "
      << inputInfo.sampleRate << " Hz, " << inputInfo.bitsPerSample << " bit, "
      << juce::String ((double) inputInfo.lengthFrames / inputInfo.sampleRate, 1) << " s";
    inputLabel.setText (s, juce::dontSendNotification);
    inputLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    convertButton.setEnabled (worker == nullptr);
    appendLog ("Loaded " + s);
    if ((int) inputInfo.sampleRate != 48000 && (int) inputInfo.sampleRate != 96000)
        appendLog ("Warning: Atmos needs 48 or 96 kHz - re-render from Reaper at 48 kHz.");
}

hoa::ConvertOptions MainComponent::gatherOptions() const
{
    hoa::ConvertOptions o;
    o.input = inputFile;
    o.layout = layoutBox.getSelectedId() == 1 ? hoa::LayoutPreset::Atmos714
             : layoutBox.getSelectedId() == 3 ? hoa::LayoutPreset::Dense
                                              : hoa::LayoutPreset::Extended;
    o.useBed = bedToggle.getToggleState();
    o.normalization = normBox.getSelectedId() == 2 ? hoa::Normalization::N3D : hoa::Normalization::SN3D;
    o.order = orderBox.getSelectedId() == 100 ? -1 : orderBox.getSelectedId();
    o.maxRE = maxReToggle.getToggleState();
    o.foldBelowHorizon = foldToggle.getToggleState();
    o.yawDeg = yawSlider.getValue();
    o.lfeEnabled = lfeToggle.getToggleState();
    o.lfeGainDb = lfeSlider.getValue();
    o.autoTrim = trimToggle.getToggleState();
    return o;
}

void MainComponent::updateLayoutView()
{
    const auto o = gatherOptions();
    layoutView.setSpeakers (hoa::makeLayout (o.layout, o.useBed));
}

void MainComponent::startConvert()
{
    if (inputFile == juce::File() || worker != nullptr)
        return;
    const auto suggested = inputFile.getSiblingFile (inputFile.getFileNameWithoutExtension() + "_atmos.wav");
    chooser = std::make_unique<juce::FileChooser> ("Save ADM BWF", suggested, "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc) {
                              auto out = fc.getResult();
                              if (out == juce::File())
                                  return;
                              if (out == inputFile)
                              {
                                  appendLog ("Refusing to overwrite the input file.");
                                  return;
                              }
                              auto o = gatherOptions();
                              o.output = out.withFileExtension ("wav");
                              if (stemsToggle.getToggleState())
                                  o.stemsDirectory = o.output.getSiblingFile (o.output.getFileNameWithoutExtension() + "_stems");
                              progress = 0;
                              convertButton.setEnabled (false);
                              chooseButton.setEnabled (false);
                              appendLog ("---");
                              worker = std::make_unique<Worker> (*this, o);
                              worker->startThread();
                              startTimerHz (10);
                          });
}

void MainComponent::timerCallback()
{
    if (worker == nullptr || ! worker->finished)
        return;
    stopTimer();
    worker->stopThread (1000);
    const auto res = worker->result;
    worker.reset();
    if (! res.ok)
        appendLog ("Error: " + res.message);
    else
        appendLog ("Done. In Logic: File > Import > ADM BWF..., pick this file (Atmos at 48 kHz).");
    progress = res.ok ? 1.0 : 0.0;
    convertButton.setEnabled (true);
    chooseButton.setEnabled (true);
}

void MainComponent::appendLog (const juce::String& line)
{
    logView.moveCaretToEnd();
    logView.insertTextAtCaret (line + "\n");
}
