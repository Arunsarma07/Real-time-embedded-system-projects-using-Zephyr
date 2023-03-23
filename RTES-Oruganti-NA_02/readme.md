The goal of the project is to implement an application program that adds 3 shell commands in Zephyr RTOS (version 2.6.0) running on the MIMXRT1050-EVKB board. Additionally, a display device driver for the MAX7219-controlled LED matrix must be developed and added to the Zephyr source tree.

The project includes three shell commands:

p2 rgb x y z: sets the duty cycles of three PWM signals to x, y, and z, and applies the PWM signals to the RGB LED.
p2 ledm r x0 x1 x2 x3 …: reads in a list of row patterns and displays the row patters starting from r-th row in a MAX7219 controlled 8x8 LED matrix.
p2 ledb n: starts or stops the blinking mode on the 8x8 LED matrix.
The application files should reside in a directory named "project_2" which contains CMakeLists.txt, prj.conf, readme.txt, a source file directory "src", and a board device tree overlay directory "boards".

For the display driver, it should at least implement three display API functions: display_blanking_on, display_blanking_off, and display_write in the source file "zephyr/drivers/display/display_max7219.c". Additional modifications are needed in:

zephyr/drivers/display/CMakeLists.txt
zephyr/drivers/display/Kconfig
zephyr/drivers/display/Kconfig.max7219
zephyr/dts/bindings/display/maxim,max7219.yaml
The maxim,max7219.yaml file will be provided and should be added to the directory /(path to zephyr tree)/dts/bindings/display.
