/* Prototype module for second mandatory DM510 assignment */
#ifndef __KERNEL__
#  define __KERNEL__
#endif
#ifndef MODULE
#  define MODULE
#endif

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>	
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/cdev.h>
/* #include <asm/uaccess.h> */
#include <linux/uaccess.h>
#include <linux/semaphore.h>
/* #include <asm/system.h> */
#include <asm/switch_to.h>

#include <linux/moduleparam.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/proc_fs.h>
#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/seq_file.h>
#include <linux/kdev_t.h>




#include <linux/ioctl.h> /* needed for the _IOW etc stuff used later */




#include "dm510_dev.h"
/* Prototypes - this would normally go in a .h file */

/* end of what really should have been in a .h file */

int dm_maj = MAJOR_NUMBER;
int dm_min = MIN_MINOR_NUMBER;
char* dm_name = DEVICE_NAME;
int dm_nr_devs = DEVICE_COUNT;
int dm_quantum = DM_QUANTUM;
int dm_qset =    DM_QSET;
int dm_buffersize = DM_BUFFER;

struct dm_dev *dm_devices;	/* allocated in dm510_init_module */

struct dm_buffer *dm_buffer0;	/* allocated in dm510_init_module */
struct dm_buffer *dm_buffer1;	/* allocated in dm510_init_module */


/* file operations struct */
static struct file_operations dm510_fops = {
	.owner   = THIS_MODULE,
	.read    = dm510_read,
	.write   = dm510_write,
	.open    = dm510_open,
	.release = dm510_release,
    .unlocked_ioctl   = dm510_ioctl,
	.fasync =	dm_fasync,
	.llseek =	no_llseek,
};


/* Methods returning the appropriate buffer, or end of buffer, depending on which device requests acces. */
/*
// Get the index, to look up buffer
static struct dm_buffer* get_Rbuffer(int minor){
	if(minor == 0){
		return dm_buffers;
	}
	return dm_buffers;
}

// Get the buffer to write to, dev0->buff1, dev1->buff0
static struct dm_buffer* get_Wbuffer(int minor){
	if(minor == 0){
		return dm_buffers;
	}
	return dm_buffers;
}
*/


// pipe done, however, need to test, when write is done,
// as it waits for data to read
/* Called when a process, which already opened the dev file, attempts to read from it. */
static ssize_t dm510_read(
	struct file	*filp,
    char 		*buf,      /* The buffer to fill with data     */
    size_t 		count,   /* The max number of bytes to read  */
    loff_t 		*f_pos  /* The offset in the file           */
	){  
	//printk(KERN_INFO "Getting mutex.\n");
	/* read code belongs here */
	// device pointer to pirvate_data of file pointer
	struct dm_dev *dev = filp->private_data;
	//printk(KERN_INFO "asd.\n");
	struct dm_buffer *dm_buf; 

	// int x = dev->minor;

	if(dev->minor == 0){
		dm_buf = dev->buffer0;
	}		
	else{
		dm_buf = dev->buffer1;
	}
	
	// if cannot get mutex of the read buffer, return error, otherwise it will wait, until buf is available.
	if (mutex_lock_interruptible(&dm_buf->mutex)){
		printk(KERN_INFO "entered IF-ERROR - buffer mutex.\n");	
		return -ERESTARTSYS;}

// 	printk(KERN_INFO "Start wait for ReadPointer to not equal WritePointer.\n");
	/*
	 * waits for read pointer to not be write pointer
	 * then takes mutex back.
	 * Enters loop when read pointer, rp, is equal to write point, wp
	 * loop waits for rp != wp, before looping. 
	 * waiting and mutex lock getting is interruptible
	 * */
	while (dm_buf->rp == dm_buf->wp) { /* nothing to read */
		mutex_unlock(&dm_buf->mutex); /* release the lock */
		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;
		// printk(KERN_INFO "\"%s\" reading: going to sleep\n", current->comm);
		// wait_event... evaluates condition, 2nd arg, until it's true.
		// then it returns 0, which is understood as null in c
		// 0 is understood as null, and it won't enter if-state,
		// when 0 returned
		// Need to wait until size of buff is more than 0cd 

		if (wait_event_interruptible(dm_buf->inq, (dm_buf->rp != dm_buf->wp)))
			return -ERESTARTSYS; /* signal: tell the fs layer to handle it */
		/* otherwise loop, but first reacquire the lock */
		// mutex lock returns 0 when lock is gotten
		if (mutex_lock_interruptible(&dm_buf->mutex))
			return -ERESTARTSYS;
	}
	
	/* ok, data is there, return something */
	if (dm_buf->wp > dm_buf->rp)
		// Pointers point to data in same sequence,
		// so difference in number, is difference in bytes
		count = min(count, (size_t)(dm_buf->wp - dm_buf->rp));
	else /* the write pointer has wrapped, return data up to dev->end */
		count = min(count, (size_t)(dm_buf->end - dm_buf->rp));	
	// Copy everything from read* to end or wp to user.
	// returns 0 on success, meaning it won't enter if-statement
	// NEW addition

	// memcpy(tmpbuf, dm_buf->rp, CopyCount);


	if (copy_to_user(buf, dm_buf->rp, count)) {
		mutex_unlock (&dm_buf->mutex);
		return -EFAULT;
	}	
	// move the read pointer
	dm_buf->rp += count;

	// need to wrap read pointer, if at end
	if (dm_buf->rp == dm_buf->end)
		dm_buf->rp = dm_buf->buffer; /* wrapped */
	mutex_unlock (&dm_buf->mutex);
	// printk(KERN_INFO"Waking up writes \n");
	/* finally, awake any writers and return */
	wake_up_interruptible(&dm_buf->outq);
	wake_up_interruptible(&dev->outq);
// 	printk(KERN_INFO "\"%s\" did read %li bytes\n",current->comm, (long)count);
	return count; //return number of bytes read
}


/* How much space is free?
 * I subtract one, as otherwise a full buffer would have wp and rp at same address,
 * and them not being at same address is used to check if there has been written anything not read to the buffer
 * */
static int spacefree(struct dm_buffer *buf)
{
	if (buf->rp == buf->wp)
		return dm_buffersize - 1;
	return ((buf->rp + dm_buffersize - buf->wp) % dm_buffersize) - 1;
}


/* Wait for space for writing; caller must hold device semaphore.  On
 * error the semaphore will be released before returning. */
static int dm_getwritespace(struct dm_buffer *buf, struct dm_dev *dev, struct file *filp)
{

	while (spacefree(buf) == 0) { /* full */
		DEFINE_WAIT(wait);
		//mutex_unlock(&dev->mutex);
		mutex_unlock(&buf->mutex);
		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;
		// printk(KERN_INFO "prepare to wait for buf outq \n");
		prepare_to_wait(&buf->outq, &wait, TASK_INTERRUPTIBLE);
		// printk(KERN_INFO "prepare to wait for dev outq \n");
		//prepare_to_wait(&dev->outq, &wait, TASK_INTERRUPTIBLE);
		if (spacefree(buf) == 0)
			schedule();
		// printk(KERN_INFO "Stopped waiting and now confirming\n");
		finish_wait(&buf->outq, &wait);
		// printk(KERN_INFO "Finished wait for buf outq \n");
		// finish_wait(&dev->outq, &wait);
		//printk(KERN_INFO "Finished wait for dev outq \n");
		if (signal_pending(current)) // !!! I'm not sure what current refers to, need to ask questions
			return -ERESTARTSYS; /* signal: tell the fs layer to handle it */
		if (mutex_lock_interruptible(&buf->mutex))
			return -ERESTARTSYS;
		// if (mutex_lock_interruptible(&dev->mutex))
			//return -ERESTARTSYS;
	}
	return 0;
}	



// missing
/* Called when a process writes to dev file */
static ssize_t dm510_write( struct file*filp,
    						const char*	buf,/* The buffer to get data from      */
    						size_t 		count,   /* The max number of bytes to write */
    						loff_t*		f_pos ){  /* The offset in the file           */
	
	
	/* write code belongs here */	
	struct dm_dev *dev = filp->private_data;
	struct dm_buffer *dm_buf;
	int result;
	
	// This works! I now know which device is writing
	if(dev->minor == 0){
		dm_buf = dev->buffer1;
		result = 1;
	}		
	else{
		dm_buf = dev->buffer0;
		result = 0;
	}
	
	if (mutex_lock_interruptible(&dev->mutex))
		return -ERESTARTSYS;
	if (mutex_lock_interruptible(&dm_buf->mutex))
		return -ERESTARTSYS;
	
	
	
	/* Make sure there's space to write */
	
	result = dm_getwritespace(dm_buf, dev, filp);
	if (result)
		return result; /* scull_getwritespace called mutex_unlock(&dev->mutex) */
	/* ok, space is there, accept something */
	// I change count here, since i now need to know just how much space is left, after writing	
	count = min(count, (size_t)spacefree(dm_buf));
	if (dm_buf->wp >= dm_buf->rp) 
		count = min(count, (size_t)(dm_buf->end - dm_buf->wp)); /* to end-of-buf */
	else /* the write pointer has wrapped, fill up to rp-1 */
		count = min(count, (size_t)(dm_buf->rp - dm_buf->wp - 1));
	// printk("Going to accept %li bytes to %p from %p\n", (long)count, dev->wp, buf);
	
	// This is where i write to a buffer. 
	if (copy_from_user(dm_buf->wp, buf, count)) {
		mutex_unlock (&dm_buf->mutex);
		mutex_unlock (&dev->mutex);
		return -EFAULT;
	}
	

	dm_buf->wp += count;

	if (dm_buf->wp == dm_buf->end)
		dm_buf->wp = dm_buf->buffer; /* wrapped */
			

	// Since wp has now wrapped, there is still space left, between wp and rp.
	mutex_unlock(&dm_buf->mutex);
	mutex_unlock(&dev->mutex);

	/* finally, awake any reader */
	wake_up_interruptible(&dm_buf->inq);  /* blocked in read() and select() */
	/* and signal asynchronous readers, explained late in chapter 5 */
	if (dev->async_queue)
		kill_fasync(&dev->async_queue, SIGIO, POLL_IN);

	return count; //return number of bytes written
}

void dm510_cleanup_module( void ) {
	/* Called when module is unloaded */
	int i;
	dev_t devno = MKDEV(dm_maj, dm_min);

	if(dm_buffer0)
	{
		kfree(dm_buffer0->buffer);	
		kfree(dm_buffer1->buffer);	
		kfree(dm_buffer0);	
		kfree(dm_buffer1);	
		printk(KERN_INFO "Freed memory for dm_buffers.\n");
	}
	/* Get rid of our char dev entries */
	if (dm_devices) {
		for (i = 0; i < dm_nr_devs; i++) {
			cdev_del(&dm_devices[i].cdev);
			printk(KERN_INFO "cleaning device  %d.\n", i);
		}
		kfree(dm_devices);
		printk(KERN_INFO "Freed memory for device pointer.\n");
	}


	/* _module is never called if registering failed */
	unregister_chrdev_region(devno, dm_nr_devs);

	printk(KERN_INFO "DM510: Module unloaded.\n");
}



// pipe done
static void dm_setup_cdev(struct dm_dev *dev, int index){
	// used in init_module
	int err, devno = MKDEV(dm_maj, dm_min + index);
    
	cdev_init(&dev->cdev, &dm510_fops);
	dev->cdev.owner = THIS_MODULE; // THIS_MODULE must be a macro that works within the kernel, at runtime or something.
								   // Can't find a declaration anywhere, perhaps from a previous included file
	dev->cdev.ops = &dm510_fops; // not sure this is needed, as it's in the cdev init as well
	//!!!
	err = cdev_add (&dev->cdev, devno, 1);
	// printk(KERN_INFO "Set up char device %d.\n", dm_min + index);
	
	/* Fail gracefully if need be */
	if (err)
		printk(KERN_NOTICE "ERROR %d adding dm510%d", err, index);
}
/*
static void dm_setup_buffer(struct dm_buffer *buf){

}
*/
// pipe done
int dm510_init_module( void ) {
	/* called when module is loaded */	
	int result, i;
	dev_t dev = 0;

	if (dm_maj) {
		//printk(KERN_INFO "Registering major.\n");
		dev = MKDEV(dm_maj, dm_min); // Mkes a dev_t struct. dev_t struct contains minor and major numbers for the device.
		// Using MAX_MINOR_NUMBER, as i'm not sure of the consequences and i know i can have more than one. Also two devices need to talk with eachother
		
		result = register_chrdev_region(dev, dm_nr_devs, dm_name); // Registers device numbers, or gives them to me.
	} else {
		//printk(KERN_INFO "Retrieving dynamic major.\n");
		// If no major number exists, allocate a new number, not sure if necessary when i'm given a constant major numbers
		result = alloc_chrdev_region(&dev, dm_maj, dm_nr_devs, dm_name);
		dm_maj = MAJOR(dev);
	}
	if (result < 0) {
		printk(KERN_WARNING "DM: can't get major %d\n", dm_maj);
		return result;
	}
	printk(KERN_INFO "Succesfully registered device.\n");
	
	
	/* 
	 * allocate the devices -- we can't have them static, as the number
	 * can be specified at load time
	 */

	dm_devices = kmalloc(dm_nr_devs * sizeof(struct dm_dev), GFP_KERNEL);
	if (dm_devices == NULL) {
		printk(KERN_INFO "No Devices or allocation failed.\n");
		// If no given devices, goto fail and run cleanup
		goto fail;
	}
	dm_buffer0 = kmalloc(sizeof(struct dm_buffer), GFP_KERNEL);
	if (dm_buffer0 == NULL) {
		printk(KERN_INFO "allocation of buffers failed.\n");
		// If allocation failed, goto fail and run cleanup
		goto fail;
	}
	dm_buffer0->buffer = kmalloc(dm_buffersize*sizeof(char), GFP_KERNEL);
	if (dm_buffer0->buffer == NULL) {
		printk(KERN_INFO "allocation of buffers failed.\n");
		// If allocation failed, goto fail and run cleanup
		goto fail;
	}
	
	dm_buffer1 = kmalloc(sizeof(struct dm_buffer), GFP_KERNEL);
	if (dm_buffer1 == NULL) {
		printk(KERN_INFO "allocation of buffers failed.\n");
		// If allocation failed, goto fail and run cleanup
		goto fail;
	}

	dm_buffer1->buffer = kmalloc(dm_buffersize*sizeof(char), GFP_KERNEL);
	if (dm_buffer1->buffer == NULL) {
		printk(KERN_INFO "allocation of buffers failed.\n");
		// If allocation failed, goto fail and run cleanup
		goto fail;
	}

// I need a much cleaner way of initializing the two buffers
// honestly, fuck it. It works.

	// initialize end
	dm_buffer0->end = dm_buffer0->buffer + dm_buffersize;
	//!!! read up on this method
	init_waitqueue_head(&(dm_buffer0->inq));
	init_waitqueue_head(&(dm_buffer0->outq));
	mutex_init(&dm_buffer0->mutex);
	// set the write pointer to the corresponding buffer
	dm_buffer0->rp = dm_buffer0->buffer;
	dm_buffer0->wp = dm_buffer0->buffer;

	// initialize end
	dm_buffer1->end = dm_buffer1->buffer + dm_buffersize;
	//!!! read up on this method
	init_waitqueue_head(&(dm_buffer1->inq));
	init_waitqueue_head(&(dm_buffer1->outq));
	mutex_init(&dm_buffer1->mutex);
	// set the write pointer to the corresponding buffer
	dm_buffer1->rp = dm_buffer1->buffer;
	dm_buffer1->wp = dm_buffer1->buffer;


	// resets the memory of the dm_devices array/pointer
	memset(dm_devices, 0, dm_nr_devs * sizeof(struct dm_dev));
	
        /* Initialize each device. */
	for (i = 0; i < dm_nr_devs; i++) {		
		
		printk(KERN_INFO "Initialization of device %d.\n", i);		
		// initialize mutexes
		mutex_init(&dm_devices[i].mutex);
		init_waitqueue_head(&dm_devices[i].inq);
		init_waitqueue_head(&dm_devices[i].outq);
		// note the number of each device, so it can be easily retrieved later
		dm_devices[i].minor = i;
		// setup the corresponding char device
		dm_setup_cdev(dm_devices + i, i);
		
		dm_devices[i].buffer0 = dm_buffer0;
		dm_devices[i].buffer1 = dm_buffer1;
	}
	
	printk(KERN_INFO "DM510_dev module has now been initialized.\n");		
	return 0;

	fail:
	printk(KERN_INFO "ERROR! Failed Initialization.\n");
	dm510_cleanup_module();
	return result;
}




// pipe done
/* Called when a process tries to open the device file */
static int dm510_open( struct inode *inode, struct file *filp ) {
	
	// Make a pointer to the cdev opened
	struct dm_dev *dev;
	dev = container_of(inode->i_cdev, struct dm_dev, cdev);
	filp->private_data = dev;

	// Gain Solo access, for concurrency
	if (mutex_lock_interruptible(&dev->mutex))
		return -ERESTARTSYS;

	// Increase readers and writers of dm510, maybe set to amounr of users?
	// Since it doesn't really make sense to open only as read, but idk. 

	/* use f_mode,not  f_flags: it's cleaner (fs/open.c tells why) */
	if (filp->f_mode & FMODE_READ)
		dev->nreaders++;
	if (filp->f_mode & FMODE_WRITE)
		dev->nwriters++;

	// printk(KERN_INFO "dm510_open done without crash.\n");
	mutex_unlock(&dev->mutex);

	return nonseekable_open(inode, filp);
}

// pipe done
/* Used for asynchronous notification
 * */
static int dm_fasync(int fd, struct file *filp, int mode){
	struct dm_dev *dev = filp->private_data;

	return fasync_helper(fd, filp, mode, &dev->async_queue);
}

// pipe done
/* Called when a process closes the device file.
* Releases it's grip from the file
* */
static int dm510_release( struct inode *inode, struct file *filp ) {
	

	/* device release code belongs here */
	struct dm_dev *dev = filp->private_data;


	/* remove this filp from the asynchronously notified filp's */
	dm_fasync(-1, filp, 0);
	mutex_lock(&dev->mutex);
	if (filp->f_mode & FMODE_READ)
		dev->nreaders--;
	if (filp->f_mode & FMODE_WRITE)
		dev->nwriters--;
	mutex_unlock(&dev->mutex);
	return 0;
}

// missing
/* called by system call icotl */ 
long dm510_ioctl( 
    struct file *filp, 
    unsigned int cmd,   /* command passed from the user */
    unsigned long arg ) /* argument of the command */
{
	/* ioctl code belongs here */
	printk(KERN_INFO "DM510: ioctl called.\n");

	return 0; //has to be changed
}

module_init( dm510_init_module );
module_exit( dm510_cleanup_module );

MODULE_AUTHOR( "...Mads Mikael Keinicke" );
MODULE_LICENSE( "GPL" );

