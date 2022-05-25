/* 
 * Operating Systems  (2INCO)  Practical Assignment
 * Interprocess Communication
 *
 * Zachary Kohnen (1655221)
 *
 * Grading:
 * Your work will be evaluated based on the following criteria:
 * - Satisfaction of all the specifications
 * - Correctness of the program
 * - Coding style
 * - Report quality
 */

#include <complex.h>
#include <errno.h>  // for perror()
#include <mqueue.h> // for mq-stuff
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>   // for time()
#include <unistd.h> // for getpid()

#include "common.h"
#include "md5s.h"

static void rsleep(int t);

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "%s: invalid arguments\n", argv[0]);
        return 1;
    }

    char *job_queue_name = argv[1];
    char *response_queue_name = argv[2];

    // fprintf(stderr, "%s:%s\n", job_queue_name, response_queue_name);

    mqd_t job_queue = mq_open(job_queue_name, O_RDONLY);

    if (job_queue == -1) {
        fprintf(stderr, "%d: Failed to open %s: %s\n",
                getpid(), job_queue_name, strerror(errno));

        return 1;
    }

    mqd_t response_queue = mq_open(response_queue_name, O_WRONLY);

    if (response_queue == -1) {
        fprintf(stderr, "%d: Failed to open %s: %s\n",
                getpid(), response_queue_name, strerror(errno));

        return 1;
    }

    while (true) {
        job_t job;
        ssize_t receive_status = mq_receive(job_queue, (char *)&job,
                                            sizeof(job_t), NULL);

        if (receive_status == -1) {
            fprintf(stderr, "%d: Failed to receive message: %s\n",
                    getpid(), strerror(errno));

            return 1;
        }

        // If the char is a null char, that means the program is finished
        if (job.starting_char == '\0') {
            fprintf(stderr, "%d: Exiting\n", getpid());

            // Close handles to both queues
            mq_close(job_queue);
            mq_close(response_queue);
            return 0;
        }

        fprintf(stderr, "%d: Received job %c 0x%016lx\n",
                getpid(), job.starting_char,
                HI(job.hash));

        rsleep(10000);

        // TODO: DO THE WORK
        char match[MAX_MESSAGE_LENGTH + 1] = {
            job.starting_char, '\0'
        };
        bool success = false;

        int alphabet_length = (job.alphabet_stop - job.alphabet_start + 1);

        int possibilities = alphabet_length;
        for (int i = 1; i < MAX_MESSAGE_LENGTH; i++) {
            possibilities *= alphabet_length;
        }

        for (int i = 0; i < possibilities; i++) {
            // Carry the increment
            for (int position = 2;
                 position < MAX_MESSAGE_LENGTH && match[position - 1] != '\0';
                 position++) {
                // If the previous char overflowed
                if (match[position - 1] > job.alphabet_stop) {
                    // Add one to this char or set it to the first one depending
                    // on if it already exists or not
                    if (match[position] == '\0') {
                        match[position] = job.alphabet_start;
                    } else {
                        match[position]++;
                    }

                    // Set the previous char back to the start
                    match[position - 1] = job.alphabet_start;
                }
            }

            // printf("%s: %d\n", match, possibilities);

            // Hash the mesage
            uint128_t new_hash = md5s(match, strlen(match));

            if (new_hash == job.hash) {
                success = true;
                break;
            }

            // Increment the second char
            match[1]++;
        }

        // Only send the message if it succeeded
        if (success) {
            response_t response;
            strncpy(response.match, match, MAX_MESSAGE_LENGTH + 1);
            response.hash_id = job.hash_id;
            ssize_t send_status = mq_send(response_queue, (char *)&response,
                                          sizeof(response_t), 0);

            if (send_status == -1) {
                fprintf(stderr, "%d: Failed to send message: %s\n",
                        getpid(), strerror(errno));

                return 1;
            }
        }
    }

    return 0;
}

/*
 * rsleep(int t)
 *
 * The calling thread will be suspended for a random amount of time
 * between 0 and t microseconds
 * At the first call, the random generator is seeded with the current time
 */
static void rsleep(int t) {
    static bool first_call = true;

    if (first_call == true) {
        srandom(time(NULL) % getpid());
        first_call = false;
    }
    usleep(random() % t);
}
