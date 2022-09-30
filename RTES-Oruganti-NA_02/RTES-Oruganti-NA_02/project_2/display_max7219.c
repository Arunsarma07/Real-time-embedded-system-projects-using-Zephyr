/*
 * Copyright (c) 2017 Jan Van Winkel <jan.van_winkel@dxplore.eu>
 * Copyright (c) 2019 Nordic Semiconductor ASA
 * Copyright (c) 2019 Marc Reilly
 * Copyright (c) 2019 PHYTEC Messtechnik GmbH
 * Copyright (c) 2020 Endian Technologies AB
 * Copyright (c) 2020 Kim Bøndergaard <kim@fam-boendergaard.dk>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT maxim_max7219

//#include "display_max7219.h"

#include <device.h>
#include <drivers/spi.h>
#include <drivers/gpio.h>
#include <sys/byteorder.h>
#include <drivers/display.h>
#include <devicetree.h>
#include <sys/util.h>
#include <version.h>
#include <stdlib.h>
#include <dt-bindings/spi/spi.h>
#include <drivers/spi.h>
#include <string.h>
#include <devicetree.h>


struct max7219_config {
	const char *spi_name;
	struct spi_config spi_config;
	uint16_t height;
	uint16_t width;
};

struct max7219_data {
	const struct max7219_config *config;
	const struct device *spi_dev;  
};



static int my_display_blanking_on(const struct device *dev)
{
	struct max7219_data *data = (struct max7219_data *)dev->data;
	struct spi_buf tx_buf;
	struct spi_buf_set tx_bufs;
	uint16_t *temp = (uint16_t *)0x0C00;
	
	tx_bufs.buffers = &tx_buf;
	tx_bufs.count = 1;
	tx_buf.buf = &temp;
	tx_buf.len = 2;
	spi_write(data->spi_dev, &data->config->spi_config, &tx_bufs);
	return 0;
}

static int my_display_blanking_off(const struct device *dev)
{
	struct max7219_data *data = (struct max7219_data *)dev->data;
	struct spi_buf tx_buf;
	struct spi_buf_set tx_bufs;
	uint16_t *temp = (uint16_t *)0x0C01;
	
	tx_bufs.buffers = &tx_buf;
	tx_bufs.count = 1;
	tx_buf.buf = &temp;
	tx_buf.len = 2;
	spi_write(data->spi_dev, &data->config->spi_config, &tx_bufs);
	return 0; 
}


static int my_display_write(const struct device *dev,
			 const uint16_t x,
			 const uint16_t y,
			 const struct display_buffer_descriptor *desc,
			 const void *buf)
{
	struct max7219_data *data = (struct max7219_data *)dev->data;
	struct spi_buf tx_buf;
	struct spi_buf_set tx_bufs;

	uint16_t *temp = (uint16_t *)buf;

	tx_bufs.buffers = &tx_buf;
	tx_bufs.count = 1;
	for(int i =0 ; i<y; i++)
	{
		tx_buf.buf = &temp[i];
		tx_buf.len = 2;
		
		spi_write(data->spi_dev, &data->config->spi_config, &tx_bufs);
	}
	
	return 0;
}

static int max7219_init(const struct device *dev)
{
	struct max7219_config *config = (struct max7219_config *)dev->config;
	struct max7219_data *data = (struct max7219_data *)dev->data;

	data->spi_dev = device_get_binding(config->spi_name);
	if (data->spi_dev == NULL) {
		printk("Could not get SPI device for LCD");
		return -ENODEV;
	}
	
	return 0;
}

static const struct display_driver_api max7219_api = {
	.blanking_on = my_display_blanking_on,
	.blanking_off = my_display_blanking_off,
	.write = my_display_write,
};


#define MAX7219_INIT(inst)							\
	static struct max7219_data max7219_data_ ## inst;			\
										\
	const static struct max7219_config max7219_config_ ## inst = {		\
		.spi_name = DT_INST_BUS_LABEL(inst),				\
		.spi_config.slave = DT_INST_REG_ADDR(inst),	\
		.spi_config.frequency = UTIL_AND(            \
			DT_HAS_PROP(inst, spi_max_frequency) ,   \
			DT_INST_PROP(inst, spi_max_frequency)),		\
		.spi_config.operation = SPI_WORD_SET(16) | SPI_TRANSFER_MSB  | SPI_MODE_CPOL | SPI_OP_MODE_MASTER,	\
		.spi_config.cs = UTIL_AND(					\
			DT_INST_SPI_DEV_HAS_CS_GPIOS(inst),			\
			&(my7219_data_ ## inst.cs_ctrl)),			\
		.width = DT_INST_PROP(inst, width),				\
		.height = DT_INST_PROP(inst, height),				\
	};									\
										\
	static struct max7219_data max7219_data_ ## inst = {			\
		.config = &max7219_config_ ## inst,				\
	};									\
	DEVICE_DT_INST_DEFINE(inst, max7219_init, max7219_pm_control,		\
			      &max7219_data_ ## inst, &max7219_config_ ## inst,	\
			      APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY,	\
			      &max7219_api);

DT_INST_FOREACH_STATUS_OKAY(MAX7219_INIT)