/*
 * str_queue.h
 *
 *  Created on: Dec 30, 2014
 *      Author: Minh
 */

#ifndef USER_STR_QUEUE_H_
#define USER_STR_QUEUE_H_
#include "os_type.h"
typedef struct {
	uint8_t *buf;
	uint32_t pr;
	uint32_t pw;
	uint32_t size;
	uint32_t strLen;
} STR_QUEUE;

void QUEUE_Init(STR_QUEUE *queue, int strLen, int strSize);
int32_t QUEUE_Puts(STR_QUEUE *queue, char* str);
int32_t QUEUE_Gets(STR_QUEUE *queue, char* str);
int32_t QUEUE_IsEmpty(STR_QUEUE *queue);
#endif /* USER_STR_QUEUE_H_ */
