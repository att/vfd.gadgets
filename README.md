     
 
 
 
# vfd.gadgets 
Gadgets is a collection of DPDK stand alone tools used to 
support VFd testing. This collection is technically a part 
of VFd (https://github.com/att/vfd), but is maintained in a 
separate repo because they can be useful on their own and 
the burden of pulling and building VFd just to have these 
tools is not needed. 
 
 
## Gobbler 
Gobbler is a DPDK application which provides a simple 
packet receipt counting funciton with optional forwarding 
or return to sender modes. Gobbler opens one or more 
devices for input (Rx) and either duplicates the Rx devices 
for Tx, or opens one or more devices for transmission (Tx). 
Pakcets received are counted and dropped, or forwarded onto 
one of the Tx devices. 
 
___________________________________________________________
Formatted on 21 July 2017 using tfm V2.2/0a266 
