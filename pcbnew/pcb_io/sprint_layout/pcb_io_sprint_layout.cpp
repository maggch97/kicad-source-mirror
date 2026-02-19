/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include "pcb_io_sprint_layout.h"
#include "sprint_layout_parser.h"

#include <board.h>
#include <footprint.h>
#include <zone.h>
#include <font/fontconfig.h>
#include <reporter.h>

#include <wx/filename.h>
#include <wx/wfstream.h>


PCB_IO_SPRINT_LAYOUT::PCB_IO_SPRINT_LAYOUT() :
        PCB_IO( wxS( "Sprint Layout" ) )
{
}


PCB_IO_SPRINT_LAYOUT::~PCB_IO_SPRINT_LAYOUT() = default;


bool PCB_IO_SPRINT_LAYOUT::CanReadBoard( const wxString& aFileName ) const
{
    const wxFileName fn( aFileName );

    if( !fn.FileExists() )
        return false;

    wxString ext = fn.GetExt().Lower();

    if( ext != wxS( "lay6" ) && ext != wxS( "lay" ) )
        return false;

    // Check magic bytes: version (<=6), 0x33, 0xAA, 0xFF
    wxFFileInputStream stream( aFileName );

    if( !stream.IsOk() || stream.GetLength() < 8 )
        return false;

    uint8_t header[4];
    stream.Read( header, 4 );

    if( stream.LastRead() != 4 )
        return false;

    if( header[0] > 6 || header[1] != 0x33 || header[2] != 0xAA || header[3] != 0xFF )
        return false;

    return true;
}


BOARD* PCB_IO_SPRINT_LAYOUT::LoadBoard( const wxString& aFileName, BOARD* aAppendToMe,
                                        const std::map<std::string, UTF8>* aProperties,
                                        PROJECT* aProject )
{
    FONTCONFIG_REPORTER_SCOPE fontconfigScope( &LOAD_INFO_REPORTER::GetInstance() );

    m_props = aProperties;
    m_loadedFootprints.clear();

    SPRINT_LAYOUT_PARSER parser;

    if( !parser.Parse( aFileName ) )
    {
        THROW_IO_ERROR( wxString::Format( _( "Failed to parse Sprint Layout file '%s'" ),
                                          aFileName ) );
    }

    const auto& fileData = parser.GetFileData();
    size_t      boardIndex = 0;

    if( m_props && m_props->contains( "pcb_id" ) )
    {
        unsigned long idx = std::stoul( m_props->at( "pcb_id" ) );
        boardIndex = static_cast<size_t>( idx );
    }
    else if( fileData.boards.size() > 1 && m_choose_project_handler )
    {
        std::vector<IMPORT_PROJECT_DESC> options;

        for( size_t i = 0; i < fileData.boards.size(); i++ )
        {
            IMPORT_PROJECT_DESC desc;
            wxString            name = wxString::FromUTF8( fileData.boards[i].name );

            if( name.empty() )
                name = wxString::Format( wxS( "Board %zu" ), i + 1 );

            desc.PCBName = name;
            desc.PCBId = wxString::Format( wxS( "%zu" ), i );
            options.push_back( desc );
        }

        std::vector<IMPORT_PROJECT_DESC> chosen = m_choose_project_handler( options );

        if( chosen.empty() )
            return nullptr;

        unsigned long idx = std::stoul( chosen[0].PCBId.ToStdString() );
        boardIndex = static_cast<size_t>( idx );
    }

    std::unique_ptr<BOARD> newBoard( parser.CreateBoard( m_loadedFootprints, boardIndex ) );

    if( !newBoard )
    {
        THROW_IO_ERROR( wxString::Format( _( "Failed to create board from Sprint Layout file '%s'" ),
                                          aFileName ) );
    }

    if( aAppendToMe )
    {
        for( FOOTPRINT* fp : newBoard->Footprints() )
            aAppendToMe->Add( static_cast<FOOTPRINT*>( fp->Clone() ) );

        for( BOARD_ITEM* item : newBoard->Drawings() )
            aAppendToMe->Add( static_cast<BOARD_ITEM*>( item->Clone() ) );

        for( ZONE* zone : newBoard->Zones() )
            aAppendToMe->Add( static_cast<ZONE*>( zone->Clone() ) );

        return aAppendToMe;
    }

    newBoard->SetFileName( aFileName );
    return newBoard.release();
}


std::vector<FOOTPRINT*> PCB_IO_SPRINT_LAYOUT::GetImportedCachedLibraryFootprints()
{
    std::vector<FOOTPRINT*> result;

    for( auto& [name, footprint] : m_loadedFootprints )
        result.push_back( static_cast<FOOTPRINT*>( footprint->Clone() ) );

    return result;
}
