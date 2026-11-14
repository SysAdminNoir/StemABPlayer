# Align A and B by cross-correlation, not a codec-delay constant

The Stem Files carry no MP4 edit list, and each Stem track is 6,688–7,168 samples longer than its FLAC
Original: AAC encoder delay plus padding, of an amount nothing in the file records. A fixed "skip 2112
priming samples" constant is fragile across encoders. On load we cross-correlate A against the Stem Sum (first
~30 s, mono, whole-sample precision), detect inverted polarity, and apply one Alignment Offset to all four
Stems (they share a length). The offset and a confidence value are shown, with a warning when confidence is
low and a manual ±sample nudge. A wrong offset is immediately obvious as a loud Residual.
