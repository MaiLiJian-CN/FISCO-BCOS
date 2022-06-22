#pragma once
#include <bcos-framework/concepts/Basic.h>
#include <bcos-framework/concepts/Block.h>
#include <bcos-framework/concepts/Storage.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <boost/iterator.hpp>
#include <boost/iterator/transform_iterator.hpp>
#include <boost/lexical_cast.hpp>
#include <bitset>
#include <tuple>

namespace bcos::ledger
{

// All get block flags
// clang-format off
struct GETBLOCK_FLAGS {};
struct BLOCK: public GETBLOCK_FLAGS {};
struct BLOCK_HEADER: public GETBLOCK_FLAGS {};
struct BLOCK_TRANSACTIONS: public GETBLOCK_FLAGS {};
struct BLOCK_RECEIPTS: public GETBLOCK_FLAGS {};
// clang-format on

template <bcos::storage::Storage Storage, bcos::concepts::block::Block Block>
class LedgerImpl
{
public:
    LedgerImpl(Storage storage) : m_storage{std::move(storage)} {}

    template <class Flag, class... Flags>
    requires std::derived_from<Flag, GETBLOCK_FLAGS>
    auto getBlock(bcos::concepts::block::BlockNumber auto blockNumber)
    {
        auto blockNumberStr = boost::lexical_cast<std::string>(blockNumber);

        if constexpr (std::is_same_v<Flag, BLOCK_HEADER>)
        {
            auto entry = m_storage.getRow(SYS_NUMBER_2_BLOCK_HEADER, blockNumberStr);
            if (!entry) [[unlikely]] { BOOST_THROW_EXCEPTION(std::runtime_error{"GetBlock not found!"}); }

            auto field = entry->getField(0);
            typename Block::BlockHeader blockHeader;
            blockHeader.decode(field);

            return blockHeader;
        }
        else if constexpr (std::is_same_v<Flag, BLOCK_TRANSACTIONS> || std::is_same_v<Flag, BLOCK_RECEIPTS>)
        {
            auto entry = m_storage.getRow(SYS_NUMBER_2_TXS, blockNumberStr);
            if (!entry) [[unlikely]] { BOOST_THROW_EXCEPTION(std::runtime_error{"GetBlock not found!"}); }

            auto field = entry->getField(0);
            Block block;
            block.decode(field);

            struct
            {
                auto const& operator()(decltype(block.transactionsMetaData) const& metaData) { return metaData.hash; }
            } HashFunc;
            auto range = std::tuple{boost::make_transform_iterator(block.transactionsMetaData.begin(), HashFunc),
                boost::make_transform_iterator(block.transactionsMetaData.end(), HashFunc)};

            constexpr auto isTransaction = std::is_same_v<Flag, BLOCK_TRANSACTIONS>;
            using OutputItemType =
                std::conditional_t<isTransaction, typename Block::Transaction, typename Block::Receipt>;
            auto tableName = isTransaction ? SYS_HASH_2_TX : SYS_HASH_2_RECEIPT;
            auto buffers = m_storage.getRows(tableName, range);
            std::vector<OutputItemType> outputs{std::size(buffers)};

            for (auto i = 0u; i < std::size(buffers); ++i)  // TODO: can be parallel
            {
                outputs[i].decode(buffers[i]);
            }
            return outputs;
        }
        else if constexpr (std::is_same_v<Flag, BLOCK>)
        {
            auto [header, transactions, receipts] =
                getBlock<BLOCK_HEADER, BLOCK_TRANSACTIONS, BLOCK_RECEIPTS>(blockNumber);
            Block block;
            block.blockHeader = std::move(header);
            block.transactions = std::move(transactions);
            block.receipts = std::move(receipts);

            return block;
        }
        else { static_assert(!sizeof(blockNumber), "Wrong input flag!"); }

        return std::tuple_cat(std::tuple{typeid(Flag).name()}, std::tuple{getBlock<Flags...>(blockNumber)});
    }

private:
    auto getBlock(bcos::concepts::block::BlockNumber auto) { return std::tuple{}; }

    Storage m_storage;
};
}  // namespace bcos::ledger