#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include "app/AppSettings.h"
#include "app/ui/Widgets.h"

namespace stemab
{

class AvatarBadge;
class LinkPad;

/** The Preferences content: a section list on the left (Folders, Interface, Audio, Session, About) and the chosen
    section on the right. Reads and writes AppSettings directly; the interface size goes through
    Callbacks::setScaleIndex so the header's A- / A+ and this page stay one setting, and the colour palette
    through Callbacks::setPalette (MainComponent saves it and repaints everything).

    Every font and metric follows setUiScale() (the same interface scale as the main window). */
class PreferencesPanel final : public juce::Component
{
public:
    enum class Section { folders, interface, audio, session, about };

    struct Callbacks
    {
        std::function<juce::File (AppSettings::Folder)> builtInFolder;   // the default when none is chosen
        std::function<void (int)> setScaleIndex;                         // index into theme::scaleSteps
        std::function<void (AppSettings::Palette)> setPalette;
    };

    PreferencesPanel (AppSettings&, juce::AudioDeviceManager&, Callbacks);
    ~PreferencesPanel() override;

    void setUiScale (float);
    void setScaleIndex (int);       // reflect a change made elsewhere (header, keyboard)
    void setPalette (AppSettings::Palette);
    /** The palette changed: re-read the colours. */
    void refreshPalette();
    void showSection (Section);

    /** The panel's size at a given interface scale. */
    static juce::Point<int> preferredSize (float uiScale);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct FolderRow
    {
        juce::Label caption, path, hint;
        HwButton browse, reset;
    };

    void refreshFolders();
    void refreshAboutColours();
    void paintAbout (juce::Graphics&) const;
    void browseFolder (AppSettings::Folder);
    FolderRow& rowFor (AppSettings::Folder f)      { return folderRows[f == AppSettings::Folder::originals ? 0 : 1]; }
    juce::Rectangle<int> contentArea() const;

    AppSettings& settings;
    juce::AudioDeviceManager& deviceManager;
    Callbacks callbacks;
    float uiScale = 1.0f;
    Section section = Section::folders;

    std::array<HwButton, 5> navButtons;

    // Folders
    juce::Component foldersPage;
    std::array<FolderRow, 2> folderRows;
    juce::ToggleButton rememberLastFolder { "Open the file dialogs in the last-used folder instead" };
    juce::Label foldersNote;
    std::unique_ptr<juce::FileChooser> chooser;

    // Interface
    juce::Component interfacePage;
    juce::Label sizeCaption, sizeNote;
    std::array<HwButton, 4> sizeButtons;
    juce::Label paletteCaption, paletteNote;
    std::array<HwButton, 2> paletteButtons;

    // Audio
    juce::Component audioPage;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> deviceSelector;

    // Session
    juce::Component sessionPage;
    juce::ToggleButton resumeSession { "Resume last session on launch" };
    juce::Label sessionNote, settingsFileNote;

    // About (no page heading: the page uses the whole content area; the panel paints the plate, rules and credits)
    struct CreditRow
    {
        juce::String name, licence, detail;
    };

    struct AboutRects
    {
        juce::Rectangle<int> plate, licenceRule, builtWithRule, credits, trademarkRule;
    };

    juce::Component aboutPage;
    std::unique_ptr<AvatarBadge> avatar;
    juce::HyperlinkButton aboutHandle;
    juce::Label aboutDescription;
    juce::Label aboutLicenseCaption, aboutLicense, aboutCopyright;
    std::unique_ptr<LinkPad> aboutSource;
    juce::Label aboutBuiltWithCaption, aboutTrademarks;
    juce::String aboutVersionText;
    std::vector<CreditRow> creditRows;
    AboutRects aboutRects;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PreferencesPanel)
};

/** The Preferences window (native dark title bar). Closing or Escape hides it; MainComponent keeps it. */
class PreferencesWindow final : public juce::DocumentWindow
{
public:
    PreferencesWindow (AppSettings&, juce::AudioDeviceManager&, PreferencesPanel::Callbacks);
    ~PreferencesWindow() override;

    PreferencesPanel& getPanel() noexcept       { return panel; }
    void setUiScale (float);
    /** The palette changed: re-read the colours. */
    void refreshPalette();

    /** Called after the window hides itself (close button or Escape). */
    std::function<void()> onClosed;

    void closeButtonPressed() override;
    bool keyPressed (const juce::KeyPress&) override;
    void visibilityChanged() override;

private:
    PreferencesPanel panel;
    bool darkTitleBarApplied = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PreferencesWindow)
};

} // namespace stemab
