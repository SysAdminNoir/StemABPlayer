#include "WaveformBuilder.h"

#include <juce_events/juce_events.h>

namespace stemab
{

WaveformBuilder::WaveformBuilder()
    : token (std::make_shared<Token>()),
      pool (juce::ThreadPoolOptions{}.withThreadName ("Waveforms").withNumberOfThreads (2))
{
    token->owner = this;
}

WaveformBuilder::~WaveformBuilder()
{
    token->owner = nullptr;         // results still queued on the message thread are dropped
    shuttingDown = true;            // running jobs stop at their next cancel check
    ++originalGeneration;
    ++stemsGeneration;
    pool.removeAllJobs (true, 10000);
}

void WaveformBuilder::buildOriginal (std::shared_ptr<const Session> session)
{
    const auto generation = ++originalGeneration;
    if (session == nullptr || session->length() <= 0)
        return;

    pool.addJob ([this, session, generation, tok = token]
    {
        const CancelCheck cancel = [this, generation] { return shuttingDown.load() || originalGeneration.load() != generation; };

        const float* channels[2] = { session->originalChannel (0), session->originalChannel (1) };
        auto summary = std::make_shared<const WaveformSummary> (
            WaveformSummary::build (channels, 2, session->length(), WaveformSummary::defaultBinSize, cancel));

        if (isCancelled (cancel))
            return;

        juce::MessageManager::callAsync ([tok, generation, summary = SummaryPtr (std::move (summary))]
        {
            if (auto* owner = tok->owner; owner != nullptr && owner->originalGeneration.load() == generation && owner->onOriginalReady)
                owner->onOriginalReady (summary);
        });
    });
}

void WaveformBuilder::buildStems (std::shared_ptr<const Session> session)
{
    const auto generation = ++stemsGeneration;
    if (session == nullptr || ! session->hasStems() || session->length() <= 0)
        return;

    pool.addJob ([this, session, generation, tok = token]
    {
        const CancelCheck cancel = [this, generation] { return shuttingDown.load() || stemsGeneration.load() != generation; };
        const auto length = session->length();

        StemSummaries result;
        const float* stemPtrs[Session::numStems][2];
        const float* const* stems[Session::numStems];

        for (int i = 0; i < Session::numStems; ++i)
        {
            stemPtrs[i][0] = session->stemChannel (i, 0);
            stemPtrs[i][1] = session->stemChannel (i, 1);
            stems[i] = stemPtrs[i];

            result.stems[(size_t) i] = std::make_shared<const WaveformSummary> (
                WaveformSummary::build (stemPtrs[i], 2, length, WaveformSummary::defaultBinSize, cancel));
            if (isCancelled (cancel))
                return;
        }

        const float* original[2] = { session->originalChannel (0), session->originalChannel (1) };
        result.residual = std::make_shared<const WaveformSummary> (
            WaveformSummary::buildResidual (original, 2, stems, Session::numStems, length,
                                            WaveformSummary::defaultBinSize, cancel));
        if (isCancelled (cancel))
            return;

        juce::MessageManager::callAsync ([tok, generation, result]
        {
            if (auto* owner = tok->owner; owner != nullptr && owner->stemsGeneration.load() == generation && owner->onStemsReady)
                owner->onStemsReady (result);
        });
    });
}

} // namespace stemab
