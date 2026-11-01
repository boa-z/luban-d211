// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (C) 2025 Artinchip Technology Co., Ltd.
 * Authors:  wulv <lv.wu@artinchip.com>
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <arpa/inet.h>

#include "wifimanager.h"

#define SERVER_STRING "Server: artinchiphttpd/0.1.0\r\n"
#define SERVER_PORT 80
#define HTML_DEFAULT "/var/http_wificonfig/html/"
#define WIFI_CONNECT_TIMEOUT_S 15

struct stat st;

static int get_line(int sock, char *buff, int size);
static void handle_get(int sock, char *url);
static int handle_post(int sock, char *url);
int do_http(int sock);
void not_found(int sock);
void responce_headers(int sock, FILE *file);
void responce_bodys(int sock, FILE *file);
void unimplement(int sock);
void do_responce(int sock, const char *path);

enum
{
    HTTP_WIFICONFIG_ERROR = 0,
    HTTP_WIFICONFIG_WARNING,
    HTTP_WIFICONFIG_INFO,
    HTTP_WIFICONFIG_DEBUG,
};
static int debug_level = HTTP_WIFICONFIG_INFO;

void http_wificonfig_debug(int level, const char *fmt, ...)
{
    va_list args;

    if (level > debug_level)
        return;

    va_start(args, fmt);

    printf("[http_wificonfig]: ");

    vprintf(fmt, args);

    va_end(args);
}

static int get_line(int sock, char *buff, int size)
{
    int count = 0;
    char ch = '\0';
    int len = 0;

    while (count < size - 1 && ch != '\n')
    {
        len = read(sock, &ch, 1);
        if (len == 1)
        {
            if (ch == '\r')
            {
                continue;
            }
            else if (ch == '\n')
            {
                break;
            }

            buff[count] = ch;
            count++;
        }
        else if (len == -1)
        {
            http_wificonfig_debug(HTTP_WIFICONFIG_ERROR, "read error\n");
            count = -1;
            break;
        }
        else
        {
            http_wificonfig_debug(HTTP_WIFICONFIG_ERROR, "client error\n");
            count = -1;
            break;
        }
    }
    if (count >= 0)
        buff[count] = '\0';

    return count;
}

static void handle_get(int sock, char *url)
{
    int len = 0;
    char buff[512];
    char path[128];

    snprintf(path, 127, HTML_DEFAULT "%s", url);
    http_wificonfig_debug(HTTP_WIFICONFIG_DEBUG, "method: GET \n ");

    do
    {
        len = get_line(sock, buff, sizeof(buff));
        http_wificonfig_debug(HTTP_WIFICONFIG_ERROR, "%s\n", buff);
    } while (len > 0);

    if (stat(path, &st) == -1)
    {
        http_wificonfig_debug(HTTP_WIFICONFIG_ERROR, "The file: %s  is not exist!\n", path);
        not_found(sock);
    }
    else
    {
        do_responce(sock, path);
    }
}

static void bad_request(int client)
{
    char buf[128];

    snprintf(buf, 127, "HTTP/1.0 400 BAD REQUEST\r\n");
    send(client, buf, sizeof(buf), 0);
    snprintf(buf, 127, "Content-type: text/html\r\n");
    send(client, buf, sizeof(buf), 0);
    snprintf(buf, 127, "\r\n");
    send(client, buf, sizeof(buf), 0);
    snprintf(buf, 127, "<P>Your browser sent a bad request, ");
    send(client, buf, sizeof(buf), 0);
    snprintf(buf, 127, "such as a POST without a Content-Length.\r\n");
    send(client, buf, sizeof(buf), 0);
}

static int hex_to_char(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

static char *url_decode(const char *str)
{
    if (str == NULL)
        return NULL;

    size_t len = strlen(str);

    char *decoded = (char *)malloc(len + 1);
    if (decoded == NULL)
        return NULL;

    size_t i = 0, j = 0;
    while (str[i] != '\0')
    {
        if (str[i] == '%' && str[i + 1] != '\0' && str[i + 2] != '\0')
        {
            int high = hex_to_char(str[i + 1]);
            int low = hex_to_char(str[i + 2]);
            if (high != -1 && low != -1)
            {
                decoded[j++] = (char)((high << 4) | low);
                i += 3;
                continue;
            }
            else
            {
                decoded[j++] = str[i++];
            }
        }
        else
        {
            decoded[j++] = str[i++];
        }
    }
    decoded[j] = '\0';
    return decoded;
}

static int parse_wificonfig_info(char *buff, char *ssid, char *password)
{
    char *ptr = NULL;
    int i;
    char *decode = NULL;

    decode = url_decode(buff);
    if (!decode)
    {
        http_wificonfig_debug(HTTP_WIFICONFIG_ERROR, "decode error\n");
        return -1;
    }

    ptr = strstr(decode, "ssid=");
    if (ptr == NULL)
    {
        http_wificonfig_debug(HTTP_WIFICONFIG_ERROR, "Can't get ssid info\n");
        free(decode);
        return -1;
    }
    ptr += 5;

    for (i = 0; *ptr != '&' && i < 64; i++)
    {
        ssid[i] = *ptr;
        ptr++;
    }
    ssid[i] = '\0';

    ptr = strstr(ptr, "password=");
    if (ptr == NULL)
    {
        http_wificonfig_debug(HTTP_WIFICONFIG_ERROR, "Can't get password info\n");
        free(decode);
        return -1;
    }
    ptr += 9;
    for (i = 0; *ptr != '\0' && i < 64; i++)
    {
        password[i] = *ptr;
        ptr++;
    }
    password[i] = '\0';
    http_wificonfig_debug(HTTP_WIFICONFIG_INFO, "ssid = %s\n password = %s\n", ssid, password);

    free(decode);
    return 0;
}

static int handle_post(int sock, char *url)
{
    int len = 0;
    char buff[128];
    char *ptr = NULL;
    char ssid[32];
    char password[64];
    int content_length = -1;
    int ret = 0;

    http_wificonfig_debug(HTTP_WIFICONFIG_INFO, "method: POST \n ");

    do
    {
        len = get_line(sock, buff, sizeof(buff));
        if (len < 0)
            return 0;

        if (len == 0 && content_length != -1)
        {
            http_wificonfig_debug(HTTP_WIFICONFIG_DEBUG, "content_length == %d\n", content_length);
            len = recv(sock, buff, content_length, 0);
            if (len <= 0)
            {
                http_wificonfig_debug(HTTP_WIFICONFIG_ERROR, "recv post request body filed\n");
                content_length = -1;
            }
            else
            {
                buff[content_length] = '\0';
            }

            http_wificonfig_debug(HTTP_WIFICONFIG_DEBUG, "POST:body: %s\n", buff);
            break;
        }

        http_wificonfig_debug(HTTP_WIFICONFIG_DEBUG, "%s\n", buff);

        ptr = strstr(buff, "Content-Length:");
        if (ptr != NULL)
        {
            content_length = atoi(&(ptr[16]));
        }

    } while (len > 0);

    if (content_length == -1)
    {
        bad_request(sock);
        return 0;
    }

    ret = parse_wificonfig_info(buff, ssid, password);
    if (ret != 0)
    {
        bad_request(sock);
        return 0;
    }

    wifi_status_t status;

    memset(&status, 0, sizeof(wifi_status_t));

    int timeout = WIFI_CONNECT_TIMEOUT_S;
    char new_path[64];
    wifimanager_connect(ssid, password);
    do
    {
        sleep(1);
        wifimanager_get_status(&status);
        if (status.state == WIFI_STATE_GOT_IP || status.state == WIFI_STATE_CONNECTED)
            break;
    } while (timeout-- > 0);

    if (timeout > 0)
        snprintf(new_path, 63, HTML_DEFAULT "success_%s", url);
    else
        snprintf(new_path, 63, HTML_DEFAULT "fail_%s", url);

    http_wificonfig_debug(HTTP_WIFICONFIG_DEBUG, "path:%s\n", new_path);

    if (stat(new_path, &st) == -1)
    {
        http_wificonfig_debug(HTTP_WIFICONFIG_ERROR, "The file: %s  is not exist!\n", new_path);
        not_found(sock);
    }

    do_responce(sock, new_path);

    if (timeout > 0)
    {
        sleep(10);
        system("killall hostapd");
        system("killall udhcpd");

        return 1;
    }

    return 0;
}

int do_http(int sock)
{
    int len = 0;
    char buff[512];
    char url[256];
    char method[64];
    char path[256];

    len = get_line(sock, buff, sizeof(buff));
    int i = 0, j = 0;
    if (len > 0)
    {
        while (!isspace(buff[j]) && i < sizeof(method) - 1)
        {
            method[i] = buff[j];
            i++;
            j++;
        }
        method[i] = '\0';
        http_wificonfig_debug(HTTP_WIFICONFIG_DEBUG, "request method:%s\n", method);
    }

    while (isspace(buff[j++])) {}
    i = 0;

    while (!isspace(buff[j]) && i < sizeof(url) - 1)
    {
        url[i] = buff[j];
        i++;
        j++;
    }
    url[i] = '\0';

    char *p = strchr(url, '?');
    if (p)
        *p = '\0';
    if (url[0] == '\0' || url[0] == '/')
        strcat(url, "index.html");
    http_wificonfig_debug(HTTP_WIFICONFIG_INFO, "actual url:%s\n", url);

    if (strncasecmp(method, "GET", i) == 0)
    {
        handle_get(sock, url);
    }
    else if (strncasecmp(method, "POST", i) == 0)
    {
        return handle_post(sock, url);
    }
    else
    {
        fprintf(stderr, "warning! other request:%s\n !", method);
        do
        {
            len = get_line(sock, buff, sizeof(buff));
            http_wificonfig_debug(HTTP_WIFICONFIG_DEBUG, "read:%s\n", buff);
        } while (len > 0);
        unimplement(sock);
    }
    return 0;
}

/**********************************************************************/
/* Give a client a 404 not found status message. */
/**********************************************************************/
void not_found(int sock)
{
    char buf[128];

    snprintf(buf, 127, "HTTP/1.0 404 NOT FOUND\r\n");
    send(sock, buf, strlen(buf), 0);
    snprintf(buf, 127, SERVER_STRING);
    send(sock, buf, strlen(buf), 0);
    snprintf(buf, 127, "Content-Type: text/html\r\n");
    send(sock, buf, strlen(buf), 0);
    snprintf(buf, 127, "\r\n");
    send(sock, buf, strlen(buf), 0);
    snprintf(buf, 127, "<HTML><TITLE>Not Found</TITLE>\r\n");
    send(sock, buf, strlen(buf), 0);
    snprintf(buf, 127, "<BODY><P>The server could not fulfill\r\n");
    send(sock, buf, strlen(buf), 0);
    snprintf(buf, 127, "your request because the resource specified\r\n");
    send(sock, buf, strlen(buf), 0);
    snprintf(buf, 127, "is unavailable or nonexistent.\r\n");
    send(sock, buf, strlen(buf), 0);
    snprintf(buf, 127, "</BODY></HTML>\r\n");
    send(sock, buf, strlen(buf), 0);
}

void responce_headers(int sock, FILE *file)
{
    struct stat st1;
    char buff[1024] = {0};
    char temp[64];
    const char *head = "HTTP/1.0 200 OK\r\nServer:ArtInChip Private Server\r\nContent-Type: text/html\r\nConnection: Close\r\n";

    strcpy(buff, head);

    int fd = fileno(file);
    fstat(fd, &st1);

    int size = st1.st_size;
    snprintf(temp, 63, "Content-Length:%d\r\n\r\n", size);

    strcat(buff, temp);

    write(sock, buff, strlen(buff));
    http_wificonfig_debug(HTTP_WIFICONFIG_DEBUG, "%s\n", buff);
}

void responce_bodys(int sock, FILE *file)
{
    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);

    rewind(file);

    char *buffer = (char *)malloc(fileSize);
    if (buffer == NULL)
    {
        perror("Failed to allocate memory");
        return;
    }

    fread(buffer, 1, fileSize, file);
    write(sock, buffer, fileSize);
    free(buffer);
}

void unimplement(int sock)
{
    char buf[128];

    snprintf(buf, 127, "HTTP/1.0 501 Method Not Implemented\r\n");
    send(sock, buf, strlen(buf), 0);
    snprintf(buf, 127, SERVER_STRING);
    send(sock, buf, strlen(buf), 0);
    snprintf(buf, 127, "Content-Type: text/html\r\n");
    send(sock, buf, strlen(buf), 0);
    snprintf(buf, 127, "\r\n");
    send(sock, buf, strlen(buf), 0);
    snprintf(buf, 127, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
    send(sock, buf, strlen(buf), 0);
    snprintf(buf, 127, "</TITLE></HEAD>\r\n");
    send(sock, buf, strlen(buf), 0);
    snprintf(buf, 127, "<BODY><P>HTTP request method not supported.\r\n");
    send(sock, buf, strlen(buf), 0);
    snprintf(buf, 127, "</BODY></HTML>\r\n");
    send(sock, buf, strlen(buf), 0);
}

void do_responce(int sock, const char *path)
{
    FILE *resource = NULL;

    resource = fopen(path, "r");
    if (resource == NULL)
    {
        http_wificonfig_debug(HTTP_WIFICONFIG_ERROR, "open file %s\n  error!\n", path);
        not_found(sock);
        return;
    }

    responce_headers(sock, resource);

    responce_bodys(sock, resource);

    fclose(resource);
}

static const char *wifistate2string(wifistate_t state)
{
    switch (state)
    {
        case WIFI_STATE_GOT_IP:
            return "WIFI_STATE_GOT_IP";
        case WIFI_STATE_CONNECTING:
            return "WIFI_STATE_CONNECTING";
        case WIFI_STATE_DHCPC_REQUEST:
            return "WIFI_STATE_DHCPC_REQUEST";
        case WIFI_STATE_DISCONNECTED:
            return "WIFI_STATE_DISCONNECTED";
        case WIFI_STATE_CONNECTED:
            return "WIFI_STATE_CONNECTED";
        default:
            return "WIFI_STATE_ERROR";
    }
}

static const char *disconn_reason2string(wifimanager_disconn_reason_t reason)
{
    switch (reason)
    {
        case AUTO_DISCONNECT:
            return "wpa auto disconnect";
        case ACTIVE_DISCONNECT:
            return "active disconnect";
        case KEYMT_NO_SUPPORT:
            return "keymt is not supported";
        case CMD_OR_PARAMS_ERROR:
            return "wpas command error";
        case IS_CONNECTTING:
            return "wifi is still connecting";
        case CONNECT_TIMEOUT:
            return "connect timeout";
        case REQUEST_IP_TIMEOUT:
            return "request ip address timeout";
        case WPA_TERMINATING:
            return "wpa_supplicant is closed";
        case AP_ASSOC_REJECT:
            return "AP assoc reject";
        case NETWORK_NOT_FOUND:
            return "can't search such ssid";
        case PASSWORD_INCORRECT:
            return "incorrect password";
        default:
            return "other reason";
    }
}

static void print_scan_result(char *result)
{
    http_wificonfig_debug(HTTP_WIFICONFIG_INFO, "%s\n", result);
}

static void print_stat_change(wifistate_t stat, wifimanager_disconn_reason_t reason)
{
    http_wificonfig_debug(HTTP_WIFICONFIG_INFO, "%s\n", wifistate2string(stat));
    if (stat == WIFI_STATE_DISCONNECTED)
        http_wificonfig_debug(HTTP_WIFICONFIG_INFO, "disconnect reason: %s\n", disconn_reason2string(reason));
}

int main(int argc, char *argv[])
{
    int server_sock = -1;
    struct sockaddr_in server_addr;
    int ret;
    int on = 1;
    wifimanager_cb_t cb = {
        .scan_result_cb = print_scan_result,
        .stat_change_cb = print_stat_change,
    };

    bzero(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(SERVER_PORT);

    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0)
    {
        http_wificonfig_debug(HTTP_WIFICONFIG_ERROR, "socket failed\n");
        return -1;
    }

    if ((setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &on, sizeof(on))) < 0)
    {
        http_wificonfig_debug(HTTP_WIFICONFIG_ERROR, "setsockopt failed\n");
        close(server_sock);
        return -1;
    }
    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        http_wificonfig_debug(HTTP_WIFICONFIG_ERROR, "bind failed\n");
        close(server_sock);
        return -1;
    }
    if (listen(server_sock, 5) < 0)
    {
        http_wificonfig_debug(HTTP_WIFICONFIG_ERROR, "listen failed\n");
        close(server_sock);
        return -1;
    }

    wifimanager_init(&cb);

    system("hostapd -d /etc/http_wificonfig/hostapd.conf -B");
    // sleep(1);
    system("ifconfig wlan1 192.168.1.1");
    system("udhcpd /etc/http_wificonfig/udhcpd.conf");

    while (1)
    {
        struct sockaddr_in client_addr;
        int client_sock;
        char buff[64];
        int client_length = sizeof(client_addr);

        client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_length);
        if (client_sock == -1)
        {
            http_wificonfig_debug(HTTP_WIFICONFIG_ERROR, "accept error\n");
            close(server_sock);
            return -1;
        }

        http_wificonfig_debug(HTTP_WIFICONFIG_INFO, "client ip:%s\t port:%d\n ",
                              inet_ntop(AF_INET, &client_addr.sin_addr.s_addr, buff, sizeof(buff)),
                              ntohs(client_addr.sin_port));
        ret = do_http(client_sock);
        close(client_sock);

        /* wifi configration success, exit! */
        if (ret)
        {
            close(server_sock);
            break;
        }
    }

    return 0;
}
