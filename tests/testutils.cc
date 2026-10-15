// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

#include "utils.hh"

#include <cassert>
#include <random>

using LiquidSFZInternal::sat_round;

template<typename T>
void
test_sat_round()
{
  using Limits = std::numeric_limits<T>;
  const double inf = std::numeric_limits<double>::infinity();
  assert (sat_round<T> (0) == 0);
  assert (sat_round<T> (-0.0) == 0);
  assert (sat_round<T> (0.49) == 0);
  assert (sat_round<T> (0.5) == 1);
  assert (sat_round<T> (1.5) == 2);
  assert (sat_round<T> (-0.49) == 0);
  assert (sat_round<T> (-0.5) == (Limits::is_signed ? T (-1) : T (0)));
  assert (sat_round<T> (-1.5) == (Limits::is_signed ? T (-2) : T (0)));
  assert (sat_round<T> (std::numeric_limits<double>::quiet_NaN()) == 0);
  assert (sat_round<T> (-std::numeric_limits<double>::quiet_NaN()) == 0);
  assert (sat_round<T> (inf) == Limits::max());
  assert (sat_round<T> (-inf) == Limits::min());
  assert (sat_round<T> (std::numeric_limits<double>::max()) == Limits::max());
  assert (sat_round<T> (-std::numeric_limits<double>::max()) == Limits::min());
  assert (sat_round<T> (double (Limits::max())) == Limits::max());
  assert (sat_round<T> (double (Limits::min())) == Limits::min());
  assert (sat_round<T> (std::nextafter (double (Limits::max()), inf)) == Limits::max());
  assert (sat_round<T> (std::nextafter (double (Limits::min()), -inf)) == Limits::min());

  // Exercise arbitrary IEEE double bit patterns under float-cast-overflow.
  std::mt19937_64 rng (42);
  for (int i = 0; i < 100000; i++)
    {
      const uint64_t bits = rng();
      double d;
      static_assert (sizeof (d) == sizeof (bits), "expected 64-bit double");
      memcpy (&d, &bits, sizeof (d));
      volatile T result = sat_round<T> (d);
      (void) result;
    }
}

int
main()
{
  test_sat_round<int8_t>();
  test_sat_round<uint8_t>();
  test_sat_round<int16_t>();
  test_sat_round<uint16_t>();
  test_sat_round<int32_t>();
  test_sat_round<uint32_t>();
  test_sat_round<int64_t>();
  test_sat_round<uint64_t>();

  // The largest 64-bit integers are not exactly representable as doubles.
  assert (sat_round<int64_t> (std::nextafter (0x1p63, 0.0)) == INT64_MAX - 1023);
  assert (sat_round<uint64_t> (std::nextafter (0x1p64, 0.0)) == UINT64_MAX - 2047);
  assert (sat_round<int64_t> (std::nextafter (-0x1p63, 0.0)) == INT64_MIN + 1024);

  // Exhaustively check quarter steps around the small integer ranges.
  for (int i = -1024; i <= 2048; i++)
    {
      const double d = i / 4.0;
      const int rounded = int (std::round (d));
      assert (sat_round<int8_t> (d) == std::clamp (rounded, -128, 127));
      assert (sat_round<uint8_t> (d) == std::clamp (rounded, 0, 255));
    }
}
