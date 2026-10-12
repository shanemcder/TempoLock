# TempoLock v1.3 (experimental)

Based on your uploaded, working TempoLock v1.2 source. This is a separate source-only project: JUCE and build products are intentionally excluded.

## Changes
- **Compare Reference** button: import a TempoLock output as the main audio, then choose your manually corrected reference. Reports duration difference, median alignment offset, and range of offsets across matched 6-second windows. This is an energy-envelope diagnostic, **not** a true beat-grid error metric.
- Keeps v1.2 spectral-flux analysis, beat anchors, and Rubber Band time-map export. The existing exporter already preserves the post-anchor tail duration; no untested changes to that mapping were made.
- Guards against invalid export duration.

## Build
Run `chmod +x setup.sh && ./setup.sh` in this folder. JUCE 8.0.10 will be cloned automatically. On macOS use `open build/TempoLock_artefacts/Debug/TempoLock.app`.

## Comparison
Import the TempoLock-exported WAV using **Import Audio**. Then click **Compare Reference** and select the manually corrected 106 BPM WAV. The status bar displays the measurements. A small offset range is encouraging but does not prove beats are on-grid.

**Note:** This version was prepared from source and has not been compiled or tested on your Mac. Keep v1.2 intact.
