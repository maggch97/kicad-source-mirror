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
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "gerber_to_png.h"
#include "gerber_file_image.h"
#include "gerber_render.h"

#include <jobs/job_gerber_export_png.h>
#include <plotters/plotter_png.h>
#include <base_units.h>
#include <cmath>


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

    // When using a viewport override with DPI, calculate pixel dimensions from the window
    int reqWidth = aOptions.width;
    int reqHeight = aOptions.height;

    if( aOptions.HasViewportOverride() && reqWidth == 0 && reqHeight == 0 )
    {
        double mmPerInch = 25.4;
        reqWidth = static_cast<int>( std::ceil( aOptions.windowWidthMm / mmPerInch * aOptions.GetDpiX() ) );
        reqHeight = static_cast<int>( std::ceil( aOptions.windowHeightMm / mmPerInch * aOptions.GetDpiY() ) );
    }

    GERBER_PLOTTER_VIEWPORT vp = CalculatePlotterViewport( bbox, aOptions.GetDpiX(), aOptions.GetDpiY(),
                                                           reqWidth, reqHeight );

    PNG_PLOTTER plotter;
    plotter.SetPixelSize( vp.width, vp.height );
    plotter.SetResolution( aOptions.GetDpiX(), aOptions.GetDpiY() );
    plotter.SetAntialias( aOptions.antialias );
    plotter.SetBackgroundColor( aOptions.backgroundColor );
    plotter.SetViewport( vp.offset, vp.iuPerDecimil, vp.plotScaleX, vp.plotScaleY, false );

    // Start plotting
    if( !plotter.StartPlot( wxEmptyString ) )
    {
        if( aErrorMsg )
            *aErrorMsg = wxS( "Failed to start PNG plotter" );

        return false;
    }

    RenderGerberImageToPlotter( image.get(), plotter, aOptions.foregroundColor, aOptions.backgroundColor,
                                [&]( bool aClear )
                                {
                                    plotter.SetClearCompositing( aClear );
                                } );

    plotter.EndPlot();

    // Save the file
    if( !plotter.SaveFile( aOutputPath ) )
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
