#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdio.h>

static pthread_mutex_t ltx=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t ftx=PTHREAD_MUTEX_INITIALIZER;

static char *logfn="/var/log/quecd.log";
static int port=5004;
static int verbose=0;

static void logger(char *msg)
{
	int fd;
	struct iovec io[2];

	io[0].iov_base=msg;
	io[0].iov_len=strlen(msg);
	io[1].iov_base="$\n";
	io[1].iov_len=2;

	pthread_mutex_lock(&ltx);

	if((fd=open(logfn,O_WRONLY|O_APPEND|O_CREAT,0644))==-1)goto out;
	if(writev(fd,io,2)==io[0].iov_len+io[1].iov_len)if(verbose)
		printf("LOG: %s$\n",msg);
	close(fd);

out:	pthread_mutex_unlock(&ltx);
}

static void forward1(char *msg)
{
	int s;
	struct sockaddr_in a;
	struct iovec io[2];

	io[0].iov_base=msg;
	io[0].iov_len=strlen(msg);
	io[1].iov_base="$";
	io[1].iov_len=1;

	pthread_mutex_lock(&ftx);

	memset(&a,0,sizeof(a));
	a.sin_family=AF_INET;
	a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
	a.sin_port=htons(port);
	if((s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP))!=-1)
	{
		if(connect(s,(struct sockaddr *)&a,sizeof(a)))
		{
			close(s);
			goto out;
		}
	}
	else goto out;

	if(writev(s,io,2)==io[0].iov_len+io[1].iov_len)
		if(verbose)printf("FWD: %s$\n",msg);

	close(s);

out:	pthread_mutex_unlock(&ftx);
}

static void forward(char *msg)
{
	int i;
	int j;
	int tot;
	char *bfr;
	char *p;
	char *tail;
	char *data;
	char mem[2048];

	if(!(bfr=strdup(msg)))return;
	memcpy(bfr,"+RESP",5);

	if(!strncmp(bfr+6,"GTFRI,",5))
	{
		for(p=bfr,i=0;*p;p++)if(*p==',')i++;
		if(i>21)
		{
			tot=i-9;
			tot/=12;
			if(tot*12+9==i)
			{
				for(p=bfr,j=0;j<7;p++)if(*p==',')
				{
					if(j>=5)*p=0;
					j++;
				}
				for(tail=p;j<i-2;tail++)if(*tail==',')j++;
				for(i=0;i<tot;i++)
				{
					p++;
					data=p;
					for(j=0;j<12;p++)if(!*p||*p==',')j++;
					*(--p)=0;
					sprintf(mem,"%s,1,%s,%s",bfr,data,tail);
					forward1(mem);
				}
			}
			else forward1(bfr);
		}
		else forward1(bfr);
	}
	else forward1(bfr);

	free(bfr);
}

static int verify(char *data)
{
	int r=-1;

	if(!strncmp(data,"+RESP:",6))r=0;
	else if(!strncmp(data,"+BUFF:",6))r=1;
	else if(!strncmp(data,"+ACK:GT",7))return 4;
	else return -1;
	if(!strncmp(data+6,"GTFRI,",5)||!strncmp(data+6,"GTGEO,",5)||
		!strncmp(data+6,"GTSPD,",5)||!strncmp(data+6,"GTSOS,",5)||
		!strncmp(data+6,"GTRTL,",5)||!strncmp(data+6,"GTPNL,",5)||
		!strncmp(data+6,"GTNMR,",5)||!strncmp(data+6,"GTDIS,",5)||
		!strncmp(data+6,"GTDOG,",5)||!strncmp(data+6,"GTIGL,",5)||
		!strncmp(data+6,"GTLBC,",5)||!strncmp(data+6,"GTCGR,",5)||
		!strncmp(data+6,"GTBPL,",5)||!strncmp(data+6,"GTBTC,",5)||
		!strncmp(data+6,"GTEPF,",5)||!strncmp(data+6,"GTEPN,",5)||
		!strncmp(data+6,"GTIGF,",5)||!strncmp(data+6,"GTIGN,",5)||
		!strncmp(data+6,"GTJDR,",5)||!strncmp(data+6,"GTJDS,",5)||
		!strncmp(data+6,"GTSTC,",5)||!strncmp(data+6,"GTSTT,",5)||
		!strncmp(data+6,"GTTEM,",5)||!strncmp(data+6,"GTSWG,",5))
			r|=2;

	return r;
}

static void *worker(void *data)
{
	int len=0;
	int l;
	char *ptr;
	struct pollfd p;
	char bfr[2048];

	p.fd=(int)((long)data);
	p.events=POLLIN;

	while(1)
	{
		if(poll(&p,1,-1)<1)continue;
		if(p.revents&POLLHUP)break;
		if(!(p.revents&POLLIN))break;
		if((l=read(p.fd,bfr+len,sizeof(bfr)-len))<=0)break;
		len+=l;

		for(ptr=bfr,l=0;l<len;l++)if(bfr[l]=='$')
		{
			bfr[l]=0;

			if(verbose)printf("RCV: %s$\n",ptr);

			switch(verify(ptr))
			{
			case 3:
			case 2:	forward(ptr);
			case 0:
			case 1:	logger(ptr);
			case 4:	break;

			default:goto out;
			}

			ptr=bfr+l+1;
		}

		if(ptr>=bfr+len)len=0;
		else
		{
			len=bfr+len-ptr;
			memmove(bfr,ptr,len);
		}

		if(len==sizeof(bfr))break;
	}

out:	close(p.fd);

	pthread_exit(NULL);
}

int main(int argc,char *argv[])
{
	int x;
	int s=-1;
	pthread_t h;
	struct sockaddr_in a;
	struct pollfd p;
	pthread_attr_t attr;

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);

	while((x=getopt(argc,argv,"l:p:t:f"))!=-1)switch(x)
	{
	case 'l':
		logfn=optarg;
		break;

	case 'p':
		s=atoi(optarg);
		break;

	case 't':
		port=atoi(optarg);
		break;

	case 'f':
		verbose=1;
		break;

	default:return 1;
	}

	if(port<1||port>65535)
	{
		fprintf(stderr,"Illegal target port");
		return 1;
	}

	if(s<1||s>65535)
	{
		fprintf(stderr,"Illegal listening port");
		return 1;
	}

	memset(&a,0,sizeof(a));
	a.sin_family=AF_INET;
	a.sin_addr.s_addr=INADDR_ANY;
	a.sin_port=htons(s);

	if((s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP))==-1)
	{
		perror("socket");
		return 1;
	}

	x=1;
	if(setsockopt(s,SOL_SOCKET,SO_REUSEADDR,&x,sizeof(x)))
	{
		perror("setsockopt");
		close(s);
		return 1;
	}

	if(bind(s,(struct sockaddr *)(&a),sizeof(a)))
	{
		perror("bind");
		close(s);
		return 1;
	}

	x=1;
	if(setsockopt(s,SOL_TCP,TCP_NODELAY,&x,sizeof(x)))
	{
		perror("setsockopt");
		close(s);
		return 1;
	}

	x=1;
	if(ioctl(s,FIONBIO,&x))
	{
		perror("ioctl");
		close(s);
		return 1;
	}

	if(listen(s,5))
	{
		perror("listen");
		close(s);
		return 1;
	}

	if(!verbose)if(daemon(0,0))
	{
		perror("daemon");
		close(s);
		return 1;
	}

	p.fd=s;
	p.events=POLLIN;

	signal(SIGPIPE,SIG_IGN);

	while(1)
	{
		if(poll(&p,1,-1)<1)continue;
		if((s=accept(p.fd,NULL,NULL))==-1)continue;
		x=1;
		ioctl(s,FIONBIO,&x);
#if defined(TCP_KEEPCNT) && defined(TCP_KEEPIDLE) && defined(TCP_KEEPINTVL)
		x=2;
		setsockopt(s,IPPROTO_TCP,TCP_KEEPCNT,&x,sizeof(x));
		x=90;
		setsockopt(s,IPPROTO_TCP,TCP_KEEPIDLE,&x,sizeof(x));
		x=90;
		setsockopt(s,IPPROTO_TCP,TCP_KEEPINTVL,&x,sizeof(x));
		x=1;
		setsockopt(s,SOL_SOCKET,SO_KEEPALIVE,&x,sizeof(x));
#endif
		if(pthread_create(&h,&attr,worker,(void *)((long)s)))close(s);
	}

	return 0;
}
