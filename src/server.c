#include "../include/neofcgi/server.h"
#include <assert.h>

FCGSConfig _FCGSConfig = {0};

fsock_t FCGS_CreateSocket(short port, int backlog){
    fsock_t listenSock;
    int len;
    struct sockaddr_in sa;

	sa.sin_family      = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	sa.sin_port        = htons(port);
	len = sizeof(sa);

	listenSock = socket(AF_INET, SOCK_STREAM, 0);

    if(listenSock != NFCGI_OS_BAD_SOCKET){
        int flag = 1;
        #ifdef __FCGX_WINDOWS__
            setsockopt(listenSock, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (char *) &flag, sizeof(flag));
        #else
            setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, (char *) &flag, sizeof(flag));
        #endif

        if (_NFCGI_OS_Bind(listenSock, (struct sockaddr *) &sa, len) < 0 || listen(listenSock, backlog) < 0) {
            return NFCGI_OS_BAD_SOCKET;
        }
    }

    return listenSock;
}

#ifdef __NFCGI_WINDOWS__
    fsock_t FCGS_Accept(fsock_t listen_socket, unsigned long* ip_addr){
        struct sockaddr_in sa;
        int len;

        len = sizeof(sa);
        fsock_t socket = accept(listen_socket, (struct sockaddr*)&sa, &len);

        *ip_addr = ntohl(sa.sin_addr.s_addr);

        return socket;
    }
#else
    fsock_t FCGS_Accept(int listen_socket, unsigned long* ip_addr){
        union {
            struct sockaddr_in in;
            struct sockaddr_un un;
        } sa;
        socklen_t len;
        fsock_t socket;
        
        len = sizeof(sa);
        
        // TODO: handle tricky errno's
        do {
            socket = accept(listen_socket, (struct sockaddr*)&sa, &len);
        } while(socket == FCGX_OS_BAD_SOCKET);

        *ip_addr = ntohl(sa.in.sin_addr.s_addr);

        return socket;
    }
#endif

int FCGS_Init() {
    if (_FCGSConfig.flags & FCGSConf__Initialized) {
        return 0;
    }

    _FCGSConfig.flags |= FCGSConf__Initialized;

    #ifdef __NFCGI_WINDOWS__
        WSADATA wsa;
        if(WSAStartup(MAKEWORD(2, 2), &wsa) != 0){
            return -1;
        }
    #endif

    return 0;
}

int FCGS_DefaultInit(){
    _FCGSConfig.IOBufSize  = NFCGI_Const_STREAMBUF_MAX;
    _FCGSConfig.auxIOBufSz = NFCGI_Const_STREAMBUF_MIN;
    _FCGSConfig.realloc    = _nfcgi_realloc;
    _FCGSConfig.mgmtValues[FCGIMgmt_MAX_CONNS] = 1;
    _FCGSConfig.mgmtValues[FCGIMgmt_MAX_REQS] = 2;
    _FCGSConfig.mgmtValues[FCGIMgmt_MPXS_CONN] = 0;

    return FCGS_Init();
}

int FCGS_Deinit(){
    #ifdef __NFCGI_WINDOWS__
        WSACleanup();
    #endif

    return 0;
}

int FCGS_IsCGI(void){
    if (_FCGSConfig.flags & FCGSConf__FCGIQueried) {
        return !(_FCGSConfig.flags & FCGSConf__IsFastCGI);
    }

    if (_FCGSConfig.flags & FCGSConf__Initialized) {
        int rc = FCGS_Init();
        if (rc) {
            return -1;
        }
    }

    return 1;
}

int FCGS_ConInit(FCGS_Connection* con, nfcgi_u32 inBufSz, nfcgi_u32 outBufSz){
    _nfcgi_memset(con, 0, sizeof(FCGS_Connection));

    inBufSz = inBufSz ? _FCGSConfig.IOBufSize : inBufSz;
    inBufSz = inBufSz < NFCGI_Const_STREAMBUF_MIN ? NFCGI_Const_STREAMBUF_MIN : inBufSz;
    inBufSz = _NFCGI_ALIGN16(inBufSz);

    outBufSz = outBufSz ? _FCGSConfig.IOBufSize : outBufSz;
    outBufSz = outBufSz < NFCGI_Const_STREAMBUF_MIN ? NFCGI_Const_STREAMBUF_MIN : outBufSz;
    outBufSz = _NFCGI_ALIGN16(outBufSz);

    con->rdbuf.buffer    = _FCGSConfig.realloc(NULL, inBufSz);
    con->rdbuf._capacity = inBufSz;

    // NOTE: prealign the buffer the the header
    con->wrbuf.buffer    = (char*)_FCGSConfig.realloc(NULL, outBufSz + sizeof(FCGI_Header)) + sizeof(FCGI_Header);
    con->wrbuf.capacity  = outBufSz;

    con->errbuf.buffer   = _FCGSConfig.realloc(NULL, outBufSz >> 2);
    con->errbuf.capacity = outBufSz >> 2;

    con->ipcFD = -1;

    return 0;
}

void FCGS_ConFree(FCGS_Connection* con){
    if ((_FCGSConfig.flags & FCGSConf__Initialized) == 0) {
        return;
    }

    _FCGSConfig.realloc(con->rdbuf.buffer, 0);
    _FCGSConfig.realloc(con->wrbuf.buffer, 0);
    _FCGSConfig.realloc(con->errbuf.buffer, 0);
    _FCGSConfig.flags &= ~FCGSConf__Initialized;
}

int FCGS_ConAccept(FCGS_Connection* con, fsock_t socket){
    if ((_FCGSConfig.flags & FCGSConf__Initialized) == 0) {
        return -1;
    }

    // Finish the current request, if any.
    FCGS_ConFinish(con, 0);

    // Accept a new connection (blocking).
    // If an OS error occurs in accepting the connection,
    // return -1 to the caller, who should exit.
    con->ipcFD = FCGS_Accept(socket, &con->ip_addr);
    if (con->ipcFD < 0) {
        return -1;
    }
    return 0;
}

int FCGS_ConIsFormData(FCGS_Connection* con){
    FCGS_ParamIt param_it;
    param_it.__offset = 0;
    if(FCGS_ConEnv_Find(con, &param_it, "CONTENT_TYPE")){
        return 0;
    }

    if(_nfcgi_memmem(param_it.value, param_it.valueLen, "multipart/form-data", sizeof("multipart/form-data") - 1) == NULL){
        return 0;
    }

    return 1;
}

void FCGS_ConFinish(FCGS_Connection* con, nfcgi_u32 status){
    if(con->ipcFD <= 0){
        return;
    }
    FCGS_StdoutFlush(con);
    FCGI_Header header;
    FCGI_RecordMeta record;
    FCGI_MakeHeader(&header, FCGI_RT_STDOUT, con->activeReqID, 0, 0);
    _NFCGI_OS_Write(con->ipcFD, (char*)&header, sizeof(header));
    FCGI_MakeHeader(&header, FCGI_RT_STDERR, con->activeReqID, 0, 0);
    _NFCGI_OS_Write(con->ipcFD, (char*)&header, sizeof(header));
    FCGI_MakeEndRequestRecord(&record, con->activeReqID, status, FCGI_ERS_REQUEST_COMPLETE);
    _NFCGI_OS_Write(con->ipcFD, (char*)&record, sizeof(record));
    // TODO: maybe read all that is left
    _NFCGI_OS_Close(con->ipcFD);

    con->ipcFD = -1;
    con->activeReqID = 0;
}

// TODO: Kinda bad, but ok for now
int FCGS_ConEnv_Find(FCGS_Connection* con, FCGS_ParamIt* it, const char *name){
    nfcgi_u32 len = _nfcgi_strlen(name);

    while(FCGS_ConEnv_Iterate(con, it)){
        if(it->nameLen == len && _nfcgi_memcmp(it->name, name, len) == 0){
            return 0;
        }
    }

    return -1;
}

int FCGS_ConEnv_Iterate(FCGS_Connection* con, FCGS_ParamIt* it){
    if(it->__offset >= con->_envbufsz){
        return 0;
    }
    nfcgi_u8* p;
    nfcgi_u32 l;

    p = (nfcgi_u8*)con->envbuf + it->__offset;

    l = 1 << ((*p & 0x80) >> 6);
    it->nameLen = l > 1 ? ((p[0] & 0x7F) << 24) | (p[1] << 16) | (p[2] << 8) | p[3] : p[0];
    p += l;

    l = 1 << ((*p & 0x80) >> 6);
    it->valueLen = l > 1 ? ((p[0] & 0x7F) << 24) | (p[1] << 16) | (p[2] << 8) | p[3] : p[0];
    p += l;

    it->name     = (const char*)p;
    it->value    = (const char*)p + it->nameLen;
    it->__offset = (char*)p - con->envbuf + it->nameLen + it->valueLen;

    return 1;
}

// * Useful for the abort request
void _FCGS_ConReset(FCGS_Connection* con){
    con->rdbuf.read = con->rdbuf.point = 0;
    con->flags &= ~FCGSCon__BufferFlags;
    con->wrbuf.point = 0;
    con->activeReqID = 0;
    con->role = FCGIRole_NONE;
}

int FCGS_StdinReqBytes(FCGS_Connection* con, nfcgi_u32 n){
    // cannot fulfill the request in general
    if(n > con->rdbuf._capacity || (con->flags & FCGSCon_StdinEmpty)){
        return -1;
    }

    // the cycle below is about reading most stdin available and parsing until next if wasn't done
    // so can't start from an FCGI Record
    if((con->flags & FCGSCon_ReadingStdin) == 0){
        if(_FCGS_RdBufReqStdin(con)){
            return -1;
        }
    }

    for(;;){
        // the amount currently readable in the buffer
        nfcgi_u32 present      = con->rdbuf.read - con->rdbuf.point;
        nfcgi_u32 content_read = present < con->rdbuf._content_len ? present : con->rdbuf._content_len;

        // ----------------------------------- FAST PATH --------------------------------------
        if(content_read >= n){
            break;
        }

        // move everything down either if cannot fit
        if(con->rdbuf.point + n >= con->rdbuf._capacity){
            if(present){
                _nfcgi_memcpy(con->rdbuf.buffer, con->rdbuf.buffer + con->rdbuf.point, con->rdbuf.read - con->rdbuf.point);
            }
            con->rdbuf.read -= con->rdbuf.point;
            con->rdbuf.point = 0;
        }

        if(content_read < con->rdbuf._content_len){
            int read_bytes = _NFCGI_OS_Read(con->ipcFD, con->rdbuf.buffer + con->rdbuf.read, con->rdbuf._capacity - con->rdbuf.read);
            if(read_bytes < 0){
                return -1;
            }

            con->rdbuf.read    += read_bytes;
            nfcgi_u32 new_read  = con->rdbuf._content_len - content_read;             // if content_read == content_len => 0
            content_read       += new_read < (nfcgi_u32)read_bytes ? new_read : read_bytes; // clamp new_content to read_bytes

            if(content_read >= n){
                break;
            }
        }else{
            // --------------------- Preserve & DefaultParse ----------------------

            // 1. hide the currently buffered linear stdin chunk to trick other functions
            nfcgi_u32 margin            = con->rdbuf.point;
            nfcgi_u32 preserved         = content_read + margin;
            con->rdbuf.buffer    += preserved;
            con->rdbuf.read      -= preserved;
            con->rdbuf._capacity -= preserved;
            con->rdbuf.point      = 0;
            con->flags &= ~FCGSCon_ReadingStdin;

            // 2. find the next stdin
            if(_FCGS_RdBufReqStdin(con)){
                con->rdbuf.buffer       -= preserved;
                con->rdbuf.read         += preserved;
                con->rdbuf._capacity    += preserved;
                con->rdbuf.point         = margin;
                return -1;
            }

            // ------------------------------ UNPRESERVE ---------------------------------

            // how much at the upper side
            nfcgi_u32 read_bytes = con->rdbuf.read - con->rdbuf.point;

            nfcgi_u32 from, to, size;

            // always move the smaller amount
            if((nfcgi_u32)read_bytes < content_read){ // down
                from = preserved + con->rdbuf.point;
                to   = preserved;
                size = read_bytes;
                // moving down does not increase the margin
                con->rdbuf.read -= con->rdbuf.point;
            } else { // up
                from = margin;
                margin += con->rdbuf.point;
                to   = margin;
                size = content_read;
            }

            con->rdbuf.buffer       -= preserved;
            con->rdbuf.read         += preserved;
            con->rdbuf._capacity    += preserved;
            con->rdbuf._content_len += content_read;
            con->rdbuf.point         = margin;

            _nfcgi_memmove(con->rdbuf.buffer + to, con->rdbuf.buffer + from, size);
        }
    }

    return 0;
}

int FCGS_StdinRead(FCGS_Connection* con, char *str, size_t n) {
    if(con->flags & FCGSCon_StdinEmpty){
        return -1;
    }

    if((con->flags & FCGSCon_ReadingStdin) == 0){
        _FCGS_RdBufReqStdin(con);
    }

	// the only point of optimization that can happen here is that n < content_len, but content_len > capacity
	// but the most likely usecase for this code is to read a small amount

	for(;;){
		size_t avail = con->rdbuf.read - con->rdbuf.point;
		size_t min = avail < n ? avail : n;
		_nfcgi_memcpy(str, con->rdbuf.buffer + con->rdbuf.point, min);
		con->rdbuf._content_len -= min;
		con->rdbuf.point += min;
		n -= min;
		if(n == 0){
			break;
		}
		if(con->rdbuf._content_len == 0){
			_FCGS_RdBufReqStdin(con);
		}else{
			// read as much as possible
			con->rdbuf.point = 0;
			con->rdbuf.read  = 0;
			size_t r = _NFCGI_OS_Read(con->ipcFD, con->rdbuf.buffer, con->rdbuf._capacity);
			if(r <= 0){
				return -1;
			}
			con->rdbuf.read = r;
		}
    }

    return 0;
}

int FCGS_StdinStore(FCGS_Connection* con, const char* filepath){
    if((con->flags & FCGSCon_ReadingStdin) == 0){
        if(con->flags & FCGSCon_StdinEmpty){
            return -1;
        }
        _FCGS_RdBufReqStdin(con);
    }
    FILE* file = fopen(filepath, "w");
    if(file == NULL){
        return -1;
    }
    for(;;){
        nfcgi_u32 avail;
        if(FCGS_StdinReqBytes(con, 1)){
            break;
        }
        _FCGS_RdBufUDataSize(con, avail);
        fwrite(con->rdbuf.buffer + con->rdbuf.point, 1, avail, file);
        FCGS_StdinAdvance(con, avail);
    }
    fclose(file);
    return 0;
}

int FCGS_StdinAdvance(FCGS_Connection* con, size_t n){
	size_t avail = con->rdbuf.read - con->rdbuf.point;
    avail = avail < con->rdbuf._content_len ? avail : con->rdbuf._content_len;

	if(avail < n){
        return -1;
	}

	con->rdbuf.point += n;
	con->rdbuf._content_len -= n;
	if(con->rdbuf._content_len == 0){
	    con->flags &= ~FCGSCon_ReadingStdin;
	}

    return 0;
}

int FCGS_StdoutReqBytes(FCGS_Connection* con, nfcgi_u32 n){
    if(con->wrbuf.point + n > con->wrbuf.capacity){
        FCGS_StdoutFlush(con);
    }
    return con->wrbuf.capacity > n ? 0 : -1;
}

int FCGS_StdoutWrite(FCGS_Connection* con, const char *str, size_t n) {
    int status = 0;
    if(con->wrbuf.capacity - con->wrbuf.point < n){
        status = FCGS_StdoutFlush(con);
    }
    if(n > con->wrbuf.capacity){
        FCGI_Header hdr;
        FCGI_MakeHeader(&hdr, FCGI_RT_STDOUT, con->activeReqID, n, 0);
        status += _NFCGI_OS_Write(con->ipcFD, &hdr, sizeof(hdr));
        status += _NFCGI_OS_Write(con->ipcFD, str, n);
    }else{
        _nfcgi_memcpy(con->wrbuf.buffer + con->wrbuf.point, str, n);
        con->wrbuf.point += n;
    }
    return status;
}

int FCGS_StdoutFlush(FCGS_Connection* con){
    int offset = -(int)sizeof(FCGI_Header);
    FCGI_Header* hdr = (FCGI_Header*)(con->wrbuf.buffer - sizeof(FCGI_Header));
    FCGI_MakeHeader(hdr, FCGI_RT_STDOUT, con->activeReqID, con->wrbuf.point, 0);
    // extend since we will be writing extra
    con->wrbuf.point += sizeof(FCGI_Header);
    while(con->wrbuf.point){
       int written = _NFCGI_OS_Write(con->ipcFD, con->wrbuf.buffer + offset, con->wrbuf.point);
       if(written < 0){
           return -1;
       }
       offset += written;
       con->wrbuf.point -= written;
    }
    return 0;
}

int _FCGS_FormData_ReadNewEntry(FCGS_Connection* con, FCGS_FDIt* it){
    // read the headers
    it->hdr_size = 0; 
    if(FCGS_StdinReqBytes(con, 4)){
        return FCGS_FD_Err;
    }
    char* p = con->rdbuf.buffer + con->rdbuf.point;
    if(p[0] == '-' && p[1] == '-' && p[2] == '\r' && p[3] == '\n'){
        FCGS_StdinAdvance(con, 4);
        return FCGS_FD_End;
    }

    for(;;){
        const char* hdr_end = "\x0d\x0a\x0d\x0a";
        nfcgi_u32 avail;
        char* found;

        if(FCGS_StdinReqBytes(con, 4)){
            return FCGS_FD_Err;
        }

        _FCGS_RdBufUDataSize(con, avail);

        found = (char*)_nfcgi_memmem(con->rdbuf.buffer + con->rdbuf.point, avail, hdr_end, 4);
        avail = found ? found - con->rdbuf.buffer - con->rdbuf.point : avail - 3;
        it->headers = (char*)_FCGSConfig.realloc(it->headers, it->hdr_size + avail + 1);
        _nfcgi_memcpy(it->headers + it->hdr_size, con->rdbuf.buffer + con->rdbuf.point, avail);
        it->hdr_size += avail;
        it->headers[it->hdr_size] = 0;
        assert(FCGS_StdinAdvance(con, avail) == 0);
        if(found){
            assert(FCGS_StdinAdvance(con, 4) == 0);
            break;
        }
    }

    return FCGS_FD_New;
}

int FCGS_FDIt_Init(FCGS_Connection* con, FCGS_FDIt* it){
    FCGS_ParamIt param_it;
    char* boundary;
    nfcgi_u32 bnd_len;

    _nfcgi_memset(it, 0, sizeof(*it));

    if(_FCGS_RdBufReqStdin(con)){
        return FCGS_FD_Err;
    }

    param_it.__offset = 0;
    if(FCGS_ConEnv_Find(con, &param_it, "CONTENT_TYPE")){
        return FCGS_FD_Err;
    }

    if(_nfcgi_memmem(param_it.value, param_it.valueLen, "multipart/form-data", sizeof("multipart/form-data") - 1) == NULL){
        return FCGS_FD_Err;
    }

    boundary = (char*)_nfcgi_memmem(param_it.value, param_it.valueLen, "boundary=", sizeof("boundary=") - 1);
    if(boundary == NULL){
        return FCGS_FD_Err;
    }

    boundary += sizeof("boundary=") - 1;
    bnd_len = param_it.valueLen - (boundary - param_it.value);

    // every boundary in between entries has a "\r\n" before the boundary
    it->boundary = (char*)_FCGSConfig.realloc(NULL, bnd_len + 4);
    it->boundary[0] = '\r';
    it->boundary[1] = '\n';
    it->boundary[2] = it->boundary[3] = '-';
    _nfcgi_memcpy(it->boundary + 4, boundary, bnd_len);
    it->bnd_len = bnd_len + 4;

    if(FCGS_StdinReqBytes(con, it->bnd_len - 2)){
        return FCGS_FD_Err;
    }
    
    if(_nfcgi_memcmp(con->rdbuf.buffer + con->rdbuf.point, it->boundary + 2, it->bnd_len - 2)){
        return FCGS_FD_Err;
    }

    if(FCGS_StdinAdvance(con, it->bnd_len - 2)){
        return FCGS_FD_Err;
    }

    return _FCGS_FormData_ReadNewEntry(con, it);
}

int FCGS_FDIt_Deinit(FCGS_FDIt* it){
    if(it->headers){
        _FCGSConfig.realloc(it->headers, 0);
    }
    return 0;
}

// returns enum FCGS_FD_
int FCGS_FormData_Iterate(FCGS_Connection* con, FCGS_FDIt* it){
    if(FCGS_StdinReqBytes(con, it->bnd_len + 1)){
        return FCGS_FD_Err;
    }
    nfcgi_u32 avail;
    char* found;
    size_t old_data;
    int code;

    _FCGS_RdBufUDataSize(con, avail);

    found = _nfcgi_memmem(con->rdbuf.buffer + con->rdbuf.point, avail, it->boundary, it->bnd_len);
    old_data = found ? found - con->rdbuf.buffer - con->rdbuf.point : avail - it->bnd_len;
    if(old_data){
        it->fd_start = con->rdbuf.buffer + con->rdbuf.point;
        it->fd_size  = old_data;

        con->rdbuf.point        += old_data;
        con->rdbuf._content_len -= old_data;

        if(it->fd_start[it->fd_size - 1] == 0x0D || it->fd_start[0] == 0x0D) {
            fprintf(stderr, "%s", "found faulty segment!\n");
        }

        return FCGS_FD_Old;
    }else{
        assert(FCGS_StdinAdvance(con, it->bnd_len) == 0);

        if((code = _FCGS_FormData_ReadNewEntry(con, it)) != FCGS_FD_New){
            return code;
        }

        it->fd_start = NULL;
        it->fd_size  = 0;

        return FCGS_FD_New;
    }
}

char* FCGS_FormData_Store(FCGS_Connection* con, const char* path, nfcgi_u32* num_files){
    if(con->flags & FCGSCon_StdinEmpty){
        return NULL;
    }

    char* fname_list  = NULL;
    nfcgi_u32   fname_bufsz = 0;
    *num_files = 0;
    char* pathbuf = NULL;
    nfcgi_u32   pathcap = 0;
    int   code;
    FCGS_FDIt fd_it;

    FILE* cur_file = NULL;

    nfcgi_u32 pathlen = _nfcgi_strlen(path);
    pathcap = pathlen + 1 + (path[pathlen - 1] != '/');
    pathbuf = (char*)_FCGSConfig.realloc(NULL, pathcap);
    _nfcgi_strcpy(pathbuf, path);
    if(pathbuf[pathlen - 1] != '/'){
        pathbuf[pathlen] = '/';
        pathbuf[pathlen + 1] = 0;
        pathlen++;
    }

    for(int code = FCGS_FDIt_Init(con, &fd_it); code != FCGS_FD_End; code = FCGS_FormData_Iterate(con, &fd_it)){
        if(code == FCGS_FD_New){
            if(cur_file){
                fclose(cur_file);
                cur_file = NULL;
            } 
            #define _HDR_NAMEPART_ "filename=\""
            #define _HDR_NAMEPART2_ "name=\""
            char* name_start = (char*)_nfcgi_memmem(fd_it.headers, fd_it.hdr_size, _HDR_NAMEPART_, sizeof(_HDR_NAMEPART_) - 1);
            if(!name_start){
                name_start = (char*)_nfcgi_memmem(fd_it.headers, fd_it.hdr_size, _HDR_NAMEPART2_, sizeof(_HDR_NAMEPART2_) - 1);
            }
            if(name_start){
                name_start += (name_start[0] == 'f' ? sizeof(_HDR_NAMEPART_) : sizeof(_HDR_NAMEPART2_)) - 1;
                char* name_end = _nfcgi_memchr(name_start, '"', fd_it.hdr_size - (name_start - fd_it.headers));
                nfcgi_u32 needed_size = pathlen + (name_end - name_start) + 1;
                if(needed_size > pathcap){
                    pathcap = needed_size;
                    pathbuf = (char*)_FCGSConfig.realloc(pathbuf, needed_size);
                }
                _nfcgi_memcpy(pathbuf + pathlen, name_start, name_end - name_start);
                pathbuf[needed_size - 1] = 0;
                fname_list = (char*)_FCGSConfig.realloc(fname_list, fname_bufsz + needed_size);
                _nfcgi_strcpy(fname_list + fname_bufsz, pathbuf);
                fname_bufsz += needed_size;
                cur_file = fopen(pathbuf, "wb");
                if(cur_file == NULL){
                    _FCGSConfig.realloc(pathbuf, 0);
                    return NULL;
                }
                (*num_files)++;
            }else{
                _FCGSConfig.realloc(pathbuf, 0);
                return NULL;
            }
            #undef _HDR_NAMEPART_
            #undef _HDR_NAMEPART2_
        }else if (code == FCGS_FD_Err){
            if(cur_file){
                fclose(cur_file);
            }

            FCGS_FDIt_Deinit(&fd_it);
            _FCGSConfig.realloc(pathbuf, 0);
            return NULL;
        }else {
            fwrite(fd_it.fd_start, 1, fd_it.fd_size, cur_file);
        }
    }

    if(cur_file){
        fclose(cur_file);
    }

    FCGS_FDIt_Deinit(&fd_it);
    _FCGSConfig.realloc(pathbuf, 0);

    return fname_list;
}

int _FCGS_RdBufReqBytes(FCGS_Connection* con, nfcgi_u32 n){
    nfcgi_u32 avail = con->rdbuf.read - con->rdbuf.point;
    if(avail >= n){ // fast path
        return 0;
    }
    if(n > con->rdbuf._capacity){ // unable to fit
        return -1;
    }
    if(con->rdbuf._capacity - con->rdbuf.point < n){ // unable to fit, unless unload what has been read
		if(con->rdbuf.point == con->rdbuf.read){
		    con->rdbuf.read = 0;
		}else{
			_nfcgi_memcpy(con->rdbuf.buffer, con->rdbuf.buffer + con->rdbuf.point, avail);
		}
		con->rdbuf.point = 0;
    }
    for(;;){
        int count = _NFCGI_OS_Read(con->ipcFD, con->rdbuf.buffer + con->rdbuf.point, con->rdbuf._capacity - avail);
        if(count <= 0){ // 0 is returned on EOF and <0 on error
            return -1;
        }
        avail += count;
        con->rdbuf.read += count;
        if(avail >= n){
            break;
        }
    }

    return 0;
}

int _FCGS_RdBufSkipBytes(FCGS_Connection* con, nfcgi_u32 n){
    nfcgi_u32 avail = con->rdbuf.read - con->rdbuf.point;
    if(avail > n){
        con->rdbuf.point += n;
        return 0;
    }
    n -= avail;
    con->rdbuf.point = 0;
    con->rdbuf.read  = 0;
    int read_bytes;
    for(;;){
        read_bytes = _NFCGI_OS_Read(con->ipcFD, con->rdbuf.buffer, con->rdbuf._capacity);
        if(read_bytes < 0){
            return -1;
        } else if((nfcgi_u32)read_bytes > n){
            break;
        }
        n -= read_bytes;
    }
    con->rdbuf.point = n;
    con->rdbuf.read  = read_bytes;

    return 0;
}

int _FCGS_RdBufReqStdin(FCGS_Connection* con){
    for(;;){
        if(_FCGS_RdBufReqBytes(con, sizeof(FCGI_Header) + con->_stdinpadd)){
            return -1;
        }
        con->rdbuf.point += con->_stdinpadd;
        con->_stdinpadd   = 0;
        FCGI_RecordMeta meta;
        meta.header = *(FCGI_Header*)(con->rdbuf.buffer + con->rdbuf.point);
        // NOTE: the content length includes the body inside the meta->body, but does not include the header
        con->rdbuf.point += sizeof(FCGI_Header);
        con->rdbuf._content_len = (meta.header.contentLengthB1 << 8) | meta.header.contentLengthB0;
        nfcgi_u32 recordBodySize = con->rdbuf._content_len + meta.header.paddingLength;

        nfcgi_u32 t = (meta.header.requestIdB1 << 8) | meta.header.requestIdB0;

        if(t == 0){ // Management
            if(meta.header.type != FCGI_RT_GET_VALUES){
                FCGI_RecordMeta record;
                FCGI_MakeUnknownTypeRecord(&record, 0, meta.header.type);
                _NFCGI_OS_Write(con->ipcFD, (const char*)&record, sizeof(record));
                _FCGS_RdBufSkipBytes(con, recordBodySize);
                continue;
            }
            // NOTE: the exact same format as the PARAMS
            size_t totalLength = sizeof(FCGI_Header);
            for(int i = 0; i < FCGIMgmt_MAX; i++){
                // NOTE: Using 4-bytes lengths for simplicity and extensibility
                totalLength += 8 + _nfcgi_fmtlen("%s%u", g_FCGIMgmtValueNames[i], _FCGSConfig.mgmtValues[i]);
            }
            totalLength += _NFCGI_FMTLEN_NULLBYTE;
            char buf[NFCGI_Const_GVR_BUF_MAX];
            assert(totalLength <= NFCGI_Const_GVR_BUF_MAX);
            char* p = buf;
            FCGI_Header* hdr = (FCGI_Header*)p;
            FCGI_MakeHeader(hdr, FCGI_RT_GET_VALUES_RESULT, 0, totalLength, 0);
            p += sizeof(FCGI_Header);
            for(int i = 0; i < FCGIMgmt_MAX; i++){
                *(nfcgi_u32*)p = (1 << 31) | _nfcgi_strlen(g_FCGIMgmtValueNames[i]);
                p += 4;
                int val = _FCGSConfig.mgmtValues[i];
                // still better than snprintf
                val = val >= 10000 ? 5 : val >= 1000 ? 4 : val >= 100 ? 3 : val >= 10 ? 2 : 1;
                *(nfcgi_u32*)p = (1 << 31) | val;
                p += 4;
                p += _nfcgi_strfmt(p, "%s%u", g_FCGIMgmtValueNames[i], _FCGSConfig.mgmtValues[i]);
            }
            _NFCGI_OS_Write(con->ipcFD, buf, totalLength);
        } else if(con->activeReqID && t != con->activeReqID){
            _FCGS_RdBufSkipBytes(con, recordBodySize);
        }else {
            //fprintf(stderr, "type: %u\n", meta.header.type);
            switch(meta.header.type){
                case FCGI_RT_BEGIN_REQUEST:
                    // NOTE: only care about the body for the FCGI_RT_BEGIN_REQUEST
                    if(_FCGS_RdBufReqBytes(con, sizeof(meta.body))){
                        return -1;
                    }
                    meta.body.begin_request = *(FCGI_BeginRequestBody*)(con->rdbuf.buffer + con->rdbuf.point);
                    con->activeReqID = t;
                    con->role = (meta.body.begin_request.roleB1 << 8) | meta.body.begin_request.roleB0;
                    // refuse to filter
                    if(con->role == FCGIRole_Filter){
                        FCGI_RecordMeta record;
                        FCGI_MakeEndRequestRecord(&record, t, 0, FCGI_ERS_UNKNOWN_ROLE);
                        _NFCGI_OS_Write(con->ipcFD, (const char*)&record, sizeof(record));
                        con->activeReqID = 0;
                        con->role = FCGIRole_NONE;
                    }
                    _FCGS_RdBufSkipBytes(con, recordBodySize);
                break;
                case FCGI_RT_ABORT_REQUEST:
                    con->activeReqID = 0;
                    con->role        = FCGIRole_NONE;
                    _FCGS_RdBufSkipBytes(con, recordBodySize);
                    {
                        FCGI_RecordMeta record;
                        FCGI_MakeEndRequestRecord(&record, t, 0, FCGI_ERS_REQUEST_COMPLETE);
                        _NFCGI_OS_Write(con->ipcFD, (const char*)&record, sizeof(record));
                    }
                break;
                case FCGI_RT_PARAMS: // copy all of these records, they're used as-is
                    for(;;){ // for every params record
                        if(con->rdbuf._content_len == 0){
                            _FCGS_RdBufSkipBytes(con, recordBodySize);
                            break;
                        }
                        nfcgi_u32 avail   = con->rdbuf.read - con->rdbuf.point;
                        nfcgi_u32 process = avail < con->rdbuf._content_len ? avail : con->rdbuf._content_len;
                        con->envbuf = _FCGSConfig.realloc(con->envbuf, con->_envbufsz + con->rdbuf._content_len);
                        _nfcgi_memcpy(con->envbuf + con->_envbufsz, con->rdbuf.buffer + con->rdbuf.point, process);
                        con->_envbufsz += process;
                        con->rdbuf._content_len -= process;
                        con->rdbuf.point        += process;
                        while(con->rdbuf._content_len){
                            process = _NFCGI_OS_Read(con->ipcFD, con->envbuf + con->_envbufsz, con->rdbuf._content_len);
                            if(process < 0){
                                return -1;
                            }
                            con->_envbufsz += process;
                            con->rdbuf._content_len -= process;
                        }
                        _FCGS_RdBufSkipBytes(con, meta.header.paddingLength);
                        _FCGS_RdBufReqBytes(con, sizeof(FCGI_Header));
                        meta.header = *(FCGI_Header*)(con->rdbuf.buffer + con->rdbuf.point);
                        con->rdbuf.point       += sizeof(FCGI_Header);
                        con->rdbuf._content_len = (meta.header.contentLengthB1 << 8) | meta.header.contentLengthB0;
                        recordBodySize          = meta.header.paddingLength + con->rdbuf._content_len;
                    }
                break;
                case FCGI_RT_STDIN:
                    con->rdbuf._content_len = (meta.header.contentLengthB1 << 8) | meta.header.contentLengthB0;
                    con->_stdinpadd = meta.header.paddingLength;
                    // STDIN is a stream record, an entry with `content_length` of 0 means the end
                    if(con->rdbuf._content_len == 0){
                        con->flags |= FCGSCon_StdinEmpty;
                        return -1;
                    }else{
                        con->flags |= FCGSCon_ReadingStdin;
                        return 0;
                    }
                break;
                default: /* FCGI_RT_DATA and any other */
                    _FCGS_RdBufSkipBytes(con, recordBodySize);
                    {
                        FCGI_RecordMeta record;
                        FCGI_MakeUnknownTypeRecord(&record, t, meta.header.type);
                        _NFCGI_OS_Write(con->ipcFD, (const char*)&record, sizeof(record));
                    }
                break;
            }
        }
    }
    return -1;
}

