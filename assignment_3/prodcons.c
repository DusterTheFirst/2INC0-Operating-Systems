/* 
 * Operating Systems  (2INCO)  Practical Assignment
 * Condition Variables Application
 *
 * Zachary Kohnen (1655221)
 * Cansu Izat (1372416)
 *
 * Grading:
 * Students who hand in clean code that fully satisfies the minimum requirements will get an 8. 
 * Extra steps can lead to higher marks because we want students to take the initiative.
 */

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "prodcons.h"

/**
 * @brief Unwrap a POSIX call return value, exiting from the program with
 * the given error message if there was a negative return value.
 * 
 * Using this function helps reduce boiler plate for C's annoying error handling.
 * 
 * This is analogous to Rust's .unwrap() call on a Result<T, E>
 * 
 * @param return_value The return value of the POSIX function call
 * @param message The error message to show on failure
 */
#define unwrap(return_value, message)                                \
    if (return_value != 0) {                                         \
        fprintf(stderr, "thread %ld panicked at: '%s: %s', %s:%d\n", \
                pthread_self(), message, strerror(return_value),     \
                __FILE__, __LINE__);                                 \
        exit(1);                                                     \
    }

// Thread Safe First in First out buffer
typedef struct {
    ITEM buffer[BUFFER_SIZE];
    pthread_mutex_t buffer_lock;

    int buffer_length;
    pthread_rwlock_t buffer_length_lock;

    ITEM expected;
    pthread_rwlock_t expected_lock;
} fifo_t;

#define FIFO_INITIALIZER                                  \
    {                                                     \
        .buffer_lock = PTHREAD_MUTEX_INITIALIZER,         \
                                                          \
        .buffer_length = 0,                               \
        .buffer_length_lock = PTHREAD_RWLOCK_INITIALIZER, \
                                                          \
        .expected = 0,                                    \
        .expected_lock = PTHREAD_RWLOCK_INITIALIZER       \
    }

// Wrapper struct for a condvar, its mutex, and a predicate
typedef struct {
    bool ready;
    pthread_cond_t cond;
    pthread_mutex_t mutex;
} condvar_t;

#define CONDVAR_INITIALIZER                 \
    {                                       \
        .cond = PTHREAD_COND_INITIALIZER,   \
        .mutex = PTHREAD_MUTEX_INITIALIZER, \
        .ready = false                      \
    }

// Condvar with an extra dependency to prevent unnecessary wakes
typedef struct {
    ITEM expecting;

    bool ready;
    pthread_cond_t cond;
    pthread_mutex_t mutex;
} expecting_t;

#define EXPECTING_INITIALIZER               \
    {                                       \
        .cond = PTHREAD_COND_INITIALIZER,   \
        .mutex = PTHREAD_MUTEX_INITIALIZER, \
        .ready = false,                     \
        .expecting = -1                     \
    }

/**
 * @brief Result from try_push
 */
typedef enum {
    // The item was pushed to the buffer
    PUSH_SUCCESS = 0,
    // The item requested to push was not the expected next item
    PUSH_NOT_NEXT = -1,
    // The buffer had no space for the item but the item is the next expected
    PUSH_FULL = -2,
} fifo_push_result_t;

/**
 * @brief Attempt to push an item onto the FIFO
 * 
 * @param item The item to push to the FIFO buffer
 * @return A code representing the result of the push
 */
static fifo_push_result_t fifo_push(fifo_t *fifo, ITEM item) {
    // Make sure the item is the next expected item
    unwrap(pthread_rwlock_rdlock(&fifo->expected_lock),
           "failed to read-lock fifo expected rwlock");
    bool is_next = item == fifo->expected;
    unwrap(pthread_rwlock_unlock(&fifo->expected_lock),
           "failed to unlock fifo expected rwlock");

    if (!is_next) {
        return PUSH_NOT_NEXT;
    }

    // Check the length of the buffer
    unwrap(pthread_rwlock_rdlock(&fifo->buffer_length_lock),
           "failed to read-lock fifo buffer_length rwlock");
    bool is_full = fifo->buffer_length == BUFFER_SIZE;
    unwrap(pthread_rwlock_unlock(&fifo->buffer_length_lock),
           "failed to unlock fifo buffer_length rwlock");

    if (is_full) {
        return PUSH_FULL;
    }

    unwrap(pthread_mutex_lock(&fifo->buffer_lock),
           "failed to lock fifo buffer mutex");
    unwrap(pthread_rwlock_wrlock(&fifo->buffer_length_lock),
           "failed to write-lock fifo buffer_length rwlock");

    // Use the current length as the index to the next free space
    fifo->buffer[fifo->buffer_length] = item;
    fifo->buffer_length++;

    unwrap(pthread_rwlock_unlock(&fifo->buffer_length_lock),
           "failed to unlock fifo buffer_length rwlock");
    unwrap(pthread_mutex_unlock(&fifo->buffer_lock),
           "failed to unlock fifo buffer mutex");

    unwrap(pthread_rwlock_wrlock(&fifo->expected_lock),
           "failed to write-lock fifo expected rwlock");
    fifo->expected++;
    unwrap(pthread_rwlock_unlock(&fifo->expected_lock),
           "failed to unlock fifo expected rwlock");

    return PUSH_SUCCESS;
}

typedef enum {
    // The item was popped from the buffer
    POP_SUCCESS = 0,
    // The fifo is empty
    POP_EMPTY = -1,
    // The fifo is not expecting any more items
    POP_DONE = -2
} fifo_pop_result_t;

/**
 * @brief Pop an item off of the FIFO buffer
 * 
 * @param item The location in memory to pop the item into. Will throw away the
 * popped value if this is NULL
 * @return A code representing the result of the pop
 */
static fifo_pop_result_t fifo_pop(fifo_t *fifo, ITEM *item) {
    // Check the length of the buffer
    unwrap(pthread_rwlock_rdlock(&fifo->buffer_length_lock),
           "failed to read-lock fifo buffer_length rwlock");
    bool is_empty = fifo->buffer_length == 0;
    unwrap(pthread_rwlock_unlock(&fifo->buffer_length_lock),
           "failed to unlock fifo buffer_length rwlock");

    if (is_empty) {
        // Check if the fifo is done receiving items
        unwrap(pthread_rwlock_rdlock(&fifo->expected_lock),
               "failed to read-lock fifo expected rwlock");
        bool is_done = fifo->expected == NROF_ITEMS;
        unwrap(pthread_rwlock_unlock(&fifo->expected_lock),
               "failed to unlock fifo expected rwlock");

        if (is_done) {
            return POP_DONE;
        } else {
            return POP_EMPTY;
        }
    }

    unwrap(pthread_rwlock_wrlock(&fifo->buffer_length_lock),
           "failed to write-lock fifo buffer_length rwlock");
    int items_to_shift = fifo->buffer_length;
    fifo->buffer_length--;
    unwrap(pthread_rwlock_unlock(&fifo->buffer_length_lock),
           "failed to unlock fifo buffer_length rwlock");

    unwrap(pthread_mutex_lock(&fifo->buffer_lock),
           "failed to lock fifo buffer mutex");
    // Write the item into the output
    if (item != NULL) {
        *item = fifo->buffer[0];
    }

    // Iterate through the items left, shifting them over
    for (int i = 1; i < items_to_shift; i++) {
        fifo->buffer[i - 1] = fifo->buffer[i];
    }

    // NOTE: This is not the most optimized, using something like a circular
    // buffer would be faster, but since the array is at most 5 items (as per
    // the examples), we found it would be easier to reason about the code if
    // we shifted the items as so
    unwrap(pthread_mutex_unlock(&fifo->buffer_lock),
           "failed to unlock fifo buffer mutex");

    return POP_SUCCESS;
}

/**
 * @brief Wait for the condvar to receive a signal
 */
static void condvar_wait(condvar_t *condvar) {
    unwrap(pthread_mutex_lock(&condvar->mutex), "failed to lock convar mutex");
    while (!condvar->ready) {
        unwrap(pthread_cond_wait(&condvar->cond, &condvar->mutex),
               "failed to wait on condvar");
    }
    condvar->ready = false;
    unwrap(pthread_mutex_unlock(&condvar->mutex),
           "failed to unlock condvar mutex");
}

/**
 * @brief Send a signal to the condvar
 */
static void condvar_broadcast(condvar_t *condvar) {
    unwrap(pthread_mutex_lock(&condvar->mutex), "failed to lock condvar mutex");
    condvar->ready = true;
    unwrap(pthread_cond_broadcast(&condvar->cond), "failed to signal condvar");
    unwrap(pthread_mutex_unlock(&condvar->mutex),
           "failed to unlock condvar mutex");
}

/**
 * @brief Wait for the condvar to receive a signal caused by the expected ITEM
 * 
 * @param expect The item to wait for
 */
static void expecting_expect(expecting_t *st, ITEM expect) {
    unwrap(pthread_mutex_lock(&st->mutex), "failed to lock convar mutex");
    st->expecting = expect;

    while (!st->ready) {
        unwrap(pthread_cond_wait(&st->cond, &st->mutex),
               "failed to wait on condvar");
    }
    st->ready = false;
    unwrap(pthread_mutex_unlock(&st->mutex),
           "failed to unlock condvar mutex");
}

/**
 * @brief Similar to condvar_broadcast, except it will only signal if the condvar
 * depends on the given item. This prevents signalling to uninterested threads.
 * 
 * @param expected The item that is expected
 */
static void expecting_signal(expecting_t *st, ITEM expected) {
    unwrap(pthread_mutex_lock(&st->mutex), "failed to lock convar mutex");
    if (st->expecting == expected) {
        st->ready = true;
        unwrap(pthread_cond_broadcast(&st->cond), "failed to signal condvar");
    }
    unwrap(pthread_mutex_unlock(&st->mutex),
           "failed to unlock condvar mutex");
}

static void rsleep(int t);
static ITEM get_next_item(void);

// Global variables
static fifo_t fifo = FIFO_INITIALIZER;

static condvar_t consumer_push_cond = CONDVAR_INITIALIZER;
// Condvar used for the next expected producer when the fifo is full
static condvar_t pop_cond = CONDVAR_INITIALIZER;
// Condvar used for other producers, not expected
static expecting_t producers_push_cond[NROF_PRODUCERS];

static __attribute__((constructor)) void init_producers_push_cond(void) {
    for (int i = 0; i < NROF_PRODUCERS; i++) {
        producers_push_cond[i] = (expecting_t)EXPECTING_INITIALIZER;
    }
}

/* producer thread */
static void *producer(void *arg) {
    int index = *(int *)arg;
    free(arg);

    while (true) {
        fprintf(stderr, "producer[%d]: getting item\n", index);
        ITEM item = get_next_item();

        if (item == NROF_ITEMS) {
            break;
        }

        // Do "work"
        fprintf(stderr, "producer[%d]: 'working' on item %d\n", index, item);
        rsleep(100);

        // Repeatedly attempt to submit work
        while (true) {
            fprintf(stderr, "producer[%d]: submitting work...\n", index);

            fifo_push_result_t result = fifo_push(&fifo, item);

            switch (result) {
                case PUSH_SUCCESS: {
                    fprintf(stderr, "producer[%d]: submitting work...success\n",
                            index);
                    condvar_broadcast(&consumer_push_cond);

                    for (int i = 0; i < NROF_PRODUCERS; i++) {
                        expecting_signal(&producers_push_cond[i], item);
                    }

                    break;
                }
                case PUSH_NOT_NEXT: {
                    fprintf(stderr,
                            "producer[%d]: submitting work...not next\n",
                            index);

                    expecting_expect(&producers_push_cond[index], item - 1);

                    continue;
                }
                case PUSH_FULL: {
                    fprintf(stderr, "producer[%d]: submitting work...full\n",
                            index);

                    condvar_wait(&pop_cond);

                    continue;
                }
                default: {
                    fprintf(stderr,
                            "producer[%d]: UNEXPECTED RETURN VALUE: %d\n",
                            index, result);

                    exit(1);
                }
            }

            break;
        }
    }

    fprintf(stderr, "producer[%d]: finished\n", index);

    return NULL;
}

/* consumer thread */
static void *consumer(void *arg) {
    (void)arg; // Ignore the argument

    while (true) {
        fprintf(stderr, "consumer: receiving work...\n");
        ITEM item;
        fifo_pop_result_t result = fifo_pop(&fifo, &item);

        switch (result) {
            case POP_SUCCESS: {
                fprintf(stderr, "consumer: receiving work...success\n");

                condvar_broadcast(&pop_cond);

                printf("%d\n", item);

                // Do "work"
                fprintf(stderr, "consumer: 'working'\n");
                rsleep(100);

                continue;
            }

            case POP_EMPTY: {
                fprintf(stderr, "consumer: receiving work...empty\n");
                condvar_wait(&consumer_push_cond);

                continue;
            }

            case POP_DONE: {
                fprintf(stderr, "consumer: receiving work...done\n");

                break;
            }

            default: {
                fprintf(stderr, "consumer: UNEXPECTED RETURN VALUE: %d\n",
                        result);

                exit(1);
            }
        }

        break;
    }

    fprintf(stderr, "consumer: finished\n");

    return NULL;
}

int main(void) {
    fprintf(stderr, "Starting consumer thread\n");
    pthread_t consumer_thread;
    unwrap(pthread_create(&consumer_thread, NULL, consumer, NULL),
           "failed to create the consumer thread");

    pthread_t producers[NROF_PRODUCERS];

    for (int i = 0; i < NROF_PRODUCERS; i++) {
        int *index = malloc(sizeof(int));

        if (index == NULL) {
            perror("failed to allocate space for producer arguments");

            exit(1);
        }

        *index = i;

        fprintf(stderr, "Starting producer thread %d\n", i);
        unwrap(pthread_create(&producers[i], NULL, producer, index),
               "failed to create the producer thread");
    }

    unwrap(pthread_join(consumer_thread, NULL),
           "failed to join consumer thread");

    for (int i = 0; i < NROF_PRODUCERS; i++) {
        unwrap(pthread_join(producers[i], NULL),
               "failed to join producer thread");
    }

    return 0;
}

/*
 * rsleep(int t)
 *
 * The calling thread will be suspended for a random amount of time between 0 and t microseconds
 * At the first call, the random generator is seeded with the current time
 */
static void rsleep(int t) {
    static bool first_call = true;

    if (first_call == true) {
        srandom(time(NULL));
        first_call = false;
    }
    usleep(random() % t);
}

/* 
 * get_next_item()
 *
 * description:
 *	thread-safe function to get a next job to be executed
 *	subsequent calls of get_next_item() yields the values 0..NROF_ITEMS-1 
 *	in arbitrary order 
 *	return value NROF_ITEMS indicates that all jobs have already been given
 * 
 * parameters:
 *	none
 *
 * return value:
 *	0..NROF_ITEMS-1: job number to be executed
 *	NROF_ITEMS:	 ready
 */
static ITEM get_next_item(void) {
    static pthread_mutex_t job_mutex = PTHREAD_MUTEX_INITIALIZER;
    // keep track of issued jobs
    static bool jobs[NROF_ITEMS + 1] = { false };
    // seq.nr. of job to be handled
    static int counter = 0;
    ITEM found; // item to be returned

    /* avoid deadlock: when all producers are busy but none has the next expected item for the consumer 
	 * so requirement for get_next_item: when giving the (i+n)'th item, make sure that item (i) is going to be handled (with n=nrof-producers)
	 */
    pthread_mutex_lock(&job_mutex);

    counter++;
    if (counter > NROF_ITEMS) {
        // we're ready
        found = NROF_ITEMS;
    } else {
        if (counter < NROF_PRODUCERS) {
            // for the first n-1 items: any job can be given
            // e.g. "random() % NROF_ITEMS", but here we bias the lower items
            found = (random() % (2 * NROF_PRODUCERS)) % NROF_ITEMS;
        } else {
            // deadlock-avoidance: item 'counter - NROF_PRODUCERS' must be
            // given now
            found = counter - NROF_PRODUCERS;
            if (jobs[found] == true) {
                // already handled, find a random one, with a bias for lower
                // items
                found = (counter + (random() % NROF_PRODUCERS)) % NROF_ITEMS;
            }
        }

        // check if 'found' is really an unhandled item;
        // if not: find another one
        if (jobs[found] == true) {
            // already handled, do linear search for the oldest
            found = 0;
            while (jobs[found] == true) {
                found++;
            }
        }
    }
    jobs[found] = true;

    pthread_mutex_unlock(&job_mutex);
    return found;
}
