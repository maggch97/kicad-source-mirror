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

#ifndef GERBER_RENDER_H
#define GERBER_RENDER_H

#include <functional>
#include <memory>
#include <wx/arrstr.h>
#include <wx/string.h>
#include <gal/color4d.h>
#include <math/box2.h>

class GERBER_DRAW_ITEM;
class GERBER_FILE_IMAGE;
class PLOTTER;


/**
 * Determine if a file is an Excellon drill file based on extension.
 */
bool IsExcellonFile( const wxString& aPath );


/**
 * Calculate bounding box for all draw items in a gerber image.
 */
BOX2I CalculateGerberBoundingBox( GERBER_FILE_IMAGE* aImage );


/**
 * Convert a Gerber-native viewport into KiCad's internal Gerber coordinate space.
 *
 * The input Y axis increases upwards.  Gerbview stores Gerber Y coordinates inverted, so the
 * resulting BOX2I origin is the top edge in internal coordinates.
 */
BOX2I CalculateGerberViewportBoundingBox( double aOriginXMm, double aOriginYMm, double aWindowWidthMm,
                                          double aWindowHeightMm );


/**
 * Load a Gerber or Excellon file, auto-detecting by extension.
 *
 * @return Loaded image, or nullptr on failure
 */
std::unique_ptr<GERBER_FILE_IMAGE> LoadGerberOrExcellon( const wxString& aPath, wxString* aErrorMsg,
                                                         wxArrayString* aMessages = nullptr );


/**
 * Render a single Gerber draw item to any KiCad plotter.
 *
 * This function intentionally mirrors GERBVIEW_PAINTER's polygon conversion path for the draw
 * item types used by the Gerber CLI exporters.
 */
void RenderGerberItemToPlotter( GERBER_DRAW_ITEM* aItem, PLOTTER& aPlotter, const KIGFX::COLOR4D& aColor );


/**
 * Render all Gerber items to any KiCad plotter, preserving item order and dark/clear polarity.
 *
 * If aSetClearCompositing is provided, it is used for clear-polarity items on transparent
 * backgrounds so raster backends can erase existing pixels instead of drawing a transparent color.
 * Vector backends can omit the callback and will draw clear-polarity items with the background
 * color.
 */
void RenderGerberImageToPlotter( GERBER_FILE_IMAGE* aImage, PLOTTER& aPlotter,
                                 const KIGFX::COLOR4D& aForegroundColor,
                                 const KIGFX::COLOR4D& aBackgroundColor,
                                 const std::function<void( bool )>& aSetClearCompositing = {} );


#endif // GERBER_RENDER_H
