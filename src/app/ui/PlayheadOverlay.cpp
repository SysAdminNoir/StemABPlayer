#include "app/ui/PlayheadOverlay.h"

#include <cmath>

#include "app/Theme.h"
#include "app/ui/Format.h"

namespace stemab
{

PlayheadOverlay::PlayheadOverlay()
{
    setOpaque (false);
    setInterceptsMouseClicks (true, false);
    setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
}

void PlayheadOverlay::setTimeline (juce::int64 lengthSamples, double sampleRate)
{
    if (lengthSamples == length && sampleRate == rate)
        return;
    length = lengthSamples;
    rate = sampleRate;
    shownSample = juce::jlimit ((juce::int64) 0, juce::jmax ((juce::int64) 0, length), shownSample);
    dragging = false;
    pendingSeek = -1;
    paintedX = -1.0f;
    repaint();
}

void PlayheadOverlay::setRulerHeight (int h)                { rulerHeight = h; repaint(); }
void PlayheadOverlay::setDarkenAreas (juce::Array<juce::Rectangle<int>> a)  { darkenAreas = std::move (a); repaint(); }
void PlayheadOverlay::setUiScale (float s)                  { uiScale = s; repaint(); }

void PlayheadOverlay::setHint (const juce::String& h)
{
    if (h != hint)
    {
        hint = h;
        repaint();
    }
}

void PlayheadOverlay::resized()
{
    paintedX = -1.0f;
}

float PlayheadOverlay::sampleToX (juce::int64 sample) const noexcept
{
    if (length <= 0)
        return 0.0f;
    return (float) ((double) getWidth() * (double) sample / (double) length);
}

juce::int64 PlayheadOverlay::xToSample (float x) const noexcept
{
    if (length <= 0 || getWidth() <= 0)
        return 0;
    const double p = juce::jlimit (0.0, 1.0, (double) x / (double) getWidth());
    return juce::jlimit ((juce::int64) 0, length, (juce::int64) std::llround (p * (double) length));
}

juce::String PlayheadOverlay::bubbleText() const
{
    return rate > 0.0 ? format::minutesSeconds ((double) shownSample / rate) : juce::String();
}

juce::Rectangle<int> PlayheadOverlay::stripAround (float x) const
{
    // Covers the glow, the cap and the bubble (which may sit on either side of the line).
    const int margin = theme::scaled (80.0f, uiScale);
    return { (int) std::floor (x) - margin, 0, 2 * margin + 2, getHeight() };
}

void PlayheadOverlay::moveShownTo (juce::int64 sample)
{
    shownSample = sample;
    const float x = sampleToX (shownSample);
    const auto bubble = bubbleText();

    if (paintedX < 0.0f)
    {
        repaint();
    }
    else if (std::abs (x - paintedX) >= 0.25f || bubble != paintedBubble)
    {
        const auto area = stripAround (paintedX).getUnion (stripAround (x));
        repaint (area);
    }
}

void PlayheadOverlay::update (juce::int64 playheadSample)
{
    const double now = juce::Time::getMillisecondCounterHiRes();

    if (dragging)
    {
        if (pendingSeek >= 0 && now - lastSeekMs >= dragSeekIntervalMs)
            sendSeek (pendingSeek);
        return;   // the drawn playhead follows the mouse
    }

    if (now < holdUntilMs)
        return;

    moveShownTo (juce::jlimit ((juce::int64) 0, juce::jmax ((juce::int64) 0, length), playheadSample));
}

void PlayheadOverlay::sendSeek (juce::int64 sample)
{
    pendingSeek = -1;
    lastSeekMs = juce::Time::getMillisecondCounterHiRes();
    if (onSeek)
        onSeek (sample);
}

void PlayheadOverlay::mouseDown (const juce::MouseEvent& e)
{
    if (length <= 0)
        return;

    dragging = true;
    const auto target = xToSample (e.position.x);
    moveShownTo (target);
    sendSeek (target);
}

void PlayheadOverlay::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;

    const auto target = xToSample (e.position.x);
    moveShownTo (target);

    if (juce::Time::getMillisecondCounterHiRes() - lastSeekMs >= dragSeekIntervalMs)
        sendSeek (target);
    else
        pendingSeek = target;
}

void PlayheadOverlay::mouseUp (const juce::MouseEvent&)
{
    if (! dragging)
        return;

    dragging = false;
    if (pendingSeek >= 0)
        sendSeek (pendingSeek);
    holdUntilMs = juce::Time::getMillisecondCounterHiRes() + holdAfterDragMs;
}

void PlayheadOverlay::paint (juce::Graphics& g)
{
    if (length <= 0)
    {
        if (hint.isNotEmpty())
        {
            g.setColour (theme::colour (theme::mutedTextArgb));
            g.setFont (theme::uiFont (theme::em (1.0f, uiScale)));
            g.drawText (hint, getLocalBounds().withTrimmedTop (rulerHeight), juce::Justification::centred, true);
        }
        paintedX = -1.0f;
        return;
    }

    const float x = sampleToX (shownSample);
    paintedX = x;
    paintedBubble = bubbleText();

    // Played region
    g.setColour (juce::Colour (8, 10, 13).withAlpha (0.5f));
    for (auto area : darkenAreas)
        if (x > (float) area.getX())
            g.fillRect (area.toFloat().withRight (juce::jmin ((float) area.getRight(), x)));

    // Playhead: soft glow, then the 2 px line
    const float h = (float) getHeight();
    for (const auto [w, a] : { std::pair<float, float> { 10.0f, 0.06f }, { 6.0f, 0.10f }, { 4.0f, 0.18f } })
    {
        g.setColour (juce::Colours::white.withAlpha (a));
        g.fillRect (juce::Rectangle<float> (x - w * 0.5f, 0.0f, w, h));
    }
    g.setColour (juce::Colours::white);
    g.fillRect (juce::Rectangle<float> (x - 1.0f, 0.0f, 2.0f, h));

    // Cap
    const float capHalf = 7.0f * uiScale, capH = 9.0f * uiScale;
    juce::Path cap;
    cap.addTriangle (x - capHalf, 0.0f, x + capHalf, 0.0f, x, capH);
    g.fillPath (cap);

    // Time bubble on the ruler
    if (paintedBubble.isNotEmpty())
    {
        const auto font = theme::monoFont (theme::em (0.68f, uiScale));
        const float padX = 5.0f * uiScale, padY = 1.0f * uiScale;
        const float tw = juce::GlyphArrangement::getStringWidth (font, paintedBubble);
        const float bw = tw + 2.0f * padX, bh = font.getHeight() + 2.0f * padY;
        float bx = x + 10.0f * uiScale;
        if (bx + bw > (float) getWidth())
            bx = x - 10.0f * uiScale - bw;
        const juce::Rectangle<float> box (bx, 4.0f * uiScale, bw, bh);

        g.setColour (juce::Colours::white);
        g.fillRoundedRectangle (box, 3.0f * uiScale);
        g.setColour (theme::colour (theme::groundArgb));
        g.setFont (font);
        g.drawText (paintedBubble, box, juce::Justification::centred, false);
    }
}

} // namespace stemab
