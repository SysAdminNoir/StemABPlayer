#include <juce_gui_extra/juce_gui_extra.h>

#include "MainComponent.h"
#include "PlatformWindows.h"
#include "Theme.h"

#include <memory>

namespace stemab
{

class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow()
        : juce::DocumentWindow ("Stem A/B",
                                juce::Colour (theme::windowArgb),
                                juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new MainComponent(), false);   // false: don't resize the window to the content
        setResizable (true, true);
        setResizeLimits (960, 600, 8192, 8192);
        centreWithSize (1280, 800);
        setVisible (true);

        // Needs the native peer, so it comes after the window is shown.
        if (auto* peer = getPeer())
            enableDarkTitleBar (peer->getNativeHandle());
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};

class StemABApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    void initialise (const juce::String&) override
    {
        lookAndFeel = std::make_unique<theme::StemLookAndFeel>();
        juce::LookAndFeel::setDefaultLookAndFeel (lookAndFeel.get());

        mainWindow = std::make_unique<MainWindow>();
    }

    void shutdown() override
    {
        mainWindow.reset();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        lookAndFeel.reset();
    }

    void systemRequestedQuit() override
    {
        quit();
    }

private:
    std::unique_ptr<theme::StemLookAndFeel> lookAndFeel;
    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace stemab

START_JUCE_APPLICATION (stemab::StemABApplication)
