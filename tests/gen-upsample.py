#!/usr/bin/env python3
from scipy import signal
from scipy.special import sinc

def dump_plot (coeffs):
  w, h = signal.freqz (coeffs, worN=2**14)

  for p in range (len (h)):
    print ((2 * 22050.0 * p) / len (h), abs (h[p]))

def halfband_fir():
    coeffs = signal.windows.kaiser (45, 7, sym=True)
    width = len (coeffs)
    center = width // 2

    print ("  o0 = in (x);");
    for i in range (-width, width):
        c = center + i
        if (c >= 0 and c < width):
            coeffs[c] *= sinc (i * 0.5) * 0.5
            if (c & 1):
                print ("  o1 += in (x + %d) * %.17gf;" % ((i + 1) / 2, coeffs[c] * 2))

def fir():
    coeffs = signal.firwin (75, 19000 / 44100, window=('kaiser', 8))
    width = len (coeffs)
    center = width // 2
    for i in range (-width, width):
        c = center + i * 2
        if (c >= 0 and c < width):
            print ("  o0 += in (x + %d) * %.17gf;" % (i, coeffs[c] * 2))
        c -= 1
        if (c >= 0 and c < width):
            print ("  o1 += in (x + %d) * %.17gf;" % (i, coeffs[c] * 2))

halfband_fir()
