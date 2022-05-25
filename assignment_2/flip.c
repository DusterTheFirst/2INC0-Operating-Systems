/* 
 * Operating Systems  (2INCO)  Practical Assignment
 * Threaded Application
 *
 * Zachary Kohnen (1655221)
 * Cansu Izat (1372416)
 *
 * Grading:
 * Students who hand in clean code that fully satisfies the minimum requirements will get an 8. 
 * Extra steps can lead to higher marks because we want students to take the initiative. 
 * 
 */

#include <errno.h> // for perror()
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

// For testing purposes only
// #include <math.h>

#include "flip.h"
#include "uint128.h"

// create a bitmask where bit at position n is set
#define BITMASK(n) (((uint128_t)1) << (n))

// check if bit n in v is set
#define BIT_IS_SET(v, n) (((v)&BITMASK(n)) == BITMASK(n))

// set bit n in v
#define BIT_SET(v, n) ((v) = (v) | BITMASK(n))

// clear bit n in v
#define BIT_CLEAR(v, n) ((v) = (v) & ~BITMASK(n))

static pthread_mutex_t mutexes[(NROF_PIECES / 128) + 1];

static void *thread(void *arg) {
    int multiple = *((int *)arg);
    free(arg);

    for (int current_piece = multiple; current_piece <= NROF_PIECES;
         current_piece += multiple) {
        int bit = current_piece % 128;
        int index = current_piece / 128;

        pthread_mutex_lock(&mutexes[index]);

        uint128_t chunk = buffer[index];

        if (BIT_IS_SET(chunk, bit)) {
            BIT_CLEAR(chunk, bit);
        } else {
            BIT_SET(chunk, bit);
        }

        buffer[index] = chunk;

        pthread_mutex_unlock(&mutexes[index]);
    }

    return NULL;
}

uint64_t micros() {
    struct timespec now;
    timespec_get(&now, TIME_UTC);
    return ((uint64_t)now.tv_sec) * 1000000 + ((uint64_t)now.tv_nsec) / 1000;
}

int main(void) {
    uint64_t start = micros();

    for (int i = 0; i < sizeof(mutexes) / sizeof(pthread_mutex_t); i++) {
        pthread_mutex_init(&mutexes[i], NULL);
    }

    pthread_t thread_ids[NROF_THREADS] = { 0 };

    // Set all pieces black
    for (int chunk = 0; chunk < (NROF_PIECES / 128) + 1; chunk++) {
        buffer[chunk] = ~0;
    }

    // This will spawn threads
    int thread_index = 0;

    for (int multiple = 2; multiple <= NROF_PIECES; multiple++) {
        pthread_t *thread_id = &thread_ids[thread_index];

        // Check if thread exists
        if (*thread_id != 0) {
            // Wait on the thread
            pthread_join(*thread_id, NULL);
        }

        // Setup the argument(s) to the thread
        int *multiple_arg = malloc(sizeof(int));
        if (multiple_arg == NULL) {
            perror("unable to allocate memory for thread arguments");
            return 1;
        }
        *multiple_arg = multiple;

        // Spawn the thread
        pthread_create(thread_id, NULL, thread, multiple_arg);

        // Increment the thread_index and wrap it if it is >(NROF_THREADS - 1)
        thread_index = (thread_index + 1) % NROF_THREADS;
    }

    // Join all remaining threads
    for (int i = 0; i < NROF_THREADS; i++) {
        pthread_t *thread_id = &thread_ids[i];

        // Check if thread exists
        if (*thread_id != 0) {
            // Wait on the thread
            pthread_join(*thread_id, NULL);
            *thread_id = 0;
        }
    }

    // Unnecessary to lock mutex since the code is single threaded at this point

    // Print all the items black
    for (int piece = 1; piece <= NROF_PIECES; piece++) {
        int bit = piece % 128;
        int index = piece / 128;

        uint128_t chunk = buffer[index];

        if (BIT_IS_SET(chunk, bit)) {
            // double square_root = sqrt((double)piece);
            // bool perfect_square = floorf(square_root) == square_root;

            printf("%d\n", piece);
        }
    }

    uint64_t end = micros();

    double time_elapsed = (double)(end - start) / 1000000.0;

    fprintf(stderr, "%f s\n", time_elapsed);

    return 0;
}
