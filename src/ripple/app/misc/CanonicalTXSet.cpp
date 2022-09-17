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

#include <ripple/app/misc/CanonicalTXSet.h>

namespace ripple {

bool
operator<(CanonicalTXSet::Key const& lhs, CanonicalTXSet::Key const& rhs)
{
    if (lhs.bucket_ < rhs.bucket_)
        return true;

    if (lhs.bucket_ > rhs.bucket_)
        return false;

    if (lhs.account_ < rhs.account_)
        return true;

    if (lhs.account_ > rhs.account_)
        return false;

    if (lhs.seqProxy_ < rhs.seqProxy_)
        return true;

    if (lhs.seqProxy_ > rhs.seqProxy_)
        return false;

    return lhs.txId_ < rhs.txId_;
}

uint256
CanonicalTXSet::accountKey(AccountID const& account)
{
    uint256 ret = beast::zero;
    memcpy(ret.begin(), account.begin(), account.size());
    ret ^= salt_;
    return ret;
}

void
CanonicalTXSet::insert(std::shared_ptr<STTx const> const& txn)
{
    if (mode_ == Mode::BATCHED)
    {
        insertBatched(txn);
    }
    else if (mode_ == Mode::STRIPED)
    {
        insertStriped(txn);
    }
}

void
CanonicalTXSet::insertBatched(std::shared_ptr<STTx const> const& txn)
{
    map_.insert(std::make_pair(
        Key(accountKey(txn->getAccountID(sfAccount)),
            txn->getSeqProxy(),
            txn->getTransactionID()),
        txn));
}

void
CanonicalTXSet::insertStriped(std::shared_ptr<STTx const> const& txn)
{
    // Insert a transaction so that it maintains its Sequence order, while
    // ensuring that it is immediately followed by a transaction of a different
    // account, if there are any.
    //
    // [Alice1, Bob1, Charlie1, Alice2, Bob2, Charlie2, Alice3]
    //
    //  1. Find the hint entry for the account.
    //
    //  1a. If the new transaction's Sequence is behind all previously inserted
    //      transactions made by the same account, set the transaction's bucket
    //      index to the total number of transactions made by the account.
    //
    //  1b. If the new transaction'S Sequence came before [...], pull out all
    //      following transactions and re-insert them one bucket higher.
    //
    //  2. Insert the transaction along with its determined bucket index. Insert
    //     a hint that points to the newly inserted entry in the transaction map.

    int bucket = 0;
    uint256 const account = accountKey(txn->getAccountID(sfAccount));

    // This constructs a new sub-map for the key, if it does not exist. But
    // that's fine, since we're going to insert this key anyways.
    std::map<Key, const_iterator> const& txnsBySameAccount = hints_[account];

    auto nextTxHint = txnsBySameAccount.lower_bound(
        Key(0, uint256{0}, txn->getSeqProxy(), txn->getTransactionID()));

    if (nextTxHint == txnsBySameAccount.end())
    {
        // The new transaction is last in list. All good.
        bucket = txnsBySameAccount.size();
    }
    else
    {
        // The new transaction comes before one or multiple existing
        // transactions of the same account. We need to pull out the existing
        // ones and re-insert them, one position behind the new transaction.
        bucket = std::distance(txnsBySameAccount.begin(), nextTxHint);

        while (nextTxHint != txnsBySameAccount.end())
        {
            std::shared_ptr<STTx const> const& tx =
                std::move(nextTxHint->second->second);
            Key const newKey =
                nextTxHint->second->first.getCopyWithBucketIncreased();

            Key const hintKey =
                Key(0, uint256{0}, tx->getSeqProxy(), tx->getTransactionID());
            std::pair hintItem = std::make_pair(newKey, tx);

            map_.erase(nextTxHint->second);
            hints_[account][hintKey] = map_.insert(hintItem).first;

            ++nextTxHint;
        }
    }

    Key const insertionKey =
        Key(bucket, account, txn->getSeqProxy(), txn->getTransactionID());
    Key const hintKey =
        Key(0, uint256{0}, txn->getSeqProxy(), txn->getTransactionID());

    auto insertionResult = map_.insert(std::make_pair(insertionKey, txn));

    hints_[account][hintKey] = insertionResult.first;
}

std::shared_ptr<STTx const>
CanonicalTXSet::popAcctTransaction(std::shared_ptr<STTx const> const& tx)
{
    if (mode_ == Mode::BATCHED)
    {
        return popAcctTransactionBatched(tx);
    }
    else if (mode_ == Mode::STRIPED)
    {
        return popAcctTransactionStriped(tx);
    }
    else
    {
        return NULL;
    }
}

std::shared_ptr<STTx const>
CanonicalTXSet::popAcctTransactionBatched(std::shared_ptr<STTx const> const& tx)
{
    // Determining the next viable transaction for an account with Tickets:
    //
    //  1. Prioritize transactions with Sequences over transactions with
    //     Tickets.
    //
    //  2. Don't worry about consecutive Sequence numbers.  Creating Tickets
    //     can introduce a discontinuity in Sequence numbers.
    //
    //  3. After handling all transactions with Sequences, return Tickets
    //     with the lowest Ticket ID first.
    std::shared_ptr<STTx const> result;
    uint256 const effectiveAccount{accountKey(tx->getAccountID(sfAccount))};

    Key const after(effectiveAccount, tx->getSeqProxy(), beast::zero);
    auto const itrNext{map_.lower_bound(after)};

    if (itrNext != map_.end() &&
        itrNext->first.getAccount() == effectiveAccount)
    {
        result = std::move(itrNext->second);
        map_.erase(itrNext);
    }

    return result;
}

std::shared_ptr<STTx const>
CanonicalTXSet::popAcctTransactionStriped(std::shared_ptr<STTx const> const& tx)
{
    // Determining the next viable transaction for an account in STRIPED mode:
    //
    //  1. Find the hint entry for the account.
    //
    //  1a. If no such entry exists, it means there are no other transactions
    //      made by this account in the set, and nothing shall be returned.
    //
    //  2. Pick the first transaction from the account's hint entry. Remove it
    //     from both the transaction map and the hint map.
    //
    //  2a. If the remaining hint list is empty, remove it as well.

    std::shared_ptr<STTx const> result;
    uint256 const effectiveAccount{accountKey(tx->getAccountID(sfAccount))};
    auto hintItem = hints_.find(effectiveAccount);

    if (hintItem == hints_.end())
    {
        return result;
    }

    std::map<Key, const_iterator> const& txnsBySameAccount = hintItem->second;

    Key const after(uint256{0}, tx->getSeqProxy(), tx->getTransactionID());
    auto const itrNext{txnsBySameAccount.lower_bound(after)};

    result = std::move(itrNext->second->second);
    hints_[effectiveAccount].erase(itrNext);
    map_.erase(itrNext->second);

    if (txnsBySameAccount.size() == 0)
    {
        hints_.erase(hintItem);
    }

    return result;
}

}  // namespace ripple
