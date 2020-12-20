//There's a special place in hell for preprocessor sinners like me...
//This is my way of doing "#pragma once"
#ifdef MM_IMPLEMENT
    #ifndef WEBSOCK_H_IMPLEMENTED
        #define SHOULD_INCLUDE 1
        #define WEBSOCK_H_IMPLEMENTED 1
    #else 
        #define SHOULD_INCLUDE 0
    #endif
#else
    #ifndef WEBSOCK_H
        #define SHOULD_INCLUDE 1
        #define WEBSOCK_H 1
    #else
        #define SHOULD_INCLUDE 0
    #endif
#endif

#if SHOULD_INCLUDE
#undef SHOULD_INCLUDE //Don't accidentally mess up other header files

#ifdef MM_IMPLEMENT
#undef MM_IMPLEMENT
#include "websock.h"
#define MM_IMPLEMENT
#endif

#include <alloca.h>
#include <stdio.h>
#include <string.h>
#include <openssl/sha.h>
#include <endian.h> //UGHHH endianness...
#include "mm_err.h"
#include "http_parse.h"

////////////////
// Parameters //
////////////////
#ifndef MM_IMPLEMENT
    #define WEBSOCK_MAX_PROTOCOL_LEN 32
    #define WEBSOCK_INITIAL_SIZE 256

    //Set up a bunch of defines used in constructing the websocket handshake 
    //response
    #define WEBSOCK_UPGRADE_HDR \
        "HTTP/1.1 101 Switching Protocols\r\n"\
        "Upgrade: websocket\r\n"\
        "Connection: Upgrade\r\n"\
        "Sec-WebSocket-Accept: "

    #define WEBSOCK_MAGIC_STRING "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

    #define WEBSOCK_SEC_ACCEPT_LEN (4*((SHA_DIGEST_LENGTH+2)/3))

    #define WEBSOCK_SUBPROTOCOL_HDR \
        "Sec-WebSocket-Protocol: "

    //Remember sizeof includes NUL. In this case I'm not overly concerned 
    //with accidentally using too many bytes, but I wouldn't want anyone to 
    //think I just forgot about it!
    #define WEBSOCK_HANDSHAKE_RESPONSE_SIZE ( \
        (sizeof(WEBSOCK_UPGRADE_HDR)-1) + \
        (WEBSOCK_SEC_ACCEPT_LEN) + \
        2 + /*For CRLF after accept key*/ \
        (sizeof(WEBSOCK_SUBPROTOCOL_HDR)-1) + \
        WEBSOCK_MAX_PROTOCOL_LEN + \
        4) //For CRLF and empty line at end of header
    
    //Based on the standard    
    #define WEBSOCK_MAX_HDR_SIZE 14
#endif

/////////////////
// Error codes //
/////////////////

#define str(s) #s
#define xstr(s) str(s)

MM_ERR(WEBSOCK_NOT_WEBSOCKET, "not a websocket request header");
MM_ERR(WEBSOCK_PROT_TOO_LONG, "protocol string too long (max = " xstr(WEBSOCK_MAX_PROTOCOL_LEN) ")");
MM_ERR(WEBSOCK_NULL_ARG, "NULL argument where non-NULL expected");
MM_ERR(WEBSOCK_BAD_OPCODE, "unsupported websocket opcode");
MM_ERR(WEBSOCK_STRAGGLERS, "leftover bytes in user buffer have been ignored");
MM_ERR(WEBSOCK_INVALID_ARG, "invalid argument");
MM_ERR(WEBSOCK_NOT_IMPL, "not implemented");
MM_ERR(WEBSOCK_OOM, "out of memory");

#undef xstr
#undef str

////////////////////////////////////////////////////////////////
// enums and "sub-structs" used in main websock packet struct //
////////////////////////////////////////////////////////////////
#ifndef MM_IMPLEMENT
    typedef enum _websock_pkt_type_t {
        WEBSOCK_CONT = 0,
        WEBSOCK_TEXT = 1,
        WEBSOCK_BIN = 2,
        WEBSOCK_CLOSE = 8,
        WEBSOCK_PING = 9,
        WEBSOCK_PONG = 10
    } websock_pkt_type_t;
    
    extern char const *const websock_pkt_type_strs[];
    
    #define WEBSOCK_PARSE_STATE_IDS \
        X(WEBSOCK_HDR), \
        X(WEBSOCK_PAYLOAD)
        
    typedef enum _websock_parse_state_t {
        WEBSOCK_HDR_FIRST_TWO_BYTES, //God websockets is such a pain
        WEBSOCK_REST_OF_HDR,
        WEBSOCK_PAYLOAD
    } websock_parse_state_t;
    
#else
    //char const *const websock_badop = "bad operation";
    #define websock_badop NULL
    char const *const websock_pkt_type_strs[] = {
        "WEBSOCK_CONT",
        "WEBSOCK_TEXT",
        "WEBSOCK_BIN",
        websock_badop,
        websock_badop,
        websock_badop,
        websock_badop,
        websock_badop,
        "WEBSOCK_CLOSE",
        "WEBSOCK_PING",
        "WEBSOCK_PONG",
        websock_badop,
        websock_badop,
        websock_badop,
        websock_badop,
        websock_badop,
    };
#endif

///////////////////////////////////
// Main websock packet structure //
///////////////////////////////////
#ifndef MM_IMPLEMENT
    typedef struct _websock_pkt {
        websock_pkt_type_t type;
        int fin;
        unsigned long payload_len;
        char *payload;
        
        struct {
            websock_parse_state_t state;
            char *base;
            int pos;
            int cap;
            int hdr_len;
            char mask[4];
        } __internal;
    } websock_pkt;
#endif

//////////////////////////////////
// Managing websock_pkt structs //
//////////////////////////////////

//Returns a newly allocated (and initialized) websock_pkt struct. Use 
//del_websock_pkt to properly free it. Returns NULL and sets *err on error
websock_pkt *new_websock_pkt(mm_err *err) 
#ifdef MM_IMPLEMENT
{
    if (*err != MM_SUCCESS) return NULL;
    
    websock_pkt *ret = malloc(sizeof(websock_pkt));
    if (!ret) {
        *err = WEBSOCK_OOM;
        return NULL;
    }
    
    char *base = malloc(WEBSOCK_INITIAL_SIZE);
    if (!base) {
        *err = WEBSOCK_OOM;
        free(ret);
        return NULL;
    }
    
    ret->__internal.base = base;
    ret->__internal.cap = WEBSOCK_INITIAL_SIZE;
    
    reset_websock_pkt(ret);
    
    return ret;    
}
#else
;
#endif

//Resets all state in a websock_pkt struct (but does not free any internal
//buffers). Assumes pkt is non-NULL.
void reset_websock_pkt(websock_pkt *pkt) 
#ifdef MM_IMPLEMENT
{
    pkt->__internal.state = WEBSOCK_HDR_FIRST_TWO_BYTES;
    pkt->__internal.pos = 0;
    pkt->payload_len = -1;
}
#else
;
#endif

//Frees all memory assicated with *pkt. Gracefull ignores NULL input.
void del_websock_pkt(websock_pkt *pkt) 
#ifdef MM_IMPLEMENT
{
    if (!pkt) return;
    free(pkt->__internal.base);
    free(pkt);
}
#else
;
#endif

//////////////////////
// Static functions //
//////////////////////

#ifdef MM_IMPLEMENT

//It's kind of silly to have several functions like this one... I mean, it
//doesn't take long to copy-paste and make edits, but I should probably move
//this functionality to its own library.

//Expand memory inside a websock_pkt struct. Makes sure the resulting 
//expanded block is at least min_sz bytes
//NOTE: does not check if pkt is non-NULL
static void expand_pkt_mem_to(websock_pkt *pkt, int min_sz, mm_err *err) {
    if (*err != MM_SUCCESS) return;
    
    //Quit early if no expansion needed
    if (pkt->__internal.cap >= min_sz) return;
    
    //Resize the memory buffer
    char *new_base = realloc(pkt->__internal.base, pkt->__internal.cap * 2);
    if (!new_base) {
        *err = HTTP_OOM;
        return;
    }
    
    //Update internal bookkeeping
    pkt->__internal.base = new_base;
    pkt->__internal.cap *= 2;
}

//What a pain! Why does websockets have such an inconvenient length format?
static void process_websock_hdr_length(websock_pkt *pkt, mm_err *err) {
    if (*err != MM_SUCCESS) return;
    
    //Sanity-check inputs
    if (!pkt) {
        *err = WEBSOCK_NULL_ARG;
        return;
    }
    
    char length_code = pkt->__internal.base[1] & 0xFF;
    
    switch (length_code) {
        case 126:
            pkt->__internal.hdr_len = 8;
            break;
        case 127:
            pkt->__internal.hdr_len = 14;
            break;
        default:
            pkt->__internal.hdr_len = 6;
            pkt->payload_len = length_code;
            break;
    }
    
    //Update parse state
    pkt->__internal.state = WEBSOCK_REST_OF_HDR;
}

//Given a buffer of WEBSOCK_HDR_SIZE bytes, fill the websock_pkt struct with
//the proper fields. 
static void process_websock_hdr(websock_pkt *pkt, mm_err *err) {
    if (*err != MM_SUCCESS) return;
    
    //Sanity-check inputs
    if (!pkt) {
        *err = WEBSOCK_NULL_ARG;
        return;
    }
    
    char const *hdr = pkt->__internal.base; //For convenience
    
    //FIN bit
    pkt->fin = hdr[0] >> 7;
    
    //Opcode
    int opcode = hdr[0] & 0xF;
    
    if (websock_pkt_type_strs[opcode] == websock_badop) {
        *err = WEBSOCK_BAD_OPCODE;
        return;
    }
    
    pkt->type = (websock_pkt_type_t) opcode;
    
    //Length
    //Why is websockets so complicated?
    unsigned long len = hdr[1] & 0x7F;
    hdr += 2;
    
    if (len == 126) {
        len = be16toh(*(unsigned short*)(hdr));
        hdr += 2;
    } else if (len == 127) {
        len = be64toh(*(unsigned short*)(hdr));
        hdr += 8;
    }
    
    pkt->payload_len = len;
    
    //Masking key
    pkt->__internal.mask[0] = hdr[0];
    pkt->__internal.mask[1] = hdr[1];
    pkt->__internal.mask[2] = hdr[2];
    pkt->__internal.mask[3] = hdr[3];
    
    //Update internal parse state of packet
    pkt->__internal.state = WEBSOCK_PAYLOAD;
    pkt->__internal.pos = 0;
    
    return;
}

#endif

//Says whether this is a websocket request. Returns 1 if true.
int is_websock_request(http_req const *req, mm_err *err)
#ifdef MM_IMPLEMENT
{
    if (*err != MM_SUCCESS) return 0;
    
    //Need to check for Connection, Upgrade, and Sec-WebSocket-Key fields
    {
    char *cxn_args = get_args(req, "Connection", err);
    if (!cxn_args) return 0;
    if (strcmp(cxn_args, "Upgrade")) return 0;
    }
    
    {
    char *upgrade_args = get_args(req, "Upgrade", err);
    if (!upgrade_args) return 0;
    if (strcmp(upgrade_args, "websocket")) return 0;
    }
    
    {
    char *sec_ws_key_args = get_args(req, "Sec-WebSocket-Key", err);
    if (!sec_ws_key_args) return 0;
    }
    
    return 1;
}
#else
;
#endif

void to_b64(unsigned char *dst, unsigned char const *src, int len)
#ifdef MM_IMPLEMENT
{
    //Taken from Rosetta code and adapted
    static const char *alpha =	"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                "abcdefghijklmnopqrstuvwxyz"
                                "0123456789+/";
    
    unsigned u;
        
	while (len >= 3) {
		u = (unsigned)src[0]<<16 | (unsigned)src[1]<<8 | (unsigned)src[2];
 
		*dst++ = alpha[u>>18 & 0x3F];
		*dst++ = alpha[u>>12 & 0x3F];
		*dst++ = alpha[u>>6  & 0x3F];
        *dst++ = alpha[u     & 0x3F];
        
        src += 3;
        len -= 3;
	}
        
    u = 0;
    switch (len) {
        case 0:
            break;
        case 1:
            u = (unsigned) src[0];
            *dst++ = alpha[u>>2 & 0x3F];
            *dst++ = alpha[u<<4 & 0x3F];
            *dst++ = '=';
            *dst++ = '=';
            break;
        case 2:
            u = (unsigned) src[0]<<8 | (unsigned) src[1];
            *dst++ = alpha[u>>10 & 0x3F];
            *dst++ = alpha[u>>4  & 0x3F];
            *dst++ = alpha[u<<2  & 0x3F];
            *dst++ = '=';
            break;
    }

    //Don't forget NUL byte:
    *dst++ = 0;
}
#else
;
#endif

//Same semantics as write_to_http_parser
int write_to_websock_parser(websock_pkt *pkt, char const *buf, int len, mm_err *err)
#ifdef MM_IMPLEMENT
{
    if (*err != MM_SUCCESS) return -1;
    
    //Sanity-check inputs
    if (!pkt || !buf) {
        *err = WEBSOCK_NULL_ARG;
        return -1;
    }
    if (len < 0) {
        *err = WEBSOCK_INVALID_ARG;
        return -1;
    }
    
    //Make sure internal buffer has enough room for this new input
    expand_pkt_mem_to(pkt, pkt->__internal.pos + len, err);
    if (*err != MM_SUCCESS) return -1;
    
    char *base = pkt->__internal.base; //For convenience
    int *pos = &(pkt->__internal.pos); //For convenience
    
    int rd_pos = 0;
    
    //Damn! Now for the third time: why is websockets so complicated??? The
    //header size is variable! What a headache!
    if (pkt->__internal.state == WEBSOCK_HDR_FIRST_TWO_BYTES) {
        while (rd_pos < len && (*pos) < 2) {
            base[(*pos)++] = buf[rd_pos++];
        }
        
        if (*pos == 2) {
            process_websock_hdr_length(pkt, err);
            if (*err != MM_SUCCESS) return -1;
        }
    }
    
    //This is deliberately NOT an else if
    if (pkt->__internal.state == WEBSOCK_REST_OF_HDR) {
        while (rd_pos < len && (*pos) < pkt->__internal.hdr_len) {
            base[(*pos)++] = buf[rd_pos++];
        }
        
        if (*pos == pkt->__internal.hdr_len) {
            process_websock_hdr(pkt, err);
            if (*err != MM_SUCCESS) return -1;
        }
    }
    
    //This is deliberately NOT an else if
    if (pkt->__internal.state == WEBSOCK_PAYLOAD) {
        while (rd_pos < len && (*pos) < pkt->payload_len) {
            base[*pos] = buf[rd_pos++] ^ pkt->__internal.mask[(*pos)&0x3]; //Unmask payload (why does websockets have this?)
            (*pos)++;
        }
        
        if (*pos == pkt->payload_len) {
            //Done reading payload. Make sure user-facing fields are in order
            pkt->payload = pkt->__internal.base;
            
            //Reset pos and parse state in case user wants to reuse this 
            //struct
            pkt->__internal.pos = 0;
            pkt->__internal.state = WEBSOCK_HDR_FIRST_TWO_BYTES;
            
            //Check for stragglers, then return
            if (rd_pos != len) {
                *err = WEBSOCK_STRAGGLERS;
                return -rd_pos;
            }
            
            return 0; //Done reading packet
        }
    }
    
    return 1; //No error, but not done
}
#else
;
#endif

////////////////////////////////////////////////////
// Functions for constructing messages to clients //
////////////////////////////////////////////////////

//Constructs a standard response to a websocket handshake request. If prot
//is non-NULL, a Sec-WebSocket-Protocol header is added into the response
//with the argument given in prot.
//Returns a pointer to statically-allocated memory; you need to copy it
//yourself it you want to save it before caling this function again. (Or 
//just returns NULL on error)
char *websock_handshake_response(http_req const *req, char const *prot, mm_err *err) 
#ifdef MM_IMPLEMENT
{
    static char buf[WEBSOCK_HANDSHAKE_RESPONSE_SIZE];
    
    if (*err != MM_SUCCESS) return NULL;
    
    //Sanity check inputs
    if (req == NULL) {
        *err = WEBSOCK_NULL_ARG;
        return NULL;
    }
    if (!is_websock_request(req, err)) {
        *err = WEBSOCK_NOT_WEBSOCKET;
        return NULL;
    }
    if (prot && strlen(prot) > WEBSOCK_MAX_PROTOCOL_LEN) {
        *err = WEBSOCK_PROT_TOO_LONG;
        return NULL;
    }
    
    char *key = get_args(req, "Sec-WebSocket-Key", err);
    unsigned keylen = strcspn(key, ",");
    unsigned const magiclen = sizeof(WEBSOCK_MAGIC_STRING) - 1; //sizeof includes NUL at end
    unsigned char *hash_me = alloca(keylen + magiclen); //Stack allocation FTW
    memcpy(hash_me, key, keylen);
    memcpy(hash_me + keylen, WEBSOCK_MAGIC_STRING, magiclen);
    
    unsigned char result[SHA_DIGEST_LENGTH];
    SHA1(hash_me, keylen + magiclen, result);
    
    unsigned char result_b64[WEBSOCK_SEC_ACCEPT_LEN+1]; //+1 for the NUL
    to_b64(result_b64, result, SHA_DIGEST_LENGTH);
    
    int incr;
    int pos = 0;
    sprintf(buf + pos, WEBSOCK_UPGRADE_HDR "%s\r\n%n", result_b64, &incr);
    pos += incr;
    
    if (prot != NULL) {
        sprintf(buf + pos, WEBSOCK_SUBPROTOCOL_HDR "%s\r\n%n", prot, &incr);
        pos += incr;
    }
    
    sprintf(buf + pos, "\r\n%n", &incr);
    pos += incr; //TODO: should I return this length?
    
    return buf;
}
#else
;
#endif

//Constructs a websocket header in the array pointed to by dest, which must
//have at least WEBSOCK_MAX_HDR_SIZE bytes of space. If this is just a 
//control frame, you can set payload_len to 0 (but control frames that have
//a payload can feel free to set it to be nonzero). Returns number of bytes
//written, or negative on error
int construct_websock_hdr(char *dst, websock_pkt_type_t type, int fin, unsigned long len, mm_err *err)
#ifdef MM_IMPLEMENT
{
    if (*err != MM_SUCCESS) return -1;
    
    char *dst_saved = dst; //We'll use this to compute number of bytes written
    
    //Sanity check inputs
    if (!dst) {
        *err = WEBSOCK_NULL_ARG;
        return -1;
    }
    if (type > 15 || websock_pkt_type_strs[type] == websock_badop || (len>>63)) {
        *err = WEBSOCK_INVALID_ARG;
        return -1;
    }
    
    //Write FIN + OPCODE portion
    *dst++ = (fin ? 0x80 : 0) | type;
    
    //Write length portion
    if (len < 126) {
        *dst++ = (len & 0xFF);
    } else if (len < 0xFFFF) {
        *dst++ = 126;
        //WARNING WARNING WARNING This assumes the host machine is little endian!
        char *bytes = (char*)&len;
        *dst++ = bytes[1];
        *dst++ = bytes[0];
    } else {
        *dst++ = 127;
        //WARNING WARNING WARNING This assumes the host machine is little endian!
        char *bytes = (char*)&len;
        *dst++ = bytes[7];
        *dst++ = bytes[6];
        *dst++ = bytes[5];
        *dst++ = bytes[4];
        *dst++ = bytes[3];
        *dst++ = bytes[2];
        *dst++ = bytes[1];
        *dst++ = bytes[0];
    }
    
    
    //Push zero bytes for the mask
    *dst++ = 0;
    *dst++ = 0;
    *dst++ = 0;
    *dst++ = 0;
    
    return dst - dst_saved;
}
#else
;
#endif


#else
#undef SHOULD_INCLUDE
#endif
