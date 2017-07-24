		/*
		Mneminic:	config_test.c
	Abstract: 	Unit test for config.c module.
				Tests obvious things, may miss edge cases.
	Date:		07 May 2017
	Author:		E. Scott Daniels

	Mods:		29 Nov 2016 - Added qshare verification.
*/

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <errno.h>
#include <string.h>

#include "gadgetlib.h"
#include "gobbler.h"

#include "config.c"
#include "crack_args.c"
#include "lib_candidates.c"

#define SAFE_STR(s) (s==NULL?"<nil>":s)

static void print_cfg( config_t* cfg ) {
	int i;
	int j;
	vlan_set_t* vs;

	fprintf( stderr, "\t rx_devs: [ " );
	for( i = 0; i < cfg->nrx_devs; i++ ) {
		fprintf( stderr, " \"%s\" ",	cfg->rx_devs[i] );				
	}
	fprintf( stderr, " ]\n" );

	fprintf( stderr, "\t tx_devs: [ " );
	for( i = 0; i < cfg->ntx_devs; i++ ) {
		fprintf( stderr, " \"%s\" ",	cfg->tx_devs[i] );				
	}
	fprintf( stderr, " ]\n" );

	fprintf( stderr, "\t tx vlan sets: [ " );
	for( i = 0; i < cfg->ntx_devs; i++ ) {
		if( (vs = cfg->vlans[i] ) != NULL ) {
			fprintf( stderr, " [ " );
			for( j = 0; j < vs->nvlans; j++ ) {
				fprintf( stderr, " %d",	vs->vlans[j] );				
			}
			fprintf( stderr, " ] " );
		} else {
			fprintf( stderr, " [ ] " );
		}
	}
	fprintf( stderr, " ]\n" );

	fprintf( stderr, "\t logdir: %s\n",	cfg->log_dir );
	fprintf( stderr, "\t log_file: %s\n",	cfg->log_file );				
	fprintf( stderr, "\t log_keeep: %d\n",	cfg->log_keep );				
	fprintf( stderr, "\t log_level: %d\n",	cfg->log_level );				
	fprintf( stderr, "\t dpdk_log_level: %d\n",	cfg->dpdk_log_level );			
	fprintf( stderr, "\t lldelta: %d\n",	cfg->init_lldelta );			

	fprintf( stderr, "\t ds_vlan: %d\n",	cfg->ds_vlanid );				
	fprintf( stderr, "\t ds_mac: %s\n",	cfg->downstream_mac );			

	fprintf( stderr, "\t pid_fname: %s\n",	cfg->pid_fname );				
	fprintf( stderr, "\t cpu_mask: %s\n",	cfg->cpu_mask );				
	fprintf( stderr, "\t flags: %02x\n",	cfg->flags );					

	fprintf( stderr, "\t hw_vlan_strip: %d\n",	cfg->hw_vlan_strip );			
	fprintf( stderr, "\t mtu: %d\n",	cfg->mtu );					

	fprintf( stderr, "\t mbufs: %d\n",	cfg->mbufs );					
	fprintf( stderr, "\t rx_des: %d\n",	cfg->rx_des );					
	fprintf( stderr, "\t tx_des: %d\n",	cfg->tx_des );	
	fprintf( stderr, "\t lock_name: %s\n",	cfg->lock_name );				

}

int main( int argc, char** argv ) {
	char const*	fname = "test.cfg";
	config_t* cfg;
	int	rc = 0;

	if( argc > 1 && strcmp( argv[1], "-?" ) == 0 ) {
		fprintf( stderr, 
			"This is the unit test programme for the config module which includes\n"
			"the command line cracking code. To allow a full test of the cracking\n"
			"code the -? will now be passed to it and the usage message generated\n"
			"for anolis will be displayed; don't be confused :)   To that end, any\n"
			"command line flags that you can pass to anolis can be passed to this\n" 
			"unit test programme and those flags should affect the configuration\n"
			"which is generated (e.g. -n will turn off one flag in the config)\n\n\n"
		);
	}

	cfg = crack_args( argc, argv, fname );		// open file, fname if -c not supplied
	if( cfg == NULL ) {
		fprintf( stderr, "[FAIL] unable to read and/or parse config file %s: %s\n", fname, strerror( errno ) );
		rc = 1;
	} else {
		fprintf( stderr, "[OK]   attempt to parse cfg was valid. If file does not exist the output below should just be defaults\n" );
		print_cfg( cfg );
		free_config( cfg );

		cfg = read_config( "/hosuchdir/nosuchfile" );		// we should get a config with just the defaults
		if( cfg == NULL ) {
			fprintf( stderr, "[FAIL] attempt to open a nonexistant file did not return a default config as expected\n" );
		} else {
			fprintf( stderr, "[OK]   attempt to open a nonexistant file returned the following defaults:\n" );
			print_cfg( cfg );
			rc = 1;
		}

		free_config( cfg );
	}

	exit( rc );		// bad exit if we failed a test
}
