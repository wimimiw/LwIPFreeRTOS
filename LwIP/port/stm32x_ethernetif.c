/**
 * <b>File:</b> stm32x_ethernetif.c
 *
 * <b>Project:</b> STM32x Eclipse demo
 *
 * <b>Description:</b> This module is used to implement a network interface bridge between
 *                     the STM32x MAC peripheral and lwIP stack.
 *
 *
 * <b>Created:</b> 27/06/2009
 *
 * <dl>
 * <dt><b>Author</b>:</dt>
 * <dd>Stefano Oliveri</dd>
 * <dt><b>E-mail:</b></dt>
 * <dd>software@stf12.net</dd>
 * </dl>
 */

/*
 * This file is a skeleton for developing Ethernet network interface
 * drivers for lwIP. Add code to the low_level functions and do a
 * search-and-replace for the word "ethernetif" to replace it with
 * something that better describes your network interface.
 */

#include "lwip/def.h"
#include "lwip/mem.h"
#include "lwip/pbuf.h"
#include "lwip/sys.h"
#include <lwip/stats.h>
#include <lwip/snmp.h>
#include <netif/etharp.h>

// Standard library include
#include <stdio.h>
#include <string.h>
#include "beth.h"

#define netifMTU								( 1500 )
#define netifINTERFACE_TASK_STACK_SIZE			( 600 )
#define netifINTERFACE_TASK_PRIORITY			( 3 )
#define IFNAME0 'd'
#define IFNAME1 'a'

/* The time to block waiting for input. */
#define emacBLOCK_TIME_WAITING_FOR_INPUT		( ( portTickType ) 100 )

extern unsigned char * s_lwip_in_buf;
extern unsigned char * s_lwip_out_buf;

struct ethernetif {
	struct eth_addr *ethaddr;
/* Add whatever per-interface state that is needed here. */
};

/* Forward declarations. */
static void ethernetif_input(void *pParams);

/* @param netif the already initialized lwip network interface structure
 *        for this ethernetif
 */
static void low_level_init(struct netif *netif) {
	/* set MAC hardware address length */
	netif->hwaddr_len = ETHARP_HWADDR_LEN;
	/* set MAC hardware address */
	netif->hwaddr[0] = configMAC_ADDR0;
	netif->hwaddr[1] = configMAC_ADDR1;
	netif->hwaddr[2] = configMAC_ADDR2;
	netif->hwaddr[3] = configMAC_ADDR3;
	netif->hwaddr[4] = configMAC_ADDR4;
	netif->hwaddr[5] = configMAC_ADDR5;
	/* maximum transfer unit */
	netif->mtu = netifMTU;
	// device capabilities.
	// don't set NETIF_FLAG_ETHARP if this device is not an ethernet one
	netif->flags |= NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP| NETIF_FLAG_LINK_UP;
	/// Initialize ethernet board
	beth_initialize(netif);
	/* Create the task that handles the EMAC. */
	sys_thread_new("ETHIN",ethernetif_input,netif,netifINTERFACE_TASK_STACK_SIZE,
			netifINTERFACE_TASK_PRIORITY);
}

/**
 * This function should do the actual transmission of the packet. The packet is
 * contained in the pbuf that is passed to the function. This pbuf
 * might be chained.
 *
 * @param netif the lwip network interface structure for this ethernetif
 * @param p the MAC packet to send (e.g. IP packet including MAC addresses and type)
 * @return ERR_OK if the packet could be sent
 *         an err_t value if the packet couldn't be sent
 *
 * @note Returning ERR_MEM here if a DMA queue of your MAC is full can lead to
 *       strange results. You might consider waiting for space in the DMA queue
 *       to become available since the stack doesn't retry to send a packet
 *       dropped because of memory failure (except for the TCP timers).
 */
static err_t low_level_output(struct netif *netif, struct pbuf *p) {
	static sys_sem_t ousem = NULL;
	if(ousem == NULL)
		sys_sem_new(&ousem,1);
	struct pbuf *q;
	u32_t l = 0;
	err_t res = ERR_OK;
#if ETH_PAD_SIZE
	pbuf_header(p, -ETH_PAD_SIZE); /* drop the padding word */
#endif
	sys_sem_wait(&ousem);
	uint8_t *buffer = (uint8_t*)ETH_GetCurrentTxBuffer();
	for (q = p; q != NULL; q = q->next) {
		/* Send the data from the pbuf to the interface, one pbuf at a
		 time. The size of the data in each pbuf is kept in the ->len
		 variable. */
		memcpy((uint8_t*) &buffer[l], (uint8_t*) q->payload, q->len);
		l += q->len;
	}
	res = ETH_TxPkt_ChainMode(l);
//	res = beth_send_buffer((u8_t*)q->payload, q->len);
//	if ( !vSendMACData(l) )
//		res = ERR_BUF;
	sys_sem_signal(&ousem);
#if ETH_PAD_SIZE
	pbuf_header(p, ETH_PAD_SIZE); /* reclaim the padding word */
#endif

	LINK_STATS_INC(link.xmit);
	return res;
}

/**
 * Should allocate a pbuf and transfer the bytes of the incoming
 * packet from the interface into the pbuf.
 *
 * @param netif the lwip network interface structure for this ethernetif
 * @return a pbuf filled with the received packet (including MAC header)
 *         NULL on memory error
 */
static struct pbuf *low_level_input(struct netif *netif) {
	static sys_sem_t insem = NULL;
	if(insem==NULL)
		sys_sem_new(&insem,1);
	struct pbuf *p, *q;
	u16_t len;
	int l = 0;
	FrameTypeDef frame;
	uint8_t* buffer;
	p = NULL;
	sys_sem_wait(&insem);
	frame = ETH_RxPkt_ChainMode();
	buffer = (uint8_t*)frame.buffer;
	/* Obtain the size of the packet and put it into the "len"
	 variable. */
	len = frame.length;
	if (len) {
#if ETH_PAD_SIZE
		len += ETH_PAD_SIZE; /* allow room for Ethernet padding */
#endif
		/* We allocate a pbuf chain of pbufs from the pool. */
		p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
		if (p != NULL) {
#if ETH_PAD_SIZE
			pbuf_header(p, -ETH_PAD_SIZE); /* drop the padding word */
#endif
			/* We iterate over the pbuf chain until we have read the entire
			 * packet into the pbuf. */
			for (q = p; q != NULL; q = q->next) {
				/* Read enough bytes to fill this pbuf in the chain. The
				 * available data in the pbuf is given by the q->len
				 * variable. */
				memcpy((uint8_t*) q->payload, (uint8_t*)&buffer[l], q->len);
				l = l + q->len;
			}
#if ETH_PAD_SIZE
			pbuf_header(p, ETH_PAD_SIZE); /* reclaim the padding word */
#endif
			LINK_STATS_INC(link.recv);
		} else {
			LINK_STATS_INC(link.memerr); LINK_STATS_INC(link.drop);

		} /* End else */
	} /* End if */
	frame.descriptor->Status = ETH_DMARxDesc_OWN;
	sys_sem_signal(&insem);
	if((ETH->DMASR & ETH_DMASR_RBUS) != (uint32_t) RESET)
	{
		ETH->DMASR = ETH_DMASR_RBUS;
		ETH->DMARPDR = 0;
	}
	return p;
}

/**
 * This function should be called when a packet is ready to be read
 * from the interface. It uses the function low_level_input() that
 * should handle the actual reception of bytes from the network
 * interface.Then the type of the received packet is determined and
 * the appropriate input function is called.
 *
 * @param netif the lwip network interface structure for this ethernetif
 */

void ethernetif_input(void *pParams) {
	struct netif *netif;
	struct eth_hdr *ethhdr;
	struct pbuf *p = NULL;
	netif = (struct netif*) pParams;
	for (;;) {
		do {
			beth_wait_packet();
			/* move received packet into a new pbuf */
			p = low_level_input(netif);
		} while (p == NULL);
		/* points to packet payload, which starts with an Ethernet header */
		ethhdr = p->payload;
		switch (htons(ethhdr->type)) {
		/* IP or ARP packet? */
		case ETHTYPE_IP:
		case ETHTYPE_ARP:
			/* full packet send to tcpip_thread to process */
			if (netif->input(p, netif) != ERR_OK) {
				LWIP_DEBUGF(NETIF_DEBUG, ("ethernetif_input: IP input error\n"));
				pbuf_free(p);
				p = NULL;
			}
			break;
		default:
			pbuf_free(p);
			p = NULL;
			break;
		}
	}
}

/**
 * Should be called at the beginning of the program to set up the
 * network interface. It calls the function low_level_init() to do the
 * actual setup of the hardware.
 *
 * This function should be passed as a parameter to netif_add().
 *
 * @param netif the lwip network interface structure for this ethernetif
 * @return ERR_OK if the loopif is initialized
 *         ERR_MEM if private data couldn't be allocated
 *         any other err_t on error
 */
err_t ethernetif_init(struct netif *netif) {
	struct ethernetif *ethernetif;

	LWIP_ASSERT("netif != NULL", (netif != NULL));

	ethernetif = mem_malloc(sizeof(struct ethernetif));

	if (ethernetif == NULL) {
		LWIP_DEBUGF(NETIF_DEBUG, ("ethernetif_init: out of memory\n"));
		return ERR_MEM;
	}

#if LWIP_NETIF_HOSTNAME
	/* Initialize interface hostname */
	netif->hostname = "lwip";
#endif /* LWIP_NETIF_HOSTNAME */

	/*
	 * Initialize the snmp variables and counters inside the struct netif.
	 * The last argument should be replaced with your link speed, in units
	 * of bits per second.
	 */
	NETIF_INIT_SNMP(netif, snmp_ifType_ethernet_csmacd, 100);

	netif->state = ethernetif;
	netif->name[0] = IFNAME0;
	netif->name[1] = IFNAME1;
	/* We directly use etharp_output() here to save a function call.
	 * You can instead declare your own function an call etharp_output()
	 * from it if you have to do some checks before sending (e.g. if link
	 * is available...)
	 */
	netif->output = etharp_output;
	netif->linkoutput = low_level_output;

	ethernetif->ethaddr = (struct eth_addr *) &(netif->hwaddr[0]);

	low_level_init(netif);

	return ERR_OK;
}
