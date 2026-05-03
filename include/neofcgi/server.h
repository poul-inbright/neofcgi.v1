#ifndef __NEOFCGI_SERVER_H__
#define __NEOFCGI_SERVER_H__

#include "fastcgi.h"

NFCGI_BEGIN_DECLS;

#define NFCGI_SERVER_DEFAULT_STDOUT_HTML \
    "Content-type:text/html\r\n\r\n<html><head><title>NEOFCGI v1.0</title></head><body><h1>Default stdout</h1></body></html>"

// use min(read, content_len) when reading stdin or _FCGS_RdBufUDataSize()
typedef struct _FCGS_RdBuf {
    char*    buffer;
    nfcgi_u32 point;        // virtual pointer into the buffer
    nfcgi_u32 read;         // how much is currently in the buffer
    nfcgi_u32 _capacity;    // how much buffer is capable of fitting
    nfcgi_u32 _content_len; // how much content bytes are available till the next record
} _FCGS_RdBuf;

typedef struct _FCGS_WrBuf {
    char*    buffer;
    nfcgi_u32 point;         // how much is currently present in the buffer
    nfcgi_u32 capacity;      // the full capacity of the buffer
} _FCGS_WrBuf;

enum FCGSConFlags_ {
    FCGSCon_ReadingStdin  = 0x01, // currently inside and reading a `FCGI_STDIN` record (http request input/body)
    FCGSCon_StdinEmpty    = 0x02, // nothing more to read from stdin
    FCGSCon_WritingStdout = 0x04, // used when flushing
    FCGSCon__BufferFlags  = FCGSCon_ReadingStdin | FCGSCon_StdinEmpty | FCGSCon_WritingStdout
};

// - A structure representing an active connection (one made with accept())
// - Request notation is not used, since technically FastCGI connection can
// host multiple requests
// - Multiplexing is off by default and there is one STDIN, STDOUT and STDERR buffer
// - Multiplexing can be implemented in the future if neccessary (there are different ways of doing it)
typedef struct FCGS_Connection {
    nfcgi_u16   activeReqID;  // active FCGI Request ID
    nfcgi_u8    role;         // TODO: move to flags
    nfcgi_u8    flags;
    nfcgi_u8    _stdinpadd;   // stdin record is padded to ensure alignment
    nfcgi_u16   _envbufsz;    
    nfcgi_u32   ip_addr;
    fsock_t     ipcFD;
    char*       envbuf;
    _FCGS_RdBuf rdbuf;
    _FCGS_WrBuf wrbuf;
    _FCGS_WrBuf errbuf;
} FCGS_Connection;

// For both the sake of simplicity and performance, neofcgi.v1 basically copies the entirety
// of the parameters into a buffer and simply iterates over it as-is
typedef struct {
    const char* name;
    const char* value;
    nfcgi_u32    nameLen;
    nfcgi_u32    valueLen;
    nfcgi_u32    __offset;
} FCGS_ParamIt;

enum FCGSConfFlags {
    FCGSConf__Initialized = 0x01,
    FCGSConf__IsFastCGI   = 0x02,
    FCGSConf__FCGIQueried = 0x04,
    // FCGSConf_Multiplex    = 0x08 // No direct support yet, need to change a lot
};

typedef struct FCGSConfig {
    nfcgi_u32 flags;
    nfcgi_u32 IOBufSize;
    nfcgi_u32 auxIOBufSz;
    nfcgi_u16 mgmtValues[FCGIMgmt_MAX];
    void* (*realloc)(void*, size_t size);
} FCGSConfig;

extern FCGSConfig _FCGSConfig;

fsock_t FCGS_CreateSocket(short port, int backlog);
fsock_t FCGS_Accept(fsock_t socket, unsigned long* ip_addr);

// ========================================================================
// High-level API
// ========================================================================

int FCGS_Init();
int FCGS_Deinit();

int FCGS_DefaultInit();

/* *----------------------------------------------------------------------
 * FCGS_IsCGI --
 *      Returns TRUE if this process appears to be a CGI process
 *      rather than a FastCGI process.
 *---------------------------------------------------------------------- */
int FCGS_IsCGI();

// Initialize the connection structure
int FCGS_ConInit(FCGS_Connection* con, nfcgi_u32 inBufSz, nfcgi_u32 outBufSz);

// Frees the resources used by the connection structure
void FCGS_ConFree(FCGS_Connection* con);

int FCGS_ConAccept(FCGS_Connection* con, fsock_t socket);

int FCGS_ConIsFormData(FCGS_Connection* con);

// Write the closing records and close the connection socket
void FCGS_ConFinish(FCGS_Connection* con, nfcgi_u32 status);

int FCGS_ConEnv_Find(FCGS_Connection* con, FCGS_ParamIt* it, const char *name);
int FCGS_ConEnv_Iterate(FCGS_Connection* con, FCGS_ParamIt* it);

// 1. Try to guarantee that the loaded-in stdin chunk is of size `n`
// 2. Usually this involves shortening the read buffer by the already present chunk,
//    processing until stdin and stitching everything back together until we have `n` bytes
// 3. In most cases STDIN comes after STDIN and the buffer will be relatively
//    easy to stitch, the function will move the smaller part to a bigger part
// * This functionality is mostly required for parsing the stdin
// * The other way of implementing this would be double-buffering, yet it will have its own quirks
int FCGS_StdinReqBytes(FCGS_Connection* con, nfcgi_u32 n);

// Provide `n` bytes of stdin data into `str`
int FCGS_StdinRead(FCGS_Connection* con, char *str, size_t n);
int FCGS_StdinStore(FCGS_Connection* con, const char* filepath);

// Use this to properly register inside the rdbuf that you've processed `n` bytes of stdin
int FCGS_StdinAdvance(FCGS_Connection* on, size_t n);

int FCGS_StdoutReqBytes(FCGS_Connection* con, nfcgi_u32 n);
int FCGS_StdoutWrite(FCGS_Connection* con, const char *str, size_t n);
int FCGS_StdoutFlush(FCGS_Connection* con);

// ============================================= FORM-DATA ==========================================

// Constants for a value returned from `FCGS_FDIt_Iterate()`
enum FCGS_FD_ {
    FCGS_FD_End = 0,
    FCGS_FD_Old = 1,
    FCGS_FD_New = 2,
    FCGS_FD_Err = 3
};

typedef struct {
    char*    boundary;
    char*    headers;
    char*    fd_start;
    size_t   fd_size;
    nfcgi_u32 bnd_len;
    nfcgi_u32 hdr_size;
} FCGS_FDIt;

int _FCGS_FormData_ReadNewEntry(FCGS_Connection* con, FCGS_FDIt* it);

// returns enum FCGS_FD_
int   FCGS_FDIt_Init(FCGS_Connection* con, FCGS_FDIt* it);
int   FCGS_FDIt_Deinit(FCGS_FDIt* it);

int   FCGS_FormData_Iterate(FCGS_Connection* con, FCGS_FDIt* it);
char* FCGS_FormData_Store(FCGS_Connection* con, const char* path, nfcgi_u32* num_files);

// ========================================================================
// Low-level API
// ========================================================================

#define _FCGS_RdBufUDataSize(con, var) {\
    var = (con)->rdbuf.read - (con)->rdbuf.point;\
    var = (var) < con->rdbuf._content_len ? (var) : con->rdbuf._content_len; \
}

// * Function: Make sure the input buffer has n bytes, if not, read
// * Result: 0 on success, -1 on failure to comply
int _FCGS_RdBufReqBytes(FCGS_Connection* con, nfcgi_u32 n);

// * Function: Skip `"n"` bytes starting from `"con->rdbuf.point"`, useful for skipping records
// * Result: 0 on success, -1 on failure to accomplish (usually out of input)
int _FCGS_RdBufSkipBytes(FCGS_Connection* con, nfcgi_u32 n);

// ---------------------------------------------------------------------------------------------------------------------------
// NOTE: Processing records / Default Parsing / _FCGS_RdBufReqStdin
// ---------------------------------------------------------------------------------------------------------------------------
// Internally, fcgiapp.c will do:
// 1. call _FCGS_RdBufReqBytes(con, sizeof(FCGI_RecordMeta))
// 2. Then extract a `FCGI_RecordMeta` from the `con->rdbuf` and advance the read pointer
// 3. For any request: since we do not support multiplexing we skip the records with request id not match the activeRequestID
// 4. Afterwards it will switch-case and handle each record depending on its type, namely:
// `FCGI_RT_BEGIN_REQUEST`: set the activeRequestID if it's 0, get the role information
//                          skip if given a filter's role (not supported)
// `FCGI_RT_ABORT_REQUEST`: reset the buffers and set the activeRequestID back to 0
// `FCGI_RT_PARAMS`       : read the params (and the following ones up until the last one,
//                           all the params must come first, then stdin.)
// `FCGI_RT_GET_VALUES`   : only comes with requestID 0 (MGMT), respond raw and immediately
// `FCGI_RT_DATA`         : filtering role is not supported, not gonna get this since we
//                          refuse the filter role here
// `FCGI_RT_STDIN`        : finally the request body, flag the connection and return
// ---------------------------------------------------------------------------------------------------------------------------
// * Perform default parsing until the stdin record, then make rdbuf ready for giving stdin (and set the appropriate flag)
// * (for more information see lower)
int _FCGS_RdBufReqStdin(FCGS_Connection* con);

// * Useful to abort a request
void _FCGS_ConReset(FCGS_Connection* con);

NFCGI_END_DECLS;

#endif /* defined(__NEOFCGI_SERVER_H__) */
