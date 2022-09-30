Name: Narasimha Arun Oruganti.
ASU ID: 1223956669
ASU Mail ID: norugant@asu.edu
==========================================

Commands used to run the program.

cd (project path)		//Project path


export ZEPHYR_TOOLCHAIN_VARIANT=gnuarmemb		//commands for setting toolchain variables on my macOS for the first time

export GNUARMEMB_TOOLCHAIN_PATH=/Applications/ARM	//commands for setting toolchain variables on my macOS for the first time

west build -b mimxrt1050_evk	//command for building the application

west flash		//command for flashing the code onto board.

sudo minicom -s		//minicom is the terminal which I have used to view logs

(choose the corresponding serial port using "ls /dev/tty*" and set the baud rate to 115200) //This will even work the same for PuTTY as well 


============================================

In the code implementation, all the printk statements are removed from the code in order to obtain the optimal performance.

Also, the attached screenshots have been taken by initially by sleeping the main for 10secs and then continue the rest of the executions. 

Polling Server:

case 1: 

#define POLL_PRIO   6   
#define BUDGET 40

Average response time:       15ms 
Number of Requests served:   183

case 2: 

#define POLL_PRIO   6  
#define BUDGET 25

Average response time: 	   15ms
Number of Requests served:   183

Background server:

case 3: 

#define POLL_PRIO   12     // to test background server.
#define BUDGET 40

Average response time:       148ms 
Number of Requests served:   177