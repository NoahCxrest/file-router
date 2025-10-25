#include <microhttpd.h>
#include <curl/curl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define PORT 8080
#define BASE_URL "https://obj.melonly.xyz/u/"

// Struct to hold fetched data
struct MemoryStruct {
    char *memory;
    size_t size;
    int is_png; // 1 for png, 0 for jpg
    int done;
};

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr) return 0;

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

int fetch_first_image(const char *id, struct MemoryStruct *result) {
    CURLM *multi = curl_multi_init();
    if (!multi) return 0;

    struct MemoryStruct png_chunk = {malloc(1), 0, 1, 0};
    struct MemoryStruct jpg_chunk = {malloc(1), 0, 0, 0};

    CURL *png_curl = curl_easy_init();
    CURL *jpg_curl = curl_easy_init();
    if (!png_curl || !jpg_curl) {
        curl_multi_cleanup(multi);
        free(png_chunk.memory);
        free(jpg_chunk.memory);
        return 0;
    }

    char png_url[512];
    char jpg_url[512];
    snprintf(png_url, sizeof(png_url), "%s%s.png", BASE_URL, id);
    snprintf(jpg_url, sizeof(jpg_url), "%s%s.jpg", BASE_URL, id);

    curl_easy_setopt(png_curl, CURLOPT_URL, png_url);
    curl_easy_setopt(png_curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(png_curl, CURLOPT_WRITEDATA, &png_chunk);
    curl_easy_setopt(png_curl, CURLOPT_PRIVATE, &png_chunk);
    curl_easy_setopt(png_curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(png_curl, CURLOPT_TIMEOUT, 10L);

    curl_easy_setopt(jpg_curl, CURLOPT_URL, jpg_url);
    curl_easy_setopt(jpg_curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(jpg_curl, CURLOPT_WRITEDATA, &jpg_chunk);
    curl_easy_setopt(jpg_curl, CURLOPT_PRIVATE, &jpg_chunk);
    curl_easy_setopt(jpg_curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(jpg_curl, CURLOPT_TIMEOUT, 10L);

    curl_multi_add_handle(multi, png_curl);
    curl_multi_add_handle(multi, jpg_curl);

    int still_running = 1;
    while (still_running) {
        CURLMcode mc = curl_multi_perform(multi, &still_running);
        if (mc != CURLM_OK) break;

        // Check for completed transfers
        CURLMsg *msg;
        int msgs_left;
        while ((msg = curl_multi_info_read(multi, &msgs_left))) {
            if (msg->msg == CURLMSG_DONE) {
                struct MemoryStruct *chunk = NULL;
                curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &chunk);
                if (chunk && !chunk->done) {
                    chunk->done = 1;
                    if (msg->data.result == CURLE_OK) {
                        result->memory = chunk->memory;
                        result->size = chunk->size;
                        result->is_png = chunk->is_png;
                        if (chunk == &png_chunk) {
                            free(jpg_chunk.memory);
                        } else {
                            free(png_chunk.memory);
                        }
                        curl_multi_cleanup(multi);
                        curl_easy_cleanup(png_curl);
                        curl_easy_cleanup(jpg_curl);
                        return 1;
                    }
                }
            }
        }

        if (still_running) {
            curl_multi_wait(multi, NULL, 0, 1000, NULL);
        }
    }

    free(png_chunk.memory);
    free(jpg_chunk.memory);
    curl_multi_cleanup(multi);
    curl_easy_cleanup(png_curl);
    curl_easy_cleanup(jpg_curl);
    return 0;
}

enum MHD_Result handle_request(void *cls, struct MHD_Connection *connection,
                               const char *url, const char *method,
                               const char *version, const char *upload_data,
                               size_t *upload_data_size, void **con_cls) {
    if (strcmp(method, "GET") != 0) {
        return MHD_NO; 
    }

    if (url[0] != '/' || strlen(url) < 2) {
        return MHD_NO;
    }

    const char *id = url + 1; 

    struct MemoryStruct chunk;
    if (!fetch_first_image(id, &chunk)) {
        const char *not_found = "Image not found";
        struct MHD_Response *response = MHD_create_response_from_buffer(strlen(not_found), (void *)not_found, MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(response, "Content-Type", "text/plain");
        enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, response);
        MHD_destroy_response(response);
        return ret;
    }

    const char *content_type = chunk.is_png ? "image/png" : "image/jpeg";
    struct MHD_Response *response = MHD_create_response_from_buffer(chunk.size, chunk.memory, MHD_RESPMEM_MUST_FREE);
    MHD_add_response_header(response, "Content-Type", content_type);
    MHD_add_response_header(response, "Cache-Control", "public, max-age=3600");
    enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
    MHD_destroy_response(response);
    return ret;
}

int main() {
    curl_global_init(CURL_GLOBAL_DEFAULT);

    struct MHD_Daemon *daemon = MHD_start_daemon(MHD_USE_THREAD_PER_CONNECTION | MHD_USE_INTERNAL_POLLING_THREAD, PORT, NULL, NULL,
                                                 &handle_request, NULL, MHD_OPTION_END);
    if (!daemon) {
        fprintf(stderr, "Failed to start daemon\n");
        return 1;
    }

    printf("Server running on port %d\n", PORT);
    getchar();

    MHD_stop_daemon(daemon);
    curl_global_cleanup();
    return 0;
}