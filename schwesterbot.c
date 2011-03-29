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
#ifndef DEBUG
#  define IRC_IDSTRING "NICK schwester\nUSER Schwester 0 * :Schwester\nJOIN #schwester\nJOIN #santa\n"
#else
#  define IRC_IDSTRING "NICK nuse\nUSER Schwester 0 * :Schwester\nJOIN #nuse\n"
#endif
#define SHELLFM_HOST "schwester.club.muc.ccc.de"
#define SHELLFM_PORT 54311

int sfd=0;

int prepare_answer(char *buf,int *words,int n);

/*
 * open a tcp connection to host on port
 * and return the sockets filehandle (positive)
 * or a negative value on error:
 *  -1: gethostbyname() == NULL
 *  -2: socket() == -1
 *  -3: connect() == -1
 */
int socket_connect(char *host, in_port_t port)
{
  struct hostent *hp;
  struct sockaddr_in addr;
  int on = 1, sock;
 
  if ((hp = gethostbyname(host)) == NULL)
    return -1;

  bcopy(hp->h_addr, &addr.sin_addr, hp->h_length);
  addr.sin_port = htons(port);
  addr.sin_family = AF_INET;
  sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
 
  if (sock == -1) 
    return -2;

  setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char *) &on,
             sizeof(int));
 
  if (connect(sock, (struct sockaddr *) &addr, sizeof(struct sockaddr_in))
      == -1) {
    close(sock);
    return -3;
  }
 
  return sock;
}

/*
 * sends bytes of command to shell-fm and
 * writes bufsize of answer into buf or
 * ignores the answer if bufsize == 0
 */
int txrx_shellfm(char *command,int bytes,char *buf, int bufsize)
{
  int socket,n=0;
 
  socket=socket_connect(SHELLFM_HOST, SHELLFM_PORT); 
 
  write(socket, command, bytes); 

  if(bufsize)
    n=read(socket, buf, bufsize);
 
  close(socket); 
 
  return n;
}

/*
 * wrapper for send() to see what's going on on stdout
 */
ssize_t send_irc(int sockfd, const void *buf, size_t len, int flags)
{
  printf("<- %s",(char*)buf);
  return send(sockfd,buf,len,flags);
}

/*
 * Queries songinfo of shell-fm and if it changed since last call,
 * announce it on channel
 */
void update_status()
{
  static char last_playing[512];
  char now_playing[512];
# define INFOFORMAT_UPDATE "info %t\" by %a on %s\n"
  int n_fm=txrx_shellfm(INFOFORMAT_UPDATE,strlen(INFOFORMAT_UPDATE),now_playing,512);
  now_playing[n_fm-1]=0;  // last char is an annoying \n, 0 it!
  if(strncmp(last_playing,now_playing,512)) {
    char irc_cmd[512];
    strncpy(last_playing,now_playing,512);
    snprintf(irc_cmd,512,"TOPIC #schwester :Now playing \"%s.\n",now_playing);
    send_irc(sfd,irc_cmd,strlen(irc_cmd),0);
    snprintf(irc_cmd,512,"PRIVMSG #schwester :Now playing \"%s.\n",now_playing);
    send_irc(sfd,irc_cmd,strlen(irc_cmd),0);
  }
}

/*
 * pthread loop to update topic and announce song changes
 */
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

/*
 * connects to defined IRC server and sets global sfd as filepointer to socket
 */
void connect_irc()
{
  struct addrinfo hints
    ,*result
    ,*rp;
  int s=0;

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
   
}

int main(int argc, char **argv) {
  pthread_t status_thread;
  char buf[5120];
  int n=0;

# ifndef DEBUG
  int f=fork();
  if (f<0) exit(1); /* fork error */
  if (f>0) exit(0); /* parent exits */
# endif /* DEBUG */

  connect_irc();
 
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
          int n_fm = txrx_shellfm(SKIPFORMAT,strlen(SKIPFORMAT),buf2,512);
          buf2[n_fm]=0;
          strncpy(buf+i,buf2,5120-i);
          buf[n_fm+i]='\n';
          buf[n_fm+i+1]=0;
          txrx_shellfm("skip\n",5,NULL,0);
          send_irc(sfd, buf,strlen(buf),0);
        } else if (!strncmp(buf+words[2]+1,"!help",5)) {
          char helptext[512];
          helptext[0]=0;
          if ((buf+words[2]+6)[0]==0xd) {  /* 0xd = carriage return */
            strncpy(helptext,":How may I satisfy you? I have a good grasp of"
              " !love, !play, !ban, !vol, !help, !info, !skip and !stop. "
              "Just ask for more :-)\n"
              ,sizeof(helptext));
          } else if ((!strncmp(buf+words[2]+((buf+words[2]+7)[0]=='!'?8:7),"vol",3))) {
            strncpy(helptext,":Try something like !vol 50 (I can go up to 64!) "
              "or !vol %50 (this is 32) or !vol +3 or !vol -1\n",sizeof(helptext));
          } else if (!strncmp(buf+words[2]+((buf+words[2]+7)[0]=='!'?8:7)," !play",6)) {
            strncpy(helptext,":I can !play user/$USER/loved, "
              "user/$USER/personal, usertags/$USER/$TAG, "
              "artist/$ARTIST/similarartists, artist/$ARTIST/fans, "
              "globaltags/$TAG, user/$USER/recommended and "
              "user/$USER/playlist\n",sizeof(helptext));
          } else {
            strncpy(helptext,":No clue...\n",sizeof(helptext));
          }
          i=prepare_answer(buf,words,n);
          strncpy(buf+i,helptext,sizeof(buf)-i);
          send_irc(sfd, buf,strlen(buf),0);
        } else if (!strncmp(buf+words[2]+1,"!stop",5)) {
          char buf2[512];
#         define STOPFORMAT "info :Trying to stop \"%t\" by %a on %s.\n"
          int n_fm = txrx_shellfm(SKIPFORMAT,strlen(SKIPFORMAT),buf2,512);
          buf2[n_fm]=0;
          i=prepare_answer(buf,words,n);
          strncpy(buf+i,buf2,5120-i);
          buf[n_fm+i]='\n';
          buf[n_fm+i+1]=0;
          txrx_shellfm("skip\n",5,NULL,0);
          send_irc(sfd, buf,strlen(buf),0);
        } else if (!strncmp(buf+words[2]+1,"!vol",4)) {
          char tmp[512],buf2[512];
#         define VOLFORMAT "info %v\n"
          int n_fm = txrx_shellfm(VOLFORMAT,strlen(VOLFORMAT),buf2,512);
          buf2[n_fm-1]=0;
          if (buf[words[3]]) {
            snprintf(tmp,sizeof(tmp),"volume %s\n",buf+words[3]);
            txrx_shellfm(tmp,strlen(tmp),NULL,0);
          }
          i=prepare_answer(buf,words,n);
          switch (buf[words[3]]) {
            case 0:
              snprintf(buf+i,sizeof(buf)-i,":We're going at %s.\n",buf2);
              break;
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
          int n_fm = txrx_shellfm(BANFORMAT,strlen(BANFORMAT),buf2,512);
          buf2[n_fm]=0;
          i=prepare_answer(buf,words,n);
          strncpy(buf+i,buf2,5120-i);
          buf[n_fm+i]='\n';
          buf[n_fm+i+1]=0;
          txrx_shellfm("ban\n",4,NULL,0);
          send_irc(sfd, buf,strlen(buf),0);
        } else if (!strncmp(buf+words[2]+1,"!play",5)) {
          char tmp[512];
          snprintf(tmp,sizeof(tmp),"play %s\n",buf+words[3]);
          i=prepare_answer(buf,words,n);
          strncpy(buf+i,":I'll try to play this for you.\n\0",33);
          txrx_shellfm(tmp,strlen(tmp),NULL,0);
          send_irc(sfd, buf,strlen(buf),0);
        } else if (!strncmp(buf+words[2]+1,"!info",5)) {
          char buf2[512];
#         define INFOFORMAT "info :Now playing \"%t\" by %a on %s.\n"
          int n_fm = txrx_shellfm(INFOFORMAT,strlen(INFOFORMAT),buf2,512);
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

/*
 * if we get a PRIVMSG in buf, edit buf so that it can be used to answer, e. g.
 * we receive: ":jomatv6!~jomat@lethe.jmt.gr PRIVMSG #jomat_testchan :!doit"
 * buf will be changed to:
 * "PRIVMSG #jomat_testchan "
 *
 * it returns the position in buf where to put the actal answer, e. g.
 * i = prepare_answer(..);
 * strncpy(buf+i,":did it",..)
 *
 * words has to be an array specifying where in buf are spaces (' ') + 1, e. g.
 * "aa bbb cccc ddddd" -> {3,7,12}
 *
 * n has to be the length of buf (ret. of read())
 */
int prepare_answer(char *buf,int *words,int n) {
  int i;
  if(buf[words[1]]=='#') { // we received from a channel, TODO: not all channels have a #
    // we want to send sth. like: "PRIVMSG #jomat_testchan :hi there\n"
    // so we just shift the first two words to the start of buf
    // attention: start with the first char, otherwise the data could be overwritten:
    // ": z!~b@y.nu PRIVMSG #jomat_testchan :!doit"
    // "PRIVMSG #jomat_testchan :did it"
    for(i=words[0];i<words[2];i++)
      buf[i-words[0]]=buf[i];

    i-=words[0];  // i is now at words[2], so sub. the chars until word[0]
  } else {  // it was a query ":jomatv6!~jomat@lethe.jmt.gr PRIVMSG schwester :!doit"
    // wanted tx:             "PRIVMSG jomatv6 :hi there\n"
    i=0;

    // increment i till 1 after the first '!' which marks the end of the sender nick
    // and not after the len of buf
    // and not if we got a \0
    while (n!=i && buf[i] && buf[i++]!='!');

    // shift the sender nick 7 to the right to make space for "PRIVMSG "
    // attn: start with the rightmost byte, otherwise you can overwrite bytes
    int j;
    for(j=i-2;j>0;j--)
      buf[j+7]=buf[j];

    buf[i+6]=' '; // place a space after the nickname
    strncpy(buf,"PRIVMSG ",8); // and copy PRIVMSG left to the nick
    // the nick shiftet his place some chars to the right, the message has to go after
    // the nick, i points somewhere near the end of the nick, so add 7.
    i+=7; 
  }
  return i;
}
