#ifndef HTTP_PARSE_H
#define HTTP_PARSE_H 1

#include "mm_err.h"

//Typical HTTP request:
/*
GET / HTTP/1.1
Host: localhost:2345
Connection: keep-alive
Cache-Control: max-age=0
Upgrade-Insecure-Requests: 1
User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/83.0.4103.61 Safari/537.36
Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*\/*;q=0.8,application/signed-exchange;v=b3;q=0.9
Sec-Fetch-Site: none
Sec-Fetch-Mode: navigate
Sec-Fetch-User: ?1
Sec-Fetch-Dest: document
Accept-Encoding: gzip, deflate, br
Accept-Language: en-US,en;q=0.9,fr-CA;q=0.8,fr;q=0.7
*/

typedef enum _http_req_t {
    GET,
    POST,
    HEAD
} http_req_t;

typedef struct _http_hdr {
    char *name; //Always converted to lower-case
    char *args; //Can use strtok with "," as delimiter to iterate through
} http_hdr;

#define HTTP_MAX_HDRS 32
typedef struct _http_pkt {
    http_req_t req_type;
    char *path;
    http_hdr hdrs[HTTP_MAX_HDRS];
    
    int payload_len;
    char *payload;
} http_pkt;



#endif
