#if !defined(__GTKWIN_H)
#define __GTKWIN_H

typedef struct _config
{
    char *section;
    char *action ;
    char *hostname;
    char *username;
    char *password;
    char *remoteDir;
    char *bakDir;
    char *localDir;
    char *tmpDir;
    char *logDir ;
    char *filematch;
    int  timeout ;
    int  logMsg ;

}config ;

//void show_message(void *out,int level ,char *fmt, ...);
//void show_progress(void *out,int pg);
//void set_file_label(void *out,char* text );
//void set_speed_label(void *out,char* text );
//void set_elaps_label(void *out,char* text );
int  start_gui(config *cfg, int pagen, int readPipe);

#endif
