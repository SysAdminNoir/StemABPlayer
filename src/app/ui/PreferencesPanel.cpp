#include "app/ui/PreferencesPanel.h"

#include "app/PlatformWindows.h"
#include "app/Theme.h"

namespace stemab
{

namespace
{
    const juce::String ellipsis (juce::CharPointer_UTF8 ("\xe2\x80\xa6"));
    const juce::String middleDot (juce::CharPointer_UTF8 ("\xc2\xb7"));
    const juce::String minusSign (juce::CharPointer_UTF8 ("\xe2\x88\x92"));

    constexpr float baseWidth = 800.0f;
    constexpr float baseHeight = 560.0f;
    constexpr float navWidth = 176.0f;
    constexpr int navGroupId = 0x5e01;
    constexpr int sizeGroupId = 0x5e02;

    const char* sectionName (PreferencesPanel::Section s)
    {
        switch (s)
        {
            case PreferencesPanel::Section::folders:   return "Folders";
            case PreferencesPanel::Section::interface: return "Interface";
            case PreferencesPanel::Section::audio:     return "Audio";
            case PreferencesPanel::Section::session:   return "Session";
        }
        return "";
    }

    const char* sectionSubtitle (PreferencesPanel::Section s)
    {
        switch (s)
        {
            case PreferencesPanel::Section::folders:   return "Where the Open dialogs start";
            case PreferencesPanel::Section::interface: return "Text and control size";
            case PreferencesPanel::Section::audio:     return "Output device, sample rate and buffer size";
            case PreferencesPanel::Section::session:   return "What comes back when the app starts";
        }
        return "";
    }

    void styleNote (juce::Label& l)
    {
        l.setColour (juce::Label::textColourId, theme::colour (theme::mutedTextArgb));
        l.setJustificationType (juce::Justification::topLeft);
        l.setMinimumHorizontalScale (1.0f);
    }
}

//==============================================================================
PreferencesPanel::PreferencesPanel (AppSettings& s, juce::AudioDeviceManager& dm, Callbacks cb)
    : settings (s), deviceManager (dm), callbacks (std::move (cb))
{
    setOpaque (true);

    // Section list
    for (int i = 0; i < (int) navButtons.size(); ++i)
    {
        auto& b = navButtons[(size_t) i];
        b.setButtonText (sectionName ((Section) i));
        b.setRadioGroupId (navGroupId);
        b.setClickingTogglesState (true);
        b.getProperties().set ("fontRatio", 0.40);
        b.onClick = [this, i] { showSection ((Section) i); };
        addAndMakeVisible (b);
    }

    // Folders
    const AppSettings::Folder folders[2] { AppSettings::Folder::originals, AppSettings::Folder::stemFiles };
    for (const auto f : folders)
    {
        auto& row = rowFor (f);
        row.caption.setText (f == AppSettings::Folder::originals ? "ORIGINALS (A)" : "STEM FILES (B)", juce::dontSendNotification);
        row.caption.setColour (juce::Label::textColourId, theme::colour (theme::mutedTextArgb));

        row.path.setColour (juce::Label::backgroundColourId, theme::colour (theme::raisedArgb));
        row.path.setColour (juce::Label::outlineColourId, theme::colour (theme::hairline2Argb));
        row.path.setColour (juce::Label::textColourId, theme::colour (theme::textArgb));
        row.path.setMinimumHorizontalScale (1.0f);

        styleNote (row.hint);

        row.browse.setButtonText ("Browse" + ellipsis);
        row.browse.setTooltip ("Choose the folder the Open dialog starts in");
        row.browse.onClick = [this, f] { browseFolder (f); };
        row.reset.setButtonText ("Reset");
        row.reset.setTooltip ("Back to the default folder");
        row.reset.onClick = [this, f]
        {
            settings.setFolder (f, {});
            settings.save();
            refreshFolders();
        };

        for (juce::Component* c : { (juce::Component*) &row.caption, (juce::Component*) &row.path, (juce::Component*) &row.hint,
                                     (juce::Component*) &row.browse, (juce::Component*) &row.reset })
            foldersPage.addAndMakeVisible (c);
    }

    rememberLastFolder.setTooltip ("When on, each Open dialog starts where you last opened a file of that kind");
    rememberLastFolder.onClick = [this]
    {
        settings.setRememberLastFolder (rememberLastFolder.getToggleState());
        settings.save();
    };
    foldersPage.addAndMakeVisible (rememberLastFolder);

    foldersNote.setText ("The Open dialogs start in these folders; you can still browse anywhere. Without a chosen "
                         "folder they use the project's \"Audio Files\" / \"Stem Files\" folders when present, "
                         "otherwise your Music folder.", juce::dontSendNotification);
    styleNote (foldersNote);
    foldersPage.addAndMakeVisible (foldersNote);
    addChildComponent (foldersPage);

    // Interface
    sizeCaption.setText ("INTERFACE SIZE", juce::dontSendNotification);
    sizeCaption.setColour (juce::Label::textColourId, theme::colour (theme::mutedTextArgb));
    interfacePage.addAndMakeVisible (sizeCaption);

    for (int i = 0; i < (int) sizeButtons.size(); ++i)
    {
        auto& b = sizeButtons[(size_t) i];
        b.setButtonText (juce::String (juce::roundToInt (theme::scaleSteps[(size_t) i] * 100.0f)) + "%");
        b.setRadioGroupId (sizeGroupId);
        b.getProperties().set ("fontRatio", 0.40);
        b.onClick = [this, i]
        {
            if (callbacks.setScaleIndex)
                callbacks.setScaleIndex (i);
        };
        interfacePage.addAndMakeVisible (b);
    }

    sizeNote.setText ("Scales every font and control in the main window and here. The same setting as A"
                      + minusSign + " / A+ in the header and Ctrl+" + minusSign + " / Ctrl+= / Ctrl+0 (100%).",
                      juce::dontSendNotification);
    styleNote (sizeNote);
    interfacePage.addAndMakeVisible (sizeNote);
    addChildComponent (interfacePage);

    // Audio: JUCE's selector, outputs only (stereo pairs), as the old Audio settings dialog.
    deviceSelector = std::make_unique<juce::AudioDeviceSelectorComponent> (deviceManager,
                                                                           0, 0,      // no inputs
                                                                           0, 2,      // outputs
                                                                           false, false,
                                                                           true,      // stereo pairs
                                                                           false);    // advanced options shown
    audioPage.addAndMakeVisible (*deviceSelector);
    addChildComponent (audioPage);

    // Session
    resumeSession.setTooltip ("Reopen the last pair, paused where you left it, with the same mix settings");
    resumeSession.setToggleState (settings.getResumeSession(), juce::dontSendNotification);
    resumeSession.onClick = [this]
    {
        settings.setResumeSession (resumeSession.getToggleState());
        settings.save();
    };
    sessionPage.addAndMakeVisible (resumeSession);

    sessionNote.setText ("On launch, reopens the last Original and stem file, paused at the same position, with the same "
                         "Crossfade, Equal-power curve, Listen mode (A/B or Residual), Mute / Solo, Master Gain, Loop "
                         "and Residual view (+24 dB / 0 dB). If either file has moved, nothing is opened.\n\n"
                         "Files given on the command line take precedence. The window size and position are "
                         "always remembered.", juce::dontSendNotification);
    styleNote (sessionNote);
    sessionPage.addAndMakeVisible (sessionNote);

    settingsFileNote.setText ("Settings file:  " + settings.getFile().getFullPathName(), juce::dontSendNotification);
    styleNote (settingsFileNote);
    settingsFileNote.setColour (juce::Label::textColourId, theme::colour (theme::mutedTextArgb).withMultipliedAlpha (0.8f));
    sessionPage.addAndMakeVisible (settingsFileNote);
    addChildComponent (sessionPage);

    refreshFolders();
    showSection (Section::folders);
    setUiScale (1.0f);
}

PreferencesPanel::~PreferencesPanel() = default;

juce::Point<int> PreferencesPanel::preferredSize (float s)
{
    return { theme::scaled (baseWidth, s), theme::scaled (baseHeight, s) };
}

void PreferencesPanel::setUiScale (float s)
{
    uiScale = s;

    const auto captionFont = theme::uiFont (theme::em (0.72f, s), true).withExtraKerningFactor (0.1f);
    const auto noteFont = theme::uiFont (theme::em (0.86f, s));

    for (auto& row : folderRows)
    {
        row.caption.setFont (captionFont);
        row.path.setFont (theme::monoFont (theme::em (0.86f, s)));
        row.path.setBorderSize ({ 0, theme::scaled (10.0f, s), 0, theme::scaled (10.0f, s) });
        row.hint.setFont (theme::uiFont (theme::em (0.8f, s)));
        row.hint.setBorderSize ({ 0, 0, 0, 0 });
    }
    sizeCaption.setFont (captionFont);

    for (auto* l : { &foldersNote, &sizeNote, &sessionNote })
    {
        l->setFont (noteFont);
        l->setBorderSize ({ 0, 0, 0, 0 });
    }
    settingsFileNote.setFont (theme::uiFont (theme::em (0.78f, s)));
    settingsFileNote.setBorderSize ({ 0, 0, 0, 0 });

    for (auto* t : { &rememberLastFolder, &resumeSession })
        t->getProperties().set ("fontPx", (double) theme::em (0.95f, s));

    deviceSelector->setItemHeight (theme::scaled (26.0f, s));

    const auto size = preferredSize (s);
    setSize (size.x, size.y);
    resized();
    repaint();
}

void PreferencesPanel::setScaleIndex (int index)
{
    for (int i = 0; i < (int) sizeButtons.size(); ++i)
        sizeButtons[(size_t) i].setToggleState (i == index, juce::dontSendNotification);
}

void PreferencesPanel::showSection (Section s)
{
    section = s;
    // Set every nav button explicitly: relying on the radio group alone left the previous one lit.
    for (int i = 0; i < (int) navButtons.size(); ++i)
        navButtons[(size_t) i].setToggleState (i == (int) s, juce::dontSendNotification);

    foldersPage.setVisible (s == Section::folders);
    interfacePage.setVisible (s == Section::interface);
    audioPage.setVisible (s == Section::audio);
    sessionPage.setVisible (s == Section::session);

    if (s == Section::folders)
        refreshFolders();
    repaint();
}

//==============================================================================
void PreferencesPanel::refreshFolders()
{
    const AppSettings::Folder folders[2] { AppSettings::Folder::originals, AppSettings::Folder::stemFiles };
    for (const auto f : folders)
    {
        auto& row = rowFor (f);
        const auto chosen = settings.getFolder (f);
        const auto builtIn = callbacks.builtInFolder ? callbacks.builtInFolder (f) : juce::File();

        if (chosen == juce::File())
        {
            row.path.setText (builtIn.getFullPathName(), juce::dontSendNotification);
            row.hint.setText ("Default", juce::dontSendNotification);
            row.hint.setColour (juce::Label::textColourId, theme::colour (theme::mutedTextArgb));
            row.reset.setEnabled (false);
        }
        else if (! chosen.isDirectory())
        {
            row.path.setText (chosen.getFullPathName(), juce::dontSendNotification);
            row.hint.setText ("Folder not found " + middleDot + " the dialog opens in the default: " + builtIn.getFullPathName(),
                              juce::dontSendNotification);
            row.hint.setColour (juce::Label::textColourId, theme::colour (theme::amberArgb));
            row.reset.setEnabled (true);
        }
        else
        {
            row.path.setText (chosen.getFullPathName(), juce::dontSendNotification);
            row.hint.setText ("Chosen folder " + middleDot + " Reset returns to " + builtIn.getFullPathName(),
                              juce::dontSendNotification);
            row.hint.setColour (juce::Label::textColourId, theme::colour (theme::mutedTextArgb));
            row.reset.setEnabled (true);
        }
        row.path.setTooltip (row.path.getText());
    }

    rememberLastFolder.setToggleState (settings.getRememberLastFolder(), juce::dontSendNotification);
}

void PreferencesPanel::browseFolder (AppSettings::Folder f)
{
    auto start = settings.getFolder (f);
    if (! start.isDirectory() && callbacks.builtInFolder)
        start = callbacks.builtInFolder (f);

    chooser = std::make_unique<juce::FileChooser> (f == AppSettings::Folder::originals ? "Default folder for Originals (A)"
                                                                                       : "Default folder for stem files (B)",
                                                   start);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this, f] (const juce::FileChooser& fc)
                          {
                              if (const auto dir = fc.getResult(); dir.isDirectory())
                              {
                                  settings.setFolder (f, dir);
                                  settings.save();
                                  refreshFolders();
                              }
                          });
}

//==============================================================================
juce::Rectangle<int> PreferencesPanel::contentArea() const
{
    const float s = uiScale;
    return getLocalBounds().withTrimmedLeft (theme::scaled (navWidth, s))
                           .reduced (theme::scaled (28.0f, s), theme::scaled (22.0f, s));
}

void PreferencesPanel::paint (juce::Graphics& g)
{
    const float s = uiScale;
    g.fillAll (theme::colour (theme::groundArgb));

    // Section list column
    const auto nav = getLocalBounds().removeFromLeft (theme::scaled (navWidth, s));
    g.setColour (theme::colour (theme::panelArgb));
    g.fillRect (nav);
    g.setColour (theme::colour (theme::hairlineArgb));
    g.fillRect (nav.getRight() - 1, 0, 1, getHeight());

    // Section title + subtitle
    auto header = contentArea().removeFromTop (theme::scaled (52.0f, s));
    g.setColour (theme::colour (theme::textArgb));
    g.setFont (theme::uiFont (theme::em (1.25f, s), true));
    g.drawText (sectionName (section), header.removeFromTop (theme::scaled (28.0f, s)), juce::Justification::centredLeft, false);
    g.setColour (theme::colour (theme::mutedTextArgb));
    g.setFont (theme::uiFont (theme::em (0.86f, s)));
    g.drawText (sectionSubtitle (section), header.removeFromTop (theme::scaled (20.0f, s)), juce::Justification::centredLeft, false);

    g.setColour (theme::colour (theme::hairlineArgb));
    const auto c = contentArea();
    g.fillRect (c.getX(), c.getY() + theme::scaled (60.0f, s), c.getWidth(), 1);
}

void PreferencesPanel::resized()
{
    const float s = uiScale;
    const auto px = [s] (float v) { return theme::scaled (v, s); };

    // Section list
    {
        auto nav = getLocalBounds().removeFromLeft (px (navWidth)).reduced (px (12.0f), px (18.0f));
        for (auto& b : navButtons)
        {
            b.setBounds (nav.removeFromTop (px (34.0f)));
            nav.removeFromTop (px (6.0f));
        }
    }

    auto page = contentArea().withTrimmedTop (px (76.0f));
    for (auto* p : { &foldersPage, &interfacePage, &audioPage, &sessionPage })
        p->setBounds (page);

    const auto local = page.withZeroOrigin();

    // Folders
    {
        auto r = local;
        for (auto& row : folderRows)
        {
            row.caption.setBounds (r.removeFromTop (px (18.0f)));
            r.removeFromTop (px (6.0f));
            auto line = r.removeFromTop (px (32.0f));
            row.reset.setBounds (line.removeFromRight (px (76.0f)));
            line.removeFromRight (px (6.0f));
            row.browse.setBounds (line.removeFromRight (px (100.0f)));
            line.removeFromRight (px (10.0f));
            row.path.setBounds (line);
            r.removeFromTop (px (5.0f));
            row.hint.setBounds (r.removeFromTop (px (20.0f)));
            r.removeFromTop (px (22.0f));
        }
        rememberLastFolder.setBounds (r.removeFromTop (px (28.0f)));
        r.removeFromTop (px (16.0f));
        foldersNote.setBounds (r.removeFromTop (px (60.0f)));
    }

    // Interface
    {
        auto r = local;
        sizeCaption.setBounds (r.removeFromTop (px (18.0f)));
        r.removeFromTop (px (8.0f));
        auto line = r.removeFromTop (px (34.0f));
        for (auto& b : sizeButtons)
        {
            b.setBounds (line.removeFromLeft (px (76.0f)));
            line.removeFromLeft (px (6.0f));
        }
        r.removeFromTop (px (16.0f));
        sizeNote.setBounds (r.removeFromTop (px (60.0f)));
    }

    // Audio
    deviceSelector->setBounds (local);

    // Session
    {
        auto r = local;
        resumeSession.setBounds (r.removeFromTop (px (28.0f)));
        r.removeFromTop (px (12.0f));
        settingsFileNote.setBounds (r.removeFromBottom (px (22.0f)));
        sessionNote.setBounds (r.removeFromTop (px (130.0f)));
    }
}

//==============================================================================
PreferencesWindow::PreferencesWindow (AppSettings& settings, juce::AudioDeviceManager& dm, PreferencesPanel::Callbacks cb)
    : juce::DocumentWindow ("Preferences", theme::colour (theme::groundArgb), juce::DocumentWindow::closeButton),
      panel (settings, dm, std::move (cb))
{
    setUsingNativeTitleBar (true);
    setContentNonOwned (&panel, true);     // true: the window follows the panel's size (interface scale)
    setResizable (false, false);
}

PreferencesWindow::~PreferencesWindow()
{
    clearContentComponent();
}

void PreferencesWindow::visibilityChanged()
{
    juce::DocumentWindow::visibilityChanged();

    // Needs the native peer of a shown window (as the main window).
    if (! darkTitleBarApplied && isShowing())
        if (auto* peer = getPeer())
        {
            enableDarkTitleBar (peer->getNativeHandle());
            darkTitleBarApplied = true;
        }
}

void PreferencesWindow::setUiScale (float s)
{
    panel.setUiScale (s);
}

void PreferencesWindow::closeButtonPressed()
{
    setVisible (false);
    if (onClosed)
        onClosed();
}

bool PreferencesWindow::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        closeButtonPressed();
        return true;
    }
    return juce::DocumentWindow::keyPressed (key);
}

} // namespace stemab
