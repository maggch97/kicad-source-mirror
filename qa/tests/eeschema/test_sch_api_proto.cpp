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

#include <boost/test/unit_test.hpp>
#include <import_export.h>
#include <qa_utils/api_test_utils.h>
#include <qa_utils/wx_utils/unit_test_utils.h>

#include <api/schematic/schematic_types.pb.h>

#include <eeschema_test_utils.h>

#include <sch_bitmap.h>
#include <sch_bus_entry.h>
#include <sch_group.h>
#include <sch_junction.h>
#include <sch_label.h>
#include <sch_line.h>
#include <sch_no_connect.h>
#include <sch_shape.h>
#include <sch_sheet.h>
#include <wx/filename.h>


BOOST_FIXTURE_TEST_SUITE( ApiSchProto, KI_TEST::SCHEMATIC_TEST_FIXTURE )

BOOST_AUTO_TEST_CASE( KitchenSink )
{
    wxFileName fn( KI_TEST::GetEeschemaTestDataDir(), wxS( "api_kitchen_sink.kicad_sch" ) );
    LoadSchematic( fn );

    for( SCH_ITEM* item : m_schematic->RootScreen()->Items() )
    {
        switch( item->Type() )
        {
        case SCH_JUNCTION_T:
            testProtoFromKiCadObject<kiapi::schematic::types::Junction>(
                    static_cast<SCH_JUNCTION*>( item ),
                    []()
                    {
                        return std::make_unique<SCH_JUNCTION>();
                    } );
            break;

        case SCH_NO_CONNECT_T:
            testProtoFromKiCadObject<kiapi::schematic::types::NoConnectMarker>(
                    static_cast<SCH_NO_CONNECT*>( item ),
                    []()
                    {
                        return std::make_unique<SCH_NO_CONNECT>();
                    } );
            break;

        case SCH_BUS_WIRE_ENTRY_T:
            testProtoFromKiCadObject<kiapi::schematic::types::BusEntry>(
                    static_cast<SCH_BUS_WIRE_ENTRY*>( item ),
                    []()
                    {
                        return std::make_unique<SCH_BUS_WIRE_ENTRY>();
                    } );
            break;

        case SCH_BUS_BUS_ENTRY_T:
            testProtoFromKiCadObject<kiapi::schematic::types::BusEntry>(
                    static_cast<SCH_BUS_BUS_ENTRY*>( item ),
                    []()
                    {
                        return std::make_unique<SCH_BUS_BUS_ENTRY>();
                    } );
            break;

        case SCH_LINE_T:
            testProtoFromKiCadObject<kiapi::schematic::types::SchematicLine>(
                    static_cast<SCH_LINE*>( item ),
                    []()
                    {
                        return std::make_unique<SCH_LINE>();
                    } );
            break;

        case SCH_SHAPE_T:
            testProtoFromKiCadObject<kiapi::schematic::types::SchematicGraphicShape>(
                    static_cast<SCH_SHAPE*>( item ),
                    []()
                    {
                        return std::make_unique<SCH_SHAPE>();
                    } );
            break;

        case SCH_BITMAP_T:
            testProtoFromKiCadObject<kiapi::schematic::types::SchematicImage>(
                    static_cast<SCH_BITMAP*>( item ),
                    []()
                    {
                        return std::make_unique<SCH_BITMAP>();
                    } );
            break;

        case SCH_LABEL_T:
            testProtoFromKiCadObject<kiapi::schematic::types::LocalLabel>(
                    static_cast<SCH_LABEL*>( item ),
                    []()
                    {
                        return std::make_unique<SCH_LABEL>();
                    } );
            break;

        case SCH_GLOBAL_LABEL_T:
            testProtoFromKiCadObject<kiapi::schematic::types::GlobalLabel>(
                    static_cast<SCH_GLOBALLABEL*>( item ),
                    []()
                    {
                        return std::make_unique<SCH_GLOBALLABEL>();
                    } );
            break;

        case SCH_HIER_LABEL_T:
            testProtoFromKiCadObject<kiapi::schematic::types::HierarchicalLabel>(
                    static_cast<SCH_HIERLABEL*>( item ),
                    []()
                    {
                        return std::make_unique<SCH_HIERLABEL>();
                    } );
            break;

        case SCH_GROUP_T:
            testProtoFromKiCadObject<kiapi::schematic::types::Group>(
                    static_cast<SCH_GROUP*>( item ),
                    [this]()
                    {
                        return std::make_unique<SCH_GROUP>( m_schematic->RootScreen() );
                    } );
            break;

        case SCH_SHEET_T:
            testProtoFromKiCadObject<kiapi::schematic::types::SheetSymbol>(
                    static_cast<SCH_SHEET*>( item ),
                    [this]()
                    {
                        return std::make_unique<SCH_SHEET>( m_schematic->RootScreen() );
                    } );
            break;

        default:
            break;
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
