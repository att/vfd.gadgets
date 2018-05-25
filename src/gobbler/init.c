/*
	Mnemonic:	init.c
	Abstract:	Initialisation functions which cover dpdk initialisation, making the 
				process a daemon, etc.  A small set ot termination functions also exist
				here.
	Author:		E. Scott Daniels
	Date:		1 February 2017
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
#include <rte_malloc.h>
#include <rte_memory.h>
#include <rte_memcpy.h>
#include <rte_memzone.h>
#include <rte_eal.h>
#include <rte_ether.h>
#include <rte_ethdev.h>

#include <rte_ip.h>
#include <rte_pci.h>
#include <rte_bus_pci.h>

#include <gadgetlib.h>
#include "gobbler.h"

#define ARGV_LEN	64 			// this should be enough for the dummy argv built for eal init


/*
	This will write all of the mac addresses in the list to the given nic.

	NOTE:  we cannot really check to see if we've already pushed a list
		to a port which shares the same PF as another port. Because 
		duplicate MAC addresses aren't allowd on the same PF, a second
		push will fail.  Allowing the second push allows for testing the
		control function (e.g. VFd) to ensure it's recognising duplicates
		and doing the right thing. 
*/
static void push_whitelist_macs( int device, int nmacs, char** macs ) {
	struct ether_addr ma;
	int mai;		// index
	int state;

	bleat_printf( 1, "hacking in mac addresses" );

	for( mai = 0; mai < nmacs; mai++ ) {
		if( macs[mai] ) {
			macstr2buf( (unsigned char *) macs[mai], (unsigned char *) &ma );								// convert human string to raw bytes
			if( (state = rte_eth_dev_mac_addr_add( device, &ma, 0 ) ) < 0 ) {
				bleat_printf( 0, "WRN: whitelist mac add failed for: %s state=%d", macs[mai], state );
			} else {
				bleat_printf( 0, "whitelist mac added: %s", macs[mai] );
			}
		}
	}
}

/*
	Test function to vet parms that will be passed on eal init call.
*/
static void print_dpdk_initstring( int argc, char** argv ) {
	int i;

	bleat_printf( 2,  "eal_init parm list: %d parms", argc );
	for( i = 0; i < argc; i++ ) {
		bleat_printf( 2, "[%d] = (%s)", i, argv[i] );
	}

	if( argv[argc] != NULL ) {
		bleat_printf( 2, "ERR:  the last element of argc wasn't nil" );
	}
}

/*
	Insert a flag/value pair, or just a flag, into the target array, and advance the index 
	accordingly.  If value is nil, then just the flag is inserted.  If an attempt to insert
	beyond the max size, then the process is aborted.
*/
static void insert_pair( char** target, int *index, int max, char const* flag, char const* value ) {
	if( *index < max-2 ) {
		target[*index] = strdup( flag );
		if( value != NULL ) {
			target[(*index)+1] = strdup( value );
			*index += 2;
		} else {
			*index += 1;
		}
	} else {
		bleat_printf( 0, "abort: unable to squeeze parms into dpdk initialisation target" );
		exit( 1 );
	}
}

/*
	So that we may be config file driven we will construct a psuedo command line and give that to DPDK
	letting it think it came in from the command line.  This function takes our config and builds the
	command line then invokes the DPDK initialisation function to do it's thing.  We expect the return
	value to be >0 to indicate success.

	We strdup all of the argument strings that are eventually passed to dpdk as the man page indicates that
	they might be altered, and that we should not fiddle with them after calling the init function. Thus we
	give them their own copy, and suffer a small leak.
	
	This function causes a process abort if any of the following are true:
		- unable to alloc memory
		- no vciids were listed in the config file
		- dpdk eal initialisation fails

	Memory leak:  This process will cause a small memory leak as the parms which are given to the dpdk
		library are strdup'd strings which dpdk will never free, and might drop the references
		to.  The DPDK doc insists that the user programme not access the parms given to the eal_init()
		function after passed, so they will leak.
*/
extern int initialise_dpdk( config_t* cfg ) {
	int		argc = 0;					// argc/v parms we dummy up
	char**	argv = NULL;
	int		i;
	char	wbuf[128];				// scratch buffer
	long	min_mem = 0;			// minimum memory required	

	if( cfg->nrx_devs <= 0  ) {
		bleat_printf( 0, "CRI: abort: must supply at least one receive device and 0 or more tx devices" );
		exit( 1 );
	}

	if( (argv = (char **) malloc( sizeof( char* ) * ARGV_LEN )) == NULL ) {
		bleat_printf( 0, "CRI: abort: unable to alloc memory for dpdk initialisation" );
		exit( 1 );
	}
	memset( argv, 0, sizeof( char* ) * ARGV_LEN );

	argv[argc++] = strdup(  "anolis" );						// dummy up a command line to pass to rte_eal_init()

	if( cfg->cpu_mask != NULL ) {
		i = (int) strtol( cfg->cpu_mask, NULL, 0 );			// assume it's something like 0x0a
		if( i <= 0 ) {
			free( cfg->cpu_mask );						 		// free and use default below
			cfg->cpu_mask = NULL;
		}
	}
	if( cfg->cpu_mask == NULL ) {					// pick a CPU if they gave 0 or neg value
		cfg->cpu_mask = strdup( "0x04" );
	} else {
		if( *(cfg->cpu_mask+1) != 'x' ) {														// not something like 0xff
			snprintf( wbuf, sizeof( wbuf ), "0x%02x", atoi( cfg->cpu_mask ) );				// assume integer as a string given; cvt to hex
			free( cfg->cpu_mask );
			cfg->cpu_mask = strdup( wbuf );
		}
	}
	
	if( cfg->mem > 0 ) {
		min_mem = cfg->mem;
	} else {
		min_mem = 200 + (cfg->ntx_devs * 10) + (cfg->nrx_devs * (cfg->duprx2tx ? 20 : 10));
	}
	bleat_printf( 1, "setting memory size to %ld", min_mem );
	

	insert_pair( argv, &argc, ARGV_LEN, "-c", cfg->cpu_mask );
	insert_pair( argv, &argc, ARGV_LEN, "-n", "1" );
	snprintf( wbuf, sizeof( wbuf ), "%ld", min_mem );
	insert_pair( argv, &argc, ARGV_LEN, "-m", wbuf );										// MIB of memory
	insert_pair( argv, &argc, ARGV_LEN, "--file-prefix", cfg->lock_name );					// dpdk uses as a lock id

	if( !( cfg->flags & CF_HUGE_PAGES) ) {
		insert_pair( argv, &argc, ARGV_LEN, "--no-huge", NULL );
	}

	snprintf( wbuf, sizeof( wbuf ), "%d", cfg->dpdk_log_level + cfg->init_lldelta );		// set the verbosity + the initialisation delta
	insert_pair( argv, &argc, ARGV_LEN, "--log-level", wbuf );

	for( i = 0; i < cfg->nrx_devs; i++ ) {
		insert_pair( argv, &argc, ARGV_LEN, "-w", cfg->rx_devs[i] );
	}

	if( !cfg->duprx2tx ) {														// tx devices are added only if not duplicated from rx list
		for( i = 0; i < cfg->ntx_devs; i++ ) {
			insert_pair( argv, &argc, ARGV_LEN, "-w", cfg->tx_devs[i] );
		}
	}

	if( cfg->log_level + cfg->init_lldelta > 2 ) {
		print_dpdk_initstring( argc, argv );			// print out cfg, vet, etc.
	}

	if( cfg->flags & CF_FORREAL ) {
		bleat_printf( 1, "invoking dpdk initialisation with %d arguments", argc );
		i = rte_eal_init( argc, argv ); 											// http://dpdk.org/doc/api/rte__eal_8h.html
		bleat_printf( 1, "dpdk initialisation returned %d", i );
	} else {
		bleat_printf( 1, "rte initialisation skipped (no harm mode)" );
		i = 1;
	}

	free( argv );			// we can't free the pointers, but we can free the vessel

	return i;
}

// ------ daemonise support ----------------------------------------------------------------------------------

/*
	Writes the current pid into the named file as a newline terminated string.
	Returns true on success. 
	This is extern so main can stash the pid even if it's not detaching from the tty.
*/
extern int save_pid( char const* fname ) {
	int fd;
	char buf[100];
	int	len;
	int rc = 0;

	if( (fd = open( fname, O_CREAT|O_TRUNC|O_WRONLY, 0644 )) >= 0 ) {
		len = snprintf( buf, sizeof( buf ), "%d\n", getpid()  );
		if( write( fd, buf, len ) == len ) {
			rc = 1;
		}
		close( fd );
	}

	return rc;
}

/*
	Necessary housekeeping to detach from the tty.
*/
static void detach_tty( void ) {
	setsid();  					// detach from parent session; become new group leader without controlling tty

	fclose(stdin);				// don't accidently pick parent's stdin
	dup2( 1, 2 );				// dup stdout to stderr rather than closing so we get rte messages that appear on stdout

	umask(0); 					// clear any inherited file mode creation mask

	setvbuf( stderr, (char *)NULL, _IOLBF, 0);
}

/*
	Detaches the process from the tty by forking a new process.
*/
extern void daemonise(  char const* pid_fname )
{
	int childpid;

	signal( SIGCHLD, SIG_IGN );
	signal( SIGQUIT, SIG_IGN );
	signal( SIGHUP, SIG_IGN );

	if((childpid = fork()) < 0) {
		bleat_printf( 0, "err: daemonise cannot fork process (errno = %d)", errno);
	} else {
		bleat_printf( 2, "daemonise: after fork() in %s (%d)", childpid ? "parent" : "child", childpid);

		if( !childpid ) {								// block executes only in child
			bleat_printf( 1, "daemonise: child has started" );
			detach_tty( );
			if( pid_fname != NULL ) {
				save_pid( pid_fname );		// stash the child pid for service/upstart or somesuch watchdog
			}
		} else {							// executes only in parent
			bleat_printf( 1, "daemonise: parent process exiting leaving child running: child pid=%d", childpid );
			exit( EXIT_SUCCESS );
		}
	}
}

/*
	This maps ports from the dpdk perspective to the config info. Specifically 
	we build the pciid from the device info for a dpdk port n and then look 
	for that port in our config (either the rx or tx list based on search_rx).
	Returns 1 if we found the index; 0 otherwise.
*/
static int map_port( config_t* cfg, int port_id, int search_rx ) {
	char pciid[128];
	struct rte_eth_dev_info dev_info;
	int i;

	rte_eth_dev_info_get( port_id, &dev_info );		// no return value ??? wtf?

	snprintf( pciid, sizeof( pciid ), "%04x:%02x:%02x.%01x", 
		dev_info.pci_dev->addr.domain, 
		dev_info.pci_dev->addr.bus, 
		dev_info.pci_dev->addr.devid, 
		dev_info.pci_dev->addr.function
	);

	if( search_rx ) {
		for( i = 0; i < cfg->nrx_devs; i++ ) {
			if( strcmp( cfg->rx_devs[i], pciid ) == 0 ) {
				bleat_printf( 1, "rx dev %s maps to port %d",pciid, port_id );
				cfg->rx_ports[i] = port_id;
				return 1;
			}
		}
	} else {
		for( i = 0; i < cfg->ntx_devs; i++ ) {
			if( strcmp( cfg->tx_devs[i], pciid ) == 0 ) {
				bleat_printf( 1, "tx dev %s maps to port %d",pciid, port_id );
				cfg->tx_ports[i] = port_id;
				return 1;
			}
		}
	}

	return 0;
}

// ------ context initialisation -----------------------------------------------------------------

/*
	Mk_iface creates an interface for the given port id, and returns it
	with an initialised port conf structure.  If add cache, then a flow cache
	for the interface is crated.

	rxdes is the number of rx descriptors for the rx ring.

	hw_vlan_filter allows this value to be overridden (default is 0) and might
	be needed in non-VFd managed environments to remove the VLAN ID from the packet.
*/
static iface_t* mk_iface( int portid, int rxdes, int txdes, int hw_vlan_filter, int mtu, char* addr ) {
	iface_t* nif = NULL;			// new interface to return

	if( (nif = (iface_t *) malloc( sizeof( *nif ) ) ) == NULL ) {
		bleat_printf( 0, "CRI: cannot build an interface: no memory" );
		return NULL;
	}
	memset( nif, 0, sizeof( *nif ) );

	nif->portid = portid;
	nif->ntxq = 1;										// default to 1 queue in each direction
	nif->nrxq = 1;
	nif->nrxdesc = rxdes;
	nif->ntxdesc = txdes;
	nif->addr = addr;

	nif->pconf = default_port_conf;						// pick up the defaults and allow some overrides
	nif->pconf.rxmode.hw_vlan_filter = hw_vlan_filter;
	if( mtu > 1500 ) {
		nif->pconf.rxmode.jumbo_frame = 1;
		nif->pconf.rxmode.max_rx_pkt_len = mtu > 9420 ? 9420 : mtu;		// enforce sanity
		bleat_printf( 0, "jumbo frames enabled with size of %d", (int)  nif->pconf.rxmode.max_rx_pkt_len  );
	}

	// make any changes needed for a specific interface; none at the moment

	return nif;
}


/*
	Mk_context will create a running context from the configuration that is
	passed in. In addition, the peer table portion of the dht support is
	created and all of the peers in the config are added to the table.

	Returns the pointer, or a nil potiner if there was an error.
*/
extern context_t* mk_context( config_t* cfg ) {
	context_t* nc = NULL;		// new context to return
	int	i;
	int ok = 0;
	long val;
	int	mb_count;				// number of mbufs; we reduce this if we can't allocate the desired amount
	int mb_need = 0;			// minimum number of mbufs needed (1/descriptor)

	mb_count = cfg->mbufs;

	if( cfg->nrx_devs <= 0 ) {
		bleat_printf( 0, "CRI: abort: no receive devices supplid" );
		return NULL;
	}

	if( (nc = (context_t *) malloc( sizeof( *nc ) )) == NULL ) {
		bleat_printf( 0, "CRI: cannot build a context: no memory" );
		return NULL;
	}
	memset( nc, 0, sizeof( *nc ) );

	nc->nrxifs = cfg->nrx_devs;
	nc->ntxifs = cfg->ntx_devs;
	nc->dump_size = cfg->dump_size;

	nc->nwhitelist = cfg->nwhitelist;
	nc->whitelist = cfg->whitelist;

	if( (nc->xmit_type = cfg->xmit_type) == SEND_DOWNSTREAM ) {				// set based on user option but we may override later if no tx devs given
		if( cfg->ds_vlanid > 0 ) {
			nc->xmit_type = SEND_DOWNSTREAM_VLAN;
		}
	}

	ok = 0;
	for( i = 0; i < cfg->nports; i++ ) {				// try to map each rx device to a port listed by hardware
		ok += map_port( cfg, i, 1 );
	}
	if( ok != cfg->nrx_devs ) {
		bleat_printf( 0, "CRI: unable to map one or more rx names to a port" );
		free( nc );
		return NULL;
	}

	ok = 0;
	if( !cfg->duprx2tx ) {							// if not dup rx to tx, then we must actually look
		for( i = 0; i < cfg->nports; i++ ) {				// try to map each tx device to a port
			ok += map_port( cfg, i, 0 );
		}
	} else {
		for( i = 0; i < cfg->ntx_devs; i++ ) {
			if( i < cfg->nrx_devs ) {					// in dup mode, ntx must be <= nrx
				bleat_printf( 1, "dup rx to tx: tx device [%d] (%s) will be ignored and will map to rx:%s", i, cfg->tx_devs[i], cfg->rx_devs[i] );
				ok++;
			} else {
				bleat_printf( 0, "ERR: when rx dup to tx, number of tx definitions must match number of rx ports and does not" );
				ok = 0;
				break;
			}
		}
	}
	if( cfg->ntx_devs > 0  &&  ok != cfg->ntx_devs ) {
		bleat_printf( 0, "CRI: unable to map one or more tx names to a port (got %d wanted %d)", ok, cfg->ntx_devs );
		free( nc );
		return NULL;
	}

	for( i = 0; i < cfg->nrx_devs; i++ ) {
		mb_need += cfg->tx_des + cfg->rx_des;
		if( (nc->rx_ifs[i] = mk_iface( cfg->rx_ports[i], cfg->rx_des, cfg->tx_des, cfg->hw_vlan_strip, cfg->mtu, cfg->rx_devs[i]  )) == NULL ) { 					// flesh out the intefaces
			bleat_printf( 0, "CRI: unable to make rx interface %d for %s", i, cfg->rx_devs[i] );
			free( nc );
			return NULL;
		}
	}
	bleat_printf( 1, "all rx interfaces were successfully created" );

	bleat_printf( 1, "checking dup devs %d %d ", cfg->duprx2tx, cfg->ntx_devs );
	if( ! cfg->duprx2tx && cfg->ntx_devs > 0 ) {
		for( i = 0; i < cfg->ntx_devs; i++ ) {
			mb_need += cfg->tx_des + cfg->rx_des;
			if( (nc->tx_ifs[i] = mk_iface( cfg->tx_ports[i], cfg->rx_des, cfg->tx_des, cfg->hw_vlan_strip, cfg->mtu, cfg->tx_devs[i]  )) == NULL ) { 					// flesh out the intefaces
				bleat_printf( 0, "CRI: unable to make tx interface %d for %s", i, cfg->tx_devs[i] );
				free( nc );
				return NULL;
			}

			nc->tx_ifs[i]->vset = cfg->vlans[i];			// give the vlan set configured
		}
		bleat_printf( 1, "all tx interfaces successfully created" );
	} else {
		if( cfg->duprx2tx ) {
			for( i = 0; i < cfg->nrx_devs; i++ ) {
				nc->tx_ifs[i] = nc->rx_ifs[i];
				if( cfg->vlans ) {
					nc->tx_ifs[i]->vset = cfg->vlans[i];			// give the vlan set configured; we ignore the address
				} 
				if( cfg->macs ) {
					nc->tx_ifs[i]->mset = cfg->macs[i];				// give the mac set configured
				}
			}

			nc->ntxifs = cfg->nrx_devs;
			bleat_printf( 1, "tx interfaces successfully duplicated onto the rx list" );
			nc->flags |= CTF_TX_DUP;
		} else {
			if( nc->xmit_type != DROP ) {			// only warn them if they didn't specify drop
				bleat_printf( 0, "wrn: xmit type changed to drop because no tx devices were given and dup was false" );
				nc->xmit_type = DROP;			// no tx so we must drop
			} else {
				bleat_printf( 1, "xmit type drop" );
			}
		}
	}

	if( (cfg->duprx2tx || cfg->ntx_devs > 0) &&  (cfg->downstream_mac != NULL && strcmp( cfg->downstream_mac, "drop" ) != 0) ) {
		macstr2buf( (unsigned char const*) cfg->downstream_mac, &nc->downstream_mac.addr_bytes[0] );
		bleat_printf( 1, "downstream mac found: %s", cfg->downstream_mac );
	} else {
		bleat_printf( 1, "downstream missing from config or == 'drop' or ntx devices is 0; ALL packets received on external interface will be dropped!" );
		nc->flags |= CTF_DROP_ALL;		// no downstream, then drop all we get
	}

	nc->ds_vlanid = cfg->ds_vlanid;

	if( ! (cfg->flags & CF_ASYNC) ) {
		nc->flags |= CTF_INTERACTIVE;			// set interactive mode as it affects tty updates
	}

	
	if( mb_count < mb_need ) {
		bleat_printf( 0, "wrn: adjusting mbuf count to %d; value in config (%d) is too small", mb_need, mb_count );
		mb_count = mb_need;
	}

	do {
		nc->mbuf_pool = rte_pktmbuf_pool_create("mbuf_pool", mb_count, MEMPOOL_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
		mb_count -= 512;
	} while( mb_count > mb_need &&  nc->mbuf_pool == NULL );
	if( nc->mbuf_pool == NULL ) {
		bleat_printf( 0, "CRI: unable to allocate message buffer pool with at least %d buffers", mb_need );
		free( nc );
		return NULL;
	}
	bleat_printf( 1, "created buffer pool with %d entries", mb_count+512 );

	val = strtol( cfg->cpu_mask, NULL, 0 );
	nc->nthreads = count_bits( &val, sizeof( val ) );		// number of bits in the mask determines number of threads

	return nc;
}

/*
	Start_one_iface will start the indicated interface:
		- configure the port
		- setup rx queues
		- setup tx queues
		- allocate and register tx buffers
		- start the port
		- put port into promisc mode (if enabled in config)

	There are a couple of things that need to wait until after
	the port is up to finally do:
		- insert the mac into the tx mac list if there is a
			00:00...:00 entry
*/
static int start_one_iface( context_t* ctx, iface_t* iface ) {
	int i;
	int j;
	int state;

	if( iface == NULL ) {
		bleat_printf( 0, "CRI: start_one: internal mishap: iface nil" );
		return 0;
	}

	if( iface->flags & IFFL_RUNNING ) {					// assume a duplicated interface that was started
		return 1;										// just get out now.
	}

	if( (state = rte_eth_dev_configure( iface->portid, iface->nrxq, iface->ntxq, &iface->pconf )) < 0 ) {
		bleat_printf( 0, "start_one: interface configure failed: %d (%s)", state, strerror( -state ) );
		return 0;
	}

	for( i = 0; i < iface->nrxq; i++ ) {			// start the inidcated number of receive queues; nil used as conf for dpdk defaults
		if( (state = rte_eth_rx_queue_setup( iface->portid, i, iface->nrxdesc, rte_eth_dev_socket_id( iface->portid ), NULL, ctx->mbuf_pool )) < 0 ) {
			bleat_printf( 0, "start_one: interface rx queue %d start failed: rx_desc=%d state=%d (%s)", i, (int) iface->nrxdesc,  state, strerror( -state ) );
			return 0;
		}
	}
	bleat_printf( 3, "started %d receive queues for port %d", iface->nrxq, iface->portid );

	if( (iface->tx_bufs = rte_malloc_socket( "txbuf", sizeof( struct rte_eth_dev_tx_buffer * ) * iface->ntxq, 0, rte_eth_dev_socket_id( iface->portid ) )) == NULL ) {
		bleat_printf( 0, "start_one: unable t allocate tx buffer pointers for port %d", iface->portid );
		return 0;
	}

	bleat_printf( 1, "setting up tx buffers for %d desc", (int) iface->ntxdesc );
	for( i = 0; i < iface->ntxq; i++ ) {
		if( (state = rte_eth_tx_queue_setup( iface->portid, i, iface->ntxdesc, rte_eth_dev_socket_id( iface->portid ), NULL )) < 0 ) {
			bleat_printf( 0, "start_one: interface tx queue %d start failed: ntxdesc=d state=%d (%s)", i, iface->ntxdesc, state, strerror( -state ) );
			return 0;
		}

		iface->tx_bufs[i] = rte_zmalloc_socket( "tx_buffer", RTE_ETH_TX_BUFFER_SIZE(MAX_PKT_BURST * 2), 
			0, rte_eth_dev_socket_id( iface->portid ) );		// alloc buffer on same numa socket as the port

		if( iface->tx_bufs[i] == NULL ) {
			bleat_printf( 0, "start_one: unable to allocate tx buffers for port %d", iface->portid );
			return 0;
		}

		if( (state = rte_eth_tx_buffer_init( iface->tx_bufs[i], MAX_PKT_BURST * 2 )) != 0 ) {
			bleat_printf( 0, "start_one: unable to initialise tx buffers for port %d state=%d (%s)", iface->portid, state, strerror( -state ) );
			return 0;
		}

		// right now stats collect for all queues, this will DROP packets when flush is called if the packets cannot be sent rather than requeuing them
		state = rte_eth_tx_buffer_set_err_callback( iface->tx_bufs[i], rte_eth_tx_buffer_count_callback, &iface->stats.drops );
		if( state < 0 ) {
			bleat_printf( 0, "start_one: unable to initialise tx error callback for port %d state=%d (%s)", iface->portid, state, strerror( -state ) );
			return 0;
		}
	}
	bleat_printf( 3, "started %d transmit queues for port %d", iface->ntxq, iface->portid );

	if( (state = rte_eth_dev_start( iface->portid )) < 0 ) {
		bleat_printf( 0, "start_one: device start failed for port %d state=%d (%s)", iface->portid, state, strerror( -state ) );
		return 0;
	}

	if( ctx->flags & CTF_PROMISC ) {
		bleat_printf( 2, "entering promiscuous mode" );
		rte_eth_promiscuous_enable( iface->portid );		// no return ?
	} else {
		bleat_printf( 1, "disabling promiscuous mode" );
		rte_eth_promiscuous_disable( iface->portid );
	}

	rte_eth_macaddr_get( iface->portid, &iface->mac_addr );			// get such that dpdk header functions/macros have known quantity
	iface->mac = get_mac_string( iface->portid );					// and a nice human readable form for logs
	bleat_printf( 1, "port %d mac: %s", iface->portid, iface->mac );

	if( iface->mset != NULL ) {
		uint8_t* m;

		for( i = 0; i < (int) iface->mset->nmacs; i++ ) {
			m = &iface->mset->macs[i].addr_bytes[0];
			if( (unsigned char) *m == 0 ) {
				m++;
				for( j = 1; j < 6; j++ ) {
					if( (unsigned char) *m != 0 ) {
						break;
					}
					m++;
				}

				if( j == 6 ) {				// mac address was all zeros
					ether_addr_copy( &iface->mac_addr, &iface->mset->macs[i] );			// copy from->to
				}
			}
		}
	}

	if( ctx->nwhitelist > 0 ) {				// white list supplied, push it to the device
		push_whitelist_macs( iface->portid, ctx->nwhitelist, ctx->whitelist );
	}

	iface->flags |= IFFL_RUNNING;			// succesfully started; can be stopped at shutdown/signal
	return 1;
}

/*
	Start_ifaces will attempt to start each of the interfaces which are described
	in the context. Returns 1 if good and 0 on error. Once interfaces are started
	we can create a peer ipv6 header for each peer known and add that to the peer
	table.
*/
extern int start_ifaces( context_t* ctx ) {
	int i;

	if( ctx == NULL ) {
		bleat_printf( 0, "CRI: start_ifaces: nil context pointer" );
		return 0;
	}
	
	for( i = 0; i < ctx->nrxifs; i++ ) {
		if( ! start_one_iface( ctx, ctx->rx_ifs[i] ) ) {
			bleat_printf( 0, "CRI: start_ifaces: start rx interface %d failed" );
			return 0;
		}
	}
	bleat_printf( 1, "%d rx interfaces started", ctx->nrxifs );

	if( ! (ctx->flags & CTF_TX_DUP) ) {
		for( i = 0; i < ctx->ntxifs; i++ ) {
			if( ! start_one_iface( ctx, ctx->tx_ifs[i] ) ) {
				bleat_printf( 0, "CRI: start_ifaces: start tx interface %d failed" );
				return 0;
			}
		}
		bleat_printf( 1, "%d tx interfaces started", ctx->ntxifs );
	} else {
		bleat_printf( 1, "tx interfaces duped to Rx interfaces; no start needed" );
	}

	return 1;
}

/*
	Given internal and external gateway mac strings, set the usable mac buffer in each 
	interface struct.
void set_gates( context_t* ctx, char* ext_gate, char* int_gate ) {
	char const*	def_gate =	"01:01:01:02:02:02"; 			// shouldn't be called with nil strings, but just be parinoid

	if( ctx == NULL ) {
		return;
	}

	if( ext_gate != NULL ) {
		macstr2buf( (unsigned char const*) ext_gate, &ctx->ext_net->gate.addr_bytes[0] );
	} else {
		macstr2buf( (unsigned char const*) &def_gate, &ctx->ext_net->gate.addr_bytes[0] );
	}

	if( int_gate != NULL ) {
		macstr2buf( (unsigned char const*) int_gate, &ctx->int_net->gate.addr_bytes[0] );
	} else {
		macstr2buf( (unsigned char const*) &def_gate, &ctx->ext_net->gate.addr_bytes[0] );
	}
}
*/


// ------ not initialisation (i.e. termination) -----------------------------------------

/*
	Stop the interface if it was successsfully started.
*/
static void stop_one_if( iface_t* iface ) {
	if( iface == NULL || !(iface->flags & IFFL_RUNNING)) {
		return;
	}

	bleat_printf( 0, "shutting down (stop/close) interface: port %d %s", iface->portid, iface->mac );
	rte_eth_dev_stop( iface->portid );
	rte_eth_dev_close( iface->portid );

	iface->flags &= ~IFFL_RUNNING;
}

/*
	Not really initialisation, but housekeeping related.
	Stap all of the ports which were configured.
*/
extern void stop_all( context_t* ctx ) {
	int i;

	if( ctx == NULL ) {
		return;
	}

	for( i = 0; i < ctx->nrxifs; i++ ) {
		stop_one_if( ctx->rx_ifs[i] );
		bleat_printf( 0, "rx interface stopped: %i", i );
	}
	bleat_printf( 1, "all rx interfaces stopped" );

	for( i = 0; i < ctx->ntxifs; i++ ) {
		stop_one_if( ctx->tx_ifs[i] );
		bleat_printf( 0, "tx interface stopped: %i", i );
	}
	bleat_printf( 1, "all tx interfaces stopped" );
}
