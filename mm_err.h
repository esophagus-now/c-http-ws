/***************/
/**How to use:**/
/***************/

/* 
INCLUDING THE HEADER FILE
-------------------------

Use the usual 

    #include "mm_err.h"
    
in all files that require the mm_err library. Then, in ONE source file ONLY,
use:

    #define MM_IMPLEMENT
    #include "mm_err.h"

The MM_IMPLEMENT macro must be defined before mm_err.h is included. 

I will usually do do this by having a separate file which just defines the
MM_IMPLEMENT macro then includes a bunch of headers, but is otherwise empty:

    implement.c
    ~~~~~~~~~~~
    #define MM_IMPLEMENT
    #include "mm_err.h"
    #include "my_lib.h"

    //etc.




DEFINING NEW ERROR CODES
------------------------

**NOTE: these instructions are slightly different if you are writing library
        functions. See the last section in this document

Suppose I had the following files:

    thing.h
    ~~~~~~~
    //Stuff
    
    
    thing.c
    ~~~~~~~
    #include "my_lib.h"
    
    //Stuff

If I wanted to define a new error for this library, I would use the MM_ERR
macro in the header file:

    thing.h
    ~~~~~~~
    MM_ERR(THING_ERROR, "error in thing");
    
    //Stuff
    

Then, in ONE source ONLY, define MM_IMPLEMENT before including thing.h:
    
    thing.c
    ~~~~~~~~
    #define MM_IMPLEMENT
    #include "thing.h"
    
    //Stuff that uses THING_ERROR



WHEN CALLING FUNCTIONS
----------------------

This is pretty simple. You first initialize an mm_err and pass it by 
reference as the final argument:

    mm_err err = MM_SUCCESS;
    some_lib_fn(arg1, arg2, ..., &err);
    //Check for errors...
    if (err != MM_SUCCESS) { ... }

If you don't want to check for errors every step of the way, you can keep
passing the same error variable (unchanged) to later functions. If a library
writer uses mm_err, they are guaranteeing that they will do the right thing
if an earlier function had an error:

    mm_err err = MM_SUCCESS




WHEN WRITING LIBRARIES
----------------------

Suppose you had a function which used to return an int for an error code:
    
    my_lib.h
    ~~~~~~~~
    int my_lib_fun(double, char, float*);
    
    
    my_lib.c
    ~~~~~~~~
    #include "my_lib.h"
    
    int my_lib_fun(double arg1, char arg2, float *ret_val) {
        //do work, or return nonzero if error
        *ret_val = something;
        return 0;
    }

This would instead be written as
    
    my_lib.h
    ~~~~~~~~
    MM_ERR(MY_LIB_ERROR, "error in my_lib");
    MM_ERR(MY_LIB_OTHER_ERROR, "other error in my_lib");
    
    float my_lib_fun(double arg1, char arg2, mm_err *err);
    
    
    my_lib.c
    ~~~~~~~~
    #include "my_lib.h"
    
    float my_lib_fun(double arg1, char arg2, mm_err *err) {
        if (*err != MM_SUCCESS) {
            //Don't continue executing the functions
            return 0.0; //The user should ignore the return value
        }
        
        //Do work
        if (some_error()) {
            *err = MY_LIB_ERROR;
            return 0.0; //The user should ignore the return value
        }
        
        //Success
        return something;
    }

As a library-writer, you should expect the user to create the file which 
defines MM_IMPLEMENT and includes your header (my_lib.h).
*/


//Special case for mm_err.h only: the following defines should always be
//made (in other words, #pragma once is not the indended behaviour)

//Very ugly preprocessor hack... there must be a cleaner way to do this!
//This makes sure that the "header version" of mm_err.h eventually gets
//included somewhere
#ifdef MM_IMPLEMENT
#undef MM_IMPLEMENT
#include "mm_err.h"
#define MM_IMPLEMENT
#endif

//Gets rid of (harmless) preprocessor warnings
#ifdef MM_ERR
#undef MM_ERR
#endif
#ifdef EXTERN_FIX
#undef EXTERN_FIX
#endif

#ifdef __cplusplus
#define EXTERN_FIX extern
#else
#define EXTERN_FIX
#endif

#ifdef MM_IMPLEMENT
#warning Defining MM_ERR in implement mode
#define MM_ERR(name, val) EXTERN_FIX mm_err const name = val
#else
#warning Defining MM_ERR in header mode
#define MM_ERR(name, val) extern mm_err const name
#endif



//This is my way of doing "#pragma once"
#ifdef MM_IMPLEMENT
    #ifndef MM_ERR_H_IMPLEMENTED
        #define SHOULD_INCLUDE 1
        #define MM_ERR_H_IMPLEMENTED 1
    #else 
        #define SHOULD_INCLUDE 0
    #endif
#else
    #ifndef MM_ERR_H
        #define SHOULD_INCLUDE 1
        #define MM_ERR_H 1
    #else
        #define SHOULD_INCLUDE 0
    #endif
#endif

#if SHOULD_INCLUDE
#undef SHOULD_INCLUDE //Don't accidentally mess up other header files

#ifdef MM_IMPLEMENT
#warning including mm_err.h in implement mode
#else
#warning including mm_err.h in header mode
#endif

#ifndef MM_IMPLEMENT
typedef char const *mm_err;
#endif

MM_ERR(MM_SUCCESS, "success");

#else
    #undef SHOULD_INCLUDE
#endif
