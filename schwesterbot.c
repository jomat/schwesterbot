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
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <pthread.h>

#define IRC_HOST "irc.blafasel.de"
#define IRC_PORT "6667"
//#define IRC_IDSTRING "NICK nuse\nUSER Schwester 0 * :Schwester\nJOIN #schwester\nJOIN #santa\n"
#define IRC_IDSTRING "NICK schwester\nUSER Schwester 0 * :Schwester\nJOIN #schwester\n"
#define SHELLFM_HOST "schwester.club.muc.ccc.de"
#define SHELLFM_PORT 54311

int sfd=0;

int prepare_answer(char *buf,int *words,int n);

int socket_connect(char *host, in_port_t port)
{
  struct hostent *hp;
  struct sockaddr_in addr;
  int on = 1, sock;
 
  if ((hp = gethostbyname(host)) == NULL) {
    // TODO: add some logging facility for these errors
    return -1; 
  }
  bcopy(hp->h_addr, &addr.sin_addr, hp->h_length);
  addr.sin_port = htons(port);
  addr.sin_family = AF_INET;
  sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char *) &on,
             sizeof(int));
 
  if (sock == -1) 
    return -1; 
 
  if (connect(sock, (struct sockaddr *) &addr, sizeof(struct sockaddr_in))
      == -1) {
    close(sock);
    return -1; 
  }
 
  return sock;
}
 
int txrx(char *command,int bytes,char *buf, int bufsize)
{
  int socket,n=0;
 
  socket=socket_connect(SHELLFM_HOST, SHELLFM_PORT); 
 
  write(socket, command, bytes); 

  if(bufsize)
    n=read(socket, buf, bufsize);
 
  close(socket); 
 
  return n;
}

ssize_t send_irc(int sockfd, const void *buf, size_t len, int flags)
{
  printf("<- %s",(char*)buf);
  return send(sockfd,buf,len,flags);
}

void update_status()
{
  static char last_playing[512];
  char now_playing[512];
# define INFOFORMAT_UPDATE "info %t\" by %a on %s\n"
  int n_fm=txrx(INFOFORMAT_UPDATE,strlen(INFOFORMAT_UPDATE),now_playing,512);
  now_playing[n_fm-1]=0;
  if(strncmp(last_playing,now_playing,512)) {
    char irc_cmd[512];
    strncpy(last_playing,now_playing,512);
    snprintf(irc_cmd,512,"TOPIC #schwester :Now playing \"%s.\n",now_playing);
    send_irc(sfd,irc_cmd,strlen(irc_cmd),0);
    snprintf(irc_cmd,512,"PRIVMSG #schwester :Now playing \"%s.\n",now_playing);
    send_irc(sfd,irc_cmd,strlen(irc_cmd),0);
  }
}

void *update_status_loop()
{
  int snooze;
  while(1) {
    update_status();
    do {
      snooze=sleep(3);
    } while(snooze);
  }
}

int main(int argc, char **argv) {
  pthread_t status_thread;
  char buf[5120];
  int s=0
    ,n=0
    ,f=0;
  struct addrinfo hints
    ,*result
    ,*rp;

  f=fork();
  if (f<0) exit(1); /* fork error */
  if (f>0) exit(0); /* parent exits */

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
  
  send_irc(sfd, IRC_IDSTRING, strlen(IRC_IDSTRING), 0);

  pthread_create(&status_thread, NULL, update_status_loop, NULL);

  while ((n=read(sfd, buf, sizeof(buf)))) {
    buf[n]=0;
    printf("-> %s\n",buf);
    if (!strncmp("PING :",buf,6)) { // rx: "PING :irc.blafasel.de"
      // tx: "PONG :irc.blafasel.de"
      buf[1]='O';
      send_irc(sfd, buf, n, 0);
    } else {
      int i=0
        ,words[4];
      while (n!=i && buf[i] && buf[i++]!=' ');
      words[0]=i;
      while (n!=i && buf[i] && buf[i++]!=' ');
      words[1]=i;
      while (n!=i && buf[i] && buf[i++]!=' ');
      words[2]=i;
      while (n!=i && buf[i] && buf[i++]!=' ');
      words[3]=i;
      // rx: ":jomatv6!~jomat@lethe.jmt.gr PRIVMSG #jomat_testchan :!play globaltags/psybient"
      if (!strncmp(buf+words[0],"PRIVMSG ",8)) {
        if (!strncmp(buf+words[2]+1,"!skip",5)) {
          char buf2[512];
#         define SKIPFORMAT "info :Skipping \"%t\" by %a on %s.\n"
          int n_fm = txrx(SKIPFORMAT,strlen(SKIPFORMAT),buf2,512);
          buf2[n_fm]=0;
          i=prepare_answer(buf,words,n);
          strncpy(buf+i,buf2,5120-i);
          buf[n_fm+i]='\n';
          buf[n_fm+i+1]=0;
          txrx("skip\n",5,NULL,0);
          send_irc(sfd, buf,strlen(buf),0);
        } else if (!strncmp(buf+words[2]+1,"!help",5)) {
          i=prepare_answer(buf,words,n);
#         define HELPTEXT ":How may I satisfy you? I have a good grasp of" \
            " !love, !play, !ban, !vol, !help, !info, !skip and !stop. I can !play" \
            " user/$USER/loved, user/$USER/personal, usertags/$USER/$TAG, "\
            "artist/$ARTIST/similarartists, artist/$ARTIST/fans, " \
            "globaltags/$TAG, user/$USER/recommended and user/$USER/playlist\n\0"
          strncpy(buf+i,HELPTEXT,strlen(HELPTEXT)+1);
          send_irc(sfd, buf,strlen(buf),0);
        } else if (!strncmp(buf+words[2]+1,"!stop",5)) {
          char buf2[512];
#         define STOPFORMAT "info :Trying to stop \"%t\" by %a on %s.\n"
          int n_fm = txrx(SKIPFORMAT,strlen(SKIPFORMAT),buf2,512);
          buf2[n_fm]=0;
          i=prepare_answer(buf,words,n);
          strncpy(buf+i,buf2,5120-i);
          buf[n_fm+i]='\n';
          buf[n_fm+i+1]=0;
          txrx("skip\n",5,NULL,0);
          send_irc(sfd, buf,strlen(buf),0);
        } else if (!strncmp(buf+words[2]+1,"!vol",4)) {
          char tmp[512],buf2[512];
#         define VOLFORMAT "info %v\n"
          int n_fm = txrx(VOLFORMAT,strlen(VOLFORMAT),buf2,512);
          buf2[n_fm-1]=0;
          snprintf(tmp,sizeof(tmp),"volume %s\n",buf+words[3]);
          txrx(tmp,strlen(tmp),NULL,0);
          i=prepare_answer(buf,words,n);
          switch (buf[words[3]]) {
            case '+':
              snprintf(buf+i,sizeof(buf)-i,":Harder! Faster! Louder! %s was too silent!\n",buf2);
              break;
            case '-':
              snprintf(buf+i,sizeof(buf)-i,":Calming down. We were at %s.\n",buf2);
              break;
            default:
              snprintf(buf+i,sizeof(buf)-i,":Setting volume as requested, it was %s.\n",buf2);
          }
          send_irc(sfd, buf,strlen(buf),0);
        } else if (!strncmp(buf+words[2]+1,"!ban",4)) {
          char buf2[512];
#         define BANFORMAT "info :Trying to ban \"%t\" by %a on %s.\n"
          int n_fm = txrx(BANFORMAT,strlen(BANFORMAT),buf2,512);
          buf2[n_fm]=0;
          i=prepare_answer(buf,words,n);
          strncpy(buf+i,buf2,5120-i);
          buf[n_fm+i]='\n';
          buf[n_fm+i+1]=0;
          txrx("ban\n",4,NULL,0);
          send_irc(sfd, buf,strlen(buf),0);
        } else if (!strncmp(buf+words[2]+1,"!play",5)) {
          char tmp[512];
          snprintf(tmp,sizeof(tmp),"play %s\n",buf+words[3]);
          i=prepare_answer(buf,words,n);
          strncpy(buf+i,":I'll try to play this for you.\n\0",33);
          txrx(tmp,strlen(tmp),NULL,0);
          send_irc(sfd, buf,strlen(buf),0);
        } else if (!strncmp(buf+words[2]+1,"!info",5)) {
          char buf2[512];
#         define INFOFORMAT "info :Now playing \"%t\" by %a on %s.\n"
          int n_fm = txrx(INFOFORMAT,strlen(INFOFORMAT),buf2,512);
          buf2[n_fm]=0;
          i=prepare_answer(buf,words,n);
          strncpy(buf+i,buf2,5120-i);
          buf[n_fm+i]='\n';
          buf[n_fm+i+1]=0;
          send_irc(sfd, buf,strlen(buf),0);
        }
      }
    }
  }

  send_irc(sfd,"QUIT :cu\n",strlen("QUIT :cu\n"),0);
  close(sfd);

  exit(EXIT_SUCCESS);
}

int prepare_answer(char *buf,int *words,int n) {
  int i;
  if(buf[words[1]]=='#') { // we received from a channel
    // tx: "PRIVMSG #jomat_testchan :hi there\n"
    for(i=words[0];i<words[2];i++)
      buf[i-words[0]]=buf[i];
    i-=words[0];
  } else {  // it was a query
    // tx: "PRIVMSG jomatv6 :hi there\n"
    i=0;
    while (n!=i && buf[i] && buf[i++]!='!');
    int j;
    for(j=i-2;j>0;j--)
      buf[j+7]=buf[j];
    buf[i+6]=' ';
    strncpy(buf,"PRIVMSG ",8);
    i+=7;
  }
  return i;
}
