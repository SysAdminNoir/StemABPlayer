#include "FilePreview.h"

#include <juce_events/juce_events.h>

#include "decode/Decode.h"

namespace stemab
{

namespace
{
    std::shared_ptr<const WaveformSummary> summarise (const juce::AudioBuffer<float>& audio, const CancelCheck& cancel)
    {
        const float* channels[2] = { audio.getNumChannels() > 0 ? audio.getReadPointer (0) : nullptr,
                                     audio.getNumChannels() > 1 ? audio.getReadPointer (1) : nullptr };
        return std::make_shared<const WaveformSummary> (
            WaveformSummary::build (channels, 2, audio.getNumSamples(), WaveformSummary::defaultBinSize, cancel));
    }
}

FilePreview::FilePreview (std::shared_ptr<DecodeStash> s)
    : stash (std::move (s)),
      token (std::make_shared<Token>()),
      pool (juce::ThreadPoolOptions{}.withThreadName ("Preview").withNumberOfThreads (2))
{
    jassert (stash != nullptr);
    token->owner = this;
}

FilePreview::~FilePreview()
{
    token->owner = nullptr;         // results still queued on the message thread are dropped
    shuttingDown = true;            // running jobs stop at their next cancel check
    ++originalGeneration;
    ++stemsGeneration;
    stash->clear();                 // also wakes a pair load waiting for one of these previews
    pool.removeAllJobs (true, 10000);
}

void FilePreview::cancelOriginal()
{
    ++originalGeneration;
    originalPending = false;
}

void FilePreview::cancelStems()
{
    ++stemsGeneration;
    stemsPending = false;
}

void FilePreview::cancelAll()
{
    cancelOriginal();
    cancelStems();
    stash->clear();
}

//==============================================================================
void FilePreview::previewOriginal (const juce::File& file)
{
    const auto generation = ++originalGeneration;
    const auto ticket = stash->beginOriginal (file);    // before the pair load can ask for it
    originalPending = true;

    pool.addJob ([this, file, generation, ticket, st = stash, tok = token]
    {
        const CancelCheck cancel = [this, generation] { return shuttingDown.load() || originalGeneration.load() != generation; };

        auto decoded = decodeOriginal (file, DecodeOptions { cancel, true });

        OriginalResult result;
        result.file = file;

        if (isCancelled (cancel))
        {
            st->abandonOriginal (ticket);
            return;
        }

        if (! decoded.ok)
        {
            st->abandonOriginal (ticket);
            result.error = decoded.error;
        }
        else
        {
            result.summary = summarise (decoded.audio, cancel);
            if (isCancelled (cancel))
            {
                st->abandonOriginal (ticket);
                return;
            }
            result.length = decoded.audio.getNumSamples();
            result.sampleRate = decoded.sampleRate;
            st->putOriginal (ticket, std::move (decoded));
        }

        juce::MessageManager::callAsync ([tok, generation, result]
        {
            auto* owner = tok->owner;
            if (owner == nullptr || owner->originalGeneration.load() != generation)
                return;
            owner->originalPending = false;
            if (owner->onOriginalReady)
                owner->onOriginalReady (result);
        });
    });
}

void FilePreview::previewStems (const StemSource& source)
{
    const auto generation = ++stemsGeneration;
    const auto ticket = stash->beginStems (source);
    stemsPending = true;

    pool.addJob ([this, source, generation, ticket, st = stash, tok = token]
    {
        const CancelCheck cancel = [this, generation] { return shuttingDown.load() || stemsGeneration.load() != generation; };

        auto decoded = decodeStemSource (source, DecodeOptions { cancel, true });

        StemsResult result;
        result.source = source;

        if (isCancelled (cancel))
        {
            st->abandonStems (ticket);
            return;
        }

        if (! decoded.ok)
        {
            st->abandonStems (ticket);
            result.error = decoded.error;
        }
        else
        {
            for (size_t i = 0; i < result.stems.size(); ++i)
            {
                result.stems[i] = summarise (decoded.stems[i], cancel);
                if (isCancelled (cancel))
                {
                    st->abandonStems (ticket);
                    return;
                }
            }
            result.length = decoded.stems[0].getNumSamples();
            result.sampleRate = decoded.sampleRate;
            st->putStems (ticket, std::move (decoded));
        }

        juce::MessageManager::callAsync ([tok, generation, result]
        {
            auto* owner = tok->owner;
            if (owner == nullptr || owner->stemsGeneration.load() != generation)
                return;
            owner->stemsPending = false;
            if (owner->onStemsReady)
                owner->onStemsReady (result);
        });
    });
}

} // namespace stemab
