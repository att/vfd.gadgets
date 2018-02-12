// :vi noet tw=4 ts=4:
// This module was derrived from github/att/vfd and thus the following licence 
// applies, and this statement meets the obligaion to clearly state that there
// have been changes to the original work.

/*
 ---------------------------------------------------------------------------
   Copyright (c) 2016 AT&T Intellectual Property

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at:

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
 ---------------------------------------------------------------------------
*/


/*
	Mnemonic:	config.c
	Abstract:	Functions to read and parse any config file we might need.
	Author:		E. Scott Daniels
	Date:		1 Feburary 2011

	Mods:
*/

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>

#include <gadgetlib.h>
#include "gobbler.h"
#include "lib_candidates.h"


// -------------------------------------------------------------------------------------
// safe free (free shouldn't balk on nil, but don't chance it)
#define SFREE(p) if((p)){free(p);}			

// Ensure low <= v <= high and return v == low if below or v == high if v is over.
#define IBOUND(v,low,high) ((v) < (low) ? (low) : ((v) > (high) ? (high) : (v)))

typedef enum { FMT_FLOAT=0, FMT_INT, FMT_HEX } formats_t;

// --------------------------- utility   --------------------------------------------------------------

/*
	Look up a boolean returning the default if it's not a boolean or not defined.
*/
static inline int get_bool( void* jblob, char const* field_name, int def_value ) {
	if( !jw_is_bool( jblob, field_name ) ) { 
		return def_value;
	}
	
	return jw_value( jblob, field_name );
}

/*
	Return the value from the json blob, or the default if it is not a value or is not 
	defined.
*/
static inline float get_value( void* jblob, char const* field_name, float def_value ) {
	if( !jw_is_value( jblob, field_name ) ) { 
		return def_value;
	}
	
	return jw_value( jblob, field_name );
}

/*
	There are cases where we need to pass a 'value' to dpdk as a string rather than 
	int/float. In the json, it makes more sense to the user to define these as values
	(e.g. "mem": 50) rather than quoting the value. This function converts a value
	to string if it is not passed in the json as a string (we will accept either).
	The fmt parm indicates the representation (%f, %d or 0x%02x), use FMT_ constants.
*/
static inline char* get_value_as_str( void* jblob, char const* field_name, char const* def_value, formats_t fmt ) {
	float	jvalue;				// value from json
	char	stuff[128];			// should be more than enough :)
	char*	jstr;				// if represented in json as a string

	if( jw_is_value( jblob, field_name ) ) { 
		jvalue = jw_value( jblob, field_name );
		switch( fmt ) {
			case FMT_INT:
				snprintf( stuff, sizeof( stuff ), "%d", (int) jvalue );
				break;
			case FMT_FLOAT:
				snprintf( stuff, sizeof( stuff ), "%f", jvalue );

			case FMT_HEX:
				snprintf( stuff, sizeof( stuff ), "0x%x", (int) jvalue );
		}
		return strdup( stuff );
	}

	if( (jstr = jw_string( jblob, field_name )) ) {		// if already a string, return that
		return strdup( jstr );
	}

	if( def_value ) {					// neither way in json, so use default if supplied
		return strdup( def_value );
	}
			
	return NULL;
}

/*
	Looks up field name and returns the value in jblob if its a string
	(with leading spaces trimmed). If it's not a string in the json, 
	then the def_value string is duplicated and returned.
*/
static inline char* get_str( void* jblob, char const* field_name, char const* def_value ) {
	char*	stuff;

	if( (stuff = jw_string( jblob, field_name )) ) {
		return ltrim( stuff );
	}

	if( def_value ) {
		return strdup( def_value );
	}

	return NULL;
}

/*
	Create an array of string pointers and populate it with what is in the json blob
	with the given name. Passes the array pointer back using target, and returns
	the number of elements.  The variable in the json may be an array of strings or 
	may be a single string which will be captured as a one element array.
*/
static int dig_string_array( void* jblob, char const* array_name, char*** target ) {
	int i;
	int nele;			// number of elements in the array
	char**	earray;		// array of pointers to the strings
	char*	blob_str;	// pointer to string in blob; we need to dup so blob free doesn't harm

	*target = NULL;
	bleat_printf( 2, "digging %d strings from %s", jw_array_len( jblob, array_name ), array_name );
	if( (nele = jw_array_len( jblob, array_name )) > 0 ) {				// json has an array
		if( (earray =  malloc( sizeof( char * ) * nele )) != NULL ) {
			for( i = 0; i < nele; i++ ) {
				if( (blob_str = jw_string_ele( jblob, array_name, i )) != NULL ) {
					earray[i] = strdup( blob_str );
				} else {
					earray[i] = NULL;
				}
			}
		} else {
			return 0;
		}

		*target = earray;
		return nele;
	} else {															// element in json is a single element
		if( (earray =  malloc( sizeof( char *) )) != NULL ) {
			if( (blob_str =  get_str( jblob, array_name, NULL )) != NULL ) {
				earray[0] = strdup( blob_str );
			} else {
				earray[0] = NULL;
			}
		} else {
			return 0;
		}

		*target = earray;
		return nele;
	}

	return 0;
}



/*
	Dig out the white-mac array and create the array in the config.
*/
static int dig_white_macs( void* jblob, config_t* config ) {
	unsigned char**	macs;			// list of mac addresses from the json
	int		nmacs;			// number
	int		i;

	nmacs = dig_string_array( jblob, "white_macs", (char ***) &macs );
	if( nmacs <= 0 ) {
		bleat_printf( 1, "no white list mac addresses found in config; will generate random addresses" );
		return 0;
	}

	if( (config->white_macs = (struct ether_addr *) malloc( sizeof( struct ether_addr ) * nmacs )) == NULL )  {
		bleat_printf( 0, "CRI: unable to allocate mac white list for %d macs", nmacs );
		return 0;
	}

	bleat_printf( 1, "%d white list mac addresses found in config", nmacs );
	for( i = 0; i < nmacs; i++ ) {
		macstr2buf( macs[i], (unsigned char *) &config->white_macs[i] );
		free( macs[i] );	
	}

	free( macs );
	return nmacs;
}

/*
	Dig out the vlan set and the device names associated with the Tx devices.
	The return is two pointers: [0]->device name array, [1]->vlan_set_t and 
	[2]->mac_set_t.
	The caller must free the array after using the pointers.

	We expect to find json like this:
			tx_defs [
				{ address: "pci-string", vlanids: [1, 2, .... n], macs[ "m1", "m"...] },
				{ address: "pci-string", vlanids: [1, 2, .... n] },
				...
			]

	where 1, 2... are vlan ids, and pci-string is a pci address of the form: xxxx:yy:zz.a  
*/
static  void* dig_tx_info( void* config ) {
	void**	mret;
	char**	dev_addrs;		// pci addresses
	vlan_set_t** vset;		// set of vlans collected from the json
	mac_set_t** mset;		// set of mac addresses collected from the json
	void*	tblob;			// tx_dev blob from the config
	int		i;
	int		j;				// index into dev_addrs
	int		ndevs;			// number of devices defined in array

	mret = (void *) malloc( sizeof( void * ) * 3 );							// allocate the return list
	dev_addrs = (char **) malloc( sizeof( char * ) * 64 );					// array of device name pointers to return
	mret[0] = (void *) dev_addrs;
	memset( dev_addrs, 0, sizeof( char * ) * 64 );

	mret[1] = vset = (vlan_set_t **) malloc( sizeof( *vset ) * 64 );		// array of vset pointers to return
	memset( vset, 0, sizeof( *vset ) );

	mret[2] = mset = (mac_set_t **) malloc( sizeof( *mset ) * 64 );			// array of mac sets
	memset( mset, 0, sizeof( *mset ) );

	if( (ndevs = jw_array_len( config, "tx_devs" )) <= 0 ) {
		return mret;
	}

	j = 0;
	for( i = 0; i < ndevs; i++ ) {
		int		nthings;															// number of vlans in the set
		int		k;
		char*	addr;
		vlan_set_t*	vs;
		mac_set_t* ms;

		if( (tblob = jw_obj_ele( config, "tx_devs", i )) != NULL ) {
			if( (addr = jw_string( tblob, "address" )) != NULL ) {						// get the pci address for this tx device
				dev_addrs[j] = strdup( addr );
			} else {
				dev_addrs[j] = strdup( "dup" );
			}

			if( (nthings = jw_array_len( tblob, "vlanids" )) > 0 ) {					// if there are vlan ids defined suss them out
				vs = (vlan_set_t *) malloc( sizeof( *vs ) );
				memset( vs, 0, sizeof( *vs ) );
				vset[j] = vs;
				vs->nvlans = nthings;
				vs->vlans = (uint16_t *) malloc( sizeof( uint16_t ) * nthings );

				for( k = 0; k < nthings; k++ ) {										// pull each and add to vlan set
					if( jw_is_value_ele( tblob, "vlanids", k ) ) {
						vs->vlans[k] = jw_value_ele( tblob, "vlanids", k );			// snarf it
					}
				}
			}

			if( (nthings = jw_array_len( tblob, "macs" )) > 0 ) {					// if there are vlan ids defined suss them out
				ms = (mac_set_t *) malloc( sizeof( *ms ) );
				memset( ms, 0, sizeof( *ms ) );
				mset[j] = ms;
				ms->nmacs = nthings;
				ms->macs = (struct ether_addr *) malloc( sizeof( struct ether_addr ) * nthings );

				for( k = 0; k < nthings; k++ ) {														// pull each and add to mac set
					if( (addr = jw_string_ele( tblob, "macs", k )) != NULL ) {					// snarf it, leave 0s if not
						macstr2buf( (unsigned char const*) addr, &ms->macs[k].addr_bytes[0] );
					}
				}
			}

			j++;
		}
	}

	return mret;
}

/*
	Open the file, and read the json there returning a populated structure from
	the json bits we expect to find.

	Primative type checking is done, and if the expected value for a field isn't the expected
	type, then the default value is generally used.  For example, if the field 'stuff' should 
	be a boolean, but "stuff": goo  is given in the json, the result is a bad type for 'stuff' 
	and the default value is used. This keeps our code a bit more simple and puts the 
	responsibility of getting the json correct on the 'user'. In some cases the default value 
	is a bad value which might trigger an error in the code which is making use of this library.

	Primative types are those types returned by the json parser as value, boolean or NULL.

	The json should look like this:
		{
			log_level:		<value>,
			dpdk_log_level: <value>,
			init_lldelta:	<value>,
			log_keep:		<value>,
			log_dir:		<string>,
			log_file:		<string>,

			pid_fname:		<string>,
			cpu_mask:		<value|string>,		# can be a string like "0x0a" or just integer like 10

			gen_macs:		<boolean>,				# if true then we generate a few whitelist mac addresses
			white_macs:		[ "m1", ..., "mn" ],	# generated macs if gen_macs true; random if list is omitted

			mem_chans:		<value>,
			huge_pages:		<boolean>,			# testing only; default is true
			tot_mem:		<value>,			# total dpdk memory to allocate for the rpocess

			#---- network interfaces -----------
			rx_devs:		[ <string>[,...] ]				# one or more device names (PCI addrs) that we should listen to
			//deprecated tx_devs:		[ <string>[,...] ]	# one or more device names (PCI addrs) that we should transmit on
			tx_devs: 		[
					{
						address: <string>,			# address of the device
						vlanids: [<int>,...]		# downstream VLAN IDs when transmitting
					}...
			]
			duprx2tx:		<bool>,				# duplicates rx_interfaces as tx interfaces
			ds_vlanid:		<value>				# default vlan id put into output packets; 0 means no change
			downstream_mac: <string>,			# mac address where downstream packets are forwarded
			xmit_type:		<string>,			# drop, rts, forward


			# applied to all inerfaces
			mtu:			<value> 			# (default 1500)
			mem:			<value>				# meg
			hw_vlan_strip:	<boolean>   		#(default false)
			mbufs:			<value>
			rx_des:			<value>				# number of rx ring decscriptors
			tx_des:			<value>				# number of tx ring decscriptors
		}
*/
extern config_t* read_config( char const* fname ) {
	config_t*	config = NULL;
	void*		jblob;			// parsed json
	char*		buf;			// buffer read from file (nil terminated)
	char*		cp;				// pointer into a string
	int			i;
	void**		mret;			// multiple return value

	if( (buf = file_into_buf( fname, NULL )) == NULL ) {
		return NULL;
	}

	if( *buf == 0 ) {											// empty/missing file
		free( buf );
		buf = strdup( "{ \"empty\": true }" );					// dummy json to parse which will cause all defaults to be set
	}

	if( (jblob = jw_new( buf )) != NULL ) {						// json successfully parsed
		if( (config = (config_t *) malloc( sizeof( *config ) )) == NULL ) {
			errno = ENOMEM;
			return NULL;
		}
		memset( config, 0, sizeof( *config ) );					// probably not needed, but we don't do this frequently enough to worry


		// logging and process stuff
		config->log_level = (int) get_value( jblob, "log_level", 0 );				// our internal bleat level
		config->dpdk_log_level = (int) get_value( jblob, "dpdk_log_level", 0 );		// general log level for dpdk 
		config->init_lldelta = (int) get_value( jblob, "init_lldelta",  1 );		// added to general bleat/log levels during initialisation
		config->log_keep = (int) get_value( jblob, "log_keep",  30 );				// number of days worth of logs to keep
		config->log_file = get_str( jblob, "log_file", "anolis.log" );
		config->pid_fname = get_str( jblob, "pid_fname", "/var/run/anolis.pid" );
		config->log_dir = get_str( jblob, "log_dir", "/var/log/switchboard" );
		config->lock_name= get_str( jblob, "lock_name", "anolis" );

		config->ds_vlanid = (int) get_value( jblob, "ds_vlanid", 0 );				// our internal bleat level

		config->downstream_mac = get_str( jblob, "downstream_mac", NULL );			// downstream mac to foward packets to
		config->mtu = get_value( jblob, "mtu", 1500 );								// mtu max for Rx; if >1500 jumbo is automatically enabled
		config->hw_vlan_strip = get_bool( jblob, "hw_vlan_strip", FALSE );			// hardware strips VLAN (needed for non-vfd vfs)
		config->duprx2tx = get_bool( jblob, "duprx2tx", FALSE );					// forces rx interfaces to double as tx interfaces

		config->mem = (int) get_value( jblob, "mem", 0 );							// meg of memory to allocate from huge pages
		config->mbufs = get_value( jblob, "mbufs", 4096 );							// number of mbufs to allocate
		config->rx_des = get_value( jblob, "rx_des", 1024 );						// size of rx ring, number of descriptors
		config->tx_des = get_value( jblob, "tx_des", 2048 );						// size of tx ring, number of descriptors
		config->lock_name = get_str( jblob, "lock_name", "gobbler" );				//  name used to prevent dup processes

		config->xmit_type = DROP;
		cp = get_str( jblob, "xmit_type", "drop" );									// type of rebroadcast
		if( strcmp( cp, "rts" ) == 0 ) {
			config->xmit_type = RETURN_TO_SENDER;
		} else {
			if( strcmp( cp, "forward" ) == 0  ) {
				config->xmit_type = SEND_DOWNSTREAM;
			}
		}

		if( get_bool( jblob, "gen_macs", FALSE ) == FALSE ) {
			config->flags &= ~CF_GEN_MACS;
		} else {
			config->flags |= CF_GEN_MACS;
			config->nwhite_macs = dig_white_macs( jblob, config );							// if on, see if there are any macs supplied and capture them 
		}

		if( get_bool( jblob, "huge_pages", TRUE ) == FALSE ) {
			config->flags &= ~CF_HUGE_PAGES;
		} else {
			config->flags |= CF_HUGE_PAGES;
		}

		if( get_bool( jblob, "promiscuous", FALSE ) == FALSE ) {
			config->flags &= ~CF_PROMISC;
		} else {
			config->flags |= CF_PROMISC;
		}

		config->cpu_mask = get_value_as_str( jblob, "cpu_mask", NULL, FMT_HEX );	// default is applied in the initialisation function

		if( *config->log_file == '/' && 
			(cp = strrchr( config->log_file, '/' )) != NULL &&
			cp != config->log_file) {							// fully qualified name (./ is assumed to be a path under log_dir)
			char* tp;

			free( config->log_dir );
			tp = strdup( config->log_file );
			*cp = 0;
			config->log_dir = config->log_file;					// points at the now 'truncated' string
			config->log_file = tp;								// the fully qualified log file name
		} else {
			char	wbuf[2048];

			if( strcmp( config->log_file, "stderr" ) ) {					// append directory name only if stderr is not the indicated file
				snprintf( wbuf, sizeof( wbuf ), "%s/%s", config->log_dir, config->log_file );
				config->log_file = strdup( wbuf );
			}
		}

		config->nrx_devs = dig_string_array( jblob, "rx_devs", &config->rx_devs );					//  list of tx/rx devices
		//if( (config->ntx_devs = dig_string_array( jblob, "tx_devs", &config->tx_devs )) < 0 ) {		// not defined, so it will be -1; we need it to be 0
			//config->ntx_devs = 0;
		//}
		

		if( config->nrx_devs > 0 ) {
			config->rx_ports = (int *) malloc( sizeof( int ) * config->nrx_devs );				// must do last as we depend on number of things in arrays
			for( i = 0; i < config->nrx_devs; i++ ) {
				config->rx_ports[i] = -1;
			}
		}

		// ---- dig out the more complicated Tx info --------

		if( (config->ntx_devs = jw_array_len( jblob, "tx_devs" )) > 0 ) {
			if( (mret = dig_tx_info( jblob )) != NULL ) {						// got some tx info mret[0] == address list, [1] == vlan set array
				config->tx_devs = (char **) mret[0];
				config->vlans = (vlan_set_t **) mret[1];
				config->macs = (mac_set_t **) mret[2];
			}		

			config->tx_ports = (int *) malloc( sizeof( int ) * config->ntx_devs );
			for( i = 0; i < config->ntx_devs; i++ ) {
				config->tx_ports[i] = -1;
			}
		}
/*
		if( config->ntx_devs > 0 ) {
		}
*/

		jw_nuke( jblob );
	} else {
		fprintf( stderr, "internal mishap parsing json blob\n" );
	}

	free( buf );
	return config;
}

/*
	Cleanup a parm block and free the data.
*/
extern void free_config( config_t* config ) {
	int i; 

	if( ! config ) {
		return;
	}

	SFREE( config->log_dir );
	SFREE( config->log_file );
	SFREE( config->pid_fname );
	SFREE( config->cpu_mask );

	SFREE( config->tx_ports );
	SFREE( config->rx_ports );
	SFREE( config->white_macs );

	for( i = 0; i < config->ntx_devs; i++ ) {
		// do NOT free vsets or msets as those pointers are passed out of the config for use later
		SFREE( config->tx_devs[i] );
	}
	for( i = 0; i < config->nrx_devs; i++ ) {
		SFREE( config->rx_devs[i] );
	}

	free( config );
}

