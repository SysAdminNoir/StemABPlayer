# Nothing in the listening path may treat A and B differently

The tool exists to judge whether the Stem Sum reconstructs the Original. Any processing that responds to the
signal (a soft limiter, loudness level-matching, an auto-trim) changes A and B differently and fakes or hides
reconstruction errors. So the earlier plan's master soft limiter and "level-matched A/B" are dropped. Master
Gain is one plain scalar applied identically to everything heard. Going over 0 dBFS is shown as Clip and fixed
by lowering Master Gain. Stems are summed at their original amplitude.

For the same reason we prefer the **Native Rate**: the output device is asked for the source rate first, and
only if it refuses are A and B both resampled (r8brain-free-src, identically) to the device rate.
