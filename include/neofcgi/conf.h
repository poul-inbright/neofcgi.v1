#ifndef __NEOFCGI__FCGICONF_H__
#define __NEOFCGI__FCGICONF_H__

#ifndef DLLAPI
    #if defined (_WIN32) && defined (_MSC_VER)
        #define WIN32_LEAN_AND_MEAN
        #define __NFCGI_WINDOWS__

        #include <winsock2.h>
        #include <windows.h>
    #else
        #define __NFCGI_UNIX__

        #include <netdb.h>
        #include <sys/socket.h> /* for getpeername */
        #include <unistd.h>
        #include <netinet/in.h>
        #include <arpa/inet.h>
        #include <sys/un.h>

        #define _nfcgi_memmem memmem
    #endif
#endif

#if defined (c_plusplus) || defined (__cplusplus)
    #define NFCGI_BEGIN_DECLS extern "C" {
    #define NFCGI_END_DECLS };
#else
    #define NFCGI_BEGIN_DECLS
    #define NFCGI_END_DECLS
#endif

NFCGI_BEGIN_DECLS;

#include <stdlib.h> // for size_t
#include <errno.h>
#include <stdio.h>  // EOF

#include <string.h>
#define _nfcgi_memcpy                  memcpy
#define _nfcgi_memcmp                  memcmp
#define _nfcgi_memmove                 memmove
#define _nfcgi_strlen                  strlen
#define _nfcgi_strcpy                  strcpy
#define _nfcgi_strncmp                 strncmp
#define _nfcgi_memset                  memset
#define _nfcgi_memchr                  memchr
#define _nfcgi_fmtlen(fmt, ...)        snprintf(NULL, 0, fmt, __VA_ARGS__)
#define _nfcgi_strfmt(buf, fmt, ...)   sprintf(buf, fmt, __VA_ARGS__)

#define _nfcgi_realloc                 realloc

// snprintf uses the 0-byte, which we don't need
#define _NFCGI_FMTLEN_NULLBYTE 1

#include <stdint.h>
typedef uint8_t   nfcgi_u8;
typedef int8_t    nfcgi_s8;
typedef uint16_t  nfcgi_u16;
typedef int16_t   nfcgi_s16;
typedef uint32_t  nfcgi_u32;
typedef int32_t   nfcgi_s32;
typedef uintmax_t nfcgi_umax_t;

#define _NFCGI_ALIGN8(n)  (((n) +  7) & 0xFFFFFFF8)
#define _NFCGI_ALIGN16(n) (((n) + 15) & 0xFFFFFFF0)

enum NEOFCGI_Const {
    NFCGI_Const_PARAMNAME_MAX  = 0x100,  // just a reasonable number
    NFCGI_Const_PARAMVALUE_MAX = 0x400,  // just a reasonable number
    NFCGI_Const_STREAMBUF_MAX  = 0x1000, // also the default value for STDIN and STDOUT sizes
    NFCGI_Const_STREAMBUF_MIN  = 0x100,  // also the default value for STDERR size
    NFCGI_Const_NETPATH_MAX    = 0x400,
    NFCGI_Const_GVR_BUF_MAX    = 0x80    // buffer size for GET_VALUES_RESULT FastCGI response
};

NFCGI_END_DECLS;

#ifdef __NFCGI_WINDOWS__
    #define NFCGI_OS_BAD_SOCKET INVALID_SOCKET

    typedef SOCKET fsock_t;

    #define _NFCGI_OS_Read(socket, buf, size)  recv(socket, (char*)buf, size, 0)
    #define _NFCGI_OS_Write(socket, buf, size) send(socket, (const char*)buf, size, 0)
    #define _NFCGI_OS_Bind                     bind 
    #define _NFCGI_OS_Close                    closesocket 
#else
    #include <unistd.h>

    #define NFCGI_OS_BAD_SOCKET -1

    typedef int fsock_t;

    #define _NFCGI_OS_Read   read
    #define _NFCGI_OS_Write  write
    #define _NFCGI_OS_Bind   bind 
    #define _NFCGI_OS_Close  close
#endif

NFCGI_BEGIN_DECLS;

#ifdef __NFCGI_WINDOWS__
    void* _nfcgi_memmem(const void* haystack, nfcgi_u32 hay_len, const void* needle, nfcgi_u32 needle_len);
#endif

NFCGI_END_DECLS;

#endif
