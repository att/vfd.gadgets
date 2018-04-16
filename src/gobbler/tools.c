/*
	Mnemonic:	tools.c
	Abstract:	Generic tool functions that are used throughout.
				These are mostly for debugging and initialisation
				and as such should generally not be called by a
				packet processing thread.
	Author:		E. Scott Daniels
	Date:		13 February 2017
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>


#include <rte_common.h>
#include <rte_ether.h>
#include <rte_cycles.h>

#include <gadgetlib.h>
#include "gobbler.h"

//------------------------------------------------------------------------------------------------------------

/*
	Simple dump of n bytes to stderr.
*/
extern void dump_octs( unsigned const char *op, int len ) {
	int k;
	int64_t sum = 0;

	fprintf( stderr, "%05x ", 0 );
	for( k = 0; k < len; k++ ) {
		sum += *op;
		fprintf( stderr, "%02x ", (unsigned int) *(op++) );
		if( (k+1) % 16 == 0 ) {
			fprintf( stderr, "\n%05x ", k+1 );
		}
	}

	//fprintf( stderr, "\nsum=%lld\n\n", (long long) sum );
	fprintf( stderr, "\n" );
	fflush( stderr );
}

/*
	Count bits returns the number of bits in a value which were on.
	E.g. if the CPU mask is specified as 0xc0, and is passed to this
	function, the return would be 2 as there are two possible CPUs
	which may be used.  Len is the size in bytes, of the data that
	is to be counted.
*/
extern int count_bits( void const* vdata, int len ) {
	int		count = 0;
	char	bdata;		// copy of the data so we are not distructive
	const char*	data;		// we'll look at the data as a series of bytes
	int		i;
	
	data = (const char *) vdata;
	while( len > 0 ) {
		bdata = *((const char *) data);
		if( bdata ) {						// skip if zero
			for( i = 0; i < 8; i ++ ) {
				count += bdata & 0x01;
				bdata >>= 1;
			}	
			data++;
		}

		len--;
	}

	return count;
}

/*
	Convert a human readable ipv6 string into bytes. Caller must 
	ensure that target (bytes) has enough space for 16 bytes.  A pointer
	to the target is returned as a convenience. Str is a nil terminated
	ascii string of the form (standard V6 address) such as:
		xxxx:xxxx::xxxx:xxxx:xxxx

	While the source is unchanged on exit of this function, this function 
	DOES fiddle the string during processing to avoid a strdup, so this
	function is thread safe ONLY if each thread has its own source string.
*/
extern uint8_t* ipv6str2bytes( char* str, uint8_t* bytes ) {
	int	ccount = 0;			// number of colons in source
	char*	c;				// pointer into the string
	int		i;
	int		val;

	if( str == NULL || bytes == NULL ) {
		return NULL;
	}

	for( c = str; *c; c++ ){		// count colons so when we hit :: we know how many to skip
		if( *c == ':' ) {
			ccount++;
		}
	}
	
	memset( bytes, 0, V6_ADDR_LEN );
	i = 0;
	c = str;
	while( i < V6_ADDR_LEN && *c ) {
		if( *c == ':' ) {
			i += 2 * ( 8 - ccount);
			ccount = 0;
			c++;
		} else {
			val = 0;
			
			while( *c && *c != ':' ) {				// accept xxxx: xxx: xx: or x:
				val <<= 4;
				if( isxdigit( *c ) ) {
					val += tolower(*c) - ((*c > '9') ? ('a'-10) : '0');
				}
				c++;
			}
			c++;

			bytes[i+1] = (uint8_t) (val & 0x0ff);
			bytes[i] = (uint8_t) ((val>>8) & 0x0ff);

			i += 2;
		}
	}

	return bytes;
}

/*
	Accepts a mac address in the form xx:xx... and converts it to 
	an integer representation that ether_* functions in dpdk can 
	use. If out_buf is NULL, one is allocated, otherwise the user
	buf is used and it it the caller's responibility to endure that
	it is at leaset 6 bytes in length.
*/
extern unsigned char* macstr2buf( unsigned char const* mstr, unsigned char *out_buf) {
	unsigned char*	mdup = NULL;		// duplicate for us to trash
	unsigned char*	tok;
	char*	tok_data = NULL;
	unsigned char*	ob_ele;				// element in the buffer
	int		need = ETHER_ADDR_LEN;		// no more than this please
	int		alloc_here = 0;				// if we allocate, we must free on error

	if( (mdup = (unsigned char *) strdup( (char const *) mstr )) == NULL ) {			// shouldn't happen, but parnoia saves lives
		return NULL;
	}
	if( out_buf == NULL ) {			// allocate if they didn't pass one
		if( (out_buf = (unsigned char *) malloc( ETHER_ADDR_LEN * sizeof( unsigned char ))) == NULL ) {
			free( mdup );
			return NULL;
		}
		alloc_here = 1;
	}
	memset( out_buf, 0, sizeof( unsigned char ) * ETHER_ADDR_LEN );
	ob_ele = out_buf;

	tok_data = (char *) mdup;
	while( need > 0 && (tok = (unsigned char *) strtok_r( NULL, ":", &tok_data )) != NULL ) {
		if( isxdigit( *tok ) ) {
			if( strlen( (char *) tok ) == 1 ) {				// allow :a: and :0a: to be the same
				*ob_ele = 0;
			} else {
				*ob_ele = (tolower(*tok) - ((*tok > '9') ? ('a'-10) : '0')) << 4;
			}
			*ob_ele |= (tolower(*(tok+1)) - ((*(tok+1) > '9') ? ('a'-10) : '0'));
		} else {
			if( alloc_here ) {
				free( out_buf );
			}
			free( mdup );
			return NULL;
		}

		ob_ele++;
		need--;
	}
	
	free( mdup );
	if( need != 0 ) {		// bad -- not enough digits
		if( alloc_here ) {
			free( out_buf );
		}
		return NULL;
	}

	return out_buf;
}

/*
	Convert a dotted v4 ip address to 32 bit integer.
*/
extern uint32_t ipv42int( unsigned char const* ip_str ) {
	unsigned char*	mdup;		// duplicate for us to trash
	unsigned char*	tok;
	char*	tok_data = NULL;
	//unsigned char*	ob_ele;				// element in the buffer
	int			in = 0;					// number inserted
	uint32_t	result = 0;
	uint32_t	val;

	if( (mdup = (unsigned char *) strdup( (char const *) ip_str )) == NULL ) {			// shouldn't happen, but parnoia saves lives
		return 0;
	}

	tok_data = (char *) mdup;
	while( in < 4 && (tok = (unsigned char *) strtok_r( NULL, ".", &tok_data )) != NULL ) {
		val = (uint32_t) atoi( (char *) tok );
		if( val > 255  ) {							// bail if not valid
			return 0;
		}
		result += val << (in * 8);
		in++;
	}

	if( in < 4 ) {			// didn't get exactly 4
		return 0;
	}
	
	free( mdup );
	return result;
}


// these can't be unit tested and prevent the test binary from linking
// so exclude when building the test binary. All functions above here 
// should be capable of being unit tested.
#ifndef TEST_BUILD

/*
	Given a pointer to a mac address struct, format them into a
	human readable string.  Caller expected to free the buffer.
*/
extern char* mac_to_string( struct ether_addr const*  mac_addr ) {
	char buf[64];								// more than enough


	buf[0] = 0;
	if( mac_addr != NULL ) {
		snprintf( buf, sizeof( buf ), "%02x:%02x:%02x:%02x:%02x:%02x", 
			mac_addr->addr_bytes[0],  mac_addr->addr_bytes[1],  mac_addr->addr_bytes[2],
			mac_addr->addr_bytes[3],  mac_addr->addr_bytes[4],  mac_addr->addr_bytes[5] );
	}

	return strdup( buf );
}

/*
	Get_mac_string reuests the mac for the given port id
	and then unbundles it from the returned sruct into a
	z-terminated string.  Caller is expected to free the 
	returned string.
*/
extern char* get_mac_string( int portid ) {
	struct ether_addr mac_addr;

	memset( &mac_addr, 0, sizeof( mac_addr ) );
	rte_eth_macaddr_get( portid, &mac_addr );
	return mac_to_string( &mac_addr );
}

/*
	Query the link state for a single interface and set the IFFL_LINK_UP
	flag if it's up. Also returns true if the link is up.
*/
static int query_lstate( iface_t* iface ) {
	struct rte_eth_link link_info;				// link info back from rte
	int rc = 0;									// return code -- 1 == up

	memset( &link_info, 0, sizeof( link_info ) );
	rte_eth_link_get_nowait( iface->portid, &link_info );
	if( link_info.link_status != ETH_LINK_DOWN ) {
		iface->flags |= IFFL_LINK_UP;
		rc = 1;
		// we could set speed in iface if we need to
	}

	return rc;
}

/*
	Wait for all of the configured interfaces to show link up.
	Returns 1 if they all came up, 0 if there was an error or timeout.
	For now this is a simple, brute force, check as we only have two
	ports to worry about.
*/
extern int all_links_up( context_t* ctx, int timeout_sec ) {
	int waited = 0;					// time already waited
	int max_wait = 0;				// computed max wait time (sec * 10) * loop_delay(100ms)
	int	all_up = 1;
	int i;
	int	tx_up = 0;
	int	rx_up = 0;

	if( ctx == NULL  ) {
		bleat_printf( 0, "cannot wait for links with nil ctx" );
		return 0;
	}

	for( i = 0; i < ctx->nrxifs; i++ ) {
		ctx->rx_ifs[i]->flags &= ~IFFL_LINK_UP;
	}
	for( i = 0; i < ctx->ntxifs; i++ ) {
		ctx->tx_ifs[i]->flags &= ~IFFL_LINK_UP;
	}

	bleat_printf( 1, "waiting up to %d seconds for links to come ready", timeout_sec );
	for( max_wait = timeout_sec * 1000; waited < max_wait; waited += 100  ) {
		if( ! ok2run ) {
			return 0;
		}

		all_up = 1;
		if( tx_up < ctx->ntxifs ) {						// not all tx links were up; try again
			for( i = 0; i < ctx->ntxifs; i++ ) {
				if( ! (ctx->tx_ifs[i]->flags | IFFL_LINK_UP) ) {
					if( ! query_lstate( ctx->tx_ifs[i] ) ) {
						all_up = 0;
					} else {
						ctx->tx_ifs[i]->flags |= IFFL_LINK_UP;
						tx_up++;
					}
				}
			}
		}

		if( rx_up < ctx->nrxifs ) {						// not all rx links were up; try again
			for( i = 0; i < ctx->nrxifs; i++ ) {
				if( ! (ctx->rx_ifs[i]->flags | IFFL_LINK_UP) ) {
					if( ! query_lstate( ctx->rx_ifs[i] ) ) {
						all_up = 0;
					} else {
						ctx->rx_ifs[i]->flags |= IFFL_LINK_UP;
						rx_up++;
					}
				}
			}
		}

		if( all_up ) {
			bleat_printf( 1, "all links are up" );
			return 1;
		}

		rte_delay_ms( 100 );			// delay 100ms before looping
	}

	bleat_printf( 1, "wrn: timeout -- some links links did not report up.  %d tx links up, %d rx links up", tx_up, rx_up );
	return 1;		// timeout if we return here
}


/*
	Return the number of logical cores which are available.
*/
extern int count_avail_cores( void ) {
	int i;
	int count = 0;

	for( i = 0; i < RTE_MAX_LCORE; i++ ) {			// find a core from the mask that's available
		if( rte_lcore_is_enabled( i ) ) {
			count++;
		}
	}

	return count;
}

/*
	Given a starting value, get the next available logical 
	core.  Returns < 0 on error, else the lcore number.
*/
extern int get_next_core( int start ) {
	int i;

	if( start < 0 ) {		// enforce sanity
		start = 0;
	} else {
		if( start >= RTE_MAX_LCORE ) {
			return -1;
		}
	}

	for( i = start; i < RTE_MAX_LCORE; i++ ) {			// find next core from the mask that's available
		if( rte_lcore_is_enabled( i ) ) {
			return i;
		}
	}

	return -1;
}

// -------- simulation: if -s xxx is passed in we simulate what ever xxx maps to
// Generally return values from these functions are 1 for OK and 0 for failure.

/*
	Simulate pooly written application which over uses the check link
	state with the wait option (causes lots of mailbox messages).
	Will drive the call for 'loops' number of times.
*/
static int sim_bad_link_chk( iface_t* iface, int loops ) {
	struct rte_eth_link link_info;				// link info back from rte

	memset( &link_info, 0, sizeof( link_info ) );

	bleat_printf( 1, "link_stat 'over use' simulation starts for %d loops", loops );
	while( loops > 0 ) {
		rte_eth_link_get( iface->portid, &link_info );
		loops--;
	}

	bleat_printf( 1, "link_stat 'over use' simulation complete" );
	return 1;
}

/*
	
	Figure out what simulation was asked for and if we recognise it
	run it. Return value is the value that the function returns.
*/
extern int run_sim( context_t* ctx, char* sim_id ) {
	if( sim_id == NULL ) {
		return 0;
	}

	if( strcmp( sim_id, "link_stat" ) == 0 ) {
		return sim_bad_link_chk( ctx->rx_ifs[0], 10000000 );
	}

	return 0;
}
#endif
