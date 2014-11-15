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

class AddReferee
    : public Transactor
{
public:
    AddReferee(
        SerializedTransaction const& txn,
        TransactionEngineParams params,
        TransactionEngine* engine)
        : Transactor (
            txn,
            params,
            engine,
            deprecatedLogs().journal("AddReferee"))
    {

    }

    TER doApply () override
    {
        Account const refereeID (mTxn.getFieldAccount160 (sfDestination));
        Account const referenceID (mTxnAccountID);

        if (!refereeID)
        {
            m_journal.warning <<
                "Malformed transaction: Referee account not specified.";

            return temDST_NEEDED;
        }
        else if (referenceID == refereeID)
        {
            // You're referring yourself.
            m_journal.trace <<
                "Malformed transaction: Redundant transaction:" <<
                " reference=" << to_string(referenceID) <<
                " referee=" << to_string(refereeID);

            return temREDUNDANT;
        }

        //
        // Open a ledger for editing.
        SLE::pointer sleReferee(mEngine->entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(refereeID)));
        SLE::pointer sleReference(mEngine->entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(referenceID)));

        if (!sleReferee) {
            // Referee account does not exist.
            m_journal.trace <<
                "Malformed transaction: Referee account does not exist.";

            return tecNO_DST;
        } else if (!sleReference) {
            // Reference account does not exist.
            m_journal.trace <<
                "Malformed transaction: Reference account does not exist.";

            return terNO_ACCOUNT;
        } else if (sleReference->getFieldAccount(sfReferee).getAccountID().isNonZero())
        {
            // The tag is basically account-specific information we don't
            // understand, but we can require someone to fill it in.

            // We didn't make this test for a newly-formed account because there's
            // no way for this field to be set.
            m_journal.trace << "Malformed transaction: Referee has been set.";

            return tefREFEREE_EXIST;
        }
        else
        {
            // Tell the engine that we are intending to change the the destination
            // account.  The source account gets always charged a fee so it's always
            // marked as modified.
            STArray references = sleReferee->getFieldArray(sfReferences);
            for (auto it = references.begin(); it != references.end(); ++it) {
                Account id = it->getFieldAccount(sfReference).getAccountID();
                if (id == referenceID) {
                    m_journal.trace << "Malformed transaction: Reference has been set.";

                    return tefREFERENCE_EXIST;
                }
            }

            mEngine->entryModify(sleReference);
            mEngine->entryModify(sleReferee);
            sleReference->setFieldAccount(sfReferee, refereeID);
            STObject obj;
            obj.setFieldAccount(sfReference, referenceID);
            references.push_back(obj);
            sleReferee->setFieldArray(sfReferences, references);
        }

        return tesSUCCESS;
    }
};

TER
transact_AddReferee (
    SerializedTransaction const& txn,
    TransactionEngineParams params,
    TransactionEngine* engine)
{
    return AddReferee(txn, params, engine).apply();
}

}  // ripple
