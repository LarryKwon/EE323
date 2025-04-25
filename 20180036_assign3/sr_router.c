/**********************************************************************
 * file:  sr_router.c
 * date:  Mon Feb 18 12:50:42 PST 2002
 * Contact: casado@stanford.edu
 *
 * Description:
 *
 * This file contains all the functions that interact directly
 * with the routing table, as well as the main entry method
 * for routing.
 *
 **********************************************************************/

#include <stdio.h>
#include <assert.h>
#include "sr_if.h"
#include "sr_rt.h"
#include "sr_router.h"
#include "sr_protocol.h"
#include "sr_arpcache.h"
#include "sr_utils.h"

/*---------------------------------------------------------------------
 * Method: sr_init(void)
 * Scope:  Global
 *
 * Initialize the routing subsystem
 *
 *---------------------------------------------------------------------*/
void sr_init(struct sr_instance *sr)
{
  /* REQUIRES */
  assert(sr);

  /* Initialize cache and cache cleanup thread */
  sr_arpcache_init(&(sr->cache));

  pthread_attr_init(&(sr->attr));
  pthread_attr_setdetachstate(&(sr->attr), PTHREAD_CREATE_JOINABLE);
  pthread_attr_setscope(&(sr->attr), PTHREAD_SCOPE_SYSTEM);
  pthread_attr_setscope(&(sr->attr), PTHREAD_SCOPE_SYSTEM);
  pthread_t thread;

  pthread_create(&thread, &(sr->attr), sr_arpcache_timeout, sr);

  /* Add initialization code here! */

} /* -- sr_init -- */

/*---------------------------------------------------------------------
  Check source ip with ip_blacklist
  Goal 1 : check whether the source ip is black ip or not
  Goal 2 : Print Log 
  - Format  :  " [Source ip blocked] : <source ip> "
  e.g.) [Source ip blocked] : 10.0.2.100
*/
int ip_black_list(struct sr_ip_hdr *iph)
{
  int blk = 0;
  char ip_blacklist[20] = "10.0.2.0";
  char mask[20] = "255.255.255.0";
  /**************** fill in code here *****************/

  uint32_t black_addr;
  uint32_t masking;
  struct in_addr addr;

  black_addr = inet_addr(ip_blacklist);
  masking = inet_addr(mask);
  addr.s_addr = iph->ip_src;
  char *src_ip = inet_ntoa(addr);
  if (black_addr == (masking & iph->ip_src))
  {
    blk = 1;
    printf("[Source ip blocked] : %s\n", src_ip);
  }

  /****************************************************/
  return blk;
}
/*---------------------------------------------------------------------
 * Method: sr_handlepacket(uint8_t* p,char* interface)
 * Scope:  Global
 *
 * This method is called each time the router receives a packet on the
 * interface.  The packet buffer, the packet length and the receiving
 * interface are passed in as parameters. The packet is complete with
 * ethernet headers.
 *
 * Note: Both the packet buffer and the character's memory are handled
 * by sr_vns_comm.c that means do NOT delete either.  Make a copy of the
 * packet instead if you intend to keep it around beyond the scope of
 * the method call.
 *
 *---------------------------------------------------------------------*/
void sr_handlepacket(struct sr_instance *sr,
                     uint8_t *packet /* lent */,
                     unsigned int len,
                     char *interface /* lent */)
{

  /* REQUIRES */
  assert(sr);
  assert(packet);
  assert(interface);

  /*  printf("*** -> Received packet of length %d \n",len);*/

  /* fill in code here */
  uint8_t *new_pck;     /* new packet */
  unsigned int new_len; /* length of new_pck */

  unsigned int len_r; /* length remaining, for validation */
  uint16_t checksum;  /* checksum, for validation */

  struct sr_ethernet_hdr *e_hdr0, *e_hdr; /* Ethernet headers */
  struct sr_ip_hdr *i_hdr0, *i_hdr;       /* IP headers */
  struct sr_arp_hdr *a_hdr0, *a_hdr;      /* ARP headers */
  struct sr_icmp_hdr *ic_hdr0;            /* ICMP header */
  struct sr_icmp_t3_hdr *ict3_hdr;        /* ICMP type3 header */

  struct sr_if *ifc;            /* router interface */
  uint32_t ipaddr;              /* IP address */
  struct sr_rt *rtentry;        /* routing table entry */
  struct sr_arpentry *arpentry; /* ARP table entry in ARP cache */
  struct sr_arpreq *arpreq;     /* request entry in ARP cache */
  struct sr_packet *en_pck;     /* encapsulated packet in ARP cache */

  /* validation */
  if (len < sizeof(struct sr_ethernet_hdr))
    return;
  len_r = len - sizeof(struct sr_ethernet_hdr);
  e_hdr0 = (struct sr_ethernet_hdr *)packet; /* e_hdr0 set */

  /* IP packet arrived */
  if (e_hdr0->ether_type == htons(ethertype_ip))
  {
    /*printf("%d\n", 5);*/
    /* validation */
    if (len_r < sizeof(struct sr_ip_hdr))
      return;
    len_r = len_r - sizeof(struct sr_ip_hdr);
    /*printf("%d\n", 6);*/
    i_hdr0 = (struct sr_ip_hdr *)(((uint8_t *)e_hdr0) + sizeof(struct sr_ethernet_hdr)); /* i_hdr0 set */
    /*printf("%d\n", 7);*/
    if (i_hdr0->ip_v != 0x4)
      return;
    checksum = i_hdr0->ip_sum;
    i_hdr0->ip_sum = 0;
    /*printf("%d\n", 8);*/
    if (checksum != cksum(i_hdr0, sizeof(struct sr_ip_hdr)))
      return;
    i_hdr0->ip_sum = checksum;
    /*printf("%d\n", 9);*/

    /* check destination */
    for (ifc = sr->if_list; ifc != NULL; ifc = ifc->next)
      if (i_hdr0->ip_dst == ifc->ip)
      {
        break;
      }
    /* check ip black list */
    if (ip_black_list(i_hdr0))
    {
      /* Drop the packet */
      return;
    }
    /* destined to router interface */
    if (ifc != NULL)
    {
      /*printf("%d\n", 12);*/
      /* with ICMP */
      if (i_hdr0->ip_p == ip_protocol_icmp)
      {

        /* validation */
        if (len_r < sizeof(struct sr_icmp_hdr))
          return;
        ic_hdr0 = (struct sr_icmp_hdr *)(((uint8_t *)i_hdr0) + sizeof(struct sr_ip_hdr)); /* ic_hdr0 set */

        /* echo request type */
        if (ic_hdr0->icmp_type == 0x08)
        {
          /*printf("%d\n", 121);*/
          /* validation */
          checksum = ic_hdr0->icmp_sum;
          /*printf("%d\n", 122);*/
          ic_hdr0->icmp_sum = 0;
          /*printf("%d\n", 123);*/
          if (checksum != cksum(ic_hdr0, len - sizeof(struct sr_ethernet_hdr) - sizeof(struct sr_ip_hdr)))
            return;
          ic_hdr0->icmp_sum = checksum;
          /*printf("%d\n", 124);*/

          /* modify to echo reply */
          i_hdr0->ip_ttl = INIT_TTL;
          ipaddr = i_hdr0->ip_src;
          i_hdr0->ip_src = i_hdr0->ip_dst;
          i_hdr0->ip_dst = ipaddr;
          i_hdr0->ip_sum = 0;
          i_hdr0->ip_sum = cksum(i_hdr0, sizeof(struct sr_ip_hdr));
          ic_hdr0->icmp_type = 0x00;
          ic_hdr0->icmp_sum = 0;
          ic_hdr0->icmp_sum = cksum(ic_hdr0, len - sizeof(struct sr_ethernet_hdr) - sizeof(struct sr_ip_hdr));
          /* search with longest prefix match */
          rtentry = sr_findLPMentry(sr->routing_table, i_hdr0->ip_dst);
          /*printf("%d\n", 125);*/
          if (rtentry != NULL)
          {
            ifc = sr_get_interface(sr, rtentry->interface);
            memcpy(e_hdr0->ether_shost, ifc->addr, ETHER_ADDR_LEN);
            /*print_hdrs(packet, len);*/
            arpentry = sr_arpcache_lookup(&(sr->cache), rtentry->gw.s_addr);
            if (arpentry != NULL)
            {
              memcpy(e_hdr0->ether_dhost, arpentry->mac, ETHER_ADDR_LEN);
              free(arpentry);
              /* send */
              /*print_hdrs(packet, len);*/
              sr_send_packet(sr, packet, len, rtentry->interface);
            }
            else
            {
              /* queue */
              arpreq = sr_arpcache_queuereq(&(sr->cache), rtentry->gw.s_addr, packet, len, rtentry->interface);
              /*printf("%d\n", 126);*/
              sr_arpcache_handle_arpreq(sr, arpreq);
              /*printf("%d\n", 127);*/
            }
          }
          /* done */
          return;
        }

        /* other types */
        else
          return;
      }

      /* with TCP or UDP */
      else if (i_hdr0->ip_p == ip_protocol_tcp || i_hdr0->ip_p == ip_protocol_udp)
      {

        /* validation */
        if (len_r + sizeof(struct sr_ip_hdr) < ICMP_DATA_SIZE)
          return;

        /**************** fill in code here *****************/
        /* generate ICMP port unreachable packet */
        /* type=3, code = 3 */

        ic_hdr0 = (struct sr_icmp_hdr *)(((uint8_t *)i_hdr0) + sizeof(struct sr_ip_hdr)); /*ic_hdr0 point set*/

        new_len = sizeof(struct sr_ethernet_hdr) + sizeof(struct sr_ip_hdr) + sizeof(struct sr_icmp_t3_hdr) + ICMP_DATA_SIZE;
        new_pck = (uint8_t *)calloc(1, new_len);

        e_hdr = (struct sr_ethernet_hdr *)new_pck;
        i_hdr = (struct sr_ip_hdr *)(((uint8_t *)e_hdr) + sizeof(struct sr_ethernet_hdr));
        ict3_hdr = (struct sr_icmp_t3_hdr *)(((uint8_t *)i_hdr) + sizeof(struct sr_ip_hdr));
        rtentry = sr_findLPMentry(sr->routing_table, i_hdr0->ip_src);
        ifc = sr_get_interface(sr, rtentry->interface);
        /* ip header*/
        i_hdr->ip_ttl = INIT_TTL;
        i_hdr->ip_v = 0x04;
        i_hdr->ip_hl = sizeof(struct sr_ip_hdr) / 4;
        i_hdr->ip_p = ip_protocol_icmp;
        i_hdr->ip_id = i_hdr0->ip_id;
        i_hdr->ip_len = htons(new_len - sizeof(struct sr_ethernet_hdr));
        i_hdr->ip_src = ifc->ip;
        i_hdr->ip_dst = i_hdr0->ip_src;
        i_hdr->ip_tos = i_hdr0->ip_tos;
        i_hdr->ip_off = i_hdr0->ip_off;
        i_hdr->ip_sum = 0;
        i_hdr->ip_sum = cksum(i_hdr, sizeof(struct sr_ip_hdr));
        /*icmp header */
        ict3_hdr->icmp_type = 0x03;
        ict3_hdr->icmp_code = 0x03;
        ict3_hdr->icmp_sum = 0;
        ict3_hdr->next_mtu = htons(0xFF);
        ict3_hdr->unused = htons(0x00);
        memcpy(ict3_hdr->data, i_hdr0, ICMP_DATA_SIZE);
        ict3_hdr->icmp_sum = cksum(ict3_hdr, new_len - sizeof(struct sr_ethernet_hdr) - sizeof(struct sr_ip_hdr));

        /*ethernet header*/
        /* search with longest prefix match*/

        e_hdr->ether_type = htons(ethertype_ip);
        if (rtentry != NULL)
        {
          memcpy(e_hdr->ether_shost, ifc->addr, ETHER_ADDR_LEN);
          arpentry = sr_arpcache_lookup(&(sr->cache), rtentry->gw.s_addr);
          /*print_hdrs(new_pck, new_len);*/
          if (arpentry != NULL)
          {
            memcpy(e_hdr->ether_dhost, arpentry->mac, ETHER_ADDR_LEN);
            free(arpentry);
            /* send */
            sr_send_packet(sr, new_pck, new_len, rtentry->interface);
          }
          else
          {
            /* queue */
            arpreq = sr_arpcache_queuereq(&(sr->cache), rtentry->gw.s_addr, new_pck, new_len, rtentry->interface);
            sr_arpcache_handle_arpreq(sr, arpreq);
          }
        }

        /*****************************************************/
        /* done */
        free(new_pck);
        return;
      }
      /* with others */
      else
        return;
    }

    /* destined elsewhere, forward */
    else
    {
      /*printf("%d\n", 13);*/
      /* refer routing table */
      rtentry = sr_findLPMentry(sr->routing_table, i_hdr0->ip_dst);
      /* hit */
      if (rtentry != NULL)
      {
        /**************** fill in code here *****************/
        /* check TTL expiration */
        if (i_hdr0->ip_ttl == 1)
        {

          /* validation */
          /*printf("%d\n", 0);*/
          if (len_r + sizeof(struct sr_ip_hdr) < ICMP_DATA_SIZE)
            return;

          /* generate ICMP time exceeded packet */
          new_len = sizeof(struct sr_ethernet_hdr) + sizeof(struct sr_ip_hdr) + sizeof(struct sr_icmp_t3_hdr) + ICMP_DATA_SIZE;
          new_pck = (uint8_t *)calloc(1, new_len);
          e_hdr = (struct sr_ethernet_hdr *)new_pck;
          i_hdr = (struct sr_ip_hdr *)((uint8_t *)e_hdr + sizeof(struct sr_ethernet_hdr));
          ict3_hdr = (struct sr_icmp_t3_hdr *)((uint8_t *)i_hdr + sizeof(struct sr_ip_hdr));
          rtentry = sr_findLPMentry(sr->routing_table, i_hdr0->ip_src);
          ifc = sr_get_interface(sr, rtentry->interface);
          /*printf("%d\n", 1);*/

          i_hdr->ip_ttl = INIT_TTL;
          i_hdr->ip_src = ifc->ip;
          i_hdr->ip_dst = i_hdr0->ip_src;
          i_hdr->ip_v = 0x04;
          i_hdr->ip_hl = sizeof(struct sr_ip_hdr) / 4;
          i_hdr->ip_p = ip_protocol_icmp;
          i_hdr->ip_id = i_hdr0->ip_id;
          i_hdr->ip_len = htons(new_len - sizeof(struct sr_ethernet_hdr));
          i_hdr->ip_tos = i_hdr0->ip_tos;
          i_hdr->ip_off = i_hdr0->ip_off;
          i_hdr->ip_sum = 0;
          i_hdr->ip_sum = cksum(i_hdr, sizeof(struct sr_ip_hdr));

          ict3_hdr->icmp_type = 0x0B;
          ict3_hdr->icmp_code = 0x00;
          memcpy(ict3_hdr->data, i_hdr0, ICMP_DATA_SIZE);
          ict3_hdr->icmp_sum = 0;
          ict3_hdr->next_mtu = htons(0xFF);
          ict3_hdr->unused = htons(0x00);
          ict3_hdr->icmp_sum = cksum(ict3_hdr, new_len - sizeof(struct sr_ethernet_hdr) - sizeof(struct sr_ip_hdr));

          /*printf("%d\n", 2);*/
          /*ethernet header*/
          e_hdr->ether_type = htons(ethertype_ip);
          if (rtentry != NULL)
          {
            memcpy(e_hdr->ether_shost, ifc->addr, ETHER_ADDR_LEN);
            arpentry = sr_arpcache_lookup(&(sr->cache), rtentry->gw.s_addr);
            /*print_hdrs(new_pck, new_len);*/
            if (arpentry != NULL)
            {
              /*printf("%d\n", 77);*/
              memcpy(e_hdr->ether_dhost, arpentry->mac, ETHER_ADDR_LEN);
              free(arpentry);
              /* send */
              sr_send_packet(sr, new_pck, new_len, rtentry->interface);
            }
            else
            {
              /* queue */
              /*printf("%d\n", 88);*/
              arpreq = sr_arpcache_queuereq(&(sr->cache), rtentry->gw.s_addr, new_pck, new_len, rtentry->interface);
              sr_arpcache_handle_arpreq(sr, arpreq);
            }
          }

          /*****************************************************/
          /* done */
          free(new_pck);
          return;
        }
        /*printf("%d\n", 21);*/
        /**************** fill in code here *****************/
        /*ethernet header*/
        /* set src MAC addr */
        ifc = sr_get_interface(sr, rtentry->interface);
        memcpy(e_hdr0->ether_shost, ifc->addr, ETHER_ADDR_LEN);
        /*printf("%d\n", 22);*/
        /* refer ARP table */
        arpentry = sr_arpcache_lookup(&(sr->cache), rtentry->gw.s_addr);
        /*printf("%d\n", 23);*/
        /* hit */
        if (arpentry != NULL)
        {
          /* set dst MAC addr */
          /*printf("%d\n", 24);*/
          memcpy(e_hdr0->ether_dhost, arpentry->mac, ETHER_ADDR_LEN);

          free(arpentry);
          /* decrement TTL */
          i_hdr0->ip_ttl = i_hdr0->ip_ttl - 1;
          i_hdr0->ip_sum = 0;
          i_hdr0->ip_sum = cksum(i_hdr0, sizeof(struct sr_ip_hdr));
          /* forward */
          sr_send_packet(sr, packet, len, rtentry->interface);
          /*****************************************************/
        }
        /* miss */
        else
        {
          /* queue */
          /*printf("%d\n", 25);*/
          arpreq = sr_arpcache_queuereq(&(sr->cache), rtentry->gw.s_addr, packet, len, rtentry->interface);
          sr_arpcache_handle_arpreq(sr, arpreq);
        }
        /* done */
        return;
      }
      /* miss */
      else
      {
        /**************** fill in code here *****************/
        ic_hdr0 = (struct sr_icmp_hdr *)(((uint8_t *)i_hdr0) + sizeof(struct sr_ip_hdr));
        if (len_r + sizeof(struct sr_ip_hdr) < ICMP_DATA_SIZE)
          return;
        /* validation */
        checksum = ic_hdr0->icmp_sum;
        /*printf("%d\n", 122);*/
        ic_hdr0->icmp_sum = 0;
        /*printf("%d\n", 123);*/
        if (checksum != cksum(ic_hdr0, len - sizeof(struct sr_ethernet_hdr) - sizeof(struct sr_ip_hdr)))
          return;
        ic_hdr0->icmp_sum = checksum;

        /* generate ICMP net unreachable packet */
        new_len = sizeof(struct sr_ethernet_hdr) + sizeof(struct sr_ip_hdr) + sizeof(struct sr_icmp_t3_hdr);
        new_pck = (uint8_t *)calloc(1, new_len);

        e_hdr = (struct sr_ethernet_hdr *)new_pck;
        i_hdr = (struct sr_ip_hdr *)((uint8_t *)e_hdr + sizeof(struct sr_ethernet_hdr));
        ict3_hdr = (struct sr_icmp_t3_hdr *)((uint8_t *)i_hdr + sizeof(struct sr_ip_hdr));
        rtentry = sr_findLPMentry(sr->routing_table, i_hdr0->ip_src);
        ifc = sr_get_interface(sr, rtentry->interface);

        i_hdr->ip_ttl = INIT_TTL;
        i_hdr->ip_src = ifc->ip;
        i_hdr->ip_dst = i_hdr0->ip_src;
        i_hdr->ip_v = 0x04;
        i_hdr->ip_hl = sizeof(struct sr_ip_hdr) / 4;
        i_hdr->ip_p = ip_protocol_icmp;
        i_hdr->ip_id = i_hdr0->ip_id;
        i_hdr->ip_len = htons(new_len - sizeof(struct sr_ethernet_hdr));
        i_hdr->ip_tos = i_hdr0->ip_tos;
        i_hdr->ip_off = i_hdr0->ip_off;
        i_hdr->ip_sum = 0;
        i_hdr->ip_sum = cksum(i_hdr, sizeof(struct sr_ip_hdr));

        ict3_hdr->icmp_type = 0x03;
        ict3_hdr->icmp_code = 0x00;
        memcpy(ict3_hdr->data, i_hdr0, ICMP_DATA_SIZE);
        ict3_hdr->next_mtu = htons(0xFF);
        ict3_hdr->unused = htons(0x00);
        ict3_hdr->icmp_sum = 0;
        ict3_hdr->icmp_sum = cksum(ict3_hdr, new_len - sizeof(struct sr_ethernet_hdr) - sizeof(struct sr_ip_hdr));

        /*printf("%d\n", 30);*/

        /*ethernet header*/
        memcpy(e_hdr->ether_shost, ifc->addr, ETHER_ADDR_LEN);
        e_hdr->ether_type = htons(ethertype_ip);
        arpentry = sr_arpcache_lookup(&(sr->cache), rtentry->gw.s_addr);
        /*sr_print_if(ifc);
        print_hdrs(new_pck, new_len);*/
        if (arpentry != NULL)
        {
          memcpy(e_hdr->ether_dhost, arpentry->mac, ETHER_ADDR_LEN);
          free(arpentry);
          /* send */
          /*print_hdrs(new_pck, new_len);*/
          sr_send_packet(sr, new_pck, new_len, rtentry->interface);
        }
        else
        {
          /* queue */
          arpreq = sr_arpcache_queuereq(&(sr->cache), rtentry->gw.s_addr, new_pck, new_len, rtentry->interface);
          sr_arpcache_handle_arpreq(sr, arpreq);
        }
        /*****************************************************/
        /* done */
        free(new_pck);
        return;
      }
    }
  }
  /* ARP packet arrived */
  else if (e_hdr0->ether_type == htons(ethertype_arp))
  {

    /* validation */
    /*printf("%s\n", "arp");*/
    if (len_r < sizeof(struct sr_arp_hdr))
      return;
    a_hdr0 = (struct sr_arp_hdr *)(((uint8_t *)e_hdr0) + sizeof(struct sr_ethernet_hdr)); /* a_hdr0 set */

    len_r = len_r - sizeof(struct sr_arp_hdr);
    rtentry = sr_findLPMentry(sr->routing_table, a_hdr0->ar_tip);
    interface = rtentry->interface;
    if (interface == NULL)
    {
      return;
    }
    /* destined to me */
    ifc = sr_get_interface(sr, interface);
    if (a_hdr0->ar_tip == ifc->ip)
    {
      /* request code */
      if (a_hdr0->ar_op == htons(arp_op_request))
      {
        /**************** fill in code here *****************/
        /* generate reply */
        new_len = sizeof(struct sr_ethernet_hdr) + sizeof(struct sr_arp_hdr);
        new_pck = (uint8_t *)calloc(1, new_len);
        e_hdr = (struct sr_ethernet_hdr *)new_pck;
        a_hdr = (struct sr_arp_hdr *)(((uint8_t *)e_hdr) + sizeof(struct sr_ethernet_hdr));
        a_hdr->ar_hrd = htons(0x0001);
        a_hdr->ar_pro = htons(ethertype_ip);
        a_hdr->ar_hln = 0x06;
        a_hdr->ar_pln = 0x04;
        a_hdr->ar_op = htons(arp_op_reply);
        memcpy(a_hdr->ar_sha, ifc->addr, ETHER_ADDR_LEN);
        a_hdr->ar_sip = a_hdr0->ar_tip;
        memcpy(a_hdr->ar_tha, a_hdr0->ar_sha, ETHER_ADDR_LEN);
        a_hdr->ar_tip = a_hdr0->ar_sip;

        /*ethernet header*/
        /*unicast*/
        memcpy(e_hdr->ether_shost, a_hdr->ar_sha, ETHER_ADDR_LEN);
        memcpy(e_hdr->ether_dhost, a_hdr->ar_tha, ETHER_ADDR_LEN);
        e_hdr->ether_type = htons(ethertype_arp);
        /* send */
        sr_send_packet(sr, new_pck, new_len, rtentry->interface);
        /*****************************************************/
        /* done */
        free(new_pck);
        return;
      }

      /* reply code */
      else if (a_hdr0->ar_op == htons(arp_op_reply))
      {
        /**************** fill in code here *****************/

        /* pass info to ARP cache */
        arpreq = sr_arpcache_insert(&(sr->cache), a_hdr0->ar_sha, a_hdr0->ar_sip);
        /*sr_arpcache_dump(&(sr->cache));*/
        /* pending request exist */

        if (arpreq != NULL)
        {
          /*send all packets on the req->packets linked list
          arpreq_destroy(req)*/
          /* decrement TTL except for self-generated packets */
          /* send */
          struct sr_packet *walk, *nxt;
          /*printf("%d\n", 999);*/
          for (walk = arpreq->packets; walk; walk = nxt)
          {
            nxt = walk->next;
            /* set dst MAC addr */
            e_hdr = (struct sr_ethernet_hdr *)walk->buf;
            memcpy(e_hdr->ether_dhost, a_hdr0->ar_sha, ETHER_ADDR_LEN);
            i_hdr = (struct sr_ip_hdr *)((uint8_t *)e_hdr + sizeof(struct sr_ethernet_hdr));
            rtentry = sr_findLPMentry(sr->routing_table, i_hdr->ip_dst);
            ifc = sr_get_interface(sr, rtentry->interface);
            if (i_hdr->ip_ttl != INIT_TTL)
            {
              i_hdr->ip_ttl = i_hdr->ip_ttl - 1;
              i_hdr->ip_sum = 0;
              i_hdr->ip_sum = cksum(i_hdr, sizeof(struct sr_ip_hdr));
            }
            /*print_hdrs(walk->buf, walk->len);*/
            sr_send_packet(sr, walk->buf, walk->len, walk->iface);
          }
          sr_arpreq_destroy(&(sr->cache), arpreq);
          /* done */
          return;
        }

        /*****************************************************/
        /* no exist */
        else
          return;
      }

      /* other codes */
      else
        return;
    }

    /* destined to others */
    else
      return;
  }

  /* other packet arrived */
  else
    return;

} /* end sr_ForwardPacket */

struct sr_rt *sr_findLPMentry(struct sr_rt *rtable, uint32_t ip_dst)
{
  struct sr_rt *entry, *lpmentry = NULL;
  uint32_t mask, lpmmask = 0;

  ip_dst = ntohl(ip_dst);

  /* scan routing table */
  for (entry = rtable; entry != NULL; entry = entry->next)
  {
    mask = ntohl(entry->mask.s_addr);
    /* longest match so far */
    if ((ip_dst & mask) == (ntohl(entry->dest.s_addr) & mask) && mask > lpmmask)
    {
      lpmentry = entry;
      lpmmask = mask;
    }
  }

  return lpmentry;
}

