/*
  ==============================================================================

  This is an automatically generated GUI class created by the Introjucer!

  Be careful when adding custom code to these files, as only the code within
  the "//[xyz]" and "//[/xyz]" sections will be retained when the file is loaded
  and re-saved.

  Created with Introjucer version: 4.1.0

  ------------------------------------------------------------------------------

  The Introjucer is part of the JUCE library - "Jules' Utility Class Extensions"
  Copyright (c) 2015 - ROLI Ltd.

  ==============================================================================
*/

#ifndef __JUCE_HEADER_450C07F5C14097B8__
#define __JUCE_HEADER_450C07F5C14097B8__

//[Headers]     -- You can add your own extra header files here --
#include <array>
#include "JuceHeader.h"
#include "PluginProcessor.h"
//[/Headers]



//==============================================================================
/**
                                                                    //[Comments]
	This is a GUI for the OPL2 VST plugin, created in Juce.
                                                                    //[/Comments]
*/
class PluginGui  : public AudioProcessorEditor,
                   public FileDragAndDropTarget,
                   public DragAndDropContainer,
                   public Timer,
                   public ComboBoxListener,
                   public SliderListener,
                   public ButtonListener
{
public:
    //==============================================================================
    PluginGui (AdlibBlasterAudioProcessor* ownerFilter);
    ~PluginGui();

    //==============================================================================
    //[UserMethods]     -- You can add your own custom methods in this section.
	void updateFromParameters();
    bool isInterestedInFileDrag (const StringArray& files);
    void fileDragEnter (const StringArray& files, int x, int y);
    void fileDragMove (const StringArray& files, int x, int y);
    void fileDragExit (const StringArray& files);
    void filesDropped (const StringArray& files, int x, int y);
	void timerCallback();
	void setRecordButtonState(bool recording);
    //[/UserMethods]

    void paint (Graphics& g);
    void resized();
    void comboBoxChanged (ComboBox* comboBoxThatHasChanged);
    void sliderValueChanged (Slider* sliderThatWasMoved);
    void buttonClicked (Button* buttonThatWasClicked);

    // Binary resources:
    static const char* full_sine_png;
    static const int full_sine_pngSize;
    static const char* half_sine_png;
    static const int half_sine_pngSize;
    static const char* abs_sine_png;
    static const int abs_sine_pngSize;
    static const char* quarter_sine_png;
    static const int quarter_sine_pngSize;
    static const char* camel_sine_png;
    static const int camel_sine_pngSize;
    static const char* alternating_sine_png;
    static const int alternating_sine_pngSize;
    static const char* square_png;
    static const int square_pngSize;
    static const char* logarithmic_saw_png;
    static const int logarithmic_saw_pngSize;
    static const char* channeloff_png;
    static const int channeloff_pngSize;
    static const char* channelon_png;
    static const int channelon_pngSize;


private:
    //[UserVariables]   -- You can add your own custom variables in this section.
	static const uint32 COLOUR_MID = 0xff007f00;
	static const uint32 COLOUR_RECORDING = 0xffff0000;
	AdlibBlasterAudioProcessor* processor;
	std::array<ScopedPointer<Label>, Hiopl::CHANNELS> channels;
	TooltipWindow tooltipWindow;
    //[/UserVariables]

    //==============================================================================
    ScopedPointer<GroupComponent> groupComponent;
    ScopedPointer<ComboBox> frequencyComboBox;
    ScopedPointer<Label> frequencyLabel;
    ScopedPointer<Slider> aSlider;
    ScopedPointer<Label> aLabel;
    ScopedPointer<Slider> dSlider;
    ScopedPointer<Label> dLabel;
    ScopedPointer<Slider> sSlider;
    ScopedPointer<Label> dLabel2;
    ScopedPointer<Slider> rSlider;
    ScopedPointer<Label> rLabel;
    ScopedPointer<Slider> attenuationSlider;
    ScopedPointer<Label> attenuationLabel;
    ScopedPointer<Label> dbLabel;
    ScopedPointer<ImageButton> sineImageButton;
    ScopedPointer<ImageButton> halfsineImageButton;
    ScopedPointer<ImageButton> abssineImageButton;
    ScopedPointer<ImageButton> quartersineImageButton;
    ScopedPointer<Label> waveLabel;
    ScopedPointer<ToggleButton> tremoloButton;
    ScopedPointer<ToggleButton> vibratoButton;
    ScopedPointer<ToggleButton> sustainButton;
    ScopedPointer<ToggleButton> keyscaleEnvButton;
    ScopedPointer<Label> frequencyLabel2;
    ScopedPointer<Label> dbLabel2;
    ScopedPointer<GroupComponent> groupComponent2;
    ScopedPointer<ComboBox> frequencyComboBox2;
    ScopedPointer<Label> frequencyLabel3;
    ScopedPointer<Slider> aSlider2;
    ScopedPointer<Label> aLabel2;
    ScopedPointer<Slider> dSlider2;
    ScopedPointer<Label> dLabel3;
    ScopedPointer<Slider> sSlider2;
    ScopedPointer<Label> dLabel4;
    ScopedPointer<Slider> rSlider2;
    ScopedPointer<Label> rLabel2;
    ScopedPointer<Slider> attenuationSlider2;
    ScopedPointer<Label> attenuationLabel2;
    ScopedPointer<Label> dbLabel3;
    ScopedPointer<ImageButton> sineImageButton2;
    ScopedPointer<ImageButton> halfsineImageButton2;
    ScopedPointer<ImageButton> abssineImageButton2;
    ScopedPointer<ImageButton> quartersineImageButton2;
    ScopedPointer<Label> waveLabel2;
    ScopedPointer<ToggleButton> tremoloButton2;
    ScopedPointer<ToggleButton> vibratoButton2;
    ScopedPointer<ToggleButton> sustainButton2;
    ScopedPointer<ToggleButton> keyscaleEnvButton2;
    ScopedPointer<Label> frequencyLabel4;
    ScopedPointer<GroupComponent> groupComponent3;
    ScopedPointer<Slider> tremoloSlider;
    ScopedPointer<Label> frequencyLabel5;
    ScopedPointer<Label> dbLabel5;
    ScopedPointer<Slider> vibratoSlider;
    ScopedPointer<Label> frequencyLabel6;
    ScopedPointer<Label> dbLabel6;
    ScopedPointer<Slider> feedbackSlider;
    ScopedPointer<Label> frequencyLabel7;
    ScopedPointer<ComboBox> velocityComboBox;
    ScopedPointer<Label> attenuationLabel3;
    ScopedPointer<ComboBox> velocityComboBox2;
    ScopedPointer<Label> attenuationLabel4;
    ScopedPointer<ImageButton> alternatingsineImageButton;
    ScopedPointer<ImageButton> camelsineImageButton;
    ScopedPointer<ImageButton> squareImageButton;
    ScopedPointer<ImageButton> logsawImageButton;
    ScopedPointer<ImageButton> alternatingsineImageButton2;
    ScopedPointer<ImageButton> camelsineImageButton2;
    ScopedPointer<ImageButton> squareImageButton2;
    ScopedPointer<ImageButton> logsawImageButton2;
    ScopedPointer<ComboBox> algorithmComboBox;
    ScopedPointer<Label> frequencyLabel8;
    ScopedPointer<Label> dbLabel4;
    ScopedPointer<ComboBox> keyscaleAttenuationComboBox2;
    ScopedPointer<ComboBox> keyscaleAttenuationComboBox;
    ScopedPointer<GroupComponent> groupComponent4;
    ScopedPointer<GroupComponent> groupComponent5;
    ScopedPointer<Slider> emulatorSlider;
    ScopedPointer<Label> emulatorLabel;
    ScopedPointer<Label> emulatorLabel2;
    ScopedPointer<ToggleButton> recordButton;
    ScopedPointer<ComboBox> percussionComboBox;
    ScopedPointer<Label> percussionLabel;
    ScopedPointer<TextButton> exportButton;


    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginGui)
};

//[EndFile] You can add extra defines here...
//[/EndFile]

#endif   // __JUCE_HEADER_450C07F5C14097B8__
