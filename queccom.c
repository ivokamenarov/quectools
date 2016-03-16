#include <sys/ioctl.h>
#include <sys/uio.h>
#include <termios.h>
#include <poll.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>

#define GL200	0x0001
#define GL300	0x0002
#define PW	0x8000
#define ALL	(GL200|GL300)
#define ALLPW	(GL200|GL300|PW)
#define NONE	0

#define SHORT_TIMEOUT	1
#define LONG_TIMEOUT	5

struct info
{
	int dev;
	int major;
	int minor;
	char name[6];
	char sw[9];
	char hw[9];
	char imei[16];
	char proto[7];
};

int fill=0;
static char bfr[2048];
static struct termios sav;
static char *pin=NULL;

static int doopen(char *dev)
{

	int fd;
	int i;
	struct termios t;

	if((fd=open(dev,O_RDWR))==-1)
	{
		perror("open");
		goto err1;
	}

	if(tcgetattr(fd,&sav))
	{
		perror("tcgetattr");
		goto err2;
	}

	t.c_cflag=CS8|CREAD|HUPCL|CLOCAL;
	t.c_iflag=IGNBRK|IGNPAR;
	t.c_oflag=0;
	t.c_lflag=0;
	for(i=0;i<NCCS;i++)t.c_cc[i]=0;
	t.c_cc[VMIN]=1;
	t.c_cc[VTIME]=0;
	cfsetispeed(&t,B9600);
	cfsetospeed(&t,B9600);

	if(tcsetattr(fd,TCSAFLUSH,&t))
	{
		perror("tcsetattr");
		goto err2;
	}

	i=1;
	if(ioctl(fd,FIONBIO,&i))
	{
		perror("ioctl");
		goto err3;
	}

	return fd;

err3:	tcsetattr(fd,TCSAFLUSH,&sav);
err2:	close(fd);
err1:	return -1;
}

static void doclose(int fd)
{
	tcsetattr(fd,TCSAFLUSH,&sav);
	close(fd);
}

static int buffread(int fd)
{
	int l=0;
	struct pollfd p;

	if(fill==sizeof(bfr))return -1;

	p.fd=fd;
	p.events=POLLIN;

	switch(poll(&p,1,0))
	{
	case -1:return -1;
	default:if(!(p.revents&POLLIN))return -1;
		if((l=read(fd,bfr+fill,sizeof(bfr)-fill))<=0)return -1;
		fill+=l;
	case 0:	return l;
	}
}

static char *readline(int fd,int ms50)
{
	int i;
	int j;
	char *ptr;
	char *line;

	for(i=0;;)
	{
		for(;i<fill;i++)if(bfr[i]=='\n')goto eol;
dowait:		if(!ms50--)return NULL;
		usleep(50000);
		switch(buffread(fd))
		{
		case -1:return NULL;
		case 0:	goto dowait;
		}
	}

eol:	if(i)if(bfr[i-1]=='\r')
	{
		bfr[i-1]=0;
		if(i>1)if(bfr[i-2]=='\r')bfr[i-2]=0;
	}
	bfr[i++]=0;
	for(ptr=bfr,j=0;j<i;j++)if(bfr[j])*ptr++=bfr[j];
	*ptr=0;
	line=strdup(bfr);
	if(i<fill)memmove(bfr,bfr+i,fill-i);
	fill-=i;
	return line;
}

static int writeline(int fd,char *req)
{
	static char cr='\r';
	struct iovec io[2];

	io[0].iov_base=req;
	io[0].iov_len=strlen(req);
	io[1].iov_base=&cr;
	io[1].iov_len=1;

	if(writev(fd,io,2)!=io[0].iov_len+1)return -1;
	return 0;
}

static int docmd(int fd,int timeout,int skip,char *cmd,void *param,
	int (*callback)(char *line,void *param))
{
	char *line;

	if(writeline(fd,cmd))return -1;

	timeout*=20;
	if(!(line=readline(fd,timeout)))return -1;
	if(strcmp(cmd,line))
	{
		free(line);
		return -1;
	}

	while(1)
	{
		free(line);
		if(!(line=readline(fd,timeout)))return -1;
		if(!strcmp(line,"OK"))
		{
			free(line);
			return 1;
		}
		if(!strcmp(line,"ERROR"))
		{
			free(line);
			return 0;
		}
		if(!*line)continue;
		if(skip)
		{
			skip--;
			continue;
		}
		if(callback)if(callback(line,param))
		{
			free(line);
			return -1;
		}
	}
}

static int versparse(char *line,void *param)
{
	struct info *inf=(struct info *)param;

	if(strncasecmp(line,"SubEdition:",11))return -1;
	line+=11;

	if(!memcmp(line,"GL200",5))inf->dev=GL200;
	else if(!memcmp(line,"GL300",5))inf->dev=GL300;
	else goto err;
	memcpy(inf->name,line,5);
	inf->name[5]=0;
	line+=5;

	if(*line!='R'||strlen(line)<9)goto err;
	line++;
	memcpy(inf->sw,line,8);
	inf->sw[8]=0;
	while(inf->sw[0]=='0')memmove(inf->sw,inf->sw+1,8);
	line+=8;

	if(*line!='M')goto err;
	line++;
	if(strlen(line)<=8)strcpy(inf->hw,line);
	else memcpy(inf->hw,line,8);
	inf->hw[8]=0;
	while(inf->hw[0]=='0')memmove(inf->hw,inf->hw+1,8);

	return 0;

err:	fprintf(stderr,"Unknown device line: %s\n",line);
	return -1;
}

static int imeiparse(char *line,void *param)
{
	struct info *inf=(struct info *)param;

	if(strncmp(line,"+EGMR: \"",8)||strlen(line)<24||line[23]!='\"')
	{
		fprintf(stderr,"Unknown IMEI line: %s\n",line);
		return -1;
	}

	memcpy(inf->imei,line+8,15);
	inf->imei[15]=0;

	return 0;
}

static int protoparse(char *line,void *param)
{
	struct info *inf=(struct info *)param;

	if(!strcasecmp(line,"Password error"))
	{
		fprintf(stderr,"Wrong device password\n");
		return -1;
	}

	if(strncmp(line,"+GTBSI:",7)||line[13]!=',')
	{
		fprintf(stderr,"Can't parse: %s\n",line);
		return -1;
	}

	memcpy(inf->proto,line+7,6);
	inf->proto[6]=0;

	inf->major=(inf->proto[2]<='9'?inf->proto[2]-'0':
		(inf->proto[2]&0xdf)-'A'+10);
	inf->major<<=4;
	inf->major|=(inf->proto[3]<='9'?inf->proto[3]-'0':
		(inf->proto[3]&0xdf)-'A'+10);

	inf->minor=(inf->proto[4]<='9'?inf->proto[4]-'0':
		(inf->proto[4]&0xdf)-'A'+10);
	inf->minor<<=4;
	inf->minor|=(inf->proto[5]<='9'?inf->proto[5]-'0':
		(inf->proto[5]&0xdf)-'A'+10);

	return 0;
}

static int dostrip(char *line)
{
	char *p;

	if(!(p=strrchr(line,',')))return -1;
	if(strlen(++p)!=15||p[14]!='$')return -1;
	*p=0;
	if(strlen(line)<30||strncmp(line,"+GT",3)||line[6]!=':'||
		line[13]!=','||line[29]!=',')return -1;
	memmove(line+7,line+30,strlen(line+30)+1);
	return 0;
}

static int getfield(char *line,int idx)
{
	int i;
	int j;

	if(idx)
	{
		for(i=0,j=0;line[i];i++)if(line[i]==',')if(++j==idx)break;
	}
	else for(i=0;line[i];i++)if(line[i]==':')break;
	if(!line[i++])return -1;
	return i;
}

static int clrfield(char *line,int idx)
{
	int i;
	int j;

	if(idx)
	{
		for(i=0,j=0;line[i];i++)if(line[i]==',')if(++j==idx)break;
	}
	else for(i=0;line[i];i++)if(line[i]==':')break;
	if(!line[i])return -1;
	for(j=++i;line[j];j++)if(line[j]==',')break;
	memmove(line+i,line+j,strlen(line+j)+1);
	return 0;
}

static int rplfield(char *line,int idx,char *cond,char *rpl)
{
	int i;
	int j;
	int l;
	int n;

	if(idx)
	{
		for(i=0,j=0;line[i];i++)if(line[i]==',')if(++j==idx)break;
	}
	else for(i=0;line[i];i++)if(line[i]==':')break;
	if(!line[i++])return -1;
	l=strlen(cond);
	n=strlen(rpl);
	if(l)if(memcmp(line+i,cond,l))return 0;
	memmove(line+i+n,line+i+l,strlen(line+i+l)+1);
	memcpy(line+i,rpl,n);
	return 0;
}

static int strip(char *line,void *param)
{
	char **result=(char **)param;

	if(dostrip(line))return -1;
	if(!(*result=strdup(line)))return -1;
	return 0;
}

static int gtsri(char *line,void *param)
{
	char **result=(char **)param;

	if(dostrip(line))return -1;
	if(clrfield(line,1))return -1;
	if(rplfield(line,3,"0.0.0.0,0,",",,"))return -1;
	if(rplfield(line,5,"0.0.0.0,0,",",,"))return -1;
	if(!(*result=strdup(line)))return -1;
	return 0;
}

static int gtqss(char *line,void *param)
{
	char **result=(char **)param;

	if(dostrip(line))return -1;
	if(clrfield(line,4))return -1;
	if(rplfield(line,6,"0.0.0.0,0,",",,"))return -1;
	if(rplfield(line,8,"0.0.0.0,0,",",,"))return -1;
	if(!(*result=strdup(line)))return -1;
	return 0;
}

static int gtcfg(char *line,void *param)
{
	char **result=(char **)param;

	if(dostrip(line))return -1;
	if(clrfield(line,0))return -1;
	if(!(*result=strdup(line)))return -1;
	return 0;
}

static int gttma(char *line,void *param)
{
	char **result=(char **)param;

	if(dostrip(line))return -1;
	if(strlen(line)<14)return -1;
	memmove(line+15,line+14,strlen(line+14)+1);
	line[14]=',';
	memmove(line+11,line+10,strlen(line+10)+1);
	line[10]=',';
	memmove(line+9,line+8,strlen(line+8)+1);
	line[8]=',';
	if(!(*result=strdup(line)))return -1;
	return 0;
}

static int gtfri(char *line,void *param)
{
	char **result=(char **)param;

	if(dostrip(line))return -1;
	if(clrfield(line,16))return -1;
	if(strlen(line)<11)return -1;
	memmove(line+13,line+11,strlen(line+11)+1);
	line[11]=',';
	line[12]=',';
	if(!(*result=strdup(line)))return -1;
	return 0;
}

static int gtgeo(char *line,void *param)
{
	char **result=(char **)param;

	if(dostrip(line))return -1;
	if(rplfield(line,14,"","\n+GTGEO:"))return -1;
	if(rplfield(line,28,"","\n+GTGEO:"))return -1;
	if(rplfield(line,42,"","\n+GTGEO:"))return -1;
	if(rplfield(line,56,"","\n+GTGEO:"))return -1;
	if(!(*result=strdup(line)))return -1;
	return 0;
}

static int gtwlt(char *line,void *param)
{
	int i;
	char **result=(char **)param;

	if(dostrip(line))return -1;
	if((i=getfield(line,11))==-1)return -1;
	line[i]=0;
	i=getfield(line,1);
	memmove(line+i+5,line+i,strlen(line+i)+1);
	memcpy(line+i,"1,10,",5);
	strcat(line,",,,,");
	if(!(*result=strdup(line)))return -1;
	return 0;
}

static int gtglm(char *line,void *param)
{
	int i;
	char **result=(char **)param;

	if(dostrip(line))return -1;
	if((i=getfield(line,4))==-1)return -1;
	line[i]=0;
	i=getfield(line,1);
	memmove(line+i+4,line+i,strlen(line+i)+1);
	memcpy(line+i,"1,3,",4);
	strcat(line,",,,,");
	if(!(*result=strdup(line)))return -1;
	return 0;
}

static int gtowh(char *line,void *param)
{
	char **result=(char **)param;

	if(dostrip(line))return -1;
	if(clrfield(line,9))return -1;
	if(clrfield(line,10))return -1;
	if(clrfield(line,11))return -1;
	if(clrfield(line,12))return -1;
	if(!(*result=strdup(line)))return -1;
	return 0;
}

static int gtout(char *line,void *param)
{
	int i;
	int j;
	char **result=(char **)param;

	if(dostrip(line))return -1;
	if((i=getfield(line,1))==-1)return -1;
	j=getfield(line,0);
	memmove(line+j,line+i,strlen(line+i)+1);
	if(!(*result=strdup(line)))return -1;
	return 0;
}

static int gtcmd(char *line,void *param)
{
	int i;
	int idx;
	char ***arr=(char ***)param;

	if(!*arr)
	{
		if(!(*arr=malloc(32*sizeof(char *))))return -1;
		memset(*arr,0,32*sizeof(char *));
	}
	idx=atoi(line+1);
	if(idx<0||idx>31)return -1;
	if((i=getfield(line,1))==-1)return -1;
	if((*arr)[idx])return -1;
	if(!((*arr)[idx]=strdup(line+i)))return -1;
	return 0;
}

static int gtudf(char *line,void *param)
{
	int i;
	int idx;
	char ***arr=(char ***)param;

	if(!*arr)
	{
		if(!(*arr=malloc(32*sizeof(char *))))return -1;
		memset(*arr,0,32*sizeof(char *));
	}
	idx=atoi(line);
	if(idx<0||idx>31)return -1;
	if((i=getfield(line,3))==-1)return -1;
	memmove(line+i+2,line+i,strlen(line+i)+1);
	line[i]=',';
	line[i+1]=',';
	i=getfield(line,1);
	if((*arr)[idx])return -1;
	if(!((*arr)[idx]=strdup(line+i)))return -1;
	return 0;
}

static int getinfo(int fd,int fast,struct info *inf)
{
	char bfr[32];

	if(fast)docmd(fd,SHORT_TIMEOUT,0,"AT+ESLP=0",NULL,NULL);

	if(docmd(fd,SHORT_TIMEOUT,0,"AT+CSUB",inf,versparse)!=1)goto fail;
	if(!pin)switch(inf->dev)
	{
	case GL200:
		pin="gl200";
		break;
	case GL300:
		pin="gl300";
		break;
	default:fprintf(stderr,"Missing device PIN\n");
		goto fail;
	}
	if(docmd(fd,SHORT_TIMEOUT,0,"AT+EGMR=0,7",inf,imeiparse)!=1)goto fail;

	sprintf(bfr,"AT+GTBSI?\"%s\"",pin);
	if(docmd(fd,SHORT_TIMEOUT,0,bfr,inf,protoparse)!=1)goto fail;

	if(fast)docmd(fd,SHORT_TIMEOUT,0,"AT+ESLP=1",NULL,NULL);
	return 0;

fail:	if(fast)docmd(fd,SHORT_TIMEOUT,0,"AT+ESLP=1",NULL,NULL);
	return -1;
}

static void skipit(void *param,void (*dumper)(char *,void *),void *prm)
{
	if(param)free(param);
}

static void string(void *param,void (*dumper)(char *,void *),void *prm)
{
	char bfr[2048];

	if(param)
	{
		sprintf(bfr,"AT%s0000$",(char *)param);
		free(param);
		dumper(bfr,prm);
	}
}

static void multis(void *param,void (*dumper)(char *,void *),void *prm)
{
	char *p;
	char bfr[2048];

	if(param)
	{
		p=strtok((char *)param,"\n");
		while(p)
		{
			sprintf(bfr,"AT%s0000$",p);
			dumper(bfr,prm);
			p=strtok(NULL,"\n");
		}
		free(param);
	}
}

static void arr32c(void *param,void (*dumper)(char *,void *),void *prm)
{
	int i;
	char **arr;
	char *dummy[32];
	char bfr[2048];

	if(!param)
	{
		param=dummy;
		memset(dummy,0,sizeof(dummy));
	}

	for(arr=(char **)param,i=0;i<32;i++)
	{
		if(arr[i])
		{
			sprintf(bfr,"AT+GTCMD:1,%d,%s,,,,,0000$",i,arr[i]);
			free(arr[i]);
		}
		else sprintf(bfr,"AT+GTCMD:0,%d,,,,,,0000$",i);
		dumper(bfr,prm);
	}

	if(param!=dummy)free(param);
}

static void arr32u(void *param,void (*dumper)(char *,void *),void *prm)
{
	int i;
	char **arr;
	char *dummy[32];
	char bfr[2048];

	if(!param)
	{
		param=dummy;
		memset(dummy,0,sizeof(dummy));
	}

	for(arr=(char **)param,i=0;i<32;i++)
	{
		if(arr[i])
		{
			sprintf(bfr,"AT+GTUDF:1,%d,%s,,,,0000$",i,arr[i]);
			free(arr[i]);
		}
		else sprintf(bfr,"AT+GTUDF:2,%d,,,,,,,,,,,0000$",i);
		dumper(bfr,prm);
	}

	if(param!=dummy)free(param);
}

static struct
{
	char *cmd;
	int dev;
	int skip;
	int (*callback)(char *,void *);
	void (*output)(void *,void (*dumper)(char *,void *),void *);
} cmdlist[]=
{
	{"AT+GTRTO",NONE ,0,NULL ,NULL  },
	{"AT+GTDAT",NONE ,0,NULL ,NULL  },
	{"AT+GTBSI",ALL  ,0,strip,string},
	{"AT+GTSRI",ALL  ,0,gtsri,string},
	{"AT+GTQSS",ALL  ,0,gtqss,skipit},
	{"AT+GTCFG",ALLPW,0,gtcfg,string},
	{"AT+GTDIS",ALL  ,0,strip,string},
	{"AT+GTTMA",ALL  ,0,gttma,string},
	{"AT+GTFRI",ALL  ,0,gtfri,string},
	{"AT+GTGEO",ALL  ,0,gtgeo,multis},
	{"AT+GTSPD",ALL  ,0,strip,string},
	{"AT+GTPIN",ALL  ,0,strip,string},
	{"AT+GTDOG",ALL  ,0,strip,string},
	{"AT+GTNMD",ALL  ,0,strip,string},
	{"AT+GTFKS",ALL  ,0,strip,string},
	{"AT+GTWLT",ALL  ,0,gtwlt,string},
	{"AT+GTGLM",ALL  ,0,gtglm,string},
	{"AT+GTNTS",ALL  ,0,strip,string},
	{"AT+GTUPC",ALL  ,0,strip,string},
	{"AT+GTOWH",ALL  ,0,gtowh,string},
	{"AT+GTCMD",ALL  ,1,gtcmd,arr32c},
	{"AT+GTUDF",ALL  ,1,gtudf,arr32u},
	{"AT+GTSFM",GL200,0,strip,string},
	{"AT+GTOUT",GL200,0,gtout,string},
	{"AT+GTTEM",GL300,0,strip,string},
	{"AT+GTJDC",GL300,0,strip,string},
	{NULL}
};

static int confread(int fd,int fast,struct info *inf,
			void (*dumper)(char *,void *),void *param)
{
	int i;
	void *res;
	char cmd[32];

	if(fast)docmd(fd,SHORT_TIMEOUT,0,"AT+ESLP=0",NULL,NULL);

	for(i=0;cmdlist[i].cmd;i++)if(cmdlist[i].dev&inf->dev)
	{
		res=NULL;
		sprintf(cmd,"%s?\"%s\"",cmdlist[i].cmd,pin);
		if(docmd(fd,LONG_TIMEOUT,cmdlist[i].skip,cmd,&res,
			cmdlist[i].callback)!=1)goto fail;
		cmdlist[i].output(res,dumper,param);
	}

	if(fast)docmd(fd,SHORT_TIMEOUT,0,"AT+ESLP=1",NULL,NULL);
	return 0;

fail:	if(fast)docmd(fd,SHORT_TIMEOUT,0,"AT+ESLP=1",NULL,NULL);
	fprintf(stderr,"Configuration read failed.\n");
	return -1;
}

static void prt(char *line,void *param)
{
	fprintf(param,"%s\n",line);
}

static int reader(char *dev,char *fn,int fast)
{
	int fd;
	FILE *fp;
	struct info inf;

	if((fd=doopen(dev))==-1)
	{
		return -1;
	}

	if(getinfo(fd,fast,&inf))
	{
		doclose(fd);
		return -1;
	}

	if(!(fp=fopen(fn,"w")))
	{
		doclose(fd);
		perror("fopen");
		return -1;
	}

	if(confread(fd,fast,&inf,prt,fp))
	{
		doclose(fd);
		fclose(fp);
		unlink(fn);
		return -1;
	}

	doclose(fd);
	fclose(fp);
	return 0;
}

static char *nextline(FILE *fp,struct info *inf)
{
	int i;
	char *p;
	char bfr[8192];

	while(fgets(bfr,sizeof(bfr),fp))
	{
		if(!(p=strtok(bfr,"\r\n")))continue;
		for(i=0;cmdlist[i].cmd;i++)if(!strncmp(p,cmdlist[i].cmd,8))
			if(p[8]==':')if(p[strlen(p)-1]=='$')
			if(cmdlist[i].dev&inf->dev)
		{
			if(cmdlist[i].dev&PW)if(clrfield(p,0))return NULL;
			i=getfield(p,0);
			p[i-1]='=';
			memmove(p+i+strlen(pin)+1,p+i,strlen(p+i)+1);
			memcpy(p+i,pin,strlen(pin));
			p[i+strlen(pin)]=',';
			return strdup(p);
		}
	}

	return NULL;
}

static int confwrite(int fd,int fast,FILE *fp,struct info *inf)
{
	int r=0;
	char *line;

	if(fast)docmd(fd,SHORT_TIMEOUT,0,"AT+ESLP=0",NULL,NULL);

	while((line=nextline(fp,inf)))
	{
		switch(docmd(fd,LONG_TIMEOUT,0,line,NULL,NULL))
		{
		case 0:	fprintf(stderr,"Failed: %s\n",line);
			r=-1;
			break;

		case -1:free(line);
			fprintf(stderr,"Configuration read failed.\n");
			r=-1;
			goto out;
		}
		free(line);
	}

out:	if(fast)docmd(fd,SHORT_TIMEOUT,0,"AT+ESLP=1",NULL,NULL);
	return r;
}

static int writer(char *dev,char *fn,int fast)
{
	int fd;
	FILE *fp;
	struct info inf;

	if((fd=doopen(dev))==-1)
	{
		return -1;
	}

	if(getinfo(fd,fast,&inf))
	{
		doclose(fd);
		return -1;
	}

	if(!(fp=fopen(fn,"r")))
	{
		doclose(fd);
		perror("fopen");
		return -1;
	}

	if(confwrite(fd,fast,fp,&inf))
	{
		doclose(fd);
		fclose(fp);
		return -1;
	}

	doclose(fd);
	fclose(fp);
	return 0;
}

static int ident(char *dev,int fast)
{
	int fd;
	struct info inf;

	if((fd=doopen(dev))==-1)
	{
		return -1;
	}

	if(getinfo(fd,fast,&inf))
	{
		doclose(fd);
		return -1;
	}

	doclose(fd);

	printf("DEV: %s SW: %s HW: %s PROTO: %d.%02d IMEI: %s\n",
		inf.name,inf.sw,inf.hw,inf.major,inf.minor,inf.imei);

	return 0;
}

static int newpass(char *dev,int fast,char *npw)
{
	int fd;
	int i;
	int r=0;
	char *p;
	void *res=NULL;
	struct info inf;
	char cmd[2048];

	if((fd=doopen(dev))==-1)
	{
		return -1;
	}

	if(getinfo(fd,fast,&inf))
	{
		doclose(fd);
		return -1;
	}

	for(i=0;cmdlist[i].cmd;i++)if(cmdlist[i].dev&inf.dev)
		if(cmdlist[i].dev&PW)break;
	if(!cmdlist[i].cmd)
	{
		doclose(fd);
		fprintf(stderr,"Don't know how to change password.\n");
		return -1;
	}

	if(fast)docmd(fd,SHORT_TIMEOUT,0,"AT+ESLP=0",NULL,NULL);

	sprintf(cmd,"%s?\"%s\"",cmdlist[i].cmd,pin);
	if(docmd(fd,LONG_TIMEOUT,cmdlist[i].skip,cmd,&res,
		cmdlist[i].callback)!=1||!res)
	{
		fprintf(stderr,"Device read failed.\n");
		r=-1;
		goto out;
	}

	clrfield((char *)res,0);
	for(p=res;*p!=':';p++);
	if(*p)*p++=0;
	sprintf(cmd,"AT%s=%s,%s%s0000$",(char *)res,pin,npw,p);
	free(res);

	if(docmd(fd,LONG_TIMEOUT,0,cmd,NULL,NULL)!=1)
	{
		fprintf(stderr,"Device write failed.\n");
		r=-1;
	}

out:	if(fast)docmd(fd,SHORT_TIMEOUT,0,"AT+ESLP=1",NULL,NULL);
	doclose(fd);

	return r;
}

static int terminal (char *dev)
{
	char c;
	int i;
	int fd;
	struct termios mem;
	struct termios t;
	struct pollfd p[2];

	if((fd=doopen(dev))==-1)
	{
		return -1;
	}

	if(tcgetattr(0,&mem))
	{
		perror("tcgetattr");
		doclose(fd);
		return -1;
	}

	memset(&t,0,sizeof(t));
	t.c_cflag=CS8|CREAD|HUPCL|CLOCAL;
	t.c_iflag=IGNBRK|IGNPAR;
	t.c_oflag=0;
	t.c_lflag=0;
	for(i=0;i<NCCS;i++)t.c_cc[i]=0;
	t.c_cc[VMIN]=1;
	t.c_cc[VTIME]=0;

	printf("Entering terminal mode, press CTRL-C to exit...\n");
	fflush(stdout);

	if(tcsetattr(0,TCSAFLUSH,&t))
	{
		perror("tcsetattr");
		doclose(fd);
		return -1;
	}

	i=1;
	if(ioctl(0,FIONBIO,&i))
	{
		perror("ioctl");
		tcsetattr(0,TCSAFLUSH,&mem);
		doclose(fd);
		return -1;
	}

	p[0].fd=0;
	p[0].events=POLLIN;
	p[1].fd=fd;
	p[1].events=POLLIN;

	while(1)
	{
		if(poll(p,2,-1)<1)continue;

		if(p[0].revents&POLLIN)
		{
			if(read(0,&c,1)!=1)break;
			if(c==3)break;
			if(write(fd,&c,1)!=1)break;
		}

		if(p[1].revents&POLLIN)
		{
			if(read(fd,&c,1)!=1)break;
			if(write(1,&c,1)!=1)break;
		}
	}

	tcsetattr(0,TCSAFLUSH,&mem);
	doclose(fd);

	return 0;
}

int main(int argc,char *argv[])
{
	int c;
	int fast=1;
	int mode=0;
	char *dev="/dev/ttyUSB0";
	char *fn="queccom.cfg";

	while((c=getopt(argc,argv,"d:p:srwint"))!=-1)switch(c)
	{
	case 'd':
		dev=optarg;
		break;

	case 'p':
		pin=optarg;
		if(!*pin||strlen(pin)>8)goto usage;
		break;

	case 's':
		fast=0;
		break;

	case 'r':
		mode|=1;
		break;

	case 'w':
		mode|=2;
		break;

	case 'i':
		mode|=4;
		break;

	case 'n':
		mode|=8;
		break;

	case 't':
		mode=16;
		break;

	default:goto usage;
	}

	switch(mode)
	{
	case 1:	if(optind==argc-1)fn=argv[optind];
		else if(optind!=argc)goto usage;
		if(reader(dev,fn,fast))return 1;
		return 0;

	case 2:	if(optind==argc-1)fn=argv[optind];
		else if(optind!=argc)goto usage;
		if(writer(dev,fn,fast))return 1;
		return 0;

	case 4:	if(optind!=argc)goto usage;
		if(ident(dev,fast))return 1;
		return 0;

	case 8:	if(optind!=argc-1)goto usage;
		if(!argv[optind][0]||strlen(argv[optind])>8)goto usage;
		if(newpass(dev,fast,argv[optind]))return 1;
		return 0;

	case 16:if(optind!=argc)goto usage;
		if(terminal(dev))return 1;
		return 0;

	default:goto usage;
	}

	return 0;

usage:	fprintf(stderr,"Usage: %s [-d <device>] [-s] [-p <password>] "
		"-r [<file>]\n",argv[0]);
	fprintf(stderr,"       %s [-d <device>] [-s] [-p <password>] "
		"-w [<file>]\n",argv[0]);
	fprintf(stderr,"       %s [-d <device>] [-s] [-p <password>] "
		"-n <newpass>\n",argv[0]);
	fprintf(stderr,"       %s [-d <device>] [-s] [-p <password>] "
		"-i\n",argv[0]);
	fprintf(stderr,"       %s [-d <device>] -t\n",argv[0]);
	fprintf(stderr,"<device>   serial device to use, default: "
		"/dev/ttyUSB0\n");
	fprintf(stderr,"<password> device password, if not specified "
		"use default password for device\n");
	fprintf(stderr,"<file>     configuration file, default: queccom.cfg\n");
	fprintf(stderr,"<newpass>  new device password\n");
	fprintf(stderr,"-s         disable slow clock toggle "
		"(default enabled)\n");
	fprintf(stderr,"-r         read device configuration to file\n");
	fprintf(stderr,"-w         write device configuration from file\n");
	fprintf(stderr,"-n         set new device password\n");
	fprintf(stderr,"-i         inquire device information\n");
	fprintf(stderr,"-t         enter interactive terminal mode\n");
	return 1;
}
