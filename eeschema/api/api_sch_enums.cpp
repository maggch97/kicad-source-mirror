/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 * @author Jon Evans <jon@craftyjon.com>
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

#include <api/api_enums.h>
#include <api/schematic/schematic_types.pb.h>
#include <wx/wx.h>

#include <core/typeinfo.h>
#include <layer_ids.h>
#include <sch_label.h>
#include <sch_sheet_pin.h>

using namespace kiapi::schematic;


template<>
types::SchematicLabelShape ToProtoEnum( LABEL_FLAG_SHAPE aValue )
{
    switch( aValue )
    {
    case LABEL_FLAG_SHAPE::L_INPUT:        return types::SchematicLabelShape::SLSH_INPUT;
    case LABEL_FLAG_SHAPE::L_OUTPUT:       return types::SchematicLabelShape::SLSH_OUTPUT;
    case LABEL_FLAG_SHAPE::L_BIDI:         return types::SchematicLabelShape::SLSH_BIDI;
    case LABEL_FLAG_SHAPE::L_TRISTATE:     return types::SchematicLabelShape::SLSH_TRISTATE;
    case LABEL_FLAG_SHAPE::L_UNSPECIFIED:  return types::SchematicLabelShape::SLSH_PASSIVE;
    case LABEL_FLAG_SHAPE::F_DOT:          return types::SchematicLabelShape::SLSH_DOT;
    case LABEL_FLAG_SHAPE::F_ROUND:        return types::SchematicLabelShape::SLSH_CIRCLE;
    case LABEL_FLAG_SHAPE::F_DIAMOND:      return types::SchematicLabelShape::SLSH_DIAMOND;
    case LABEL_FLAG_SHAPE::F_RECTANGLE:    return types::SchematicLabelShape::SLSH_RECTANGLE;

    default:
        wxCHECK_MSG( false, types::SchematicLabelShape::SLSH_UNKNOWN,
                     "Unhandled case in ToProtoEnum<LABEL_FLAG_SHAPE>" );
    }
}


template<>
LABEL_FLAG_SHAPE FromProtoEnum( types::SchematicLabelShape aValue )
{
    switch( aValue )
    {
    case types::SchematicLabelShape::SLSH_UNKNOWN:
    case types::SchematicLabelShape::SLSH_PASSIVE:    return LABEL_FLAG_SHAPE::L_UNSPECIFIED;
    case types::SchematicLabelShape::SLSH_INPUT:      return LABEL_FLAG_SHAPE::L_INPUT;
    case types::SchematicLabelShape::SLSH_OUTPUT:     return LABEL_FLAG_SHAPE::L_OUTPUT;
    case types::SchematicLabelShape::SLSH_BIDI:       return LABEL_FLAG_SHAPE::L_BIDI;
    case types::SchematicLabelShape::SLSH_TRISTATE:   return LABEL_FLAG_SHAPE::L_TRISTATE;
    case types::SchematicLabelShape::SLSH_DOT:        return LABEL_FLAG_SHAPE::F_DOT;
    case types::SchematicLabelShape::SLSH_CIRCLE:     return LABEL_FLAG_SHAPE::F_ROUND;
    case types::SchematicLabelShape::SLSH_DIAMOND:    return LABEL_FLAG_SHAPE::F_DIAMOND;
    case types::SchematicLabelShape::SLSH_RECTANGLE:  return LABEL_FLAG_SHAPE::F_RECTANGLE;

    default:
        wxCHECK_MSG( false, LABEL_FLAG_SHAPE::L_UNSPECIFIED,
                     "Unhandled case in FromProtoEnum<types::SchematicLabelShape>" );
    }
}


template<>
types::SchematicLabelSpinStyle ToProtoEnum( SPIN_STYLE::SPIN aValue )
{
    switch( aValue )
    {
    case SPIN_STYLE::SPIN::LEFT:    return types::SchematicLabelSpinStyle::SLSS_LEFT;
    case SPIN_STYLE::SPIN::UP:      return types::SchematicLabelSpinStyle::SLSS_UP;
    case SPIN_STYLE::SPIN::RIGHT:   return types::SchematicLabelSpinStyle::SLSS_RIGHT;
    case SPIN_STYLE::SPIN::BOTTOM:  return types::SchematicLabelSpinStyle::SLSS_BOTTOM;

    default:
        wxCHECK_MSG( false, types::SchematicLabelSpinStyle::SLSS_UNKNOWN,
                     "Unhandled case in ToProtoEnum<SPIN_STYLE::SPIN>" );
    }
}


template<>
SPIN_STYLE::SPIN FromProtoEnum( types::SchematicLabelSpinStyle aValue )
{
    switch( aValue )
    {
    case types::SchematicLabelSpinStyle::SLSS_UNKNOWN:
    case types::SchematicLabelSpinStyle::SLSS_LEFT:    return SPIN_STYLE::SPIN::LEFT;
    case types::SchematicLabelSpinStyle::SLSS_UP:      return SPIN_STYLE::SPIN::UP;
    case types::SchematicLabelSpinStyle::SLSS_RIGHT:   return SPIN_STYLE::SPIN::RIGHT;
    case types::SchematicLabelSpinStyle::SLSS_BOTTOM:  return SPIN_STYLE::SPIN::BOTTOM;

    default:
        wxCHECK_MSG( false, SPIN_STYLE::SPIN::LEFT,
                     "Unhandled case in FromProtoEnum<types::SchematicLabelSpinStyle>" );
    }
}


template<>
types::SheetSide ToProtoEnum( SHEET_SIDE aValue )
{
    switch( aValue )
    {
    case SHEET_SIDE::LEFT:       return types::SheetSide::SHS_LEFT;
    case SHEET_SIDE::RIGHT:      return types::SheetSide::SHS_RIGHT;
    case SHEET_SIDE::TOP:        return types::SheetSide::SHS_TOP;
    case SHEET_SIDE::BOTTOM:     return types::SheetSide::SHS_BOTTOM;

    default:
        wxCHECK_MSG( false, types::SheetSide::SHS_UNKNOWN,
                     "Unhandled case in ToProtoEnum<SHEET_SIDE>" );
    }
}


template<>
SHEET_SIDE FromProtoEnum( types::SheetSide aValue )
{
    switch( aValue )
    {
    case types::SheetSide::SHS_UNKNOWN:  return SHEET_SIDE::UNDEFINED;
    case types::SheetSide::SHS_LEFT:     return SHEET_SIDE::LEFT;
    case types::SheetSide::SHS_RIGHT:    return SHEET_SIDE::RIGHT;
    case types::SheetSide::SHS_TOP:      return SHEET_SIDE::TOP;
    case types::SheetSide::SHS_BOTTOM:   return SHEET_SIDE::BOTTOM;

    default:
        wxCHECK_MSG( false, SHEET_SIDE::UNDEFINED,
                     "Unhandled case in FromProtoEnum<types::SheetSide>" );
    }
}
