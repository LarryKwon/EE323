/*
 * transport.c 
 *
 * CS244a HW#3 (Reliable Transport)
 *
 * This file implements the STCP layer that sits between the
 * mysocket and network layers. You are required to fill in the STCP
 * functionality in this file. 
 *
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "mysock.h"
#include "stcp_api.h"
#include "transport.h"

#define RECV_WINDOW_SIZE 3072
#define CWND_WINDOW_SIZE 536
#define MSS 536
#define PAYLOAD_SIZE 536

typedef enum
{
    CSTATE_CLOSED = 0,
    CSTATE_LISTEN,
    CSTATE_SYN_SENT,
    CSTATE_SYN_RCVD,
    CSTATE_ESTABLISHED,
    CSTATE_FIN_WAIT_1,
    CSTATE_FIN_WAIT_2,
    CSTATE_CLOSE_WAIT,
    CSTATE_LAST_ACK,
    CSTATE_CLOSING
} CSTATE; /* obviously you should have more states */

CSTATE state;
/* this structure is global to a mysocket descriptor */
typedef struct
{
    bool_t done; /* TRUE once connection is closed */

    int connection_state; /* state of the connection (established, etc.) */
    tcp_seq initial_sequence_num;

    size_t SWND;
    size_t SWND_prev;
    size_t CWND;
    size_t RECV_WIN;
    size_t SendBase;
    size_t RecvBase;
    size_t next_seqnum;
    size_t rem;
    uint8_t *buffer;
    uint8_t *new_buffer;
    size_t buffer_size;
    size_t new_buffer_size;
    size_t ACK_size;
    bool_t is_client;
    FILE *client_log;
    FILE *server_log;
    /* any other connection-wide global variables go here */
} context_t;

static void generate_initial_seq_num(context_t *ctx);
static void control_loop(mysocket_t sd, context_t *ctx);

/* initialise the transport layer, and start the main loop, handling
 * any data from the peer or the application.  this function should not
 * return until the connection is closed.
 */
void transport_init(mysocket_t sd, bool_t is_active)
{
    context_t *ctx;

    ctx = (context_t *)calloc(1, sizeof(context_t));
    assert(ctx);

    generate_initial_seq_num(ctx);

    /* XXX: you should send a SYN packet here if is_active, or wait for one
     * to arrive if !is_active.  after the handshake completes, unblock the
     * application with stcp_unblock_application(sd).  you may also use
     * this to communicate an error condition back to the application, e.g.
     * if connection fails; to do so, just set errno appropriately (e.g. to
     * ECONNREFUSED, etc.) before calling the function.
     */
    if (is_active)
    {
        uint8_t *src = (uint8_t *)calloc(1, sizeof(struct tcphdr));
        struct tcphdr *header = (struct tcphdr *)src;
        header->th_seq = ctx->initial_sequence_num;
        header->th_flags = TH_SYN;
        header->th_off = 5;
        header->th_win = RECV_WINDOW_SIZE;
        stcp_network_send(sd, header, sizeof(struct tcphdr), NULL);
        ctx->connection_state = CSTATE_SYN_SENT;
    }
    else
    {
        ctx->connection_state = CSTATE_LISTEN;
    }
    while (ctx->connection_state != CSTATE_ESTABLISHED)
    {
        switch (ctx->connection_state)
        {
        case CSTATE_SYN_SENT:
        {
            uint8_t *packet = (uint8_t *)calloc(1, sizeof(struct tcphdr));
            struct tcphdr *ACK = (struct tcphdr *)packet;
            stcp_network_recv(sd, ACK, sizeof(struct tcphdr));
            tcp_seq temp = ACK->th_ack;
            ACK->th_ack = ACK->th_seq + 1;
            ACK->th_seq = temp;
            stcp_network_send(sd, ACK, sizeof(struct tcphdr), NULL);
            ctx->connection_state = CSTATE_ESTABLISHED;
        }
        break;

        case CSTATE_LISTEN:
        {
            uint8_t *packet = (uint8_t *)calloc(1, sizeof(struct tcphdr));
            struct tcphdr *SYN_ACK = (struct tcphdr *)packet;
            stcp_network_recv(sd, SYN_ACK, sizeof(struct tcphdr));
            SYN_ACK->th_ack = SYN_ACK->th_seq + 1;
            SYN_ACK->th_seq = ctx->initial_sequence_num;
            stcp_network_send(sd, SYN_ACK, sizeof(struct tcphdr), NULL);
            ctx->connection_state = CSTATE_SYN_RCVD;
        }
        break;

        case CSTATE_SYN_RCVD:
        {
            uint8_t *ack = (uint8_t *)calloc(1, sizeof(struct tcphdr));
            struct tcphdr *ACK = (struct tcphdr *)ack;
            stcp_network_recv(sd, ACK, sizeof(struct tcphdr));
            ctx->connection_state = CSTATE_ESTABLISHED;
        }
        break;
        }
    }
    ctx->SWND = MSS;
    ctx->SWND_prev = ctx->SWND;
    ctx->CWND = MSS;
    ctx->RECV_WIN = 3072;
    ctx->SendBase = ctx->initial_sequence_num;
    ctx->RecvBase = ctx->initial_sequence_num;
    ctx->next_seqnum = ctx->SendBase;
    ctx->rem = ctx->SWND - (ctx->next_seqnum - ctx->SendBase);
    ctx->buffer_size = MSS;
    ctx->is_client = is_active;
    if (ctx->is_client == FALSE)
    {
        ctx->server_log = fopen("server_log.txt", "w");
        fclose(ctx->server_log);
    }
    else
    {
        ctx->client_log = fopen("client_log.txt", "w");
        fclose(ctx->client_log);
    }
    /* ctx->connection_state = CSTATE_ESTABLISHED; */

    stcp_unblock_application(sd);

    control_loop(sd, ctx);

    /* do any cleanup here */
    free(ctx->buffer);
    free(ctx->new_buffer);
    free(ctx);
}

/* generate initial sequence number for an STCP connection */
static void generate_initial_seq_num(context_t *ctx)
{
    assert(ctx);
    ctx->initial_sequence_num = 1;
}

/* control_loop() is the main STCP loop; it repeatedly waits for one of the
 * following to happen:
 *   - incoming data from the peer
 *   - new data from the application (via mywrite())
 *   - the socket to be closed (via myclose())
 *   - a timeout
 */
static void control_loop(mysocket_t sd, context_t *ctx)
{
    assert(ctx);
    //sliding window initiation//
    ctx->buffer = (uint8_t *)calloc(MSS, sizeof(char));
    while (!ctx->done)
    {
        unsigned int event;

        /* see stcp_api.h or stcp_api.c for details of this function */
        /* XXX: you will need to change some of these arguments! */
        event = stcp_wait_for_event(sd, ANY_EVENT, NULL);

        /* check whether it was the network, app, or a close request */

        if (event & APP_DATA)
        {
            /* the application has requested that data be sent */
            /* see stcp_app_recv() */
            if (ctx->rem == 0)
            {
                goto next;
            }
            size_t max_size = MIN(ctx->rem, MSS);
            size_t data_size = stcp_app_recv(sd, ctx->buffer + ctx->next_seqnum - ctx->SendBase, max_size);
            uint8_t *packet = (uint8_t *)calloc(1, sizeof(struct tcphdr) + data_size);
            struct tcphdr *header = (struct tcphdr *)packet;
            header->th_seq = ctx->next_seqnum;
            header->th_win = RECV_WINDOW_SIZE;
            header->th_off = 5;
            header->th_flags = 0x00;
            memcpy(packet + sizeof(struct tcphdr), ctx->buffer + ctx->next_seqnum - ctx->SendBase, data_size);
            stcp_network_send(sd, packet, sizeof(struct tcphdr) + data_size, NULL);
            if (ctx->is_client == FALSE)
            {
                ctx->server_log = fopen("server_log.txt", "a");
                fprintf(ctx->server_log, "Send:\t%lu\t%lu\t%lu\n", ctx->SWND, ctx->rem, data_size);
                fclose(ctx->server_log);
            }
            else
            {
                ctx->client_log = fopen("client_log.txt", "a");
                fprintf(ctx->client_log, "Send:\t%lu\t%lu\t%lu\n", ctx->SWND, ctx->rem, data_size);
                fclose(ctx->client_log);
            }
            ctx->next_seqnum = ctx->next_seqnum + data_size;
            ctx->rem = ctx->SWND - (ctx->next_seqnum - ctx->SendBase);
            free(packet);
            /*
            make sending packet buffer wth size (packet hdr+min(MSS,REM)) // 보낼 패킷 만들기
            set packet hdr (seq_num = next_seqnum_server) // hdr의 seq_num을 next_seqnum_server로 설정
            send packet // packet 보내기
            next_seqnum_server += data_size // data size seqnumber 늘리기
            rem = SWND - (next seqnum_server - send_base_server)
            */
        }
    next:
        if (event & NETWORK_DATA)
        {
            // // receive data from network // stcp_network_recv()
            if (ctx->connection_state == CSTATE_FIN_WAIT_1)
            {
                // printf("%s\n", "fin Acked");
                goto close;
            }
            else if (ctx->connection_state == CSTATE_LAST_ACK)
            {
                goto close;
            }

            uint8_t *packet = (uint8_t *)calloc(1, sizeof(struct tcphdr) + MSS);
            ssize_t data_size = stcp_network_recv(sd, packet, sizeof(struct tcphdr) + MSS) - sizeof(struct tcphdr);
            struct tcphdr *header = (struct tcphdr *)packet;
            uint8_t *data = (uint8_t *)((uint8_t *)header + sizeof(struct tcphdr));
            assert(data_size >= 0);
            if (header->th_flags == TH_ACK)
            {
                size_t temp = ctx->SendBase;
                ctx->SendBase = header->th_ack;
                ctx->ACK_size = ctx->SendBase - temp;
                if (ctx->is_client == TRUE)
                {
                    ctx->client_log = fopen("client_log.txt", "a");
                    fprintf(ctx->client_log, "RECV:\t%lu\t%lu\t%lu\n", ctx->SWND, ctx->rem, ctx->ACK_size);
                    fclose(ctx->client_log);
                }
                else
                {
                    ctx->server_log = fopen("server_log.txt", "a");
                    fprintf(ctx->server_log, "RECV:\t%lu\t%lu\t%lu\n", ctx->SWND, ctx->rem, ctx->ACK_size);
                    fclose(ctx->server_log);
                }

                if (ctx->CWND < 4 * MSS)
                {
                    ctx->CWND = ctx->CWND + MSS;
                }
                else
                {
                    ctx->CWND = ctx->CWND + MSS * MSS / ctx->CWND;
                }
                ctx->SWND_prev = ctx->SWND;
                ctx->SWND = MIN(ctx->CWND, ctx->RECV_WIN);
                ctx->rem = ctx->SWND - (ctx->next_seqnum - ctx->SendBase);
                ctx->new_buffer_size = ctx->buffer_size + ctx->SWND - ctx->SWND_prev + ctx->ACK_size;
                uint8_t *new_buffer = (uint8_t *)calloc(ctx->SWND, sizeof(char));
                memcpy(new_buffer, ctx->buffer + ctx->ACK_size, ctx->SWND_prev - ctx->ACK_size);

                ctx->buffer = new_buffer;

                free(header);
            }
            else if (header->th_flags == TH_FIN)
            {
                // printf("%s\n", "packet: fin");
                ctx->RecvBase = ctx->RecvBase + 1;
                uint8_t *packet = (uint8_t *)calloc(1, sizeof(struct tcphdr));
                struct tcphdr *FIN_ACK = (struct tcphdr *)packet;
                FIN_ACK->th_flags = TH_ACK;
                FIN_ACK->th_ack = header->th_seq + 1;
                FIN_ACK->th_seq = ctx->RecvBase;
                stcp_network_send(sd, FIN_ACK, sizeof(struct tcphdr), NULL);
                if (ctx->is_client == TRUE)
                {
                    // printf("%s\n", "FIN_WAIT_2 -> closed");
                    ctx->connection_state = CSTATE_CLOSED;
                    ctx->done = TRUE;
                }
                else
                {
                    // printf("%s\n", "establisehd -> close wait");
                    ctx->connection_state = CSTATE_CLOSE_WAIT;
                }
                stcp_fin_received(sd);
                free(header);
            }
            else
            {
                ctx->RecvBase = ctx->RecvBase + (size_t)data_size;
                struct tcphdr *ACK = (struct tcphdr *)calloc(1, sizeof(struct tcphdr));
                ACK->th_flags = TH_ACK;
                ACK->th_ack = header->th_seq + (size_t)data_size;
                ACK->th_seq = ctx->RecvBase;
                stcp_network_send(sd, ACK, sizeof(struct tcphdr), NULL);

                stcp_app_send(sd, data, (size_t)data_size);

                free(header);
            }
        }
        if (event & APP_CLOSE_REQUESTED)
        {
        close:
            // printf("%s\n", "close");
            /* the application has requested that data be sent */
            /* see stcp_app_recv() */
            if (ctx->is_client == FALSE)
            {
                if (ctx->connection_state == CSTATE_CLOSE_WAIT)
                {
                    uint8_t *packet = (uint8_t *)calloc(1, sizeof(struct tcphdr));
                    struct tcphdr *header = (struct tcphdr *)packet;
                    header->th_seq = ctx->next_seqnum;
                    header->th_win = RECV_WINDOW_SIZE;
                    header->th_flags = TH_FIN;
                    stcp_network_send(sd, packet, sizeof(struct tcphdr), NULL);
                    ctx->next_seqnum = ctx->next_seqnum + 1;
                    ctx->rem = ctx->SWND - (ctx->next_seqnum - ctx->SendBase);
                    free(packet);
                    ctx->connection_state = CSTATE_LAST_ACK;
                    // printf("%s\n", "close_wait -> Last_ACK");
                }
                else if (ctx->connection_state == CSTATE_LAST_ACK)
                {
                    uint8_t *packet = (uint8_t *)calloc(1, sizeof(struct tcphdr));
                    stcp_network_recv(sd, packet, sizeof(struct tcphdr));
                    struct tcphdr *header = (struct tcphdr *)packet;
                    if (header->th_flags == TH_ACK)
                    {
                        ctx->connection_state = CSTATE_CLOSED;
                        ctx->done = TRUE;
                    }
                }
            }

            else if (ctx->is_client == TRUE)
            {
                if (ctx->connection_state == CSTATE_ESTABLISHED)
                {
                    uint8_t *packet = (uint8_t *)calloc(1, sizeof(struct tcphdr));
                    struct tcphdr *header = (struct tcphdr *)packet;

                    header->th_seq = ctx->next_seqnum;
                    header->th_win = RECV_WINDOW_SIZE;
                    header->th_flags = TH_FIN;
                    stcp_network_send(sd, packet, sizeof(struct tcphdr), NULL);

                    ctx->next_seqnum = ctx->next_seqnum + 1;

                    ctx->rem = ctx->SWND - (ctx->next_seqnum - ctx->SendBase);

                    free(packet);
                    ctx->connection_state = CSTATE_FIN_WAIT_1;
                    // printf("%s\n", "established->FIN_WAIT_1");
                }
                else if (ctx->connection_state == CSTATE_FIN_WAIT_1)
                {
                    // printf("%s\n", "fin_wait_1");
                    uint8_t *packet = (uint8_t *)calloc(1, sizeof(struct tcphdr));
                    stcp_network_recv(sd, packet, sizeof(struct tcphdr));
                    struct tcphdr *header = (struct tcphdr *)packet;
                    if (header->th_flags == TH_ACK)
                    {
                        ctx->connection_state = CSTATE_FIN_WAIT_2;
                        // printf("%s\n", "fin_wait_1 -> FIN_WAIT2");
                    }
                }
            }
        }
        /* etc. */
    }
}

/**********************************************************************/
/* our_dprintf
 *
 * Send a formatted message to stdout.
 * 
 * format               A printf-style format string.
 *
 * This function is equivalent to a printf, but may be
 * changed to log errors to a file if desired.
 *
 * Calls to this function are generated by the dprintf amd
 * dperror macros in transport.h
 */
void our_dprintf(const char *format, ...)
{
    va_list argptr;
    char buffer[1024];

    assert(format);
    va_start(argptr, format);
    vsnprintf(buffer, sizeof(buffer), format, argptr);
    va_end(argptr);
    fputs(buffer, stdout);
    fflush(stdout);
}
