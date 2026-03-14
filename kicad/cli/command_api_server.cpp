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

#include <csignal>

#include <api/api_handler_common.h>
#include <api/api_server.h>
#include <cli/exit_codes.h>
#include <settings/settings_manager.h>
#include <wildcards_and_files_ext.h>
#include <wx/app.h>
#include <wx/crt.h>
#include <wx/filename.h>

#include "command_api_server.h"

#define ARG_PATH "path"
#define ARG_SOCKET "--socket"


void apiServerSignalHandler( int )
{
    if( wxTheApp )
        wxTheApp->ExitMainLoop();
}


CLI::API_SERVER_COMMAND::API_SERVER_COMMAND() :
        COMMAND( "api-server" )
{
    m_argParser.add_description( UTF8STDSTR( _( "Run the KiCad IPC API server in headless mode" ) ) );

    m_argParser.add_argument( ARG_PATH )
            .default_value( std::string() )
            .nargs( argparse::nargs_pattern::optional )
            .help( UTF8STDSTR( _( "Optional path to a .kicad_pro or .kicad_pcb file to pre-load" ) ) )
            .metavar( "PROJECT_OR_BOARD" );

    m_argParser.add_argument( ARG_SOCKET )
            .default_value( std::string() )
            .help( UTF8STDSTR( _( "Override API socket path" ) ) )
            .metavar( "SOCKET_PATH" );
}


int CLI::API_SERVER_COMMAND::doPerform( KIWAY& aKiway )
{
    std::unique_ptr<KICAD_API_SERVER> server = std::make_unique<KICAD_API_SERVER>( false );
    API_HANDLER_COMMON                commonHandler;

    wxString socketPath = wxString::FromUTF8( m_argParser.get<std::string>( ARG_SOCKET ) );

    if( !socketPath.IsEmpty() )
        server->SetSocketPath( socketPath );

    kiapi::common::types::DocumentType openDocumentType = kiapi::common::types::DOCTYPE_UNKNOWN;
    wxFileName                         openProjectPath;

    auto closeCurrentDocument = [&]()
    {
        if( openDocumentType != kiapi::common::types::DOCTYPE_UNKNOWN )
        {
            wxString boardFileName;

            if( openDocumentType == kiapi::common::types::DOCTYPE_PCB )
            {
                wxFileName boardPath( openProjectPath );
                boardPath.SetExt( FILEEXT::KiCadPcbFileExtension );
                boardFileName = boardPath.GetFullName();
            }

            wxString error;
            aKiway.ProcessApiCloseDocument( KIWAY::FACE_PCB, boardFileName, server.get(), &error );
        }

        openProjectPath.Clear();
        openDocumentType = kiapi::common::types::DOCTYPE_UNKNOWN;
    };

    auto openDocument = [&]( const kiapi::common::commands::OpenDocument& aRequest )
            -> HANDLER_RESULT<kiapi::common::commands::OpenDocumentResponse>
    {
        using namespace kiapi::common;

        if( aRequest.type() != types::DOCTYPE_PCB && aRequest.type() != types::DOCTYPE_PROJECT )
        {
            ApiResponseStatus e;
            e.set_status( ApiStatusCode::AS_UNIMPLEMENTED );
            e.set_error_message( "Only PCB and project document types are supported" );
            return tl::unexpected( e );
        }

        wxString inputPath = wxString::FromUTF8( aRequest.path() );

        if( inputPath.IsEmpty() )
        {
            ApiResponseStatus e;
            e.set_status( ApiStatusCode::AS_BAD_REQUEST );
            e.set_error_message( "OpenDocument requires a non-empty path" );
            return tl::unexpected( e );
        }

        wxFileName projectPath( inputPath );

        if( projectPath.GetExt() == FILEEXT::KiCadPcbFileExtension )
            projectPath.SetExt( FILEEXT::ProjectFileExtension );
        else if( projectPath.GetExt() != FILEEXT::ProjectFileExtension )
            projectPath.SetExt( FILEEXT::ProjectFileExtension );

        projectPath.MakeAbsolute();

        closeCurrentDocument();

        wxString error;

        if( !aKiway.ProcessApiOpenDocument( KIWAY::FACE_PCB, projectPath.GetFullPath(), server.get(), &error ) )
        {
            ApiResponseStatus e;
            e.set_status( ApiStatusCode::AS_BAD_REQUEST );
            e.set_error_message( error.ToStdString() );
            return tl::unexpected( e );
        }

        openProjectPath = projectPath;
        openDocumentType = aRequest.type();

        kiapi::common::commands::OpenDocumentResponse response;
        kiapi::common::types::DocumentSpecifier*      doc = response.mutable_document();
        PROJECT&                                      project = Pgm().GetSettingsManager().Prj();

        doc->set_type( openDocumentType );

        if( openDocumentType == types::DOCTYPE_PCB )
        {
            wxFileName boardPath( openProjectPath );
            boardPath.SetExt( FILEEXT::KiCadPcbFileExtension );
            doc->set_board_filename( boardPath.GetFullName().ToStdString() );
        }

        doc->mutable_project()->set_name( project.GetProjectName().ToStdString() );
        doc->mutable_project()->set_path( project.GetProjectDirectory().ToStdString() );

        return response;
    };

    auto closeDocument =
            [&]( const kiapi::common::commands::CloseDocument& aRequest ) -> HANDLER_RESULT<google::protobuf::Empty>
    {
        using namespace kiapi::common;

        if( openDocumentType == types::DOCTYPE_UNKNOWN )
        {
            ApiResponseStatus e;
            e.set_status( ApiStatusCode::AS_BAD_REQUEST );
            e.set_error_message( "No document is currently open" );
            return tl::unexpected( e );
        }

        if( aRequest.has_document() )
        {
            if( aRequest.document().type() != openDocumentType )
            {
                ApiResponseStatus e;
                e.set_status( ApiStatusCode::AS_BAD_REQUEST );
                e.set_error_message( "Requested document type does not match the open document" );
                return tl::unexpected( e );
            }

            if( openDocumentType == types::DOCTYPE_PCB && !aRequest.document().board_filename().empty() )
            {
                wxFileName expectedBoardPath( openProjectPath );
                expectedBoardPath.SetExt( FILEEXT::KiCadPcbFileExtension );

                if( expectedBoardPath.GetFullName() != wxString::FromUTF8( aRequest.document().board_filename() ) )
                {
                    ApiResponseStatus e;
                    e.set_status( ApiStatusCode::AS_BAD_REQUEST );
                    e.set_error_message( "Requested document does not match the open document" );
                    return tl::unexpected( e );
                }
            }
        }

        wxString boardFileName;

        if( openDocumentType == types::DOCTYPE_PCB )
        {
            wxFileName expectedBoardPath( openProjectPath );
            expectedBoardPath.SetExt( FILEEXT::KiCadPcbFileExtension );
            boardFileName = expectedBoardPath.GetFullName();
        }

        wxString error;

        if( !aKiway.ProcessApiCloseDocument( KIWAY::FACE_PCB, boardFileName, server.get(), &error ) )
        {
            ApiResponseStatus e;
            e.set_status( ApiStatusCode::AS_BAD_REQUEST );
            e.set_error_message( error.ToStdString() );
            return tl::unexpected( e );
        }

        openProjectPath.Clear();
        openDocumentType = types::DOCTYPE_UNKNOWN;

        return google::protobuf::Empty();
    };

    commonHandler.SetOpenDocumentHandler( openDocument );
    commonHandler.SetCloseDocumentHandler( closeDocument );

    server->RegisterHandler( &commonHandler );
    server->Start();

    if( !server->Running() )
    {
        wxFprintf( stderr, _( "Failed to start API server\n" ) );
        return EXIT_CODES::ERR_UNKNOWN;
    }

    wxString preloadPath = wxString::FromUTF8( m_argParser.get<std::string>( ARG_PATH ) );

    if( !preloadPath.IsEmpty() )
    {
        kiapi::common::commands::OpenDocument request;
        request.set_type( kiapi::common::types::DOCTYPE_PROJECT );
        request.set_path( preloadPath.ToStdString() );

        auto preloadResult = openDocument( request );

        if( !preloadResult )
        {
            wxFprintf( stderr, "%s\n", preloadResult.error().error_message() );
            server->DeregisterHandler( &commonHandler );
            return EXIT_CODES::ERR_ARGS;
        }
    }

    server->SetReadyToReply( true );

    wxString listenPath = wxString::FromUTF8( server->SocketPath() );
    wxFprintf( stdout, "KiCad API server listening at %s\n", listenPath );

    auto oldSigInt = std::signal( SIGINT, apiServerSignalHandler );
#ifdef SIGTERM
    auto oldSigTerm = std::signal( SIGTERM, apiServerSignalHandler );
#endif

    wxTheApp->MainLoop();

    std::signal( SIGINT, oldSigInt );
#ifdef SIGTERM
    std::signal( SIGTERM, oldSigTerm );
#endif

    wxFprintf( stdout, "Shutting down\n" );

    closeCurrentDocument();
    server->DeregisterHandler( &commonHandler );

    return EXIT_CODES::OK;
}
