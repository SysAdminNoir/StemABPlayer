#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Theme.h"

namespace stemab
{

/** Keys and pads of the hardware panel.

    The component is a little larger than the visible face: a key reserves 2 px under its face for the bottom
    shadow, a glowing pad (or key) a margin on every side for its glow. Lay them out with setFaceBounds() (the
    face rectangle); clicks only hit the face. */
class HwButton : public juce::TextButton
{
public:
    enum class Kind { key, pad };

    explicit HwButton (Kind = Kind::key, const juce::String& text = {});

    /** The look when the toggle state is on: a lit pad in `fill` with text in `ink` (glowing if `glow`). */
    void setLitColours (theme::C fill, theme::C ink, bool glow = true);
    void setUiScale (float);
    /** Text size at 100 % (default: 12 for a key, 11 for a pad). */
    void setFontPx (float basePx);

    void setFaceBounds (juce::Rectangle<int> face);
    juce::Rectangle<int> getFaceBounds() const;

    bool hitTest (int x, int y) override;
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

protected:
    int margin() const;

    Kind kind;
    theme::C litFill = theme::C::colourB, litInk = theme::C::inkDark;
    bool hasLit = false, litGlow = true;
    float uiScale = 1.0f, fontPx;
};

/** Transport (and header) keys drawn with Paths (24-unit icon box, scaled to the key). Play is a green pad
    that glows (pause bars while playing); Loop lights while looping. A disabled key draws its icon dimmed. */
class IconButton final : public juce::Button
{
public:
    enum class Icon { toStart, back, forward, loop, playPause, eject };

    IconButton (const juce::String& name, Icon);

    /** playPause only: shows the pause bars instead of the play triangle. */
    void setPlaying (bool);
    void setUiScale (float s)       { uiScale = s; repaint(); }

    void setFaceBounds (juce::Rectangle<int> face);

    bool hitTest (int x, int y) override;
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    bool glows() const noexcept     { return icon == Icon::playPause || icon == Icon::loop; }
    int margin() const;
    juce::Rectangle<int> face() const;

    Icon icon;
    bool playing = false;
    float uiScale = 1.0f;
};

/** The CLIP pad: lit red (with glow) while a Clip is held; clicking it resets the hold. */
class ClipButton final : public juce::Button
{
public:
    ClipButton();

    void setLit (bool);
    void setUiScale (float s)       { uiScale = s; repaint(); }

    void setFaceBounds (juce::Rectangle<int> face);
    juce::Rectangle<int> getFaceBounds() const;

    bool hitTest (int x, int y) override;
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    int margin() const              { return theme::scaled (8.0f, uiScale); }

    bool lit = false;
    float uiScale = 1.0f;
};

} // namespace stemab
