/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

#include "aesd-circular-buffer.h"

/**
 * @param buffer the buffer to search for corresponding offset.  Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset.  This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(struct aesd_circular_buffer *buffer,
            size_t char_offset, size_t *entry_offset_byte_rtn )
{
    /**
    * TODO: implement per description
    */

 	uint8_t index;
	struct aesd_buffer_entry *entry;
	int n, nn;

// bad parameters 
	if (!entry_offset_byte_rtn || !buffer) 
		return NULL;

// inconsisteny cases (should not have happened)
	if (!buffer->full && buffer->out_offs >= buffer->in_offs) 
		return NULL;
	if (buffer->full && buffer->out_offs < buffer->in_offs) 
		return NULL;

// assign to a signed variable to make C compiler happy
	n = char_offset;

// handle case when buffer is not full
	if (!buffer->full) {
		AESD_CIRCULAR_BUFFER_FOREACH(entry,buffer,index) {
// already read, skip
			if (index < buffer->out_offs)
				continue;
// rerad past
			if (index == buffer->in_offs)
				return NULL;				
			nn = n - entry->size;
			if (nn < 0) 
				goto found;				
			n = nn;
		}

	} else {	// if (buffer->full) {
// part 1, read starting from out_offs
		AESD_CIRCULAR_BUFFER_FOREACH(entry,buffer,index) {
// this will be read in part 2
			if (index < buffer->out_offs) 
				continue;			
			if (index < buffer->in_offs) 
				continue;
			nn = n - entry->size;
			if (nn < 0) 
				goto found;
			n = nn;
		}
// part 2, read from 0 to out_offs		
		AESD_CIRCULAR_BUFFER_FOREACH(entry,buffer,index) {
// not found
			if (index == buffer->out_offs)
				return NULL;
// not found
			if (index == buffer->in_offs)
				return NULL;				
			nn = n - entry->size;
			if (nn < 0) 
				goto found;
			n = nn;
		}
	} 
	return NULL;	

found:
	if (n > entry->size)
		return NULL;				
	*entry_offset_byte_rtn = n;
	return entry;
}

/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
* @return 1 if success, 0 on failure
* @sold_entry_size size of entry freed
*/

int aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry, size_t *old_entry_size)
{
    /**
    * TODO: implement per description
    */  
	int bufsize;
	char *p;
	struct aesd_buffer_entry *entry;

// bad parameters
	if (!buffer || !add_entry)
		return 0;

// allocate new entry
	bufsize = add_entry->size+1;
	if (old_entry_size)
	        *old_entry_size = 0;

#ifdef __KERNEL__
	if (!(p = kmalloc(bufsize, GFP_KERNEL))) 
#else
	if (!(p = malloc(bufsize))) 
#endif
		return 0;
	strncpy(p, add_entry->buffptr, bufsize);				

//remove previously used entry
	entry = &buffer->entry[buffer->in_offs];
	if (buffer->full) {
#ifdef __KERNEL__
		kfree((void*)entry->buffptr);
#else
		free((void*)entry->buffptr);
#endif
		if (old_entry_size)
		        *old_entry_size = entry->size;
		entry->size = 0;		
	}

// insert new entry
	entry->buffptr = p;
	entry->size = add_entry->size;

// advance out_offs ptr if buffer full and handle the limit case
	if (buffer->full) {
		if (buffer->out_offs == buffer->in_offs) 
			buffer->out_offs++;		
		if (buffer->out_offs == AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) 
			buffer->out_offs = 0;
	}

// mark buffer full in in_offs reaches its limit
	if (++buffer->in_offs == AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) {
		buffer->full = true;
		buffer->in_offs = 0;
	}	
	return 1;
}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}
