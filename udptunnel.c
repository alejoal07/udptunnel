/* Wait for an incoming TCP connection.  Once it arrives, listen for UDP on
 * the specified port, then send the UDP packets (with a length header) over
 * the TCP connection */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#include "wirvars.h"
#include "host2ip.h"

#define UDPBUFFERSIZE 65536
#define TCPBUFFERSIZE (UDPBUFFERSIZE + 2) /* UDP packet + 2 (length field) */

#define SET_MAX(fd) do { if (max < (fd) + 1) { max = (fd) + 1; } } while (0)

#if (SIZEOF_SHORT == 2)
typedef unsigned short u_int16;
#else
#error Need a typedef for a 16-bit type
#endif

typedef unsigned char u_int8;

struct out_packet {
  u_int16 length;
  unsigned char buf[UDPBUFFERSIZE];
};

struct relay {
  struct sockaddr_in udpaddr;
  struct sockaddr_in tcpaddr;
  u_int8 udp_ttl;
  int multicast_udp;

  int udp_send_sock;
  int udp_recv_sock;
  int tcp_listen_sock;
  int tcp_sock;

  char buf[TCPBUFFERSIZE];
  char *buf_ptr, *packet_start;
  int packet_length;
  enum {uninitialized = 0, reading_length, reading_packet} state;
};

static int debug = 0;

/*
 * usage()
 * Print the program usage info, and exit.
 */
static void usage(char *progname) {
  fprintf(stderr, "Usage: %s -s TCP-port [-r] [-v] UDP-addr/UDP-port[/ttl]\n",
          progname);
  fprintf(stderr, "    or %s -c TCP-addr[/TCP-port] [-r] [-v] UDP-addr/UDP-port[/ttl]\n",
          progname);
  fprintf(stderr, "     -s: Server mode.  Wait for TCP connections on the port.\n");
  fprintf(stderr, "     -c: Client mode.  Connect to the given address.\n");
  fprintf(stderr, "     -r: RTP mode.  Connect/listen on ports N and N+1 for both UDP and TCP.\n");
  fprintf(stderr, "         Port numbers must be even.\n");
  fprintf(stderr, "     -v: Verbose mode.  Specify -v multiple times for increased verbosity.\n");
  exit(2);
} /* usage */


/*
 * parse_args()
 * Parse argv, and return parsed info in **relays, *relay_count, and
 * *is_server.  On failure, exit.
 */
static void parse_args(int argc, char *argv[], struct relay **relays,
                       int *relay_count, int *is_server)
{
  int c;
  char *tcphostname, *tcpportstr, *udphostname, *udpportstr, *udpttlstr;
  struct in_addr tcpaddr, udpaddr;
  int tcpport, udpport, udpttl;
  int i;

  *is_server = -1;
  *relay_count = 1;

  debug = 0;

  tcphostname = NULL;
  tcpportstr = NULL;

  while ((c = getopt(argc, argv, "s:c:rvh")) != EOF) {
    switch (c) {
    case 's':
      if (*is_server != -1) {
        fprintf(stderr, "%s: Only one of -s and -c may be specified.\n",
                argv[0]);
        exit(2);
      }
      *is_server = 1;
      tcpportstr = optarg;
      break;
    case 'c':
      if (*is_server != -1) {
        fprintf(stderr, "%s: Only one of -s and -c may be specified.\n",
                argv[0]);
        exit(2);
      }
      *is_server = 0;
      tcphostname = optarg;
      break;
    case 'r':
      *relay_count = 2;
      break;
    case 'v':
      debug++;
      break;
    case 'h':
    case '?':
    default:
      usage(argv[0]);
      break;
    }
  }

  if (*is_server == -1) {
    fprintf(stderr, "%s: You must specify one of -s and -c.\n",
            argv[0]);
    exit(2);
  }

  if (argc <= optind) {
    usage(argv[0]);
  }

  udphostname = strtok(argv[optind], ":/ ");
  udpportstr = strtok(NULL, ":/ ");
  if (udpportstr == NULL) {
    usage(argv[0]);
  }
  udpttlstr = strtok(NULL, ":/ ");

  if (!*is_server) {
    tcphostname = strtok(tcphostname, ":/ ");
    tcpportstr = strtok(NULL, ":/ ");
  }
  else {
    tcphostname = NULL;
  }

  errno = 0;
  udpport = strtol(udpportstr, NULL, 0);
  if (errno || udpport <= 0 || udpport >= 65536) {
    fprintf(stderr, "%s: invalid port number\n", udpportstr);
    exit(2);
  }

  if (udpttlstr != NULL) {
    errno = 0;
    udpttl = strtol(udpttlstr, NULL, 0);
    if (errno || udpttl < 0 || udpttl >= 256) {
      fprintf(stderr, "%s: invalid TTL\n", udpttlstr);
      exit(2);
    }
  }
  else {
    udpttl = 1;
  }

  if (tcpportstr != NULL) {
    errno = 0;
    tcpport = strtol(tcpportstr, NULL, 0);
    if (errno || tcpport <= 0 || tcpport >= 65536) {
      fprintf(stderr, "%s: invalid port number\n", tcpportstr);
      exit(2);
    }
  }
  else {
    tcpport = udpport;
  }

  if (*relay_count == 2 && (tcpport % 2 != 0 || udpport % 2 != 0)) {
    fprintf(stderr, "Port numbers must be even when using RTP mode.\n");
    exit(2);
  }

  udpaddr = host2ip(udphostname);
  if (udpaddr.s_addr == INADDR_ANY) {
    fprintf(stderr, "%s: UDP host unknown\n", udphostname);
    exit(2);
  }

  if (*is_server) {
    tcpaddr.s_addr = INADDR_ANY;
  }
  else {
    tcpaddr = host2ip(tcphostname);
    if (tcpaddr.s_addr == INADDR_ANY) {
      fprintf(stderr, "%s: TCP host unknown\n", tcphostname);
      exit(2);
    }
  }
   
  *relays = (struct relay *) calloc(*relay_count, sizeof(struct relay));
  if (relays == NULL) {
    perror("Error allocating relay structure");
    exit(1);
  }

  for (i = 0; i < *relay_count; i++) {
    (*relays)[i].udpaddr.sin_addr = udpaddr;
    (*relays)[i].udpaddr.sin_port = htons(udpport + i);
    (*relays)[i].udpaddr.sin_family = AF_INET;
    (*relays)[i].udp_ttl = udpttl;
    (*relays)[i].multicast_udp = IN_MULTICAST(htons(udpaddr.s_addr));

    (*relays)[i].tcpaddr.sin_addr = tcpaddr;
    (*relays)[i].tcpaddr.sin_port = htons(tcpport + i);
    (*relays)[i].tcpaddr.sin_family = AF_INET;
  }
} /* parse_args */


/* setup_udp_recv()
 * Set up the UDP receiving socket for the specified relay.
 * Exit if anything goes wrong.
 */
static void setup_udp_recv(struct relay *relay)
{
  int opt;
  struct sockaddr_in udp_recv_addr;

  if ((relay->udp_recv_sock = socket(PF_INET, SOCK_DGRAM, 0)) < 0) {
    perror("setup_udp_recv: socket");
    exit(1);
  }

  /* Set "reuseaddr" (and "reuseport", if it exists) */
  opt = 1;
  if (setsockopt(relay->udp_recv_sock, SOL_SOCKET, SO_REUSEADDR,
                 (void *)&opt, sizeof(opt)) < 0) {
    perror("setup_udp_recv: setsockopt(SO_REUSEADDR)");
    exit(1);
  }

#ifdef SO_REUSEPORT
  opt = 1;
  if (setsockopt(relay->udp_recv_sock, SOL_SOCKET, SO_REUSEPORT,
                 (void *)&opt, sizeof(opt)) < 0) {
    perror("setup_udp_recv: setsockopt(SO_REUSEPORT)");
    exit(1);
  }
#endif

  if (relay->multicast_udp) {
#ifdef IP_ADD_MEMBERSHIP
    struct ip_mreq mreq;  /* multicast group */

    mreq.imr_multiaddr = relay->udpaddr.sin_addr;
    mreq.imr_interface.s_addr = INADDR_ANY;

    if (setsockopt(relay->udp_recv_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                   (void *)&mreq, sizeof(mreq)) < 0) {
      perror("setup_udp_recv: setsockopt(IP_ADD_MEMBERSHIP)");
      exit(1);
    }
#else
    fprintf(stderr, "Multicast addresses not supported\n");
    exit(1);
#endif
  }

  memcpy(&udp_recv_addr, &(relay->udpaddr), sizeof(struct sockaddr_in));
  
  if (!(relay->multicast_udp)) {
    /* XXX: some platforms don't allow you to bind to a multicast addr;
       these need to bind recv_addr to INADDR_ANY regardless? */
    udp_recv_addr.sin_addr.s_addr = INADDR_ANY;
  }

  if (bind(relay->udp_recv_sock, (struct sockaddr *)&udp_recv_addr,
           sizeof(udp_recv_addr)) < 0) {
    perror("setup_udp_recv: bind");
    exit(1);
  }

  return;
} /* setup_udp_recv */


/* setup_udp_send()
 * Set up the UDP sending socket for the specified relay.
 * Exit if anything goes wrong.
 */
static void setup_udp_send(struct relay *relay)
{
  /* Create UDP socket. */
  if ((relay->udp_send_sock = socket(PF_INET, SOCK_DGRAM, 0)) < 0) {
    perror("setup_udp_send: socket");
    exit(1);
  }

  if (connect(relay->udp_send_sock, (struct sockaddr *) &(relay->udpaddr),
              sizeof(relay->udpaddr)) < 0) { 
    perror("setup_udp_send: connect");
    exit(1);
  }

  if (IN_MULTICAST(htonl(relay->udpaddr.sin_addr.s_addr))) {
#ifdef IP_MULTICAST_LOOP
    u_int8 loop = 0;

    if (setsockopt(relay->udp_send_sock, IPPROTO_IP, IP_MULTICAST_LOOP,
                   (void *)&loop, sizeof(loop)) < 0) {
      perror("setup_udp_send: setsockopt(IP_MULTICAST_LOOP)");
      exit(1);
    }
#endif

#ifdef IP_MULTICAST_TTL
    if (setsockopt(relay->udp_send_sock, IPPROTO_IP, IP_MULTICAST_TTL,
                   (void *)&(relay->udp_ttl), sizeof(relay->udp_ttl)) < 0) {
      perror("setup_udp_send: setsockopt(IP_MULTICAST_TTL)");
      exit(1);
    }
#endif
  }
} /* setup_udp_send */


/*
 * setup_server_listen()
 * Set up a TCP listening socket, and wait for an incoming connection to
 * it.  Fill in the socket in the relay structure.
 * Exit if anything goes wrong.
 */
static void setup_server_listen(struct relay *relay)
{
  int opt;

  /* Create TCP listening socket. */
  if ((relay->tcp_listen_sock = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
    perror("setup_server_listen: socket");
    exit(1);
  }
    
  /* Set "reuseaddr" (and "reuseport", if it exists) so that we don't
   * have to wait for TIME_WAIT to expire if we crash the server while
   * connections are open. */
  opt = 1;
  if (setsockopt(relay->tcp_listen_sock, SOL_SOCKET, SO_REUSEADDR,
                 (void *)&opt, sizeof(opt)) < 0) {
    perror("setup_server_listen: setsockopt(SO_REUSEADDR)");
    exit(1);
  }
  
#ifdef SO_REUSEPORT
  opt = 1;
  if (setsockopt(relay->tcp_listen_sock, SOL_SOCKET, SO_REUSEPORT,
                 (void *)&opt, sizeof(opt)) < 0) { 
    perror("setup_server_listen: setsockopt(SO_REUSEPORT)");
    exit(1);
  }
#endif

  if (bind(relay->tcp_listen_sock, (struct sockaddr *)&(relay->tcpaddr),
           sizeof(relay->tcpaddr)) < 0) {
    perror("setup_server_listen: bind");
    exit(1);
  }
    
  if (listen(relay->tcp_listen_sock, 1) < 0) {
    perror("setup_server_listen: listen");
    exit(1);
  }

  relay->tcp_sock = -1;

  if (debug) fprintf(stderr, "Listening for TCP connections on port %hu\n",
                     ntohs(relay->tcpaddr.sin_port));

} /* setup_server_listen */


/* await_incoming_connections()
 * Wait for connections to be established to all the TCP listeners.
 * Fill in the tcp_sock element of each relay.
 * Exit on any errors.
 */
static void await_incoming_connections(struct relay *relays, int relay_count) 
{
  int i;
  fd_set readfds;
  int max = 0;
  int all_connected;

  do {
    FD_ZERO(&readfds);
    all_connected = 1;
    for (i = 0; i < relay_count; i++) {
      if (relays[i].tcp_sock == -1) {
        /* Only count relays we haven't had connections on yet */
        all_connected = 0;
        FD_SET(relays[i].tcp_listen_sock, &readfds);
        SET_MAX(relays[i].tcp_listen_sock);
      }
    }
    
    if (all_connected) break;
    
    if (select(max, &readfds, NULL, NULL, NULL) < 0) {
      if (errno != EINTR) {
        perror("await_incoming_connection: select");
        exit(1);
      }
    }
    
    for (i = 0; i < relay_count; i++) {
      if (FD_ISSET(relays[i].tcp_listen_sock, &readfds)) {
        struct sockaddr_in client_addr;
        int addrlen = sizeof(client_addr);
        
        if ((relays[i].tcp_sock =
             accept(relays[i].tcp_listen_sock,
                    (struct sockaddr *) &client_addr, &addrlen)) < 0) {
          perror("await_incoming_connections: accept");
          exit(1);
        }
        
        if (debug) {
          fprintf(stderr, "TCP connection from %s/%hu\n",
                  inet_ntoa(client_addr.sin_addr),
                  ntohs(client_addr.sin_port));
        }
      }
    }
  } while (!all_connected);
  
} /* await_incoming_connections */


/* setup_tcp_client()
 * Connect the given relay to the desired address.  Fill in the tcp_sock
 * element of the relay structure.
 * Exit on failure.
 */
static void setup_tcp_client(struct relay *relay)
{
  /* Create TCP socket. */
  if ((relay->tcp_sock = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
    perror("setup_tcp_client: socket");
    exit(1);
  }

  if (connect(relay->tcp_sock, (struct sockaddr *) &(relay->tcpaddr),
              sizeof(relay->tcpaddr)) < 0) {
    perror("setup_tcp_client: connect");
    exit(1);
  }

  if (debug) fprintf(stderr, "Connected TCP to %s/%hu\n",
                     inet_ntoa(relay->tcpaddr.sin_addr),
                     ntohs(relay->tcpaddr.sin_port));
} /* connect_tcp */

/***************************** Telt - Wir Custom Code  v1.0 ******************************************/

/* udp_to_tcp()
 * A packet has arrived on the UDP port of the relay.  Forward it to the TCP
 * port.  If we need to bail out, return non-zero.
 */
static int udp_to_tcp(struct relay *relay)
{
  struct out_packet p;
  int buflen;
  struct sockaddr_in remote_udpaddr;
  int addrlen = sizeof(remote_udpaddr);

  //////////// Custom Variables /////////////////
  uint64_t imei; 
  struct atrack_wir_message wirMessage = {};
  struct out_packet sendPacket;
  struct tm * ptm;
	time_t epch;
	float floatTemp;
	float floatLat =0;
	float floatLon =0;
	int8_t wirCount=0;
  uint16_t scanPointer;
  uint8_t twoByteIOCount;
  /////////////////////////////

  if ((buflen = recvfrom(relay->udp_recv_sock, p.buf, UDPBUFFERSIZE, 0,
                         (struct sockaddr *) &remote_udpaddr,
                         &addrlen)) <= 0) {
    if (buflen < 0) {
      perror("udp_to_tcp: recv");
    }
    return 1;
  }

  if (debug > 1) {
    fprintf(stderr, "\nReceived %d byte UDP packet from %s/%hu\n", buflen,
            inet_ntoa(remote_udpaddr.sin_addr),
            ntohs(remote_udpaddr.sin_port));
    /* Print the buffer */      
    for(int i = 0; i<buflen ; i++){
      fprintf(stderr, "%02X ",p.buf[i]); 
    }
    fprintf(stderr, "\n");
    /* End Of Print the buffer */ 
  }

  if(buflen== 17 && p.buf[0]== 0x00 && p.buf[1]== 0x0F){ // Imei Registration to Server
    imei = 0;
    for(int i = 2; i<buflen ; i++){
      imei *= 10;
      imei += (p.buf[i]-0x30);
    }
    
    for (int i = 0; i < deviceCount; i++){
      if(nameMap[i].id == imei){
        wirMessage.idMapIndex = i;
        nameMap[wirMessage.idMapIndex].port = ntohs(remote_udpaddr.sin_port);
        break;
      }
    }
    fprintf(stderr, "Device registration imei: %lu\n",nameMap[wirMessage.idMapIndex].id);
    fprintf(stderr, "Asigned port: %ld\n",nameMap[wirMessage.idMapIndex].port);
  } else if(isCodec8(buflen, p.buf)){ // Check if message is codec 8 and then parse
    
    fprintf(stderr, "Codec8 Message\n");
    wirCount = 0;
    wirMessage.idMapIndex = deviceCount;
    for(int i=0;i<wirMessage.idMapIndex;i++){
      if(nameMap[i].port==ntohs(remote_udpaddr.sin_port)){ // if port is previously registered, load the map index
        wirMessage.idMapIndex = i;
      }
    }
    if(wirMessage.idMapIndex != deviceCount){ // was ID Found?
      wirMessage.id = nameMap[wirMessage.idMapIndex].id; 
      fprintf(stderr, "Message from imei: %lu\n",wirMessage.id);
    } else{ // Message sender not prevouosly registered
      fprintf(stderr, "Unregistered Sender\n");
    } 
    revmemcpy(&wirMessage.gpsDateTime,&p.buf[10],sizeof(wirMessage.gpsDateTime)); // Load Timestamp
		epch=wirMessage.gpsDateTime/1000;
		ptm = gmtime(&epch);
    fprintf(stderr, "DateTime: %02d/%02d/%02d %02d:%02d:%02d \n",ptm->tm_mday,ptm->tm_mon + 1,ptm->tm_year-100,ptm->tm_hour,ptm->tm_min,ptm->tm_sec);
    revmemcpy(&wirMessage.latitude,&p.buf[23],sizeof(wirMessage.latitude)); // Load Latitude
		floatLat=wirMessage.latitude;
		floatLat/=10000000;
		revmemcpy(&wirMessage.longitude,&p.buf[19],sizeof(wirMessage.longitude)); // Load Longitude
		floatLon=wirMessage.longitude;
    floatLon/=10000000;
    fprintf(stderr, "Coordinates: %+09.5f,%+010.5f \n",floatLat,floatLon);
    revmemcpy(&wirMessage.speed,&p.buf[32],sizeof(wirMessage.speed)); // Load Speed
		revmemcpy(&wirMessage.heading,&p.buf[29],sizeof(wirMessage.heading)); // Load Heading
		// revmemcpy(&wirMessage.event,&p.buf[34],sizeof(wirMessage.event)); // Load Event
    wirMessage.event = 2; // temporarily send all events as 2 , event implementation pending
    wirMessage.odometer = 0; // No odometer implementation
    fprintf(stderr, "Speed: %03d Heading: %03d Event: %03d \n",wirMessage.speed,wirMessage.heading,wirMessage.event);
    
    wirMessage.temperature1 = -9900;
    wirMessage.humidity1 = 3000;
    scanPointer=36; // set scan pointer to "N1 Of One Byte IO"
    scanPointer += 1+(p.buf[scanPointer]*2); // offset all 1 byte IO Values, pointer now points to  "N2 Of two Byte IO"
    twoByteIOCount = p.buf[scanPointer]; // How many two byte IO's were sent
    scanPointer++; // Point to first two byte IO ID
    for(uint8_t i = 0; i<twoByteIOCount; i++){ // Scan for Hum and Temp Values
      if(p.buf[scanPointer] == 25)revmemcpy(&wirMessage.temperature1,&p.buf[scanPointer+1],sizeof(wirMessage.temperature1)); // Load Temp Value
      else if(p.buf[scanPointer] == 86)revmemcpy(&wirMessage.humidity1,&p.buf[scanPointer+1],sizeof(wirMessage.humidity1)); // Load Hum Value
      scanPointer += 3; // Read Next Value
    }
    if(wirMessage.humidity1 == 3000){ // If not found or sensor disconnected
      wirMessage.temperature1 = -9900;  
    }
    floatTemp=wirMessage.temperature1; // Load to a float
    floatTemp/=100; // set decimal point where it's supposed to be
    fprintf(stderr, "Temperature: %+.0f \n",floatTemp);
    
    if (wirMessage.idMapIndex != deviceCount){ // if device has previously registered
      wirCount = sprintf(sendPacket.buf, "%s,%02d%02d%02d%02d%02d%02d,%+09.5f,%+010.5f,%03d,%03d,%03d,%d,%+.0f|", nameMap[wirMessage.idMapIndex].name,
                         ptm->tm_mday, ptm->tm_mon + 1, ptm->tm_year - 100, ptm->tm_hour, ptm->tm_min, ptm->tm_sec, floatLat, floatLon, wirMessage.speed, wirMessage.heading,
                         wirMessage.event, wirMessage.odometer, floatTemp);
      sendPacket.buf[wirCount] = 0;                  
      fprintf(stderr, "%s\n",sendPacket.buf);
      sendPacket.length = htons(wirCount);
      if (send(relay->tcp_sock, (void *) &sendPacket.buf, wirCount, 0) < 0) {
        perror("udp_to_tcp: send");
        return 1;
      }

      /*wirCount = sprintf(wirMessage.message, "%s,%02d%02d%02d%02d%02d%02d,%+09.5f,%+010.5f,%03d,%03d,%03d,%d,%+.0f|", nameMap[wirMessage.idMapIndex].name,
                         ptm->tm_mday, ptm->tm_mon + 1, ptm->tm_year - 100, ptm->tm_hour, ptm->tm_min, ptm->tm_sec, floatLat, floatLon, wirMessage.speed, wirMessage.heading,
                         wirMessage.event, wirMessage.odometer, floatTemp);
      wirMessage.message[wirCount] = 0;                  
      fprintf(stderr, "%s\n",wirMessage.message);*/
    }

  } // End of Codec8 Message parser

/* Original Send
  p.length = htons(buflen);
  if (send(relay->tcp_sock, (void *) &p, buflen+sizeof(p.length), 0) < 0) {
    perror("udp_to_tcp: send");
    return 1;
  }*/

  return 0;
} /* udp_to_tcp */




/************************************ End Of Custom Code ****************************************/

/**************** Original Function *********************/
/*
static int udp_to_tcp(struct relay *relay)
{
  struct out_packet p;
  int buflen;
  struct sockaddr_in remote_udpaddr;
  int addrlen = sizeof(remote_udpaddr);

  if ((buflen = recvfrom(relay->udp_recv_sock, p.buf, UDPBUFFERSIZE, 0,
                         (struct sockaddr *) &remote_udpaddr,
                         &addrlen)) <= 0) {
    if (buflen < 0) {
      perror("udp_to_tcp: recv");
    }
    return 1;
  }

  if (debug > 1) {
    fprintf(stderr, "Received %d byte UDP packet from %s/%hu\n", buflen,
            inet_ntoa(remote_udpaddr.sin_addr),
            ntohs(remote_udpaddr.sin_port));
  }
  p.length = htons(buflen);
  if (send(relay->tcp_sock, (void *) &p, buflen+sizeof(p.length), 0) < 0) {
    perror("udp_to_tcp: send");
    return 1;
  }

  return 0;
}*/ /* udp_to_tcp */

/*********************** End Of Original Function **********************/


/* tcp_to_udp()
 * The TCP socket of the relay has something for us to read.  Read it; if we
 * have a complete packet, send it to the UDP port.  If we need to bail out,
 * return non-zero.
 */
static int tcp_to_udp(struct relay *relay)
{
  int read_len;

  if (relay->state == uninitialized) {
    relay->state = reading_length;
    relay->buf_ptr = relay->buf;
    relay->packet_start = relay->buf;
    relay->packet_length = 0;
  }

  if ((read_len = read(relay->tcp_sock, relay->buf_ptr,
                       (relay->buf + TCPBUFFERSIZE - relay->buf_ptr))) <= 0) {
    if (read_len < 0) {
      perror("tcp_to_udp: read");
    }
    return 1;
  }
    
  relay->buf_ptr += read_len;
  if (relay->state == reading_length) {
    if (relay->buf_ptr - relay->packet_start < sizeof(u_int16)) {
      return 0;
    }
    relay->packet_length = ntohs(*(u_int16 *)relay->packet_start);
    relay->packet_start += sizeof(u_int16);
    relay->state = reading_packet;
  }
  if (relay->buf_ptr - relay->packet_start < relay->packet_length) {
    return 0;
  }
  /* If we get here, we have a complete UDP packet to send */
  if (debug > 1) {
    fprintf(stderr, "Received packet on TCP, length %u; sending as UDP\n",
            relay->packet_length);
  }
  if (send(relay->udp_send_sock, relay->packet_start,
           relay->packet_length, 0) < 0) {
    if (errno != ECONNREFUSED) {
      perror("tcp_to_udp: send");
      return 1;
    }
    else {
      /* There isn't a UDP listener waiting on the other end, but
       * that's okay, it's probably just not up at the moment or something.
       * Use getsockopt(SO_ERROR) to clear the error state. */
      int err, len = sizeof(err);

      if (debug > 1) {
        fprintf(stderr, "ECONNREFUSED on udp_send_sock; clearing.\n");
      }
      if (getsockopt(relay->udp_send_sock, SOL_SOCKET, SO_ERROR,
                     (void *)&err, &len) < 0) {
        perror("tcp_to_udp: getsockopt(SO_ERROR)");
        return 1;
      }
    }
  }

  memmove(relay->buf, relay->packet_start + relay->packet_length,
          relay->buf_ptr - (relay->packet_start + relay->packet_length));
  relay->buf_ptr -= relay->packet_length + (relay->packet_start - relay->buf);
  relay->packet_start = relay->buf;
  relay->state = reading_length;

  return 0;
} /* tcp_to_udp */


int main(int argc, char *argv[])
{
  struct relay *relays;
  int relay_count, is_server;
  int i;
  fd_set readfds;
  int max = 0;
  int ok;

  parse_args(argc, argv, &relays, &relay_count, &is_server);

  for (i = 0; i < relay_count; i++) {
    if (is_server) {
      setup_server_listen(&relays[i]);
    }
    else {
      setup_tcp_client(&relays[i]);
    }
    setup_udp_recv(&relays[i]);
    setup_udp_send(&relays[i]);
  }

  if (is_server) {
    await_incoming_connections(relays, relay_count);
  }

  do {
    FD_ZERO(&readfds);
    for (i = 0; i < relay_count; i++) {
      FD_SET(relays[i].tcp_sock, &readfds);
      SET_MAX(relays[i].tcp_sock);
      FD_SET(relays[i].udp_recv_sock, &readfds);
      SET_MAX(relays[i].udp_recv_sock);
    }

    if (select(max, &readfds, NULL, NULL, NULL) < 0) {
      if (errno != EINTR) {
        perror("main loop: select");
        exit(1);
      }
    }

    ok = 0;
    for (i = 0; i < relay_count; i++) {
      if (FD_ISSET(relays[i].tcp_sock, &readfds)) {
        ok += tcp_to_udp(&relays[i]);
      }
      if (FD_ISSET(relays[i].udp_recv_sock, &readfds)) {
        ok += udp_to_tcp(&relays[i]);
      }
    }
  } while (ok == 0);

  exit(0);
} /* main */
