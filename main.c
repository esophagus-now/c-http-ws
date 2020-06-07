#include <stdio.h>
#include <unistd.h>
#include "http_parse.h"
#include "mm_err.h"

int main() {
    puts("Hello world!\n");
    
    #define test_req \
        "GET / HTTP/1.1\r\n"\
        "Host: localhost:2345\r\n"\
        "Connection: keep-alive\r\n"\
        "Cache-Control: max-age=0\r\n"\
        "Upgrade-Insecure-Requests: 1\r\n"\
        "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/83.0.4103.61 Safari/537.36\r\n"\
        "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.9\r\n"\
        "Sec-Fetch-Site: none\r\n"\
        "Sec-Fetch-Mode: navigate\r\n"\
        "Sec-Fetch-User: ?1\r\n"\
        "Sec-Fetch-Dest: document\r\n"\
        "Accept-Encoding: gzip, deflate, br\r\n"\
        "Accept-Language: en-US,en;q=0.9,fr-CA;q=0.8,fr;q=0.7\r\n"\
        "\r\n"
    
    mm_err err = MM_SUCCESS;
    http_req *res = new_http_req(&err);
    int rc = supply_req_data(res, test_req, sizeof(test_req)-1, &err);
    
    if (rc == 0) {
        puts("Parsed a request!");
        printf("\tMethod = %s\n", http_req_strs[res->req_type]);
        printf("\tPath = [%s]\n", res->path);
        int i;
        for (i = 0; i < res->num_hdrs; i++) {
            printf("\t\t[%s] = [%s]\n", res->hdrs[i].name, res->hdrs[i].args);
        }
        
        printf("\tPayload length = %d\n", res->payload_len);
    }
    
    del_http_req(res);
    
    if (err != MM_SUCCESS) {
        printf("Error: %s\n", err);
    }
    
    return 0;
}
