Name: Narasimha Arun Oruganti.
ASU ID: 1223956669
ASU Mail ID: norugant@asu.edu
==========================================

Commands used to run the program.

west init		//in case west is not present

west update		//west update

cd RTES-Oruganti-NA_02		//assignment is folder name


export ZEPHYR_TOOLCHAIN_VARIANT=gnuarmemb		//commands for setting toolchain variables on my macOS for the first time

export GNUARMEMB_TOOLCHAIN_PATH=/Applications/ARM	//commands for setting toolchain variables on my macOS for the first time

west build -b mimxrt1050_evk	//command for building the application

west flash		//command for flashing the code onto board.

sudo minicom -s		//minicom is the terminal which I have used to view logs

(choose the corresponding serial port using "ls /dev/tty*" and set the baud rate to 115200) //This will even work the same for PuTTY as well 

p2 rgb 10 40 50 -- This command tries to set the Red, Green and Blue leds with the corresponding duty cycles

p2 ledm 4 FF 00 22 11 -- This command sets the Led matrix rows starting from "4" as per the data passed as parameters. 

p2 ledb 1 -- This command tries to blink the led matrix continuously.

p2 ledb 0 -- This command tries to stop the blinking of the matrix. 


