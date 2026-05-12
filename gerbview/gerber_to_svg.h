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

#ifndef GERBER_TO_SVG_H
#define GERBER_TO_SVG_H

#include <wx/arrstr.h>
#include <wx/string.h>
#include <gal/color4d.h>

class JOB_GERBER_EXPORT_SVG;


/**
 * Render options for Gerber to SVG conversion.
 */
struct GERBER_SVG_RENDER_OPTIONS
{
    KIGFX::COLOR4D foregroundColor = KIGFX::COLOR4D::BLACK;
    KIGFX::COLOR4D backgroundColor = KIGFX::COLOR4D::WHITE;
    bool           includeBackground = true;

    double originXMm = 0.0;        ///< Viewport origin X in mm
    double originYMm = 0.0;        ///< Viewport origin Y in mm
    double windowWidthMm = 0.0;    ///< Viewport width in mm (> 0 enables viewport mode)
    double windowHeightMm = 0.0;   ///< Viewport height in mm (> 0 enables viewport mode)

    bool HasViewportOverride() const
    {
        return windowWidthMm > 0.0 && windowHeightMm > 0.0;
    }
};


/**
 * Render a Gerber or Excellon file to SVG.
 *
 * Loads the file, converts all draw items to polygons, and renders using Cairo's SVG surface.
 */
bool RenderGerberToSvg( const wxString& aInputPath, const wxString& aOutputPath,
                        const GERBER_SVG_RENDER_OPTIONS& aOptions, wxString* aErrorMsg = nullptr,
                        wxArrayString* aMessages = nullptr );


/**
 * Render a Gerber or Excellon file to SVG using job parameters.
 */
bool RenderGerberToSvg( const wxString& aInputPath, const wxString& aOutputPath,
                        const JOB_GERBER_EXPORT_SVG& aJob, wxString* aErrorMsg = nullptr,
                        wxArrayString* aMessages = nullptr );


#endif // GERBER_TO_SVG_H
