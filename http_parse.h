//Credit to https://www.jmarshall.com/easy/http/ who does a great job of
//explaining HTTP. MDN and the RFCs are needlessly overcomplicated.

//There's a special place in hell for preprocessor sinners like me...
//This is my way of doing "#pragma once"
#ifdef MM_IMPLEMENT
    #ifndef HTTP_PARSE_H_IMPLEMENTED
        #define SHOULD_INCLUDE 1
        #define HTTP_PARSE_H_IMPLEMENTED 1
    #else 
        #define SHOULD_INCLUDE 0
    #endif
#else
    #ifndef HTTP_PARSE_H
        #define SHOULD_INCLUDE 1
        #define HTTP_PARSE_H 1
    #else
        #define SHOULD_INCLUDE 0
    #endif
#endif

#if SHOULD_INCLUDE
#undef SHOULD_INCLUDE //Don't accidentally mess up other header files

//Unforgivable preprocessor shenanigans! There must be a cleaner way to do
//this! This makes sure the implemented version of a file can still use the
//"header" versions of itself
#ifdef MM_IMPLEMENT
    #undef MM_IMPLEMENT
    #include "http_parse.h"
    #define MM_IMPLEMENT
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mm_err.h"

#ifdef MM_IMPLEMENT
#warning including http_parse.h in implement mode
#else
#warning including http_parse.h in header mode
#endif


//////////////////////////
//Error code definitions//
//////////////////////////

MM_ERR(HTTP_BAD_METHOD, "http library only accepts GET, HEAD, and POST");
MM_ERR(HTTP_MISSING_PATH, "URI path not given in request status line");
MM_ERR(HTTP_MISSING_PROTOCOL, "HTTP protocol not given in request status line");
MM_ERR(HTTP_BAD_PROTOCOL, "malformed HTTP protocol string");
MM_ERR(HTTP_FOLD_NO_HDR, "folded header argument with no prior header field");
MM_ERR(HTTP_BAD_HDR, "HTTP header has bad syntax");
MM_ERR(HTTP_CONTENT_LENGTH_UNSPECIFIED, "HTTP Content-Length unspecified");
MM_ERR(HTTP_CHUNKED_NOT_SUPPORTED, "this server does not support chunked transfers");
MM_ERR(HTTP_INVALID_CONTENT_LENGTH, "invalid argument for Content-Length");
MM_ERR(HTTP_STRAGGLERS, "leftover bytes in user buf have been ignored");
MM_ERR(HTTP_NOT_IMPL, "function not implemented");
MM_ERR(HTTP_NULL_ARG, "NULL argument given but non-NULL expected");
MM_ERR(HTTP_INVALID_ARG, "invalid argument");
MM_ERR(HTTP_INVALID_STATE, "http parser in invalid state (must reset!)");
MM_ERR(HTTP_NOT_FOUND, "not found");
MM_ERR(HTTP_OOM, "out of memory");
MM_ERR(HTTP_IMPOSSIBLE, "HTTP parsing code reached location Marco thought was impossible");

//////////////
//Parameters//
//////////////

#define HTTP_REQ_INITIAL_SIZE 257
#define HTTP_MAX_HDRS 32

////////////////////////////////////////////////////////
//enums and "sub-structs" used in main http_req struct//
////////////////////////////////////////////////////////

#define HTTP_REQ_TYPE_IDS \
    X(HTTP_GET), \
    X(HTTP_POST), \
    X(HTTP_HEAD)

#ifndef MM_IMPLEMENT
    typedef enum _http_req_t {
    #define X(x) x
        HTTP_REQ_TYPE_IDS
    #undef X
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
    
    extern char const *const http_req_strs[];
#else
    #define X(x) #x
    char const *const http_req_strs[] = {
        HTTP_REQ_TYPE_IDS
    };
    #undef X
#endif


///////////////////////////////
//Main HTTP request structure//
///////////////////////////////
#ifndef MM_IMPLEMENT
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
            unsigned pos;
            //Number of (total) bytes allocated in base
            unsigned cap;
            
            //To (significantly) simplify parsing code, we will process text 
            //line-by-line. This index keeps track of the beginning of the
            //current line (for example, if we need to read() more bytes to get
            //to the end)
            int line;
        } __internal;
    } http_req;
#endif

////////////////////
//Static functions//
////////////////////
#ifdef MM_IMPLEMENT

//Expand memory inside an http_req struct. Makes sure the resulting expanded
//block is at least min_sz bytes
//NOTE: does not check if res is non-NULL
static void expand_req_mem_to(http_req *res, int min_sz, mm_err *err) {
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
        
        //Terminate this arg, whether that be with a comma or a NUL
        line[wr_pos++] = line[rd_pos];
        
        //Quit early if encountered end of string
        if (!line[rd_pos++]) break;
        
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
//Also, the pointers this saves are actually offsets from __internal.base.
//After a complete http_req struct is read, you have to run final_addresses
//on it
static int process_line(http_req *res, mm_err *err) {
    if (*err != MM_SUCCESS) return -1;
    
    char *line = res->__internal.base + res->__internal.line;
#ifdef DEBUG_ON
    fprintf(stderr, "DEBUG: line = [%s]\n", line);
#endif
    char *line_end = res->__internal.base + res->__internal.pos;
    
    switch(res->__internal.state) {
    case HTTP_STATUS_LINE: {
        //Start by reading the request type
        //Not efficient, but who cares?
        int reqtype_len;
        if (strncmp("GET ", line, 4) == 0) {
            res->req_type = HTTP_GET;
            reqtype_len = 4;
        } else if (strncmp("HEAD ", line, 5) == 0) {
            res->req_type = HTTP_HEAD;
            reqtype_len = 5;
        } else if (strncmp("POST ", line, 5) == 0) {
            res->req_type = HTTP_POST;
            reqtype_len = 5;
        } else {
            *err = HTTP_BAD_METHOD;
            return -1;
        }
        line += reqtype_len;
        
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
        if (strncmp("HTTP/1.0", line, 8) && strncmp("HTTP/1.1", line, 8)) {
            *err = HTTP_BAD_PROTOCOL;
            return -1;
        }
        //Laziness: don't bother checking if there is extra garbage on this line
        
        //Move up the beginning-of-line indicator to just past the path
        res->__internal.line += reqtype_len + path_len + 1;
        //Also ask the data reader function to write at this location
        res->__internal.pos = res->__internal.line;
        res->__internal.state = HTTP_HDR;
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
        //Make sure line and pos point to one after the end
        res->__internal.line += hdr_len + args_len + 1;
        res->__internal.pos = res->__internal.line;
        
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

//When building an http_req struct, the pointers are actually offsets from
//__internal.base, because sometimes we will realloc() it. However, once a
//struct is filled, no more realloc()s will happen, and now we add the base
//to all pointers
static void final_addresses(http_req *res, mm_err *err) {
    unsigned long base = (unsigned long) res->__internal.base;
    
    if (res->num_hdrs < 0 || res->num_hdrs >= HTTP_MAX_HDRS) {
        *err = HTTP_INVALID_ARG;
        return;
    }
    
    res->path += base;
    res->payload += base;
    
    int i;
    for (i = 0; i < res->num_hdrs; i++) {
        res->hdrs[i].name += base;
        res->hdrs[i].args += base;
    }
    
    return;
}
#endif

/////////////////////////////
//Managing http_req_structs//
/////////////////////////////

//Returns a newly allocated (and initialized) http_req struct. Use 
//del_http_req to properly free it. Returns NULL and sets *err on error
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

//Resets all state in an http_req struct (but does not free any internal
//buffers). Assumes h is non-NULL.
void reset_http_req(http_req *h) 
#ifdef MM_IMPLEMENT
{
    h->num_hdrs = 0;
    h->payload_len = -1;
    h->__internal.state = HTTP_STATUS_LINE;
    h->__internal.pos = 0;
    h->__internal.line = 0;
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

/////////////////////////
//Parsing HTTP requests//
/////////////////////////

/* write_to_http_parser:

DESCRIPTION
-----------
Uses entire buffer given by buf and len (unless an error occurs) to fill 
the http_req structure pointed to by res. Obeys the usual behaviour 
regarding err (see comments in mm_err.h). The return value indicates 
whether a complete request was parsed, or if more data is needed.

RETURN VALUE
------------
Returns 0 if a complete request has been seen. This indicates to the user 
that the information in *res can now be used. Returns positive if no errors 
have occurred, but more data is needed to parse the request. Returns 
negative on error, and sometimes the value has a meaning. See the 
explanation of the HTTP_STRAGGLERS error, below.

HTTP SYNTAX ERRORS
------------------
This function can return one of the HTTP request syntax errors, and will 
immediately stop everything it's doing at that point. If you want to try 
and recover, consider using reset_http_req.

HTTP_STRAGGLERS ERROR (AND HOW TO RECOVER FROM IT)
--------------------------------------------------
This function also assumes the buffer passed in using buf and len does not 
"straddle" two adjacent requests in memory. For example, suppose for some 
bizarre reason you obtained the following data:
      ________________  ____________________
  ... HTTP request 1 |  | HTTP request 2 ...
      ----------------  --------------------
          ^                ^ 
         buf            buf+len
          [----------------]

In this case, write_to_http_parser will set *err to HTTP_STRAGGLERS, and 
will return (-1) times the number of bytes that were read from buf. 
 -> This error is not fatal!

In this case, write_to_http_parser DOES guarantee the http_req struct is 
correctly filled with the data from request 1, and thus it is possible to 
recover from this error as follows:

  //Contrived setup to illustrate the problem
  char *req1 = get_request_1();
  char *req2 = get_request_2();
  int total_len = strlen(req1) + strlen(req2);
  char *buf = malloc(total_len + 1); //"+ 1" makes room for the NUL byte
  strcpy(buf, req1);
  strcat(buf, req2);

  //Now try parsing a request
  mm_err err = MM_SUCCESS;
  http_req *req = new_http_req(&err); //Assume this succeeds
  write_to_http_parser(req, buf, total_len, &err);
  if (err != MM_SUCCESS) {
      //Error happened
      if (err = HTTP_STRAGGLERS) {
          //Data in req is valid, so use it:
          use_parsed_req_struct(req);
          //Skip past data read by the parser
          int num_read = -rc;
          buf += num_read;
          total_len -= num_read;
          //Indicate that we recovered
          err = MM_SUCCESS;
          //Parse the second request (note: this invalidates old contents
          //of req):
          int rc = write_to_http_parser(req, buf, total_len, &err);
          if (rc < 0) {
              puts("We tried to recover (and we still can) but we decided"
                   " to give up. Sorry!");
              exit(1);
          }
      }
  }
  
  if (err == MM_SUCCESS) {
      use_parsed_req_struct(req);
  }
  
EXTRA DETAILS
-------------
If res points to a structure that was previously filled by 
write_to_http_parser, then res's memory will be reused. This improves 
performance, but it invalidates the prior contents of res.

Speaking of performance, this function copies buf to an internally managed 
buffer, and the text sanitization functions (only used on the header) are 
about as expensive as a second copy. However, the payload is only copied 
once.
*/
int write_to_http_parser(http_req *res, char const *buf, int len, mm_err *err)
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
    
    //Reset the struct if we're starting fresh
    if (res->__internal.state == HTTP_STATUS_LINE) reset_http_req(res);
    
    //Make sure there would be enough room for the entire buffer
    expand_req_mem_to(res, res->__internal.pos + len, err);
    if (*err != MM_SUCCESS) return -1;
    
    //Wait... sometimes we're just copying in the payload, so we shouldn't
    //do the header processing. And also, what if the data in the buffer
    //inludes payload data and some data for a new header?
    
    //Copy buf into the http_req struct's internal memory, taking care to
    //process carriage returns and line feeds properly, while also making
    //calls to process_line when lines are scanned in
    int rd_pos = 0;
    char *req_mem = res->__internal.base; //For convenience
    unsigned *wr_pos = &res->__internal.pos; //For convenience
    while (rd_pos < len) {
        if (buf[rd_pos] == '\r') {
            //Skip this character
            rd_pos++;
            continue;
        } else if (buf[rd_pos] == '\n') {
            //Terminate the line and feed to process_line
            req_mem[(*wr_pos)++] = '\0'; 
            rd_pos++;
            
            int rc = process_line(res, err);
            if (rc < 0) {
                //Error occurred
                return rc;
            } else if (rc == 0) {
                //The entire line has been read, but it was not empty
                continue;
            }
            //The line was empty. This means the header is finished
            if (res->__internal.state == HTTP_PAYLOAD) {
                //Make sure there's enough room for the payload
                expand_req_mem_to(res, res->__internal.pos + res->payload_len, err);
                if (*err != MM_SUCCESS) return -1;
                
                //This is our tricky hack of only storing the offset until
                //we're completely sure no more realloc()s will happen
                res->payload = (char *) ((unsigned long)res->__internal.pos);
                
                //TODO: finish implementing support for reading payloads
                *err = HTTP_NOT_IMPL;
                return -1;
            } else {
                //This means there is no payload and we can just return
                //the filled struct
                
                //Finalize addresses
                final_addresses(res, err);
                if (*err != MM_SUCCESS) return -1;
                
                //Finally, make sure that there are no stragglers:
                if (rd_pos < len - 1) {
                    *err = HTTP_STRAGGLERS;
                    return -rd_pos;
                }
                return 0; //Done!
            }
        } else {
            req_mem[(*wr_pos)++] = buf[rd_pos++];
        }
    }
    
    //Entire buffer was read, but a complete request has not yet been seen.
    return 1;
}
#else
;
#endif

/////////////////////////////////
//Working with http_req structs//
/////////////////////////////////

//Return pointer to args given header name, or NULL on error
char *get_args(http_req const* req, char const *hdr_name, mm_err *err) 
#ifdef MM_IMPLEMENT
{
    if (*err != MM_SUCCESS) return NULL;
    
    //Sanity-check inputs
    if (req == NULL || hdr_name == NULL) {
        *err = HTTP_NULL_ARG;
        return NULL;
    }
    if (req->num_hdrs < 0 || req->num_hdrs >= HTTP_MAX_HDRS) {
        *err = HTTP_INVALID_ARG;
        return NULL;
    }
    
    //Just do a dumb linear search
    char *found = NULL;
    int i;
    for (i = 0; i < req->num_hdrs; i++) {
        if (strcmp(req->hdrs[i].name, hdr_name) == 0) {
            found = req->hdrs[i].args;
            break;
        }
    }
    
    if (found == NULL) *err = HTTP_NOT_FOUND;
    
    return found;
}
#else
;
#endif


#else
    #undef SHOULD_INCLUDE
#endif
