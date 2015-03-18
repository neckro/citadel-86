/*
 *				Dom-init.C
 *
 * Domain handing code.
 */

#define NET_INTERNALS
#define NET_INTERFACE

#include "ctdl.h"

/*
 *				history
 *
 * 93Jun10 HAW  Created.
 */

/*
 *				contents
 *
 */

#define MAPSYS		"map.sys"

/*
 * Required Global Variables
 */

/*
 * Current domain mail in #DOMAINAREA -- map of directories to domain names.
 */
void *CheckDomain(), *EatDomainLine(char *line);
int CmpDomain();
SListBase DomainMap    = { NULL, CheckDomain, CmpDomain, free, EatDomainLine };
char *DomainFlags      = NULL;

extern CONFIG    cfg;

/*
 * This code is responsible for managing the domain part of Citadel-86.
 *
 * DOMAIN DIRECTORY:
 *    The domain directory consists of a single file and a collection of
 * directories.
 *
 * MAP.SYS: this contains a mapping between the directory names and the
 * domain mail they purport to store.  Format:
 *
 * <number><space><domain><highest filename>
 *
 * DIRECTORIES (1, 2,...): these contain the actual outgoing mail, plus a file
 * named "info" which will hold the name of the domain (for rebuilding
 * purposes).
 */

/*
 * UtilDomainInit()
 *
 * This function initializes the domain stuff.  It's called from NetInit(),
 * addNetNode(), and from RationalizeDomains, so it has to open the relevant
 * files only once.
 *
 * Appropriate lists are created, call out flags set, etc.
 */
void UtilDomainInit(char FirstTime)
{
    void HandleExistingDomain();
    SYS_FILE temp;

    if (FirstTime) {
	makeSysName(temp, MAPSYS, &cfg.domainArea);
	MakeList(&DomainMap, temp, NULL);
    }
    else
	if (DomainFlags != NULL) free(DomainFlags);

    if (cfg.netSize != 0)
	DomainFlags = GetDynamic(cfg.netSize);	/* auto-zeroed */

    /* This will also set the temporary call out flags, eh?		*/
}

/*
 * EatDomainLine()
 *
 * This function parses a line from map.sys.  Map.sys is kept in the
 * #DOMAINAREA and maps the from the name of each domain with outstanding
 * mail and the directory it resides in.  Directories are numerically
 * designated.  Format of each line is
 *
 * <directory name> <domain name> <last file number>
 *
 * This function, since it's used by calls to MakeList, returns a void pointer
 * to the assembled data structure.
 */
static void *EatDomainLine(char *line)
{
    DomainDir *data;
    char *c;

    data = GetDynamic(sizeof *data);
    data->UsedFlag   = FALSE;
    data->TargetSlot = -1;
    if ((c = strchr(line, ' ')) == NULL) {
	free(data);
	return NULL;
    }
    *c++ = 0;
    data->MapDir = atoi(line);
    line = c;
    if ((c = strchr(line, ' ')) == NULL) {
	free(data);
	return NULL;
    }
    *c++ = 0;
    strCpy(data->Domain, line);
    data->HighFile = atoi(c);
    return data;
}

char CheckNumber = FALSE;	/* double duty switcheroonie	*/
/*
 * CheckDomain()
 *
 * This is used to look for a domain in a list.  The search can either be on
 * the directory number or the domain name.
 */
static void *CheckDomain(DomainDir *d, char *str)
{
    int *i;

    if (!CheckNumber) {
	if (strCmpU(d->Domain, str) == SAMESTRING) return d;
    }
    else {
	i = (int *) str;
	if (*i == d->MapDir) return d;
    }
    return NULL;
}

/*
 * CmpDomain()
 *
 * This function is used in the list of domains with mail to keep the domains
 * in numerical order.  The list is kept in numerical order to make MAP.SYS
 * a bit more tractable.
 */
static int CmpDomain(DomainDir *s, DomainDir *t)
{
    return s->MapDir - t->MapDir;
}


/*
 * UpdateMap()
 *
 * This function is charged with updating MAP.SYS with new information.
 * Most of the work is done in WriteDomainMap().
 */
void UpdateMap()
{
    SYS_FILE temp;
    void WriteDomainMap();
    FILE *fd;

    makeSysName(temp, MAPSYS, &cfg.domainArea);
    if ((fd = fopen(temp, WRITE_TEXT)) != NULL) {
	RunListA(&DomainMap, WriteDomainMap, fd);
	fclose(fd);
    }
}

/*
 * WriteDomainMap()
 *
 * This writes out an entry of the domain map.
 */
static void WriteDomainMap(DomainDir *data, FILE *fd)
{
    fprintf(fd, "%d %s %d\n", data->MapDir, data->Domain, data->HighFile);
}
