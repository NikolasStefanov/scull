#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <pthread.h>

#include "scull.h"

#define CDEV_NAME "/dev/scull"

/* Quantum command line option */
static int g_quantum;

static void usage(const char *cmd)
{
	printf("Usage: %s <command>\n"
	       "Commands:\n"
	       "  R          Reset quantum\n"
	       "  S <int>    Set quantum\n"
	       "  T <int>    Tell quantum\n"
	       "  G          Get quantum\n"
	       "  Q          Qeuery quantum\n"
	       "  X <int>    Exchange quantum\n"
	       "  H <int>    Shift quantum\n"
	       "  h          Print this message\n"
	       "  K          Runs 1 step of K case\n"
	       "  p <int>    Runs the K case <int> times\n"
	       "  t <int>    Runs <int> threads of the K case\n",
	       cmd);
}

typedef int cmd_t;

static cmd_t parse_arguments(int argc, const char **argv)
{
	cmd_t cmd;

	if (argc < 2) {
		fprintf(stderr, "%s: Invalid number of arguments\n", argv[0]);
		cmd = -1;
		goto ret;
	}

	/* Parse command and optional int argument */
	cmd = argv[1][0];
	switch (cmd) {
	case 'S':
	case 'T':
	case 'H':
	case 'p':
		//Ensure that value exists and is between 0 and 11
		if (argc < 3) {
			fprintf(stderr, "%s: Missing quantum\n", argv[0]);
			cmd = -1;
			break;
		}
		g_quantum = atoi(argv[2]);
		if(g_quantum > 10 || g_quantum < 1){
			fprintf(stderr, "num_processes not between 0 and 11");
			return -1;
		}
		break;
	case 't':
		//Ensure that value exists and is between 0 and 11
		if (argc < 3) {
			fprintf(stderr, "%s: Missing quantum\n", argv[0]);
			cmd = -1;
			break;
		}
		g_quantum = atoi(argv[2]);
		if(g_quantum > 10 || g_quantum < 1){
			fprintf(stderr, "num_threads not between 0 and 11");
			return -1;
		}
		break;
	case 'X':
		if (argc < 3) {
			fprintf(stderr, "%s: Missing quantum\n", argv[0]);
			cmd = -1;
			break;
		}
		g_quantum = atoi(argv[2]);
		break;
	case 'R':
	case 'G':
	case 'Q':
	case 'h':
		break;
	default:
		fprintf(stderr, "%s: Invalid command\n", argv[0]);
		cmd = -1;
	}

ret:
	if (cmd < 0 || cmd == 'h') {
		usage(argv[0]);
		exit((cmd == 'h')? EXIT_SUCCESS : EXIT_FAILURE);
	}
	return cmd;
}

//Perform the K case
static int k_case(int fd){
	struct task_info* taskTest;
	int ret;
	//Create the Task Struct that will be filled by Kernel
	taskTest = (struct task_info*)malloc(sizeof(struct task_info));
	ret = ioctl(fd, SCULL_IOCKQUANTUM, taskTest);
	//On success, print info
	if(ret == 0){
		printf("state: %ld, stack %lx, cpu %d, prio %d, sprio %d, nprio %d, rtprio %d, pid %d, tgid %d, nv %lu, niv %lu\n",
			taskTest->state, (long)taskTest->stack, taskTest->cpu, taskTest->prio,
			taskTest->static_prio, taskTest->normal_prio, taskTest->rt_priority,
			taskTest->pid, taskTest->tgid, taskTest->nvcsw, taskTest->nivcsw);

	}
	//On failure, print error
	else{
		printf("Error");
	}
	free(taskTest);
	return ret;
}

//Perform the K case but takes in void*
static void* thread_k_case(void* fd){
	struct task_info* taskTest;
	int ret;
	int good_fd;
	good_fd = (int)fd;
	//Create the Task Struct that will be filled by Kernel
	taskTest = (struct task_info*)malloc(sizeof(struct task_info));
	ret = ioctl(good_fd, SCULL_IOCKQUANTUM, taskTest);
	//On success, print info
	if(ret == 0){
		printf("state: %ld, stack %lx, cpu %d, prio %d, sprio %d, nprio %d, rtprio %d, pid %d, tgid %d, nv %lu, niv %lu\n",
			taskTest->state, (long)taskTest->stack, taskTest->cpu, taskTest->prio,
			taskTest->static_prio, taskTest->normal_prio, taskTest->rt_priority,
			taskTest->pid, taskTest->tgid, taskTest->nvcsw, taskTest->nivcsw);

	}
	//On failure, print error
	else{
		printf("Error");
	}
	free(taskTest);
	return ret;
}

static int do_op(int fd, cmd_t cmd)
{
	int ret, q;
	int num_procs;
	int num_threads;
	int i;
	int retval;
	int returnStatus;
	pid_t wpid;
	pthread_t *tid;

	switch (cmd) {
	case 'R':
		ret = ioctl(fd, SCULL_IOCRESET);
		if (ret == 0)
			printf("Quantum reset\n");
		break;
	case 'Q':
		q = ioctl(fd, SCULL_IOCQQUANTUM);
		printf("Quantum: %d\n", q);
		ret = 0;
		break;
	case 'G':
		ret = ioctl(fd, SCULL_IOCGQUANTUM, &q);
		if (ret == 0)
			printf("Quantum: %d\n", q);
		break;
	case 'T':
		ret = ioctl(fd, SCULL_IOCTQUANTUM, g_quantum);
		if (ret == 0)
			printf("Quantum set\n");
		break;
	case 'S':
		q = g_quantum;
		ret = ioctl(fd, SCULL_IOCSQUANTUM, &q);
		if (ret == 0)
			printf("Quantum set\n");
		break;
	case 'X':
		q = g_quantum;
		ret = ioctl(fd, SCULL_IOCXQUANTUM, &q);
		if (ret == 0)
			printf("Quantum exchanged, old quantum: %d\n", q);
		break;
	case 'H':
		q = ioctl(fd, SCULL_IOCHQUANTUM, g_quantum);
		printf("Quantum shifted, old quantum: %d\n", q);
		ret = 0;
		break;
	case 'p':
		ret = 0;
		num_procs = g_quantum;
		//Fork num_procs time and run k_case for each child process
		for(i=0;i<num_procs;i++){
			if(fork() == 0){
			//In the child process
				retval = k_case(fd);
				if(retval == 0){
					//Process finished and can exit
					exit(EXIT_SUCCESS);
				}
				else{
					//Process crashed
					exit(EXIT_FAILURE);
				}
			}
			else{
			//In the parent process
				//Need to wait for all children to finish
				while((wpid = wait(&returnStatus))>0){
					if(!WIFEXITED(returnStatus)){
						printf("Error, chilren not waiting properly");
					}
				}
			}
		}
		break;
	case 't':
		ret = 0;
		num_threads = g_quantum;
		tid= (pthread_t*)malloc(sizeof(pthread_t)*num_threads);
		for(i=0;i<num_threads;i++){
			//Create all of the threads and get them to run thread_k_case
			if((retval = pthread_create(&tid[i], NULL, thread_k_case,(void*) fd) != 0)){
				printf("Error making thread");
			}
		}
		for(i=0;i<num_threads;i++){
			//Join all of the threads back together
			pthread_join(tid[i],NULL);
		}
		free(tid);
		break;
	default:
		/* Should never occur */
		abort();
		ret = -1; /* Keep the compiler happy */
	}

	if (ret != 0)
		perror("ioctl");
	return ret;
}

int main(int argc, const char **argv)
{
	int fd, ret;
	cmd_t cmd;

	cmd = parse_arguments(argc, argv);

	fd = open(CDEV_NAME, O_RDONLY);
	if (fd < 0) {
		perror("cdev open");
		return EXIT_FAILURE;
	}

	printf("Device (%s) opened\n", CDEV_NAME);

	ret = do_op(fd, cmd);

	if (close(fd) != 0) {
		perror("cdev close");
		return EXIT_FAILURE;
	}

	printf("Device (%s) closed\n", CDEV_NAME);

	return (ret != 0)? EXIT_FAILURE : EXIT_SUCCESS;
}
