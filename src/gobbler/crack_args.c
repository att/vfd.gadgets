// :vi noet tw=4 ts=4:
/*
	Mnemonic:	crack_args.c
	Abstract:	Command line parser, and config file interface.
				Parses the command line argc/v passed in and 
				causes the config file to be parsed.  Any command
				line flags that need to be pushed into the config
				are, and the config pointer is returned.

				unit test with the config_test.c module.

	Author:		E. Scott Daniels
	Date:		02 February 2017	(shadow seen)
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gobbler.h"

/*
	Present a usage message.
*/
static void usage( void ) {
	const char *version = VERSION "    build: " __DATE__ " " __TIME__;

	fprintf( stdout, "anolis version %s\n", version );
	fprintf( stdout, "usage: anolis [-c config-file] [-d dump_size]  [-i] [-n] [-?]\n" );
	fprintf( stdout, "\t-c file -  supplies the name of the file to read as the configuration; /etc/switchboard/anolis.cfg assumed if missing\n" );
	fprintf( stdout, "\t-d n    -  dump first n bytes of each received packet\n" );
	fprintf( stdout, "\t-i      - interactive mode; prevents process from detaching the tty\n" );
	fprintf( stdout, "\t-n      - no harm mode; won't be distructive though exactly what that means is not defined\n" );
	fprintf( stdout, "\t-?      - display usage\n" );
}

/*
	Get the parm pointed to by pidx unless it's out of range. If oor
	then we abort and error. pidx is a pointer to the index of 
	the next parameter in argv to use. It must be >=1 and < argc.
*/
static char* get_nxt( int argc, char** argv, int* pidx ) {
	if( *pidx >= argc || *pidx <= 0 || argv[*pidx] == NULL ) {
		fprintf( stderr, "abort: missing command line data; unable to parse command line\n" );
		usage( );
		exit( 1 );
	}

	(*pidx)++;
	return argv[(*pidx-1)];
}

/*
	Crack the command line args, then parse the config file and return the overall 
	config structure with everything in order. We allow old school command lines
	only, all are legit:
			-inc /path/cfgfile
			-i -c /path/cfgfile -n
			-ni -c /path/cfg

	The cfg_fname parm is the default filename used if -c is not supplied. 
*/
extern config_t* crack_args( int argc, char** argv, char const* cfg_fname ) {
	config_t*	cfg;
	int		async = 1;			// -i keeps this interactive and attached to the tty
	int		forreal = 1;		// -n sets this to 0 preventing destructive things
	int		parg = 1;
	char*	opt;				// pointer into current token we are parsing
	char*	str;				// pointer to string that needs to be atoi'd
	int		flags;
	int		dump_size = 0;
	
	// we pull config from a file, not command line, so parse just the minimal things that 
	// need to come from the command line. We'll build a 'dpdk parsable' argv/argc later 
	// and pretend we got it from the command line.
	while( parg < argc ) {
		opt = argv[parg++];						// parg at the next parameter
		if( *opt != '-' || strcmp( opt, "--" ) == 0 ) {
			break;
		}

		for( opt++; *opt; opt++ ) {
			switch( *opt ) {
				case 'd':					// dump first n bytes of each packet
					str = get_nxt( argc, argv, &parg ); 
					dump_size = atoi( str );
					break;

				case 'i':					// interactive mode
					async = 0;
					break;

				case 'n':
					forreal = 0;						// do NOT actually make calls to change the nic
					break;

				case 'c':
					cfg_fname = get_nxt( argc, argv, &parg );	// get parm and inc parg
					break;

				case '?':
					usage();
					exit( 0 );
					break;


				default:
					fprintf( stderr, "unrecognised commandline flag: %c\n", *opt );
					usage();
					exit( 1 );
			}
		}
	}

	cfg = read_config( cfg_fname );
	if( cfg == NULL ) {
		fprintf( stderr, "abort: unable to generate a configuration with: %s\n", cfg_fname );
		exit( 1 );
	}

	flags = 0;		// flags set based on command line options
	if( async ) {
		flags |= CF_ASYNC;
	}
	if( forreal ) {
		flags |= CF_FORREAL;
	}
	cfg->flags |= flags;

	cfg->dump_size = dump_size;

	return cfg;
}
