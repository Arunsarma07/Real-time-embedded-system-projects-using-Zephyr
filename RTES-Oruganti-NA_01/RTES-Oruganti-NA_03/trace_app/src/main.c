#include <zephyr.h>
#include <device.h>
#include <drivers/gpio.h>
#include <sys/printk.h>
#include <sys/__assert.h>
#include <string.h>
#include <console/console.h>
#include <sys/_timespec.h>
#include <shell/shell.h>
#include <logging/log.h>
#include <version.h>
#include <stdlib.h>
#include <usb/usb_device.h>
#include <drivers/uart.h>
#include <ctype.h>
#include "task_model.h"


#define MY_STACK_SIZE 1024			//Stack size for each thread


K_THREAD_STACK_ARRAY_DEFINE(my_stack, NUM_THREADS, MY_STACK_SIZE);    //Creating an array of equally sized stacks


struct k_mutex my_mutex[NUM_MUTEXES];		//Defining Mutexes
struct k_thread my_thread_data[NUM_THREADS];	//Defining Threads
struct k_timer main_timer;			//Defining Timer (for main thread)
struct k_timer indv_timer;			//Defining Timers (for individual threads)

k_tid_t t_id_array[NUM_THREADS];			//Defining array thread id's as global variables



extern void my_expiry_function(struct k_timer *timer_id)		//Main timer expiry function for TOTAL_TIME
{
	printk("Main timer has expired\n");

	for (int i = 0; i < NUM_THREADS; i++)
	{
		printk("Putting %s to suspend\n", threads[i].t_name);

		k_thread_suspend(t_id_array[i]);		//Terminating all threads after main timer timeouts. 
	}
}

extern void indv_expiry_function(struct k_timer *indv_timer_id)	// Expiry function for the individual timer. 
{
	printk("Deadline is missed\n");		//Printing error message once the deadline is missed. 
}

extern void indv_stop_function(struct k_timer *indv_timer_id)	// timer stop function
{
	printk("Computation finished successfully\n");		// on successful calling of this function indicates, the computation is finished.
}


void initialize_mutexes()			//Generic function for initializing mutexes based on NUM_MUTEXES
{
	for (int i = 0; i < NUM_MUTEXES; i++)
	{
		k_mutex_init(&my_mutex[i]);	//initializing mutexes. 
	}	
}


void task_body(void *p1, void *p2, void *p3)		//Periodic task body.
{	
	struct task_s p = *(struct task_s *)p1;		//Pointer typecasting	
	
	while (1)					//task body
	{
		//printk("%s has started the task\n", p.t_name);	//Debug statement
		
		k_timer_start(&indv_timer, K_MSEC(p.period), K_NO_WAIT);

		//printk("Local timer started\n");			//Debug statement

		volatile uint64_t n = p.loop_iter[0];

		//printk("Computation 1 (%s)\n", p.t_name);	//Debugging statement to check in console

		while (n > 0)
		{
			n--;		//local computation 1 loop
		}

	
		k_mutex_lock(&my_mutex[p.mutex_m], K_FOREVER);	//locking corresponding mutex

		//printk("Computation 2 (%s)\n", p.t_name);	//Debugging statement to check in console

		n = p.loop_iter[1];

		while (n > 0)
		{
			n--;		//local computation 2
		}


		k_mutex_unlock(&my_mutex[p.mutex_m]);		//unlocking corresponding mutex

		//printk("Computation 3 (%s)\n", p.t_name);	//Debugging statement to check in console

		n = p.loop_iter[2];

		while (n > 0)
		{
			n--;		//local computation 3 
		}

		k_msleep(p.period);		//Thread waiting for the period

		k_timer_stop(&indv_timer);

		//printk("%s has ended the task\n", p.t_name);	//Debug statement

		//printk("Leaving task_body\n");	//Debugging statement to check in console
		
	}
}



void create_threads()			//Generic function for creating threads
{
	for (int count = 0; count < NUM_THREADS; count++)
	{
		t_id_array[count] = k_thread_create(&my_thread_data[count], my_stack[count],
                                 				MY_STACK_SIZE,
                                 				task_body,
                                 				&threads[count],NULL, NULL,
                                 				threads[count].priority, 0, K_FOREVER);		//Creating threads

		k_thread_name_set(t_id_array[count], threads[count].t_name);	//Setting thread names (to view in SystemView)
	}
}



static int activate_threads(const struct shell *shell, size_t argc, char **argv)		//Shell command function
{
 	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	k_timer_start(&main_timer, K_MSEC(TOTAL_TIME), K_NO_WAIT);			// starting timer for "total computation" 

	printk("Main Timer Started\n");		//Debug statement

	for(int i = 0; i < NUM_THREADS; i++)
	{
		k_thread_start(t_id_array[i]);			// threads starting after shell command activate
	}

	return 0;
}

SHELL_CMD_REGISTER(activate, NULL, "Activate all threads", activate_threads);		//Registering shell command "activate" 

void main()	//Main function.
{
	
	printk("Main function.\n");	//Debug statement

	printk("Please enter activate to start the threads.\n");	//Debug statement. 

	initialize_mutexes();		//calling generic mutex initialization function.

	k_timer_init(&main_timer, my_expiry_function, NULL);		//initializing the main timer for total computation
	k_timer_init(&indv_timer, indv_expiry_function, indv_stop_function);	//initializing the individual timers for deadline check

	create_threads();	//calling a generic thread create function.
}