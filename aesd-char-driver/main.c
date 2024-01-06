/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>	/* printk() */
#include <linux/slab.h>		/* kmalloc() */
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> 		// file_operations
#include <linux/errno.h>	/* error codes */
#include "aesdchar.h"


#define USE_FIXED_MUTEX

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("galazwoj"); 
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

// 2
// set circular buffer to empty removing all allocated data
int aesd_trim(struct aesd_circular_buffer *buffer)
{
	uint8_t index;
	struct aesd_buffer_entry *entry;
    	PDEBUG("aesd_trim entry");

// free all mewmory
	AESD_CIRCULAR_BUFFER_FOREACH(entry, buffer, index) {
		entry->size = 0;		
		kfree(entry->buffptr);
		entry->buffptr = NULL;  
	}

	buffer->in_offs = 0;	
	buffer->out_offs = 0;	
	buffer->full = false;

    	PDEBUG("aesd_trim exit");
	return 0;
}

// 2
int aesd_open(struct inode *inode, struct file *filp)
{
        ssize_t retval = 0;
	struct aesd_dev *dev; /* device information */
    	PDEBUG("aesd_open entry");
	dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
	filp->private_data = dev; /* for other methods */

	/* now trim to 0 the length of the device if open was write-only */
	if ( (filp->f_flags & O_ACCMODE) == O_WRONLY) {
		if (mutex_lock_interruptible(&dev->lock))
			retval = -ERESTARTSYS;
		else {
			aesd_trim(dev->data); /* ignore errors */
			mutex_unlock(&dev->lock);
		}
	}
    	PDEBUG("aesd_open exit, result (%ld)", retval);
    	return retval;
}

// 2
int aesd_release(struct inode *inode, struct file *filp)
{
    	PDEBUG("aesd_release entry");
    	PDEBUG("aesd_release exit");
    	return 0;
}

// 2
// actually this function is sort of misleading because in a true circular buffer scenario 
// once an entry is read it is then freed which doesn't happen in this implementation
//
ssize_t aesd_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
        ssize_t retval = 0;
    	struct aesd_dev *dev = filp->private_data;
    	size_t entry_offset;
    	struct aesd_buffer_entry *entry;
	size_t nbytes;
    	PDEBUG("aesd_read entry, (%zu) bytes with offset (%lld)", count, *f_pos);

    	if (!count) 
        	goto out_nomutex;   

	if (mutex_lock_interruptible(&dev->lock)) {
		retval = -ERESTARTSYS;
        	PDEBUG("failed to lock mutex");
		goto out_nomutex;
	}

// read past file 
	if (*f_pos >= dev->size)
		goto out_mutex;

// read too much data
	if (*f_pos + count > dev->size)
		count = dev->size - *f_pos;

// read data
    	entry = aesd_circular_buffer_find_entry_offset_for_fpos(dev->data, *f_pos, &entry_offset);

// no data to read
    	if (!entry || !entry->buffptr) 
		goto out_mutex;

// correct count to read   
        nbytes = entry->size - entry_offset;
	if (nbytes <0) {
        	PDEBUG("unexpected nbytes to copy (%zu) (%zu)",entry->size, entry_offset);
		goto out_mutex;
	}
	if (count > nbytes)
	    	count = nbytes;

// copy to user	
        if (copy_to_user(buf, entry->buffptr + entry_offset, count)) {
        	PDEBUG("failed to copy to the user buffer");
            	retval = -EFAULT;   // Error while copying data to user space
		goto out_mutex;
        } 

	*f_pos += count;    	// Update file position
	retval = count;   	// Set return value to the actual number of bytes read

out_mutex:
	mutex_unlock(&dev->lock);

out_nomutex:
       	PDEBUG("aesd_read exit, result: (%ld)", retval);
	return retval;
}

// 2
ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    	ssize_t retval = 0;
    	struct aesd_dev *dev = filp->private_data;
        size_t old_entry_size;
	char *tmp_buf;
    	PDEBUG("aesd_write entry (%zu) bytes with offset (%lld)",count,*f_pos);    

// nothing to write     
    	if (!count) 
        	goto out_nonmutex;   

    	if (mutex_lock_interruptible(&dev->lock)) {
        	retval = -ERESTARTSYS;
        	PDEBUG("failed to lock mutex");
        	goto out_nonmutex;
    	}

// allocate entry    
	tmp_buf = krealloc(dev->entry.buffptr, dev->entry.size + count, GFP_KERNEL); 
    	if (!tmp_buf){
		retval = -ENOMEM;
        	PDEBUG("failed to allocate memory for the buffer");
        	goto out_mutex;
    	}
	else 
		dev->entry.buffptr = tmp_buf;

// copy data from user space to kernel space.
    	if (copy_from_user((void *)(dev->entry.buffptr + dev->entry.size), buf, count)) {
        	retval = -EFAULT;
// shrink if error
		dev->entry.buffptr = krealloc(dev->entry.buffptr, dev->entry.size , GFP_KERNEL); 		
        	PDEBUG("failed to copy from the user buffer");
        	goto out_mutex;
    	}
	dev->entry.size += count;	

// write to circular buffer if \n present at the end of string
   	if (dev->entry.buffptr[dev->entry.size-1] == '\n') {
	    	if (!aesd_circular_buffer_add_entry_ext(dev->data, &dev->entry, &old_entry_size)) {
			retval = -ENOMEM;
// shrink if error
			dev->entry.size -= count;
			dev->entry.buffptr = krealloc(dev->entry.buffptr, dev->entry.size , GFP_KERNEL); 		
	        	PDEBUG("failed to write data to the circular buffer");
	        	goto out_mutex;
	    	}

// update size
	        dev->size += dev->entry.size;    
	        dev->size -= old_entry_size;    
// free entry
        	dev->entry.size = 0;
        	dev->entry.buffptr = krealloc(dev->entry.buffptr, dev->entry.size, GFP_KERNEL); 
	}
    	*f_pos += count;
    	retval = count;
  
out_mutex:
    	mutex_unlock(&dev->lock);

out_nonmutex:
       	PDEBUG("aesd_write exit, result: (%ld)", retval);
    	return retval;
}

// 2
loff_t aesd_llseek(struct file *filp, loff_t off, int whence)
{
	struct aesd_dev *dev = filp->private_data;
    	loff_t newpos;
    	PDEBUG("aesd_llseek entry, pos (%lld), action (%d)", off, whence);
    	if (mutex_lock_interruptible(&dev->lock)) {
        	newpos = -ERESTARTSYS;
        	PDEBUG("failed to lock mutex");
        	goto out_nomutex;
    	}
#ifndef  USE_FIXED_MUTEX
	switch(whence) {
	  case 0: /* SEEK_SET */
		newpos = off;
		break;

	  case 1: /* SEEK_CUR */
		newpos = filp->f_pos + off;
		break;

	  case 2: /* SEEK_END */
		newpos = dev->size + off;
		break;

	  default: /* can't happen */
		newpos = -EINVAL;
		goto out_mutex;

	}
	if (newpos < 0) return -EINVAL;
	filp->f_pos = newpos;
out_mutex:
#else
// generic llseek
    	newpos = fixed_size_llseek(filp, off, whence, dev->size);
#endif

    	mutex_unlock(&dev->lock);
out_nomutex:

       	PDEBUG("aesd_llseek exit, result: (%lld)", newpos);
    	return newpos;
}

// done
struct file_operations aesd_fops = {
    	.owner =    THIS_MODULE,
     	.read =     aesd_read,
    	.write =    aesd_write,
    	.open =     aesd_open,
	.llseek =   aesd_llseek,	
    	.release =  aesd_release,
};

// done
static int aesd_setup_cdev(struct aesd_dev *dev)
{
    	int err, devno = MKDEV(aesd_major, aesd_minor);
       	PDEBUG("aesd_setup_cdev entry");
    	cdev_init(&dev->cdev, &aesd_fops);
    	dev->cdev.owner = THIS_MODULE;
    	dev->cdev.ops = &aesd_fops;
    	err = cdev_add (&dev->cdev, devno, 1);
    	if (err) {
        	printk(KERN_ERR "Error %d adding aesd cdev", err);
    	}
       	PDEBUG("aesd_setup_cdev exit, result (%d)", err);
    	return err;
}

// 2
int aesd_init_module(void)
{
    	dev_t dev = 0;
    	int result;
       	PDEBUG("aesd_init_module entry");
    	result = alloc_chrdev_region(&dev, aesd_minor, 1,"aesdchar");
    	aesd_major = MAJOR(dev);
    	if (result < 0) {
        	printk(KERN_WARNING "Can't get major devnum %d\n", aesd_major);
        	return result;
    	}
    	memset(&aesd_device, 0, sizeof(struct aesd_dev));

	aesd_device.data = kmalloc(sizeof(struct aesd_circular_buffer), GFP_KERNEL);
	if (!aesd_device.data) {
		result = -ENOMEM;
     		unregister_chrdev_region(dev, 1);
		return result;
	}
	aesd_circular_buffer_init(aesd_device.data);
	aesd_device.size = 0;		            
	aesd_device.entry.size = 0;
	aesd_device.entry.buffptr = NULL; 
                  
 	mutex_init(&aesd_device.lock);

    	result = aesd_setup_cdev(&aesd_device);
    	if( result ) {
        	unregister_chrdev_region(dev, 1);
   	}
       	PDEBUG("aesd_init_module exit, result (%d)", result);
    	return result;
}

// 2
void aesd_cleanup_module(void)
{
    	dev_t devno = MKDEV(aesd_major, aesd_minor);
       	PDEBUG("aesd_cleanup_module entry");
	aesd_trim(aesd_device.data);

    	cdev_del(&aesd_device.cdev);
	kfree(aesd_device.data);
    	mutex_destroy(&aesd_device.lock);

     	unregister_chrdev_region(devno, 1);
       	PDEBUG("aesd_cleanup_module exit");
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);

// czy tworzenie circular buffer w init czy w open? - chyba przy open aby kazdy mial 0 
// czy usuwanie circular bufer w release czy w exit
