#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <readline/readline.h>
#include <readline/history.h>

static int running=1;
static struct pollfd p[3];

static void dosend(char *cmd)
{
	p[2].events=POLLOUT;
	if(poll(&p[2],1,0)!=1||p[2].revents!=POLLOUT||
		write(p[2].fd,cmd,strlen(cmd))!=strlen(cmd))
	{
		close(p[2].fd);
		p[2].fd=-1;
		p[2].revents=0;
	}
	p[2].events=POLLIN;
}

static void docmd(char *cmd)
{
	char *ptr;

	if(!cmd)running=0;
	else if(*cmd)
	{
		for(ptr=cmd;*ptr;ptr++)if(*ptr=='\n')*ptr=0;
		if(!strcmp(cmd,"exit"))running=0;
		else if(!strcmp(cmd,"quit"))running=0;
		else if(!strcmp(cmd,"q"))running=0;
		else if(!strncmp(cmd,"AT+GT",5))
		{
			if(strlen(cmd)>=15)if(cmd[8]=='=')
			{
				for(ptr=cmd;*ptr;ptr++);
				if(ptr[-1]=='$')if(p[2].fd!=-1)dosend(cmd);
			}
		}
	}
}

static int mklisten(int port)
{
	int s;
	int x;
	struct sockaddr_in a;

	if((s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP))==-1)return -1;

	x=1;
	if(setsockopt(s,SOL_SOCKET,SO_REUSEADDR,&x,sizeof(x)))
	{
		close(s);
		return -1;
	}

	memset(&a,0,sizeof(a));
	a.sin_family=AF_INET;
	a.sin_port=htons(port);
	if(bind(s,(struct sockaddr *)(&a),sizeof(a)))
	{
		close(s);
		return -1;
	}

	x=1;
	if(setsockopt(s,SOL_TCP,TCP_NODELAY,&x,sizeof(x)))
	{
		close(s);
		return -1;
	}

	x=1;
	if(ioctl(s,FIONBIO,&x))
	{
		close(s);
		return -1;
	}

	if(listen(s,5))
	{
		close(s);
		return -1;
	}

	return s;
}

int main(int argc,char *argv[])
{
	int c;
	int port=-1;
	int len=0;
	char *ptr;
	char bfr[8192];

	while((c=getopt(argc,argv,"p:"))!=-1)switch(c)
	{
	case 'p':
		port=atoi(optarg);
		break;
	}

	if(port<1||port>65535)
	{
		fprintf(stderr,"Illegal port\n");
		return 1;
	}

	p[0].fd=0;
	p[0].events=POLLIN;
	if((p[1].fd=mklisten(port))==-1)return 1;
	p[1].events=POLLIN;
	p[2].fd=-1;
	p[2].events=POLLIN;

	rl_bind_key ('\t',rl_insert);
	rl_callback_handler_install("> ",docmd);

	while(running)
	{
		p[2].revents=0;
		if(poll(p,p[2].fd==-1?2:3,-1)<=0)continue;
		if(p[0].revents&POLLHUP)break;
		if(p[0].revents&POLLIN)rl_callback_read_char();
		if(p[1].revents&POLLIN)
			if((c=accept(p[1].fd,NULL,NULL))!=-1)
		{
			if(p[2].fd!=-1)close(p[2].fd);
			p[2].revents=0;
			p[2].fd=c;
			len=0;
			c=1;
			ioctl(p[2].fd,FIONBIO,&c);
#if defined(TCP_KEEPCNT) && defined(TCP_KEEPIDLE) && defined(TCP_KEEPINTVL)
			c=2;
			setsockopt(p[2].fd,IPPROTO_TCP,TCP_KEEPCNT,
				&c,sizeof(c));
			c=90;
			setsockopt(p[2].fd,IPPROTO_TCP,TCP_KEEPIDLE,
				&c,sizeof(c));
			c=90;
			setsockopt(p[2].fd,IPPROTO_TCP,TCP_KEEPINTVL,
				&c,sizeof(c));
			c=1;
			setsockopt(p[2].fd,SOL_SOCKET,SO_KEEPALIVE,
				&c,sizeof(c));
#endif
		}
		if(p[2].revents&POLLHUP)
		{
			close(p[2].fd);
			p[2].fd=-1;
		}
		else if(p[2].revents&POLLIN)
		{
			if((c=read(p[2].fd,bfr+len,sizeof(bfr)-len))<=0)
			{
				close(p[2].fd);
				p[2].fd=-1;
			}
			else
			{
				len+=c;

				for(ptr=bfr,c=0;c<len;c++)if(bfr[c]=='$')
				{
					bfr[c]=0;
					printf("\n%s$",ptr);
					ptr=bfr+c+1;
				}

				if(ptr!=bfr)
				{
					printf("\n");
					rl_on_new_line();
					rl_redisplay();

					if(ptr<bfr+len)
					{
						len=bfr+len-ptr;
						memmove(bfr,ptr,len);
					}
					else len=0;
				}

				if(len==sizeof(bfr))
				{
					close(p[2].fd);
					p[2].fd=-1;
				}
			}
		}
	}

	close(p[1].fd);
	if(p[2].fd!=-1)
	{
		close(p[2].fd);
		p[2].fd=-1;
	}

	rl_set_prompt("");
	rl_replace_line("",0);
	rl_redisplay();
	rl_callback_handler_remove();

	return 0;
}
