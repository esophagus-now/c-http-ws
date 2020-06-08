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
    websock_pkt *pkt = new_websock_pkt(&err);
    
    char buf[80];
    int num;
    
    int http = 1;
    
    while ((num = read(STDIN_FILENO, buf, sizeof(buf))) > 0) {
        int rc;
        if (http) rc = write_to_http_parser(res, buf, num, &err);
        else rc = write_to_websock_parser(pkt, buf, num, &err);
        
        if (rc == 0) {
            if(http) {
                fprintf(stderr, "Parsed a request!\n");
                if (is_websock_request(res, &err)) {
                    fprintf(stderr, "It's actually a websocket request!\n");
                    char * resp = websock_handshake_response(res, NULL, &err);
                    //printf("Response:\n%s\n", resp);
                    printf("%s", resp);
                    fflush(stdout);
                    
                    http = 0;
                }
                fprintf(stderr, "\tMethod = %s\n", http_req_strs[res->req_type]);
                fprintf(stderr, "\tPath = [%s]\n", res->path);
                int i;
                for (i = 0; i < res->num_hdrs; i++) {
                    fprintf(stderr, "\t\t[%s] = [%s]\n", res->hdrs[i].name, res->hdrs[i].args);
                }
                
                fprintf(stderr, "\tPayload length = %d\n", res->payload_len);
            } else {
                fprintf(stderr, "Parsed a websockets message!\n");
                fprintf(stderr, "Opcode = [%s]\n", websock_pkt_type_strs[pkt->type]);
                fprintf(stderr, "FIN = [%d]\n", pkt->fin);
                fprintf(stderr, "Payload len = [%lu]\n", pkt->payload_len);
                fprintf(stderr, "Masking key = %02x%02x%02x%02x\n",
                    pkt->__internal.mask[0] & 0xFF,
                    pkt->__internal.mask[1] & 0xFF,
                    pkt->__internal.mask[2] & 0xFF,
                    pkt->__internal.mask[3] & 0xFF
                );
                int i;
                for (i = 0; i < pkt->payload_len; i++) {
                    fprintf(stderr, "%02x ", pkt->payload[i] & 0xFF);
                }
                
                if (pkt->type == WEBSOCK_CLOSE) {
                    //Answer with a close frame
                    char close_pkt[WEBSOCK_MAX_HDR_SIZE];
                    int len = construct_websock_hdr(close_pkt, WEBSOCK_CLOSE, 1, 0, &err);
                    if (err == MM_SUCCESS) {
                        fwrite(close_pkt, len, 1, stdout);
                        fflush(stdout);
                        break;
                    }
                }
            }
        } else if (rc < 0) {
            //Error
            break;
        }
    }
    
    del_websock_pkt(pkt);
    del_http_req(res);
    
    if (err != MM_SUCCESS) {
        fprintf(stderr, "Error: %s\n", err);
    }
    
    fprintf(stderr, "\nDone\n");
    
    return 0;
}
