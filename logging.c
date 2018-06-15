#include<stdio.h>
#include <sys/time.h>

void logit(char *logdir , char *section ,char *host,char *action, char *filename, char *msg)
{
	FILE *fp = NULL  ;
	char logfile[128] ; 
	char logdate[16] ; 
	char logtime[16] ; 

	time_t now = time( (time_t*) 0 );
	struct tm *t  = localtime( &now );

	strftime( logdate, sizeof(logdate), "%Y%m%d", t );
	strftime( logtime, sizeof(logtime), "%H:%M:%S", t );

	snprintf(logfile , sizeof(logfile),"%s/%s+%s.log",logdir,section,logdate);

	if( (fp = fopen(logfile,"a+")) )
	{
		if(msg) fprintf(fp,"%s %s\n",logtime, msg);
		else fprintf(fp,"%s %s-%s\n",logtime,action,filename);
		fclose(fp); fp = NULL ;
	}
}
