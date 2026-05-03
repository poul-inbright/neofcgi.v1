#include "../include/neofcgi/fastcgi.h"

#ifdef __NFCGI_WINDOWS__
    void* _nfcgi_memmem(const void* haystack, size_t hay_len, const void* needle, size_t needle_len){
        if (needle_len == 0) return (void*)haystack;
        if (hay_len < needle_len) return NULL;

        const char* hay = (const char*)haystack;
        const char* n = (const char*)needle;
        char first = *n;

        // We only need to search up to the point where the needle can still fit
        while (hay_len >= needle_len) {
            const char* ptr = (const char*)_nfcgi_memchr(hay, first, hay_len);
            
            if (ptr == NULL) return NULL;

            // Calculate how much we skipped to reach ptr
            size_t skipped = (size_t)(ptr - hay);
            
            // Check if the remaining haystack is shorter than the needle
            if (skipped + needle_len > hay_len) {
                return NULL;
            }

            if (_nfcgi_memcmp(ptr, n, needle_len) == 0) {
                return (void*)ptr;
            }

            // Advance: move past the current 'first' character match
            hay = ptr + 1;
            hay_len -= (skipped + 1);
        }

        return NULL;
    }
#endif

const char* g_FCGIMgmtValueNames[FCGIMgmt_MAX] = {
    "FCGI_MAX_CONNS",
    "FCGI_MAX_REQS",
    "FCGI_MPXS_CONNS"
};

void FCGI_MakeHeader(FCGI_Header* hdr, int type, int requestId, int contentLength, int paddingLength){
    hdr->version          = FCGI_VERSION_1;
    hdr->type             = (unsigned char) type;
    hdr->requestIdB1      = (unsigned char) ((requestId     >> 8) & 0xff);
    hdr->requestIdB0      = (unsigned char) ((requestId         ) & 0xff);
    hdr->contentLengthB1  = (unsigned char) ((contentLength >> 8) & 0xff);
    hdr->contentLengthB0  = (unsigned char) ((contentLength     ) & 0xff);
    hdr->paddingLength    = (unsigned char) paddingLength;
    hdr->reserved         =  0;
}

void FCGI_MakeEndRequestRecord(FCGI_RecordMeta* record, int requestID, nfcgi_u32 status, nfcgi_u8 protoStatus){
    FCGI_MakeHeader(&record->header,FCGI_RT_END_REQUEST, requestID, 0, 0);

    record->body.end_request.appStatusB0 = (status >>  0) & 0xff;
    record->body.end_request.appStatusB1 = (status >>  8) & 0xff;
    record->body.end_request.appStatusB2 = (status >> 16) & 0xff;
    record->body.end_request.appStatusB3 = (status >> 24) & 0xff;

    record->body.end_request.protocolStatus = protoStatus;
    record->body.end_request.reserved[0] = 0;
    record->body.end_request.reserved[1] = 0;
    record->body.end_request.reserved[2] = 0;
}

void FCGI_MakeUnknownTypeRecord(FCGI_RecordMeta* record, int requestID, nfcgi_u8 type){
    FCGI_MakeHeader(&record->header, FCGI_RT_UNKNOW_TYPE, requestID, 0, 0);
    
    record->body.unknown_type.type        = type;
    record->body.unknown_type.reserved[0] = 0;
    record->body.unknown_type.reserved[1] = 0;
    record->body.unknown_type.reserved[2] = 0;
    record->body.unknown_type.reserved[3] = 0;
    record->body.unknown_type.reserved[4] = 0;
    record->body.unknown_type.reserved[5] = 0;
    record->body.unknown_type.reserved[6] = 0;
}


