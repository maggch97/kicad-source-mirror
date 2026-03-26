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

#include "3d_placeholder_utils.h"

#include <footprint.h>
#include <pad.h>
#include <board_item.h>
#include <layer_ids.h>
#include <geometry/eda_angle.h>


BOX2I CalcPlaceholderLocalBox( const FOOTPRINT* aFootprint )
{
    BOX2I localBox;
    bool  hasLocalBounds = false;

    for( PAD* pad : aFootprint->Pads() )
    {
        VECTOR2I padPos = pad->GetFPRelativePosition();
        VECTOR2I padSize = pad->GetSize( PADSTACK::ALL_LAYERS );

        BOX2I padBox;
        padBox.SetOrigin( padPos - padSize / 2 );
        padBox.SetSize( padSize );

        if( !hasLocalBounds )
        {
            localBox = padBox;
            hasLocalBounds = true;
        }
        else
        {
            localBox.Merge( padBox );
        }
    }

    if( !hasLocalBounds )
    {
        VECTOR2I  fpPos = aFootprint->GetPosition();
        EDA_ANGLE fpAngle = aFootprint->GetOrientation();

        for( BOARD_ITEM* item : aFootprint->GraphicalItems() )
        {
            PCB_LAYER_ID layer = item->GetLayer();

            if( layer == F_Fab || layer == B_Fab || layer == F_CrtYd || layer == B_CrtYd || layer == F_SilkS
                || layer == B_SilkS )
            {
                BOX2I itemBox = item->GetBoundingBox();

                VECTOR2I corners[4] = { itemBox.GetOrigin() - fpPos,
                                        VECTOR2I( itemBox.GetRight(), itemBox.GetTop() ) - fpPos,
                                        VECTOR2I( itemBox.GetRight(), itemBox.GetBottom() ) - fpPos,
                                        VECTOR2I( itemBox.GetLeft(), itemBox.GetBottom() ) - fpPos };

                BOX2I localItemBox;

                for( int ci = 0; ci < 4; ++ci )
                {
                    RotatePoint( corners[ci], -fpAngle );

                    if( ci == 0 )
                    {
                        localItemBox.SetOrigin( corners[ci] );
                        localItemBox.SetSize( VECTOR2I( 0, 0 ) );
                    }
                    else
                    {
                        localItemBox.Merge( BOX2I( corners[ci], VECTOR2I( 0, 0 ) ) );
                    }
                }

                if( !hasLocalBounds )
                {
                    localBox = localItemBox;
                    hasLocalBounds = true;
                }
                else
                {
                    localBox.Merge( localItemBox );
                }
            }
        }
    }

    if( !hasLocalBounds )
    {
        BOX2I fpBox = aFootprint->GetBoundingBox( false );
        int   size = std::min( fpBox.GetWidth(), fpBox.GetHeight() );
        localBox.SetOrigin( VECTOR2I( -size / 2, -size / 2 ) );
        localBox.SetSize( VECTOR2I( size, size ) );
    }

    return localBox;
}
