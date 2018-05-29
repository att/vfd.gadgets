     
 
 
 
# Gobbler 
Gobbler is a DPDK application which provides a simple 
packet receipt counting function with optional forwarding 
or return to sender modes. Gobbler opens one or more 
devices for input (Rx) and either duplicates the Rx devices 
for Tx, or opens one or more devices for transmission (Tx). 
Packets received are counted and dropped, or forwarded onto 
one of the Tx devices. 
 
 
 
## Requirements 
The most recent commit works with DPDK 18.02 (rc1) and that 
version should be used. It has been built in the past with 
DPDK version 17.08 of DPDK, and it might still build 
against that (two changes in init.c, includes, were made to 
support 18.02). 
 
 
## Building 
Gobbler is built in the same manner as any other DPDK 
application; using gmake. Execute the following command in 
this directory which will generate ./build/app/gobbler as a 
statically linked binary. 
 
   make clean; make
 
The gobbler binary (statically linked) will be placed into 
the ./build/apps directory. 
 
 
## Execution 
Gobbler uses a configuration file rather than overloading 
the command line with both application and DPDK (EAL) 
parameters. As such, the command line is fairly straight 
forward: 
   sudo ./gobbler [-c config-file] [-i]
If the name of the configuration file is not supplied on 
the command line, then ./gobbler.cfg is assumed. The -i 
parameter (interactive) assumes that gobbler should not 
detach from the TTY and that periodic updates of counts 
should be made more frequently to standard out. 
 
 
### The Configuration File 
The gobbler configuration file is used to define 
information about which ports (PCI addresses) to use for 
reception and/or transmission, MTU size, CPU mask and other 
EAL configuration parameters. 
 
 
# Configuration 
A configuration file is necessary to define all of the 
needed information to control the behaviour of gobbler. 
Some of the bits of information which can be defined 
include: 
 
 
* White list of both Tx and Rx devices 
* VLAN IDs to insert into forwarded messages 
* MAC addresses to insert as the source in forwarded 
messages 
* Log file, directory, and verbosity settings 
* MAC address of downstream process (forward target) 
* traditional DPDK command line parameters 
 
 
The configuration file is assumed to be in ./gobbler.cfg, 
though the command line parameter -c can be used to supply 
the name of the configuration file if it exists in an 
alternate location. 
 
 
## Sample Config File 
The configuration file is a set of information provided in 
JSON format. The following illustrates the layout with an 
explanation of the various fields following. 
  
  
  
    {
       "ds_vlanid":        99,
    
       "log_level":        2,
       "dpdk_log_level":   3,
       "log_keep":         27,
       "xlog_dir":         "/tmp/gbbler/logs",
       "log_file":         "stderr",
       "init_lldelta":     2,
    
       "mbufs":            1024,
       "rx_des":           128,
       "tx_des":           128,
       "mtu":              9009,
    
       "default_macs":		[ "fa:ce:ed:09:00:05", 
                             "fa:ce:ed:09:01:01" ],
       "gen_macs":         true,
       "mac_whitelist":    [ "04:30:86:02:19:89", 
                             "04:01:60:02:01:61" ]
    
       "duprx2tx":         true,
       "rx_devs":          [ "0000:00:04.0",
                             "0000:00:05.0" ],
    
       "tx_devs":          [
                  {  "address": "dup0",
                     "vlanids": [ 101, 11, 12 ],
                     "macs": [ "76:df:b5:6a:99:0d",
                               "76:df:b5:6a:99:0e", "" ]
                  },
                  {  "address": "dup0",
                     "vlanids": [ 101, 111, 112 ],
                     "macs": [ "76:df:b5:6a:99:0d",
                               "76:df:b5:6a:99:0e" ]
                  },
       ],
    
       "xmit_type":        "forward",
       "downstream_mac":   "fa:ce:de:02:00:02",
    
       "cpu_mask":         "0x1",
       "lock_name":        "gobbler",
       "mem_chans":        4,
       "huge_pages":       true,
       "promiscuous":      false
    }
 
 
 
 
 
### Log Information 
The log_level and dpdk_log_level fields set the verbosity 
level during execution. The larger the number the more 
chatty both gobbler and DPDK will be. The **init_lldelta** 
is a value which is added to the log level values during 
initialisation. This allows for more information during 
initialisation with a quieter runtime which is often 
desired. 
 
The log directory and log file are used to set the output 
of gobbler 'bleat' messages. If _stderr_ is supplied as the 
log_file, then messages will be written to the standard 
error. DPDK messages always seem to be written to the 
standard error device. 
 
 
### Transmission Mode 
When a packet is received gobbler will take one of three 
actions on the packet depending on the mode which is 
supplied by the **xmit_type** field. Values may be _rts_ 
(return packet to sender), _forward_ (send packet to the 
MAC address listed in the downstream field), _drop._ 
 
 
### Receive Devices 
The **rx-devs** field is an array of PCI device addresses 
which gobber is expected to configure and use to receive 
packets. Gobbler will abort if it is not possible to use 
all of the named devices for reception. 
 
 
### Duplicating Tx onto Rx devices 
When _true,_ the **duprx2tx** boolean field causes the Tx 
devices to be duplicated using the PCI addresses set in the 
_rx_devs_ field. This allows the PCI addresses in the 
tx_devs information to be omitted (see below). When set to 
_false,_ the Tx devices are different than the Rx devices 
and their PCI addresses must be specified. 
 
In this mode the devices supplied in the _tx_devs_ array 
must be zero, or must exactly match the number supplied in 
the _rx_devs_ array. 
 
 
### Transmission Devices 
The **tx_devs** field supplies an array of transmission 
device information. As gobbler reads batches of packets 
from the Rx devices it selects the next Tx device in turn 
to use for transmitting the batch. The array contains 
objects which specify the PCI address, and arrays of MAC 
addresses and VLAN IDs. 
 
The PCI address will be ignored, and is usually set to 
"dup," when the _duprx2tx_ field is set to _true._ When the 
field _xmit_type_ is set to **forward,** the arrays of VLAN 
IDs and MAC addresses are used to force different values 
into packets as they are transmitted. The MAC array is also 
used when _xmit_type_ is set to **rts,** but in this mode 
the original VLAN ID is left in the packet when it is 
returned. With each packet transmitted the next address/ID 
from each list is placed into the packet which allows for 
testing with a series of addresses/IDs when needed (spoof 
checking etc.). 
 
If a MAC address is left as the empty string (e.g. ""), the 
MAC address for the device is used. 
 
 
### Downstream MAC 
The **downstream_mac** address is the destination mack 
address given to all forwarded packets. When the mode is _ 
rts,_ this field value is ignored. 
 
 
### Downstream, Default, VLAN ID 
The **ds_vlanid** field provides the VLAN ID which is 
inserted into packes which are forwarded to the downstream 
MAC address. This value is overridden by VLAN IDs supplied 
for a Tx device, and thus is used as the default when there 
is no array given. 
 
If this value is set to 0, then gobbler assumes that VLAN 
IDs are being stripped from the packets and gobbler will 
NOT attempt to insert a VLAN ID into any packet on 
transmission. 
 
 
### Default MAC Addresses 
The array of default MAC addresses is applied, in order, to 
each of the interfaces. Unlike the the white list of MAC 
addresses (below) the address pushed to a device as the 
default will persist over Gobbler restarts. If there are 
fewer MAC addresses in this array, then the interfaces 
which are initialised after all have been assigned will not 
have a default MAC assigned. MAC addresses may be 
duplicated for interfaces which reside on different PFs; 
attempting to use the same MAC address on the same PF will 
result in an error. If the array is omitted from the 
configuration file, no defaults are assigned. 
 
 
 
### Whitelist MAC Addresses 
Gobbler will genenerate a series of _set mac-vlan_ requests 
to set one or more MAC addresses in the VF's whitelist. 
There are two ways of doing this. First, the boolean field 
_gen_macs_ can be set to true which causes a small set of 
generated MAC addresses to be added. This is primarly just 
to test the ability to add MAC lists to the device. 
 
The mac_whitelist array can also be given in the config 
file. The MAC addresses supplied in this manner are applied 
to ** all ** of the interfaces defined. The intent of 
supplying a known list is to allow for testing both the 
ability to set a white list and to verify that the NIC is 
behaving properly with respect to allowing and/or denying 
traffic based on the white list. When the whitelist is 
given, the value of _gen_macs_ is ignored (treated as 
false). 
 
 
### Other Parameters 
The other parameters in the configuration file should be 
fairly obvious and are briefly described below. 
 
 
**mbufs:** The number of message buffers that gobbler 
allocates for packet processing. 
 
**rx_des:** The number of receive descriptors allocated. 
 
**tx_des:** The number of transmit descriptors allocated. 
 
**mtu:** The MTU size that gobbler will attempt to set on 
each device. 
 
**cpu_mask:** The MASK of CPUs that gobbler will attempt to 
use (must include CPUs which are NUMA aligned with the 
NICs. 
 
**lock_name:** The process duplication prevention lock name 
(DPDK). 
 
**mem_chans:** The number of memory channels supported on 
the host. 
 
**huge_pages:** If false, huge pages are used (this must 
usually be true or odd results happen). 
 
**promiscuous:** If true, gobbler will set promiscuous mode 
on the device(s). 
 
 
 
___________________________________________________________
Formatted on 29 May 2018 using tfm V2.2/0a266 
