#ifndef _READCONF_H
#define _READCONF_H
#include <stdio.h>

typedef enum
{
	CF_BOOLEAN,
	CF_INT,
	CF_DOUBLE,
	CF_STRING,
	CF_MULTI_STRING,
	CF_MULTI_LINE

} CF_LINE_KIND;

typedef struct
{
	CF_LINE_KIND kind;		/* what kind of statement this is */
	char *tag;				/* tag name */
	void *reference;		/* variable to change or function to call */
	int size;				/* used by string items only */
	char *delim;			/* used by multi-items only */
	int flags;				/* reserved, set to zero */
	void (*dispose)(void);	/* optional dispose function */

} CONFIG;

/*
** Prototoypes.
*/
int read_config(const char *argv0, const CONFIG *config_tab, int config_size, FILE *fp);
int read_config_file(const char *argv0, const CONFIG *config_tab, int config_count, const char *filename);
int dispose_config(const char *argv0, const CONFIG *config_tab, int config_count);

#endif /*_READCONF_H*/


 /*******************************************************************************
 * The basic configuration line looks like:
 *
 *	tag_name : value
 *
 * Leading and trailing whitespace is removed.  Whitespace before and after
 * the colon is removed.  Blank lines and lines starting with '#' are
 * ignored (this happens after the leading w/s is removed, so any line where
 * a '#' is the first non-whitespace char is ignored).
 *
 * Double quotes and backslashes may be used to escape whitespace and break
 * long lines.  The maximum length of a complete line (i.e. including parts
 * broken across newlines with '\') is CF_MAX_LINE_LEN.  The maximum length
 * of the tag is CF_MAX_TAG_LEN.  Note that the tag portion may NOT include
 * quotes or backslashes (well, they won't be interpreted specially).
 *
 * Some of the ANSI C character escape sequences (e.g. \n, \t) are supported.
 *
 *
 * The following classes of configuration statements are supported:
 *
 * CF_BOOLEAN		boolean_value: True
 *
 *	The corresponding variable is set to 0 if the rhs is "off" or "false",
 *	or 1 if the rhs is "on" or "true".  The values are case-insensitive.
 *
 * CF_INT		int_value: 12345
 *
 *	The corresponding variable is set to the value of the rhs as it's
 *	evaluated by strtol(str, NULL, 0).  So, "0x1234" and "01234" are
 *	treated as hex and octal, respectively.
 *
 * CF_DOUBLE		double_value: 12345.0
 *
 *	The corresponding variable is set to the value of the rhs as it's
 *	evaluated by strtod(str, NULL).  If the system is lame, atof(str)
 *	may be used instead.
 *
 * CF_STRING		string_value: The quick brown fox.
 *
 *	The value, stripped of leading and trailing whitespace, is copied
 *	with substitutions into the storage space indicated (see below).  The
 *	length of the space should be given in the "size" field.  If "size"
 *	is zero, and "reference" points to a NULL char*, then space will be
 *	allocated with malloc(3).  "size"==0, *"ref"!=NULL generates an error.
 *
 * CF_MULTI_STRING	multi_value: The quick brown fox.
 *
 *	The difference between CF_STRING and CF_MULTI_STRING is that the
 *	latter parses the string into separate components, and then passes
 *	them as individual arguments to a subroutine (see below).  The
 *	"delim" field is a pointer to a string with the field delimiters,
 *	usually whitespace ("\t ") or a colon (":").  The "size" field is
 *	not used here.
 *
 *	The function called by a CF_MULTI_STRING line takes two arguments,
 *	argc and argv, which are identical in form to the arguments supplied
 *	to main().  The strings are part of a static buffer, and the
 *	argument vector pointers are dynamically allocated space which will
 *	be freed after the procedure call, so the arguments must be copied
 *	if they are to be kept.
 *
 * CF_MULTI_LINE	multi_line_start
 *			line1
 *			line2
 *			line3
 *			multi_line_end
 *
 *	When you need a whole collection of free-form lines, use
 *	CF_MULTI_LINE.  Each line is passed verbatim (NO w/s stripping, NO
 *	'\' evaluation, etc) to the specified routine.  An end tag should be
 *	pointed to be the "delim" field; when the parser sees it at the start
 *	of a line it goes on to the next tag.
 *
 *	The function called by CF_MULTI_LINE takes one argument, the
 *	un-parsed rhs.  This is also in a static buffer, and must be copied.
 */
 /**************************************************************************
 * The structure defining the config file has five fields:
 *
 * kind
 *	What kind of field this is, e.g. CF_STRING.
 *
 * tag
 *	What tag will be used to identify this item, i.e. what word comes
 *	before the ':'.  Matching of tags is case-insensitive.
 *
 * reference
 *	Variable to change (for BOOLEAN, INT, DOUBLE, and STRING) or function
 *	to call (for MULTI_STRING and MULTI_LINE).  Note that string is
 *	either a (char *) for size != 0 or a (char **) for size == 0.
 *
 * size
 *	Used by STRING to specify the size of a buffer.  If set to zero,
 *	*and* the variable to change is NULL, space will be allocated with
 *	malloc().  This value should be equal to the TOTAL size of the
 *	buffer, i.e. "size" of 16 means 15 characters plus the '\0'.
 *
 * delim
 *	Used by MULTI_STRING to define where the word breaks are, and used
 *	by MULTI_LINE to define where the list ends.
 *
 * flags
 *	Currently not used.  See next section for possible uses.
 **************************************************************************/
 /**************************************************************************
 * Miscellaneous ideas:
 *
 * - allow a combined MULTI_STRING and MULTI_LINE for convenience.
 * - allow /bin/sh-style variable substitution, including environment
 *   variables.
 * - support all of the ANSI C escape sequences, especially \x07 and \007.
 *
 * - use the "flags" field to define items as "configure-once-only", so
 *   that a second instance of that tag generates an error.
 * - use the "flags" field to define MULTI_STRING delimiters as "single"
 *   or "many".  Right now "ack   foo  bar" results in 'ack', 'foo', and
 *   'bar', which is desirable.  However, parsing an /etc/passwd entry like
 *   "daemon:*:1:1::/:" will result in a missing field, because the "::" is
 *   meant to indicate an empty string for the fifth entry, but the current
 *   implementation will combine the delimiters together.
 * - use the "flags" field to define whether MULTI_LINE fields must have a
 *   closing delimiter, or if it's acceptable to just let them run until EOF.
 * - use the "flags" field to decide if we really want to strip off trailing
 *   whitespace.  Right now, it will cut off trailing spaces even if they
 *   are inside double quotes.  (Better yet, fix the code so that it deals
 *   with trailing w/s correctly.)
 *
 * - could move the configuration struct, along with all of these comments,
 *   into a separate header file for ease of use.  But that creates yet
 *   another file to deal with, which probably isn't desirable.
 ******************************************************************************/

