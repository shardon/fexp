// file motif.c

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <Xm/TextF.h>
#include <Xm/TextFP.h>
#include <Xm/LabelG.h>
#include <Xm/RowColumn.h>
#include <locale.h>
#include <ctype.h>


String fallbacks[] = {
	"*fontList:8x16,cclib16_1",
	NULL
};

main(int argc, char *argv[])
{
	Widget toplevel, text_w, rowcol;
	XtAppContext app;

	setlocale(LC_ALL, "");

	toplevel = XtVaAppInitialize(&app, "Demos",
		NULL, 0, &argc, argv, fallbacks, NULL);

	rowcol = XtVaCreateWidget("rowcol",
		xmRowColumnWidgetClass, toplevel,
		XmNorientation, XmHORIZONTAL,
		NULL);
	XtVaCreateManagedWidget("«Î ‰»Î:",
		xmLabelGadgetClass, rowcol, NULL);

	text_w = XtVaCreateManagedWidget("text_w",
		xmTextFieldWidgetClass, rowcol, 
		NULL);

	//XtAddCallback(text_w, XmNactivateCallback, Test, 0);
	XtAddCallback(text_w, XmNactivateCallback, NULL, 0);

	XtManageChild(rowcol);

	XtRealizeWidget(toplevel);

	XtAppMainLoop(app);
}
