# Stem A/B Player — Domain Glossary

A critical-listening tool for judging stem-separation quality: does the sum of the separated stems
reconstruct the original track?

## Terms

**Original (A)**
The unseparated source track, loaded from its own file (e.g. a FLAC in `Audio Files/`). The ground-truth
reference every comparison is made against.

**Stem File**
A single NI/Traktor `.stem.m4a` file (e.g. in `Stem Files/`). Holds an Embedded Master plus exactly four
Stems. It is what the player reads to get B.

**Embedded Master**
The first audio track inside a Stem File: NI's own re-encoded full mix. *Not* the Original and *not* used
as A.

**Stem**
One separated component inside a Stem File. There are always exactly four, in this order:
**Drums, Bass, Other, Vocals**.

**Stem Sum (B)**
The four Stems added together at their original amplitude: no level-matching, normalisation or trim. If the
separation is perfect, B equals A.

**Reconstruction**
The property being judged: how closely B matches A. "Perfect reconstruction" means B is sonically
indistinguishable from A.

**Alignment Offset**
The number of samples B must be shifted to line up with A on one shared timeline. Codec delay/padding makes
it non-zero (several thousand samples for the test files). Measured on load; can be nudged by hand.

**Crossfade**
The single control that blends from A (all A) to B (all B) for listening comparison.

**Residual (A − B)**
A minus the sum of **all four** Stems, on the shared timeline. Mute/Solo never changes it. Silence means
perfect Reconstruction. A third listening position alongside A and B.

**Mute / Solo**
Shape which Stems are heard in B. If any Stem is soloed, the audible set is the soloed Stems; otherwise it is
the un-muted Stems. Listening only: never affects the Residual or Null Depth.

**Loop Region**
A span of the timeline that playback repeats while Loop is on. With no region set, the whole track loops.

**Pause**
The only way to halt playback: audio fades out and the playhead stays where it is. There is no separate Stop.

**Spectrogram Size**
The STFT length used for the spectrogram view: a power of two from 4096 (default) to 65536. One shared
colour scale across all lanes so they can be compared.

**Null Depth**
How far the Residual sits below A, in dB. Higher is better. The numeric answer to "how accurate is the
Reconstruction?" Reported two ways: **Track Null Depth** (whole track, computed on load) and **Live Null
Depth** (short window around the playhead).

**Master Gain**
One plain scalar applied identically to everything heard. Nothing in the listening path may treat A and B
differently (no limiter, no level-matching), or the comparison is invalid.

**Clip**
The output exceeding 0 dBFS. Indicated, never hidden; the remedy is lowering Master Gain.

**Native Rate / Resampled**
Playback runs at the source's own sample rate when the output device accepts it (Native Rate). Only when it
refuses is the mixed output Resampled to the device rate on the fly. Everything heard goes through the
same resampler, so A and B are still treated identically.
