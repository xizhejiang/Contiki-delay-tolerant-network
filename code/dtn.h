/**
 * \defgroup dtn-network-header dtn code header file detail
 *
 * Detailed implementation will be described in this header file.
 *
 * \{
 */
 
/**
 * \file dtn.h
 * \author Xizhe Jiang <xizhe.jiang.15\ucl.ac.uk>
 * DTN- Delay Tolerant Network Implementation header file
 *         
 *
 */

#include "contiki.h"
#include "net/rime.h"
#include <stdint.h>
#include <stdbool.h>


#define MAX_MESSAGES 5
#define MAX_MSG_VECTORS MAX_MESSAGES
#define MAX_MSG_SIZE 5
/**
 * \enum
 * enum type for the message type
 */
enum
{
	DTN_RESERVED = 0, 			/**< reserved type*/
	DTN_SUMMARY_VECTOR = 1, 	/**< dtn_sumary_vector type*/
	DTN_MESSAGE = 2,	    	/**< message type*/
	DTN_MESSAGE_DELIVERY = 3    /**< message delivery type*/
};
/**
 * \struct
 * the dtn header for summery vector and message vector
 */
typedef struct
{
  uint8_t ver  : 3;             /**< three bit version*/
  uint8_t type : 2;				/**< two bit type*/
  uint8_t len  : 3;				/**<three bit length*/
}dtn_header;

/**
 * \struct
 * the message id for a message
 */
typedef struct
{
	rimeaddr_t dest;			/**< destination of the message*/
	rimeaddr_t src;				/**< the source address of message to diliver*/
	uint8_t seq;				/**< sequnce number*/
}dtn_msg_id;

/**
 * \struct
 * message header				
 */
typedef struct
{
	uint32_t timestamp;			/**< timestamp when the message was created*/
	uint8_t number_of_copies;	/**< number of copies of the message*/
	uint8_t length;				/**< the message length*/
	dtn_msg_id  message_id;		/**< the message id*/
	uint8_t reserved;			/**< reserved space*/
}dtn_msg_header;

/**
 * \struct 
 * the summery vector of the local history
 */
typedef struct
{
	dtn_header header;			/**< the header of the summery vector*/
	dtn_msg_id message_ids[MAX_MSG_VECTORS];	/**< list of message ids*/
}dtn_summary_vector;
/**
 * \struct
 * dtn message
 */
typedef struct
{
	dtn_msg_header hdr; 		/**< header of dtn message*/
	char msg[MAX_MSG_SIZE];		/**< message content*/
}dtn_message; 

/**
 * \struct
 * message vector send to other nodes
 */
typedef struct
{
  dtn_header header;  			/**<message vector header*/
  dtn_message message[MAX_MESSAGES];	/**< list of messages to send*/
}dtn_vector;

/**
 * \struct
 * \brief local cache
 * this is the local cache to cache messages, it has been 
 * constrained to hold only 5 messages
 * 
 */
struct history_entry {
  struct history_entry *next;	/**< pointer to the next entry*/
  dtn_message dtn_msg_cache;	/**< message cache*/
  bool tosend;					/**< flag of sending to others or not*/
};
/** \} */


