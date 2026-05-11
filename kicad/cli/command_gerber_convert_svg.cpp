/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
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

#include "command_gerber_convert_svg.h"

#include <cli/exit_codes.h>
#include <jobs/job_gerber_export_svg.h>
#include <kiface_base.h>
#include <macros.h>
#include <string_utils.h>
#include <wx/crt.h>
#include <wx/filename.h>


#define ARG_STRICT "--strict"
#define ARG_UNITS "--units"
#define ARG_ORIGIN_X "--origin-x"
#define ARG_ORIGIN_Y "--origin-y"
#define ARG_WINDOW_WIDTH "--window-width"
#define ARG_WINDOW_HEIGHT "--window-height"
#define ARG_FOREGROUND "--foreground"
#define ARG_BACKGROUND "--background"
#define ARG_NO_BACKGROUND "--no-background"


CLI::GERBER_CONVERT_SVG_COMMAND::GERBER_CONVERT_SVG_COMMAND() :
        COMMAND( "svg" )
{
    addCommonArgs( true, true, IO_TYPE::FILE, IO_TYPE::FILE );

    m_argParser.add_description( UTF8STDSTR( _( "Convert a Gerber or Excellon file to SVG" ) ) );

    m_argParser.add_argument( ARG_STRICT ).help( UTF8STDSTR( _( "Fail on any parse warnings or errors" ) ) ).flag();

    m_argParser.add_argument( ARG_UNITS )
            .default_value( std::string( "mm" ) )
            .help( UTF8STDSTR( _( "Units for viewport parameters, options: mm, inch, mils (default: mm)" ) ) )
            .metavar( "UNITS" );

    m_argParser.add_argument( ARG_ORIGIN_X )
            .default_value( 0.0 )
            .scan<'g', double>()
            .help( UTF8STDSTR( _( "Viewport origin X" ) ) )
            .metavar( "VALUE" );

    m_argParser.add_argument( ARG_ORIGIN_Y )
            .default_value( 0.0 )
            .scan<'g', double>()
            .help( UTF8STDSTR( _( "Viewport origin Y" ) ) )
            .metavar( "VALUE" );

    m_argParser.add_argument( ARG_WINDOW_WIDTH )
            .default_value( 0.0 )
            .scan<'g', double>()
            .help( UTF8STDSTR( _( "Viewport width (enables viewport mode)" ) ) )
            .metavar( "VALUE" );

    m_argParser.add_argument( ARG_WINDOW_HEIGHT )
            .default_value( 0.0 )
            .scan<'g', double>()
            .help( UTF8STDSTR( _( "Viewport height (enables viewport mode)" ) ) )
            .metavar( "VALUE" );

    m_argParser.add_argument( ARG_FOREGROUND )
            .default_value( std::string() )
            .help( UTF8STDSTR( _( "Foreground color as hex (e.g., '#000000')" ) ) )
            .metavar( "COLOR" );

    m_argParser.add_argument( ARG_BACKGROUND )
            .default_value( std::string() )
            .help( UTF8STDSTR( _( "Background color as hex (e.g., '#FFFFFF')" ) ) )
            .metavar( "COLOR" );

    m_argParser.add_argument( ARG_NO_BACKGROUND )
            .help( UTF8STDSTR( _( "Do not write an SVG background rectangle" ) ) )
            .flag();
}


int CLI::GERBER_CONVERT_SVG_COMMAND::doPerform( KIWAY& aKiway )
{
    std::unique_ptr<JOB_GERBER_EXPORT_SVG> svgJob = std::make_unique<JOB_GERBER_EXPORT_SVG>();

    svgJob->m_inputFile = m_argInput;
    svgJob->m_strict = m_argParser.get<bool>( ARG_STRICT );
    svgJob->m_includeBackground = !m_argParser.get<bool>( ARG_NO_BACKGROUND );

    wxString units = From_UTF8( m_argParser.get<std::string>( ARG_UNITS ).c_str() );

    if( units == wxS( "mm" ) )
    {
        svgJob->m_units = JOB_GERBER_EXPORT_SVG::UNITS::MM;
    }
    else if( units == wxS( "inch" ) )
    {
        svgJob->m_units = JOB_GERBER_EXPORT_SVG::UNITS::INCH;
    }
    else if( units == wxS( "mils" ) )
    {
        svgJob->m_units = JOB_GERBER_EXPORT_SVG::UNITS::MILS;
    }
    else
    {
        wxFprintf( stderr, _( "Invalid units: %s\n" ), units );
        return CLI::EXIT_CODES::ERR_ARGS;
    }

    svgJob->m_originX = m_argParser.get<double>( ARG_ORIGIN_X );
    svgJob->m_originY = m_argParser.get<double>( ARG_ORIGIN_Y );
    svgJob->m_windowWidth = m_argParser.get<double>( ARG_WINDOW_WIDTH );
    svgJob->m_windowHeight = m_argParser.get<double>( ARG_WINDOW_HEIGHT );

    if( ( svgJob->m_windowWidth > 0.0 ) != ( svgJob->m_windowHeight > 0.0 ) )
    {
        wxFprintf( stderr, _( "Error: both --window-width and --window-height must be specified together\n" ) );
        return CLI::EXIT_CODES::ERR_ARGS;
    }

    svgJob->m_foregroundColor = wxString::FromUTF8( m_argParser.get<std::string>( ARG_FOREGROUND ) );
    svgJob->m_backgroundColor = wxString::FromUTF8( m_argParser.get<std::string>( ARG_BACKGROUND ) );

    if( m_argOutput.IsEmpty() )
    {
        wxFileName inputFn( m_argInput );
        svgJob->SetConfiguredOutputPath( inputFn.GetPath() + wxFileName::GetPathSeparator() + inputFn.GetName()
                                         + wxS( ".svg" ) );
    }
    else
    {
        svgJob->SetConfiguredOutputPath( m_argOutput );
    }

    return aKiway.ProcessJob( KIWAY::FACE_GERBVIEW, svgJob.get() );
}
