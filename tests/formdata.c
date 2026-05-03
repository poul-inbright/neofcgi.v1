#include "../include/neofcgi/server.h"
#include <stdio.h>
#include <assert.h>

const char* SAVE_FOLDER = "formdata_test_files/";

char* nfcgi_getcwd(){
    char* buffer;

    #ifdef __NFCGI_WINDOWS__
        nfcgi_u32 needed_size = GetCurrentDirectoryA(0, NULL) + 1;
        buffer = _nfcgi_realloc(NULL, needed_size);
        GetCurrentDirectoryA(needed_size, buffer);
    #else
        buffer = _nfcgi_realloc(NULL, PATH_MAX);
        assert(getcwd(pathbuf, PATH_MAX) != NULL);
    #endif

    return buffer;
}

#ifdef __NFCGI_WINDOWS__
    #define nfcgi_mkdir(name) CreateDirectoryA(name, NULL)
#else
    #define nfcgi_mkdir(name) mkdir(name, 0777)
#endif

int main(int argc, char** argv){
	if(FCGS_DefaultInit()){
        fprintf(stderr, "%s", "FCGS_DefaultInit() failed!\n");
        return -1;
    }

    fsock_t sock = FCGS_CreateSocket(2727, 100);

    fprintf(stderr, "%s", "Trying to listen on 2727...\n");

    char* cwd = nfcgi_getcwd();
    
    fprintf(stderr, "Formdata parts will be stored as files in the folder '%s' under '%s'\n", SAVE_FOLDER, cwd);
    nfcgi_mkdir(SAVE_FOLDER);

    _nfcgi_realloc(cwd, 0);
    
    char* files;
    nfcgi_u32   num;

    FCGS_Connection connection;
    FCGS_ConInit(&connection, 0, 0);

    int status = FCGS_ConAccept(&connection, sock);
    fprintf(stderr, "FCGX_Accept(): %d\n", status);
    if(status){
        return NULL;
    }

    if((files = FCGS_FormData_Store(&connection, SAVE_FOLDER, &num)) == NULL){
        fprintf(stderr, "%s", "FCGX_FormData_Store(): failed!\n");
    }else{
        char* p = files;
        for(nfcgi_u32 i = 0; i < num; i++){
            fprintf(stderr, "%s\n", p);
            p += strlen(p) + 1;
        }
    }

	FCGS_StdoutWrite(&connection, NFCGI_SERVER_DEFAULT_STDOUT_HTML, sizeof(NFCGI_SERVER_DEFAULT_STDOUT_HTML) - 1);
    FCGS_ConFinish(&connection, 0);
    _nfcgi_realloc(files, 0);

    FCGS_Deinit();

	return 0;
}