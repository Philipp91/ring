/* Copyright 2016 Brian Smith.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHORS DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE. */

#include "limbs.h"

#include <openssl/type_check.h>

#include "../internal.h"

#include "limbs.inl"


/* XXX: We assume that the conversion from |Carry| to |Limb| is constant-time,
 * but we haven't verified that assumption. TODO: Fix it so we don't need to
 * make that assumption. */

/* Prototypes to avoid -Wmissing-prototypes warnings. */
Limb LIMBS_less_than(const Limb a[], const Limb b[], size_t num_limbs);


/* We have constant time primitives on |size_t|. Rather than duplicate them,
 * take advantage of the fact that |size_t| and |Limb| are currently
 * compatible on all platforms we support. */
OPENSSL_COMPILE_ASSERT(sizeof(size_t) == sizeof(Limb),
                       size_t_and_limb_are_different_sizes);


/* Returns 0xfff..f if |a| is all zero limbs, and zero otherwise. */
Limb LIMBS_are_zero(const Limb a[], size_t num_limbs) {
  assert(num_limbs >= 1);
  Limb is_zero = constant_time_is_zero_size_t(a[0]);
  for (size_t i = 1; i < num_limbs; ++i) {
    is_zero = constant_time_select_size_t(
        is_zero, constant_time_is_zero_size_t(a[i]), is_zero);
  }
  return is_zero;
}

/* Returns 0xffff..f if |a| is less than |b|, and zero otherwise. */
Limb LIMBS_equal(const Limb a[], const Limb b[], size_t num_limbs) {
  assert(num_limbs >= 1);
  Limb eq = constant_time_eq_size_t(a[0], b[0]);
  for (size_t i = 1; i < num_limbs; ++i) {
    eq = constant_time_select_size_t(eq, constant_time_eq_size_t(a[i], b[i]),
                                     eq);
  }
  return eq;
}

/* Returns 0xffff...f if |a| is less than |b|, and zero otherwise. */
Limb LIMBS_less_than(const Limb a[], const Limb b[], size_t num_limbs) {
  assert(num_limbs >= 1);
  /* There are lots of ways to implement this. It is implemented this way to
   * be consistent with |LIMBS_limbs_reduce_once| and other code that makes such
   * comparisons as part of doing conditional reductions. */
  Limb dummy;
  Carry borrow = limb_sub(&dummy, a[0], b[0]);
  for (size_t i = 1; i < num_limbs; ++i) {
    borrow = limb_sbb(&dummy, a[i], b[i], borrow);
  }
  return constant_time_is_nonzero_size_t(borrow);
}

/* if (r >= m) { r -= m; } */
void LIMBS_reduce_once(Limb r[], const Limb m[], size_t num_limbs) {
  assert(num_limbs >= 1);
  /* This could be done more efficiently if we had |num_limbs| of extra space
   * available, by storing |r - m| and then doing a conditional copy of either
   * |r| or |r - m|. But, in order to operate in constant space, with an eye
   * towards this function being used in RSA in the future, we do things a
   * slightly less efficient way. */
  Limb lt = LIMBS_less_than(r, m, num_limbs);
  Carry borrow =
      limb_sub(&r[0], r[0], constant_time_select_size_t(lt, 0, m[0]));
  for (size_t i = 1; i < num_limbs; ++i) {
    /* XXX: This is probably particularly inefficient because the operations in
     * constant_time_select affect the carry flag, so there will likely be
     * loads and stores of |borrow|. */
    borrow =
        limb_sbb(&r[i], r[i], constant_time_select_size_t(lt, 0, m[i]), borrow);
  }
  assert(borrow == 0);
}

void LIMBS_add_mod(Limb r[], const Limb a[], const Limb b[], const Limb m[],
                   size_t num_limbs) {
  Limb overflow1 =
      constant_time_is_nonzero_size_t(limbs_add(r, a, b, num_limbs));
  Limb overflow2 = ~LIMBS_less_than(r, m, num_limbs);
  Limb overflow = overflow1 | overflow2;
  Carry borrow = limb_sub(&r[0], r[0], m[0] & overflow);
  for (size_t i = 1; i < num_limbs; ++i) {
    borrow = limb_sbb(&r[i], r[i], m[i] & overflow, borrow);
  }
}

void LIMBS_sub_mod(Limb r[], const Limb a[], const Limb b[], const Limb m[],
                   size_t num_limbs) {
  Limb underflow =
      constant_time_is_nonzero_size_t(limbs_sub(r, a, b, num_limbs));
  Carry carry = limb_add(&r[0], r[0], m[0] & underflow);
  for (size_t i = 1; i < num_limbs; ++i) {
    carry = limb_adc(&r[i], r[i], m[i] & underflow, carry);
  }
}
