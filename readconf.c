#include <stdio.h>
#ifdef SYSV
# include <stdlib.h>
# include <string.h>
#else
# include <string.h>
# include <strings.h>
#endif
#include <math.h>	/* for strtod() */
#include <malloc.h>
#include <errno.h>

#include "readconf.h"


#ifndef TRUE
# define TRUE	1
# define FALSE	0
#endif /*TRUE*/

/*
** Tweak these as needed (change values, make them public, etc).
*/
static int cf_debug = FALSE;		/* print debugging info */
static int cf_verbose = FALSE;		/* print progress info */
static int cf_forgiving = FALSE;	/* allow unknown tags? */
static int cf_check_alloc = TRUE;	/* only malloc onto NULL pointer? */

/*#define NEED_STRCASECMP*/

#ifdef NEED_STRCASECMP		/* not uncommon for libc to be missing this */
/*
** Like strcmp(), but case independent.
*/
int strcasecmp(register const char *str1, register const char *str2)
{
	while (*str1 && *str2 && toupper(*str1) == toupper(*str2)) str1++, str2++;
	return (*str1 - *str2);
}

/*
** Like strncmp(), but case independent.
*/
int strncasecmp(register const char *str1, register const char *str2, size_t n)
{
	while (n && *str1 && *str2 && toupper(*str1) == toupper(*str2))
		str1++, str2++, n--;
	if (n)
		return (*str1 - *str2);
	else
		return (0);	/* no mismatch in first n chars */
}
#endif /*NEED_STRCASECMP*/


typedef int (*CF_INTFUNCPTR)();	/* pointer to function returning int */


/*
** Local prototypes.
*/
static int add_ms(char *str);
static int convert_ms(void);
static const CONFIG * lookup_tag(const CONFIG *config_tab, int config_count, char *tag_name);
static int read_line(FILE *fp);
static int get_full_line(const CONFIG *config_tab, int config_size, FILE *fp);
static int eval_boolean(const CONFIG *configp);
static int eval_int(const CONFIG *configp);
static int eval_double(const CONFIG *configp);
static int eval_string(const CONFIG *configp);
static int eval_multi_string(const CONFIG *configp);
static int eval_multi_line(const CONFIG *configp, FILE *fp);

#define CF_MAX_LINE_LEN	1024
#define CF_MAX_TAG_LEN 32
#define CF_MAX_MS_TOKEN 128			/* just a sanity check */

const char *default_argv0 = "<caller unknown>";

/* private definition of isspace(); only tab and space */
#define cf_isspace(x)	((x) == ' ' || (x) == '\t')
#define cf_iseol(x)		((x) == '\n' || (x) == '\0')

/* local buffers */
static char line_buf[CF_MAX_LINE_LEN+1];		/* used with fgets() */
static char full_line_buf[CF_MAX_LINE_LEN+1];	/* post-processing version */
static char tag_buf[CF_MAX_TAG_LEN+1];			/* the tag, in lower case */

static int line;				/* current line # in input */

static char *cf_err_str = NULL;		/* error message */


/* MULTI_STRING data */
typedef struct list_t
{
	char *data;
	struct list_t *next;
} LIST;

static int ms_argc;
static char **ms_argv;
static LIST *head = NULL, *tail;

/*
** Initialize MULTI_STRING stuff.
*/
static int init_ms(void)
{
	if (head != NULL)
	{
		fprintf(stderr, "init_ms(): called twice without convert_ms()\n");
		return (-1);
	}
	ms_argc = 0;
	ms_argv = (char **)NULL;

	return (0);
}

/*
** Add a new token to the MULTI_STRING list.
*/
static int add_ms(char *str)
{
	LIST *lp;

	if (ms_argc >= CF_MAX_MS_TOKEN)
	{
		cf_err_str = "too many tokens on one line";
		return (-1);
	}
	ms_argc++;

	lp = (LIST *)malloc(sizeof(LIST));
	if (lp == NULL)
	{
		fprintf(stderr, "add_ms(): malloc() failed\n");
		return (-1);
	}
	lp->data = str;
	lp->next = NULL;

	if (head == NULL)
	{
		head = tail = lp;
	}else
	{
		tail->next = lp;
		tail = lp;
	}

	return (0);
}

/*
** Convert the list of tokens into an argument vector.  Frees the list nodes.
*/
static int convert_ms(void)
{
	LIST *lp, *tmplp;
	int i;

	/* allocate space for the string vector; freed in eval_multi_string */
	ms_argv = (char **)malloc(sizeof(char *) * ms_argc);

	/* copy the pointers from the list to the vector, freeing the list nodes */
	i = 0;
	lp = head;
	while (lp != NULL)
	{
		ms_argv[i++] = lp->data;
		tmplp = lp->next;
		free(lp);
		lp = tmplp;
	}
	head = tail = NULL;

	/* if the last token got eaten as trailing whitespace, nuke it */
	if (*(ms_argv[ms_argc-1]) == '\0')
		ms_argc--;

	if (cf_debug)
	{
		int j;
		for (j = 0; j < ms_argc; j++)
			printf("  TOKEN: '%s'\n", ms_argv[j]);
	}

	return (0);
}


/*
** Look up a tag in the config[] array.
**
** Returns a pointer to the CONFIG struct if found, NULL if not.
*/
static const CONFIG * lookup_tag(const CONFIG *config_tab, int config_count, char *tag_name)
{
	const CONFIG *conp;
	int i;

	for (conp = config_tab, i = 0; i < config_count; i++, conp++)
	{
		if (strcasecmp(conp->tag, tag_name) == 0)
		{
			if (cf_debug) printf("lookup: '%s' found\n", tag_name);
			return (conp);
		}
	}
	if (cf_debug) printf("lookup: '%s' NOT FOUND\n", tag_name);
	return (NULL);
}


/*
** Read a line of input.
**
** Returns 0 on success, 1 on EOF reached, -1 on error.  Results are placed
** into line_buf.
*/
static int read_line(FILE *fp)
{
	if ((fgets(line_buf, CF_MAX_LINE_LEN, fp)) == NULL)
	{
		if (ferror(fp)) return (-1);
		if (feof(fp)) return (1);
		cf_err_str = "fgets() failed, but I don't know why";
		return (-1);
	}

	line++;

	if (cf_debug) printf("LINE(%d): '%s'\n", line, line_buf);
	return (0);
}

/*
** Read a full line of input from the configuration file, doing all of the
** appropriate processing.
**
** Uses a DFA to process the input.  I cheat a bit and allow transitions
** without eating a character.
**
** Returns 0 on success, 1 on EOF reached, and -1 on error.  The results
** of the routine are placed into tag_buf and full_line_buf.
*/
static int get_full_line(const CONFIG *config_tab, int config_size, FILE *fp)
{
	enum
	{
		R_CHAOS,
		R_START,
		R_TAG,
		R_PRECOLON,
		R_POSTCOLON,
		R_RHS,
		R_RHS_MS,
		R_DQUOTE,
		R_BACK,
		R_DQ_BACK,
		R_ERROR,
		R_DONE
	}state;

	const CONFIG *configp = NULL;
	char *inp, *outp, *cp;
	int val, is_ms = 0;

	full_line_buf[0] = '\0';
	inp = outp = NULL;
	init_ms();
	state = R_CHAOS;

	/*
	** Welcome to the machine.
	*/
	while (state != R_DONE)
	{
		switch (state)
		{
		case R_CHAOS:				/* in the beginning, there was... */
			if ((val = read_line(fp)))
				return (val);			/* bail on error or EOF */
			inp = line_buf;
			state = R_START;
			break;

		case R_START:				/* skip leading whitespace */
			if (cf_isspace(*inp))
			{		/* WS --> loop */
				inp++;
				break;
			}
			if (*inp == '#' || cf_iseol(*inp))
			{
				state = R_CHAOS;		/* '#' or blank --> R_CHAOS */
				break;
			}
			state = R_TAG;				/* else --> R_TAG */
			outp = tag_buf;
			break;

		case R_TAG:					/* copy tag into tag_buf */
			/* terminate outp in case we exit */
			*outp = '\0';
			if (cf_isspace(*inp))
			{		/* WS --> R_PRECOLON */
				inp++;
				state = R_PRECOLON;
				break;
			}
			if (*inp == '=')
			{			/* ':' --> R_POSTCOLON */
				inp++;
				state = R_POSTCOLON;
				break;
			}
			/* this happens for MULTI_LINE stuff */
			if (cf_iseol(*inp))
			{		/* EOL --> R_DONE */
				state = R_DONE;
				break;
			}

			*outp++ = *inp++;			/* else --> loop */
			break;

		case R_PRECOLON:			/* tag done, waiting for colon */
			if (*inp == '=')
			{			/* ':' --> R_POSTCOLON */
				inp++;
				state = R_POSTCOLON;
				break;
			}
			/* again, MULTI_LINE stuff here */
			if (cf_iseol(*inp))
			{		/* EOL --> R_DONE */
				state = R_DONE;
				break;
			}
			if (cf_isspace(*inp))
			{		/* WS --> loop */
				inp++;
				break;
			}

			cf_err_str = "invalid lhs - more than one word before colon";
			state = R_ERROR;
			break;

		case R_POSTCOLON:			/* got colon, eat whitespace */
			if (cf_isspace(*inp))
			{		/* WS --> loop */
				inp++;
				break;
			}
			if (cf_iseol(*inp))
			{		/* EOL --> R_ERROR */
				cf_err_str = "invalid rhs - no data after colon";
				state = R_ERROR;
				break;
			}

			/* figure out if it's a MULTI_STRING or not, since we have to
			   parse it differently if it is
			*/
			if ((configp = lookup_tag(config_tab, config_size, tag_buf)) == NULL)
			{
				if (cf_forgiving)
				{
					return (0);		/* error gets caught later */
				}
				cf_err_str = "unknown tag";
				state = R_ERROR;
				break;
			}
			is_ms = (configp->kind == CF_MULTI_STRING);
			outp = full_line_buf;
			if (is_ms)
				add_ms(outp);
			state = R_RHS;				/* else --> R_RHS */
			break;

		case R_RHS:					/* reading right-hand-side now */
			if (outp - full_line_buf >= CF_MAX_LINE_LEN)
			{
				cf_err_str = "invalid rhs - line too long";
				state = R_ERROR;
				break;
			}
			if (cf_iseol(*inp))
			{		/* EOL --> R_DONE */
				*outp = '\0';
				state = R_DONE;
				break;
			}
			if (*inp == '\"')
			{			/* '"' --> R_DQUOTE */
				inp++;
				state = R_DQUOTE;
				break;
			}
			if (*inp == '\\')
			{			/* '\' --> R_BACK */
				inp++;
				state = R_BACK;
				break;
			}
			if (is_ms)
			{
				/* see if we want to break here */
				if (strchr(configp->delim, *inp) != NULL)
				{
					*outp++ = '\0';
					inp++;
					state = R_RHS_MS;
					break;
				}
			}

			*outp++ = *inp++;			/* else --> loop */
			break;

		case R_RHS_MS:				/* skipping over delimiter(s) */
			if (strchr(configp->delim, *inp) == NULL)
			{
										/* !DELIM --> RHS */
				if (add_ms(outp) < 0)
				{	/* another one coming! */
					state = R_ERROR;
					break;
				}
				state = R_RHS;
				break;
			}

			inp++;						/* else --> loop */
			break;

		case R_DQUOTE:				/* inside double quotes */
			if (outp - full_line_buf >= CF_MAX_LINE_LEN)
			{
				cf_err_str = "invalid rhs - line too long";
				state = R_ERROR;
				break;
			}
			if (cf_iseol(*inp))
			{		/* EOL --> R_ERROR */
				cf_err_str = "reached EOL inside double quotes";
				state = R_ERROR;
				break;
			}
			if (*inp == '\"')
			{			/* '"' --> R_RHS */
				inp++;
				state = R_RHS;
				break;
			}
			if (*inp == '\\')
			{			/* '\' --> R_DQ_BACK */
				inp++;
				state = R_DQ_BACK;
				break;
			}

			*outp++ = *inp++;			/* else --> loop */
			break;

		case R_BACK:				/* escape the next character */
			if (outp - full_line_buf >= CF_MAX_LINE_LEN)
			{
				cf_err_str = "invalid rhs - line too long";
				state = R_ERROR;
				break;
			}
			if (cf_iseol(*inp))
			{		/* EOL --> read next */
				if ((val = read_line(fp)))
				{
					if (val > 0)
					cf_err_str = "can't escape EOF!";
					state = R_ERROR;
					break;
				}
				inp = line_buf;

				state = R_RHS;
				break;
			}

			switch (*inp)
			{
				case 'n':	*outp++ = '\n'; break;
				case 't':	*outp++ = '\t'; break;
				default:	*outp++ = *inp; break;
			}
			inp++;
			state = R_RHS;
			break;

		case R_DQ_BACK:				/* escape next, inside double quotes */
			if (outp - full_line_buf >= CF_MAX_LINE_LEN)
			{
				cf_err_str = "invalid rhs - line too long";
				state = R_ERROR;
				break;
			}
			if (cf_iseol(*inp))
			{		/* EOL --> read next */
				if ((val = read_line(fp)))
				{
					if (val > 0)
					cf_err_str = "can't escape EOF!";
					state = R_ERROR;
				}
				inp = line_buf;

				state = R_DQUOTE;
				break;
			}

			switch (*inp)
			{
				case 'n':	*outp++ = '\n'; break;
				case 't':	*outp++ = '\t'; break;
				default:	*outp++ = *inp; break;
			}
			inp++;
			state = R_DQUOTE;
			break;

		case R_DONE:					/* we're all done! */
			/* shouldn't actually get here, caught by while() */
			break;

		case R_ERROR:					/* error! */
			if (cf_debug) printf("--- full_line_buf R_ERROR\n");
			return (-1);

		default:
			cf_err_str = "damaged state in read_full_line()";
			return (-1);
		}
	}

	/*
	** Now trim off trailing white space.  In the case of a MULTI_STRING,
	** this could cause us to completely trim off the final string.  This
	** is fine... usually.
	*/
	cp = outp-1;
	while (cf_isspace(*cp))
		*cp-- = '\0';

	if (is_ms)
		convert_ms();

	if (cf_debug)
	{
		if (is_ms)
			printf("PARSED: '%s':(multi-line str)\n", tag_buf);
		else
			printf("PARSED: '%s':'%s'\n", tag_buf, full_line_buf);
	}

	return (0);
}


/*
** Evaluate an argument of the appropriate kind, storing the value in
** configp->reference or calling the function (*configp->reference)().
**
** Returns 0 on a successful evaluation, -1 on error.
*/

static int eval_boolean(const CONFIG *configp)
{
	if (strcasecmp(full_line_buf, "true") == 0 || strcasecmp(full_line_buf, "on") == 0)
	{
		*((int *) configp->reference) = TRUE;
		return (0);
	}else if (strcasecmp(full_line_buf, "false") == 0 || strcasecmp(full_line_buf, "off") == 0)
	{
		*((int *) configp->reference) = FALSE;
		return (0);
	}else
	{
		cf_err_str = "invalid value for a boolean";
		return (-1);
	}
}

static int eval_int(const CONFIG *configp)
{
	*((int *) configp->reference) = strtol(full_line_buf, NULL, 0);
	return (0);
}

static int eval_double(const CONFIG *configp)
{
	/* Can use atof() here.  Note this will fail if strtod() not prototyped. */
	*((double *) configp->reference) = (double)strtod(full_line_buf, NULL);
	return (0);
}

static int eval_string(const CONFIG *configp)
{
	char *cp;
	int len;

	len = strlen(full_line_buf);

	if (!configp->size)
	{
		/* need to allocate space */
		if (cf_check_alloc)
		{
			if (*((char **) configp->reference) != NULL)
			{
				cf_err_str = "tried to store dynamic string in non-NULL var";
				return (-1);
			}
		}
		if ((cp = (char *)malloc(len+1)) == NULL)
		{
			cf_err_str = "malloc() failed";
			return (-1);
		}
		if (cf_debug) printf("MALLOC 0x%.8lx\n", (long)cp);
		strcpy(cp, full_line_buf);
		*((char **) configp->reference) = cp;
	}else
	{
		/* reference is a fixed-size buffer */
		if (configp->size < (len+1))
		{
			cf_err_str = "string longer than allocated space";
			return (-1);
		}
		strcpy((char *) configp->reference, full_line_buf);
	}

	return (0);
}

static int eval_multi_string(const CONFIG *configp)
{
    int res;

    res = (*((CF_INTFUNCPTR) configp->reference))(ms_argc, ms_argv);
    free(ms_argv);		/* free vector of pointers into full_line_buf */
    ms_argc = 0;

    if (res < 0)
		cf_err_str = "error parsing string data";
    return (res);
}

static int eval_multi_line(const CONFIG *configp, FILE *fp)
{
	int res, len;

	len = strlen(configp->delim);

	while (1)
	{
		res = read_line(fp);
		if (res < 0) return (-1);
		/*if (res > 0) break;*/		/* EOF reached; allow it */
		if (res > 0) return (-1);	/* EOF reached; error */

		/* match as a prefix; don't require a '\n' after it in line_buf */
		if (strncasecmp(configp->delim, line_buf, len) == 0)
			break;

		res = (*((CF_INTFUNCPTR) configp->reference))(line_buf);
		if (res < 0)
		{
			cf_err_str = "error parsing multi-line data";
			return (-1);
		}
	}

	return (0);
}

/*
** Report a problem to the user.
**
** This exists because of a specific application in something I was working
** on (multiple independently updated programs sharing a config file, some
** of which didn't have a stdout/stderr), so it's not used consistently.
*/
static void report_problem(char *whine, int cline)
{
	/*syslog(LOG_ALERT|LOG_USER, "WARNING: config line %d: %s", cline, whine);*/
	fprintf(stderr, "WARNING: config line %d: %s\n", cline, whine);
}


/*
** ===========================================================================
**	Public routines.
** ===========================================================================
*/

/*
** Main entry point.
**
** Returns 0 on success, -1 on failure.  Appropriate diagnostic messages
** will be sent to stderr on failure.
**
** (Takes argv0 as an argument to generate nice error messages.)
*/
int read_config(const char *argv0, const CONFIG *config_tab, int config_size, FILE *fp)
{
	const CONFIG *configp;
	int err, res;

	/* sanity check on config table */
	if (config_tab[0].kind > CF_MULTI_LINE || config_size < 0 || config_size > 1024)
	{
		cf_err_str = "config table corrupt";
		goto error;
	}

	if (argv0 == NULL)
		argv0 = default_argv0;

	line = 0;

	while (1)
	{
		/* read a full config line into full_line_buf, tag in tag_buf */
		res = get_full_line(config_tab, config_size, fp);
		if (res > 0) break;		/* EOF reached */
		if (res < 0) goto error;	/* error */

		/* find the matching entry in config[] */
		if ((configp = lookup_tag(config_tab, config_size, tag_buf)) == NULL)
		{
			cf_err_str = "unknown tag";
			if (cf_forgiving)
			{
				report_problem(cf_err_str, line);
				continue;
			}else
			{
				goto error;
			}
		}

		/* empty right-hand-side only allowed on MULTI_LINE tag */
		if (configp->kind != CF_MULTI_LINE && full_line_buf[0] == '\0')
		{
			/* (...but full_line_buf is irrelevant for MULTI_STRING) */
			if (!(configp->kind == CF_MULTI_STRING && ms_argc))
			{
				cf_err_str = "missing rhs";
				goto error;
			}
		}

		/* do something appropriate with the tag we got */
		switch (configp->kind)
		{
		case CF_BOOLEAN:
			res = eval_boolean(configp);
			break;
		case CF_INT:
			res = eval_int(configp);
			break;
		case CF_DOUBLE:
			res = eval_double(configp);
			break;
		case CF_STRING:
			res = eval_string(configp);
			break;
		case CF_MULTI_STRING:
			res = eval_multi_string(configp);
			break;
		case CF_MULTI_LINE:
			res = eval_multi_line(configp, fp);
			break;
		default:
			cf_err_str = "bogus config kind?";
			res = -1;
		}

		if (res < 0) goto error;
	}

	if (cf_verbose) printf("--- configuration successfully read\n");
	return (0);

error:
	err = errno;
	fflush(stdout);
	errno = err;
	if (cf_err_str != NULL)
	{
		fprintf(stderr, "%s: ", argv0);
		if (line) fprintf(stderr, "line %d: ", line);
		fprintf(stderr, "%s\n", cf_err_str);
	}else
	{
		perror(argv0);
	}
	cf_err_str = NULL;

	if (cf_verbose) printf("--- aborting config file read\n");
	return (-1);
}

/*
** Alternate entry point; takes a filename as an argument instead of a
** FILE*.  File will be opened before calling read_config(), and closed
** before returning.
*/
int read_config_file(const char *argv0, const CONFIG *config_tab, int config_count, const char *filename)
{
	FILE *fp;
	int err, res;

	if (argv0 == NULL)
		argv0 = default_argv0;

	if ((fp = fopen(filename, "r")) == NULL)
	{
		err = errno;
		fprintf(stderr, "%s: unable to read %s: ", argv0, filename);
		errno = err;	/* fprintf() could change errno */
		perror(NULL);
		return (-1);
	}

	if (cf_verbose) printf("--- reading configuration from '%s'\n", filename);

	res = read_config(argv0, config_tab, config_count, fp);
	fclose(fp);
	return (res);
}

/*
** Run down the config_tab, calling any dispose routines found.
**
** Entries with type=CF_STRING and size!=0 that are non-NULL and don't
** have a dispose routine will be free()d.  If cf_check_alloc is false
** this could end up calling free() on static storage, but that isn't fatal.
**
** To reload the config file, you should call this routine and then
** call read_config() or read_config_file().
*/
int dispose_config(const char *argv0, const CONFIG *config_tab, int config_count)
{
	const CONFIG *configp;

	if (argv0 == NULL)
		argv0 = default_argv0;

	for (configp = config_tab; config_count--; configp++)
	{
		if (configp->dispose != NULL)
		{
			if (cf_debug) printf("DISPOSE 0x%.8lx\n", (long)configp->dispose);
			(*configp->dispose)();
		}else
		{
			/* no dispose routine; see if we want to call free() on the "reference" field */
			if (configp->kind == CF_STRING && !configp->size && *((char **)configp->reference) != NULL)
			{
				char *cp = *((char **) configp->reference);
				if (cf_debug) printf("FREE 0x%.8lx\n", (long)cp);
				free(cp);
				*((char **)configp->reference) = NULL;
			}
		}
	}

	return (0);
}
