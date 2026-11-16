#include "app/AppSettings.h"

namespace stemab
{

AppSettings::AppSettings()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "settings";
    options.filenameSuffix = ".xml";
    options.folderName = "StemABPlayer";
    options.osxLibrarySubFolder = "Application Support";
    options.millisecondsBeforeSaving = 1000;
    properties.setStorageParameters (options);
}

juce::PropertiesFile* AppSettings::props() const
{
    return properties.getUserSettings();
}

juce::File AppSettings::getFile() const
{
    if (auto* p = props())
        return p->getFile();
    return {};
}

void AppSettings::save()
{
    if (auto* p = props())
        p->saveIfNeeded();
}

juce::File AppSettings::fileFromSetting (const juce::String& path)
{
    return juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
}

//==============================================================================
int AppSettings::getScaleIndex (int fallback) const
{
    auto* p = props();
    return p != nullptr ? p->getIntValue (Keys::uiScaleIndex, fallback) : fallback;
}

void AppSettings::setScaleIndex (int index)
{
    if (auto* p = props())
        p->setValue (Keys::uiScaleIndex, index);
}

//==============================================================================
juce::File AppSettings::getFolder (Folder f) const
{
    auto* p = props();
    return p != nullptr ? fileFromSetting (p->getValue (f == Folder::originals ? Keys::originalsFolder : Keys::stemFilesFolder))
                        : juce::File();
}

void AppSettings::setFolder (Folder f, const juce::File& dir)
{
    if (auto* p = props())
    {
        const auto* key = f == Folder::originals ? Keys::originalsFolder : Keys::stemFilesFolder;
        if (dir == juce::File())
            p->removeValue (key);
        else
            p->setValue (key, dir.getFullPathName());
    }
}

bool AppSettings::getRememberLastFolder() const
{
    auto* p = props();
    return p != nullptr && p->getBoolValue (Keys::rememberLastFolder, false);
}

void AppSettings::setRememberLastFolder (bool remember)
{
    if (auto* p = props())
        p->setValue (Keys::rememberLastFolder, remember);
}

juce::File AppSettings::getLastUsedFolder (Folder f) const
{
    auto* p = props();
    return p != nullptr ? fileFromSetting (p->getValue (f == Folder::originals ? Keys::lastOriginalFolder : Keys::lastStemFileFolder))
                        : juce::File();
}

void AppSettings::setLastUsedFolder (Folder f, const juce::File& dir)
{
    if (auto* p = props())
        p->setValue (f == Folder::originals ? Keys::lastOriginalFolder : Keys::lastStemFileFolder, dir.getFullPathName());
}

//==============================================================================
std::unique_ptr<juce::XmlElement> AppSettings::getAudioDeviceState() const
{
    auto* p = props();
    return p != nullptr ? p->getXmlValue (Keys::audioDeviceState) : nullptr;
}

void AppSettings::setAudioDeviceState (const juce::XmlElement* xml)
{
    if (auto* p = props())
    {
        if (xml == nullptr)
            p->removeValue (Keys::audioDeviceState);
        else
            p->setValue (Keys::audioDeviceState, xml);
    }
}

//==============================================================================
bool AppSettings::getResumeSession() const
{
    auto* p = props();
    return p == nullptr || p->getBoolValue (Keys::resumeSession, true);
}

void AppSettings::setResumeSession (bool resume)
{
    if (auto* p = props())
        p->setValue (Keys::resumeSession, resume);
}

std::optional<SessionState> AppSettings::getSession() const
{
    auto* p = props();
    if (p == nullptr)
        return std::nullopt;
    const auto xml = p->getXmlValue (Keys::session);
    return SessionState::fromXml (xml.get());
}

void AppSettings::setSession (const SessionState& s)
{
    if (auto* p = props())
    {
        const auto xml = s.toXml();
        p->setValue (Keys::session, xml.get());
    }
}

void AppSettings::clearSession()
{
    if (auto* p = props())
        p->removeValue (Keys::session);
}

//==============================================================================
juce::String AppSettings::getWindowState() const
{
    auto* p = props();
    return p != nullptr ? p->getValue (Keys::windowState) : juce::String();
}

void AppSettings::setWindowState (const juce::String& state)
{
    if (auto* p = props())
        p->setValue (Keys::windowState, state);
}

} // namespace stemab
