
// vim: ts=4 sw=4 :

#ifndef _genlib_h_
#define _genlib_h_

#include <stdint.h>
#include <time.h>
#include <sys/types.h>

/*
	Mnemonic:	genlib.h
	Abstract:	Header file for the generic library.
	Author:		E. Scott Daniels
	Date:		01 February 2017
*/


// --------------- list ----------------------------------------------------------------------------------
#define LF_QUALIFED		1				// list_files should return qualified names
#define LF_UNQUALIFIED	0

extern char** list_files( char* dname, const char* suffix, int qualify, int* len );
extern char** list_pfiles( char* dname, const char* prefix, int qualify, int* len );
extern char** list_old_files( char* dname, int qualify, int seconds, int* len );
extern char** rm_new_files( char** flist, int seconds, int *ulen );
extern void free_list( char** list, int size );

// --------------- bleat ----------------------------------------------------------------------------------
#define BLEAT_ADD_DATE	1
#define BLEAT_NO_DATE	0

extern int bleat_set_lvl( int l );
extern void bleat_set_purge( const char* dname, const char* prefix, int seconds );
extern time_t bleat_next_roll( void );
extern void bleat_push_lvl( int l );
extern void bleat_push_glvl( int l );
extern void bleat_pop_lvl( void );
extern int bleat_will_it( int l );
extern int bleat_set_log( char* fname, int add_date );
extern void bleat_printf( int level, const char* fmt, ... );

//---------------- jwrapper -------------------------------------------------------------------------------
extern int jw_array_len( void* st, const char* name );
extern void* jw_blob( void* st, const char* name );
extern int jw_exists( void* st, const char* name );
extern int jw_is_value( void* st, const char* name );
extern int jw_is_bool( void* st, const char* name );
extern int jw_is_null( void* st, const char* name );
extern int jw_is_value_ele( void* st, const char* name, int idx );
extern int jw_is_bool_ele( void* st, const char* name, int idx );
extern int jw_is_null_ele( void* st, const char* name, int idx );
extern int jw_missing( void* st, const char* name );
extern void* jw_new( char* json );
extern void jw_nuke( void* st );
extern void* jw_obj_ele( void* st, const char* name, int idx );
extern char* jw_string( void* st, const char* name );
extern char* jw_string_ele( void* st, const char* name, int idx );
extern float jw_value( void* st, const char* name );
extern float jw_value_ele( void* st, const char* name, int idx );

//------------------ ng_flowmgr --------------------------------------------------------------------------
void ng_flow_close( void *vf );
void ng_flow_flush( void *vf );
char* ng_flow_get( void *vf, char sep );
void *ng_flow_open(  int size );
void ng_flow_ref( void *vf, char *buf, long len );


// ---------------- fifo ---------------------------------------------------------------------------------
extern void* rfifo_create( char* fname, int mode );
extern void rfifo_close( void* vfifo );
extern char* rfifo_read( void* vfifo );
extern char* rfifo_readln( void* vfifo );


#endif
