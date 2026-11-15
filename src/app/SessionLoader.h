#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <functional>
#include <memory>

#include "engine/PlaybackEngine.h"

namespace stemab
{

/** What the UI shows about the loaded pair. Message thread only. */
struct LoadStatus
{
    juce::String originalName, stemFileName;

    bool busy = false;
    juce::String progress;              // "Decoding original...", "Ready", ...
    juce::String error;                 // non-empty: the load failed (shown, never a crash)

    bool hasAlignment = false;
    juce::int64 offsetSamples = 0;
    int polarity = +1;
    double confidence = 0.0;
    bool lowConfidence = false;

    bool hasTrackNullDepth = false;
    double trackNullDepthDb = 0.0;

    double sourceRate = 0.0;            // the Session's rate = A's own rate (0 until decoded)
    double deviceRate = 0.0;            // the device's rate when the status was last updated (0: no device)
    double loadSeconds = 0.0;           // decode + alignment + Track Null Depth + Session build
    juce::String rateState;             // "Native Rate 44100 Hz" / "Resampled 44100 -> 48000 Hz (realtime)"
    juce::String rateNote;              // e.g. a WASAPI shared-mode remark
};

/** Glue between files, Decode/Analysis and the PlaybackEngine:

      worker:  decodeOriginal + decodeStemFile -> Stems resampled to A's rate if they differ ->
               measureAlignment -> alignStems -> computeNullDepth -> Session at the source rate
      message: rate policy (Native Rate preferred: ask the device for the source rate), then hand the
               Session to the engine.

    Loading never depends on the device rate: if the device runs at another rate, the engine resamples its
    mixed output in realtime (ADR 0005). Construct, use and destroy on the message thread. */
class SessionLoader final
{
public:
    SessionLoader (juce::AudioDeviceManager&, PlaybackEngine&);
    ~SessionLoader();

    /** Starts loading a pair, replacing the current one (the engine is cleared at once to free memory). */
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
    struct SourceResult;

    void runDecodeJob (int generation, juce::File original, juce::File stemFile);
    void sourceReady (int generation, std::shared_ptr<SourceResult>);
    void fail (int generation, const juce::String& message);
    void setProgress (int generation, const juce::String& text);

    /** Rate policy on the message thread; returns the rate the device now runs at (0 if no device). */
    double applyRatePolicy (double sourceRate);
    void updateRateStatus();
    juce::String deviceIdentity() const;
    void changed();

    template <typename Fn>
    void postToMessageThread (Fn&& fn);

    juce::AudioDeviceManager& deviceManager;
    PlaybackEngine& engine;

    juce::ThreadPool pool { juce::ThreadPoolOptions{}.withThreadName ("SessionLoader").withNumberOfThreads (1) };
    std::atomic<int> latestGeneration { 0 };
    int generation = 0;

    std::shared_ptr<const Session> session;    // what the engine plays (at the source rate)
    juce::String lastDeviceIdentity;
    juce::String policyNote;                   // e.g. "Device refused 44100 Hz: ..."
    juce::WeakReference<SessionLoader> weakSelf;

    LoadStatus status;

    JUCE_DECLARE_WEAK_REFERENCEABLE (SessionLoader)
    JUCE_DECLARE_NON_COPYABLE (SessionLoader)
};

} // namespace stemab
