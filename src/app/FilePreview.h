#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <functional>
#include <memory>

#include "analysis/WaveformSummary.h"
#include "engine/Session.h"
#include "load/DecodeStash.h"

namespace stemab
{

/** Waveform previews for a file chosen on its own, before the pair is complete (and so before any Session or
    Alignment exists): the file is decoded on a worker thread with the same decoders as the pair load and
    summarised with WaveformSummary. A Stem File's preview is its four Stems as decoded - not aligned to any
    Original (the codec delay is still in) - and has no Residual.

    The decoded audio goes into the DecodeStash, so when the other file is chosen the pair load takes it
    instead of decoding the file again (and waits for a preview still decoding it).

    One preview per kind at a time: a newer request (or a cancel) bumps that kind's generation, the old job
    stops at its next cancel check and its result is dropped. Results arrive on the message thread through
    MessageManager::callAsync, guarded by a token the destructor clears; the destructor also cancels and
    waits for running jobs. Construct, use and destroy on the message thread. */
class FilePreview final
{
public:
    using SummaryPtr = std::shared_ptr<const WaveformSummary>;

    struct OriginalResult
    {
        juce::File file;
        SummaryPtr summary;                 // null on failure
        juce::int64 length = 0;
        double sampleRate = 0.0;
        juce::String error;                 // non-empty: the file could not be decoded
    };

    struct StemsResult
    {
        juce::File file;
        std::array<SummaryPtr, Session::numStems> stems;   // Drums, Bass, Other, Vocals (null on failure)
        juce::int64 length = 0;
        double sampleRate = 0.0;
        juce::String error;
    };

    explicit FilePreview (std::shared_ptr<DecodeStash>);
    ~FilePreview();

    /** Called on the message thread with the result of the latest previewOriginal() / previewStems(). */
    std::function<void (const OriginalResult&)> onOriginalReady;
    std::function<void (const StemsResult&)> onStemsReady;

    void previewOriginal (const juce::File&);
    void previewStems (const juce::File&);

    void cancelOriginal();
    void cancelStems();
    /** Cancels both previews and empties the stash. */
    void cancelAll();

    /** A preview was requested and its result has not been delivered (or cancelled) yet. */
    bool isOriginalPending() const noexcept     { return originalPending; }
    bool isStemsPending() const noexcept        { return stemsPending; }

private:
    struct Token
    {
        FilePreview* owner = nullptr;       // cleared by the destructor; read only on the message thread
    };

    std::shared_ptr<DecodeStash> stash;
    std::shared_ptr<Token> token;
    std::atomic<juce::uint64> originalGeneration { 0 }, stemsGeneration { 0 };
    std::atomic<bool> shuttingDown { false };
    bool originalPending = false, stemsPending = false;
    juce::ThreadPool pool;

    JUCE_DECLARE_NON_COPYABLE (FilePreview)
};

} // namespace stemab
