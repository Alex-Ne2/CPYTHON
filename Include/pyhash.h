#ifndef Py_HASH_H
#define Py_HASH_H
#ifdef __cplusplus
extern "C" {
#endif

/* Prime multiplier used in string and various other hashes. */
#define _PyHASH_MULTIPLIER 1000003UL  /* 0xf4243 */

/* Parameters used for the numeric hash implementation.  See notes for
   _Py_HashDouble in Python/pyhash.c.  Numeric hashes are based on
   reduction modulo the prime 2**_PyHASH_BITS - 1. */

#if SIZEOF_VOID_P >= 8
#  define _PyHASH_BITS 61
#else
#  define _PyHASH_BITS 31
#endif

#define PyUnstable_PyHASH_BITS _PyHASH_BITS

#define _PyHASH_MODULUS (((size_t)1 << _PyHASH_BITS) - 1)
#define PyUnstable_PyHASH_MODULUS _PyHASH_MODULUS
#define _PyHASH_INF 314159
#define PyUnstable_PyHASH_INF _PyHASH_INF
#define _PyHASH_IMAG _PyHASH_MULTIPLIER
#define PyUnstable_PyHASH_IMAG _PyHASH_IMAG

/* Cutoff for small string DJBX33A optimization in range [1, cutoff).
 *
 * About 50% of the strings in a typical Python application are smaller than
 * 6 to 7 chars. However DJBX33A is vulnerable to hash collision attacks.
 * NEVER use DJBX33A for long strings!
 *
 * A Py_HASH_CUTOFF of 0 disables small string optimization. 32 bit platforms
 * should use a smaller cutoff because it is easier to create colliding
 * strings. A cutoff of 7 on 64bit platforms and 5 on 32bit platforms should
 * provide a decent safety margin.
 */
#ifndef Py_HASH_CUTOFF
#  define Py_HASH_CUTOFF 0
#elif (Py_HASH_CUTOFF > 7 || Py_HASH_CUTOFF < 0)
#  error Py_HASH_CUTOFF must in range 0...7.
#endif /* Py_HASH_CUTOFF */


/* Hash algorithm selection
 *
 * The values for Py_HASH_* are hard-coded in the
 * configure script.
 *
 * - FNV and SIPHASH* are available on all platforms and architectures.
 * - With EXTERNAL embedders can provide an alternative implementation with::
 *
 *     PyHash_FuncDef PyHash_Func = {...};
 *
 * XXX: Figure out __declspec() for extern PyHash_FuncDef.
 */
#define Py_HASH_EXTERNAL 0
#define Py_HASH_SIPHASH24 1
#define Py_HASH_FNV 2
#define Py_HASH_SIPHASH13 3

#ifndef Py_HASH_ALGORITHM
#  ifndef HAVE_ALIGNED_REQUIRED
#    define Py_HASH_ALGORITHM Py_HASH_SIPHASH13
#  else
#    define Py_HASH_ALGORITHM Py_HASH_FNV
#  endif /* uint64_t && uint32_t && aligned */
#endif /* Py_HASH_ALGORITHM */

#ifndef Py_LIMITED_API
#  define Py_CPYTHON_HASH_H
#  include "cpython/pyhash.h"
#  undef Py_CPYTHON_HASH_H
#endif

#ifdef __cplusplus
}
#endif
#endif  // !Py_HASH_H
