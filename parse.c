/******************************************************************************
 *
 *  File Name........: parse.c
 *
 *  Description......:
 *	Provides parse() a subroutine that parses the command line for
 *  the ash shell.  parse() return a pipe list.  Each element of the
 *  pipe list is a pipe that contains one or more commands.
 *
 *  Author...........: Vincent W. Freeh
 *
 *****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include "parse.h"

#define ERR_MSG		"Invalid input\n"
#define BUF_SIZE        63
#define EOS             '\0'   
#define Next()		do { LookAhead = nextToken(); } while (0)
#define LA		LookAhead
#define ReadChar(c)	do {c  = getchar(); if (c < 0) return Terror;} while (0)


#define InCmd(t)	((t)==Tword||(t)==Tin||(t)==Tout|| \
			 (t)==Tapp||(t)==ToutErr||(t)==TappErr)

#define PipeToken(t)	((t)==Tpipe||(t)==TpipeErr)


#define CmdToken(t)	((t)==Tsemi||(t)==Tamp)


#define EndOfInput(t)	((t)==Tend||(t)==Tnl||(t)==Terror)


char *_empty="empty";
char *_endd="end";
static struct cmd_t Empty={Tnil, Tnil, Tnil,"","",1,1,&_empty,NULL};
static struct cmd_t End={Tnil, Tnil, Tnil,"","",1,1,&_endd,NULL};
static Token LookAhead;
static char Word[BUF_SIZE+1];

extern void *malloc(size_t);
extern void *realloc(void *, size_t);


void *ckmalloc(unsigned);
static char *mkWord(char *);
static Cmd newCmd(char *);
static void freeCmd(Cmd);
static Cmd mkCmd();
static Pipe mkPipe();
static Token nextToken();

/*-----------------------------------------------------------------------------
 *
 * Name...........: mkCmd
 *
 * Description....: reads stdin and creates a Cmd (struct cmd_t*).  The
 * Cmd that is returned should be freed with freeCmd().
 *
 * Input Param(s).: Token inpipe -- if in a pipe, then this is the
 * pipe command, otherwise it is Tnil.
 *
 * Return Value(s): a Cmd or NULL if there was an error.
 *
 */

static Cmd mkCmd(Token inpipe)
{
  Cmd c;

  while ( CmdToken(LA) )	
    Next();

  if ( LA != Tword ) {		
    if ( LA == Tend )
      return &End;

    if ( LA == Tnl || LA == Terror )
     
      return &Empty;
    printf(ERR_MSG);

    return NULL;
  }

  assert(LA == Tword);
  c = newCmd(Word);
  Next();
  c->in = inpipe;

  while ( InCmd(LA) ) {		
    switch ( LA ) {
    case Tin:
      if ( c->in != Tnil ) {	
	printf("Ambiguous input redirect.\n");
	
	do {
	  Next();
	} while ( !EndOfInput(LA) );
	freeCmd(c);
	return NULL;
      }
      c->in = LA;
      Next();
      if ( LA != Tword ) {
	printf(ERR_MSG);
	
	do {
	  Next();
	} while ( !EndOfInput(LA) );
	freeCmd(c);
	return NULL;
      }	
      c->infile = mkWord(Word);		
      Next();
      break;

    case Tout:
    case ToutErr:
    case Tapp:
    case TappErr:
      if ( c->out != Tnil ) {
	printf("Ambiguous output redirect.\n");
	
	do {
	  Next();
	} while ( !EndOfInput(LA) );
	freeCmd(c);
	return NULL;
      }
      c->out = LA;			
      Next();
      if ( LA != Tword) {
	printf(ERR_MSG);
	
	do {
	  Next();
	} while ( !EndOfInput(LA) );
	freeCmd(c);
	return NULL;
      }
      c->outfile = mkWord(Word);	
      Next();
      break;

    case Tword:
      if ( c->args == NULL ) {
	printf("Hmmm...\n");
	exit(-2);
      }
      
      if ( c->nargs + 2 > c->maxargs ) {
	c->maxargs += c->maxargs;
	c->args = realloc(c->args, c->maxargs*sizeof(char *));
	if ( c->args == NULL ) {
	  perror("realloc");
	  exit(errno);
	}
      }
      c->args[c->nargs++] = mkWord(Word);	
      Next();
      break;

    default:
      printf("Shouldn't get here\n");
      exit(-1);
      break;
    }
  }
  if ( LA == Terror ) {		
    freeCmd(c);
    return NULL;
  }
  if ( LA == Tsemi )		
    Next();
  else if ( LA == Tamp ){
    c->exec = Tamp;
    Next();
  }
  c->args[c->nargs] = NULL;
  return c;
} /*---------- End of mkCmd -------------------------------------------------*/

/*-----------------------------------------------------------------------------
 *
 * Name...........: mkPipe
 *
 * Description....: Groups commands in a pipe.  Creates a list of
 * pipe, each of which contains one or more commands.
 *
 * Input Param(s).: none
 *
 * Return Value(s): Pipe (struct pipe_t*)
 *
 */

static Pipe mkPipe()
{
  Pipe p;
  Cmd c;

  c = mkCmd(Tnil);		

  
  if ( c == NULL || c == &Empty )
    return NULL;

 
  p = ckmalloc(sizeof(*p));
  p->type = Pout;	
  p->head = c;

  while ( PipeToken(LA) ) {
    if ( LA == TpipeErr )
      p->type = PoutErr;	
    else if(LA == Tpipe)
      p->type = Pout;     
    if ( c->out != Tnil ) {
      printf("Ambiguous output redirect.\n");
      
      do { 
	Next();
      } while ( !EndOfInput(LA) );
      freeCmd(c);
      return NULL;
    } else
      c->out = p->type == Pout ? Tpipe : TpipeErr;
    Next();
    c->next = mkCmd(p->type == Pout ? Tpipe : TpipeErr);
    if ( c->next == NULL || c->next == &Empty ) {
      if ( c->next == &Empty )
	printf("Invalid null command.\n");
      while ( !EndOfInput(LA) )
	Next();
      return NULL;
    }
    c = c->next;
  }

  
  p->next = NULL;
  while ( !EndOfInput(LA) ) {
    p->next = mkPipe();
    if ( !p->next )
      break;
  }
  return p;
} 

Pipe parse()
{
  Pipe p;

  Next();		
  p = mkPipe();
  return p;
}
void *ckmalloc(unsigned l)
{
  void *p;

  p = malloc(l);
  if ( p == NULL ) {
    perror("malloc");
    exit(errno);
  }
  return p;
}

static char *mkWord(char *s)
{
  char *b;

  b = ckmalloc(strlen(s)+1);
  strcpy(b, s);
  return b;
} /*---------- End of mkWord ------------------------------------------------*/

/*-----------------------------------------------------------------------------
 *
 * Name...........: newCmd
 *
 * Description....: allocates a new Cmd structure.  Initializes it
 * with sane values
 *
 * Input Param(s).: 
 *		char *cmd -- the command name (a word).
 *
 * Return Value(s): a Cmd structure
 *
 */

static Cmd newCmd(char *cmd)
{
  Cmd c;
  c = ckmalloc(sizeof(*c));
  c->nargs = 1;
  c->maxargs = 4;
  c->args = ckmalloc(c->maxargs*sizeof(char*));
  c->args[0] = mkWord(cmd);
  c->exec = Tsemi;
  c->in = c->out = Tnil;
  c->infile = c->outfile = NULL;
  c->next = NULL;
  return c;
} /*---------- End of newCmd ------------------------------------------------*/

/*-----------------------------------------------------------------------------
 *
 * Name...........: nextToken
 *
 * Description....: reads stdin and returns the next token.
 *
 * Input Param(s).: none
 *
 * Return Value(s): a Token
 *
 */

static Token nextToken()
{
  char* p;
  char c, q;

  Word[0] = EOS;
  p = Word;

  c = getchar();
  if ( c < 0 )
    return Tend;

  switch ( c ) {
  case ' ':
  case '\t':
    return nextToken();

  case '\n':
    return Tnl;
  case '&':
    return Tamp;
  case ';':
    return Tsemi;
  case '<':
    return Tin;

  case '|':			
    ReadChar(c);
    if ( c == '&' )
      return TpipeErr;
    ungetc(c, stdin);		
    return Tpipe;

  case '>':
    ReadChar(c);		
    if ( c == '>' ) {
      ReadChar(c);
      if ( c == '&' )
	return TappErr;
      else {
	ungetc(c, stdin);	
	return Tapp;
      }
    }
    else if ( c == '&' ) {
      return ToutErr;
    }
    else {
      ungetc(c, stdin);		
      return Tout;
    }
    break;

  case '\'':
  case '"':
  string:
   
    q = c;
   
    c = getchar();
     
    while ( c != q ) {
      if ( c < 0 || c == '\n' ) {	
	
	printf("Unmatched %c\n", q);
	return Terror;
      }
      *p++ = c;		
      if ( p > Word + BUF_SIZE ) {
	printf("String too long (> %d bytes)\n", BUF_SIZE);
	while ( (c = getchar()) > 0 && c != '\n' )
	  ;
	return Terror;
      }
      c = getchar();
    }
    *p++ = EOS;
    p = Word;
    return Tword;

  default:		
    
    while (1) {
      if ( c == '\\' ) {	
	ReadChar(c);
      }
      *p++ = c;
      if ( p > Word + BUF_SIZE ) {
	printf("Word too long (> %d bytes)\n", BUF_SIZE);
	while ( (c = getchar()) > 0 && c != '\n' )
	  ;
	return Terror;
      }

      ReadChar(c);

      switch ( c ) {
      case ' ':
      case '\t':
	*p++ = EOS;
	return Tword;
      case '\n':
      case '&':
      case ';':
      case '<':
      case '|':
      case '>':
	*p++ = EOS;
	ungetc(c, stdin);	
	p = Word;		
	return Tword;
      case '\'':
      case '\"':
      goto string;
      default:
	break;
      }
    }
  }
} /*---------- End of nextToken ---------------------------------------------*/

/*-----------------------------------------------------------------------------
 *
 * Name...........: freeCmd
 *
 * Description....: return heap storage associated with Cd
 *
 * Input Param(s).: 
 *		Cmd c -- the command that is to be freed
 *
 * Return Value(s): none
 *
 */

static void freeCmd(Cmd c)
{
  int i;

  if ( c == NULL || c == &Empty || c == &End ) return;

  freeCmd(c->next);

  if ( c->infile )
    free(c->infile);
  if ( c->outfile )
    free(c->outfile);
  if ( c->args ) {
    for ( i = 0; i < c->nargs; i++ )
      free(c->args[i]);
    free(c->args);
  }
  free(c);
} /*---------- End of freeCmd -----------------------------------------------*/

/*-----------------------------------------------------------------------------
 *
 * Name...........: freePipe
 *
 * Description....: return heap storage for pipe
 *
 * Input Param(s).: 
 *		Pipe p -- the pipe to be freed
 *
 * Return Value(s): none
 *
 */

void freePipe(Pipe p)
{
  if ( p == NULL )
    return;

  freeCmd(p->head);
  freePipe(p->next);
  free(p);
} /*---------- End of freePipe ----------------------------------------------*/

/*........................ end of parse.c ...................................*/
