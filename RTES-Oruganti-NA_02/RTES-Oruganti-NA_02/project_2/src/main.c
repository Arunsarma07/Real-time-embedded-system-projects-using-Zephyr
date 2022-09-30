#include <zephyr.h>
#include <device.h>
#include <devicetree.h>
#include <drivers/gpio.h>
#include <sys/util.h>
#include <sys/printk.h>
#include <shell/shell.h>
#include <shell/shell_uart.h>
#include <version.h>
#include <stdlib.h>
#include <fsl_iomuxc.h>
#include <drivers/pwm.h>
#include <devicetree/pwms.h>
#include <dt-bindings/spi/spi.h>
#include <drivers/spi.h>
#include <drivers/display.h>
#include <string.h>

#define DEBUG 

#if defined(DEBUG) 
	#define DPRINTK(fmt, args...) printk("DEBUG: %s():%d: " fmt, \
   		 __func__, __LINE__, ##args)
#else
 	#define DPRINTK(fmt, args...) /* do nothing if not defined*/
#endif

// using DT marcos to get pin information for leds

#define PWMLED_RED DT_NODELABEL(pwm_r_led)

#define PWMLED0_LABEL 	DT_PROP (PWMLED_RED, label)
#define LED0_CHANNEL	DT_PWMS_CHANNEL (PWMLED_RED)

#define PWMLED_GREEN DT_NODELABEL(pwm_g_led)

#define PWMLED1_LABEL 	DT_PROP (PWMLED_GREEN, label)
#define LED1_CHANNEL	DT_PWMS_CHANNEL (PWMLED_GREEN)

#define PWMLED_BLUE DT_NODELABEL(pwm_b_led)

#define PWMLED2_LABEL 	DT_PROP (PWMLED_BLUE, label)
#define LED2_CHANNEL	DT_PWMS_CHANNEL (PWMLED_BLUE)

#define MAX7219_NODE DT_NODELABEL(max7219)
#define MAX7219_LABEL DT_PROP(MAX7219_NODE, label)

/* Sleep time */
#define SLEEP_TIME	1000

bool blink_mode=false;

const struct device *pwmb1, *pwmb2, *spi2; //Pointers for device

uint16_t spi_config_data[5] = {0x0F00, 0x0900, 0x0A0F, 0x0B07, 0x0C01}; // Default data to set the SPI in normal operation mode
uint16_t clear_data[8] = {0x0100, 0x0200, 0x0300, 0x0400, 0x0500, 0x0600, 0x0700, 0x0800}; //Default data to clear the matrix

struct display_driver_api *apifunc; //api functions

//Hard coded values for the SPI pins Multiplexing

void spi_pinmux_config()
{
	IOMUXC_SetPinMux(IOMUXC_GPIO_SD_B0_01_LPSPI1_PCS0, 0);

	IOMUXC_SetPinConfig(IOMUXC_GPIO_SD_B0_01_LPSPI1_PCS0,
			    IOMUXC_SW_PAD_CTL_PAD_PUE(1) |
			    IOMUXC_SW_PAD_CTL_PAD_PKE_MASK |
			    IOMUXC_SW_PAD_CTL_PAD_SPEED(2) |
			    IOMUXC_SW_PAD_CTL_PAD_DSE(6));

	IOMUXC_SetPinMux(IOMUXC_GPIO_SD_B0_02_LPSPI1_SDO, 0);

	IOMUXC_SetPinConfig(IOMUXC_GPIO_SD_B0_02_LPSPI1_SDO,
			    IOMUXC_SW_PAD_CTL_PAD_PUE(1) |
			    IOMUXC_SW_PAD_CTL_PAD_PKE_MASK |
			    IOMUXC_SW_PAD_CTL_PAD_SPEED(2) |
			    IOMUXC_SW_PAD_CTL_PAD_DSE(6));

	IOMUXC_SetPinMux(IOMUXC_GPIO_SD_B0_00_LPSPI1_SCK, 0);

	IOMUXC_SetPinConfig(IOMUXC_GPIO_SD_B0_00_LPSPI1_SCK,
			    IOMUXC_SW_PAD_CTL_PAD_PUE(1) |
			    IOMUXC_SW_PAD_CTL_PAD_PKE_MASK |
			    IOMUXC_SW_PAD_CTL_PAD_SPEED(2) |
			    IOMUXC_SW_PAD_CTL_PAD_DSE(6));
}

//Hard coded values for the PWM Multiplexing

void pwm_pinmux_config()
{
	IOMUXC_SetPinMux(IOMUXC_GPIO_AD_B0_10_FLEXPWM1_PWMA03, 0);

	IOMUXC_SetPinConfig(IOMUXC_GPIO_AD_B0_10_FLEXPWM1_PWMA03,
			    IOMUXC_SW_PAD_CTL_PAD_PUE(1) |
			    IOMUXC_SW_PAD_CTL_PAD_PKE_MASK |
			    IOMUXC_SW_PAD_CTL_PAD_SPEED(2) |
			    IOMUXC_SW_PAD_CTL_PAD_DSE(6));

	IOMUXC_SetPinMux(IOMUXC_GPIO_AD_B0_11_FLEXPWM1_PWMB03, 0);

	IOMUXC_SetPinConfig(IOMUXC_GPIO_AD_B0_11_FLEXPWM1_PWMB03,
			    IOMUXC_SW_PAD_CTL_PAD_PUE(1) |
			    IOMUXC_SW_PAD_CTL_PAD_PKE_MASK |
			    IOMUXC_SW_PAD_CTL_PAD_SPEED(2) |
			    IOMUXC_SW_PAD_CTL_PAD_DSE(6));

	IOMUXC_SetPinMux(IOMUXC_GPIO_SD_B0_03_FLEXPWM1_PWMB01, 0);

	IOMUXC_SetPinConfig(IOMUXC_GPIO_SD_B0_03_FLEXPWM1_PWMB01,
			    IOMUXC_SW_PAD_CTL_PAD_PUE(1) |
			    IOMUXC_SW_PAD_CTL_PAD_PKE_MASK |
			    IOMUXC_SW_PAD_CTL_PAD_SPEED(2) |
			    IOMUXC_SW_PAD_CTL_PAD_DSE(6));
}


//Function for device bindings

void all_device_bindings()
{
	pwmb1 = device_get_binding(PWMLED0_LABEL);
	if (!pwmb1) {
		DPRINTK("error\n");
		return;
	}

	pwmb2 = device_get_binding(PWMLED2_LABEL);
	if (!pwmb2) {
		DPRINTK("error\n");
		return;
	}

	spi2 = device_get_binding(MAX7219_LABEL);
	if(!spi2) {
		DPRINTK("error SPI2\n");
		return;
	}
	
}

void configure_spi()
{
	const struct display_driver_api *api = (struct display_driver_api*)spi2->api;
	api->write(spi2, 0, 5, NULL, (void *)spi_config_data);
}

void clear_matrix()
{
	const struct display_driver_api *api = (struct display_driver_api*)spi2->api;
	api->write(spi2, 0, 5, NULL, (void *)clear_data);
}

// Defining all the shell root and sub commands

static int cmd_rgb(const struct shell *shell, size_t argc, char **argv)  // PWM sub command implementation
{
	int pwm_duty_cycle[3]; //Array for storing the duty cycles
	int tmp = 1; 

	//Converting the string format of duty cycle to integer format
	for (int i = 0; i < argc-1; i++)
	{
		pwm_duty_cycle[i] = atoi(argv[tmp]); 
		tmp++; 
	}

	k_msleep(SLEEP_TIME);
	pwm_pin_set_cycles(pwmb1, LED0_CHANNEL, 100, pwm_duty_cycle[0], PWM_POLARITY_NORMAL); //Turning on red led
	k_msleep(SLEEP_TIME);

	pwm_pin_set_cycles(pwmb1, LED1_CHANNEL, 100, pwm_duty_cycle[1], PWM_POLARITY_NORMAL); //Turning on green led
	k_msleep(SLEEP_TIME);

	pwm_pin_set_cycles(pwmb2, LED2_CHANNEL, 100, pwm_duty_cycle[2], PWM_POLARITY_NORMAL); //Turning on blue led 
	k_msleep(SLEEP_TIME);

    return 0;
}

static int cmd_ledm(const struct shell *shell, size_t argc, char **argv) // LED matrix turn on command implementation
{
	const struct display_driver_api *api = (struct display_driver_api*)spi2->api; // Declaring the local API functions
	api->write(spi2, 0, 8, NULL, (void *)clear_data);  //Clearing the LED matrix from the previous values

	uint16_t address[8];  
	uint16_t data[8];
	int j = 2; 
	int x; 
	int y = argc-2; //Number of rows passing as an argument to the write command 

	int row = strtol(argv[1], NULL, 16);  //converting the row value given in the command from char to int 
	int addr = 0xF0; 

	//converting the arguments from strings to hexadecimal values
	for (int i = 0; i < argc-2; i++)
	{
		address[i] = (int)strtol(argv[j], NULL, 16);
		j++; 
	}
	x = row;
	row++;
	//converting the values to proper data address format 
	for (int i = 0; i < argc-2; i++)
	{
		addr = (addr | row)*256;
		data[i] = addr | address[i];
		j++; 
		row++;
		addr = 0xF0;
	}
	
	api->write(spi2, x, y, NULL, (void *)data); //calling the write api function
	
	return 0;
}

static int cmd_ledb(const struct shell *shell, size_t argc, char **argv) // LED matrix blinking on and off command implementation
{
	const struct display_driver_api *api = (struct display_driver_api*)spi2->api; // Declaring the local API functions
	int a = atoi(argv[1]); //converting values from char to integer

	if (a == 1)
	{
		blink_mode = true;    //the flag is used to set the blink mode to be used in main function 
	}
	else
	{
		blink_mode = false;
		api->blanking_off(spi2);     //calling the blanking off function 
	}

	return 0;
}

//shell commands register

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_rgb, 
	SHELL_CMD(rgb, NULL, "PWM Led Intensity command.", cmd_rgb),          //rgb sub command
	SHELL_CMD(ledm, NULL, "Turning on LED matrix command.", cmd_ledm),    //ledm sub command
	SHELL_CMD(ledb, NULL, "Blinking the pattern on Matrix", cmd_ledb),    //ledb sub command
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(p2, &sub_rgb, "List of commands", NULL);               //p2 root command


//Main function

void main(void)
{
	spi_pinmux_config();          //configuring the gpio pins as spi pins
	pwm_pinmux_config();		  //configuring the gpio pins as pwm pins	
	all_device_bindings();		  //getting device bindings
	configure_spi();			  //configuring the spi for normal mode
	clear_matrix();				  //clearing the led matrix before writing

	const struct display_driver_api *apifunc = (struct display_driver_api*)spi2->api;

	bool local = true;           //implementing the continuously blinking
	while(true)
	{
		if(blink_mode)
		{
			if(local)
			{
				apifunc->blanking_off(spi2);
				local = false;
			}
			else
			{
				apifunc->blanking_on(spi2);
				local = true;
			}
		}
		k_msleep(SLEEP_TIME);
	}
}
