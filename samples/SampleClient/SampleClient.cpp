/*********************************************************************
 * \page   SampleClient.cpp
 * \file   SampleClient.cpp
 * \brief  Sample client using NatNet library
 * This program connects to a NatNet server, receives a data stream, and writes that data stream
 * to an ascii file.  The purpose is to illustrate using the NatNetClient class.
 * Usage [optional]:
 *	SampleClient [ServerIP] [LocalIP] [OutputFilename]
 *	[ServerIP]			IP address of the server (e.g. 192.168.0.107) ( defaults to local machine)
 *	[OutputFilename]	Name of points file (pts) to write out.  defaults to Client-output.pts
 *********************************************************************/

 /* 
Copyright © 2012 NaturalPoint Inc.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License. */


#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <conio.h>
#else
#include <unistd.h>
#include <termios.h>
#include <stdio.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#endif

// stl
#include <map>
#include <string>
#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <memory>
using namespace std;

#include <NatNetTypes.h>
#include <NatNetCAPI.h>
#include <NatNetClient.h>

#ifndef _WIN32
char getch();
int _kbhit();
#endif

// NatNet Callbacks
void NATNET_CALLCONV ServerDiscoveredCallback(const sNatNetDiscoveredServer* pDiscoveredServer, void* pUserContext);
void NATNET_CALLCONV DataHandler(sFrameOfMocapData* data, void* pUserData);    // receives data from the server
void NATNET_CALLCONV MessageHandler(Verbosity msgType, const char* msg);      // receives NatNet error messages

// Write output to file
void WriteHeader(FILE* fp, sDataDescriptions* pDataDefs);
void WriteFrame(FILE* fp, sFrameOfMocapData* data);
void WriteFooter(FILE* fp);

// Helper functions
void ResetClient();
int ConnectClient();
bool UpdateDataDescriptions(bool printToConsole);
void UpdateDataToDescriptionMaps(sDataDescriptions* pDataDefs);
void PrintDataDescriptions(sDataDescriptions* pDataDefs);
int ProcessKeyboardInput();
int SetGetProperty(char* szSetGetCommand);
void OutputFrameQueueToConsole();

static const ConnectionType kDefaultConnectionType = ConnectionType_Multicast;
//static const ConnectionType kDefaultConnectionType = ConnectionType_Unicast;
static const int kMaxMessageLength = 256;

// Connection variables
NatNetClient* g_pClient = NULL;
sNatNetClientConnectParams g_connectParams;
sServerDescription g_serverDescription;

// Server Discovery (optional)
vector< sNatNetDiscoveredServer > g_discoveredServers;
char g_discoveredMulticastGroupAddr[kNatNetIpv4AddrStrLenMax] = NATNET_DEFAULT_MULTICAST_ADDRESS;

// DataDescriptions to Frame Data Lookup maps
sDataDescriptions* g_pDataDefs = NULL;
map<int, int> g_AssetIDtoAssetDescriptionOrder;
map<int, string> g_AssetIDtoAssetName;
bool gUpdatedDataDescriptions = false;
bool gNeedUpdatedDataDescriptions = true;

string strDefaultLocal = "";
string strDefaultMotive = "";

// Frame Queue
typedef struct MocapFrameWrapper
{
    shared_ptr<sFrameOfMocapData> data;
    double transitLatencyMillisec;
    double clientLatencyMillisec;
} MocapFrameWrapper;
std::timed_mutex gNetworkQueueMutex;
std::deque<MocapFrameWrapper> gNetworkQueue;
const int kMaxQueueSize = 500;

// Misc
FILE* g_outputFile = NULL;
int g_analogSamplesPerMocapFrame = 0;
float gSmoothingValue = 0.1f;
bool gPauseOutput = false;

void printfBits(uint64_t val, int nBits)
{
    for (int i = nBits - 1; i >= 0; i--)
    {
        printf("%d", (int) (val >> i) & 0x01);
    }
    printf("\n");
}

/**
 * \brief Simple NatNet client
 * 
 * \param argc Number of input arguments.
 * \param argv Array of input arguments.
 * \return NatNetTypes.h error code.
 */
int main( int argc, char* argv[] )
{
    // Print NatNet client version info
    unsigned char ver[4];
    NatNet_GetVersion( ver );
    printf( "NatNet Sample Client (NatNet ver. %d.%d.%d.%d)\n", ver[0], ver[1], ver[2], ver[3] );

    // Install logging callback
    NatNet_SetLogCallback( MessageHandler );

    // Create NatNet client
    g_pClient = new NatNetClient();

    // Set the frame callback handler
    g_pClient->SetFrameReceivedCallback( DataHandler, g_pClient );	// this function will receive data from the server

    // [Optional] Automatic Motive server discovery
    // If no arguments were specified on the command line, attempt to discover servers on the local network.
    if ( (argc == 1) && (strDefaultLocal.empty() && strDefaultMotive.empty()) )
    {
        // An example of synchronous server discovery.
#if 0
        const unsigned int kDiscoveryWaitTimeMillisec = 5 * 1000; // Wait 5 seconds for responses.
        const int kMaxDescriptions = 10; // Get info for, at most, the first 10 servers to respond.
        sNatNetDiscoveredServer servers[kMaxDescriptions];
        int actualNumDescriptions = kMaxDescriptions;
        NatNet_BroadcastServerDiscovery( servers, &actualNumDescriptions );

        if ( actualNumDescriptions < kMaxDescriptions )
        {
            // If this happens, more servers responded than the array was able to store.
        }
#endif
        // Do asynchronous server discovery.
        printf( "Looking for servers on the local network.\n" );
        printf( "Press the number key that corresponds to any discovered server to connect to that server.\n" );
        printf( "Press Q at any time to quit.\n\n" );

        NatNetDiscoveryHandle discovery;
        NatNet_CreateAsyncServerDiscovery( &discovery, ServerDiscoveredCallback );

        while ( const int c = getch() )
        {
            if ( c >= '1' && c <= '9' )
            {
                const size_t serverIndex = c - '1';
                if ( serverIndex < g_discoveredServers.size() )
                {
                    const sNatNetDiscoveredServer& discoveredServer = g_discoveredServers[serverIndex];

                    if ( discoveredServer.serverDescription.bConnectionInfoValid )
                    {
                        // Build the connection parameters.
#ifdef _WIN32
                        _snprintf_s(
#else
                        snprintf(
#endif
                            g_discoveredMulticastGroupAddr, sizeof g_discoveredMulticastGroupAddr,
                            "%" PRIu8 ".%" PRIu8".%" PRIu8".%" PRIu8"",
                            discoveredServer.serverDescription.ConnectionMulticastAddress[0],
                            discoveredServer.serverDescription.ConnectionMulticastAddress[1],
                            discoveredServer.serverDescription.ConnectionMulticastAddress[2],
                            discoveredServer.serverDescription.ConnectionMulticastAddress[3]
                        );

                        g_connectParams.connectionType = discoveredServer.serverDescription.ConnectionMulticast ? ConnectionType_Multicast : ConnectionType_Unicast;
                        g_connectParams.serverCommandPort = discoveredServer.serverCommandPort;
                        g_connectParams.serverDataPort = discoveredServer.serverDescription.ConnectionDataPort;
                        g_connectParams.serverAddress = discoveredServer.serverAddress;
                        g_connectParams.localAddress = discoveredServer.localAddress;
                        g_connectParams.multicastAddress = g_discoveredMulticastGroupAddr;
                    }
                    else
                    {
                        // We're missing some info because it's a legacy server.
                        // Guess the defaults and make a best effort attempt to connect.
                        g_connectParams.connectionType = kDefaultConnectionType;
                        g_connectParams.serverCommandPort = discoveredServer.serverCommandPort;
                        g_connectParams.serverDataPort = 0;
                        g_connectParams.serverAddress = discoveredServer.serverAddress;
                        g_connectParams.localAddress = discoveredServer.localAddress;
                        g_connectParams.multicastAddress = NULL;
                    }

                    break;
                }
            }
            else if ( c == 'q' )
            {
                return 0;
            }
        }

        NatNet_FreeAsyncServerDiscovery( discovery );
    }
    else
    {
        // Manually specify Motive server IP/connection type
        g_connectParams.connectionType = kDefaultConnectionType;
        g_connectParams.localAddress = strDefaultLocal.c_str();
        g_connectParams.serverAddress = strDefaultMotive.c_str();
        g_connectParams.connectionType = kDefaultConnectionType;
        g_connectParams.serverCommandPort = 1510;
        g_connectParams.serverDataPort = 1511;
        g_connectParams.multicastAddress = g_discoveredMulticastGroupAddr;
        if ( argc >= 2 )
        {
            g_connectParams.serverAddress = argv[1];
        }
        if ( argc >= 3 )
        {
            g_connectParams.localAddress = argv[2];
        }
    }

    // Connect to Motive
    int iResult = ConnectClient();
    if (iResult != ErrorCode_OK)
    {
        printf("Error initializing client. See log for details. Exiting.\n");
        return 1;
    }
    else
    {
        printf("Client initialized and ready.\n");
    }

    // Get latest asset list from Motive
    gUpdatedDataDescriptions = UpdateDataDescriptions(true);
    if (!gUpdatedDataDescriptions)
    {
        printf("[SampleClient] ERROR : Unable to retrieve Data Descriptions from Motive.\n");
    }
    else
    {
        // Create data file for writing received stream into
        const char* szFile = "Client-output.pts";
        if (argc > 3)
        {
            szFile = argv[3];
        }
        g_outputFile = fopen(szFile, "w");
        if (!g_outputFile)
        {
            printf("[SampleClient] Error opening output file %s.  Exiting.\n", szFile);
        }
        else
        {
            WriteHeader(g_outputFile, g_pDataDefs);
        }
    }

	// Main thread loop
    // Data will be delivered in a separate thread to DataHandler() callback functon
	printf("\n[SampleClient] Client is connected to server and listening for data...\n");
	bool bRunning = true;
    while (bRunning)
    {   
        // If Motive Asset list has changed, update our lookup maps
        if (gNeedUpdatedDataDescriptions)
        {
            gUpdatedDataDescriptions = UpdateDataDescriptions(false);
            if (gUpdatedDataDescriptions)
            {
                gNeedUpdatedDataDescriptions = false;
            }
        }
        
        // Process any keyboard commands
        int keyboardInputChar = ProcessKeyboardInput();
        if (keyboardInputChar == 'q')
        {
            bRunning = false;
        }

        // print all mocap frames in data queue to console
        if (!gPauseOutput)
        {
            OutputFrameQueueToConsole();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

	// Exiting - clean up
	if (g_pClient)
	{
		g_pClient->Disconnect();
		delete g_pClient;
		g_pClient = NULL;
	}
	if (g_outputFile)
	{
		WriteFooter(g_outputFile);
		fclose(g_outputFile);
		g_outputFile = NULL;
	}
    if (g_pDataDefs)
    {
        NatNet_FreeDescriptions(g_pDataDefs);
        g_pDataDefs = NULL;
    }

	return ErrorCode_OK;
}

/**
 * \brief Process Keyboard Input.
 * 
 * \return Keyboard character.
 */
int ProcessKeyboardInput()
{
    int keyboardChar = 0;
    int iResult = 0;
    void* response = nullptr;
    int nBytes = 0;

    if (_kbhit())
    {
        keyboardChar = getch();
        switch (keyboardChar)
        {
            // Exit program
            case 'q':
                printf("\n\nQuitting...");
                break;
            // Disconnect and reconnect to Motive
            case 'r':
                ResetClient();
                break;
            // Get Motive description
            case 'p':
                sServerDescription ServerDescription;
                memset(&ServerDescription, 0, sizeof(ServerDescription));
                g_pClient->GetServerDescription(&ServerDescription);
                if (!ServerDescription.HostPresent)
                {
                    printf("[SampleClient] Unable to connect to Motive server.");
                }
                break;
            // Update data descriptions ( Motive active asset list )
            case 's':
                printf("\n\n[SampleClient] Requesting Data Descriptions...");
                gUpdatedDataDescriptions = UpdateDataDescriptions(true);
                if (!gUpdatedDataDescriptions)
                {
                    printf("[SampleClient] ERROR : Unable to retrieve Data Descriptions from Motive.\n");
                }
                else
                {
                    gNeedUpdatedDataDescriptions = false;
                }
                break;
            // Change connection type to multicast
            case 'm':
                g_connectParams.connectionType = ConnectionType_Multicast;
                iResult = ConnectClient();
                if (iResult == ErrorCode_OK)
                    printf("[SampleClient] Client connection type changed to Multicast.\n\n");
                else
                    printf("[SampleClient] Error changing client connection type to Multicast.\n\n");
                break;
            // Change connection type to unicast
            case 'u':
                g_connectParams.connectionType = ConnectionType_Unicast;
                iResult = ConnectClient();
                if (iResult == ErrorCode_OK)
                    printf("[SampleClient] Client connection type changed to Unicast.\n\n");
                else
                    printf("[SampleClient] Error changing client connection type to Unicast.\n\n");
                break;
            // Connect to Motive
            case 'c':
                iResult = ConnectClient();
                break;
            // Disconnect from Motive
            // note: applies to unicast connections only - indicates to Motive to stop sending packets
            // to this client's endpoint
            case 'd':
                iResult = g_pClient->SendMessageAndWait("Disconnect", &response, &nBytes);
                if (iResult == ErrorCode_OK)
                    printf("[SampleClient] Disconnected");
                else
                    printf("[SampleClient] Error Disconnecting");
                break;
            // test command : get framerate
            case 'f' :
                iResult = g_pClient->SendMessageAndWait("FrameRate", &response, &nBytes);
                if (iResult == ErrorCode_OK)
                {
                    float fRate = *((float*)response);
                    printf("Mocap Framerate : %3.2f\n", fRate);
                }
                else
                {
                    printf("Error getting frame rate.\n");
                }
                break;
            // test command : Set / Get a RigidBody property
            case 'e':
                {
                    // Note : Smoothing value is actually float [0.0,1.0].  Motive UI formats it to [0-100]
                    char szCommand[kMaxMessageLength];
                    sprintf(szCommand, "SetProperty,RigidBody1,Smoothing,%f", gSmoothingValue);
                    SetGetProperty(szCommand);
                    gSmoothingValue += .1f;
                }
                break;
            case 'z':
                gPauseOutput = !gPauseOutput;
                if (gPauseOutput)
                {
                    printf("\n\n--- Console Data Output Paused ---\n\n");
                }
                else
                {
                    printf("\n\n--- Console Data Output Resumed ---\n\n");
                }
                break;
            default:
                break;
        }

    }

    return keyboardChar;
}


/**
 * \brief [optional] called by NatNet with a list of automatically discovered Motive instances on the network(s).
 * 
 * \param pDiscoveredServer
 * \param pUserContext
 * \return 
 */
void NATNET_CALLCONV ServerDiscoveredCallback( const sNatNetDiscoveredServer* pDiscoveredServer, void* pUserContext )
{
    char serverHotkey = '.';
    if ( g_discoveredServers.size() < 9 )
    {
        serverHotkey = static_cast<char>('1' + g_discoveredServers.size());
    }

    printf( "[%c] %s %d.%d.%d at %s ",
        serverHotkey,
        pDiscoveredServer->serverDescription.szHostApp,
        pDiscoveredServer->serverDescription.HostAppVersion[0],
        pDiscoveredServer->serverDescription.HostAppVersion[1],
        pDiscoveredServer->serverDescription.HostAppVersion[2],
        pDiscoveredServer->serverAddress);

    if ( pDiscoveredServer->serverDescription.bConnectionInfoValid )
    {
        printf( "(%s)", pDiscoveredServer->serverDescription.ConnectionMulticast ? "multicast" : "unicast" );
    }
    else
    {
        printf( "(WARNING: Legacy server, could not auto-detect settings. Auto-connect may not work reliably.)" );
    }

    printf( " from %s\n", pDiscoveredServer->localAddress );

    g_discoveredServers.push_back( *pDiscoveredServer );
}

/**
 * \brief Establish a NatNet Client connection.
 * 
 * \return 
 */
int ConnectClient()
{
    // Disconnect from any previous server (if connected)
    g_pClient->Disconnect();

    // Connect to NatNet server (e.g. Motive)
    int retCode = g_pClient->Connect( g_connectParams );
    if (retCode != ErrorCode_OK)
    {
        // Connection failed - print connection error code
        printf("[SampleClinet] Unable to connect to server.  Error code: %d. Exiting.\n", retCode);
        return ErrorCode_Internal;
    }
    else
    {
        // Connection succeeded
        void* pResult;
        int nBytes = 0;
        ErrorCode ret = ErrorCode_OK;

        // example : print server info
        memset(&g_serverDescription, 0, sizeof(g_serverDescription));
        ret = g_pClient->GetServerDescription(&g_serverDescription);
        if (ret != ErrorCode_OK || !g_serverDescription.HostPresent)
        {
            printf("[SampleClient] Unable to connect to server. Host not present. Exiting.\n");
            return 1;
        }
        printf("\n[SampleClient] Server application info:\n");
        printf("Application: %s (ver. %d.%d.%d.%d)\n", g_serverDescription.szHostApp, g_serverDescription.HostAppVersion[0],
            g_serverDescription.HostAppVersion[1], g_serverDescription.HostAppVersion[2], g_serverDescription.HostAppVersion[3]);
        printf("NatNet Version: %d.%d.%d.%d\n", g_serverDescription.NatNetVersion[0], g_serverDescription.NatNetVersion[1],
            g_serverDescription.NatNetVersion[2], g_serverDescription.NatNetVersion[3]);
        printf("Client IP:%s\n", g_connectParams.localAddress);
        printf("Server IP:%s\n", g_connectParams.serverAddress);
        printf("Server Name:%s\n", g_serverDescription.szHostComputerName);

        // example : get mocap frame rate
        ret = g_pClient->SendMessageAndWait("FrameRate", &pResult, &nBytes);
        if (ret == ErrorCode_OK)
        {
            float fRate = *((float*)pResult);
            printf("Mocap Framerate : %3.2f\n", fRate);
        }
        else
        {
            printf("Error getting frame rate.\n");
        }

    }

    return ErrorCode_OK;
}

/**
 * \brief Get the latest active assets list from Motive.
 * 
 * \param printToConsole
 * \return 
 */
bool UpdateDataDescriptions(bool printToConsole)
{
    // release memory allocated by previous in previous GetDataDescriptionList()
    if (g_pDataDefs)
    {
        NatNet_FreeDescriptions(g_pDataDefs);
    }

    // Retrieve Data Descriptions from Motive
    printf("\n\n[SampleClient] Requesting Data Descriptions...\n");
    int iResult = g_pClient->GetDataDescriptionList(&g_pDataDefs);
    if (iResult != ErrorCode_OK || g_pDataDefs == NULL)
    {
        return false;
    }
    else
    {
        if (printToConsole)
        {
            PrintDataDescriptions(g_pDataDefs);
        }
    }

    UpdateDataToDescriptionMaps(g_pDataDefs);

    return true;
}

/**
 * Print data descriptions to std out.
 * 
 * \param pDataDefs
 */
void PrintDataDescriptions(sDataDescriptions* pDataDefs)
{
    printf("[SampleClient] Received %d Data Descriptions:\n", pDataDefs->nDataDescriptions);
    for (int i = 0; i < pDataDefs->nDataDescriptions; i++)
    {
        printf("Data Description # %d (type=%d)\n", i, pDataDefs->arrDataDescriptions[i].type);
        if (pDataDefs->arrDataDescriptions[i].type == Descriptor_MarkerSet)
        {
            // MarkerSet
            sMarkerSetDescription* pMS = pDataDefs->arrDataDescriptions[i].Data.MarkerSetDescription;
            printf("MarkerSet Name : %s\n", pMS->szName);
            for (int i = 0; i < pMS->nMarkers; i++)
                printf("%s\n", pMS->szMarkerNames[i]);

        }
        else if (pDataDefs->arrDataDescriptions[i].type == Descriptor_RigidBody)
        {
            // RigidBody
            sRigidBodyDescription* pRB = pDataDefs->arrDataDescriptions[i].Data.RigidBodyDescription;
            printf("RigidBody Name : %s\n", pRB->szName);
            printf("RigidBody ID : %d\n", pRB->ID);
            printf("RigidBody Parent ID : %d\n", pRB->parentID);
            printf("Parent Offset : %3.2f,%3.2f,%3.2f\n", pRB->offsetx, pRB->offsety, pRB->offsetz);

            if (pRB->MarkerPositions != NULL && pRB->MarkerRequiredLabels != NULL)
            {
                for (int markerIdx = 0; markerIdx < pRB->nMarkers; ++markerIdx)
                {
                    const MarkerData& markerPosition = pRB->MarkerPositions[markerIdx];
                    const int markerRequiredLabel = pRB->MarkerRequiredLabels[markerIdx];

                    printf("\tMarker #%d:\n", markerIdx);
                    printf("\t\tPosition: %.2f, %.2f, %.2f\n", markerPosition[0], markerPosition[1], markerPosition[2]);

                    if (markerRequiredLabel != 0)
                    {
                        printf("\t\tRequired active label: %d\n", markerRequiredLabel);
                    }
                }
            }
        }
        else if (pDataDefs->arrDataDescriptions[i].type == Descriptor_Skeleton)
        {
            // Skeleton
            sSkeletonDescription* pSK = pDataDefs->arrDataDescriptions[i].Data.SkeletonDescription;
            printf("Skeleton Name : %s\n", pSK->szName);
            printf("Skeleton ID : %d\n", pSK->skeletonID);
            printf("RigidBody (Bone) Count : %d\n", pSK->nRigidBodies);
            for (int j = 0; j < pSK->nRigidBodies; j++)
            {
                sRigidBodyDescription* pRB = &pSK->RigidBodies[j];
                printf("  RigidBody Name : %s\n", pRB->szName);
                printf("  RigidBody ID : %d\n", pRB->ID);
                printf("  RigidBody Parent ID : %d\n", pRB->parentID);
                printf("  Parent Offset : %3.2f,%3.2f,%3.2f\n", pRB->offsetx, pRB->offsety, pRB->offsetz);
            }
        }
        else if (pDataDefs->arrDataDescriptions[i].type == Descriptor_Asset)
        {
            // Trained Markerset
            sAssetDescription* pAsset = pDataDefs->arrDataDescriptions[i].Data.AssetDescription;
            printf("Trained Markerset Name : %s\n", pAsset->szName);
            printf("Asset ID : %d\n", pAsset->AssetID);

            // Trained Markerset Rigid Bodies
            printf("Trained Markerset RigidBody (Bone) Count : %d\n", pAsset->nRigidBodies);
            for (int j = 0; j < pAsset->nRigidBodies; j++)
            {
                sRigidBodyDescription* pRB = &pAsset->RigidBodies[j];
                printf("  RigidBody Name : %s\n", pRB->szName);
                printf("  RigidBody ID : %d\n", pRB->ID);
                printf("  RigidBody Parent ID : %d\n", pRB->parentID);
                printf("  Parent Offset : %3.2f,%3.2f,%3.2f\n", pRB->offsetx, pRB->offsety, pRB->offsetz);
            }

            // Trained Markerset Markers
            printf("Trained Markerset Marker Count : %d\n", pAsset->nMarkers);
            for (int j = 0; j < pAsset->nMarkers; j++)
            {
                sMarkerDescription marker = pAsset->Markers[j];
                int modelID, markerID;
                NatNet_DecodeID(marker.ID, &modelID, &markerID);
                printf("  Marker Name : %s\n", marker.szName);
                printf("  Marker ID   : %d\n", markerID);
                printf("  Marker Params : ");
                printfBits(marker.params, sizeof(marker.params) * 8);
            }
        }
        else if (pDataDefs->arrDataDescriptions[i].type == Descriptor_ForcePlate)
        {
            // Force Plate
            sForcePlateDescription* pFP = pDataDefs->arrDataDescriptions[i].Data.ForcePlateDescription;
            printf("Force Plate ID : %d\n", pFP->ID);
            printf("Force Plate Serial : %s\n", pFP->strSerialNo);
            printf("Force Plate Width : %3.2f\n", pFP->fWidth);
            printf("Force Plate Length : %3.2f\n", pFP->fLength);
            printf("Force Plate Electrical Center Offset (%3.3f, %3.3f, %3.3f)\n", pFP->fOriginX, pFP->fOriginY, pFP->fOriginZ);
            for (int iCorner = 0; iCorner < 4; iCorner++)
                printf("Force Plate Corner %d : (%3.4f, %3.4f, %3.4f)\n", iCorner, pFP->fCorners[iCorner][0], pFP->fCorners[iCorner][1], pFP->fCorners[iCorner][2]);
            printf("Force Plate Type : %d\n", pFP->iPlateType);
            printf("Force Plate Data Type : %d\n", pFP->iChannelDataType);
            printf("Force Plate Channel Count : %d\n", pFP->nChannels);
            for (int iChannel = 0; iChannel < pFP->nChannels; iChannel++)
                printf("\tChannel %d : %s\n", iChannel, pFP->szChannelNames[iChannel]);
        }
        else if (pDataDefs->arrDataDescriptions[i].type == Descriptor_Device)
        {
            // Peripheral Device
            sDeviceDescription* pDevice = pDataDefs->arrDataDescriptions[i].Data.DeviceDescription;
            printf("Device Name : %s\n", pDevice->strName);
            printf("Device Serial : %s\n", pDevice->strSerialNo);
            printf("Device ID : %d\n", pDevice->ID);
            printf("Device Channel Count : %d\n", pDevice->nChannels);
            for (int iChannel = 0; iChannel < pDevice->nChannels; iChannel++)
                printf("\tChannel %d : %s\n", iChannel, pDevice->szChannelNames[iChannel]);
        }
        else if (pDataDefs->arrDataDescriptions[i].type == Descriptor_Camera)
        {
            // Camera
            sCameraDescription* pCamera = pDataDefs->arrDataDescriptions[i].Data.CameraDescription;
            printf("Camera Name : %s\n", pCamera->strName);
            printf("Camera Position (%3.2f, %3.2f, %3.2f)\n", pCamera->x, pCamera->y, pCamera->z);
            printf("Camera Orientation (%3.2f, %3.2f, %3.2f, %3.2f)\n", pCamera->qx, pCamera->qy, pCamera->qz, pCamera->qw);
        }
        else
        {
            // Unknown
            printf("Unknown data type.\n");
        }
    }
}

/**
 * Update maps whenever the asset list in Motive has changed (as indicated in the data packet's TrackedModelsChanged bit)
 * 
 * \param pDataDefs
 */
void UpdateDataToDescriptionMaps(sDataDescriptions* pDataDefs)
{
    g_AssetIDtoAssetDescriptionOrder.clear();
    g_AssetIDtoAssetName.clear();
    int assetID = 0;
    std::string assetName = "";
    int index = 0;
    int cameraIndex = 0;

    if (pDataDefs == nullptr || pDataDefs->nDataDescriptions <= 0)
        return;

    for (int i = 0; i < pDataDefs->nDataDescriptions; i++)
    {
        assetID = -1;
        assetName = "";

        if (pDataDefs->arrDataDescriptions[i].type == Descriptor_RigidBody)
        {
            sRigidBodyDescription* pRB = pDataDefs->arrDataDescriptions[i].Data.RigidBodyDescription;
            assetID = pRB->ID;
            assetName = std::string(pRB->szName);
        }
        else if (pDataDefs->arrDataDescriptions[i].type == Descriptor_Skeleton)
        {
            sSkeletonDescription* pSK = pDataDefs->arrDataDescriptions[i].Data.SkeletonDescription;
            assetID = pSK->skeletonID;
            assetName = std::string(pSK->szName);

            // Add individual bones
            // skip for now since id could clash with non-skeleton RigidBody ids in our RigidBody lookup table
            /*
            if (insertResult.second == true)
            {
                for (int j = 0; j < pSK->nRigidBodies; j++)
                {
                    // Note:
                    // In the DataCallback packet (sFrameOfMocapData) skeleton bones (rigid bodies) ids are of the form:
                    //   parent skeleton ID   : high word (upper 16 bits of int)
                    //   rigid body id        : low word  (lower 16 bits of int)
                    //
                    // In DataDescriptions packet (sDataDescriptions) they are not, so apply the data id format here
                    // for correct lookup during data callback
                    std::pair<std::map<int, std::string>::iterator, bool> insertBoneResult;
                    sRigidBodyDescription rb = pSK->RigidBodies[j];
                    int id = (rb.parentID << 16) | rb.ID;
                    std::string skeletonBoneName = string(pSK->szName) + (":") + string(rb.szName) + string(pSK->szName);
                    insertBoneResult = g_AssetIDtoAssetName.insert(id, skeletonBoneName);
                }
            }
            */
        }
        else if (pDataDefs->arrDataDescriptions[i].type == Descriptor_MarkerSet)
        {
            // Skip markersets for now as they dont have unique id's, but do increase the index
            // as they are in the data packet
            index++;
            continue;
            /*
            sMarkerSetDescription* pDesc = pDataDefs->arrDataDescriptions[i].Data.MarkerSetDescription;
            assetID = index;
            assetName = pDesc->szName;
            */
        }

        else if (pDataDefs->arrDataDescriptions[i].type == Descriptor_ForcePlate)
        {
            sForcePlateDescription* pDesc = pDataDefs->arrDataDescriptions[i].Data.ForcePlateDescription;
            assetID = pDesc->ID;
            assetName = pDesc->strSerialNo;
        }
        else if (pDataDefs->arrDataDescriptions[i].type == Descriptor_Device)
        {
            sDeviceDescription* pDesc = pDataDefs->arrDataDescriptions[i].Data.DeviceDescription;
            assetID = pDesc->ID;
            assetName = std::string(pDesc->strName);
        }
        else if (pDataDefs->arrDataDescriptions[i].type == Descriptor_Camera)
        {
            // skip cameras as they are not in the data packet
            continue;
        }
        else if (pDataDefs->arrDataDescriptions[i].type == Descriptor_Asset)
        {
            sAssetDescription* pDesc = pDataDefs->arrDataDescriptions[i].Data.AssetDescription;
            assetID = pDesc->AssetID;
            assetName = std::string(pDesc->szName);
        }

        if (assetID == -1)
        {
            printf("\n[SampleClient] Warning : Unknown data type in description list : %d\n", pDataDefs->arrDataDescriptions[i].type);
        }
        else 
        {
            // Add to Asset ID to Asset Name map
            std::pair<std::map<int, std::string>::iterator, bool> insertResult;
            insertResult = g_AssetIDtoAssetName.insert(std::pair<int,std::string>(assetID, assetName));
            if (insertResult.second == false)
            {
                printf("\n[SampleClient] Warning : Duplicate asset ID already in Name map (Existing:%d,%s\tNew:%d,%s\n)",
                    insertResult.first->first, insertResult.first->second.c_str(), assetID, assetName.c_str());
            }
        }

        // Add to Asset ID to Asset Description Order map
        if (assetID != -1)
        {
            std::pair<std::map<int, int>::iterator, bool> insertResult;
            insertResult = g_AssetIDtoAssetDescriptionOrder.insert(std::pair<int, int>(assetID, index++));
            if (insertResult.second == false)
            {
                printf("\n[SampleClient] Warning : Duplicate asset ID already in Order map (ID:%d\tOrder:%d\n)", insertResult.first->first, insertResult.first->second);
            }
        }
    }

}

/**
 * Output frame queue to console.
 * 
 */
void OutputFrameQueueToConsole()
{
    // Add data from the network queue into our display queue in order to quickly
    // free up access to the network queue.
    std::deque<MocapFrameWrapper> displayQueue;
    if (gNetworkQueueMutex.try_lock_for(std::chrono::milliseconds(5)))
    {
        for (MocapFrameWrapper f : gNetworkQueue)
        {
            displayQueue.push_back(f);
        }

        // Release all frames in network queue
        gNetworkQueue.clear();
        gNetworkQueueMutex.unlock();
    }


    // Now we can take our time displaying our data without
    // worrying about interfering with the network processing queue.
    for (MocapFrameWrapper f : displayQueue)
    {
        sFrameOfMocapData* data = f.data.get();

        printf("\n=====================  New Packet Arrived  =============================\n");
        printf("FrameID : %d\n", data->iFrame);
        printf("Timestamp : %3.2lf\n", data->fTimestamp);
        printf("Params : ");
        printfBits(data->params, sizeof(data->params)*8);

        // timecode - for systems with an eSync and SMPTE timecode generator - decode to values
        int hour, minute, second, frame, subframe;
        NatNet_DecodeTimecode(data->Timecode, data->TimecodeSubframe, &hour, &minute, &second, &frame, &subframe);
        char szTimecode[128] = "";
        NatNet_TimecodeStringify(data->Timecode, data->TimecodeSubframe, szTimecode, 128);
        printf("Timecode : %s\n", szTimecode);

        // Latency Metrics
        // 
        // Software latency here is defined as the span of time between:
        //   a) The reception of a complete group of 2D frames from the camera system (CameraDataReceivedTimestamp)
        // and
        //   b) The time immediately prior to the NatNet frame being transmitted over the network (TransmitTimestamp)
        //
        // This figure may appear slightly higher than the "software latency" reported in the Motive user interface,
        // because it additionally includes the time spent preparing to stream the data via NatNet.
        const uint64_t softwareLatencyHostTicks = data->TransmitTimestamp - data->CameraDataReceivedTimestamp;
        const double softwareLatencyMillisec = (softwareLatencyHostTicks * 1000) / static_cast<double>(g_serverDescription.HighResClockFrequency);
        printf("Motive Software latency : %.2lf milliseconds\n", softwareLatencyMillisec);

        // Only recent versions of the Motive software in combination with Ethernet camera systems support system latency measurement.
        // If it's unavailable (for example, with USB camera systems, or during playback), this field will be zero.
        const bool bSystemLatencyAvailable = data->CameraMidExposureTimestamp != 0;
        if (bSystemLatencyAvailable)
        {
            // System latency here is defined as the span of time between:
            //   a) The midpoint of the camera exposure window, and therefore the average age of the photons (CameraMidExposureTimestamp)
            // and
            //   b) The time immediately prior to the NatNet frame being transmitted over the network (TransmitTimestamp)
            const uint64_t systemLatencyHostTicks = data->TransmitTimestamp - data->CameraMidExposureTimestamp;
            const double systemLatencyMillisec = (systemLatencyHostTicks * 1000) / static_cast<double>(g_serverDescription.HighResClockFrequency);
            printf("Motive System latency : %.2lf milliseconds\n", systemLatencyMillisec);

            // Transit latency is defined as the span of time between Motive transmitting the frame of data, and its reception by the client (now).
            // The SecondsSinceHostTimestamp method relies on NatNetClient's internal clock synchronization with the server using Cristian's algorithm.
            printf("NatNet Transit latency : %.2lf milliseconds\n", f.transitLatencyMillisec);

            // Total Client latency is defined as the sum of system latency and the transit time taken to relay the data to the NatNet client.
            // This is the all-inclusive measurement (photons to client processing).
            // You could equivalently do the following (not accounting for time elapsed since we calculated transit latency above):
            //const double clientLatencyMillisec = systemLatencyMillisec + transitLatencyMillisec;
            printf("Total Client latency : %.2lf milliseconds \n", f.clientLatencyMillisec);
        }
        else
        {
            printf("Transit latency : %.2lf milliseconds\n", f.transitLatencyMillisec);
        }

        // precision timestamps (optionally present, typically PTP) (NatNet 4.1 and later)
        if (data->PrecisionTimestampSecs != 0)
        {
            printf("Precision Timestamp Seconds : %d\n", data->PrecisionTimestampSecs);
            printf("Precision Timestamp Fractional Seconds : %d\n", data->PrecisionTimestampFractionalSecs);
        }

        if (gNeedUpdatedDataDescriptions)
        {
            printf("\n\n[SampleClient] Waiting for updated asset list\n");
            break;
        }

        bool bTrackedModelsChanged = ((data->params & 0x02) != 0);
        if (bTrackedModelsChanged)
        {
            printf("\n\nMotive asset list changed.  Requesting new data descriptions.\n");
            gNeedUpdatedDataDescriptions = true;
            break;
        }

        if (g_outputFile)
        {
            WriteFrame(g_outputFile, data);
        }

        bool bIsRecording = ((data->params & 0x01) != 0);
        if (bIsRecording)
        {
            printf("\nRECORDING\n");
        }

        // Rigid Bodies
        int i = 0;
        printf("------------------------\n");
        printf("Rigid Bodies [Count=%d]\n", data->nRigidBodies);
        for (i = 0; i < data->nRigidBodies; i++)
        {
            // params
            // 0x01 : bool, rigid body was successfully tracked in this frame
            bool bTrackingValid = data->RigidBodies[i].params & 0x01;
            int streamingID = data->RigidBodies[i].ID;
            printf("%s [ID=%d  Error(mm)=%.5f  Tracked=%d]\n", g_AssetIDtoAssetName[streamingID].c_str(), streamingID, data->RigidBodies[i].MeanError*1000.0f, bTrackingValid);
            printf("\tx\ty\tz\tqx\tqy\tqz\tqw\n");
            printf("\t%3.2f\t%3.2f\t%3.2f\t%3.2f\t%3.2f\t%3.2f\t%3.2f\n",
                data->RigidBodies[i].x,
                data->RigidBodies[i].y,
                data->RigidBodies[i].z,
                data->RigidBodies[i].qx,
                data->RigidBodies[i].qy,
                data->RigidBodies[i].qz,
                data->RigidBodies[i].qw);
        }

        // Skeletons
        printf("------------------------\n");
        printf("Skeletons [Count=%d]\n", data->nSkeletons);
        for (i = 0; i < data->nSkeletons; i++)
        {
            sSkeletonData skData = data->Skeletons[i];
            printf("Skeleton [ID=%d  Bone count=%d]\n", skData.skeletonID, skData.nRigidBodies);
            for (int j = 0; j < skData.nRigidBodies; j++)
            {
                sRigidBodyData rbData = skData.RigidBodyData[j];
                printf("Bone %d\t%3.2f\t%3.2f\t%3.2f\t%3.2f\t%3.2f\t%3.2f\t%3.2f\n",
                    rbData.ID, rbData.x, rbData.y, rbData.z, rbData.qx, rbData.qy, rbData.qz, rbData.qw);
            }
        }

        // Trained Markerset Data (Motive 3.1 / NatNet 4.1 and later)
        printf("------------------------\n");
        printf("Assets [Count=%d]\n", data->nAssets);
        for (int i = 0; i < data->nAssets; i++)
        {
            sAssetData asset = data->Assets[i];
            printf("Trained Markerset [ID=%d  Bone count=%d   Marker count=%d]\n", 
                asset.assetID, asset.nRigidBodies, asset.nMarkers);

            // Trained Markerset Rigid Bodies
            for (int j = 0; j < asset.nRigidBodies; j++)
            {
                // note : Trained markerset ids are of the form:
                // parent markerset ID  : high word (upper 16 bits of int)
                // rigid body id        : low word  (lower 16 bits of int)
                int assetID, rigidBodyID;
                sRigidBodyData rbData = asset.RigidBodyData[j];
                NatNet_DecodeID(rbData.ID, &assetID, &rigidBodyID);
                printf("Bone %d\t%3.2f\t%3.2f\t%3.2f\t%3.2f\t%3.2f\t%3.2f\t%3.2f\n",
                    rigidBodyID, rbData.x, rbData.y, rbData.z, rbData.qx, rbData.qy, rbData.qz, rbData.qw);
            }

            // Trained Markerset markers
            for (int j = 0; j < asset.nMarkers; j++)
            {
                sMarker marker = asset.MarkerData[j];
                int assetID, markerID;
                NatNet_DecodeID(marker.ID, &assetID, &markerID);
                printf("Marker [AssetID=%d, MarkerID=%d] [size=%3.2f] [pos=%3.2f,%3.2f,%3.2f] [residual(mm)=%.4f]\n",
                    assetID, markerID, marker.size, marker.x, marker.y, marker.z, marker.residual * 1000.0f);
            }
        }   

        // labeled markers - this includes all markers (Active, Passive, and 'unlabeled' (markers with no asset but a PointCloud ID)
        bool bOccluded;     // marker was not visible (occluded) in this frame
        bool bPCSolved;     // reported position provided by point cloud solve
        bool bModelSolved;  // reported position provided by model solve
        bool bHasModel;     // marker has an associated asset in the data stream
        bool bUnlabeled;    // marker is 'unlabeled', but has a point cloud ID that matches Motive PointCloud ID (In Motive 3D View)
        bool bActiveMarker; // marker is an actively labeled LED marker

        printf("------------------------\n");
        printf("Markers [Count=%d]\n", data->nLabeledMarkers);
        for (i = 0; i < data->nLabeledMarkers; i++)
        {
            bOccluded = ((data->LabeledMarkers[i].params & 0x01) != 0);
            bPCSolved = ((data->LabeledMarkers[i].params & 0x02) != 0);
            bModelSolved = ((data->LabeledMarkers[i].params & 0x04) != 0);
            bHasModel = ((data->LabeledMarkers[i].params & 0x08) != 0);
            bUnlabeled = ((data->LabeledMarkers[i].params & 0x10) != 0);
            bActiveMarker = ((data->LabeledMarkers[i].params & 0x20) != 0);

            sMarker marker = data->LabeledMarkers[i];

            // Marker ID Scheme:
            // Active Markers:
            //   ID = ActiveID, correlates to RB ActiveLabels list
            // Passive Markers: 
            //   If Asset with Legacy Labels
            //      AssetID 	(Hi Word)
            //      MemberID	(Lo Word)
            //   Else
            //      PointCloud ID
            int modelID, markerID;
            NatNet_DecodeID(marker.ID, &modelID, &markerID);

            char szMarkerType[512];
            if (bActiveMarker)
                strcpy(szMarkerType, "Active");
            else if (bUnlabeled)
                strcpy(szMarkerType, "Unlabeled");
            else
                strcpy(szMarkerType, "Labeled");

            printf("%s Marker [ModelID=%d, MarkerID=%d] [size=%3.2f] [pos=%3.2f,%3.2f,%3.2f] [residual(mm)=%.4f]\n",
                szMarkerType, modelID, markerID, marker.size, marker.x, marker.y, marker.z, marker.residual*1000.0f);
        }

        // force plates
        printf("------------------------\n");
        printf("Force Plates [Count=%d]\n", data->nForcePlates);
        for (int iPlate = 0; iPlate < data->nForcePlates; iPlate++)
        {
            printf("Force Plate %d\n", data->ForcePlates[iPlate].ID);
            for (int iChannel = 0; iChannel < data->ForcePlates[iPlate].nChannels; iChannel++)
            {
                printf("\tChannel %d:\t", iChannel);
                if (data->ForcePlates[iPlate].ChannelData[iChannel].nFrames == 0)
                {
                    printf("\tEmpty Frame\n");
                }
                else if (data->ForcePlates[iPlate].ChannelData[iChannel].nFrames != g_analogSamplesPerMocapFrame)
                {
                    printf("\tPartial Frame [Expected:%d   Actual:%d]\n", g_analogSamplesPerMocapFrame, data->ForcePlates[iPlate].ChannelData[iChannel].nFrames);
                }
                for (int iSample = 0; iSample < data->ForcePlates[iPlate].ChannelData[iChannel].nFrames; iSample++)
                    printf("%3.2f\t", data->ForcePlates[iPlate].ChannelData[iChannel].Values[iSample]);
                printf("\n");
            }
        }

        // devices
        printf("------------------------\n");
        printf("Devices [Count=%d]\n", data->nDevices);
        for (int iDevice = 0; iDevice < data->nDevices; iDevice++)
        {
            printf("Device %d\n", data->Devices[iDevice].ID);
            for (int iChannel = 0; iChannel < data->Devices[iDevice].nChannels; iChannel++)
            {
                printf("\tChannel %d:\t", iChannel);
                if (data->Devices[iDevice].ChannelData[iChannel].nFrames == 0)
                {
                    printf("\tEmpty Frame\n");
                }
                else if (data->Devices[iDevice].ChannelData[iChannel].nFrames != g_analogSamplesPerMocapFrame)
                {
                    printf("\tPartial Frame [Expected:%d   Actual:%d]\n", g_analogSamplesPerMocapFrame, data->Devices[iDevice].ChannelData[iChannel].nFrames);
                }
                for (int iSample = 0; iSample < data->Devices[iDevice].ChannelData[iChannel].nFrames; iSample++)
                    printf("%3.2f\t", data->Devices[iDevice].ChannelData[iChannel].Values[iSample]);
                printf("\n");
            }
        }
    }

    // Release all frames (and frame data) in the display queue
    for (MocapFrameWrapper f : displayQueue)
    {
        NatNet_FreeFrame(f.data.get());
    }
    displayQueue.clear();

}

/**
 * DataHandler is called by NatNet on a separate network processing thread
 * when a frame of mocap data is available
 * 
 * \param data
 * \param pUserData
 * \return 
 */
void NATNET_CALLCONV DataHandler(sFrameOfMocapData* data, void* pUserData)
{
    NatNetClient* pClient = (NatNetClient*) pUserData;
    if (!pClient)
        return;

    // Note : This function is called every 1 / mocap rate ( e.g. 100 fps = every 10 msecs )
    // We don't want to do too much here and cause the network processing thread to get behind,
    // so let's just safely add this frame to our shared  'network' frame queue and return.
    
    // Note : The 'data' ptr passed in is managed by NatNet and cannot be used outside this function.
    // Since we are keeping the data, we need to make a copy of it.
    shared_ptr<sFrameOfMocapData> pDataCopy = make_shared<sFrameOfMocapData>();
    NatNet_CopyFrame(data, pDataCopy.get());

    MocapFrameWrapper f;
    f.data = pDataCopy;
    f.clientLatencyMillisec = pClient->SecondsSinceHostTimestamp(data->CameraMidExposureTimestamp) * 1000.0;
    f.transitLatencyMillisec = pClient->SecondsSinceHostTimestamp(data->TransmitTimestamp) * 1000.0;

    if (gNetworkQueueMutex.try_lock_for(std::chrono::milliseconds(5)))
    {
        gNetworkQueue.push_back(f);

        // Maintain a cap on the queue size, removing oldest as necessary
        while ((int)gNetworkQueue.size() > kMaxQueueSize)
        {
            f = gNetworkQueue.front();
            NatNet_FreeFrame(f.data.get());
            gNetworkQueue.pop_front();
        }
        gNetworkQueueMutex.unlock();
    }
    else
    {
        // Unable to lock the frame queue and we chose not to wait - drop the frame and notify
        NatNet_FreeFrame(pDataCopy.get());
        printf("\nFrame dropped (Frame : %d)\n", f.data->iFrame);
    }

    return;
}

/**
 * MessageHandler receives NatNet error/debug messages.
 * 
 * \param msgType
 * \param msg
 * \return 
 */
void NATNET_CALLCONV MessageHandler( Verbosity msgType, const char* msg )
{
    // Optional: Filter out debug messages
    if ( msgType < Verbosity_Info )
    {
        return;
    }

    printf( "\n[NatNetLib]" );

    switch ( msgType )
    {
        case Verbosity_Debug:
            printf( " [DEBUG]" );
            break;
        case Verbosity_Info:
            printf( "  [INFO]" );
            break;
        case Verbosity_Warning:
            printf( "  [WARN]" );
            break;
        case Verbosity_Error:
            printf( " [ERROR]" );
            break;
        default:
            printf( " [?????]" );
            break;
    }

    printf( ": %s\n", msg );
}

/**
 * Write header to output file.
 * 
 * \param fp
 * \param pBodyDefs
 */
void WriteHeader(FILE* fp, sDataDescriptions* pBodyDefs)
{
	int i=0;

    // For now, lets just write markerset data
    if ( pBodyDefs->arrDataDescriptions[0].type != Descriptor_MarkerSet )
        return;

	sMarkerSetDescription* pMS = pBodyDefs->arrDataDescriptions[0].Data.MarkerSetDescription;

	fprintf(fp, "<MarkerSet>\n\n");
	fprintf(fp, "<Name>\n%s\n</Name>\n\n", pMS->szName);

	fprintf(fp, "<Markers>\n");
	for(i=0; i < pMS->nMarkers; i++)
	{
		fprintf(fp, "%s\n", pMS->szMarkerNames[i]);
	}
	fprintf(fp, "</Markers>\n\n");

	fprintf(fp, "<Data>\n");
	fprintf(fp, "Frame#\t");
	for(i=0; i < pMS->nMarkers; i++)
	{
		fprintf(fp, "M%dX\tM%dY\tM%dZ\t", i, i, i);
	}
	fprintf(fp,"\n");

}

/**
 * Write frame of data to output file.
 * 
 * \param fp
 * \param data
 */
void WriteFrame(FILE* fp, sFrameOfMocapData* data)
{
	fprintf(fp, "%d", data->iFrame);
	for(int i =0; i < data->MocapData->nMarkers; i++)
	{
		fprintf(fp, "\t%.5f\t%.5f\t%.5f", data->MocapData->Markers[i][0], data->MocapData->Markers[i][1], data->MocapData->Markers[i][2]);
	}
	fprintf(fp, "\n");
}

/**
 * Write footer to output file.
 * 
 * \param fp Output file pointer
 */
void WriteFooter(FILE* fp)
{
	fprintf(fp, "</Data>\n\n");
	fprintf(fp, "</MarkerSet>\n");
}

/**
 * Reset the client.
 * 
 */
void ResetClient()
{
	int iSuccess;

	printf("\n\nre-setting Client\n\n.");

	iSuccess = g_pClient->Disconnect();
	if(iSuccess != 0)
		printf("error un-initting Client\n");

    iSuccess = g_pClient->Connect( g_connectParams );
	if(iSuccess != 0)
		printf("error re-initting Client\n");
}

/**
 * Set or get a rigid body property.
 * 
 * \param szSetGetCommand
 * \return 
 */
int SetGetProperty(char* szSetGetCommand)
{
    int iResult = 0;
    void* response = nullptr;
    int nBytes = 0;
    char szMessage[kMaxMessageLength];

    // Set Value
    strcpy(szMessage, szSetGetCommand);
    printf("\n");
    printf(szMessage);
    printf("\n");
    iResult = g_pClient->SendMessageAndWait(szMessage, &response, &nBytes);

    // Confirm Value was set
    if (iResult == ErrorCode_OK)
    {
        // Note : we sleep here to give Motive a chance to process the above SetProperty 
        // on a safe thread.  Otherwise Motive may return the previous value
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        sprintf(szMessage, "GetProperty,RigidBody1,Smoothing");
        iResult = g_pClient->SendMessageAndWait(szMessage, &response, &nBytes);
        if (iResult == ErrorCode_OK)
        {
            double val = atof((char*)response);
            sprintf(szMessage, "%s,%.2f", szMessage, val);
            printf(szMessage);
            printf("\n");
        }
        else
        {
            printf("Error getting property [ Err=%d ]\n", iResult);
        }
    }
    else
    {
        printf("Error setting property [ Err=%d ]\n", iResult);
    }

    return iResult;
}

#ifndef _WIN32
int _kbhit() 
{
    static const int STDIN = 0;
    static bool initialized = false;

    if (!initialized) {
        // Use termios to turn off line buffering
        termios term;
        tcgetattr(STDIN, &term);
        term.c_lflag &= ~ICANON;
        tcsetattr(STDIN, TCSANOW, &term);
        setbuf(stdin, NULL);
        initialized = true;
    }

    int bytesWaiting;
    ioctl(STDIN, FIONREAD, &bytesWaiting);
    return bytesWaiting;
}

char getch()
{
    char buf = 0;
    termios old = { 0 };

    fflush( stdout );

    if ( tcgetattr( 0, &old ) < 0 )
        perror( "tcsetattr()" );

    old.c_lflag &= ~ICANON;
    old.c_lflag &= ~ECHO;
    old.c_cc[VMIN] = 1;
    old.c_cc[VTIME] = 0;

    if ( tcsetattr( 0, TCSANOW, &old ) < 0 )
        perror( "tcsetattr ICANON" );

    if ( read( 0, &buf, 1 ) < 0 )
        perror( "read()" );

    old.c_lflag |= ICANON;
    old.c_lflag |= ECHO;

    if ( tcsetattr( 0, TCSADRAIN, &old ) < 0 )
        perror( "tcsetattr ~ICANON" );

    //printf( "%c\n", buf );

    return buf;
}
#endif
