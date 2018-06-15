//#define GTK_ENABLE_BROKEN

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <gtk/gtk.h>
#include <sys/param.h>
#include <sys/types.h>
#include "gtkwin.h"

typedef struct _ftp_window
{
	GtkWidget *text; 

	GtkWidget *pageLabel;
	GtkWidget *verboseCheck;
	GtkWidget *silenceCheck;

	GtkWidget *hostnameEntry;
	GtkWidget *localDirEntry;
	GtkWidget *remoteDirEntry;

	GtkWidget *fileEntry;
	GtkWidget *speedEntry;
	GtkWidget *elapsEntry;

	GtkWidget *pbar;

	int		verbose ;
	int		silence ;

}ftpw;


GdkColor color[3];
GdkFont *fixed_font ; 

static GtkWidget *window=NULL;
static int runLevel= 2;
static char display[64]={0};

//gint delete_event(GtkWidget *widget, GdkEvent *event, gpointer data);
//gint delete_event();
gint delete_event(GtkWidget *window);
void make_color();

void show_msg(ftpw *ftpwin,int level ,char *msg)
{
	int tlen = 0 ; 
	char tch ;
	char	buffer[1024];

	//printf("\nget message: [%s]\n",msg);

	GtkWidget *text = ftpwin->text ;

	if(ftpwin->silence == 0 && ! (level ==2 && ftpwin->verbose == 0) )
	{
		gtk_text_freeze (GTK_TEXT (text));

		gtk_text_set_point(GTK_TEXT(text),gtk_text_get_length(GTK_TEXT(text)) );

		gtk_text_insert (GTK_TEXT (text), NULL, NULL, NULL, "\n\n[Fexp] ", -1);
		gtk_text_insert (GTK_TEXT (text), fixed_font, &color[level], NULL, msg, -1);

		while(gtk_text_get_length(GTK_TEXT(text)) > 102400 )
		{	
			while(++tlen)
			{
				tch = GTK_TEXT_INDEX((GTK_TEXT(text)),tlen) ;
				if (tch == '\n') break;
			}
			gtk_text_set_point(GTK_TEXT(text),tlen+1);
			gtk_text_backward_delete(GTK_TEXT(text),tlen);
		}

		gtk_text_thaw (GTK_TEXT (text));

		gtk_adjustment_set_value( GTK_TEXT(text)->vadj , GTK_TEXT(text)->vadj->upper );
	}

}

void show_progress(void *out,int pg)
{

	ftpw *ftpwin = (ftpw *)out ;
	GtkWidget *pbar = ftpwin->pbar ;

	gtk_progress_set_value (GTK_PROGRESS (pbar), pg);

}

void set_file_label(void *out,char* text )
{
	ftpw *ftpwin = (ftpw *)out ;
	GtkWidget *et = ftpwin->fileEntry; 

	gtk_entry_set_text (GTK_ENTRY (et), text);
	//gtk_editable_select_region (GTK_EDITABLE (et), 0,GTK_ENTRY (et)->text_length);
}

void set_speed_label(void *out,char* text )
{
	ftpw *ftpwin = (ftpw *)out ;
	GtkWidget *et = ftpwin->speedEntry; 

	gtk_entry_set_text (GTK_ENTRY (et), text);

}

void set_elaps_label(void *out,char* text )
{
	ftpw *ftpwin = (ftpw *)out ;
	GtkWidget *et = ftpwin->elapsEntry; 

	gtk_entry_set_text (GTK_ENTRY (et), text);
}

void set_silence_check(GtkWidget *check,void *arg )
{
	ftpw *out = (ftpw *)arg ;

	out->silence = (out->silence) ? 0 : 1 ;
}
void set_verbose_check(GtkWidget *check,void *arg )
{
	ftpw *out = (ftpw *)arg ;

	out->verbose = (out->verbose) ? 0 : 1 ;
}

static int read_pipe_check(int fd)
{
    int ret;
    fd_set fdset;
    struct timeval tval;

    tval.tv_sec = 0;
    tval.tv_usec = 10;

    FD_ZERO(&fdset);
    FD_SET(fd, &fdset);

    return(select(fd+ 1, &fdset, NULL, NULL, &tval));

    //time out return 0;
}

FILE *readFp= NULL;
int readFd= -1;

gboolean  update_gui(gpointer *data)
{
	static buf[256];
	int pnum=-1,wnum=-1,level=-1;
	char *p,*w,*l;
	
	ftpw *ftpWin= (ftpw *)data;

	//if(read_pipe_check(readFd)==0) return(TRUE);

	memset(buf,0,sizeof(buf));
	while(fgets(buf,sizeof(buf),readFp))
	{
		//printf("\nget message [%s]\n",buf); break;

		p=strchr(buf,':'); *p=0; p++; pnum= atoi(buf);
		w=strchr(p,':'); *w=0; w++; wnum= atoi(p);
		l=strchr(w,':'); *l=0; l++; level= atoi(w);
		l[strlen(l)-1]=0;

		switch(wnum)
		{
			case 1: //show_msg
			{
				show_msg(&(ftpWin[pnum]),level,l);
				break;
			}
			case 2: //show_file_label
			{
				set_file_label(&(ftpWin[pnum]),l);
				break;
			}
			case 3: //set_speed_label
			{
				set_speed_label(&(ftpWin[pnum]),l);
				break;
			}
			case 4: //set_elaps_label
			{
				set_elaps_label(&(ftpWin[pnum]),l);
				break;
			}
			case 5: //show_file_label
			{
				show_progress(&(ftpWin[pnum]),atoi(l));
				break;
			}
			default: break;
		}

		//printf("\nget message: [%d][%d][%d][%s]\n",pnum,wnum,level,p+1);
	}


	return(TRUE);
	//return false means call once
}

int start_gui(config *cfg, int pagen, int readPipe)
{
	GtkWidget *button;
	GtkWidget *table;
	GtkWidget *vscrollbar;
	GtkWidget *hscrollbar;
	GtkWidget *notebook;
	GtkWidget *checkbutton;

	GtkWidget *mainbox;
	GtkWidget *ftpbox;
	GtkWidget *flagbox;
	GtkWidget *infobox;
	GtkWidget *filebox;
	GtkWidget *progressbox;

	GtkWidget *check;
	GtkWidget *label;
	GtkWidget *separator;

	GtkWidget *align;
	GtkAdjustment *adj;

	char buffer[256];
	int timer;
	int i ;

	ftpw *ftpWin = (ftpw *)calloc(sizeof(ftpw),pagen);
	memset(ftpWin,0,sizeof(ftpw)*pagen);

	//if(!CheckDisplay()) return NULL;
	//printf("\nsetup windown 1!\n");
	//if(!g_thread_supported ()) g_thread_init(NULL);
	//if (!g_thread_get_initialized())

	//gtk_init (&argc, &argv);


	printf("\nsetup windown 2!\n");

	//gtk_init (NULL, NULL);
	if(!gtk_init_check (NULL, NULL))
	{
		printf("\ngtk init fail!\n");
		return -1;
	}

	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title (GTK_WINDOW (window), "File Express     <ZZNode Copyright>");
	//gtk_window_set_title (GTK_WINDOW (window), "File Express");
	gtk_widget_set_usize (window, 800, 500);
	gtk_window_set_policy (GTK_WINDOW(window), FALSE, TRUE, FALSE);  
	//gtk_signal_connect(GTK_OBJECT(window),"delete_event",GTK_SIGNAL_FUNC(gtk_exit),NULL);
	//gtk_signal_connect(GTK_OBJECT(window),"delete_event",GTK_SIGNAL_FUNC(gtk_main_quit),NULL);
	gtk_signal_connect(GTK_OBJECT(window),"delete_event",GTK_SIGNAL_FUNC(delete_event),window);

	gtk_container_set_border_width (GTK_CONTAINER (window), 10);

	notebook = gtk_notebook_new ();
	gtk_notebook_set_tab_pos (GTK_NOTEBOOK (notebook), GTK_POS_TOP);
	gtk_notebook_set_scrollable( GTK_NOTEBOOK (notebook), TRUE );
	gtk_container_add (GTK_CONTAINER (window), notebook);

	printf("\nsetup windown 3!\n");

	for(i=0;i<pagen;i++)
	{
		ftpWin[i].silence = 0 ;
		ftpWin[i].verbose = 0 ;

		mainbox = gtk_vbox_new (FALSE, 0);
		//gtk_widget_show (mainbox);

		/*****************************/
  
		ftpbox = gtk_vbox_new (FALSE, 10);
		gtk_container_set_border_width (GTK_CONTAINER (ftpbox), 10);
		gtk_box_pack_start (GTK_BOX (mainbox), ftpbox, TRUE, TRUE, 0);
		//gtk_widget_show (ftpbox);
  
		table = gtk_table_new (2, 2, FALSE);
		gtk_table_set_row_spacing (GTK_TABLE (table), 0, 2);
		gtk_table_set_col_spacing (GTK_TABLE (table), 0, 2);
		gtk_box_pack_start (GTK_BOX (ftpbox), table, TRUE, TRUE, 0);
		//gtk_widget_show (table);
  
		/* Create the GtkText widget */
		ftpWin[i].text = gtk_text_new (NULL, NULL);

		gtk_table_attach(GTK_TABLE (table),ftpWin[i].text, 0, 1, 0, 1,
			GTK_EXPAND | GTK_SHRINK | GTK_FILL, GTK_EXPAND | GTK_SHRINK | GTK_FILL, 0, 0);
		//gtk_text_set_editable (GTK_TEXT (ftpWin[i].text), TRUE);
		//gtk_widget_show (ftpWin[i].text);

		/* Add a vertical scrollbar to the GtkText widget */
		vscrollbar = gtk_vscrollbar_new (GTK_TEXT (ftpWin[i].text)->vadj);
		gtk_table_attach (GTK_TABLE (table), vscrollbar, 1, 2, 0, 1,
			GTK_FILL, GTK_EXPAND | GTK_SHRINK | GTK_FILL, 0, 0);
		//gtk_widget_show (vscrollbar);

		flagbox = gtk_hbutton_box_new ();
		gtk_box_pack_start (GTK_BOX (ftpbox), flagbox, FALSE, FALSE, 0);
		//gtk_widget_show (flagbox);

		ftpWin[i].verboseCheck = gtk_check_button_new_with_label("Verbose");
		gtk_box_pack_start (GTK_BOX (flagbox), ftpWin[i].verboseCheck, FALSE, FALSE, 0);
		gtk_signal_connect (GTK_OBJECT(ftpWin[i].verboseCheck), "toggled",
								GTK_SIGNAL_FUNC(set_verbose_check), (void *)&(ftpWin[i]));
		//gtk_widget_show (ftpWin[i].verboseCheck);

		ftpWin[i].silenceCheck = gtk_check_button_new_with_label("Freeze");
		gtk_box_pack_start (GTK_BOX (flagbox), ftpWin[i].silenceCheck, FALSE, TRUE, 0);
		gtk_signal_connect (GTK_OBJECT(ftpWin[i].silenceCheck), "toggled", 
				GTK_SIGNAL_FUNC(set_silence_check), (void *)&(ftpWin[i]));
		//gtk_widget_show (ftpWin[i].silenceCheck);

		separator = gtk_hseparator_new ();
		gtk_box_pack_start (GTK_BOX (mainbox), separator, FALSE, TRUE, 0);
		//gtk_widget_show (separator);

		/*****************************/
		infobox = gtk_hbox_new (FALSE, 10);
		gtk_container_set_border_width (GTK_CONTAINER (infobox), 10);
		gtk_box_pack_start (GTK_BOX (mainbox), infobox, FALSE, TRUE, 0);
		//gtk_widget_show (infobox);

		label = gtk_label_new ("Server :");
		gtk_label_set_justify( GTK_LABEL(label), GTK_JUSTIFY_LEFT);
		gtk_box_pack_start (GTK_BOX (infobox), label, TRUE, FALSE, 0);
		//gtk_widget_show(label);

		ftpWin[i].hostnameEntry = gtk_entry_new_with_max_length(16);
		gtk_editable_set_editable (GTK_EDITABLE (ftpWin[i].hostnameEntry),FALSE);
		gtk_entry_set_text (GTK_ENTRY (ftpWin[i].hostnameEntry), cfg->hostname);
		gtk_box_pack_start (GTK_BOX (infobox), ftpWin[i].hostnameEntry, TRUE, FALSE, 0);
		//gtk_widget_show(ftpWin[i].hostnameEntry);

		label = gtk_label_new ("Local Dir :");
		gtk_label_set_justify( GTK_LABEL(label), GTK_JUSTIFY_LEFT);
		gtk_box_pack_start (GTK_BOX (infobox), label, TRUE, FALSE, 0);
		//gtk_widget_show(label);

		ftpWin[i].localDirEntry = gtk_entry_new_with_max_length(64);
		gtk_editable_set_editable (GTK_EDITABLE (ftpWin[i].localDirEntry),FALSE);
		gtk_entry_set_text (GTK_ENTRY (ftpWin[i].localDirEntry), cfg->localDir);
		gtk_box_pack_start (GTK_BOX (infobox), ftpWin[i].localDirEntry, TRUE, FALSE, 0);
		//gtk_widget_show(ftpWin[i].localDirEntry);

		label = gtk_label_new ("Remote Dir :");
		gtk_label_set_justify( GTK_LABEL(label), GTK_JUSTIFY_LEFT);
		gtk_box_pack_start (GTK_BOX (infobox), label, TRUE, FALSE, 0);
		//gtk_widget_show(label);

		ftpWin[i].remoteDirEntry = gtk_entry_new_with_max_length(64);
		gtk_editable_set_editable (GTK_EDITABLE (ftpWin[i].remoteDirEntry),FALSE);
		gtk_entry_set_text (GTK_ENTRY (ftpWin[i].remoteDirEntry), cfg->remoteDir);
		gtk_box_pack_start (GTK_BOX (infobox), ftpWin[i].remoteDirEntry, TRUE, FALSE, 0);
		//gtk_widget_show(ftpWin[i].remoteDirEntry);

		separator = gtk_hseparator_new ();
		gtk_box_pack_start (GTK_BOX (mainbox), separator, FALSE, TRUE, 0);
		//gtk_widget_show (separator);

		/*****************************/
		filebox = gtk_hbox_new (FALSE,10);
		gtk_container_set_border_width (GTK_CONTAINER (filebox), 10);
		gtk_box_pack_start (GTK_BOX (mainbox), filebox, FALSE, TRUE, 0);
		//gtk_widget_show (filebox);

		label = gtk_label_new ("File :  ");
		gtk_label_set_justify( GTK_LABEL(label), GTK_JUSTIFY_LEFT);
		gtk_box_pack_start (GTK_BOX (filebox), label, TRUE, FALSE, 0);
		//gtk_widget_show(label);

		ftpWin[i].fileEntry = gtk_entry_new_with_max_length(128);
		gtk_editable_set_editable (GTK_EDITABLE (ftpWin[i].fileEntry),FALSE);
		gtk_box_pack_start (GTK_BOX (filebox), ftpWin[i].fileEntry, TRUE, FALSE, 0);
		//gtk_widget_show(ftpWin[i].fileEntry);

		//label = gtk_label_new ("Speed : ");
		label = gtk_label_new ("Download rate :");
		gtk_label_set_justify( GTK_LABEL(label), GTK_JUSTIFY_LEFT);
		gtk_box_pack_start (GTK_BOX (filebox), label, TRUE, FALSE, 0);
		//gtk_widget_show(label);

		ftpWin[i].speedEntry = gtk_entry_new_with_max_length(32);
		gtk_editable_set_editable (GTK_EDITABLE (ftpWin[i].speedEntry),FALSE);
		gtk_box_pack_start (GTK_BOX (filebox), ftpWin[i].speedEntry, TRUE, FALSE, 0);
		//gtk_widget_show(ftpWin[i].speedEntry);

		label = gtk_label_new ("Elaps :     ");
		gtk_label_set_justify( GTK_LABEL(label), GTK_JUSTIFY_LEFT);
		gtk_box_pack_start (GTK_BOX (filebox), label, TRUE, FALSE, 0);
		//gtk_widget_show(label);

		ftpWin[i].elapsEntry = gtk_entry_new_with_max_length(32);
		gtk_editable_set_editable (GTK_EDITABLE (ftpWin[i].elapsEntry),FALSE);
		gtk_box_pack_start (GTK_BOX (filebox), ftpWin[i].elapsEntry, TRUE, FALSE, 0);
		//gtk_widget_show(ftpWin[i].elapsEntry);

		separator = gtk_hseparator_new ();
		gtk_box_pack_start (GTK_BOX (mainbox), separator, FALSE, TRUE, 0);
		//gtk_widget_show (separator);

		/*****************************/

		progressbox = gtk_vbox_new (FALSE, 10);
		gtk_container_set_border_width (GTK_CONTAINER (progressbox), 10);
		gtk_box_pack_start (GTK_BOX (mainbox), progressbox, FALSE, TRUE, 0);
		//gtk_widget_show (progressbox);

		/* Create a centering alignment object */
		align = gtk_alignment_new (0.5, 0.5, 1, 1);
		gtk_box_pack_start (GTK_BOX (progressbox), align, FALSE, FALSE, 5);
		//gtk_widget_show(align);

		/* Create the GtkProgressBar */
		ftpWin[i].pbar = gtk_progress_bar_new ();

		//gtk_progress_set_show_text (GTK_PROGRESS (ftpWin[i].pbar), TRUE);

		gtk_container_add (GTK_CONTAINER (align), ftpWin[i].pbar);
		//gtk_widget_show(ftpWin[i].pbar);

		/* Add a timer callback to update the value of the progress bar */
		//timer = gtk_timeout_add (100*(i+1), progress_timeout, ftpWin[i].pbar);

		/*****************************/

		ftpWin[i].pageLabel = gtk_label_new ("fexp");
		gtk_label_set_text(GTK_LABEL(ftpWin[i].pageLabel) ,cfg->section );
		gtk_notebook_append_page (GTK_NOTEBOOK (notebook), mainbox, ftpWin[i].pageLabel);
	}
	/******************************************************/

	make_color();

	/******************************************************/
	// Set start-page to page No. 1
	gtk_notebook_set_page (GTK_NOTEBOOK(notebook), 0);

	//gint gtk_idle_add( GtkFunction function, gpointer    data );

	printf("\nsetup windown 4![%d]\n",readPipe);

	readFd= readPipe;
	readFp= fdopen(readFd,"r");

	gtk_idle_add( update_gui, ftpWin);

	//gtk_widget_show(notebook);
	gtk_widget_show_all(window);

	printf("\nsetup windown 5 !\n");

	/* Add a timer callback to update the value of the progress bar */
	//timer = gtk_timeout_add (100, progress_timeout, ftpWin[i].pbar);

	gtk_main();

	//fclose(readFp);

	printf("\nsetup windown 6!\n");
	return runLevel;
}

gint click_yes(GtkWidget *w, gpointer *d)
{
	printf("\nclick yes\n");

	runLevel=0;

	gtk_widget_destroy(GTK_WIDGET(d));
	gtk_widget_destroy(GTK_WIDGET(window)); window=NULL;

	gtk_main_quit();

	return(TRUE);
}

gint click_no(GtkWidget *w, gpointer *d)
{
	printf("\nclick no\n");

	gtk_widget_destroy(GTK_WIDGET(d));
	return(TRUE);
}

gint click_backend(GtkWidget *w, gpointer *d)
{
	printf("\nclick backend\n");

	runLevel=1;
	gtk_widget_destroy(GTK_WIDGET(d));
	gtk_widget_destroy(GTK_WIDGET(window)); window=NULL;
	gtk_main_quit();
	return(TRUE);
}


gint delete_event(GtkWidget *window)
{
	GtkWidget *dialog, *button, *label; 

	//printf("\nClose window !\n");

	dialog= gtk_dialog_new();   
	//gtk_container_add (GTK_CONTAINER (window), dialog);

	label= gtk_label_new("Really Exit?");
	gtk_container_add (GTK_CONTAINER (GTK_DIALOG(dialog)->vbox), label);

	button = gtk_button_new_with_label("Yes");
	gtk_signal_connect(GTK_OBJECT(button),"clicked",GTK_SIGNAL_FUNC(click_yes),dialog);
	gtk_container_add (GTK_CONTAINER (GTK_DIALOG(dialog)->action_area), button);

	button = gtk_button_new_with_label("No");
	gtk_signal_connect(GTK_OBJECT(button),"clicked",GTK_SIGNAL_FUNC(click_no),dialog);
	gtk_container_add (GTK_CONTAINER (GTK_DIALOG(dialog)->action_area), button);

	button = gtk_button_new_with_label("Run as backend");
	gtk_signal_connect(GTK_OBJECT(button),"clicked",GTK_SIGNAL_FUNC(click_backend),dialog);
	gtk_container_add (GTK_CONTAINER (GTK_DIALOG(dialog)->action_area), button);

	gtk_widget_show_all (dialog);

	//return(FALSE);
	return(TRUE);
}

void make_color()
{
	// Get the system color map and allocate the color red 
	GdkColormap *cmap;
	cmap = gdk_colormap_get_system();

	color[0].red = 0xffff;
	color[0].green = 0;
	color[0].blue = 0;
	if (!gdk_color_alloc(cmap, &color[0]))
	{
		g_error("couldn't allocate color");
	}

	color[1].blue = 0xffff;
	color[1].green = 0;
	color[1].red = 0;
	if (!gdk_color_alloc(cmap, &color[1]))
	{
		g_error("couldn't allocate color");
	}

	color[2].blue = 0;
	color[2].green = 0xffff;
	color[2].red = 0;
	if (!gdk_color_alloc(cmap, &color[2]))
	{
		g_error("couldn't allocate color");
	}

	// Load a fixed font
	fixed_font = gdk_font_load ("-misc-fixed-medium-r-*-*-*-140-*-*-*-*-*-*");
}
