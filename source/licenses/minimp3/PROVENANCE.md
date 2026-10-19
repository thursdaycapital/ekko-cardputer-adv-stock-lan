# minimp3

Unmodified `minimp3.h` and `LICENSE` from the upstream library:
https://github.com/lieff/minimp3

Pinned commit: `ea99364f61c14656440e8d77e9c233ccf3124633`.
License: CC0-1.0 (see LICENSE and the header).

Only the MP3 decoder is compiled, without platform SIMD. Its local decoder
scratch requires a dedicated ESP32 voice task; do not run it on a 12 KiB loop
stack. The adapter checks free stack before its first decode and binds that
decoder instance to the same task for later frames.
