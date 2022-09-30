/*
 * This code implements the CoAP server 
 * to measure the distance from HC-SR04 Ultrasonic sensors
 * and to operate and read the status of RGB leds 
 * through Cu4cr extension for CoAP server 
 */

#include <kernel.h>
#include <string.h>
#include <zephyr.h>
#include <device.h>
#include <drivers/sensor.h>
#include <stdio.h>
#include <sys/__assert.h>
#include <sys/byteorder.h>
#include <sys/printk.h>
#include <logging/log.h>
#include <net/net_if.h>
#include <net/net_core.h>
#include <net/net_context.h>
#include <net/net_mgmt.h>
#include <linker/sections.h>
#include <errno.h>
#include <net/socket.h>
#include <net/net_mgmt.h>
#include <net/net_ip.h>
#include <net/udp.h>
#include <net/coap.h>
#include <net/coap_link_format.h>
#include <drivers/gpio.h>
#include <stdlib.h>

#define DEBUG 

#if defined(DEBUG) 
	#define DPRINTK(fmt, args...) printk("DEBUG: %s():%d: " fmt, \
   		 __func__, __LINE__, ##args)
#else
 	#define DPRINTK(fmt, args...) /* do nothing if not defined*/
#endif

LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

#define MY_STACK_SIZE 1024
#define MY_PRIORITY_1 5

// User defined LEDs - GPIO
#define LED_RED DT_NODELABEL(r_led)

#define LED0	DT_GPIO_LABEL(LED_RED, gpios)
#define PIN0	DT_GPIO_PIN(LED_RED, gpios)
#define FLAGS0	DT_GPIO_FLAGS(LED_RED, gpios)

#define LED_GREEN DT_NODELABEL(g_led)

#define LED1	DT_GPIO_LABEL(LED_GREEN, gpios)
#define PIN1	DT_GPIO_PIN(LED_GREEN, gpios)
#define FLAGS1	DT_GPIO_FLAGS(LED_GREEN, gpios)

#define LED_BLUE DT_NODELABEL(b_led)

#define LED2	DT_GPIO_LABEL(LED_BLUE, gpios)
#define PIN2	DT_GPIO_PIN(LED_BLUE, gpios)
#define FLAGS2	DT_GPIO_FLAGS(LED_BLUE, gpios)

//Device structures as global parameters
const struct device *dev1, *dev2;
const struct device *gpio_1, *gpio_3;
int flag =0;
int sensor0_flag=0;
int sensor1_flag=0;

//CoAP server definitions
#include "net_private.h"

#define MAX_COAP_MSG_LEN 256

#define MY_COAP_PORT 5683

#define BLOCK_WISE_TRANSFER_SIZE_GET 2048

#define NUM_OBSERVERS 3

#define NUM_PENDINGS 3

int sampling_period = 500;

static const char * const ssr_path0[] = { "sensor", "hcsr_0", NULL };
static const char * const ssr_path1[] = { "sensor", "hcsr_1", NULL };
static const char * const ssr_period[] = { "sensor", "period", NULL };

static const char * const led_r_path[] = { "led", "led_r", NULL };
static const char * const led_g_path[] = { "led", "led_g", NULL };
static const char * const led_b_path[] = { "led", "led_b", NULL };

// CoAP socket definitions
static int sock;

static struct coap_observer observers[NUM_OBSERVERS];

static struct coap_pending pendings[NUM_PENDINGS];

static struct coap_resource *resource_to_notify1;
static struct coap_resource *resource_to_notify2;
static struct coap_resource *resource_to_notify;

static struct k_work_delayable retransmit_work;

//Sensor flags for observe 
struct sensor_value distance;

float prev_dist1 = 0.0f;
float prev_dist2 = 0.0f;
float curr_dist1 = 0.0f;
float curr_dist2 = 0.0f;
int obs_start; 
//HC-SR04 Ultrasonic Senor measurement function using the driver
static int distance_measure(const struct device *dev)
{
    int ret;
    ret = sensor_sample_fetch_chan(dev, SENSOR_CHAN_ALL);
    switch (ret) {
    case 0:
        ret = sensor_channel_get(dev, SENSOR_CHAN_DISTANCE, &distance);
        if (ret) {
            LOG_ERR("sensor_channel_get failed ret %d", ret);
            return ret;
        }
        return 0; 
    case -EIO:
        LOG_WRN("%s: Could not read device", dev->name);
        return -1;
    default:
        LOG_ERR("Error when reading device: %s", dev->name);
        return -1;
    }
    return 0;
}

//DHCP client for dynamic IPV4 address
static struct net_mgmt_event_callback mgmt_cb;

static void handler(struct net_mgmt_event_callback *cb,
		    uint32_t mgmt_event,
		    struct net_if *iface)
{
	int i = 0;

	if (mgmt_event != NET_EVENT_IPV4_ADDR_ADD) {
		return;
	}

	for (i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
		char buf[NET_IPV4_ADDR_LEN];

		if (iface->config.ip.ipv4->unicast[i].addr_type !=
							NET_ADDR_DHCP) {
			continue;
		}

		LOG_INF("Your address: %s",
			log_strdup(net_addr_ntop(AF_INET,
			    &iface->config.ip.ipv4->unicast[i].address.in_addr,
						  buf, sizeof(buf))));
		LOG_INF("Lease time: %u seconds",
			 iface->config.dhcpv4.lease_time);
		LOG_INF("Subnet: %s",
			log_strdup(net_addr_ntop(AF_INET,
				       &iface->config.ip.ipv4->netmask,
				       buf, sizeof(buf))));
		LOG_INF("Router: %s",
			log_strdup(net_addr_ntop(AF_INET,
						 &iface->config.ip.ipv4->gw,
						 buf, sizeof(buf))));
	}
}

//CoAP server start function
static int start_coap_server(void)
{
	struct sockaddr_in addr;
	int r;

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(MY_COAP_PORT);

	sock = socket(addr.sin_family, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0) {
		LOG_ERR("Failed to create UDP socket %d", errno);
		return -errno;
	}

	r = bind(sock, (struct sockaddr *)&addr, sizeof(addr));
	if (r < 0) {
		LOG_ERR("Failed to bind UDP socket %d", errno);
		return -errno;
	}

	return 0;
}

//CoAP server reply function
static int send_coap_reply(struct coap_packet *cpkt,
			   const struct sockaddr *addr,
			   socklen_t addr_len)
{
	int r;

	net_hexdump("Response", cpkt->data, cpkt->offset);

	r = sendto(sock, cpkt->data, cpkt->offset, 0, addr, addr_len);
	if (r < 0) {
		LOG_ERR("Failed to send %d", errno);
		r = -errno;
	}

	return r;
}

//well known core - get function()
static int well_known_core_get(struct coap_resource *resource,
			       struct coap_packet *request,
			       struct sockaddr *addr, socklen_t addr_len)
{
	struct coap_packet response;
	uint8_t *data;
	int r;

	data = (uint8_t *)k_malloc(MAX_COAP_MSG_LEN);
	if (!data) {
		return -ENOMEM;
	}

	r = coap_well_known_core_get(resource, request, &response,
				     data, MAX_COAP_MSG_LEN);
	if (r < 0) {
		goto end;
	}

	r = send_coap_reply(&response, addr, addr_len);

end:
	if(data)
	{
		k_free(data);
	}

	return r;
}

//LED status get function
static int led_get(struct coap_resource *resource,
			 struct coap_packet *request,
			 struct sockaddr *addr, socklen_t addr_len)
{
    struct coap_packet response;
	uint8_t payload[40];
	uint8_t token[COAP_TOKEN_MAX_LEN];
	uint8_t *data;
	uint16_t id;
	uint8_t code;
	uint8_t type;
	uint8_t tkl;
	int r;

	code = coap_header_get_code(request);
	type = coap_header_get_type(request);
	id = coap_header_get_id(request);
	tkl = coap_header_get_token(request, token);

	LOG_INF("*******");
	LOG_INF("type: %u code %u id %u", type, code, id);
	LOG_INF("*******");

	if (type == COAP_TYPE_CON) {
		type = COAP_TYPE_ACK;
	} else {
		type = COAP_TYPE_NON_CON;
	}

	data = (uint8_t *)k_malloc(MAX_COAP_MSG_LEN);
	if (!data) {
		return -ENOMEM;
	}

	r = coap_packet_init(&response, data, MAX_COAP_MSG_LEN,
			     COAP_VERSION_1, type, tkl, token,
			     COAP_RESPONSE_CODE_CONTENT, id);
	if (r < 0) {
		goto end;
	}

	r = coap_append_option_int(&response, COAP_OPTION_CONTENT_FORMAT,
				   COAP_CONTENT_FORMAT_TEXT_PLAIN);
	if (r < 0) {
		goto end;
	}

	r = coap_packet_append_payload_marker(&response);
	if (r < 0) {
		goto end;
	}
    
    char temp[10];
    int val = 0;
	
	//If the resource path is equal to led_r path, then get LED_R status
    if (strcmp((const char *)led_r_path, (const char *)resource->path) == 0)
    {
        strcpy(temp, "Led Red");
        val = gpio_pin_get(gpio_1, PIN0);
    }
	//If the resource path is equal to led_g path, then get LED_G status
    else if (strcmp((const char *)led_g_path, (const char *)resource->path) == 0)
    {
        strcpy(temp, "Led Green");
        val = gpio_pin_get(gpio_1, PIN1);
    }
	//If the resource path is equal to led_b path, then get LED_B status
    else if (strcmp((const char *)led_b_path, (const char *)resource->path) == 0)
    {
        strcpy(temp, "Led Blue");
        val = gpio_pin_get(gpio_3, PIN2);
    }
    //CoAP server reply
	r = snprintk((char *) payload, sizeof(payload),
		     "%s status: %d", temp, val);
	if (r < 0) {
		goto end;
	}

	r = coap_packet_append_payload(&response, (uint8_t *)payload,
				       strlen(payload));
	if (r < 0) {
		goto end;
	}

	r = send_coap_reply(&response, addr, addr_len);

end:
	if(data)
	{
		k_free(data);
	}

	return r;
}


//LED put function to turn on/off the user led from CoAP server
static int led_put(struct coap_resource *resource,
		    struct coap_packet *request,
		    struct sockaddr *addr, socklen_t addr_len)
{
	struct coap_packet response;
	uint8_t token[COAP_TOKEN_MAX_LEN];
	const uint8_t *payload;
	uint8_t *data;
	uint16_t payload_len;
	uint8_t code;
	uint8_t type;
	uint8_t tkl;
	uint16_t id;
	int r;

    int led_val = 0;

	code = coap_header_get_code(request);
	type = coap_header_get_type(request);
	id = coap_header_get_id(request);
	tkl = coap_header_get_token(request, token);

	LOG_INF("*******");
	LOG_INF("type: %u code %u id %u", type, code, id);
	LOG_INF("*******");
    
	payload = coap_packet_get_payload(request, &payload_len);
	if (payload) {
		net_hexdump("PUT Payload", payload, payload_len);
        led_val = (int) *payload;
        led_val = led_val - 48;
        LOG_INF("led val is %d\r\n", led_val);


	}
	//If the path is for led-r, the led-r is turned on/off
    if (strcmp((const char *)led_r_path, (const char *)resource->path) == 0)
    {
        gpio_pin_set(gpio_1, PIN0, led_val);
    }
	//If the path is for led-r, the led-r is turned on/off
    else if (strcmp((const char *)led_g_path, (const char *)resource->path) == 0)
    {
        gpio_pin_set(gpio_1, PIN1, led_val);
    }
	//If the path is for led-r, the led-r is turned on/off
    else if (strcmp((const char *)led_b_path, (const char *)resource->path) == 0)
    {
        gpio_pin_set(gpio_3, PIN2, led_val);
    }

	if (type == COAP_TYPE_CON) {
		type = COAP_TYPE_ACK;
	} else {
		type = COAP_TYPE_NON_CON;
	}

	data = (uint8_t *)k_malloc(MAX_COAP_MSG_LEN);
	if (!data) {
		return -ENOMEM;
	}

	r = coap_packet_init(&response, data, MAX_COAP_MSG_LEN,
			     COAP_VERSION_1, type, tkl, token,
			     COAP_RESPONSE_CODE_CHANGED, id);
	if (r < 0) {
		goto end;
	}

	r = send_coap_reply(&response, addr, addr_len);

end:
	if(data)
	{
		k_free(data);
	}

	return r;
}

//Sensor period put function for setting the sampling period
static int sensor_period_put(struct coap_resource *resource,
		    struct coap_packet *request,
		    struct sockaddr *addr, socklen_t addr_len)
{
	struct coap_packet response;
	uint8_t token[COAP_TOKEN_MAX_LEN];
	const uint8_t *payload;
	uint8_t *data;
	uint16_t payload_len;
	uint8_t code;
	uint8_t type;
	uint8_t tkl;
	uint16_t id;
	int r;

	int period = 0;
	int temp[10];

	code = coap_header_get_code(request);
	type = coap_header_get_type(request);
	id = coap_header_get_id(request);
	tkl = coap_header_get_token(request, token);

	LOG_INF("*******");
	LOG_INF("type: %u code %u id %u", type, code, id);
	LOG_INF("*******");

	payload = coap_packet_get_payload(request, &payload_len);
	if (payload) {
		net_hexdump("PUT Payload", payload, payload_len);

		//Converting the payload string to integer value
		for (int i = 0; i < payload_len; i++)
		{
			temp[i] = (int) *payload;
			temp[i] = temp[i] - 48;
			period = (period + temp[i])*10;
			payload++;
		}
		period = period / 10;
		sampling_period = period;  //Sampling period
	}

	if (type == COAP_TYPE_CON) {
		type = COAP_TYPE_ACK;
	} else {
		type = COAP_TYPE_NON_CON;
	}

	data = (uint8_t *)k_malloc(MAX_COAP_MSG_LEN);
	if (!data) {
		return -ENOMEM;
	}

	r = coap_packet_init(&response, data, MAX_COAP_MSG_LEN,
			     COAP_VERSION_1, type, tkl, token,
			     COAP_RESPONSE_CODE_CHANGED, id);
	if (r < 0) {
		goto end;
	}

	r = send_coap_reply(&response, addr, addr_len);

end:
	if(data)
	{
		k_free(data);
	}

	return r;
}


static int send_notification_packet(const struct sockaddr *addr,
				    socklen_t addr_len,
				    uint16_t age, uint16_t id,
				    const uint8_t *token, uint8_t tkl,
				    bool is_response);

static void retransmit_request(struct k_work *work)
{
	struct coap_pending *pending;
	
	pending = coap_pending_next_to_expire(pendings, NUM_PENDINGS);
	if (!pending) {
		return;
	}

	if (!coap_pending_cycle(pending)) {
		k_free(pending->data);
		coap_pending_clear(pending);
		return;
	}

	k_work_reschedule(&retransmit_work, K_MSEC(pending->timeout));
}

static int create_pending_request(struct coap_packet *response,
				  const struct sockaddr *addr)
{
	struct coap_pending *pending;
	int r;
	LOG_INF("Inside create pending request\n");
	pending = coap_pending_next_unused(pendings, NUM_PENDINGS);
	if (!pending) {
		return -ENOMEM;
	}

	r = coap_pending_init(pending, response, addr,
			      COAP_DEFAULT_MAX_RETRANSMIT);
	if (r < 0) {
		return -EINVAL;
	}

	coap_pending_cycle(pending);

	pending = coap_pending_next_to_expire(pendings, NUM_PENDINGS);
	if (!pending) {
		return 0;
	}

	k_work_reschedule(&retransmit_work, K_MSEC(pending->timeout));

	return 0;
}

static int send_notification_packet(const struct sockaddr *addr,
				    socklen_t addr_len,
				    uint16_t age, uint16_t id,
				    const uint8_t *token, uint8_t tkl,
				    bool is_response)
{
	struct coap_packet response;
	char payload[100];
	uint8_t *data;
	uint8_t type;
	int r;

	if (is_response) {
		type = COAP_TYPE_ACK;
	} else {
		type = COAP_TYPE_CON;
	}

	if (!is_response) {
		id = coap_next_id();
	}

	data = (uint8_t *)k_malloc(MAX_COAP_MSG_LEN);
	if (!data) {
		return -ENOMEM;
	}

	r = coap_packet_init(&response, data, MAX_COAP_MSG_LEN,
			     COAP_VERSION_1, type, tkl, token,
			     COAP_RESPONSE_CODE_CONTENT, id);
	if (r < 0) {
		goto end;
	}

	if (age >= 2U) {
		r = coap_append_option_int(&response, COAP_OPTION_OBSERVE, age);
		if (r < 0) {
			goto end;
		}
	}

	r = coap_append_option_int(&response, COAP_OPTION_CONTENT_FORMAT,
				   COAP_CONTENT_FORMAT_TEXT_PLAIN);
	if (r < 0) {
		goto end;
	}

	r = coap_packet_append_payload_marker(&response);
	if (r < 0) {
		goto end;
	}

	/* The response that coap-client expects */
	if(flag == 1 && obs_start ==1)
	{
		r = snprintk((char *) payload, sizeof(payload), "Observation Started");
	}
	if(flag == 1 && sensor0_flag ==1)
	{
		r = snprintk((char *) payload, sizeof(payload), "Distance(s0), Old: %f:, New: %f", prev_dist1, curr_dist1);
	}
	else if(flag == 1 && sensor1_flag ==1)
	{
		r = snprintk((char *) payload, sizeof(payload), "Distance(s1), Old: %f:, New: %f", prev_dist2, curr_dist2);
	}
	
	if (r < 0) {
		goto end;
	}

	r = coap_packet_append_payload(&response, (uint8_t *)payload,
				       strlen(payload));
	if (r < 0) {
		goto end;
	}

	if (type == COAP_TYPE_CON) {
		r = create_pending_request(&response, addr);
		if (r < 0) {
			goto end;
		}
	}

	r = send_coap_reply(&response, addr, addr_len);

	/* On succesfull creation of pending request, do not free memory */
	if (type == COAP_TYPE_CON) {
		return r;
	}

end:
	if(data)
	{
		k_free(data);
	}

	return r;
}

//Sensor get function for fetching the distance
static int sensor_get(struct coap_resource *resource,
			 struct coap_packet *request,
			 struct sockaddr *addr, socklen_t addr_len)
{
    struct coap_packet response;
	struct coap_observer *observer;
	uint8_t payload[40];
	uint8_t token[COAP_TOKEN_MAX_LEN];
	uint8_t *data;
	uint16_t id;
	uint8_t code;
	uint8_t type;
	uint8_t tkl;
	int r = -1;
	bool observe = true;
	obs_start = 0;
	flag = 0;
	sensor0_flag = 0;
	sensor1_flag = 0;

	code = coap_header_get_code(request);
	type = coap_header_get_type(request);
	id = coap_header_get_id(request);
	tkl = coap_header_get_token(request, token);

	LOG_INF("*******");
	LOG_INF("type: %u code %u id %u", type, code, id);
	LOG_INF("*******");

	if (type == COAP_TYPE_CON) {
		type = COAP_TYPE_ACK;
	} else {
		type = COAP_TYPE_NON_CON;
	}

	data = (uint8_t *)k_malloc(MAX_COAP_MSG_LEN);
	if (!data) {
		return -ENOMEM;
	}

	if (coap_request_is_observe(request)) 
	{
		observer = coap_observer_next_unused(observers, NUM_OBSERVERS);
		if (!observer) {
			return -ENOMEM;
		}

		coap_observer_init(observer, request, addr);

		coap_register_observer(resource, observer);

		resource_to_notify = resource;

		send_notification_packet(addr, addr_len,
					observe ? resource->age : 0,
					id, token, tkl, true);

		if (coap_get_option_int(request, COAP_OPTION_OBSERVE) == 0)
		{
			flag = 1;
			
			if (strcmp((const char *)ssr_path0, (const char *)resource->path) == 0)
			{
				sensor0_flag = 1;
				sensor1_flag = 0;
				resource_to_notify1 = resource;
			}
			else if (strcmp((const char *)ssr_path1, (const char *)resource->path) == 0)
			{
				sensor0_flag = 0;
				sensor1_flag = 1;
				resource_to_notify2 = resource;
			}
			obs_start = 1;
			send_notification_packet(addr, addr_len,
                resource->age,
                id, token, tkl, true);
			obs_start = 0;
		}
		else if (coap_get_option_int(request, COAP_OPTION_OBSERVE) == 1)
		{
			flag = 0;
			sensor0_flag = 0;
			sensor1_flag = 0;
			send_notification_packet(addr, addr_len,
                resource->age,
                id, token, tkl, false);
		}
	}
	else
	{
		r = coap_packet_init(&response, data, MAX_COAP_MSG_LEN,
			     COAP_VERSION_1, type, tkl, token,
			     COAP_RESPONSE_CODE_CONTENT, id);
		if (r < 0) {
			goto end;
		}

		r = coap_append_option_int(&response, COAP_OPTION_CONTENT_FORMAT,
					COAP_CONTENT_FORMAT_TEXT_PLAIN);
		if (r < 0) {
			goto end;
		}

		r = coap_packet_append_payload_marker(&response);
		if (r < 0) {
			goto end;
		}
		
		char temp[10];
		bool flag_distance = false;
		if (strcmp((const char *)ssr_path0, (const char *)resource->path) == 0)
		{
			strcpy(temp, "Sensor 0");
			if (distance_measure(dev1) == 0)
			{
				flag_distance = true;
				r = snprintk((char *) payload, sizeof(payload),
					"%s : %0.2f Inches", temp, curr_dist1);
				if (r < 0) {
					goto end;
				}
			}
		}
		else if (strcmp((const char *)ssr_path1, (const char *)resource->path) == 0)
		{
			strcpy(temp, "Sensor 1");
			if (distance_measure(dev2) == 0)
			{
				flag_distance = true;
				r = snprintk((char *) payload, sizeof(payload),
					"%s : %0.2f Inches", temp, curr_dist2);
				if (r < 0) {
					goto end;
				}
			}
			
		}
	
		if (flag_distance == false)
		{
			//CoAP server response
			r = snprintk((char *) payload, sizeof(payload), "Failed to get distance:");
			if (r < 0) {
				goto end;
			}
		}
		r = coap_packet_append_payload(&response, (uint8_t *)payload,
						strlen(payload));
		if (r < 0) {
			goto end;
		}

		r = send_coap_reply(&response, addr, addr_len);
	}

end:
	if(data)
	{
		k_free(data);
	}
	return r;
}



static void sensor_notify(struct coap_resource *resource,
		       struct coap_observer *observer)
{
	LOG_INF("Inside observe notify function\n");
	send_notification_packet(&observer->addr,
				 sizeof(observer->addr),
				 resource->age, 0,
				 observer->token, observer->tkl, false);
}


static struct coap_resource resources[] = {
	{ .get = well_known_core_get,
	  .path = COAP_WELL_KNOWN_CORE_PATH,
	},
	{ .get = sensor_get,
	  .path = ssr_path0,
	  .notify = sensor_notify,
	},
	{ .get = sensor_get,
	  .path = ssr_path1,
	  .notify = sensor_notify,
	},
	{ .put = sensor_period_put,
	  .path = ssr_period
	},
	{ .get = led_get,
      .put = led_put,
	  .path = led_r_path
	},
	{ .get = led_get,
      .put = led_put,
	  .path = led_g_path
	},
	{ .get = led_get,
      .put = led_put,
	  .path = led_b_path
	},
	{ },
};

//CoAP resource by observer
static struct coap_resource *find_resouce_by_observer(
		struct coap_resource *resources, struct coap_observer *o)
{
	struct coap_resource *r;

	for (r = resources; r && r->path; r++) {
		sys_snode_t *node;

		SYS_SLIST_FOR_EACH_NODE(&r->observers, node) {
			if (&o->list == node) {
				return r;
			}
		}
	}

	return NULL;
}

//Function to process the CoAP request
static void process_coap_request(uint8_t *data, uint16_t data_len,
				 struct sockaddr *client_addr,
				 socklen_t client_addr_len)
{
	struct coap_packet request;
	struct coap_pending *pending;
	struct coap_option options[16] = { 0 };
	uint8_t opt_num = 16U;
	uint8_t type;
	int r;

	r = coap_packet_parse(&request, data, data_len, options, opt_num);
	if (r < 0) {
		LOG_ERR("Invalid data received (%d)\n", r);
		return;
	}

	type = coap_header_get_type(&request);

	pending = coap_pending_received(&request, pendings, NUM_PENDINGS);
	if (!pending) {
		goto not_found;
	}

	/* Clear CoAP pending request */
	if (type == COAP_TYPE_ACK) {
		k_free(pending->data);
		coap_pending_clear(pending);
	}

	return;

not_found:

	if (type == COAP_TYPE_RESET) {
		struct coap_resource *r;
		struct coap_observer *o;

		o = coap_find_observer_by_addr(observers, NUM_OBSERVERS,
					       client_addr);
		if (!o) {
			LOG_ERR("Observer not found\n");
			goto end;
		}

		r = find_resouce_by_observer(resources, o);
		if (!r) {
			LOG_ERR("Observer found but Resource not found\n");
			goto end;
		}

		coap_remove_observer(r, o);

		return;
	}

end:
	r = coap_handle_request(&request, resources, options, opt_num,
				client_addr, client_addr_len);
	if (r < 0) {
		LOG_WRN("No handler for such request (%d)\n", r);
	}
}

//Function to process the client request
static int process_client_request(void)
{
	int received;
	struct sockaddr client_addr;
	socklen_t client_addr_len;
	uint8_t request[MAX_COAP_MSG_LEN];

	do {
		client_addr_len = sizeof(client_addr);
		received = recvfrom(sock, request, sizeof(request), 0,
				    &client_addr, &client_addr_len);
		if (received < 0) {
			LOG_ERR("Connection error %d", errno);
			return -errno;
		}

		process_coap_request(request, received, &client_addr,
				     client_addr_len);
	} while (true);

	return 0;
}

//Thread body which calculates the distance from 2 sensors
extern void my_entry_point_1(void *p1, void *p2, void *p3)
{
    int ret1, ret2;
	double a, b;

    while (1) {
		//Measuring the distance from sensor1
        ret1 = distance_measure(dev1);
		a = sensor_value_to_double(&distance);
		curr_dist1 = (float) a;
		//Checking if observe is called or not
		if(flag == 1 && sensor0_flag == 1 && resource_to_notify1)
		{
			if (curr_dist1 - prev_dist1 > 0.5)
			{
				coap_resource_notify(resource_to_notify1); //calling notify only when the disance > 0.5
			}
		}
		prev_dist1 = curr_dist1;
		k_msleep(25);

		//Measuring the distance from sensor1
		ret2 = distance_measure(dev2);
		b = sensor_value_to_double(&distance);
		curr_dist2 = (float) b;
		//Checking if observe is called or not
		if(flag == 1 && sensor1_flag == 1 && resource_to_notify2)
		{
			if (curr_dist2 - prev_dist2 > 0.5)
			{
				coap_resource_notify(resource_to_notify2); //calling notify only when the disance > 0.5
			}
		}
		prev_dist2 = curr_dist2;
		k_msleep(sampling_period); //sleeping the thread for user sampling period
    }
    LOG_INF("exiting");
}

//Defingin the thread stack area
K_THREAD_STACK_ARRAY_DEFINE(my_stack_area, 2, MY_STACK_SIZE);

//Thread declarations
struct k_thread my_thread_data[2];
k_tid_t t_id_array[2];


//Main function
void main(void)
{
    int ret;

    if (IS_ENABLED(CONFIG_LOG_BACKEND_RTT)) {
        /* Give RTT log time to be flushed before executing tests */
        k_sleep(K_MSEC(500));
    }
    
	//GPIO device binding for user leds. 
    gpio_1 = device_get_binding(LED0);
	if (!gpio_1) {
		DPRINTK("error\n");
		return;
	}
    gpio_3 = device_get_binding(LED2);
	if (!gpio_3) {
		DPRINTK("error\n");
		return;
	}

	//GPIO pin configuration
    ret = gpio_pin_configure(gpio_1, PIN0, GPIO_OUTPUT_ACTIVE | FLAGS0);
	if (ret < 0) {
		return;
	}
    ret = gpio_pin_configure(gpio_1, PIN1, GPIO_OUTPUT_ACTIVE | FLAGS1);
	if (ret < 0) {
		return;
	}
    ret = gpio_pin_configure(gpio_3, PIN2, GPIO_OUTPUT_ACTIVE | FLAGS2);
	if (ret < 0) {
		return;
	}

	//HC-SR04 sensor device bindings
	#if CONFIG_HC_SR04
	    dev1 = device_get_binding("HC-SR04_0");
    	dev2 = device_get_binding("HC-SR04_1");
	#endif

    if (dev1 == NULL) {
        LOG_ERR("Failed to get dev1 binding");
        return;
    }
    if (dev2 == NULL) {
        LOG_ERR("Failed to get dev2 binding");
        return;
    }
    LOG_INF("dev is %p, name is %s", dev1, dev1->name);
    LOG_INF("dev is %p, name is %s", dev2, dev2->name);

	//Creating threads for sensor values
	DPRINTK("Creating thread for running the sensor values");
	t_id_array[0] = k_thread_create(&my_thread_data[0], my_stack_area[0],
                                 MY_STACK_SIZE,
                                 my_entry_point_1,
                                 NULL, NULL, NULL,
                                 MY_PRIORITY_1, 0, K_FOREVER);

	k_thread_start(t_id_array[0]); //starting the thread
	k_sleep(K_MSEC(500));
	
	//Running DHCPV4 Client for IPV4 address
    int r = 0;
	struct net_if *iface;
	LOG_INF("Running dhcpv4 client");

	net_mgmt_init_event_callback(&mgmt_cb, handler,
				     NET_EVENT_IPV4_ADDR_ADD);
	net_mgmt_add_event_callback(&mgmt_cb);

	iface = net_if_get_default();
	net_dhcpv4_start(iface);

	//Starting the CoAP server
	DPRINTK("Starting CoAP server\n\n");
    r = start_coap_server();
	if (r < 0) {
		goto quit;
	}

	k_work_init_delayable(&retransmit_work, retransmit_request);

	while (1) {
		r = process_client_request();
		if (r < 0) {
			goto quit;
		}
	}

    LOG_INF("Done");
	return;
quit:
	LOG_INF("Quit");
}
