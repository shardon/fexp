#include<stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fnmatch.h>
#include <errno.h>
#include <time.h>

#ifdef STDC_HEADERS
# include <string.h>
# include <stdlib.h>
#endif

#ifdef MAJOR_IN_MKDEV
# include <sys/mkdev.h>
#else
# ifdef MAJOR_IN_SYSMACROS
#  include <sys/sysmacros.h>
# endif
#endif

#include "dscan.h"

static FATT *fprofile(char*,char*,char*);
static int fsorter(void*,void*);
static int ffilter(void*,void*);
static int _modestr(char *,mode_t *);
static char * strsep(char **stringp, char *delim);

/*
**
**
**
*/
FATT **dscan(char *dir,int *n,char *patt)
{
	DIR	*dirp = NULL ;
	struct dirent	*dp = NULL ;
	FATT	**f_tab = NULL ;
	FATT	*ft = NULL ;

	*n = 0 ;
	if( (dirp=opendir(dir))==NULL)
	{
		return (FATT **)(-1);
	}
	while(dp = readdir(dirp))
	{
		if((ft = fprofile(dir,dp->d_name,patt))==NULL) continue ;
		f_tab=(FATT **)realloc(f_tab,(*n+1)*sizeof(FATT *));
		if (f_tab == NULL)
		{
			closedir(dirp) ;
			return (FATT **)(-1);
		}
		f_tab[(*n)++] = ft ; 
	}

	closedir(dirp) ; 
	qsort((void *)(f_tab),(size_t)*n,sizeof(FATT *),fsorter);
	return(f_tab) ;
}
/*
**
*/
static FATT *fprofile(char *dir,char *file,char* patt)
{
	FATT	*ft;
	char	fullname[256];

	if( (ft = (FATT *)malloc(sizeof(FATT))) == 0 ) return 0 ;
	strncpy(ft->fn, file,sizeof(ft->fn));
	snprintf(fullname,sizeof(fullname),"%s/%s",dir,file);
	if( stat(fullname,&(ft->st)) < 0 || ffilter(ft,patt) )
	{
		free(ft);
		return(NULL);
	}
	return(ft);
}

/*
**
*/
static int ffilter(void *fatt,void *patt)
{
	FATT * fa = (FATT *)fatt ;
	char *pa = (char *)patt ;

	if( S_ISDIR(fa->st.st_mode) || *(fa->fn)=='.' ) return 1 ;
	return (pa && fnmatch(pa, fa->fn, 0)) ;
}

/*
**
*/
static int fsorter(void *f1, void *f2)
{
	FATT **a1 = (FATT **)f1 ;
 	FATT **a2 = (FATT **)f2 ;

	if((*a1)->st.st_mtime == (*a2)->st.st_mtime) return(0) ;
	if((*a1)->st.st_mtime < (*a2)->st.st_mtime) return(-1) ;
	return(1) ;
}

#define major_t unsigned short
#define minor_t unsigned short

/*
** FIXME: use strftime() instead ???
*/
static const char
*months[]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};

/*
** parse Unix directories
*/
FATT *list_parse_unix( char *buf ,char *patt)
{
	char *linep = buf, *fieldp, *cp, *save_fieldp = NULL;
	time_t now;
	struct tm tt, *tn;
	int i;
	unsigned long ul;
	major_t maj;
	minor_t min;
	char modestring[11];
	size_t sz;

	FATT tempft ;
	FATT *ft = NULL; 

	mode_t fs_mode ;
	char fs_username[16] ;
	char fs_groupname[16] ;
	dev_t fs_rdev ;
	off_t fs_size ;
	time_t fs_mtime;
	nlink_t fs_nlink ;
	char filename[32];
	char linkto[32];

	/*
	** for "/reuquested/path unreadable" messages, fail with EPERM
	*/
	if (buf[0] == '/' && strcmp(buf + strlen(buf) - 11, " unreadable") == 0)
	{
		errno = EPERM;
		return NULL;
	}

	/*
	** for error messages from ls or from the ftp server,
	** fail with EPERM
	*/
	if (strncmp(buf, "ls: ", 4) == 0 || strncmp(buf, "in.ftpd: ", 9) == 0)
	{
		errno = EPERM;
		return NULL;
	}

	/*
	** if the first field on the line is all numeric, it's
	** some funky unparsable thing.
	** (the Heimdal FTP server is known to do this sometimes)
	*/
	if (strspn(buf, "0123456789") == strcspn(buf, " \t"))
	{
		errno = EINVAL;
		return NULL;
	}

	/* skip the "total #" line */
	if (strncmp(buf, "total ", 6) == 0) return NULL;

	/*
	** initialize time stuff
	*/
	memset(&tt, 0, sizeof(tt));
	time(&now);
	tn = localtime(&now);
	if (tn->tm_isdst) tt.tm_isdst = 1;

	/*
	** mode
	*/
	strncpy(modestring, linep, sizeof(modestring));
	linep += sizeof(modestring) - 1;
	if (_modestr(modestring, &fs_mode) == -1)
	{
		errno = EINVAL;
		return NULL;
	}

	do
		fieldp = strsep(&linep, " \n");
	while (fieldp != NULL && *fieldp == '\0');
	fs_nlink = atoi(fieldp);

	/*
	** username
	*/
	do
		fieldp = strsep(&linep, " \n");
	while (fieldp != NULL && *fieldp == '\0');
	strncpy(fs_username, fieldp, sizeof(fs_username));

	/*
	** groupname (or maybe size/device - see next field)
	*/
	do
		fieldp = strsep(&linep, " \n");
	while (fieldp != NULL && *fieldp == '\0');
	strncpy(fs_groupname, fieldp, sizeof(fs_groupname));

	/*
	** save pointer to this field, just in case it's
	** actually the size field
	** this avoids truncation in the case where the size has
	** more digits than the length of the fs_groupname field
	** (see below)
	*/
	save_fieldp = fieldp;

	/*
	** size/device (or maybe month)
	*/
	do
		fieldp = strsep(&linep, " \n");
	while (fieldp != NULL && *fieldp == '\0');
	if (S_ISCHR(fs_mode) || S_ISBLK(fs_mode))
	{
		/* previous field is actually major device number */
		sz = strlen(fs_groupname) - 1;
		if (fs_groupname[sz] == ',')
		{
			sscanf(fs_groupname, "%lu", &ul);
			maj = (major_t)ul;
			strncpy(fs_groupname, "-1", sizeof(fs_groupname));
		}
		else
		{
			sscanf(fieldp, "%lu", &ul);
			maj = (major_t)ul;
			do
				fieldp = strsep(&linep, " \n");
			while (fieldp != NULL && *fieldp == '\0');
		}
		sscanf(fieldp, "%lu", &ul);
		min = (minor_t)ul;
		//fs_rdev = makedev(maj, min);

		/* read month into fieldp */
		do
			fieldp = strsep(&linep, " \n");
		while (fieldp != NULL && *fieldp == '\0');
	}
	else
	{
		for (i = 0; i < 12; i++)
		{
			if (strcmp(months[i], fieldp) == 0)
			{
				/*
				** yup, it's a month - that means the previous
				** field was the size, and the group wasn't
				** listed
				*/
				sscanf(save_fieldp, "%lu", &ul);
				fs_size = (off_t)ul;
				strncpy(fs_groupname, "-1", sizeof(fs_groupname));
				break;
			}
		}
		/* didn't find the month, so this must be the size */
		if (i == 12)
		{
			sscanf(fieldp, "%lu", &ul);
			fs_size = (off_t)ul;

			/* read month into fieldp */
			do
				fieldp = strsep(&linep, " \n");
			while (fieldp != NULL && *fieldp == '\0');
		}
	}

	/* month should be in fieldp now */
	for (i = 0; i < 12; i++)
	{
		if (strcmp(months[i], fieldp) == 0)
		{
			tt.tm_mon = i;
			break;
		}
	}

	/* if not a valid month, fail with EINVAL */
	if (i == 12)
	{
		errno = EINVAL;
		return NULL;
	}

	/* day of month */
	do
		fieldp = strsep(&linep, " \n");
	while (fieldp != NULL && *fieldp == '\0');
	tt.tm_mday = atoi(fieldp);

	/* hour/minute or year */
	do
		fieldp = strsep(&linep, " \n");
	while (fieldp != NULL && *fieldp == '\0');
	if ((cp = strchr(fieldp, ':')) != NULL)
	{
		*cp++ = '\0';
		tt.tm_hour = atoi(fieldp);
		tt.tm_min = atoi(cp);

		/* either this year or last... */
		tt.tm_year = tn->tm_year;
		if (tn->tm_mon < tt.tm_mon) tt.tm_year--;
	}
	else
		tt.tm_year = atoi(fieldp) - 1900;

	/* save date */
	fs_mtime = mktime(&tt);

	/* skip any extra spaces */
	linep += strspn(linep, " ");

	/* check if it's a link */
	if ((cp = strstr(linep, " -> ")) != NULL)
	{
		*cp = '\0';
		cp += 4;
		strncpy(linkto, cp, sizeof(linkto));
	}

	/* the rest is the filename */
	strncpy(filename, linep, sizeof(filename));

	strncpy(tempft.fn , filename,sizeof(tempft.fn));
	tempft.st.st_size = fs_size ;
	tempft.st.st_mode = fs_mode ;

	if(!ffilter(&tempft,patt) && (ft=(FATT *)malloc(sizeof(FATT)))) 
	{
		*ft = tempft ;
	}

	return ft;
}

static char * strsep(char **stringp, char *delim)
{
	register char *s;
	register const char *spanp;
	register int c, sc;
	char *tok;

	if ((s = *stringp) == NULL) return (NULL);
	for (tok = s;;)
	{
		c = *s++;
		spanp = delim;
		do
		{
			if ((sc = *spanp++) == c)
			{
				if (c == 0) s = NULL;
				else s[-1] = 0;
				*stringp = s;
				return (tok);
			}
		}while (sc != 0);
	}
	// NOTREACHED
}

/* does the opposite of strmode() */
static int _modestr(char *bp, mode_t *mode)
{
	*mode = 0;

	if (strlen(bp) < 10) return -1;

	/* file type */
	switch (bp[0])
	{
	case 'd':
		*mode |= S_IFDIR;
		break;
	case 'l':
		*mode |= S_IFLNK;
		break;
	case 's':
		*mode |= S_IFSOCK;
		break;
	case 'p':
		*mode |= S_IFIFO;
		break;
	case 'c':
		*mode |= S_IFCHR;
		break;
	case 'b':
		*mode |= S_IFBLK;
		break;
	case '-':
		*mode |= S_IFREG;
		break;
	default:
		return -1;
	}

	/* user bits */
	switch (bp[1])
	{
	case 'r':
		*mode |= S_IRUSR;
	case '-':
		break;
	default:
		return -1;
	}
	switch (bp[2])
	{
	case 'w':
		*mode |= S_IWUSR;
	case '-':
		break;
	default:
		return -1;
	}
	switch (bp[3])
	{
	case 's':
		*mode |= S_ISUID;
		/* intentional fall-through */
	case 'x':
		*mode |= S_IXUSR;
		break;
	case 'S':
		*mode |= S_ISUID;
	case '-':
		break;
	default:
		return -1;
	}

	/* group bits */
	switch (bp[4])
	{
	case 'r':
		*mode |= S_IRGRP;
	case '-':
		break;
	default:
		return -1;
	}
	switch (bp[5])
	{
	case 'w':
		*mode |= S_IWGRP;
	case '-':
		break;
	default:
		return -1;
	}
	switch (bp[6])
	{
	case 's':
		*mode |= S_ISGID;
		/* intentional fall-through */
	case 'x':
		*mode |= S_IXGRP;
		break;
	case 'S':
		*mode |= S_ISGID;
	case '-':
		break;
	default:
		return -1;
	}

	/* other bits */
	switch (bp[7])
	{
	case 'r':
		*mode |= S_IROTH;
	case '-':
		break;
	default:
		return -1;
	}
	switch (bp[8])
	{
	case 'w':
		*mode |= S_IWOTH;
	case '-':
		break;
	default:
		return -1;
	}
	switch (bp[9])
	{
	case 't':
		*mode |= S_ISVTX;
		/* intentional fall-through */
	case 'x':
		*mode |= S_IXOTH;
		break;
	case 'T':
		*mode |= S_ISVTX;
	case '-':
		break;
	default:
		return -1;
	}

	return 0;
}
#ifdef DSCAN_TEST
main(int argc, char** argv)
{
	FATT	**ftab = NULL ;
	int		i,n =0 ;
	char	*pa = NULL ;
	
	ftab = dscan(argv[1],&n,pa) ;
	if(ftab == (FATT **)-1)
	{
		fprintf(stderr,"Error!\n");
		exit(-1);
	}
	for(i=0; i<n; i++)
	{
		printf("%s\n", ftab[i]->fn);
		free(ftab[i]);
	}
	free(ftab);
}
#endif
