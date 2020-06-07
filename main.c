#include <stdio.h>
#include <unistd.h>
#include "http_parse.h"
#include "mm_err.h"

//I'm just using this as driver code to feed test files into the library.

int main() {
    puts("Hello world!\n");
    
    mm_err err = MM_SUCCESS;
    http_req *res = new_http_req(&err);
    
    char buf[80];
    int num;
    while ((num = read(STDIN_FILENO, buf, sizeof(buf))) > 0) {
        int rc = write_to_http_parser(res, buf, num, &err);
        
        if (rc == 0) {
            puts("Parsed a request!");
            printf("\tMethod = %s\n", http_req_strs[res->req_type]);
            printf("\tPath = [%s]\n", res->path);
            int i;
            for (i = 0; i < res->num_hdrs; i++) {
                printf("\t\t[%s] = [%s]\n", res->hdrs[i].name, res->hdrs[i].args);
            }
            
            printf("\tPayload length = %d\n", res->payload_len);
            fflush(stdout);
        } else if (rc < 0) {
            //Error
            break;
        }
    }
    
    del_http_req(res);
    
    if (err != MM_SUCCESS) {
        printf("Error: %s\n", err);
    }
    
    return 0;
}
