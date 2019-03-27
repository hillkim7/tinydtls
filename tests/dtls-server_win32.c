
/* This is needed for apple */
#define __APPLE_USE_RFC_3542

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "tinydtls.h" 
#include "dtls.h" 
#include "dtls_debug.h"

#pragma comment(lib, "Ws2_32.lib")

static const unsigned char ecdsa_priv_key[] = {
			0xD9, 0xE2, 0x70, 0x7A, 0x72, 0xDA, 0x6A, 0x05,
			0x04, 0x99, 0x5C, 0x86, 0xED, 0xDB, 0xE3, 0xEF,
			0xC7, 0xF1, 0xCD, 0x74, 0x83, 0x8F, 0x75, 0x70,
			0xC8, 0x07, 0x2D, 0x0A, 0x76, 0x26, 0x1B, 0xD4};

static const unsigned char ecdsa_pub_key_x[] = {
			0xD0, 0x55, 0xEE, 0x14, 0x08, 0x4D, 0x6E, 0x06,
			0x15, 0x59, 0x9D, 0xB5, 0x83, 0x91, 0x3E, 0x4A,
			0x3E, 0x45, 0x26, 0xA2, 0x70, 0x4D, 0x61, 0xF2,
			0x7A, 0x4C, 0xCF, 0xBA, 0x97, 0x58, 0xEF, 0x9A};

static const unsigned char ecdsa_pub_key_y[] = {
			0xB4, 0x18, 0xB6, 0x4A, 0xFE, 0x80, 0x30, 0xDA,
			0x1D, 0xDC, 0xF4, 0xF4, 0x2E, 0x2F, 0x26, 0x31,
			0xD0, 0x43, 0xB1, 0xFB, 0x03, 0xE2, 0x2F, 0x4D,
			0x17, 0xDE, 0x43, 0xF9, 0xF9, 0xAD, 0xEE, 0x70};

#if 0
/* SIGINT handler: set quit to 1 for graceful termination */
void
handle_sigint(int signum) {
  dsrv_stop(dsrv_get_context());
}
#endif

#ifdef DTLS_ECC
static int
get_ecdsa_key(struct dtls_context_t *ctx,
	      const session_t *session,
	      const dtls_ecdsa_key_t **result) {
  static const dtls_ecdsa_key_t ecdsa_key = {
    .curve = DTLS_ECDH_CURVE_SECP256R1,
    .priv_key = ecdsa_priv_key,
    .pub_key_x = ecdsa_pub_key_x,
    .pub_key_y = ecdsa_pub_key_y
  };

  *result = &ecdsa_key;
  return 0;
}

static int
verify_ecdsa_key(struct dtls_context_t *ctx,
		 const session_t *session,
		 const unsigned char *other_pub_x,
		 const unsigned char *other_pub_y,
		 size_t key_size) {
  return 0;
}
#endif /* DTLS_ECC */

#define DTLS_SERVER_CMD_CLOSE "server:close"
#define DTLS_SERVER_CMD_RENEGOTIATE "server:renegotiate"

static int
read_from_peer(struct dtls_context_t *ctx, 
	       session_t *session, uint8 *data, size_t len) {
  size_t i;
  for (i = 0; i < len; i++)
    printf("%c", data[i]);
  printf("\n");
  if (len >= strlen(DTLS_SERVER_CMD_CLOSE) &&
      !memcmp(data, DTLS_SERVER_CMD_CLOSE, strlen(DTLS_SERVER_CMD_CLOSE))) {
    printf("server: closing connection\n");
    dtls_close(ctx, session);
    return len;
  } else if (len >= strlen(DTLS_SERVER_CMD_RENEGOTIATE) &&
      !memcmp(data, DTLS_SERVER_CMD_RENEGOTIATE, strlen(DTLS_SERVER_CMD_RENEGOTIATE))) {
    printf("server: renegotiate connection\n");
    dtls_renegotiate(ctx, session);
    return len;
  }

  return dtls_write(ctx, session, data, len);
}

static int
send_to_peer(struct dtls_context_t *ctx, 
	     session_t *session, uint8 *data, size_t len) {

  int fd = *(int *)dtls_get_app_data(ctx);
  return sendto(fd, data, len, 0,
		&session->addr.sa, session->size);
}

static int
dtls_handle_read(struct dtls_context_t *ctx) {
  int *fd;
  session_t session;
  static uint8 buf[DTLS_MAX_BUF];
  int len;

  fd = dtls_get_app_data(ctx);

  assert(fd);

  memset(&session, 0, sizeof(session_t));
  session.size = sizeof(session.addr);
  len = recvfrom(*fd, buf, sizeof(buf), 0,
		 &session.addr.sa, &session.size);

  if (len < 0) {
    perror("recvfrom");
    return -1;
  } else {
    dtls_debug("got %d bytes from port %d\n", len, 
	     ntohs(session.addr.sin.sin_port));
    if (sizeof(buf) < len) {
      dtls_warn("packet was truncated (%d bytes lost)\n", len - sizeof(buf));
    }
  }

  return dtls_handle_message(ctx, &session, buf, len);
}    

static int
resolve_address(const char *server, struct sockaddr *dst) {
  
  struct addrinfo *res, *ainfo;
  struct addrinfo hints;
  static char addrstr[256];
  int error;

  memset(addrstr, 0, sizeof(addrstr));
  if (server && strlen(server) > 0)
    memcpy(addrstr, server, strlen(server));
  else
    memcpy(addrstr, "localhost", 9);

  memset ((char *)&hints, 0, sizeof(hints));
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_family = AF_UNSPEC;

  error = getaddrinfo(addrstr, "", &hints, &res);

  if (error != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(error));
    return error;
  }

  for (ainfo = res; ainfo != NULL; ainfo = ainfo->ai_next) {

    switch (ainfo->ai_family) {
    case AF_INET6:

      memcpy(dst, ainfo->ai_addr, ainfo->ai_addrlen);
      return ainfo->ai_addrlen;
    default:
      ;
    }
  }

  freeaddrinfo(res);
  return -1;
}

static void
usage(const char *program, const char *version) {
  const char *p;

  p = strrchr( program, '/' );
  if (!p)
    p = strrchr(program, '\\');
  if ( p )
    program = ++p;

  fprintf(stderr, "%s v%s -- DTLS client implementation\n"
    "(c) 2011-2014 Olaf Bergmann <bergmann@tzi.org>\n\n"
    "usage: %s port [verbosity]\n"
    "\t\tverbosity level (default: 3)\n",
    program, version, program);
}

static dtls_handler_t cb = {
  .write = send_to_peer,
  .read  = read_from_peer,
  .event = NULL,
#ifdef DTLS_ECC
  .get_ecdsa_key = get_ecdsa_key,
  .verify_ecdsa_key = verify_ecdsa_key
#endif /* DTLS_ECC */
};

int 
main(int argc, char **argv)
{
  dtls_context_t *the_context = NULL;
  log_t log_level = DTLS_LOG_WARN;
  fd_set rfds, wfds;
  struct timeval timeout;
  int fd, result;
  int on = 1;
  struct sockaddr_in listen_addr;

  if (argc < 2)
  {
    usage(argv[0], dtls_package_version());
    exit(1);
    return 1;
  }

  if (argc >= 3)
    log_level = atoi(argv[2]);

  memset(&listen_addr, 0, sizeof(struct sockaddr_in));

  listen_addr.sin_family = AF_INET;
  listen_addr.sin_port = htons(atoi(argv[1]));
  listen_addr.sin_addr = in4addr_any;

  dtls_set_log_level(log_level);

  {
    WSADATA w;
    if (WSAStartup(0x0101, &w) != 0)
    {
      fprintf(stderr, "Could not open Windows connection.\n");
      return -1;
    }
  }

  /* init socket and set it to non-blocking */
  fd = socket(listen_addr.sin_family, SOCK_DGRAM, 0);

  if (fd < 0) {
    dtls_alert("socket: %s\n", strerror(errno));
    return 0;
  }

  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on) ) < 0) {
    dtls_alert("setsockopt SO_REUSEADDR: %s\n", strerror(errno));
  }
#if 0
  flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    dtls_alert("fcntl: %s\n", strerror(errno));
    goto error;
  }
#endif

  if (bind(fd, (struct sockaddr *)&listen_addr, sizeof(listen_addr)) < 0) {
    dtls_alert("bind: %s\n", strerror(errno));
    goto error;
  }

  fprintf(stderr, "wait... %d\n", ntohs(listen_addr.sin_port));

  dtls_init();

  the_context = dtls_new_context(&fd);

  dtls_set_handler(the_context, &cb);

  while (1) {
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);

    FD_SET(fd, &rfds);
    /* FD_SET(fd, &wfds); */
    
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    
    result = select( fd+1, &rfds, &wfds, 0, &timeout);
    
    if (result < 0) {		/* error */
      if (errno != EINTR)
	perror("select");
    } else if (result == 0) {	/* timeout */
    } else {			/* ok */
      if (FD_ISSET(fd, &wfds))
	;
      else if (FD_ISSET(fd, &rfds)) {
	dtls_handle_read(the_context);
      }
    }
  }
  
 error:
  dtls_free_context(the_context);
  exit(0);

  return 0;
}
