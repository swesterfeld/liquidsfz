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
  o0 += in (x + -18) * -8.4884013168683032e-05f;
  o1 += in (x + -18) * -7.4111178242977968e-06f;
  o0 += in (x + -17) * 0.00021671949237682831f;
  o1 += in (x + -17) * -3.7144734113549181e-05f;
  o0 += in (x + -16) * -0.00033096979379892614f;
  o1 += in (x + -16) * 0.00023236075990519745f;
  o0 += in (x + -15) * 0.00023742204517274539f;
  o1 += in (x + -15) * -0.00067213748243803587f;
  o0 += in (x + -14) * 0.00035257240687915f;
  o1 += in (x + -14) * 0.0013593123681780765f;
  o0 += in (x + -13) * -0.0017232615641109132f;
  o1 += in (x + -13) * -0.0020951822857745825f;
  o0 += in (x + -12) * 0.0039645958708182149f;
  o1 += in (x + -12) * 0.0024037512085301588f;
  o0 += in (x + -11) * -0.0067439400455638595f;
  o1 += in (x + -11) * -0.0015612528740619476f;
  o0 += in (x + -10) * 0.0091340460845673287f;
  o1 += in (x + -10) * -0.0012164375736234581f;
  o0 += in (x + -9) * -0.00960835357814766f;
  o1 += in (x + -9) * 0.0064291754036198039f;
  o0 += in (x + -8) * 0.0062824655104158598f;
  o1 += in (x + -8) * -0.013864262928591447f;
  o0 += in (x + -7) * 0.0026000237642372728f;
  o1 += in (x + -7) * 0.022218304364622569f;
  o0 += in (x + -6) * -0.018043991050867222f;
  o1 += in (x + -6) * -0.028912093328427431f;
  o0 += in (x + -5) * 0.039735223258602105f;
  o1 += in (x + -5) * 0.03016601153895232f;
  o0 += in (x + -4) * -0.065748952678743672f;
  o1 += in (x + -4) * -0.021208061342983666f;
  o0 += in (x + -3) * 0.092696860685640842f;
  o1 += in (x + -3) * -0.0039607466197627301f;
  o0 += in (x + -2) * -0.1163427781919362f;
  o1 += in (x + -2) * 0.055363348977428541f;
  o0 += in (x + -1) * 0.13255420748828156f;
  o1 += in (x + -1) * -0.16459782053545136f;
  o0 += in (x + 0) * 0.86169373619685397f;
  o1 += in (x + 0) * 0.61996641241273398f;
  o0 += in (x + 1) * 0.13255420748828156f;
  o1 += in (x + 1) * 0.61996641241273398f;
  o0 += in (x + 2) * -0.1163427781919362f;
  o1 += in (x + 2) * -0.16459782053545136f;
  o0 += in (x + 3) * 0.092696860685640842f;
  o1 += in (x + 3) * 0.055363348977428541f;
  o0 += in (x + 4) * -0.065748952678743672f;
  o1 += in (x + 4) * -0.0039607466197627301f;
  o0 += in (x + 5) * 0.039735223258602105f;
  o1 += in (x + 5) * -0.021208061342983666f;
  o0 += in (x + 6) * -0.018043991050867222f;
  o1 += in (x + 6) * 0.03016601153895232f;
  o0 += in (x + 7) * 0.0026000237642372728f;
  o1 += in (x + 7) * -0.028912093328427431f;
  o0 += in (x + 8) * 0.0062824655104158598f;
  o1 += in (x + 8) * 0.022218304364622569f;
  o0 += in (x + 9) * -0.00960835357814766f;
  o1 += in (x + 9) * -0.013864262928591447f;
  o0 += in (x + 10) * 0.0091340460845673287f;
  o1 += in (x + 10) * 0.0064291754036198039f;
  o0 += in (x + 11) * -0.0067439400455638595f;
  o1 += in (x + 11) * -0.0012164375736234581f;
  o0 += in (x + 12) * 0.0039645958708182149f;
  o1 += in (x + 12) * -0.0015612528740619476f;
  o0 += in (x + 13) * -0.0017232615641109132f;
  o1 += in (x + 13) * 0.0024037512085301588f;
  o0 += in (x + 14) * 0.00035257240687915f;
  o1 += in (x + 14) * -0.0020951822857745825f;
  o0 += in (x + 15) * 0.00023742204517274539f;
  o1 += in (x + 15) * 0.0013593123681780765f;
  o0 += in (x + 16) * -0.00033096979379892614f;
  o1 += in (x + 16) * -0.00067213748243803587f;
  o0 += in (x + 17) * 0.00021671949237682831f;
  o1 += in (x + 17) * 0.00023236075990519745f;
  o0 += in (x + 18) * -8.4884013168683032e-05f;
  o1 += in (x + 18) * -3.7144734113549181e-05f;
  o1 += in (x + 19) * -7.4111178242977968e-06f;
  out[0] = o0;
  out[1] = o1;
}

}

#endif /* LIQUIDSFZ_UPSAMPLE_HH */
