#pragma once

#include <functional>

namespace stemab
{

/** Cooperative cancellation for long offline work (decoding, alignment). The work loops poll it; when it
    returns true the work stops early and reports failure ("Cancelled."). It may be polled from several worker
    threads at once, so it must be thread-safe (typically it compares an atomic load generation). An empty
    CancelCheck never cancels. Header-only, so Decode and Analysis can share it without depending on each other. */
using CancelCheck = std::function<bool()>;

inline bool isCancelled (const CancelCheck& cancel)    { return cancel && cancel(); }

} // namespace stemab
