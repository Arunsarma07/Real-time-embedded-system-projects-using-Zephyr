/*
* Name: Narasimha Arun Oruganti
* ASU Id: 1223956669
*
* This program tries to implement a 
* Polling server in order to serve 
* the aperiodic request arrived with 
* periodic tasks. 
*/

#include <zephyr.h>
#include <kernel.h>
#include <sys/printk.h>
#include <sys/util.h>
#include <timing/timing.h>
#include <shell/shell_uart.h>
#include <sys_clock.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/arch_interface.h>
#include "task_model_p4_new.h"

//Periodic threads and thread Ids
static struct k_thread my_thread_data[NUM_THREADS];
static k_tid_t thread_ids[NUM_THREADS];

//Polling server thread and Id
struct k_thread polling_thread_data;
static k_tid_t polling_tid;

//Periodic task signals 
static int my_thread_idx[NUM_THREADS];
static int complete_flag[NUM_THREADS];
static struct k_sem waiting_sem[NUM_THREADS];

//Thread stack definition
static K_THREAD_STACK_DEFINE(thread_stack_area, STACK_SIZE * NUM_THREADS);
static K_THREAD_STACK_DEFINE(polling_stack_area, STACK_SIZE);

//Worker Queue structure
struct thread_priority_info {
    struct k_work work;
    int priority;
} my_polling_thread;

//Timers for aperiodic computations
struct k_timer remaining_budget_timer;
struct k_timer replenishing_timer;

//Global variable for the polling server info
struct task_aps *gpoll_info;

//For calculating the average response time
uint64_t average_response_time = 0;
int total_requests = 0;

//Flag used for exiting the while loops
static bool run_thread_flag = true;

/*
* Aperiodic switched in function. 
*
* This function helps to track the budget. When the context switch in happens, 
* it checks the thread id is same as polling server id. 
* If it is same, then it starts a timer for the left over budget. 
*/
void aperiodic_switched_in(void)
{
    static k_tid_t curr_thrd_id;
    curr_thrd_id = k_current_get();  //read the current thread id
    
    if((curr_thrd_id == polling_tid)& (gpoll_info->left_budget > 0))  //If it is same as polling server, then start the timer for remaining budget
    {
        k_timer_start(&remaining_budget_timer, K_NSEC(gpoll_info->left_budget), K_NO_WAIT);
        gpoll_info->last_switched_in = k_cycle_get_32();
    }   
}

/*
* Aperiodic switched out function.
* 
* When the context switch out happens, it checks if the 
* exited thread is polling server and then stops the above timer
* and the left over budget is changed to remaining timer count. 
*/
void aperiodic_switched_out(void)
{
	static k_tid_t curr_thrd_id;
    curr_thrd_id = k_current_get(); // read the current thread id

    if(curr_thrd_id == polling_tid)  //If it is same as polling server, then stop the timer and update left_budget
    {
        k_timer_stop(&remaining_budget_timer);
        gpoll_info->left_budget = 1000000*k_timer_remaining_get(&remaining_budget_timer);
    }   
}

//Periodic threads timer expiry function for deadline misses
static void timer_expiry_function(struct k_timer *timer_exp)
{
    int id = *(int *)timer_exp->user_data;
    int ret;

    ret = complete_flag[id];   
    if (ret==1)         //if the task is done within the time period, give the semaphore
    {
        k_sem_give(&waiting_sem[id]); 
    }
    else                //else update the deadline missing and give the semaphore
    {
        printk("Task %d missed its deadline\n", id);
        k_sem_give(&waiting_sem[id]);
    }
}

//polling server local timer expiry function
/*
* When the budget is finished, the polling server goes to
* background mode by setting the priority to 14. 
*/
static void local_timer_expiry_func(struct k_timer *loc_timer_exp)
{
    //k_thread_priority_set(polling_tid, 14);
    gpoll_info->left_budget = 0;
    my_polling_thread.priority = 14;
    k_work_submit(&my_polling_thread.work);     //submit the priority to worker queue
}

//replenishing the budget
/*
* When the timer is expired, replenish the budget and restore back the priority.
*/
static void replenishing_timer_exp_func(struct k_timer *repl_timer_exp)
{
    gpoll_info->left_budget = 1000000*BUDGET;
    my_polling_thread.priority = POLL_PRIO;
    k_work_submit(&my_polling_thread.work);     //submit the priority to worker queue
}

//set priority
/*
* This function is the worker queue handler function
* which sets the priority when the timer handler is expired
*/
void set_thread_priority(struct k_work *item)
{
    struct thread_priority_info *the_thread_prio = CONTAINER_OF(item, struct thread_priority_info, work);
    k_thread_priority_set(polling_tid, the_thread_prio->priority);
}

//Polling server entry point function
static void polling_entry_point(void *v_poll_info, void *v_poll_prio, void *unused)
{

    printk("\nInside Polling server\n");
    struct task_aps *my_poll_info = (struct task_aps *)v_poll_info;
    gpoll_info = my_poll_info;      //global variable 

    struct req_type data;
    uint32_t end_time;
    uint64_t current_response_time;

    int ret; 

    //replenishing the budget
    k_timer_init(&replenishing_timer, replenishing_timer_exp_func, NULL);
    k_timer_start(&replenishing_timer, K_MSEC(my_poll_info->period), K_MSEC(my_poll_info->period));

    //Initiating the worker queue
    k_work_init(&my_polling_thread.work, set_thread_priority);

    printk("Reading the message queue\n");
    while(run_thread_flag)
    {
        //reading the message queue
        ret = k_msgq_get(&req_msgq, &data, K_FOREVER);  
        if(ret == 0)  //check if there are messages in the polling server queue
        {
            k_thread_priority_set(polling_tid, POLL_PRIO);
            looping(data.iterations);           //Aperiodic calculations
            end_time = k_cycle_get_32();
            current_response_time = timing_cycles_to_ns(sub32(data.arr_time, end_time));
            average_response_time = average_response_time + current_response_time;
            total_requests = data.id +1; 
        }   
        else        //if there are no messages in queue, change the budget to 0 and priority to background mode
        {
            gpoll_info->left_budget = 0;
            k_thread_priority_set(polling_tid, 14);
        }  
    }
    return;
}


// Function that is executed for each thread in the task set
static void thread(void *v_task_info, void *v_thread_id, void *unused)
{
    struct task_s *task_info = (struct task_s *)v_task_info;
    int thread_id = *(int *)v_thread_id; 
 
	uint32_t period;

    struct k_timer task_timer;  //periodic task timer

    k_timer_init(&task_timer, timer_expiry_function, NULL);     //intializing the each task timer
    task_timer.user_data = v_thread_id;

    printk("\nTask Id: %d started\nPeriod: %d\nPriority: %d\n\n", thread_id, task_info->period, task_info->priority);

	period = 1000000*task_info->period; 
    k_timer_start(&task_timer,K_NSEC(period), K_NSEC(period));  //starting the timer

    while (run_thread_flag) 
    {     
        complete_flag[thread_id]=0;
        looping(task_info->loop_iter);      //looping computation task.
        complete_flag[thread_id]=1;

        k_sem_take(&waiting_sem[thread_id], K_FOREVER);
    }
}

// Start all threads defined in the task set
static void start_threads(void)
{
    for (int i = 0; i < NUM_THREADS; i++) {
        k_sem_init(&waiting_sem[i], 0, 1);
        complete_flag[i]=0;
    }

    // Start each periodic task thread
    for (int i = 0; i < NUM_THREADS; i++) {
		my_thread_idx[i]=i;
        thread_ids[i] = k_thread_create(&my_thread_data[i],
                                         &thread_stack_area[STACK_SIZE * i],
                                         STACK_SIZE * sizeof(k_thread_stack_t),
                                         thread, (void *)&threads[i],
                                         (void *)&my_thread_idx[i], NULL, threads[i].priority,
                                         0, K_MSEC(10));

        k_thread_name_set(thread_ids[i], threads[i].t_name);        //setting the thread name
        printk("\nCreating thread %d.\n", i);       

    }

    //Starting the polling server thread
    polling_tid = k_thread_create(&polling_thread_data, polling_stack_area,
                                 K_THREAD_STACK_SIZEOF(polling_stack_area),
                                 polling_entry_point,
                                 (void *)&poll_info, 0, NULL,
                                 POLL_PRIO, 0, K_MSEC(10));

    k_thread_name_set(polling_tid, poll_info.t_name);           //setting the polling server thread
}


//Main Function - Entry point
void main(void)
{
    //Sleeping the main for 5 secs to record the proper data in systemview.
    //k_sleep(K_MSEC(10000));

    // Spawning the polling server and all periodic threads
    start_threads();

    //Starting a timer for replenishing the budget
    k_timer_init(&remaining_budget_timer, local_timer_expiry_func, NULL);
    
    //Starting the message request timer
    k_timer_start(&req_timer, K_USEC(ARR_TIME), K_NO_WAIT);

    //Put the main thread for total period. 
    k_sleep(K_MSEC(TOTAL_TIME));

    //Exiting the while loop
    run_thread_flag = false;

    //give the semaphore for waiting threads
    for (int i = 0; i < NUM_THREADS; ++i) {
        k_sem_give(&waiting_sem[i]);
    }
    printk("\r\n");
    //Terminating the threads
    for (int i = 0; i < NUM_THREADS; ++i) {
        k_thread_join(&my_thread_data[i],K_FOREVER);
        printk("Terminating Thread %d\n", i);
    }
    //terminating the polling server
    k_thread_abort(polling_tid);
    k_timer_stop(&remaining_budget_timer);
    k_timer_stop(&replenishing_timer);
    k_timer_stop(&req_timer);
    
    printk("Terminating polling server\n");
    
    printk("\nNo of request served: %d\n",total_requests);
    average_response_time = (average_response_time) / (total_requests * 1000000); //converting nanoseconds to milliseconds

    printk("\nAverage response time: %lldms\n", average_response_time);
}
