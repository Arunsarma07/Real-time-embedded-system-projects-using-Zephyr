Name: Narasimha Arun Oruganti.
ASU ID: 1223956669
ASU Mail ID: norugant@asu.edu
==========================================

Commands used to run the program.

cd trace_app		//assignment is folder name

west init		//in case west is not present

west update		//west update

export ZEPHYR_TOOLCHAIN_VARIANT=gnuarmemb		//commands for setting toolchain variables on my macOS

export GNUARMEMB_TOOLCHAIN_PATH=/Applications/ARM	//commands for setting toolchain variables on my macOS

west build -b mimxrt1050_evk	//command for building the application

west flash		//command for flashing the code onto board.

sudo minicom -s		//minicom is the terminal which I have used to view logs

(choose the corresponding serial port using "ls /dev/tty*" and set the baud rate to 115200) //This will even work the same for PuTTY as well 

activate			//shell command to start the threads
