// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0
// Author: 2014, Tim Janik, see http://testbit.org/keccak

#pragma once

#include <stdint.h>
#include <random>

namespace LiquidSFZInternal
{

/** Pcg32Rng is a permutating linear congruential PRNG.
 * At the core, this pseudo random number generator uses the well known
 * linear congruential generator:
 * 6364136223846793005 * accumulator + 1442695040888963407 mod 2^64.
 * See also TAOCP by D. E. Knuth, section 3.3.4, table 1, line 26.
 * For good statistical performance, the output function of the permuted congruential
 * generator family is used as described on http://www.pcg-random.org/.
 * Period length for this generator is 2^64, the specified seed @a offset
 * chooses the position of the genrator and the seed @a sequence parameter
 * can be used to choose from 2^63 distinct random sequences.
 */
class Pcg32Rng {
  uint64_t increment_;          // must be odd, allows for 2^63 distinct random sequences
  uint64_t accu_;               // can contain all 2^64 possible values
  static constexpr const uint64_t A = 6364136223846793005ULL; // from C. E. Hayness, see TAOCP by D. E. Knuth, 3.3.4, table 1, line 26.
  static inline constexpr uint32_t
  ror32 (const uint32_t bits, const uint32_t offset)
  {
    // bitwise rotate-right pattern recognized by gcc & clang iff 32==sizeof (bits)
    return (bits >> offset) | (bits << ((32 - offset) & 31));
  }
  static inline constexpr uint32_t
  pcg_xsh_rr (const uint64_t input)
  {
    // Section 6.3.1. 32-bit Output, 64-bit State: PCG-XSH-RR
    // http://www.pcg-random.org/pdf/toms-oneill-pcg-family-v1.02.pdf
    return ror32 ((input ^ (input >> 18)) >> 27, input >> 59);
  }
public:
  /// Initialize and seed from @a seed_sequence.
  template<class SeedSeq>
  explicit Pcg32Rng  (SeedSeq &seed_sequence) : increment_ (0), accu_ (0) { seed (seed_sequence); }
  /// Initialize and seed by seeking to position @a offset within stream @a sequence.
  explicit Pcg32Rng  (uint64_t offset, uint64_t sequence) :
    increment_ (0), accu_ (0)
  {
    seed (offset, sequence);
  }
  /// Initialize and seed the generator from a system specific nondeterministic random source.
  explicit Pcg32Rng  () :
    increment_ (0), accu_ (0)
  {
    auto_seed();
  }

  /// Seed the generator from a system specific nondeterministic random source.
  void auto_seed ()
  {
    std::random_device random_dev;
    auto a = random_dev();
    auto b = random_dev();
    seed (a, b);
  }
  /// Seed by seeking to position @a offset within stream @a sequence.
  void seed (uint64_t offset, uint64_t sequence)
  {
    accu_ = sequence;
    increment_ = (sequence << 1) | 1;    // force increment_ to be odd
    accu_ += offset;
    accu_ = A * accu_ + increment_;
  }
  /// Seed the generator state from a @a seed_sequence.
  template<class SeedSeq> void
  seed (SeedSeq &seed_sequence)
  {
    uint64_t seeds[2];
    seed_sequence.generate (&seeds[0], &seeds[2]);
    seed (seeds[0], seeds[1]);
  }
  /// Generate uniformly distributed 32 bit pseudo random number.
  uint32_t
  random ()
  {
    const uint64_t lcgout = accu_;      // using the *last* state as ouput helps with CPU pipelining
    accu_ = A * accu_ + increment_;
    return pcg_xsh_rr (lcgout);         // PCG XOR-shift + random rotation
  }
};

}
