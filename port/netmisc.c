/*
 *				netmisc.c
 *
 * Networking functions of miscellaneous type.
 */

/*
 *				history
 *
 * 91Aug17 HAW  New comment style.
 * 86Aug20 HAW  History not maintained due to space problems.
 */
#define NET_INTERNALS
#define NET_INTERFACE

#include "ctdl.h"

/*
 *				contents
 *
 */

/* #define LUNATIC */
/* #define NET_DEBUG 1  */
/*
 *		External variable declarations in NET.C
 */
char *DupDomain = "Sorry, %s is listed in more than one domain.  Please use a full specification\n ";
char		ErrBuf[100];		/* General buffer for error messages */

int		AnyIndex = 0;  /* tracks who to call between net sessions */
char *BaudString =
	"Baud code (0=300, 1=1200, 2=2400, 3=4800, 4=9600, 5=14.4, 6=19.2)";

FILE		*netLog, *netMisc, *netMsg;
static char     UsedNetMsg;
char		*nMsgTemplate = "netMsg.$$$";
char		logNetResults = FALSE;
char		inNet = NON_NET;
AN_UNSIGNED     RecBuf[SECTSIZE + 5];
int		callSlot;
label		normed, callerName, callerId;
logBuffer       *lBuf;
int		PriorityMail = 0;

char		*pollCall;
extern MenuId	GetListId;

static UNS_16	*Unstable;
static char	*SupportedBauds[] = {
	"300", "3/12", "3/24", "3/48", "3/96", "3/14.4", "3/19.2"
};

/*
 * UntilNetSessions
 *
 * This is used to maintain a list of Until-Done net sessions as requested by
 * the sysop.  Each element contains information concerning which member net
 * is involved (only one can be specified) and how long the session should
 * last.
 */
void freeUNS();
SListBase UntilNetSessions = { NULL, ChkTwoNumbers, NULL, freeUNS, NULL };

/*
 *		External variable definitions for NET.C
 */
extern CONFIG    cfg;		/* Lots an lots of variables    */
extern NetBuffer netTemp;
extern logBuffer logBuf;	/* Person buffer		*/
extern logBuffer logTmp;	/* Person buffer		*/
extern aRoom     roomBuf;	/* Room buffer			*/
extern rTable    *roomTab;
extern MessageBuffer   msgBuf;
extern MessageBuffer   tempMess;
extern NetBuffer netBuf;
extern NetTable  *netTab;
extern int       thisNet;
extern char      onConsole;
extern char      loggedIn;	/* Is we logged in?		*/
extern char      outFlag;	/* Output flag			*/
extern char      haveCarrier;	/* Do we still got carrier?     */
extern char      modStat;	/* Needed so we don't die       */
extern char      WCError;
extern int       thisRoom;
extern int       thisLog;
extern char      *confirm;
extern char      heldMess;
extern int	 CurLine;
extern char	 Pageable;
extern char      netDebug;
extern char      *AssignAddress;
extern int	 outPut;
extern FILE	 *upfd;
extern char	 *APPEND_TEXT;
extern char      remoteSysop;
extern char	 *DomainFlags;
extern char	 *ALL_LOCALS;
extern char	 *R_SH_MARK;

/*
 * called_stabilize()
 *
 * This function attempts to stabilize communication on the receiver end.
 */
char called_stabilize()
{
    char retVal = TRUE;

    retVal = getNetBaud();	/* has to handle stroll, too. */

    if (!gotCarrier()) {
	killConnection("caller_stab");
	retVal = FALSE;
    }

    return retVal;
}

static int table[2][3] = {
	{ 7, 13, 69 },
	{ 68, 79, 35 }
};
/*
 * check_for_init()
 *
 * This function looks for the networking initialization sequence.
 */
char check_for_init(char mode)
{
    int index;
    int	count, timeOut;
    AN_UNSIGNED thisVal, lastVal;

    index = (inNet == STROLL_CALL) ? 1 : 0;
    lastVal = (mode) ? table[index][0] : 0;
    timeOut = (INTERVALS / 2) * (25);
    for (count = 0; count < timeOut; count++) {
	if (MIReady()) {
	    thisVal = Citinp();
	    if (cfg.BoolFlags.debug) splitF(netLog, "%d ", thisVal);
	    if (thisVal == table[index][0])
		lastVal = table[index][0];
	    else if (thisVal == table[index][1]) {
		if (lastVal == table[index][0]) lastVal = table[index][1];
		else	lastVal = 0;
	    }
	    else if (thisVal == table[index][2]) {
		if (lastVal == table[index][1]) {
		    lastVal = AckStabilize(index);
		    if (lastVal == ACK) return TRUE;
		    else if (lastVal == table[index][2])
			return (AckStabilize(index) == ACK);
		    else if (lastVal != table[index][0] &&
					lastVal != table[index][1])
			return FALSE;
		}
	    }
	}
	else pause(1);
    }
    return FALSE;
}

/*
 * AckStabilize()
 *
 * This function tries to stabilize with net caller.
 */
int AckStabilize(int index)
{
    outMod(~(table[index][0]));
    outMod(~(table[index][1]));
    outMod(~(table[index][2]));
    return receive(1);
}

/*
 * AddNetMsgs()
 *
 * This function integrates messages into the data base.  Options include
 * adding the net area or not to the filename and specifying the processing
 * function rather than being stuck with a standard processing function.
 * Usually the processing function will integrate messages into the message
 * data base, although it may do negative mail checking instead.
 */
int AddNetMsgs(char *base, void (*procFn)(void), char zap, int roomNo,
								char AddNetArea)
{
    char tempNm[80];
    int count = 0;
    extern char *READ_ANY;

    if (AddNetArea)
	makeSysName(tempNm, base, &cfg.netArea);
    else
	strcpy(tempNm, base);

    if ((netMisc = fopen(tempNm, READ_ANY)) == NULL) {
	return ERROR;
    }
    getRoom(roomNo);
    /* If reading for mail room, prepare a log buffer. */
    if (roomNo == MAILROOM)
	lBuf = &logTmp;
    else
	lBuf = NULL;

    while (getMessage(getNetChar, TRUE, TRUE, TRUE)) {
	count++;
	if (strCmpU(cfg.nodeId + cfg.codeBuf, msgBuf.mborig) != SAMESTRING)
	    (*procFn)();
    }
    fclose(netMisc);

    if (zap == 1) unlink(tempNm);
    else if (zap == 2 && count != 0) unlink(tempNm);
    return count;
}

/*
 * getNetChar()
 *
 * This function gets a character from a network temporary file.  The file
 * should have been opened elsewhere.
 */
int getNetChar()
{
     int c;

     c = fgetc(netMisc);
     if (c == EOF) return -1;
     return c;
}

/*
 * inMail()
 *
 * This function integrates a message into the message database.  It includes
 * recognizing bangmail, vortex activation, and bad word scanning.
 */
void inMail()
{
    extern SListBase BadWords;
    extern char BadMessages[];

	/* do we need any code here? */
    if (thisRoom == MAILROOM &&
	(strchr(msgBuf.mbto, '!') != NULL ||
	 strchr(msgBuf.mbauth, '!') != NULL))
	MakeRouted();		/* Route.C */
    else if (NotVortex()) {
	if (cfg.BoolFlags.NetScanBad) {
	    if (thisRoom != MAILROOM &&
			SearchList(&BadWords, msgBuf.mbtext) != NULL) {
		if (strlen(BadMessages) != 0)
		    DiscardMessage(roomBuf.rbname, BadMessages);
		sprintf(msgBuf.mbtext,
		 	"Decency: Net message from %s @%s in %s discarded.",
			msgBuf.mbauth, msgBuf.mboname,
			(roomExists(msgBuf.mbroom)) ?
			formRoom(roomExists(msgBuf.mbroom), FALSE, FALSE) :
			msgBuf.mbroom);
		netResult(msgBuf.mbtext);
		return;
	    }
	}
	if (AssignAddress != NULL)
	    strcpy(msgBuf.mbaddr, AssignAddress);
	putMessage(&logBuf, SKIP_AUTHOR);
    }
    else {
	DiscardMessage("", "discard");
    }
}

static int GoodCount, BadCount;
/*
 * inRouteMail()
 *
 * This function handles incoming route mail.
 */
void inRouteMail()
{
    label oname, domain;

    if (RecipientAvail()) {
	inMail();
    }
    if (BadCount) {
	sprintf(lbyte(tempMess.mbtext), " on %s _ %s was undeliverable.",
			cfg.nodeName + cfg.codeBuf,
			cfg.codeBuf + cfg.nodeDomain);
	strcpy(tempMess.mbto, msgBuf.mbauth);
	strcpy(oname, msgBuf.mboname);
	strcpy(domain, msgBuf.mbdomain);
	strcpy(tempMess.mbauth, "Citadel");
	strcpy(tempMess.mbroom, "Mail");
	strcpy(tempMess.mbtime, Current_Time());
	strcpy(tempMess.mbdate, formDate());
	sprintf(tempMess.mbId, "%lu", cfg.newest++ + 1);
	ZeroMsgBuffer(&msgBuf);
	MoveMsgBuffer(&msgBuf, &tempMess);
	netMailOut(TRUE, UseNetAlias(oname, FALSE), domain, FALSE, -1, 0);
    }
}

/*
 * RecipientAvail()
 *
 * This function checks to see if recipient is here.  This includes override
 * handling.
 */
char RecipientAvail()
{
    void RecAvWork();

    GoodCount = BadCount = 0;

    if (msgBuf.mbdomain[0]) {
	if (!HasOverrides(&msgBuf)) {
	    RecAvWork(msgBuf.mbto);
	}
	else {
	    RunList(&msgBuf.mbOverride, RecAvWork);
	}
	return GoodCount;
    }
    return TRUE;
}

/*
 * RecAvWork()
 *
 * This function does the real work of RecipientAvailable() - split out to
 * better handle other recipients.
 */
static void RecAvWork(char *name)
{
    if (PersonExists(name) == ERROR &&
		strCmpU(msgBuf.mbauth, "Citadel") != SAMESTRING) {
	BadCount++;
	splitF(netLog, "No recipient: %s\n", name);
	if (BadCount == 1)
	    sprintf(tempMess.mbtext, "Your message to %s", name);
	else
	    sprintf(lbyte(tempMess.mbtext), ", %s", name);
    }
    else if (PersonExists(name) != ERROR ||
		strCmpU(msgBuf.mbauth, "Citadel") != SAMESTRING)
    	GoodCount++;
}

/*
 * DiscardMessage()
 *
 * This function prints a message to a discard file.
 */
void DiscardMessage(char *name, char *filename)
{
    if (redirect(filename, APPEND_TO)) {
	if (strlen(name)) {
	    fprintf(upfd, "%s\n", name);
	    mPrintf("%s", formHeader());
	}
	else mPrintf("%s (%s)", formHeader(), msgBuf.mbsrcId);
	doCR();
	mFormat(msgBuf.mbtext);
	doCR();
	doCR();
	undirect();
    }
}

char *kip = NULL;
/*
 * netController()
 *
 * This is the main manager of a network session.  It is responsible for
 * scheduling calls, noticing incoming calls, exiting network sessions due
 * to timeouts or other events, forming error reports for the Aide> room,
 * etc.
 */
#define unSetPoll()     free(pollCall)
void netController(int NetStart, int NetLength, MULTI_NET_DATA whichNets,
						char mode, UNS_16 flags)
{
    int x;
    int searcher = 0, start, first;
    SYS_FILE AideMsg;
    long waitTime, InterCallDelay;
    extern char *WRITE_TEXT, *READ_TEXT, *APPEND_TEXT;

    if (loggedIn)	/* should only happen on mistake by sysop */
	terminate( /* hangUp == */ TRUE, TRUE);

    outFlag = OUTOK;		/* for discarding messages correctly */

    inNet = mode;

    setPoll();
    switch (mode) {
	case ANYTIME_NET:
	case UNTIL_NET:
	    if (!AnyCallsNeeded(whichNets)) {
		inNet = NON_NET;
		unSetPoll();
		return;
	    }
	    searcher = AnyIndex;    /* so we don't always start at front */
	    break;
	case ANY_CALL:
	    while (MIReady()) Citinp();
	    break;
    }

    InterCallDelay = (!(flags & LEISURELY)) ? 2l : 15l;

    if (logNetResults) {
	makeSysName(AideMsg, "netlog.sys", &cfg.netArea);
	if ((netLog = fopen(AideMsg, APPEND_TEXT)) == NULL)
	    netResult("Network Logging: Couldn't open netLog.");
    }
    else
	netLog = NULL;

    loggedIn = FALSE;			/* Let's be VERY sure.	*/
    thisLog = -1;

    splitF(netLog, "\nNetwork Session");
    splitF(netLog, "\n%s @ %s\n", formDate(), Current_Time());
    SpecialMessage("Network Session");
    logMessage(INTO_NET, 0, 0);
    modStat = haveCarrier = FALSE;
    setTime(NetStart, NetLength);
    makeSysName(AideMsg, nMsgTemplate, &cfg.netArea);
    if ((netMsg = fopen(AideMsg, WRITE_TEXT)) == NULL)
	splitF(netLog, "WARNING: Can't open %s, errno %d!!!!\n", AideMsg,errno);

    UsedNetMsg = FALSE;

    x = timeLeft();
    Unstable = GetDynamic(sizeof *Unstable * cfg.netSize);

    do {	/* force at least one time through loop */
	waitTime = (cfg.catChar % 5) + 1;
	while (waitTime > minimum(5, ((x/2)))) waitTime /= 2;
	if (flags & LEISURELY)
	    for (startTimer(WORK_TIMER);
			chkTimeSince(WORK_TIMER) < (waitTime * 60) && 
							!KBReady();) {
		if (gotCarrier()) break;
		else BeNice(NET_PAUSE);
	    }

	/* This will break us out of a network session if ESC is hit */
	if (KBReady()) 
	    if (getCh() == SPECIAL) break;

	/*
	 * In case someone calls while we're doing after-call processing.
	 */
	while (gotCarrier()) { 
	    modStat = haveCarrier = TRUE;
	    called();
	    /* CacheMessages(ALL_NETS, TRUE); */
	}

	/* ok, make calls */
	if (cfg.netSize != 0) {
	    start = searcher;
	    do {
		if (needToCall(searcher, whichNets)) {
#ifdef LUNATIC
ExplainNeed(searcher, whichNets);
#endif
		    if (!HasPriorityMail(searcher))
			CacheSystem(searcher, FALSE);

		    if (callOut(searcher)) {
			if (!caller())
			    Unstable[searcher]++;
			splitF(netLog, "(%s)\n", Current_Time());
if (kip != NULL && netBuf.baudCode >= B_5) {
    moPuts(kip);
    outMod('\r');
    splitF(netLog, "debug from modem:\n");
    for (startTimer(WORK_TIMER); chkTimeSince(WORK_TIMER) < 5l || MIReady();)
	splitF(netLog, "%c", Citinp());
}
			/* CacheMessages(ALL_NETS, TRUE); */
		    }
		    for (startTimer(WORK_TIMER); !gotCarrier() &&
				chkTimeSince(WORK_TIMER) < InterCallDelay;)
			;
		    while (gotCarrier()) {
			modStat = haveCarrier = TRUE;
			called();
		    }
		    if (whichNets == PRIORITY_MAIL) {
			getNet(thisNet, &netBuf);
			netBuf.MemberNets &= ~(PRIORITY_MAIL);
			putNet(thisNet, &netBuf);
		    }
		}
		searcher = (searcher + 1) % cfg.netSize;
		if (mode == ANYTIME_NET && timeLeft() < 0 &&
						whichNets != PRIORITY_MAIL)
		    break;      /* maintain discipline */
	    } while (!KBReady() && searcher != start);
	}
	if (mode == ANYTIME_NET || mode == UNTIL_NET) {
	    if (!AnyCallsNeeded(whichNets)) break;
	}
    } while ((x = timeLeft()) > 0);

    free(Unstable);
    Unstable = NULL;

    splitF(netLog, "\nOut of Networking Mode (%s)\n\n", Current_Time());

    for (x = 0; x < cfg.netSize; x++)
	if (netTab[x].ntMemberNets & PRIORITY_MAIL) {
	    getNet(x, &netBuf);
	    netBuf.MemberNets &= ~(PRIORITY_MAIL);
	    putNet(x, &netBuf);
	}

    if (flags & REPORT_FAILURE) {
	if (AnyCallsNeeded(whichNets)) {
	    sprintf(msgBuf.mbtext, 
			"The following systems could not be reached: ");
	    for (searcher = 0, first = 1; searcher < cfg.netSize; searcher++)
		if (needToCall(searcher, whichNets)) {
		    if (!first) strcat(msgBuf.mbtext,", ");
		    first = FALSE;
		    getNet(searcher, &netBuf);
		    strcat(msgBuf.mbtext, netBuf.netName);
		}
	    strcat(msgBuf.mbtext, ".");
	    netResult(msgBuf.mbtext);
	}
    }

    if (inNet == ANYTIME_NET) {
	AnyIndex = searcher;    /* so we can start from here later */
    }

    fclose(netMsg);
    netMsg = NULL;

	/* Make the error and status messages generated into an Aide> msg */
    makeSysName(AideMsg, nMsgTemplate, &cfg.netArea);
    if (UsedNetMsg) {
	ZeroMsgBuffer(&msgBuf);
	if (access(AideMsg, 4) == -1) {
	    sprintf(msgBuf.mbtext, "Where did '%s' go???", AideMsg);
	    aideMessage("Net Aide", FALSE);
	}
	else {
	    ingestFile(AideMsg, msgBuf.mbtext);
	    aideMessage("Net Aide", FALSE);
	}
    }
    unlink(AideMsg);

    modStat = haveCarrier = FALSE;
    inNet = NON_NET;
    if (logNetResults) {
	fclose(netLog);
	netLog = NULL;
    }
    unSetPoll();
    ITL_DeInit();
    logMessage(OUTOF_NET, 0, 0);
    startTimer(NEXT_ANYNET);      /* anytime net timer */
    getRoom(LOBBY);
}

static int RunUntil;

/*
 * setTime()
 *
 * This function sets up some global variables for the networker.
 */
void setTime(int NetStart, int NetLength)
{
    int yr, hr, mins, dy, temp;
    char *mn;

    startTimer(NET_SESSION);
    if (NetLength == 0)
	RunUntil = 0;
    else {
	getCdate(&yr, &mn, &dy, &hr, &mins);
	temp = (hr * 60) + mins;
	RunUntil = 60 * (NetLength - abs(temp - NetStart));
    }
}

/*
 * timeLeft()
 *
 * This function does a rough estimate of how much time left and returns it.
 */
int timeLeft()
{
    int elapsed;

    elapsed = chkTimeSince(NET_SESSION);
    if (elapsed > RunUntil) return 0;
    return (((RunUntil - elapsed) / 60) + 1);
}

/*
 * callOut()
 *
 * This function attempts to call some other system.
 */
char callOut(int i)
{
    getNet(callSlot = i, &netBuf);
    splitF(netLog, "Calling %s @ %s (%s): ",
			netBuf.netName, netBuf.netId, Current_Time());
    strcpy(normed, netBuf.netId);		/* Cosmetics */
    strcpy(callerId, netBuf.netId);
    strcpy(callerName, netBuf.netName);
    if (makeCall(TRUE, NO_MENU)) return modStat = haveCarrier = TRUE;
    killConnection("callout");		/* Take SmartModem out of call mode   */
    splitF(netLog, "No luck.\n");
    return FALSE;
}

/*
 * moPuts()
 *
 * This function puts a string out to modem without carr check.
 */
void moPuts(char *s)
{
    while (*s) {
	pause(5);
	if (cfg.BoolFlags.debug) mputChar(*s);
	outMod(*s++);
    }
}

/*
 * netMessage()
 *
 * This function will send message via net.  This is a userland function.
 */
int netMessage(int uploading)
{
    if (!NetValidate(TRUE)) return FALSE;

    ZeroMsgBuffer(&msgBuf);

    if (!netInfo(TRUE)) return FALSE;
    return procMessage(uploading, FALSE);
}

/*
 * writeNet()
 *
 * This function writes nodes on the net to the screen.  Options include
 * showing only local systems and with or without their ids.
 */
static void writeNet(char idsAlso, char LocalOnly)
{
    int rover, count = 0, len;

    outFlag = OUTOK;

    Pageable = TRUE;
    CurLine = 1;

    mPrintf("Systems on the net:\n ");
    doCR();
    for (rover = 0; outFlag != OUTSKIP && rover < cfg.netSize; rover++) {
	if (netTab[rover].ntflags.in_use &&
		(!LocalOnly || netTab[rover].ntflags.local)) {
	    getNet(rover, &netBuf);
	    if ((idsAlso || netBuf.MemberNets & ALL_NETS)) {
		/* mPrintf("%-22s", netBuf.netName); */
		mPrintf("%s", netBuf.netName);
		if (idsAlso) {
#ifdef TURBO_C_VSPRINTF_BUG
		    SpaceBug(22 - strlen(netBuf.netName));   /* EEEESH */
		    mPrintf("%-22s%-16s%-12s", netBuf.netId,
			(needToCall(rover, ALL_NETS)) ? "<need to call>" : "",
			SupportedBauds[netBuf.baudCode]);
#else
		    mPrintf("%*c%-22s%-16s%-12s", 22 - strlen(netBuf.netName),
			' ', netBuf.netId,
			(needToCall(rover, ALL_NETS)) ? "<need to call>" : "",
			SupportedBauds[netBuf.baudCode]);
#endif
		    if (netBuf.nbflags.OtherNet) mPrintf("O");
		    else if (!(netBuf.MemberNets & ALL_NETS))
			mPrintf("d");
		    if (netBuf.nbflags.MassTransfer)
			mPrintf("F");
		    doCR();
		}
		else {
		    if (strlen(netBuf.nbShort) != 0) {
			mPrintf(" (%s)", netBuf.nbShort);
			len = strlen(netBuf.nbShort) + 3;
		    }
		    else len = 0;
		    /* mPrintf(", "); */
		    if (++count % 3 == 0) {
			count = 0;
			doCR();
		    }
		    else {
#ifdef TURBO_C_VSPRINTF_BUG
			SpaceBug(28 - (len + strlen(netBuf.netName)));   /* EEEESH */
#else
			mPrintf("%*c", 28 - (len + strlen(netBuf.netName)), ' ');
#endif
		    }
		}
	    }
	}
    }
    if (!idsAlso)
	WriteDomainContents();
    Pageable = FALSE;
}

/*
 * netStuff()
 *
 * This function handles main net menu.
 */
void netStuff()
{
    extern char *who_str;
    extern char ForceNet;
    TwoNumbers  tmp;
    char	work[50];
    label       who;
    int		logNo;
    MenuId	id, id2;
    long	Redials, duration;
    char	*NetMiscOpts[] = {
	"Add node to netlist\n", "Credit setting\n", "Dial system",
	"Edit a node\n", "Initiate Anytime Net Session", "Local list\n",
	"Net privileges\n", "Priority Mail\n", "Request File\n",
	"Send File\n", "Until Done Net Sessions", "View net list\n", "X\beXit",
#ifdef ZNEEDED
"Z",
#endif
	""
    };
    int AdminPriorityMail(char *line, int arg);

    /* If we don't net, don't allow this. */
    if (!cfg.BoolFlags.netParticipant) {
	SysopInfoReport(NO_MENU, "Networking is disabled on this installation.\n ");
	return ;
    }

    id = RegisterSysopMenu("netopt.mnu", NetMiscOpts, " Net Menu ");
    do {
	outFlag = OUTOK;
	RegisterThisMenu("netopt.mnu", NetMiscOpts);
	SysopMenuPrompt(id, "\n Net function: ");
	switch (GetSysopMenuChar(id)) {
	    case ERROR:
	    case 'X':
		CloseSysopMenu(id);
		return;
	    case 'P':
		getList(AdminPriorityMail, "Systems and Priority Mail",
							NAMESIZE, TRUE, 0);
		break;
	    case 'I':
		ForceAnytime();
		sprintf(work, "now %s.\n ", ForceNet ? "ON" : "OFF");
		SysopInfoReport(id, work);
		break;
	    case 'R':   /* File requests */
		SysopRequestString(id, "System", who, sizeof who, 0);
		if (!ReqNodeName("", who, NULL, RNN_SYSMENU, &netBuf))
		    break;
		fileRequest();
		break;
	    case 'S':   /* File transmissions */
		SysopRequestString(id, "System", who, sizeof who, 0);
		if (!ReqNodeName("", who, NULL, RNN_SYSMENU, &netBuf))
		    break;
		getSendFiles(id, who);
		break;
	    case 'C':   /* Set users' LD credits */
		if ((logNo = GetUser(who, &logTmp, TRUE)) == ERROR ||
				logNo == cfg.MAXLOGTAB) break;
		sprintf(work,
			"Currently %d credits.  How many now", logTmp.credit);
		logTmp.credit = (int) SysopGetNumber(id, work, 0l, 1000l);
		sprintf(work, "Set to %d.", logTmp.credit);
		SysopInfoReport(id, work);
		if (loggedIn  &&  strCmpU(logBuf.lbname, who) == SAMESTRING)
		    logBuf.credit = logTmp.credit;

		putLog(&logTmp, logNo);
		break;
	    case 'D':   /* Primitive dial out ability.  Don't get excited */
		if (!onConsole) break;
		if (gotCarrier()) {	/* carrier already?  just jump in */
		    CloseSysopMenu(id);
		    interact(FALSE);
		    id = RegisterSysopMenu("netopt.mnu", NetMiscOpts,
								" Net Menu ");
		    break;
		}
		/* Get node to call, if none specified abort */
		SysopRequestString(id, "System", who, sizeof who, 0);
		if (!ReqNodeName("", who, NULL, RNN_ONCE | RNN_SYSMENU,&netBuf))
		    break;

		/* How many times should we try to call? */
		if ((Redials = SysopGetNumber(id, "# of redial attempts", 0l,
							65000l)) <= 0l)
		    Redials = 1l;       /* allow empty C/R to generate 1. */

		/* Modem should be disabled since we're in CONSOLE mode. */
		id2 = SysopContinual(netBuf.netName, "Dialing ...", 50, 13);
		EnableModem(FALSE);
		for (; Redials > 0l; Redials--) {
		    /* if successful call, start chattin'! */
		    if (makeCall(FALSE, id2)) {
			SysopCloseContinual(id2);
			CloseSysopMenu(id);
			mputChar(BELL);
			interact(FALSE);
			id = RegisterSysopMenu("netopt.mnu", NetMiscOpts,
								" Net Menu ");
			break;
		    }
		    /* This handles an abort from kbd */
		    if (KBReady()) {
			getCh();
			/* Hope this turns off modem */
			outMod(' '); pause(2);
			DisableModem(FALSE);
			break;
		    }
		    /* printf("Failed\n"); */
		    /* Let modem stabilize for a moment. */
		    for (startTimer(WORK_TIMER);chkTimeSince(WORK_TIMER) < 3l; )
			;
		}
		SysopCloseContinual(id2);
		/*
		 * If we don't have carrier disable the modem.  We have this
		 * check in case sysop wants to perform download within
		 * Citadel.
		 */
		if (!gotCarrier()) {
		    DisableModem(FALSE);
		    modStat = haveCarrier = FALSE;
		}
		break;
	    case 'V':   /* View the net list. */
		CloseSysopMenu(id);
		doCR();
		writeNet(TRUE, FALSE);
		if (NeedSysopInpPrompt()) modIn();
		id = RegisterSysopMenu("netopt.mnu", NetMiscOpts, " Net Menu ");
		break;
	    case 'L':
		CloseSysopMenu(id);
		writeNet(TRUE, TRUE);
		if (NeedSysopInpPrompt()) modIn();
		id = RegisterSysopMenu("netopt.mnu", NetMiscOpts, " Net Menu ");
		break;
	    case 'A':   /* Add a new node to the list */
		addNetNode();
		break;
	    case 'E':   /* Edit a node that is on the list */
		SysopRequestString(id, "Name of system to edit", who, sizeof who,0);
		if (!ReqNodeName("", who, NULL, RNN_ONCE | RNN_DISPLAY |
							RNN_SYSMENU, &netBuf))
		    break;

		CloseSysopMenu(id);
		editNode();
		id = RegisterSysopMenu("netopt.mnu", NetMiscOpts, " Net Menu ");
		break;
	    case 'N':   /* Give someone net privileges. */
		NetPrivs(who);
		break;
	    case 'U':
		id2 = SysopContinual(" Net Session ", "", 30, 4);
		SysopContinualString(id2, "Member Net", work, 3, 0);
		tmp.first = atoi(work);
		if (tmp.first < 1 || tmp.first > MAX_NET - 1) {
		    if (strlen(work) != 0)
			SysopError(id2, "Illegal Member Net");
		}
		else {
		    if (SearchList(&UntilNetSessions, &tmp) != NULL) {
			SysopInfoReport(NO_MENU, "Net session deactivated.\n");
			KillData(&UntilNetSessions, &tmp);
		    }
		    else {
			SysopContinualString(id2, "Duration", work, 4, 0);
			duration = atol(work);
			if (duration < 1l)
			    SysopError(id2, "Illegal Duration");
			else
			    AddData(&UntilNetSessions,MakeTwo(tmp.first,duration),
								NULL, FALSE);
		    }
		}
		SysopCloseContinual(id2);
		break;
#ifdef ZNEEDED
	    case 'Z':
		inNet = NORMAL_NET;
		AddNetMsgs("tempmail.$$$", inMail, FALSE, MAILROOM, TRUE);
		inNet = NON_NET;
		break;
#endif
	}
    } while (onLine());
}

/*
 * AdminPriorityMail()
 *
 * This handles the administration of priority mail.
 */
int AdminPriorityMail(char *system, int arg)
{
    if (searchNameNet(system, &netBuf) == ERROR) {
	SysopError(NO_MENU, "No such system\n");
    }
    else {
	if (!(netBuf.nbflags.normal_mail ||
		netBuf.nbflags.HasRouted ||
		AnyRouted(thisNet) ||
		DomainFlags[thisNet])) {
	    SysopError(NO_MENU, "No outgoing mail.\n");
	    return TRUE;
	}
	else if ((netBuf.MemberNets & PRIORITY_MAIL)) {
	    SysopInfoReport(NO_MENU, "Priority mail deactivated.\n");
	    PriorityMail--;
	    netBuf.MemberNets &= ~(PRIORITY_MAIL);
	}
	else {
	    PriorityMail++;
	    netBuf.MemberNets |= (PRIORITY_MAIL);
	}
	putNet(thisNet, &netBuf);
    }
    return TRUE;
}

/*
 * NetPrivs()
 *
 * This will setup net privs for someone.
 */
void NetPrivs(label who)
{
    int logNo, result;
    char work[50];

    if ((logNo = GetUser(who, &logTmp, TRUE)) == ERROR) return;
    if (logNo == cfg.MAXLOGTAB) {
	result = DoAllQuestion("Give everyone net privs",
					"Take away everyone's net privs");
	if (result == ERROR) return;
	for (logNo = 0; logNo < cfg.MAXLOGTAB; logNo++) {
    	    getLog(&logTmp, logNo);
    	    if (!onConsole) mPrintf(".");
    	    if (logTmp.lbflags.L_INUSE && logTmp.lbflags.NET_PRIVS != result) {
    		logTmp.lbflags.NET_PRIVS = result;
    		putLog(&logTmp, logNo);
	    }
	}
	return;
    }
    sprintf(work, "%s has %snet privileges\n ", who,
				(logTmp.lbflags.NET_PRIVS) ? "no " : "");
    if (!SysopGetYesNo(NO_MENU, work, confirm))   return;
    logTmp.lbflags.NET_PRIVS = !logTmp.lbflags.NET_PRIVS;
    if (strCmpU(logTmp.lbname, logBuf.lbname) == SAMESTRING)
	logBuf.lbflags.NET_PRIVS = logTmp.lbflags.NET_PRIVS;

    putLog(&logTmp, logNo);
}

/*
 * getSendFiles()
 *
 * This will get the files from the sysop to send to another system.
 */
static void getSendFiles(MenuId id, label sysName)
{
    SYS_FILE       sysFile;
    char	   temp[10];
    extern char    *APPEND_ANY;

    sprintf(temp, "%d.sfl", thisNet);
    makeSysName(sysFile, temp, &cfg.netArea);
    if ((upfd = fopen(sysFile, APPEND_ANY)) == NULL) {
	SysopPrintf(id, "Couldn't open %s for update?\n ", sysFile);
	return ;
    }
    sprintf(msgBuf.mbtext, "Files to send to %s", sysName);
    if (getList(addSendFile, msgBuf.mbtext, 126, TRUE, 0)) {
	netBuf.nbflags.send_files = TRUE;
	putNet(thisNet, &netBuf);
    }
    fclose(upfd);
}

/*
 * addSendFile()
 *
 * This is a work function, called indirectly by getList().
 */
int addSendFile(char *Files, int arg)
{
    struct fl_send sendWhat;
    extern MenuId GetListId;

    if (sysGetSendFilesV2(GetListId, Files, &sendWhat)) {
	putSLNet(sendWhat, upfd);
	return TRUE;
    }

    return ERROR;
}

/*
 * addNetNode()
 *
 * This adds a node to the net listing.
 */
void addNetNode()
{
    int searcher, gen;
    char  found;
    extern char *ALL_LOCALS;
    MenuId id;

    id = SysopContinual("", "", 2 * NAMESIZE, 5);
    for (searcher = 0; searcher < cfg.netSize; searcher++)
	if (netTab[searcher].ntflags.in_use == FALSE) break;

    if (searcher != cfg.netSize) {
	getNet(searcher, &netBuf);
	found = TRUE;
	gen = (netBuf.nbGen + 1) % NET_GEN;
    }
    else {
	found = FALSE;
	gen = 0;
    }

    killNetBuf(&netBuf);
    zero_struct(netBuf);	/* Useful initialization       */
    initNetBuf(&netBuf);

	/* Get a unique name */
    if (!GetSystemName(netBuf.netName, -1, id)) {
	SysopCloseContinual(id);
	return;
    }

	/* Get a unique ID */
#ifndef OLD_STYLE
    if (!GetSystemId(netBuf.netId, -1, id)) {
	SysopCloseContinual(id);
	return;
    }
#else
    do {
	goodAnswer = TRUE;
	SysopContinualString(id, "System ID", netBuf.netId, NAMESIZE, 0);
	if (strlen(netBuf.netId) == 0) {
	    SysopCloseContinual(id);
	    return;
	}
	if (searchNet(netBuf.netId, &netTemp) != ERROR) {
	    sprintf(msgBuf.mbtext, "Sorry, %s is already in use.\n ",
							netBuf.netId);
	    SysopError(id, msgBuf.mbtext);
	    goodAnswer = FALSE;
	}
    } while (!goodAnswer);
#endif

    netBuf.baudCode = (int) SysopGetNumber(id, BaudString, 0l, 6l);
    netBuf.nbflags.local	= SysopGetYesNo(id, "", "Is system local");
    netBuf.nbflags.in_use	= TRUE;
    netBuf.MemberNets		= 1;     /* Default */
    netBuf.nbGen		= gen;   /* Update generation #  */
    netBuf.nbflags.RouteTo	= TRUE;
    netBuf.nbflags.RouteFor	= TRUE;

    if (!found) {
	if (cfg.netSize != 0)
	    netTab = (NetTable *) 
			realloc(netTab, sizeof (*netTab) * ++cfg.netSize);
	else
	    netTab = (NetTable *) 
			GetDynamic(sizeof(*netTab) * ++cfg.netSize);
	searcher = cfg.netSize - 1;
    }
    putNet(searcher, &netBuf);
    InitVNode(searcher);
    DomainInit(FALSE);		/* so we can redirect easily enough */
    SysopCloseContinual(id);
}

/*
 * GetSystemName()
 *
 * Get a new system name.
 */
static int GetSystemName(char *buf, int curslot, MenuId id)
{
    char  goodAnswer;
    int slot;

    do {
	SysopContinualString(id, "System name", buf, NAMESIZE, 0);
	if (strlen(buf) == 0) {
	    return FALSE;
	}
	if ((goodAnswer = strCmpU(ALL_LOCALS, buf)) == 0)
	    SysopError(id, "Sorry, reserved name\n ");
	else if (strchr(buf, '_') != NULL) {
	    goodAnswer = FALSE;
	    SysopError(id, "Please don't use '_' in the system name.\n ");
	}
	else {
	    slot = searchNameNet(buf, &netTemp);
	    if (slot != ERROR && slot != curslot) {
		sprintf(msgBuf.mbtext, "Sorry, %s is already in use.\n ", buf);
		SysopError(id, msgBuf.mbtext);
		goodAnswer = FALSE;
	    }
	}
    } while (!goodAnswer);
    return TRUE;
}

/*
 * GetSystemId()
 *
 * This function gets a system id from the user and does error checking.
 */
static int GetSystemId(char *buf, int curslot, MenuId id)
{
    char goodAnswer;
    int  slot;

    do {
	goodAnswer = TRUE;
	SysopContinualString(id, "System ID", buf, NAMESIZE, 0);
	if (strlen(buf) == 0) {
	    return FALSE;
	}
	if ((slot = searchNet(buf, &netTemp)) != ERROR && slot != curslot) {
	    sprintf(msgBuf.mbtext, "Sorry, %s is already in use.\n ",
							buf);
	    SysopError(id, msgBuf.mbtext);
	    goodAnswer = FALSE;
	}
    } while (!goodAnswer);
    return TRUE;
}

/*
 * addNetMem()
 *
 * This adds nets to this system's list.
 */
int MemberNets(char *netnum, int add)
{
    int num;
    MULTI_NET_DATA temp;

    num = atoi(netnum);
    if (num < 1 || num > MAX_NET - 1) {
	SysopError(NO_MENU, "There are only 31 nets to choose from.\n");
	return TRUE;
    }
    temp = 1l;
    temp <<= (num-1);
    if (add)
	netBuf.MemberNets |= temp;
    else {
	temp = ~temp;
	netBuf.MemberNets &= temp;
    }
    return TRUE;
}

/*
 * editNode()
 *
 * This function will edit a net node.
 */
void editNode()
{
    label  temp2;
    char   title[50], work[80], temp[NAMESIZE*3];
    int    place, compress;
    MenuId id, id2;
    char   exttemp;
    char   *NetEditOpts[] = {
	"Access setting\n", "Baud code change\n", "Condensed name\n",
	"Download toggle", "External Dialer", "Fast Transfers\n",
	"ID change\n",
	"Kill node from list\n", "Local setting\n", "Member Nets\n",
	"Name change\n", "Othernet toggle\n", "Passwords\n", "Rooms stuff\n",
	"Spine settings\n", "Values", "Wait for room", "X\beXit",
#ifdef NEEDED
	"ZKludge",
#endif
	""
    };

    place = thisNet;    /* this is really a kludge, but for now will serve */

    if (!NeedSysopInpPrompt())	/* rather icky, really.  fix someday?	*/
	NodeValues(NO_MENU);

    sprintf(title, " Editing %s ", netBuf.netName);
    id = RegisterSysopMenu("netedit.mnu", NetEditOpts, title);

    while (onLine()) {
	outFlag = OUTOK;
	sprintf(work, "\n (%s) edit fn: ", netBuf.netName);
	SysopMenuPrompt(id, work);
	switch (GetSysopMenuChar(id)) {
	    case ERROR:
	    case 'X':
		putNet(place, &netBuf);
		CloseSysopMenu(id);
		return;
	    case 'E':
		exttemp = netBuf.nbflags.ExternalDialer;
		if ((netBuf.nbflags.ExternalDialer =
		SysopGetYesNo(id, "", "Use an external dialer to reach this system"))) {
		    SysopRequestString(id, "Dialer information",
				netBuf.access, sizeof netBuf.access, 0);
		}
		else if (exttemp)	/* clear old information */
		    netBuf.access[0] = 0;
		break;
	    case 'A':
		SysopRequestString(id, "Access string", netBuf.access,
						sizeof netBuf.access, 0);
		break;
	    case 'B':
		netBuf.baudCode = (int) SysopGetNumber(id, BaudString, 0l, 6l);
		break;
	    case 'C':
		SysopRequestString(id, "shorthand for node", temp, 3, 0);
		if (strCmpU(temp, netBuf.nbShort) == 0)
		    break;
		if (searchNameNet(temp, &netTemp) != ERROR) {
		    sprintf(work, "'%s' is already in use.", temp);
		    SysopError(id, work);
		}
		else
		    strcpy(netBuf.nbShort, temp);
		break;
	    case 'D':
		sprintf(work, "for %s %s.\n ",
			netBuf.netName, netBuf.nbflags.NoDL ? "ON" : "OFF");
		SysopInfoReport(id, work);
		netBuf.nbflags.NoDL = !netBuf.nbflags.NoDL;
		break;
	    case 'F':
		netBuf.nbflags.MassTransfer = !netBuf.nbflags.MassTransfer;
		if (netBuf.nbflags.MassTransfer) {
			/* kludges - next major release make into char */
		    if ((compress = GetUserCompression()) == NO_COMP) {
			netBuf.nbflags.MassTransfer = FALSE;
			RegisterThisMenu("netedit.mnu", NetEditOpts);
			break;
		    }
		    else netBuf.nbCompress = compress;
		    RegisterThisMenu("netedit.mnu", NetEditOpts);
		}
		sprintf(work, "for %s %s.\n ", netBuf.netName,
				netBuf.nbflags.MassTransfer ? "ON" : "OFF");
		SysopInfoReport(id, work);
		if (netBuf.nbflags.MassTransfer) {
		    MakeNetCacheName(temp, thisNet);
		    mkdir(temp);
		}
		putNet(thisNet, &netBuf);
		/* more work here? */
		break;
	    case 'R':
		CloseSysopMenu(id);
		RoomStuff(title);
		id = RegisterSysopMenu("netedit.mnu", NetEditOpts, title);
		break;
	    case 'N':
		id2 = SysopContinual("", "", 2 * NAMESIZE, 5);
		if (GetSystemName(temp, thisNet, id2))
		    strcpy(netBuf.netName, temp);
		SysopCloseContinual(id2);
		break;
	    case 'I':
		id2 = SysopContinual("", "", 2 * NAMESIZE, 5);
		if (GetSystemId(temp, thisNet, id2))
		    strcpy(netBuf.netId, temp);
		SysopCloseContinual(id2);
		break;
	    case 'K':
		if (netBuf.nbflags.normal_mail) {
		    sprintf(work, "There is outgoing mail outstanding.\n ");
		    SysopInfoReport(id, work);
		}
		if (netBuf.nbflags.room_files) {
		    sprintf(work, "There are file requests outstanding.\n ");
			SysopInfoReport(id, work);
		}
		if (SysopGetYesNo(id, "", "Confirm")) {
		    EachSharedRoom(thisNet, KillShared, KillShared, NULL);
		    UpdateSharedRooms();
		    netBuf.nbflags.in_use = FALSE;
		    putNet(place, &netBuf);
		    KillTempFiles(thisNet);
		    KillCacheFiles(thisNet);
		    CloseSysopMenu(id);
		    return;
		}
		break;
	    case 'L':
		netBuf.nbflags.local = SysopGetYesNo(id, "", "Is system local");
		break;
	    case 'P':
		CloseSysopMenu(id);
		mPrintf(" Current passwords\n");
		mPrintf(" Our password: %s\n", netBuf.OurPwd);
		mPrintf(" Their password: %s\n", netBuf.TheirPwd);
		if (getXString("our new password", temp2, NAMESIZE, "", ""))
		    strcpy(netBuf.OurPwd, temp2);
		if (getXString("their new password", temp2, NAMESIZE, "", ""))
		    strcpy(netBuf.TheirPwd, temp2);
		id = RegisterSysopMenu("netedit.mnu", NetEditOpts, title);
		break;
	    case 'M':
		getList(MemberNets, "Nets to add to this system's member list",
								5, TRUE, TRUE);
		getList(MemberNets,"Nets to take off this system's member list",
								5, TRUE, FALSE);
		break;
	    case 'S':
		sprintf(msgBuf.mbtext, "We will be a spine for %s", 
								netBuf.netName);
		if (!(netBuf.nbflags.spine =
					SysopGetYesNo(id, "", msgBuf.mbtext))) {
		    sprintf(msgBuf.mbtext, "%s will be a spine",
							netBuf.netName);
		    netBuf.nbflags.is_spine =
					SysopGetYesNo(id, "", msgBuf.mbtext);
		}
		else
		    netBuf.nbflags.is_spine = FALSE;
		break;
	    case 'O':
		sprintf(work, " System is %sOtherNet\n ",
				(netBuf.nbflags.OtherNet) ? "not " : "");
		SysopInfoReport(id, work);
		netBuf.nbflags.OtherNet = !netBuf.nbflags.OtherNet;
		break;
	    case 'V':
		NodeValues(id);
		break;
	    case 'W':
		netBuf.nbflags.Login = !netBuf.nbflags.Login;
		sprintf(work, "Wait %s", netBuf.nbflags.Login ? "ON" : "OFF");
		SysopInfoReport(id, work);
		break;
#ifdef NEEDED
	    case 'Z':
		netBuf.nbHiRouteInd = (int) getNumber("Kludge value", 0l, 255l);
		netBuf.nbflags.HasRouted = TRUE;
		break;
#endif
	}
    }
}

/*
 * RoomStuff()
 *
 * This function handles the user when selecting 'r' on the net edit menu.
 */
static void RoomStuff(char *title)
{
    char   work[50];
    int    CurRoom;
    int AddSharedRooms(char *data, int arg);
    int DelSharedRooms(char *data, int arg);
    MenuId id;
    char *RoomOpts[] = {
	"Add Rooms\n", "Show Rooms\n", "Unshare Rooms", "X\beXit",
	"",
    };
    char done = FALSE;

    id = RegisterSysopMenu("netrooms.mnu", RoomOpts, title);
    CurRoom = thisRoom;
    while (onLine() && !done) {
	outFlag = OUTOK;
	sprintf(work, "\n (Rooms: %s) edit fn: ", netBuf.netName);
	SysopMenuPrompt(id, work);
	switch (GetSysopMenuChar(id)) {
	case ERROR:
	case 'X':
		done = TRUE;
		break;
	case 'A':
		getList(AddSharedRooms, "Rooms to Share as Backbone", NAMESIZE,
							TRUE, BACKBONE);
		getList(AddSharedRooms,"Rooms to Share as Peon",NAMESIZE,
							TRUE, PEON);
		break;
	case 'U':
		getList(DelSharedRooms,"Rooms to Unshare",NAMESIZE,TRUE, 0);
		break;
	case 'S':
		CloseSysopMenu(id);
		mPrintf("\n ");
		EachSharedRoom(thisNet, DumpRoom, DumpVRoom, NULL);
		if (onConsole) modIn();
		id = RegisterSysopMenu("netrooms.mnu", RoomOpts, title);
		break;
	}
    }
    getRoom(CurRoom);
    UpdateSharedRooms();
    CloseSysopMenu(id);
}

/*
 * AddSharedRooms()
 *
 * This function allows the addition of shared rooms from node editing.
 */
static int AddSharedRooms(char *data, int ShType)
{
    extern VirtualRoom *VRoomTab;
    int roomslot;
    RoomSearch arg;
    SharedRoomData *room;
    char virt = FALSE;

    strcpy(arg.Room, data);
    if (RoomRoutable(&arg)) {
	SetMode(arg.room->room->mode, ShType);
	arg.room->srd_flags |= SRD_DIRTY;
	return TRUE;
    }

    if ((roomslot = FindVirtualRoom(data)) == ERROR) {
	if ((roomslot = roomExists(data)) == ERROR) {
	    SysopPrintf(GetListId, "No such room.\n");
	    return TRUE;
	}
	else if (!roomTab[roomslot].rtflags.SHARED) {
	    SysopPrintf(GetListId, "Room is not shared.\n");
	    return TRUE;
	}
    }
    else virt = TRUE;

    room = NewSharedRoom();
    room->room->srslot   = roomslot;
    room->room->sr_flags = (virt) ? SR_VIRTUAL : 0;
    room->room->lastPeon = VRoomTab[roomslot].vrLoLocal;
    room->room->lastMess = (virt) ? VRoomTab[roomslot].vrLoLD : cfg.newest;
    room->room->srgen    = (virt) ? 0 : roomTab[roomslot].rtgen + (unsigned) 0x8000;
    room->room->netGen   = netBuf.nbGen;
    room->room->netSlot  = thisNet;
    SetMode(room->room->mode, ShType);

    return TRUE;
}

/*
 * DelSharedRooms()
 *
 * This function kills the specified room sharing link, if it exists.
 */
int DelSharedRooms(char *data, int arg)
{
    RoomSearch search;

    strcpy(search.Room, data);
    if (!RoomRoutable(&search)) {
	SysopPrintf(GetListId, "Not found\n");
	return TRUE;
    }
    KillShared(search.room, thisNet, search.room->room->srslot, NULL);
    return TRUE;
}

/*
 * KillTempFiles()
 *
 * This eliminates unneeded temp files for dead node.
 */
void KillTempFiles(int which)
{
    label    temp;
    SYS_FILE temp2;

    sprintf(temp, "%d.ml", which);
    makeSysName(temp2, temp, &cfg.netArea);
    unlink(temp2);
    netBuf.nbflags.normal_mail = FALSE;
    sprintf(temp, "%d.rfl", which);
    makeSysName(temp2, temp, &cfg.netArea);
    unlink(temp2);
    netBuf.nbflags.room_files = FALSE;
    sprintf(temp, "%d.sfl", which);
    makeSysName(temp2, temp, &cfg.netArea);
    unlink(temp2);
    netBuf.nbflags.send_files = FALSE;
    sprintf(temp, "%d.vtx", which);
    makeSysName(temp2, temp, &cfg.netArea);
    unlink(temp2);
    InitVNode(thisNet);
}

/*
 * NodeValues()
 *
 * This function prints out the values for the current node.
 */
void NodeValues(MenuId id)
{
    int	i, first;
    MULTI_NET_DATA h;

    sprintf(msgBuf.mbtext, "\n Node #%d: %s", thisNet, netBuf.netName);
    if (strlen(netBuf.nbShort))
	sprintf(lbyte(msgBuf.mbtext), " (%s)", netBuf.nbShort);

    sprintf(lbyte(msgBuf.mbtext), "\n Id: %s (%slocal @ %s)\n ",
				netBuf.netId,
				netBuf.nbflags.local ? "" : "non",
				SupportedBauds[netBuf.baudCode]);

    if (netBuf.nbflags.ExternalDialer)
	sprintf(lbyte(msgBuf.mbtext), "External Dialer Information: %s\n ",
								netBuf.access);

    if (strlen(netBuf.access) != 0 && !netBuf.nbflags.ExternalDialer)
	sprintf(lbyte(msgBuf.mbtext), "Access: %s\n ", netBuf.access);

    if (netBuf.nbflags.spine)
	sprintf(lbyte(msgBuf.mbtext), "We are a spine\n ");
    else if (netBuf.nbflags.is_spine)
	sprintf(lbyte(msgBuf.mbtext), "This system is a spine\n ");

    if (netBuf.nbflags.OtherNet)
	sprintf(lbyte(msgBuf.mbtext), "OtherNet system.\n ");

    if (netBuf.nbflags.normal_mail || netBuf.nbflags.HasRouted)
	sprintf(lbyte(msgBuf.mbtext), "Outgoing Mail>.\n ");

    if (DomainFlags[thisNet])
	sprintf(lbyte(msgBuf.mbtext), "Outgoing DomainMail.\n ");

    if (AnyRouted(thisNet))
	sprintf(lbyte(msgBuf.mbtext), "Mail routed via this system.\n ");

    if (netBuf.nbflags.room_files)
	sprintf(lbyte(msgBuf.mbtext), "File requests outstanding.\n ");

    if (netBuf.nbflags.send_files)
	sprintf(lbyte(msgBuf.mbtext), "Files to be sent.\n ");

    if (netBuf.nbflags.MassTransfer)
	sprintf(lbyte(msgBuf.mbtext), "Fast Transfers on (using %s).\n ",
				GetCompEnglish(netBuf.nbCompress));

    if (netBuf.nbflags.Login)
	sprintf(lbyte(msgBuf.mbtext), "Wait for Room Prompt on call\n ");

    if (netBuf.MemberNets != 0l) {
	sprintf(lbyte(msgBuf.mbtext), "Assigned to net(s) ");
	for (i = 0, first = 1, h = 1l; i < MAX_NET; i++) {
	    if (h & netBuf.MemberNets) {
		if (!first)
		    sprintf(lbyte(msgBuf.mbtext), ", ");
		else first = FALSE;

		/* Yes - +1. Number the bits starting with 1 */
		sprintf(lbyte(msgBuf.mbtext), "%d", i+1);
	    }
	    h <<= 1;
	}
	sprintf(lbyte(msgBuf.mbtext), ".\n ");
    }
    else sprintf(lbyte(msgBuf.mbtext), "System is disabled.\n ");

    sprintf(lbyte(msgBuf.mbtext), "Last connected: %s\n",
					AbsToReadable(netBuf.nbLastConnect));
    SysopDisplayInfo(id, msgBuf.mbtext, " Values ");
}

/*
 * fileRequest()
 *
 * This handles the administration of requesting files from another system.
 */
void fileRequest()
{
    struct fl_req file_data;
    label    data;
    char     loc[100], *c, *work;
    SYS_FILE fn;
    char     abort;
    FILE     *temp;
    int      place;
    extern char *APPEND_ANY;
    char     ambiguous, again;
    MenuId   id;

    place = thisNet;    /* again, a kludge to be killed later */

    id = SysopContinual("", "", 75, 10);
    sprintf(loc, "\nname of room on %s that has desired file", netBuf.netName);
    SysopContinualString(id, loc, file_data.room, NAMESIZE, 0);
    if (strlen(file_data.room) == 0) {
	SysopCloseContinual(id);
	return;
    }

    SysopContinualString(id, "\nthe file(s)'s name", loc, sizeof loc, 0);
    if (strlen(loc) == 0) {
	SysopCloseContinual(id);
	return;
    }

    ambiguous = !(strchr(loc, '*') == NULL && strchr(loc, '?') == NULL &&
		strchr(loc, ' ') == NULL);

    abort = !netGetAreaV2(id, loc, &file_data, ambiguous);

    if (!abort) {
	sprintf(data, "%d.rfl", place);
	makeSysName(fn, data, &cfg.netArea);
	if ((temp = fopen(fn, APPEND_ANY)) == NULL) {
	    SysopPrintf(id, "Couldn't append to '%s'????", fn);
	}
	else {
	    work = loc;

	    do {
		again = (c = strchr(work, ' ')) != NULL;
		if (again) *c = 0;
		strcpy(file_data.roomfile, work);
		if (ambiguous) strcpy(file_data.filename, work);
		fwrite(&file_data, sizeof (file_data), 1, temp);
		if (again) work = c + 1;
	    } while (again);

	    netBuf.nbflags.room_files = TRUE;
	    putNet(place, &netBuf);
	    fclose(temp);
	}
    }
    SysopCloseContinual(id);
}

/*
 * roomsShared()
 *
 * This function returns TRUE if this system has a room with new data to share
 * (orSomething).
 */
char roomsShared(int slot)
{
    int ROutGoing(SharedRoomData *room, int system, int roomslot, void *d);
    char OutGoing;

	/* We only want to make one "successful" call per 
	voluntary net session */
    if ((inNet == UNTIL_NET || inNet == NORMAL_NET || inNet == ANYTIME_NET ) && 
						pollCall[slot] <= 0)
	return FALSE;

	/*
	 * Rules:
	 * We check each slot of the shared rooms list for this node.  For
	 * each one that is in use, we do the following:
	 *		HOSTS ARE OBSOLETE!
	 * a) if we are regional host for the room and other system is
	 *    a backbone, then don't assume we need to call.
	 * b) if we are backboning the room, check to see what status of this
	 *    room for other system is.
	 *    1) If we are Passive Backbone, then we need not call.
	 *    2) If we are Active Backbone, then do call.
	 *    3) The Regional Host looks screwy.  This may be a bug.
	 * c) If none of the above applies, implies we are a simple Peon, so
	 *    we simply check to see if we have outgoing messages, and if so,
	 *    return TRUE indicating that we need to call; otherwise, continue
	 *    search.
	 *
	 * LATER NOTE: now this is split up due to the use of EachSharedRoom.
	 */
    OutGoing = FALSE;
    EachSharedRoom(slot, ROutGoing, VRNeedCall, &OutGoing);
    return OutGoing;
}

/*
 * ROutGoing()
 *
 * This decides if the system in question needs to be called due to the
 * situation of the rooms.
 */
static int ROutGoing(SharedRoomData *room, int system, int roomslot, void *d)
{
    char *arg;

    arg = d;
    if (GetMode(room->room->mode) == BACKBONE) {
	if (inNet == NORMAL_NET) {
	    *arg = TRUE;
	    return ERROR;
	}
    }
    if (
	roomTab[roomslot].rtlastNetAll > room->room->lastMess ||
	(GetMode(room->room->mode) == BACKBONE &&
		roomTab[roomslot].rtlastNetBB > room->room->lastMess)
	) {
	*arg = TRUE;
	return ERROR;
    }
    if (GetFA(room->room->mode)) {
	*arg = TRUE;
	return ERROR;
    }
    return TRUE;
}

/*
 * DumpRoom()
 *
 * This dumps out information concerning a shared room, such as the status
 * and the message stuff.
 */
int DumpRoom(SharedRoomData *room, int system, int roomslot, void *d)
{
    char cmd, *s1, *s2, *s3, *name, doit;

    mPrintf("%-22sRelationship: ", roomTab[roomslot].rtname);
    Addressing(system, room->room, &cmd, &s1, &s2, &s3, &name, &doit);
    mPrintf(name);

mPrintf(" (last sent=%ld, netlast=%ld, nb=%ld)",
room->room->lastMess, roomTab[roomslot].rtlastNetAll,
roomTab[roomslot].rtlastNetBB);

    if (GetFA(room->room->mode))
	mPrintf("*");

    mPrintf("\n ");

    return TRUE;
}

/*
 * netResult()
 *
 * This will put a message to the net msg holder, building a message for the
 * Aide room.
 */
void netResult(char *msg)
{
    if (netMsg != NULL) {
	fprintf(netMsg, "(%s) %s\n\n", Current_Time(), msg);
	fflush(netMsg);
	UsedNetMsg = TRUE;
    }
}

/*
 * netInfo()
 *
 * This function acquires necessary info from the user when entering a message.
 */
char netInfo(char GetName)
{
    int    cost, flags;
    label  domain = "";
    char   sys[NAMESIZE * 2];
    char   isdomain, *address;
    extern char *NoCredit;
    char   work[45];

    if (thisRoom == MAILROOM) {
	strcpy(sys, msgBuf.mbaddr);
	flags = 0;
	if (GetName) flags |= RNN_ASK;
	if (aide) flags |= RNN_WIDESPEC;
	if (!ReqNodeName("system to send to", sys, domain, flags, &netBuf))
	    return FALSE;

	isdomain = (domain[0] != 0);

	if (strCmpU(sys, ALL_LOCALS) != SAMESTRING) {
	    if (strCmpU(domain, cfg.nodeDomain + cfg.codeBuf) == SAMESTRING &&
		(strCmpU(sys, cfg.nodeName + cfg.codeBuf) == SAMESTRING ||
		strCmpU(sys, UseNetAlias(cfg.nodeName+cfg.codeBuf, TRUE))
							== SAMESTRING)) {
		mPrintf("Hey, that's this system!\n ");
		return FALSE;
	    }
	    cost = (isdomain) ? FindCost(domain) : !netBuf.nbflags.local;
	    if (logBuf.credit < cost) {
		if (HalfSysop()) {
		    logBuf.credit += cost;
		}
		else {
		    mPrintf(NoCredit);
		    return FALSE;
		}
	    }
	    if (isdomain) {
		sprintf(work, "%s _ %s", sys, domain);
		address = work;
	    }
	    else address = sys;

	    if (!isdomain && netBuf.nbflags.OtherNet) {
		sprintf(work, "%s address", netBuf.netName);
		getNormStr(work, msgBuf.mbOther, O_NET_PATH_SIZE, 0);
		if (strlen(msgBuf.mbOther) == 0) return FALSE;
	    }
	}
	else {
	    address = ALL_LOCALS;
	}
    }
    else {
	if (!roomBuf.rbflags.SHARED) {
	    mPrintf("This is not a network room\n ");
	    return FALSE;
	}
	address = R_SH_MARK;
	strcpy(msgBuf.mboname, cfg.codeBuf + cfg.nodeName);
	strcpy(msgBuf.mbdomain, cfg.codeBuf + cfg.nodeDomain);
    }
    strcpy(msgBuf.mbaddr, address);
    return TRUE;
}

/*
 * killConnection()
 *
 * Zaps carrier for network.
 */
void killConnection(char *s)
{
    HangUp(TRUE);
    modStat = haveCarrier = FALSE;
    while (MIReady()) Citinp();    /* Clear buffer of garbage */
}

/*
 * setPoll()
 *
 * This allows us to make sure we don't poll hosts too often during a net
 * session.
 */
void setPoll()
{
    int rover;

    pollCall = GetDynamic(cfg.netSize);
    for (rover = 0; rover < cfg.netSize; rover++) {
	pollCall[rover] = 1;
    }
}

/*
 * makeCall()
 *
 * This handles the actual task of dialing the modem.
 */
static int makeCall(char EchoErr, MenuId id)
{
    char  call[80];
    label blip1;
    int   bufc, result;
    char  buf[30], c, viable;
    char  ourArea[4], targetArea[4];

    while (MIReady()) Citinp();

    AreaCode(netBuf.netId, targetArea);
    AreaCode(cfg.nodeId + cfg.codeBuf, ourArea);

    if (!netBuf.nbflags.ExternalDialer) {
	setNetCallBaud(netBuf.baudCode);
	normId(netBuf.netId, blip1);

#ifdef POSSIBLE
	strcpy(call, cfg.codeBuf + cfg.DialPrefixes[minimum(netBuf.baudCode, cfg.sysBaud)]);
#else
	strcpy(call, GetPrefix(minimum(netBuf.baudCode, cfg.sysBaud)));
#endif

	if (strlen(netBuf.access) != 0) {	/* don't need to check extdial*/
	    strcat(call, netBuf.access);
	}
	else if (!netBuf.nbflags.local) {
	    strcat(call, "1");

	    /* LD within same area code? (courtesy farokh irani) */
	    if (strCmp(targetArea, ourArea) == SAMESTRING)
		strcat(call, blip1 + 5);
	    else
		strcat(call, blip1 + 2);
	}
	else {
	    /* local but different area codes?  (e.g., NYC) */
	    /* again courtesy farokh irani */
	    if (strCmp(targetArea, ourArea) != SAMESTRING)
		strcat(call, blip1 + 2);
	    else
		strcat(call, blip1 + 5);
	}
	strcat(call, cfg.codeBuf + cfg.netSuffix);

	switch (RottenDial(call)) {
	    case FALSE: moPuts(call); break;
	    case TRUE:  break;
	    case ERROR: return FALSE;
	}

	for (startTimer(WORK_TIMER), bufc = 0, viable = TRUE;
	    chkTimeSince(WORK_TIMER) < ((netBuf.nbflags.local) ? 40l :
								cfg.LD_Delay)
							&& viable;) {
	    if (gotCarrier()) break;
		/* Parse incoming string from modem -- call progress detection */
	    if (KBReady()) viable = FALSE;
	    if (MIReady()) {
		if ((c = Citinp()) == '\r') {
		    buf[bufc] = 0;
		    switch ((result = ResultVal(buf))) {
			case R_NODIAL:
			case R_NOCARR:
			case R_BUSY:
			    if (EchoErr) splitF(netLog, "(%s) ", buf);
			    else SysopPrintf(id, "\n%s", buf);
			    viable = FALSE; break;
			case R_300:
			case R_1200:
			case R_2400:
			case R_4800:
			case R_9600:
			case R_14400:
			case R_19200:
			    if ((minimum(netBuf.baudCode, cfg.sysBaud)) != result) {
				setNetCallBaud(result);
				splitF(netLog, "(Mismatch: %s, adjusting.)\n", buf);
			    }
			    break;
		    }
		    bufc = 0;
		}
		else {
		    if (bufc > 28) bufc = 0;
		    else if (c != '\n') {
			buf[bufc++] = c;
		    }
		}
	    }
	}
	if (gotCarrier())
	    return TRUE;
    }
    else {
	return DialExternal(&netBuf);
    }
    return FALSE;
}

#ifdef LUNATIC
ExplainNeed(int i, MULTI_NET_DATA x)
{
    splitF(netLog, "slot %d i%d isp%d sp%d MN%ld nm%d rf%d sf%d shared%d\n", i,
	  netTab[i].ntflags.in_use,
	  netTab[i].ntflags.is_spine,
	  netTab[i].ntflags.spine,
	  netTab[i].ntMemberNets & x, netTab[i].ntflags.normal_mail,
	  netTab[i].ntflags.room_files,
	  netTab[i].ntflags.send_files, roomsShared(i));
    splitF(netLog, "DF%d HR%d anyr%d\n",
	    DomainFlags[i], netTab[i].ntflags.HasRouted, AnyRouted(i));
}
#endif

#define SpineSet(i) (netTab[i].ntflags.is_spine && !(netTab[i].ntMemberNets & PRIORITY_MAIL))
/*
 * needToCall()
 *
 * This is responsible for checking to see if we need to call this system.
 * Basically, here's what the rules are:
 *
 * Is this account in use?
 * Is this account on one of the eligible net ('x' parameter)?
 * Is this account not a spine and not an OtherNet system?
 *
 * If this system is a spine and we're not doing an anytime-net session
 * and we haven't had a successful connection yet then call.
 *
 * If this system has normal mail, room file requests, send file requests,
 * rooms that need to share (outgoing messages), domain mail outgoing, mail
 * routing and hasn't been connected with yet, then call.
 * 
 * I'm not entirely sure what the reference to Priority Mail signifies in
 * this mess.
 */
int needToCall(int i, MULTI_NET_DATA x)
{
    extern SListBase Routes;
				/* first check for permission to call   */
    if (netTab[i].ntflags.in_use &&     /* account in use		*/
       (netTab[i].ntMemberNets & x) &&  /* system is member of net      */
	!SpineSet(i) &&
       !netTab[i].ntflags.OtherNet) {   /* system not OtherNet		*/
				/* check for requirement to call	*/
	if (Unstable != NULL && Unstable[i] >= cfg.MaxNotStable)
	    return FALSE;
	if (netTab[i].ntflags.spine &&
		!(netTab[i].ntMemberNets & PRIORITY_MAIL) &&
	   (inNet == NON_NET || ((inNet == NORMAL_NET || inNet == UNTIL_NET) &&
							pollCall[i] == 1)))
	    return TRUE;
				/* now check for need to call     */
	if (
	    (SearchList(&Routes, &i) == NULL &&
	    netTab[i].ntflags.normal_mail ||	/* normal outgoing mail?*/
	    netTab[i].ntflags.HasRouted) ||
	    netTab[i].ntflags.room_files ||	/* request files ?	*/
	    netTab[i].ntflags.send_files ||	/* send files ?		*/
	    DomainFlags[i] ||			/* domain mail to send?	*/
	    roomsShared(i) ||			/* rooms to share?	*/
	    AnyRouted(i) ||
	    (netTab[i].ntflags.HasRouted &&
	    (inNet == NON_NET || 
	    ((inNet == NORMAL_NET || inNet == ANYTIME_NET ||
				inNet == UNTIL_NET) &&
			pollCall[i] == 1)))) {
#ifdef NEEDED
printf("\nmail=%d, HR=%d, SL=%lx, AnyRouted=%d\n",
netTab[i].ntflags.normal_mail,
netTab[i].ntflags.HasRouted,
SearchList(&Routes, &i), AnyRouted(i));
#endif
	    return TRUE;
	}
    }
    return FALSE;
}

/*
 * AnyCallsNeeded()
 *
 * Do we need to make any calls 'tall?
 */
char AnyCallsNeeded(MULTI_NET_DATA whichNets)
{
    int searcher;

    for (searcher = 0; searcher < cfg.netSize; searcher++)
	if (needToCall(searcher, whichNets)) return TRUE;

    return FALSE;
}

/*
 * ReqNodeName()
 *
 * This function is a general request for node name from user.  It supports
 * various options for prompting or not prompting for the name, allowing the
 * input of '&L', allow display of nodelists, etc. (see the code).  The
 * function will validate the choice, query again if appropriate, handle
 * domains, and do a getNet of the node if necessary.
 */
char ReqNodeName(char *prompt, label target, label domain, int flags,
							NetBuffer *nBuf)
{
    extern char *ALL_LOCALS;
    char sysname[2 * NAMESIZE], dup, work[2 * NAMESIZE];
    int  slot;

    do {
	slot = ERROR;
	if (domain != NULL) domain[0] = 0;
	/* Allows function to act as validator only */
	if ((flags & RNN_ASK)) {
	    getString(prompt, sysname, 2 * NAMESIZE, QUEST_SPECIAL);
	}
	else strcpy(sysname, target);

	NormStr(sysname);

	/* Empty line implies operation abort. */
	if (strlen(sysname) == 0) {
	    strcpy(target, sysname);
	    return FALSE;
	}

	/* If "&L" entered and is acceptable ... */
	if ((flags & RNN_WIDESPEC) && strCmpU(sysname, ALL_LOCALS) == SAMESTRING) {
	    strcpy(target, sysname);
	    return TRUE;
	}

	/* Questioning frown */
	if (sysname[0] == '?') {
	    writeNet((flags & RNN_DISPLAY), FALSE);   /* write out available nets */
	    if ((flags & RNN_WIDESPEC)) 
		mPrintf("'&L' == Local Systems Announcement\n ");
	}
	/* finally, must be real system name so seeeeearch for it! */
	else if ((slot = searchNameNet(sysname, nBuf)) != ERROR) {
	    strcpy(target, nBuf->netName);	/* aesthetics */
	    if (nBuf->nbflags.local || nBuf->nbflags.RouteLock) {
		return TRUE;		/* Yup */
	    }
	}
	if (domain != NULL && SystemInSecondary(sysname, domain, &dup)) {
	    if (dup) {	/* oops */
		if (slot != ERROR) return TRUE;
			/* do it as a double if, not claused */
		if ((flags & RNN_ASK)) mPrintf(DupDomain, sysname);
	    }
	    else {
		strcpy(target, sysname);	/* aesthetics */
		return TRUE;
	    }
	}
	if (slot != ERROR) return TRUE;
	if (sysname[0] != '?') {
	    if (!(flags & RNN_QUIET)) {
		sprintf(work, "%s not listed.\n", sysname); /* Nope */
		if ((flags & RNN_SYSMENU)) SysopError(NO_MENU, work);
		else	   mPrintf(work);
	    }
	}
    } while (!(flags & RNN_ONCE) && (flags & RNN_ASK)); /* This controls if we ask repeatedly or only once */

    return FALSE;       /* And if we get here, we definitely are a failure */
}

/*
 * NetValidate()
 *
 * This will return TRUE if net privs are go, FALSE otherwise.
 */
char NetValidate(char talk)
{
    if (!cfg.BoolFlags.netParticipant) {
	if (talk)
	    mPrintf("This Citadel is not participating in the net.\n ");
	return FALSE;
    }

    if ( !loggedIn ||
		(!logBuf.lbflags.NET_PRIVS &&
		(!roomBuf.rbflags.AUTO_NET || !roomBuf.rbflags.ALL_NET) )) {
	if (talk) 
	    mPrintf("\n Sorry, you don't have net privileges.\n ");
	return FALSE;
    }
    return TRUE;
}

/*
 * FindRouteIndex()
 *
 * This will find the next route filename in sequence.
 */
int FindRouteIndex(int slot)
{
    label temp;
    SYS_FILE newfn;

    sprintf(temp, "R%d", slot);
    makeSysName(newfn, temp, &cfg.netArea);

    return FindNextFile(newfn);
}

typedef struct {
    int count;
    char *str;
} SR_Arg;
/*
 * ParticipatingNodes()
 *
 * This function prepares a string indicating who shares the current room with
 * us.
 */
void ParticipatingNodes(char *target)
{
    int    node;
    SR_Arg arg;
    char   *c;
    int ShowSharedRoomName(SharedRoomData *room, int system, 
						int roomslot, void *arg);

    sprintf(lbyte(target), ". This room is shared with: ");
    arg.count = 0;
    arg.str   = target;
    for (node = 0; node < cfg.netSize; node++) {
	EachSharedRoom(node, ShowSharedRoomName, NULL, &arg);
    }

    if (arg.count) {	/* this eliminates the trailing comma */
	c = lbyte(target);
	c -= 2;
	*c = 0;
    }
}

/*
 * ShowSharedRoomName()
 *
 * This appends the name of this shared room to a string.
 */
int ShowSharedRoomName(SharedRoomData *room, int system, int roomslot, void *d)
{
    SR_Arg *arg;
    char *name;
    char commnd, *s1, *s2, *s3, doit;

    arg = d;
    if (roomslot == thisRoom) {
	getNet(system, &netBuf);
	arg->count++;
	Addressing(system, room->room, &commnd, &s1, &s2, &s3, &name, &doit);
	sprintf(lbyte(arg->str), "%s (%s), ", netBuf.netName, name);
	return ERROR;
    }
    return TRUE;
}

/*
 * AreaCode()
 *
 * This function extracts the area code from the node id.
 */
void AreaCode(char *Id, char *Target)
{
	int i, j;

	for (i = j = 0; j < 3 && Id[i]; i++)
		if (isdigit(Id[i]))
			Target[j++] = Id[i];

	Target[j] = 0;
}

/*
 * NetInit()
 *
 * This function does network initialization: Cache handling, network recovery
 * (in case of crash during netting), etc...
 */
void NetInit()
{
    int rover;
    extern SListBase Routes;
    SYS_FILE routes;

    SpecialMessage("Network Initialization");
    VirtInit();
    VortexInit();
    /* we never need do this again */
    makeSysName(routes, "routing.sys", &cfg.netArea);
    MakeList(&Routes, routes, NULL);

    DomainInit(TRUE);

    for (rover = 0; rover < cfg.netSize; rover++)
	if (netTab[rover].ntMemberNets & PRIORITY_MAIL) PriorityMail++;

    RecoverNetwork();

    SpecialMessage("");
}

/*
 * MakeNetted()
 *
 * This function will make a message into a net message.  This is userland
 * code called when an aide wants to make a non-netted message into a netted
 * message.
 */
char MakeNetted(int m)
{
    if (findMessage(roomBuf.msg[m].rbmsgLoc, roomBuf.msg[m].rbmsgNo, TRUE)) {
	getMsgStr(getMsgChar, msgBuf.mbtext, MAXTEXT);	/* get balance */
	strcpy(msgBuf.mboname, cfg.codeBuf + cfg.nodeName);
	strcpy(msgBuf.mbdomain, cfg.codeBuf + cfg.nodeDomain);
	strcpy(msgBuf.mbaddr, R_SH_MARK);
	DelMsg(TRUE, m);
	putMessage(NULL, 0);
	return NETTED;
    }
    return NO_CHANGE;
}

/*
 * freeUNS()
 *
 * This function is purportedly a free function for a list.  In actuality,
 * it also runs a net session for each.
 */
void freeUNS(TwoNumbers *netdata)
{
    int yr, dy, hr, mn, mon, secs, milli;

    if (!onLine()) {
	getRawDate(&yr, &mon, &dy, &hr, &mn, &secs, &milli);
	netController((hr * 60) + mn, (int) netdata->second,
			(1l << (netdata->first - 1)), UNTIL_NET, 0);
    }
    free(netdata);
}

/*
 * HasOutgoing()
 *
 * This function checks to see if the given room as specified by the system,
 * index pair has outgoing messages in either cache or message form.
 *
 * NB: This code may not be OK.
 */
char HasOutgoing(SharedRoom *room)
{
    if (GetFA(room->mode)) return TRUE;
    if (roomTab[netRoomSlot(room)].rtlastMessage > room->lastMess)
	return TRUE;
    return FALSE;
}
