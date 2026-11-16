#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <memory>
#include <optional>

#include "app/SessionState.h"

namespace stemab
{

/** The app's persistent settings: %APPDATA%/StemABPlayer/settings.xml (a juce::PropertiesFile).

    Typed getters / setters over the string keys below, so no other code spells a key. Setters only mark the
    file dirty (JUCE writes it about a second later, and on destruction); save() writes it now.
    Message thread only. */
class AppSettings final
{
public:
    /** The keys in settings.xml. */
    struct Keys
    {
        static constexpr const char* uiScaleIndex       = "uiScaleIndex";        // int, index into theme::scaleSteps
        static constexpr const char* originalsFolder    = "originalsFolder";     // path; unset = the built-in default
        static constexpr const char* stemFilesFolder    = "stemFilesFolder";     // path; unset = the built-in default
        static constexpr const char* rememberLastFolder = "rememberLastFolder";  // bool (default false)
        static constexpr const char* lastOriginalFolder = "lastOriginalFolder";  // path of the last Original chosen
        static constexpr const char* lastStemFileFolder = "lastStemFileFolder";  // path of the last Stem File chosen
        static constexpr const char* audioDeviceState   = "audioDeviceState";    // XML (AudioDeviceManager::createStateXml)
        static constexpr const char* resumeSession      = "resumeSession";       // bool (default true)
        static constexpr const char* session            = "session";             // XML (SessionState)
        static constexpr const char* windowState        = "windowState";         // ResizableWindow::getWindowStateAsString
    };

    /** Which file chooser a folder belongs to. */
    enum class Folder { originals, stemFiles };

    AppSettings();

    juce::File getFile() const;
    void save();

    int getScaleIndex (int fallback) const;
    void setScaleIndex (int);

    /** The folder the user chose in Preferences, or an empty File when unset (use the built-in default). */
    juce::File getFolder (Folder) const;
    void setFolder (Folder, const juce::File&);     // an empty File resets to the default

    bool getRememberLastFolder() const;
    void setRememberLastFolder (bool);
    juce::File getLastUsedFolder (Folder) const;
    void setLastUsedFolder (Folder, const juce::File&);

    std::unique_ptr<juce::XmlElement> getAudioDeviceState() const;
    void setAudioDeviceState (const juce::XmlElement*);

    bool getResumeSession() const;
    void setResumeSession (bool);
    std::optional<SessionState> getSession() const;
    void setSession (const SessionState&);
    /** Forgets the saved pair (Unload all): nothing is resumed on the next launch. */
    void clearSession();

    juce::String getWindowState() const;
    void setWindowState (const juce::String&);

private:
    juce::PropertiesFile* props() const;
    static juce::File fileFromSetting (const juce::String&);

    mutable juce::ApplicationProperties properties;

    JUCE_DECLARE_NON_COPYABLE (AppSettings)
};

} // namespace stemab
