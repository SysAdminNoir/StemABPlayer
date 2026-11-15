#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>

#include <functional>
#include <memory>

#include "engine/PlaybackEngine.h"
#include "load/AlignmentCache.h"
#include "load/PairLoader.h"

namespace stemab
{

/** Progress of one load stage, as the UI shows it. */
enum class StageState { idle, running, done, failed };

/** What the UI shows about the loaded pair. Message thread only. */
struct LoadStatus
{
    juce::String originalName, stemFileName;

    bool busy = false;                  // A or B still on its way
    juce::String progress;              // one-line summary: "Decoding...", "Original playable - Stems...", "Ready"
    juce::String error;                 // non-empty: a stage failed (shown, never a crash)
    juce::String notice;                // e.g. a cached Alignment Offset that the re-measurement corrected

    StageState original = StageState::idle, stems = StageState::idle,
               alignment = StageState::idle, trackNullDepth = StageState::idle;
    bool originalReused = false;        // same Original file as before: not decoded again
    bool alignmentFromCache = false;

    bool hasAlignment = false;
    juce::int64 offsetSamples = 0;
    int polarity = +1;
    double confidence = 0.0;
    bool lowConfidence = false;

    bool hasTrackNullDepth = false;
    double trackNullDepthDb = 0.0;

    // Timings in seconds, measured from the moment the pair was chosen (< 0: not reached yet).
    double timeToOriginal = -1.0;       // A playable
    double timeToStems = -1.0;          // B and the Residual ready
    double timeToNullDepth = -1.0;
    double decodeOriginalSeconds = 0.0, decodeStemsSeconds = 0.0, alignmentSeconds = 0.0, nullDepthSeconds = 0.0;

    double sourceRate = 0.0;            // the Session's rate = A's own rate (0 until decoded)
    double deviceRate = 0.0;            // the device's rate when the status was last updated (0: no device)
    juce::String rateState;             // "Native Rate 44100 Hz" / "Resampled 44100 -> 48000 Hz (realtime)"
    juce::String rateNote;              // e.g. a WASAPI shared-mode remark
};

/** Glue between files, the staged PairLoader and the PlaybackEngine (message thread):

      - load(): the file names and stage states show at once; the current pair keeps playing.
      - Original decoded -> rate policy (Native Rate preferred), then the Original-only Session replaces the
        playing one: fade out, swap, fade in at 0 if it was playing (at the same position if it is the same
        Original file).
      - Stems + Alignment -> engine.upgradeSession(): B and the Residual fade in, the playhead carries on.
      - Track Null Depth arrives last, off the critical path.

    Measured alignments are cached (AlignmentCache, %APPDATA%/StemABPlayer). Loading never depends on the
    device rate: if the device runs at another rate, the engine resamples its mixed output in realtime
    (ADR 0005). Construct, use and destroy on the message thread. */
class SessionLoader final
{
public:
    SessionLoader (juce::AudioDeviceManager&, PlaybackEngine&);
    ~SessionLoader();

    /** Starts loading a pair, replacing the current one once the new Original is decoded. */
    void load (const juce::File& original, const juce::File& stemFile);

    /** Call on every AudioDeviceManager change. A different device re-runs the rate policy (Native Rate
        preferred); a rate change on the same device is respected (the engine resamples in realtime). */
    void deviceConfigurationChanged();

    const LoadStatus& getStatus() const noexcept                    { return status; }

    /** The session most recently handed to the engine (nullptr if none). */
    std::shared_ptr<const Session> getPlaybackSession() const       { return session; }

    /** Called on the message thread whenever getStatus() changed. */
    std::function<void()> onStatusChanged;

private:
    void originalReady (const PairLoader::OriginalReady&);
    void stemsDecoded (double seconds);
    void stemsReady (const PairLoader::StemsReady&);
    void alignmentCorrected (const PairLoader::AlignmentCorrected&);
    void trackNullDepthReady (const PairLoader::TrackNullDepth&);
    void failed (LoadStage, const juce::String& message);
    void setAlignment (const AlignmentResult&);
    void updateProgress();

    /** Rate policy on the message thread; returns the rate the device now runs at (0 if no device). */
    double applyRatePolicy (double sourceRate);
    void updateRateStatus();
    juce::String deviceIdentity() const;
    void changed();

    juce::AudioDeviceManager& deviceManager;
    PlaybackEngine& engine;

    AlignmentCache cache;               // before the loader: it must outlive it
    PairLoader pairLoader;

    std::shared_ptr<const Session> session;    // what the engine plays (at the source rate)
    juce::String lastDeviceIdentity;
    juce::String policyNote;                   // e.g. "Device refused 44100 Hz: ..."

    LoadStatus status;

    JUCE_DECLARE_NON_COPYABLE (SessionLoader)
};

} // namespace stemab
