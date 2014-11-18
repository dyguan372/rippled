//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

namespace ripple {

class Change
    : public Transactor
{
public:
    Change (
        SerializedTransaction const& txn,
        TransactionEngineParams params,
        TransactionEngine* engine)
        : Transactor (
            txn,
            params,
            engine,
            deprecatedLogs().journal("Change"))
    {
    }

    TER doApply () override
    {
        if (mTxn.getTxnType () == ttAMENDMENT)
            return applyAmendment ();

        if (mTxn.getTxnType () == ttFEE)
            return applyFee ();

        if (mTxn.getTxnType () == ttDIVIDEND)
            return applyDividend ();

        return temUNKNOWN;
    }

    TER checkSig () override
    {
        if (mTxn.getFieldAccount160 (sfAccount).isNonZero ())
        {
            m_journal.warning << "Bad source account";
            return temBAD_SRC_ACCOUNT;
        }

        if (!mTxn.getSigningPubKey ().empty () || !mTxn.getSignature ().empty ())
        {
            m_journal.warning << "Bad signature";
            return temBAD_SIGNATURE;
        }

        return tesSUCCESS;
    }

    TER checkSeq () override
    {
        if ((mTxn.getSequence () != 0) || mTxn.isFieldPresent (sfPreviousTxnID))
        {
            m_journal.warning << "Bad sequence";
            return temBAD_SEQUENCE;
        }

        return tesSUCCESS;
    }

    TER payFee () override
    {
        if (mTxn.getTransactionFee () != STAmount ())
        {
            m_journal.warning << "Non-zero fee";
            return temBAD_FEE;
        }

        return tesSUCCESS;
    }

    TER preCheck () override
    {
        mTxnAccountID = mTxn.getSourceAccount ().getAccountID ();

        if (mTxnAccountID.isNonZero ())
        {
            m_journal.warning << "Bad source id";

            return temBAD_SRC_ACCOUNT;
        }

        if (mParams & tapOPEN_LEDGER)
        {
            m_journal.warning << "Change transaction against open ledger";
            return temINVALID;
        }

        return tesSUCCESS;
    }

private:
    TER applyAmendment ()
    {
        uint256 amendment (mTxn.getFieldH256 (sfAmendment));

        SLE::pointer amendmentObject (mEngine->entryCache (
            ltAMENDMENTS, Ledger::getLedgerAmendmentIndex ()));

        if (!amendmentObject)
        {
            amendmentObject = mEngine->entryCreate(
                ltAMENDMENTS, Ledger::getLedgerAmendmentIndex());
        }

        STVector256 amendments (amendmentObject->getFieldV256 (sfAmendments));

        if (std::find (amendments.begin(), amendments.end(),
            amendment) != amendments.end ())
        {
            return tefALREADY;
        }

        amendments.push_back (amendment);
        amendmentObject->setFieldV256 (sfAmendments, amendments);
        mEngine->entryModify (amendmentObject);

        getApp().getAmendmentTable ().enable (amendment);

        if (!getApp().getAmendmentTable ().isSupported (amendment))
            getApp().getOPs ().setAmendmentBlocked ();

        return tesSUCCESS;
    }

    TER applyFee ()
    {
        SLE::pointer feeObject = mEngine->entryCache (
            ltFEE_SETTINGS, Ledger::getLedgerFeeIndex ());

        if (!feeObject)
            feeObject = mEngine->entryCreate (
                ltFEE_SETTINGS, Ledger::getLedgerFeeIndex ());

        m_journal.trace <<
            "Previous fee object: " << feeObject->getJson (0);

        feeObject->setFieldU64 (
            sfBaseFee, mTxn.getFieldU64 (sfBaseFee));
        feeObject->setFieldU32 (
            sfReferenceFeeUnits, mTxn.getFieldU32 (sfReferenceFeeUnits));
        feeObject->setFieldU32 (
            sfReserveBase, mTxn.getFieldU32 (sfReserveBase));
        feeObject->setFieldU32 (
            sfReserveIncrement, mTxn.getFieldU32 (sfReserveIncrement));

        mEngine->entryModify (feeObject);

        m_journal.trace <<
            "New fee object: " << feeObject->getJson (0);
        m_journal.warning << "Fees have been changed";
        return tesSUCCESS;
    }

    TER applyDividend()
    {
        m_journal.debug <<
            "vPal: Start dividend.";

        SLE::pointer dividendObject = mEngine->entryCache(
            ltDIVIDEND, Ledger::getLedgerDividendIndex());
        
        if (!dividendObject) {
            dividendObject = mEngine->entryCreate(
                ltDIVIDEND, Ledger::getLedgerDividendIndex());
        }
        
        m_journal.info <<
            "Previous dividend object: " << dividendObject->getJson(0);

        uint32_t dividendLedger = mTxn.getFieldU32(sfDividendLedger);
        uint64_t dividendCoins = mTxn.getFieldU64(sfDividendCoins);
		uint64_t dividendCoinsVBC = mTxn.getFieldU64(sfDividendCoinsVBC);

        std::vector<std::pair<RippleAddress, uint64_t> > accounts;
        std::vector<RippleAddress> roots;
        mEngine->getLedger()->visitStateItems(std::bind(retrieveAccount,
            std::ref(accounts), std::ref(roots), std::placeholders::_1));
        std::sort(accounts.begin(), accounts.end(), pair_less());
        hash_map<RippleAddress, std::pair<uint64_t, uint64_t> > power;
        for (auto it = roots.begin(); it != roots.end(); ++it) {
            getPower(*it, power);
        }

        hash_map<RippleAddress, std::pair<uint32_t, uint64_t> > rank;
        int r = 1;
        rank[accounts[0].first] = std::make_pair(r, power[accounts[0].first].first - power[accounts[0].first].second + pow(power[accounts[0].first].second, 1.0/3));
        int sum = r;
        uint64_t sumPower = power[accounts[0].first].second;
        for (int i=1; i<accounts.size(); ++i) {
            if (accounts[i].second > accounts[i-1].second)
                ++r;
            rank[accounts[i].first] = std::make_pair(r, power[accounts[i].first].first - power[accounts[i].first].second + pow(power[accounts[i].first].second, 1.0/3));
            sum += r;
            sumPower += power[accounts[i].first].second;
        }
        uint64_t actualTotalDividend = 0;
        uint64_t actualTotalDividendVBC = 0;
        std::for_each(rank.begin(), rank.end(), dividend_account(mEngine, dividendCoinsVBC, sum, sumPower, &actualTotalDividend, &actualTotalDividendVBC, m_journal));

        dividendObject->setFieldU32(sfDividendLedger, dividendLedger);
        dividendObject->setFieldU64(sfDividendCoins, actualTotalDividend);
        dividendObject->setFieldU64(sfDividendCoinsVBC, actualTotalDividendVBC);

        mEngine->entryModify(dividendObject);

        m_journal.info <<
            "Current dividend object: " << dividendObject->getJson(0);

        return tesSUCCESS;
    }

    uint64_t getPower(const RippleAddress &r, hash_map<RippleAddress, std::pair<uint64_t, uint64_t> > &p)
    {
        if (p.find(r) != p.end()) return p[r].first;

        auto const index = Ledger::getAccountRootIndex(r);
        SLE::pointer sle(mEngine->entryCache(ltACCOUNT_ROOT, index));
        if (!sle) {
            m_journal.warning <<
                "Account " << r.humanAccountID() << " does not exist.";
            return 0;
        }

        STArray references = sle->getFieldArray(sfReferences);
        if (references.empty()) {
            p[r] = std::make_pair(0, 0);
            return p[r].first;
        }
        uint64_t sum = 0;
        uint64_t maxP = 0;
        for (auto it = references.begin(); it != references.end(); ++it) {
            RippleAddress raChild = it->getFieldAccount(sfReference);
            auto const index = Ledger::getAccountRootIndex(raChild);
            SLE::pointer sleChild(mEngine->entryCache(ltACCOUNT_ROOT, index));
            if (!sleChild) {
                m_journal.warning <<
                    "Account " << raChild.humanAccountID() << " does not exist.";
                continue;
            }
            uint64_t v = getPower(raChild, p);
            uint64_t c = sleChild->getFieldAmount(sfBalanceVBC).getNValue();
            uint64_t m = p[raChild].second;
            sum += v + c;
            maxP = std::max(std::max(m, c), maxP);
        }
        p[r] = std::make_pair(sum, maxP);
        return p[r].first;
    }

    class pair_less {
        public:
            bool operator()(const std::pair<RippleAddress, uint64_t> &x, const std::pair<RippleAddress, uint64_t> &y)
            {
                return x.second < y.second;
            }
    };

    class dividend_account {
        RippleAddress root;
        TransactionEngine *engine;
        uint64_t totalDividendVBC;
        uint32_t totalPart;
        uint64_t totalPower;
        beast::Journal m_journal;
        uint64_t *pActualTotalDividend;
        uint64_t *pActualTotalDividendVBC;
    public:
        dividend_account(TransactionEngine *e, uint64_t d, uint32_t p, uint64_t p2, uint64_t *patd, uint64_t *patdv, const beast::Journal &j)
            : engine(e), totalDividendVBC(d), totalPart(p), totalPower(p2), m_journal(j)
            , pActualTotalDividend(patd), pActualTotalDividendVBC(patdv)
        {
            RippleAddress rootSeedMaster = RippleAddress::createSeedGeneric("masterpassphrase");
            RippleAddress rootGeneratorMaster = RippleAddress::createGeneratorPublic(rootSeedMaster);
            root = RippleAddress::createAccountPublic(rootGeneratorMaster, 0);
        }

        void operator ()(const std::pair<RippleAddress, std::pair<uint32_t, uint64_t> > &v)
        {
            uint64_t totalDivVBCbyRank = totalDividendVBC / 2;
            uint64_t totalDivVBCbyPower = totalDividendVBC - totalDivVBCbyRank;
            uint64_t divVBCbyRank = totalDivVBCbyRank * v.second.first / totalPart;
            uint64_t divVBCbyPower = totalDivVBCbyPower * v.second.second / totalPower;
            uint64_t divVBC = divVBCbyRank + divVBCbyPower;
            m_journal.info << v.first.humanAccountID() << "\t" << root.humanAccountID();
            //if (v.first.humanAccountID() != root.humanAccountID()) {
            if (1) {
                auto const index = Ledger::getAccountRootIndex(v.first);
                SLE::pointer sleDst(engine->entryCache(ltACCOUNT_ROOT, index));
                if (sleDst) {
                    engine->entryModify(sleDst);
                    uint64_t prevBalanceVBC = sleDst->getFieldAmount(sfBalanceVBC).getNValue();
                    if (divVBC >= SYSTEM_CURRENCY_PARTS) {
                        sleDst->setFieldAmount(sfBalanceVBC, prevBalanceVBC + divVBC);
                        *pActualTotalDividendVBC += divVBC;
                    }
                    uint64_t prevBalance = sleDst->getFieldAmount(sfBalance).getNValue();
                    uint64_t div = prevBalanceVBC * VRP_INCREASE_RATE;
                    sleDst->setFieldAmount(sfBalance, prevBalance + div);
                    *pActualTotalDividend += div;
                }
            }
        }
    };

    static void retrieveAccount (std::vector<std::pair<RippleAddress, uint64_t> > &accounts, std::vector<RippleAddress> &roots, SLE::ref sle)
    {
        if (sle->getType() == ltACCOUNT_ROOT) {
            RippleAddress addr = sle->getFieldAccount(sfAccount);
            uint64_t bal = sle->getFieldAmount(sfBalanceVBC).getNValue();
            accounts.push_back(std::make_pair(addr, bal));
            RippleAddress referee = sle->getFieldAccount(sfReferee);
            if (referee.getAccountID().isZero()) {
                roots.push_back(addr);
            }
        }
    }

    // VFALCO TODO Can this be removed?
    bool mustHaveValidAccount () override
    {
        return false;
    }
};


TER
transact_Change (
    SerializedTransaction const& txn,
    TransactionEngineParams params,
    TransactionEngine* engine)
{
    return Change (txn, params, engine).apply ();
}

}
