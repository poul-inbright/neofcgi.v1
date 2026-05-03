#ifndef __NEOFCGI_FASTCGI_H__
#define __NEOFCGI_FASTCGI_H__

#include "conf.h"

NFCGI_BEGIN_DECLS;

// ======================================
// FastCGI protocol v1.1
// ======================================
#define FCGI_VERSION_1           1

// Listening socket file number
#define FCGI_LISTENSOCK_FILENO 0

// Value for requestId component of FCGI_Header | Management request
#define FCGI_NULL_REQUEST_ID     0

// WSAP   = WebServer -> App (WS to App only)
// STREAM = Stream records, end with an empty instance
// MGMT   = Management (ID 0) requests

enum FCGIRole_ {
    FCGIRole_NONE       = 0, // placeholder, undefined role
    FCGIRole_Responder  = 1, // classic server behaviour (WSAP)
    FCGIRole_Authorizer = 2, // get user data, reply yes or no (WSAP)
    FCGIRole_Filter     = 3  // filter files, but not quite supported by nginx for example (WSAP)
};

enum FCGIMgmtValue_ {
    FCGIMgmt_MAX_CONNS = 0, // max concurrent connections
    FCGIMgmt_MAX_REQS  = 1, // max concurrent requests
    FCGIMgmt_MPXS_CONN = 2, // multiplex or not (1 or 0)
    FCGIMgmt_MAX
};

extern const char* g_FCGIMgmtValueNames[FCGIMgmt_MAX];

enum FCGIRecordType_ {
    FCGI_RT_BEGIN_REQUEST     = 1,  // <= in  | WSAP        | Small, whole
    FCGI_RT_ABORT_REQUEST     = 2,  // <= in  | WSAP        | Small, whole
    FCGI_RT_END_REQUEST       = 3,  // => out |             | Small, whole
    FCGI_RT_PARAMS            = 4,  // <= in  | WSAP/STREAM | Terminated by empty instance
    FCGI_RT_STDIN             = 5,  // <= in  | STREAM      | Terminated by empty instance
    FCGI_RT_STDOUT            = 6,  // => out | STREAM      | Terminated by empty instance
    FCGI_RT_STDERR            = 7,  // => out | STREAM      | Terminated by empty instance
    FCGI_RT_DATA              = 8,  // <= in  | STREAM      | Terminated by empty instance  | Only present in `FCGIRole_Filter`
    FCGI_RT_GET_VALUES        = 9,  // <= in  | MGMT/WSAP   | Whole
    FCGI_RT_GET_VALUES_RESULT = 10, // => out | MGMT        | Whole
    FCGI_RT_UNKNOW_TYPE       = 11, // => out | MGMT        | Whole
    FCGI_RT_MAX
};

enum FCGIEndRequestStatus_ {
    FCGI_ERS_REQUEST_COMPLETE = 0, // all finished, all good
    FCGI_ERS_CANT_MPX_CONN    = 1, // cannot multiplex connections (multiple BEGIN_REQUEST)
    FCGI_ERS_OVERLOADED       = 2, // cannot handle more requests
    FCGI_ERS_UNKNOWN_ROLE     = 3  // asked of uknown role
};

// Mask for flags component of FCGI_BeginRequestBody
#define FCGI_KEEP_CONN  1

typedef struct {
    unsigned char version;
    unsigned char type;
    unsigned char requestIdB1;
    unsigned char requestIdB0;
    unsigned char contentLengthB1;
    unsigned char contentLengthB0;
    unsigned char paddingLength;
    unsigned char reserved;
} FCGI_Header;

typedef struct {
    unsigned char roleB1;
    unsigned char roleB0;
    unsigned char flags;
    unsigned char reserved[5];
} FCGI_BeginRequestBody;

typedef struct {
    unsigned char appStatusB3;
    unsigned char appStatusB2;
    unsigned char appStatusB1;
    unsigned char appStatusB0;
    unsigned char protocolStatus;
    unsigned char reserved[3];
} FCGI_EndRequestBody;

typedef struct {
    unsigned char type;    
    unsigned char reserved[7];
} FCGI_UnknownTypeBody;

typedef union {
    FCGI_BeginRequestBody begin_request;
    FCGI_EndRequestBody   end_request;
    FCGI_UnknownTypeBody  unknown_type;
} FCGI_GenericBody;

typedef struct {
    FCGI_Header      header;
    FCGI_GenericBody body;
} FCGI_RecordMeta;

void FCGI_MakeHeader(FCGI_Header* hdr, int type, int requestID, int contentLength, int paddingLength);
void FCGI_MakeEndRequestRecord(FCGI_RecordMeta* record, int requestID, nfcgi_u32 status, nfcgi_u8 protoStatus);
void FCGI_MakeUnknownTypeRecord(FCGI_RecordMeta* record, int requestID, nfcgi_u8 type);

NFCGI_END_DECLS;

#endif
