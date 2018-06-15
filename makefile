# By default, ftplib uses PASV.  If you need it to use  PORT
# instead, uncomment the next line
#DEFINES = -DFTPLIB_DEFMODE=FTPLIB_PORT

CC= gcc -g
TARGET = fexp
OBJECTS = fexp.o parsecfg.o dscan.o ftplib.o gtkwin.o logging.o 
SOURCES = fexp.c parsecfg.c dscan.c ftplib.c gtkwin.c logging.c
LIBS = `/usr/bin/gtk-config --libs gthread`

CFLAGS =

all : $(TARGET)

fexp : $(OBJECTS)
	$(CC) -o fexp $(OBJECTS) $(LIBS) 

fexp.o: fexp.c
	$(CC) -c $(CFLAGS) fexp.c -o fexp.o

parsecfg.o: parsecfg.c parsecfg.h
	$(CC) -c $(CFLAGS) parsecfg.c -o parsecfg.o

dscan.o: dscan.c dscan.h
	$(CC) -c $(CFLAGS) dscan.c -o dscan.o

ftplib.o: ftplib.c ftplib.h
	$(CC) -c $(CFLAGS) $(DEFINES) ftplib.c -o ftplib.o

logging.o: logging.c logging.h
	$(CC) -c $(CFLAGS) logging.c -o logging.o

gtkwin.o: gtkwin.c gtkwin.h
	$(CC) -c $(CFLAGS) `/usr/bin/gtk-config --cflags` gtkwin.c -o gtkwin.o

clean :
	rm -f $(OBJECTS) $(TARGET).exe
