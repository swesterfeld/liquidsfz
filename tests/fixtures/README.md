# Audio fixtures

`stereo.flac` is an eight-frame, 48 kHz, stereo PCM16 signal created for
the FLAC decoding regression test. The interleaved samples are specified in
`testflac.cc`. No encoder is needed to run the test.

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
