This assignment focuses on developing an application program that uses multiple threads to implement periodic task models on Zephyr RTOS environment. The task set specifications are provided in task_model.h file, and the program should create a thread for each task to perform periodic operations. The tasks are expressed as endless loops with time-based triggers, and locks must be acquired when entering any critical sections.

The input to the program is the specification of the task set, defined in struct task_s. The tasks have a name, priority, period for periodic tasks in milliseconds, loop iterations for computing, and mutex to be locked and unlocked by the task. The program should use the task set specification defined in task_model.h to perform task operations periodically.

Additional requirements of the program include implementing a new shell root command "activate" to trigger task activation once all periodic tasks complete their initialization, put out an error message when a deadline is missed, and terminate all threads properly when the execution is completed.