/*
 * port_ra_net.h — async HTTPS transport for the RetroAchievements client.
 *
 * One worker thread performs blocking libcurl requests; the game thread
 * never blocks. Completions are queued and dispatched from
 * Port_RA_Net_Pump(), so callbacks always run on the thread that pumps
 * (the game thread) and never from the worker.
 *
 * Every queued request's callback is eventually invoked exactly once —
 * on completion, on transport error, on a full queue, or during
 * Port_RA_Net_Shutdown() (with http_status 0). Callers can therefore free
 * per-request state in the callback unconditionally. Because Shutdown()
 * dispatches those pending callbacks, call it *before* tearing down
 * whatever the callbacks touch (i.e. before rc_client_destroy).
 */
#ifndef PORT_RA_NET_H
#define PORT_RA_NET_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char* body; /* NUL-terminated response body ("" on transport error) */
    int http_status;  /* 0 = transport error */
    void* user;
} Port_RA_NetResult;

typedef void (*Port_RA_NetCallback)(const Port_RA_NetResult* result);

void Port_RA_Net_Init(void);
void Port_RA_Net_Shutdown(void);

/* url/post_data are copied. post_data == NULL => GET. content_type may be NULL. */
void Port_RA_Net_Request(const char* url, const char* post_data, const char* content_type,
                         Port_RA_NetCallback cb, void* user);

/* Call once per frame from the game thread; invokes pending callbacks. */
void Port_RA_Net_Pump(void);

int Port_RA_Net_InFlight(void);

#ifdef __cplusplus
}
#endif
#endif /* PORT_RA_NET_H */
