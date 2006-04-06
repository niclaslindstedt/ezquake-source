/*
Copyright (C) 1996-1997 Id Software, Inc.
 
This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
 
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  
 
See the GNU General Public License for more details.
 
You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

	$Id: cmd.c,v 1.36 2006-04-06 23:23:18 disconn3ct Exp $
*/

#include "quakedef.h"


#ifndef SERVERONLY
qbool CL_CheckServerCommand (void);
#endif

static void Cmd_ExecuteStringEx (cbuf_t *context, char *text);

cvar_t cl_warncmd = {"cl_warncmd", "0"};

cbuf_t	cbuf_main;
#ifndef SERVERONLY
cbuf_t	cbuf_svc;
cbuf_t	cbuf_safe, cbuf_formatted_comms;
#endif

cbuf_t	*cbuf_current = NULL;

//=============================================================================

//Causes execution of the remainder of the command buffer to be delayed until next frame.
//This allows commands like: bind g "impulse 5 ; +attack ; wait ; -attack ; impulse 2"
void Cmd_Wait_f (void)
{
	if (cbuf_current)
		cbuf_current->wait = true;
}

/*
=============================================================================
						COMMAND BUFFER
=============================================================================
*/

void Cbuf_AddText (char *text)
{
	Cbuf_AddTextEx (&cbuf_main, text);
}

void Cbuf_InsertText (char *text)
{
	Cbuf_InsertTextEx (&cbuf_main, text);
}

void Cbuf_Execute (void)
{
	Cbuf_ExecuteEx (&cbuf_main);
#ifndef SERVERONLY
	Cbuf_ExecuteEx (&cbuf_safe);
	Cbuf_ExecuteEx (&cbuf_formatted_comms);
#endif
}

//fuh : ideally we should have 'cbuf_t *Cbuf_Register(int maxsize, int flags, qbool (*blockcmd)(void))
//fuh : so that cbuf_svc and cbuf_safe can be registered outside cmd.c in cl_* .c
//fuh : flags can be used to deal with newline termination etc for cbuf_svc, and *blockcmd can be used for blocking cmd's for cbuf_svc
//fuh : this way cmd.c would be independant of '#ifdef CLIENTONLY's'.
//fuh : I'll take care of that one day.
static void Cbuf_Register (cbuf_t *cbuf, int maxsize)
{
	assert(!host_initialized);
	cbuf->maxsize = maxsize;
	cbuf->text_buf = (char *) Hunk_Alloc(maxsize);
	cbuf->text_start = cbuf->text_end = (cbuf->maxsize >> 1);
	cbuf->wait = false;
}

/*
============
Cbuf_Init
============
*/
void Cbuf_Init (void)
{
	Cbuf_Register(&cbuf_main, 1 << 18); // 256kb
#ifndef SERVERONLY
	Cbuf_Register(&cbuf_svc, 1 << 13); // 8kb
	Cbuf_Register(&cbuf_safe, 1 << 11); // 2kb
	Cbuf_Register(&cbuf_formatted_comms, 1 << 11); // 2kb
#endif
}

//Adds command text at the end of the buffer
void Cbuf_AddTextEx (cbuf_t *cbuf, char *text)
{
	int len, new_start, new_bufsize;

	len = strlen (text);

	if (cbuf->text_end + len <= cbuf->maxsize) {
		memcpy (cbuf->text_buf + cbuf->text_end, text, len);
		cbuf->text_end += len;
		return;
	}

	new_bufsize = cbuf->text_end-cbuf->text_start+len;
	if (new_bufsize > cbuf->maxsize) {
		Com_Printf ("Cbuf_AddText: overflow\n");
		return;
	}

	// Calculate optimal position of text in buffer
	new_start = ((cbuf->maxsize - new_bufsize) >> 1);

	memcpy (cbuf->text_buf + new_start, cbuf->text_buf + cbuf->text_start, cbuf->text_end-cbuf->text_start);
	memcpy (cbuf->text_buf + new_start + cbuf->text_end-cbuf->text_start, text, len);
	cbuf->text_start = new_start;
	cbuf->text_end = cbuf->text_start + new_bufsize;
}

//Adds command text at the beginning of the buffer
void Cbuf_InsertTextEx (cbuf_t *cbuf, char *text)
{
	int len, new_start, new_bufsize;

	len = strlen(text);

	if (len <= cbuf->text_start) {
		memcpy (cbuf->text_buf + (cbuf->text_start - len), text, len);
		cbuf->text_start -= len;
		return;
	}

	new_bufsize = cbuf->text_end - cbuf->text_start + len;
	if (new_bufsize > cbuf->maxsize) {
		Com_Printf ("Cbuf_InsertText: overflow\n");
		return;
	}

	// Calculate optimal position of text in buffer
	new_start = ((cbuf->maxsize - new_bufsize) >> 1);

	memmove (cbuf->text_buf + (new_start + len), cbuf->text_buf + cbuf->text_start,
	         cbuf->text_end - cbuf->text_start);
	memcpy (cbuf->text_buf + new_start, text, len);
	cbuf->text_start = new_start;
	cbuf->text_end = cbuf->text_start + new_bufsize;
}

#define MAX_RUNAWAYLOOP 1000

void Cbuf_ExecuteEx (cbuf_t *cbuf)
{
	int i, j, cursize, nextsize;
	char *text, line[1024], *src, *dest;
	qbool comment, quotes;

#ifndef SERVERONLY
	nextsize = cbuf->text_end - cbuf->text_start;
#endif

	while (cbuf->text_end > cbuf->text_start) {
		// find a \n or ; line break
		text = (char *) cbuf->text_buf + cbuf->text_start;

		cursize = cbuf->text_end - cbuf->text_start;
		comment = quotes = false;

		for (i = 0; i < cursize; i++) {
			if (text[i] == '\n')
				break;
			if (text[i] == '"') {
				quotes = !quotes;
				continue;
			}
			if (comment || quotes)
				continue;

			if (text[i] == '/' && i + 1 < cursize && text[i + 1] == '/')
				comment = true;
			else if (text[i] == ';')
				break;
		}

#ifndef SERVERONLY
		if ((cursize - i) < nextsize) // have we reached the next command?
			nextsize = cursize - i;

		// don't execute lines without ending \n; this fixes problems with
		// partially stuffed aliases not being executed properly

		if (cbuf_current == &cbuf_svc && i == cursize)
			break;
#endif

		// Copy text to line, skipping carriage return chars
		src = text;
		dest = line;
		j = min (i, sizeof(line) - 1);
		for ( ; j; j--, src++) {
			if (*src != '\r')
				*dest++ = *src;
		}
		*dest = 0;

		// delete the text from the command buffer and move remaining commands down  This is necessary
		// because commands (exec, alias) can insert data at the beginning of the text buffer
		if (i == cursize) {
			cbuf->text_start = cbuf->text_end = (cbuf->maxsize >> 1);
		} else {
			i++;
			cbuf->text_start += i;
		}

		cursize = cbuf->text_end - cbuf->text_start;
		Cmd_ExecuteStringEx (cbuf, line);	// execute the command line

		if (cbuf->text_end - cbuf->text_start > cursize)
			cbuf->runAwayLoop++;


		if (cbuf->runAwayLoop > MAX_RUNAWAYLOOP) {
			Com_Printf("\x02" "A recursive alias has caused an infinite loop.");
			Com_Printf("\x02" " Clearing execution buffer to prevent lockup.\n");
			cbuf->text_start = cbuf->text_end = (cbuf->maxsize >> 1);
			cbuf->runAwayLoop = 0;
		}

		if (cbuf->wait) {
			// skip out while text still remains in buffer, leaving it for next frame
			cbuf->wait = false;
#ifndef SERVERONLY

			cbuf->runAwayLoop += Q_rint(0.5 * cls.frametime * MAX_RUNAWAYLOOP);
#endif
			return;
		}
	}

	cbuf->runAwayLoop = 0;
}

/*
==============================================================================
						SCRIPT COMMANDS
==============================================================================
*/

/*
Set commands are added early, so they are guaranteed to be set before
the client and server initialize for the first time.
 
Other commands are added late, after all initialization is complete.
*/
void Cbuf_AddEarlyCommands (void)
{
	int i;

	for (i = 0; i < COM_Argc() - 2; i++) {
		if (strcasecmp(COM_Argv(i), "+set"))
			continue;
		Cbuf_AddText (va("set %s %s\n", COM_Argv(i+1), COM_Argv(i+2)));
		i += 2;
	}
}


/*
Adds command line parameters as script statements
Commands lead with a +, and continue until a - or another +
quake +prog jctest.qp +cmd amlev1
quake -nosound +cmd amlev1
*/
void Cmd_StuffCmds_f (void)
{
	int k, len;
	char *s, *text, *token;

	// build the combined string to parse from
	len = 0;
	for (k = 1; k < com_argc; k++)
		len += strlen (com_argv[k]) + 1;

	if (!len)
		return;

	text = (char *) Q_malloc (len + 1);
	for (k = 1; k < com_argc; k++) {
		strcat (text, com_argv[k]);
		if (k != com_argc - 1)
			strcat (text, " ");
	}

	// pull out the commands
	token = (char *) Q_malloc (len + 1);

	s = text;
	while (*s) {
		if (*s == '+')	{
			k = 0;
			for (s = s + 1; s[0] && (s[0] != ' ' || (s[1] != '-' && s[1] != '+')); s++)
				token[k++] = s[0];
			token[k++] = '\n';
			token[k] = 0;
			if (strncasecmp(token, "set ", 4))
				Cbuf_AddText (token);
		} else if (*s == '-') {
			for (s = s + 1; s[0] && s[0] != ' '; s++)
				;
		} else {
			s++;
		}
	}

	Q_free (text);
	Q_free (token);
}

void Cmd_Exec_f (void)
{
	char *f, name[MAX_OSPATH];
	int mark;

	if (Cmd_Argc () != 2) {
		Com_Printf ("exec <filename> : execute a script file\n");
		return;
	}

	strlcpy (name, Cmd_Argv(1), sizeof(name) - 4);
	mark = Hunk_LowMark();
	if (!(f = (char *) FS_LoadHunkFile (name)))	{
		char *p;
		p = COM_SkipPath (name);
		if (!strchr (p, '.')) {
			// no extension, so try the default (.cfg)
			strcat (name, ".cfg");
			f = (char *) FS_LoadHunkFile (name);
		}
		if (!f) {
			Com_Printf ("couldn't exec %s\n", Cmd_Argv(1));
			return;
		}
	}
	if (cl_warncmd.value || developer.value)
		Com_Printf ("execing %s\n", name);

#ifndef SERVERONLY
	if (cbuf_current == &cbuf_svc) {
		Cbuf_AddText (f);
		Cbuf_AddText ("\n");
	} else
#endif
	{
		Cbuf_InsertText ("\n");
		Cbuf_InsertText (f);
	}
	Hunk_FreeToLowMark (mark);
}

//Just prints the rest of the line to the console
/*void Cmd_Echo_f (void) {
	int i;
 
	for (i = 1; i < Cmd_Argc(); i++)
		Com_Printf ("%s ", Cmd_Argv(i));
	Com_Printf ("\n");
}*/
void Cmd_Echo_f (void)
{
#ifdef SERVERONLY
	Com_Printf ("%s\n",Cmd_Args());
#else
	int	i;
	char	*str;
	char	args[MAX_MACRO_STRING];
	char	buf[MAX_MACRO_STRING];


	args[0]='\0';

	str = Q_strcat(args, Cmd_Argv(1));
	for (i=2 ; i<Cmd_Argc() ; i++) {
		str = Q_strcat(str, " ");
		str = Q_strcat(str, Cmd_Argv(i));
	}

	//	str = TP_ParseMacroString(args);

	str = TP_ParseMacroString(args);
	str = TP_ParseFunChars(str, false);

	strcpy(buf,str);
	CL_SearchForReTriggers (buf, RE_PRINT_ECHO); 	// BorisU
	Print_flags[Print_current] |= PR_TR_SKIP;
	Com_Printf ("%s\n", buf);
#endif
}

/*
=============================================================================
								ALIASES
=============================================================================
*/

cmd_alias_t *cmd_alias_hash[32];
cmd_alias_t	*cmd_alias;

cmd_alias_t *Cmd_FindAlias (char *name)
{
	int key;
	cmd_alias_t *alias;

	key = Com_HashKey (name);
	for (alias = cmd_alias_hash[key]; alias; alias = alias->hash_next) {
		if (!strcasecmp(name, alias->name))
			return alias;
	}
	return NULL;
}

char *Cmd_AliasString (char *name)
{
	int key;
	cmd_alias_t *alias;

	key = Com_HashKey (name);
	for (alias = cmd_alias_hash[key]; alias; alias = alias->hash_next) {
		if (!strcasecmp(name, alias->name))
#ifdef EMBED_TCL
			if (!(alias->flags & ALIAS_TCL))
#endif
				return alias->value;
	}
	return NULL;
}

void Cmd_Viewalias_f (void)
{
	cmd_alias_t	*alias;
	char		*name;
	int		i,m;

	if (Cmd_Argc() < 2) {
		Com_Printf ("viewalias <cvar> [<cvar2>..] : view body of alias\n");
		return;
	}

	for (i=1; i<Cmd_Argc(); i++) {
		name = Cmd_Argv(i);


		if ( IsRegexp(name) ) {
			if (!ReSearchInit(name))
				return;
			Com_Printf ("Current alias commands:\n");

			for (alias = cmd_alias, i=m=0; alias ; alias=alias->next, i++)
				if (ReSearchMatch(alias->name)) {
#ifdef EMBED_TCL
					if (alias->flags & ALIAS_TCL)
						Com_Printf ("%s : Tcl procedure\n", alias->name);
					else
#endif
						Com_Printf ("%s : %s\n", alias->name, alias->value);
					m++;
				}

			Com_Printf ("------------\n%i/%i aliases\n", m, i);
			ReSearchDone();


		} else 	{
			if ((alias = Cmd_FindAlias(name)))
#ifdef EMBED_TCL
				if (alias->flags & ALIAS_TCL)
					Com_Printf ("%s : Tcl procedure\n", name);
				else
#endif
					Com_Printf ("%s : \"%s\"\n", Cmd_Argv(i), alias->value);
			else
				Com_Printf ("No such alias: %s\n", Cmd_Argv(i));
		}
	}
}


int Cmd_AliasCompare (const void *p1, const void *p2)
{
	cmd_alias_t *a1, *a2;

	a1 = *((cmd_alias_t **) p1);
	a2 = *((cmd_alias_t **) p2);

	if (a1->name[0] == '+') {
		if (a2->name[0] == '+')
			return strcasecmp(a1->name + 1, a2->name + 1);
		else
			return -1;
	} else if (a1->name[0] == '-') {
		if (a2->name[0] == '+')
			return 1;
		else if (a2->name[0] == '-')
			return strcasecmp(a1->name + 1, a2->name + 1);
		else
			return -1;
	} else if (a2->name[0] == '+' || a2->name[0] == '-') {
		return 1;
	} else {
		return strcasecmp(a1->name, a2->name);
	}
}

void Cmd_AliasList_f (void)
{
	cmd_alias_t *a;
	int i, c, m = 0;
	static int count;
	static qbool sorted = false;
	static cmd_alias_t *sorted_aliases[2048];

#define MAX_SORTED_ALIASES (sizeof(sorted_aliases) / sizeof(sorted_aliases[0]))

	if (!sorted) {
		for (a = cmd_alias, count = 0; a && count < MAX_SORTED_ALIASES; a = a->next, count++)
			sorted_aliases[count] = a;
		qsort(sorted_aliases, count, sizeof (cmd_alias_t *), Cmd_AliasCompare);
		sorted = true;
	}

	if (count == MAX_SORTED_ALIASES)
		assert(!"count == MAX_SORTED_ALIASES");

	c = Cmd_Argc();
	if (c>1)
		if (!ReSearchInit(Cmd_Argv(1)))
			return;

	Com_Printf ("List of aliases:\n");
	for (i = 0; i < count; i++) {
		a = sorted_aliases[i];
		if (c==1 || ReSearchMatch(a->name)) {
			Com_Printf ("\x02%s :", sorted_aliases[i]->name);
			Com_Printf (" %s\n\n", sorted_aliases[i]->value);
			m++;
		}
	}

	if (c>1)
		ReSearchDone();
	Com_Printf ("------------\n%i/%i aliases\n", m, count);
}





void Cmd_EditAlias_f (void)
{
#define		MAXCMDLINE	256
	extern char	key_lines[32][MAXCMDLINE];
	extern int		edit_line;
	cmd_alias_t	*a;
	char *s, final_string[MAXCMDLINE -1];
	int c;
	extern void Key_ClearTyping();

	c = Cmd_Argc();
	if (c == 1)	{
		Com_Printf ("%s <name> : modify an alias\n", Cmd_Argv(0));
		Com_Printf ("aliaslist : list all aliases\n");
		return;
	}

	a = Cmd_FindAlias(Cmd_Argv(1));
	if ( a == NULL ) {
		s = CopyString("");
	} else {
		s = CopyString(a->value);
	}

	snprintf(final_string, sizeof(final_string), "/alias \"%s\" \"%s\"", Cmd_Argv(1), s);
	Key_ClearTyping();
	memcpy (key_lines[edit_line]+1, final_string, strlen(final_string));
	Q_free(s);
}



//Creates a new command that executes a command string (possibly ; separated)
void Cmd_Alias_f (void)
{
	cmd_alias_t	*a;
	char *s;
	int c, key;

	c = Cmd_Argc();
	if (c == 1)	{
		Com_Printf ("%s <name> <command> : create or modify an alias\n", Cmd_Argv(0));
		Com_Printf ("aliaslist : list all aliases\n");
		return;
	}

	s = Cmd_Argv(1);
	if (strlen(s) >= MAX_ALIAS_NAME) {
		Com_Printf ("Alias name is too long\n");
		return;
	}

	key = Com_HashKey(s);

	// if the alias already exists, reuse it
	for (a = cmd_alias_hash[key]; a; a = a->hash_next) {
		if (!strcasecmp(a->name, s)) {
			Q_free (a->value);
			break;
		}
	}

	if (!a)	{
		a = (cmd_alias_t *) Q_malloc (sizeof(cmd_alias_t));
		a->next = cmd_alias;
		cmd_alias = a;
		a->hash_next = cmd_alias_hash[key];
		cmd_alias_hash[key] = a;
	}
	strcpy (a->name, s);

	a->flags = 0;
	// QW262 -->
	s=Cmd_MakeArgs(2);
	while (*s) {
		if (*s == '%' && ( s[1]>='0' || s[1]<='9')) {
			a->flags |= ALIAS_HAS_PARAMETERS;
			break;
		}
		++s;
	}
	// <-- QW262
	if (!strcasecmp(Cmd_Argv(0), "aliasa"))
		a->flags |= ALIAS_ARCHIVE;

#ifndef SERVERONLY
	if (cbuf_current == &cbuf_svc)
		a->flags |= ALIAS_SERVER;
	if (!strcasecmp(Cmd_Argv(0), "tempalias"))
		a->flags |= ALIAS_TEMP;
#endif

	// copy the rest of the command line
	a->value = CopyString (Cmd_MakeArgs(2));
}

qbool Cmd_DeleteAlias (char *name)
{
	cmd_alias_t	*a, *prev;
	int key;

	key = Com_HashKey (name);

	prev = NULL;
	for (a = cmd_alias_hash[key]; a; a = a->hash_next) {
		if (!strcasecmp(a->name, name)) {
			// unlink from hash
			if (prev)
				prev->hash_next = a->hash_next;
			else
				cmd_alias_hash[key] = a->hash_next;
			break;
		}
		prev = a;
	}

	if (!a)
		return false;	// not found

	prev = NULL;
	for (a = cmd_alias; a; a = a->next) {
		if (!strcasecmp(a->name, name)) {
			// unlink from alias list
			if (prev)
				prev->next = a->next;
			else
				cmd_alias = a->next;

			// free
			Q_free (a->value);
			Q_free (a);
			return true;
		}
		prev = a;
	}

	assert(!"Cmd_DeleteAlias: alias list broken");
	return false;	// shut up compiler
}

void Cmd_UnAlias (qbool use_regex)
{
	int 		i;
	char		*name;
	cmd_alias_t	*a;
	qbool		re_search = false;

	if (Cmd_Argc() < 2) {
		Com_Printf ("unalias <cvar> [<cvar2>..]: erase an existing alias\n");
		return;
	}

	for (i=1; i<Cmd_Argc(); i++) {
		name = Cmd_Argv(i);

		if (use_regex && (re_search = IsRegexp(name)))
			if(!ReSearchInit(name))
				continue;

		if (strlen(name) >= MAX_ALIAS_NAME) {
			Com_Printf ("Alias name is too long: \"%s\"\n", Cmd_Argv(i));
			continue;
		}

		if (use_regex && re_search) {
			for (a = cmd_alias ; a ; a=a->next) {
				if (ReSearchMatch(a->name))
					Cmd_DeleteAlias(a->name);
			}
		} else {
			if (!Cmd_DeleteAlias(Cmd_Argv(i)))
				Com_Printf ("unalias: unknown alias \"%s\"\n", Cmd_Argv(i));
		}

		if (use_regex && re_search)
			ReSearchDone();

	}
}

void Cmd_UnAlias_f (void)
{
	Cmd_UnAlias(false);
}

void Cmd_UnAlias_re_f (void)
{
	Cmd_UnAlias(true);
}

// remove all aliases
void Cmd_UnAliasAll_f (void)
{
	cmd_alias_t	*a, *next;
	int i;

	for (a = cmd_alias; a ; a = next) {
		next = a->next;
		Q_free (a->value);
		Q_free (a);
	}
	cmd_alias = NULL;

	// clear hash
	for (i = 0; i < 32; i++)
		cmd_alias_hash[i] = NULL;
}




void DeleteServerAliases(void)
{
	extern cmd_alias_t *cmd_alias;
	cmd_alias_t	*a;

	for (a = cmd_alias; a; a = a->next) {
		if (a->flags & ALIAS_SERVER)
			Cmd_DeleteAlias(a->name);
	}
}



void Cmd_WriteAliases (FILE *f)
{
	cmd_alias_t	*a;

	for (a = cmd_alias ; a ; a=a->next)
		if (a->flags & ALIAS_ARCHIVE)
			fprintf (f, "aliasa %s \"%s\"\n", a->name, a->value);
}

/*
=============================================================================
					LEGACY COMMANDS
=============================================================================
*/

typedef struct legacycmd_s
{
	char *oldname, *newname;
	struct legacycmd_s *next;
}
legacycmd_t;

static legacycmd_t *legacycmds = NULL;

void Cmd_AddLegacyCommand (char *oldname, char *newname)
{
	legacycmd_t *cmd;
	cmd = (legacycmd_t *) Q_malloc (sizeof(legacycmd_t));
	cmd->next = legacycmds;
	legacycmds = cmd;

	cmd->oldname = CopyString(oldname);
	cmd->newname = CopyString(newname);
}

qbool Cmd_IsLegacyCommand (char *oldname)
{
	legacycmd_t *cmd;

	for (cmd = legacycmds; cmd; cmd = cmd->next) {
		if (!strcasecmp(cmd->oldname, oldname))
			return true;
	}
	return false;
}

static qbool Cmd_LegacyCommand (void)
{
	qbool recursive = false;
	legacycmd_t *cmd;
	char text[1024];

	for (cmd = legacycmds; cmd; cmd = cmd->next) {
		if (!strcasecmp(cmd->oldname, Cmd_Argv(0)))
			break;
	}
	if (!cmd)
		return false;

	if (!cmd->newname[0])
		return true;		// just ignore this command

	// build new command string
	strlcpy (text, cmd->newname, sizeof(text));
	strlcat (text, " ", sizeof(text));
	strlcat (text, Cmd_Args(), sizeof(text));

	assert (!recursive);
	recursive = true;
	Cmd_ExecuteString (text);
	recursive = false;

	return true;
}

/*
=============================================================================
					COMMAND EXECUTION
=============================================================================
*/

#define	MAX_ARGS		80

static	int		cmd_argc;
static	char	*cmd_argv[MAX_ARGS];
static	char	*cmd_null_string = "";
static	char	*cmd_args = NULL;

cmd_function_t	*cmd_hash_array[32];
/*static*/ cmd_function_t	*cmd_functions;		// possible commands to execute

int Cmd_Argc (void)
{
	return cmd_argc;
}

char *Cmd_Argv (int arg)
{
	if (arg >= cmd_argc)
		return cmd_null_string;
	return cmd_argv[arg];
}

//Returns a single string containing argv(1) to argv(argc() - 1)
char *Cmd_Args (void)
{
	if (!cmd_args)
		return "";
	return cmd_args;
}

//Returns a single string containing argv(start) to argv(argc() - 1)
//Unlike Cmd_Args, shrinks spaces between argvs
char *Cmd_MakeArgs (int start)
{
	int i, c;

	static char	text[1024];

	text[0] = 0;
	c = Cmd_Argc();
	for (i = start; i < c; i++) {
		if (i > start)
			strncat (text, " ", sizeof(text) - strlen(text) - 1);
		strncat (text, Cmd_Argv(i), sizeof(text) - strlen(text) - 1);
	}

	return text;
}

//Parses the given string into command line tokens.
void Cmd_TokenizeString (char *text)
{
	int idx;
	static char argv_buf[1024];

	idx = 0;

	cmd_argc = 0;
	cmd_args = NULL;

	while (1) {
		// skip whitespace
		while (*text == ' ' || *text == '\t' || *text == '\r')
			text++;

		if (*text == '\n') {	// a newline separates commands in the buffer
			text++;
			break;
		}

		if (!*text)
			return;

		if (cmd_argc == 1)
			cmd_args = text;

		text = COM_Parse (text);
		if (!text)
			return;

		if (cmd_argc < MAX_ARGS) {
			cmd_argv[cmd_argc] = argv_buf + idx;
			strcpy (cmd_argv[cmd_argc], com_token);
			idx += strlen(com_token) + 1;
			cmd_argc++;
		}
	}
}

void Cmd_AddCommand (char *cmd_name, xcommand_t function)
{
	cmd_function_t *cmd;
	int	key;

	if (host_initialized)	// because hunk allocation would get stomped
		assert (!"Cmd_AddCommand after host_initialized");

	// fail if the command is a variable name
	if (Cvar_FindVar(cmd_name)) {
		Com_Printf ("Cmd_AddCommand: %s already defined as a var\n", cmd_name);
		return;
	}

	key = Com_HashKey (cmd_name);

	// fail if the command already exists
	for (cmd = cmd_hash_array[key]; cmd; cmd=cmd->hash_next) {
		if (!strcasecmp (cmd_name, cmd->name)) {
			Com_Printf ("Cmd_AddCommand: %s already defined\n", cmd_name);
			return;
		}
	}

	cmd = (cmd_function_t *) Hunk_Alloc (sizeof(cmd_function_t));
	cmd->name = cmd_name;
	cmd->function = function;
	cmd->next = cmd_functions;
	cmd_functions = cmd;
	cmd->hash_next = cmd_hash_array[key];
	cmd_hash_array[key] = cmd;
}

qbool Cmd_Exists (char *cmd_name)
{
	int	key;
	cmd_function_t	*cmd;

	key = Com_HashKey (cmd_name);
	for (cmd=cmd_hash_array[key]; cmd; cmd = cmd->hash_next) {
		if (!strcasecmp (cmd_name, cmd->name))
			return true;
	}
	return false;
}

cmd_function_t *Cmd_FindCommand (const char *cmd_name)
{
	int	key;
	cmd_function_t *cmd;

	key = Com_HashKey (cmd_name);
	for (cmd = cmd_hash_array[key]; cmd; cmd = cmd->hash_next) {
		if (!strcasecmp (cmd_name, cmd->name))
			return cmd;
	}
	return NULL;
}

char *Cmd_CompleteCommand (char *partial)
{
	cmd_function_t *cmd;
	int len;
	cmd_alias_t *alias;

	len = strlen(partial);

	if (!len)
		return NULL;

	// check for exact match
	for (cmd = cmd_functions; cmd; cmd = cmd->next)
		if (!strcasecmp (partial, cmd->name))
			return cmd->name;
	for (alias = cmd_alias; alias; alias = alias->next)
		if (!strcasecmp (partial, alias->name))
			return alias->name;

	// check for partial match
	for (cmd = cmd_functions; cmd; cmd = cmd->next)
		if (!strncasecmp (partial, cmd->name, len))
			return cmd->name;
	for (alias = cmd_alias; alias; alias = alias->next)
		if (!strncasecmp (partial, alias->name, len))
			return alias->name;

	return NULL;
}

int Cmd_CompleteCountPossible (char *partial)
{
	cmd_function_t *cmd;
	int len, c = 0;

	len = strlen(partial);
	if (!len)
		return 0;

	for (cmd = cmd_functions; cmd; cmd = cmd->next)
		if (!strncasecmp (partial, cmd->name, len))
			c++;

	return c;
}

int Cmd_AliasCompleteCountPossible (char *partial)
{
	cmd_alias_t *alias;
	int len, c = 0;

	len = strlen(partial);
	if (!len)
		return 0;

	for (alias = cmd_alias; alias; alias = alias->next)
		if (!strncasecmp (partial, alias->name, len))
			c++;

	return c;
}

int Cmd_CommandCompare (const void *p1, const void *p2)
{
	return strcmp((*((cmd_function_t **) p1))->name, (*((cmd_function_t **) p2))->name);
}

void Cmd_CmdList_f (void)
{
	cmd_function_t *cmd;
	int i, c, m = 0;
	static int count;
	static qbool sorted = false;
	static cmd_function_t *sorted_cmds[512];

#define MAX_SORTED_CMDS (sizeof(sorted_cmds) / sizeof(sorted_cmds[0]))

	if (!sorted) {
		for (cmd = cmd_functions, count = 0; cmd && count < MAX_SORTED_CMDS; cmd = cmd->next, count++)
			sorted_cmds[count] = cmd;
		qsort(sorted_cmds, count, sizeof (cmd_function_t *), Cmd_CommandCompare);
		sorted = true;
	}

	if (count == MAX_SORTED_CMDS)
		assert(!"count == MAX_SORTED_CMDS");

	c = Cmd_Argc();
	if (c>1)
		if (!ReSearchInit(Cmd_Argv(1)))
			return;

	Com_Printf ("List of commands:\n");
	for (i = 0; i < count; i++) {
		cmd = sorted_cmds[i];
		if (c==1 || ReSearchMatch(cmd->name)) {
			Com_Printf ("%s\n", cmd->name);
			m++;
		}
	}

	if (c>1)
		ReSearchDone();
	Com_Printf ("------------\n%i/%i commands\n", m,count);
}



#define MAX_MACROS 64

typedef struct
{
	char name[32];
	char *(*func) (void);
	qbool teamplay;
}
macro_command_t;

static macro_command_t macro_commands[MAX_MACROS];
static int macro_count = 0;

void Cmd_AddMacroEx(char *s, char *(*f)(void), qbool teamplay)
{
	if (macro_count == MAX_MACROS)
		Sys_Error("Cmd_AddMacro: macro_count == MAX_MACROS");
	strlcpy(macro_commands[macro_count].name, s, sizeof(macro_commands[macro_count].name));
	macro_commands[macro_count].func = f;
	macro_commands[macro_count].teamplay = teamplay;
	macro_count++;
}

void Cmd_AddMacro(char *s, char *(*f)(void))
{
	Cmd_AddMacroEx(s, f, false);
}

char *Cmd_MacroString (char *s, int *macro_length)
{
	int i;
	macro_command_t	*macro;

	for (i = 0; i < macro_count; i++) {
		macro = &macro_commands[i];
		if (!strncasecmp(s, macro->name, strlen(macro->name))) {
#ifndef SERVERONLY
			if (cbuf_current == &cbuf_main && macro->teamplay)
				cbuf_current = &cbuf_formatted_comms;
#endif
			*macro_length = strlen(macro->name);
			return macro->func();
		}
		macro++;
	}
	*macro_length = 0;
	return NULL;
}

int Cmd_MacroCompare (const void *p1, const void *p2)
{
	return strcmp((*((macro_command_t **) p1))->name, (*((macro_command_t **) p2))->name);
}

void Cmd_MacroList_f (void)
{
	int i, c, m = 0;
	static qbool sorted = false;
	static macro_command_t *sorted_macros[MAX_MACROS];

	if (!macro_count) {
		Com_Printf("No macros!");
		return;
	}

	if (!sorted) {
		for (i = 0; i < macro_count; i++)
			sorted_macros[i] = &macro_commands[i];
		qsort(sorted_macros, macro_count, sizeof (macro_command_t *), Cmd_MacroCompare);
		sorted = true;
	}

	c = Cmd_Argc();
	if (c>1)
		if (!ReSearchInit(Cmd_Argv(1)))
			return;

	Com_Printf ("List of macros:\n");
	for (i = 0; i < macro_count; i++) {
		if (c==1 || ReSearchMatch(sorted_macros[i]->name)) {
			Com_Printf ("$%s\n", sorted_macros[i]->name);
			m++;
		}
	}

	if (c>1)
		ReSearchDone();
	Com_Printf ("------------\n%i/%i macros\n", m, macro_count);
}



//Expands all $cvar expressions to cvar values
//If not SERVERONLY, also expands $macro expressions
//Note: dest must point to a 1024 byte buffer
void Cmd_ExpandString (char *data, char *dest)
{
	unsigned int c;
	char buf[255], *str;
	int i, len, quotes = 0, name_length = 0;
	cvar_t	*var, *bestvar;
#ifndef SERVERONLY
	int macro_length;
#endif

	len = 0;

	while ((c = *data)) {
		if (c == '"')
			quotes++;

		if (c == '$' && !(quotes&1)) {
			data++;

			// Copy the text after '$' to a temp buffer
			i = 0;
			buf[0] = 0;
			bestvar = NULL;
			while ((c = *data) > 32) {
				if (c == '$')
					break;
				data++;
				buf[i++] = c;
				buf[i] = 0;
				if ((var = Cvar_FindVar(buf))) {
					bestvar = var;
				}
			}

#ifndef SERVERONLY
			if (!dedicated) {
				str = Cmd_MacroString (buf, &macro_length);
				name_length = macro_length;

				if (bestvar && (!str || (strlen(bestvar->name) > macro_length))) {
					str = bestvar->string;
					name_length = strlen(bestvar->name);
				}
			} else
#endif
			{
				if (bestvar) {
					str = bestvar->string;
					name_length = strlen(bestvar->name);
				} else {
					str = NULL;
				}
			}

			if (str) {
				// check buffer size
				if (len + strlen(str) >= 1024 - 1)
					break;

				strcpy(&dest[len], str);
				len += strlen(str);
				i = name_length;
				while (buf[i])
					dest[len++] = buf[i++];
			} else {
				// no matching cvar or macro
				dest[len++] = '$';
				if (len + strlen(buf) >= 1024 - 1)
					break;
				strcpy (&dest[len], buf);
				len += strlen(buf);
			}
		} else {
			dest[len] = c;
			data++;
			len++;
			if (len >= 1024 - 1)
				break;
		}
	};

	dest[len] = 0;
}

int Commands_Compare_Func(const void * arg1, const void * arg2)
{
	return strcasecmp( * ( char** ) arg1, * ( char** ) arg2 );
}
char *msgtrigger_commands[] = {
                                  "play", "playvol", "stopsound", "set", "echo", "say", "say_team",
                                  "alias", "unalias", "msg_trigger", "inc", "bind", "unbind", "record",
                                  "easyrecord", "stop", "if", "wait", "log", "match_forcestart",
                                  "dns", "addserver", "connect", "join", "observe",
                                  "tcl_proc", "tcl_exec", "tcl_eval", "exec",
                                  "set_ex", "set_alias_str", "set_bind_str","unset", "unset_re" ,
                                  "toggle", "toggle_re", "set_calc", "rcon", "user", "users",
                                  "re_trigger", "re_trigger_options", "re_trigger_delete",
                                  "re_trigger_enable","re_trigger_disable", "re_trigger_match",
                                  "hud262_add","hud262_remove","hud262_position","hud262_bg",
                                  "hud262_move","hud262_width","hud262_alpha","hud262_blink",
                                  "hud262_disable","hud262_enable","hud262_list","hud262_bringtofront",
                                  "hud_262font","hud262_hover","hud262_button"
                                  //               ,NULL
                              };

char *formatted_comms_commands[] = {
                                       "if", "wait", "echo", "say", "say_team",
                                       "tp_point", "tp_pickup", "tp_took",
                                       NULL
                                   };

float	impulse_time = -9999;
int		impulse_counter;

qbool AllowedImpulse(int imp)
{

	static int Allowed_TF_Impulses[] = {
	                                   135, 99, 101, 102, 103, 104, 105, 106, 107, 108, 109, 23, 144, 145,
	                                   159, 160, 161, 162, 163, 164, 165, 166, 167
	                               };

	int i;

	if (!cl.teamfortress) return false;
	for (i=0; i<sizeof(Allowed_TF_Impulses)/sizeof(Allowed_TF_Impulses[0]); i++) {
		if (Allowed_TF_Impulses[i] == imp) {
			if(++impulse_counter >= 30) {
				if (cls.realtime < impulse_time + 5 && !cls.demoplayback) {
					return false;
				}
				impulse_time = cls.realtime;
				impulse_counter = 0;
			}
			return true;
		}
	}
	return false;
}

qbool Cmd_IsCommandAllowedInMessageTrigger( const char *command )
{
	if( !strcasecmp( command, "impulse") )
		return AllowedImpulse(Q_atoi(Cmd_Argv(1)));

	return 	  bsearch( &(command), msgtrigger_commands,
	                   sizeof(msgtrigger_commands)/sizeof(msgtrigger_commands[0]),
	                   sizeof(msgtrigger_commands[0]),Commands_Compare_Func) != NULL;
}
qbool Cmd_IsCommandAllowedInTeamPlayMacros( const char *command )
{
	char **s;
	for (s = formatted_comms_commands; *s; s++) {
		if (!strcasecmp(command, *s))
			break;
	}
	return *s != NULL;
}
//A complete command line has been parsed, so try to execute it
static void Cmd_ExecuteStringEx (cbuf_t *context, char *text)
{
	cvar_t *v;
	cmd_function_t *cmd;
	cmd_alias_t *a;
	static char buf[1024];
	cbuf_t *inserttarget, *oldcontext;
	char *p, *n, *s;
	char text_exp[1024];

	oldcontext = cbuf_current;
	cbuf_current = context;

#ifndef SERVERONLY
	Cmd_ExpandString (text, text_exp);
	Cmd_TokenizeString (text_exp);
#else
	Cmd_TokenizeString (text);
#endif

	if (!Cmd_Argc())
		goto done;		// no tokens

#ifndef SERVERONLY
	if (cbuf_current == &cbuf_svc) {
		if (CL_CheckServerCommand())
			goto done;
	}
#endif

	// check functions
	if ((cmd = Cmd_FindCommand(cmd_argv[0]))) {
#ifndef SERVERONLY
		if (cbuf_current == &cbuf_safe) {
			if( !Cmd_IsCommandAllowedInMessageTrigger(cmd_argv[0])) {
				Com_Printf ("\"%s\" cannot be used in message triggers\n", cmd_argv[0]);
				goto done;
			}
		} else if (cbuf_current == &cbuf_formatted_comms) {
			if( !Cmd_IsCommandAllowedInTeamPlayMacros(cmd_argv[0]) ) {
				Com_Printf("\"%s\" cannot be used in combination with teamplay $macros\n", cmd_argv[0]);
				goto done;
			}
		}
		/*
		                char **s;
				if (cbuf_current == &cbuf_safe) {
					for (s = msgtrigger_commands; *s; s++) {
						if (!strcasecmp(cmd_argv[0], *s))
							break;
					}
					if (!*s)
					{
						Com_Printf ("\"%s\" cannot be used in message triggers\n", cmd_argv[0]);
						goto done;
					}
				} else if (cbuf_current == &cbuf_formatted_comms) {
					for (s = formatted_comms_commands; *s; s++) {
						if (!strcasecmp(cmd_argv[0], *s))
							break;
					}
					if (!*s) {
						Com_Printf("\"%s\" cannot be used in combination with teamplay $macros\n", cmd_argv[0]);
						goto done;
					}
				}*/

#endif

		if (cmd->function)
			cmd->function();
		else
			Cmd_ForwardToServer ();
		goto done;
	}

	// some bright guy decided to use "skill" as a mod command in Custom TF, sigh
	if (!strcmp(Cmd_Argv(0), "skill") && cmd_argc == 1 && Cmd_FindAlias("skill"))
		goto checkaliases;

	// check cvars
	if ((v = Cvar_FindVar (Cmd_Argv(0)))) {
#ifndef SERVERONLY
		if (cbuf_current == &cbuf_formatted_comms) {
			Com_Printf("\"%s\" cannot be used in combination with teamplay $macros\n", cmd_argv[0]);
			goto done;
		}
#endif
		if (Cvar_Command())
			goto done;
	}

	// check aliases
checkaliases:
	if ((a = Cmd_FindAlias(cmd_argv[0]))) {

		// QW262 -->
#ifdef EMBED_TCL
		if (a->flags & ALIAS_TCL)
		{
			TCL_ExecuteAlias (a);
			return;
		}
#endif

		if (a->value[0]=='\0') goto done; // alias is empty.

		if(a->flags & ALIAS_HAS_PARAMETERS) { // %parameters are given in alias definition
			s=a->value;
			buf[0] = '\0';
			do {
				n = strchr(s, '%');
				if(n) {
					if(*++n >= '1' && *n <= '9') {
						n[-1] = 0;
						strlcat(buf, s, sizeof(buf));
						n[-1] = '%';
						// insert numbered parameter
						strlcat(buf,Cmd_Argv(*n-'0'), sizeof(buf));
					} else if (*n == '0') {
						n[-1] = 0;
						strlcat(buf, s, sizeof(buf));
						n[-1] = '%';
						// insert all parameters
						strlcat(buf, Cmd_Args(), sizeof(buf));
					} else if (*n == '%') {
						n[0] = 0;
						strlcat(buf, s, sizeof(buf));
						n[0] = '%';
					} else {
						if (*n) {
							char tmp = n[1];
							n[1] = 0;
							strlcat(buf, s, sizeof(buf));
							n[1] = tmp;
						} else
							strlcat(buf, s, sizeof(buf));
					}
					s=n+1;
				}
			} while(n);
			strlcat(buf, s, sizeof(buf));
			p = buf;

		} else  // alias has no parameters
			p = a->value;
		// <-- QW262

#ifndef SERVERONLY
		if (cbuf_current == &cbuf_svc)
		{
			Cbuf_AddText (p);
			Cbuf_AddText ("\n");
		} else
#endif
		{

#ifdef SERVERONLY
			inserttarget = &cbuf_main;
#else
			inserttarget = cbuf_current ? cbuf_current : &cbuf_main;
#endif

			Cbuf_InsertTextEx (inserttarget, "\n");

			// if the alias value is a command or cvar and
			// the alias is called with parameters, add them
			if (Cmd_Argc() > 1 && !strchr(p, ' ') && !strchr(p, '\t') &&
			        (Cvar_FindVar(p) || (Cmd_FindCommand(p) && p[0] != '+' && p[0] != '-'))
			   ) {
				Cbuf_InsertTextEx (inserttarget, Cmd_Args());
				Cbuf_InsertTextEx (inserttarget, " ");
			}
			Cbuf_InsertTextEx (inserttarget, p);
		}
		goto done;
	}

#ifndef SERVERONLY
	if (Cmd_LegacyCommand())
		goto done;
#endif

#ifndef SERVERONLY
	if (cbuf_current != &cbuf_svc)
#endif
	{
		if (cl_warncmd.value || developer.value)
			Com_Printf ("Unknown command \"%s\"\n", Cmd_Argv(0));
	}

done:
	cbuf_current = oldcontext;
}

void Cmd_ExecuteString (char *text)
{
	Cmd_ExecuteStringEx (NULL, text);
}

static qbool is_numeric (char *c)
{
	return ( isdigit((int)(unsigned char)*c) ||
	         ((*c == '-' || *c == '+') && (c[1] == '.' || isdigit((int)(unsigned char)c[1]))) ||
	         (*c == '.' && isdigit((int)(unsigned char)c[1])) );
}

void Re_Trigger_Copy_Subpatterns (char *s, int* offsets, int num, cvar_t *re_sub); // QW262
extern cvar_t re_sub[10]; // QW262

void Cmd_If_f (void)
{
	int	i, c;
	char *op, buf[1024] = {0};
	qbool result;

	if ((c = Cmd_Argc()) < 5) {
		Com_Printf ("Usage: if <expr1> <op> <expr2> <command> [else <command>]\n");
		return;
	}

	op = Cmd_Argv(2);
	if (!strcmp(op, "==") || !strcmp(op, "=") || !strcmp(op, "!=") || !strcmp(op, "<>")) {
		if (is_numeric(Cmd_Argv(1)) && is_numeric(Cmd_Argv(3)))
			result = Q_atof(Cmd_Argv(1)) == Q_atof(Cmd_Argv(3));
		else
			result = !strcmp(Cmd_Argv(1), Cmd_Argv(3));

		if (op[0] != '=')
			result = !result;
	} else if (!strcmp(op, ">")) {
		result = Q_atof(Cmd_Argv(1)) > Q_atof(Cmd_Argv(3));
	} else if (!strcmp(op, "<")) {
		result = Q_atof(Cmd_Argv(1)) < Q_atof(Cmd_Argv(3));
	} else if (!strcmp(op, ">=")) {
		result = Q_atof(Cmd_Argv(1)) >= Q_atof(Cmd_Argv(3));
	} else if (!strcmp(op, "<=")) {
		result = Q_atof(Cmd_Argv(1)) <= Q_atof(Cmd_Argv(3));

	} else if (!strcmp(op, "isin")) {
		result = (strstr(Cmd_Argv(3), Cmd_Argv(1)) ? 1 : 0);
	} else if (!strcmp(op, "!isin")) {
		result = (strstr(Cmd_Argv(3), Cmd_Argv(1)) ? 0 : 1);

	} else if (!strcmp(op, "=~") || !strcmp(op, "!~")) {
		pcre*		regexp;
		const char	*error;
		int		error_offset;
		int		rc;
		int		offsets[99];

		regexp = pcre_compile (Cmd_Argv(3), 0, &error, &error_offset, NULL);
		if (!regexp) {
			Com_Printf ("Error in regexp: %s\n", error);
			return;
		}
		rc = pcre_exec (regexp, NULL, Cmd_Argv(1), strlen(Cmd_Argv(1)),
		                0, 0, offsets, 99);
		if (rc >= 0) {
			Re_Trigger_Copy_Subpatterns (Cmd_Argv(1), offsets, min(rc, 10), re_sub);
			result = true;
		} else
			result = false;

		if (op[0] != '=')
			result = !result;

		pcre_free (regexp);
	} else {
		Com_Printf ("unknown operator: %s\n", op);
		Com_Printf ("valid operators are ==, =, !=, <>, >, <, >=, <=, isin, !isin, =~, !~\n");
		return;
	}

	if (result)	{
		for (i = 4; i < c; i++) {
			if ((i == 4) && !strcasecmp(Cmd_Argv(i), "then"))
				continue;
			if (!strcasecmp(Cmd_Argv(i), "else"))
				break;
			if (buf[0])
				strncat (buf, " ", sizeof(buf) - strlen(buf) - 2);
			strncat (buf, Cmd_Argv(i), sizeof(buf) - strlen(buf) - 2);
		}
	} else {
		for (i = 4; i < c ; i++) {
			if (!strcasecmp(Cmd_Argv(i), "else"))
				break;
		}
		if (i == c)
			return;
		for (i++; i < c; i++) {
			if (buf[0])
				strncat (buf, " ", sizeof(buf) - strlen(buf) - 2);
			strncat (buf, Cmd_Argv(i), sizeof(buf) - strlen(buf) - 2);
		}
	}

	strncat (buf, "\n", sizeof(buf) - strlen(buf) - 1);
	Cbuf_InsertTextEx (cbuf_current ? cbuf_current : &cbuf_main, buf);
}

void Cmd_If_Exists_f(void)
{
	int	argc;
	char	*type;
	char	*name;
	qbool	exists;
	qbool	iscvar, isalias, istrigger, ishud;

	argc = Cmd_Argc();
	if ( argc < 4 || argc > 5) {
		Com_Printf ("if_exists <type> <name> <cmd1> [<cmd2>] - conditional execution\n");
		return;
	}

	type = Cmd_Argv(1);
	name = Cmd_Argv(2);
	if ( ( (iscvar = !strcmp(type, "cvar")) && Cvar_FindVar (name) )			||
	        ( (isalias = !strcmp(type, "alias")) && Cmd_FindAlias (name) )			||
	        ( (istrigger = !strcmp(type, "trigger")) && CL_FindReTrigger (name) )	||
	        ( (ishud = !strcmp(type, "hud")) && Hud_FindElement (name) ) )
		exists = true;
	else {
		exists = false;
		if (!(iscvar || isalias || istrigger || ishud)) {
			Com_Printf("if_exists: <type> can be cvar, alias, trigger, hud\n");
			return;
		}
	}

	if (exists) {
		Cbuf_InsertTextEx (cbuf_current ? cbuf_current : &cbuf_main,"\n");
		Cbuf_InsertTextEx (cbuf_current ? cbuf_current : &cbuf_main,Cmd_Argv(3));
	} else if (argc == 5) {
		Cbuf_InsertTextEx (cbuf_current ? cbuf_current : &cbuf_main,"\n");
		Cbuf_InsertTextEx (cbuf_current ? cbuf_current : &cbuf_main,Cmd_Argv(4));
	} else
		return;
}

//Returns the position (1 to argc - 1) in the command's argument list where the given parameter apears, or 0 if not present
int Cmd_CheckParm (char *parm)
{
	int i, c;

	if (!parm)
		assert(!"Cmd_CheckParm: NULL");

	c = Cmd_Argc();
	for (i = 1; i < c; i++)
		if (! strcasecmp (parm, Cmd_Argv (i)))
			return i;

	return 0;
}



void Cmd_Init (void)
{
	// register our commands
	Cmd_AddCommand ("exec", Cmd_Exec_f);
	Cmd_AddCommand ("echo", Cmd_Echo_f);
	Cmd_AddCommand ("aliaslist", Cmd_AliasList_f);
	Cmd_AddCommand ("aliasedit", Cmd_EditAlias_f);
	//Cmd_AddCommand ("aliasa", Cmd_Alias_f);
	Cmd_AddCommand ("alias", Cmd_Alias_f);
	Cmd_AddCommand ("tempalias", Cmd_Alias_f);
	Cmd_AddCommand ("viewalias", Cmd_Viewalias_f);
	Cmd_AddCommand ("unaliasall", Cmd_UnAliasAll_f);
	Cmd_AddCommand ("unalias", Cmd_UnAlias_f);
	Cmd_AddCommand ("unalias_re", Cmd_UnAlias_re_f);
	Cmd_AddCommand ("wait", Cmd_Wait_f);
	Cmd_AddCommand ("cmdlist", Cmd_CmdList_f);
	Cmd_AddCommand ("if", Cmd_If_f);
	Cmd_AddCommand ("if_exists", Cmd_If_Exists_f);

	Cmd_AddCommand ("macrolist", Cmd_MacroList_f);
	qsort(msgtrigger_commands,
	      sizeof(msgtrigger_commands)/sizeof(msgtrigger_commands[0]),
	      sizeof(msgtrigger_commands[0]),Commands_Compare_Func);
}
