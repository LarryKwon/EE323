#include <netinet/in.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <sched.h>
#include <string.h>
#include "sr_arpcache.h"
#include "sr_router.h"
#include "sr_if.h"
#include "sr_protocol.h"
#include "sr_rt.h"
/* 
  This function gets called every second. For each request sent out, we keep
  checking whether we should resend an request or destroy the arp request.
  See the comments in the header file for an idea of what it should look like.
*/
/*
   void sr_arpcache_sweepreqs(struct sr_instance *sr) {
       for each request on sr->cache.requests:
           handle_arpreq(request)
   }
   */
void sr_arpcache_sweepreqs(struct sr_instance *sr)
{
    /* Fill this in */
    struct sr_arpcache *cache = &(sr->cache); /* cache */
    struct sr_arpreq *req, *next;             /* requests */

    /* call sr_arpcache_handle_arpreq() to every request entry */
    for (req = cache->requests; req != NULL; req = next)
    {
        next = req->next;
        sr_arpcache_handle_arpreq(sr, req);
    }
}
/*
   function handle_arpreq(req):
       if difftime(now, req->sent) > 1.0
           if req->times_sent >= 5:
               send icmp host unreachable to source addr of all pkts waiting
                 on this request
               arpreq_destroy(req)
           else:
               send arp request
               req->sent = now
               req->times_sent++

*/
void sr_arpcache_handle_arpreq(struct sr_instance *sr, struct sr_arpreq *req)
{
    struct sr_arpcache *cache = &(sr->cache); /* cache */
    struct sr_packet *pck;                    /* packet */
    uint8_t *buf;                             /* raw Ethernet frame */
    unsigned int len;                         /* length of buf */
    struct sr_ethernet_hdr *e_hdr, *e_hdr0;   /* Ethernet header */
    struct sr_ip_hdr *i_hdr0, *i_hdr;         /* IP headers */
    struct sr_arp_hdr *a_hdr;                 /* ARP header */
    struct sr_icmp_t3_hdr *ict3_hdr;          /* ICMP type3 header */
    struct sr_rt *rtentry;                    /* routing table entry */
    struct sr_if *ifc;                        /* router interface */
    struct sr_arpentry *entry;                /* ARP table entry */

    time_t curtime = time(NULL); /* current time */

    if (difftime(curtime, req->sent) > 1.0)
    {

        /* 5 failures accumulated, discard */
        if (req->times_sent >= 5)
        {
            /**************** fill in code here *****************/

            /* generate ICMP host unreachable packets */

            struct sr_packet *walk, *nxt;
            /*printf("%d\n", 999);*/
            for (walk = req->packets; walk; walk = nxt)
            {
                len = sizeof(struct sr_ethernet_hdr) + sizeof(struct sr_ip_hdr) + sizeof(struct sr_icmp_t3_hdr) + ICMP_DATA_SIZE;
                uint8_t *new_pck = (uint8_t *)calloc(1, len);
                e_hdr = (struct sr_ethernet_hdr *)new_pck;
                i_hdr = (struct sr_ip_hdr *)((uint8_t *)e_hdr + sizeof(struct sr_ethernet_hdr));
                ict3_hdr = (struct sr_icmp_t3_hdr *)((uint8_t *)i_hdr + sizeof(struct sr_ip_hdr));
                /*printf("%d\n", 50);*/

                /*set the headers on the packets*/
                i_hdr0 = (struct sr_ip_hdr *)(walk->buf + sizeof(struct sr_ethernet_hdr));
                e_hdr0 = (struct sr_ethernet_hdr *)(walk->buf);
		rtentry = sr_findLPMentry(sr->routing_table,req->ip);
		ifc=sr_get_interface(sr,rtentry->interface);
		i_hdr->ip_src = ifc->ip;
                rtentry = sr_findLPMentry(sr->routing_table, i_hdr0->ip_src);
                ifc = sr_get_interface(sr, rtentry->interface);

                i_hdr->ip_ttl = INIT_TTL;
                i_hdr->ip_v = 0x04;
                i_hdr->ip_hl = sizeof(struct sr_ip_hdr) / 4;
		
                
                i_hdr->ip_dst = i_hdr0->ip_src;
                i_hdr->ip_p = ip_protocol_icmp;
                i_hdr->ip_id = i_hdr0->ip_id;
                i_hdr->ip_len = htons(len - sizeof(struct sr_ethernet_hdr));
                i_hdr->ip_tos = i_hdr0->ip_tos;
                i_hdr->ip_off = i_hdr0->ip_off;
                i_hdr->ip_sum = 0;
                i_hdr->ip_sum = cksum(i_hdr, sizeof(struct sr_ip_hdr));
                /*printf("%d\n", 51);*/
                ict3_hdr->icmp_type = 0x03;
                ict3_hdr->icmp_code = 0x01;
                ict3_hdr->icmp_sum = 0;
                ict3_hdr->next_mtu = htons(0xFF);
                ict3_hdr->unused = htons(0x00);
                memcpy(ict3_hdr->data, i_hdr0, ICMP_DATA_SIZE);
                ict3_hdr->icmp_sum = cksum(ict3_hdr, len - sizeof(struct sr_ethernet_hdr) - sizeof(struct sr_ip_hdr));

                e_hdr->ether_type = htons(ethertype_ip);
                memcpy(e_hdr->ether_shost, ifc->addr, ETHER_ADDR_LEN);

                /*printf("%d\n", 52);*/
                /*print_hdrs(new_pck, len);*/
                entry = sr_arpcache_lookup(&(sr->cache), rtentry->gw.s_addr);
                if (entry != NULL)
                {
                    memcpy(e_hdr->ether_dhost, entry->mac, ETHER_ADDR_LEN);
                    free(entry);
                    sr_send_packet(sr, new_pck, len, rtentry->interface);
                }
                else
                {
                    /* queue */
                    struct sr_arpreq *arpreq = sr_arpcache_queuereq(&(sr->cache), rtentry->gw.s_addr, new_pck, len, rtentry->interface);
                    sr_arpcache_handle_arpreq(sr, arpreq);
                }
                nxt = walk->next;
            }

            /****************************************************/
            /* done */
            sr_arpreq_destroy(cache, req);
        }

        /* try again */
        else
        {
            /**************** fill in code here *****************/

            /* generate ARP request */
            len = sizeof(struct sr_ethernet_hdr) + sizeof(struct sr_arp_hdr);
            uint8_t *new_pck = (uint8_t *)calloc(1, len);
            e_hdr = (struct sr_ethernet_hdr *)new_pck;
            a_hdr = (struct sr_arp_hdr *)((uint8_t *)e_hdr + sizeof(struct sr_ethernet_hdr));

            i_hdr0 = (struct sr_ip_hdr *)(req->packets->buf + sizeof(struct sr_ethernet_hdr));
            e_hdr0 = (struct sr_ethernet_hdr *)(req->packets->buf);
            /*printf("%s\n", "new");*/
            rtentry = sr_findLPMentry(sr->routing_table, i_hdr0->ip_dst);
            /*sr_print_routing_entry(rtentry);*/
            ifc = sr_get_interface(sr, req->packets->iface);
            /*sr_print_if(ifc);*/
            a_hdr->ar_op = htons(arp_op_request);
            a_hdr->ar_hln = 0x06;
            a_hdr->ar_hrd = htons(0x0001);
            a_hdr->ar_pln = 0x04;
            a_hdr->ar_pro = htons(ethertype_ip);
            memcpy(a_hdr->ar_sha, e_hdr0->ether_shost, ETHER_ADDR_LEN);
            a_hdr->ar_sip = ifc->ip;
            a_hdr->ar_tip = i_hdr0->ip_dst;
            /* send */
            /*printf("%s\n", "init_arp");*/
            /*ethernet header*/
            /*print_addr_ip_int(req->ip);
            print_hdrs(req->packets->buf, req->packets->len);*/
            /*broadcast*/
            memcpy(e_hdr->ether_shost, ifc->addr, ETHER_ADDR_LEN);
            /*printf("%d\n", 1234);*/
            unsigned char *broadcast = (unsigned char *)calloc(1, 6);
            *(uint64_t *)broadcast = 0xffffffffffff;
            memcpy(e_hdr->ether_dhost, broadcast, ETHER_ADDR_LEN);
            /*printf("%d\n", 12345);*/
            free(broadcast);
            e_hdr->ether_type = htons(ethertype_arp);
            /*printf("%s\n", "init_ethernet");*/
            /* send */
            /*print_hdrs(new_pck, len);*/
            sr_send_packet(sr, new_pck, len, rtentry->interface);
            /* done */

            /* update fields */
            req->sent = time(NULL);
            req->times_sent = req->times_sent + 1;
            /****************************************************/
        }
    }
}

/* You should not need to touch the rest of this code. */

/* Checks if an IP->MAC mapping is in the cache. IP is in network byte order.
   You must free the returned structure if it is not NULL. */
struct sr_arpentry *sr_arpcache_lookup(struct sr_arpcache *cache, uint32_t ip)
{
    pthread_mutex_lock(&(cache->lock));

    struct sr_arpentry *entry = NULL, *copy = NULL;

    int i;
    for (i = 0; i < SR_ARPCACHE_SZ; i++)
    {
        if ((cache->entries[i].valid) && (cache->entries[i].ip == ip))
        {
            entry = &(cache->entries[i]);
        }
    }

    /* Must return a copy b/c another thread could jump in and modify
       table after we return. */
    if (entry)
    {
        copy = (struct sr_arpentry *)malloc(sizeof(struct sr_arpentry));
        memcpy(copy, entry, sizeof(struct sr_arpentry));
    }

    pthread_mutex_unlock(&(cache->lock));

    return copy;
}

/* Adds an ARP request to the ARP request queue. If the request is already on
   the queue, adds the packet to the linked list of packets for this sr_arpreq
   that corresponds to this ARP request. You should free the passed *packet.
   
   A pointer to the ARP request is returned; it should not be freed. The caller
   can remove the ARP request from the queue by calling sr_arpreq_destroy. */
struct sr_arpreq *sr_arpcache_queuereq(struct sr_arpcache *cache,
                                       uint32_t ip,
                                       uint8_t *packet, /* borrowed */
                                       unsigned int packet_len,
                                       char *iface)
{
    pthread_mutex_lock(&(cache->lock));

    struct sr_arpreq *req;
    for (req = cache->requests; req != NULL; req = req->next)
    {
        if (req->ip == ip)
        {
            break;
        }
    }

    /* If the IP wasn't found, add it */
    if (!req)
    {
        req = (struct sr_arpreq *)calloc(1, sizeof(struct sr_arpreq));
        req->ip = ip;
        req->next = cache->requests;
        cache->requests = req;
    }

    /* Add the packet to the list of packets for this request */
    if (packet && packet_len && iface)
    {
        struct sr_packet *new_pkt = (struct sr_packet *)malloc(sizeof(struct sr_packet));

        new_pkt->buf = (uint8_t *)malloc(packet_len);
        memcpy(new_pkt->buf, packet, packet_len);
        new_pkt->len = packet_len;
        new_pkt->iface = (char *)malloc(sr_IFACE_NAMELEN);
        strncpy(new_pkt->iface, iface, sr_IFACE_NAMELEN);
        new_pkt->next = req->packets;
        req->packets = new_pkt;
    }

    pthread_mutex_unlock(&(cache->lock));

    return req;
}

/* This method performs two functions:
   1) Looks up this IP in the request queue. If it is found, returns a pointer
      to the sr_arpreq with this IP. Otherwise, returns NULL.
   2) Inserts this IP to MAC mapping in the cache, and marks it valid. */
struct sr_arpreq *sr_arpcache_insert(struct sr_arpcache *cache,
                                     unsigned char *mac,
                                     uint32_t ip)
{
    pthread_mutex_lock(&(cache->lock));

    struct sr_arpreq *req, *prev = NULL, *next = NULL;
    for (req = cache->requests; req != NULL; req = req->next)
    {
        if (req->ip == ip)
        {
            if (prev)
            {
                next = req->next;
                prev->next = next;
            }
            else
            {
                next = req->next;
                cache->requests = next;
            }

            break;
        }
        prev = req;
    }

    int i;
    for (i = 0; i < SR_ARPCACHE_SZ; i++)
    {
        if (!(cache->entries[i].valid))
            break;
    }

    if (i != SR_ARPCACHE_SZ)
    {
        memcpy(cache->entries[i].mac, mac, 6);
        cache->entries[i].ip = ip;
        cache->entries[i].added = time(NULL);
        cache->entries[i].valid = 1;
    }

    pthread_mutex_unlock(&(cache->lock));

    return req;
}

/* Frees all memory associated with this arp request entry. If this arp request
   entry is on the arp request queue, it is removed from the queue. */
void sr_arpreq_destroy(struct sr_arpcache *cache, struct sr_arpreq *entry)
{
    pthread_mutex_lock(&(cache->lock));

    if (entry)
    {
        struct sr_arpreq *req, *prev = NULL, *next = NULL;
        for (req = cache->requests; req != NULL; req = req->next)
        {
            if (req == entry)
            {
                if (prev)
                {
                    next = req->next;
                    prev->next = next;
                }
                else
                {
                    next = req->next;
                    cache->requests = next;
                }

                break;
            }
            prev = req;
        }

        struct sr_packet *pkt, *nxt;

        for (pkt = entry->packets; pkt; pkt = nxt)
        {
            nxt = pkt->next;
            if (pkt->buf)
                free(pkt->buf);
            if (pkt->iface)
                free(pkt->iface);
            free(pkt);
        }

        free(entry);
    }

    pthread_mutex_unlock(&(cache->lock));
}

/* Prints out the ARP table. */
void sr_arpcache_dump(struct sr_arpcache *cache)
{
    fprintf(stderr, "\nMAC            IP         ADDED                      VALID\n");
    fprintf(stderr, "-----------------------------------------------------------\n");

    int i;
    for (i = 0; i < SR_ARPCACHE_SZ; i++)
    {
        struct sr_arpentry *cur = &(cache->entries[i]);
        unsigned char *mac = cur->mac;
        fprintf(stderr, "%.1x%.1x%.1x%.1x%.1x%.1x   %.8x   %.24s   %d\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], ntohl(cur->ip), ctime(&(cur->added)), cur->valid);
    }

    fprintf(stderr, "\n");
}

/* Initialize table + table lock. Returns 0 on success. */
int sr_arpcache_init(struct sr_arpcache *cache)
{
    /* Seed RNG to kick out a random entry if all entries full. */
    srand(time(NULL));

    /* Invalidate all entries */
    memset(cache->entries, 0, sizeof(cache->entries));
    cache->requests = NULL;

    /* Acquire mutex lock */
    pthread_mutexattr_init(&(cache->attr));
    pthread_mutexattr_settype(&(cache->attr), PTHREAD_MUTEX_RECURSIVE);
    int success = pthread_mutex_init(&(cache->lock), &(cache->attr));

    return success;
}

/* Destroys table + table lock. Returns 0 on success. */
int sr_arpcache_destroy(struct sr_arpcache *cache)
{
    return pthread_mutex_destroy(&(cache->lock)) && pthread_mutexattr_destroy(&(cache->attr));
}

/* Thread which sweeps through the cache and invalidates entries that were added
   more than SR_ARPCACHE_TO seconds ago. */

void *sr_arpcache_timeout(void *sr_ptr)
{
    struct sr_instance *sr = sr_ptr;
    struct sr_arpcache *cache = &(sr->cache);

    while (1)
    {
        sleep(1.0);

        pthread_mutex_lock(&(cache->lock));

        time_t curtime = time(NULL);

        int i;
        for (i = 0; i < SR_ARPCACHE_SZ; i++)
        {
            if ((cache->entries[i].valid) && (difftime(curtime, cache->entries[i].added) > SR_ARPCACHE_TO))
            {
                cache->entries[i].valid = 0;
            }
        }

        sr_arpcache_sweepreqs(sr);

        pthread_mutex_unlock(&(cache->lock));
    }

    return NULL;
}

