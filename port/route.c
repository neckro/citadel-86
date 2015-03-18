/*
 *				Route.C
 *
 * Some of the C86Net routemail code.
 */

#define NET_INTERNALS
#define NET_INTERFACE
#include "ctdl.h"

/*
 *				history
 *
 * 89Feb?? HAW  Created.
 */

/*
 *			      contents
 *
 *	netRouteMail()		Incoming route mail
 *	MakeRouted()		ST route mail => C86Net
 */
char   RouteMailForHere;
int    RouteToDirect = ERROR;

static int  RWorkBuf[7];
char RCount, SCount;

extern int       TransProtocol;
extern int    RouteSlot;
extern char      TrError;
extern PROTO_TABLE Table[];
extern CONFIG    cfg;
extern NetBuffer netBuf, netTemp;
extern NetTable  *netTab;
extern FILE      *upfd, *netMisc, *netLog;
extern int       thisNet;
extern MessageBuffer   msgBuf;
extern logBuffer logBuf;
extern long ByteCount, EncCount;
extern char     *READ_ANY, OverRides;
extern AN_UNSIGNED RecBuf[];

#define BAD_ROUTE "Mail Routing: %s did not recognize %s (%s) for routed mail (%s)."

#define NodeDisabled(x) (!(netTab[x].ntMemberNets & ALL_NETS))

char ParseSTRoute(char *str, char **TargetSystem, char **Domain,
						 char **UserName);

/**** These functions handle incoming route mail. ****/

/*
 * netRouteMail() should be called in the Net Receive routines, in the big
 * case statement.
 */

int RMcount = 0;		/* Incoming RoutMail count for us       */
/*
 * netRouteMail()
 *
 * The caller has asked us to route Mail somewhere.  We determine if we want
 * to accept the mail or not, and if we do then we do, in fact, receive it.
 */
void netRouteMail(struct cmd_data *cmds)
{
    DOMAIN_FILE fn;
    label       temp;
    int		val;
    char	MailForUs = FALSE, bad, dup;
    extern char *APPEND_ANY, PosId;
    label       Name, Id;

/* #define FLAWLESS */
#ifndef FLAWLESS

splitF(netLog, "nRM - '%s' '%s' '%s'\n", 
cmds->fields[0], cmds->fields[1], cmds->fields[2]);

#endif
    NormStr(cmds->fields[1]);
    NormStr(cmds->fields[2]);
    /*
     * Do we do net routing?  At all?  From this node?  To the indicated
     * node?  Even if we don't do routing per se, we should accept routed
     * mail intended for this system, which is handled by RouteHere().
     * Also, we do not route to disabled nodes.
     */
    bad = (!PosId ||
	(!(MailForUs = RouteHere(cmds->fields[0], cmds->fields[1],
				cmds->fields[2]))) && !netBuf.nbflags.RouteFor);

#ifndef FLAWLESS

if (bad) {
splitF(netLog, "bad set at 1, PosId is %d, RouteHere('%s', '%s', '%s') returns %d\n", PosId, 
cmds->fields[0], cmds->fields[1], cmds->fields[2],
RouteHere(cmds->fields[0], cmds->fields[1], cmds->fields[2]));
}

#endif

    if (!bad && !MailForUs) {
	if (!FindTheNode(cmds->fields[0], cmds->fields[1])) {
	    if (strLen(cmds->fields[2]) != 0) {
		bad = ((val=DomainMailFileName(fn, cmds->fields[2],
				cmds->fields[0], cmds->fields[1])) == REFUSE);
#ifndef FLAWLESS
if (bad) {
splitF(netLog, "bad set at 2, DMFN('%s', '%s', '%s') returned REFUSE\n",
cmds->fields[2], cmds->fields[0], cmds->fields[1]);
}
#endif
	    }
	    else val = (MailForUs) ? OURS : LOCALROUTE;
	    if (!bad && val == LOCALROUTE) {
		bad = (!AcceptRoute(cmds->fields[0],"") ||
						NodeDisabled(RouteSlot));
#ifndef FLAWLESS
if (bad) {
splitF(netLog, "bad set at 3, AR('%s') || NodeDisabled(%d)\n", cmds->fields[0], RouteSlot);
}
#endif
	    }

	    /* trying to route mail without domain we don't know about? */
	    if (bad && strLen(cmds->fields[2]) == 0) {
		if (SystemInSecondary(cmds->fields[1], cmds->fields[2], &dup))
		    if (!dup) {
			bad = FALSE;
			val=DomainMailFileName(fn, cmds->fields[2],
					cmds->fields[0], cmds->fields[1]);
		    }
	    }
	}
	else val = LOCALROUTE;
    }
    else if (MailForUs) val = OURS;

    if (bad) {
	splitF(netLog, "Rejecting %s (%s)\n", cmds->fields[1], cmds->fields[0]);
	reply(BAD, "not routing to this node for you");
	return;
    }

    if (val == LOCALROUTE) {
	strcpy(Name, netTemp.netName);
	strcpy(Id, netTemp.netId);
    }

    if (!MailForUs)
	MailForUs = val == OURS;

    if (MailForUs) {
	sprintf(temp, "rmail.%d", RMcount++);
	makeSysName(fn, temp, &cfg.netArea);
    }
    else if (val == LOCALROUTE) {
	/* Ugly ugly kludge until we figure out how nbHiRouteInd is wrong */
	if (netTemp.nbHiRouteInd < 0) netTemp.nbHiRouteInd = 0;
	sprintf(temp, "R%d.%d", RouteSlot, netTemp.nbHiRouteInd++);
	makeSysName(fn, temp, &cfg.netArea);
    }
    else {
	strcpy(Name, cmds->fields[1]);
	strcpy(Id, cmds->fields[0]);
    }

    if ((upfd = fopen(fn, APPEND_ANY)) == NULL) {
	reply(BAD, "internal error");
splitF(netLog, "internal error, couldn't open %s\n", fn);
	return;
    }

    if (!MailForUs) {
	strcpy(Name, UseNetAlias(Name, TRUE));
	fprintf(upfd, "%-20s", Id);
	putc(0, upfd);
	fprintf(upfd, "%-20s", Name);
	putc(0, upfd);
    }
    splitF(netLog, "Route mail for %s.\n", cmds->fields[1]);

    if (!MailForUs)
	StartEncode(putFLChar);

    if (ITL_StartRecMsgs(fn, TRUE, FALSE, (!MailForUs) ? Encode : NULL)
							!= ITL_SUCCESS) {
	switch (val) {
	case LOCALROUTE:
	    netTemp.nbHiRouteInd--; break;
	case DOMAINFILE:
	    DomainFileAddResult(cmds->fields[2], "", "", DOMAIN_FAILURE); break;
	}
	MailForUs = FALSE;
	splitF(netLog, "Problem receiving routemail\n");
    }
    else {
    /*
     * Now we note the fact that we have received routed mail.
     */
	switch (val) {
	case LOCALROUTE:
	    netTemp.nbflags.HasRouted = TRUE;
	    putNet(RouteSlot, &netTemp);
	    break;
	case DOMAINFILE:
	    DomainFileAddResult(cmds->fields[2], Name, cmds->fields[0],
						 DOMAIN_SUCCESS); break;
	}
    }
    if (MailForUs)
	RouteMailForHere = TRUE;
}

/*
 * AcceptRoute()
 *
 * Will we route to the given node?
 */
char AcceptRoute(char *id, char *name)
{
    if (!FindTheNode(id, name)) return FALSE;

    return netTemp.nbflags.RouteTo;
}

/**** These functions handle outgoing route mail. ****/
/*
 * RouteOut()
 *
 * This is the manager of sending outgoing route mail to the current node.  It
 * must handle error conditions.
 */
void RouteOut(NetBuffer *nBuf, int node, char DirectConnect)
{
    void RouteOutWork(NetBuffer *, int , char , char );

    if (DirectConnect && nBuf->nbflags.Stadel) {
	RouteToDirect = 0;
	return;
    }
    RouteOutWork(nBuf, node, DirectConnect, FALSE);
}

/*
 * RouteOutWork()
 *
 * This function does some of the common routing work.
 */
void RouteOutWork(NetBuffer *nBuf, int node, char DirectConnect, char LCheck)
{
    char	    failed = FALSE;
    int	     rover, result;
    label	   temp, Tid, Tname, ThisId;
    SYS_FILE	fn;

    normId(nBuf->netId, ThisId);
    for (rover = 0; rover <= nBuf->nbHiRouteInd; rover++) {
	sprintf(temp, "R%d.%d", node, rover);
	makeSysName(fn, temp, &cfg.netArea);
	if ((result = SendRouteMail(fn, "", Tid, Tname, LCheck)) == GOOD_SEND)
	    unlink(fn);
	else if (result == REFUSED_ROUTE) {
	    /*
	     * Bad return.  Could be
	     *   a) system is not compatible at this level, or
	     *   b) system doesn't know about target
	     */
	    if (DirectConnect && strCmpU(ThisId, Tid) == SAMESTRING) {
		splitF(netLog, "Rerouting to use normal mail.\n");
		if (RouteToDirect == -1) {
		    RouteToDirect = rover;
		}
	    }
	    else {
		sprintf(msgBuf.mbtext, BAD_ROUTE, nBuf->netName, 
						Tname, Tid, RecBuf + 1);
		netResult(msgBuf.mbtext);
		failed = TRUE;
	    }
	}
	else if (result != NO_SUCH_FILE)
	    failed = TRUE;	/* something failed, dunno what */
    }

    if (!failed) {
	nBuf->nbflags.HasRouted = FALSE;
	nBuf->nbHiRouteInd      = 0;
    }
}

/*
 * SendRouteMail()
 *
 * This function will send route mail.
 *
 * If LocalCheck is true then we are sending in normal mode format (request
 * type 1), as a stream of messages.  If it is not on then we are sending each
 * message separately in its own envelope (a series of ROUTE_MAIL requests).
 */
char SendRouteMail(char *filename, char *domainname, char *Tid, char *Tname,
								char LocalCheck)
{
    struct cmd_data cmds;
    char work[(2 * NAMESIZE) + 10];
    label ThisId;

    normId(netBuf.netId, ThisId);
    if ((netMisc = fopen(filename, READ_ANY)) != NULL) {
	getMsgStr(getNetChar, cmds.fields[0], NAMESIZE);
	getMsgStr(getNetChar, cmds.fields[1], NAMESIZE);

	if (!normId(cmds.fields[0], Tid) || strLen(Tid) == 0)
	    strcpy(cmds.fields[0], "  ");
	else
	    strcpy(cmds.fields[0], Tid);

	if (LocalCheck)
	    if (!netBuf.nbflags.Stadel && strCmpU(Tid,ThisId) != SAMESTRING) {
		fclose(netMisc);
		return REFUSED_ROUTE;	/* actually, just a lie */
	    }

	NormStr(cmds.fields[1]);
	strcpy(Tname, cmds.fields[1]);		/* reporting purposes */
	strcpy(cmds.fields[2], domainname);	/* just for luck	*/

	cmds.command = ROUTE_MAIL;
	if (strLen(domainname) != 0)
		splitF(netLog, "Routing mail to %s _ %s\n", cmds.fields[1],
								domainname);
	else
		splitF(netLog, "Routing mail to %s\n", cmds.fields[1]);

	if (LocalCheck || sendNetCommand(&cmds, "Route Mail")) {
	    if (LocalCheck || ITL_SendMessages()) {
		StartDecode(ReadRoutedDest);
		RCount = SCount = 0;
		sprintf(work,
			(strLen(domainname) != 0) ? "%s _ %s" : "%s%s",
				cmds.fields[1], domainname);
		
		while (getMessage(ReadRouted, TRUE, FALSE, TRUE))
		    if (netBuf.nbflags.Stadel)
			prStStyle(1,ReadRouted, Tname, sendITLchar, domainname);
		    else
			prNetStyle(TRUE, ReadRouted, sendITLchar, TRUE, work);
		fclose(netMisc);

		if (!LocalCheck) ITL_StopSendMessages();
if (cfg.BoolFlags.debug)
splitF(netLog, "Encoded %ld bytes, sent %ld bytes.\n", EncCount, ByteCount);
		if (TrError == TRAN_SUCCESS)
		    return GOOD_SEND;
		else
		    return UNKNOWN_ERROR;
	    }
	}
	else {
	    splitF(netLog, "RouteMail rejection!\n");
	    fclose(netMisc);
	    return REFUSED_ROUTE;
	}
    }
    return NO_SUCH_FILE;
}

/*
 * AdjustRoute()
 *
 * This function adjusts the routed files names.  Basically, it finds "holes"
 * in the sequence of numerical filenames and adjusts names to fill those
 * holes.
 */
void AdjustRoute()
{
    int      rover, next = ERROR;
    label    temp;
    SYS_FILE fn, fn2;

    rover = 0;
    while (rover < netBuf.nbHiRouteInd) {
	sprintf(temp, "R%d.%d", thisNet, rover);
	makeSysName(fn, temp, &cfg.netArea);
	if (access(fn, 0) != 0) {
	    next = rover + 1;

	    do {
		sprintf(temp, "R%d.%d", thisNet, next++);
		makeSysName(fn2, temp, &cfg.netArea);
	    } while (access(fn2, 0) != 0 && next <= netBuf.nbHiRouteInd);

	    if (access(fn2, 0) != 0) {
		if (rover == 0) {
		    netBuf.nbflags.HasRouted = FALSE;
		    netBuf.nbHiRouteInd = 0;
		}
		else
		    netBuf.nbHiRouteInd = rover - 1;
		break;
	    }
	    else {
		rename(fn2, fn);
		rover++;
	    }
	}
	rover++;
    }

    /* I think* this is right. */
    if (rover == 0) {
	netBuf.nbflags.HasRouted = FALSE;
	netBuf.nbHiRouteInd = 0;
    }
}

/*
 * SendRoutedAsLocal()
 *
 * This function will reroute route mail to use local mail.
 */
int SendRoutedAsLocal()
{
    label ThisId, Tid, Name, temp;
    SYS_FILE fn;
    char finished = FALSE;
    int  toReturn = 0, result;
    extern char OverRides;

    if (RouteToDirect == -1 && !netBuf.nbflags.Stadel) return 0;

    normId(netBuf.netId, ThisId);

    if (RouteToDirect == -1)
	RouteToDirect = 0;

    do {
	sprintf(temp, "R%d.%d", thisNet, RouteToDirect++);
	makeSysName(fn, temp, &cfg.netArea);
	if ((result = SendRouteMail(fn, "", Tid, Name, TRUE)) == GOOD_SEND) {
	    unlink(fn);
	    toReturn++;
	}
	else if (result == NO_SUCH_FILE)
	    finished = TRUE;
    } while (!finished);
    return toReturn;
}

/*
 * ReadRoutedDest()
 *
 * This work function will help read encrypted (style 1) data.
 */
int ReadRoutedDest(int c)
{
    RWorkBuf[RCount++] = c;
    return TRUE;
}

/*
 * ReadRouted()
 *
 * This function will read a routed char for getMessage().
 */
int ReadRouted()
{
    int c;

    if (RCount != SCount)
	return RWorkBuf[SCount++];

    RCount = SCount = 0;
    while (SCount == RCount && (c = fgetc(netMisc)) != EOF)
	Decode(c);

    if (RCount != SCount)
	return RWorkBuf[SCount++];

    if (c == EOF) StopDecode();

    if (RCount != SCount)
	return RWorkBuf[SCount++];

    return -1;
}

/*
 * prStStyle()
 *
 * This is used to handle problems inherent in STadel mail routing.
 */
void prStStyle(int mode, int (*SourceFn)(void), char *Name,
					int (*M)(int c), char *Domain)
{
    char work[4 * NAMESIZE];
    extern SListBase MailForward;
    ForwardMail *address;

    if (strCmpU(netBuf.netName, Name) != SAMESTRING) {
	if (!msgBuf.mbaddr[0]) {
	    if ((address = SearchList(&MailForward, msgBuf.mbto)) != NULL)
		strcpy(msgBuf.mbto, address->Alias);
	}
	if (strLen(Domain) != 0) {
	    sprintf(work, "%s.%s!%s", Name, Domain, msgBuf.mbto);
	    strcpy(msgBuf.mbto, work);
	}
	else {
	    sprintf(work, "%s!%s", netBuf.netName, Name);
	    NormStr(work);	/* shave off trailing blanks... */
	    sprintf(lbyte(work), "!%s", msgBuf.mbto);
	    strcpy(msgBuf.mbto, work);
	}
    }
    if (strLen(msgBuf.mbdomain) != 0 && strLen(msgBuf.mboname) != 0) {
	sprintf(work, "%s.%s!%s", msgBuf.mboname, msgBuf.mbdomain,
							msgBuf.mbauth);
	strcpy(msgBuf.mbauth, work);
    }
    prNetStyle(mode, SourceFn, M, TRUE, Name);
}

/*
 * MakeRouted()
 *
 * This function transforms an ST route mail message into C86Net.
 *
 *	Format: <system>![<system>!...]<username> ... I hope!
 *
 * There should also be allowances for using domain addresses.
 */
void MakeRouted()
{
    char  dup, *UserName, *TargetSystem, *Domain, work[(2 * NAMESIZE) + 6];
    label TheDomain, System;
    int   Slot, val = ERROR;

    /*
     * We parse the author field (which is the route back to the
     * author, form [<system>!...]<home system>!<username>.  TargetSystem
     * should have the name of the home system, UserName is obvious.  If
     * TargetSystem contains a '.', then the address is domain-defined.
     */
    if (ParseSTRoute(msgBuf.mbauth, &TargetSystem, &Domain, &UserName)) {
	strcpy(msgBuf.mboname, TargetSystem);

	msgBuf.mborig[0] = 0;

	/*
	 * Is this domain mail?  If so, just set the domain stuff up
	 * correctly and don't worry whether we know this guy personally.
	 */
	if (Domain != NULL) {
	    strcpy(msgBuf.mbdomain, Domain);
	    strcpy(msgBuf.mboname, TargetSystem);
	}
	else {
	/*
	 * So now we find out if we know the name.  If we do, netTemp will
	 * contain the appropriate net data.
	 */
	    if (IdStName(msgBuf.mboname) != ERROR)
		strcpy(msgBuf.mborig, netTemp.netId);
	    strcpy(msgBuf.mbauth, UserName);
	}
    }

    /*
     * Now we parse the To field, which is potentially in the same format
     * as the Author field, above.  TargetSystem is the final target,
     * User name is the recipient.  If, for some reason, no target system
     * is specified, we assume we are the target.
     */
    if (!ParseSTRoute(msgBuf.mbto, &TargetSystem, &Domain, &UserName)) {
	TargetSystem = cfg.nodeName + cfg.codeBuf;
	UserName = msgBuf.mbto;
    }

    if (Domain == NULL)
	splitF(netLog, "Routing mail to %s.\n", TargetSystem);
    else
	splitF(netLog, "Routing mail to %s _ %s.\n", TargetSystem, Domain);


    /* If we are the target, just deliver the damn message and get out. */
    if (RouteHere("", TargetSystem, Domain)) {
	strcpy(msgBuf.mbto, UserName);
	putMessage(&logBuf, SKIP_AUTHOR);
	return;
    }

    /*
     * OK, so the Mail is not for our system.  Find out whom and put in
     * routing.
     */

    /*
     * If we can't figure out the target system, dump it and die.
     */
    if ((Slot = IdStName(TargetSystem)) == ERROR) {
	/* search secondary list now */
	if (Domain != NULL && strLen(Domain) != 0)
	    sprintf(work, "%s _ %s", TargetSystem, Domain);
	else
	    strcpy(work, TargetSystem);

	if (!SystemInSecondary(work, TheDomain, &dup) || dup) {
	    strcpy(System, TargetSystem);
	    strcpy(msgBuf.mbto, UserName);
	    splitF(netLog, "Could not identify target '%s'.\n", TargetSystem);
	    netMailOut(TRUE, System, "unknown", FALSE, 0, 0);
	    return ;
	}
	Domain = TheDomain;
	TargetSystem = work;
    }
    else TargetSystem = netTemp.netName;

    /* We know where, now to put it all together. */
    strcpy(msgBuf.mbto, UserName);

    if (val == LOCALROUTE && !netBuf.nbflags.RouteFor) {
	sprintf(msgBuf.mbtext, "Mail Routing: %s tried to illegally route via you.\n",
							netBuf.netName);
	netResult(msgBuf.mbtext);
	return ;
    }

#ifdef WANTED
    if (val == LOCALROUTE && !netTemp.nbflags.RouteTo) {
	sprintf(msgBuf.mbtext, "Mail Routing: %s tried to route to %s.\n",
					netBuf.netName, netTemp.netName);
	netResult(msgBuf.mbtext);
	return ;
    }
#endif

    if (Domain != NULL)
	netMailOut((Domain != NULL), TargetSystem, Domain, FALSE, Slot, 0);

    if (val == LOCALROUTE && thisNet == Slot) {
		/* kludge fix if system is routing back to itself */
	netBuf.nbflags.HasRouted = netTemp.nbflags.HasRouted;
	netBuf.nbHiRouteInd++;
    }
}

/*
 * IdStName()
 *
 * This function will find the STadel route name in our lists. It leaves
 * netTemp with the net element which matched, and returns the
 * slot number.  It does limited translations for underscores.
 */
int IdStName(char *name)
{
    int Slot;
    char *Alias;
    int StSearch(char *name);

    while ((Alias = strchr(name, ' ')) != NULL)
	*Alias = '_';

    if ((Slot = StSearch(name)) != ERROR) return Slot;

    if ((Alias = strchr(name, '_')) != NULL) {
	while ((Alias = strchr(name, '_')) != NULL)
	    *Alias = ' ';

	Slot = StSearch(name);
    }
    return Slot;
}

/*
 * StSearch()
 *
 * This function will search the ctdlnet for the given node by name.  If the
 * initial search fails, then the alias list is searched for an alias and
 * another attempt is made using the result.
 */
int StSearch(char *name)
{
    int Slot;

    if ((Slot = searchNameNet(name, &netTemp)) != ERROR)
	return Slot;

    if ((Slot = searchNameNet(UseNetAlias(name, FALSE), &netTemp)) != ERROR)
	return Slot;

    return ERROR;
}

/*
 * UseNetAlias()
 *
 * This will find a usenet alias or the converse from ALIASES.SYS.
 */
char *UseNetAlias(char *Name, char FindAlias)
{
	FILE *fd;
	char line[200], *c, *realname;
	SYS_FILE fn;

	makeSysName(fn, "aliases.sys", &cfg.roomArea);
	if ((fd = fopen(fn, READ_TEXT)) != NULL) {
		while (GetAString(line, sizeof line, fd) != NULL) {
			if ((c = strchr(line, '#')) != NULL) *c = 0;
			if (!*line || *line == ' ') continue;
			if ((realname = strtok(line, ":")) == NULL) continue;
			if (FindAlias && strCmpU(realname, Name) == 0) {
				fclose(fd);
				if ((c = strtok(NULL, ":")) == NULL)
					return NULL;
				else return strdup(c);
			}
			else if (!FindAlias) {
				while ((c = strtok(NULL, ":")) != NULL) {
					if (strCmpU(c, Name) == 0) {
						fclose(fd);
						return strdup(realname);
					}
				}
			}
		}
	}
	fclose(fd);
	return Name;
}

/*
 * ParseSTRoute()
 *
 * This parses a ST route path for recipient and target.  It can handle
 * domain specifications, too.
 *
 * RETURNS:
 *
 *  o FALSE if this does not appear to have any STadel routing information
 *    information in the field.
 *
 *  o TRUE if there did seem to be information and it was parsed out.
 *
 * str - the field containing the unparsed information.  This will be
 * changed by this function.
 *
 * TargetSystem - will contain the target system name after parsing.
 *
 * Domain - will contain the domain name after parsing.
 *
 * UserName - will contain the user's name after parsing.
 */
static char ParseSTRoute(char *str, char **TargetSystem, char **Domain,
							 char **UserName)
{
    if ((*UserName = strrchr(str, '!')) == NULL) {
	return FALSE;
    }

    **UserName = 0;	/* now recipient's name does not interfere */
    (*UserName)++;		/* with system target */

    if ((*TargetSystem = strrchr(str, '!')) == NULL)
	*TargetSystem = str;
    else
	(*TargetSystem)++;		/* gets us past our search target */

    if ((*Domain = strrchr(str, '.')) != NULL) {
	**Domain = 0;
	(*Domain)++;		/* with system target */
    }

    return TRUE;
}

#ifdef REFERENCE

typedef struct {
    char *Target, *Via;		/* system names (or condensed)	*/
    char checked;		/* avoid loops */
} Routing;

#endif

void *OnTarget(), *EatRoute(char *line);
extern SListBase Routes; /* = { NULL, OnTarget, NULL, NULL, EatRoute }; */

static char CheckForRouting;
/*
 * AnyRouted()
 *
 * This function determines if there is any need to call a node due to mail
 * routing.
 */
char AnyRouted(int i)
{
    void CheckRouting();

    if (SearchList(&Routes, &i) != NULL) return FALSE;
    CheckForRouting = FALSE;
    RunListA(&Routes, CheckRouting, &i);
    return CheckForRouting;
}

/*
 * CheckRouting()
 *
 * This function helps us determine if a specified node should be called
 * due to outstanding route mail. This is done by checking the current element
 * of Routes for the specified node being the via node, and then for mail
 * needing to be sent.
 */
static void CheckRouting(Routing *element, int *i)
{
    if (CheckForRouting) return;
    if (element->Via != *i) return;
    if (netTab[element->Target].ntflags.normal_mail) CheckForRouting++;
    if (netTab[element->Target].ntflags.HasRouted) CheckForRouting++;
}

/*
 * SendOtherRoutedMail()
 *
 * This function sends mail for other systems via a routing node.
 */
void SendOtherRoutedMail(Routing *element, int *net)
{
    struct cmd_data cmds;

    if (element->Via != *net) return;
    if (!netTab[element->Target].ntflags.normal_mail &&
				!netTab[element->Target].ntflags.HasRouted) {
	return;
    }

    if (netTab[element->Target].ntflags.Stadel) {
	/* RouteToDirect++; */
	return;
    }

    getNet(element->Target, &netTemp);

    if (gotCarrier() && netTab[element->Target].ntflags.normal_mail) {
	zero_struct(cmds);
	normId(netTemp.netId, cmds.fields[0]);
	strcpy(cmds.fields[1], netTemp.netName);
	cmds.command = ROUTE_MAIL;
	if (sendNetCommand(&cmds, "Route Mail")) {
	    splitF(netLog, "Routing mail to %s\n", netTemp.netName);
	    if (ITL_SendMessages()) {
		send_direct_mail(element->Target, netTemp.netName);
		if (gotCarrier()) {
		    ITL_StopSendMessages();
		    netTemp.nbflags.normal_mail = FALSE;
		}
	    }
	    else {
		splitF(netLog, "ITL_SM failed\n");
		killConnection("SORM");
	    }
	}
	else {
	    sprintf(msgBuf.mbtext, BAD_ROUTE, netBuf.netName, netTemp.netName,
						netTemp.netId, RecBuf + 1);
	    netResult(msgBuf.mbtext);
	}
    }

    if (gotCarrier() && netTab[element->Target].ntflags.HasRouted) {
	RouteOut(&netTemp, element->Target, FALSE);
    }

    /* All done - so save what we did */
    putNet(element->Target, &netTemp);
}

/*
 * SendOtherStadelRoutedMail()
 *
 * This function handles routing direct mail through an STadel.
 */
void SendOtherStadelRoutedMail(Routing *element, int *net)
{
    int result;

    if (element->Via != *net) return;
    if (!netTab[element->Target].ntflags.normal_mail &&
				!netTab[element->Target].ntflags.HasRouted) {
	return;
    }

    if (gotCarrier()) {
	getNet(element->Target, &netTemp);
	if (netTab[element->Target].ntflags.normal_mail) {
	    result = send_direct_mail(element->Target, netTemp.netName);
	}
	if (gotCarrier()) {
	    RouteOutWork(&netTemp, element->Target, TRUE, TRUE);
	}
	if (result) {
	    netTemp.nbflags.normal_mail = FALSE;
	    putNet(element->Target, &netTemp);
	}
    }
}
