/*
 * liquidsfz - sfz sampler
 *
 * Copyright (C) 2022  Stefan Westerfeld
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2.1 of the License, or (at your
 * option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef LIQUIDSFZ_UPSAMPLE_HH
#define LIQUIDSFZ_UPSAMPLE_HH

namespace LiquidSFZInternal
{

template<class In>
void
upsample (const In& in, float *out, int x)
{
  float o0 = 0, o1 = 0;

  o0 = in (x);
  o1 += in (x + -10) * 0.00043564746173319177f;
  o1 += in (x + -9) * -0.0015023373417955108f;
  o1 += in (x + -8) * 0.0036936200627533675f;
  o1 += in (x + -7) * -0.00763898924164643f;
  o1 += in (x + -6) * 0.014168419143340378f;
  o1 += in (x + -5) * -0.024427947042245154f;
  o1 += in (x + -4) * 0.040215547574385509f;
  o1 += in (x + -3) * -0.064996197861812793f;
  o1 += in (x + -2) * 0.10748860423425083f;
  o1 += in (x + -1) * -0.1997498002401274f;
  o1 += in (x + 0) * 0.63237116454128905f;
  o1 += in (x + 1) * 0.63237116454128905f;
  o1 += in (x + 2) * -0.1997498002401274f;
  o1 += in (x + 3) * 0.10748860423425083f;
  o1 += in (x + 4) * -0.064996197861812793f;
  o1 += in (x + 5) * 0.040215547574385509f;
  o1 += in (x + 6) * -0.024427947042245154f;
  o1 += in (x + 7) * 0.014168419143340378f;
  o1 += in (x + 8) * -0.00763898924164643f;
  o1 += in (x + 9) * 0.0036936200627533675f;
  o1 += in (x + 10) * -0.0015023373417955108f;
  o1 += in (x + 11) * 0.00043564746173319177f;

  out[0] = o0;
  out[1] = o1;
}

}

#endif /* LIQUIDSFZ_UPSAMPLE_HH */
