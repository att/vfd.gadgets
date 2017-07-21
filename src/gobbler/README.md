     
 
 
 
# Gobbler 
Gobbler is a DPDK application which provides a simple 
packet receipt counting funciton with optional forwarding 
or return to sender modes. Gobbler opens one or more 
devices for input (Rx) and either duplicates the Rx devices 
for Tx, or opens one or more devices for transmission (Tx). 
Pakcets received are counted and dropped, or forwarded onto 
one of the Tx devices. 
 
 
 
## Requirements 
Version 17.08 of DPDK should be used to build gobbler. 
 
 
## Building 
Gobbler is built in the same manner as any other DPDK 
appliction; using gmake. Execute the following command in 
this directory which will generate ./build/app/gobbler as a 
statically linked binary. 
 
   make clean; make
___________________________________________________________
Formatted on 21 July 2017 using tfm V2.2/0a266 
