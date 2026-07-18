/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2025 KiCad Developers, see AUTHORS.txt for contributors.
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
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "gerber_to_png.h"
#include "gerber_file_image.h"
#include "gerber_draw_item.h"
#include "dcode.h"
#include "aperture_macro.h"
#include "excellon_image.h"
#include "excellon_defaults.h"
#include "gerber_to_png_deferred_viewport.hpp"
#include <convert_basic_shapes_to_polygon.h>
#include <jobs/job_gerber_export_png.h>
#include <plotters/plotter_png.h>
#include <geometry/geometry_utils.h>
#include <geometry/shape_poly_set.h>
#include <base_units.h>
#include <trigo.h>
#include <cmath>
#include <unordered_map>
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


namespace
{

namespace DEFERRED_VIEWPORT = GERBER_TO_PNG_DEFERRED_VIEWPORT;

/**
 * Pre-rotated cap vertices for the round-aperture segment (oval/stadium) fast path.
 *
 * TransformOvalToPolygon() rebuilds these cap arcs with RotatePoint() trig calls on every
 * segment even though they only depend on the aperture width.  Cache them once per width;
 * the per-segment work reduces to an offset, one rotation and one translation, matching
 * TransformOvalToPolygon()'s output bit for bit.
 */
struct OVAL_CAP_TEMPLATE
{
    int                   radius = 0;
    std::vector<VECTOR2I> capTop;   // right cap arc points, before the seg_len offset
    std::vector<VECTOR2I> capBot;   // left cap arc points
};


/**
 * Per-render scratch state reused across items to avoid per-item allocations.
 */
struct RENDER_CACHE
{
    std::unordered_map<int, OVAL_CAP_TEMPLATE> ovalCaps;      // key: aperture width in IU
    std::vector<VECTOR2I>                      pointScratch;  // oval fast-path point buffer
    SHAPE_POLY_SET                             polyScratch;   // generated polygon buffer
};


const OVAL_CAP_TEMPLATE& GetOvalCapTemplate( RENDER_CACHE& aCache, int aWidth )
{
    auto it = aCache.ovalCaps.find( aWidth );

    if( it != aCache.ovalCaps.end() )
        return it->second;

    OVAL_CAP_TEMPLATE tmpl;
    tmpl.radius = aWidth / 2;

    // Same segment count computation as TransformOvalToPolygon()
    int arcError = static_cast<int>( gerbIUScale.IU_PER_MM * ARC_LOW_DEF_MM );
    int numSegs = GetArcToSegmentCount( tmpl.radius, arcError, FULL_CIRCLE );
    numSegs = ( numSegs + 7 ) / 8 * 8;

    EDA_ANGLE delta = ANGLE_360 / numSegs;

    for( EDA_ANGLE angle = delta / 2; angle < ANGLE_180; angle += delta )
    {
        VECTOR2I corner( 0, tmpl.radius );
        RotatePoint( corner, angle );
        tmpl.capTop.push_back( corner );
    }

    for( EDA_ANGLE angle = delta / 2; angle < ANGLE_180; angle += delta )
    {
        VECTOR2I corner( 0, -tmpl.radius );
        RotatePoint( corner, angle );
        tmpl.capBot.push_back( corner );
    }

    return aCache.ovalCaps.emplace( aWidth, std::move( tmpl ) ).first->second;
}


/**
 * Build the oval (stadium) polygon for a round-aperture segment.
 *
 * Replicates TransformOvalToPolygon( ..., ERROR_INSIDE, 0, true ) exactly, including
 * SHAPE_LINE_CHAIN::Append()'s consecutive-duplicate suppression and RotatePoint()'s
 * rounding, but hoists the per-point trig out of the rotate loop and reuses cached caps.
 */
void BuildOvalSegmentPoints( const OVAL_CAP_TEMPLATE& aTmpl, const VECTOR2I& aStart,
                             const VECTOR2I& aEnd, std::vector<VECTOR2I>& aOut )
{
    VECTOR2I endp = aEnd - aStart;
    VECTOR2I startp = aStart;

    // normalize the position in order to have endp.x >= 0
    if( endp.x < 0 )
    {
        endp = aStart - aEnd;
        startp = aEnd;
    }

    EDA_ANGLE delta_angle( endp );
    int       seg_len = endp.EuclideanNorm();
    int       radius = aTmpl.radius;

    aOut.clear();

    auto append = [&aOut]( const VECTOR2I& pt )
    {
        // match SHAPE_LINE_CHAIN::Append()'s duplicate suppression
        if( aOut.empty() || aOut.back() != pt )
            aOut.push_back( pt );
    };

    append( VECTOR2I( seg_len, radius ) );

    for( const VECTOR2I& c : aTmpl.capTop )
        append( VECTOR2I( c.x + seg_len, c.y ) );

    append( VECTOR2I( seg_len, -radius ) );
    append( VECTOR2I( 0, -radius ) );

    for( const VECTOR2I& c : aTmpl.capBot )
        append( c );

    append( VECTOR2I( 0, radius ) );

    // Rotate( -delta_angle ) about the origin, then Move( startp ), with the trig and
    // angle normalization hoisted out of the loop.  Matches RotatePoint() bit for bit.
    EDA_ANGLE rot = -delta_angle;
    rot.Normalize();

    if( rot == ANGLE_0 )
    {
        for( VECTOR2I& p : aOut )
            p += startp;
    }
    else if( rot == ANGLE_90 )
    {
        for( VECTOR2I& p : aOut )
            p = VECTOR2I( p.y, -p.x ) + startp;
    }
    else if( rot == ANGLE_180 )
    {
        for( VECTOR2I& p : aOut )
            p = VECTOR2I( -p.x, -p.y ) + startp;
    }
    else if( rot == ANGLE_270 )
    {
        for( VECTOR2I& p : aOut )
            p = VECTOR2I( -p.y, p.x ) + startp;
    }
    else
    {
        double sinus = rot.Sin();
        double cosinus = rot.Cos();

        for( VECTOR2I& p : aOut )
        {
            p = VECTOR2I( KiROUND( ( p.y * sinus ) + ( p.x * cosinus ) ),
                          KiROUND( ( p.y * cosinus ) - ( p.x * sinus ) ) ) + startp;
        }
    }
}


/**
 * Convert a single draw item to the polygons consumed by PNG_PLOTTER.
 */
void BuildRenderPolygons( GERBER_DRAW_ITEM* aItem, RENDER_CACHE& aCache,
                          std::vector<DEFERRED_VIEWPORT::RENDER_POLYGON>& aPolygons )
{
    const SHAPE_POLY_SET* itemPoly = nullptr;
    bool                  needsFlashOffset = false;
    bool                  alreadyInABCoordinates = false;

    if( aItem->m_ShapeAsPolygon.OutlineCount() > 0 )
    {
        itemPoly = &aItem->m_ShapeAsPolygon;
    }
    else if( aItem->m_ShapeType == GBR_SEGMENT )
    {
        D_CODE* dcode = aItem->GetDcodeDescr();

        if( dcode && dcode->m_ApertType != APT_RECT )
        {
            const OVAL_CAP_TEMPLATE& tmpl = GetOvalCapTemplate( aCache, aItem->m_Size.x );

            BuildOvalSegmentPoints( tmpl, aItem->m_Start, aItem->m_End, aCache.pointScratch );

            if( aCache.pointScratch.size() >= 3 )
            {
                DEFERRED_VIEWPORT::RENDER_POLYGON polygon;
                polygon.clearPolarity = aItem->GetLayerPolarity();
                polygon.points.reserve( aCache.pointScratch.size() );

                for( const VECTOR2I& pt : aCache.pointScratch )
                    polygon.points.push_back( aItem->GetABPosition( pt ) );

                aPolygons.push_back( std::move( polygon ) );
            }

            return;
        }
        else
        {
            aCache.polyScratch.RemoveAllContours();
            aItem->ConvertSegmentToPolygon( &aCache.polyScratch );
            itemPoly = &aCache.polyScratch;
        }
    }
    else if( aItem->m_ShapeType == GBR_ARC )
    {
        const int arcError = gerbIUScale.mmToIU( 0.005 );

        aCache.polyScratch.RemoveAllContours();

        if( aItem->m_Start == aItem->m_End )
        {
            int radius = KiROUND( aItem->m_Start.Distance( aItem->m_ArcCentre ) );
            TransformRingToPolygon( aCache.polyScratch, aItem->m_ArcCentre, radius, aItem->m_Size.x,
                                    arcError, ERROR_INSIDE );
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

            TransformArcToPolygon( aCache.polyScratch, aItem->m_Start, mid, aItem->m_End, aItem->m_Size.x,
                                   arcError, ERROR_INSIDE );
        }

        itemPoly = &aCache.polyScratch;
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

                    itemPoly = &aItem->m_AbsolutePolygon;
                    alreadyInABCoordinates = true;
                }
            }
            else
            {
                // The polygon only depends on the aperture definition; build it once per
                // D-code instead of on every flash.
                if( dcode->m_Polygon.OutlineCount() == 0 )
                    dcode->ConvertShapeToPolygon( aItem );

                itemPoly = &dcode->m_Polygon;
                needsFlashOffset = true;
            }
        }
    }

    if( !itemPoly || itemPoly->OutlineCount() == 0 )
        return;

    // Flashed shapes from ConvertShapeToPolygon are centered at (0,0).
    // Offset by the item's position before applying the AB transform.
    VECTOR2I offset = needsFlashOffset ? VECTOR2I( aItem->m_Start ) : VECTOR2I( 0, 0 );

    for( int i = 0; i < itemPoly->OutlineCount(); i++ )
    {
        const SHAPE_LINE_CHAIN& outline = itemPoly->COutline( i );
        DEFERRED_VIEWPORT::RENDER_POLYGON polygon;
        polygon.clearPolarity = aItem->GetLayerPolarity();
        polygon.points.reserve( outline.PointCount() );

        for( int j = 0; j < outline.PointCount(); j++ )
        {
            if( alreadyInABCoordinates )
                polygon.points.push_back( outline.CPoint( j ) );
            else
                polygon.points.push_back( aItem->GetABPosition( outline.CPoint( j ) + offset ) );
        }

        if( polygon.points.size() >= 3 )
            aPolygons.push_back( std::move( polygon ) );
    }
}


void PlotRenderPolygon( const DEFERRED_VIEWPORT::RENDER_POLYGON& aPolygon, PNG_PLOTTER& aPlotter )
{
    aPlotter.PlotPoly( aPolygon.points, FILL_T::FILLED_SHAPE, 0 );
}


/**
 * Render a single draw item to the plotter.
 */
void RenderItem( GERBER_DRAW_ITEM* aItem, PNG_PLOTTER& aPlotter, const KIGFX::COLOR4D& aColor,
                 RENDER_CACHE& aCache, std::vector<DEFERRED_VIEWPORT::RENDER_POLYGON>& aPolygonBuffer )
{
    aPolygonBuffer.clear();

    BuildRenderPolygons( aItem, aCache, aPolygonBuffer );

    aPlotter.SetColor( aColor );

    for( const DEFERRED_VIEWPORT::RENDER_POLYGON& polygon : aPolygonBuffer )
        PlotRenderPolygon( polygon, aPlotter );
}

} // anonymous namespace


GERBER_PLOTTER_VIEWPORT CalculatePlotterViewport( const BOX2I& aBBox, int aDpi, int aWidth, int aHeight )
{
    return CalculatePlotterViewport( aBBox, aDpi, aDpi, aWidth, aHeight );
}


GERBER_PLOTTER_VIEWPORT CalculatePlotterViewport( const BOX2I& aBBox, int aDpiX, int aDpiY,
                                                  int aWidth, int aHeight )
{
    GERBER_PLOTTER_VIEWPORT vp;
    vp.width = aWidth;
    vp.height = aHeight;

    if( vp.width == 0 && vp.height == 0 )
    {
        double iuPerInch = gerbIUScale.IU_PER_MM * 25.4;
        double widthInches = static_cast<double>( aBBox.GetWidth() ) / iuPerInch;
        double heightInches = static_cast<double>( aBBox.GetHeight() ) / iuPerInch;

        vp.width = static_cast<int>( std::ceil( widthInches * aDpiX ) );
        vp.height = static_cast<int>( std::ceil( heightInches * aDpiY ) );

        if( vp.width < MIN_PIXEL_SIZE )
            vp.width = MIN_PIXEL_SIZE;

        if( vp.height < MIN_PIXEL_SIZE )
            vp.height = MIN_PIXEL_SIZE;
    }
    else if( vp.width == 0 )
    {
        double aspect = static_cast<double>( aBBox.GetWidth() ) / aBBox.GetHeight();
        vp.width = static_cast<int>( vp.height * aspect );
    }
    else if( vp.height == 0 )
    {
        double aspect = static_cast<double>( aBBox.GetHeight() ) / aBBox.GetWidth();
        vp.height = static_cast<int>( vp.width * aspect );
    }

    vp.iuPerDecimil = gerbIUScale.IU_PER_MILS / 10.0;

    double scaleX = static_cast<double>( vp.width ) * vp.iuPerDecimil * 10000.0
                    / ( aBBox.GetWidth() * aDpiX );
    double scaleY = static_cast<double>( vp.height ) * vp.iuPerDecimil * 10000.0
                    / ( aBBox.GetHeight() * aDpiY );
    vp.plotScale = std::min( scaleX, scaleY );
    vp.plotScaleX = scaleX;
    vp.plotScaleY = scaleY;
    vp.offset = aBBox.GetOrigin();

    return vp;
}


bool RenderGerberToPng( const wxString& aInputPath, const wxString& aOutputPath, const GERBER_RENDER_OPTIONS& aOptions,
                        wxString* aErrorMsg, wxArrayString* aMessages )
{
    auto image = LoadGerberOrExcellon( aInputPath, aErrorMsg, aMessages );

    if( !image )
        return false;

    if( image->m_ImageNegative )
    {
        if( aErrorMsg )
        {
            *aErrorMsg = wxString::Format(
                    wxS( "Unsupported Gerber command IPNEG in %s: "
                         "BMP export does not support image negative polarity." ),
                    aInputPath );
        }

        return false;
    }

    if( image->GetItemsCount() == 0 )
    {
        if( aErrorMsg )
            *aErrorMsg = wxS( "Gerber file contains no draw items" );

        return false;
    }

    BOX2I bbox;
    std::vector<DEFERRED_VIEWPORT::RENDER_POLYGON> deferredPolygons;
    RENDER_CACHE renderCache;

    if( aOptions.HasViewportOverride() )
    {
        bbox = DEFERRED_VIEWPORT::MakeBBoxFromGerberViewport( aOptions.originXMm, aOptions.originYMm,
                                                              aOptions.windowWidthMm, aOptions.windowHeightMm );
    }
    else
    {
        if( aOptions.deferredViewport )
        {
            bool bboxValid = false;

            for( GERBER_DRAW_ITEM* item : image->GetItems() )
            {
                size_t firstPolygon = deferredPolygons.size();

                BuildRenderPolygons( item, renderCache, deferredPolygons );

                for( size_t i = firstPolygon; i < deferredPolygons.size(); ++i )
                    DEFERRED_VIEWPORT::MergePolygonBBox( deferredPolygons[i].points, bbox, bboxValid );
            }

            if( !bboxValid )
            {
                if( aErrorMsg )
                    *aErrorMsg = wxS( "Gerber file has no renderable polygons" );

                return false;
            }

            if( !DEFERRED_VIEWPORT::WriteDeferredViewportBBox( bbox, aErrorMsg ) )
                return false;

            if( !DEFERRED_VIEWPORT::ReadDeferredViewport( bbox, aErrorMsg ) )
                return false;
        }
        else
        {
            bbox = CalculateGerberBoundingBox( image.get() );
        }
    }

    if( bbox.GetWidth() == 0 || bbox.GetHeight() == 0 )
    {
        if( aErrorMsg )
            *aErrorMsg = wxS( "Gerber file has zero-size bounding box" );

        return false;
    }

    // When using a viewport override with DPI, calculate pixel dimensions from the window
    int reqWidth = aOptions.width;
    int reqHeight = aOptions.height;

    if( ( aOptions.HasViewportOverride() || aOptions.deferredViewport ) && reqWidth == 0 && reqHeight == 0 )
    {
        double mmPerInch = 25.4;

        double windowWidthMm = aOptions.windowWidthMm;
        double windowHeightMm = aOptions.windowHeightMm;

        if( aOptions.deferredViewport )
        {
            windowWidthMm = static_cast<double>( bbox.GetWidth() ) / gerbIUScale.IU_PER_MM;
            windowHeightMm = static_cast<double>( bbox.GetHeight() ) / gerbIUScale.IU_PER_MM;
        }

        reqWidth = static_cast<int>( std::ceil( windowWidthMm / mmPerInch * aOptions.GetDpiX() ) );
        reqHeight = static_cast<int>( std::ceil( windowHeightMm / mmPerInch * aOptions.GetDpiY() ) );
    }

    GERBER_PLOTTER_VIEWPORT vp = CalculatePlotterViewport( bbox, aOptions.GetDpiX(), aOptions.GetDpiY(),
                                                           reqWidth, reqHeight );

    PNG_PLOTTER plotter;
    plotter.SetColorMode( true );
    plotter.SetPixelSize( vp.width, vp.height );
    plotter.SetResolution( aOptions.GetDpiX(), aOptions.GetDpiY() );
    plotter.SetAntialias( aOptions.antialias );
    plotter.SetBackgroundColor( aOptions.backgroundColor );
    plotter.SetViewport( vp.offset, vp.iuPerDecimil, vp.plotScaleX, vp.plotScaleY, false );
    plotter.OpenFile( aOutputPath );

    if( !plotter.StartPlot( wxEmptyString ) )
    {
        if( aErrorMsg )
            *aErrorMsg = wxS( "Failed to start PNG plotter" );

        return false;
    }

    // Render all items with proper polarity handling.
    // Negative (clear) polarity items erase copper by drawing with the background color.
    // On transparent exports the background is alpha=0, so source-over is a no-op.
    // Switch to CAIRO_OPERATOR_CLEAR so those regions become fully transparent.
    bool transparentBg = ( aOptions.backgroundColor.a == 0 );

    if( aOptions.deferredViewport )
    {
        for( const DEFERRED_VIEWPORT::RENDER_POLYGON& polygon : deferredPolygons )
        {
            if( polygon.clearPolarity )
            {
                plotter.SetClearCompositing( transparentBg );
                plotter.SetColor( aOptions.backgroundColor );
                PlotRenderPolygon( polygon, plotter );
                plotter.SetClearCompositing( false );
            }
            else
            {
                plotter.SetColor( aOptions.foregroundColor );
                PlotRenderPolygon( polygon, plotter );
            }
        }
    }
    else
    {
        std::vector<DEFERRED_VIEWPORT::RENDER_POLYGON> polygonBuffer;

        for( GERBER_DRAW_ITEM* item : image->GetItems() )
        {
            if( item->GetLayerPolarity() )
            {
                plotter.SetClearCompositing( transparentBg );
                RenderItem( item, plotter, aOptions.backgroundColor, renderCache, polygonBuffer );
                plotter.SetClearCompositing( false );
            }
            else
            {
                RenderItem( item, plotter, aOptions.foregroundColor, renderCache, polygonBuffer );
            }
        }
    }

    if( !plotter.EndPlot() )
    {
        if( aErrorMsg )
            *aErrorMsg = wxString::Format( wxS( "Failed to save BMP file: %s" ), aOutputPath );

        return false;
    }

    return true;
}


bool RenderGerberToPng( const wxString& aInputPath, const wxString& aOutputPath, const JOB_GERBER_EXPORT_PNG& aJob,
                        wxString* aErrorMsg, wxArrayString* aMessages )
{
    GERBER_RENDER_OPTIONS options;
    options.dpi = aJob.m_dpi;
    options.dpiX = aJob.m_dpiX;
    options.dpiY = aJob.m_dpiY;
    options.width = aJob.m_width;
    options.height = aJob.m_height;
    options.antialias = aJob.m_antialias;
    options.deferredViewport = aJob.m_deferredViewport;
    options.backgroundColor =
            aJob.m_transparentBackground ? KIGFX::COLOR4D( 1.0, 1.0, 1.0, 0.0 ) : KIGFX::COLOR4D::WHITE;

    if( !aJob.m_foregroundColor.IsEmpty() )
        options.foregroundColor = KIGFX::COLOR4D( aJob.m_foregroundColor );

    if( !aJob.m_backgroundColor.IsEmpty() )
        options.backgroundColor = KIGFX::COLOR4D( aJob.m_backgroundColor );

    double toMm = 1.0;

    switch( aJob.m_units )
    {
    case JOB_GERBER_EXPORT_PNG::UNITS::INCH: toMm = 25.4;   break;
    case JOB_GERBER_EXPORT_PNG::UNITS::MILS: toMm = 0.0254; break;
    case JOB_GERBER_EXPORT_PNG::UNITS::MM:   toMm = 1.0;    break;
    }

    options.originXMm = aJob.m_originX * toMm;
    options.originYMm = aJob.m_originY * toMm;
    options.windowWidthMm = aJob.m_windowWidth * toMm;
    options.windowHeightMm = aJob.m_windowHeight * toMm;

    return RenderGerberToPng( aInputPath, aOutputPath, options, aErrorMsg, aMessages );
}
