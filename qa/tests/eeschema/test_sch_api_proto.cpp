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

#include <sch_label.h>
#include <sch_line.h>
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
        case SCH_LINE_T:
            testProtoFromKiCadObject<kiapi::schematic::types::SchematicLine>(
                    static_cast<SCH_LINE*>( item ),
                    []()
                    {
                        return std::make_unique<SCH_LINE>();
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

        default:
            break;
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
