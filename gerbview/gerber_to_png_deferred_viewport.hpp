#pragma once

#include <base_units.h>
#include <math/box2.h>
#include <math/vector2d.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <wx/string.h>


namespace GERBER_TO_PNG_DEFERRED_VIEWPORT
{

struct RENDER_POLYGON
{
    bool                  clearPolarity = false;
    std::vector<VECTOR2I> points;
};


inline BOX2I MakeBBoxFromGerberViewport( double aOriginXMm, double aOriginYMm, double aWindowWidthMm,
                                          double aWindowHeightMm )
{
    // Viewport is specified in Gerber-native coordinates (Y increases upward).
    // KiCad stores gerber items with Y negated (Y increases downward), so
    // negate the origin Y and flip the window vertically.
    double iuPerMm = gerbIUScale.IU_PER_MM;
    int    ox = static_cast<int>( std::round( aOriginXMm * iuPerMm ) );
    int    oy = static_cast<int>( std::round( -( aOriginYMm + aWindowHeightMm ) * iuPerMm ) );
    int    w = static_cast<int>( std::round( aWindowWidthMm * iuPerMm ) );
    int    h = static_cast<int>( std::round( aWindowHeightMm * iuPerMm ) );

    return BOX2I( VECTOR2I( ox, oy ), VECTOR2I( w, h ) );
}


inline void MergePolygonBBox( const std::vector<VECTOR2I>& aPts, BOX2I& aBBox, bool& aBBoxValid )
{
    if( aPts.empty() )
        return;

    int minX = std::numeric_limits<int>::max();
    int minY = std::numeric_limits<int>::max();
    int maxX = std::numeric_limits<int>::min();
    int maxY = std::numeric_limits<int>::min();

    for( const VECTOR2I& pt : aPts )
    {
        minX = std::min( minX, pt.x );
        minY = std::min( minY, pt.y );
        maxX = std::max( maxX, pt.x );
        maxY = std::max( maxY, pt.y );
    }

    BOX2I polyBox( VECTOR2I( minX, minY ), VECTOR2I( maxX - minX, maxY - minY ) );

    if( !aBBoxValid )
    {
        aBBox = polyBox;
        aBBoxValid = true;
    }
    else
    {
        aBBox.Merge( polyBox );
    }
}


inline bool WriteDeferredViewportBBox( const BOX2I& aBBox, wxString* aErrorMsg )
{
    double iuToInches = 1.0 / gerbIUScale.IU_PER_MM / 25.4;

    double minX = static_cast<double>( aBBox.GetOrigin().x ) * iuToInches;
    double maxX = static_cast<double>( aBBox.GetOrigin().x + aBBox.GetWidth() ) * iuToInches;
    double maxY = -static_cast<double>( aBBox.GetOrigin().y ) * iuToInches;
    double minY = -static_cast<double>( aBBox.GetOrigin().y + aBBox.GetHeight() ) * iuToInches;

    nlohmann::json bbox = {
        { "event", "bbox" },
        { "coordinate_system", "gerber" },
        { "units", "in" },
        { "origin_x", minX },
        { "origin_y", minY },
        { "width", maxX - minX },
        { "height", maxY - minY }
    };

    std::cout << bbox.dump() << std::endl;

    if( !std::cout )
    {
        if( aErrorMsg )
            *aErrorMsg = wxS( "Failed to write deferred viewport bounding box" );

        return false;
    }

    return true;
}


inline bool ReadDeferredViewport( BOX2I& aBBox, wxString* aErrorMsg )
{
    std::string line;

    if( !std::getline( std::cin, line ) )
    {
        if( aErrorMsg )
            *aErrorMsg = wxS( "Failed to read deferred viewport JSON from stdin" );

        return false;
    }

    try
    {
        nlohmann::json viewport = nlohmann::json::parse( line );

        double originX = viewport.at( "origin_x" ).get<double>();
        double originY = viewport.at( "origin_y" ).get<double>();
        double width = viewport.contains( "window_width" ) ? viewport.at( "window_width" ).get<double>()
                                                           : viewport.at( "width" ).get<double>();
        double height = viewport.contains( "window_height" ) ? viewport.at( "window_height" ).get<double>()
                                                             : viewport.at( "height" ).get<double>();

        if( width <= 0.0 || height <= 0.0 )
        {
            if( aErrorMsg )
                *aErrorMsg = wxS( "Deferred viewport window dimensions must be greater than zero" );

            return false;
        }

        constexpr double inchToMm = 25.4;
        aBBox = MakeBBoxFromGerberViewport( originX * inchToMm, originY * inchToMm,
                                            width * inchToMm, height * inchToMm );
    }
    catch( const std::exception& e )
    {
        if( aErrorMsg )
            *aErrorMsg = wxString::Format( wxS( "Invalid deferred viewport JSON: %s" ),
                                           wxString::FromUTF8( e.what() ) );

        return false;
    }

    return true;
}

} // namespace GERBER_TO_PNG_DEFERRED_VIEWPORT
