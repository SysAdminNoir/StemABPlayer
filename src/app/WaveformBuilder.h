#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <functional>
#include <memory>

#include "analysis/WaveformSummary.h"
#include "engine/Session.h"

namespace stemab
{

/** Computes waveform summaries off the message thread (a small juce::ThreadPool) for the lanes.

      - buildOriginal (session): A's summary.
      - buildStems (session): the four Stem summaries and the Residual (A - all four Stems), for a Session
        with Stems.

    Each job holds a shared_ptr<const Session>, so the audio buffers outlive the work. Every request bumps a
    generation counter; a job whose generation is no longer current stops early (cooperative cancel) and its
    result is dropped on delivery, so results from a previous pair never reach the UI. Results arrive on the
    message thread through MessageManager::callAsync, guarded by a token that the destructor clears; the
    destructor also cancels and waits for running jobs. Construct, use and destroy on the message thread. */
class WaveformBuilder final
{
public:
    using SummaryPtr = std::shared_ptr<const WaveformSummary>;

    struct StemSummaries
    {
        std::array<SummaryPtr, Session::numStems> stems;
        SummaryPtr residual;
    };

    WaveformBuilder();
    ~WaveformBuilder();

    /** Called on the message thread with the result of the latest buildOriginal() / buildStems(). */
    std::function<void (SummaryPtr)> onOriginalReady;
    std::function<void (const StemSummaries&)> onStemsReady;

    void buildOriginal (std::shared_ptr<const Session>);
    void buildStems (std::shared_ptr<const Session>);

    void cancelOriginal() noexcept      { ++originalGeneration; }
    void cancelStems() noexcept         { ++stemsGeneration; }

private:
    struct Token
    {
        WaveformBuilder* owner = nullptr;   // cleared by the destructor; read only on the message thread
    };

    std::shared_ptr<Token> token;
    std::atomic<juce::uint64> originalGeneration { 0 }, stemsGeneration { 0 };
    std::atomic<bool> shuttingDown { false };
    juce::ThreadPool pool;

    JUCE_DECLARE_NON_COPYABLE (WaveformBuilder)
};

} // namespace stemab
