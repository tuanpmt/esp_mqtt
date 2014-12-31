/* str_queue.c
*
* Copyright (c) 2014-2015, Tuan PM <tuanpm at live dot com>
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* * Redistributions of source code must retain the above copyright notice,
* this list of conditions and the following disclaimer.
* * Redistributions in binary form must reproduce the above copyright
* notice, this list of conditions and the following disclaimer in the
* documentation and/or other materials provided with the distribution.
* * Neither the name of Redis nor the names of its contributors may be used
* to endorse or promote products derived from this software without
* specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
* LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
* CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.
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


