#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "http_parse.h"
#include "websock.h"
#include "mm_err.h"

//I'm just using this as driver code to feed test files into the library.

int main() {    
    mm_err err = MM_SUCCESS;
    http_req *res = new_http_req(&err);
    
    char buf[80];
    int num;
    while ((num = read(STDIN_FILENO, buf, sizeof(buf))) > 0) {
        int rc = write_to_http_parser(res, buf, num, &err);
        
        if (rc == 0) {
            fprintf(stderr, "Parsed a request!\n");
            if (is_websock_request(res, &err)) {
                fprintf(stderr, "It's actually a websocket request!\n");
                char * resp = websock_handshake_response(res, NULL, &err);
                //printf("Response:\n%s\n", resp);
                printf("%s", resp);
                fflush(stdout);
            }
            fprintf(stderr, "\tMethod = %s\n", http_req_strs[res->req_type]);
            fprintf(stderr, "\tPath = [%s]\n", res->path);
            int i;
            for (i = 0; i < res->num_hdrs; i++) {
                fprintf(stderr, "\t\t[%s] = [%s]\n", res->hdrs[i].name, res->hdrs[i].args);
            }
            
            fprintf(stderr, "\tPayload length = %d\n", res->payload_len);
        } else if (rc < 0) {
            //Error
            break;
        }
    }
    
    del_http_req(res);
    
    if (err != MM_SUCCESS) {
        fprintf(stderr, "Error: %s\n", err);
    }
    
    return 0;
}
