/* Hand-written libssh2 build configuration for PocketSSH (Linux / glibc,
 * gcc, OpenSSL 3 backend). Replaces the header that libssh2's CMake or
 * autotools build would generate from src/libssh2_config_cmake.h.in.
 * Included by libssh2's own libssh2_setup.h when HAVE_CONFIG_H is defined,
 * which 3rdparty/libssh2.pri does for C files only.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef POCKETSSH_LIBSSH2_CONFIG_H
#define POCKETSSH_LIBSSH2_CONFIG_H

/* Crypto backend */
#define LIBSSH2_OPENSSL 1

/* Headers */
#define HAVE_UNISTD_H
#define HAVE_INTTYPES_H
#define HAVE_SYS_SELECT_H
#define HAVE_SYS_UIO_H
#define HAVE_SYS_SOCKET_H
#define HAVE_SYS_IOCTL_H
#define HAVE_SYS_TIME_H
#define HAVE_SYS_UN_H
#define HAVE_ARPA_INET_H
#define HAVE_NETINET_IN_H

/* Functions */
#define HAVE_GETTIMEOFDAY
#define HAVE_STRTOLL
#define HAVE_SNPRINTF
#define HAVE_EXPLICIT_BZERO   /* glibc >= 2.25 */
#define HAVE_POLL
#define HAVE_SELECT

/* Socket non-blocking support */
#define HAVE_O_NONBLOCK

/* Not built: zlib compression (LIBSSH2_HAVE_ZLIB), debug tracing (LIBSSH2DEBUG). */

#endif
