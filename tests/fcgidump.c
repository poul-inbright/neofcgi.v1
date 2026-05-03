#include "../include/neofcgi/server.h"
#include <stdio.h>
#include <stdarg.h>

#define LOG(fmt, ...) fprintf(stderr, "[LOG] " fmt, __VA_ARGS__)

void FCGSD_Hexdump(FILE* file, const void* _buf, size_t size, size_t line_len){
	const char* buf = (const char*)_buf;
    while(size >= line_len){
        for(nfcgi_u32 i = 0; i < line_len; i++){
            fprintf(file, "%02hhx ", buf[i]);
        }
        fputc('|', file);
        fputc(' ', file);
        for(nfcgi_u32 i = 0; i < line_len; i++){
            fprintf(file, "%c", buf[i] > 0x1F && buf[i] <= 0x7F ? buf[i] : '.');
        }
        fputc('\n', file);
        size -= line_len;
        buf += line_len;
    }
    if(size){
        for(nfcgi_u32 i = 0; i < size; i++){
            fprintf(file, "%02hhx ", buf[i]);
        }
        for(nfcgi_u32 i = size; i < line_len; i++){
            fprintf(file, "%s", "   ");
        }
        fputc('|', file);
        fputc(' ', file);
        for(nfcgi_u32 i = 0; i < size; i++){
            fprintf(file, "%c", buf[i] > 0x1F && buf[i] <= 0x7F ? buf[i] : '.');
        }
        fputc('\n', file);
    }
}


int main(int argc, char** argv){
	if(FCGS_DefaultInit()){
        fprintf(stderr, "%s", "FCGS_DefaultInit() failed!\n");
        return -1;
	}

	int status;
    fsock_t sock = FCGS_CreateSocket(2727, 100);
    LOG("FCGS_CreateSocket(): %d\n", sock);
    if(sock < 0){
        return;
    }

    LOG("%s", "Trying to listen on 2727...\n");

    FCGS_Connection connection;
    FCGS_ConInit(&connection, 0, 0);
    status = FCGS_ConAccept(&connection, sock);
    LOG("FCGX_Accept(): %d\n", status);
    if(status){
        return;
    }

    FCGS_ParamIt paramIt;
    paramIt.__offset = 0;

    _FCGS_RdBufReqStdin(&connection);

    while(FCGS_ConEnv_Iterate(&connection, &paramIt)){
        fwrite(paramIt.name, paramIt.nameLen, 1, stdout);
        fputc('=', stdout);
        fwrite(paramIt.value, paramIt.valueLen, 1, stdout);
        fputc('\n', stdout);
    }

    LOG("content_length = %u\n", connection.rdbuf._content_len);

    for(;;){
        if(FCGS_StdinReqBytes(&connection, 1)){
            break;
        }
        nfcgi_u32 avail = connection.rdbuf.read - connection.rdbuf.point;
		avail = avail < connection.rdbuf._content_len ? avail : connection.rdbuf._content_len;
        char* p = connection.rdbuf.buffer + connection.rdbuf.point;
        FCGSD_Hexdump(stdout, p, avail, 64);
		FCGS_StdinAdvance(&connection, avail);
    }
    FCGS_StdoutWrite(&connection, NFCGI_SERVER_DEFAULT_STDOUT_HTML, sizeof(NFCGI_SERVER_DEFAULT_STDOUT_HTML) - 1);

    FCGS_ConFinish(&connection, 0);

    FCGS_Deinit();

    return 0;
}