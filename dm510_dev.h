/*
 * scull.h -- definitions for the char module
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
 * $Id: scull.h,v 1.15 2004/11/04 17:51:18 rubini Exp $
 */



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


#ifndef _dm510_dev_H_
#define _dm510_dev_H_

#include <linux/ioctl.h> /* needed for the _IOW etc stuff used later */

#define DEVICE_NAME "dm510_dev" /* Dev name as it appears in /proc/devices */
#define MAJOR_NUMBER 254
#define MIN_MINOR_NUMBER 0
#define MAX_MINOR_NUMBER 1

#define DEVICE_COUNT 2
#ifndef DM_QUANTUM
#define DM_QUANTUM 4000
#endif

#ifndef DM_QSET
#define DM_QSET    1000
#endif

/*
 * The pipe device is a simple circular buffer. Here its default size
 */
#ifndef DM_BUFFER
#define DM_BUFFER 4000
#endif


/*
 * Representation of scull quantum sets.
 */
struct dm_qset {
	void **data;
	struct dm_qset *next;
};

struct dm_buffer{
     char *rp, *wp;                     /* where to read, where to write */
     char *buffer, *end;                   /* buffer and end of buffer, pointer */
     int size;                             /* Amount of chars written to the buffer */
     struct mutex mutex;                   /* mutex for reading and writing from the buffer*/
     wait_queue_head_t inq, outq;       /* read and write queues */  // copied from dm_dev, as i most likely will end up needing these
};

struct dm_dev {
     wait_queue_head_t inq, outq;       /* read and write queues */  // copied from dm_dev, as i most likely will end up needing these
     int minor;
     struct dm_buffer *buffer0;            
     struct dm_buffer *buffer1;            
     int nreaders, nwriters;            /* number of openings for r/w */
     struct fasync_struct *async_queue; /* asynchronous readers */ 
     struct mutex mutex;              /* mutual exclusion semaphore */
     struct cdev cdev;                  /* Char device structure */
};



/*
 * Macros to help debugging
 */
/*
#undef PDEBUG             // undef it, just in case /
#ifdef SCULL_DEBUG
#  ifdef __KERNEL__
     // This one if debugging is on, and kernel space /
#    define PDEBUG(fmt, args...) printk( KERN_DEBUG "scull: " fmt, ## args)
#  else
     // This one for user space /
#    define PDEBUG(fmt, args...) fprintf(stderr, fmt, ## args)
#  endif
#else
#  define PDEBUG(fmt, args...) // not debugging: nothing /
#endif

#undef PDEBUGG
#define PDEBUGG(fmt, args...) // nothing: it's a placeholder /

#ifndef SCULL_MAJOR
#define SCULL_MAJOR 0   // dynamic major by default /
#endif

#ifndef SCULL_NR_DEVS
#define SCULL_NR_DEVS 4    // scull0 through scull3 /
#endif

#ifndef SCULL_P_NR_DEVS
#define SCULL_P_NR_DEVS 4  // scullpipe0 through scullpipe3 /
#endif
*/
/*
 * The bare device is a variable-length region of memory.
 * Use a linked list of indirect blocks.
 *
 * "scull_dev->data" points to an array of pointers, each
 * pointer refers to a memory area of SCULL_QUANTUM bytes.
 *
 * The array (quantum-set) is SCULL_QSET long.
 */


/*
 * Split minors in two parts
 */
#define TYPE(minor)	(((minor) >> 4) & 0xf)	/* high nibble */
#define NUM(minor)	((minor) & 0xf)		/* low  nibble */


/*
 * The different configurable parameters
 /
extern int scull_major;     // main.c /
extern int scull_nr_devs;
extern int scull_quantum;
extern int scull_qset;

extern int scull_p_buffer;	// pipe.c */


/*
 * Prototypes for shared functions
 */


static int dm510_open( struct inode*, struct file* );
static int dm510_release( struct inode*, struct file* );
static ssize_t dm510_read( struct file*, char*, size_t, loff_t* );
static ssize_t dm510_write( struct file*, const char*, size_t, loff_t* );
long dm510_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
static int dm_fasync(int fd, struct file *filp, int mode);
/*
// For Scull
int     scull_p_init(dev_t dev);
void    scull_p_cleanup(void);
int     scull_access_init(dev_t dev);
void    scull_access_cleanup(void);
int     scull_trim(struct scull_dev *dev);
loff_t  scull_llseek(struct file *filp, loff_t off, int whence);
*/
// -------------------- done ------------------------
/*
 * Ioctl definitions
 */

/* Use 'k' as magic number */
#define SCULL_IOC_MAGIC  'k'
/* Please use a different 8-bit number in your code */

#define SCULL_IOCRESET    _IO(SCULL_IOC_MAGIC, 0)

/*
 * S means "Set" through a ptr,
 * T means "Tell" directly with the argument value
 * G means "Get": reply by setting through a pointer
 * Q means "Query": response is on the return value
 * X means "eXchange": switch G and S atomically
 * H means "sHift": switch T and Q atomically
 */
#define SCULL_IOCSQUANTUM _IOW(SCULL_IOC_MAGIC,  1, int)
#define SCULL_IOCSQSET    _IOW(SCULL_IOC_MAGIC,  2, int)
#define SCULL_IOCTQUANTUM _IO(SCULL_IOC_MAGIC,   3)
#define SCULL_IOCTQSET    _IO(SCULL_IOC_MAGIC,   4)
#define SCULL_IOCGQUANTUM _IOR(SCULL_IOC_MAGIC,  5, int)
#define SCULL_IOCGQSET    _IOR(SCULL_IOC_MAGIC,  6, int)
#define SCULL_IOCQQUANTUM _IO(SCULL_IOC_MAGIC,   7)
#define SCULL_IOCQQSET    _IO(SCULL_IOC_MAGIC,   8)
#define SCULL_IOCXQUANTUM _IOWR(SCULL_IOC_MAGIC, 9, int)
#define SCULL_IOCXQSET    _IOWR(SCULL_IOC_MAGIC,10, int)
#define SCULL_IOCHQUANTUM _IO(SCULL_IOC_MAGIC,  11)
#define SCULL_IOCHQSET    _IO(SCULL_IOC_MAGIC,  12)

/*
 * The other entities only have "Tell" and "Query", because they're
 * not printed in the book, and there's no need to have all six.
 * (The previous stuff was only there to show different ways to do it.
 */
#define SCULL_P_IOCTSIZE _IO(SCULL_IOC_MAGIC,   13)
#define SCULL_P_IOCQSIZE _IO(SCULL_IOC_MAGIC,   14)
/* ... more to come */

#define SCULL_IOC_MAXNR 14

#endif /* _SCULL_H_ */
