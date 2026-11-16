#include "load/DecodeStash.h"

#include <chrono>

namespace stemab
{

template <typename Decoded>
DecodeStash::Ticket DecodeStash::begin (Slot<Decoded>& slot, const juce::File& file)
{
    auto id = FileIdentity::of (file);      // (file system access outside the lock)
    std::optional<Decoded> dropped;         // a big buffer is freed outside the lock too

    Ticket ticket = 0;
    {
        const std::lock_guard lock (mutex);
        dropped = std::move (slot.value);
        slot.value.reset();
        slot.id = std::move (id);
        slot.ticket = ticket = ++nextTicket;
        slot.pending = true;
    }
    changed.notify_all();
    return ticket;
}

template <typename Decoded>
void DecodeStash::put (Slot<Decoded>& slot, Ticket ticket, Decoded&& decoded)
{
    {
        const std::lock_guard lock (mutex);
        if (slot.ticket != ticket || ! slot.pending)
            return;     // a newer preview (or a clear()) took the slot: `decoded` stays with the caller

        if (decoded.ok)
        {
            slot.value = std::move (decoded);
            slot.pending = false;
        }
        else
        {
            slot = {};
        }
    }
    changed.notify_all();
}

template <typename Decoded>
void DecodeStash::abandon (Slot<Decoded>& slot, Ticket ticket)
{
    std::optional<Decoded> dropped;
    {
        const std::lock_guard lock (mutex);
        if (slot.ticket != ticket)
            return;
        dropped = std::move (slot.value);
        slot = {};
    }
    changed.notify_all();
}

template <typename Decoded>
std::optional<Decoded> DecodeStash::take (Slot<Decoded>& slot, const juce::File& file, const CancelCheck& cancel)
{
    const auto id = FileIdentity::of (file);
    if (! id.isValid())
        return std::nullopt;

    std::unique_lock lock (mutex);
    for (;;)
    {
        if (slot.ticket == 0 || slot.id != id)
            return std::nullopt;

        if (slot.value.has_value())
        {
            auto result = std::move (slot.value);
            slot = {};
            return result;
        }

        if (! slot.pending || isCancelled (cancel))
            return std::nullopt;

        changed.wait_for (lock, std::chrono::milliseconds (10));   // and poll `cancel`
    }
}

//==============================================================================
DecodeStash::Ticket DecodeStash::beginOriginal (const juce::File& f)        { return begin (original, f); }
void DecodeStash::putOriginal (Ticket t, DecodedOriginal&& d)               { put (original, t, std::move (d)); }
void DecodeStash::abandonOriginal (Ticket t)                                { abandon (original, t); }

std::optional<DecodedOriginal> DecodeStash::takeOriginal (const juce::File& f, const CancelCheck& cancel)
{
    return take (original, f, cancel);
}

DecodeStash::Ticket DecodeStash::beginStems (const juce::File& f)           { return begin (stems, f); }
void DecodeStash::putStems (Ticket t, DecodedStems&& d)                     { put (stems, t, std::move (d)); }
void DecodeStash::abandonStems (Ticket t)                                   { abandon (stems, t); }

std::optional<DecodedStems> DecodeStash::takeStems (const juce::File& f, const CancelCheck& cancel)
{
    return take (stems, f, cancel);
}

void DecodeStash::clear()
{
    std::optional<DecodedOriginal> droppedOriginal;
    std::optional<DecodedStems> droppedStems;
    {
        const std::lock_guard lock (mutex);
        droppedOriginal = std::move (original.value);
        droppedStems = std::move (stems.value);
        original = {};
        stems = {};
    }
    changed.notify_all();
}

bool DecodeStash::isEmpty() const
{
    const std::lock_guard lock (mutex);
    return original.ticket == 0 && stems.ticket == 0;
}

PairLoader::Backend DecodeStash::wrap (std::shared_ptr<DecodeStash> stash, PairLoader::Backend inner)
{
    const auto defaults = PairLoader::defaultBackend();
    auto decodeA = inner.decodeOriginal ? inner.decodeOriginal : defaults.decodeOriginal;
    auto decodeB = inner.decodeStems ? inner.decodeStems : defaults.decodeStems;

    PairLoader::Backend b = std::move (inner);
    b.decodeOriginal = [stash, decodeA] (const juce::File& f, const DecodeOptions& o) -> DecodedOriginal
    {
        if (auto d = stash->takeOriginal (f, o.cancel))
            return std::move (*d);
        return decodeA (f, o);
    };
    b.decodeStems = [stash, decodeB] (const juce::File& f, const DecodeOptions& o) -> DecodedStems
    {
        if (auto d = stash->takeStems (f, o.cancel))
            return std::move (*d);
        return decodeB (f, o);
    };
    return b;
}

} // namespace stemab
