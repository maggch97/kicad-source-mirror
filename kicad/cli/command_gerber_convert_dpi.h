#ifndef COMMAND_GERBER_CONVERT_DPI_H
#define COMMAND_GERBER_CONVERT_DPI_H

#include <limits>
#include <string>

#include <wx/string.h>

namespace CLI
{

struct GERBER_DPI_SPEC
{
    int x = 300;
    int y = 300;
};


inline bool ParseGerberDpiComponent( const wxString& aValue, int& aOut )
{
    long parsed = 0;

    if( !aValue.ToLong( &parsed ) )
        return false;

    if( parsed <= 0 || parsed > std::numeric_limits<int>::max() )
        return false;

    aOut = static_cast<int>( parsed );
    return true;
}


inline bool ParseGerberDpiSpec( const std::string& aValue, GERBER_DPI_SPEC& aOut, wxString* aError )
{
    wxString spec = wxString::FromUTF8( aValue );
    spec.Trim( true );
    spec.Trim( false );

    if( spec.IsEmpty() )
    {
        if( aError )
            *aError = _( "Invalid DPI: empty value" );

        return false;
    }

    int separator = spec.Find( 'x' );

    if( separator == wxNOT_FOUND )
        separator = spec.Find( 'X' );

    if( separator == wxNOT_FOUND )
    {
        if( !ParseGerberDpiComponent( spec, aOut.x ) )
        {
            if( aError )
                *aError = wxString::Format( _( "Invalid DPI: %s (use N or XxY, e.g. 300 or 720x360)" ),
                                            spec );

            return false;
        }

        aOut.y = aOut.x;
        return true;
    }

    wxString xSpec = spec.Left( separator );
    wxString ySpec = spec.Mid( separator + 1 );

    if( !ParseGerberDpiComponent( xSpec, aOut.x ) || !ParseGerberDpiComponent( ySpec, aOut.y ) )
    {
        if( aError )
            *aError = wxString::Format( _( "Invalid DPI: %s (use N or XxY, e.g. 300 or 720x360)" ),
                                        spec );

        return false;
    }

    return true;
}

} // namespace CLI

#endif
