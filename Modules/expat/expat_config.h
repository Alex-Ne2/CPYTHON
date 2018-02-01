/*
 * Expat configuration for python. This file is not part of the expat
 * distribution.
 */
#ifndef EXPAT_CONFIG_H
#define EXPAT_CONFIG_H

/* bpo-31374: Expat defines _POSIX_C_SOURCE before including expat_config.h,
   but pyconfig.h redefines _POSIX_C_SOURCE on some platforms. Unset it to
   prevent a compiler warning. */
#ifdef _POSIX_C_SOURCE
#  undef _POSIX_C_SOURCE
#endif

/* Get WORDS_BIGENDIAN and HAVE_MEMMOVE defines from pyconfig.h */
#include <pyconfig.h>

#ifdef WORDS_BIGENDIAN
#define BYTEORDER 4321
#else
#define BYTEORDER 1234
#endif

#define HAVE_MEMMOVE 1

#define XML_NS 1
#define XML_DTD 1
#define XML_CONTEXT_BYTES 1024

#endif /* EXPAT_CONFIG_H */
