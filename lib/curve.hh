/*
 * liquidsfz - sfz sampler
 *
 * Copyright (C) 2019-2020  Stefan Westerfeld
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

#ifndef LIQUIDSFZ_CURVE_HH
#define LIQUIDSFZ_CURVE_HH

#include <vector>
#include <map>

namespace LiquidSFZInternal
{

struct Curve
{
  std::vector<std::pair<int, float>> points;
  std::vector<float> *table = nullptr;

  float
  get (int pos) const
  {
    if (pos < 0)
      return 0;
    if (pos > 127)
      return 1;
    return (*table)[pos];
  }
  void
  set (int pos, float value)
  {
    points.push_back (std::make_pair (pos, value));
  }
  bool
  empty() const
  {
    return points.empty();
  }
};

class CurveTable
{
  std::map<std::vector<std::pair<int, float>>, std::vector<float>> curve_map;
  const float *
  find_point (Curve& curve, int pos)
  {
    const float *result = nullptr;
    for (auto& v : curve.points)
      if (v.first == pos)
        {
          /* allow overwrite - if value is there more than once, return last match */
          result = &v.second;
        }

    return result;
  }
public:
  void
  expand_curve (Curve& curve)
  {
    if (curve.points.empty())
      return;

    auto& table = curve_map[curve.points];
    if (table.empty())
      {
        /* ensure that first and last point is defined (use defaults if not) */
        if (!find_point (curve, 0))
          curve.points.push_back ({0, 0});

        if (!find_point (curve, 127))
          curve.points.push_back ({127, 1});

        int last_pos = 0;
        table.resize (128);
        for (int pos = 0; pos < 128; pos++)
          {
            auto value_ptr = find_point (curve, pos);
            if (value_ptr)
              {
                table[pos] = *value_ptr;

                /* linear interpolation if at least one point is missing */
                for (int x = last_pos + 1; x < pos; x++)
                  {
                    const double alpha = (x - last_pos) / double (pos - last_pos);
                    table[x] = (1 - alpha) * table[last_pos] + alpha * table[pos];
                  }
                last_pos = pos;
              }
          }
      }
    curve.table = &table;
  }
};

}

#endif /* LIQUIDSFZ_CURVE_HH */
