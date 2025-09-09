#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "lwip/sockets.h"
#include <errno.h>
#include <lwip/netdb.h>
#include <stdlib.h>
#include <string.h>

#include "esp_jpeg_common.h"
#include "esp_jpeg_dec.h"

#include "esp_heap_caps.h"
#include "ui_image_transfer.h"
#include "wifi_image_transfer.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include <unistd.h>


static const char* TAG = "WIFI_IMG_TRANSFER";

#define LISTEN_SOCKET_NUM 1
#define TCP_RECV_BUF_SIZE 4096
#define MAX_JPEG_FRAME_SIZE (100 * 1024) // Max expected JPEG frame size
#define FRAME_QUEUE_SIZE 5              // Increased queue size

// Structure to hold frame data
typedef struct {
    uint8_t* data;
    size_t size;
} frame_data_t;

static TaskHandle_t s_tcp_server_task_handle = NULL;
static TaskHandle_t s_jpeg_decode_task_handle = NULL;
static bool s_server_running = false;
static int s_listen_sock = -1;
static QueueHandle_t s_frame_queue = NULL;

// Double buffer for decoded image
static uint8_t* s_frame_buffer_1 = NULL;
static uint8_t* s_frame_buffer_2 = NULL;
static uint8_t* s_active_frame_buffer = NULL; // Buffer for decoding
static uint8_t* s_display_frame_buffer = NULL; // Buffer for display
static int s_frame_width = 0;
static int s_frame_height = 0;
static SemaphoreHandle_t s_frame_mutex = NULL;
static EventGroupHandle_t s_ui_event_group = NULL;

// FPS calculation
static volatile uint32_t s_frame_count = 0;
static uint32_t s_last_tick = 0;
static float s_fps = 0.0f;

#define FRAME_READY_BIT (1 << 0)

// Forward declarations
static void handle_decoded_image(uint8_t* img_buf, int width, int height);
static void tcp_recv_task(void* pvParameters);
static void jpeg_decode_task(void* pvParameters);
static void cleanup_resources(void);

// Helper function to find a byte sequence in a buffer
static int find_marker(const uint8_t* buffer, int size, const uint8_t* marker, int marker_size)
{
    for (int i = 0; i <= size - marker_size; i++) {
        if (memcmp(buffer + i, marker, marker_size) == 0) {
            return i;
        }
    }
    return -1;
}

static void tcp_recv_task(void* pvParameters) {
    uint16_t port = *(uint16_t*)pvParameters;
    char addr_str[128];
    int addr_family = AF_INET;
    int ip_protocol = IPPROTO_IP;
    struct sockaddr_in dest_addr;

    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port);
    ip_protocol = IPPROTO_IP;

    s_listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (s_listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        cleanup_resources();
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Socket created");

    // Set socket options to allow address reuse
    int opt = 1;
    if (setsockopt(s_listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        ESP_LOGE(TAG, "Failed to set SO_REUSEADDR: errno %d", errno);
        close(s_listen_sock);
        cleanup_resources();
        vTaskDelete(NULL);
        return;
    }

    // Also set SO_REUSEPORT if available (helps with port reuse)
    opt = 1;
    if (setsockopt(s_listen_sock, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
        ESP_LOGW(TAG, "Failed to set SO_REUSEPORT (may not be supported): errno %d", errno);
        // This is not critical, continue anyway
    }

    int err = bind(s_listen_sock, (struct sockaddr*)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        close(s_listen_sock);
        cleanup_resources();
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Socket bound, port %d", port);

    err = listen(s_listen_sock, LISTEN_SOCKET_NUM);
    if (err != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        close(s_listen_sock);
        cleanup_resources();
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Socket listening");

    s_server_running = true;

    while (s_server_running) {
        struct sockaddr_in source_addr;
        socklen_t addr_len = sizeof(source_addr);
        int sock = accept(s_listen_sock, (struct sockaddr*)&source_addr, &addr_len);
        if (sock < 0) {
            if (s_server_running) {
                ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            }
            // If server is stopping, this is expected - don't log error
            continue;
        }

        // Convert ip address to string
        if (addr_family == AF_INET) {
            inet_ntoa_r(source_addr.sin_addr.s_addr, addr_str, sizeof(addr_str) - 1);
        }
        ESP_LOGI(TAG, "Socket accepted IP address: %s", addr_str);

        // Set socket timeout to detect disconnections
        struct timeval timeout;
        timeout.tv_sec = 5; // 5 second timeout for receiving data
        timeout.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        uint8_t rx_buffer[TCP_RECV_BUF_SIZE];
        uint8_t* jpeg_frame_buffer = (uint8_t*)heap_caps_malloc(MAX_JPEG_FRAME_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (jpeg_frame_buffer == NULL) {
            ESP_LOGE(TAG, "Failed to allocate JPEG frame buffer");
            close(sock);
            continue;
        }

        int jpeg_frame_pos = 0;
        int len;
        const uint8_t soi_marker[] = {0xFF, 0xD8};
        const uint8_t eoi_marker[] = {0xFF, 0xD9};

        while (s_server_running) {
            len = recv(sock, rx_buffer, sizeof(rx_buffer), 0);
            if (len < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    ESP_LOGW(TAG, "Receive timeout, closing connection.");
                } else {
                    ESP_LOGE(TAG, "Error occurred during receive: errno %d", errno);
                }
                break; // Connection lost or timed out
            } else if (len == 0) {
                ESP_LOGW(TAG, "Connection closed by client");
                break;
            }

            // Append new data to buffer
            if (jpeg_frame_pos + len > MAX_JPEG_FRAME_SIZE) {
                ESP_LOGE(TAG, "JPEG frame buffer overflow! Resetting buffer.");
                jpeg_frame_pos = 0;
                continue;
            }
            memcpy(jpeg_frame_buffer + jpeg_frame_pos, rx_buffer, len);
            jpeg_frame_pos += len;

            // Process all complete frames in the buffer
            int search_offset = 0;
            while (search_offset < jpeg_frame_pos) {
                // Find SOI
                int soi_pos = find_marker(jpeg_frame_buffer + search_offset, jpeg_frame_pos - search_offset, soi_marker, sizeof(soi_marker));
                if (soi_pos == -1) {
                    // No more frames in buffer, wait for more data
                    break;
                }
                soi_pos += search_offset; // Adjust to absolute position in buffer

                // Find EOI after SOI
                int eoi_pos = find_marker(jpeg_frame_buffer + soi_pos, jpeg_frame_pos - soi_pos, eoi_marker, sizeof(eoi_marker));
                if (eoi_pos == -1) {
                    // Frame is incomplete, move the partial frame to the start of the buffer and wait for more data
                    if (soi_pos > 0) {
                        memmove(jpeg_frame_buffer, jpeg_frame_buffer + soi_pos, jpeg_frame_pos - soi_pos);
                        jpeg_frame_pos -= soi_pos;
                    }
                    break;
                }
                eoi_pos += soi_pos; // Adjust to absolute position in buffer

                // Frame found, enqueue it
                int frame_size = (eoi_pos + sizeof(eoi_marker)) - soi_pos;
                frame_data_t* frame = malloc(sizeof(frame_data_t));
                if (frame) {
                    frame->data = malloc(frame_size);
                    if (frame->data) {
                        memcpy(frame->data, jpeg_frame_buffer + soi_pos, frame_size);
                        frame->size = frame_size;
                        if (xQueueSend(s_frame_queue, &frame, pdMS_TO_TICKS(10)) != pdTRUE) {
                            free(frame->data);
                            free(frame);
                            ESP_LOGW(TAG, "Frame queue full, dropping frame");
                        } else {
                            // Send ACK back to the client
                            send(sock, "ACK", 3, 0);
                        }
                    } else {
                        free(frame);
                        ESP_LOGE(TAG, "Failed to allocate frame data");
                    }
                } else {
                    ESP_LOGE(TAG, "Failed to allocate frame structure");
                }

                // Move to the position after the processed frame
                search_offset = eoi_pos + sizeof(eoi_marker);

                // Yield to prevent watchdog timeout when processing multiple frames
                vTaskDelay(1);
            }

            // Move remaining data to the start of the buffer
            if (search_offset > 0 && search_offset < jpeg_frame_pos) {
                memmove(jpeg_frame_buffer, jpeg_frame_buffer + search_offset, jpeg_frame_pos - search_offset);
                jpeg_frame_pos -= search_offset;
            } else if (search_offset >= jpeg_frame_pos) {
                // Buffer is fully processed
                jpeg_frame_pos = 0;
            }
        } // End of frame processing loop

        free(jpeg_frame_buffer);
        shutdown(sock, 0);
        close(sock);
    }

    if (s_listen_sock >= 0) {
        close(s_listen_sock);
        s_listen_sock = -1;
    }
    s_server_running = false;
    s_tcp_server_task_handle = NULL;
    vTaskDelete(NULL);
}

static void jpeg_decode_task(void* pvParameters) {
    jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
    config.output_type = JPEG_PIXEL_FORMAT_RGB565_LE; // Set output to RGB565 Little Endian for LVGL on ESP32

    jpeg_error_t dec_ret;
    jpeg_dec_handle_t jpeg_dec = NULL;
    dec_ret = jpeg_dec_open(&config, &jpeg_dec);
    if (dec_ret != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "Failed to open JPEG decoder: %d", dec_ret);
        s_jpeg_decode_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "JPEG decode task started");

    while (s_server_running) {
        frame_data_t* frame = NULL;
        
        // Wait for frame data from queue with shorter timeout for better responsiveness
        if (xQueueReceive(s_frame_queue, &frame, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (frame != NULL && frame->data != NULL && frame->size > 0) {
                
                jpeg_dec_io_t* jpeg_io = calloc(1, sizeof(jpeg_dec_io_t));
                jpeg_dec_header_info_t* out_info = calloc(1, sizeof(jpeg_dec_header_info_t));
                uint8_t* output_buffer = NULL;
                int output_len = 0;

                if (jpeg_io != NULL && out_info != NULL) {
                    // Set input buffer for complete frame
                    jpeg_io->inbuf = frame->data;
                    jpeg_io->inbuf_len = frame->size;

                    // Parse JPEG header
                    dec_ret = jpeg_dec_parse_header(jpeg_dec, jpeg_io, out_info);
                    if (dec_ret == JPEG_ERR_OK) {
                        // Allocate/reallocate double buffers if necessary
                        if (s_frame_buffer_1 == NULL || s_frame_width != out_info->width || s_frame_height != out_info->height) {
                            ESP_LOGI(TAG, "JPEG Header parsed: Width=%d, Height=%d. Allocating buffers.", out_info->width, out_info->height);
                            
                            // Free old buffers if they exist
                            if (s_frame_buffer_1) {
                                free(s_frame_buffer_1);
                                s_frame_buffer_1 = NULL;
                            }
                            if (s_frame_buffer_2) {
                                free(s_frame_buffer_2);
                                s_frame_buffer_2 = NULL;
                            }

                            // Calculate output buffer size
                            int output_len = 0;
                            if (config.output_type == JPEG_PIXEL_FORMAT_RGB565_LE ||
                                config.output_type == JPEG_PIXEL_FORMAT_RGB565_BE) {
                                output_len = out_info->width * out_info->height * 2;
                            } else if (config.output_type == JPEG_PIXEL_FORMAT_RGB888) {
                                output_len = out_info->width * out_info->height * 3;
                            }

                                                            if (output_len > 0) {
                                    // Allocate frame buffers in PSRAM with 16-byte alignment for JPEG decoder
                                    s_frame_buffer_1 = heap_caps_aligned_alloc(16, output_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                                    s_frame_buffer_2 = heap_caps_aligned_alloc(16, output_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                                if (!s_frame_buffer_1 || !s_frame_buffer_2) {
                                    ESP_LOGE(TAG, "Failed to allocate double buffers!");
                                    // Handle allocation failure
                                    if (s_frame_buffer_1) free(s_frame_buffer_1);
                                    if (s_frame_buffer_2) free(s_frame_buffer_2);
                                    s_frame_buffer_1 = s_frame_buffer_2 = NULL;
                                    continue; // Skip this frame
                                }
                                s_active_frame_buffer = s_frame_buffer_1;
                                s_display_frame_buffer = s_frame_buffer_2;
                                s_frame_width = out_info->width;
                                s_frame_height = out_info->height;
                            }
                        }

                        if (s_active_frame_buffer) {
                            jpeg_io->outbuf = s_active_frame_buffer;

                            // Decode the complete JPEG frame
                            dec_ret = jpeg_dec_process(jpeg_dec, jpeg_io);
                            if (dec_ret == JPEG_ERR_OK) {
                                handle_decoded_image(s_active_frame_buffer, out_info->width, out_info->height);
                            } else {
                                ESP_LOGE(TAG, "Failed to decode JPEG data: %d", dec_ret);
                            }
                        }
                    } else {
                        ESP_LOGE(TAG, "Failed to parse JPEG header: %d", dec_ret);
                    }
                }

                // Clean up
                if (jpeg_io) free(jpeg_io);
                if (out_info) free(out_info);
                
                // Free frame data
                free(frame->data);
                free(frame);
            }
        }
    }

    // Clean up decoder
    if (jpeg_dec) {
        jpeg_dec_close(jpeg_dec);
    }

    ESP_LOGI(TAG, "JPEG decode task stopped");
    s_jpeg_decode_task_handle = NULL;
    vTaskDelete(NULL);
}

static void handle_decoded_image(uint8_t* img_buf, int width, int height) {
    if (xSemaphoreTake(s_frame_mutex, portMAX_DELAY) == pdTRUE) {
        // The buffer we just decoded into (img_buf) is the active buffer.
        // Swap it with the display buffer.
        uint8_t* temp = s_display_frame_buffer;
        s_display_frame_buffer = s_active_frame_buffer;
        s_active_frame_buffer = temp;

        // Update frame count for FPS calculation
        s_frame_count++;

        xSemaphoreGive(s_frame_mutex);
        
        // Notify the UI that a new frame is ready
        if (s_ui_event_group) {
            xEventGroupSetBits(s_ui_event_group, FRAME_READY_BIT);
        }
    }
}

static void cleanup_resources(void)
{
    s_server_running = false; // Ensure loops in tasks will terminate

    // Close socket if it's still open
    if (s_listen_sock >= 0) {
        close(s_listen_sock);
        s_listen_sock = -1;
    }

    // Delete tasks if they are running
    if (s_tcp_server_task_handle != NULL) {
        vTaskDelete(s_tcp_server_task_handle);
        s_tcp_server_task_handle = NULL;
    }
    if (s_jpeg_decode_task_handle != NULL) {
        vTaskDelete(s_jpeg_decode_task_handle);
        s_jpeg_decode_task_handle = NULL;
    }

    // Clean up frame queue
    if (s_frame_queue != NULL) {
        frame_data_t* frame;
        while (xQueueReceive(s_frame_queue, &frame, 0) == pdTRUE) {
            if (frame) {
                if (frame->data) {
                    free(frame->data);
                }
                free(frame);
            }
        }
        vQueueDelete(s_frame_queue);
        s_frame_queue = NULL;
    }
    
    // Free double buffers
    if (s_frame_buffer_1) {
        free(s_frame_buffer_1);
        s_frame_buffer_1 = NULL;
    }
    if (s_frame_buffer_2) {
        free(s_frame_buffer_2);
        s_frame_buffer_2 = NULL;
    }
    s_active_frame_buffer = NULL;
    s_display_frame_buffer = NULL;

    // Delete semaphore and event group
    if (s_frame_mutex) {
        vSemaphoreDelete(s_frame_mutex);
        s_frame_mutex = NULL;
    }
    if (s_ui_event_group) {
        vEventGroupDelete(s_ui_event_group);
        s_ui_event_group = NULL;
    }
}

bool wifi_image_transfer_start(uint16_t port) {
    if (s_server_running) {
        ESP_LOGW(TAG, "TCP server already running.");
        return true;
    }

    // Clean up any old resources before starting
    cleanup_resources();
    s_server_running = true; // Set running state early

    // Create frame queue
    s_frame_queue = xQueueCreate(FRAME_QUEUE_SIZE, sizeof(frame_data_t*));
    if (s_frame_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create frame queue");
        cleanup_resources();
        return false;
    }

    // Create mutex and event group
    s_frame_mutex = xSemaphoreCreateMutex();
    if (s_frame_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create frame mutex");
        cleanup_resources();
        return false;
    }
    s_ui_event_group = xEventGroupCreate();
    if (s_ui_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create UI event group");
        cleanup_resources();
        return false;
    }

    // Reset FPS counter
    s_frame_count = 0;
    s_last_tick = xTaskGetTickCount();
    s_fps = 0.0f;

    s_tcp_server_task_handle = NULL;
    s_jpeg_decode_task_handle = NULL;

    // Start the JPEG decode task first
    if (xTaskCreate(jpeg_decode_task, "jpeg_decode", 8192, NULL, 4, &s_jpeg_decode_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create JPEG decode task");
        cleanup_resources();
        return false;
    }

    // Start the TCP receive task
    if (xTaskCreate(tcp_recv_task, "tcp_recv", 8192, &port, 5, &s_tcp_server_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create TCP receive task");
        cleanup_resources();
        return false;
    }
    
    return true;
}

void wifi_image_transfer_stop(void) {
    if (!s_server_running) {
        ESP_LOGW(TAG, "TCP server not running.");
        return;
    }
    ESP_LOGI(TAG, "Stopping TCP server...");
    s_server_running = false; // Signal tasks to stop

    // Close the listen socket to unblock accept() in tcp_recv_task
    if (s_listen_sock >= 0) {
        shutdown(s_listen_sock, SHUT_RDWR);
        close(s_listen_sock);
        s_listen_sock = -1;
    }

    // Wait a bit for tasks to notice the stop signal and exit their loops
    vTaskDelay(pdMS_TO_TICKS(100));

    // Cleanup all resources
    cleanup_resources();

    ESP_LOGI(TAG, "TCP server stopped.");
}

EventGroupHandle_t wifi_image_transfer_get_ui_event_group(void) {
    return s_ui_event_group;
}

esp_err_t wifi_image_transfer_get_latest_frame(uint8_t** buffer, int* width, int* height) {
    if (xSemaphoreTake(s_frame_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return ESP_FAIL; // Could not get mutex in time
    }
    if (s_display_frame_buffer && s_frame_width > 0 && s_frame_height > 0) {
        *buffer = s_display_frame_buffer;
        *width = s_frame_width;
        *height = s_frame_height;
        return ESP_OK;
    } else {
        xSemaphoreGive(s_frame_mutex); // Release mutex if no valid frame
        return ESP_FAIL;
    }
}

void wifi_image_transfer_frame_unlock(void) {
    xSemaphoreGive(s_frame_mutex);
}

float wifi_image_transfer_get_fps(void) {
    uint32_t current_tick = xTaskGetTickCount();
    uint32_t diff = current_tick - s_last_tick;

    if (diff > pdMS_TO_TICKS(1000) || s_last_tick == 0) {
        if (diff > 0) {
            float time_ms = (float)diff * portTICK_PERIOD_MS;
            s_fps = (float)s_frame_count * 1000.0f / time_ms;
        } else {
            s_fps = 0.0f;
        }
        s_frame_count = 0;
        s_last_tick = current_tick;
    }
    return s_fps;
}