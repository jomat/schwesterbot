/*
	Copyright (C) 2011 jomat <jomat+schwesterbot@jmt.gr>
	Published under the terms of the GNU General Public License (GPL).
*/

#include <sys/types.h>
#include <sys/socket.h>
#include <signal.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <ctype.h>

#ifndef IRC_HOST
#  define IRC_HOST "irc.blafasel.de"
#endif
#ifndef IRC_PORT
#  define IRC_PORT 6667
#endif
#ifndef DEBUG
#  ifndef IRC_NICK
#    define IRC_NICK "schwester"
#  endif
#  ifndef IRC_MAINCHAN
#    define IRC_MAINCHAN "#schwester"
#  endif
#  ifndef IRC_EXTRACHAN
// this is an array of char arrays seperated by
// the byte-sequence 0XA 0X4A 0X4F 0X49 0X4E 0X20
#    define IRC_EXTRACHAN "#santa"
#  endif
#else /* ifndef DEBUG */
#  ifndef IRC_NICK
#    define IRC_NICK "nuse"
#  endif
#  ifndef IRC_MAINCHAN
#    define IRC_MAINCHAN "#nuse"
#  endif
#  ifndef IRC_EXTRACHAN
#    define IRC_EXTRACHAN "#nuse2\nJOIN #nuse3"
#  endif
#endif /* ifndef DEBUG */
#ifndef SHELLFM_HOST
#  define SHELLFM_HOST "schwester.club.muc.ccc.de"
#endif
#ifndef SHELLFM_PORT
#  define SHELLFM_PORT 54311
#endif

#define IRC_BUFSIZE 5120

struct cfg_struct {
  char irc_host[128];
  int  irc_port;
  char irc_nick[32];
  char irc_mainchan[32];
  char irc_extrachan[512];
  char shellfm_host[128];
  int  shellfm_port;
} cfg;

int irc_sock=0
  ,shellfm_sock=-1;
pthread_t status_thread;

int prepare_answer(char *buf,int *words,int n);
void quit(void);

void trim(char *s) {
  char *p = s;
  int l = strlen(p);

  while (isspace(p[l - 1]))
    p[--l] = 0;
  while (*p && isspace(*p))
    ++p, --l;

  memmove(s, p, l + 1);
}

void read_cfg(char *file) {
  FILE *fd;
  char line[512];
  char *key, *keyval, *saveptr = 0;
  int extrachan_set=0;

  strncpy(cfg.irc_host,IRC_HOST,sizeof(cfg.irc_host));
  cfg.irc_port=IRC_PORT;
  strncpy(cfg.irc_nick,IRC_NICK,sizeof(cfg.irc_nick));
  strncpy(cfg.irc_mainchan,IRC_MAINCHAN,sizeof(cfg.irc_mainchan));
  cfg.irc_extrachan[0]=0;
  strncpy(cfg.shellfm_host,SHELLFM_HOST,sizeof(cfg.shellfm_host));
  cfg.shellfm_port=SHELLFM_PORT;

  fd = fopen(file, "r");

  if (NULL==fd)
    goto end;

  while (NULL!=fgets(line, sizeof(line), fd)) {
    if ('#'==line[0])
      continue;

    key = strtok_r(line, "=", &saveptr);
    if (NULL==key||0XA==0X0[key])
      continue;
    trim(key);
    keyval = strtok_r(NULL, "=", &saveptr);
    if (NULL==keyval||0XA==0X0[key])
      continue;
    trim(keyval);
    if (!strcmp("irc_host",key)) {
      strncpy(cfg.irc_host, keyval, sizeof(cfg.irc_host));
    } else if (!strcmp("irc_port",key)) {
      cfg.irc_port=atoi(keyval);
    } else if (!strcmp("irc_mainchan",key)) {
      strncpy(cfg.irc_mainchan, keyval, sizeof(cfg.irc_mainchan));
    } else if (!strcmp("irc_extrachan",key)) {
      extrachan_set=1;
      if (cfg.irc_extrachan[0]) {
        strncpy(cfg.irc_extrachan+strlen(cfg.irc_extrachan), "\nJOIN ", sizeof(cfg.irc_extrachan)-strlen(cfg.irc_extrachan));
        strncpy(cfg.irc_extrachan+strlen(cfg.irc_extrachan), keyval, sizeof(cfg.irc_extrachan)-strlen(cfg.irc_extrachan));
      } else {
        strncpy(cfg.irc_extrachan, keyval, sizeof(cfg.irc_extrachan));
      }
    } else if (!strcmp("shellfm_host",key)) {
      strncpy(cfg.shellfm_host, keyval, sizeof(cfg.shellfm_host));
    } else if (!strcmp("shellfm_port",key)) {
      cfg.shellfm_port=atoi(keyval);
    }
  }
  fclose(fd);

end:
  if(!cfg.irc_extrachan[0]&&!extrachan_set)
    strncpy(cfg.irc_extrachan,IRC_EXTRACHAN,sizeof(cfg.irc_extrachan));
}

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

void shellfm_connect()
{
  if (0>shellfm_sock)
    shellfm_sock=socket_connect(cfg.shellfm_host, cfg.shellfm_port);
}

/*
 * sends bytes of command to shell-fm and
 * writes bufsize of answer into buf or
 * ignores the answer if bufsize == 0
 */
int txrx_shellfm(char *command,int bytes,char *buf, int bufsize)
{
  int n=0;

  shellfm_connect();

  if (0>shellfm_sock)
    return -1;
 
  if (bufsize)
    buf[0]=0;

  write(shellfm_sock, command, bytes);

  if (bufsize) {
    if (-1==(n=read(shellfm_sock, buf, bufsize)))
      return -2;

    if (buf[n-1]=='\n')
      buf[n-1]=0;  // last char is an annoying \n, 0 it!
  }
 
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
  int n_fm;
  if (0>(n_fm=txrx_shellfm(INFOFORMAT_UPDATE,strlen(INFOFORMAT_UPDATE),now_playing,512)))
    return;

  if (strncmp(last_playing,now_playing,512)) {
    char irc_cmd[512];
    strncpy(last_playing,now_playing,512);
    snprintf(irc_cmd,512,"TOPIC #schwester :Now playing \"%s.\n",now_playing);
    send_irc(irc_sock,irc_cmd,strlen(irc_cmd),0);
    snprintf(irc_cmd,512,"PRIVMSG #schwester :Now playing \"%s.\n",now_playing);
    send_irc(irc_sock,irc_cmd,strlen(irc_cmd),0);
  }
}

/*
 * pthread loop to update topic and announce song changes
 */
void *update_status_loop()
{
  int snooze;
  atexit(quit);
  while(1) {
    update_status();
    do {
      snooze=sleep(3);
    } while(snooze);
  }
}

/*
 * connects to defined IRC server and sets global irc_sock as filepointer to socket
 */
void connect_irc()
{
  struct addrinfo hints
    ,*result
    ,*rp;
  int s=0;
  char portstr[6];

  memset (&hints, 0, sizeof (struct addrinfo));
  hints.ai_family = AF_INET6;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = 0;
  hints.ai_protocol = 0;

  snprintf(portstr,sizeof(portstr),"%i",cfg.irc_port);
  s = getaddrinfo(cfg.irc_host, portstr, &hints, &result);
  if (s != 0) {
    fprintf (stderr, "getaddrinfo: %s\n", gai_strerror (s));
    exit (EXIT_FAILURE);
  }  

  for (rp = result; rp != NULL; rp = rp->ai_next) {
    irc_sock = socket (rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (irc_sock == -1)
      continue;

    if (connect (irc_sock, rp->ai_addr, rp->ai_addrlen) != -1) 
      break;

    close (irc_sock); 
  }

  free(result);
}

#define find_words(irc_buf,irc_bytes_read,words) do {                \
    int i=0,j=0,arraysize=sizeof(words)/sizeof(*words);              \
    for (;j<arraysize;j++) {                                         \
      while (irc_bytes_read!=i && irc_buf[i] && irc_buf[i++]!=' ');  \
      words[j]=i;                                                    \
    }                                                                \
  } while (0);

/*
 * read current song from shell-fm
 * announce current song in irc
 * skip current song on shell-fm
 */
void cmd_skip(char *irc_buf,int *words,int *irc_bytes_read) {
# define SKIPFORMAT "info :Skipping \"%t\" by %a on %s.\n"
  char shellfm_rxbuf[512];
  int n_fm;
  int i=prepare_answer(irc_buf,words,*irc_bytes_read);
  if (0>(n_fm = txrx_shellfm(SKIPFORMAT,strlen(SKIPFORMAT),shellfm_rxbuf,512))) {
    strncpy(irc_buf+i,":shell-fm doesn't talk to me :-(\n",IRC_BUFSIZE-i);
    send_irc(irc_sock,irc_buf,strlen(irc_buf),0);
    return;
  }

  shellfm_rxbuf[n_fm]=0;
  strncpy(irc_buf+i,shellfm_rxbuf,IRC_BUFSIZE-i);
  irc_buf[n_fm+i-1]='\n';
  irc_buf[n_fm+i]=0;
  send_irc(irc_sock,irc_buf,strlen(irc_buf),0);

  txrx_shellfm("skip\n",5,NULL,0);
}

void cmd_ver(char *irc_buf,int *words,int *irc_bytes_read) {
  int i;
  char helptext[128];
  helptext[0]=0;
  snprintf(helptext,sizeof(helptext),":I was compiled on %s at %s\n",__DATE__,__TIME__);
  i=prepare_answer(irc_buf,words,*irc_bytes_read);
  strncpy(irc_buf+i,helptext,IRC_BUFSIZE-i);
  send_irc(irc_sock,irc_buf,strlen(irc_buf),0);
}


void cmd_help(char *irc_buf,int *words,int *irc_bytes_read) {
  int i;
  char helptext[512];
  helptext[0]=0;
  if ((irc_buf+words[2]+6)[0]==0xd) {  /* 0xd = carriage return */
    strncpy(helptext,":How may I satisfy you? I have a good grasp of"
      " !love, !play, !ban, !vol, !help, !info, !skip and !stop. "
      "Just ask for more :-)\n"
      ,sizeof(helptext));
  } else if ((!strncmp(irc_buf+words[2]+((irc_buf+words[2]+7)[0]=='!'?8:7),"vol",3))) {
    strncpy(helptext,":Try something like !vol 50 (I can go up to 64!) "
      "or !vol %50 (this is 32) or !vol +3 or !vol -1\n",sizeof(helptext));
  } else if (!strncmp(irc_buf+words[2]+((irc_buf+words[2]+7)[0]=='!'?8:7),"play",4)) {
    strncpy(helptext,":I can !play user/$USER/loved, "
      "user/$USER/personal, usertags/$USER/$TAG, "
      "artist/$ARTIST/similarartists, artist/$ARTIST/fans, "
      "globaltags/$TAG, user/$USER/recommended and "
      "user/$USER/playlist\n",sizeof(helptext));
  } else {
    strncpy(helptext,":No clue...\n",sizeof(helptext));
  }
  i=prepare_answer(irc_buf,words,*irc_bytes_read);
  strncpy(irc_buf+i,helptext,IRC_BUFSIZE-i);
  send_irc(irc_sock,irc_buf,strlen(irc_buf),0);
}

void cmd_stop(char *irc_buf,int *words,int *irc_bytes_read) {
  char shellfm_rxbuf[512];
# define STOPFORMAT "info :Trying to stop \"%t\" by %a on %s.\n"
  int i=prepare_answer(irc_buf,words,*irc_bytes_read);
  int n_fm;
  if (0>(n_fm = txrx_shellfm(STOPFORMAT,strlen(STOPFORMAT),shellfm_rxbuf,512))) {
    strncpy(irc_buf+i,":shell-fm doesn't talk to me :-(\n",IRC_BUFSIZE-i);
    send_irc(irc_sock,irc_buf,strlen(irc_buf),0);
    return;
  }

  shellfm_rxbuf[n_fm]=0;
  strncpy(irc_buf+i,shellfm_rxbuf,IRC_BUFSIZE-i);
  irc_buf[n_fm+i-1]='\n';
  irc_buf[n_fm+i]=0;
  txrx_shellfm("skip\n",5,NULL,0);
  send_irc(irc_sock,irc_buf,strlen(irc_buf),0);
}

void cmd_vol(char *irc_buf,int *words,int *irc_bytes_read) {
  char tmp[512],shellfm_rxbuf[512],shellfm_rxbuf2[64];
# define VOLFORMAT "info %v\n"
  int i,n_fm;

  i=prepare_answer(irc_buf,words,*irc_bytes_read);
  if (0>(n_fm = txrx_shellfm(VOLFORMAT,strlen(VOLFORMAT),shellfm_rxbuf,512))) {
    strncpy(tmp,irc_buf,sizeof(tmp));
    strncpy(tmp+i,":shell-fm doesn't talk to me :-(\n",sizeof(tmp)-i);
    send_irc(irc_sock,tmp,strlen(tmp),0);
    return;
  }
  shellfm_rxbuf[n_fm-1]=0;

  if (irc_buf[words[3]]) {
    snprintf(tmp,sizeof(tmp),"volume %s\n",irc_buf+words[3]);
    txrx_shellfm(tmp,strlen(tmp),shellfm_rxbuf2,64);
  }

  switch (irc_buf[words[3]]) {
    case 0:
      snprintf(irc_buf+i,sizeof(irc_buf)-i,":We're going at %s.\n",shellfm_rxbuf);
      break;
    case '+':
      snprintf(irc_buf+i,sizeof(irc_buf)-i
        ,":Harder! Faster! Louder! %s was too silent, %s is better!\n"
        ,shellfm_rxbuf,shellfm_rxbuf2+7); // TODO: ALARM! here we assume that shellfm
                                          // answers s. th. like "VOLUME 31"
      break;
    case '-':
      snprintf(irc_buf+i,sizeof(irc_buf)-i
        ,":Calming down. We were at %s, now it's %s.\n"
        ,shellfm_rxbuf,shellfm_rxbuf2+7); // TODO: see above
      break;
    default:
      snprintf(irc_buf+i,sizeof(irc_buf)-i
        ,":Setting volume as requested, it was %s, now it's %s.\n"
        ,shellfm_rxbuf,shellfm_rxbuf2+7); // TODO: see above
  }
  send_irc(irc_sock, irc_buf,strlen(irc_buf),0);
}

void cmd_ban(char *irc_buf,int *words,int *irc_bytes_read) {
  char shellfm_rxbuf[512];
  int n_fm
    ,i=prepare_answer(irc_buf,words,*irc_bytes_read);

# define BANFORMAT "info :Trying to ban \"%t\" by %a on %s.\n"
  if (0>(n_fm = txrx_shellfm(BANFORMAT,strlen(BANFORMAT),shellfm_rxbuf,512))) {
    strncpy(irc_buf+i,":shell-fm doesn't talk to me :-(\n",IRC_BUFSIZE-i);
    send_irc(irc_sock,irc_buf,strlen(irc_buf),0);
    return;
  }

  shellfm_rxbuf[n_fm]=0;
  strncpy(irc_buf+i,shellfm_rxbuf,IRC_BUFSIZE-i);
  irc_buf[n_fm+i-1]='\n';
  irc_buf[n_fm+i]=0;
  txrx_shellfm("ban\n",4,NULL,0);
  send_irc(irc_sock,irc_buf,strlen(irc_buf),0);
}

void cmd_love(char *irc_buf,int *words,int *irc_bytes_read) {
  char shellfm_rxbuf[512];
  int n_fm
    ,i=prepare_answer(irc_buf,words,*irc_bytes_read);

# define LOVEFORMAT "info :Loving %a with \"%t\" on %s.\n"
  if (0>(n_fm = txrx_shellfm(LOVEFORMAT,strlen(LOVEFORMAT),shellfm_rxbuf,512))) {
    strncpy(irc_buf+i,":shell-fm doesn't talk to me :-(\n",IRC_BUFSIZE-i);
    send_irc(irc_sock,irc_buf,strlen(irc_buf),0);
    return;
  }

  shellfm_rxbuf[n_fm]=0;
  strncpy(irc_buf+i,shellfm_rxbuf,IRC_BUFSIZE-i);
  irc_buf[n_fm+i-1]='\n';
  irc_buf[n_fm+i]=0;
  txrx_shellfm("love\n",5,NULL,0);
  send_irc(irc_sock,irc_buf,strlen(irc_buf),0);
}

void cmd_play(char *irc_buf,int *words,int *irc_bytes_read) {
  char tmp[512];
  int n_fm
    ,i=prepare_answer(irc_buf,words,*irc_bytes_read);
  snprintf(tmp,sizeof(tmp),"play %s\n",irc_buf+words[3]);
  if (0>(n_fm = txrx_shellfm(tmp,strlen(tmp),NULL,0))) {
    strncpy(irc_buf+i,":shell-fm doesn't talk to me :-(\n",IRC_BUFSIZE-i);
    send_irc(irc_sock,irc_buf,strlen(irc_buf),0);
    return;
  }

  strncpy(irc_buf+i,":I'll try to play this for you.\n\0",33);
  send_irc(irc_sock,irc_buf,strlen(irc_buf),0);
}

void cmd_info(char *irc_buf,int *words,int *irc_bytes_read) {
  char shellfm_rxbuf[512];
  int n_fm
    ,i=prepare_answer(irc_buf,words,*irc_bytes_read);
# define INFOFORMAT "info :Now playing \"%t\" by %a on %s.\n"
  if (0>(n_fm = txrx_shellfm(INFOFORMAT,strlen(INFOFORMAT),shellfm_rxbuf,512))) {
    strncpy(irc_buf+i,":shell-fm doesn't talk to me :-(\n",IRC_BUFSIZE-i);
    send_irc(irc_sock,irc_buf,strlen(irc_buf),0);
    return;
  }

  n_fm = txrx_shellfm(INFOFORMAT,strlen(INFOFORMAT),shellfm_rxbuf,512);
  shellfm_rxbuf[n_fm]=0;
  strncpy(irc_buf+i,shellfm_rxbuf,IRC_BUFSIZE-i);
  irc_buf[n_fm+i-1]='\n';
  irc_buf[n_fm+i]=0;
  send_irc(irc_sock,irc_buf,strlen(irc_buf),0);
}

void termination_handler (int signum) {
  exit(EXIT_SUCCESS);
}

int main(int argc, char **argv) {
  char irc_buf[IRC_BUFSIZE];
  int irc_bytes_read=0;
  struct sigaction new_action, old_action;
  char configfile[FILENAME_MAX];
  char connectstr[1024];

  snprintf(configfile, FILENAME_MAX, "%s/%s", getenv("HOME"),".schwesterbotrc");
  read_cfg(configfile);

# ifndef DEBUG
  int f=fork();
  if (f<0) exit(1); /* fork error */
  if (f>0) exit(0); /* parent exits */
# endif /* DEBUG */

  atexit(quit);

  new_action.sa_handler = termination_handler;
  sigemptyset (&new_action.sa_mask);
  new_action.sa_flags = 0;

  sigaction (SIGINT, NULL, &old_action);
  if (old_action.sa_handler != SIG_IGN)
    sigaction (SIGINT, &new_action, NULL);
  sigaction (SIGHUP, NULL, &old_action);
  if (old_action.sa_handler != SIG_IGN)
    sigaction (SIGHUP, &new_action, NULL);
  sigaction (SIGTERM, NULL, &old_action);
  if (old_action.sa_handler != SIG_IGN)
    sigaction (SIGTERM, &new_action, NULL);

  connect_irc();
 
  // set nick, join channels, etc.
  snprintf(connectstr,sizeof(connectstr),"NICK %s\nUSER %s 0 * :%s\nJOIN %s\n"
    ,cfg.irc_nick,cfg.irc_nick,cfg.irc_nick,cfg.irc_mainchan);
  if(cfg.irc_extrachan[0])
    snprintf(connectstr+strlen(connectstr),sizeof(connectstr)-strlen(connectstr),"JOIN %s\n",cfg.irc_extrachan);

  send_irc(irc_sock, connectstr, strlen(connectstr), 0);

  pthread_create(&status_thread, NULL, update_status_loop, NULL);

  // read from irc and react on it
  while ((irc_bytes_read=read(irc_sock, irc_buf, IRC_BUFSIZE))) {
    irc_buf[irc_bytes_read]=0;
    printf("-> %s%c",irc_buf,irc_buf[irc_bytes_read-1]=='\n'?0:'\n');

    if (!strncmp("PING :",irc_buf,6)) {
      // rx: "PING :irc.blafasel.de"
      // tx: "PONG :irc.blafasel.de"
      // just replace the second char
      irc_buf[1]='O';
      send_irc(irc_sock, irc_buf, irc_bytes_read, 0);
    } else {
      int words[4];
      find_words(irc_buf,irc_bytes_read,words);

      // rx: ":jomatv6!~jomat@lethe.jmt.gr PRIVMSG #jomat_testchan :!play globaltags/psybient"
      if (!strncmp(irc_buf+words[0],"PRIVMSG ",8)) {
        if (!strncmp(irc_buf+words[2]+1,"!skip",5)) {
          cmd_skip(irc_buf,words,&irc_bytes_read);
        } else if (!strncmp(irc_buf+words[2]+1,"!help",5)) {
          cmd_help(irc_buf,words,&irc_bytes_read);
        } else if (!strncmp(irc_buf+words[2]+1,"!stop",5)) {
          cmd_stop(irc_buf,words,&irc_bytes_read);
        } else if (!strncmp(irc_buf+words[2]+1,"!vol",4)) { 
          cmd_vol(irc_buf,words,&irc_bytes_read);
        } else if (!strncmp(irc_buf+words[2]+1,"!ban",4)) {
          cmd_ban(irc_buf,words,&irc_bytes_read);
        } else if (!strncmp(irc_buf+words[2]+1,"!ver",4)) {
          cmd_ver(irc_buf,words,&irc_bytes_read);
        } else if (!strncmp(irc_buf+words[2]+1,"!play",5)) {
          cmd_play(irc_buf,words,&irc_bytes_read);
        } else if (!strncmp(irc_buf+words[2]+1,"!love",5)) {
          cmd_love(irc_buf,words,&irc_bytes_read);
        } else if (!strncmp(irc_buf+words[2]+1,"!info",5)) {
          cmd_info(irc_buf,words,&irc_bytes_read);
        }
      }
    }
  }

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

void quit(void)
{
  pthread_cancel(status_thread);
  pthread_join(status_thread,NULL);

  send_irc(irc_sock,"QUIT :cu\n",strlen("QUIT :cu\n"),0);
  while(!close(irc_sock));

  txrx_shellfm("detach\n",7,NULL,0);
  while(!close(shellfm_sock));
  exit(0);
}

