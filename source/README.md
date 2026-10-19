# Cardputer ADV stock Studio LAN client

Current documentation: [README.zh-CN.md](README.zh-CN.md).

This firmware uses unmodified official Ekko Studio 0.7.36 over LAN. Legacy MCU chat extensions, automatic listening and remote relay are not part of this build. See the delivery validation records for hardware acceptance status.

Audio regressions use `tests/run_official_voice_tests.py` (local synthetic audio; ffmpeg required). The shared PCM queue, microphone capture and recording-store tests remain applicable. Legacy MCU ADPCM-contract and automatic-listening tests are outside this candidate.

After the pinned PlatformIO dependencies are installed, run `python3 -B tests/run_http_transport_tests.py` for the actual Arduino selection/history HTTP transports with host SDK doubles. It verifies one protocol-selected client, borrowed-client lifetime, status propagation, cancellation and deadlines without network access. The complete build/test commands are in the Chinese README.
