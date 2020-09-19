1. The applications for the publisher and the subscriber can be easily started by 
changing the constants for the publisher and subscriber board (development boards - both must run the Linux 
kernel with an RT patch - both have armv7 support - both must run an SSH server) in the script 
**evaluate_on_board.py** and then starting the script itself. This script will also produce csv output 
as a file and it will also print the results of time measurements on the command line. Both boards must have a synchronized real-time clock. Therefore, you need to setup a Linux implementation of the PTP protocol of your choice, assure that its running and then you need to synchronize the PTP clock to **CLOCK_REALTIME** using the **phc2sys** command. 
2. The application can be improved by adding a real data source instead of a stupid increasing counter. 
A new **uint64** source can easily be added by replacing "*publishValue = *publishValue + 1;" in the opcua_publisher.c file 
with a suitable assignment from a sensor source.
3. Things left to do is the VLAN configuration of the dedicated TSN switch (UDP multicast and VLAN support needed) in the network 
topology that connects both boards and to adapt the VLAN configuration of both boards too accordingly. The QBV config, which configures 
the opening and closing of gates must be adapted too, as your QBV config tool may have a different syntax. The dynamically created QBV config file **qbv.cfg** closes all gates for 
**MAX_PUBLISH_DELAY_NS** and then opens them again for the rest of the cycle time. This is done such that the publisher packet can be 
prepared before the gates open again. This leads to little jitter during publishing. The subscriber board can leave all its gates open as 
it constantly listens for incoming packets. If the switch is configured properly, overall measured jitter should be pretty low. 
The period of operation is secured with a real-time operating system on publisher and subscriber side and the **clock_nanosleep** 
function assures that the publisher is waked to prepare the message while the gates are closed.
4. Building in the project is done via **docker**, the application start, configuring of 
the nodes and evaluation of logs is done in **Python** and the applications for the publisher and subscriber 
themselves are written in **C** using the open62541 stack. All measures were taken to minimize latency at the 
device level. All memory allocated should be freed during a normal execution of the program, however when some component fails 
unexpectedly, memory is not freed by the program, but from the OS afterwards. This was done in order to not introduce too many 
global variables that would be needed to do this. But global variables make following the information flow harder, 
since data cannot be only propagated through parameters, but also through global variables. I also used the amalgamated c file of 
the open62541 stack, as all available header files are too high level for this application. Therefore, I introduced 
the "-z muldefs" option to the linker, as the c file has no include guard and functions appear twice to the linker. 
Maybe performance can be improved by changing the standard queuing discipline of linux, which is pfifo_fast, to anything else.
I assured with the right ToS flags that the pubsub traffic is always on the fastest band. Each application starts three threads. 
One thread that does the 
setup and starts the other two threads: one thread for the OPC UA server (low priority) and one thread for publishing and 
subscribing (high priority). The latter two threads are configured to run on different cores. The **evaluate_on_board.py** 
script is written in a way that one board is enough to test the setup (simply set all connection constants of both boards to the 
same value). I had to a add a "git reset" in the **Dockerfile** as the current master (3e11f6ddba6167ce5cc179b9da613ac5d0e065f4) of open62541 
is not building.
