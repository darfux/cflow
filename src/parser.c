/* $Id$ */
/*  cflow:
 *  Copyright (C) 1997 Gray
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <stdio.h>
#include <stdlib.h>
#include "cflow.h"
#include "parser.h"
#include "obstack1.h"

typedef struct {
    char *name;
    int type_end;
    int parmcnt;
    int line;
    enum storage storage;
} Ident;

void parse_declaration(Ident*);
void parse_function_declaration(Ident*);
void parse_dcl(Ident*);
void parse_knr_dcl(Ident*);
void parse_typedef();
void parse_struct();
void expression();
void initializer_list();
void func_body();
void declare(Ident*);
void declare_type(Ident*);
int dcl(Ident*);
int parmdcl(Ident*);
int dirdcl(Ident*);
void skip_struct();
Symbol *get_symbol(char *name);
    
void call(char*, int);
void reference(char*, int);

int level;
char *declstr;
Symbol *caller;
struct obstack text_stk;

typedef struct {
    int type;
    char *token;
    int line;
} TOKSTK;

typedef int Stackpos[1];

TOKSTK tok;
TOKSTK *token_stack;
int tos;
int curs;
int token_stack_length = 64;
int token_stack_increase = 32;
static int need_space;

void mark(Stackpos);
void restore(Stackpos);
void tokpush(int,int,char*);
void save_token(TOKSTK *);


void
mark(pos)
    Stackpos pos;
{
    pos[0] = curs;
}

void
restore(pos)
    Stackpos pos;
{
    curs = pos[0];
    if (curs)
	tok = token_stack[curs-1];
}

void
tokpush(type, line, token)
    int type;
    int line;
    char *token;
{
    token_stack[tos].type = type;
    token_stack[tos].token = token;
    token_stack[tos].line = line;
    if (++tos == token_stack_length) {
	token_stack_length += token_stack_increase;
	token_stack = realloc(token_stack,
			      token_stack_length*sizeof(*token_stack));
	if (!token_stack)
	    error(FATAL(3), "Out of token pushdown");
    }
}

void
clearstack()
{
    int delta = tos - curs;

    if (delta) 
	memmove(token_stack, token_stack+curs, delta*sizeof(token_stack[0]));

    tos = delta;
    curs = 0;
}

void
delete_tokens(sp)
    Stackpos sp;
{
    int delta = tos - curs;
    
    if (delta)
	memmove(token_stack+sp[0], token_stack+curs,
		delta*sizeof(token_stack[0]));
    restore(sp);
}

int
nexttoken()
{
    int type;
    
    if (curs == tos) {
	type = yylex();
	tokpush(type, line_num, yylval.str);
    }
    tok = token_stack[curs];
    curs++;
    return tok.type;
}

int
putback()
{
    if (curs == 0)
	error(FATAL(10), "can't putback");
    curs--;
    if (curs > 0) {
	tok.type = token_stack[curs-1].type;
	tok.token = token_stack[curs-1].token;
    } else
	tok.type = 0;
    return tok.type;
}

void
init_parse()
{
    obstack_init(&text_stk);
    token_stack = emalloc(token_stack_length*sizeof(*token_stack));
    clearstack();
}

void
save_token(tokptr)
    TOKSTK *tokptr;
{
    switch (tokptr->type) {
    case IDENTIFIER:
    case TYPE:
    case STRUCT:
    case PARM_WRAPPER:
    case WORD:
	if (need_space) 
	    obstack_1grow(&text_stk, ' ');
	obstack_grow(&text_stk, tokptr->token, strlen(tokptr->token));
	need_space = 1;
	break;
    case MODIFIER:
	if (need_space) 
	    obstack_1grow(&text_stk, ' ');
	if (tokptr->token[0] == '*') 
	    need_space = 0;
	else
	    need_space = 1;
	obstack_grow(&text_stk, tokptr->token, strlen(tokptr->token));
	break;
    case EXTERN: /* storage class specifiers are already taken care of */
    case STATIC:
	break;  
    case '(':
	if (need_space) 
	    obstack_1grow(&text_stk, ' ');
	/*fall through */
    default:
	obstack_1grow(&text_stk, tokptr->type);
	need_space = 0;
    }
}

void
save_stack()
{
    int i;

    need_space = 0;
    for (i = 0; i < curs-1; i++) 
	save_token(token_stack+i);
}

void
finish_save()
{
    obstack_1grow(&text_stk, 0);
    declstr = obstack_finish(&text_stk);
}

void
skip_to(c)
    int c;
{
    while (nexttoken()) {
	if (tok.type == c)
	    break;
    }
}
	
yyparse()
{
    Ident identifier;

    level = 0;
    clearstack();
    while (nexttoken()) {
	identifier.storage = ExternStorage;
	switch (tok.type) {
	case 0:
	    return 0;
	case TYPEDEF:
	    parse_typedef();
	    break;
	case STATIC:
	    identifier.storage = StaticStorage;
	    /* fall through */
	default:
	    if (is_function())
		parse_function_declaration(&identifier);
	    else
		parse_declaration(&identifier);
	    break;
	}
	clearstack();
    }
    /*NOTREACHED*/
}

void
expression()
{
    char *name;
    int line;
    int parens_lev;

    parens_lev = 0;
    while (1) {
	switch (tok.type) {
	case ';':
	    return;
	case LBRACE:
	case LBRACE0:
	case RBRACE:
	case RBRACE0:
	    putback();
	    return;
	case ',':
	    if (parens_lev == 0)
		return;
	    break;
	case 0:
	    if (verbose)
		file_error("unexpected eof in expression", 0);
	    return;
	    
	case IDENTIFIER:
	    name = tok.token;
	    line = tok.line;
	    nexttoken();
	    if (tok.type == '(') {
		call(name, line);
		parens_lev++;
	    } else {
		reference(name, line);
		if (tok.type == MEMBER_OF) {
		    while (tok.type == MEMBER_OF)
			nexttoken();
		} else {
		    putback();
		}
	    }
	    break;
	case '(':
	    /* maybe typecast */
	    if (nexttoken() == TYPE)
		skip_to(')');
	    else {
		putback();
		parens_lev++;
	    }
	    break;
	case ')':
	    parens_lev--;
	    break;
	}
	nexttoken();
    }
}

int
is_function()
{
    Stackpos sp;
    int res = 0;

    mark(sp);
/*    if (tok.type == STRUCT)
	nexttoken();*/
    while (tok.type == TYPE ||
	   tok.type == IDENTIFIER ||
	   tok.type == MODIFIER ||
	   tok.type == STATIC ||
	   tok.type == EXTERN)
	nexttoken();

    if (tok.type == '(') 
	res = nexttoken() != MODIFIER;
	    
    restore(sp);
    return res;
}

void
parse_function_declaration(ident)
    Ident *ident;
{
    ident->type_end = -1;
    parse_knr_dcl(ident);

    switch (tok.type) {
    default:
	if (verbose) 
	    file_error("expected ';'", 1);
	/* should putback() here */
	/* fall through */
    case ';':
	break;
    case LBRACE0:
    case LBRACE:
	caller = lookup(ident->name);
	func_body();
	break;
    case 0:
	if (verbose)
	    file_error("unexpected eof in declaration", 0);
    }
}

int
fake_struct(ident)
    Ident *ident;
{
    Stackpos sp;

    mark(sp);
    ident->type_end = -1;
    if (tok.type == STRUCT) {
	if (nexttoken() == IDENTIFIER) {
	    ident->type_end = tos;
	}
	putback();
	skip_struct();
	if (tok.type == IDENTIFIER || tok.type == MODIFIER) {
	    TOKSTK hold = tok;
	    restore(sp);
	    if (ident->type_end == -1) {
		/* there was no tag. Insert { ... } */
		tos = curs;
		token_stack[curs].type = IDENTIFIER;
		token_stack[curs].token = "{ ... }";
		tos++;
	    } else {
		tos = curs + 1;
	    }
	    tokpush(hold.type, hold.line, hold.token);
	} else {
	    if (tok.type != ';')
		file_error("missing ; after struct declaration");
	}
	return 1;
    }
    return 0;
}

void
parse_declaration(ident)
    Ident *ident;
{
    Stackpos sp;

    mark(sp);
    ident->type_end = -1;
    if (tok.type == STRUCT) {
	if (nexttoken() == IDENTIFIER) {
	    ident->type_end = tos;
	}
	putback();
	skip_struct();
	if (tok.type == IDENTIFIER) {
	    TOKSTK hold = tok;
	    restore(sp);
	    if (ident->type_end == -1) {
		/* there was no tag. Insert { ... } */
		tos = curs;
		token_stack[curs].type = IDENTIFIER;
		token_stack[curs].token = "{ ... }";
		tos++;
	    } else {
		tos = curs + 1;
	    }
	    tokpush(hold.type, hold.line, hold.token);
	} else {
	    if (tok.type == ';')
		return;
	    restore(sp);
	}
    }
 again:
    parse_dcl(ident);

 select:    
    switch (tok.type) {
    default:
	if (verbose) 
	    file_error("expected ';'", 1);
	/* should putback() here */
	/* fall through */
    case ';':
	break;
    case ',':
	tos = ident->type_end;
	restore(sp);
	goto again;
    case '=':
	nexttoken();
	if (tok.type == LBRACE || tok.type == LBRACE0)
	    initializer_list();
	else
	    expression();
	goto select;
	break;
    case LBRACE0:
    case LBRACE:
	func_body();
	break;
    case 0:
	if (verbose)
	    file_error("unexpected eof in declaration", 0);
    }
}

void
initializer_list()
{
    int lev = 0;
    while (1) {
	switch (tok.type) {
	case LBRACE:
	case LBRACE0:
	    lev++;
	    break;
	case RBRACE:
	case RBRACE0:
	    if (--lev <= 0) {
		nexttoken();
		return;
	    }
	    break;
	case 0:
	    file_error("unexpected eof in initializer list");
	    return;
	case ',':
	    break;
	default:
	    expression();
	    break;
	}
	nexttoken();
    }
}

void
parse_knr_dcl(ident)
    Ident *ident;
{
    ident->type_end = -1;
    parse_dcl(ident);
    if (strict_ansi)
	return;
    switch (tok.type) {
    case IDENTIFIER:
    case TYPE:
    case STRUCT:
	if (ident->parmcnt >= 0) {
	    /* maybe K&R function definition */
	    int i, parmcnt, stop;
	    Stackpos sp, new_sp;
	    Ident id;
	    
	    mark(sp);
	    parmcnt = 0;
	    
	    for (stop = 0; !stop && parmcnt < ident->parmcnt;
		 nexttoken()) {
		id.type_end = -1;
		switch (tok.type) {
		case LBRACE:
		case LBRACE0:
		    putback();
		    stop = 1;
		    break;
		case TYPE:
		case IDENTIFIER:
		case STRUCT:
		    putback();
		    mark(new_sp);
		    if (dcl(&id) == 0) {
 			parmcnt++;
			if (tok.type == ',') {
			    do {
				tos = id.type_end; /* ouch! */
				restore(new_sp);
				dcl(&id);
			    } while (tok.type == ',');
			} else if (tok.type != ';')
			    putback();
			break;
		    }
		    /* else fall through */
		default:
		    restore(sp);
		    return;
		}
	    }
	}
    }
}

void
skip_struct()
{
    int lev = 0;
    
    if (nexttoken() == IDENTIFIER) {
	nexttoken();
    } else if (tok.type == ';')
	return;

    if (tok.type == '{') {
	do {
	    switch (tok.type) {
	    case 0:
		file_error("unexpected eof in struct");
		return;
	    case LBRACE:
	    case LBRACE0:
		lev++;
		break;
	    case RBRACE:
	    case RBRACE0:
		lev--;
	    }
	    nexttoken();
	} while (lev);
    }
}

void
parse_typedef()
{
    Ident ident;

    ident.name = NULL;
    ident.type_end = -1;
    ident.parmcnt = -1;
    ident.line = -1;
    ident.storage = AnyStorage;

    nexttoken();
    if (!fake_struct(&ident))
	putback();
    
    dcl(&ident);
    if (ident.name) 
	declare_type(&ident);
}

void
parse_struct()
{
}

void
parse_dcl(ident)
    Ident *ident;
{
    ident->parmcnt = -1;
    ident->name = NULL;
    putback();
    dcl(ident);
    save_stack();
    if (ident->name)
	declare(ident);
}

int
dcl(idptr)
    Ident *idptr;
{
    int type;
    
    while (nexttoken() != 0 && tok.type != '(') {
	if (tok.type == MODIFIER) {
	    if (idptr && idptr->type_end == -1)
		idptr->type_end = curs-1;
	} else if (tok.type == IDENTIFIER) {
	    while (tok.type == IDENTIFIER)
		nexttoken();
	    type = tok.type;
	    putback();
	    if (type == TYPE)
		continue;
	    else if (type != MODIFIER) 
		break;
	}
    }
    if (idptr && idptr->type_end == -1)
	idptr->type_end = curs-1;
    return dirdcl(idptr);
}

int
dirdcl(idptr)
    Ident *idptr;
{
    int rc;
    int wrapper = 0;
    int *parm_ptr = NULL;
    
    if (tok.type == '(') {
	dcl(idptr);
	if (tok.type != ')' && verbose) {
	    file_error("expected ')'", 1);
	    return 1;
	}
    } else if (tok.type == IDENTIFIER) {
	if (idptr) {
	    idptr->name = tok.token;
	    idptr->line = tok.line;
	    parm_ptr = &idptr->parmcnt;
	}
    }

    if (nexttoken() == PARM_WRAPPER) {
	wrapper = 1;
	nexttoken(); /* read '(' */
    } else
	putback();
    while (nexttoken() == '[' || tok.type == '(') {
	if (tok.type == '[') 
	    skip_to(']');
	else {
	    maybe_parm_list(parm_ptr);
	    if (tok.type != ')' && verbose) {
		file_error("expected ')'", 1);
		return 1;
	    }
	}
    }
    if (wrapper)
	nexttoken(); /* read ')' */
    return 0;
}

int
parmdcl(idptr)
    Ident *idptr;
{
    int type;

    while (nexttoken() != 0 && tok.type != '(') {
	if (tok.type == MODIFIER) {
	    if (idptr && idptr->type_end == -1)
		idptr->type_end = curs-1;
	} else if (tok.type == IDENTIFIER) {
	    while (tok.type == IDENTIFIER)
		nexttoken();
	    type = tok.type;
	    putback();
	    if (type != MODIFIER) 
		break;
	} else if (tok.type == ')' || tok.type == ',') 
	    return 0;
    }
    if (idptr && idptr->type_end == -1)
	idptr->type_end = curs-1;
    return dirdcl(idptr);
}


int
maybe_parm_list(parm_cnt_return)
    int *parm_cnt_return;
{
    int parmcnt=0;
    while (nexttoken()) {
	switch (tok.type) {
	case ')':
	    if (parm_cnt_return)
		*parm_cnt_return = parmcnt+1;
	    return;
	case ',':
	    parmcnt++;
	    break;
	default:
	    putback();
	    parmdcl(NULL);
	    putback();
	}
    }
    /*NOTREACHED*/
}

void
func_body()
{
    char *name;
    Ident ident;
    
    level++;
    while (level) {
	clearstack();
	nexttoken();
	switch (tok.type) {
	default:
	    expression();
	    break;
	case STATIC:
	case TYPE:
	    ident.storage = AutoStorage;
	    parse_declaration(&ident);
	    break;
	case LBRACE0:
	case '{':
	    level++;
	    break;
	case RBRACE0:
	    if (!ignore_indentation) {
		if (verbose && level != 1)
		    file_error("forced function body close", 0);
		for ( ; level; level--) {
		    delete_autos(level);
		}
		break;
	    }
	    /* else: fall thru */
	case '}':
	    delete_autos(level);
	    level--;
	    break;
	case 0:
	    if (verbose)
		file_error("unexpected eof in function body", 0);
	    return;
	}
    }
}

void
declare(ident)
    Ident *ident;
{
    Symbol *sp;
    
    finish_save();

    if (ident->storage == AutoStorage) {
	obstack_free(&text_stk, declstr);
	sp = install(ident->name);
	sp->type = SymFunction;
	sp->v.func.storage = ident->storage;
	sp->v.func.level = level;
	return;
    }
    
    sp = get_symbol(ident->name);
    sp->type = SymFunction;
    sp->v.func.argc = ident->parmcnt;
    sp->v.func.storage = ident->storage;
    sp->v.func.type = declstr;
    sp->v.func.source = filename;
    sp->v.func.def_line = ident->line;
    sp->v.func.level = level;
#ifdef DEBUG
    if (debug)
	printf("%s:%d: %s/%d defined to %s\n",
	       filename,
	       line_num,
	       ident->name, ident->parmcnt,
	       declstr);
#endif
}

void
declare_type(ident)
    Ident *ident;
{
    Symbol *sp;
    
    finish_save();
    sp = lookup(ident->name);
    for ( ; sp; sp = sp->next)
	if (sp->type == SymToken && sp->v.type.token_type == TYPE)
	    break;
    if (!sp)
	sp = install(ident->name);
    sp->type = SymToken;
    sp->v.type.token_type = TYPE;
    sp->v.type.source = filename;
    sp->v.type.def_line = ident->line;
    sp->v.type.ref_line = NULL;
#ifdef DEBUG
    if (debug)
	printf("%s:%d: type %s\n",
	       filename,
	       line_num,
	       ident->name);
#endif
}

Symbol *
get_symbol(name)
    char *name;
{
    Symbol *sp;

    if (sp = lookup(name)) {
	while (sp->type != SymFunction) 
	    sp = sp->next;
	if (sp)
	    return sp;
    }
    sp = install(name);
    sp->type = SymFunction;
    sp->v.func.argc = -1;
    sp->v.func.storage = ExternStorage;
    sp->v.func.type = NULL;
    sp->v.func.source = NULL;
    sp->v.func.def_line = -1;
    sp->v.func.ref_line = NULL;
    sp->v.func.caller = sp->v.func.callee = NULL;
    sp->v.func.level = -1;
    return sp;
}

Symbol *
add_reference(name, line)
    char *name;
    int line;
{
    Symbol *sp = get_symbol(name);
    Ref *refptr;
    Cons *cons;

    if (sp->v.func.storage == AutoStorage)
	return;
    refptr = emalloc(sizeof(*refptr));
    refptr->source = filename;
    refptr->line = line;
    append_to_list(&sp->v.func.ref_line, refptr);
    return sp;
}


void
call(name, line)
    char *name;
    int line;
{
    Symbol *sp = add_reference(name, line);
    Cons *cons;

    if (sp->v.func.argc < 0)
	sp->v.func.argc = 0;
    append_to_list(&sp->v.func.caller, caller);
    append_to_list(&caller->v.func.callee, sp);
}

void
reference(name, line)
    char *name;
    int line;
{
    add_reference(name, line);
}

void
print_token(tokptr)
    TOKSTK *tokptr;
{
    switch (tokptr->type) {
    case IDENTIFIER:
    case TYPE:
    case WORD:
    case MODIFIER:
    case STRUCT:
	fprintf(stderr, "`%s'", tokptr->token);
	break;
    case LBRACE0:
    case LBRACE:
	fprintf(stderr, "`{'");
	break;
    case RBRACE0:
    case RBRACE:
	fprintf(stderr, "`}'");
	break;
    case EXTERN:
	fprintf(stderr, "`extern'");
	break;
    case STATIC:
	fprintf(stderr, "`static'");
	break;
    case TYPEDEF:
	fprintf(stderr, "`typedef'");
	break;
    case OP:
	fprintf(stderr, "OP"); /* ouch!!! */
	break;
    default:
	fprintf(stderr, "`%c'", tokptr->type);
    }
}

file_error(msg, near)
    char *msg;
    int near;
{
    fprintf(stderr, "%s:%d: %s", filename, tok.line, msg);
    if (near) {
	fprintf(stderr, " near ");
	print_token(&tok);
    }
    fprintf(stderr, "\n");
}