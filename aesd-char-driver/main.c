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
 * differences compared to ther kernel described in the assignment
 * 	uses lseek()
 * 	allocates and frees entries in the circural buffer differently to the method advised in the video
 *     
 */

#include "aesdchar.h"

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("galazwoj"); 
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

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

int aesd_open(struct inode *inode, struct file *filp)
{
        ssize_t retval = 0;
	struct aesd_dev *dev; /* device information */
    	PDEBUG("aesd_open entry");
	dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
	filp->private_data = dev; /* for other methods */

    	PDEBUG("aesd_open exit, result (%ld)", retval);
    	return retval;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    	PDEBUG("aesd_release entry");
    	PDEBUG("aesd_release exit");
    	return 0;
}

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

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    	ssize_t retval = 0;
    	struct aesd_dev *dev = filp->private_data;
        size_t old_entry_size;
	char *tmp_buf;
    	PDEBUG("aesd_write entry, (%zu) bytes with offset (%lld)",count,*f_pos);    

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

// generic llseek oneliner
// https://lkml.iu.edu/hypermail/linux/kernel/1506.1/04776.html
// https://elixir.bootlin.com/linux/v5.10.204/source/fs/read_write.c#L154
    	newpos = fixed_size_llseek(filp, off, whence, dev->size);
    	mutex_unlock(&dev->lock);

out_nomutex:
       	PDEBUG("aesd_llseek exit, result: (%lld)", newpos);
    	return newpos;
}

/**
 * Adjust the file offset (f_pos) parameter of @param filp based on the location specified by
 * @param write_cmd (the zero referenced command to locate)
 * and @param write_cmd_offset (the zero referenced offset into the command)
 * @return 0 if successful, negative if error occured:
 * 	-ERESTARTSYS if mutex could not be obtained
 * 	-EINVAL if write command or write_cmd_offset was out of range
 */
static long aesd_adjust_file_offset(struct file *filp, unsigned int write_cmd, size_t write_cmd_offset)
{
	struct aesd_dev *dev = filp->private_data;
	long retval = 0;
	int id_entry; 

	PDEBUG("aesd_adjust_file_offset entry, command: (%d) offset: (%zu)", write_cmd, write_cmd_offset);    

	if (mutex_lock_interruptible(&dev->lock)) {
		retval = -ERESTARTSYS;
		PDEBUG("failed to lock mutex");
		goto out_nonmutex;
	}

// check for index into circular buffer and for write_cmd_offset in entry size
	if (write_cmd >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED || write_cmd_offset >= dev->data->entry[write_cmd].size) {
		retval = -EINVAL;
		goto out_mutex;
	}
	
// compute offset 
	for (id_entry = 0; id_entry < write_cmd; id_entry++) 
		filp->f_pos += dev->data->entry[id_entry].size;
	filp->f_pos += write_cmd_offset;

out_mutex:
	mutex_unlock(&dev->lock);

out_nonmutex:
	PDEBUG("aesd_adjust_file_offset exit, result: (%ld)", retval);
	return retval;
}

long aesd_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	long retval;
	PDEBUG("aesd_unlocked_ioctl entry, command: (%d)", cmd);

// check parameters 
	if (_IOC_TYPE(cmd) != AESD_IOC_MAGIC || _IOC_NR(cmd) > AESDCHAR_IOC_MAXNR) {
		retval = -EINVAL;
		goto out; 
	}

// check if command valid 
	switch (cmd) {
		case AESDCHAR_IOCSEEKTO: {
// copy data
			struct aesd_seekto seek_to;
			if (copy_from_user(&seek_to, (const void __user *)arg, sizeof(struct aesd_seekto))) 
				retval = -EFAULT;
			else	
// perform action
				retval = aesd_adjust_file_offset(filp, seek_to.write_cmd, seek_to.write_cmd_offset);
			break;
		}
		default:
			retval = -ENOTTY;
	}

out:
	PDEBUG("aesd_unlocked_ioctl exit, result: (%ld)", retval);
	return retval;
}

struct file_operations aesd_fops = {
    	.owner =    THIS_MODULE,
     	.read =     aesd_read,
    	.write =    aesd_write,
    	.open =     aesd_open,
	.llseek =   aesd_llseek,	
    	.release =  aesd_release,
	.unlocked_ioctl = aesd_unlocked_ioctl,
};

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
