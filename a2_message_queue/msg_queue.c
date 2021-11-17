/*
 * This code is provided solely for the personal and private use of students
 * taking the CSC369H course at the University of Toronto. Copying for purposes
 * other than this use is expressly prohibited. All forms of distribution of
 * this code, including but not limited to public repositories on GitHub,
 * GitLab, Bitbucket, or any other online platform, whether as given or with
 * any changes, are expressly prohibited.
 *
 * Authors: Alexey Khrabrov, Andrew Pelegris, Karen Reid
 *
 * All of the files in this directory and all subdirectories are:
 * Copyright (c) 2019, 2020 Karen Reid
 */

/**
 * CSC369 Assignment 2 - Message queue implementation.
 *
 * You may not use the pthread library directly. Instead you must use the
 * functions and types available in sync.h.
 */

#include <assert.h>
#include <errno.h>
#include <stdlib.h>

#include "errors.h"
#include "list.h"
#include "msg_queue.h"
#include "ring_buffer.h"

typedef struct waiting_node{
	cond_t is_ready;
	mutex_t poll_mutex;
	int events;
	list_entry entry;
} waiting_node;

// Message queue implementation backend
typedef struct mq_backend {
	// Ring buffer for storing the messages
	ring_buffer buffer;

	// Reference count
	size_t refs;

	// Number of handles open for reads
	size_t readers;
	// Number of handles open for writes
	size_t writers;

	// Set to true when all the reader handles have been closed. Starts false
	// when they haven't been opened yet.
	bool no_readers;
	// Set to true when all the writer handles have been closed. Starts false
	// when they haven't been opened yet.
	bool no_writers;

	//TODO: add necessary synchronization primitives, as well as data structures
	//      needed to implement the msg_queue_poll() functionality
	mutex_t mutex;
	cond_t space_full;
	cond_t space_empty;
	int queue_event;//current event
	list_head queue_list_head;

} mq_backend;


static int mq_init(mq_backend *mq, size_t capacity)
{
	if (ring_buffer_init(&mq->buffer, capacity) < 0) {
		return -1;
	}

	mq->refs = 0;

	mq->readers = 0;
	mq->writers = 0;

	mq->no_readers = false;
	mq->no_writers = false;

	//TODO: initialize remaining fields (synchronization primitives, etc.)
	mutex_init(&mq->mutex);
	cond_init(&mq->space_empty);
	cond_init(&mq->space_full);
	list_init(&mq->queue_list_head);
	return 0;
}

static void mq_destroy(mq_backend *mq)
{
	assert(mq->refs == 0);
	assert(mq->readers == 0);
	assert(mq->writers == 0);

	ring_buffer_destroy(&mq->buffer);

	//TODO: cleanup remaining fields (synchronization primitives, etc.)
	mutex_destroy(&mq->mutex);
	cond_destroy(&mq->space_empty);
	cond_destroy(&mq->space_full);
	list_destroy(&mq->queue_list_head);
}


#define ALL_FLAGS (MSG_QUEUE_READER | MSG_QUEUE_WRITER | MSG_QUEUE_NONBLOCK)
#define ALL_EVENT (MQPOLL_NOWRITERS | MQPOLL_NOREADERS | MQPOLL_READABLE | MQPOLL_WRITABLE)
// Message queue handle is a combination of the pointer to the queue backend and
// the handle flags. The pointer is always aligned on 8 bytes - its 3 least
// significant bits are always 0. This allows us to store the flags within the
// same word-sized value as the pointer by ORing the pointer with the flag bits.

// Get queue backend pointer from the queue handle
static mq_backend *get_backend(msg_queue_t queue)
{
	mq_backend *mq = (mq_backend*)(queue & ~ALL_FLAGS);
	assert(mq);
	return mq;
}

// Get handle flags from the queue handle
static int get_flags(msg_queue_t queue)
{
	return (int)(queue & ALL_FLAGS);
}

// Create a queue handle for given backend pointer and handle flags
static msg_queue_t make_handle(mq_backend *mq, int flags)
{
	assert(((uintptr_t)mq & ALL_FLAGS) == 0);
	assert((flags & ~ALL_FLAGS) == 0);
	return (uintptr_t)mq | flags;
}


static msg_queue_t mq_open(mq_backend *mq, int flags)
{
	++mq->refs;

	if (flags & MSG_QUEUE_READER) {
		++mq->readers;
		mq->no_readers = false;
	}
	if (flags & MSG_QUEUE_WRITER) {
		++mq->writers;
		mq->no_writers = false;
	}

	return make_handle(mq, flags);
}

// Returns true if this was the last handle
static bool mq_close(mq_backend *mq, int flags)
{
	assert(mq->refs != 0);
	assert(mq->refs >= mq->readers);
	assert(mq->refs >= mq->writers);

	if ((flags & MSG_QUEUE_READER) && (--mq->readers == 0)) {
		mq->no_readers = true;
	}
	if ((flags & MSG_QUEUE_WRITER) && (--mq->writers == 0)) {
		mq->no_writers = true;
	}

	if (--mq->refs == 0) {
		assert(mq->readers == 0);
		assert(mq->writers == 0);
		return true;
	}
	return false;
}


msg_queue_t msg_queue_create(size_t capacity, int flags)
{
	if (flags & ~ALL_FLAGS) {
		errno = EINVAL;
		report_error("msg_queue_create");
		return MSG_QUEUE_NULL;
	}

	// Refuse to create a message queue without capacity for
	// at least one message (length + 1 byte of message data).
	if (capacity < sizeof(size_t) + 1) {
		errno = EINVAL;
		report_error("msg_queue_create");
		return MSG_QUEUE_NULL;
	}

	mq_backend *mq = (mq_backend*)malloc(sizeof(mq_backend));
	if (!mq) {
		report_error("malloc");
		return MSG_QUEUE_NULL;
	}
	// Result of malloc() is always aligned on 8 bytes, allowing us to use the
	// 3 least significant bits of the handle to store the 3 bits of flags
	assert(((uintptr_t)mq & ALL_FLAGS) == 0);

	if (mq_init(mq, capacity) < 0) {
		// Preserve errno value that can be changed by free()
		int e = errno;
		free(mq);
		errno = e;
		return MSG_QUEUE_NULL;
	}

	return mq_open(mq, flags);
}

msg_queue_t msg_queue_open(msg_queue_t queue, int flags)
{
	if (!queue) {
		errno = EBADF;
		report_error("msg_queue_open");
		return MSG_QUEUE_NULL;
	}

	if (flags & ~ALL_FLAGS) {
		errno = EINVAL;
		report_error("msg_queue_open");
		return MSG_QUEUE_NULL;
	}

	mq_backend *mq = get_backend(queue);

	//TODO: add necessary synchronization
	mutex_lock(&mq->mutex);
	msg_queue_t new_handle = mq_open(mq, flags);
	mutex_unlock(&mq->mutex);
	return new_handle;
}

int msg_queue_close(msg_queue_t *queue)
{
	if (!queue || !*queue) {
		errno = EBADF;
		report_error("msg_queue_close");
		return -1;
	}

	mq_backend *mq = get_backend(*queue);

	//TODO: add necessary synchronization

	mutex_lock(&mq->mutex);
	if (mq_close(mq, get_flags(*queue))) {
		// Closed last handle; destroy the queue
		mutex_unlock(&mq->mutex);
		mq_destroy(mq);
		free(mq);
		*queue = MSG_QUEUE_NULL;
		return 0;
	}

	if(mq->no_readers){
		mq->queue_event = mq->queue_event | MQPOLL_WRITABLE | MQPOLL_NOREADERS;
		cond_broadcast(&mq->space_full);
	}
	if(mq->no_writers){
		mq->queue_event = mq->queue_event | MQPOLL_READABLE | MQPOLL_NOWRITERS;
		cond_broadcast(&mq->space_empty);
	}

	mutex_unlock(&mq->mutex);
	*queue = MSG_QUEUE_NULL;
	return 0;
}

void helper_wakeup(msg_queue_t queue){
	mq_backend *mq = get_backend(queue);
	list_entry *pos;
		list_for_each(pos, &mq->queue_list_head){
			waiting_node *wt_nodes = container_of(pos, waiting_node, entry);
			if(wt_nodes->events & mq->queue_event){ //requested event flag for this thread is true right now
				mutex_lock(&wt_nodes->poll_mutex);
				cond_signal(&wt_nodes->is_ready); 
				mutex_unlock(&wt_nodes->poll_mutex);
			}
		}
	
}

ssize_t msg_queue_read(msg_queue_t queue, void *buffer, size_t length)
{
	//TODO
	if (!(get_flags(queue) & MSG_QUEUE_READER)) {
		errno = EBADF;
		report_error("msg_queue_read");
		return -1;
	}
	mq_backend *mq =  get_backend(queue);
	mutex_lock(&mq->mutex); 

	//close when nothing to read
	if (mq->no_writers && ring_buffer_used(&mq->buffer) == 0 ){
		mq->queue_event = mq->queue_event | MQPOLL_NOWRITERS;
		helper_wakeup(queue);

		mutex_unlock(&mq->mutex); 
		return 0;
	}
	//consume
	
	while(!mq->no_writers && ring_buffer_used(&mq->buffer)==0){
		if (get_flags(queue) & MSG_QUEUE_NONBLOCK){
		errno = EAGAIN;
		mutex_unlock(&mq->mutex); 
		return -1;
		}
		cond_wait(&mq->space_empty, &mq->mutex);
	}

	if(ring_buffer_used(&mq->buffer) == 0 && mq->no_writers){
		mq->queue_event = mq->queue_event | MQPOLL_NOWRITERS;
		//part2
		helper_wakeup(queue);
		mutex_unlock(&mq->mutex);
		return 0;
	}

	size_t msg_head;
	ring_buffer_peek(&(mq->buffer),&msg_head,sizeof(size_t));
	if(length < msg_head){
		errno = EMSGSIZE;
		report_info("msg_queue_read");
		mutex_unlock(&mq->mutex); 
	}
	ring_buffer_read(&(mq->buffer), &msg_head, sizeof(size_t));
	ring_buffer_read(&(mq->buffer), buffer, msg_head);
	cond_signal(&mq->space_full);

	//consume
	//TODO end
	// part2
	if(ring_buffer_used(&mq->buffer) == 0 && !mq->no_writers){
		mq->queue_event = mq->queue_event & ~MQPOLL_READABLE;
	}
	mq->queue_event = mq->queue_event | MQPOLL_WRITABLE;
	helper_wakeup(queue);
	
	mutex_unlock(&mq->mutex); 
	//part2
	return msg_head;
}

int msg_queue_write(msg_queue_t queue, const void *buffer, size_t length)
{
	//TODO
	if (!(get_flags(queue) & MSG_QUEUE_WRITER)) {
		errno = EBADF;
		report_error("msg_queue_write");
		return -1;
	}
	if (length == 0){
		errno = EINVAL;
		return -1;
	}

	mq_backend *mq =  get_backend(queue);
	mutex_lock(&(mq->mutex));
	
	if (mq->no_readers){
		errno = EPIPE;
		mutex_unlock(&(mq->mutex));
		return -1;
	}
	if (mq->buffer.size <= (length+sizeof(size_t))){
		errno = EMSGSIZE;
		mutex_unlock(&(mq->mutex));
		return -1;
	}
	//produce
	while(ring_buffer_free(&mq->buffer)< (length+sizeof(size_t))){
		cond_wait(&mq->space_full, &mq->mutex);
	}
	bool result = ring_buffer_write(&(mq->buffer), (void *)&length, sizeof(size_t));
	result = ring_buffer_write(&(mq->buffer), buffer, length);
	(void)result;
	cond_signal(&mq->space_empty);
	//produce
	if (get_flags(queue) & MSG_QUEUE_NONBLOCK){
		errno = EAGAIN;
		mutex_unlock(&(mq->mutex));
		return -1;
	}
	//part 2
	if(ring_buffer_free(&mq->buffer) == 0 && !mq->no_readers){
		mq->queue_event = mq->queue_event & ~MQPOLL_WRITABLE;
	}
	mq->queue_event = mq->queue_event | MQPOLL_READABLE;
	helper_wakeup(queue);

	//errno = ENOSYS;
	mutex_unlock(&(mq->mutex));

	return 0;
}

int helper_count(msg_queue_pollfd *fds, size_t nfds){
	int ready = 0;
	for(unsigned int i = 0; i < nfds; ++i){
		if(fds[i].queue == MSG_QUEUE_NULL) continue;
		//count
		int current_event = get_backend(fds[i].queue)->queue_event;
		if((fds[i].revents = current_event & fds[i].events) > 0){
			ready++;
		}
		//count

		//core
		if(get_flags(fds[i].queue) & (MSG_QUEUE_READER | MSG_QUEUE_WRITER)){
			fds[i].revents = fds[i].revents | (current_event & (MQPOLL_NOWRITERS | MQPOLL_NOREADERS));
		}
		//core
	}
	return ready;
}

int msg_queue_poll(msg_queue_pollfd *fds, size_t nfds)
{
	unsigned int num_null = 0;
	for(int unsigned i = 0; i < nfds; ++i){
		fds[i].revents = 0;
		if(fds[i].queue == MSG_QUEUE_NULL){
			num_null++; 
			continue;
		} 
		if (fds[i].events == 0){
			errno = EINVAL;
			return -1;
		}
		if((fds[i].events & MQPOLL_READABLE) && !(get_flags(fds[i].queue) & MSG_QUEUE_READER)){
			errno = EINVAL;
			return -1;
		}
		if((fds[i].events & MQPOLL_WRITABLE) && !(get_flags(fds[i].queue) & MSG_QUEUE_WRITER)){
			errno = EINVAL;
			return -1;
		}
	}
	if(num_null == nfds || nfds == 0){ 
		errno = EINVAL;
		report_error("msg_queue_poll");
		return -1;
	}

	waiting_node *wt_nodes = (waiting_node *)malloc(sizeof(waiting_node) * nfds);
	if(wt_nodes == NULL){
		errno = ENOMEM;
		report_error("malloc");
		return -1;
	}

	cond_init(&wt_nodes->is_ready);
	mutex_init(&wt_nodes->poll_mutex);
	mutex_lock(&wt_nodes->poll_mutex);
	for(unsigned int i = 0; i < nfds; ++i){
		if(fds[i].queue != MSG_QUEUE_NULL){
		
			wt_nodes[i].events = fds[i].events;//

			mq_backend *mq = get_backend(fds[i].queue);
			mutex_lock(&mq->mutex);

			list_entry_init(&wt_nodes[i].entry);		
			list_add_tail(&mq->queue_list_head, &wt_nodes[i].entry);

			mutex_unlock(&mq->mutex);
		}
	}
	
	int num_ready = helper_count(fds, nfds);
	if(num_ready == 0){
		cond_wait(&wt_nodes->is_ready, &wt_nodes->poll_mutex);
		num_ready = helper_count(fds, nfds);
	}
	mutex_unlock(&wt_nodes->poll_mutex);
	for (unsigned int i = 0; i < nfds; ++i){
		if(fds[i].queue != MSG_QUEUE_NULL){
			mq_backend *mq = get_backend(fds[i].queue);
			mutex_lock(&mq->mutex);
			//list_del(&mq->queue_list_head, &wt_entries[i].entry);
			//
			list_del(&mq->queue_list_head, &wt_nodes[i].entry);
			mutex_unlock(&mq->mutex);
		}
	}
	
	cond_destroy(&wt_nodes->is_ready);

	mutex_unlock(&wt_nodes->poll_mutex);
	
	free(wt_nodes);
	return num_ready;
}
