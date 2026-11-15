#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

#include "engine/Crossfade.h"
#include "engine/Session.h"

namespace stemab
{

class OutputResampler;

/** What the listener hears (CONTEXT.md): the A<->B Crossfade, or the Residual (A - all four Stems). */
enum class ListenMode { AB, Residual };

/** Meter taps. Every tap is stereo (channel 0 = L, 1 = R) and includes the transport fade, so a paused
    engine reads silence:
      Original - A, before the Crossfade;
      StemSum  - B after Mute/Solo, before the Crossfade;
      Output   - what is written to the device (after Crossfade, Master Gain and, when the device does not run
                 at the source rate, after the realtime resampler - ADR 0005);
      Drums..Vocals - each Stem after its Mute/Solo gain.
    All taps but Output are measured at the source rate, before the resampler. */
enum class MeterTap : int { Original = 0, StemSum, Output, Drums, Bass, Other, Vocals };
inline constexpr int numMeterTaps = 7;

struct MeterReading
{
    float peak = 0.0f;   // max |x| since the previous readMeter() of this tap/channel
    float rms  = 0.0f;   // RMS of the most recent audio block
};

//==============================================================================
/** The realtime player: transport, A/B/Residual mix, Mute/Solo, Master Gain, Clip and meters.

    Threads:
      - "audio thread": process() - allocation-, lock- and I/O-free. prepare() is called while no callback
        runs (it may allocate: it sizes the buffers and builds the output resampler).
      - "UI thread" (one thread, normally the message thread): every other member. Control setters only
        store atomics; the audio thread applies them at the start of its next block, ramped click-free.

    Listening path (ADR 0004): out = (gA*A + gB*B + gR*(A - S)) * MasterGain * transportFade, where
    B = sum of audible Stems (Mute/Solo) and S = sum of all four Stems. Nothing else: no limiter, no level
    matching; samples above 1.0 are written as they are and reported as Clip.

    Rates (ADR 0005): the mix (transport, fades, ramps, meters, playhead) always runs at the Session's own
    rate. At the Native Rate (device rate == Session rate) the mix goes straight to the device. Otherwise the
    one stereo mix goes through a realtime r8brain resampler (OutputResampler) to the device rate; Clip and
    the Output meter are measured after it, so inter-sample overs count. */
class PlaybackEngine final
{
public:
    /** Every gain change (Crossfade, A/B/R switch, Mute/Solo, Master Gain) is a linear ramp of this length. */
    static constexpr double gainRampSeconds = 0.010;
    /** Pause, seek, loop wrap, end of track and session swap fade out/in over this length (smoothstep).
        A fade-out ends on one zero-gain sample; the jump/pause/swap happens right after it. */
    static constexpr double transportFadeSeconds = 0.010;
    /** Master Gain at or below this many dB is treated as -inf (silence). */
    static constexpr double masterGainFloorDb = -100.0;

    PlaybackEngine();
    ~PlaybackEngine();

    //==============================================================================
    // Audio thread

    /** Call before the first process() and whenever the device (re)starts, while process() is not running.
        Builds the output resampler if the active session's rate differs from `deviceSampleRate` (allocates). */
    void prepare (double deviceSampleRate, int maximumBlockSize);

    /** Renders one device block. Channels 0/1 get L/R (a mono device gets (L+R)/2); channels beyond 2 are
        zeroed. With no session or while paused: silence. Null channel pointers are skipped. Blocks larger
        than prepare()'s maximumBlockSize are rendered in pieces. */
    void process (float* const* outputChannels, int numOutputChannels, int numSamples) noexcept;

    //==============================================================================
    // UI thread: session

    /** Hands a session to the audio thread (nullptr clears it). On the swap the playhead goes to 0 and the
        transport pauses; if audio is playing it fades out first. Sessions are only ever released on the UI
        thread (here, in releaseRetiredSessions() or in the destructor), never on the audio thread.
        If the session's rate differs from the prepared device rate, the output resampler it needs is built
        here and handed over with it. */
    void setSession (std::shared_ptr<const Session> session);

    /** Frees sessions and output resamplers the audio thread has finished with, and builds an output
        resampler the audio thread asked for (rare: a race between setSession() and a device restart).
        Call it regularly (e.g. from the UI timer). */
    void releaseRetiredSessions();

    //==============================================================================
    // UI thread: transport (there is no Stop - Pause holds the playhead)

    void play() noexcept;
    void pause() noexcept;
    void togglePlayPause() noexcept         { if (isPlaying()) pause(); else play(); }

    /** Fade out, jump to `sample` (clamped to the track), fade in. While paused it just moves the playhead. */
    void seek (juce::int64 sample) noexcept;

    void setLoopEnabled (bool shouldLoop) noexcept;
    /** [start, end) in samples. An empty or invalid region (start >= end, start < 0) means the whole track;
        `end` beyond the track is clamped to it. */
    void setLoopRegion (juce::int64 start, juce::int64 end) noexcept;

    //==============================================================================
    // UI thread: mix

    void setCrossfade (float x) noexcept;           // 0 = all A, 1 = all B (clamped)
    void setCurve (Curve curve) noexcept;
    void setListenMode (ListenMode mode) noexcept;  // Residual ignores the Crossfade and Mute/Solo
    void setStemMuted (int stemIndex, bool muted) noexcept;
    void setStemSoloed (int stemIndex, bool soloed) noexcept;
    /** One plain scalar for everything heard. Clamped to <= 0 dB; <= masterGainFloorDb (or -inf) is silence. */
    void setMasterGainDb (double gainDb) noexcept;

    float getCrossfade() const noexcept         { return crossfadeX.load (std::memory_order_relaxed); }
    Curve getCurve() const noexcept             { return (Curve) curve.load (std::memory_order_relaxed); }
    ListenMode getListenMode() const noexcept   { return (ListenMode) listenMode.load (std::memory_order_relaxed); }
    bool isStemMuted (int i) const noexcept;
    bool isStemSoloed (int i) const noexcept;
    double getMasterGainDb() const noexcept     { return masterGainDb.load (std::memory_order_relaxed); }

    /** The Mute/Solo rule (CONTEXT.md): any Stem soloed -> the soloed set; otherwise the un-muted set. */
    static std::array<bool, Session::numStems> audibleStems (const std::array<bool, Session::numStems>& muted,
                                                             const std::array<bool, Session::numStems>& soloed) noexcept;

    //==============================================================================
    // UI thread: reads (published by the audio thread once per block)

    /** Transport fade length in samples at the mix (source) rate (for tests and diagnostics). */
    int getTransportFadeSamples() const noexcept    { return fadeLenPublished.load (std::memory_order_relaxed); }

    /** In source-rate samples: the next sample the mixer will read. Not compensated for the output
        resampler's latency (a few ms), so pause/seek/loop positions stay exact. */
    juce::int64 getPlayheadSample() const noexcept  { return playhead.load (std::memory_order_relaxed); }
    /** True while the transport runs (not during or after a Pause fade-out). */
    bool isPlaying() const noexcept                 { return playingFlag.load (std::memory_order_relaxed); }
    bool isLoopEnabled() const noexcept             { return loopEnabled.load (std::memory_order_relaxed); }
    /** Of the session the audio thread is playing (0 if none yet). */
    juce::int64 getSessionLength() const noexcept   { return sessionLength.load (std::memory_order_relaxed); }
    double getSessionSampleRate() const noexcept    { return sessionRate.load (std::memory_order_relaxed); }
    /** The rate passed to the last prepare(). */
    double getDeviceSampleRate() const noexcept     { return deviceRatePublished.load (std::memory_order_relaxed); }
    /** True while the mix goes through the realtime output resampler (device rate != mix rate). */
    bool isResampling() const noexcept              { return resamplingFlag.load (std::memory_order_relaxed); }
    /** The output resampler's nominal extra latency in seconds (0 at the Native Rate). */
    double getResamplerLatencySeconds() const noexcept { return resamplerLatency.load (std::memory_order_relaxed); }

    /** Peak since the previous call for this tap/channel (read-and-reset) and the RMS of the last block. */
    MeterReading readMeter (MeterTap tap, int channel) noexcept;

    /** True if any sample sent to the device (after the resampler) had |x| > 1.0 since the previous call
        (read-and-clear). */
    bool readAndClearClip() noexcept                { return clipFlag.exchange (false, std::memory_order_relaxed); }

private:
    //==============================================================================
    // Audio-thread helpers
    void takeCommands() noexcept;
    void updateGainTargets() noexcept;
    void beginFadeOut() noexcept;
    void resolveFadeOut() noexcept;
    void swapInIncomingSession() noexcept;
    void retire (const Session*) noexcept;
    void publish (int numSamples) noexcept;
    juce::int64 effectiveLoopStart() const noexcept;
    juce::int64 effectiveEnd() const noexcept;

    void setMixRate (double rate) noexcept;
    void updateOutputPath() noexcept;
    void resumeMixer() noexcept;
    void retireResampler (OutputResampler*) noexcept;
    void requestResampler() noexcept;
    static void pullSourceThunk (void* engine, float* left, float* right, int numSamples);
    void pullSource (float* left, float* right, int numSamples) noexcept;
    bool renderMix (float* left, float* right, int numSamples) noexcept;
    void writeOutput (float* outL, float* outR, bool monoOut, int offset, int numSamples) noexcept;

    // UI thread: hands a resampler built here to the audio thread.
    void offerResampler (std::unique_ptr<OutputResampler>);

    //==============================================================================
    // UI -> audio (atomics)
    std::atomic<const Session*> pendingSession { nullptr };
    std::atomic<int> transportCommand { 0 };        // 0 none, 1 play, 2 pause (last one wins)
    std::atomic<juce::int64> seekRequest { -1 };
    std::atomic<bool> loopEnabled { false };
    std::atomic<juce::uint32> loopRegionVersion { 0 };   // seqlock: odd while the UI writes
    std::atomic<juce::int64> loopStartRequest { 0 }, loopEndRequest { 0 };

    std::atomic<float> crossfadeX { 0.0f };
    std::atomic<int> curve { (int) Curve::EqualPower };
    std::atomic<int> listenMode { (int) ListenMode::AB };
    std::array<std::atomic<bool>, Session::numStems> muted {}, soloed {};
    std::atomic<double> masterGainDb { 0.0 };
    std::atomic<float> masterGainLinear { 1.0f };

    // UI -> audio: an output resampler built off the audio thread. Whoever exchanges a pointer out owns it.
    std::atomic<OutputResampler*> pendingResampler { nullptr };

    // audio -> UI (atomics)
    std::atomic<juce::int64> playhead { 0 };
    std::atomic<bool> playingFlag { false };
    std::atomic<juce::int64> sessionLength { 0 };
    std::atomic<double> sessionRate { 0.0 };
    std::atomic<double> deviceRatePublished { 44100.0 };
    std::atomic<int> maxBlockPublished { 512 };
    std::atomic<double> mixRatePublished { 44100.0 };
    std::atomic<bool> resamplingFlag { false };
    std::atomic<double> resamplerLatency { 0.0 };
    std::atomic<bool> clipFlag { false };
    std::atomic<int> fadeLenPublished { 441 };
    std::array<std::array<std::atomic<float>, 2>, numMeterTaps> meterPeak {}, meterRms {};

    // Retired sessions: audio thread pushes, UI thread pops (single producer / single consumer).
    static constexpr int retiredCapacity = 16;
    juce::AbstractFifo retiredFifo { retiredCapacity };
    std::array<const Session*, retiredCapacity> retiredSlots {};

    // Retired output resamplers: same pattern (audio pushes, UI frees).
    static constexpr int retiredResamplerCapacity = 8;
    juce::AbstractFifo retiredResamplerFifo { retiredResamplerCapacity };
    std::array<OutputResampler*, retiredResamplerCapacity> retiredResamplerSlots {};

    // Audio -> UI: "please build a resampler mixRate -> deviceRate" (the serial bumps on each request).
    std::atomic<double> requestSourceRate { 0.0 }, requestDeviceRate { 0.0 };
    std::atomic<int> requestMaxBlock { 0 };
    std::atomic<juce::uint32> requestSerial { 0 };
    juce::uint32 servedRequestSerial = 0;   // UI thread only

    // UI-thread only: owners of every session the audio thread may still touch.
    std::vector<std::shared_ptr<const Session>> keepAlive;

    //==============================================================================
    // Audio-thread state
    double deviceRate = 44100.0;
    double mixRate = 44100.0;               // the active session's rate (kept while there is none)
    int maxBlock = 512;
    const Session* current = nullptr;
    const Session* incoming = nullptr;      // taken from pendingSession, waiting for a fade-out
    bool swapPending = false;

    juce::int64 pos = 0;                    // playhead
    bool running = false;                   // transport advancing
    int fadePos = 0;                        // transport fade position 0..fadeLen (gain = smoothstep(fadePos/fadeLen))
    int fadeLen = 441;
    int fadeDir = 0;                        // -1 fading out, +1 fading in, 0 steady
    juce::int64 pendingJump = -1;           // applied when a fade-out reaches 0
    bool pendingPause = false;              // applied when a fade-out reaches 0

    bool loopOn = false;
    juce::int64 loopStart = 0, loopEnd = 0; // raw request; see effectiveLoopStart()/effectiveEnd()

    using Smoothed = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>;
    Smoothed gainA, gainB, gainR, gainMaster;
    std::array<Smoothed, Session::numStems> gainStem;

    // per-block meter accumulators
    std::array<std::array<float, 2>, numMeterTaps> blockPeak {};
    std::array<std::array<double, 2>, numMeterTaps> blockSumSq {};
    bool blockClipped = false;
    int sourceSamplesThisBlock = 0;

    // Output path. resampler == nullptr: Native Rate, the mix goes straight to the device.
    OutputResampler* resampler = nullptr;
    OutputResampler* standby = nullptr;     // taken from pendingResampler, not needed yet (e.g. swap pending)
    bool holdMixer = false;                 // switching the output path: feed silence, the transport waits
    bool requestOpen = false;               // a request is published and not yet answered
    juce::int64 silentRun = 0;              // consecutive silent source samples fed to the output path
    std::vector<float> mixL, mixR;          // maxBlock: the mix (passthrough) or the resampled output

    JUCE_DECLARE_NON_COPYABLE (PlaybackEngine)
};

} // namespace stemab
