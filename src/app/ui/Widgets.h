#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace stemab
{

/** Transport (and header) buttons drawn with Paths (24-unit icon box, scaled to the button). A disabled
    button draws its icon dimmed. */
class IconButton final : public juce::Button
{
public:
    enum class Icon { toStart, back, forward, loop, playPause, eject };

    IconButton (const juce::String& name, Icon);

    /** playPause only: shows the pause bars (amber) instead of the play triangle (ivory). */
    void setPlaying (bool);
    void setUiScale (float s)       { uiScale = s; repaint(); }

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    Icon icon;
    bool playing = false;
    float uiScale = 1.0f;
};

/** The round CLIP LED button: lit red (with glow) while a Clip is held; clicking it resets the hold. */
class ClipButton final : public juce::Button
{
public:
    ClipButton();

    void setLit (bool);
    void setUiScale (float s)       { uiScale = s; repaint(); }

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    bool lit = false;
    float uiScale = 1.0f;
};

} // namespace stemab
