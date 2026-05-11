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

#include <jobs/job_registry.h>
#include <jobs/job_gerber_export_svg.h>
#include <i18n_utility.h>


NLOHMANN_JSON_SERIALIZE_ENUM( JOB_GERBER_EXPORT_SVG::UNITS,
                              { { JOB_GERBER_EXPORT_SVG::UNITS::MM, "mm" },
                                { JOB_GERBER_EXPORT_SVG::UNITS::INCH, "in" },
                                { JOB_GERBER_EXPORT_SVG::UNITS::MILS, "mils" } } )


JOB_GERBER_EXPORT_SVG::JOB_GERBER_EXPORT_SVG() :
    JOB( "gerber_export_svg", false )
{
    m_params.emplace_back( new JOB_PARAM<bool>( "strict", &m_strict, m_strict ) );
    m_params.emplace_back(
            new JOB_PARAM<bool>( "include_background", &m_includeBackground, m_includeBackground ) );
    m_params.emplace_back( new JOB_PARAM<UNITS>( "units", &m_units, m_units ) );
    m_params.emplace_back( new JOB_PARAM<double>( "origin_x", &m_originX, m_originX ) );
    m_params.emplace_back( new JOB_PARAM<double>( "origin_y", &m_originY, m_originY ) );
    m_params.emplace_back( new JOB_PARAM<double>( "window_width", &m_windowWidth, m_windowWidth ) );
    m_params.emplace_back( new JOB_PARAM<double>( "window_height", &m_windowHeight, m_windowHeight ) );
    m_params.emplace_back( new JOB_PARAM<wxString>( "foreground_color", &m_foregroundColor, m_foregroundColor ) );
    m_params.emplace_back( new JOB_PARAM<wxString>( "background_color", &m_backgroundColor, m_backgroundColor ) );
}


wxString JOB_GERBER_EXPORT_SVG::GetDefaultDescription() const
{
    return _( "Gerber Export SVG" );
}


wxString JOB_GERBER_EXPORT_SVG::GetSettingsDialogTitle() const
{
    return _( "Gerber Export SVG Job Settings" );
}


REGISTER_JOB( gerber_export_svg, _HKI( "Gerber: Export SVG" ), KIWAY::FACE_GERBVIEW, JOB_GERBER_EXPORT_SVG );
