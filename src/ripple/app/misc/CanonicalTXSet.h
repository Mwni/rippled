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

#ifndef RIPPLE_APP_MISC_CANONICALTXSET_H_INCLUDED
#define RIPPLE_APP_MISC_CANONICALTXSET_H_INCLUDED

#include <ripple/basics/CountedObject.h>
#include <ripple/protocol/RippleLedgerHash.h>
#include <ripple/protocol/STTx.h>
#include <ripple/protocol/SeqProxy.h>

namespace ripple {

/** Holds transactions which were deferred to the next pass of consensus.

    "Canonical" refers to the order in which transactions are applied.

    - Puts transactions from the same account in SeqProxy order

*/
// VFALCO TODO rename to SortedTxSet
class CanonicalTXSet : public CountedObject<CanonicalTXSet>
{
private:
    class Key
    {
    public:
        Key(std::uint32_t const& bucket, uint256 const& account, SeqProxy seqProx, uint256 const& id)
            : bucket_(bucket), account_(account), seqProxy_(seqProx), txId_(id)
        {
        }

        Key(uint256 const& account, SeqProxy seqProx, uint256 const& id)
            : bucket_(0), account_(account), seqProxy_(seqProx), txId_(id)
        {
        }

        friend bool
        operator<(Key const& lhs, Key const& rhs);

        inline friend bool
        operator>(Key const& lhs, Key const& rhs)
        {
            return rhs < lhs;
        }

        inline friend bool
        operator<=(Key const& lhs, Key const& rhs)
        {
            return !(lhs > rhs);
        }

        inline friend bool
        operator>=(Key const& lhs, Key const& rhs)
        {
            return !(lhs < rhs);
        }

        inline friend bool
        operator==(Key const& lhs, Key const& rhs)
        {
            return lhs.txId_ == rhs.txId_;
        }

        inline friend bool
        operator!=(Key const& lhs, Key const& rhs)
        {
            return !(lhs == rhs);
        }

        uint256 const&
        getAccount() const
        {
            return account_;
        }

        uint256 const&
        getTXID() const
        {
            return txId_;
        }

        Key const
        getCopyWithBucketIncreased() const
        {
            return Key(bucket_ + 1, account_, seqProxy_, txId_);
        }

    private:
        std::uint32_t bucket_;
        uint256 account_;
        SeqProxy seqProxy_;
        uint256 txId_;
    };

    friend bool
    operator<(Key const& lhs, Key const& rhs);

    // Calculate the salted key for the given account
    uint256
    accountKey(AccountID const& account);

public:
    using const_iterator =
        std::map<Key, std::shared_ptr<STTx const>>::const_iterator;

    enum Mode
	{
		BATCHED,
		STRIPED
	};

public:
    explicit CanonicalTXSet(LedgerHash const& saltHash) : salt_(saltHash), mode_(Mode::BATCHED)
    {
    }

    explicit CanonicalTXSet(LedgerHash const& saltHash, Mode mode) : salt_(saltHash), mode_(mode)
    {
    }

    void
    insert(std::shared_ptr<STTx const> const& txn);

    // Pops the next transaction on account that follows seqProx in the
    // sort order.  Normally called when a transaction is successfully
    // applied to the open ledger so the next transaction can be resubmitted
    // without waiting for ledger close.
    //
    // The return value is often null, when an account has no more
    // transactions.
    std::shared_ptr<STTx const>
    popAcctTransaction(std::shared_ptr<STTx const> const& tx);

    void
    reset(LedgerHash const& salt)
    {
        salt_ = salt;
        map_.clear();
        hints_.clear();
    }

    const_iterator
    erase(const_iterator const& it)
    {
        return map_.erase(it);
    }

    const_iterator
    begin() const
    {
        return map_.begin();
    }

    const_iterator
    end() const
    {
        return map_.end();
    }

    size_t
    size() const
    {
        return map_.size();
    }
    bool
    empty() const
    {
        return map_.empty();
    }

    uint256 const&
    key() const
    {
        return salt_;
    }

private:
    void
    insertBatched(std::shared_ptr<STTx const> const& txn);

	void
    insertStriped(std::shared_ptr<STTx const> const& txn);

    std::shared_ptr<STTx const>
    popAcctTransactionBatched(std::shared_ptr<STTx const> const& tx);

	std::shared_ptr<STTx const>
    popAcctTransactionStriped(std::shared_ptr<STTx const> const& tx);


    std::map<Key, std::shared_ptr<STTx const>> map_;
    std::map<uint256, std::map<Key, const_iterator>> hints_;

    // Used to salt the accounts so people can't mine for low account numbers
    uint256 salt_;
    Mode mode_;
};

}  // namespace ripple

#endif
