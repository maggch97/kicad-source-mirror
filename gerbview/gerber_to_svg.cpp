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

#include "gerber_to_svg.h"
#include "gerber_file_image.h"
#include "gerber_render.h"

#include <base_units.h>
#include <convert_basic_shapes_to_polygon.h>
#include <geometry/shape_poly_set.h>
#include <jobs/job_gerber_export_svg.h>
#include <math/util.h>
#include <plotters/plotter.h>
#include <trigo.h>

#include <cairo.h>
#include <cairo-svg.h>

#include <algorithm>
#include <cmath>
#include <cstdio>


namespace
{

constexpr double POINTS_PER_MM = 72.0 / 25.4;
constexpr double TWO_PI = 6.28318530717958647692;


cairo_status_t cairoWriteToFile( void* aClosure, const unsigned char* aData, unsigned int aLength )
{
    FILE* file = static_cast<FILE*>( aClosure );

    return std::fwrite( aData, 1, aLength, file ) == aLength ? CAIRO_STATUS_SUCCESS
                                                             : CAIRO_STATUS_WRITE_ERROR;
}


class GERBER_CAIRO_SVG_PLOTTER : public PLOTTER
{
public:
    GERBER_CAIRO_SVG_PLOTTER( double aPageWidthMm, double aPageHeightMm ) :
            m_pageWidthMm( aPageWidthMm ),
            m_pageHeightMm( aPageHeightMm )
    {
    }

    ~GERBER_CAIRO_SVG_PLOTTER() override
    {
        destroyCairoObjects();
    }

    PLOT_FORMAT GetPlotterType() const override { return PLOT_FORMAT::SVG; }

    bool OpenFile( const wxString& aFullFilename ) override
    {
        m_filename = aFullFilename;

        wxASSERT( !m_outputFile );

        m_outputFile = wxFopen( m_filename, wxT( "wb" ) );

        return m_outputFile != nullptr;
    }

    bool StartPlot( const wxString& aPageNumber ) override
    {
        if( !m_outputFile )
            return false;

#if CAIRO_VERSION >= CAIRO_VERSION_ENCODE( 1, 16, 0 )
        m_surface = cairo_svg_surface_create_for_stream( cairoWriteToFile, m_outputFile,
                                                         m_pageWidthMm, m_pageHeightMm );
#else
        m_surface = cairo_svg_surface_create_for_stream( cairoWriteToFile, m_outputFile,
                                                         m_pageWidthMm * POINTS_PER_MM,
                                                         m_pageHeightMm * POINTS_PER_MM );
#endif

        if( cairo_surface_status( m_surface ) != CAIRO_STATUS_SUCCESS )
            return false;

#if CAIRO_VERSION >= CAIRO_VERSION_ENCODE( 1, 16, 0 )
        cairo_svg_surface_set_document_unit( m_surface, CAIRO_SVG_UNIT_MM );
#endif

        m_context = cairo_create( m_surface );

        if( cairo_status( m_context ) != CAIRO_STATUS_SUCCESS )
            return false;

#if CAIRO_VERSION < CAIRO_VERSION_ENCODE( 1, 16, 0 )
        // Older Cairo versions do not support SVG document units; they use points.
        cairo_scale( m_context, POINTS_PER_MM, POINTS_PER_MM );
#endif

        cairo_set_line_cap( m_context, CAIRO_LINE_CAP_ROUND );
        cairo_set_line_join( m_context, CAIRO_LINE_JOIN_ROUND );

        SetColor( m_currentColor );

        return true;
    }

    bool EndPlot() override
    {
        cairo_status_t status = CAIRO_STATUS_SUCCESS;

        if( m_context )
        {
            cairo_show_page( m_context );
            status = cairo_status( m_context );
            cairo_destroy( m_context );
            m_context = nullptr;
        }

        if( m_surface )
        {
            cairo_surface_finish( m_surface );

            if( status == CAIRO_STATUS_SUCCESS )
                status = cairo_surface_status( m_surface );

            cairo_surface_destroy( m_surface );
            m_surface = nullptr;
        }

        bool closeOk = true;

        if( m_outputFile )
        {
            closeOk = std::fclose( m_outputFile ) == 0;
            m_outputFile = nullptr;
        }

        return status == CAIRO_STATUS_SUCCESS && closeOk;
    }

    void SetCurrentLineWidth( int aWidth, void* aData = nullptr ) override
    {
        m_currentPenWidth = aWidth;

        if( m_context )
        {
            double deviceWidth = userToDeviceSizeMm( static_cast<double>( aWidth ) );
            cairo_set_line_width( m_context, deviceWidth > 0 ? deviceWidth : 1.0 );
        }
    }

    void SetColor( const COLOR4D& aColor ) override
    {
        m_currentColor = aColor;

        if( m_context )
            cairo_set_source_rgba( m_context, aColor.r, aColor.g, aColor.b, aColor.a );
    }

    void SetDash( int aLineWidth, LINE_STYLE aLineStyle ) override
    {
        if( m_context )
            cairo_set_dash( m_context, nullptr, 0, 0 );
    }

    void SetViewport( const VECTOR2I& aOffset, double aIusPerDecimil, double aScale,
                      bool aMirror ) override
    {
        m_plotMirror = aMirror;
        m_yaxisReversed = true;
        m_plotOffset = aOffset;
        m_plotScale = aScale;
        m_IUsPerDecimil = aIusPerDecimil;

        double iusPerMM = m_IUsPerDecimil / 2.54 * 1000.0;
        m_iuPerDeviceUnit = 1.0 / iusPerMM;
        m_paperSize = VECTOR2I( KiROUND( m_pageWidthMm * iusPerMM ),
                                KiROUND( m_pageHeightMm * iusPerMM ) );
    }

    void Rect( const VECTOR2I& aP1, const VECTOR2I& aP2, FILL_T aFill, int aWidth,
               int aCornerRadius = 0 ) override
    {
        if( !m_context )
            return;

        VECTOR2D start = userToDeviceCoordinatesMm( aP1 );
        VECTOR2D end = userToDeviceCoordinatesMm( aP2 );

        double x = std::min( start.x, end.x );
        double y = std::min( start.y, end.y );
        double width = std::abs( end.x - start.x );
        double height = std::abs( end.y - start.y );

        cairo_rectangle( m_context, x, y, width, height );

        if( aFill == FILL_T::NO_FILL )
        {
            SetCurrentLineWidth( aWidth );
            cairo_stroke( m_context );
        }
        else
        {
            cairo_fill( m_context );
        }
    }

    void Circle( const VECTOR2I& aCenter, int aDiameter, FILL_T aFill, int aWidth ) override
    {
        if( !m_context )
            return;

        VECTOR2D center = userToDeviceCoordinatesMm( aCenter );
        double radius = userToDeviceSizeMm( static_cast<double>( aDiameter ) / 2.0 );

        cairo_arc( m_context, center.x, center.y, radius, 0, TWO_PI );

        if( aFill == FILL_T::NO_FILL )
        {
            SetCurrentLineWidth( aWidth );
            cairo_stroke( m_context );
        }
        else
        {
            cairo_fill( m_context );
        }
    }

    void Arc( const VECTOR2D& aCenter, const EDA_ANGLE& aStartAngle, const EDA_ANGLE& aAngle,
              double aRadius, FILL_T aFill, int aWidth ) override
    {
        if( !m_context )
            return;

        VECTOR2D center = userToDeviceCoordinatesMm( VECTOR2I( aCenter.x, aCenter.y ) );
        double radius = userToDeviceSizeMm( aRadius );
        double startRad = aStartAngle.AsRadians();
        double endRad = ( aStartAngle + aAngle ).AsRadians();

        if( aAngle.AsDegrees() < 0 )
            cairo_arc_negative( m_context, center.x, center.y, radius, startRad, endRad );
        else
            cairo_arc( m_context, center.x, center.y, radius, startRad, endRad );

        if( aFill == FILL_T::NO_FILL )
        {
            SetCurrentLineWidth( aWidth );
            cairo_stroke( m_context );
        }
        else
        {
            cairo_fill( m_context );
        }
    }

    void PenTo( const VECTOR2I& aPos, char aPlume ) override
    {
        if( !m_context )
            return;

        VECTOR2D pos = userToDeviceCoordinatesMm( aPos );

        switch( aPlume )
        {
        case 'U':
            cairo_move_to( m_context, pos.x, pos.y );
            m_penState = 'U';
            break;

        case 'D':
            if( m_penState == 'U' )
                cairo_move_to( m_context, m_penLastDevicePos.x, m_penLastDevicePos.y );

            cairo_line_to( m_context, pos.x, pos.y );
            m_penState = 'D';
            break;

        case 'Z':
            cairo_stroke( m_context );
            m_penState = 'Z';
            break;
        }

        m_penLastDevicePos = pos;
    }

    void PlotPoly( const std::vector<VECTOR2I>& aCornerList, FILL_T aFill, int aWidth,
                   void* aData = nullptr ) override
    {
        if( !m_context || aCornerList.size() < 2 )
            return;

        VECTOR2D start = userToDeviceCoordinatesMm( aCornerList[0] );
        cairo_move_to( m_context, start.x, start.y );

        for( size_t i = 1; i < aCornerList.size(); i++ )
        {
            VECTOR2D pt = userToDeviceCoordinatesMm( aCornerList[i] );
            cairo_line_to( m_context, pt.x, pt.y );
        }

        if( aFill == FILL_T::NO_FILL )
        {
            SetCurrentLineWidth( aWidth );
            cairo_stroke( m_context );
        }
        else
        {
            cairo_close_path( m_context );
            cairo_fill( m_context );
        }
    }

    void FlashPadCircle( const VECTOR2I& aPadPos, int aDiameter, void* aData ) override
    {
        Circle( aPadPos, aDiameter, FILL_T::FILLED_SHAPE, 0 );
    }

    void FlashPadOval( const VECTOR2I& aPadPos, const VECTOR2I& aSize,
                       const EDA_ANGLE& aPadOrient, void* aData ) override
    {
        int width = std::min( aSize.x, aSize.y );
        int len = std::max( aSize.x, aSize.y ) - width;

        if( len == 0 )
        {
            FlashPadCircle( aPadPos, width, aData );
            return;
        }

        VECTOR2I delta;

        if( aSize.x > aSize.y )
            delta.x = len / 2;
        else
            delta.y = len / 2;

        RotatePoint( delta, aPadOrient );
        ThickSegment( aPadPos - delta, aPadPos + delta, width, aData );
    }

    void FlashPadRect( const VECTOR2I& aPadPos, const VECTOR2I& aSize,
                       const EDA_ANGLE& aPadOrient, void* aData ) override
    {
        std::vector<VECTOR2I> corners =
        {
            VECTOR2I( -aSize.x / 2, -aSize.y / 2 ),
            VECTOR2I( -aSize.x / 2,  aSize.y / 2 ),
            VECTOR2I(  aSize.x / 2,  aSize.y / 2 ),
            VECTOR2I(  aSize.x / 2, -aSize.y / 2 )
        };

        for( VECTOR2I& corner : corners )
        {
            RotatePoint( corner, aPadOrient );
            corner += aPadPos;
        }

        PlotPoly( corners, FILL_T::FILLED_SHAPE, 0, aData );
    }

    void FlashPadRoundRect( const VECTOR2I& aPadPos, const VECTOR2I& aSize, int aCornerRadius,
                            const EDA_ANGLE& aOrient, void* aData ) override
    {
        SHAPE_POLY_SET outline;
        TransformRoundChamferedRectToPolygon( outline, aPadPos, aSize, aOrient, aCornerRadius,
                                              0.0, 0, 0, GetPlotterArcHighDef(), ERROR_INSIDE );

        if( outline.OutlineCount() > 0 )
            PLOTTER::PlotPoly( outline.COutline( 0 ), FILL_T::FILLED_SHAPE, 0, aData );
    }

    void FlashPadCustom( const VECTOR2I& aPadPos, const VECTOR2I& aSize,
                         const EDA_ANGLE& aPadOrient, SHAPE_POLY_SET* aPolygons,
                         void* aData ) override
    {
        if( !aPolygons )
            return;

        for( int i = 0; i < aPolygons->OutlineCount(); i++ )
            PLOTTER::PlotPoly( aPolygons->COutline( i ), FILL_T::FILLED_SHAPE, 0, aData );
    }

    void FlashPadTrapez( const VECTOR2I& aPadPos, const VECTOR2I* aCorners,
                         const EDA_ANGLE& aPadOrient, void* aData ) override
    {
        std::vector<VECTOR2I> corners;
        corners.reserve( 4 );

        for( int i = 0; i < 4; i++ )
        {
            VECTOR2I corner = aCorners[i];
            RotatePoint( corner, aPadOrient );
            corners.push_back( corner + aPadPos );
        }

        PlotPoly( corners, FILL_T::FILLED_SHAPE, 0, aData );
    }

    void FlashRegularPolygon( const VECTOR2I& aShapePos, int aDiameter, int aCornerCount,
                              const EDA_ANGLE& aOrient, void* aData ) override
    {
        std::vector<VECTOR2I> corners;
        corners.reserve( aCornerCount );

        double radius = aDiameter / 2.0;
        EDA_ANGLE delta = ANGLE_360 / aCornerCount;

        for( int i = 0; i < aCornerCount; i++ )
        {
            EDA_ANGLE angle = aOrient + delta * i;
            corners.emplace_back( KiROUND( radius * std::cos( angle.AsRadians() ) ) + aShapePos.x,
                                  KiROUND( radius * std::sin( angle.AsRadians() ) ) + aShapePos.y );
        }

        PlotPoly( corners, FILL_T::FILLED_SHAPE, 0, aData );
    }

private:
    void destroyCairoObjects()
    {
        if( m_context )
        {
            cairo_destroy( m_context );
            m_context = nullptr;
        }

        if( m_surface )
        {
            cairo_surface_destroy( m_surface );
            m_surface = nullptr;
        }
    }

    VECTOR2D userToDeviceCoordinatesMm( const VECTOR2I& aCoordinate ) const
    {
        VECTOR2D pos( aCoordinate - m_plotOffset );
        pos.x *= m_plotScale * m_iuPerDeviceUnit;
        pos.y *= m_plotScale * m_iuPerDeviceUnit;

        if( m_plotMirror )
            pos.x = m_pageWidthMm - pos.x;

        return pos;
    }

    double userToDeviceSizeMm( double aSize ) const
    {
        return aSize * m_plotScale * m_iuPerDeviceUnit;
    }

private:
    cairo_surface_t* m_surface = nullptr;
    cairo_t*         m_context = nullptr;
    double           m_pageWidthMm = 0.0;
    double           m_pageHeightMm = 0.0;
    COLOR4D          m_currentColor = COLOR4D::BLACK;
    VECTOR2D         m_penLastDevicePos;
};

} // namespace


bool RenderGerberToSvg( const wxString& aInputPath, const wxString& aOutputPath,
                        const GERBER_SVG_RENDER_OPTIONS& aOptions, wxString* aErrorMsg,
                        wxArrayString* aMessages )
{
    auto image = LoadGerberOrExcellon( aInputPath, aErrorMsg, aMessages );

    if( !image )
        return false;

    if( image->GetItemsCount() == 0 )
    {
        if( aErrorMsg )
            *aErrorMsg = wxS( "Gerber file contains no draw items" );

        return false;
    }

    BOX2I bbox;

    if( aOptions.HasViewportOverride() )
    {
        bbox = CalculateGerberViewportBoundingBox( aOptions.originXMm, aOptions.originYMm,
                                                   aOptions.windowWidthMm, aOptions.windowHeightMm );
    }
    else
    {
        bbox = CalculateGerberBoundingBox( image.get() );
    }

    if( bbox.GetWidth() == 0 || bbox.GetHeight() == 0 )
    {
        if( aErrorMsg )
            *aErrorMsg = wxS( "Gerber file has zero-size bounding box" );

        return false;
    }

    double pageWidthMm = static_cast<double>( bbox.GetWidth() ) / gerbIUScale.IU_PER_MM;
    double pageHeightMm = static_cast<double>( bbox.GetHeight() ) / gerbIUScale.IU_PER_MM;

    GERBER_CAIRO_SVG_PLOTTER plotter( pageWidthMm, pageHeightMm );
    plotter.SetCreator( wxS( "KiCad" ) );
    plotter.SetColorMode( true );
    plotter.SetViewport( bbox.GetOrigin(), gerbIUScale.IU_PER_MILS / 10.0, 1.0, false );

    if( !plotter.OpenFile( aOutputPath ) )
    {
        if( aErrorMsg )
            *aErrorMsg = wxString::Format( wxS( "Failed to open SVG file: %s" ), aOutputPath );

        return false;
    }

    if( !plotter.StartPlot( wxEmptyString ) )
    {
        if( aErrorMsg )
            *aErrorMsg = wxS( "Failed to start SVG plotter" );

        return false;
    }

    if( aOptions.includeBackground && aOptions.backgroundColor.a > 0.0 )
    {
        plotter.SetColor( aOptions.backgroundColor );
        plotter.Rect( bbox.GetOrigin(), bbox.GetEnd(), FILL_T::FILLED_SHAPE, 0 );
    }

    RenderGerberImageToPlotter( image.get(), plotter, aOptions.foregroundColor, aOptions.backgroundColor );

    if( !plotter.EndPlot() )
    {
        if( aErrorMsg )
            *aErrorMsg = wxS( "Failed to finalize SVG plotter" );

        return false;
    }

    return true;
}


bool RenderGerberToSvg( const wxString& aInputPath, const wxString& aOutputPath,
                        const JOB_GERBER_EXPORT_SVG& aJob, wxString* aErrorMsg, wxArrayString* aMessages )
{
    GERBER_SVG_RENDER_OPTIONS options;
    options.includeBackground = aJob.m_includeBackground;

    if( !aJob.m_foregroundColor.IsEmpty() )
        options.foregroundColor = KIGFX::COLOR4D( aJob.m_foregroundColor );

    if( !aJob.m_backgroundColor.IsEmpty() )
        options.backgroundColor = KIGFX::COLOR4D( aJob.m_backgroundColor );

    double toMm = 1.0;

    switch( aJob.m_units )
    {
    case JOB_GERBER_EXPORT_SVG::UNITS::INCH: toMm = 25.4;   break;
    case JOB_GERBER_EXPORT_SVG::UNITS::MILS: toMm = 0.0254; break;
    case JOB_GERBER_EXPORT_SVG::UNITS::MM:   toMm = 1.0;    break;
    }

    options.originXMm = aJob.m_originX * toMm;
    options.originYMm = aJob.m_originY * toMm;
    options.windowWidthMm = aJob.m_windowWidth * toMm;
    options.windowHeightMm = aJob.m_windowHeight * toMm;

    return RenderGerberToSvg( aInputPath, aOutputPath, options, aErrorMsg, aMessages );
}
