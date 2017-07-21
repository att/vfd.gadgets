//  these are candidates for the library


#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>

#include "lib_candidates.h"

/*
	Read an entire file into a buffer. We assume for config files
	they will be smallish and so this won't be a problem.
	Returns a pointer to the buffer, or NULL. Caller must free.
	Terminates the buffer with a nil character for string processing.


	If uid is not a nil pointer, then the user number of the owner of the file
	is returned to the caller via this pointer.

	If we cannot stat the file, we assume it's empty or missing and return
	an empty buffer, as opposed to a NULL, so the caller can generate defaults
	or error if an empty/missing file isn't tolerated.
*/
extern char* file_into_buf( char const* fname, uid_t* uid ) {
	struct stat	stats;
	off_t		fsize = 8192;	// size of the file
	off_t		nread;			// number of bytes read
	int			fd;
	char*		buf;			// input buffer
	
	if( uid != NULL ) {
		*uid = -1;				// invalid to begin with
	}
	
	if( (fd = open( fname, O_RDONLY )) >= 0 ) {
		if( fstat( fd, &stats ) >= 0 ) {
			if( stats.st_size <= 0 ) {					// empty file
				close( fd );
				fd = -1;
			} else {
				fsize = stats.st_size;						// stat ok, save the file size
				if( uid != NULL ) {
					*uid = stats.st_uid;					// pass back the user id
				}
			}
		} else {
			fsize = 8192; 								// stat failed, we'll leave the file open and try to read a default max of 8k
		}
	}

	if( fd < 0 ) {											// didn't open or empty
		//bleat_printf( 1, "open failed: %s: %s\n", fname, strerror( errno ) );
		if( (buf = (char *) malloc( sizeof( char ) * 128 )) == NULL ) {
			return NULL;
		}

		*buf = 0;
		return buf;
	}

	if( (buf = (char *) malloc( sizeof( char ) * fsize + 2 )) == NULL ) {
		close( fd );
		errno = ENOMEM;
		return NULL;
	}

	nread = read( fd, buf, fsize );
	if( nread < 0 || nread > fsize ) {							// too much or two little
		errno = EFBIG;											// likely too much to handle
		close( fd );
		return NULL;
	}

	//bleat_printf( 2, "read %d of %d from %s", nread, fsize, fname );
	buf[nread] = 0;

	close( fd );
	return buf;
}



/*
	Trim leading spaces.  If the resulting string is completely empty NULL
	is returned, else a pointer to a trimmed string is returned. The original
	is unharmed.
*/
extern char* ltrim( char* orig ) {
	char*	ch;			// pointer into buffer

	if( ! orig || !(*orig) ) {
		return NULL;
	}

	for( ch = orig; *ch && isspace( *ch ); ch++ );
	if( *ch ) {
		return strdup( ch );
	}

	return NULL;
}
