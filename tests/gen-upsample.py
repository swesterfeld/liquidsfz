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

    print ("  l0 = in[0];");
    print ("  if (CHANNELS == 2)");
    print ("    r0 = in[1];");
    print ("");
    for i in range (0, width):
        c = center + i
        if (c >= 0 and c < width):
            coeffs[c] *= sinc (i * 0.5) * 0.5
            if (c & 1):
                a = i / 2 + 1
                b = -i / 2
                print ("  const float c%d = %.17gf;" % (a, coeffs[c] * 2))
                print ("  l1 += (in[%d * CHANNELS] + in[%d * CHANNELS]) * c%d;" % (b, a, a))
                print ("  if (CHANNELS == 2)");
                print ("    r1 += (in[%d * CHANNELS + 1] + in[%d * CHANNELS + 1]) * c%d;" % (b, a, a))
                print ("");

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

print ("  // ---- generated code by gen-upsample.py ----");
halfband_fir()
