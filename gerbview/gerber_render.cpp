/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2026 KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "gerber_render.h"
#include "aperture_macro.h"
#include "dcode.h"
#include "excellon_defaults.h"
#include "gerber_draw_item.h"
#include "gerber_file_image.h"
#include "excellon_image.h"

#include <base_units.h>
#include <convert_basic_shapes_to_polygon.h>
#include <geometry/shape_poly_set.h>
#include <plotters/plotter.h>
#include <trigo.h>

#include <cmath>
#include <wx/filename.h>


bool IsExcellonFile( const wxString& aPath )
{
    wxFileName fn( aPath );
    wxString   ext = fn.GetExt().Lower();
    return ext == wxS( "drl" ) || ext == wxS( "xln" ) || ext == wxS( "exc" ) || ext == wxS( "ncd" );
}


BOX2I CalculateGerberBoundingBox( GERBER_FILE_IMAGE* aImage )
{
    BOX2I bbox;
    bool  first = true;

    for( GERBER_DRAW_ITEM* item : aImage->GetItems() )
    {
        BOX2I itemBox = item->GetBoundingBox();

        if( first )
        {
            bbox = itemBox;
            first = false;
        }
        else
        {
            bbox.Merge( itemBox );
        }
    }

    return bbox;
}


BOX2I CalculateGerberViewportBoundingBox( double aOriginXMm, double aOriginYMm, double aWindowWidthMm,
                                          double aWindowHeightMm )
{
    // Viewport is specified in Gerber-native coordinates (Y increases upward).  KiCad stores
    // Gerber items with Y negated (Y increases downward), so negate the origin Y and flip the
    // window vertically.
    double iuPerMm = gerbIUScale.IU_PER_MM;
    int    ox = static_cast<int>( std::round( aOriginXMm * iuPerMm ) );
    int    oy = static_cast<int>( std::round( -( aOriginYMm + aWindowHeightMm ) * iuPerMm ) );
    int    w = static_cast<int>( std::round( aWindowWidthMm * iuPerMm ) );
    int    h = static_cast<int>( std::round( aWindowHeightMm * iuPerMm ) );

    return BOX2I( VECTOR2I( ox, oy ), VECTOR2I( w, h ) );
}


std::unique_ptr<GERBER_FILE_IMAGE> LoadGerberOrExcellon( const wxString& aPath, wxString* aErrorMsg,
                                                         wxArrayString* aMessages )
{
    std::unique_ptr<GERBER_FILE_IMAGE> image;

    if( IsExcellonFile( aPath ) )
    {
        auto              excellon = std::make_unique<EXCELLON_IMAGE>( 0 );
        EXCELLON_DEFAULTS defaults;

        if( !excellon->LoadFile( aPath, &defaults ) )
        {
            if( aErrorMsg )
                *aErrorMsg = wxString::Format( wxS( "Failed to load Excellon file: %s" ), aPath );

            return nullptr;
        }

        image = std::move( excellon );
    }
    else
    {
        image = std::make_unique<GERBER_FILE_IMAGE>( 0 );

        if( !image->LoadGerberFile( aPath ) )
        {
            if( aErrorMsg )
                *aErrorMsg = wxString::Format( wxS( "Failed to load Gerber file: %s" ), aPath );

            return nullptr;
        }
    }

    if( aMessages )
        *aMessages = image->GetMessages();

    return image;
}


void RenderGerberItemToPlotter( GERBER_DRAW_ITEM* aItem, PLOTTER& aPlotter, const KIGFX::COLOR4D& aColor )
{
    SHAPE_POLY_SET itemPoly;
    bool           needsFlashOffset = false;
    bool           alreadyInABCoordinates = false;

    if( aItem->m_ShapeAsPolygon.OutlineCount() > 0 )
    {
        itemPoly = aItem->m_ShapeAsPolygon;
    }
    else if( aItem->m_ShapeType == GBR_SEGMENT )
    {
        D_CODE* dcode = aItem->GetDcodeDescr();

        if( dcode && dcode->m_ApertType != APT_RECT )
        {
            int arcError = static_cast<int>( gerbIUScale.IU_PER_MM * ARC_LOW_DEF_MM );
            TransformOvalToPolygon( itemPoly, aItem->m_Start, aItem->m_End, aItem->m_Size.x, arcError,
                                    ERROR_INSIDE );
        }
        else
        {
            aItem->ConvertSegmentToPolygon( &itemPoly );
        }
    }
    else if( aItem->m_ShapeType == GBR_ARC )
    {
        const int arcError = gerbIUScale.mmToIU( 0.005 );

        if( aItem->m_Start == aItem->m_End )
        {
            int radius = KiROUND( aItem->m_Start.Distance( aItem->m_ArcCentre ) );
            TransformRingToPolygon( itemPoly, aItem->m_ArcCentre, radius, aItem->m_Size.x, arcError, ERROR_INSIDE );
        }
        else
        {
            double startAngle = atan2( static_cast<double>( aItem->m_Start.y - aItem->m_ArcCentre.y ),
                                       static_cast<double>( aItem->m_Start.x - aItem->m_ArcCentre.x ) );
            double endAngle = atan2( static_cast<double>( aItem->m_End.y - aItem->m_ArcCentre.y ),
                                     static_cast<double>( aItem->m_End.x - aItem->m_ArcCentre.x ) );

            if( startAngle > endAngle )
                endAngle += 2.0 * M_PI;

            VECTOR2I mid = GetRotated( aItem->m_Start, aItem->m_ArcCentre,
                                       -EDA_ANGLE( ( endAngle - startAngle ) / 2.0, RADIANS_T ) );

            TransformArcToPolygon( itemPoly, aItem->m_Start, mid, aItem->m_End, aItem->m_Size.x, arcError,
                                   ERROR_INSIDE );
        }
    }
    else if( aItem->m_Flashed )
    {
        D_CODE* dcode = aItem->GetDcodeDescr();

        if( dcode )
        {
            if( dcode->m_ApertType == APT_MACRO )
            {
                APERTURE_MACRO* macro = dcode->GetMacro();

                if( macro )
                {
                    // Aperture macro polygons are returned in absolute AB coordinates, matching
                    // GERBVIEW_PAINTER::drawApertureMacro().  Do not offset/transform them again.
                    if( aItem->m_AbsolutePolygon.OutlineCount() == 0 )
                        aItem->m_AbsolutePolygon = *macro->GetApertureMacroShape( aItem, aItem->m_Start );

                    itemPoly = aItem->m_AbsolutePolygon;
                    alreadyInABCoordinates = true;
                }
            }
            else
            {
                dcode->ConvertShapeToPolygon( aItem );
                itemPoly = dcode->m_Polygon;
                needsFlashOffset = true;
            }
        }
    }

    if( itemPoly.OutlineCount() == 0 )
        return;

    // Flashed shapes from ConvertShapeToPolygon are centered at (0,0).  Offset by the item's
    // position before applying the AB transform.
    VECTOR2I offset = needsFlashOffset ? VECTOR2I( aItem->m_Start ) : VECTOR2I( 0, 0 );

    aPlotter.SetColor( aColor );

    for( int i = 0; i < itemPoly.OutlineCount(); i++ )
    {
        const SHAPE_LINE_CHAIN& outline = itemPoly.COutline( i );
        std::vector<VECTOR2I>   pts;
        pts.reserve( outline.PointCount() );

        for( int j = 0; j < outline.PointCount(); j++ )
        {
            if( alreadyInABCoordinates )
                pts.push_back( outline.CPoint( j ) );
            else
                pts.push_back( aItem->GetABPosition( outline.CPoint( j ) + offset ) );
        }

        if( pts.size() >= 3 )
            aPlotter.PlotPoly( pts, FILL_T::FILLED_SHAPE, 0, nullptr );
    }
}


void RenderGerberImageToPlotter( GERBER_FILE_IMAGE* aImage, PLOTTER& aPlotter,
                                 const KIGFX::COLOR4D& aForegroundColor,
                                 const KIGFX::COLOR4D& aBackgroundColor,
                                 const std::function<void( bool )>& aSetClearCompositing )
{
    bool canClearTransparentBackground = aBackgroundColor.a == 0 && static_cast<bool>( aSetClearCompositing );

    for( GERBER_DRAW_ITEM* item : aImage->GetItems() )
    {
        if( item->GetLayerPolarity() )
        {
            if( canClearTransparentBackground )
                aSetClearCompositing( true );

            RenderGerberItemToPlotter( item, aPlotter, aBackgroundColor );

            if( canClearTransparentBackground )
                aSetClearCompositing( false );
        }
        else
        {
            RenderGerberItemToPlotter( item, aPlotter, aForegroundColor );
        }
    }
}
