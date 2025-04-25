README 20180036 권혁태

<transport.c>

I have context statements like below:

    CSTATE_CLOSED = 0,
    CSTATE_LISTEN,
    CSTATE_SYN_SENT,
    CSTATE_SYN_RCVD,
    CSTATE_ESTABLISHED,
    CSTATE_FIN_WAIT_1,
    CSTATE_FIN_WAIT_2,
    CSTATE_CLOSE_WAIT,
    CSTATE_LAST_ACK

Also I have some elements in context_t like below:

size_t SWND; 			Sending window Size, determined by MIN(RECV_WIN, CWND)
size_t SWND_prev 		the previous Sending window size before expanding when ack comes
size_t CWND; 			congestion window size
size_t RECV_WIN;		Receiver window size
size_t SendBase; 		in the Sending window, the sequence number of the SendBase position
size_t RecvBase; 		in the receiving window, the sequence number of the RecvBase
size_t next_seqnum; 		it represent the next sequence number after sending data
size_t rem; 			it is about the remaining data size that can be sent from the sliding window
uint8_t *buffer;		it is the memory for sliding window, it is used to store the data from the app with size of SWND
uint8_t *new_buffer;	 	new_buffer that the buffer would move into when the sliding window is expanding
size_t buffer_size; 		size of buffer
size_t new_buffer_size; 	size of new buffer
size_t ACK_size; 		the size of ACKED data size
bool_t is_client; 		same as is_active
FILE *client_log;
FILE *server_log;



1. 3-way handshaking
First of all, I decided whether this is client or server through checking is active.
if it is client, make and send SYN packet to the server
then server receive  the packet and send ACK
then client receive ACK packet and Send packet to server and change ESTABLISHED

2. transferring packet

first of all, I decided the event into three types

APP_DATA
NETWORK_DATA
APP_CLOSED_REQUESTED

in APP_DATA
I implement that the client send the file name which it want and the server send the data of requested file. 
whenever I send packet, I add TCP packet header with sequence number.
the sequence number starts with 1. and it increases with size of the data the host sent.

In NETWORK_DATA
I implement that the client received the data packets from server
and the server receive ACK from the client. 
the client should take off the header from the packet and contract some packet header’s value to count RCWD_BASE number
Also, I implement some part of termination in here. I changed the client’s state into CLOSED in here when they received FIN header, also changed the server’s state into CLOSE_WAIT when they received FIN header


In APP_CLOSED_REQESTED

I do termination in this part. In here, I implement sending FIN packet and Received the ACk of FIn packet.


3. sliding window

For sliding window, I create new buffer for Sliding window.
the first size is 536 but it increases to 1072, 1608 ….

However, every moment that the sliding window size increases, the additional data packet must added to it. 
Therefore, I create new buffer and memcpy the sliding window into new buffer starting with the SEND_BASE
Also, if the remain is 0, it should not receive new packets from app, so I use goto-label to break that situation.

4. 4-way handshaking

it is implemented in the APP_CLSOSED_REQUESTED
when the client finished the requesting, it sends FIN to server
also, when the server finished sending data, it sends FIN to client.

when the server waits for ACK and received it, the state changes into CLOSED.