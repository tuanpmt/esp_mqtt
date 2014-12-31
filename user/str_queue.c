/*
 * str_queue.c
 *
 *  Created on: Dec 30, 2014
 *      Author: Minh
 */
#include "str_queue.h"
#include "user_interface.h"
#include "osapi.h"
#include "os_type.h"
#include "mem.h"

void QUEUE_Init(STR_QUEUE *queue, int strLen, int maxString)
{
	queue->buf = (uint8_t*)os_zalloc(strLen*maxString);
	queue->size = maxString;
	queue->strLen = strLen;
	queue->pr = queue->pw = 0;
}
int32_t QUEUE_Puts(STR_QUEUE *queue, char* str)
{
	uint32_t next = (queue->pw + 1)%queue->size;
	if(next == queue->pr) return -1;
	os_strcpy(queue->buf + queue->pw*queue->strLen, str);
	queue->pw = next;
	return 0;
}
int32_t QUEUE_Gets(STR_QUEUE *queue, char* str)
{
	if(queue->pr == queue->pw) return -1;
	os_strcpy(str, queue->buf + queue->pr*queue->strLen);
	queue->pr = (queue->pr + 1) % queue->size;
	return 0;
}
int32_t QUEUE_IsEmpty(STR_QUEUE *queue)
{
	return (queue->pr == queue->pw);
}


