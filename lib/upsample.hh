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

template<int CHANNELS>
void
upsample (const float *in, float *out)
{
  static_assert (CHANNELS == 1 || CHANNELS == 2);

  float l0 = 0, l1 = 0, r0 = 0, r1 = 0;

  // ---- generated code by gen-upsample.py ----
  l0 = in[0];
  if (CHANNELS == 2)
    r0 = in[1];

  const float c1 = 0.63237116454128905f;
  l1 += (in[0 * CHANNELS] + in[1 * CHANNELS]) * c1;
  if (CHANNELS == 2)
    r1 += (in[0 * CHANNELS + 1] + in[1 * CHANNELS + 1]) * c1;

  const float c2 = -0.1997498002401274f;
  l1 += (in[-1 * CHANNELS] + in[2 * CHANNELS]) * c2;
  if (CHANNELS == 2)
    r1 += (in[-1 * CHANNELS + 1] + in[2 * CHANNELS + 1]) * c2;

  const float c3 = 0.10748860423425083f;
  l1 += (in[-2 * CHANNELS] + in[3 * CHANNELS]) * c3;
  if (CHANNELS == 2)
    r1 += (in[-2 * CHANNELS + 1] + in[3 * CHANNELS + 1]) * c3;

  const float c4 = -0.064996197861812793f;
  l1 += (in[-3 * CHANNELS] + in[4 * CHANNELS]) * c4;
  if (CHANNELS == 2)
    r1 += (in[-3 * CHANNELS + 1] + in[4 * CHANNELS + 1]) * c4;

  const float c5 = 0.040215547574385509f;
  l1 += (in[-4 * CHANNELS] + in[5 * CHANNELS]) * c5;
  if (CHANNELS == 2)
    r1 += (in[-4 * CHANNELS + 1] + in[5 * CHANNELS + 1]) * c5;

  const float c6 = -0.024427947042245154f;
  l1 += (in[-5 * CHANNELS] + in[6 * CHANNELS]) * c6;
  if (CHANNELS == 2)
    r1 += (in[-5 * CHANNELS + 1] + in[6 * CHANNELS + 1]) * c6;

  const float c7 = 0.014168419143340378f;
  l1 += (in[-6 * CHANNELS] + in[7 * CHANNELS]) * c7;
  if (CHANNELS == 2)
    r1 += (in[-6 * CHANNELS + 1] + in[7 * CHANNELS + 1]) * c7;

  const float c8 = -0.00763898924164643f;
  l1 += (in[-7 * CHANNELS] + in[8 * CHANNELS]) * c8;
  if (CHANNELS == 2)
    r1 += (in[-7 * CHANNELS + 1] + in[8 * CHANNELS + 1]) * c8;

  const float c9 = 0.0036936200627533675f;
  l1 += (in[-8 * CHANNELS] + in[9 * CHANNELS]) * c9;
  if (CHANNELS == 2)
    r1 += (in[-8 * CHANNELS + 1] + in[9 * CHANNELS + 1]) * c9;

  const float c10 = -0.0015023373417955108f;
  l1 += (in[-9 * CHANNELS] + in[10 * CHANNELS]) * c10;
  if (CHANNELS == 2)
    r1 += (in[-9 * CHANNELS + 1] + in[10 * CHANNELS + 1]) * c10;

  const float c11 = 0.00043564746173319177f;
  l1 += (in[-10 * CHANNELS] + in[11 * CHANNELS]) * c11;
  if (CHANNELS == 2)
    r1 += (in[-10 * CHANNELS + 1] + in[11 * CHANNELS + 1]) * c11;

  if (CHANNELS == 1)
    {
      out[0] = l0;
      out[1] = l1;
    }
  else
    {
      out[0] = l0;
      out[1] = r0;
      out[2] = l1;
      out[3] = r1;
    }
}

}

#endif /* LIQUIDSFZ_UPSAMPLE_HH */
