// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2014-2015 The Tongxingpoints developers
// Copyright (c) 2016 The Tongxingpoints developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "miner.h"

#include "core.h"
#include "main.h"
#include "tx.h"
#include "net.h"

#include "./wallet/wallet.h"
extern CWallet* pwalletMain;
extern void SetMinerStatus(bool bstatue );
//////////////////////////////////////////////////////////////////////////////
//
// TongxingpointsMiner
//

//int static FormatHashBlocks(void* pbuffer, unsigned int len) {
//	unsigned char* pdata = (unsigned char*) pbuffer;
//	unsigned int blocks = 1 + ((len + 8) / 64);
//	unsigned char* pend = pdata + 64 * blocks;
//	memset(pdata + len, 0, 64 * blocks - len);
//	pdata[len] = 0x80;
//	unsigned int bits = len * 8;
//	pend[-1] = (bits >> 0) & 0xff;
//	pend[-2] = (bits >> 8) & 0xff;
//	pend[-3] = (bits >> 16) & 0xff;
//	pend[-4] = (bits >> 24) & 0xff;
//	return blocks;
//}

static const unsigned int pSHA256InitState[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f,
		0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };

//void SHA256Transform(void* pstate, void* pinput, const void* pinit) {
//	SHA256_CTX ctx;
//	unsigned char data[64];
//
//	SHA256_Init(&ctx);
//
//	for (int i = 0; i < 16; i++)
//		((uint32_t*) data)[i] = ByteReverse(((uint32_t*) pinput)[i]);
//
//	for (int i = 0; i < 8; i++)
//		ctx.h[i] = ((uint32_t*) pinit)[i];
//
//	SHA256_Update(&ctx, data, sizeof(data));
//	for (int i = 0; i < 8; i++)
//		((uint32_t*) pstate)[i] = ctx.h[i];
//}

// Some explaining would be appreciated
class COrphan {
public:
	const CTransaction* ptx;
	set<uint256> setDependsOn;
	double dPriority;
	double dFeePerKb;

	COrphan(const CTransaction* ptxIn) {
		ptx = ptxIn;
		dPriority = dFeePerKb = 0;
	}

	void print() const {
		LogPrint("INFO","COrphan(hash=%s, dPriority=%.1f, dFeePerKb=%.1f)\n", ptx->GetHash().ToString(), dPriority,
				dFeePerKb);
		for (const auto& hash : setDependsOn)
			LogPrint("INFO", "   setDependsOn %s\n", hash.ToString());
	}
};

uint64_t nLastBlockTx = 0;    // 块中交易的总笔数,不含coinbase
uint64_t nLastBlockSize = 0;  //被创建的块 尺寸

//base on the last 50 blocks
int GetElementForBurn(CBlockIndex* pindex)
{
	if (NULL == pindex) {
		return INIT_FUEL_RATES;
	}
	int nBlock = SysCfg().GetArg("-blocksizeforburn", DEFAULT_BURN_BLOCK_SIZE);
	if (nBlock * 2 >= pindex->nHeight - 1) {
		return INIT_FUEL_RATES;
	} else {
		int64_t nTotalStep(0);
		int64_t nAverateStep(0);
		CBlockIndex * pTemp = pindex;
		for (int ii = 0; ii < nBlock; ii++) {
			nTotalStep += pTemp->nFuel / pTemp->nFuelRate * 100;
			pTemp = pTemp->pprev;
		}
		nAverateStep = nTotalStep / nBlock;
		int newFuelRate(0);
		if (nAverateStep < MAX_BLOCK_RUN_STEP * 0.75) {
			newFuelRate = pindex->nFuelRate * 0.9;
		} else if (nAverateStep > MAX_BLOCK_RUN_STEP * 0.85) {
			newFuelRate = pindex->nFuelRate * 1.1;
		} else {
			newFuelRate = pindex->nFuelRate;
		}
		if (newFuelRate < MIN_FUEL_RATES)
			newFuelRate = MIN_FUEL_RATES;
		LogPrint("fuel", "preFuelRate=%d fuelRate=%d, nHeight=%d\n", pindex->nFuelRate, newFuelRate, pindex->nHeight);
		return newFuelRate;
	}


}

// We want to sort transactions by priority and fee, so:

void GetPriorityTx(vector<TxPriority> &vecPriority, int nFuelRate) {
	vecPriority.reserve(mempool.mapTx.size());
	// Priority order to process transactions
	list<COrphan> vOrphan; // list memory doesn't move
	double dPriority = 0;
	for (map<uint256, CTxMemPoolEntry>::iterator mi = mempool.mapTx.begin(); mi != mempool.mapTx.end(); ++mi) {
		CBaseTransaction *pBaseTx = mi->second.GetTx().get();

		if (uint256() == std::move(pTxCacheTip->IsContainTx(std::move(pBaseTx->GetHash())))) {
			unsigned int nTxSize = ::GetSerializeSize(*pBaseTx, SER_NETWORK, PROTOCOL_VERSION);
			double dFeePerKb = double(pBaseTx->GetFee() - pBaseTx->GetFuel(nFuelRate))/ (double(nTxSize) / 1000.0);
			dPriority = 1000.0 / double(nTxSize);
			LogPrint("TX", "fee %d, fuelRate %d, fuel %d, dFeePerKb %f",
					 pBaseTx->GetFee(), nFuelRate, pBaseTx->GetFuel(nFuelRate), dFeePerKb);
			LogPrint("TX", "ntxtype %d resend tx hash:%s \n nTxSize %d, ", pBaseTx->nTxType, pBaseTx->GetHash().ToString());
			vecPriority.push_back(TxPriority(dPriority, dFeePerKb, mi->second.GetTx()));
		} else {
			LogPrint("TX", "ntxtype %d resend tx hash:%s \n", pBaseTx->nTxType, pBaseTx->GetHash().ToString());
		}

	}
}

void IncrementExtraNonce(CBlock* pblock, CBlockIndex* pindexPrev, unsigned int& nExtraNonce) {
	// Update nExtraNonce
	static uint256 hashPrevBlock;
	if (hashPrevBlock != pblock->GetHashPrevBlock()) {
		nExtraNonce = 0;
		hashPrevBlock = pblock->GetHashPrevBlock();
	}
	++nExtraNonce;
//	unsigned int nHeight = pindexPrev->nHeight + 1; // Height first in coinbase required for block.version=2
//    pblock->vtx[0].vin[0].scriptSig = (CScript() << nHeight << CBigNum(nExtraNonce)) + COINBASE_FLAGS;
//    assert(pblock->vtx[0].vin[0].scriptSig.size() <= 100);

	pblock->GetHashMerkleRoot() = pblock->BuildMerkleTree();
}

struct CAccountComparator {
	bool operator()(const CAccount &a, const CAccount&b) {
		// First sort by acc over 30days
		CAccount &a1 = const_cast<CAccount &>(a);
		CAccount &b1 = const_cast<CAccount &>(b);
		if (a1.GetAccountPos(chainActive.Tip()->nHeight+1) < b1.GetAccountPos(chainActive.Tip()->nHeight+1)) {
			return false;
		}

		return true;
	}
};

struct PosTxInfo {
	int nVersion;
	uint256 hashPrevBlock;
	uint256 hashMerkleRoot;
	uint64_t nValues;
	unsigned int nTime;
	unsigned int nNonce;
    unsigned int nHeight;
    int64_t    nFuel;
    int nFuelRate;
	uint256 GetHash()
	{
		CDataStream ds(SER_DISK, CLIENT_VERSION);
		ds<<nVersion;
		ds<<hashPrevBlock;
		ds<<hashMerkleRoot;
		ds<<nValues;
		ds<<nTime;
		ds<<nNonce;
		ds<<nHeight;
		ds<<nFuel;
		ds<<nFuelRate;
		return Hash(ds.begin(),ds.end());
	}
	string ToString() {
		return strprintf("nVersion=%d, hashPreBlock=%s, hashMerkleRoot=%s, nValue=%ld, nTime=%ld, nNonce=%ld, nHeight=%d, nFuel=%ld nFuelRate=%d\n",
		nVersion, hashPrevBlock.GetHex(), hashMerkleRoot.GetHex(), nValues, nTime, nNonce, nHeight, nFuel, nFuelRate);
	}
};

uint256 GetAdjustHash(const uint256 TargetHash, const uint64_t nPos, const int nCurHeight) {

	uint64_t posacc = nPos/COIN;
	posacc /= SysCfg().GetIntervalPos();
	posacc = max(posacc, (uint64_t) 1);
	arith_uint256 adjusthash = UintToArith256(TargetHash); //adjust nbits
	arith_uint256 minhash = SysCfg().ProofOfWorkLimit();
//	LogPrint("INFO", "minhash %s", minhash.GetHex());

	while (posacc) {
		adjusthash = adjusthash << 1;
		posacc = posacc >> 1;
		if (adjusthash > minhash) {
			adjusthash = minhash;
			break;
		}
	}

	return std::move(ArithToUint256(adjusthash));
}

bool CreatePosTx(const CBlockIndex *pPrevIndex, CBlock *pBlock, set<CKeyID>&setCreateKey, CAccountViewCache &view,
		CTransactionDBCache &txCache, CScriptDBViewCache &scriptCache) {
	set<CKeyID> setKeyID;
	setKeyID.clear();

	set<CAccount, CAccountComparator> setAcctInfo;

	{
		LOCK2(cs_main, pwalletMain->cs_wallet);

		if((unsigned int)(chainActive.Tip()->nHeight + 1) !=  pBlock->GetHeight())
			return false;
		pwalletMain->GetKeys(setKeyID, true);                         // first:get keyID from pwalletMain
		if (setKeyID.empty()) {
			return ERRORMSG("CreatePosTx setKeyID empty");
		}

		LogPrint("INFO","\n\nCreatePosTx block time:%d\n",  pBlock->GetTime());
		for(const auto &keyid:setKeyID) {                             //second:get account by keyID
			//find CAccount info by keyid
			if(setCreateKey.size()) {
				bool bfind = false;
				for(auto &item: setCreateKey){
					if(item == keyid){
						bfind = true;
						break;
					}
				}
				if (!bfind)
					continue;
			}
			CUserID userId = keyid;
			CAccount acctInfo;
			if (view.GetAccount(userId, acctInfo)) {     // check  acctInfo is or not allowed to mining ,
				//available
//				LogPrint("INFO", "account info:regid=%s llvalue=%d keyid=%s ncoinday=%lld isMiner=%d\n", acctInfo.regID.ToString(),acctInfo.llValues,
//						acctInfo.keyID.ToString(), acctInfo.GetAccountPos(pBlock->GetHeight()),
//						 acctInfo.IsMiner(pBlock->GetHeight()));
				if (acctInfo.IsRegister() && acctInfo.GetAccountPos(pBlock->GetHeight()) > 0 && acctInfo.IsMiner(pBlock->GetHeight())) {
					setAcctInfo.insert(std::move(acctInfo));
					LogPrint("miner", "miner account info:%s\n", acctInfo.ToString());
                    break; //太多的账户会很慢 导致跳过部分时间 挖不到块
                }
			}
		}
	}

	if (setAcctInfo.empty()) {
		setCreateKey.clear();
		LogPrint("INFO", "CreatePosTx setSecureAcc empty");
		return false;
	}

	uint64_t maxNonce = SysCfg().GetBlockMaxNonce(); //cacul times

	uint256 prevblockhash = pPrevIndex->GetBlockHash();
	const uint256 targetHash = CBigNum().SetCompact(pBlock->GetBits()).getuint256(); //target hash difficult
	set<CAccount, CAccountComparator>::iterator iterAcct = setAcctInfo.begin();
	for (;iterAcct!=setAcctInfo.end();++iterAcct) {                            //third: 根据不同的账户 ,去计算挖矿
		CAccount  &item = const_cast<CAccount&>(*iterAcct);
		uint64_t posacc = item.GetAccountPos(pBlock->GetHeight());
		if (0 == posacc) {  //have no pos
			LogPrint("ERROR", "CreatePosTx posacc zero\n");
			continue;
		}
//		LogPrint("INFO", "miner account:%s\n", item.ToString());
//		LogPrint("INFO", "target hash:%s\n", targetHash.ToString());
		//LogPrint("INFO", "posacc:%d\n", posacc);
		uint256 adjusthash = GetAdjustHash(targetHash, posacc, pBlock->GetHeight()-1); //adjust nbits
		LogPrint("INFO", "adjusthash:%s\n", adjusthash.ToString());

		//need compute this block proofofwork
		struct PosTxInfo postxinfo;
		postxinfo.nVersion = pBlock->GetVersion();
		postxinfo.hashPrevBlock = prevblockhash;
		postxinfo.hashMerkleRoot = item.GetHash();
		postxinfo.nValues = item.llValues;
		postxinfo.nHeight = pBlock->GetHeight();
		postxinfo.nFuel = pBlock->GetFuel();
		postxinfo.nFuelRate = pBlock->GetFuelRate();
		postxinfo.nTime = pBlock->GetTime(); //max(pPrevIndex->GetMedianTimePast() + 1, GetAdjustedTime());
		unsigned int nNonce = 0;
		for (; nNonce < maxNonce; ++nNonce) {        //循环的 更改随机数，计算curhash看是否满足
			postxinfo.nNonce = nNonce;
			pBlock->SetNonce(nNonce);
			uint256 curhash = postxinfo.GetHash();

			if (UintToArith256(curhash) <= UintToArith256(adjusthash)) {
				LogPrint("INFO", "why find pos tx hash succeed: \n"
						"   pos hash:%s \n"
						"adjust hash:%s \r\n", curhash.GetHex(), adjusthash.GetHex());
				CRegID regid;

				if (pAccountViewTip->GetRegId(item.keyID, regid)) {
					CRewardTransaction *prtx = (CRewardTransaction *) pBlock->vptx[0].get();
					prtx->account = regid;                                   //存矿工的 账户ID
					prtx->nHeight = pPrevIndex->nHeight+1;

					pBlock->SetHashMerkleRoot(pBlock->BuildMerkleTree());
					pBlock->SetHashPos(curhash);
					LogPrint("INFO", "find pos tx hash succeed: \n"
									  "   pos hash:%s \n"
									  "adjust hash:%s \r\n", curhash.GetHex(), adjusthash.GetHex());
					vector<unsigned char> vSign;
					if (pwalletMain->Sign(item.keyID, pBlock->SignatureHash(), vSign,
							item.MinerPKey.IsValid())) {
						LogPrint("INFO", "Create new block hash:%s\n", pBlock->GetHash().GetHex());
						LogPrint("miner", "Miner account info:%s\n", item.ToString());
						LogPrint("miner", "CreatePosTx block hash:%s, postxinfo:%s\n",pBlock->GetHash().GetHex(), postxinfo.ToString().c_str());
						pBlock->SetSignature(vSign);
						return true;
					} else {
						LogPrint("ERROR", "sign fail\r\n");
					}
				} else {
					LogPrint("ERROR", "GetKey fail or GetVec6 fail\r\n");
				}

			} else {
			//	LogPrint("INFO", "current nNonce %d", nNonce);
			}

		}
	}

	return false;
}

bool VerifyPosTx(CAccountViewCache &accView, const CBlock *pBlock, CTransactionDBCache &txCache, CScriptDBViewCache &scriptCache, bool bNeedRunTx) {
//	LogPrint("INFO", "VerifyPoxTx begin\n");
	uint64_t maxNonce = SysCfg().GetBlockMaxNonce(); //cacul times

	if (pBlock->GetNonce() > maxNonce) {
		return ERRORMSG("Nonce is larger than maxNonce");
	}

	if (pBlock->GetHashMerkleRoot() != pBlock->BuildMerkleTree()) {
		return ERRORMSG("hashMerkleRoot is error");
	}
	CAccountViewCache view(accView, true);
	CScriptDBViewCache scriptDBView(scriptCache, true);
	CAccount account;
	CRewardTransaction *prtx = (CRewardTransaction *) pBlock->vptx[0].get();
	if (view.GetAccount(prtx->account, account)) {
		if(!CheckSignScript(pBlock->SignatureHash(), pBlock->GetSignature(), account.PublicKey)) {
			if (!CheckSignScript(pBlock->SignatureHash(), pBlock->GetSignature(), account.MinerPKey)) {
//				LogPrint("ERROR", "block verify fail\r\n");
//				LogPrint("ERROR", "block hash:%s\n", pBlock->GetHash().GetHex());
//				LogPrint("ERROR", "signature block:%s\n", HexStr(pBlock->vSignature.begin(), pBlock->vSignature.end()));
				return ERRORMSG("Verify miner publickey signature error");
			}
		}
	} else {
		return ERRORMSG("AccountView have no the accountid");
	}

	//校验reward_tx 版本是否正确

	if (prtx->nVersion != nTxVersion2) {
		return ERRORMSG("CTransaction CheckTransction,tx version is not equal current version, (tx version %d: vs current %d)",
				prtx->nVersion, nTxVersion2);
	}


	if (bNeedRunTx) {
		int64_t nTotalFuel(0);
		uint64_t nTotalRunStep(0);
		for (unsigned int i = 1; i < pBlock->vptx.size(); i++) {
			shared_ptr<CBaseTransaction> pBaseTx = pBlock->vptx[i];
			if (uint256() != txCache.IsContainTx(pBaseTx->GetHash())) {
				return ERRORMSG("VerifyPosTx duplicate tx hash:%s", pBaseTx->GetHash().GetHex());
			}
			CTxUndo txundo;
			CValidationState state;
			if (CONTRACT_TX == pBaseTx->nTxType) {
				LogPrint("vm", "tx hash=%s VerifyPosTx run contract\n", pBaseTx->GetHash().GetHex());
			}
			pBaseTx->nFuelRate = pBlock->GetFuelRate();
			if (!pBaseTx->ExecuteTx(i, view, state, txundo, pBlock->GetHeight(), txCache, scriptDBView)) {
				return ERRORMSG("transaction UpdateAccount account error");
			}
			nTotalRunStep += pBaseTx->nRunStep;
			if(nTotalRunStep > MAX_BLOCK_RUN_STEP) {
				return ERRORMSG("block total run steps exceed max run step");
			}

			nTotalFuel += pBaseTx->GetFuel(pBlock->GetFuelRate());
			LogPrint("fuel", "VerifyPosTx total fuel:%d, tx fuel:%d runStep:%d fuelRate:%d txhash:%s \n",nTotalFuel, pBaseTx->GetFuel(pBlock->GetFuelRate()), pBaseTx->nRunStep, pBlock->GetFuelRate(), pBaseTx->GetHash().GetHex());
		}

		if(nTotalFuel != pBlock->GetFuel()) {
			return ERRORMSG("fuel value at block header calculate error");
		}

		if (view.GetAccount(prtx->account, account)) {
			//available acc
//     		cout << "check block hash:" << pBlock->SignatureHash().GetHex() << endl;
//			cout << "check signature:" << HexStr(pBlock->vSignature) << endl;
//			cout <<"account miner"<< account.ToString()<< endl;
			if(account.GetAccountPos(pBlock->GetHeight()) <= 0 || !account.IsMiner(pBlock->GetHeight()))
				return ERRORMSG("coindays of account dismatch, can't be miner, account info:%s", account.ToString());
		} else {
			LogPrint("ERROR", "AccountView have no the accountid\r\n");
			return false;
		}
	}

	uint256 prevblockhash = pBlock->GetHashPrevBlock();
	const uint256 targetHash = CBigNum().SetCompact(pBlock->GetBits()).getuint256(); //target hash difficult

	uint64_t posacc = account.GetAccountPos(pBlock->GetHeight());
/*
	arith_uint256 bnNew;
	bnNew.SetCompact(pBlock->nBits);
	string  timeFormat = DateTimeStrFormat("%Y-%m-%d %H:%M:%S", pBlock->nTime);
	LogPrint("coinday","height:%.10d, account:%-10s, coinday:%.10ld, difficulty=%.8lf,  time=%-20s, interval=%d\n", pBlock->nHeight, account.regID.ToString(), posacc/COIN/SysCfg().GetIntervalPos(), CaculateDifficulty(bnNew.GetCompact()), timeFormat,  pBlock->nTime-chainActive[pBlock->nHeight-1]->nTime);
*/
	if (posacc == 0) {
		return ERRORMSG("Account have no pos");
	}

	uint256 adjusthash = GetAdjustHash(targetHash, posacc, pBlock->GetHeight()-1); //adjust nbits

    //need compute this block proofofwork
	struct PosTxInfo postxinfo;
	postxinfo.nVersion = pBlock->GetVersion();
	postxinfo.hashPrevBlock = prevblockhash;
	postxinfo.hashMerkleRoot = account.GetHash();
	postxinfo.nValues = account.llValues;
	postxinfo.nTime = pBlock->GetTime();
	postxinfo.nNonce = pBlock->GetNonce();
	postxinfo.nHeight = pBlock->GetHeight();
	postxinfo.nFuel = pBlock->GetFuel();
	postxinfo.nFuelRate = pBlock->GetFuelRate();
	uint256 curhash = postxinfo.GetHash();

	LogPrint("miner", "Miner account info:%s\n", account.ToString());
	LogPrint("miner", "VerifyPosTx block hash:%s, postxinfo:%s\n", pBlock->GetHash().GetHex(), postxinfo.ToString().c_str());
	if(pBlock->GetHashPos() != curhash) {
		return ERRORMSG("PosHash Error: \n"
					" computer PoS hash:%s \n"
					" block PoS hash:%s\n", curhash.GetHex(),  pBlock->GetHashPos().GetHex());
	}
	if (UintToArith256(curhash) > UintToArith256(adjusthash)) {
		return ERRORMSG("Account ProofOfWorkLimit error: \n"
		           "   pos hash:%s \n"
		           "adjust hash:%s\r\n", curhash.GetHex(), adjusthash.GetHex());
	}

	return true;
}

CBlockTemplate* CreateNewBlock(CAccountViewCache &view, CTransactionDBCache &txCache, CScriptDBViewCache &scriptCache){

	//    // Create new block
		auto_ptr<CBlockTemplate> pblocktemplate(new CBlockTemplate());
		if (!pblocktemplate.get())
			return NULL;
		CBlock *pblock = &pblocktemplate->block; // pointer for convenience

		// Create coinbase tx
		CRewardTransaction rtx;

		// Add our coinbase tx as first transaction
		pblock->vptx.push_back(make_shared<CRewardTransaction>(rtx));
		pblocktemplate->vTxFees.push_back(-1); // updated at end
		pblocktemplate->vTxSigOps.push_back(-1); // updated at end

		// Largest block you're willing to create:
		unsigned int nBlockMaxSize = SysCfg().GetArg("-blockmaxsize", DEFAULT_BLOCK_MAX_SIZE);
		// Limit to betweeen 1K and MAX_BLOCK_SIZE-1K for sanity:
		nBlockMaxSize = max((unsigned int) 1000, min((unsigned int) (MAX_BLOCK_SIZE - 1000), nBlockMaxSize));

		// How much of the block should be dedicated to high-priority transactions,
		// included regardless of the fees they pay
		unsigned int nBlockPrioritySize = SysCfg().GetArg("-blockprioritysize", DEFAULT_BLOCK_PRIORITY_SIZE);
		nBlockPrioritySize = min(nBlockMaxSize, nBlockPrioritySize);

		// Minimum block size you want to create; block will be filled with free transactions
		// until there are no more or the block reaches this size:
		unsigned int nBlockMinSize = SysCfg().GetArg("-blockminsize", DEFAULT_BLOCK_MIN_SIZE);
		nBlockMinSize = min(nBlockMaxSize, nBlockMinSize);

		// Collect memory pool transactions into the block
		int64_t nFees = 0;
		{
			LOCK2(cs_main, mempool.cs);
			CBlockIndex* pIndexPrev = chainActive.Tip();
			pblock->SetFuelRate(GetElementForBurn(pIndexPrev));

			// This vector will be sorted into a priority queue:
			vector<TxPriority> vecPriority;
			GetPriorityTx(vecPriority, pblock->GetFuelRate());

			// Collect transactions into block
			uint64_t nBlockSize = ::GetSerializeSize(*pblock, SER_NETWORK, PROTOCOL_VERSION);

			uint64_t nBlockTx(0);
			bool fSortedByFee(true);
			uint64_t nTotalRunStep(0);
			int64_t  nTotalFuel(0);
			TxPriorityCompare comparer(fSortedByFee);
			make_heap(vecPriority.begin(), vecPriority.end(), comparer);
			while (!vecPriority.empty()) {
				// Take highest priority transaction off the priority queue:
				double dPriority = vecPriority.front().get<0>();
				double dFeePerKb = vecPriority.front().get<1>();
				shared_ptr<CBaseTransaction> stx = vecPriority.front().get<2>();
				CBaseTransaction *pBaseTx = stx.get();
				//const CTransaction& tx = *(vecPriority.front().get<2>());

				pop_heap(vecPriority.begin(), vecPriority.end(), comparer);
				vecPriority.pop_back();

				// Size limits
				unsigned int nTxSize = ::GetSerializeSize(*pBaseTx, SER_NETWORK, PROTOCOL_VERSION);
				if (REG_APP_TX == pBaseTx->nTxType) {
					LogPrint("vm", "tx ----- REG_APP_TX hash=%s CreateNewBlock run contract\n", pBaseTx->GetHash().GetHex());
					LogPrint("vm", "tx ----- nBlockSize is %d \n", nBlockSize);

					LogPrint("vm", "tx ----- txSize is %d \n", nTxSize);
					LogPrint("vm", "tx ----- nBlockMaxSize is %d \n", nBlockMaxSize);
					LogPrint("vm", "tx ----- nBlockMinSize is %d \n", nBlockMinSize);
					LogPrint("VM", "tx ----- pBaseTx->nRunSte is %d \n", pBaseTx->nRunStep);
				}
				if (nBlockSize + nTxSize >= nBlockMaxSize)
					continue;
				// Skip free transactions if we're past the minimum block size:
				if (fSortedByFee && (dFeePerKb < CTransaction::nMinRelayTxFee) && (nBlockSize + nTxSize >= nBlockMinSize))
					continue;

				// Prioritize by fee once past the priority size or we run out of high-priority
				// transactions:
				if (!fSortedByFee && ((nBlockSize + nTxSize >= nBlockPrioritySize) || !AllowFree(dPriority))) {
					fSortedByFee = true;
					comparer = TxPriorityCompare(fSortedByFee);
					make_heap(vecPriority.begin(), vecPriority.end(), comparer);
				}

				if(uint256() != std::move(txCache.IsContainTx(std::move(pBaseTx->GetHash())))) {
					LogPrint("INFO","CreatePosTx duplicate tx\n");
					continue;
				}

				CTxUndo txundo;
				CValidationState state;
				if(pBaseTx->IsCoinBase()){
					ERRORMSG("TX type is coin base tx error......");
			//		assert(0); //never come here
				}
				if (CONTRACT_TX == pBaseTx->nTxType) {
					LogPrint("vm", "tx hash=%s CreateNewBlock run contract\n", pBaseTx->GetHash().GetHex());
				}
				CAccountViewCache viewTemp(view, true);
				CScriptDBViewCache scriptCacheTemp(scriptCache, true);
				pBaseTx->nFuelRate = pblock->GetFuelRate();
				if (!pBaseTx->ExecuteTx(nBlockTx + 1, viewTemp, state, txundo, pIndexPrev->nHeight + 1,
						txCache, scriptCacheTemp)) {
					LogPrint("vm ", "exe continue");
					continue;
				}
				// Run step limits
				if(nTotalRunStep + pBaseTx->nRunStep >= MAX_BLOCK_RUN_STEP) {
					LogPrint("vm ", "nTotalRunStep continue nTotalRunStep %d, pBaseTx->nRunStep %d", nTotalRunStep, pBaseTx->nRunStep);
					continue;
				}
				assert(viewTemp.Flush());
				assert(scriptCacheTemp.Flush());
				nFees += pBaseTx->GetFee();
				nBlockSize += stx->GetSerializeSize(SER_NETWORK, PROTOCOL_VERSION);
				nTotalRunStep += pBaseTx->nRunStep;
				nTotalFuel += pBaseTx->GetFuel(pblock->GetFuelRate());
				nBlockTx++;
				pblock->vptx.push_back(stx);
				LogPrint("fuel", "miner total fuel:%d, tx fuel:%d runStep:%d fuelRate:%d txhash:%s\n",
						 nTotalFuel, pBaseTx->GetFuel(pblock->GetFuelRate()), pBaseTx->nRunStep, pblock->GetFuelRate(),
						 pBaseTx->GetHash().GetHex());
			}

			nLastBlockTx = nBlockTx;
			nLastBlockSize = nBlockSize;
			LogPrint("INFO","CreateNewBlock(): total size %u\n", nBlockSize);

			assert(nFees-nTotalFuel >= 0);
			((CRewardTransaction*) pblock->vptx[0].get())->rewardValue = nFees - nTotalFuel + GetBlockSubsidy(pIndexPrev->nHeight + 1);

			// Fill in header
			pblock->SetHashPrevBlock(pIndexPrev->GetBlockHash());
			UpdateTime(*pblock, pIndexPrev);
			pblock->SetBits(GetNextWorkRequired(pIndexPrev, pblock));
			pblock->SetNonce(0);
			pblock->SetHeight(pIndexPrev->nHeight + 1);
			pblock->SetFuel(nTotalFuel);
		}

		return pblocktemplate.release();
}

bool CheckWork(CBlock* pblock, CWallet& wallet) {
//	uint256 hash = pblock->GetHash();
//	uint256 hashTarget = CBigNum().SetCompact(pblock->nBits).getuint256();
//
//	if (hash > hashTarget)
//		return false;

	//// debug print
//	LogPrint("INFO","proof-of-work found  \n  hash: %s  \ntarget: %s\n", hash.GetHex(), hashTarget.GetHex());
	pblock->print(*pAccountViewTip);
	// LogPrint("INFO","generated %s\n", FormatMoney(pblock->vtx[0].vout[0].nValue));

	// Found a solution
	{
		LOCK(cs_main);
		if (pblock->GetHashPrevBlock() != chainActive.Tip()->GetBlockHash())
			return ERRORMSG("TongxingpointsMiner : generated block is stale");

		// Remove key from key pool
	//	reservekey.KeepKey();

		// Track how many getdata requests this block gets
//		{
//			LOCK(wallet.cs_wallet);
//			wallet.mapRequestCount[pblock->GetHash()] = 0;
//		}

		// Process this block the same as if we had received it from another node
		CValidationState state;
		if (!ProcessBlock(state, NULL, pblock))
			return ERRORMSG("TongxingpointsMiner : ProcessBlock, block not accepted");
	}

	return true;
}

bool static MiningBlock(CBlock *pblock,CWallet *pwallet,CBlockIndex* pindexPrev,unsigned int nTransactionsUpdatedLast,CAccountViewCache &view, CTransactionDBCache &txCache, CScriptDBViewCache &scriptCache){

	int64_t nStart = GetTime();

	unsigned int lasttime = 0xFFFFFFFF;
	while (true) {

		// Check for stop or if block needs to be rebuilt
		boost::this_thread::interruption_point();
		if (vNodes.empty() && SysCfg().NetworkID() != CBaseParams::REGTEST)
			return false;


		if (pindexPrev != chainActive.Tip()) {
            LogPrint("INFO", "MiningBlock but is not the head of chain, chain height %d", chainActive.Tip()->nHeight);
            return false;
        }

		//获取时间 同时等待下次时间到
		auto GetNextTimeAndSleep = [&]() {
			while(max(pindexPrev->GetMedianTimePast() + 1, GetAdjustedTime()) == lasttime)
			{
				::MilliSleep(800);
			}
			return (lasttime = max(pindexPrev->GetMedianTimePast() + 1, GetAdjustedTime()));
		};

		GetNextTimeAndSleep();	// max(pindexPrev->GetMedianTimePast() + 1, GetAdjustedTime());
		UpdateTime(*pblock, pindexPrev);

		if (pindexPrev != chainActive.Tip()) {
			return false;
		}

		set<CKeyID> setCreateKey;
		setCreateKey.clear();
		int64_t lasttime = GetTimeMillis();
		bool increatedfalg =CreatePosTx(pindexPrev, pblock, setCreateKey,view,txCache,scriptCache);
		LogPrint("INFO","CreatePosTx used time :%d ms, %d\n",   GetTimeMillis() - lasttime, increatedfalg);
		if (increatedfalg == true) {
			SetThreadPriority(THREAD_PRIORITY_NORMAL);
			{
			int64_t lasttime1 = GetTimeMillis();
			CheckWork(pblock, *pwallet);
		    LogPrint("INFO","CheckWork used time :%d ms\n",   GetTimeMillis() - lasttime1);
			}
			SetThreadPriority(THREAD_PRIORITY_LOWEST);

			return true;
		}

		if (mempool.GetTransactionsUpdated() != nTransactionsUpdatedLast || GetTime() - nStart > 60)
				return false;
	}
	return false;
}

void static TongxingpointsMiner(CWallet *pwallet,int targetConter) {
	LogPrint("INFO","Miner started\n");

	SetThreadPriority(THREAD_PRIORITY_LOWEST);
	RenameThread("Tongxingpoints-miner");

	auto CheckIsHaveMinerKey = [&]() {
		    LOCK2(cs_main, pwalletMain->cs_wallet);
			set<CKeyID> setMineKey;
			setMineKey.clear();
			pwalletMain->GetKeys(setMineKey, true);
			return !setMineKey.empty();
		};


	if (!CheckIsHaveMinerKey()) {
			LogPrint("INFO", "TongxingpointsMiner  terminated\n");
			ERRORMSG("ERROR:%s ", "no key for minering\n");
            return ;
		}

	auto getcurhigh = [&]() {
		LOCK(cs_main);
		return chainActive.Height();
	};

	targetConter = targetConter+getcurhigh();


	try {
        SetMinerStatus(true);
		while (true) {
            LogPrint("INFO", "\n\n\nbegin new miner current height %d ", chainActive.Tip()->nHeight);
			if (SysCfg().NetworkID() != CBaseParams::REGTEST) {
				// Busy-wait for the network to come online so we don't waste time mining
				// on an obsolete chain. In regtest mode we expect to fly solo.
                while (vNodes.empty() || (chainActive.Tip() && chainActive.Tip()->nHeight>1 && GetAdjustedTime()-chainActive.Tip()->nTime > 60*60)) {
                    LogPrint("miner", "vNodes size:%d nHieght : %d, chainActiveTip()%d , vchain size %d, GetAdjustedTime() %d, chainActive.Tip()->nTime %d\n", vNodes.size(),
                             chainActive.Tip()->nHeight, chainActive.Tip(), chainActive.Height(), GetAdjustedTime(), chainActive.Tip()->nTime);

                    MilliSleep(1000);
                }

			}

			//
			// Create new block
			//
			unsigned int LastTrsa = mempool.GetTransactionsUpdated();
			CBlockIndex* pindexPrev = chainActive.Tip();

			CAccountViewCache accview(*pAccountViewTip, true);
			CTransactionDBCache txCache(*pTxCacheTip, true);
			CScriptDBViewCache ScriptDbTemp(*pScriptDBTip, true);
			int64_t lasttime1 = GetTimeMillis();
			shared_ptr<CBlockTemplate> pblocktemplate(CreateNewBlock(accview, txCache, ScriptDbTemp));
			if (!pblocktemplate.get()){
				throw runtime_error("Create new block fail.");
			}
			LogPrint("INFO", "CreateNewBlock tx count:%d used time :%d ms\n", pblocktemplate.get()->block.vptx.size(),
					GetTimeMillis() - lasttime1);
			CBlock *pblock = &pblocktemplate.get()->block;
			MiningBlock(pblock, pwallet, pindexPrev, LastTrsa, accview, txCache, ScriptDbTemp);
			
			if (SysCfg().NetworkID() != CBaseParams::MAIN)
				if(targetConter <= getcurhigh())	{
						throw boost::thread_interrupted();
				}	
		}
	} catch (...) {
		LogPrint("INFO","TongxingpointsMiner  terminated\n");
    	SetMinerStatus(false);
		throw;
	}
}

uint256 CreateBlockWithAppointedAddr(CKeyID const &keyID)
{
	if (SysCfg().NetworkID() == CBaseParams::REGTEST)
	{
//		unsigned int nTransactionsUpdatedLast = mempool.GetTransactionsUpdated();
		mempool.GetTransactionsUpdated();
		CBlockIndex* pindexPrev = chainActive.Tip();

		CAccountViewCache accview(*pAccountViewTip, true);
		CTransactionDBCache txCache(*pTxCacheTip, true);
		CScriptDBViewCache ScriptDbTemp(*pScriptDBTip, true);
		shared_ptr<CBlockTemplate> pblocktemplate(CreateNewBlock(accview,txCache,ScriptDbTemp));
		if (!pblocktemplate.get())
			return uint256();
		CBlock *pblock = &pblocktemplate.get()->block;

//		int nBlockSize = pblock->GetSerializeSize(SER_NETWORK, PROTOCOL_VERSION);
		pblock->GetSerializeSize(SER_NETWORK, PROTOCOL_VERSION);

//		int64_t nStart = GetTime();
		while (true) {
			pblock->SetTime(max(pindexPrev->GetMedianTimePast() + 1, GetAdjustedTime()));
			set<CKeyID> setCreateKey;
			setCreateKey.clear();
			setCreateKey.insert(keyID);
			if (CreatePosTx(pindexPrev, pblock,setCreateKey,accview,txCache,ScriptDbTemp)) {
				CheckWork(pblock, *pwalletMain);
//				int nBlockSize = pblock->GetSerializeSize(SER_NETWORK, PROTOCOL_VERSION);
			}
			if(setCreateKey.empty())
			{
				LogPrint("postx", "%s is not exist in the wallet\r\n",keyID.ToAddress());
				break;
			}
			::MilliSleep(1);
			if (pindexPrev != chainActive.Tip())
			{
				return chainActive.Tip()->GetBlockHash() ;
			}
		}
	}
	return uint256();
}

void GenerateTongxingpointsBlock(bool fGenerate, CWallet* pwallet, int targetHigh) {
	static boost::thread_group* minerThreads = NULL;

	if (minerThreads != NULL) {
		minerThreads->interrupt_all();
//		minerThreads->join_all();
		delete minerThreads;
		minerThreads = NULL;
	}

	if(targetHigh <= 0 && fGenerate == true)
	{
//		assert(0);
		ERRORMSG("targetHigh, fGenerate value error");
		return ;
	}
	if (!fGenerate)
		return;
	//in pos system one thread is enough  marked by ranger.shi
	minerThreads = new boost::thread_group();
	minerThreads->create_thread(boost::bind(&TongxingpointsMiner, pwallet,targetHigh));

//	minerThreads->join_all();
}



