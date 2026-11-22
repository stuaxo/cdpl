/* cli.h - command words (PLAY 3, W, ST ...) run without the UI */
#ifndef CLI_H
#define CLI_H

#define EXIT_OK      0
#define EXIT_FAIL    1              /* a command failed */
#define EXIT_NOCD    2              /* no MSCDEX / no CD drive */
#define EXIT_USAGE   3              /* bad command line */

/* argv[i] as a command word.  Returns 1 and sets *used to the number of
 * argv entries it takes (the word, plus an argument when the next entry
 * starts with a digit); 0 if argv[i] is not a command; -1 with err set
 * if it is one but its argument is missing. */
int cli_match(int argc, char **argv, int i, int *used, char *err);
int cli_run(int argc, char **argv, int init_rc);

#endif
