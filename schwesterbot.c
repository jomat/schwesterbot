/*
	Copyright (C) 2011 jomat <jomat+schwesterbot@jmt.gr>
	Published under the terms of the GNU General Public License (GPL).
*/

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define IRC_HOST "irc.blafasel.de"
#define IRC_PORT "6667"
#define IRC_IDSTRING "NICK jmt_sis\nUSER jmt_sis 0 * :Schwester\nJOIN #jomat_testchan\n"

int main(int argc, char **argv) {
  char buf[5120];
  int s=0
    ,sfd=0
    ,n=0;
  struct addrinfo hints
    ,*result
    ,*rp;

  memset (&hints, 0, sizeof (struct addrinfo));
  hints.ai_family = AF_INET6;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = 0;
  hints.ai_protocol = 0;

  s = getaddrinfo (IRC_HOST, IRC_PORT, &hints, &result);
  if (s != 0) {
    fprintf (stderr, "getaddrinfo: %s\n", gai_strerror (s));
    exit (EXIT_FAILURE);
  }  

  for (rp = result; rp != NULL; rp = rp->ai_next) {
    sfd = socket (rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (sfd == -1)
      continue;

    if (connect (sfd, rp->ai_addr, rp->ai_addrlen) != -1) 
      break;

    close (sfd); 
  }
  
  send(sfd, IRC_IDSTRING, strlen(IRC_IDSTRING), 0);

  while ((n=read(sfd, buf, sizeof(buf)))) {
    buf[n]=0;
    printf("-> %s\n",buf);
    if (!strncmp("PING :",buf,6))  // "PING :irc.blafasel.de"
    {
      // "PONG :irc.blafasel.de"
      buf[1]='O';
      send(sfd, buf, n, 0);
    }
  }

  send(sfd,"QUIT :cu\n",strlen("QUIT :cu\n"),0);
  close(sfd);

  exit(EXIT_SUCCESS);
}
