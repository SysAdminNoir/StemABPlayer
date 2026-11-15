# Resample the mixed output in realtime, not the sources on load

Checkpoint 4 resampled A and all four Stems offline when the device rate differed from the source rate. That
made loading slow, doubled memory, and meant re-processing on every device change. Instead, the engine always
mixes at the **source rate** (A/B Crossfade, Residual, Mute/Solo, Master Gain, meters). The single stereo
output then goes through **one** realtime r8brain resampler to the device rate, only when the device refuses
the Native Rate. Because everything heard passes through the same resampler, A and B are still treated
identically (ADR 0004), at the cost of one stereo stream's CPU.

## Consequences
- Load = decode + alignment only. A device or rate change only re-prepares the output resampler.
- The resampler adds a small fixed output latency to control changes (fades are applied before it, at the
  source rate). Clip is detected after resampling, so inter-sample overs count.
- Per-source meters are measured at the source rate, before the resampler.
