#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>

#include <juce_core/juce_core.h>

#include "decode/Decode.h"
#include "load/AlignmentCache.h"
#include "decode/StemSet.h"
#include "load/PairLoader.h"

namespace stemab
{

/** Hands audio decoded for a waveform preview over to the pair load, so that a file the user previewed
    (chosen before the other file of the pair) is not decoded a second time. One slot for an Original and one
    for the B side - a Stem File or a Stem Set - each keyed by FileIdentity (path, size, modification time; for
    a Stem Set the identity of its ordered files, FileIdentity::of (const StemSource&)):

      - begin...(): a preview of that file has started. The slot now waits for it (whatever it held is
        dropped); the returned ticket names this preview.
      - put...(): the preview's decoded result. Kept only while the slot still waits for that ticket.
      - abandon...(): the preview failed or was cancelled; the slot empties if it still belongs to the ticket.
      - take...(): the pair load's decoder asks for a file. While that very file's preview is still decoding
        it waits (polling `cancel`); then it moves the result out (the slot empties). Returns nothing - and
        the caller decodes the file itself - for any other file, an abandoned preview or a cancel.
      - clear(): drops both slots and wakes any waiting take().

    Thread-safe. Hold it in a shared_ptr: wrap() captures one in the decoders it returns. */
class DecodeStash final
{
public:
    using Ticket = juce::uint64;

    Ticket beginOriginal (const juce::File&);
    void putOriginal (Ticket, DecodedOriginal&&);
    void abandonOriginal (Ticket);
    std::optional<DecodedOriginal> takeOriginal (const juce::File&, const CancelCheck& cancel = {});

    /** A Stem File (a juce::File converts) or a Stem Set. */
    Ticket beginStems (const StemSource&);
    void putStems (Ticket, DecodedStems&&);
    void abandonStems (Ticket);
    std::optional<DecodedStems> takeStems (const StemSource&, const CancelCheck& cancel = {});

    void clear();

    /** True if neither slot holds or waits for anything. */
    bool isEmpty() const;

    /** `inner`'s decoders, taking from the stash first (empty decoders in `inner` mean the FFmpeg ones). */
    static PairLoader::Backend wrap (std::shared_ptr<DecodeStash>, PairLoader::Backend inner = PairLoader::defaultBackend());

private:
    template <typename Decoded>
    struct Slot
    {
        FileIdentity id;
        Ticket ticket = 0;
        bool pending = false;
        std::optional<Decoded> value;
    };

    template <typename Decoded> Ticket begin (Slot<Decoded>&, FileIdentity);
    template <typename Decoded> void put (Slot<Decoded>&, Ticket, Decoded&&);
    template <typename Decoded> void abandon (Slot<Decoded>&, Ticket);
    template <typename Decoded> std::optional<Decoded> take (Slot<Decoded>&, const FileIdentity&, const CancelCheck&);

    mutable std::mutex mutex;
    std::condition_variable changed;
    Ticket nextTicket = 0;
    Slot<DecodedOriginal> original;
    Slot<DecodedStems> stems;
};

} // namespace stemab
