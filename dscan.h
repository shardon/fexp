#if !defined(__DSCAN_H)
#define __DSCAN_H

typedef struct fattribute
{
	char	fn[256];
	struct stat st ;
}FATT;

FATT **dscan(char*,int*,char*);
FATT *list_parse_unix( char *,char * );

#endif
