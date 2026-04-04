/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/gpl-3.0.html
 * or you may search the http://www.gnu.org website for the version 3 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include "allegro_db_utils.h"


using namespace ALLEGRO;


#define BLK_FIELD( BLK_T, FIELD ) BlockDataAs<BLK_T>( aBlock ).FIELD


uint32_t ALLEGRO::GetPrimaryNext( const BLOCK_BASE& aBlock )
{
    const uint8_t type = aBlock.GetBlockType();

    switch( type )
    {
    case 0x01: return BLK_FIELD( BLK_0x01_ARC, m_Next );
    case 0x03: return BLK_FIELD( BLK_0x03_FIELD, m_Next );
    case 0x04: return BLK_FIELD( BLK_0x04_NET_ASSIGNMENT, m_Next );
    case 0x05: return BLK_FIELD( BLK_0x05_TRACK, m_Next );
    case 0x0E: return BLK_FIELD( BLK_0x0E_RECT, m_Next );
    case 0x14: return BLK_FIELD( BLK_0x14_GRAPHIC, m_Next );
    case 0x15:
    case 0x16:
    case 0x17: return BLK_FIELD( BLK_0x15_16_17_SEGMENT, m_Next );
    case 0x1B: return BLK_FIELD( BLK_0x1B_NET, m_Next );
    case 0x1D: return BLK_FIELD( BLK_0x1D_CONSTRAINT_SET, m_Next );
    case 0x1E: return BLK_FIELD( BLK_0x1E_SI_MODEL, m_Next );
    case 0x1F: return BLK_FIELD( BLK_0x1F_PADSTACK_DIM, m_Next );
    case 0x2B: return BLK_FIELD( BLK_0x2B_FOOTPRINT_DEF, m_Next );
    case 0x2D: return BLK_FIELD( BLK_0x2D_FOOTPRINT_INST, m_Next );
    case 0x2E: return BLK_FIELD( BLK_0x2E_CONNECTION, m_Next );
    case 0x30: return BLK_FIELD( BLK_0x30_STR_WRAPPER, m_Next );
    case 0x31: return 0; // Doesn't exist
    case 0x32: return BLK_FIELD( BLK_0x32_PLACED_PAD, m_Next );
    case 0x24: return BLK_FIELD( BLK_0x24_RECT, m_Next );
    case 0x28: return BLK_FIELD( BLK_0x28_SHAPE, m_Next );
    case 0x2C: return BLK_FIELD( BLK_0x2C_TABLE, m_Next );
    case 0x33: return BLK_FIELD( BLK_0x33_VIA, m_Next );
    case 0x36: return BLK_FIELD( BLK_0x36_DEF_TABLE, m_Next );
    case 0x37: return BLK_FIELD( BLK_0x37_PTR_ARRAY, m_Next );
    default: return 0;
    }
}


std::optional<FIELD_VALUE> ALLEGRO::GetFirstFieldOfType( const BRD_DB& aDb, uint32_t aFieldsPtr, uint32_t aEndKey,
                                                         uint16_t aFieldCode )
{
    LL_WALKER fieldWalker{ aFieldsPtr, aEndKey, aDb };

    for( const BLOCK_BASE* block : fieldWalker )
    {
        if( block->GetBlockType() != 0x03 )
            continue;

        const auto& field = BlockDataAs<BLK_0x03_FIELD>( *block );

        if( field.m_Hdr1 != aFieldCode )
            continue;

        switch( field.m_SubType )
        {
        case 0x68:
        {
            const std::string* str = std::get_if<std::string>( &field.m_Substruct );

            if( str )
                return wxString( *str );

            break;
        }
        case 0x66:
        {
            const uint32_t* val = std::get_if<uint32_t>( &field.m_Substruct );

            if( val )
                return *val;

            break;
        }
        }
    }

    return std::nullopt;
}


std::optional<int> ALLEGRO::GetFirstFieldOfTypeInt( const BRD_DB& aDb, uint32_t aFieldsPtr, uint32_t aEndKey,
                                                    uint16_t aFieldCode )
{
    std::optional<FIELD_VALUE> result = GetFirstFieldOfType( aDb, aFieldsPtr, aEndKey, aFieldCode );

    if( !result.has_value() )
        return std::nullopt;

    if( uint32_t* val = std::get_if<uint32_t>( &result.value() ) )
        return static_cast<int>( *val );

    return std::nullopt;
}
