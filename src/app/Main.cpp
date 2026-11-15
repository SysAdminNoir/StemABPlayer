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
    explicit MainWindow (const juce::String& commandLine)
        : juce::DocumentWindow ("Stem A/B",
                                juce::Colour (theme::windowArgb),
                                juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        auto* content = new MainComponent();
        setContentOwned (content, false);   // false: don't resize the window to the content

        // Optional: "<original> <stem file>" on the command line loads that pair.
        const auto args = juce::StringArray::fromTokens (commandLine, true);
        if (args.size() == 2)
        {
            const juce::File original (args[0].unquoted()), stems (args[1].unquoted());
            if (original.existsAsFile() && stems.existsAsFile())
                content->loadPair (original, stems);
        }
        setResizable (true, true);

        // The constrainer works on the whole window including the native title bar and borders, while the
        // panel's minimum is a client-area size. Use a generous frame estimate now and the real frame once
        // the native peer exists, so the client area can never drop below MainComponent's minimum.
        applyMinimumSize (juce::BorderSize<int> (48, 8, 8, 8));
        centreWithSize (1280, 800);
        setVisible (true);

        // Needs the native peer, so it comes after the window is shown.
        if (auto* peer = getPeer())
        {
            enableDarkTitleBar (peer->getNativeHandle());
            applyMinimumSize (peer->getFrameSize());
        }
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    void applyMinimumSize (const juce::BorderSize<int>& frame)
    {
        setResizeLimits (MainComponent::minimumWidth + frame.getLeftAndRight(),
                         MainComponent::minimumHeight + frame.getTopAndBottom(),
                         8192, 8192);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};

class StemABApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    void initialise (const juce::String& commandLine) override
    {
        lookAndFeel = std::make_unique<theme::StemLookAndFeel>();
        juce::LookAndFeel::setDefaultLookAndFeel (lookAndFeel.get());

        mainWindow = std::make_unique<MainWindow> (commandLine);
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
