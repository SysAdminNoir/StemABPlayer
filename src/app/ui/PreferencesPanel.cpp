#include "app/ui/PreferencesPanel.h"

#include "app/PlatformWindows.h"
#include "app/Theme.h"
#include "decode/Decode.h"

#include "StemABImages.h"

namespace stemab
{

namespace
{
    const juce::String ellipsis (juce::CharPointer_UTF8 ("\xe2\x80\xa6"));
    const juce::String middleDot (juce::CharPointer_UTF8 ("\xc2\xb7"));
    const juce::String minusSign (juce::CharPointer_UTF8 ("\xe2\x88\x92"));
    const juce::String enDash (juce::CharPointer_UTF8 ("\xe2\x80\x93"));
    const juce::String copyrightSign (juce::CharPointer_UTF8 ("\xc2\xa9"));

    constexpr const char* sourceCodeUrl = "https://github.com/argentstygian/StemABPlayer";
    constexpr const char* authorUrl = "https://github.com/argentstygian";

    /** The running application's version, e.g. "1.0.0". */
    juce::String appVersionString()
    {
       #ifdef JUCE_APPLICATION_VERSION_STRING
        return JUCE_APPLICATION_VERSION_STRING;
       #else
        if (auto* app = juce::JUCEApplication::getInstance())
            return app->getApplicationVersion();
        return {};
       #endif
    }

    constexpr float baseWidth = 800.0f;
    constexpr float baseHeight = 560.0f;
    constexpr float navWidth = 176.0f;
    constexpr int navGroupId = 0x5e01;
    constexpr int sizeGroupId = 0x5e02;
    constexpr int paletteGroupId = 0x5e03;

    using theme::C;

    const char* sectionName (PreferencesPanel::Section s)
    {
        switch (s)
        {
            case PreferencesPanel::Section::folders:   return "Folders";
            case PreferencesPanel::Section::interface: return "Interface";
            case PreferencesPanel::Section::audio:     return "Audio";
            case PreferencesPanel::Section::session:   return "Session";
            case PreferencesPanel::Section::about:     return "About";
        }
        return "";
    }

    const char* sectionSubtitle (PreferencesPanel::Section s)
    {
        switch (s)
        {
            case PreferencesPanel::Section::folders:   return "Where the Open dialogs start";
            case PreferencesPanel::Section::interface: return "Text and control size, colors";
            case PreferencesPanel::Section::audio:     return "Output device, sample rate and buffer size";
            case PreferencesPanel::Section::session:   return "What comes back when the app starts";
            case PreferencesPanel::Section::about:     return "Version, license and credits";
        }
        return "";
    }

    void styleNote (juce::Label& l)
    {
        l.setColour (juce::Label::textColourId, theme::colour (C::muted));
        l.setJustificationType (juce::Justification::topLeft);
        l.setMinimumHorizontalScale (1.0f);
    }

    /** A silkscreen caption (the text is already in capitals). */
    void styleCaption (juce::Label& l)
    {
        l.setColour (juce::Label::textColourId, theme::colour (C::silk));
    }

    /** Keys that light (no glow) when they are the chosen one. */
    void styleChoiceKey (HwButton& b)
    {
        b.setLitColours (C::colourB, C::inkDark, false);
    }
}

//==============================================================================
/** The author's picture on the About page: a round badge (bezel, ring in the accent colour, sunken shadow). */
class AvatarBadge final : public juce::Component,
                          private juce::Timer
{
public:
    AvatarBadge()
    {
        image = juce::ImageCache::getFromMemory (StemABImages::avatar_jpg, StemABImages::avatar_jpgSize);
    }

    ~AvatarBadge() override { stopTimer(); }

    void setUiScale (float s)
    {
        uiScale = s;
        repaint();
    }

    bool hitTest (int x, int y) override
    {
        const auto d = discBounds();
        const auto dx = (float) x - d.getCentreX();
        const auto dy = (float) y - d.getCentreY();
        return dx * dx + dy * dy <= d.getWidth() * d.getWidth() * 0.25f;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isLeftButtonDown())
            registerClick();
    }

    void paint (juce::Graphics& g) override
    {
        const float s = uiScale;
        const auto disc = discBounds();
        const auto face = disc.reduced (4.0f * s);
        const auto f = frame();

        // Well under the picture, with a faint lip along the bottom edge
        g.setColour (juce::Colour (0x12ffffff));
        g.fillEllipse (disc.translated (0.0f, s));
        g.setColour (theme::colour (C::slot));
        g.fillEllipse (disc);

        {
            juce::Graphics::ScopedSaveState save (g);
            juce::Path clip;
            clip.addEllipse (face);
            g.reduceClipRegion (clip);

            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            if (image.isValid())
            {
                g.setOpacity (1.0f);
                g.drawImage (image, face, juce::RectanglePlacement::fillDestination);

                if (f.invertedAlpha > 0.0f && inverted.isValid())
                {
                    g.setOpacity (f.invertedAlpha);
                    g.drawImage (inverted, face.translated (f.slideOffset * face.getWidth(), 0.0f),
                                 juce::RectanglePlacement::fillDestination);
                }
            }
            else
            {
                g.setColour (theme::colour (C::padOff));
                g.fillEllipse (face);
            }

            if (f.greyAlpha > 0.0f)
            {
                g.setColour (juce::Colour (0xff808080).withAlpha (f.greyAlpha));
                g.fillEllipse (face);
            }

            // Sunken: darker towards the rim, a little heavier at the top
            const auto centre = face.getCentre().translated (0.0f, face.getHeight() * 0.1f);
            juce::ColourGradient shade (juce::Colours::transparentBlack, centre.x, centre.y,
                                        juce::Colour (0x99000000), centre.x + face.getWidth() * 0.58f, centre.y, true);
            shade.addColour (0.72, juce::Colours::transparentBlack);
            g.setGradientFill (shade);
            g.fillEllipse (face);
        }

        // The ring: accent, with a dimmer one just inside it
        g.setColour (theme::colour (C::accent).withAlpha (0.22f));
        g.drawEllipse (disc.reduced (2.6f * s), 1.5f * s);
        g.setColour (theme::colour (C::accent).withAlpha (0.92f));
        g.drawEllipse (disc.reduced (0.75f * s), 1.5f * s);

        if (f.plateAlpha > 0.0f)
            paintReadout (g, f.plateAlpha);
    }

private:
    enum class Phase { idle, running };

    struct Frame
    {
        float slideOffset = 0.0f;       // fraction of the picture's width still to travel
        float invertedAlpha = 0.0f;
        float greyAlpha = 0.0f;
        float plateAlpha = 0.0f;
    };

    // Timings (ms): slide and cross-fade, grey hold, readout shown, fade back
    static constexpr double slideMs = 1000.0, holdMs = 400.0, readoutMs = 3000.0, fadeMs = 500.0;
    static constexpr double readoutFadeInMs = 150.0;
    static constexpr double clickGapMs = 600.0;
    static constexpr int clicksNeeded = 7;

    static float smooth (float lo, float hi, float v) noexcept
    {
        const auto t = juce::jlimit (0.0f, 1.0f, (v - lo) / (hi - lo));
        return t * t * (3.0f - 2.0f * t);
    }

    juce::Rectangle<float> discBounds() const
    {
        const auto d = (float) theme::scaled (84.0f, uiScale);
        return { (float) getWidth() - d, 0.0f, d, d };
    }

    void registerClick()
    {
        if (phase == Phase::running)
        {
            clickCount = 0;
            return;
        }

        const auto now = juce::Time::getMillisecondCounterHiRes();
        clickCount = (clickCount > 0 && now - lastClickMs < clickGapMs) ? clickCount + 1 : 1;
        lastClickMs = now;

        if (clickCount >= clicksNeeded)
        {
            clickCount = 0;
            begin();
        }
    }

    void begin()
    {
        if (image.isValid() && ! inverted.isValid())
            inverted = makeInverted (image);

        phase = Phase::running;
        startMs = juce::Time::getMillisecondCounterHiRes();
        startTimerHz (60);
        repaint();
    }

    void timerCallback() override
    {
        if (juce::Time::getMillisecondCounterHiRes() - startMs >= slideMs + holdMs + readoutMs + fadeMs)
        {
            phase = Phase::idle;
            stopTimer();
        }
        repaint();
    }

    Frame frame() const
    {
        Frame f;
        if (phase != Phase::running)
            return f;

        const auto t = juce::Time::getMillisecondCounterHiRes() - startMs;

        if (t < slideMs)
        {
            const auto u = (float) (t / slideMs);
            const auto eased = 1.0f - (1.0f - u) * (1.0f - u) * (1.0f - u);
            f.slideOffset = 1.0f - eased;
            f.invertedAlpha = 1.0f - 0.5f * smooth (0.5f, 1.0f, eased);
            f.greyAlpha = smooth (0.72f, 1.0f, eased);
        }
        else if (t < slideMs + holdMs + readoutMs)
        {
            f.greyAlpha = 1.0f;
            f.plateAlpha = (float) juce::jlimit (0.0, 1.0, (t - slideMs - holdMs) / readoutFadeInMs);
        }
        else
        {
            const auto fade = (float) juce::jlimit (0.0, 1.0, 1.0 - (t - slideMs - holdMs - readoutMs) / fadeMs);
            f.greyAlpha = fade;
            f.plateAlpha = fade;
        }
        return f;
    }

    static juce::Image makeInverted (const juce::Image& source)
    {
        juce::Image out (juce::Image::ARGB, source.getWidth(), source.getHeight(), true);
        {
            juce::Graphics g (out);
            g.drawImageAt (source, 0, 0);
        }

        juce::Image::BitmapData data (out, juce::Image::BitmapData::readWrite);
        for (int y = 0; y < data.height; ++y)
            for (int x = 0; x < data.width; ++x)
            {
                const auto c = data.getPixelColour (x, y);
                data.setPixelColour (x, y, juce::Colour::fromRGBA ((juce::uint8) (255 - c.getRed()),
                                                                   (juce::uint8) (255 - c.getGreen()),
                                                                   (juce::uint8) (255 - c.getBlue()), c.getAlpha()));
            }
        return out;
    }

    void paintReadout (juce::Graphics& g, float alpha) const
    {
        const float s = uiScale;
        const auto first = "NULL DEPTH  " + minusSign + juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x9e")) + " dB";
        const juce::String second ("PERFECT");
        const auto font = theme::monoFont (10.5f * s);
        const auto boldFont = theme::monoFont (10.5f * s, true);

        const float textW = juce::jmax (juce::GlyphArrangement::getStringWidth (font, first),
                                        juce::GlyphArrangement::getStringWidth (boldFont, second));
        const float w = juce::jmin ((float) getWidth(), std::ceil (textW) + 20.0f * s);
        const float lineH = 14.0f * s;
        const juce::Rectangle<float> plate ((float) getWidth() - w, 66.0f * s, w, 2.0f * lineH + 8.0f * s);

        g.beginTransparencyLayer (alpha);
        theme::drawScreen (g, plate, s, 4.0f);

        auto text = plate.reduced (0.0f, 4.0f * s);
        g.setColour (theme::colour (C::nullDepth));
        g.setFont (font);
        g.drawText (first, text.removeFromTop (lineH), juce::Justification::centred, false);
        g.setFont (boldFont);
        g.drawText (second, text.removeFromTop (lineH), juce::Justification::centred, false);
        g.endTransparencyLayer();
    }

    juce::Image image, inverted;
    float uiScale = 1.0f;
    Phase phase = Phase::idle;
    int clickCount = 0;
    double lastClickMs = 0.0, startMs = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AvatarBadge)
};

//==============================================================================
/** The source link as a pad: the unlit pad face with the address in the accent colour. */
class LinkPad final : public juce::HyperlinkButton
{
public:
    LinkPad (const juce::String& text, const juce::URL& url) : juce::HyperlinkButton (text, url) {}

    void setUiScale (float s)
    {
        uiScale = s;
        repaint();
    }

    juce::Font padFont() const      { return theme::uiFont (theme::em (0.92f, uiScale), theme::Weight::semiBold); }

    /** The width that fits the text with its padding. */
    int preferredWidth() const
    {
        return (int) std::ceil (juce::GlyphArrangement::getStringWidth (padFont(), getButtonText()))
             + 2 * theme::scaled (14.0f, uiScale);
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        const float s = uiScale;
        auto face = getLocalBounds().toFloat().withTrimmedBottom (2.0f * s);     // the pad's shadow is drawn below
        theme::drawPad (g, face, s, false, {}, highlighted, down, isEnabled(), 0.0f, 4.0f);

        if (down)
            face = face.translated (0.0f, juce::jmax (1.0f, s));
        g.setColour (theme::colour (C::accent));
        g.setFont (padFont());
        g.drawText (getButtonText(), face, juce::Justification::centred, false);
    }

private:
    float uiScale = 1.0f;
};

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
        styleChoiceKey (b);
        b.setFontPx (13.0f);
        b.onClick = [this, i] { showSection ((Section) i); };
        addAndMakeVisible (b);
    }

    // Folders
    const AppSettings::Folder folders[2] { AppSettings::Folder::originals, AppSettings::Folder::stemFiles };
    for (const auto f : folders)
    {
        auto& row = rowFor (f);
        row.caption.setText (f == AppSettings::Folder::originals ? "ORIGINALS (A)" : "STEM FILES (B)", juce::dontSendNotification);
        styleCaption (row.caption);

        row.path.setColour (juce::Label::backgroundColourId, theme::colour (C::screen));
        row.path.setColour (juce::Label::outlineColourId, theme::colour (C::screenLine));
        row.path.setColour (juce::Label::textColourId, theme::colour (C::screenText));
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
    styleCaption (sizeCaption);
    interfacePage.addAndMakeVisible (sizeCaption);

    for (int i = 0; i < (int) sizeButtons.size(); ++i)
    {
        auto& b = sizeButtons[(size_t) i];
        b.setButtonText (juce::String (juce::roundToInt (theme::scaleSteps[(size_t) i] * 100.0f)) + "%");
        b.setRadioGroupId (sizeGroupId);
        styleChoiceKey (b);
        b.setFontPx (13.0f);
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

    paletteCaption.setText ("COLOR PALETTE", juce::dontSendNotification);
    styleCaption (paletteCaption);
    interfacePage.addAndMakeVisible (paletteCaption);

    const AppSettings::Palette palettes[2] { AppSettings::Palette::standard, AppSettings::Palette::colourBlindSafe };
    paletteButtons[0].setButtonText ("Standard");
    paletteButtons[1].setButtonText (juce::String (juce::CharPointer_UTF8 ("Color-blind safe (red\xe2\x80\x93green)")));
    paletteButtons[0].setTooltip ("The standard colors: green / amber / red meters and pads");
    paletteButtons[1].setTooltip ("Colors told apart without red-green vision: blue / yellow / vermilion meters, "
                                  "Okabe-Ito stem colors");
    for (int i = 0; i < (int) paletteButtons.size(); ++i)
    {
        auto& b = paletteButtons[(size_t) i];
        b.setRadioGroupId (paletteGroupId);
        styleChoiceKey (b);
        b.setFontPx (13.0f);
        const auto palette = palettes[i];
        b.onClick = [this, palette]
        {
            setPalette (palette);
            if (callbacks.setPalette)
                callbacks.setPalette (palette);
        };
        interfacePage.addAndMakeVisible (b);
    }

    paletteNote.setText ("Color-blind safe replaces the red / green pairs (meters, Play, Mute, the Residual and the "
                         "stem colors) with hues that stay distinct for protanopia and deuteranopia. "
                         "It applies at once to the main window and here.", juce::dontSendNotification);
    styleNote (paletteNote);
    interfacePage.addAndMakeVisible (paletteNote);
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
    settingsFileNote.setColour (juce::Label::textColourId, theme::colour (C::muted).withMultipliedAlpha (0.8f));
    sessionPage.addAndMakeVisible (settingsFileNote);
    addChildComponent (sessionPage);

    // About
    const auto version = appVersionString();
    aboutVersionText = version.isNotEmpty() ? "v" + version : juce::String();

    avatar = std::make_unique<AvatarBadge>();
    aboutPage.addAndMakeVisible (*avatar);

    aboutHandle.setButtonText ("@argentstygian");
    aboutHandle.setURL (juce::URL (authorUrl));
    aboutHandle.setTooltip (authorUrl);
    aboutPage.addAndMakeVisible (aboutHandle);

    aboutDescription.setText ("Compare an original mix (or a stem set) against stems, A/B, with a live null / residual.",
                              juce::dontSendNotification);
    aboutDescription.setJustificationType (juce::Justification::topLeft);
    aboutDescription.setMinimumHorizontalScale (1.0f);
    aboutPage.addAndMakeVisible (aboutDescription);

    aboutLicenseCaption.setText ("LICENSE", juce::dontSendNotification);
    styleCaption (aboutLicenseCaption);
    aboutPage.addAndMakeVisible (aboutLicenseCaption);

    aboutLicense.setText ("Free software under the GNU Affero General Public License v3.0", juce::dontSendNotification);
    aboutLicense.setJustificationType (juce::Justification::topLeft);
    aboutLicense.setMinimumHorizontalScale (1.0f);
    aboutPage.addAndMakeVisible (aboutLicense);

    aboutCopyright.setText (copyrightSign + " The Stem A/B Player contributors", juce::dontSendNotification);
    styleNote (aboutCopyright);
    aboutPage.addAndMakeVisible (aboutCopyright);

    aboutSource = std::make_unique<LinkPad> ("github.com/argentstygian/StemABPlayer", juce::URL (sourceCodeUrl));
    aboutSource->setTooltip (sourceCodeUrl);
    aboutPage.addAndMakeVisible (*aboutSource);

    aboutBuiltWithCaption.setText ("BUILT WITH", juce::dontSendNotification);
    styleCaption (aboutBuiltWithCaption);
    aboutPage.addAndMakeVisible (aboutBuiltWithCaption);

    {
        // The runtime string reads "FFmpeg 9.0.2 (libavformat ..., libavcodec ...)": the row has the name already.
        auto ffmpeg = juce::String (ffmpegVersionString());
        if (ffmpeg.startsWith ("FFmpeg "))
            ffmpeg = ffmpeg.substring (7);

        creditRows.push_back ({ "JUCE 8", "AGPLv3", {} });
        creditRows.push_back ({ "FFmpeg", "LGPL 2.1+", ffmpeg });
        creditRows.push_back ({ "r8brain-free-src", "MIT", {} });
       #if JUCE_ASIO
        creditRows.push_back ({ "Steinberg ASIO SDK", "GPLv3 option", {} });
       #else
        creditRows.push_back ({ "Built without ASIO", {}, {} });
       #endif
        creditRows.push_back ({ "Archivo, Archivo Narrow, JetBrains Mono fonts", "SIL OFL 1.1", {} });
        creditRows.push_back ({ "Okabe" + enDash + "Ito palette", "color-blind safe", {} });
    }

    aboutTrademarks.setText ("ASIO is a trademark of Steinberg Media Technologies GmbH.\n"
                             "Stems and the .stem.m4a format are by Native Instruments; not affiliated.",
                             juce::dontSendNotification);
    styleNote (aboutTrademarks);
    aboutPage.addAndMakeVisible (aboutTrademarks);

    refreshAboutColours();
    addChildComponent (aboutPage);

    refreshFolders();
    setPalette (settings.getColourPalette());
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

    const auto captionFont = theme::silkFont (11.0f * s).withExtraKerningFactor (0.08f);
    const auto noteFont = theme::uiFont (theme::em (0.86f, s));

    for (auto& b : navButtons)
        b.setUiScale (s);
    for (auto& b : sizeButtons)
        b.setUiScale (s);
    for (auto& b : paletteButtons)
        b.setUiScale (s);

    for (auto& row : folderRows)
    {
        row.caption.setFont (captionFont);
        row.path.setFont (theme::monoFont (theme::em (0.86f, s)));
        row.path.setBorderSize ({ 0, theme::scaled (10.0f, s), 0, theme::scaled (10.0f, s) });
        row.hint.setFont (theme::uiFont (theme::em (0.8f, s)));
        row.hint.setBorderSize ({ 0, 0, 0, 0 });
        row.browse.setUiScale (s);
        row.reset.setUiScale (s);
    }
    sizeCaption.setFont (captionFont);
    paletteCaption.setFont (captionFont);

    aboutLicenseCaption.setFont (captionFont);
    aboutBuiltWithCaption.setFont (captionFont);
    aboutDescription.setFont (theme::uiFont (theme::em (0.95f, s)));
    aboutLicense.setFont (theme::uiFont (theme::em (0.95f, s)));
    aboutHandle.setFont (theme::uiFont (theme::em (0.82f, s)), false, juce::Justification::centredRight);
    avatar->setUiScale (s);
    aboutSource->setUiScale (s);
    for (auto* l : { &aboutLicenseCaption, &aboutBuiltWithCaption, &aboutDescription, &aboutLicense })
        l->setBorderSize ({ 0, 0, 0, 0 });

    for (auto* l : { &foldersNote, &sizeNote, &paletteNote, &sessionNote, &aboutCopyright })
    {
        l->setFont (noteFont);
        l->setBorderSize ({ 0, 0, 0, 0 });
    }
    aboutTrademarks.setFont (theme::uiFont (theme::em (0.74f, s)));
    aboutTrademarks.setBorderSize ({ 0, 0, 0, 0 });
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

void PreferencesPanel::setPalette (AppSettings::Palette palette)
{
    paletteButtons[0].setToggleState (palette == AppSettings::Palette::standard, juce::dontSendNotification);
    paletteButtons[1].setToggleState (palette == AppSettings::Palette::colourBlindSafe, juce::dontSendNotification);
}

void PreferencesPanel::refreshPalette()
{
    refreshFolders();       // the hint colours
    refreshAboutColours();
    sendLookAndFeelChange();
    repaint();
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
    aboutPage.setVisible (s == Section::about);

    if (s == Section::folders)
        refreshFolders();
    repaint();
}

//==============================================================================
void PreferencesPanel::refreshAboutColours()
{
    for (auto* l : { &aboutDescription, &aboutLicense })
        l->setColour (juce::Label::textColourId, theme::colour (C::text));
    for (auto* l : { &aboutLicenseCaption, &aboutBuiltWithCaption })
        l->setColour (juce::Label::textColourId, theme::colour (C::silk));
    for (auto* l : { &aboutCopyright, &aboutTrademarks })
        l->setColour (juce::Label::textColourId, theme::colour (C::muted));

    aboutHandle.setColour (juce::HyperlinkButton::textColourId, theme::colour (C::muted));
    aboutHandle.repaint();
    aboutSource->repaint();
    avatar->repaint();
    repaint();
}

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
            row.hint.setColour (juce::Label::textColourId, theme::colour (C::muted));
            row.reset.setEnabled (false);
        }
        else if (! chosen.isDirectory())
        {
            row.path.setText (chosen.getFullPathName(), juce::dontSendNotification);
            row.hint.setText ("Folder not found " + middleDot + " the dialog opens in the default: " + builtIn.getFullPathName(),
                              juce::dontSendNotification);
            row.hint.setColour (juce::Label::textColourId, theme::colour (C::amber));
            row.reset.setEnabled (true);
        }
        else
        {
            row.path.setText (chosen.getFullPathName(), juce::dontSendNotification);
            row.hint.setText ("Chosen folder " + middleDot + " Reset returns to " + builtIn.getFullPathName(),
                              juce::dontSendNotification);
            row.hint.setColour (juce::Label::textColourId, theme::colour (C::muted));
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
    g.fillAll (theme::colour (C::ground));

    // Section list column: a raised panel with a dark edge
    const auto nav = getLocalBounds().removeFromLeft (theme::scaled (navWidth, s));
    theme::drawPanel (g, nav.toFloat(), s, C::panel, 0.0f, true);
    g.setColour (theme::colour (C::panelEdge));
    g.fillRect (nav.getRight() - 1, 0, 1, getHeight());

    if (section == Section::about)
    {
        paintAbout (g);
        return;
    }

    // Section title + subtitle
    auto header = contentArea().removeFromTop (theme::scaled (52.0f, s));
    g.setColour (theme::colour (C::text));
    g.setFont (theme::uiFont (theme::em (1.25f, s), true));
    g.drawText (sectionName (section), header.removeFromTop (theme::scaled (28.0f, s)), juce::Justification::centredLeft, false);
    g.setColour (theme::colour (C::muted));
    g.setFont (theme::uiFont (theme::em (0.86f, s)));
    g.drawText (sectionSubtitle (section), header.removeFromTop (theme::scaled (20.0f, s)), juce::Justification::centredLeft, false);

    const auto c = contentArea();
    g.setColour (theme::colour (C::panelEdge));
    g.fillRect (c.getX(), c.getY() + theme::scaled (60.0f, s), c.getWidth(), 1);
    g.setColour (theme::colour (C::panelHi).withAlpha (0.5f));
    g.fillRect (c.getX(), c.getY() + theme::scaled (60.0f, s) + 1, c.getWidth(), 1);
}

void PreferencesPanel::paintAbout (juce::Graphics& g) const
{
    const float s = uiScale;
    const auto px = [s] (float v) { return theme::scaled (v, s); };
    const auto& r = aboutRects;

    juce::Graphics::ScopedSaveState save (g);
    g.setOrigin (contentArea().getPosition());

    // Name and version on a sunken screen, as the transport time
    {
        const auto plate = r.plate.toFloat();
        theme::drawScreen (g, plate, s);

        const auto inner = r.plate.reduced (px (18.0f), 0);
        const auto versionFont = theme::monoFont (22.0f * s, true);
        const float versionW = aboutVersionText.isEmpty() ? 0.0f
                                                          : std::ceil (juce::GlyphArrangement::getStringWidth (versionFont, aboutVersionText));
        auto right = inner.withLeft (inner.getRight() - (int) versionW - px (2.0f));

        g.setColour (theme::colour (C::text));
        g.setFont (theme::uiFont (theme::em (1.5f, s), true));
        g.drawText ("Stem A/B Player", inner.withRight (right.getX() - px (12.0f)), juce::Justification::centredLeft, true);

        if (versionW > 0.0f)
        {
            theme::drawSilk (g, "Version", right.removeFromTop (px (20.0f)).withTrimmedTop (px (7.0f)).toFloat(), 9.0f * s,
                             theme::colour (C::screenDim), juce::Justification::centredRight);
            g.setColour (theme::colour (C::screenText));
            g.setFont (versionFont);
            g.drawText (aboutVersionText, right.withTrimmedBottom (px (6.0f)), juce::Justification::centredRight, false);
        }
    }

    // Hairline rules after the captions
    const auto rule = [&g] (juce::Rectangle<int> area)
    {
        g.setColour (theme::colour (C::panelEdge));
        g.fillRect (area.getX(), area.getY(), area.getWidth(), 1);
        g.setColour (theme::colour (C::panelHi).withAlpha (0.5f));
        g.fillRect (area.getX(), area.getY() + 1, area.getWidth(), 1);
    };
    rule (r.licenceRule);
    rule (r.builtWithRule);
    rule (r.trademarkRule);

    // Credits: component | licence, one hairline between the rows
    {
        const auto nameFont = theme::uiFont (theme::em (0.9f, s));
        const auto detailFont = theme::monoFont (10.0f * s);
        const int nameW = px (330.0f);
        const int rowH = px (21.0f);
        const int detailH = px (15.0f);
        auto area = r.credits;

        for (size_t i = 0; i < creditRows.size(); ++i)
        {
            const auto& row = creditRows[i];
            auto line = area.removeFromTop (rowH);

            g.setFont (nameFont);
            g.setColour (theme::colour (C::text));
            g.drawText (row.name, line.removeFromLeft (nameW), juce::Justification::centredLeft, true);
            g.setColour (theme::colour (C::muted));
            g.drawText (row.licence, line, juce::Justification::centredLeft, true);

            if (row.detail.isNotEmpty())
            {
                g.setFont (detailFont);
                g.setColour (theme::colour (C::muted).withMultipliedAlpha (0.8f));
                g.drawText (row.detail, area.removeFromTop (detailH), juce::Justification::centredLeft, true);
            }

            if (i + 1 < creditRows.size())
            {
                g.setColour (theme::colour (C::panelEdge).withAlpha (0.8f));
                g.fillRect (area.getX(), area.getY() - 1, area.getWidth(), 1);
            }
        }
    }
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
    aboutPage.setBounds (contentArea());        // no page heading on About: it keeps that height

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
        sizeNote.setBounds (r.removeFromTop (px (44.0f)));

        r.removeFromTop (px (20.0f));
        paletteCaption.setBounds (r.removeFromTop (px (18.0f)));
        r.removeFromTop (px (8.0f));
        line = r.removeFromTop (px (34.0f));
        const int widths[2] { px (120.0f), px (250.0f) };
        for (size_t i = 0; i < paletteButtons.size(); ++i)
        {
            paletteButtons[i].setBounds (line.removeFromLeft (widths[i]));
            line.removeFromLeft (px (6.0f));
        }
        r.removeFromTop (px (16.0f));
        paletteNote.setBounds (r.removeFromTop (px (60.0f)));
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

    // About (base scale: 568 x 516, no heading). The right column (132 wide) holds the avatar and its handle;
    // everything beside it is 420 wide, everything below y = 134 uses the whole width.
    {
        const int w = aboutPage.getWidth();
        const int columnW = px (132.0f);
        const int leftW = w - columnW - px (16.0f);
        const auto captionRule = [&] (juce::Label& caption, int y, juce::Rectangle<int>& ruleOut)
        {
            const int capW = (int) std::ceil (theme::silkWidth (caption.getText(), 11.0f * s)) + px (4.0f);
            caption.setBounds (0, y, capW, px (18.0f));
            ruleOut = { capW + px (10.0f), y + px (8.0f), w - capW - px (10.0f), 2 };
        };

        aboutRects.plate = { 0, 0, leftW, px (60.0f) };
        aboutDescription.setBounds (0, px (68.0f), leftW, px (40.0f));

        avatar->setBounds (w - columnW, 0, columnW, px (102.0f));
        aboutHandle.setBounds (w - columnW, px (104.0f), columnW, px (18.0f));

        captionRule (aboutLicenseCaption, px (134.0f), aboutRects.licenceRule);
        aboutLicense.setBounds (0, px (158.0f), w, px (20.0f));
        aboutCopyright.setBounds (0, px (178.0f), w, px (20.0f));
        aboutSource->setBounds (0, px (206.0f), juce::jmin (w, aboutSource->preferredWidth()), px (30.0f));

        captionRule (aboutBuiltWithCaption, px (256.0f), aboutRects.builtWithRule);
        aboutRects.credits = { 0, px (282.0f), w, aboutPage.getHeight() - px (282.0f) };

        const int trademarkH = px (32.0f);
        aboutTrademarks.setBounds (0, aboutPage.getHeight() - trademarkH, w, trademarkH);
        aboutRects.trademarkRule = { 0, aboutPage.getHeight() - trademarkH - px (12.0f), w, 2 };
    }
}

//==============================================================================
PreferencesWindow::PreferencesWindow (AppSettings& settings, juce::AudioDeviceManager& dm, PreferencesPanel::Callbacks cb)
    : juce::DocumentWindow ("Preferences", theme::colour (C::ground), juce::DocumentWindow::closeButton),
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

void PreferencesWindow::refreshPalette()
{
    setBackgroundColour (theme::colour (C::ground));
    panel.refreshPalette();
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
