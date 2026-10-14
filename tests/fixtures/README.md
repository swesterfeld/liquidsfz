# Audio fixtures

`stereo.flac` is an eight-frame, 48 kHz, stereo PCM16 signal created for
the FLAC decoding regression test. The interleaved samples are specified in
`testflac.cc`. No encoder is needed to run the test.

`flac.sfz` loops this signal for the standalone Linux LV2 output smoke test.
After `make check` builds the test host, run it with an explicit plugin path:

```sh
tests/testlv2output /path/to/liquidsfz.lv2/liquidsfz_lv2.so tests/fixtures/flac.sfz
```

The static Docker build runs this against the installed plugin. The host uses
only the public LV2 API and does not link liquidsfz or libsndfile. It requires
silence before note-on and finite, nonzero audio on both channels afterwards.

To regenerate it from this directory (Python 3 and the `flac` CLI required):

```sh
python3 - <<'PY'
import struct
import subprocess

pcm = [0, 32767, -32768, 0, 8192, -16384, 24576, -8192,
       1, -1, 12345, -23456, -16384, 16384, 0, 0]
subprocess.run([
    'flac', '--force', '--silent', '--force-raw-format', '--endian=little',
    '--sign=signed', '--channels=2', '--bps=16', '--sample-rate=48000',
    '--no-padding', '--no-seektable', '-o', 'stereo.flac', '-'
], input=struct.pack('<16h', *pcm), check=True)
PY
```
