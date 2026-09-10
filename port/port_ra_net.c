/*
 * port_ra_net.c — async HTTPS transport for the RetroAchievements client.
 *
 * One worker thread drains a bounded FIFO of requests with blocking
 * curl_easy_perform() calls and appends the results to a completion FIFO.
 * Port_RA_Net_Pump() (game thread) moves that FIFO into a local list and
 * runs the callbacks with no lock held, so a callback is free to enqueue
 * the next request.
 *
 * Everything the worker touches lives in the request struct it popped;
 * the only shared mutable state is the two lists + counters behind
 * sMutex.
 *
 * ponytail: one worker thread, not a pool — RA does a handful of requests
 * per session and rcheevos serializes most of them anyway. Add a second
 * worker only if request latency ever stacks up.
 */
#include "port_ra_net.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <curl/curl.h>

#ifndef TMC_PC_VERSION
#define TMC_PC_VERSION "0.0.0"
#endif

/* Bounded queue: a full queue fails the request through its callback
 * instead of blocking the game thread. */
#define RA_NET_QUEUE_MAX 32
/* Response cap. RA's biggest payload (a patch response) is tens of KB;
 * anything past this is treated as a transport error rather than grown. */
#define RA_NET_BODY_MAX (1024u * 1024u)
#define RA_NET_TIMEOUT_S 30L
#define RA_NET_CONNECT_TIMEOUT_S 10L
#define RA_NET_MAX_REDIRS 4L

typedef struct RaNetRequest {
    struct RaNetRequest* next;

    /* Owned copies of the caller's inputs. */
    char* url;
    char* post; /* NULL => GET */
    char* contentType;
    Port_RA_NetCallback cb;
    void* user;

    /* Worker-owned response state. */
    char* body;
    size_t bodySize;
    size_t bodyCapacity;
    int httpStatus;
    bool tooLarge;
} RaNetRequest;

static SDL_Mutex* sMutex;
static SDL_Semaphore* sWakeup;
static SDL_Thread* sWorker;
static RaNetRequest* sPendingHead;
static RaNetRequest* sPendingTail;
static RaNetRequest* sDoneHead;
static RaNetRequest* sDoneTail;
static int sPendingCount;
static int sActiveCount;
static int sDoneCount;
static bool sQuit;
static bool sInitialized;

static char* DuplicateString(const char* value) {
    size_t length = strlen(value) + 1;
    char* copy = (char*)malloc(length);
    if (copy) {
        memcpy(copy, value, length);
    }
    return copy;
}

static void FreeRequest(RaNetRequest* request) {
    free(request->url);
    free(request->post);
    free(request->contentType);
    free(request->body);
    free(request);
}

/* Callbacks are invoked from the pumping thread only. */
static void DispatchRequest(RaNetRequest* request) {
    Port_RA_NetResult result;
    result.body = (request->httpStatus != 0 && request->body) ? request->body : "";
    result.http_status = request->httpStatus;
    result.user = request->user;
    if (request->cb) {
        request->cb(&result);
    }
    FreeRequest(request);
}

static void AppendLocked(RaNetRequest** head, RaNetRequest** tail, RaNetRequest* request) {
    request->next = NULL;
    if (*tail) {
        (*tail)->next = request;
    } else {
        *head = request;
    }
    *tail = request;
}

static size_t WriteBody(char* data, size_t size, size_t count, void* userdata) {
    RaNetRequest* request = (RaNetRequest*)userdata;
    size_t byteCount = size * count;

    if (request->bodySize + byteCount > RA_NET_BODY_MAX) {
        /* Aborts the transfer with CURLE_WRITE_ERROR. */
        request->tooLarge = true;
        return 0;
    }

    if (request->bodySize + byteCount + 1 > request->bodyCapacity) {
        size_t newCapacity = request->bodyCapacity ? request->bodyCapacity * 2 : 8192;
        while (request->bodySize + byteCount + 1 > newCapacity) {
            newCapacity *= 2;
        }
        if (newCapacity > RA_NET_BODY_MAX + 1) {
            newCapacity = RA_NET_BODY_MAX + 1;
        }

        char* newBody = (char*)realloc(request->body, newCapacity);
        if (!newBody) {
            return 0;
        }
        request->body = newBody;
        request->bodyCapacity = newCapacity;
    }

    memcpy(request->body + request->bodySize, data, byteCount);
    request->bodySize += byteCount;
    request->body[request->bodySize] = '\0';
    return byteCount;
}

static void PerformRequest(CURL* curl, RaNetRequest* request) {
    struct curl_slist* headers = NULL;
    char userAgent[96];

    curl_easy_reset(curl);
    SDL_snprintf(userAgent, sizeof(userAgent), "tmc_pc/%s (Project Picori)", TMC_PC_VERSION);

    curl_easy_setopt(curl, CURLOPT_URL, request->url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, userAgent);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, request);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, RA_NET_TIMEOUT_S);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, RA_NET_CONNECT_TIMEOUT_S);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, RA_NET_MAX_REDIRS);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    if (request->post) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request->post);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(request->post));
        if (request->contentType) {
            char header[128];
            SDL_snprintf(header, sizeof(header), "Content-Type: %s", request->contentType);
            headers = curl_slist_append(headers, header);
            if (headers) {
                curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            }
        }
    }

    if (curl_easy_perform(curl) == CURLE_OK) {
        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        request->httpStatus = (int)status;
    } else {
        request->httpStatus = 0;
    }

    if (request->tooLarge) {
        request->httpStatus = 0;
    }

    if (headers) {
        curl_slist_free_all(headers);
    }
}

static int SDLCALL WorkerMain(void* unused) {
    CURL* curl = curl_easy_init();
    (void)unused;

    for (;;) {
        RaNetRequest* request;

        SDL_WaitSemaphore(sWakeup);

        SDL_LockMutex(sMutex);
        if (sQuit) {
            SDL_UnlockMutex(sMutex);
            break;
        }
        request = sPendingHead;
        if (request) {
            sPendingHead = request->next;
            if (!sPendingHead) {
                sPendingTail = NULL;
            }
            sPendingCount--;
            sActiveCount++;
        }
        SDL_UnlockMutex(sMutex);

        if (!request) {
            continue;
        }

        if (curl) {
            PerformRequest(curl, request);
        } else {
            request->httpStatus = 0;
        }

        SDL_LockMutex(sMutex);
        sActiveCount--;
        AppendLocked(&sDoneHead, &sDoneTail, request);
        sDoneCount++;
        SDL_UnlockMutex(sMutex);
    }

    if (curl) {
        curl_easy_cleanup(curl);
    }
    return 0;
}

void Port_RA_Net_Init(void) {
    if (sInitialized) {
        return;
    }

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        SDL_Log("RA net: curl_global_init failed; RetroAchievements networking is unavailable");
        return;
    }

    sMutex = SDL_CreateMutex();
    sWakeup = SDL_CreateSemaphore(0);
    if (!sMutex || !sWakeup) {
        SDL_Log("RA net: failed to create worker sync primitives (%s)", SDL_GetError());
        if (sMutex) {
            SDL_DestroyMutex(sMutex);
            sMutex = NULL;
        }
        if (sWakeup) {
            SDL_DestroySemaphore(sWakeup);
            sWakeup = NULL;
        }
        curl_global_cleanup();
        return;
    }

    sQuit = false;
    sWorker = SDL_CreateThread(WorkerMain, "tmc_ra_net", NULL);
    if (!sWorker) {
        SDL_Log("RA net: failed to start worker thread (%s)", SDL_GetError());
        SDL_DestroyMutex(sMutex);
        sMutex = NULL;
        SDL_DestroySemaphore(sWakeup);
        sWakeup = NULL;
        curl_global_cleanup();
        return;
    }

    sInitialized = true;
}

void Port_RA_Net_Request(const char* url, const char* post_data, const char* content_type,
                         Port_RA_NetCallback cb, void* user) {
    RaNetRequest* request;
    bool rejected = false;

    if (!url || !*url) {
        return;
    }

    request = (RaNetRequest*)calloc(1, sizeof(*request));
    if (request) {
        request->cb = cb;
        request->user = user;
        request->url = DuplicateString(url);
        if (post_data) {
            request->post = DuplicateString(post_data);
        }
        if (content_type) {
            request->contentType = DuplicateString(content_type);
        }
        if (!request->url || (post_data && !request->post) || (content_type && !request->contentType)) {
            rejected = true;
        }
    }

    if (!request) {
        /* Out of memory: still honour the "callback runs exactly once" contract. */
        Port_RA_NetResult result;
        result.body = "";
        result.http_status = 0;
        result.user = user;
        if (cb) {
            cb(&result);
        }
        return;
    }

    if (!sInitialized) {
        rejected = true;
    }

    if (!rejected) {
        SDL_LockMutex(sMutex);
        if (sQuit || sPendingCount >= RA_NET_QUEUE_MAX) {
            rejected = true;
        } else {
            AppendLocked(&sPendingHead, &sPendingTail, request);
            sPendingCount++;
        }
        SDL_UnlockMutex(sMutex);
    }

    if (rejected) {
        request->httpStatus = 0;
        DispatchRequest(request); /* transport error, on this (game) thread */
        return;
    }

    SDL_SignalSemaphore(sWakeup);
}

void Port_RA_Net_Pump(void) {
    RaNetRequest* list;

    if (!sInitialized) {
        return;
    }

    SDL_LockMutex(sMutex);
    list = sDoneHead;
    sDoneHead = NULL;
    sDoneTail = NULL;
    sDoneCount = 0;
    SDL_UnlockMutex(sMutex);

    while (list) {
        RaNetRequest* next = list->next;
        DispatchRequest(list);
        list = next;
    }
}

int Port_RA_Net_InFlight(void) {
    int count;

    if (!sInitialized) {
        return 0;
    }

    SDL_LockMutex(sMutex);
    count = sPendingCount + sActiveCount + sDoneCount;
    SDL_UnlockMutex(sMutex);
    return count;
}

void Port_RA_Net_Shutdown(void) {
    RaNetRequest* list;

    if (!sInitialized) {
        return;
    }

    SDL_LockMutex(sMutex);
    sQuit = true;
    SDL_UnlockMutex(sMutex);

    /* The worker may be mid-request; the semaphore post makes it observe
     * sQuit as soon as it comes back around. */
    SDL_SignalSemaphore(sWakeup);
    SDL_WaitThread(sWorker, NULL);
    sWorker = NULL;
    sInitialized = false;

    /* Splice both queues and fail whatever never ran, so callers can free
     * their per-request state. */
    SDL_LockMutex(sMutex);
    list = sDoneHead;
    if (sDoneTail) {
        sDoneTail->next = sPendingHead;
    } else {
        list = sPendingHead;
    }
    sDoneHead = NULL;
    sDoneTail = NULL;
    sPendingHead = NULL;
    sPendingTail = NULL;
    sPendingCount = 0;
    sDoneCount = 0;
    SDL_UnlockMutex(sMutex);

    while (list) {
        RaNetRequest* next = list->next;
        DispatchRequest(list);
        list = next;
    }

    SDL_DestroyMutex(sMutex);
    sMutex = NULL;
    SDL_DestroySemaphore(sWakeup);
    sWakeup = NULL;
    curl_global_cleanup();
}
