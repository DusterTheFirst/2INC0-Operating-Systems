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

#include <errno.h>
#include <mqueue.h> // for mq
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h> // for execlp

#include "common.h"
#include "settings.h" // definition of work

int main(int argc, char *argv[]) {
    if (argc != 1) {
        fprintf(stderr, "%s: invalid arguments\n", argv[0]);
        return 1;
    }

#define STUDENT_NAME "zachary_kohnen"

    char job_queue_name[128];
    char response_queue_name[128];

    // Name the message queues
    snprintf(job_queue_name,
             sizeof(job_queue_name),
             "/job_queue_%s_%d",
             STUDENT_NAME, getpid());

    snprintf(response_queue_name,
             sizeof(response_queue_name),
             "/response_queue_%s_%d",
             STUDENT_NAME, getpid());

    // Create the message queues
    mqd_t job_queue = mq_open(
        job_queue_name,
        O_WRONLY | O_CREAT | O_EXCL,
        0600,
        &((struct mq_attr){
            .mq_msgsize = sizeof(job_t),
            .mq_maxmsg = MQ_MAX_MESSAGES,
        }));

    if (job_queue == -1) {
        perror("Failed to create job queue");

        return 1;
    }

    mqd_t response_queue = mq_open(
        response_queue_name,
        O_RDONLY | O_CREAT | O_EXCL,
        0600,
        &((struct mq_attr){
            .mq_msgsize = sizeof(response_t),
            .mq_maxmsg = MQ_MAX_MESSAGES,
        }));

    if (response_queue == -1) {
        perror("Failed to create response queue");

        return 1;
    }

    pid_t workers[NROF_WORKERS] = { 0 };

    // Spawn the children
    fprintf(stderr, "farmer: Spawning workers\n");
    for (size_t i = 0; i < NROF_WORKERS; i++) {
        pid_t worker;

        // Fork the child
        worker = fork();

        // Exit if fork failed for some reason
        if (worker < 0) {
            perror("fork() failed");
            return 1;
        }

        // Replace this process with the worker if it is the fork
        if (worker == 0) {
            execlp("./worker",
                   "worker",
                   job_queue_name,
                   response_queue_name, NULL);

            // we should never arrive here...
            perror("execlp() failed");
            return 1;
        }

        // Save the PID and queues of the farmer
        workers[i] = worker;
    }

    size_t sent_jobs = 0, received_responses = 0;
    char matches[MD5_LIST_LENGTH][MAX_MESSAGE_LENGTH + 1] = { { '\0' } };

    // Dispatch jobs
    fprintf(stderr, "farmer: Dispatching jobs\n");
    while (received_responses < MD5_LIST_LENGTH) {
        // Fill up the job buffers
        while (sent_jobs < JOBS_COUNT) {
            struct mq_attr attributes;
            int attr_status = mq_getattr(job_queue, &attributes);

            if (attr_status == -1) {
                perror("Failed to get attributes of job queue");
                return 1;
            }

            // Do not attempt to send job if the queue is full
            if (attributes.mq_curmsgs == MQ_MAX_MESSAGES) {
                break;
            }

            job_t job = {
                .hash = md5_list[sent_jobs / ALPHABET_LENGTH],
                .hash_id = sent_jobs / ALPHABET_LENGTH,
                .starting_char = ALPHABET_START_CHAR +
                                 sent_jobs % ALPHABET_LENGTH,
                .alphabet_start = ALPHABET_START_CHAR,
                .alphabet_stop = ALPHABET_END_CHAR,
            };

            fprintf(stderr, "farmer: Sending job %c 0x%016lx\n",
                    job.starting_char,
                    HI(job.hash));

            int send_status = mq_send(job_queue, (char *)&job,
                                      sizeof(job_t), 0);

            if (send_status == -1) {
                perror("Failed to send job to worker");
                return 1;
            }

            sent_jobs++;
        }

        // Try to receieve all of the avaliable jobs
        while (received_responses < MD5_LIST_LENGTH) {
            struct mq_attr attributes;
            int status = mq_getattr(response_queue, &attributes);

            if (status == -1) {
                perror("Failed to get attributes of response queue");
                return 1;
            }

            // Do not attempt to read from an empty message queue
            if (attributes.mq_curmsgs == 0) {
                break;
            }

            fprintf(stderr, "farmer: Receiving response\n");

            response_t response;
            ssize_t receive_status = mq_receive(
                response_queue,
                (char *)&response,
                sizeof(job_t), NULL);

            if (receive_status == -1) {
                perror("parent: Failed to receive respone");

                return 1;
            }

            char *response_match = matches[response.hash_id];

            memcpy(response_match, response.match,
                   MAX_MESSAGE_LENGTH + 1);

            received_responses++;
        }
    }

    // Print the received matches
    for (int i = 0; i < MD5_LIST_LENGTH; i++) {
        printf("'%s'\n", matches[i]);
    }

    // Send the shutdown message to all children
    fprintf(stderr, "farmer: Shutting down children\n");
    for (size_t i = 0; i < NROF_WORKERS; i++) {
        int status = mq_send(job_queue,
                             (char *)&((job_t){ .starting_char = '\0' }),
                             sizeof(job_t), 0);

        if (status == -1) {
            perror("Failed to send stop job to worker");
            return 1;
        }
    }

    // Wait for all of the children
    fprintf(stderr, "farmer: Waiting for children\n");
    for (size_t i = 0; i < NROF_WORKERS; i++) {
        pid_t worker = workers[i];

        fprintf(stderr, "farmer: Waiting for %d\n", worker);
        // Wait for the child
        waitpid(worker, NULL, 0);
        fprintf(stderr, "farmer: child %d has finished\n", worker);

        // Remove the pid just incase im dumb and try to use it again
        worker = 0;
    }

    // Close the message queues
    {
        int job_close = mq_close(job_queue);

        if (job_close == -1) {
            perror("Failed to close job queue");
            return 1;
        }

        int job_unlink = mq_unlink(job_queue_name);

        if (job_unlink == -1) {
            perror("Failed to unlink job queue");
            return 1;
        }

        int response_close = mq_close(response_queue);

        if (response_close == -1) {
            perror("Failed to close job queue");
            return 1;
        }

        int response_unlink = mq_unlink(response_queue_name);

        if (response_unlink == -1) {
            perror("Failed to unlink job queue");
            return 1;
        }
    }

    return 0;
}
