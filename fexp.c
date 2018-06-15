#include <stdio.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>
#include <sys/param.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/wait.h>


#include "parsecfg.h"
#include "dscan.h"
#include "ftplib.h"
#include "gtkwin.h"
#include "logging.h"

typedef struct _thread_arg
{
	config *cfg ;
	pthread_t pid;
	int pnum ;

}targ;

pid_t process_gui(config *cfg, int pnum);
void *thread_ftp (void *);
int read_cfg(char *,config **);
void get_file(void *);
void put_file(void *);
netbuf *setup_ftp_env(void *);
void progress_report(int pnum, size_t s,struct timeval *,int );
FATT **ftp_dscan(netbuf *nControl,char *dir, int *n,char *patt);
off_t ftp_size(char *file,netbuf *nControl);
void PassCode(char *src);
char *PassDecode(char *src, char *dest);
int SigCatcher(void *arg);
void SigSet();
char *CheckDisplay();

void set_message(int pnum, int wnum, int level,char *buf);

int infoPipe[2];
FILE *writeFp= NULL ;
pthread_mutex_t mutex ;

/*
** Routine :
** ->main entry
** Summary :
** ->read configration ,
** ->setup Graphical User Interface 
** ->create ftp client thread
*/
int main( int argc, char **argv )
{
	config *cfg ;
	char *extName, *cfile,configfile[128];
	targ *tharg;
	int pnum ,i, runLevel=1 ;
	pid_t guiPid= 0;

	signal(SIGPIPE,SIG_IGN);

	if(argc== 1)
	{
		cfile= argv[0];
		extName=".cfg";

	}else if(argc== 2)
	{
		cfile= argv[1];
		extName="";

	}else if(argc== 3)
	{
		if( strcmp(argv[1],"-password")== 0)
		{
			PassCode(argv[2]);
			return 0;
		}
		if( strcmp(argv[1],"-zznode")== 0)
		{
			char buf[64];
			printf("\nPass: [%s]\n\n",PassDecode(argv[2],buf));
			return 0;
		}
		return 0;
	}
	else exit(0);

	snprintf(configfile,sizeof(configfile),"%s%s",cfile,extName);
	pnum = read_cfg(configfile,&cfg);

	pipe(infoPipe);
	fcntl(infoPipe[0], F_SETFL, O_NONBLOCK);
	fcntl(infoPipe[1], F_SETFL, O_NONBLOCK);
	writeFp = fdopen(infoPipe[1],"w");

	if(CheckDisplay()) guiPid= process_gui(cfg, pnum);

	SigSet();

	pthread_mutex_init(&mutex, NULL);

	tharg = (targ *)calloc(sizeof(targ),pnum);

	for(i= 0; i< pnum; i++)
	{
		tharg[i].cfg = &cfg[i];
		tharg[i].pnum = i ;

		if(pthread_create(&(tharg[i].pid),NULL,thread_ftp,(void *)&tharg[i]))
		{
			fprintf(stderr, "Create pthread error!\n");
			exit (1);
		}
		pthread_detach(tharg[i].pid);
	}

	while(runLevel>0)
	{
		runLevel= SigCatcher(NULL);
		switch(runLevel)
		{
			case 1: 
				if(guiPid==0 && CheckDisplay())
				{
					guiPid= process_gui(cfg, pnum);
					printf("\nGui process create: [%d]\n",guiPid);
				}
				break;

			case 2:

				printf("\nGui process exit: [%d]\n",guiPid);
				guiPid = 0; //gui exit;
				break;

			default:
				printf("\nkill gui process: [%d]\n",guiPid);
				if(guiPid> 0) kill(guiPid,SIGKILL);
				break;
		}
	}

	printf("\nBye! \n\n");
}

void SigSet()
{
	sigset_t new;
	sigemptyset(&new);
	//int i ; for(i=1;i<=36; i++){sigaddset(&new, i);}
	sigaddset(&new, SIGINT);
	sigaddset(&new, SIGQUIT);
	sigaddset(&new, SIGTERM);
	sigaddset(&new, SIGUSR1);
	sigaddset(&new, SIGUSR2);

	pthread_sigmask(SIG_BLOCK, &new, NULL);
}

int SigCatcher(void *arg)
{
	sigset_t mySigset;
	int sig;

	sigfillset(&mySigset);
	pthread_sigmask(SIG_BLOCK, &mySigset, NULL);
	sigemptyset(&mySigset);

	//int i ; for(i=1;i<=36; i++){sigaddset(&mySigset, i);}
	sigaddset(&mySigset, SIGINT);
	sigaddset(&mySigset, SIGQUIT);
	sigaddset(&mySigset, SIGTERM);
	sigaddset(&mySigset, SIGUSR1);
	sigaddset(&mySigset, SIGUSR2);

	printf("\nMain thread wait signal ...\n");

	for (;;)
	{
		sigwait(&mySigset, &sig);

		printf("\nReceive signal %d![%d]\n",sig,getpid());

		switch (sig)
		{
			case SIGINT:
			case SIGQUIT:
			case SIGTERM:

				return 0;

			case SIGUSR1:

				return 1;

			case SIGUSR2:
	
				return 2;

			default:
				printf("unexpected signal %d\n", sig);
				break;
		}
	}
}

pid_t process_gui (config *cfg, int pnum)
{
	pid_t pid,ppid;
	int ret=0;

	signal(SIGCHLD, SIG_IGN); 
	if((pid= fork())== 0)
	{
		SigSet();
		printf("\nprocess_gui create [%d]!",getpid());
		if(CheckDisplay())
		{
			ret= start_gui(cfg, pnum, infoPipe[0]);
		}
		if(ret==0)
		{
			kill(getppid(),SIGTERM);

		}else //backend or fail to create windown
		{
			kill(getppid(),SIGUSR2);
		}
		printf("\nprocess_gui exit!");
		exit(ret);

	}else if(pid< 0)
	{
		return -1;
	}

	return pid;
}

char *CheckDisplay()
{
	char display[64];

    if(access("/tmp/fexp.display",F_OK)== 0)
    {
        //printf("\ngtkwin: find display file and load it ...\n");
        FILE *fp= fopen("/tmp/fexp.display","r");
        if(fp && fgets(display,sizeof(display),fp))
        {   
            display[strlen(display)-1]=NULL;
            //printf("\ngtkwin: set display to [%s]\n",display);
            putenv(display);
        }
        if(fp) fclose(fp);
        //unlink("/tmp/fexp.display");
    }
    printf("\nCheckDisplay: [%s]\n",getenv("DISPLAY") ? getenv("DISPLAY") : "NULL");
    return((char *)getenv("DISPLAY"));
}   

/* 
** Routine :
** ->ftp client thread main entry
** Summary :
** ->fill ftp client info in GUI 
** ->action to 'PUT' or 'GET'
*/
void *thread_ftp (void *arg)
{
	targ *tharg = (targ *)arg ;
	config *mycfg = (config *)tharg->cfg ;

	if(strcmp(mycfg->action,"PUT")) get_file(arg);	
	else put_file(arg);
}

void set_message(int pnum, int wnum, int level,char *buf)
{
	int ret=0;

	pthread_mutex_lock(&mutex) ;

	ret= fprintf(writeFp,"%d:%d:%d:%s\n",pnum, wnum, level, buf);

	if(ret< 2) printf("\nwrite pipe fail!\n");

	fflush(writeFp);
	pthread_mutex_unlock(&mutex) ;
}

void send_debug(netbuf *ftp,const char *text, void *out)
{
	//if(out) show_message(out,2,">>%s",text);
	//else fprintf(stderr, ">>%s\n",text);
}
void recv_debug(netbuf *ftp,const char *text, void *out)
{
	//if(out) show_message(out,2,"<<%s",text);
	//else fprintf(stderr, ">>%s\n",text);
}

void logmsg(void *arg,int level ,char *fmt, ... )
{
	targ *tharg = (targ *)arg ;
	config *mycfg = (config *)tharg->cfg ;
	va_list argptr; 
	char buf[256];
	int cnt;

	va_start(argptr, fmt); 
	cnt = vsnprintf(buf,sizeof(buf), fmt, argptr); 
	va_end(argptr); 

	set_message(tharg->pnum,1,level, buf);

	if(level <= mycfg->logMsg )
		logit(mycfg->logDir ,mycfg->section, mycfg->hostname,mycfg->action,0,buf);
}


/*
** Routine : 
** ->set up ftp conneciton 
** Summary :
** ->connect to ftp server
** ->login to ftp server
** ->set connection options
*/
netbuf *setup_ftp_env(void *arg)
{
	targ *tharg = (targ *)arg ;
	FATT **ftab = NULL;
	config *mycfg = (config *)tharg->cfg ;
	char lmsg[256];
	int fcount= 0;

	netbuf *ftp = NULL ;

	if(!FtpConnect(mycfg->hostname,send_debug,recv_debug,0,mycfg->timeout,&ftp))
	{
		logmsg(arg,0,"Error: connect [ %s ] fail !",mycfg->hostname);
		return (netbuf *)0 ;
	}
	logmsg(arg,1, "Connect to host [ %s ] OK !",mycfg->hostname);

	if(!FtpLogin(mycfg->username,PassDecode(mycfg->password,lmsg),ftp))
	{
		logmsg(arg,0,"Error: login %s as %s / %s fail !",
		mycfg->hostname,mycfg->username,mycfg->password); 
		FtpQuit(ftp); return (netbuf *)0 ;
	}

	if(access(mycfg->localDir, F_OK)!= 0)
	{
		logmsg(arg,0,"Error: access local directory [ %s ] fail !",mycfg->localDir);
		FtpQuit(ftp); return (netbuf *)0 ;
	}

	if(FtpChdir(mycfg->remoteDir, ftp) == 0)
	{
		logmsg(arg,0,"Error: access remote directory [ %s ] fail !",mycfg->remoteDir);
		FtpQuit(ftp); return (netbuf *)0 ;
	}
	FtpChdir("~", ftp);
	logmsg(arg,1, "Login as [ %s ] OK !",mycfg->username);
	return ftp ;
}

#define FTPBUFSIZE 1024

/*
** Routine : 
** Summary :
*/
void get_file(void *arg )
{
	targ *tharg = (targ *)arg ;
	config *mycfg = (config *)tharg->cfg ;

	netbuf *ftp = NULL;
	netbuf *ftpfp = NULL;

	FATT **ftab = NULL;
	struct stat st;
	int ftotal=0, fnum=0 ;

	char buf[MAXPATHLEN];
	char readbuf[FTPBUFSIZE];

	int  ofd = -1;
	int i,j;
	off_t bytecount = 0;
	off_t initcount = 0;
	off_t interval = 10240;
	off_t nextreport = 0;
	off_t startpoint ;

	char localFile[256];
	char remoteFile[256];
	char tmpFile[256];
	char bakFile[256];

	int polltime = 1 ;
	
	struct timeval starttime ;
	if(mycfg->tmpDir== NULL || *(mycfg->tmpDir)== NULL) mycfg->tmpDir= mycfg->localDir;

	for( ; ; )	
	{
		while( !FtpStatus(ftp) )
		{
			ftp = setup_ftp_env(arg);
			if(!ftp)
			{
				logmsg(arg,1,"Sleep 5 seconds ...");
			 	sleep(5);
			}
		}

		ftab = ftp_dscan(ftp,mycfg->remoteDir,&ftotal,mycfg->filematch);
		if(ftab == (FATT **)-1)
		{
			logmsg(arg,0,"Error: access remote directory [%s] error!", mycfg->remoteDir); 
			sleep(1); continue ;
		}
		logmsg(arg,1,"Info: scan remote directory [ %s ][ %d files] ...",mycfg->remoteDir,ftotal); 


		if(ftotal> 0) for(fnum=0; fnum<ftotal; fnum++)
		{
			snprintf(remoteFile,sizeof(remoteFile),"%s/%s",mycfg->remoteDir,ftab[fnum]->fn);
			snprintf(tmpFile,sizeof(tmpFile),"%s/.%s",mycfg->tmpDir,ftab[fnum]->fn);
			snprintf(localFile,sizeof(localFile),"%s/%s",mycfg->localDir,ftab[fnum]->fn);

			logmsg(arg,1,"Info: get file [ %s ][ %ld bytes ] to tmp file [%s] ...",
						remoteFile,ftab[fnum]->st.st_size,tmpFile); 

			if(access(tmpFile, F_OK)==0 && stat(tmpFile,&st)==0 )
			{
				startpoint = st.st_size ;

			}else startpoint = 0 ;

			if (!FtpAccess(remoteFile, FTPLIB_FILE_READ, FTPLIB_IMAGE,&startpoint, ftp, &ftpfp))
			{
				logmsg(arg,0,"Error: FtpSize remote file <%s>[%d/%d] failed !",remoteFile,fnum,ftotal);
				break;
			}

			if(startpoint>0)
			{
				logmsg(arg,1,"Info: file not finish , restarting at < %ld > ...",startpoint);
				ofd = open(tmpFile,O_RDWR,0644);
				lseek(ofd,startpoint,SEEK_CUR);
			}else
			{
				ofd = open(tmpFile,O_RDWR|O_CREAT,0644);
			}
			bytecount = nextreport = startpoint ;

			//set_file_label(out,ftab[fnum]->fn);
			set_message(tharg->pnum,2,0,ftab[fnum]->fn);

			gettimeofday(&starttime,NULL);
			while ((i = FtpRead(readbuf, sizeof(readbuf), ftpfp)) > 0)
			{
				j = write(ofd, readbuf,i);
				if(j== -1)
				{
					i = -1 ;
					break ;
				}

				bytecount += i;
				if (bytecount > nextreport || bytecount == ftab[fnum]->st.st_size)
				{
					progress_report(tharg->pnum, bytecount-startpoint,&starttime,
							(int)(100.0*bytecount/ftab[fnum]->st.st_size));
					nextreport += interval;
				}
			}
			close(ofd);
			FtpClose(ftpfp);

			if(i == -1) 
			{
				logmsg(arg,0,"Error: ftp read error [%s] !",tmpFile);
				break;
			}

			if(stat(tmpFile,&st)== -1 || ftab[fnum]->st.st_size != st.st_size || rename(tmpFile,localFile)==-1 )
			{
				logmsg(arg,0,"Error : file transfer is not correct [ %s ] !", tmpFile); 
				unlink(tmpFile); break;
			}
			logmsg(arg,0,"Info: transfer file [%s] [%ld bytes] ok!",localFile,bytecount-startpoint);

			if(*(mycfg->bakDir) == NULL)
			{
				if(!FtpDelete(remoteFile,ftp))
				{
					logmsg(arg,0,"Error: remove remote file [%s] failed !",remoteFile); 
					break;
				}
				logmsg(arg,1,"Info: delete remote file [%s] OK !",remoteFile); 
			}else
			{
				snprintf(bakFile,sizeof(bakFile),"%s/%s",mycfg->bakDir,ftab[fnum]->fn);
				if(!FtpRename(remoteFile,bakFile,ftp) )
				{
					logmsg(arg,0,"Error: backup remote file [%s]  error !",ftab[fnum]->fn); 
					break;
				}
				logmsg(arg,1,"Info: backup remote file [%s] to remote dir [%s] !",remoteFile, mycfg->bakDir); 
			}

		}
		if(ftotal> 0)
		{
			while(ftotal--)
			{
				free(ftab[ftotal]);
				ftab[ftotal]= NULL;
			}
			free(ftab); ftab = NULL;
			if(polltime>0 ) polltime = 0 ;
		}
		if(polltime> 0)
		{
			logmsg(arg,1,"No files found !!!");
			logmsg(arg,1,"Sleep %d seconds ...",polltime);
			sleep(polltime);
		}
		polltime= ( polltime < 5 )? (polltime+1):3 ;
	}
}

/*
** Routine : 
** Summary :
*/
void put_file(void *arg)
{
	targ *tharg = (targ *)arg ;
	config *mycfg = (config *)tharg->cfg ;

	char buf[MAXPATHLEN];
	char readbuf[FTPBUFSIZE];

	int i,j ;
	int ofd = -1;
	off_t bytecount = 0;
	off_t initcount = 0;
	off_t interval = 10240;
	off_t nextreport = 0;
	off_t startpoint ;

	char bakFile[256];
	char tmpFile[256];
	char remoteFile[256];
	char localFile[256];

	char *temp;
	struct timeval starttime ;

	FATT	**ftab = NULL ;
	int		ftotal=0,fnum ;

	netbuf *ftp = NULL ;
	netbuf *ftpfp = NULL ;

	int		polltime  = 1;

	if(mycfg->tmpDir== NULL || *(mycfg->tmpDir)== NULL) mycfg->tmpDir= mycfg->localDir;

	for( ; ; )
	{
		while( !FtpStatus(ftp) )
		{
			ftp = setup_ftp_env(arg);
			if(!ftp)
			{
				logmsg(arg,1,"Sleep 5 seconds ...");
			 	sleep(5);
			}
		}

		ftab = dscan(mycfg->localDir,&ftotal,mycfg->filematch) ;
		if(ftab == (FATT **)-1)
		{
			logmsg(arg,0,"Error: access local directory [%s] error!",mycfg->localDir); 
			sleep(1); continue ;
		}
		logmsg(arg,1,"Info: scan local directory [ %s ][ %d files] ...",mycfg->localDir, ftotal); 

		if(ftotal> 0) for(fnum=0; fnum<ftotal; fnum++)
		{
			snprintf(remoteFile,sizeof(remoteFile),"%s/%s",mycfg->remoteDir,ftab[fnum]->fn);
			snprintf(tmpFile,sizeof(tmpFile),"%s/.%s",mycfg->tmpDir,ftab[fnum]->fn);
			snprintf(localFile,sizeof(localFile),"%s/%s",mycfg->localDir,ftab[fnum]->fn);

			logmsg(arg,1,"Info: put file [ %s ][ %ld bytes ] to tmp file [%s] ...",
						localFile, ftab[fnum]->st.st_size, tmpFile); 

			if((startpoint = ftp_size(tmpFile,ftp) )< 0)
			{
				logmsg(arg,0,"Error: FtpSize remote file <%s>[%d/%d] failed !",remoteFile,fnum,ftotal);
				break;
			}

			if(!FtpAccess(tmpFile,FTPLIB_FILE_WRITE,FTPLIB_IMAGE,&startpoint,ftp,&ftpfp))
			{
				logmsg(arg,0,"Error: FtpAccess remote file <%s>[%d/%d] error !",remoteFile,fnum,ftotal);
				break;
			}

			ofd = open(localFile , O_RDONLY, 0644);
			if(startpoint>0 )
			{
				logmsg(arg,1,"Info: file not finish , restarting at < %ld > ...",startpoint);
				lseek(ofd,startpoint,SEEK_CUR);
			}

			bytecount = nextreport = startpoint ;

			//set_file_label(out,ftab[fnum]->fn);
			set_message(tharg->pnum,2,0,ftab[fnum]->fn);
				
			gettimeofday(&starttime,NULL);
			while((i = read(ofd,readbuf, sizeof(readbuf))) > 0)
			{
				bytecount += i;

				/*
				temp = readbuf;
				while( (j = FtpWrite(temp,i,ftpfp)) >0 )
				{
					i -= j;
					temp += j;
				}
				*/

				j = FtpWrite(readbuf,i,ftpfp);
				
				if(j == -1) 
				{
					i = -1 ;
					break ;
				}

				if (bytecount > nextreport || bytecount == ftab[fnum]->st.st_size )
				{
					progress_report(tharg->pnum, bytecount-startpoint,&starttime,
					(int)(100.0*bytecount/ftab[fnum]->st.st_size));
					nextreport += interval;
				}
			}
			close(ofd);
			FtpClose(ftpfp);

			if(i == -1) 
			{
				logmsg(arg,0,"Error: ftp write error [%s] !",tmpFile);
				break;
			}

			if(ftp_size(tmpFile,ftp) != ftab[fnum]->st.st_size || !FtpRename(tmpFile,remoteFile,ftp) )
			{
				logmsg(arg,0,"Error : file transfer is not correct [ %s ] !", tmpFile); 
				break;
			}
			logmsg(arg,0,"Info: transfer file [%s] [%ld bytes] ok!",remoteFile,bytecount-startpoint);

			if(*(mycfg->bakDir) == NULL)
			{
				if(unlink(localFile) == -1)
				{
					logmsg(arg,0,"Error: delete file [ %s ] failed !",localFile); 
					break;
				}
				logmsg(arg,1,"Info: delete local file [ %s ] OK !",localFile); 
			}else
			{
				snprintf(bakFile,sizeof(bakFile),"%s/%s",mycfg->bakDir,ftab[fnum]->fn);
				if(rename(localFile, bakFile) == -1)
				{
					logmsg(arg,0,"Error: backup local file [%s] to local dir [ %s ] failed !",localFile, mycfg->bakDir); 
					break;
				}
				logmsg(arg,1,"Info: backup local file [%s] to local dir [%s] OK !", localFile, mycfg->bakDir); 
			}
		}
		if(ftotal> 0)
		{
			while(ftotal--)
			{
				free(ftab[ftotal]);
				ftab[ftotal]= NULL;
			}
			free(ftab); ftab = NULL;
			if(polltime>0 ) polltime = 0 ;
		}
		if(polltime> 0)
		{
			logmsg(arg,1,"No files found !!!");
			logmsg(arg,1,"Sleep %d seconds ...",polltime);
			sleep(polltime);
		}
		polltime= ( polltime < 5 )? (polltime+1):3 ;
	}
}

/*
** Routine : 
** Summary :
*/
void progress_report(int pnum, size_t bytes, struct timeval *start ,int percentage)
{
	struct timeval now ;
	char buf[256];
	time_t lapsed ;
	double dlrate ;

	gettimeofday(&now,NULL);
	if (start->tv_usec > now.tv_usec)
	{
		now.tv_usec += 1000000;
		now.tv_sec--;
	}

	lapsed=(now.tv_usec-start->tv_usec)+1000000*(now.tv_sec-start->tv_sec);
	dlrate = (double)1000000 * bytes/lapsed ;

	if(dlrate < 1024.0)
		snprintf(buf,sizeof(buf),"%.2f B/s", dlrate);
	else if (dlrate < 1024.0*1024.0)
		snprintf(buf,sizeof(buf),"%.2f KB/s", dlrate/1024.0);
	else if (dlrate < 1024.0*1024.0*1024.0)
		snprintf(buf,sizeof(buf),"%.2f MB/s", dlrate/(1024.0*1024.0));

	//set_speed_label(out,buf);
	set_message(pnum,3,0,buf);

	snprintf(buf,sizeof(buf),"%.2f Seconds",(double)lapsed/1000000);

	//set_elaps_label(out,buf);	
	set_message(pnum,4,0,buf);

	snprintf(buf,sizeof(buf), "%d",percentage);
	//show_progress(out,percentage);
	set_message(pnum,5,0,buf);
}

/*
** Routine : 
** Summary :
*/
int read_cfg(char *file,config **cfg)
{
	int count , i ;

	char **action      ;
	char **hostname    ;
	char **username    ;
	char **password    ;
	char **remoteDir   ;
	char **bakDir;
	char **tmpDir;
	char **localDir    ;
	char **logDir    ;
	char **filematch   ;
	int  *logMsg    ;
	int	 *timeout	;

	cfgStruct cfgini[] =
	{
		{"ACTION",CFG_STRING,&action},
		{"HOST",CFG_STRING,&hostname},
		{"USER",CFG_STRING,&username},
		{"PASS",CFG_STRING,&password},
		{"REMOTEDIR",CFG_STRING,&remoteDir},
		{"BAKDIR",CFG_STRING,&bakDir},
		{"LOCALDIR",CFG_STRING, &localDir},
		{"TMPDIR",CFG_STRING, &tmpDir},
		{"LOGMSG",CFG_INT, &logMsg},
		{"LOGDIR",CFG_STRING, &logDir},
		{"FILEMATCH",CFG_STRING, &filematch},
		{"TIMEOUT",CFG_INT,&timeout},
		{NULL, CFG_END, NULL}	/* no more parameters */
	};

	if((count = cfgParse(file, cfgini, CFG_INI))== -1)
	{
		fprintf(stderr, "\n\nFata Error %s\n", strerror(errno));
		exit(-1);
	}
	*cfg = (config *)calloc(sizeof(config),count);
	for(i = 0; i < count; i++)
	{
		(*cfg)[i].section = cfgSectionNumberToName(i);
		(*cfg)[i].action=action[i];
		(*cfg)[i].hostname=hostname[i];
		(*cfg)[i].username=username[i];
		(*cfg)[i].password=password[i];
		(*cfg)[i].remoteDir=remoteDir[i];
		(*cfg)[i].bakDir=bakDir[i];
		(*cfg)[i].localDir=localDir[i];
		(*cfg)[i].tmpDir=tmpDir[i];
		(*cfg)[i].logMsg=logMsg[i];
		(*cfg)[i].logDir=logDir[i];
		(*cfg)[i].filematch=filematch[i];
		(*cfg)[i].timeout = timeout[i];
		
	}
	return count ;
}

/*
** Routine : 
** Summary :
*/
FATT **ftp_dscan(netbuf *nControl,char *dir, int *n,char *patt)
{
	int i;
	char dbuf[256];
	netbuf *nData;
	FATT	**f_tab = NULL ;
	FATT	*ft = NULL ;

	*n = 0;
	if (!FtpAccess(dir, FTPLIB_DIR_VERBOSE, FTPLIB_ASCII,NULL, nControl, &nData))
	{
		return (FATT **)(-1);
	}
	while ((i = FtpRead(dbuf, sizeof(dbuf), nData)) > 0)
	{
		dbuf[strlen(dbuf)-1] = '\0' ;	

		if((ft = list_parse_unix( dbuf ,patt))== NULL )
		{
			continue ;
		}

		f_tab=(FATT **)realloc(f_tab,(*n+1)*sizeof(FATT *));
		if (f_tab == NULL)
		{
			return (FATT **)(-1);
		}
		f_tab[(*n)++] = ft ; 
	}
	FtpClose(nData);
	return f_tab ;
}

/*
** Routine : 
** Summary :
*/
off_t ftp_size(char *file,netbuf *nControl)
{
	int i = 0;
	char dbuf[256];
	netbuf *nData;
	FATT	*ft = NULL ;
	off_t fsize = 0 ;

	if (FtpSize(file,&fsize,FTPLIB_IMAGE,nControl))
	{
		return fsize ;
	}

	/*
	if (!FtpAccess(file, FTPLIB_DIR_VERBOSE, FTPLIB_ASCII,NULL,nControl, &nData))
	{
		return 0;
	}
	while ( FtpRead(dbuf, sizeof(dbuf), nData) > 0)
	{
		dbuf[strlen(dbuf)-1] = '\0' ;	

		if((ft = list_parse_unix( dbuf ,NULL))!= NULL)
		{
			fsize = ft->st.st_size ;
			free(ft);
			ft = NULL ;
		} 
	}
	FtpClose(nData);
	*/
	return fsize ;
}

unsigned int MagicNum(char *key)
{
	char *p = key;
	int len= strlen(key);
	unsigned int h = *p;
	int i= 1 ;

	if (h) while( i < len ) h = (h << 5) -h + p[i++] ;
	return h;
}

void PassCode(char *src)
{
	int  i,n= 0, len= 0;
	char buf[64],*ptr,tmp;
	
	len= strlen(src);
	memset(buf,0,sizeof(buf));

	printf("\nPassword: [%s]\n",src);
	if(len*2> sizeof(buf)) return;
	unsigned int magic= MagicNum(src)%10000;

	//printf("\nPass code magic : [%u]\n\n",magic);

	sprintf(buf, "%04u", magic);
	ptr= buf+4;
	magic= magic%49;

	for(i=0; i<len; i++)
	{
		n+= sprintf(ptr+n, "%02x", src[i]+magic+i);
	}
	//printf("\nPass code 1: [%s]\n\n",buf);

	for(i=0; i<len; i++)
	{
		tmp= ptr[i];
		ptr[i]=ptr[n-i-1];
		ptr[n-i-1]= tmp;
	}
	//printf("\nPass code 2: [%s]\n\n",buf);

	for(i=0; i<len; i++)
	{
		tmp= ptr[i];
		ptr[i]=ptr[len+i];
		ptr[len+i]= tmp;
	}
	printf("\nSecret code: [%s]\n",buf);

	printf("\nDecode: [%s]\n",PassDecode(buf, buf));

}

char *PassDecode(char *src, char *dest)
{
	int  i,j,n= 0, len= 0;
	char buf[64],tmp;
	
	n= strlen(src);

	memset(buf,0,sizeof(buf));
	unsigned int magic= 0;
	

	sscanf(src,"%04u",&magic);
	strcpy(buf,src+4);

	//printf("\nPass Decode magic : [%u]\n\n",magic);
	//printf("\nPass Decode 1: [%s]\n\n",buf);

	n-= 4; len= n/2;
	magic= magic%49;

	for(i=0; i<len; i++)
	{
		tmp= buf[i];
		buf[i]=buf[len+i];
		buf[len+i]= tmp;
	}

	//printf("\nPass Decode 2: [%s]\n\n",buf);

	for(i=0; i<len; i++)
	{
		tmp= buf[i];
		buf[i]=buf[n-i-1];
		buf[n-i-1]= tmp;
	}

	//printf("\nPass Decode 3: [%s]\n\n",buf);

	for(i=0,j=0; i<len;i++,j+=2)
	{
		sscanf(buf+j,"%02x",&(dest[i]));
		dest[i] -= (magic+i);
	}
	dest[i]=0;
	return dest;
}
