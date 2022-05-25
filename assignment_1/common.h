/* 
 * Operating Systems (2INCO) Practical Assignment
 * Interprocess Communication
 * 
 * Zachary Kohnen (1655221)
 *
 * Contains definitions which are commonly used by the farmer and the workers
 *
 */

#ifndef COMMON_H_
#define COMMON_H_

#include "uint128.h"

// Maximum size for any message in the tests
#define MAX_MESSAGE_LENGTH 6

typedef struct {
    char starting_char; // The first char of the message to search for
    char alphabet_start; // The min char value that could be in the message
    char alphabet_stop; // The max char value that could be in the message
    uint128_t hash; // The hash to compare against
    int hash_id; // A unique ID of the hash to identify responses
} job_t;

typedef struct {
    char match[MAX_MESSAGE_LENGTH + 1]; // The message that matches the hash
    int hash_id; // The ID of the hash that was matched against
} response_t;

#endif // COMMON_H_
