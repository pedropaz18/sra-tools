/*==============================================================================
*
*                            PUBLIC DOMAIN NOTICE
*               National Center for Biotechnology Information
*
*  This software/database is a "United States Government Work" under the
*  terms of the United States Copyright Act.  It was written as part of
*  the author's official duties as a United States Government employee and
*  thus cannot be copyrighted.  This software/database is freely available
*  to the public for use. The National Library of Medicine and the U.S.
*  Government have not placed any restriction on its use or reproduction.
*
*  Although all reasonable efforts have been taken to ensure the accuracy
*  and reliability of the software and data, the NLM and the U.S.
*  Government do not and cannot warrant the performance or results that
*  may be obtained by using this software or data. The NLM and the U.S.
*  Government disclaim all warranties, express or implied, including
*  warranties of performance, merchantability or fitness for any particular
*  purpose.
*
*  Please cite the author in any work or product based on this material.
*
* ==============================================================================
*
*/

#include "test-sra-priv.h" /* PrintOS */

#include <kapp/main.h> /* KMain */

#include <kfg/config.h> /* KConfig */
#include <kfg/kart.h> /* Kart */
#include <kfg/repository.h> /* KRepositoryMgr */

#include <kfs/dyload.h> /* KDyld */

#include <klib/time.h> /* KTimeMsStamp */

#include <kns/endpoint.h> /* KEndPoint */
#include <kns/kns-mgr-priv.h> /* KNSManagerMakeReliableClientRequest */

#include <vfs/manager.h> /* VFSManager */
#include <vfs/path.h> /* VPath */
#include <vfs/resolver.h> /* VResolver */

#include <sra/sraschema.h> /* VDBManagerMakeSRASchema */

#include <vdb/database.h> /* VDatabase */
#include <vdb/dependencies.h> /* VDBDependencies */
#include <vdb/manager.h> /* VDBManager */
#include <vdb/report.h> /* ReportSetVDBManager */
#include <vdb/schema.h> /* VSchemaRelease */
#include <vdb/table.h> /* VDBManagerOpenTableRead */
#include <vdb/vdb-priv.h> /* VDBManagerSetResolver */

#include <kns/ascp.h> /* ascp_locate */
#include <kns/http.h>
#include <kns/manager.h>
#include <kns/manager-ext.h> /* KNSManagerNewReleaseVersion */
#include <kns/stream.h>

#include <kdb/manager.h> /* kptDatabase */

#include <kfs/directory.h> /* KDirectory */
#include <kfs/file.h> /* KFile */

#include <klib/data-buffer.h> /* KDataBuffer */
#include <klib/debug.h> /* DBGMSG */
#include <klib/log.h> /* KLogHandlerSet */
#include <klib/out.h> /* KOutMsg */
#include <klib/printf.h> /* string_vprintf */
#include <klib/rc.h>
#include <klib/report.h> /* ReportForceFinalize */
#include <klib/sra-release-version.h>
#include <klib/text.h> /* String */

#include <sysalloc.h>

#include <assert.h>
#include <ctype.h> /* isprint */
#include <stdlib.h> /* calloc */
#include <string.h> /* memset */

VFS_EXTERN rc_t CC VResolverProtocols ( VResolver * self,
    VRemoteProtocols protocols );

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define RELEASE(type, obj) do { rc_t rc2 = type##Release(obj); \
    if (rc2 != 0 && rc == 0) { rc = rc2; } obj = NULL; } while (false)

typedef enum {
    eCfg            = 1,
    eResolve        = 2,
    eDependMissing  = 4,
    eDependAll      = 8,
    eNetwork       = 16,
    eVersion       = 32,
    eNewVersion    = 64,
    eOpenTable    = 128,
    eOpenDB       = 256,
    eType         = 512,
    eNcbiReport  = 1024,
    eOS          = 2048,
    eAscp        = 4096,
    eAscpVerbose = 8192,
    eNgs        = 16384,
    eAll        = 32768,
    eNoTestArg  = 65536,
} Type;
typedef uint16_t TTest;
static const char TESTS[] = "crdDwsSoOtnufFga";
typedef struct {
    KConfig *cfg;
    KDirectory *dir;
    KNSManager *knsMgr;

    const VDBManager *mgr;
    uint32_t projectId;

    VFSManager *vMgr;
    const KRepositoryMgr *repoMgr;
    VResolver *resolver;
    VSchema *schema;

    TTest tests;
    bool recursive;
    bool noVDBManagerPathType;
    bool noRfs;
    bool full;
    bool xml;
    bool network;

    bool allowCaching;
    VResolverEnableState cacheState;
} Main;

const char UsageDefaultName[] = "test-sra";

rc_t CC UsageSummary(const char *prog_name) {
    return KOutMsg(
        "Usage:\n"
        "  quick check mode:\n"
        "   %s -Q [ name... ]\n\n"
        "  full test mode:\n"
        "   %s [+crdDa] [-crdDa] [-R] [-N] [-C] [-X <type>] [options] "
                                                   "name [ name... ]\n"
        , prog_name, prog_name);
}

rc_t CC Usage(const Args *args) {
    rc_t rc2 = 0;

    const char *progname, *fullpath;
    rc_t rc = ArgsProgram(args, &fullpath, &progname);
    if (rc != 0) {
        progname = fullpath = UsageDefaultName;
    }

    rc2 = UsageSummary(progname);
    if (rc == 0 && rc2 != 0) {
        rc = rc2;
    }

    rc2 = KOutMsg("\n"
        "Test [SRA] object, resolve it, print dependencies, configuration\n\n"
        "[+tests] - add tests\n"
        "[-tests] - remove tests\n\n");
    if (rc == 0 && rc2 != 0) {
        rc = rc2;
    }
    rc2 = KOutMsg(
        "Tests:\n"
        "  s - print SRA software information\n"
        "  S - print SRA software information and last SRA toolkit versions\n"
        "  u - print operation system information\n"
        "  c - print configuration\n"
        "  n - print NCBI error report\n"
        "  f - print ascp information\n"
        "  F - print verbose ascp information\n"
        "  t - print object types\n"
        "  w - run network test\n");
    if (rc == 0 && rc2 != 0) {
        rc = rc2;
    }
    rc2 = KOutMsg(
        "  r - call VResolver\n"
        "  d - call ListDependencies(missing)\n"
        "  D - call ListDependencies(all)\n"
        "  o - call VDBManagerOpenTableRead(object)\n"
        "  O - call VDBManagerOpenDBRead(object)\n"
        "  a - all tests except VDBManagerOpen...Read and verbose ascp\n\n");
    if (rc == 0 && rc2 != 0) {
        rc = rc2;
    }
    rc2 = KOutMsg(
        "In quick check mode - the base checks are run;\n"
        "in full test mode (default) all the tests are available.\n\n"
        "In full mode, if no tests were specified then all tests will be run.\n"
        "\n"
        "-X < xml | text > - whether to generate well-formed XML\n"
        "-R - check objects recursively\n"
        "-N - do not call VDBManagerPathType\n"
        "-C - do not disable caching (default: from configuration)\n\n"
        "More options:\n");
    if (rc == 0 && rc2 != 0) {
        rc = rc2;
    }

    HelpOptionsStandard();

    return rc;
}

static bool testArg(const char *arg, TTest *testOn, TTest *testOff) {
    int j = 1;
    TTest *res = NULL;

    assert(arg && testOn && testOff);
    if (arg[0] != '+' && arg[0] != '-') {
        return false;
    }

    if (arg[0] == '-' &&
        arg[1] != '\0' && strchr(TESTS, arg[1]) == NULL)
    {
        return false;
    }

    res = arg[0] == '-' ? testOff : testOn;

    for (j = 1; arg[j] != '\0'; ++j) {
        char *c = strchr(TESTS, arg[j]);
        if (c != NULL) {
            int64_t offset = c - TESTS;
            *res |= 1 << offset;
        }
    }

    return true;
}

static TTest Turn(TTest in, TTest tests, bool on) {
    TTest c = 1;
    for (c = 1; c < eAll; c <<= 1) {
        if (tests & c) {
            if (on) {
                in |= c;
            }
            else {
                in &= ~c;
            }
        }
    }
    return in;
}

static TTest processTests(TTest testsOn, TTest testsOff) {
    TTest tests = 0;

    bool allOn = false;
    bool allOff = false;

    if (testsOn & eAll && testsOff & eAll) {
        testsOn &= ~eAll;
        testsOff &= ~eAll;
    }
    else if (testsOn & eAll) {
        allOn = true;
    }
    else if (testsOff & eAll) {
        allOff = true;
    }

    if (allOn) {
        tests = ~0;
        tests = Turn(tests, testsOff, false);
        tests = Turn(tests, eOpenTable, testsOn & eOpenTable);
        tests = Turn(tests, eOpenDB, testsOn & eOpenDB);
        tests = Turn(tests, eOpenDB, testsOn & eAscpVerbose);
    }
    else if (allOff) {
        tests = Turn(tests, testsOn, true);
    }
    else if (testsOn != 0 || testsOff != 0) {
        tests = Turn(tests, testsOff, false);
        tests = Turn(tests, testsOn, true);
    }
    else /* no test argument provided */ {
        tests = ~0;
        tests = Turn(tests, eOpenTable, false);
        tests = Turn(tests, eOpenDB, false);
        tests = Turn(tests, eAscpVerbose, false);
    }

    if (tests & eAscpVerbose) {
        tests = Turn(tests, eAscp, true);
    }

    if (tests & eNcbiReport) {
        tests = Turn(tests, eCfg, false);
    }

    return tests;
} 

static void MainMakeQuick(Main *self) {
    assert(self);
#if 0
    self->tests = eCurl;
#endif
}

static void MainAddTest(Main *self, Type type) {
    assert(self);
    self->tests |= type;
}

static bool MainHasTest(const Main *self, Type type) {
    return (self->tests & type) != 0;
}

static void MainPrint(const Main *self) {
    return;

    assert(self);

    if (MainHasTest(self, eCfg)) {
        KOutMsg("eCfg\n");
    }

    if (MainHasTest(self, eResolve)) {
        KOutMsg("eResolve\n");
    }

    if (MainHasTest(self, eDependMissing)) {
        KOutMsg("eDependMissing\n");
    }

    if (MainHasTest(self, eDependAll)) {
        KOutMsg("eDependAll\n");
    }

#if 0
    if (MainHasTest(self, eCurl)) {
        KOutMsg("eCurl\n");
    }
#endif

    if (MainHasTest(self, eVersion)) {
        KOutMsg("eVersion\n");
    }

    if (MainHasTest(self, eNewVersion)) {
        KOutMsg("eNewVersion\n");
    }

    if (MainHasTest(self, eOpenTable)) {
        KOutMsg("eOpenTable\n");
    }

    if (MainHasTest(self, eOpenDB)) {
        KOutMsg("eOpenDB\n");
    }

    if (MainHasTest(self, eType)) {
        KOutMsg("eType\n");
    }

    if (MainHasTest(self, eNcbiReport)) {
        KOutMsg("eNcbiReport\n");
    }

    if (MainHasTest(self, eAll)) {
        KOutMsg("eAll\n");
    }

    if (MainHasTest(self, eNoTestArg)) {
        KOutMsg("eNoTestArg\n");
    }
}

static rc_t MainInitObjects(Main *self) {
    rc_t rc = 0;

    VResolver *resolver = NULL;

    if (rc == 0) {
        rc = KDirectoryNativeDir(&self->dir);
    }

    if (rc == 0) {
        rc = VDBManagerMakeRead(&self->mgr, NULL);
        ReportSetVDBManager(self->mgr);
    }

    if (rc == 0) {
        rc = KConfigMake(&self->cfg, NULL);
    }

    if (rc == 0) {
        rc = KConfigMakeRepositoryMgrRead(self->cfg, &self->repoMgr);
        if (rc == 0) {
             bool has_project_id = self->projectId != 0;
             if (has_project_id) {
                const KRepository *repository = NULL;
                rc = KRepositoryMgrGetProtectedRepository(
                    self->repoMgr, self->projectId, &repository);
                if (rc == 0) {
                    VResolver *resolver = NULL;
                    rc = KRepositoryMakeResolver(
                        repository, &resolver, self->cfg);
                    if (rc == 0) {
                         rc = VDBManagerSetResolver(self->mgr, resolver);
                    }
                    RELEASE(VResolver, resolver);
                }
                RELEASE(KRepository, repository);
             }
        }
    }

    if (rc == 0) {
        rc = VFSManagerMake(&self->vMgr);
    }

    if (rc == 0) {
        rc = VFSManagerGetResolver(self->vMgr, &resolver);
    }

    if (rc == 0) {
        rc = VFSManagerGetKNSMgr(self->vMgr, &self->knsMgr);
    }

    if (rc == 0) {
        if (!self->allowCaching) {
            self->cacheState = VResolverCacheEnable(resolver, vrAlwaysDisable);
        }
    }

    if (rc == 0) {
        rc = VFSManagerMakeResolver(self->vMgr, &self->resolver, self->cfg);
    }

    if (rc == 0) {
        rc = VDBManagerMakeSRASchema(self->mgr, &self->schema);
        if (rc != 0) {
            OUTMSG(("VDBManagerMakeSRASchema() = %R\n", rc));
        }
    }

    RELEASE(VResolver, resolver);

    return rc;
}

static
void _MainInit(Main *self, int argc, char *argv[], int *argi, char **argv2)
{
    int i = 0;

    bool hasTestArg = false;

    TTest testsOn = 0;
    TTest testsOff = 0;

    assert(self && argv && argi && argv2);

    *argi = 0;
    argv2[(*argi)++] = argv[0];

    for (i = 1; i < argc; ++i) {
        if (!testArg(argv[i], &testsOn, &testsOff)) {
            argv2[(*argi)++] = argv[i];
        }
        else {
            hasTestArg = true;
        }
    }

    self->tests = processTests(testsOn, testsOff);

    if (hasTestArg) {
        self->tests &= ~eNoTestArg;
    }
    else {
        self->tests |= eNoTestArg;
    }

    MainPrint(self);
}

static char** MainInit(Main *self, int argc, char *argv[], int *argi) {
    char **argv2 = calloc(argc, sizeof *argv2);

    assert(self);

    memset(self, 0, sizeof *self);

    if (argv2 != NULL) {
        _MainInit(self, argc, argv, argi, argv2);
    }

    return argv2;
}

static rc_t MainCallCgiImpl(const Main *self,
    const KConfigNode *node, const char *acc)
{
    rc_t rc = 0;
    String *url = NULL;
    KHttpRequest *req = NULL;
    KDataBuffer result;
    memset(&result, 0, sizeof result);
    assert(self && node && acc);
    if (rc == 0) {
        rc = KConfigNodeReadString(node, &url);
        if (url == NULL) {
            rc = RC(rcExe, rcNode, rcReading, rcString, rcNull);
        }
    }
    if (rc == 0) {
        rc = KNSManagerMakeRequest(self->knsMgr,
            &req, 0x01000000, NULL, url->addr);
    }
    if (rc == 0) {
        rc = KHttpRequestAddPostParam ( req, "acc=%s", acc );
    }
    if (rc == 0) {
        KHttpResult *rslt;
        rc = KHttpRequestPOST ( req, & rslt );
        if ( rc == 0 )
        {
            uint32_t code;
            size_t msg_size;
            char msg_buff [ 256 ];
            rc = KHttpResultStatus(rslt,
                & code, msg_buff, sizeof msg_buff, & msg_size);
            if ( rc == 0 && code == 200 )
            {
                KStream * response;
                rc = KHttpResultGetInputStream ( rslt, & response );
                if ( rc == 0 )
                {
                    size_t num_read;
                    size_t total = 0;
                    KDataBufferMakeBytes ( & result, 4096 );
                    while ( 1 )
                    {
                        uint8_t *base;
                        uint64_t avail = result . elem_count - total;
                        if ( avail < 256 )
                        {
                            rc = KDataBufferResize
                                (&result, result.elem_count + 4096);
                            if ( rc != 0 )
                                break;
                        }

                        base = result . base;
                        rc = KStreamRead(response, &base[total],
                            result.elem_count - total, &num_read);
                        if ( rc != 0 )
                        {
                            if ( num_read > 0 )
                                rc = 0;
                            else
                                break;
                        }

                        if ( num_read == 0 )
                            break;

                        total += num_read;
                    }

                    KStreamRelease ( response );
                }
            }

            KHttpResultRelease ( rslt );
        }
    }
    if (rc == 0) {
        const char *start = (const void*)(result.base);
        size_t size = KDataBufferBytes(&result);
        if (*(start + size) != '\0') {
            rc = RC(rcExe, rcString, rcParsing, rcString, rcUnexpected);
        }
        if (strstr(start, "200|ok") == NULL) {
            rc = RC(rcExe, rcString, rcParsing, rcParam, rcIncorrect);
        }
    }
    KDataBufferWhack(&result);
    RELEASE(KHttpRequest, req);
    free(url);
    if (rc == 0) {
        OUTMSG(("NCBI access: ok\n"));
    }
    else {
        OUTMSG(("ERROR: cannot access NCBI Website\n"));
    }
    return rc;
}

static rc_t MainCallCgi(const Main *self,
    const KConfigNode *node, const char *acc)
{
    rc_t rc = 0;
    int i = 0, retryOnFailure = 2;
    for (i = 0; i < retryOnFailure; ++i) {
        rc = MainCallCgiImpl(self, node, acc);
        if (rc == 0) {
            break;
        }
        DBGMSG(DBG_KNS, DBG_FLAG(DBG_KNS_ERR), (
            "@@@@@@@@2: MainCallCgi %d/%d = %R\n", i + 1, retryOnFailure, rc));
    }
    return rc;
}

#define rcResolver   rcTree
static rc_t MainQuickResolveQuery(const Main *self, const char *acc) {
    rc_t rc = 0;
    VPath *query = NULL;
    const VPath *remote = NULL;
    const VPath *cache = NULL;
    assert(self && acc);
    rc = VFSManagerMakePath(self->vMgr, &query, "%s", acc);
    if (rc == 0) {
        if (!self->allowCaching) {
            VResolverCacheEnable(self->resolver, self->cacheState);
        }
        rc = VResolverQuery(self->resolver, eProtocolHttp, query,
            NULL, &remote, &cache);
        if (!self->allowCaching) {
            VResolverCacheEnable(self->resolver, vrAlwaysDisable);
        }
    }
    if (rc == 0) {
        if (remote != NULL) {
            OUTMSG(("remote location: ok\n"));
        }
        else {
            OUTMSG(("ERROR: cannot resolve remote location\n"));
            rc = RC(rcExe, rcResolver, rcResolving, rcPath, rcNotFound);
        }
        if (cache != NULL) {
            OUTMSG(("cache location: ok\n"));
        }
        else {
            OUTMSG(("ERROR: cannot resolve cache location\n"));
            rc = RC(rcExe, rcResolver, rcResolving, rcPath, rcNotFound);
        }
    }
    else {
        OUTMSG(("ERROR: cannot resolve public accession\n"));
    }
    RELEASE(VPath, cache);
    RELEASE(VPath, remote);
    RELEASE(VPath, query);
    return rc;
}

static rc_t MainQuickCheck(const Main *self) {
    rc_t rc = 0;
    rc_t rc2 = 0;
    const char acc[] = "SRR000001";
    const char path[] = "/repository/remote/protected/CGI/resolver-cgi";
    const KConfigNode *node = NULL;
    assert(self);
    rc = KConfigOpenNodeRead(self->cfg, &node, "%s", path);
    if (rc == 0) {
        OUTMSG(("configuration: found\n"));
    }
    else {
        OUTMSG(("ERROR: configuration not found or incomplete\n"));
    }
    if (rc == 0) {
        rc_t rc3 = MainCallCgi(self, node, acc);
        if (rc3 != 0 && rc2 == 0) {
            rc2 = rc3;
        }

        rc3 = MainQuickResolveQuery(self, acc);
        if (rc3 != 0 && rc2 == 0) {
            rc2 = rc3;
        }
    }
    RELEASE(KConfigNode, node);
    if (rc2 != 0 && rc == 0) {
        rc = rc2;
    }
    return rc;
}

static rc_t MainPrintConfig(const Main *self) {
    rc_t rc = 0;

    assert(self);

    if (rc == 0) {
        rc = KConfigPrint(self->cfg, 0);
        if (rc != 0) {
            OUTMSG(("KConfigPrint() = %R", rc));
        }
        OUTMSG(("\n"));
    }

    return rc;
}

static
rc_t _KDBPathTypePrint(const char *head, KPathType type, const char *tail)
{
    rc_t rc = 0;
    rc_t rc2 = 0;
    assert(head && tail);
    {
        rc_t rc2 = OUTMSG(("%s", head));
        if (rc == 0 && rc2 != 0) {
            rc = rc2;
        }
    }
    switch (type) {
        case kptNotFound:
            rc2 = OUTMSG(("NotFound"));
            break;
        case kptBadPath:
            rc2 = OUTMSG(("BadPath"));
            break;
        case kptFile:
            rc2 = OUTMSG(("File"));
            break;
        case kptDir:
            rc2 = OUTMSG(("Dir"));
            break;
        case kptCharDev:
            rc2 = OUTMSG(("CharDev"));
            break;
        case kptBlockDev:
            rc2 = OUTMSG(("BlockDev"));
            break;
        case kptFIFO:
            rc2 = OUTMSG(("FIFO"));
            break;
        case kptZombieFile:
            rc2 = OUTMSG(("ZombieFile"));
            break;
        case kptDataset:
            rc2 = OUTMSG(("Dataset"));
            break;
        case kptDatatype:
            rc2 = OUTMSG(("Datatype"));
            break;
        case kptDatabase:
            rc2 = OUTMSG(("Database"));
            break;
        case kptTable:
            rc2 = OUTMSG(("Table"));
            break;
        case kptIndex:
            rc2 = OUTMSG(("Index"));
            break;
        case kptColumn:
            rc2 = OUTMSG(("Column"));
            break;
        case kptMetadata:
            rc2 = OUTMSG(("Metadata"));
            break;
        case kptPrereleaseTbl:
            rc2 = OUTMSG(("PrereleaseTbl"));
            break;
        default:
            rc2 = OUTMSG(("unexpectedFileType(%d)", type));
            assert(0);
            break;
    }
    if (rc == 0 && rc2 != 0) {
        rc = rc2;
    }
    {
        rc_t rc2 = OUTMSG(("%s", tail));
        if (rc == 0 && rc2 != 0) {
            rc = rc2;
        }
    }
    return rc;
}

static bool isprintString(const unsigned char *s) {
    assert(s);

    while (*s) {
        int c = *(s++);
        if (!isprint(c)) {
            return false;
        }
    }

    return true;
}

static rc_t printString(const char *s) {
    rc_t rc = 0;

    const unsigned char *u = (unsigned char*)s;

    assert(u);

    if (isprintString(u)) {
        return OUTMSG(("%s", u));
    }

    while (*u) {
        rc_t rc2 = 0;
        int c = *(u++);
        if (isprint(c)) {
            rc2 = OUTMSG(("%c", c));
        }
        else {
            rc2 = OUTMSG(("\\%03o", c));
        }
        if (rc == 0 && rc2 != 0) {
            rc = rc2;
        }
    }

    return rc;
}

static rc_t _KDirectoryReport(const KDirectory *self,
    const char *name, int64_t *size, KPathType *type, bool *alias)
{
    rc_t rc = 0;
    const KFile *f = NULL;

    bool dummyB = false;
    int64_t dummy = 0;

    KPathType dummyT = kptNotFound;;
    if (type == NULL) {
        type = &dummyT;
    }

    if (alias == NULL) {
        alias = &dummyB;
    }
    if (size == NULL) {
        size = &dummy;
    }

    *type = KDirectoryPathType(self, "%s", name);

    if (*type & kptAlias) {
        OUTMSG(("alias|"));
        *type &= ~kptAlias;
        *alias = true;
    }

    rc = _KDBPathTypePrint("", *type, " ");

    if (*type == kptFile) {
        rc = KDirectoryOpenFileRead(self, &f, "%s", name);
        if (rc != 0) {
            OUTMSG(("KDirectoryOpenFileRead("));
            printString(name);
            OUTMSG((")=%R ", rc));
        }
        else {
            uint64_t sz = 0;
            rc = KFileSize(f, &sz);
            if (rc != 0) {
                OUTMSG(("KFileSize(%s)=%R ", name, rc));
            }
            else {
                OUTMSG(("%,lu ", sz));
                *size = sz;
            }
        }
    }
    else {
        OUTMSG(("- "));
    }

    RELEASE(KFile, f);

    return rc;
}

static rc_t _VDBManagerReport(const VDBManager *self,
    const char *name, KPathType *type)
{
    KPathType dummy = kptNotFound;;

    if (type == NULL) {
        type = &dummy;
    }

    *type = VDBManagerPathType(self, "%s", name);

    *type &= ~kptAlias;

    return _KDBPathTypePrint("", *type, " ");
}

static rc_t _VDBManagerReportRemote
    (const VDBManager *self, const char *name, const VSchema *schema)
{
    bool notFound = false;
    const VDatabase *db = NULL;
    const VTable *tbl = NULL;
    rc_t rc = VDBManagerOpenDBRead(self, &db, schema, name);
    if (rc == 0) {
        RELEASE(VDatabase, db);
        return _KDBPathTypePrint("", kptDatabase, " ");
    }
    else if (GetRCState(rc) == rcNotFound) {
        notFound = true;
    }
    rc = VDBManagerOpenTableRead(self,  &tbl, schema, name);
    if (rc == 0) {
        RELEASE(VTable, tbl);
        return _KDBPathTypePrint("", kptTable, " ");
    }
    else if (GetRCState(rc) == rcNotFound) {
        notFound = true;
    }
    if (notFound) {
        return OUTMSG(("NotFound "));
    }
    else {
        return OUTMSG(("Unknown "));
    }
}

static
rc_t _KDirectoryFileHeaderReport(const KDirectory *self, const char *path)
{
    rc_t rc = 0;
    char hdr[8] = "";
    const KFile *f = NULL;
    size_t num_read = 0;
    size_t i = 0;

    assert(self && path);

    rc = KDirectoryOpenFileRead(self, &f, "%s", path);
    if (rc != 0) {
        OUTMSG(("KDirectoryOpenFileRead(%s) = %R\n", path, rc));
        return rc;
    }

    rc = KFileReadAll(f, 0, hdr, sizeof hdr, &num_read);
    if (rc != 0) {
        OUTMSG(("KFileReadAll(%s, 8) = %R\n", path, rc));
    }

    for (i = 0; i < num_read && rc == 0; ++i) {
        if (isprint(hdr[i])) {
            OUTMSG(("%c", hdr[i]));
        }
        else {
            OUTMSG(("\\X%02X", hdr[i]));
        }
    }
    OUTMSG((" "));

    RELEASE(KFile, f);
    return rc;
}

static rc_t MainReport(const Main *self,
    const char *name, int64_t *size, KPathType *type, bool *alias)
{
    rc_t rc = 0;

    assert(self);

    rc = _KDirectoryReport(self->dir, name, size, type, alias);

    if (!self->noVDBManagerPathType) {
        _VDBManagerReport(self->mgr, name, type);
    }

    if (type != NULL && *type == kptFile) {
        _KDirectoryFileHeaderReport(self->dir, name);
    }

    return rc;
}

static rc_t MainReportRemote(const Main *self, const char *name, int64_t size) {
    rc_t rc = 0;

    assert(self);

    OUTMSG(("%,lu ", size));

    if (!self->noVDBManagerPathType) {
        _VDBManagerReportRemote(self->mgr, name, self->schema);
    }

    return rc;
}

static rc_t MainOpenAs(const Main *self, const char *name, bool isDb) {
    rc_t rc = 0;
    const VTable *tbl = NULL;
    const VDatabase *db = NULL;

    assert(self);

    if (isDb) {
        rc = VDBManagerOpenDBRead(self->mgr, &db, self->schema, "%s", name);
        ReportResetDatabase(name, db);
        OUTMSG(("VDBManagerOpenDBRead(%s) = ", name));
    }
    else {
        rc = VDBManagerOpenTableRead(self->mgr, &tbl, self->schema, "%s", name);
        ReportResetTable(name, tbl);
        OUTMSG(("VDBManagerOpenTableRead(%s) = ", name));
    }
    if (rc == 0) {
        OUTMSG(("OK\n"));
    }
    else {
        OUTMSG(("%R\n", rc));
    }
    RELEASE(VDatabase, db);
    RELEASE(VTable, tbl);
    return rc;
}

#define rcResolver   rcTree
static bool NotFoundByResolver(rc_t rc) {
    if (GetRCModule(rc) == rcVFS) {
        if (GetRCTarget(rc) == rcResolver) {
            if (GetRCContext(rc) == rcResolving) {
                if (GetRCState(rc) == rcNotFound) {
                    return true;
                }
            }
        }
    }
    return false;
}

typedef enum {
      ePathLocal
    , ePathRemote
    , ePathCache
} EPathType;
static rc_t MainPathReport(const Main *self, rc_t rc, const VPath *path,
    EPathType type, const char *name, const VPath* remote, int64_t *size,
    bool fasp, const KFile *fRemote)
{
    const char *eol = "\n";
    const char *bol = "";

    assert(self);

    if (self->xml) {
        eol = "<br/>\n";
        bol = "      ";
    }

    switch (type) {
        case ePathLocal:
            OUTMSG(("%sLocal:\t\t  ", bol));
            break;
        case ePathRemote:
            OUTMSG(("%sRemote %s:\t  ", bol, fasp ? "FaspHttp" : "HttpFasp"));
            break;
        case ePathCache:
            OUTMSG(("%sCache %s:\t  ", bol, fasp ? "FaspHttp" : "HttpFasp"));
            if (remote == NULL) {
                OUTMSG(("skipped%s", eol));
                return rc;
            }
            break;
    }
    if (rc != 0) {
        if (NotFoundByResolver(rc)) {
            OUTMSG(("not found%s", eol));
            rc = 0;
        }
        else {
            switch (type) {
                case ePathLocal:
                    OUTMSG(("VResolverLocal(%s) = %R%s", name, rc, eol));
                    break;
                case ePathRemote:
                    OUTMSG(("VResolverRemote(%s) = %R%s", name, rc, eol));
                    break;
                case ePathCache:
                    OUTMSG(("VResolverCache(%s) = %R%s", name, rc, eol));
                    break;
            }
        }
    }
    else {
        const char ncbiFile[] = "ncbi-file:";
        size_t sz = sizeof ncbiFile - 1;
        const String *s = NULL;
        char buffer[PATH_MAX] = "";
        const char *fPath = buffer;
        rc_t rc = VPathMakeString(path, &s);
        if (rc != 0 || s == NULL || s->addr == NULL ||
            string_cmp(s->addr, sz, ncbiFile, sz, (uint32_t)sz) == 0)
        {
            rc = VPathReadPath(path, buffer, sizeof buffer, NULL);
        }
        else {
            fPath = s->addr;
        }
        if (rc == 0) {
            OUTMSG(("%s ", fPath));
            switch (type) {
                case ePathLocal:
                case ePathCache:
                    rc = MainReport(self, fPath, size, NULL, NULL);
                    break;
                case ePathRemote: {
                    uint64_t sz = 0;
                    if (!fasp && fRemote != NULL) {
                        rc = KFileSize(fRemote, &sz);
                        if (rc != 0) {
                            OUTMSG(("KFileSize(%s)=%R ", name, rc));
                        }
                        else {
                            MainReportRemote(self, fPath, sz);
                            *size = sz;
                        }
                    }
                    break;
                }
            }
            OUTMSG(("%s", eol));
        }
        else {
            const char *s = "";
            switch (type) {
                case ePathLocal:
                    s = "Local";
                    break;
                case ePathCache:
                    s = "Cache";
                    break;
                case ePathRemote:
                    s = "Remote";
                    break;
                default:
                    assert(0);
                    break;
            }
            OUTMSG(("VPathMakeUri(VResolver%s(%s)) = %R%s", s, name, rc, eol));
        }
        if (type == ePathCache) {
            char cachecache[PATH_MAX] = "";
            if (rc == 0) {
                rc = string_printf(cachecache,
                    sizeof cachecache, NULL, "%s.cache", fPath);
                if (rc != 0) {
                    OUTMSG(("string_printf(%s) = %R%s", fPath, rc, eol));
                }
            }

            if (rc == 0) {
                OUTMSG((
                    "%sCache.cache %s: ", bol, fasp ? "FaspHttp" : "HttpFasp"));
                OUTMSG(("%s ", cachecache));
                rc = MainReport(self, cachecache, NULL, NULL, NULL);
                OUTMSG(("%s", eol));
            }
        }
        free((void*)s);
    }
    return rc;
}

static rc_t MainResolveLocal(const Main *self, const VResolver *resolver,
    const char *name, const VPath *acc, int64_t *size)
{
    rc_t rc = 0;

    const VPath* local = NULL;

    assert(self);

    if (resolver == NULL) {
        resolver = self->resolver;
    }

    rc = VResolverLocal(resolver, acc, &local);
    rc = MainPathReport(self,
        rc, local, ePathLocal, name, NULL, NULL, false, NULL);

    RELEASE(VPath, local);

    return rc;
}

static rc_t MainResolveRemote(const Main *self, VResolver *resolver,
    const char *name, const VPath* acc, const VPath **remote, int64_t *size,
    bool fasp)
{
    rc_t rc = 0;

    const KFile* f = NULL;

    assert(self && size && remote);

    if (resolver == NULL) {
        resolver = self->resolver;
    }

    rc = VResolverRemote(resolver,
        fasp ? eProtocolFaspHttp : eProtocolHttp, acc, remote);

    if (rc == 0) {
        rc_t rc = 0;
        String str;
        memset(&str, 0, sizeof str);
        rc = VPathGetScheme(*remote, &str);
        if (rc != 0) {
            OUTMSG(("VPathGetScheme(%S) = %R\n", *remote, rc));
        }
        else {
            String fasp;
            CONST_STRING(&fasp, "fasp");
            if (StringCompare(&str, &fasp) != 0) {
                char path_str[8192];
                rc = VPathReadUri(*remote, path_str, sizeof path_str, NULL);
                if (rc != 0) {
                    OUTMSG(("VPathReadUri(%S) = %R\n", *remote, rc));
                }
                else {
                    rc = KNSManagerMakeHttpFile
                        (self->knsMgr, &f, NULL, 0x01010000, path_str);
                }
            }
        }
    }

    rc = MainPathReport(self,
        rc, *remote, ePathRemote, name, NULL, size, fasp, f);
    RELEASE(KFile, f);

    return rc;
}

static rc_t MainResolveCache(const Main *self, const VResolver *resolver,
    const char *name, const VPath* remote, bool fasp)
{
    rc_t rc = 0;
    const VPath* cache = NULL;

    assert(self);
    
    if (remote == NULL) {
        rc = MainPathReport(self,
            rc, cache, ePathCache, name, remote, NULL, fasp, NULL);
    }
    else {
        if (resolver == NULL) {
            resolver = self->resolver;
        }

        if (!self->allowCaching) {
            VResolverCacheEnable(resolver, self->cacheState);
        }
        rc = VResolverQuery(resolver, fasp ? eProtocolFasp : eProtocolHttp,
            remote, NULL, NULL, &cache);
        rc = MainPathReport(self,
            rc, cache, ePathCache, name, remote, NULL, fasp, NULL);

        RELEASE(VPath, cache);
        if (!self->allowCaching) {
            VResolverCacheEnable(resolver, vrAlwaysDisable);
        }
    }

    return rc;
}

typedef enum {
      eQueryAll
    , eQueryLocal
    , eQueryRemote
} EQueryType ;
static rc_t VResolverQueryByType(const Main *self, const VResolver *resolver,
    const char *name, const VPath *query,
    bool fasp, VRemoteProtocols protocols, EQueryType type)
{
    const char *eol = "\n";
    const char *bol = "";

    rc_t rc = 0;

    const VPath *local = NULL;
    const VPath *remote = NULL;
    const VPath *cache = NULL;
    const VPath **pLocal = NULL;
    const VPath **pRemote = NULL;
    const VPath **pCache = NULL;

    assert(self);

    if (self->xml) {
        eol = "<br/>\n";
        bol = "      ";
    }

    switch (type) {
        case eQueryLocal:
        case eQueryAll:
            pLocal = &local;
        default:
            break;
    }

    switch (type) {
        case eQueryRemote:
        case eQueryAll:
            pRemote = &remote;
            pCache = &cache;
        default:
            break;
    }

    if (pCache != NULL && !self->allowCaching) {
        VResolverCacheEnable(resolver, vrAlwaysEnable);
    }

    rc = VResolverQuery(resolver, protocols, query, pLocal, pRemote, pCache);
    OUTMSG(("%sVResolverQuery(%s, %s, local%s, remote%s, cache%s)= %R%s",
        bol, name, protocols == eProtocolHttp ? "Http" : "FaspHttp", 
        pLocal == NULL ? "=NULL" : "", pRemote == NULL ? "=NULL" : "",
        pCache == NULL ? "=NULL" : "", rc, eol));
    if (rc == 0) {
        if (local != NULL) {
/*          rc2 =*/ MainPathReport(self,
                rc, local, ePathLocal, name, NULL, NULL, false, NULL);
        }
        if (remote != NULL) {
/*          rc2 =*/ MainPathReport(self,
                rc, remote, ePathRemote, name, NULL, NULL, fasp, NULL);
        }
        if (cache != NULL) {
            MainPathReport(self,
                rc, cache, ePathCache, name, remote, NULL, fasp, NULL);
        }
    }

    OUTMSG(("\n"));

    RELEASE(VPath, local);
    RELEASE(VPath, remote);
    RELEASE(VPath, cache);

    if (pCache != NULL && !self->allowCaching) {
        VResolverCacheEnable(resolver, self->cacheState);
    }

    return rc;
}

static rc_t MainResolveQuery(const Main *self, const VResolver *resolver,
    const char *name, const VPath *query, bool fasp)
{
    const char root[] = "Timer";
    rc_t rc = 0;
    rc_t rc2 = 0;
    KTimeMs_t time = 0;
    KTimeMs_t start_time = 0;

    VRemoteProtocols protocols = eProtocolHttp;
    if (fasp) {
        protocols = eProtocolFaspHttp;
    }
    else {
        protocols = eProtocolHttp;
    }

    if (resolver == NULL) {
        resolver = self->resolver;
    }

    rc2 = VResolverQueryByType(self, resolver, name, query, fasp, protocols,
        eQueryAll);
    if (rc2 != 0 && rc == 0) {
        rc = rc2;
    }

    if (self->xml) {
        OUTMSG(("    <%s>\n", root));
    }
    else {
        OUTMSG(("time started...\n"));
    }
    start_time = KTimeMsStamp();

    rc2 = VResolverQueryByType(self, resolver, name, query, fasp, protocols,
        eQueryLocal);
    if (rc2 != 0 && rc == 0) {
        rc = rc2;
    }

    rc2 = VResolverQueryByType(self, resolver, name, query, fasp, protocols,
        eQueryRemote);
    if (rc2 != 0 && rc == 0) {
        rc = rc2;
    }

/* TODO check vdbcache */

    time = KTimeMsStamp() - start_time;
    if (self->xml) {
        OUTMSG(("      <Time time=\"%d ms\"/>\n", time));
        OUTMSG(("    </%s>\n", root));
    }
    else {
        OUTMSG(("...elapsed time=\"%d ms\"\n\n", time));
    }

    return rc;
}

static rc_t _KartItemToVPath(const KartItem *self,
    const VFSManager *vfs, VPath **path)
{
    uint64_t oid = 0;
    rc_t rc = KartItemItemIdNumber(self, &oid);
    if (rc == 0) {
        rc = VFSManagerMakeOidPath(vfs, path, (uint32_t)oid);
    }
    else {
        char path_str[PATH_MAX] = "";
        const String *accession = NULL;
        rc = KartItemAccession(self, &accession);
        if (rc == 0) {
            rc =
                string_printf(path_str, sizeof path_str, NULL, "%S", accession);
        }
        if (rc == 0) {
            rc = VFSManagerMakePath(vfs, path, path_str);
        }
    }
    return rc;
}

static rc_t perform_read_test(void) { return 0; }

static rc_t MainResolve(const Main *self, const KartItem *item,
    const char *name, int64_t *localSz, int64_t *remoteSz, bool refseqCtx)
{
    const char root[] = "Resolve";
    rc_t rc = 0;

    VPath* acc = NULL;
    VResolver* resolver = NULL;

    assert(self);

    if (self->xml) {
        OUTMSG(("<%s>\n", root));
    }

    if (rc == 0) {
        if (item == NULL) {
            if (refseqCtx) {
                rc = VFSManagerMakePath(self->vMgr, &acc,
                    "ncbi-acc:%s?vdb-ctx=refseq", name);
            }
            else {
                rc = VFSManagerMakePath(self->vMgr, &acc, "%s", name);
            }
            if (rc != 0) {
                OUTMSG(("VFSManagerMakePath(%s) = %R\n", name, rc));
            }
        }
        else {
            const KRepository *p_protected = NULL;
            uint64_t project = 0;
            if (rc == 0) {
                rc = KartItemProjIdNumber(item, &project);
                if (rc != 0) {
                    OUTMSG(("KartItemProjectIdNumber = %R\n", rc));
                }
            }
            if (rc == 0) {
                rc = _KartItemToVPath(item, self->vMgr, &acc);
                if (rc != 0) {
                    OUTMSG(("Invalid kart file row: %R\n", rc));
                }
            }
            if (rc == 0) {
                rc = KRepositoryMgrGetProtectedRepository(self->repoMgr, 
                    (uint32_t)project, &p_protected);
                if (rc != 0) {
                    OUTMSG((
                        "KRepositoryMgrGetProtectedRepository(%d) = %R\n",
                        project, rc));
                }
            }
            if (rc == 0) {
                rc = KRepositoryMakeResolver(p_protected, &resolver,
                    self->cfg);
                if (rc != 0) {
                    OUTMSG((
                        "KRepositoryMakeResolver(%d) = %R\n", project, rc));
                }
            }
            RELEASE(KRepository, p_protected);
        }
    }

    if (rc == 0) {
        const String *id = NULL;
        const char root[] = "Query";
        rc_t rc2 = 0;

        const VPath* remote = NULL;

        const char *attr = name;
        if (attr == NULL) {
            rc2 = KartItemItemId(item, &id);
            if (rc2 != 0) {
                OUTMSG(("KartItemItemId = %R\n", rc2));
                rc2 = 0;
                attr = "Accession";
            }
        }

        if (self->xml) {
            if (attr != NULL) {
                OUTMSG(("  <%s name=\"%s\">\n", root, attr));
            }
            else {
                OUTMSG(("  <%s name=\"%S\">\n", root, id));
            }
        }

        rc = VDBManagerSetResolver(self->mgr, resolver);

        rc2 = MainResolveLocal(self, resolver, name, acc, localSz);
        if (rc2 != 0 && rc == 0) {
            rc = rc2;
        }

        rc2 = MainResolveRemote(self, resolver, name, acc, &remote, remoteSz,
            false);
        if (rc2 != 0 && rc == 0) {
            rc = rc2;
        }

        rc2 = MainResolveCache(self, resolver, name, remote, false);
        if (rc2 != 0 && rc == 0) {
            rc = rc2;
        }

        RELEASE(VPath, remote);

        rc2 = MainResolveRemote(self, resolver, name, acc, &remote, remoteSz,
            true);
        if (rc2 != 0 && rc == 0) {
            rc = rc2;
        }

        rc2 = MainResolveCache(self, resolver, name, remote, true);
        if (rc2 != 0 && rc == 0) {
            rc = rc2;
        }

        OUTMSG(("\n"));

        rc2 = MainResolveQuery(self, resolver, name, acc, false);
        if (rc2 != 0 && rc == 0) {
            rc = rc2;
        }

        rc2 = MainResolveQuery(self, resolver, name, acc, true);
        if (rc2 != 0 && rc == 0) {
            rc = rc2;
        }

        RELEASE(VPath, remote);

        if (MainHasTest(self, eNetwork)) {
            perform_read_test();
        }

        if (self->xml) {
            OUTMSG(("  </%s>\n", root));
        }
    }

    RELEASE(VPath, acc); 
    RELEASE(VResolver, resolver);

    if (self->xml) {
        OUTMSG(("</%s>\n", root));
    }

    return rc;
}

static
rc_t MainDepend(const Main *self, const char *name, bool missing)
{
    const char root[] = "Dependencies";
    const char *eol = "\n";
    rc_t rc = 0;

    const VDatabase *db = NULL;
    const VDBDependencies* dep = NULL;
    uint32_t count = 0;

    assert(self);
    eol = self->xml ? "<br/>\n" : "\n";

    if (self->xml) {
        OUTMSG(("<%s>\n", root));
    }

    if (rc == 0) {
        rc = VDBManagerOpenDBRead(self->mgr, &db, self->schema, "%s", name);
        if (rc != 0) {
            if (rc == SILENT_RC(rcVFS,rcMgr,rcOpening,rcDirectory,rcNotFound)) {
                return 0;
            }
            OUTMSG(("VDBManagerOpenDBRead(%s) = %R\n", name, rc));
        }
    }

    if (rc == 0) {
        if (!self->allowCaching) {
            VResolverCacheEnable(self->resolver, self->cacheState);
        }
        rc = VDatabaseListDependenciesWithCaching(db,
            &dep, missing, !self->allowCaching);
        if (rc != 0) {
            OUTMSG(("VDatabaseListDependencies(%s, %s) = %R%s",
                name, missing ? "missing" : "all", rc, eol));
        }
        if (!self->allowCaching) {
            VResolverCacheEnable(self->resolver, vrAlwaysDisable);
        }
    }

    if (rc == 0) {
        rc = VDBDependenciesCount(dep, &count);
        if (rc != 0) {
            OUTMSG(("VDBDependenciesCount(%s, %s) = %R\n",
                name, missing ? "missing" : "all", rc));
        }
        else {
            OUTMSG(("VDBDependenciesCount(%s)=%d\n",
                missing ? "missing" : "all", count));
        }
    }

    if (rc == 0) {
        uint32_t i = 0;
        rc_t rc2 = 0;
        for (i = 0; i < count; ++i) {
            const char root[] = "Dependency";
            bool b = true;
            const char *s = NULL;
            const char *seqId = NULL;
            KPathType type = kptNotFound;

            if (self->xml) {
                OUTMSG(("<%s>\n", root));
            }

            OUTMSG((" %6d\t", i + 1));

            rc2 = VDBDependenciesSeqId(dep, &seqId, i);
            if (rc2 == 0) {
                OUTMSG(("seqId=%s,", seqId));
            }
            else {
                OUTMSG(("VDBDependenciesSeqId(%s, %s, %i)=%R ",
                    name, missing ? "missing" : "all", i, rc2));
                if (rc == 0) {
                    rc = rc2;
                }
            }

            rc2 = VDBDependenciesName(dep, &s, i);
            if (rc2 == 0) {
                OUTMSG(("name=%s,", s));
            }
            else {
                OUTMSG(("VDBDependenciesName(%s, %s, %i)=%R ",
                    name, missing ? "missing" : "all", i, rc2));
                if (rc == 0) {
                    rc = rc2;
                }
            }

            rc2 = VDBDependenciesCircular(dep, &b, i);
            if (rc2 == 0) {
                OUTMSG(("circular=%s,", b ? "true" : "false"));
            }
            else {
                OUTMSG(("VDBDependenciesCircular(%s, %s, %i)=%R ",
                    name, missing ? "missing" : "all", i, rc2));
                if (rc == 0) {
                    rc = rc2;
                }
            }

            rc2 = VDBDependenciesType(dep, &type, i);
            if (rc2 == 0) {
                rc2 = _KDBPathTypePrint("type=", type, ",");
                if (rc2 != 0 && rc == 0) {
                    rc = rc2;
                }
            }
            else {
                OUTMSG(("VDBDependenciesType(%s, %s, %i)=%R ",
                    name, missing ? "missing" : "all", i, rc2));
                if (rc == 0) {
                    rc = rc2;
                }
            }

            rc2 = VDBDependenciesLocal(dep, &b, i);
            if (rc2 == 0) {
                OUTMSG(("local=%s,", b ? "local" : "remote"));
            }
            else {
                OUTMSG(("VDBDependenciesLocal(%s, %s, %i)=%R ",
                    name, missing ? "missing" : "all", i, rc2));
                if (rc == 0) {
                    rc = rc2;
                }
            }

            rc2 = VDBDependenciesPath(dep, &s, i);
            if (rc2 == 0) {
                OUTMSG(("%s\tpathLocal=%s,", eol, s == NULL ? "notFound" : s));
            }
            else {
                OUTMSG(("VDBDependenciesPath(%s, %s, %i)=%R ",
                    name, missing ? "missing" : "all", i, rc2));
                if (rc == 0) {
                    rc = rc2;
                }
            }

            rc2 = VDBDependenciesPathRemote(dep, &s, i);
            if (rc2 == 0) {
                if (s == NULL) {
                    OUTMSG(("%s\tpathRemote: notFound ", eol));
                }
                else {
                    OUTMSG(("%s\tpathRemote: %s ", eol, s));
                    if (!self->noRfs) {
                        const KFile *f = NULL;
                        rc2 = KNSManagerMakeHttpFile ( self->knsMgr, & f, NULL, 0x01010000, s );
                        if (rc2 != 0) {
                            OUTMSG(("KNSManagerMakeHttpFile=%R", rc2));
                            if (rc == 0) {
                                rc = rc2;
                            }
                        }
                        if (rc2 == 0) {
                            uint64_t sz = 0;
                            rc2 = KFileSize(f, &sz);
                            if (rc2 != 0) {
                                OUTMSG(("KFileSize=%R", rc2));
                                if (rc == 0) {
                                    rc = rc2;
                                }
                            }
                            else {
                                OUTMSG(("%,lu", sz));
                            }
                        }
                        RELEASE(KFile, f);
                    }
                }
            }
            else {
                OUTMSG(("VDBDependenciesPathRemote(%s, %s, %i)=%R ",
                    name, missing ? "missing" : "all", i, rc2));
                if (rc == 0) {
                    rc = rc2;
                }
            }

            rc2 = VDBDependenciesPathCache(dep, &s, i);
            if (rc2 == 0) {
                OUTMSG(("%s\tpathCache: %s ", eol, s == NULL ? "notFound" : s));
                if (s != NULL) {
                    char cachecache[PATH_MAX] = "";
                    rc2 = MainReport(self, s, NULL, NULL, NULL);
                    OUTMSG(("%s", eol));
                    if (rc == 0) {
                        rc = rc2;
                    }
                    if (rc2 == 0) {
                        rc2 = string_printf(cachecache,
                            sizeof cachecache, NULL, "%s.cache", s);
                        if (rc2 != 0) {
                            if (rc == 0) {
                                rc = rc2;
                            }
                            OUTMSG(("string_printf(%s) = %R%s", s, rc2, eol));
                        }
                    }
                    if (rc == 0) {
                        OUTMSG(("\tpathCache.cache: "));
                        OUTMSG(("%s ", cachecache));
                        rc = MainReport(self, cachecache, NULL, NULL, NULL);
                    }
                }
            }
            else {
                OUTMSG(("VDBDependenciesPathCache(%s, %s, %i)=%R ",
                    name, missing ? "missing" : "all", i, rc2));
                if (rc == 0) {
                    rc = rc2;
                }
            }
            if (MainHasTest(self, eResolve) && seqId != NULL) {
                int64_t remoteSz = 0;
                OUTMSG(("%s", eol));

     /* ignore returned value :
        resolver's errors are detected but not reported as test-sra's failure */
                MainResolve(self, NULL, seqId, NULL, &remoteSz, true);
            }

            if (self->xml) {
                OUTMSG(("</%s>\n", root));
            }
            else {
                OUTMSG(("%s", eol));
            }
        }
    }

    RELEASE(VDBDependencies, dep);
    RELEASE(VDatabase, db);

    if (self->xml) {
        OUTMSG(("</%s>\n", root));
    }

    return rc;
}

static rc_t MainPrintAscp(const Main *self) {
    rc_t rc = 0;
    bool status = false;
    const char *b = self->xml ? "  <Ascp>\n"  : "";
    const char *e = self->xml ? "  </Ascp>\n" : "\n";
    const char *ascp_bin = NULL;
    const char *private_file = NULL;
    AscpOptions opt;
    memset(&opt, 0, sizeof opt);
    assert(self);
    if (MainHasTest(self, eAscpVerbose)) {
        status = true;
    }
    OUTMSG(("%s", b));
    rc = ascp_locate(&ascp_bin, &private_file, true, status);
    if (rc != 0) {
        OUTMSG(("ascp_locate = %R\n", rc));
    }
    if (rc == 0) {
        const char *b = self->xml ? "    <Ascp>"       : "ascp    : ";
        const char *e = self->xml ? "</Ascp>\n" : "\n";
        OUTMSG(("%s%s%s", b, ascp_bin == NULL ? "NotFound" : ascp_bin, e));
    }
    if (rc == 0) {
        const char *b = self->xml ? "    <KeyFile>"    : "key file: ";
        const char *e = self->xml ? "</KeyFile>\n" : "\n";
        OUTMSG(("%s%s%s", b,
            private_file == NULL ? "NotFound" : private_file, e));
    }

    rc = aspera_options(&opt);
    if (rc != 0) {
        OUTMSG(("%saspera_options = %R%s", b, rc, e));
    }
    if (rc == 0) {
        const char *b = self->xml ? "    <MaxRate>"    : "max rate: ";
        const char *e = self->xml ? "</MaxRate>\n" : "\n";
        OUTMSG(("%s%s%s", b,
            opt.target_rate == NULL ? "NotFound" : opt.target_rate, e));
    }
    if (rc == 0) {
        const char *b = self->xml ? "    <Disabled>"    : "disabled: ";
        const char *e = self->xml ? "</Disabled>\n" : "\n";
        OUTMSG(("%s%s%s", b, opt.disabled ? "true" : "false", e));
    }

    if (ascp_bin != NULL) {
        size_t num_writ = 0;
        char command[PATH_MAX] = "";
        const char *b = self->xml ? "<Version>\n"  : "";
        const char *e = self->xml ? "</Version>\n" : "";
        OUTMSG(("%s", b));
        rc = string_printf(command, sizeof command, &num_writ,
            "\"%s\" -A", ascp_bin);
        if (rc != 0 || num_writ >= sizeof command) {
            OUTMSG(("cannot generate ascp command: %R\n", rc));
        }
        else {
            int s = system(command);
            if (s != 0) {
                OUTMSG(("system(%s) = %d\n", command, s));
            }
        }
        OUTMSG(("%s", e));
    }

    free((void*)ascp_bin);
    free((void*)private_file);

    OUTMSG(("%s", e));
    return 0;
}

#if 0
static rc_t PrintCurl(bool full, bool xml) {
    const char *b = xml ? "  <Curl>"  : "";
    const char *e = xml ? "</Curl>" : "";

    KNSManager *mgr = NULL;

    rc_t rc = KNSManagerMake(&mgr);
    if (rc != 0) {
        OUTMSG(("%sKNSManagerMake = %R%s\n", b, rc, e));
    }
    else {
        rc_t rc = KNSManagerAvail(mgr);
        OUTMSG(("%s", b));

        if (full) {
            OUTMSG(("KNSManagerAvail = %R. ", rc));
        }

        if (rc == 0) {
            const char *version_string = NULL;
            rc = KNSManagerCurlVersion(mgr, &version_string);
            if (rc == 0) {
                if (full) {
                    OUTMSG(("Curl Version = %s", version_string));
                }
            }
            else {
                OUTMSG(("KNSManagerCurlVersion = %R", rc));
            }
        }

        if (rc == 0 && !full) {
            OUTMSG(("libcurl: found"));
        }
        OUTMSG(("%s\n", e));
    }

    RELEASE(KNSManager, mgr);

    return rc;
}
#endif

#define kptKartITEM (kptAlias - 1)

static rc_t _KartItemPrint(const KartItem *self, bool xml) {
    const char root[] = "KartRow";
    const String *elem = NULL;
    if (xml) {
        OUTMSG(("  <%s>\n", root));
    }
    {
        const char root[] = "ProjId";
        rc_t rc = KartItemProjId(self, &elem);
        if (rc != 0) {
            OUTMSG(("KartItem%s = %R\n", root, rc));
        }
        else if (xml) {
            OUTMSG(("    <%s>%S</%s>\n", root, elem, root));
        }
        else {
            OUTMSG(("%s: %S\n", root, elem));
        }
    }
    {
        const char root[] = "ItemId";
        rc_t rc = KartItemItemId(self, &elem);
        if (rc != 0) {
            OUTMSG(("KartItem%s = %R\n", root, rc));
        }
        else if (xml) {
            OUTMSG(("    <%s>%S</%s>\n", root, elem, root));
        }
        else {
            OUTMSG(("%s: %S\n", root, elem));
        }
    }
    {
        const char root[] = "Accession";
        rc_t rc = KartItemAccession(self, &elem);
        if (rc != 0) {
            OUTMSG(("KartItem%s = %R\n", root, rc));
        }
        else if (xml) {
            OUTMSG(("    <%s>%S</%s>\n", root, elem, root));
        }
        else {
            OUTMSG(("%s: %S\n", root, elem));
        }
    }
    {
        const char root[] = "Name";
        rc_t rc = KartItemName(self, &elem);
        if (rc != 0) {
            OUTMSG(("KartItem%s = %R\n", root, rc));
        }
        else if (xml) {
            OUTMSG(("    <%s>%S</%s>\n", root, elem, root));
        }
        else {
            OUTMSG(("%s: %S\n", root, elem));
        }
    }
    {
        const char root[] = "ItemDesc";
        rc_t rc = KartItemItemDesc(self, &elem);
        if (rc != 0) {
            OUTMSG(("KartItem%s = %R\n", root, rc));
        }
        else if (xml) {
            OUTMSG(("    <%s>%S</%s>\n", root, elem, root));
        }
        else {
            OUTMSG(("%s: %S\n", root, elem));
        }
    }
    if (xml) {
        OUTMSG(("  </%s>\n", root));
    }
    return 0;
}

/*static rc_t _KartPrint(const Kart *self, bool xml) {
    const char root[] = "Kart";
    if (xml) {
        OUTMSG(("  <%s>\n", root));
    }
    rc_t rc = KartPrint(self);
    if (rc != 0) {
        OUTMSG(("KartPrint = %R\n", rc));
    }
    if (xml) {
        OUTMSG(("  </%s>\n", root));
    }
    return 0;
}
static rc_t _KartPrintSized(const Kart *self, bool xml) {
    return 0;
}
*/

static rc_t _KartPrintNumbered(const Kart *self, bool xml) {
    const char root[] = "Kart";
    if (xml) {
        OUTMSG(("  <%s numbered=\"true\">\n", root));
    }
    {
        rc_t rc = KartPrintNumbered(self);
        if (rc != 0) {
            OUTMSG(("KartPrint = %R\n", rc));
        }
    }
    if (xml) {
        OUTMSG(("  </%s>\n", root));
    }
    return 0;
}


static rc_t ipv4_endpoint_to_string(char *buffer, size_t buflen, KEndPoint *ep)
{
	uint32_t b[4];
	b[0] = ( ep->u.ipv4.addr & 0xFF000000 ) >> 24;
	b[1] = ( ep->u.ipv4.addr & 0xFF0000 ) >> 16;
	b[2] = ( ep->u.ipv4.addr & 0xFF00 ) >> 8;
	b[3] = ( ep->u.ipv4.addr & 0xFF );
	return string_printf( buffer, buflen, NULL, "ipv4: %d.%d.%d.%d : %d",
						   b[0], b[1], b[2], b[3], ep->u.ipv4.port );
}

static rc_t ipv6_endpoint_to_string(char *buffer, size_t buflen, KEndPoint *ep)
{
	uint32_t b[8];
	b[0] = ( ep->u.ipv6.addr[ 0  ] << 8 ) | ep->u.ipv6.addr[ 1  ];
	b[1] = ( ep->u.ipv6.addr[ 2  ] << 8 ) | ep->u.ipv6.addr[ 3  ];
	b[2] = ( ep->u.ipv6.addr[ 4  ] << 8 ) | ep->u.ipv6.addr[ 5  ];
	b[3] = ( ep->u.ipv6.addr[ 6  ] << 8 ) | ep->u.ipv6.addr[ 7  ];
	b[4] = ( ep->u.ipv6.addr[ 8  ] << 8 ) | ep->u.ipv6.addr[ 9  ];
	b[5] = ( ep->u.ipv6.addr[ 10 ] << 8 ) | ep->u.ipv6.addr[ 11 ];
	b[6] = ( ep->u.ipv6.addr[ 12 ] << 8 ) | ep->u.ipv6.addr[ 13 ];
	b[7] = ( ep->u.ipv6.addr[ 14 ] << 8 ) | ep->u.ipv6.addr[ 15 ];
	return string_printf( buffer, buflen, NULL,
        "ipv6: %.04X:%.04X:%.04X:%.04X:%.04X:%.04X:%.04X:%.04X: :%d", 
		b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], ep->u.ipv6.port );
}

static rc_t ipc_endpoint_to_string(char *buffer, size_t buflen, KEndPoint *ep)
{
	return string_printf( buffer, buflen, NULL, "ipc: %s", ep->u.ipc_name );
}

static rc_t endpoint_to_string( char * buffer, size_t buflen, KEndPoint * ep )
{
	rc_t rc;
	switch( ep->type )
	{
		case epIPV4 : rc = ipv4_endpoint_to_string( buffer, buflen, ep ); break;
		case epIPV6 : rc = ipv6_endpoint_to_string( buffer, buflen, ep ); break;
		case epIPC  : rc = ipc_endpoint_to_string( buffer, buflen, ep ); break;
		default     : rc = string_printf( buffer, buflen, NULL,
                          "unknown endpoint-tyep %d", ep->type ); break;
	}
	return rc;
}

static rc_t perfrom_dns_test(const Main *self, const char *eol) {
    rc_t rc = 0;
    const char domain[] = "www.ncbi.nlm.nih.gov";
    const uint16_t port = 80;
	KEndPoint ep;
    String s_domain;
    KTimeMs_t time = 0;
	KTimeMs_t start_time = KTimeMsStamp();
    assert(self);
    StringInitCString(&s_domain, domain);
    rc = KNSManagerInitDNSEndpoint(self->knsMgr, &ep, &s_domain, port);
    time = KTimeMsStamp() - start_time;
    if (rc != 0) {
        OUTMSG
            (("KNSManagerInitDNSEndpoint(%s, %d)=%R%s", domain, port, rc, eol));
    }
    else {
        const char root[] = "DnsEndpoint";/*"www.ncbi.nlm.nih.gov", 80*/
        char s_endpoint[1024] = "";
        rc = endpoint_to_string(s_endpoint, sizeof s_endpoint, &ep);
        if (self->xml) {
            OUTMSG(("  <%s "
                "domain=\"%s\" port=\"%d\" address=\"%s\" time=\"%d ms\"/>\n",
                root, domain, port, s_endpoint, time));
        }
        else {
            OUTMSG((
                "%s domain=\"%s\" port=\"%d\" address=\"%s\" time=\"%d ms\"\n",
                root, domain, port, s_endpoint, time));
        }
    }
    return rc;
}

static rc_t read_stream_into_databuffer(
    KStream *stream, KDataBuffer *databuffer)
{
	rc_t rc;
	
	size_t total = 0;
	KDataBufferMakeBytes( databuffer, 4096 );
	while ( 1 )
	{
		size_t num_read;
		uint8_t * base;
		uint64_t avail = databuffer->elem_count - total;
		if ( avail < 256 )
		{
			rc = KDataBufferResize( databuffer, databuffer->elem_count + 4096 );
			if ( rc != 0 )
			{
				LogErr( klogErr, rc, "CGI: KDataBufferResize failed" );
				break;
			}
		}
		
		base = databuffer->base;
		rc = KStreamRead(stream, &base[total],
            (size_t) databuffer->elem_count - total, &num_read);
		if ( rc != 0 )
		{
			/* TBD - look more closely at rc */
			if ( num_read > 0 )
			{
				LogErr( klogErr, rc, "CGI: KStreamRead failed" );
				rc = 0;
			}
			else
				break;
		}

		if ( num_read == 0 )
			break;

		total += num_read;
	}

	if ( rc == 0 )
		databuffer->elem_count = total;
	return rc;
}

static rc_t call_cgi(const Main *self, const char *cgi_url,
    uint32_t ver_major, uint32_t ver_minor, const char *protocol,
    const char *acc, KDataBuffer *databuffer, const char *eol)
{
	KHttpRequest *req = NULL;
    rc_t rc = 0;
    assert(self);
    rc = KNSManagerMakeReliableClientRequest
        (self->knsMgr, &req, 0x01000000, NULL, cgi_url);
    if (rc != 0) {
        OUTMSG(
            ("KNSManagerMakeReliableClientRequest(%s)=%R%s", cgi_url, rc, eol));
    }
    if (rc == 0) {
        const char param[] = "acc";
		rc = KHttpRequestAddPostParam( req, "%s=%s", param, acc);
        if (rc != 0) {
            OUTMSG(("KHttpRequestAddPostParam(%s)=%R%s", param, rc, eol));
        }
    }
    if (rc == 0) {
        const char param[] = "accept-proto";
		rc = KHttpRequestAddPostParam( req, "%s=%s", param, protocol);
        if (rc != 0) {
            OUTMSG(("KHttpRequestAddPostParam(%s)=%R%s", param, rc, eol));
        }
    }
    if (rc == 0) {
        const char param[] = "version";
        rc = KHttpRequestAddPostParam
            (req, "%s=%u.%u", param, ver_major, ver_minor);
        if (rc != 0) {
            OUTMSG(("KHttpRequestAddPostParam(%s)=%R%s", param, rc, eol));
        }
    }
    if (rc == 0) {
        KHttpResult *rslt = NULL;
        rc = KHttpRequestPOST(req, &rslt);
        if (rc != 0) {
            OUTMSG(("KHttpRequestPOST(%s)=%R%s", cgi_url, rc, eol));
        }
        else {
            const char root[] = "StatusCode";
            uint32_t code = 0;
            rc = KHttpResultStatus(rslt, &code, NULL, 0, NULL);
            if (rc != 0) {
                OUTMSG(("KHttpResultStatus(%s)=%R%s", cgi_url, rc, eol));
            }
            else if (code != 200) {
                if (self->xml) {
                    OUTMSG(("    <%s>%d</%s>\n", root, code, root));
                }
                else {
                    OUTMSG(("%s=%d\n", root, code));
                }
                rc = RC(rcNS, rcFile, rcReading, rcFile, rcInvalid);
            }
            else {
                KStream *response = NULL;
                rc = KHttpResultGetInputStream(rslt, &response);
                if (rc != 0) {
                    OUTMSG(("KHttpResultGetInputStream(%s)=%R%s",
                        cgi_url, rc, eol));
                }
                else {
                    rc = read_stream_into_databuffer(response, databuffer);
                }
                RELEASE(KStream, response);
            }
        }

        RELEASE(KHttpResult, rslt);
    }
    RELEASE(KHttpRequest, req);
    return rc;
}

static rc_t perform_cgi_test(const Main *self, const char *eol, const char *acc)
{
    rc_t rc = 0;
    const char root[] = "Cgi";
    KDataBuffer databuffer;
    KTimeMs_t start_time = KTimeMsStamp();
    if (acc == NULL) {
        return 0;
    }
    assert(self);
    memset(&databuffer, 0, sizeof databuffer);
    if (self->xml) {
        OUTMSG(("  <%s>\n", root));
    }
    {
        KTimeMs_t time = 0;
        const char root[] = "Response";
        rc = call_cgi(self, "http://www.ncbi.nlm.nih.gov/Traces/names/names.cgi"
            , 1, 1, "http", acc, &databuffer, eol);
        time = KTimeMsStamp() - start_time;
        if (rc == 0) {
            const char *start = databuffer.base;
            size_t size = KDataBufferBytes(&databuffer);
            if (self->xml) {
                OUTMSG(("    <%s time=\"%d ms\">%.*s</%s>\n",
                    root, time, size, start, root));
            }
            else {
                OUTMSG(("%s = \"%.*s\"  time=\"%d ms\"\n",
                    root, size, start, time));
            }
        }
        else {/*
            if (self->xml) {
                //OUTMSG(("    <%s/>\n", root, size, start, root));
            }
            else {
                //OUTMSG(("%s = '%.*s'\n", root, size, start));
            }*/
        }
    }
    if (self->xml) {
        OUTMSG(("  </%s>\n", root));
    }
    return rc;
}

static rc_t MainNetwotk(const Main *self, const char *arg, const char *eol)
{
    const char root[] = "Network";
    assert(self);
    if (self->xml) {
        OUTMSG(("<%s>\n", root));
    }
    {
        const char root[] = "HttpProxy";
        bool enabled = KNSManagerGetHTTPProxyEnabled(self->knsMgr);
        if (!enabled) {
            if (self->xml) {
                OUTMSG(("  <%s enabled=\"false\">\n", root));
            }
            else {
                OUTMSG(("HTTPProxyEnabled=\"false\"\n", root));
            }
        }
        else {
            if (self->xml) {
                OUTMSG(("  <%s enabled=\"true\">\n", root));
            }
            else {
                OUTMSG(("HTTPProxyEnabled=\"true\"\n", root));
            }
        }
        {
            const String *proxy = NULL;
            rc_t rc = KNSManagerGetHTTPProxyPath(self->knsMgr, &proxy);
            if (rc != 0) {
                OUTMSG(("KNSManagerGetHTTPProxyPath()=%R%s", rc, eol));
            }
            else {
                const char root[] = "Path";
                if (self->xml) {
                    OUTMSG(("    <%s>%S</%s>\n", root, proxy, root));
                }
                else {
                    OUTMSG(("HTTPProxyPath=\"%S\"\n", proxy));
                }
            }
        }
        if (self->xml) {
            OUTMSG(("  </%s>\n", root));
        }
    }
    {
		const char *user_agent = NULL;
        rc_t rc = KNSManagerGetUserAgent(&user_agent);
        if (rc != 0) {
            OUTMSG(("KNSManagerGetUserAgent()=%R%s", rc, eol));
        }
        else {
            const char root[] = "UserAgent";
            if (self->xml) {
                OUTMSG(("  <%s>%s</%s>\n", root, user_agent, root));
            }
            else {
                OUTMSG(("UserAgent=\"%s\"\n", user_agent));
            }
        }
    }
    perfrom_dns_test(self, eol);
    perform_cgi_test(self, eol, arg);
    if (self->xml) {
        OUTMSG(("</%s>\n", root));
    }
    return 0;
}

static
rc_t MainExec(const Main *self, const KartItem *item, const char *aArg, ...)
{
    const char root[] = "Object";

    rc_t rc = 0;
    rc_t rce = 0;

    KPathType type = kptNotFound;
    bool alias = false;
    int64_t directSz = -1;
    int64_t localSz = -1;
    int64_t remoteSz = -1;
    size_t num_writ = 0;
    char arg[PATH_MAX] = "";

    const char *eol = NULL;

    va_list args;
    va_start(args, aArg);

    assert(self);

    eol = self->xml ? "<br/>\n" : "\n";

    if (self->xml) {
        OUTMSG(("<%s>\n", root));
    }

    if (item != NULL) {
        type = kptKartITEM;

        _KartItemPrint(item, self->xml);
    }

    else {
        rc = string_vprintf(arg, sizeof arg, &num_writ, aArg, args);
        if (rc != 0) {
            OUTMSG(("string_vprintf(%s)=%R%s", aArg, rc, eol));
            return rc;
        }
        assert(num_writ < sizeof arg);

        OUTMSG(("\n"));
        rc = printString(arg);
        if (rc != 0) {
            OUTMSG(("printString=%R%s", rc, eol));
            return rc;
        }
        if (MainHasTest(self, eType)) {
            OUTMSG((" "));
            rc = MainReport(self, arg, &directSz, &type, &alias);
        }
        else {
            type = KDirectoryPathType(self->dir, "%s", arg);
        }
        OUTMSG(("%s", eol));

        if (MainHasTest(self, eOpenTable)) {
            MainOpenAs(self, arg, false);
        }
        if (MainHasTest(self, eOpenDB)) {
            MainOpenAs(self, arg, true);
        }
    }

    if (self->recursive && type == kptDir && !alias) {
        uint32_t i = 0;
        uint32_t count = 0;
        KNamelist *list = NULL;
        rc = KDirectoryList(self->dir, &list, NULL, NULL, "%s", arg);
        if (rc != 0) {
            OUTMSG(("KDirectoryList(%s)=%R ", arg, rc));
        }
        else {
            rc = KNamelistCount(list, &count);
            if (rc != 0) {
                OUTMSG(("KNamelistCount(KDirectoryList(%s))=%R ", arg, rc));
            }
        }
        for (i = 0; i < count && rc == 0; ++i) {
            const char *name = NULL;
            rc = Quitting();
            if (rc != 0) {
                break;
            }
            rc = KNamelistGet(list, i, &name);
            if (rc != 0) {
                OUTMSG(("KNamelistGet(KDirectoryList(%s), %d)=%R ",
                    arg, i, rc));
            }
            else {
                rc_t rc2 = MainExec(self, NULL, "%s/%s", arg, name);
                if (rc2 != 0 && rce == 0) {
                    rce = rc2;
                }
            }
        }
        RELEASE(KNamelist, list);
    }
    else {
        bool isKart = false;
        Kart *kart = NULL;
        if (type == kptFile) {
            rc_t rc = KartMake(self->dir, arg, &kart, &isKart);
            if (rc != 0) {
                OUTMSG(("KartMake = %R\n", rc));
            }
        }

        if (isKart) {
            const KartItem *item = NULL;
            while (true) {
                rc_t rc2 = 0;
                RELEASE(KartItem, item);
                rc2 = Quitting();
                if (rc2 != 0) {
                    if (rce == 0) {
                        rce = rc2;
                    }
                    break;
                }
                rc2 = KartMakeNextItem(kart, &item);
                if (rc2 != 0) {
                    OUTMSG(("KartMakeNextItem = %R\n", rc2));
                    if (rce == 0) {
                        rce = rc2;
                    }
                    break;
                }
                if (item == NULL) {
                    break;
                }
                rc2 = MainExec(self, item, NULL);
                if (rc2 != 0 && rce == 0) {
                    rce = rc2;
                }
            }
            if (true) {
                _KartPrintNumbered(kart, self->xml);
            }
            /*if (true) {
                _KartPrint(kart, self->xml);
            }
            if (true) {
                _KartPrintSized(kart, self->xml);
            }*/
            KartRelease(kart);
            kart = NULL;
        }
        else {
            if (MainHasTest(self, eResolve)) {
     /* ignore returned value :
        resolver's errors are detected but not reported as test-sra's failure */
                MainResolve(self, item, arg, &localSz, &remoteSz, false);
            }

            if (MainHasTest(self, eNetwork)) {
                MainNetwotk(self, arg, eol);
            }

            if (item == NULL) { /* TODO || kartitem & database */
                if (type == kptDatabase || type == kptNotFound) {
                    if (MainHasTest(self, eDependMissing)) {
                        rc_t rc2 = MainDepend(self, arg, true);
                        if (rc == 0 && rc2 != 0) {
                            rc = rc2;
                        }
                    }

                    if (MainHasTest(self, eDependAll)) {
                        rc_t rc2 = MainDepend(self, arg, false);
                        if (rc == 0 && rc2 != 0) {
                            rc = rc2;
                        }
                    }
                }
            }

            if (MainHasTest(self, eResolve) && (
                (directSz != -1 && localSz != -1 && directSz != localSz) ||
                (remoteSz != -1 && localSz != -1 && localSz != remoteSz))
               )
            {
                OUTMSG(("FILE SIZES DO NOT MATCH: "));
                if (directSz != -1 && localSz != -1 &&
                    directSz != remoteSz)
                {
                    OUTMSG(("direct=%ld != remote=%ld. ", directSz, remoteSz));
                }
                if (remoteSz != -1 && localSz != -1 &&
                    localSz != remoteSz)
                {
                    OUTMSG(("local=%ld != remote=%ld. ", localSz, remoteSz));
                }
                OUTMSG(("\n"));
            }

        }
    }

    if (rce != 0 && rc == 0) {
        rc = rce;
    }

    if (self->xml) {
        OUTMSG(("</%s>\n", root));
    }

    va_end(args);
    return rc;
}

static
rc_t _SraReleaseVersionPrint(const SraReleaseVersion *self, rc_t rc, bool xml,
    const char *error, const char *msg)
{
    assert(self && error && msg);

    if (rc != 0) {
        OUTMSG(("ERROR: %s.", error));
    }
    else {
        char version[256] = "";
        const char *eol = xml ? "<br/>\n" : "\n";
        rc = SraReleaseVersionPrint(self, version, sizeof version, NULL);
        if (rc == 0) {
            rc = OUTMSG(("%s: %s.%s", msg, version, eol));
        }
    }

    return rc;
}

static rc_t _KDyldLoadLib(KDyld *self, char *name, size_t sz,
    const char *path, bool xml, int indent)
{
    rc_t rc = 0;
    KDylib *lib = NULL;
    rc = KDyldLoadLib(self, &lib, "%.*s", sz, path);
    if (rc == 0) {
        rc = OUTMSG(("LOADED %.*s\n", sz, path));
    }
    else {
        rc = OUTMSG(("NOT LOADED %.*s\n", sz, path));
    }
    RELEASE(KDylib, lib);
    return rc;
}

static
rc_t _KHttpRequestPOST(KHttpRequest *self, KDataBuffer *result, size_t *total) {
    rc_t rc = 0;
    KHttpResult *rslt = NULL;
    assert(result && total);
    memset(result, 0, sizeof *result);
    *total = 0;
    rc = KHttpRequestPOST(self, &rslt);
    if (rc == 0) {
        uint32_t code = 0;
        size_t msg_size = 0;
        char msg_buff[256] = "";
        rc = KHttpResultStatus(rslt,
            &code, msg_buff, sizeof msg_buff, &msg_size);
        if (rc == 0 && code == 200) {
            KStream *response = NULL;
            rc = KHttpResultGetInputStream( rslt, &response);
            if (rc == 0) {
                size_t num_read = 0;
                KDataBufferMakeBytes(result, 4096);
                while (true) {
                    uint8_t *base = NULL;
                    uint64_t avail = result->elem_count - *total;
                    if (avail < 256) {
                        rc = KDataBufferResize
                            (result, result->elem_count + 4096);
                        if (rc != 0) {
                            break;
                        }
                    }
                    base = result->base;
                    rc = KStreamRead(response, &base[*total],
                        result->elem_count - *total, &num_read);
                    if (rc != 0) {
                        if (num_read > 0) {
                            rc = 0;
                        }
                        else {
                            break;
                        }
                    }
                    if (num_read == 0) {
                        break;
                    }
                    *total += num_read;
                }
                RELEASE(KStream, response);
            }
        }
    }
    RELEASE(KHttpResult, rslt);
    return rc;
}

static
rc_t _MainPost(const Main *self, const char *name, char *buffer, size_t sz)
{
    rc_t rc = 0;

    KHttpRequest *req = NULL;
    KDataBuffer result;

    size_t total = 0;

    assert(self && buffer && sz);

    buffer[0] = '\0';

    rc = KNSManagerMakeRequest(self->knsMgr, &req, 0x01000000, NULL,
        "http://trace.ncbi.nlm.nih.gov/Traces/sratoolkit/sratoolkit.cgi");

    if (rc == 0) {
        rc = KHttpRequestAddPostParam(req, "cmd=vers");
    }
    if (rc == 0) {
        rc = KHttpRequestAddPostParam(req, "libname=%s", name);
    }
    if (rc == 0) {
        rc = _KHttpRequestPOST(req, &result, &total);
    }

    if (rc == 0) {
        const char *start = (const void*)(result.base);
        if (total > 0) {
            if (*(start + total) != '\0') {
                rc = RC(rcExe, rcString, rcParsing, rcString, rcUnexpected);
            }
            else if (*(start + total - 1) != '\n') {
                rc = RC(rcExe, rcString, rcParsing, rcString, rcUnexpected);
            }
            else {
                string_copy(buffer, sz, start, total - 1);
            }
        }
    }

    KDataBufferWhack(&result);
    RELEASE(KHttpRequest, req);

    return rc;
}

static rc_t _MainPrintNgsInfo(const Main* self) {
    rc_t rc = 0;
    const char root[] = "Ngs";
    KDyld *l = NULL;
    assert(self);
    if (self->xml) {
        OUTMSG(("  <%s>\n", root));
    }
    {
        const char root[] = "Latest";
        char v[512];
        if (self->xml) {
            OUTMSG(("    <%s>\n", root));
        }
        {
            const char name[] = "ncbi-vdb";
            _MainPost(self, name, v, sizeof v);
            if (self->xml) {
                OUTMSG(("      <%s version=\"%s\"/>\n", name, v));
            }
            else {
                OUTMSG(("%s latest version=\"%s\"\n", name, v));
            }
        }
        {
            const char name[] = "ngs-sdk";
            _MainPost(self, name, v, sizeof v);
            if (self->xml) {
                OUTMSG(("      <%s version=\"%s\"/>\n", name, v));
            }
            else {
                OUTMSG(("%s latest version=\"%s\"\n", name, v));
                OUTMSG(("\n"));
            }
        }
        if (self->xml) {
            OUTMSG(("    </%s>\n", root));
        }
    }
    rc = KDyldMake(&l);
    {
        String *result = NULL;
        rc = KConfigReadString(self->cfg, "NCBI_HOME", &result);
        if (rc == 0) {
            bool found = false;
            const KFile *f = NULL;
            char *buffer = NULL;
            char *ps = NULL;
            size_t ls = 0;
            char *pv = NULL;
            size_t lv = 0;
            assert(result);
            rc = KDirectoryOpenFileRead
                (self->dir, &f, "%s/LibManager.properties", result->addr);
            if (rc == 0) {
                uint64_t size = 0;
                rc = KFileSize(f, &size);
                if (rc == 0) {
                    buffer = malloc(size + 1);
                    if (buffer == NULL) {
                        rc = RC(rcExe, rcData, rcAllocating,
                            rcMemory, rcExhausted);
                    }
                    else {
                        size_t num_read = 0;
                        rc = KFileRead(f, 0, buffer, size + 1, &num_read);
                        if (rc == 0) {
#if _ARCH_BITS == 32
                            const char* sneed = "/dll/ngs-sdk/32/loaded/path=";
                            const char* vneed = "/dll/ncbi-vdb/32/loaded/path=";
#else
                            const char* sneed = "/dll/ngs-sdk/64/loaded/path=";
                            const char* vneed = "/dll/ncbi-vdb/64/loaded/path=";
#endif

                            assert(num_read <= size);
                            buffer[num_read] = '\0';
                            found = true;

                            ps = strstr(buffer, sneed);
                            if (ps != NULL) {
                                ps += strlen(sneed);
                            }
                            if (ps != NULL) {
                                const char *e = strchr(ps, '\n');
                                if (e != NULL) {
                                    ls = e - ps;
                                }
                            }

                            pv = strstr(buffer, vneed);
                            if (pv != NULL) {
                                pv += strlen(vneed);
                            }
                            if (pv != NULL) {
                                const char *e = strchr(pv, '\n');
                                if (e != NULL) {
                                    lv = e - pv;
                                }
                            }
                        }
                    }
                }
                RELEASE(KFile, f);
            }
            if (self->xml) {
                if (found) {
                    OUTMSG(("    <LibManager>\n"
                            "      <Properties>\n%s"
                            "      </Properties>\n", buffer));
                    if (ls != 0) {
                        OUTMSG(("      <ngs-sdk>\n"
                                "        <Path>%.*s</Path>\n", ls, ps));
                        _KDyldLoadLib(l, "ngs-sdk", ls, ps, self->xml, 8);
                        OUTMSG(("      </ngs-sdk>\n"));
                    }
                    if (lv != 0) {
                        OUTMSG(("      <ncbi-vdb>\n"
                                "        <Path>%.*s</Path>\n", lv, pv));
                        _KDyldLoadLib(l, "ncbi-vdb", ls, ps, self->xml, 8);
                        OUTMSG(("      </ncbi-vdb>\n"));
                    }
                    OUTMSG(("    </LibManager>\n"));
                }
                else {
                    OUTMSG(("    <LibManager.properties/>\n"));
                }
            }
            else {
                OUTMSG(("LibManager.properties=\n"));
                if (found) {
                    OUTMSG(("%s\n", buffer));
                    if (ls != 0) {
                        OUTMSG(("LibManager.ngs-sdk='%.*s'\n", ls, ps));
                    }
                    if (lv != 0) {
                        OUTMSG(("LibManager.ngs-sdk='%.*s'\n", lv, pv));
                    }
                }
            }
            free(buffer);
            buffer = NULL;
            free(result);
            result = NULL;
        }
    }
    {
        const char root[] = "ncbi-vdb";
        if (self->xml) {
            OUTMSG(("    <%s>\n", root));
        }
        if (self->xml) {
            OUTMSG(("    </%s>\n", root));
        }
    }
    {
        const char root[] = "ngs-sdk";
        if (self->xml) {
            OUTMSG(("    <%s>\n", root));
        }
        if (self->xml) {
            OUTMSG(("    </%s>\n", root));
        }
    }
    if (self->xml) {
        OUTMSG(("  </%s>\n", root));
    }
    RELEASE(KDyld, l);
    return rc;
}

static rc_t MainPrintVersion(Main *self) {
    const char root[] = "Version";

    rc_t rc = 0;

    SraReleaseVersion version;
    SraReleaseVersion newVersion;

    assert(self);

    if (MainHasTest(self, eNewVersion)) {
        MainAddTest(self, eVersion);
    }

    if (!MainHasTest(self, eVersion)) {
        return 0;
    }
    memset(&version, 0, sizeof version);
    memset(&newVersion, 0, sizeof newVersion);
    if (self->xml) {
        OUTMSG(("  <%s>\n", root));
    }
    rc = SraReleaseVersionGet(&version);
    rc = _SraReleaseVersionPrint(&version, rc, self->xml,
        "cannot get SRA Toolkit release version",
        "NCBI SRA Toolkit release version");

    if (MainHasTest(self, eNewVersion)) {
        int32_t isNew = 0;
        rc = KNSManagerNewReleaseVersion(self->knsMgr, &newVersion);
        rc = _SraReleaseVersionPrint(&newVersion, rc, self->xml,
            "cannot get latest available SRA Toolkit release version",
            "Latest available NCBI SRA Toolkit release version");
        if (rc == 0) {
            rc = SraReleaseVersionCmp(&version, &newVersion, &isNew);
        }
        if (rc == 0) {
            if (isNew > 0) {
                OUTMSG((
           "A new version of SRA Toolkit is available for download from\n"
           "\"http://www.ncbi.nlm.nih.gov/Traces/sra/sra.cgi?view=software\".\n"
                ));
            }
            else if (isNew == 0) {
                OUTMSG(("You already have the latest version of "
                    "SRA Toolkit.\n"));
            }
            else {
                OUTMSG(("Your version of "
                    "SRA Toolkit is more recent than the latest available.\n"));
            }
        }
    }

    if (self->xml) {
        OUTMSG(("  </%s>\n", root));
    }
    else if (self->full) {
        OUTMSG(("\n"));
    }

    return rc;
}

static rc_t MainFini(Main *self) {
    rc_t rc = 0;

    assert(self);

    RELEASE(VResolver, self->resolver);
    RELEASE(KConfig, self->cfg);
    RELEASE(KNSManager, self->knsMgr);
    RELEASE(KRepositoryMgr, self->repoMgr);
    RELEASE(VFSManager, self->vMgr);
    RELEASE(VDBManager, self->mgr);
    RELEASE(KDirectory, self->dir);
    RELEASE(VSchema, self->schema);

    return rc;
}

#define OPTION_CACHE "allow-caching"
#define ALIAS_CACHE  "C"
static const char* USAGE_CACHE[] = { "do not disable caching", NULL };

#define OPTION_FULL "full"
#define ALIAS_FULL  NULL
static const char* USAGE_FULL[] = { "full test mode", NULL };

#define OPTION_QUICK "quick"
#define ALIAS_QUICK  "Q"
static const char* USAGE_QUICK[] = { "quick test mode", NULL };

#define OPTION_NO_RFS "no-rfs"
static const char* USAGE_NO_RFS[]
    = { "do not check remote file size for dependencies", NULL };

#define OPTION_NO_VDB "no-vdb"
#define ALIAS_NO_VDB  "N"
static const char* USAGE_NO_VDB[] = { "do not call VDBManagerPathType", NULL };

#define OPTION_PRJ "project-id"
#define ALIAS_PRJ  "p"
static const char* USAGE_PRJ[] = { "set project context", NULL };

#define OPTION_REC "recursive"
#define ALIAS_REC  "R"
static const char* USAGE_REC[] = { "check object type recursively", NULL };

#define OPTION_OUT "output"
#define ALIAS_OUT  "X"
static const char* USAGE_OUT[] = { "output type: one of (xml text)", NULL };

OptDef Options[] = {                             /* needs_value, required */
    { OPTION_CACHE , ALIAS_CACHE , NULL, USAGE_CACHE , 1, false, false },
    { OPTION_FULL  , ALIAS_FULL  , NULL, USAGE_FULL  , 1, false, false },
    { OPTION_NO_RFS, NULL        , NULL, USAGE_NO_RFS, 1, false, false },
    { OPTION_NO_VDB, ALIAS_NO_VDB, NULL, USAGE_NO_VDB, 1, false, false },
    { OPTION_OUT   , ALIAS_OUT   , NULL, USAGE_OUT   , 1, true , false },
    { OPTION_PRJ   , ALIAS_PRJ   , NULL, USAGE_PRJ   , 1, true , false },
    { OPTION_QUICK , ALIAS_QUICK , NULL, USAGE_QUICK , 1, false, false },
    { OPTION_REC   , ALIAS_REC   , NULL, USAGE_REC   , 1, false, false },
};

rc_t CC KMain(int argc, char *argv[]) {
    rc_t rc = 0;
    uint32_t pcount = 0;
    uint32_t i = 0;
    Args *args = NULL;
    rc_t rc3 = 0;
    int argi = 0;

    Main prms;
    char **argv2 = MainInit(&prms, argc, argv, &argi);

    if (rc == 0) {
        rc = ArgsMakeAndHandle(&args, argi, argv2, 1,
            Options, sizeof Options / sizeof Options[0]);
    }

    if (rc == 0) {
        rc = ArgsOptionCount(args, OPTION_CACHE, &pcount);
        if (rc) {
            LOGERR(klogErr, rc, "Failure to get '" OPTION_CACHE "' argument");
        }
        else {
            if (pcount > 0) {
                prms.allowCaching = true;
            }
        }
    }

    if (rc == 0) {
        rc = ArgsOptionCount(args, OPTION_PRJ, &pcount);
        if (rc) {
            LOGERR(klogErr, rc, "Failure to get '" OPTION_PRJ "' argument");
        }
        else {
            if (pcount > 0) {
                const char *dummy = NULL;
                rc = ArgsOptionValue(args, OPTION_PRJ, 0, (const void **)&dummy);
                if (rc != 0) {
                    LOGERR(klogErr, rc,
                        "Failure to get '" OPTION_PRJ "' argument");
                }
                else {
                    prms.projectId = AsciiToU32(dummy, NULL, NULL);
                }
            }
        }
    }

    prms.full = true;

    if (rc == 0) {
        rc = ArgsOptionCount(args, OPTION_QUICK, &pcount);
        if (rc) {
            LOGERR(klogErr, rc, "Failure to get '" OPTION_QUICK "' argument");
        }
        else {
            if (pcount > 0) {
                prms.full = false;
            }
        }
    }

    if (rc == 0) {
        rc = ArgsOptionCount(args, OPTION_FULL, &pcount);
        if (rc) {
            LOGERR(klogErr, rc, "Failure to get '" OPTION_FULL "' argument");
        }
        else {
            if (pcount > 0) {
                prms.full = true;
            }
        }
    }

    if (!prms.full) {
        MainMakeQuick(&prms);
    }

    if (rc == 0) {
        rc = MainInitObjects(&prms);
    }

    if (rc == 0) {
        const char root[] = "Test-sra";
        rc = ArgsOptionCount(args, OPTION_OUT, &pcount);
        if (rc) {
            LOGERR(klogErr, rc, "Failure to get '" OPTION_OUT "' argument");
        }
        else {
            if (pcount > 0) {
                const char *dummy = NULL;
                rc =
                    ArgsOptionValue(args, OPTION_OUT, 0, (const void **)&dummy);
                if (rc != 0) {
                    LOGERR(klogErr, rc,
                        "Failure to get '" OPTION_OUT "' argument");
                }
                else if (strcmp(dummy, "x") == 0 || strcmp(dummy, "X") == 0) {
                    prms.xml = true;
                }
                else {
                    prms.xml = false;
                }
            }
            else {
                prms.xml = MainHasTest(&prms, eCfg)
                    || MainHasTest(&prms, eNcbiReport);
            }
        }

        if (prms.xml) {
            OUTMSG(("<%s>\n", root));
        }

        if (MainHasTest(&prms, eNgs)) {
            _MainPrintNgsInfo(&prms);
        }

        MainPrintVersion(&prms);

        if (MainHasTest(&prms, eCfg)) {
            rc_t rc2 = MainPrintConfig(&prms);
            if (rc == 0 && rc2 != 0) {
                rc = rc2;
            }
        }

        if (MainHasTest(&prms, eOS)) {
            PrintOS(prms.xml);
        }

        if (MainHasTest(&prms, eAscp)) {
            MainPrintAscp(&prms);
        }

        if (!prms.full) {
            rc_t rc2 = MainQuickCheck(&prms);
            if (rc == 0 && rc2 != 0) {
                rc = rc2;
            }
        }

        if (rc == 0) {
            rc = ArgsOptionCount(args, OPTION_NO_RFS, &pcount);
            if (rc) {
                LOGERR(klogErr, rc,
                    "Failure to get '" OPTION_NO_RFS "' argument");
            }
            else {
                if (pcount > 0) {
                    prms.noRfs = true;
                }
            }
        }

        if (rc == 0) {
            rc = ArgsOptionCount(args, OPTION_NO_VDB, &pcount);
            if (rc) {
                LOGERR(klogErr, rc,
                    "Failure to get '" OPTION_NO_VDB "' argument");
            }
            else {
                if (pcount > 0) {
                    prms.noVDBManagerPathType = true;
                }
            }
        }

        if (rc == 0) {
            rc = ArgsOptionCount(args, OPTION_REC, &pcount);
            if (rc) {
                LOGERR(klogErr, rc, "Failure to get '" OPTION_REC "' argument");
            }
            else {
                if (pcount > 0) {
                    prms.recursive = true;
                }
            }
        }

        if (rc == 0) {
            rc = ArgsParamCount(args, &pcount);
        }
/* TODO if pcount == 0 && there are no test type options use a small run as name[0]*/
        for (i = 0; i < pcount; ++i) {
            const char *name = NULL;
            rc3 = ArgsParamValue(args, i, (const void **)&name);
            if (rc3 == 0) {
                rc_t rc2 = Quitting();
                if (rc2 != 0) {
                    if (rc == 0 && rc2 != 0) {
                        rc = rc2;
                    }
                    break;
                }
                ReportResetObject(name);
                rc2 = MainExec(&prms, NULL, name);
                if (rc == 0 && rc2 != 0) {
                    rc = rc2;
                }
            }
        }
        if (rc == 0 && rc3 != 0) {
            rc = rc3;
        }

        if (MainHasTest(&prms, eNcbiReport)) {
            ReportForceFinalize();
        }

        if (!prms.full) {
            OUTMSG(("\nAdd -F option to try all the tests."));
        }

        if (prms.xml) {
            OUTMSG(("</%s>\n", root));
        }
    }

    RELEASE(Args, args);

    {
        rc_t rc2 = MainFini(&prms);
        if (rc == 0 && rc2 != 0) {
            rc = rc2;
        }
    }
    free(argv2);

    return rc;
}
