// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2014-2015 The Dacrs developers
// Copyright (c) 2016 The Tongxingpoints developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "tongxingpoints-config.h"
#endif

#include "init.h"

#include "addrman.h"
#include "checkpoints.h"
#include "main.h"
#include "miner.h"
#include "net.h"
#include "./rpc/rpcserver.h"
#include "txdb.h"
#include "ui_interface.h"
#include "util.h"
#include "cuiserver.h"
#include "tx.h"
#include "./wallet/wallet.h"
#include "./wallet/walletdb.h"
#include "syncdatadb.h"
#include "noui.h"

#include <stdint.h>
#include <stdio.h>

#ifndef WIN32
#include <signal.h>
#endif

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>
#include <boost/interprocess/sync/file_lock.hpp>
#include <openssl/crypto.h>

#include <boost/assign/list_of.hpp>
using namespace boost::assign;

using namespace std;
using namespace boost;


CWallet* pwalletMain;


#ifdef WIN32
// Win32 LevelDB doesn't use filedescriptors, and the ones used for
// accessing block files, don't count towards to fd_set size limit
// anyway.
#define MIN_CORE_FILEDESCRIPTORS 0
#else
#define MIN_CORE_FILEDESCRIPTORS 150
#endif

// Used to pass flags to the Bind() function
enum BindFlags {
    BF_NONE         = 0,
    BF_EXPLICIT     = (1U << 0),
    BF_REPORT_ERROR = (1U << 1)
};


//////////////////////////////////////////////////////////////////////////////
//
// Shutdown
//

//
// Thread management and startup/shutdown:
//
// The network-processing threads are all part of a thread group
// created by AppInit() or the Qt main() function.
//
// A clean exit happens when StartShutdown() or the SIGTERM
// signal handler sets fRequestShutdown, which triggers
// the DetectShutdownThread(), which interrupts the main thread group.
// DetectShutdownThread() then exits, which causes AppInit() to
// continue (it .joins the shutdown thread).
// Shutdown() is then
// called to clean up database connections, and stop other
// threads that should only be stopped after the main network-processing
// threads have exited.
//
// Note that if running -daemon the parent process returns from AppInit2
// before adding any threads to the threadGroup, so .join_all() returns
// immediately and the parent exits from main().
//
// Shutdown for Qt is very similar, only it uses a QTimer to detect
// fRequestShutdown getting set, and then does the normal Qt
// shutdown thing.
//

volatile bool fRequestShutdown = false;

void StartShutdown()
{
    fRequestShutdown = true;
}
bool ShutdownRequested()
{
    return fRequestShutdown;
}



void Shutdown()
{
    LogPrint("INFO","Shutdown : In progress...\n");
    static CCriticalSection cs_Shutdown;
    TRY_LOCK(cs_Shutdown, lockShutdown);
    if (!lockShutdown) return;

    RenameThread("Tongxingpoints-shutoff");
    mempool.AddTransactionsUpdated(1);
    StopRPCThreads();
    ShutdownRPCMining();


    GenerateTongxingpointsBlock(false, NULL, 0);

    StopNode();
    UnregisterNodeSignals(GetNodeSignals());
    {
        LOCK(cs_main);

        if (pwalletMain) {
            pwalletMain->SetBestChain(chainActive.GetLocator());
        	bitdb.Flush(true);
        }

        if (pblocktree)
            pblocktree->Flush();

        if(pAccountViewTip)
        	pAccountViewTip->Flush();

        if(pTxCacheTip)
        	pTxCacheTip->Flush();

        if(pScriptDBTip)
        	pScriptDBTip->Flush();
        delete pAccountViewTip; pAccountViewTip = NULL;
        delete pAccountViewDB; pAccountViewDB = NULL;
        delete pblocktree; pblocktree = NULL;
        delete pTxCacheDB; pTxCacheDB = NULL;
        delete pScriptDB; pScriptDB = NULL;
        delete pTxCacheTip; pTxCacheTip = NULL;
        delete pScriptDBTip; pScriptDBTip = NULL;

    }

    boost::filesystem::remove(GetPidFile());
    UnregisterAllWallets();

    if (pwalletMain)
        delete pwalletMain;

    LogPrint("INFO","Shutdown : done\n");
    printf("Shutdown : done\n");
}

//
// Signal handlers are very limited in what they are allowed to do, so:
//
void HandleSIGTERM(int)
{
    fRequestShutdown = true;
}

void HandleSIGHUP(int)
{
    fReopenDebugLog = true;
}

bool static InitError(const string &str)
{
    uiInterface.ThreadSafeMessageBox(str, "", CClientUIInterface::MSG_ERROR | CClientUIInterface::NOSHOWGUI);
    return false;
}

bool static InitWarning(const string &str)
{
    uiInterface.ThreadSafeMessageBox(str, "", CClientUIInterface::MSG_WARNING | CClientUIInterface::NOSHOWGUI);
    return true;
}

bool static Bind(const CService &addr, unsigned int flags) {
    if (!(flags & BF_EXPLICIT) && IsLimited(addr))
        return false;
    string strError;
    if (!BindListenPort(addr, strError)) {
        if (flags & BF_REPORT_ERROR)
            return InitError(strError);
        return false;
    }
    return true;
}

// Core-specific options shared between UI, daemon and RPC client
string HelpMessage(HelpMessageMode hmm)
{
    string strUsage = _("Options:") + "\n";
    strUsage += "  -?                     " + _("This help message") + "\n";
    strUsage += "  -alertnotify=<cmd>     " + _("Execute command when a relevant alert is received or we see a really long fork (%s in cmd is replaced by message)") + "\n";
    strUsage += "  -blocknotify=<cmd>     " + _("Execute command when the best block changes (%s in cmd is replaced by block hash)") + "\n";
    strUsage += "  -checkblocks=<n>       " + _("How many blocks to check at startup (default: 288, 0 = all)") + "\n";
    strUsage += "  -checklevel=<n>        " + _("How thorough the block verification of -checkblocks is (0-4, default: 3)") + "\n";
    strUsage += "  -conf=<file>           " + _("Specify configuration file (default: tongxingpoints.conf)") + "\n";
    if (hmm == HMM_BITCOIND)
    {
#if !defined(WIN32)
        strUsage += "  -daemon                " + _("Run in the background as a daemon and accept commands") + "\n";
#endif
    }
    strUsage += "  -datadir=<dir>         " + _("Specify data directory") + "\n";
    strUsage += "  -dbcache=<n>           " + strprintf(_("Set database cache size in megabytes (%d to %d, default: %d)"), nMinDbCache, nMaxDbCache, nDefaultDbCache) + "\n";
    strUsage += "  -keypool=<n>           " + _("Set key pool size to <n> (default: 100)") + "\n";
    strUsage += "  -loadblock=<file>      " + _("Imports blocks from external blk000??.dat file") + " " + _("on startup") + "\n";
    strUsage += "  -par=<n>               " + strprintf(_("Set the number of script verification threads (%u to %d, 0 = auto, <0 = leave that many cores free, default: %d)"), -(int)boost::thread::hardware_concurrency(), MAX_SCRIPTCHECK_THREADS, DEFAULT_SCRIPTCHECK_THREADS) + "\n";
    strUsage += "  -pid=<file>            " + _("Specify pid file (default: tongxingpoints.pid)") + "\n";
    strUsage += "  -reindex               " + _("Rebuild block chain index from current blk000??.dat files") + " " + _("on startup") + "\n";
    strUsage += "  -txindex               " + _("Maintain a full transaction index (default: 0)") + "\n";

    strUsage += "\n" + _("Connection options:") + "\n";
    strUsage += "  -addnode=<ip>          " + _("Add a node to connect to and attempt to keep the connection open") + "\n";
    strUsage += "  -banscore=<n>          " + _("Threshold for disconnecting misbehaving peers (default: 100)") + "\n";
    strUsage += "  -bantime=<n>           " + _("Number of seconds to keep misbehaving peers from reconnecting (default: 86400)") + "\n";
    strUsage += "  -bind=<addr>           " + _("Bind to given address and always listen on it. Use [host]:port notation for IPv6") + "\n";
    strUsage += "  -connect=<ip>          " + _("Connect only to the specified node(s)") + "\n";
    strUsage += "  -discover              " + _("Discover own IP address (default: 1 when listening and no -externalip)") + "\n";
    strUsage += "  -dns                   " + _("Allow DNS lookups for -addnode, -seednode and -connect") + " " + _("(default: 1)") + "\n";
    strUsage += "  -dnsseed               " + _("Find peers using DNS lookup (default: 1 unless -connect)") + "\n";
    strUsage += "  -externalip=<ip>       " + _("Specify your own public address") + "\n";
    strUsage += "  -listen                " + _("Accept connections from outside (default: 1 if no -proxy or -connect)") + "\n";
    strUsage += "  -maxconnections=<n>    " + _("Maintain at most <n> connections to peers (default: 125)") + "\n";
    strUsage += "  -maxreceivebuffer=<n>  " + _("Maximum per-connection receive buffer, <n>*1000 bytes (default: 5000)") + "\n";
    strUsage += "  -maxsendbuffer=<n>     " + _("Maximum per-connection send buffer, <n>*1000 bytes (default: 1000)") + "\n";
    strUsage += "  -onion=<ip:port>       " + _("Use separate SOCKS5 proxy to reach peers via Tor hidden services (default: -proxy)") + "\n";
    strUsage += "  -onlynet=<net>         " + _("Only connect to nodes in network <net> (IPv4, IPv6 or Tor)") + "\n";
    strUsage += "  -port=<port>           " + _("Listen for connections on <port> (default: 8333 or testnet: 18333)") + "\n";
    strUsage += "  -proxy=<ip:port>       " + _("Connect through SOCKS proxy") + "\n";
    strUsage += "  -seednode=<ip>         " + _("Connect to a node to retrieve peer addresses, and disconnect") + "\n";
    strUsage += "  -socks=<n>             " + _("Select SOCKS version for -proxy (4 or 5, default: 5)") + "\n";
    strUsage += "  -timeout=<n>           " + _("Specify connection timeout in milliseconds (default: 5000)") + "\n";
#ifdef USE_UPNP
#if USE_UPNP
    strUsage += "  -upnp                  " + _("Use UPnP to map the listening port (default: 1 when listening)") + "\n";
#else
    strUsage += "  -upnp                  " + _("Use UPnP to map the listening port (default: 0)") + "\n";
#endif
#endif

#ifdef ENABLE_WALLET
    strUsage += "\n" + _("Wallet options:") + "\n";
    strUsage += "  -disablewallet         " + _("Do not load the wallet and disable wallet RPC calls") + "\n";
    strUsage += "  -paytxfee=<amt>        " + _("Fee per kB to add to transactions you send") + "\n";
    strUsage += "  -rescan                " + _("Rescan the block chain for missing wallet transactions") + " " + _("on startup") + "\n";
    strUsage += "  -salvagewallet         " + _("Attempt to recover private keys from a corrupt wallet.dat") + " " + _("on startup") + "\n";
    strUsage += "  -spendzeroconfchange   " + _("Spend unconfirmed change when sending transactions (default: 1)") + "\n";
    strUsage += "  -upgradewallet         " + _("Upgrade wallet to latest format") + " " + _("on startup") + "\n";
    strUsage += "  -wallet=<file>         " + _("Specify wallet file (within data directory)") + " " + _("(default: wallet.dat)") + "\n";
    strUsage += "  -walletnotify=<cmd>    " + _("Execute command when a wallet transaction changes (%s in cmd is replaced by TxID)") + "\n";
    strUsage += "  -zapwallettxes         " + _("Clear list of wallet transactions (diagnostic tool; implies -rescan)") + "\n";
#endif

    strUsage += "\n" + _("Debugging/Testing options:") + "\n";
    if (SysCfg().GetBoolArg("-help-debug", false))
    {
        strUsage += "  -benchmark             " + _("Show benchmark information (default: 0)") + "\n";
        strUsage += "  -checkpoints           " + _("Only accept block chain matching built-in checkpoints (default: 1)") + "\n";
        strUsage += "  -dblogsize=<n>         " + _("Flush database activity from memory pool to disk log every <n> megabytes (default: 100)") + "\n";
        strUsage += "  -disablesafemode       " + _("Disable safemode, override a real safe mode event (default: 0)") + "\n";
        strUsage += "  -testsafemode          " + _("Force safe mode (default: 0)") + "\n";
        strUsage += "  -dropmessagestest=<n>  " + _("Randomly drop 1 of every <n> network messages") + "\n";
        strUsage += "  -fuzzmessagestest=<n>  " + _("Randomly fuzz 1 of every <n> network messages") + "\n";
        strUsage += "  -flushwallet           " + _("Run a thread to flush wallet periodically (default: 1)") + "\n";
    }
    strUsage += "  -debug=<category>      " + _("Output debugging information (default: 0, supplying <category> is optional)") + "\n";
    strUsage += "                         " + _("If <category> is not supplied, output all debugging information.") + "\n";
    strUsage += "                         " + _("<category> can be:");
    strUsage +=                                 " addrman, alert, coindb, db, lock, rand, rpc, selectcoins, mempool, net"; // Don't translate these and qt below
    if (hmm == HMM_BITCOIN_QT)
        strUsage += ", qt";
    strUsage += ".\n";
    strUsage += "  -gen                   " + _("Generate coins (default: 0)") + "\n";
    strUsage += "  -genproclimit=<n>      " + _("Set the processor limit for when generation is on (-1 = unlimited, default: -1)") + "\n";
    strUsage += "  -help-debug            " + _("Show all debugging options (usage: --help -help-debug)") + "\n";
    strUsage += "  -logtimestamps         " + _("Prepend debug output with timestamp (default: 1)") + "\n";
    if (SysCfg().GetBoolArg("-help-debug", false))
    {
        strUsage += "  -limitfreerelay=<n>    " + _("Continuously rate-limit free transactions to <n>*1000 bytes per minute (default:15)") + "\n";
        strUsage += "  -maxsigcachesize=<n>   " + _("Limit size of signature cache to <n> entries (default: 50000)") + "\n";
    }
    strUsage += "  -mintxfee=<amt>        " + _("Fees smaller than this are considered zero fee (for transaction creation) (default:") + " " + FormatMoney(CTransaction::nMinTxFee) + ")" + "\n";
    strUsage += "  -minrelaytxfee=<amt>   " + _("Fees smaller than this are considered zero fee (for relaying) (default:") + " " + FormatMoney(CTransaction::nMinRelayTxFee) + ")" + "\n";
    strUsage += "  -printtoconsole        " + _("Send trace/debug info to console instead of debug.log file") + "\n";
    if (SysCfg().GetBoolArg("-help-debug", false))
    {
        strUsage += "  -printblock=<hash>     " + _("Print block on startup, if found in block index") + "\n";
        strUsage += "  -printblocktree        " + _("Print block tree on startup (default: 0)") + "\n";
        strUsage += "  -printpriority         " + _("Log transaction priority and fee per kB when mining blocks (default: 0)") + "\n";
        strUsage += "  -privdb                " + _("Sets the DB_PRIVATE flag in the wallet db environment (default: 1)") + "\n";
        strUsage += "  -regtest               " + _("Enter regression test mode, which uses a special chain in which blocks can be solved instantly.") + "\n";
        strUsage += "                         " + _("This is intended for regression testing tools and app development.") + "\n";
        strUsage += "                         " + _("In this mode -genproclimit controls how many blocks are generated immediately.") + "\n";
    }
    strUsage += "  -shrinkdebugfile       " + _("Shrink debug.log file on client startup (default: 1 when no -debug)") + "\n";
    strUsage += "  -testnet               " + _("Use the test network") + "\n";

    strUsage += "\n" + _("Block creation options:") + "\n";
    strUsage += "  -blockminsize=<n>      " + _("Set minimum block size in bytes (default: 0)") + "\n";
    strUsage += "  -blockmaxsize=<n>      " + strprintf(_("Set maximum block size in bytes (default: %d)"), DEFAULT_BLOCK_MAX_SIZE) + "\n";
    strUsage += "  -blockprioritysize=<n> " + strprintf(_("Set maximum size of high-priority/low-fee transactions in bytes (default: %d)"), DEFAULT_BLOCK_PRIORITY_SIZE) + "\n";

    strUsage += "\n" + _("RPC server options:") + "\n";
    strUsage += "  -server                " + _("Accept command line and JSON-RPC commands") + "\n";
    strUsage += "  -rpcuser=<user>        " + _("Username for JSON-RPC connections") + "\n";
    strUsage += "  -rpcpassword=<pw>      " + _("Password for JSON-RPC connections") + "\n";
    strUsage += "  -rpcport=<port>        " + _("Listen for JSON-RPC connections on <port> (default: 8332 or testnet: 18332)") + "\n";
    strUsage += "  -rpcallowip=<ip>       " + _("Allow JSON-RPC connections from specified IP address") + "\n";
    strUsage += "  -rpcthreads=<n>        " + _("Set the number of threads to service RPC calls (default: 4)") + "\n";

    strUsage += "\n" + _("RPC SSL options: (see the Tongxingpoints Wiki for SSL setup instructions)") + "\n";
    strUsage += "  -rpcssl                                  " + _("Use OpenSSL (https) for JSON-RPC connections") + "\n";
    strUsage += "  -rpcsslcertificatechainfile=<file.cert>  " + _("Server certificate file (default: server.cert)") + "\n";
    strUsage += "  -rpcsslprivatekeyfile=<file.pem>         " + _("Server private key (default: server.pem)") + "\n";
    strUsage += "  -rpcsslciphers=<ciphers>                 " + _("Acceptable ciphers (default: TLSv1.2+HIGH:TLSv1+HIGH:!SSLv2:!aNULL:!eNULL:!3DES:@STRENGTH)") + "\n";

    return strUsage;
}

struct CImportingNow
{
    CImportingNow() {
        assert(SysCfg().IsImporting() == false);
        SysCfg().SetImporting(true);
    }

    ~CImportingNow() {
    	assert(SysCfg().IsImporting() == true);
    	SysCfg().SetImporting(false);
    }
};

void ThreadImport(vector<boost::filesystem::path> vImportFiles)
{
    RenameThread("tongxingpoints-loadblk");

    // -reindex
    if (SysCfg().IsReindex()) {
        CImportingNow imp;
        int nFile = 0;
        while (true) {
            CDiskBlockPos pos(nFile, 0);
            FILE *file = OpenBlockFile(pos, true);
            if (!file)
                break;
            LogPrint("INFO","Reindexing block file blk%05u.dat...\n", (unsigned int)nFile);
            LoadExternalBlockFile(file, &pos);
            nFile++;
        }
        pblocktree->WriteReindexing(false);
        SysCfg().SetReIndex(false);
        LogPrint("INFO","Reindexing finished\n");
        // To avoid ending up in a situation without genesis block, re-try initializing (no-op if reindexing worked):
        InitBlockIndex();
    }

    // hardcoded $DATADIR/bootstrap.dat
    filesystem::path pathBootstrap = GetDataDir() / "bootstrap.dat";
    if (filesystem::exists(pathBootstrap)) {
        FILE *file = fopen(pathBootstrap.string().c_str(), "rb");
        if (file) {
            CImportingNow imp;
            filesystem::path pathBootstrapOld = GetDataDir() / "bootstrap.dat.old";
            LogPrint("INFO","Importing bootstrap.dat...\n");
            LoadExternalBlockFile(file);
            RenameOver(pathBootstrap, pathBootstrapOld);
        } else {
            LogPrint("INFO","Warning: Could not open bootstrap file %s\n", pathBootstrap.string());
        }
    }

    // -loadblock=
	for (const auto &path : vImportFiles) {
		FILE *file = fopen(path.string().c_str(), "rb");
		if (file) {
			CImportingNow imp;
			LogPrint("INFO", "Importing blocks file %s...\n", path.string());
			LoadExternalBlockFile(file);
		} else {
			LogPrint("INFO", "Warning: Could not open blocks file %s\n", path.string());
		}
	}
}


/** Initialize Tongxingpoints.
 *  @pre Parameters should be parsed and config file should be read.
 */
bool AppInit2(boost::thread_group& threadGroup)
{
    // ********************************************************* Step 1: setup
#ifdef _MSC_VER
    // Turn off Microsoft heap dump noise
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, CreateFileA("NUL", GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, 0));
#endif
#if _MSC_VER >= 1400
    // Disable confusing "helpful" text message on abort, Ctrl-C
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
#ifdef WIN32
    // Enable Data Execution Prevention (DEP)
    // Minimum supported OS versions: WinXP SP3, WinVista >= SP1, Win Server 2008
    // A failure is non-critical and needs no further attention!
#ifndef PROCESS_DEP_ENABLE
    // We define this here, because GCCs winbase.h limits this to _WIN32_WINNT >= 0x0601 (Windows 7),
    // which is not correct. Can be removed, when GCCs winbase.h is fixed!
#define PROCESS_DEP_ENABLE 0x00000001
#endif
    typedef BOOL (WINAPI *PSETPROCDEPPOL)(DWORD);
    PSETPROCDEPPOL setProcDEPPol = (PSETPROCDEPPOL)GetProcAddress(GetModuleHandleA("Kernel32.dll"), "SetProcessDEPPolicy");
    if (setProcDEPPol != NULL) setProcDEPPol(PROCESS_DEP_ENABLE);

    // Initialize Windows Sockets
    WSADATA wsadata;
    int ret = WSAStartup(MAKEWORD(2,2), &wsadata);
    if (ret != NO_ERROR || LOBYTE(wsadata.wVersion ) != 2 || HIBYTE(wsadata.wVersion) != 2)
    {
        return InitError(strprintf("Error: Winsock library failed to start (WSAStartup returned error %d)", ret));
    }
#endif
#ifndef WIN32
    umask(077);

    // Clean shutdown on SIGTERM
    struct sigaction sa;
    sa.sa_handler = HandleSIGTERM;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    // Reopen debug.log on SIGHUP
    struct sigaction sa_hup;
    sa_hup.sa_handler = HandleSIGHUP;
    sigemptyset(&sa_hup.sa_mask);
    sa_hup.sa_flags = 0;
    sigaction(SIGHUP, &sa_hup, NULL);

#if defined (__SVR4) && defined (__sun)
    // ignore SIGPIPE on Solaris
    signal(SIGPIPE, SIG_IGN);
#endif
#endif

    CUIServer::StartServer();

    if(SysCfg().GetBoolArg("-ui", false)) {
    	threadGroup.create_thread(ThreadSendMessageToUI);
    }
    // ********************************************************* Step 2: parameter interactions

    if (SysCfg().IsArgCount("-bind")) {
        // when specifying an explicit binding address, you want to listen on it
        // even when -connect or -proxy is specified
        if (SysCfg().SoftSetBoolArg("-listen", true))
            LogPrint("INFO","AppInit2 : parameter interaction: -bind set -> setting -listen=1\n");
    }

    if (SysCfg().IsArgCount("-connect") && SysCfg().GetMultiArgs("-connect").size() > 0) {
        // when only connecting to trusted nodes, do not seed via DNS, or listen by default
        if (SysCfg().SoftSetBoolArg("-dnsseed", false))
            LogPrint("INFO","AppInit2 : parameter interaction: -connect set -> setting -dnsseed=0\n");
        if (SysCfg().SoftSetBoolArg("-listen", false))
            LogPrint("INFO","AppInit2 : parameter interaction: -connect set -> setting -listen=0\n");
    }

    if (SysCfg().IsArgCount("-proxy")) {
        // to protect privacy, do not listen by default if a default proxy server is specified
        if (SysCfg().SoftSetBoolArg("-listen", false))
            LogPrint("INFO","AppInit2 : parameter interaction: -proxy set -> setting -listen=0\n");
    }

    if (!SysCfg().GetBoolArg("-listen", true)) {
        // do not map ports or try to retrieve public IP when not listening (pointless)
        if (SysCfg().SoftSetBoolArg("-upnp", false))
            LogPrint("INFO","AppInit2 : parameter interaction: -listen=0 -> setting -upnp=0\n");
        if (SysCfg().SoftSetBoolArg("-discover", false))
            LogPrint("INFO","AppInit2 : parameter interaction: -listen=0 -> setting -discover=0\n");
    }

    if (SysCfg().IsArgCount("-externalip")) {
        // if an explicit public IP is specified, do not try to find others
        if (SysCfg().SoftSetBoolArg("-discover", false))
            LogPrint("INFO","AppInit2 : parameter interaction: -externalip set -> setting -discover=0\n");
    }

    if (SysCfg().GetBoolArg("-salvagewallet", false)) {
        // Rewrite just private keys: rescan to find transactions
        if (SysCfg().SoftSetBoolArg("-rescan", true))
            LogPrint("INFO","AppInit2 : parameter interaction: -salvagewallet=1 -> setting -rescan=1\n");
    }

    // -zapwallettx implies a rescan
    if (SysCfg().GetBoolArg("-zapwallettxes", false)) {
        if (SysCfg().SoftSetBoolArg("-rescan", true))
            LogPrint("INFO","AppInit2 : parameter interaction: -zapwallettxes=1 -> setting -rescan=1\n");
    }

    // Make sure enough file descriptors are available
    int nBind = max((int)SysCfg().IsArgCount("-bind"), 1);
    nMaxConnections = SysCfg().GetArg("-maxconnections", 125);
    nMaxConnections = max(min(nMaxConnections, (int)(FD_SETSIZE - nBind - MIN_CORE_FILEDESCRIPTORS)), 0);
    int nFD = RaiseFileDescriptorLimit(nMaxConnections + MIN_CORE_FILEDESCRIPTORS);
    if (nFD < MIN_CORE_FILEDESCRIPTORS)
        return InitError(_("Not enough file descriptors available."));
    if (nFD - MIN_CORE_FILEDESCRIPTORS < nMaxConnections)
		nMaxConnections = nFD - MIN_CORE_FILEDESCRIPTORS;

    // ********************************************************* Step 3: parameter-to-internal-flags

//	fDebug = !mapMultiArgs["-debug"].empty();
//    // Special-case: if -debug=0/-nodebug is set, turn off debugging messages
//    const vector<string>& categories = mapMultiArgs["-debug"];
//    if (GetBoolArg("-nodebug", false) || find(categories.begin(), categories.end(), string("0")) != categories.end())
//        fDebug = false;

    // Check for -debugnet (deprecated)
//    if (SysCfg().GetBoolArg("-debugnet", false))
//        InitWarning(_("Warning: Deprecated argument -debugnet ignored, use -debug=net"));

    SysCfg().SetBenchMark(SysCfg().GetBoolArg("-benchmark", false));
    mempool.setSanityCheck(SysCfg().GetBoolArg("-checkmempool", RegTest()));
    Checkpoints::fEnabled = SysCfg().GetBoolArg("-checkpoints", true);



//    fServer = GetBoolArg("-server", false);
//    fPrintToConsole = GetBoolArg("-printtoconsole", false);
//    fLogTimestamps = GetBoolArg("-logtimestamps", true);
    setvbuf(stdout, NULL, _IOLBF, 0);




//    if (mapArgs.count("-timeout"))
//    {
//        int nNewTimeout = GetArg("-timeout", 5000);
//        if (nNewTimeout > 0 && nNewTimeout < 600000)
//            nConnectTimeout = nNewTimeout;
//    }

    // Continue to put "/P2SH/" in the coinbase to monitor
    // BIP16 support.
    // This can be removed eventually...
//    const char* pszP2SH = "/P2SH/";
//    COINBASE_FLAGS << vector<unsigned char>(pszP2SH, pszP2SH+strlen(pszP2SH));

    // Fee-per-kilobyte amount considered the same as "free"
    // If you are mining, be careful setting this:
    // if you set it to zero then
    // a transaction spammer can cheaply fill blocks using
    // 1-satoshi-fee transactions. It should be set above the real
    // cost to you of processing a transaction.
    if (SysCfg().IsArgCount("-mintxfee"))
    {
        int64_t n = 0;
        if (ParseMoney(SysCfg().GetArg("-mintxfee", ""), n) && n > 0)
            CTransaction::nMinTxFee = n;
        else
            return InitError(strprintf(_("Invalid amount for -mintxfee=<amount>: '%s'"), SysCfg().GetArg("-mintxfee", "")));
    }
    if (SysCfg().IsArgCount("-minrelaytxfee"))
    {
        int64_t n = 0;
        if (ParseMoney(SysCfg().GetArg("-minrelaytxfee", ""), n) && n > 0)
            CTransaction::nMinRelayTxFee = n;
        else
            return InitError(strprintf(_("Invalid amount for -minrelaytxfee=<amount>: '%s'"), SysCfg().GetArg("-minrelaytxfee", "")));
    }


    if (SysCfg().GetTxFee() > nHighTransactionFeeWarning)
    {
       InitWarning(_("Warning: -paytxfee is set very high! This is the transaction fee you will pay if you send a transaction."));
    }

    // ********************************************************* Step 4: application initialization: dir lock, daemonize, pidfile, debug log

    string strDataDir = GetDataDir().string();

    // Make sure only a single Tongxingpoints process is using the data directory.
    boost::filesystem::path pathLockFile = GetDataDir() / ".lock";
    FILE* file = fopen(pathLockFile.string().c_str(), "a"); // empty lock file; created if it doesn't exist.
    if (file) fclose(file);
    static boost::interprocess::file_lock lock(pathLockFile.string().c_str());
    if (!lock.try_lock())
        return InitError(strprintf(_("Cannot obtain a lock on data directory %s. tongxingpoints Core is probably already running."), strDataDir));

//    if (GetBoolArg("-shrinkdebugfile", !fDebug))
//        ShrinkDebugFile();

    LogPrint("INFO","\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n");
    LogPrint("INFO","tongxingpoints version %s (%s)\n", FormatFullVersion().c_str(), CLIENT_DATE);
    printf("tongxingpoints version %s (%s)\n", FormatFullVersion().c_str(), CLIENT_DATE.c_str());
    LogPrint("INFO","Using OpenSSL version %s\n", SSLeay_version(SSLEAY_VERSION));
    printf("Using OpenSSL version %s\n", SSLeay_version(SSLEAY_VERSION));
//    if (!fLogTimestamps)
    LogPrint("INFO","Startup time: %s\n", DateTimeStrFormat("%Y-%m-%d %H:%M:%S", GetTime()));
    printf("Startup time: %s\n", DateTimeStrFormat("%Y-%m-%d %H:%M:%S", GetTime()).c_str());
    LogPrint("INFO","Default data directory %s\n", GetDefaultDataDir().string());
    printf("Default data directory %s\n", GetDefaultDataDir().string().c_str());
    LogPrint("INFO","Using data directory %s\n", strDataDir);
    printf("Using data directory %s\n", strDataDir.c_str());
    LogPrint("INFO","Using at most %i connections (%i file descriptors available)\n", nMaxConnections, nFD);
    printf("Using at most %i connections (		%i file descriptors available)\n", nMaxConnections, nFD);
    ostringstream strErrors;



    int64_t nStart;

    // ********************************************************* Step 5: verify wallet database integrity

    // ********************************************************* Step 6: network initialization

    RegisterNodeSignals(GetNodeSignals());

    int nSocksVersion = SysCfg().GetArg("-socks", 5);
    if (nSocksVersion != 4 && nSocksVersion != 5)
        return InitError(strprintf(_("Unknown -socks proxy version requested: %i"), nSocksVersion));

	if (SysCfg().IsArgCount("-onlynet")) {
		set<enum Network> nets;
		vector<string> tmp = SysCfg().GetMultiArgs("-onlynet");
		for (auto& snet : tmp) {
			enum Network net = ParseNetwork(snet);
			if (net == NET_UNROUTABLE)
				return InitError(strprintf(_("Unknown network specified in -onlynet: '%s'"), snet));
			nets.insert(net);
		}
		for (int n = 0; n < NET_MAX; n++) {
			enum Network net = (enum Network) n;
			if (!nets.count(net))
				SetLimited(net);
		}
	}

    CService addrProxy;
    bool fProxy = false;
	if (SysCfg().IsArgCount("-proxy")) {
		addrProxy = CService(SysCfg().GetArg("-proxy", ""), 9053);
		if (!addrProxy.IsValid())
			return InitError(strprintf(_("Invalid -proxy address: '%s'"), SysCfg().GetArg("-proxy", "")));

		if (!IsLimited(NET_IPV4))
			SetProxy(NET_IPV4, addrProxy, nSocksVersion);
		if (nSocksVersion > 4) {
			if (!IsLimited(NET_IPV6))
				SetProxy(NET_IPV6, addrProxy, nSocksVersion);
			SetNameProxy(addrProxy, nSocksVersion);
		}
		fProxy = true;
	}

    // -onion can override normal proxy, -noonion disables tor entirely
    // -tor here is a temporary backwards compatibility measure
	if (SysCfg().IsArgCount("-tor"))
		LogPrint("INFO", "Notice: option -tor has been replaced with -onion and will be removed in a later version.\n");
	if (!(SysCfg().GetArg("-onion", "") == "0") && !(SysCfg().GetArg("-tor", "") == "0")
			&& (fProxy || SysCfg().IsArgCount("-onion") || SysCfg().IsArgCount("-tor"))) {
		CService addrOnion;
		if (!SysCfg().IsArgCount("-onion") && !SysCfg().IsArgCount("-tor"))
			addrOnion = addrProxy;
		else
			addrOnion =
					SysCfg().IsArgCount("-onion") ?
							CService(SysCfg().GetArg("-onion", ""), 9053) :
							CService(SysCfg().GetArg("-tor", ""), 9053);
		if (!addrOnion.IsValid())
			return InitError(strprintf(_("Invalid -onion address: '%s'"), SysCfg().IsArgCount("-onion")?SysCfg().GetArg("-onion", ""):SysCfg().GetArg("-tor", "")));
		SetProxy(NET_TOR, addrOnion, 5);
		SetReachable(NET_TOR);
	}

    // see Step 2: parameter interactions for more information about these
    fNoListen = !SysCfg().GetBoolArg("-listen", true);
    fDiscover = SysCfg().GetBoolArg("-discover", true);
    fNameLookup = SysCfg().GetBoolArg("-dns", true);

    bool fBound = false;
    if (!fNoListen) {
		if (SysCfg().IsArgCount("-bind")) {
			vector<string> tmp = SysCfg().GetMultiArgs("-bind");
			for (const auto& strBind : tmp) {
				CService addrBind;
				if (!Lookup(strBind.c_str(), addrBind, GetListenPort(), false))
					return InitError(strprintf(_("Cannot resolve -bind address: '%s'"), strBind));
				fBound |= Bind(addrBind, (BF_EXPLICIT | BF_REPORT_ERROR));
			}
		}
        else {
            struct in_addr inaddr_any;
            inaddr_any.s_addr = INADDR_ANY;
            fBound |= Bind(CService(in6addr_any, GetListenPort()), BF_NONE);
            fBound |= Bind(CService(inaddr_any, GetListenPort()), !fBound ? BF_REPORT_ERROR : BF_NONE);
        }
        if (!fBound)
            return InitError(_("Failed to listen on any port. Use -listen=0 if you want this."));
    }

	if (SysCfg().IsArgCount("-externalip")) {
		vector<string> tmp = SysCfg().GetMultiArgs("-externalip");
		for (const auto& strAddr : tmp) {
			CService addrLocal(strAddr, GetListenPort(), fNameLookup);
			if (!addrLocal.IsValid())
				return InitError(strprintf(_("Cannot resolve -externalip address: '%s'"), strAddr));
			AddLocal(CService(strAddr, GetListenPort(), fNameLookup), LOCAL_MANUAL);
		}
	}

	{
		vector<string> tmp = SysCfg().GetMultiArgs("-seednode");
		for (auto strDest : tmp)
			AddOneShot(strDest);
	}

    // ********************************************************* Step 7: load block chain

    SysCfg().SetReIndex(SysCfg().GetBoolArg("-reindex", false) );


    filesystem::path blocksDir = GetDataDir() / "blocks";
    if (!filesystem::exists(blocksDir))
    {
        filesystem::create_directories(blocksDir);
//        bool linked = false;
//        for (unsigned int i = 1; i < 10000; i++) {
//            filesystem::path source = GetDataDir() / strprintf("blk%04u.dat", i);
//            if (!filesystem::exists(source)) break;
//            filesystem::path dest = blocksDir / strprintf("blk%05u.dat", i-1);
//            try {
//                filesystem::create_hard_link(source, dest);
//                LogPrint("INFO","Hardlinked %s -> %s\n", source.string(), dest.string());
//                linked = true;
//            } catch (filesystem::filesystem_error & e) {
//                // Note: hardlink creation failing is not a disaster, it just means
//                // blocks will get re-downloaded from peers.
//                LogPrint("INFO","Error hardlinking blk%04u.dat : %s\n", i, e.what());
//                break;
//            }
//        }
//        if (linked)
//        {
//        	SysCfg().SetReIndex(true);
//        }
    }

    // cache size calculations
    size_t nTotalCache = (SysCfg().GetArg("-dbcache", nDefaultDbCache) << 20);
    if (nTotalCache < (nMinDbCache << 20))
        nTotalCache = (nMinDbCache << 20); // total cache cannot be less than nMinDbCache
    else if (nTotalCache > (nMaxDbCache << 20))
        nTotalCache = (nMaxDbCache << 20); // total cache cannot be greater than nMaxDbCache
    size_t nBlockTreeDBCache = nTotalCache / 8;
    if (nBlockTreeDBCache > (1 << 21) && !SysCfg().GetBoolArg("-txindex", false))
        nBlockTreeDBCache = (1 << 21); // block tree db cache shouldn't be larger than 2 MiB
    nTotalCache -= nBlockTreeDBCache;
    size_t nAccountDBCache = nTotalCache / 2; // use half of the remaining cache for coindb cache
    nTotalCache -= nAccountDBCache;
    size_t nScriptCacheSize = nTotalCache / 2;
    nTotalCache -= nScriptCacheSize;
    size_t nTxCacheSize = nTotalCache / 2;

    SysCfg().SetViewCacheSize(nTotalCache / 300); // coins in memory require around 300 bytes

    try {
        pwalletMain = CWallet::getinstance();
        RegisterWallet(pwalletMain);
//      DBErrors nLoadWalletRet = pwalletMain->LoadWallet(false);
        pwalletMain->LoadWallet(false);
    	} catch (std::exception &e) {
    		cout<< "load wallet failed:"<<  e.what() << endl;
    	}

    //load checkpoint
    SyncData::CSyncDataDb db;
	if (db.InitializeSyncDataDb(GetDataDir() / "syncdata")) {
		if (!Checkpoints::LoadCheckpoint()) {
			LogPrint("INFO", "load check point error!\n");
			return false;
		}
	}

    bool fLoaded = false;
    while (!fLoaded) {
        bool fReset = SysCfg().IsReindex();
        string strLoadError;

        uiInterface.InitMessage(_("Loading block index..."));

        nStart = GetTimeMillis();
        do {
            try {
                UnloadBlockIndex();
                delete pAccountViewDB;
                delete pblocktree;
                delete pAccountViewTip;
                delete pTxCacheDB;
                delete pTxCacheTip;
                delete pScriptDB;
                delete pScriptDBTip;

                pblocktree = new CBlockTreeDB(nBlockTreeDBCache, false, SysCfg().IsReindex());
                pAccountViewDB = new CAccountViewDB(nAccountDBCache, false, SysCfg().IsReindex());
                pAccountViewTip =  new CAccountViewCache(*pAccountViewDB,true);
                pTxCacheDB = new CTransactionDB(nTxCacheSize, false, SysCfg().IsReindex());
                pTxCacheTip = new CTransactionDBCache(*pTxCacheDB,true);
                pScriptDB = new CScriptDB(nScriptCacheSize, false , SysCfg().IsReindex());
                pScriptDBTip = new CScriptDBViewCache(*pScriptDB,true);


                if (SysCfg().IsReindex())
                    pblocktree->WriteReindexing(true);

				mempool.SetAccountViewDB(pAccountViewTip);
				mempool.SetScriptDBViewDB(pScriptDBTip);

                if (!LoadBlockIndex()) {
                    strLoadError = _("Error loading block database");
                    break;
                }


                // If the loaded chain has a wrong genesis, bail out immediately
                // (we're likely using a testnet datadir, or the other way around).
        
	            if (!mapBlockIndex.empty() && chainActive.Genesis() == NULL)
                    return InitError(_("Incorrect or no genesis block found. Wrong datadir for network?"));

                // Initialize the block index (no-op if non-empty database was already loaded)
                if (!InitBlockIndex()) {
                    strLoadError = _("Error initializing block database");
                    break;
                }

                // Check for changed -txindex state
                if (SysCfg().IsTxIndex() != SysCfg().GetBoolArg("-txindex", true)) {
                    strLoadError = _("You need to rebuild the database using -reindex to change -txindex");
                    break;
                }

				if (!pTxCacheTip->LoadTransaction()) {
					strLoadError = _("Error loading transaction cache database");
				}

                uiInterface.InitMessage(_("Verifying blocks..."));
                if (!VerifyDB(SysCfg().GetArg("-checklevel", 3),
                		SysCfg().GetArg("-checkblocks", 288))) {
                    strLoadError = _("Corrupted block database detected");
                    break;
                }

            } catch(std::exception &e) {
                LogPrint("INFO","%s\n", e.what());
                strLoadError = _("Error opening block database");
                break;
            }

            fLoaded = true;
        } while(false);

        uiInterface.InitMessage(_("Verifying Finished"));
        uiInterface.InitMessage(_("Sync Tx"));
        if (!fLoaded) {
            // first suggest a reindex
            if (!fReset) {
                bool fRet = uiInterface.ThreadSafeMessageBox(
                    strLoadError + ".\n\n" + _("Do you want to rebuild the block database now?"),
                    "", CClientUIInterface::MSG_ERROR | CClientUIInterface::BTN_ABORT);
                if (fRet) {
                	SysCfg().SetReIndex(true);
                    fRequestShutdown = false;
                } else {
                    LogPrint("INFO","Aborted block database rebuild. Exiting.\n");
                    return false;
                }
            } else {
                return InitError(strLoadError);
            }
        }
    }

    // As LoadBlockIndex can take several minutes, it's possible the user
    // requested to kill the GUI during the last operation. If so, exit.
    // As the program has not fully started yet, Shutdown() is possibly overkill.
    if (fRequestShutdown)
    {
        LogPrint("INFO","Shutdown requested. Exiting.\n");
        return false;
    }
    LogPrint("INFO"," block index %15dms\n", GetTimeMillis() - nStart);

    if (SysCfg().GetBoolArg("-printblockindex", false) || SysCfg().GetBoolArg("-printblocktree", false))
    {
        PrintBlockTree();
        return false;
    }



    if (SysCfg().IsArgCount("-printblock"))
    {
        string strMatch = SysCfg().GetArg("-printblock", "");
        int nFound = 0;
        for (map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.begin(); mi != mapBlockIndex.end(); ++mi)
        {
            uint256 hash = (*mi).first;
            if (strncmp(hash.ToString().c_str(), strMatch.c_str(), strMatch.size()) == 0)
            {
                CBlockIndex* pindex = (*mi).second;
                CBlock block;
                ReadBlockFromDisk(block, pindex);
                block.BuildMerkleTree();
                block.print(*pAccountViewTip);
                LogPrint("INFO","\n");
                nFound++;
            }
        }
        if (nFound == 0)
            LogPrint("INFO","No blocks matching %s were found\n", strMatch);
        return false;
    }



    // ********************************************************* Step 9: import blocks

    // scan for better chains in the block chain database, that are not yet connected in the active best chain
    CValidationState state;
    if (!ActivateBestChain(state)){
        strErrors << "Failed to connect best block";
        } else {
        }
    // check current chain according to checkpoint
    CBlockIndex* pcheckpoint = Checkpoints::GetLastCheckpoint(mapBlockIndex);
    if(NULL != pcheckpoint) {
    	CheckActiveChain(pcheckpoint->nHeight, pcheckpoint->GetBlockHash());
    }
    vector<boost::filesystem::path> vImportFiles;
    if (SysCfg().IsArgCount("-loadblock"))
	{
		vector<string>tmp = SysCfg().GetMultiArgs("-loadblock");
		for(auto strFile:tmp)
			vImportFiles.push_back(strFile);
	}
    threadGroup.create_thread(boost::bind(&ThreadImport, vImportFiles));

    // ********************************************************* Step 10: load peers

    uiInterface.InitMessage(_("Loading addresses..."));

    nStart = GetTimeMillis();

    {
        CAddrDB adb;
        if (!adb.Read(addrman))
            LogPrint("INFO","Invalid or missing peers.dat; recreating\n");
    }

    LogPrint("INFO","Loaded %i addresses from peers.dat  %dms\n",
           addrman.size(), GetTimeMillis() - nStart);

    // ********************************************************* Step 11: start node

    if (!CheckDiskSpace())
        return false;

    if (!strErrors.str().empty())
        return InitError(strErrors.str());

    RandAddSeedPerfmon();

    //// debug print
    LogPrint("INFO","mapBlockIndex.size() = %u\n",   mapBlockIndex.size());
    LogPrint("INFO","nBestHeight = %d\n",            chainActive.Height());


    StartNode(threadGroup);
    // InitRPCMining is needed here so getwork/getblocktemplate in the GUI debug console works properly.
    InitRPCMining();
	if (SysCfg().IsServer())
        StartRPCThreads();


    // Generate coins in the background
	if (pwalletMain) {
		GenerateTongxingpointsBlock(SysCfg().GetBoolArg("-gen", false), pwalletMain, SysCfg().GetArg("-genproclimit", -1));
		pwalletMain->ResendWalletTransactions();
        threadGroup.create_thread(boost::bind(&ThreadFlushWalletDB, boost::ref(pwalletMain->strWalletFile)));

       //resend unconfirmed tx
       threadGroup.create_thread(boost::bind(&ThreadRelayTx, pwalletMain));
	}
    // ********************************************************* Step 12: finished

    uiInterface.InitMessage("initialize end");

    return !fRequestShutdown;
}
