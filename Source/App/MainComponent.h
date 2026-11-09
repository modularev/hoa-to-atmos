#pragma once

#include "../Core/Converter.h"

#include <juce_gui_basics/juce_gui_basics.h>

/** Top-down view of the Atmos room cube with the current virtual speakers. */
class LayoutView : public juce::Component
{
public:
    void setSpeakers (std::vector<hoa::VirtualSpeaker> s)
    {
        speakers = std::move (s);
        repaint();
    }
    void paint (juce::Graphics&) override;

private:
    std::vector<hoa::VirtualSpeaker> speakers;
};

class MainComponent : public juce::Component,
                      public juce::FileDragAndDropTarget,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    class Worker;

    void chooseInput();
    void loadInput (const juce::File&);
    void startConvert();
    void updateLayoutView();
    hoa::ConvertOptions gatherOptions() const;
    void appendLog (const juce::String&);
    void timerCallback() override;

    juce::File inputFile;
    hoa::InputInfo inputInfo;

    juce::TextButton chooseButton { "Choose ambisonic WAV..." };
    juce::Label inputLabel;

    juce::Label layoutLabel { {}, "Layout" }, normLabel { {}, "Input" }, orderLabel { {}, "Order" },
        yawLabel { {}, "Yaw" }, lfeLabel { {}, "LFE gain" };
    juce::ComboBox layoutBox, normBox, orderBox;
    juce::ToggleButton bedToggle { "7.1 bed (else all objects)" }, maxReToggle { "max-rE weighting" },
        foldToggle { "Fold below-horizon to ear level" }, lfeToggle { "Feed LFE from W" },
        trimToggle { "Auto-trim if clipping" }, stemsToggle { "Also write mono stems" };
    juce::Slider yawSlider, lfeSlider;

    juce::TextButton convertButton { "Convert to ADM BWF..." };
    double progress = 0;
    juce::ProgressBar progressBar { progress };
    juce::TextEditor logView;
    LayoutView layoutView;

    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<Worker> worker;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
