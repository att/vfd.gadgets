
/*
	Mnemonic:	gobbler.h
	Abstract:	Main header file for anolis.
	Date:		1 February 2017
	Author:		E. Scott Daniels
*/


#ifndef _gobbler_h_
#define _gobbler_h_

#include <rte_common.h>
#include <rte_eal.h>
#include <rte_ether.h>
#include <rte_ethdev.h>

#define	IPC_VIA_V6	1				// set to true to use IPv6 for ipc traffic

#define FALSE		0
#define TRUE		1

#define ONE_MEG		1048576

#define MAX_PORTS	10				// max number of listen interfaces

#define MAX_PKT_BURST 32
#define MBUF_COUNT	8192
#define MEMPOOL_CACHE_SIZE 256

									//config flags
#define CF_FORREAL	0x01			// started without -n option
#define CF_ASYNC	0x02			// detach the process from the tty
#define CF_HUGE_PAGES	0x04		// enable huge pages
#define CF_PROMISC	0x08			// prmoisc==true in config


#define RETURN_TO_SENDER	1		// xmit types; send back to the orig addres
#define SEND_DOWNSTREAM		2		// send to downstream
#define SEND_DOWNSTREAM_VLAN 3
#define DROP				4		// send to downstream


									// context flags
#define CTF_DROP_ALL	0x01		// drop all packets (no downstream)
#define CTF_PROMISC		0x02		// enable promiscuous mode (may need to be pushed to iface level)
#define CTF_INTERACTIVE 0x04		// interactive; did not daemonise
#define CTF_TX_DUP		0x08		// tx was dup'd onto rx ports

									// interface flags
#define IFFL_RUNNING	0x01		// port was successfully started
#define IFFL_LINK_UP	0x02		// link was reported as being up

#define ETH_OFFTO_VLAN1	12			// offset to the first vlan tag
#define ETH_OFFTO_VLAN2 16			// offset to the second if QinQ

#define IP4_PROTO		9			// offset to protocol header in ipv4 packet (8bit)
#define IP6_PROTO		6			// offset to next header in ipv6 header (8bit)

#define IP_PROTO_UDP	17			// values in the next header/proto field of the ip header
#define IP_PROTO_TCP	6

#define VERSION_IPV4	0x40		// bit(s) set for various IP versions (dpdk 8bit field)
#define VERSION_IPV6	0x6000		// v6 info from dpdk is 32bits
#define V6_ADDR_LEN		16			// bytes in an address

#define ETH_PROTO_IP		0x0800		// IP proto as marked in ether frame
#define ETH_PROTO_ARP		0x0806		// arp proto as marked in ether frame
#define ETH_PROTO_VLAN		0x8100		// vlan id inserted before proto

										// inter proc comm operation codes
// -------------------------------------------------------------------------------------------
// these are dpdk structs used to manage default settings

static const struct rte_eth_conf default_port_conf = {
	.rxmode = {
		.split_hdr_size = 0,
		.header_split   = 0, 		// Header Split disabled
		.hw_ip_checksum = 0, 		// IP checksum offload disabled
		.hw_vlan_filter = 0, 		// VLAN filtering disabled
		.max_rx_pkt_len	= 1500,		// can be overridden by mk_iface()
		.jumbo_frame    = 0, 		// disabled; can be changed in mk_iface()
		.hw_strip_crc   = 0, 		// CRC stripped by hardware
	},
	.txmode = {
		.mq_mode = ETH_MQ_TX_NONE,
	},
};

//------------ references to the globals ---------------------------------------------
extern int ok2run;					// set to 0 when we need to stop

// -------------------------------------------------------------------------------------------
/*
	Hash(es) which manage the overall flow cache. Due to limits on the dpdk hash
	table (size limits based on underlying ring implementation) we create many
	of them and use the hash key to first select a table and then use the dpdk 
	functions to save/fetch the data.
*/
typedef struct flow_cache {
	int	nhashes;				// number of dpdk hahes we've created
	struct rte_hash** caches;	// pointers dpdk gives us
} flow_cache_t;

/*
	Stats collected on a particular interface
*/
typedef struct if_stats {
	int64_t	drops;				// number of packets dropped by nic on write
	int64_t rxed;
	int64_t txed;
	int64_t	nonip;				// number dropped because bad ip
	int64_t chits;
	int64_t cadds;
} if_stats_t;


/*
	Manages a set of VLAN IDs that we rotate through when we Tx on a device.
	Allows us to simulate an application that writes to multiple VLANs.
*/
typedef struct vlan_set {
	uint16_t*	vlans;			// the array of IDs we loop through
	uint32_t	idx;			// index into the array
	uint32_t	nvlans;			// number in the list
} vlan_set_t;

/*
	Set of mac addresses that we will 'loop' through as we forward/return 
	packets so that we can simulate an application that might be sending
	with multiple src addresses.
*/
typedef struct mac_set {
	struct ether_addr* macs;	// macs in the form that can be inserted into the packet
	uint32_t	idx;			// index into the array
	uint32_t	nmacs;			// number in the list
} mac_set_t;

/*
	Describes an interface (pci we assume) and maps it to a port in dpdk terms.
*/
typedef struct iface {
	char*	mac;							// human readable mac address returned from the device
	char*	addr;							// ip address needed to put into routable header
	int	bwrites;							// count of buffered writes for better flushing
	int flags;								// IFFL_ constants
	int	portid;								// the rte addresable port id number
	int	ntxq;								// number of queues to configure
	int nrxq;
	int	ntxdesc;							// number of descriptors to allocate
	int nrxdesc;
	vlan_set_t*	vset;						// a list of VLAN IDs that are rotated through when Txing to this dev
	mac_set_t*	mset;						// set of macs to rotate through if Tx-ing to this device
	uint64_t last_clock;					// clock value of last flush
	struct ether_addr gate;					// router/gateway mac address to send routable packets to on this interface
	struct ether_addr mac_addr;				// the mac address of this port in dpdk form
	if_stats_t stats;						// this interface's statistics
	struct rte_eth_conf pconf;				// port configuration with specifics for this interface
	struct rte_eth_dev_tx_buffer **tx_bufs;	// allocated transmit space (one per queue)
} iface_t;

/*
	Main set of configuration information either gleaned from the config
	file itself, or built during initialisation. Some seemingly numeric
	values, e.g. mem, are kept as strings as they are passed as strings
	to the DPDK initialisation function.
*/
typedef struct config {
	// these come from the config and/or the command line
	char**	rx_devs;				// interface 'names' (pci addrs) for listening
	char**	tx_devs;				// interface "names" (PCI addresses) for xmission
	int		nrx_devs;				// number in each array
	int		ntx_devs;
	int*	rx_ports;				// port numbers corresponding to the rx_devs names
	int*	tx_ports;				// tx port numbers
	vlan_set_t** vlans;				// vlans per tx dev (order matches tx_ports order)
	mac_set_t**	macs;				// macs per tx dev (order matches tx_ports order)

	char*	log_dir;
	char*	log_file;				// fully qualified log file name to give to bleat
	int		log_keep;				// number of days to keep log files
	int		log_level;				// our log verbosity level
	int		dpdk_log_level;			// dpdk log level
	int		init_lldelta;			// log level delta during initialisation

	int		ds_vlanid;				// downstream vlan ID
	char*	downstream_mac;			// mac which will receive packets forwarded on the ext net

	char*	pid_fname;				// we'll write our pid to this file for upstart-ish things
	char*	cpu_mask;				// mask of CPUs we are assigned to (e.g. 0x0a)
	int		flags;					// CF_ constants
	int		xmit_type;
	int		duprx2tx;				// if true, then we force all rx interfaces into the tx list

	int		hw_vlan_strip;			// hardware to strip vlan ID on Rx
	int		mtu;					// max Rx mtu size (jumbo flag set if >1500, cap is 9420)

	int		mem;					// MB of memory
	int		mbufs;					// number of mbufs to allocate in the pool (4096) per interface
	int		rx_des;					// number of rx/tx descriptors for the rings
	int		tx_des;	
	char*	lock_name;				// name used to prevent duplicate procesess (dpdk --file-prefix parm)

	// these are populated at run time or only from command line
	int		nports;					// number of ports reported by hardware/dpdk
	int*	rx_port_map;			// port maps filled in by comparing tx/rx_devs to rte info at runtime
	int*	tx_port_map;			// 1:1 correspondence to the rx/tx_devs array elements
	int		dump_size;				// number of bytes of each packet to dump
} config_t;

/*
	Thread private context is a small bit of state which is given to 
	each thread. A set of pointers is maintained in the main context
	and indexed by core ID.
*/
typedef struct thread_private {
} thread_private_t;

/*
	A runing context describing interfaces and other things
	that are needed to run the thing.
*/
typedef struct context {
	int			flags;					// CTF_* constants
	int			nthreads;				// number of threads (based on cpu mask bit count)
	uint16_t	ds_vlanid;				// vlan ID for downstream sends
	iface_t*	rx_ifs[MAX_PORTS];		// listen interfaces
	iface_t*	tx_ifs[MAX_PORTS];		// transmit interfaces
	int			ntxifs;					// number of interfaces in each array
	int			nrxifs;
	int			xmit_type;				// type of retransmssion we're doing
	int			dump_size;

	struct rte_mempool* mbuf_pool;		// buffer pool allocated from huge pages
	//thread_private_t**	thd_data;		// pointers to thread private stuff
	struct ether_addr downstream_mac;	// mac that we forward to in dpdk form
} context_t;


//------------ prototypes ------------------------------------------------------------


//---- config things ------------------------------------------------------
extern config_t* crack_args( int argc, char** argv, char const* def_fname );
extern void free_config( config_t* config );
extern config_t* read_config( char const* fname );

//--- initialisation ------------------------------------------------------
extern int initialise_dpdk( config_t* cfg );
extern void daemonise(  char const* pid_fname );
//extern int map_ports( config_t* cfg, int port_id );
extern context_t* mk_context( config_t* cfg );
extern int save_pid( char const* fname );
extern int start_ifaces( context_t* ctxt );
extern void stop_all( context_t* ctx );
extern void set_gates( context_t* ctx, char* ext_gate, char* int_gate );

//---------- tools -------------------------------------------------------
extern char* get_mac_string( int portid );
extern uint8_t* ipv6str2bytes( char* str, uint8_t* bytes );
extern int all_links_up( context_t* ctx, int timeout_sec );
extern int count_avail_cores( void );
extern int count_bits( void const* data, int len );
extern void dump_octs( unsigned const char* op, int len );
extern uint32_t ipv42int( unsigned char const* ip_str );
extern unsigned char* macstr2buf( unsigned char const* mac_str, unsigned char* target_buf );
extern char* mac_to_string( struct ether_addr const*  mac_addr );
extern int get_next_core( int start );

#endif
