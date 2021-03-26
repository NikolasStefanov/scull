/*
 * main.c -- the bare scull char module
 *
 * Copyright (C) 2001 Alessandro Rubini and Jonathan Corbet
 * Copyright (C) 2001 O'Reilly & Associates
 *
 * The source code in this file can be freely used, adapted,
 * and redistributed in source or binary form, so long as an
 * acknowledgment appears in derived source files.  The citation
 * should list that the code comes from the book "Linux Device
 * Drivers" by Alessandro Rubini and Jonathan Corbet, published
 * by O'Reilly & Associates.   No warranty is attached;
 * we cannot take responsibility for errors or fitness for use.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/kernel.h>	/* printk() */
#include <linux/slab.h>		/* kmalloc() */
#include <linux/fs.h>		/* everything... */
#include <linux/errno.h>	/* error codes */
#include <linux/types.h>	/* size_t */
#include <linux/cdev.h>

#include <linux/uaccess.h>	/* copy_*_user */

#include <linux/mutex.h>	/* mutex */

#include "scull.h"		/* local definitions */
#include "access_ok_version.h"


/*
 * Our parameters which can be set at load time.
 */

static int scull_major =   SCULL_MAJOR;
static int scull_minor =   0;
static int scull_quantum = SCULL_QUANTUM;

module_param(scull_major, int, S_IRUGO);
module_param(scull_minor, int, S_IRUGO);
module_param(scull_quantum, int, S_IRUGO);

MODULE_AUTHOR("Wonderful student of CS-492");
MODULE_LICENSE("Dual BSD/GPL");

static struct cdev scull_cdev;		/* Char device structure		*/


/*
 * Open and close
 */

static int scull_open(struct inode *inode, struct file *filp)
{
	printk(KERN_INFO "scull open\n");
	return 0;          /* success */
}

static int scull_release(struct inode *inode, struct file *filp)
{
	printk(KERN_INFO "scull close\n");
	return 0;
}

//Node struct for link list containing pid and tgid
struct Node{
	pid_t pid;
	pid_t tgid;
	struct Node* next;
};

//Linked list struct that contains pointers to the
//beginning and end of list
struct link_list{
	struct Node* head;
	struct Node* tail;
};

//Defining list globally
struct link_list* l_list;

static DEFINE_MUTEX(the_lock);
/*
 * The ioctl() implementation
 */

static long scull_ioctl(struct file *filp, unsigned int cmd,
		unsigned long arg)
{

	int err = 0, tmp;
	int retval = 0;
	int flag;
	struct task_info task;
	struct Node* newNode;
	pid_t pidToCheck;
	pid_t tidToCheck;
	struct Node* currPoint;
	/*
	 * extract the type and number bitfields, and don't decode
	 * wrong cmds: return ENOTTY (inappropriate ioctl) before access_ok()
	 */
	if (_IOC_TYPE(cmd) != SCULL_IOC_MAGIC) return -ENOTTY;
	if (_IOC_NR(cmd) > SCULL_IOC_MAXNR) return -ENOTTY;

	/*
	 * the direction is a bitmask, and VERIFY_WRITE catches R/W
	 * transfers. `Type' is user-oriented, while
	 * access_ok is kernel-oriented, so the concept of "read" and
	 * "write" is reversed
	 */
	if (_IOC_DIR(cmd) & _IOC_READ)
		err = !access_ok_wrapper(VERIFY_WRITE, (void __user *)arg,
				_IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		err =  !access_ok_wrapper(VERIFY_READ, (void __user *)arg,
				_IOC_SIZE(cmd));
	if (err) return -EFAULT;

	switch(cmd) {

	case SCULL_IOCRESET:
		scull_quantum = SCULL_QUANTUM;
		break;

	case SCULL_IOCSQUANTUM: /* Set: arg points to the value */
		retval = __get_user(scull_quantum, (int __user *)arg);
		break;

	case SCULL_IOCTQUANTUM: /* Tell: arg is the value */
		scull_quantum = arg;
		break;

	case SCULL_IOCGQUANTUM: /* Get: arg is pointer to result */
		retval = __put_user(scull_quantum, (int __user *)arg);
		break;

	case SCULL_IOCQQUANTUM: /* Query: return it (it's positive) */
		return scull_quantum;

	case SCULL_IOCXQUANTUM: /* eXchange: use arg as pointer */
		tmp = scull_quantum;
		retval = __get_user(scull_quantum, (int __user *)arg);
		if (retval == 0)
			retval = __put_user(tmp, (int __user *)arg);
		break;

	case SCULL_IOCHQUANTUM: /* sHift: like Tell + Query */
		tmp = scull_quantum;
		scull_quantum = arg;
		return tmp;

        case SCULL_IOCKQUANTUM: /* tasK_struct: copy's a task_info structure */
		//Check if the linked_list contains the current pid
		pidToCheck = current->pid;
		tidToCheck = current->tgid;
		currPoint = l_list->head;
		flag = 1;
		while(currPoint != NULL){
			//Iterate using currPoint until currPoint is NULL and check if PID and
			//TGID match existing values
			if(currPoint->pid == pidToCheck && currPoint->tgid == tidToCheck){
				printk("Tried adding pid that already existed\n");
				flag = 0;
				break;
			}
			currPoint = currPoint -> next;
		}
		if(flag == 1){
			//If the list doesn't contain pid:
			//Creating and Adding a new node to be added to the globally defined ll
			newNode = (struct Node*)kmalloc(sizeof(struct Node), GFP_KERNEL);
			newNode->pid = current->pid;
			newNode->tgid = current->tgid;
			newNode->next = NULL;
			//Making sure that the head and tail are set properly
			if(l_list->head == NULL){
				//First Time Adding
				mutex_lock(&the_lock);
				l_list->head = newNode;
				l_list->tail = newNode;
				mutex_unlock(&the_lock);
			}
			else{
				//Adding when first element is already in list
				mutex_lock(&the_lock);
				l_list->tail->next = newNode;
				l_list->tail = l_list->tail->next;
				mutex_unlock(&the_lock);
			}
		}
		//Filling the task struct with proper information to be copied over to user space
		task.state = current->state;
		task.stack = current->stack;
		task.cpu = current->cpu;
		task.prio = current->prio;
		task.static_prio = current->static_prio;
		task.normal_prio = current->normal_prio;
		task.rt_priority = current->rt_priority;
		task.pid = current->pid;
		task.tgid = current->tgid;
		task.nvcsw = current->nvcsw;
		task.nivcsw = current->nivcsw;
		//Copying task struct back to user space with proper information
		retval = copy_to_user((void __user *) arg, &task, sizeof(struct task_info));
		break;

	 default:  /* redundant, as cmd was checked against MAXNR */
		return -ENOTTY;
	}
	return retval;

}


struct file_operations scull_fops = {
	.owner =    THIS_MODULE,
	.unlocked_ioctl = scull_ioctl,
	.open =     scull_open,
	.release =  scull_release,
};

/*
 * Finally, the module stuff
 */

/*
 * The cleanup function is used to handle initialization failures as well.
 * Thefore, it must be careful to work correctly even if some of the items
 * have not been initialized
 */
void scull_cleanup_module(void)
{
	int taskNum;
	struct Node* tempNode;

	dev_t devno = MKDEV(scull_major, scull_minor);

	/* Get rid of the char dev entry */
	cdev_del(&scull_cdev);

	//If nothing gets added to l_list, print Empty List
	if(l_list->head == NULL){
		printk("List is empty\n");
	}
	else{
		printk("Linked List to be deleted: ");

		//Delete the linked list while printing out each step and using head as iterator
		taskNum=1;
		while(l_list->head->next != NULL){
			printk("Task %d: PID: %d; TID: %d ->",
				taskNum, l_list->head->pid, l_list->head->tgid);
			taskNum++;
			//Use tempNode to free the previous Node that was just printed
			tempNode = l_list->head;
			l_list->head = l_list->head->next;
			kfree(tempNode);
		}
		//Be sure not to print arrow after last element
		if(l_list->head != NULL){
			printk("Task %d: PID: %d: TID: %d\n", taskNum, l_list->head->pid, l_list->head->tgid);
			kfree(l_list->head);
		}
		//Free the tail in case some error happens and head != tail
		if(l_list->tail != NULL){
			kfree(l_list->tail);
		}
	}
	//Free the list
	kfree(l_list);
	/* cleanup_module is never called if registering failed */
	unregister_chrdev_region(devno, 1);

}


int scull_init_module(void)
{
	int result;
	dev_t dev = 0;

	//Malloc the linked_list and setting head and tail to NULL before K case is allowed to add
	l_list = (struct link_list*)kmalloc(sizeof(struct link_list), GFP_KERNEL);
	l_list->head = NULL;
	l_list->tail = NULL;
	/*
	 * Get a range of minor numbers to work with, asking for a dynamic
	 * major unless directed otherwise at load time.
	 */
	if (scull_major) {
		dev = MKDEV(scull_major, scull_minor);
		result = register_chrdev_region(dev, 1, "scull");
	} else {
		result = alloc_chrdev_region(&dev, scull_minor, 1, "scull");
		scull_major = MAJOR(dev);
	}
	if (result < 0) {
		printk(KERN_WARNING "scull: can't get major %d\n", scull_major);
		return result;
	}

	cdev_init(&scull_cdev, &scull_fops);
	scull_cdev.owner = THIS_MODULE;
	result = cdev_add (&scull_cdev, dev, 1);
	/* Fail gracefully if need be */
	if (result) {
		printk(KERN_NOTICE "Error %d adding scull character device", result);
		goto fail;
	}

	return 0; /* succeed */

  fail:
	scull_cleanup_module();
	return result;
}

module_init(scull_init_module);
module_exit(scull_cleanup_module);
