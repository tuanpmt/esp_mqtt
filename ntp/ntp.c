#include <c_types.h>
#include <user_interface.h>
#include <espconn.h>
#include <osapi.h>
#include <mem.h>
#include <time.h>
#include <sys_time.h>

#include "ntp.h"
#include "lwip/def.h"
#include "user_config.h"
#include "driver/uart.h"
//#include "utils.h"

#define OFFSET 2208988800ULL

static ip_addr_t ntp_server_ip = {0};
static os_timer_t ntp_timeout;
static struct espconn *pCon, pConDNS;

static struct timeval t_tv = {0,0};
static uint64_t t_offset = 0;

void ICACHE_FLASH_ATTR get_cur_time(struct timeval *tv)
{
	uint64_t t_curr = get_long_systime() - t_offset + t_tv.tv_usec;
	tv->tv_sec = t_tv.tv_sec + t_curr/1000000;
	tv->tv_usec = t_curr%1000000; 
}


uint8_t* ICACHE_FLASH_ATTR get_timestr(int16_t timezone)
{
	struct timeval tv;
	static uint8_t buf[10];

	get_cur_time(&tv);
	tv.tv_sec += timezone * 3600;
	os_sprintf(buf, "%02d:%02d:%02d", (tv.tv_sec/3600)%24, (tv.tv_sec/60)%60, tv.tv_sec%60);
	return buf;
}


void ICACHE_FLASH_ATTR ntp_to_tv(uint8_t ntp[8], struct timeval *tv)
{
uint64_t aux = 0;

	tv->tv_sec = ntohl(*(uint32_t*)ntp) - OFFSET;

	aux = ntohl(*(uint32_t*)&ntp[4]);

	// aux is the NTP fraction (0..2^32-1)
	aux *= 1000000; // multiply by 1e6 
	aux >>= 32;     // and divide by 2^32
	tv->tv_usec = (uint32_t)aux;
}


LOCAL void ICACHE_FLASH_ATTR ntp_dns_found(const char *name, ip_addr_t *ipaddr, void *arg) {
struct espconn *pespconn = (struct espconn *)arg;

	if (ipaddr != NULL) {
	    os_printf("Got NTP server: %d.%d.%d.%d\r\n", IP2STR(ipaddr));
	    // Call the NTP update
	    ntp_server_ip.addr = ipaddr->addr;
	    ntp_get_time(); 
	}
}


static void ICACHE_FLASH_ATTR ntp_udp_timeout(void *arg) {
	
	os_timer_disarm(&ntp_timeout);
	os_printf("NTP timout\r\n");

	// clean up connection
	if (pCon) {
		espconn_delete(pCon);
		os_free(pCon->proto.udp);
		os_free(pCon);
		pCon = 0;
	}
}


static void ICACHE_FLASH_ATTR ntp_udp_recv(void *arg, char *pdata, unsigned short len) {
ntp_t *ntp;

struct timeval tv;
int32_t hh, mm, ss;

	get_cur_time(&tv);

	// get the according sys_time;
	t_offset = get_long_systime();

	os_timer_disarm(&ntp_timeout);

	// extract ntp time
	ntp = (ntp_t*)pdata;

	ntp_to_tv(ntp->trans_time, &t_tv);

	os_printf("NTP resync - diff: %d usecs\r\n", t_tv.tv_usec-tv.tv_usec);
/*
	ss = t_tv.tv_sec%60;
	mm = (t_tv.tv_sec/60)%60;
	hh = (t_tv.tv_sec/3600)%24;
	os_printf("time: %2d:%02d:%02d\r\n", hh, mm, ss);
*/
	// clean up connection
	if (pCon) {
		espconn_delete(pCon);
		os_free(pCon->proto.udp);
		os_free(pCon);
		pCon = 0;
	}
}


void ICACHE_FLASH_ATTR ntp_set_server(uint8_t *ntp_server) {

	ntp_server_ip.addr = 0;

	// invalid arg?
	if (ntp_server == NULL)
	   return;

	// ip or DNS name?
	if (UTILS_IsIPV4(ntp_server)) {
	   // read address
	   UTILS_StrToIP(ntp_server, &ntp_server_ip);
	   ntp_get_time(); 
	} else {
	   // call DNS and wait for callback
	   espconn_gethostbyname(&pConDNS, ntp_server, &ntp_server_ip, ntp_dns_found);
	}
}


void ICACHE_FLASH_ATTR ntp_get_time() {
ntp_t ntp;

	// either ongoing request or invalid ip?
	if (pCon != 0 || ntp_server_ip.addr == 0)
	   return;

	// set up the udp "connection"
	pCon = (struct espconn*)os_zalloc(sizeof(struct espconn));
	pCon->type = ESPCONN_UDP;
	pCon->state = ESPCONN_NONE;
	pCon->proto.udp = (esp_udp*)os_zalloc(sizeof(esp_udp));
	pCon->proto.udp->local_port = espconn_port();
	pCon->proto.udp->remote_port = 123;
	os_memcpy(pCon->proto.udp->remote_ip, &ntp_server_ip, 4);

	// create a really simple ntp request packet
	os_memset(&ntp, 0, sizeof(ntp_t));
	ntp.options = 0b00011011; // leap = 0, version = 3, mode = 3 (client)

	// set timeout timer
	os_timer_disarm(&ntp_timeout);
	os_timer_setfn(&ntp_timeout, (os_timer_func_t*)ntp_udp_timeout, pCon);
	os_timer_arm(&ntp_timeout, NTP_TIMEOUT_MS, 0);

	// send the ntp request
	espconn_create(pCon);
	espconn_regist_recvcb(pCon, ntp_udp_recv);
	espconn_sent(pCon, (uint8*)&ntp, sizeof(ntp_t));
}
