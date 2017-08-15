#ifndef _PUB_LIST_
#define _PUB_LIST_

void pub_insert(const char* topic, uint32_t topic_len, const char *data, uint32_t data_len, bool local);
void pub_process();

#endif /* _PUB_LIST_ */
