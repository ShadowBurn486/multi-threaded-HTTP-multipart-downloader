#include "queue.h"
#include <pthread.h>
#include <semaphore.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>

#define handle_error_en(en, msg) \
        do { errno = en; perror(msg); exit(EXIT_FAILURE); } while (0)

#define handle_error(msg) \
        do { perror(msg); exit(EXIT_FAILURE); } while (0)


/*
 * Queue - the abstract type of a concurrent queue.
 * You must provide an implementation of this type
 * but it is hidden from the outside.
 */
typedef struct QueueStruct {
    void **data;        // Buffer data pointer
    int read_index;     // Buffer read
    int write_index;    // Buffer write
    int size;           // Buffer size

    sem_t read;         // Read and write is semaphore (lock and unlock) to make sure
    sem_t write;        // available read file is less then write file
    pthread_mutex_t mutex_lock; // Mutex lock to Avoid deadLock

} Queue;


/**
 * Allocate a concurrent queue of a specific size
 * @param size - The size of memory to allocate to the queue
 * @return queue - Pointer to the allocated queue
 */
Queue *queue_alloc(int size) {
    Queue *queue = (Queue *)calloc(1, sizeof(Queue));   // Allocate memory for the queue
    queue -> data = (void **)calloc(size, sizeof(void *)); // Initial queue
    queue -> read_index = 0;                               // Initial queue
    queue -> write_index = 0;                              // Initial queue
    queue -> size = size;                                  // Initial queue

    sem_init(&queue->read, 0, 0);                 // Initial semaphore read as 0, nothing can read at the beginning
    sem_init(&queue->write, 0, size);             // Initial semaphore write as Maxmium availible in queue can write
    pthread_mutex_init(&queue->mutex_lock, NULL); // Initial thread mutex to avoid deadLock
    return queue;
}


/**
 * Free a concurrent queue and associated memory
 *
 * Don't call this function while the queue is still in use.
 * (Note, this is a pre-condition to the function and does not need
 * to be checked)
 *
 * @param queue - Pointer to the queue to free
 */
void queue_free(Queue *queue) {
    free(queue->data);  // Clean data and Reset evrything
    queue->read_index = 0;
    queue->write_index = 0;
    queue->size = 0;
    free(queue);
}


/**
 * Place an item into the concurrent queue.
 * If no space available then queue will block
 * until a space is available when it will
 * put the item into the queue and immediatly return
 *
 * @param queue - Pointer to the queue to add an item to
 * @param item - An item to add to queue. Uses void* to hold an arbitrary
 *               type. User's responsibility to manage memory and ensure
 *               it is correctly typed.
 */
void queue_put(Queue *queue, void *item) {

    sem_wait(&queue->write);                // Wait until get write signal
    pthread_mutex_lock(&queue->mutex_lock);       // Lock the queue to avoid deadlock

    queue->data[queue->write_index++] = item; // Get the data and save in the current write index and then increase the write index

    if (queue->write_index >= queue->size) queue->write_index = 0;  // Circular buffer when write index reach end

    pthread_mutex_unlock(&queue->mutex_lock);     // Unlock the queque
    sem_post(&queue->read);                 // Release read signal which means the queue is able to read
}


/**
 * Get an item from the concurrent queue
 *
 * If there is no item available then queue_get
 * will block until an item becomes avaible when
 * it will immediately return that item.
 *
 * @param queue - Pointer to queue to get item from
 * @return item - item retrieved from queue. void* type since it can be
 *                arbitrary
 */
void *queue_get(Queue *queue) {
    sem_wait(&queue->read);             // Wait until get read signal
    pthread_mutex_lock(&queue->mutex_lock);   // Lock the queue to avoid deadlock

    void *buffer_data = queue->data[queue->read_index++];  // Get the data and save in the current read index and then increase the read index

    if (queue->read_index >= queue->size) queue->read_index = 0;     // Circular buffer when read index reach end

    pthread_mutex_unlock(&queue->mutex_lock);   // Unlock the queque
    sem_post(&queue->write);              // Release write signal which means the queue is able to write
    return buffer_data;
}

