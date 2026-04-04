/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#pragma once

/**
 * @file allegro_db_utils.h
 *
 * Utility functions that operate over BRD_DBs and BLOCKs
 */

#include <cstdint>
#include <functional>
#include <optional>
#include <variant>

#include <convert/allegro_pcb_structs.h>
#include <convert/allegro_db.h>

#include <wx/string.h>


namespace ALLEGRO
{

/**
 * Cast a BLOCK_BASE to a typed BLOCK<T> and return the data.
 */
template <typename BLK_T>
const BLK_T& BlockDataAs( const BLOCK_BASE& aBlock )
{
    return static_cast<const BLOCK<BLK_T>&>( aBlock ).GetData();
}


/**
 * Get the next block key in the linked list for a given block.
 *
 * Each block type stores its linked-list pointer in a different member. This function
 * dispatches on block type to return the appropriate m_Next value.
 *
 * @return the next block key, or 0 if there is none
 */
uint32_t GetPrimaryNext( const BLOCK_BASE& aBlock );


/**
 * Range-for-compatible walker over a linked list of BLOCK_BASE objects in a BRD_DB.
 *
 * By default, follows the primary linked-list pointer for each block type
 * (via GetPrimaryNext). A custom next-function can be provided via SetNextFunc()
 * for lists that use a different link field (e.g. m_NextInFp for pad lists).
 */
class LL_WALKER
{
public:
    using NEXT_FUNC_T = std::function<uint32_t( const BLOCK_BASE& )>;

    class iterator
    {
    public:
        iterator( uint32_t aCurrent, uint32_t aTail, const BRD_DB& aBoard, NEXT_FUNC_T aNextFunc ) :
                m_current( aCurrent ),
                m_tail( aTail ),
                m_board( aBoard ),
                m_NextFunc( aNextFunc )
        {
            m_currBlock = m_board.GetObjectByKey( m_current );

            if( !m_currBlock )
                m_current = 0;
        }

        const BLOCK_BASE* operator*() const { return m_currBlock; }

        iterator& operator++()
        {
            if( m_current == m_tail || !m_currBlock )
            {
                m_current = 0;
            }
            else
            {
                m_current = m_NextFunc( *m_currBlock );

                if( m_current == m_tail || m_board.IsSentinel( m_current ) )
                {
                    m_current = 0;
                }
                else
                {
                    m_currBlock = m_board.GetObjectByKey( m_current );

                    if( m_currBlock == nullptr )
                    {
                        m_current = 0;
                    }
                }
            }

            return *this;
        }

        bool operator!=( const iterator& other ) const { return m_current != other.m_current; }

    private:
        uint32_t          m_current;
        const BLOCK_BASE* m_currBlock;
        uint32_t          m_tail;
        const BRD_DB&     m_board;
        NEXT_FUNC_T       m_NextFunc;
    };

    LL_WALKER( uint32_t aHead, uint32_t aTail, const BRD_DB& aBoard ) :
            m_head( aHead ),
            m_tail( aTail ),
            m_board( aBoard )
    {
        m_nextFunction = GetPrimaryNext;
    }

    LL_WALKER( const FILE_HEADER::LINKED_LIST& aList, const BRD_DB& aBoard ) :
            LL_WALKER( aList.m_Head, aList.m_Tail, aBoard )
    {
    }

    iterator begin() const { return iterator( m_head, m_tail, m_board, m_nextFunction ); }
    iterator end() const { return iterator( 0, m_tail, m_board, m_nextFunction ); }

    void SetNextFunc( NEXT_FUNC_T aNextFunc ) { m_nextFunction = aNextFunc; }

private:
    uint32_t      m_head;
    uint32_t      m_tail;
    const BRD_DB& m_board;
    NEXT_FUNC_T   m_nextFunction;
};


/**
 * Some value stored in an 0x03 FIELD block.
 */
using FIELD_VALUE = std::variant<wxString, uint32_t>;

/**
 * Look up the first 0x03 FIELD value of a given type in a linked field chain.
 *
 * @param aDb the database to look in
 * @param aFieldsPtr pointer to the head of the field chain
 * @param aEndKey key of the end-of-chain sentinel node
 * @param aFieldCode the field code to look for (e.g. 0x68)
 * @return the field value as a string or integer, or nullopt if not found
 */
std::optional<FIELD_VALUE> GetFirstFieldOfType( const BRD_DB& aDb, uint32_t aFieldsPtr, uint32_t aEndKey,
                                                uint16_t aFieldCode );

/**
 * Convenience wrapper around GetFirstFieldOfType() for integer-valued fields.
 */
std::optional<int> GetFirstFieldOfTypeInt( const BRD_DB& aDb, uint32_t aFieldsPtr, uint32_t aEndKey,
                                           uint16_t aFieldCode );

} // namespace ALLEGRO
