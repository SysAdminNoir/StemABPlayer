# Align A and B by cross-correlation, not a codec-delay constant

The Stem Files carry no MP4 edit list, and each Stem track is 6,688–7,168 samples longer than its FLAC
Original: AAC encoder delay plus padding, of an amount nothing in the file records. A fixed "skip 2112
priming samples" constant is fragile across encoders. On load we cross-correlate A against the Stem Sum (mono, whole-sample
precision) in three ~24 s windows spread across the track (20/50/80 %), which must agree, detect inverted polarity, and apply one Alignment Offset to all four
Stems (they share a length). The offset and a confidence value are shown, with a warning when confidence is
low and a manual ±sample nudge. A wrong offset is immediately obvious as a loud Residual.

Measured on the three test pairs (checkpoint 3): the offset is **+6,522 samples** for every pair
(2,112 AAC priming + exactly 100 ms of extra lead-in), polarity +1, every window agreeing to the sample, and
ρ ≥ 0.9998. A parabolic sub-sample estimate lands on exactly +6522.000, so whole-sample precision loses nothing.
