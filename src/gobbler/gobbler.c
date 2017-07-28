// :vi noet tw=4 ts=4:
/*-
 *   BSD LICENSE
 *
 *   Copyright(c) 2010-2016 Intel Corporation. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
	Mnemonic:	gobbler.c
	Abstract:	This is an application that gobbles packets from the NIC. It 
				has several disposition modes:
					- echo packets back to sender
					- forward packets to a 'downstream' MAC
					- drop packets completely

				It reads a config file (given with the -c command line parm)
				which defines the PCI addresses that it listens to and writes
				to along with other things such as mtu sizes, number of descriptors
				etc. 

	Author:		E. Scott Daniels
	Date:		30 Jan 2017

	Note:		This code was derrived from anolis which was based paritally 
				on the DPDK l2forwarder source.  It is doubtful that in the 
				end much here will resemble the orignal DPDK  code, but the 
				first copyright statement above is maintained in accordance 
				with the orignial authors' wishes.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <netinet/in.h>
#include <setjmp.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdbool.h>

#include <rte_common.h>
#include <rte_log.h>
#include <rte_malloc.h>
#include <rte_memory.h>
#include <rte_memcpy.h>
#include <rte_memzone.h>
#include <rte_eal.h>
#include <rte_per_lcore.h>
#include <rte_launch.h>
#include <rte_atomic.h>
#include <rte_cycles.h>
#include <rte_prefetch.h>
#include <rte_lcore.h>
#include <rte_per_lcore.h>
#include <rte_branch_prediction.h>
#include <rte_interrupts.h>
#include <rte_pci.h>
#include <rte_random.h>
#include <rte_debug.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_ip.h>

#include <gadgetlib.h>
#include "gobbler.h"

// ------ module files which provide some in-line code ----------------------------

// --- a few globals --------------------------------------------------------------
const char *version = VERSION "    build: " __DATE__ " " __TIME__;
int ok2run = 1;

// --------------------------------------------------------------------------------


// --- these need to stay here as they are inline; don't move to tools --------------

/*
	Given an interface for Tx, find the next VLAN in the list to use, or return
	the default if there isn't a vlan list.
*/
static inline uint16_t get_vlan( vlan_set_t* vset, uint16_t def_vlan ) {

	if( vset !=  NULL ) {
		if( vset->idx >= vset->nvlans ) {
			vset->idx = 0;
		}

		return vset->vlans[vset->idx++];
	}

	return def_vlan;
}

/*
	Push both the source and dest mac addresses into the packet referenced by the 
	mbuf passed in.
*/
static inline void push_mac_addrs( struct rte_mbuf *mb, struct ether_addr const* dst_addr, struct ether_addr const* src_addr  ) {
	struct ether_hdr *eth;									// ethernet header in the mbuf

	eth = rte_pktmbuf_mtod( mb, struct ether_hdr *);		// @header (this is a bleeding macro; why is it not caps? DPDK fail)
	ether_addr_copy( dst_addr, &eth->d_addr);
	ether_addr_copy( src_addr, &eth->s_addr);
}

/*
	Swap the dest/src mac addresses to return the traffic to the sender.
	If tcif is not nil, we look to see if the dest mac is a multicast packet and if it is
	we put the mac address from the interface in rather than swapping them.
*/
static inline void swap_mac_addrs( iface_t* tcif, struct rte_mbuf *mb ) {
	struct ether_hdr *eth;									// ethernet header in the mbuf
	struct ether_addr tmp;

	eth = rte_pktmbuf_mtod( mb, struct ether_hdr *);		// @header (this is a bleeding macro; why is it not caps? DPDK fail)
	if( likely( tcif != NULL ) && eth->d_addr.addr_bytes[0] == 0x01 ) {	 //multicast address
		ether_addr_copy( &tcif->mac_addr, &tmp);
	} else {
		ether_addr_copy( &eth->d_addr, &tmp);
	}

	ether_addr_copy( &eth->s_addr, &eth->d_addr);
	ether_addr_copy( &tmp, &eth->s_addr);
}


/*
	Given an ethernet header, find the vlan tag and insert what is passed here over it.
*/
static inline void insert_vlan( struct ether_hdr* eth_hdr, uint16_t vlan  ) {
	struct vlan_hdr* vh;

	if( eth_hdr->ether_type == rte_cpu_to_be_16( ETH_PROTO_VLAN ) ) {		// must flip const as 8100 is reversed on read from buffer
		vh = (struct vlan_hdr*) &eth_hdr->ether_type;							// 'overlay' ty/proto with vlan struct
		//vh->vlan_tci =  rte_cpu_to_be_16( vlan );
		vh->eth_proto =  rte_cpu_to_be_16( vlan );
	}
}

/*
	push mac addresses and the vlanid
*/
static inline void push_mac_vlan( struct rte_mbuf *mb, struct ether_addr const* dst_addr, struct ether_addr const* src_addr, uint16_t vlan  ) {
	struct ether_hdr *eth;									// ethernet header in the mbuf

	eth = rte_pktmbuf_mtod( mb, struct ether_hdr * );		// @header (this is a bleeding macro; why is it not caps? DPDK fail)

	ether_addr_copy( dst_addr, &eth->d_addr);
	ether_addr_copy( src_addr, &eth->s_addr);
	insert_vlan( eth, vlan );
}

/*
	Returns a1 if a1 is not the source address in the packet header, else returns a2
	(e.g. return src == a1 ? a1 : a2)
*/
static inline struct ether_addr* pick_dest_mac( struct rte_mbuf *mb, struct ether_addr* a1, struct ether_addr* a2 ) {
	struct ether_hdr *eth;									// ethernet header in the mbuf

	eth = rte_pktmbuf_mtod( mb, struct ether_hdr *);		// @header (this is a bleeding macro; why is it not caps? DPDK fail)
	if( memcmp( &eth->s_addr, a1, ETHER_ADDR_LEN ) == 0 ) {	// TODO -- is there a dpdk fast mcmp?
		return a2;
	}

	return a1;
}

/*
	Read the hardware tick counter
*/
static __inline__ uint64_t read_clock( void ) {
	uint64_t x;
	__asm__ volatile (".byte 0x0f, 0x31" : "=A" (x));
	return x;
}


// ----------------------------------------------------------------------------------
/*
	Called for a set of mbufs which could not be flushed
static void flush_tx_error_cb( struct rte_mbuf **unsent, uint16_t count, void *userdata ) {
        int i;
        uint8_t* counter = (uintptr_t)userdata;

		if( counter ) {
			*counter += count;
}
*/

// ----------------------------------------------------------------------------------
/*
	Set global terminate flag for int/term signals. Others (not registered)
	shouldn't make it here but will be ignored.
*/
static void signal_handler(int signum)
{
	if( signum == SIGINT || signum == SIGTERM) {
		bleat_printf( 0, "signal received=%d: shutdown begins", signum );
		ok2run = 0;
	} else {
		bleat_printf( 2, "signal received=%d: ignored", signum );
	}
}


/*
	Given an ethernet header, determine it's actual length as vlan tags add to it
	when present. We also fill in the protocol of the next header.
*/
static inline int suss_ehdr_len( struct ether_hdr const* eth_hdr, uint16_t* proto  ) {
	int elen = 0;
	struct vlan_hdr const* vh;

	elen = sizeof( struct ether_hdr );

																						// get the next protocol, skipping vlan tags if there
	if( (*proto = eth_hdr->ether_type) == rte_cpu_to_be_16( ETH_PROTO_VLAN ) ) {		// must flip const as 8100 is reversed on read from buffer
		vh = (struct vlan_hdr const *) &eth_hdr->ether_type;							// 'overlay' ty/proto with vlan struct
		vh++;																			// move it past; overlay next type or inner qtag
		elen += 4;

		if( (*proto = vh->vlan_tci) == rte_cpu_to_be_16( ETH_PROTO_VLAN ) ) {			// another .1q tag? if not proto has the real proto
			vh++;																		// position past; tci will now rest over the real proto
			*proto = vh->vlan_tci;														// finally pick up the real proto
			elen += 4;																	// and another 4 to skip
		}
	}

	return elen;
}


/*
	Flush one interface.  The number of packets written and dropped are added to the 
	tx/dropped counters whose addresses were passsed in. The interface counters
	are also updated.
*/
static inline void flush_if( iface_t* iface, uint64_t* txcounter, uint64_t* dcounter ) {
	int	flushed;

	iface->stats.drops = 0;														// reset just to get the number dropped this call
	flushed = rte_eth_tx_buffer_flush( iface->portid, 0, iface->tx_bufs[0] );
	iface->stats.txed  += flushed;												// actually sent on this interface 
	*txcounter += flushed;														// add to caller's tx counter
	iface->bwrites = 0;															// no writes buffered for this interface
	*dcounter += iface->stats.drops;												// update caller's drop counter
}

/*
	Flushes the interface if it is full enough that receiving another burst of writes
	would cause it to overflow.
*/
static inline void flush_full_if( iface_t* iface, uint64_t* txcounter, uint64_t* dcounter ) {
	if( iface->bwrites > 32 ) {
		flush_if( iface, txcounter, dcounter );
	}
}


// -------------- thread processing functions ------------------------------------------
/*
	Gobble up packets from the rx intefaces.  If one or more Tx interface(s) are in the
	config file the packets will be written using either the downstream MAC (xmit type
	== forward in config), or using the sender's MAC (rts in the config). If no Tx 
	interface is given, or xmit type == drop, then packets are dropped.  The VLAN supplied
	in the config is used when forwarding unless it is supplied as -1 in which case the
	VLAN id is left unchanged.
*/
static int gobble( void* vctx ) {
	int64_t			stats_delay;		// number of clock cycles between stats updates
	int				i;
	int				j;
	int				tx_idx = 0;			// tx round robin index
	//int				flushes = 0;
	struct rte_mbuf* pkts[128];			// mbuf pointers for received pkts
	context_t*		ctx;
	int64_t			npkts = 0;			// packets in the burst
	iface_t*		rcif = NULL;				// direct pointers to current interface being worked with
	iface_t*		tcif = NULL;				// direct pointers to current interface being worked with

	uint64_t		rcount = 0;		// number of packets total received
	uint64_t		tcount = 0;		// number of packets total xmitted

	uint64_t		drops = 0;			// total drops across all interfaces
	int64_t			drain_delay = 0;
	int64_t			last_clock = 0;
	int64_t			stats_clock = 0;			// last time we spit stats
	int64_t			this_clock = 0;
	int				doodle_count = 0;
	char const*		doodle = NULL;

	if( vctx == NULL ) {
		bleat_printf( 0, "thread on core %d received nil context; terminating", rte_lcore_id() );
		return -1;
	}
	ctx = (context_t *) vctx;

	bleat_printf( 1, "whispering gobbler running on core %d", rte_lcore_id() );

	if( !(ctx->flags & CTF_INTERACTIVE) ){		// when not in interactive mode these go to stderr less frequently
		doodle_count = 10;						// force 'static' doodle
 		stats_delay = rte_get_tsc_hz() * 60;	// keep updates into log at much less of a rate
	}  else {
 		stats_delay = rte_get_tsc_hz() * 3;		// update about every 2 seconds
	}

 	//drain_delay = (rte_get_tsc_hz() + US_PER_S - 1) / (US_PER_S * 100);		// drain every 100 u-sec
 	drain_delay = (rte_get_tsc_hz() + US_PER_S - 1) / (US_PER_S * 50);		// drain every 50 u-sec

	//bleat_printf( 1, "stats delay: %lld  %lld", (long long) stats_loops / 10, (long long) drain_delay );

	//last_clock = read_clock();
	last_clock = rte_rdtsc();

	bleat_printf( 1, "xmit type: %d", ctx->xmit_type );

	while( ok2run ) {
		int64_t state;

		this_clock = rte_rdtsc();

		for( i = 0; i < ctx->ntxifs; i++ ) {					// flush all tx interfaces
			tcif = ctx->tx_ifs[i];

			// assuming 64 buffers ensure that we don't overwrite with a full read, or ensure periodic flush when slow
			if( tcif->bwrites > 32 || (tcif->bwrites && ((this_clock - last_clock) > drain_delay)) ) {	
				flush_if( tcif, &tcount, &drops );
				last_clock = this_clock;
			}
		}

		for( j = 0; j < ctx->nrxifs; j++ ) {			// pull from each receive interface and do something 
			rcif = ctx->rx_ifs[j];

			if( ctx->ntxifs > 0 ) {						// pick an output destination
				tcif = ctx->tx_ifs[tx_idx];
				if( ++tx_idx >= ctx->ntxifs ) {
					tx_idx = 0;
				}
			}

			if( (npkts = rte_eth_rx_burst( rcif->portid, 0, pkts, 32 )) > 0 ) {		// process a burst from this interface
				rcif->stats.rxed += npkts;	
				rcount += npkts;

				if( unlikely( ctx->dump_size ) ) {
					for( i = 0; i < npkts; i++ ) {
						bleat_printf( 1, "if=%d xmit=%d pkt %d of %d len=%d first %d bytes", j, ctx->xmit_type,  i, npkts, rte_pktmbuf_pkt_len( pkts[i] ), ctx->dump_size );
						dump_octs( rte_pktmbuf_mtod( pkts[i], unsigned const char*), ctx->dump_size > 1 ? (int) ctx->dump_size : (int)  rte_pktmbuf_pkt_len( pkts[i] ) );
					}
				}

				switch( ctx->xmit_type ) {
					case RETURN_TO_SENDER:							// just push the packets back out with the addresses reversed
						for( i = 0; i < npkts; i++ ) {
							swap_mac_addrs( tcif, pkts[i] );
							if( (state = rte_eth_tx_buffer( tcif->portid, 0, tcif->tx_bufs[0], pkts[i] )) >= 0 ) {
								tcif->stats.txed += state;	
								tcount += state;
								tcif->bwrites++;
								if( unlikely( ctx->dump_size ) ) {
									bleat_printf( 1, "RTS: if=%d pkt %d of %d len=%d first %d bytes", j, i, npkts, rte_pktmbuf_pkt_len( pkts[i] ), ctx->dump_size );
									dump_octs( rte_pktmbuf_mtod( pkts[i], unsigned const char*), ctx->dump_size > 1 ? (int) ctx->dump_size : (int)  rte_pktmbuf_pkt_len( pkts[i] ) );
								}
							} else {
								rte_pktmbuf_free( pkts[i] );
							}
						}	
						break;

					case SEND_DOWNSTREAM:					// set if ds_vlan is <= 0 in config
						for( i = 0; i < npkts; i++ ) {
							push_mac_addrs( pkts[i], &ctx->downstream_mac, &tcif->mac_addr );		// set just the mac address

							if( (state = rte_eth_tx_buffer( tcif->portid, 0, tcif->tx_bufs[0], pkts[i] )) >= 0 ) {
								tcif->stats.txed += state;	
								tcount += state;
								tcif->bwrites++;
							} else {
								rte_pktmbuf_free( pkts[i] );
							}
						}
						break;

					case SEND_DOWNSTREAM_VLAN:				// set if ds_vlan is > 0 in config
						for( i = 0; i < npkts; i++ ) {
							//push_mac_vlan( pkts[i], &ctx->downstream_mac, &tcif->mac_addr, ctx->ds_vlanid  );	// set addresses and vlan
							// todo - rotate through source mac addresses too
							push_mac_vlan( pkts[i], &ctx->downstream_mac, &tcif->mac_addr, get_vlan( tcif->vset, ctx->ds_vlanid )  );	// set addresses and vlan

							if( (state = rte_eth_tx_buffer( tcif->portid, 0, tcif->tx_bufs[0], pkts[i] )) >= 0 ) {
								tcif->stats.txed += state;				// unlikely, but it could have forced a flush and sent more than 1
								tcount += state;
								tcif->bwrites++;						// bwrites isn't accurate if tx_buffer forced a flush as we never know drops

								if( unlikely( ctx->dump_size ) ) {
									bleat_printf( 1, "FWDv: if=%d pkt %d of %d len=%d first %d bytes", j, i, npkts, rte_pktmbuf_pkt_len( pkts[i] ), ctx->dump_size );
									dump_octs( rte_pktmbuf_mtod( pkts[i], unsigned const char*), ctx->dump_size > 1 ? (int) ctx->dump_size : (int)  rte_pktmbuf_pkt_len( pkts[i] ) );
								}
							} else {
								rte_pktmbuf_free( pkts[i] );
							}
						}
						break;

					default:										// unknown -- just drop
						for( i = 0; i < npkts; i++ ) {
							rte_pktmbuf_free( pkts[i] );
						}
						break;
				}
			}

			//flush_full_if( tcif, &tcount, &drops );			// must flush when full to prevent overrun if ntx < nrx interfaces
		}

		if( (ctx->flags & CTF_TX_DUP) == 0 ) {				// if tx interfaces not dup'd on rx, then we trash anything that comes in
			for( j = 0; j < ctx->ntxifs; j++ ) {
				tcif = ctx->tx_ifs[j];
				if( (npkts = rte_eth_rx_burst( tcif->portid, 0, pkts, 32 )) > 0 ) {				// drop any packets received on the tx interfaces
					for( i = 0; i < npkts; i++ ) {
						rte_pktmbuf_free( pkts[i] );
					}
				}
			}
		}

		if( unlikely( npkts == 0 && stats_clock < this_clock ) ) {
			stats_clock = this_clock + stats_delay;

			switch( doodle_count ) {
				case 0: doodle = "^ . . .\r"; doodle_count++; break;
				case 1:	doodle = ". ^ . .\r"; doodle_count++; break;
				case 2:	doodle = ". . ^ .\r"; doodle_count++; break;
				case 3:	doodle = ". . . ^\r"; doodle_count = 0; break;
				default: doodle = "\n"; break;   					// non-interactive
			}

			fprintf( stderr,  "Rx: %-10lld  Tx: %-10lld  Drops: %-10llu  %s", (long long) rcount, (long long) tcount, (unsigned long long) drops, doodle );
			fflush( stderr );
		}
	}

	bleat_printf( 1, "whispering gobbler on core %d is terminating", rte_lcore_id() );

	return 0;
}

//---------------------------------------------------------------------------------------------------------

/*
	We are not the typical dpdk application inasmuch as we use a config file to configure
	settings and thus accept only a minimal number of command line options. We dummy up
	a command line (argc/v) to pass to the eal initialisation function with information
	derrived from the configuration. 
*/
int main( int argc, char** argv ) {
	context_t*	ctx;					// the context that we will run with 
	config_t*	cfg;					// configuration generated from parm file and cmd line parms

	int state;
	unsigned lcore_id;

	cfg = crack_args( argc, argv, "./gobbler.cfg" );			// crack the command line args, parse the config to build a config struct
	if( cfg == NULL ) {
		fprintf( stderr, "abort: internal mishap: unable to build a config struct\n" );
		exit( 1 );
	}

	bleat_set_lvl( cfg->log_level + cfg->init_lldelta );
	if( cfg->flags & CF_ASYNC  ) {
		bleat_printf( 3, "detaching from tty (daemonise)" );
		daemonise( cfg->pid_fname );
	}


	if( strcmp( cfg->log_file, "stderr" ) ) {						// switch if not stderr
		bleat_printf( 1, "setting log to: %s", cfg->log_file );
		bleat_set_log( cfg->log_file, 86400 );						// open bleat log with a date suffix after we daemonise so it doesn't close the fd
		if( cfg->log_keep > 0 ) {									// set days to keep log files
			bleat_printf( 2, "log files will be purged after they are %d days old", cfg->log_keep );
			bleat_set_purge( cfg->log_dir, "gobbler.log.", cfg->log_keep * 86400 );
		}
	}

	bleat_printf( 1, "gobbler started: v3.0/17723" );
	bleat_printf( 1, version );	

	if( getuid() != 0 || geteuid() != 0 ) {
		bleat_printf( 0, "CRI: process must be run as root (0) or suid root and is not" );
		exit( 1 );		
	}

	if( initialise_dpdk( cfg ) < 0 ) {					// build a dummy argv/c and give to eal_init
		fprintf( stderr, "abort: unable to initialise the dpdk environment: %s\n", strerror( errno ) );
		exit( 1 );
	} else {
		bleat_printf( 1, "dpdk successfully initialised" );
	}

	// ----- from here on rte_exit() must be used to ensure graceful shutdown --------


	signal( SIGINT, signal_handler );
	signal( SIGTERM, signal_handler );


	cfg->nports = rte_eth_dev_count();								// total number of ports visible to us; could be more than we need
	bleat_printf( 1, "%d ports reported by the system", cfg->nports );

	if( (ctx = mk_context( cfg )) == NULL ) {					// build a context for us to run with, map ports
		rte_exit( EXIT_FAILURE, "CRI: general gobbler initialisation errors\n" );
	}

	if( (state = count_avail_cores()) < ctx->nthreads ) {
		bleat_printf( 0, "CRI: unable to find enough enabled core; tried: wanted %d found %d", ctx->nthreads, state );
		rte_exit( EXIT_FAILURE, "no enabled cores available\n" );
	}
	bleat_printf( 1, "%d logical cores reported available", state );
		
	bleat_printf( 1, "starting interfaces" );
	if( start_ifaces( ctx ) == 0 ) {							// fire them puppies up
		bleat_printf( 1, "start ports failed" );
		rte_exit( EXIT_FAILURE, "CRI: interface start malfunction\n" );
	}

	bleat_printf( 1, "letting the jelly stop wiggling..." );
	if( ! all_links_up( ctx, 20 ) ) {							// wait up to 20 seconds for all the links to show in up state
		bleat_printf( 0, "CRI: all links did not come up in 20s so we're bailing out" );
		rte_exit( EXIT_FAILURE, "not all links are up\n" );
	}

	rte_eal_mp_remote_launch( gobble, (void *) ctx, CALL_MASTER );			// start our packet turkeys to gobble up messages
	state = 0;

	RTE_LCORE_FOREACH_SLAVE( lcore_id ) {									// wait for gobblers to finish; lcore_id gets processor id each iter
		if( (state = rte_eal_wait_lcore( lcore_id )) < 0) {
			bleat_printf( 0, "gobbler: gobbler has finished on slave core %d but wait returns error: %d\n", lcore_id, state );
			state = 1;
			break;
		} else {
			bleat_printf( 0, "gobbler: gobbler has finished on slave core %d\n", lcore_id );
		}
	}

	stop_all( ctx );			// close all of the ports and other shutdown

	return state;
}
