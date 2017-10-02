/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Martin d'Allens <martin.dallens@gmail.com> wrote this file. As long as you retain
 * this notice you can do whatever you want with this stuff. If we meet some day,
 * and you think this stuff is worth it, you can buy me a beer in return.
 * ----------------------------------------------------------------------------
 */

#ifndef HTTPCLIENT_H
#define HTTPCLIENT_H

//#include <espmissingincludes.h> // This can remove some warnings depending on your project setup. It is safe to remove this line.

#define HTTP_STATUS_GENERIC_ERROR  -1   // In case of TCP or DNS error the callback is called with this status.
#define BUFFER_SIZE_MAX            5000 // Size of http responses that will cause an error.

/*
 * "full_response" is a string containing all response headers and the response body.
 * "response_body and "http_status" are extracted from "full_response" for convenience.
 *
 * A successful request corresponds to an HTTP status code of 200 (OK).
 * More info at http://en.wikipedia.org/wiki/List_of_HTTP_status_codes
 */
typedef void (* http_callback)(char * response_body, int http_status, char * response_headers, int body_size);

/*
 * Download a web page from its URL.
 * Try:
 * http_get("http://wtfismyip.com/text", http_callback_example);
 */
void ICACHE_FLASH_ATTR http_get(const char * url, const char * headers, http_callback user_callback);

/*
 * Post data to a web form.
 * The data should be encoded as application/x-www-form-urlencoded.
 * Try:
 * http_post("http://httpbin.org/post", "first_word=hello&second_word=world", http_callback_example);
 */
void ICACHE_FLASH_ATTR http_post(const char * url, const char * post_data, const char * headers, http_callback user_callback);

/*
 * Call this function to skip URL parsing if the arguments are already in separate variables.
 */
void ICACHE_FLASH_ATTR http_raw_request(const char * hostname, int port, bool secure, const char * path, const char * post_data, const char * headers, http_callback user_callback);

/*
 * Output on the UART.
 */
void ICACHE_FLASH_ATTR http_callback_example(char * response_body, int http_status, char * response_headers, int body_size);

#endif

