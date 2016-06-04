
/**
 * \defgroup dtn-network dtn code source file detail
 *
 * Detailed implementation will be described in detail in this document.
 *
 * \{
 */

/**
 * \file dtn.c
 * \author Xizhe Jiang <xizhe.jiang.15\ucl.ac.uk>
 * DTN- Delay Tolerant Network Implementation source file
 *
 *
 */
#include "contiki.h"
#include "lib/list.h"
#include "lib/memb.h"
#include "lib/random.h"
#include "net/rime.h"
#include "dtn.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "button-sensors.h"
#include "dev/leds.h"


#define MAX_RETRANSMISSIONS 4   /**<the max times of retransmissions*/
#define NUM_HISTORY_ENTRIES 5   /**<the max number of history entries*/
#define NUMBER_OF_COPIES 16      /**<the default number of copies*/
#define TX_PWR 0x01             /**<tx_power preset*/
#define BC_interval 2         /**< interval for broadcast*/
rimeaddr_t my_node;             /**<global variable of the node addr*/
static int ifinit = 0;          /**<global variable to control and make sure one-time operations*/
static rimeaddr_t recvb;        /**<recived address from broadcast, used for cross-process variable transmissions */
static uint8_t b_seq = 0;       /**<continuesly incremented sequence number during the whole process*/
static struct broadcast_conn broadcast;   /**<connection of broadcast*/
static struct runicast_conn runicast;     /**<connection of runicast*/






/**
 * \brief declare message list
 *
 */
 LIST(msg_list);
/**
 * \brief allocate memory
 * \detail allocate MAX_MESSAGES of blocks of memories for message list
 *
 * \param msg_cache_memb name of the memory pool
 * \param history_entry type to add in the list
 * \param MAX_MESSAGES the limiation length of message buffer
 */
 MEMB(msg_cache_memb, struct history_entry, MAX_MESSAGES);

/*---------------------------------------------------------------------------*/
PROCESS(broadcast_process, "Broadcast process"); /**<declare process for broadcasting*/
PROCESS(test_runicast_process, "runicast test"); /**<declare process for runicasting*/
PROCESS(print_cache_process, "print cache");     /**<declare process for printing cache*/
PROCESS(create_local_process, "create local");   /**<declare process for creating local cache*/
/**
 * \brief processes to be started
 * \detail four processes are declared to be started
 * \param v1 broadcast process
 * \param v2 runicast process
 * \param v3 print buffer process
 * \param v4 create messages process
 */
 AUTOSTART_PROCESSES(&broadcast_process, &test_runicast_process, &print_cache_process, &create_local_process);
/*---------------------------------------------------------------------------*/

/**
 * \brief copy messages
 * \detail copy two dtn messages from src to dest
 *
 * \param dest the destination
 * \param src the source
 */
 void cp_msg(dtn_message *dest, dtn_message *src) {
  dest->hdr.timestamp = src->hdr.timestamp;
  dest->hdr.length = src->hdr.length;
  dest->hdr.number_of_copies = src->hdr.number_of_copies;
  dest->hdr.message_id.seq = src->hdr.message_id.seq;
  dest->hdr.reserved = src->hdr.reserved;
  rimeaddr_copy(&dest->hdr.message_id.dest, &src->hdr.message_id.dest);
  rimeaddr_copy(&dest->hdr.message_id.src, &src->hdr.message_id.src);
  strcpy(dest->msg, src->msg);
}
/**
 * \brief copy dtn_vector
 * \detail copy two dtn_vectors from source to destination
 * \param d1 the destination
 * \param d2 the source
 */
 void cp_dtn_vc(dtn_vector *d1, dtn_vector d2) {
  d1->header.len = d2.header.len;
  d1->header.ver = d2.header.ver;
  d1->header.type = d2.header.type;
  int i;
  for (i = 0; i < d1->header.len; i++) {
    cp_msg(&d1->message[i], &d2.message[i]);
  }
}
/**
 * \brief print message id
 * \details print message id for log files in a uniform format<src:dest:seq>
 * \param message the message with the msg id to be printed
 */
 void print_message_id(dtn_message message) {
  printf("%d.%d:%d.%d:%d", message.hdr.message_id.src.u8[0], message.hdr.message_id.src.u8[1]
   , message.hdr.message_id.dest.u8[0], message.hdr.message_id.dest.u8[1]
   , message.hdr.message_id.seq);
}
/**
 * \brief print the summery vector
 * \param dsv the summery vector to be printed
 */
 void pr_sum_v(dtn_summary_vector *dsv) {
  int i;
  for (i = 0; i < dsv->header.len; i++) {
    printf("[pr_BC_sum_vec]->%d->->%u:%u--%u:%u\n", i, dsv->message_ids[i].dest.u8[0]
     , dsv->message_ids[i].dest.u8[1]
     , dsv->message_ids[i].src.u8[0]
     , dsv->message_ids[i].src.u8[1]);
  }
}
/**
 * \brief print out the recive summery vector
 * \param d_rec_sum_vec the summery vector to be printed
 */
 void pr_rec_sum_v(dtn_summary_vector *d_rec_sum_vec) {
  int i;
  for (i = 0; i < d_rec_sum_vec->header.len; i++) {
    printf("recieved sum_vec_entry->%d->->%u:%u--%u:%u\n", i, d_rec_sum_vec->message_ids[i].dest.u8[0]
     , d_rec_sum_vec->message_ids[i].dest.u8[1]
     , d_rec_sum_vec->message_ids[i].src.u8[0]
     , d_rec_sum_vec->message_ids[i].src.u8[1]);
  }
}
/**
 * \brief compare two message ids
 * \detail compare two message ids by their destination,source and sequence number
 * \param id1 parameter 1
 * \param id2 parameter 2
 * \return if two messsage ids are the same
 */
 bool compare_id(dtn_msg_id id1, dtn_msg_id id2) {
  if (rimeaddr_cmp(&id1.src, &id2.src) &&
    rimeaddr_cmp(&id1.dest, &id2.dest) &&
    id1.seq == id2.seq) {
    return true;
}
return false;
}
/**
 * \brief half the number of copies
 * \detail half the number of copies for the messages to send that are successfully
 * sent and with bigger number of copies that bigger 1
 * \param tosend the dtn_vector successfully sent
 */
 void hf_cp_tosend(dtn_vector *tosend) {
  int j;
  for (j = 0; j < tosend->header.len; j++) {
    if (tosend->message[j].hdr.number_of_copies > 1) {
      tosend->message[j].hdr.number_of_copies /= 2;
    }
  }
}
/**
 * \brief half the local number of copies
 * \detail after confirmation of successfully sent of the messages,
 * the function will half the number of copies of the local messages with bigger
 * number of copies than one.
 */
 void hf_cp_history() {
  struct history_entry *hi;
  for (hi = list_head(msg_list); hi != NULL; hi = hi->next) {
    if (hi->tosend == true && hi->dtn_msg_cache.hdr.number_of_copies > 1) {
      hi->dtn_msg_cache.hdr.number_of_copies /= 2;
    }
  }

}
/**
 * \brief set the address of my node
 * \detail the node address has been set to 2 bits, set my_node will set my address
 * to be 128.12 by default
 */
 void set_my_node() {
  my_node.u8[0] = 128;
  my_node.u8[1] = 2;
  rimeaddr_set_node_addr(&my_node);
}
/**
 * \brief create a local message
 * \detail create a local message by a given sequnce number, destination and the message
 *
 * \param set_seq given a sequence number
 * \param last_bit the last bit of the destination
 * \param msg the message content given to create the messageset_
 */
 void create_local_msg(uint8_t set_seq, uint8_t last_bit, char msg[5]) {
  if (list_length(msg_list) > MAX_MESSAGES - 1) {
    memb_free(&msg_cache_memb, list_chop(msg_list));
    printf("chop now:%d msg left in buffer\n", list_length(msg_list));
  }
  struct history_entry *initiate;   /**<the start */
  initiate = memb_alloc(&msg_cache_memb);
  memset(initiate, 0, sizeof(struct history_entry));
  rimeaddr_t dest;
  dest.u8[0] = 128;
  dest.u8[1] = last_bit;
  initiate->tosend = false;
  initiate->dtn_msg_cache.hdr.timestamp = clock_seconds();
  initiate->dtn_msg_cache.hdr.number_of_copies = NUMBER_OF_COPIES;
  rimeaddr_copy(&initiate->dtn_msg_cache.hdr.message_id.src, &my_node);
  rimeaddr_copy(&initiate->dtn_msg_cache.hdr.message_id.dest, &dest);
  initiate->dtn_msg_cache.hdr.message_id.seq = set_seq;
  strcpy(initiate->dtn_msg_cache.msg, msg);
  initiate->dtn_msg_cache.hdr.length = strlen(msg);
  initiate->next = NULL;
  list_push(msg_list, initiate);
  /*print log of creating messages*/
  printf("[MSG-CRT] <");
  print_message_id(initiate->dtn_msg_cache);
  printf("> --%d\n", clock_seconds());

}
/**
 * \brief clear messages reached destination
 * \detail when a message reached the destination, it will be removed from the local
 * cache
 * \param to the destination address to send
 */
 void clr_rc_dest(const rimeaddr_t *to, uint8_t rtx) {
  struct history_entry *hi;
  for (hi = list_head(msg_list); hi != NULL; hi = hi->next) {
    if (rimeaddr_cmp(&hi->dtn_msg_cache.hdr.message_id.dest, to) && hi->tosend == true) {
      printf("[ACK-DLV] <");
      print_message_id(hi->dtn_msg_cache);
      printf("> to  %d.%d, rtx %d--%d\n", to->u8[0], to->u8[1], rtx, clock_seconds());

      list_remove(msg_list, hi);
      memb_free(&msg_cache_memb, hi);
      printf("-----------------------------------------------------------------\n");
      printf(">>>>>>>>>>>>>>>   removed %s to dest %u.%u--timestamp:%d\n"
       , hi->dtn_msg_cache.msg, hi->dtn_msg_cache.hdr.message_id.dest.u8[0]
       , hi->dtn_msg_cache.hdr.message_id.dest.u8[1], clock_seconds());
      printf("-----------------------------------------------------------------\n");
    }
  }
}
/**
 * \brief print the local cache
 * \detail the message id, number of copies and the content of the message
 */
 void print_cache() {
  printf("-----------------------------------------------------------------\n");
  struct history_entry *hi;
  printf("|printf cache timestamp:--%d\n", clock_seconds());
  for (hi = list_head(msg_list); hi != NULL; hi = hi->next) {
    printf("|print my cache: %s |dest:%u.%u |src:%u.%u |seq: %u|number_of_copies:%u|tosend:%d\n", hi->dtn_msg_cache.msg
      ,hi->dtn_msg_cache.hdr.message_id.dest.u8[0],hi->dtn_msg_cache.hdr.message_id.dest.u8[1],hi->dtn_msg_cache.hdr.message_id.src.u8[0]
      , hi->dtn_msg_cache.hdr.message_id.src.u8[1], hi->dtn_msg_cache.hdr.message_id.seq
      , hi->dtn_msg_cache.hdr.number_of_copies, hi->tosend);
  }
  printf("-----------------------------------------------------------------\n");
}
/**
 * \brief clear the summery vector
 * \detail set all of the flag in the history entry to be false and reset all the
 * variables to be 0
 * \param dsv the dtn_summery_vector to be cleared
 */

 void crt_sum_vec(dtn_summary_vector *dsv) {
  struct history_entry * he;
  dsv->header.ver = 0;
  dsv->header.type = DTN_SUMMARY_VECTOR;
  dsv->header.len = 0;
  for (he = list_head(msg_list); he != NULL; he = he->next) {
    rimeaddr_copy(&dsv->message_ids[dsv->header.len].src, &he->dtn_msg_cache.hdr.message_id.src);
    rimeaddr_copy(&dsv->message_ids[dsv->header.len].dest, &he->dtn_msg_cache.hdr.message_id.dest);
    dsv->message_ids[dsv->header.len].seq = he->dtn_msg_cache.hdr.message_id.seq;
    dsv->header.len++;
    if (dsv->header.len > 4) {
      break;
    }
  }
}
/**
 * \brief create the dtn_vector to send
 * \detail traverse the local cache and find out all the entries with the
 * flag of true and put all of them in the dtn_vector
 * \return a dtn_vector with messages to send
 */
 dtn_vector creat_tosend() {
  dtn_vector tosend;
  tosend.header.len = 0;
  tosend.header.type = DTN_MESSAGE;
  tosend.header.ver = 0;
  struct history_entry * he; /**<declare a header entry for local cache*/
  for (he = list_head(msg_list); he != NULL; he = he->next) {
    if (he->tosend == true) {
      cp_msg(&tosend.message[tosend.header.len], &he->dtn_msg_cache);
      tosend.header.len++;
    }
  }
  return tosend;
}

/**
 * \brief clear all of the flags in the local history cache
 * \detail traverse the local history and reset the flag of each entry to be false
 */
 void clear_tosend_flag() {
  struct history_entry *hi;
  for (hi = list_head(msg_list); hi != NULL; hi = hi->next) {
    hi->tosend = false;
  }
}
/**
 * \brief find the entry with the smallest number of copies to be removed
 * \detail travse the local history and find the smallest entry to be removed
 * \return return the history entry with the smallest copy of numbers
 */
 struct history_entry* return_small_cp() {
  struct history_entry *hi = NULL;
  struct history_entry *smallest = list_head(msg_list);
  for (hi = list_head(msg_list); hi != NULL; hi = hi->next) {
    if (hi->dtn_msg_cache.hdr.number_of_copies <= smallest->dtn_msg_cache.hdr.number_of_copies) {
      smallest = hi;
    }
  }
  if (smallest->dtn_msg_cache.hdr.number_of_copies == NUMBER_OF_COPIES) {
    smallest = NULL;
  }
  return smallest;
}
/**
 * \brief print ack or timeout msg
 * \details print the messages after recieving an ACK or timeout
 *
 * \param to the address to send
 * \param rtx retransmission time
 */
 void print_ACK_or_timeout_msg(const rimeaddr_t *to, uint8_t rtx) {
  dtn_vector tosend;/*messages sent to a destination for the usage of logging*/
  cp_dtn_vc(&tosend, creat_tosend());
  int i;
  for (i = 0; i < tosend.header.len; i++) {
    if (rtx <= 3) {
      printf("[ACK-SENT] <");
    } else {
      printf("[TMOUT] <");
    }
    print_message_id(tosend.message[i]);
    printf(">to %d.%d, rtx %d\n", to->u8[0], to->u8[1], rtx);
  }
}
/**
 * \brief print the messages sent to others
 *
 * \param tosend the dtn_vector to be sent
 */
 void print_tosend(dtn_vector tosend) {
  int i;
  for (i = 0; i < tosend.header.len; i++) {
    printf("[SND-RAW] <");
    print_message_id(tosend.message[i]);
    printf(" msg:%s> --%d\n", tosend.message[i].msg, clock_seconds());
  }
}
/**
 * The function will be call as a call back when recived runicast.
 * The function will store the messages from others which are not present in the
 * local cache. However, if the message's destination was myself, print it in local
 * but do not store it in the local history.
 */
 static void
 recv_runicast(struct runicast_conn *c, const rimeaddr_t *from, uint8_t seqno)
 {
  //printf("\nrecived runicast at timestamp:--%d\n",clock_seconds());
  printf("[RCV-RAW] from %d.%d --%d\n", from->u8[0], from->u8[1], clock_seconds());
  dtn_vector *d_v;
  d_v = (dtn_vector*)packetbuf_dataptr();
  struct history_entry *he; /**<temp pointer to traverse the local cache*/
  int index;
  printf("runicast message received from =>%d.%d \n",
   from->u8[0], from->u8[1]);
  int i; /*short variable for loops*/
  printf("recieved unicast header length:%d\n", d_v->header.len);
  printf("-----------------------------------------------------------------\n");
  for (i = 0; i < d_v->header.len; i++) {
    printf("show all recieved unicast MSG->:%s with seq: %d dest:%u.%u\n", d_v->message[i].msg, d_v->message[i].hdr.message_id.seq
     , d_v->message[i].hdr.message_id.dest.u8[0], d_v->message[i].hdr.message_id.dest.u8[1]);
  }
  printf("-----------------------------------------------------------------\n");
  /*compare recieved message and find out if is is present in the local cache*/
  for (index = 0; index < d_v->header.len; index++) {
    if (rimeaddr_cmp(&my_node, from)) {
      printf("received runicast from the same address as myself and stop reciving that\n");
      break;
    }
    int diff = 1;   /*mark to judge if the message is currently in the buffer*/
    for (he = list_head(msg_list); he != NULL; he = he->next) {
      if (compare_id(d_v->message[index].hdr.message_id, he->dtn_msg_cache.hdr.message_id)) {
        diff = 0;
        break;
      }
    }
    if (diff) {
      printf("recived different msg: %s\n", d_v->message[index].msg);
      if (rimeaddr_cmp(&d_v->message[index].hdr.message_id.dest, &my_node)) {
        printf("[RCV-RCH] <");
        print_message_id(d_v->message[index]);
        printf("> from %d.%d--%d\n", from->u8[0], from->u8[1], clock_seconds());
        // printf("-----------------------------------------------------------------\n");
        // printf("->->->->message :%s reached destination, good job! at timestamp--%d\n", d_v->message[index].msg,clock_seconds());
        // printf("-----------------------------------------------------------------\n");
      } else if (d_v->header.len <= MAX_MESSAGES) {
        if (list_length(msg_list) > MAX_MESSAGES - 1) {
          struct history_entry *toremove = NULL;
          if ((toremove = return_small_cp()) != NULL) {
            list_remove(msg_list, toremove);
            memb_free(&msg_cache_memb, toremove);
            printf(">>>>>>>>>The list is full, removed:%s with copy:%u\n", toremove->dtn_msg_cache.msg
             , toremove->dtn_msg_cache.hdr.number_of_copies);
          } else {
            printf(">>>>>>>>>will drop packet because the messages in the buffer haven't been sent yet!\n");
          }
        }
        if (list_length(msg_list) < MAX_MESSAGES)
        {
          struct history_entry *add;
          add = memb_alloc(&msg_cache_memb);
          add->tosend = false;
          cp_msg(&add->dtn_msg_cache, &d_v->message[index]);
          add->next = NULL;
          if (sizeof(add) <= sizeof(struct history_entry) && sizeof(add->dtn_msg_cache.msg) != 0) {
            printf("[RCV-ST] <");
            print_message_id(d_v->message[index]);
            printf("> from %u.%u --%d \n", from->u8[0], from->u8[1], clock_seconds());
            list_push(msg_list, add);
          } else {
            printf("found trashed data, not gonna push into the buffer\n");
          }
        }
      }
    }
  }
}
/*the function will be called when recived an ACK*/
static void
sent_runicast(struct runicast_conn *c, const rimeaddr_t *to, uint8_t retransmissions)
{
  printf("\n->->->runicast message sent to %d.%d at timestamp:%d, retransmissions %d\n",
   to->u8[0], to->u8[1], clock_seconds(), retransmissions);
  print_ACK_or_timeout_msg(to, retransmissions);
  clr_rc_dest(to, retransmissions);   /*clear messages reached the destination*/
  hf_cp_history();    /*halve the number of copies of the message stored in the local history*/
  clear_tosend_flag();  /*set all the tosend flat to be false*/
}
/*the function will be called when timeout*/
static void
timedout_runicast(struct runicast_conn *c, const rimeaddr_t *to, uint8_t retransmissions)
{
  printf("runicast message timed out when sending to %d.%d at timestamp:%d, retransmissions %d\n",
   to->u8[0], to->u8[1], clock_seconds(), retransmissions);
  print_ACK_or_timeout_msg(to, retransmissions);
  clear_tosend_flag(); /*set all to send message to be false*/
}
static const struct runicast_callbacks runicast_callbacks = {recv_runicast,
 sent_runicast,
 timedout_runicast
};
/*---------------------------------------------------------------------------*/
/**
 * The function will be called when recieved a broadcast. Local history will be traversed
 * and mark the messages that not present in the summery vector, prepare to send to others.
 */
 static void
 broadcast_recv(struct broadcast_conn *c, const rimeaddr_t *from)
 {
  dtn_header *temp; /*to judge the recieved header*/
  temp = packetbuf_dataptr();
  dtn_summary_vector *m;
  uint8_t send_len = 0;
  /*to judge if recived summery_vector is a type of DTN_SUMMERY_VECTOR*/
  if (temp->type == DTN_SUMMARY_VECTOR) {
    m = (dtn_summary_vector*)temp;
  }

  struct history_entry *he;
  struct history_entry remember;
  int index;
  int find = 0;
  int count_tosend = 0; /*count how many messages to send*/

  if (!runicast_is_transmitting(&runicast) && !rimeaddr_cmp(&my_node, from)){
    printf("[RCV-BC] from %d.%d (%d)\n", from->u8[0], from->u8[1], clock_seconds());
    pr_rec_sum_v(m);/*print the recieved summery_vector*/
    printf("[B_MSG] from %d.%d ", from->u8[0], from->u8[1]);
    printf("MSG payloads<ver:%d type:%d len:%d>\n", m->header.ver, m->header.type, m->header.len);
    rimeaddr_copy(&recvb, from);
    for (he = list_head(msg_list); he != NULL; he = he->next) {
      cp_msg(&remember.dtn_msg_cache, &he->dtn_msg_cache);
      for (index = 0; index < m->header.len; index++) {
        if (compare_id(m->message_ids[index], he->dtn_msg_cache.hdr.message_id)
          || (he->dtn_msg_cache.hdr.number_of_copies == 1 && !rimeaddr_cmp(&he->dtn_msg_cache.hdr.message_id.dest, from))) {
          find = 1;
        break;
      }
    }
    if (find != 1) {
      printf("recived different msg from what I have and will add %s\n", &remember.dtn_msg_cache.msg);
      if (count_tosend < MAX_MESSAGES) {

        if (rimeaddr_cmp(&he->dtn_msg_cache.hdr.message_id.src, from)) {
          printf("skip msg from src\n");
        } else {
          he->tosend = true;
        }
        count_tosend++;
      } else {
        printf("to send is full\n");
      }
    }
    find = 0;
  }
} else {
  printf("-------------------local history locked------------------\n");
}

  print_cache();/*print the local cache*/
}
/*---------------------------------------------------------------------------*/
static const struct broadcast_callbacks broadcast_call = {broadcast_recv};  /**<recieve call back for broadcast*/
/*---------------------------------------------------------------------------*/
/**
 * \brief broadcasting process
 * \details summery_vector will be broadcasted periodically
 *
 * \param v1 broadcast process
 * \param v2 event
 * \param v3 see the contiki reference, not really use in this case
 */
 PROCESS_THREAD(broadcast_process, ev, data)
 {
  static struct etimer et; /*timer to wait*/
  PROCESS_EXITHANDLER(broadcast_close(&broadcast);)
  PROCESS_BEGIN();
  set_power(0x01);
  if (!ifinit)
  {
    set_my_node();
    printf("%d.%d:config: L:%d, BC:%d, TX_PWR:%d--%d\n",my_node.u8[0],my_node.u8[1],NUMBER_OF_COPIES, BC_interval, TX_PWR, clock_seconds());
    if (b_seq >= 127) {
      b_seq = 0;
    }
    int dest1,dest2;
    do {
     dest1 = 1+random_rand()%10;
     dest2 = 1+random_rand()%10;
   }
   while(dest1 == my_node.u8[1]||dest2 == my_node.u8[1]);
   printf("[random]:%d\n",dest1);
   create_local_msg(b_seq , dest1, "ale1");
   b_seq++;
   printf("[random]:%d\n",dest2);
   create_local_msg(b_seq , dest2, "ale2");
   b_seq++;
    //create_local_msg(b_seq , 1, "alex");
   ifinit = 1;
 }

 broadcast_open(&broadcast, 229, &broadcast_call);
 while (1) {
  int interval;
  interval=BC_interval+random_rand()%5;
  etimer_set(&et, interval*CLOCK_SECOND);
  PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
    dtn_summary_vector dsv; /*dtn_summary_vector recived from other nodes*/
  crt_sum_vec(&dsv);
    //pr_sum_v(&dsv);
  packetbuf_copyfrom(&dsv, sizeof(dtn_summary_vector));
  printf("[BC] @%d\n", clock_seconds());
  broadcast_send(&broadcast);
}
PROCESS_END();
}

/*---------------------------------------------------------------------------*/
/**
 * \brief send message to others
 * \details traverse the local history and send
 *
 * \param v1 ruicast process
 * \param v even
 * \param a not really use in this case
 */
 PROCESS_THREAD(test_runicast_process, ev, data)
 {
  static dtn_vector tosend;
  PROCESS_EXITHANDLER(runicast_close(&runicast);)
  PROCESS_BEGIN();
  runicast_open(&runicast, 244, &runicast_callbacks);
  while (1) {
    static struct etimer et;
    etimer_set(&et, 2 * CLOCK_SECOND);
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
    cp_dtn_vc(&tosend, creat_tosend());
    if ((!runicast_is_transmitting(&runicast)) && tosend.header.len > 0) {
      hf_cp_tosend(&tosend);
      packetbuf_copyfrom(&tosend, sizeof(dtn_vector));
      // printf("%u.%u: sending runicast to address %u.%u with first msg:%s\n",
      //        rimeaddr_node_addr.u8[0],
      //        rimeaddr_node_addr.u8[1],
      //        recvb.u8[0],
      //        recvb.u8[1], tosend.message[0].msg);
      runicast_send(&runicast, &recvb, MAX_RETRANSMISSIONS);
      print_tosend(tosend);
    }

    if (runicast_is_transmitting(&runicast)) {
      printf("[WARNING]message can not send because of being transmissions\n");
    }
  }
  PROCESS_END();
}
/**
 * \brief print the local cache by button 2
 *
 * \param v1 print process
 * \param v2 event
 * \param v3 not really use in this case
 */
 PROCESS_THREAD(print_cache_process, ev, data) {
  PROCESS_BEGIN();
  SENSORS_ACTIVATE(button_sensor);
  while (1) {
    PROCESS_WAIT_EVENT_UNTIL(ev  == sensors_event &&
     data == &button_sensor);
    print_cache();
  }
  PROCESS_END();
}
/**
 * \brief  generate new data by button 1
 *
 * \param v1 generate message process
 * \param v2 event
 * \param v3 not really use in this case
 */
 PROCESS_THREAD(create_local_process, ev, data) {
  PROCESS_BEGIN();
  SENSORS_ACTIVATE(button2_sensor);
  while (1) {
    PROCESS_WAIT_EVENT_UNTIL(ev  == sensors_event &&
     data == &button2_sensor);
    int dest1,dest2;
    do {
     dest1 = 1+random_rand()%10;
     dest2 = 1+random_rand()%10;
   }
   while(dest1 == my_node.u8[1]||dest2 == my_node.u8[1]);
   printf("[random]:%d\n",dest1);
   create_local_msg(b_seq , dest1, "ale1");
   b_seq++;
   printf("[random]:%d\n",dest2);
   create_local_msg(b_seq , dest2, "ale2");
   b_seq++;
   // create_local_msg(b_seq , 11, "you");
    //b_seq++;
 }
 PROCESS_END();
}

/*---------------------------------------------------------------------------*/

/** \} */
