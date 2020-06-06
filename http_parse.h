#ifndef HTTP_PARSE_H
#define HTTP_PARSE_H 1

//Credit to https://www.jmarshall.com/easy/http/ who does a great job of
//explaining HTTP. MDN and the RFCs are needlessly overcomplicated.

#include <stdlib.h>
#include "mm_err.h"

//Typical HTTP request:
/*
GET / HTTP/1.1
Host: localhost:2345
Connection: keep-alive
Cache-Control: max-age=0
Upgrade-Insecure-Requests: 1
User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/83.0.4103.61 Safari/537.36
Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*SLASH*;q=0.8,application/signed-exchange;v=b3;q=0.9
Sec-Fetch-Site: none
Sec-Fetch-Mode: navigate
Sec-Fetch-User: ?1
Sec-Fetch-Dest: document
Accept-Encoding: gzip, deflate, br
Accept-Language: en-US,en;q=0.9,fr-CA;q=0.8,fr;q=0.7
*/

MM_ERR(HTTP_BAD_METHOD, "http library only accepts GET, HEAD, and POST");
MM_ERR(HTTP_MISSING_PATH, "URI path not given in request status line");
MM_ERR(HTTP_MISSING_PROTOCOL, "HTTP protocol not given in request status line");
MM_ERR(HTTP_BAD_PROTOCOL, "malformed HTTP protocol string");
MM_ERR(HTTP_MISSING_HDR, "header argument without header field");
MM_ERR(HTTP_NOT_IMPL, "function not implemented");
MM_ERR(HTTP_NULL_ARG, "NULL argument given but non-NULL expected");
MM_ERR(HTTP_INVALID_ARG, "invalid argument");
MM_ERR(HTTP_INVALID_STATE, "http parser in invalid state (must reset!)");
MM_ERR(HTTP_OOM, "out of memory");

typedef enum _http_req_t {
    HTTP_GET,
    HTTP_POST,
    HTTP_HEAD
} http_req_t;

typedef struct _http_hdr {
    char *name; //Always converted to lower-case
    char *args; //Can use strtok with "," as delimiter to iterate through
} http_hdr;

typedef enum req_parse_state_t {
    HTTP_STATUS_LINE,
    HTTP_HDR,
    HTTP_HDR_ARGS,
    HTTP_PAYLOAD
} req_parse_state_t;

#define HTTP_MAX_HDRS 32
typedef struct _http_req {
    http_req_t req_type;
    char *path;
    int num_hdrs;
    http_hdr hdrs[HTTP_MAX_HDRS];
    
    int cnx_closed;
    
    int payload_len;
    char *payload;
    
    //Internal fields. Don't touch!
    struct {
        //Parser state
        req_parse_state_t state;
        
        //Saved memory
        char *base;
        //Next write location in base
        int pos;
        //Number of (total) bytes allocated in base
        int cap;
        
        //To (significantly) simplify parsing code, we will process text 
        //line-by-line. This index keeps track of the beginning of the
        //current line (for example, if we need to read() more bytes to get
        //to the end)
        int line;
    } __internal;
} http_req;


//Static functions
#ifdef MM_IMPLEMENT
//Expand memory inside an http_req struct.
static void expand_mem(http_req *res, mm_err *err) {
    if (*err != MM_SUCCESS) return;
    
    char *new_base = realloc(res->__internal.base, res->__internal.cap * 2);
    if (!new_base) {
        *err = HTTP_OOM;
        return;
    }
    
    res->__internal.base = new_base;
    res->__internal.cap *= 2;
}

//Process a single line from an HTTP request. Updates the internal write
//position for new data. If the line is empty, returns 1, otherwise 0. 
//Returns negative on error
static int process_line(http_req *res, mm_err *err) {
    
}
#endif


//Returns 0 if a complete request has been seen. This indicates to the user
//that the information in *res can now be used. Returns positive if no 
//errors have occurred, but a complete request has not yet been see.
//Otherwise, returns negative. 
//
//EXTRA DETAILS: 
//If res points to a structure that was previously filled by supply_req_data
//when a new request is being parsed, its memory will be reused. This 
//improves performance, but it means that you can't use an old http_req
//after you start parsing a new request.
int supply_req_data(http_req *res, char *buf, int len, mm_err *err)
#ifdef MM_IMPLEMENT
{
    if (*err != MM_SUCCESS) return -1;
    
    //Sanity check inputs
    if (res == NULL || (len > 0 && buf == NULL)) {
        *err = HTTP_NULL_ARG;
        return -1;
    } else if (len <= 0) {
        *err = HTTP_INVALID_ARG;
        return -1;
    }
    
    *err = HTTP_NOT_IMPL;
    return -1;
}
#else
;
#endif

#endif
