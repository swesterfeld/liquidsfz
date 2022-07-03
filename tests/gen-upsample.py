#!/usr/bin/env python3
from scipy import signal
#for f in signal.firwin (75, 19000 / 44100, window=('kaiser', 8)): print ("%.17g," % (f * 2))
coeffs = signal.firwin (75, 19000 / 44100, window=('kaiser', 8))

def dump_plot (coefffs):
  w, h = signal.freqz (coeffs, worN=2**14)

  for p in range (len (h)):
    print ((2 * 22050.0 * p) / len (h), abs (h[p]))

width = len (coeffs)
center = width // 2
for i in range (-width, width):
    c = center + i * 2
    if (c >= 0 and c < width):
        print ("  o0 += in (x + %d) * %.17gf;" % (i, coeffs[c] * 2))
    c -= 1
    if (c >= 0 and c < width):
        print ("  o1 += in (x + %d) * %.17gf;" % (i, coeffs[c] * 2))
