#ifndef HTTP_PARSE_H
#define HTTP_PARSE_H 1

//Credit to https://www.jmarshall.com/easy/http/ who does a great job of
//explaining HTTP. MDN and the RFCs are needlessly overcomplicated.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
MM_ERR(HTTP_FOLD_NO_HDR, "folded header argument with no prior header field");
MM_ERR(HTTP_BAD_HDR, "HTTP header has bad syntax");
MM_ERR(HTTP_CONTENT_LENGTH_UNSPECIFIED, "HTTP Content-Length unspecified");
MM_ERR(HTTP_CHUNKED_NOT_SUPPORTED, "this server does not support chunked transfers");
MM_ERR(HTTP_INVALID_CONTENT_LENGTH, "invalid argument for Content-Length");
MM_ERR(HTTP_NOT_IMPL, "function not implemented");
MM_ERR(HTTP_NULL_ARG, "NULL argument given but non-NULL expected");
MM_ERR(HTTP_INVALID_ARG, "invalid argument");
MM_ERR(HTTP_INVALID_STATE, "http parser in invalid state (must reset!)");
MM_ERR(HTTP_OOM, "out of memory");
MM_ERR(HTTP_IMPOSSIBLE, "HTTP parsing code reached location Marco thought was impossible");

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
//Resets everything in an http_req, except for __internal.base and 
//__internal.cap. Assumes h is non-NULL.
static void reset_http_req(http_req *h) {
    h->num_hdrs = 0;
    h->payload_len = -1;
    h->__internal.state = HTTP_STATUS_LINE;
    h->__internal.pos = 0;
    h->__internal.line = 0;
}

//Expand memory inside an http_req struct. Makes sure the resulting expanded
//block is at least min_sz bytes
//NOTE: does not check if res is non-NULL
static void expand_mem_to(http_req *res, int min_sz, mm_err *err) {
    if (*err != MM_SUCCESS) return;
    
    //Quit early if no expansion needed
    if (res->__internal.cap >= min_sz) return;
    
    //Resize the memory buffer
    char *new_base = realloc(res->__internal.base, res->__internal.cap * 2);
    if (!new_base) {
        *err = HTTP_OOM;
        return;
    }
    
    //Update internal bookkeeping
    res->__internal.base = new_base;
    res->__internal.cap *= 2;
}

#define WS_CHARS " \t"
//Helper function to advance a pointer past whitespace
static void skip_ws(char **str) {
    int num_spaces = strspn(*str, WS_CHARS);
    *str += num_spaces;
}

//Helper function to convert a raw list of args from an HTTP header into a
//more manageable format. Example:
//
//  my first arg  , my=second+arg,        third
//
//should become
//
//  my first arg  ,my=second+arg,third
//
//Returns number of characters in scrunched line, **INCLUDING** the NUL
static int scrunch_args(char *line) {
    int rd_pos = 0, wr_pos = 0;
    
    while (line[rd_pos]) {
        //Read up until the comma
        //"Premature optimization is the root of all evil"
        if (rd_pos != wr_pos) {
            while (line[rd_pos] && line[rd_pos] != ',') {
                line[wr_pos++] = line[rd_pos++];
            }
        } else {
            while (line[rd_pos] && line[rd_pos] != ',') {
                rd_pos++; wr_pos++;
            }
        }
        
        //Skip whitespace
        while (line[rd_pos] == ' ' || line[rd_pos] == '\t') rd_pos++;
    }
    
    line[wr_pos++] = '\0';
    return wr_pos;
}

//Process a single line from an HTTP request. Updates the internal write
//position for new data. If the line is empty, returns 1, otherwise 0. 
//Returns negative on error
//
//EXTRA DETAILS: the caller (i.e. supply_req_data) must process the input
//string to 1) remove carriage returns and 2) convert line feeds to NULs.
static int process_line(http_req *res, mm_err *err) {
    if (*err != MM_SUCCESS) return -1;
    
    char *line = res->__internal.base + res->__internal.line;
    char *line_end = res->__internal.base + res->__internal.pos;
    
    switch(res->__internal.state) {
    case HTTP_STATUS_LINE: {
        //Start by reading the request type
        //Not efficient, but who cares?
        if (strncmp("GET ", line, 4) == 0) {
            res->req_type = HTTP_GET;
            line += 4;
        } else if (strncmp("HEAD ", line, 5) == 0) {
            res->req_type = HTTP_HEAD;
            line += 5;
        } else if (strncmp("POST ", line, 5) == 0) {
            res->req_type = HTTP_POST;
            line += 5;
        } else {
            *err = HTTP_BAD_METHOD;
            return -1;
        }
        
        skip_ws(&line);
        //Next get the path
        if (line >= line_end) {
            *err = HTTP_MISSING_PATH;
            return -1;
        }
        //Unforgivable hack: until we return the struct to the user, the
        //pointer fields will only store offsets. We'll add the base back on
        //when we're sure there will not be any more realloc()s.
        res->path = line - (unsigned long) res->__internal.base;
        //Find the end of this word and place a NUL byte. 
        //WARNING WARNING WARNING this code does not convert %20 into a
        //space character
        int path_len = strcspn(line, " \t");
        line[path_len] = '\0';
        line += path_len + 1;
        
        //Finally, make sure the protocol is one that we expect
        skip_ws(&line);
        if (line >= line_end) {
            *err = HTTP_MISSING_PROTOCOL;
            return -1;
        }
        //I'm taking a little shortcut here: no one ever has spaces inside
        //their "HTTP/1.0" or "HTTP/1.1" string
        if (strncmp("HTTP/1.0", line, 8) || strncmp("HTTP/1.1", line, 8)) {
            *err = HTTP_BAD_PROTOCOL;
            return -1;
        }
        //Laziness: don't bother checking if there is extra garbage on this line
        
        res->__internal.state = HTTP_HDR;
        res->__internal.pos = res->__internal.line; //We can reuse this memory
        return 0;
    }
    case HTTP_HDR: {
        //If this line is empty, we move on to reading the payload. This 
        //assumes that the caller has properly processed newlines.
        if (line[0] == '\0') {
            if (res->payload_len < 0) {
                //This happens if no Content-Length was given. This is only
                //a problem for POST requests
                if (res->req_type == HTTP_POST) {
                    *err = HTTP_CONTENT_LENGTH_UNSPECIFIED;
                    return -1;
                }
                res->payload_len = 0;
                res->__internal.state = HTTP_STATUS_LINE;
                return 1;
            } else if (res->payload_len == 0) {
                //Oddball case, but possible I guess
                res->__internal.state = HTTP_STATUS_LINE;
                return 1;
            } else {
                res->__internal.state = HTTP_PAYLOAD;
                return 1;
            }
        }
        
        //Here's where things get thorny. If this line begins with whitespace,
        //it is a folded argument.
        else if (line[0] == ' ' || line[0] == '\t') {
            if (res->num_hdrs <= 0) {
                *err = HTTP_FOLD_NO_HDR;
                return -1;
            }
            
            //Undo NUL at end of last arg list and replace with comma
            line[-1] = ','; //Looks pretty nasty!
            int length = scrunch_args(line);
            res->__internal.pos = res->__internal.line + length;
            return 0;
        }
        
        //Otherwise, do our normal header processing
        int hdr_len = strcspn(line, " \t:");
        //Mark the NUL at the end of the header string
        line[hdr_len] = '\0';
        //Hang onto this for later
        char *hdr_str = line;
        
        line += hdr_len + 1; //Skip past the NUL byte
        
        //Skip to beginning of args
        int num_to_skip = strspn(line, " \t:");
        line += num_to_skip;
        //Also hang onto this
        char *args_str = line;
        //Process args
        int args_len = scrunch_args(line);
        
        //Update entries in http_req struct
        //This uses the same ugly hack of storing offsets instead of 
        //absolute addresses (since they will be "post-processed" later)
        http_hdr *hdr = res->hdrs + res->num_hdrs++;
        hdr->name = hdr_str - (unsigned long) res->__internal.base;
        hdr->args = args_str - (unsigned long) res->__internal.base;
        //Make sure pos points to one after the end
        res->__internal.pos = res->__internal.line + hdr_len + args_len + 1;
        
        //As a last step, look for headers used for parsing payload
        if (strcmp("Content-Length", hdr_str) == 0) {
            int rc = sscanf(args_str, "%d", &res->payload_len);
            if (rc != 1) {
                *err = HTTP_INVALID_CONTENT_LENGTH;
                return -1;
            }
        } else if (strcmp("Transfer-Encoding", hdr_str) == 0) {
            //Not super robust, but probably good enough
            if (strcmp("chunked", args_str) == 0) {
                *err = HTTP_CHUNKED_NOT_SUPPORTED;
                return -1;
            }
        }
        
        return 0;
    }
    case HTTP_PAYLOAD: {
        //This function should not have been called to process payload data
        *err = HTTP_INVALID_STATE;
        break;
    }
    
    }
    
    *err = HTTP_NOT_IMPL;
    return -1;
}
#endif

#define HTTP_REQ_INITIAL_SIZE 256
//Returns a newly allocated (and initialized) http_req struct. Use 
//del_http_req to properly free it. Returns NULL on error and sets the error
http_req *new_http_req(mm_err *err) 
#ifdef MM_IMPLEMENT
{
    if (*err != MM_SUCCESS) return NULL;
    
    http_req *ret = malloc(sizeof(http_req));
    if (!ret) {
        *err = HTTP_OOM;
        return NULL;
    }
    
    ret->__internal.base = malloc(HTTP_REQ_INITIAL_SIZE);
    if (!ret->__internal.base) {
        *err = HTTP_OOM;
        free(ret);
        return NULL;
    }
    
    ret->__internal.cap = HTTP_REQ_INITIAL_SIZE;
    
    reset_http_req(ret);
    
    return ret;
}
#else
;
#endif

//Properly frees an http_req struct. Gracefully ignores NULL input.
void del_http_req(http_req *h)
#ifdef MM_IMPLEMENT
{
    if (h == NULL) return;
    
    free(h->__internal.base);
    free(h);
}
#else
;
#endif

//Returns 0 if a complete request has been seen. This indicates to the user
//that the information in *res can now be used. Returns positive if no 
//errors have occurred, but a complete request has not yet been seen.
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
    
    //Make sure there would be enough room for the entire buffer
    expand_mem_to(res, res->__internal.pos + len, err);
    if (*err != MM_SUCCESS) return -1;
    
    //RETURN ADDRESS (i.e. where I was last editing before stopping)
    //Wait... sometimes we're just copying in the payload, so we shouldn't
    //do the header processing. And also, what if the data in the buffer
    //inludes payload data and some data for a new header?
    
    //Copy buf into the http_req struct's internal memory, taking care to
    //process carriage returns and line feeds properly, while also making
    //calls to process_line when lines are scanned in
    int rd_pos = 0;
    char *req_mem = res->__internal.base; //For convenience
    int *pos = &res->__internal.pos; //For convenience
    while (rd_pos < len) {
        if (buf[rd_pos] == '\r') {
            rd_pos++;
            continue;
        } else if (buf[rd_pos] == '\n') {
            req_mem[(*pos)++] = '\0';
            
            int rc = process_line(res, err);
            if (rc < 0) {
                return rc;
            } else if (rc == 0) {
                continue;
            } else {
                
            }
        }
    }
    
    *err = HTTP_NOT_IMPL;
    return -1;
}
#else
;
#endif

#endif
