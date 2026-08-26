/*
 * SPDX-FileCopyrightText: 2021-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <sys/param.h>
#include <inttypes.h>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_check.h"
#include "esp_netif.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "dns_server.h"

#define DNS_PORT (53)
#define DNS_MAX_LEN (256)

#define OPCODE_MASK (0x7800)
#define QR_FLAG (1 << 7)
#define QD_TYPE_A (0x0001)
#define ANS_TTL_SEC (300)

static const char *TAG = "example_dns_redirect_server";

// DNS Header Packet
typedef struct __attribute__((__packed__))
{
    uint16_t id;
    uint16_t flags;
    uint16_t qd_count;
    uint16_t an_count;
    uint16_t ns_count;
    uint16_t ar_count;
} dns_header_t;

// DNS Question Packet
typedef struct {
    uint16_t type;
    uint16_t class;
} dns_question_t;

// DNS Answer Packet
typedef struct __attribute__((__packed__))
{
    uint16_t ptr_offset;
    uint16_t type;
    uint16_t class;
    uint32_t ttl;
    uint16_t addr_len;
    uint32_t ip_addr;
} dns_answer_t;

// DNS server handle
// `started` is volatile (local change vs. upstream): stop_dns_server() clears
// it from another task while dns_server_task() polls it, and without volatile
// the compiler may cache the read across the recvfrom() loop.
struct dns_server_handle {
    volatile bool started;
    TaskHandle_t volatile task; // volatile: cleared by the task, polled by stop_dns_server()
    int num_of_entries;
    dns_entry_pair_t entry[];
};

/*
    Parse the name from the packet from the DNS name format to a regular .-seperated name
    returns the pointer to the next part of the packet
*/
static char *parse_dns_name(char *raw_name, char *parsed_name, size_t parsed_name_max_len)
{

    char *label = raw_name;
    char *name_itr = parsed_name;
    int name_len = 0;

    do {
        int sub_name_len = *label;
        // (len + 1) since we are adding  a '.'
        name_len += (sub_name_len + 1);
        if (name_len > parsed_name_max_len) {
            return NULL;
        }

        // Copy the sub name that follows the the label
        memcpy(name_itr, label + 1, sub_name_len);
        name_itr[sub_name_len] = '.';
        name_itr += (sub_name_len + 1);
        label += sub_name_len + 1;
    } while (*label != 0);

    // Terminate the final string, replacing the last '.'
    parsed_name[name_len - 1] = '\0';
    // Return pointer to first char after the name
    return label + 1;
}

// Parses the DNS request and prepares a DNS response with the IP of the softAP
static int parse_dns_request(char *req, size_t req_len, char *dns_reply, size_t dns_reply_max_len, dns_server_handle_t h)
{
    if (req_len > dns_reply_max_len) {
        return -1;
    }

    // Prepare the reply
    memset(dns_reply, 0, dns_reply_max_len);
    memcpy(dns_reply, req, req_len);

    // Endianess of NW packet different from chip
    dns_header_t *header = (dns_header_t *)dns_reply;
    ESP_LOGD(TAG, "DNS query with header id: 0x%X, flags: 0x%X, qd_count: %d",
             ntohs(header->id), ntohs(header->flags), ntohs(header->qd_count));

    // Not a standard query
    if ((header->flags & OPCODE_MASK) != 0) {
        return 0;
    }

    // Set question response flag
    header->flags |= QR_FLAG;

    uint16_t qd_count = ntohs(header->qd_count);

    // Upper-bound size check only - an_count/actual reply length are set
    // below from how many questions we actually answer, not qd_count. The
    // original code set an_count = qd_count unconditionally, which lies for
    // any question we skip (AAAA/non-A queries, or names with no matching
    // rule): the client sees "N answers" but reads uninitialized/zeroed
    // bytes for the ones we never filled in. Reproduced this: `dig` against
    // this server for a domain also queried as AAAA reported FORMERR.
    int max_reply_len = qd_count * sizeof(dns_answer_t) + req_len;
    if (max_reply_len > dns_reply_max_len) {
        return -1;
    }

    // Pointer to current answer and question
    char *cur_ans_ptr = dns_reply + req_len;
    char *cur_qd_ptr = dns_reply + sizeof(dns_header_t);
    char name[128];
    uint16_t answered = 0;

    // Respond to all questions based on configured rules
    for (int qd_i = 0; qd_i < qd_count; qd_i++) {
        char *name_end_ptr = parse_dns_name(cur_qd_ptr, name, sizeof(name));
        if (name_end_ptr == NULL) {
            ESP_LOGE(TAG, "Failed to parse DNS question: %s", cur_qd_ptr);
            return -1;
        }

        dns_question_t *question = (dns_question_t *)(name_end_ptr);
        uint16_t qd_type = ntohs(question->type);
        uint16_t qd_class = ntohs(question->class);

        ESP_LOGI(TAG, "Received type: %d | Class: %d | Question for: %s", qd_type, qd_class, name);

        if (qd_type != QD_TYPE_A) {
            ESP_LOGI(TAG, "Not answering '%s' - non-A query type %d (e.g. AAAA)", name, qd_type);
        } else {
            esp_ip4_addr_t ip = { .addr = IPADDR_ANY };
            // Check the configured rules to decide whether to answer this question or not
            for (int i = 0; i < h->num_of_entries; ++i) {
                // check if the name either corresponds to the entry, or if we should answer to all queries ("*")
                if (strcmp(h->entry[i].name, "*") == 0 || strcmp(h->entry[i].name, name) == 0) {
                    if (h->entry[i].if_key) {
                        esp_netif_ip_info_t ip_info;
                        esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey(h->entry[i].if_key), &ip_info);
                        ip.addr = ip_info.ip.addr;
                        break;
                    } else if (h->entry->ip.addr != IPADDR_ANY) {
                        ip.addr = h->entry[i].ip.addr;
                        break;
                    }
                }
            }
            if (ip.addr == IPADDR_ANY) {    // no rule applies, continue with another question
                ESP_LOGI(TAG, "No matching rule for '%s' (type %d) - not answering", name, qd_type);
                continue;
            }
            dns_answer_t *answer = (dns_answer_t *)cur_ans_ptr;

            answer->ptr_offset = htons(0xC000 | (cur_qd_ptr - dns_reply));
            answer->type = htons(qd_type);
            answer->class = htons(qd_class);
            answer->ttl = htonl(ANS_TTL_SEC);

            ESP_LOGI(TAG, "Answering '%s' with IP " IPSTR, name, IP2STR(&ip));

            answer->addr_len = htons(sizeof(ip.addr));
            answer->ip_addr = ip.addr;

            cur_ans_ptr += sizeof(dns_answer_t);
            answered++;
        }
    }
    header->an_count = htons(answered);
    return answered * sizeof(dns_answer_t) + req_len;
}

/*
    Sets up a socket and listen for DNS queries,
    replies to all type A queries with the IP of the softAP
*/
void dns_server_task(void *pvParameters)
{
    char rx_buffer[128];
    char addr_str[128];
    int addr_family;
    int ip_protocol;
    dns_server_handle_t handle = pvParameters;

    while (handle->started) {

        struct sockaddr_in dest_addr;
        dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(DNS_PORT);
        addr_family = AF_INET;
        ip_protocol = IPPROTO_IP;
        inet_ntoa_r(dest_addr.sin_addr, addr_str, sizeof(addr_str) - 1);

        int sock = socket(addr_family, SOCK_DGRAM, ip_protocol);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Socket created");

        int err = bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (err < 0) {
            ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        }
        ESP_LOGI(TAG, "Socket bound, port %d", DNS_PORT);

        // Local change vs. upstream: a receive timeout, so the loop re-checks
        // handle->started periodically instead of blocking in recvfrom()
        // forever. Upstream's stop_dns_server() is a bare vTaskDelete() on a
        // task that is almost always inside that recvfrom() - killing a task
        // mid-syscall inside lwIP leaves the lwIP core lock held, and the next
        // esp_wifi_stop()/esp_wifi_set_mode() then deadlocks (observed on
        // hardware as a device that never completed its reboot). This timeout
        // plus the EWOULDBLOCK check below is what makes a graceful stop
        // possible.
        struct timeval rcvto = { .tv_sec = 0, .tv_usec = 250 * 1000 };
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &rcvto, sizeof(rcvto));

        while (handle->started) {
            ESP_LOGI(TAG, "Waiting for data");
            struct sockaddr_in6 source_addr; // Large enough for both IPv4 or IPv6
            socklen_t socklen = sizeof(source_addr);
            int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&source_addr, &socklen);

            // Receive timeout (see the SO_RCVTIMEO above, local change): not an
            // error, just the periodic chance to notice handle->started went
            // false and exit cleanly.
            if (len < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)) {
                continue;
            }
            // Error occurred during receiving
            if (len < 0) {
                ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
                // Deliberately no close() here - breaking out reaches the
                // shutdown()/close() below, and closing twice would hand the
                // same descriptor number back to lwIP's recycler and then tear
                // down whatever took it in the meantime (e.g. an in-flight HTTP
                // connection on the setup AP).
                break;
            }
            // Data received
            else {
                // Get the sender's ip address as string
                if (source_addr.sin6_family == PF_INET) {
                    inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr.s_addr, addr_str, sizeof(addr_str) - 1);
                } else if (source_addr.sin6_family == PF_INET6) {
                    inet6_ntoa_r(source_addr.sin6_addr, addr_str, sizeof(addr_str) - 1);
                }

                // Null-terminate whatever we received and treat like a string...
                rx_buffer[len] = 0;

                char reply[DNS_MAX_LEN];
                int reply_len = parse_dns_request(rx_buffer, len, reply, DNS_MAX_LEN, handle);

                ESP_LOGI(TAG, "Received %d bytes from %s | DNS reply with len: %d", len, addr_str, reply_len);
                if (reply_len <= 0) {
                    ESP_LOGE(TAG, "Failed to prepare a DNS reply");
                } else {
                    int err = sendto(sock, reply, reply_len, 0, (struct sockaddr *)&source_addr, sizeof(source_addr));
                    if (err < 0) {
                        ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                        break;
                    }
                }
            }
        }

        if (sock != -1) {
            ESP_LOGE(TAG, "Shutting down socket");
            shutdown(sock, 0);
            close(sock);
        }
    }
    // Local change vs. upstream: signal stop_dns_server() that this task is
    // done with the handle before self-deleting, so it knows when free() is
    // safe. Must be the last touch of `handle`.
    handle->task = NULL;
    vTaskDelete(NULL);
}

dns_server_handle_t start_dns_server(dns_server_config_t *config)
{
    dns_server_handle_t handle = calloc(1, sizeof(struct dns_server_handle) + config->num_of_entries * sizeof(dns_entry_pair_t));
    ESP_RETURN_ON_FALSE(handle, NULL, TAG, "Failed to allocate dns server handle");

    handle->started = true;
    handle->num_of_entries = config->num_of_entries;
    memcpy(handle->entry, config->item, config->num_of_entries * sizeof(dns_entry_pair_t));

    // Local temp (local change vs. upstream): handle->task is volatile now, and
    // xTaskCreate wants a plain TaskHandle_t*.
    TaskHandle_t task = NULL;
    xTaskCreate(dns_server_task, "dns_server", 4096, handle, 5, &task);
    handle->task = task;
    return handle;
}

void stop_dns_server(dns_server_handle_t handle)
{
    if (handle) {
        // Local change vs. upstream, which called vTaskDelete(handle->task)
        // here directly: that task is almost always blocked inside recvfrom(),
        // and deleting a task mid-syscall inside lwIP leaves the lwIP core
        // lock held - the next wifi call then deadlocks (observed on
        // hardware). Instead: ask the task to exit (it polls `started` every
        // 250ms via the receive timeout above), wait for it to confirm by
        // clearing handle->task, then free. The fallback delete only triggers
        // if the task fails to exit in time, in which case the upstream
        // behaviour (and its risk) is no worse than before.
        handle->started = false;
        for (int i = 0; i < 20 && handle->task != NULL; i++) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        // Snapshot once: handle->task is volatile and the task can clear it
        // between a check and a use - vTaskDelete(NULL) would delete the
        // CALLER. One read, then act only on the snapshot.
        TaskHandle_t remaining = handle->task;
        if (remaining != NULL) {
            ESP_LOGE(TAG, "dns task did not exit gracefully - deleting it");
            vTaskDelete(remaining);
        }
        free(handle);
    }
}
