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

/*

PacketClient.cpp

Decodes NatNet packets directly.

Usage [optional]:

	PacketClient [ServerIP] [LocalIP]

	[ServerIP]			IP address of server ( defaults to local machine)
	[LocalIP]			IP address of client ( defaults to local machine)

*/

// Adopted from PacketClient.cpp (NatNetSDK 3.1)
// Windows Socket related functions removed

#include <inttypes.h>
#include <stdio.h>
#include <cstring>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <vector>

#define MAX_NAMELENGTH              256

// NATNET message ids
#define NAT_CONNECT                 0 
#define NAT_SERVERINFO              1
#define NAT_REQUEST                 2
#define NAT_RESPONSE                3
#define NAT_REQUEST_MODELDEF        4
#define NAT_MODELDEF                5
#define NAT_REQUEST_FRAMEOFDATA     6
#define NAT_FRAMEOFDATA             7
#define NAT_MESSAGESTRING           8
#define NAT_UNRECOGNIZED_REQUEST    100
#define UNDEFINED                   999999.9999

#define MAX_PACKETSIZE				100000	// max size of packet (actual packet size is dynamic)

// sender
typedef struct
{
    char szName[MAX_NAMELENGTH];            // sending app's name
    unsigned char Version[4];               // sending app's version [major.minor.build.revision]
    unsigned char NatNetVersion[4];         // sending app's NatNet version [major.minor.build.revision]

} sSender;

typedef struct
{
    unsigned short iMessage;                // message ID (e.g. NAT_FRAMEOFDATA)
    unsigned short nDataBytes;              // Num bytes in payload
    union
    {
        unsigned char  cData[MAX_PACKETSIZE];
        char           szData[MAX_PACKETSIZE];
        unsigned long  lData[MAX_PACKETSIZE/4];
        float          fData[MAX_PACKETSIZE/4];
        sSender        Sender;
    } Data;                                 // Payload incoming from NatNet Server

} sPacket;


// bool IPAddress_StringToAddr(char *szNameOrAddress, struct in_addr *Address);
void Unpack(char* pData);
// int GetLocalIPAddresses(unsigned long Addresses[], int nMax);
// int SendCommand(char* szCOmmand);

// This should match the multicast address listed in Motive's streaming settings.
#define MULTICAST_ADDRESS		"239.255.42.99"    

// NatNet Command channel
#define PORT_COMMAND            1510

// NatNet Data channel
#define PORT_DATA  			    1511                

// NatNetVersion: 240.164.53.0
// ServerVersion: 3.0.0.0


int NatNetVersion[4] = {240,164,53,0};
int ServerVersion[4] = {3,0,0,0};

void buildConnectPacket(std::vector<char>& buffer)
{
    sPacket packet;
    packet.iMessage = NAT_CONNECT;
    packet.nDataBytes = 0;
    buffer.resize(4);
    memcpy(buffer.data(), &packet, 4);
}

void UnpackCommand(char* pData)
{
    const sPacket* replyPacket = reinterpret_cast<const sPacket*>(pData);

    // handle command
    switch (replyPacket->iMessage)
    {
    // case NAT_MODELDEF:
    //     Unpack(pData);
    //     break;
    // case NAT_FRAMEOFDATA:
    //     Unpack(pData);
    //     break;
    case NAT_SERVERINFO:
        for(int i=0; i<4; i++)
        {
            NatNetVersion[i] = (int)replyPacket->Data.Sender.NatNetVersion[i];
            ServerVersion[i] = (int)replyPacket->Data.Sender.Version[i];
        }
        printf("NatNetVersion: %d.%d.%d.%d\n", NatNetVersion[0], NatNetVersion[1], NatNetVersion[2], NatNetVersion[3]);
        printf("ServerVersion: %d.%d.%d.%d\n", ServerVersion[0], ServerVersion[1], ServerVersion[2], ServerVersion[3]);
        break;
    // case NAT_RESPONSE:
    //     gCommandResponseSize = PacketIn.nDataBytes;
    //     if(gCommandResponseSize==4)
    //         memcpy(&gCommandResponse, &PacketIn.Data.lData[0], gCommandResponseSize);
    //     else
    //     {
    //         memcpy(&gCommandResponseString[0], &PacketIn.Data.cData[0], gCommandResponseSize);
    //         printf("Response : %s", gCommandResponseString);
    //         gCommandResponse = 0;   // ok
    //     }
    //     break;
    // case NAT_UNRECOGNIZED_REQUEST:
    //     printf("[Client] received 'unrecognized request'\n");
    //     gCommandResponseSize = 0;
    //     gCommandResponse = 1;       // err
    //     break;
    // case NAT_MESSAGESTRING:
    //     printf("[Client] Received message: %s\n", PacketIn.Data.szData);
    //     break;
    default:
        printf("Unknown command response!");
        break;
    }
}

// non-standard/optional extension of C; define an unsafe version here
// to not change example code below
int strcpy_s(char* dest, size_t destsz, const char *src)
{
    strcpy(dest, src);
    return 0;
}

template<typename... Args> int sprintf_s(char * buffer, size_t bufsz, const char * format, Args... args)
{
  return sprintf(buffer, format, args...);
}

// Funtion that assigns a time code values to 5 variables passed as arguments
// Requires an integer from the packet as the timecode and timecodeSubframe
bool DecodeTimecode(unsigned int inTimecode, unsigned int inTimecodeSubframe, int* hour, int* minute, int* second, int* frame, int* subframe)
{
	bool bValid = true;

	*hour = (inTimecode>>24)&255;
	*minute = (inTimecode>>16)&255;
	*second = (inTimecode>>8)&255;
	*frame = inTimecode&255;
	*subframe = inTimecodeSubframe;

	return bValid;
}

// Takes timecode and assigns it to a string
bool TimecodeStringify(unsigned int inTimecode, unsigned int inTimecodeSubframe, char *Buffer, int BufferSize)
{
	bool bValid;
	int hour, minute, second, frame, subframe;
	bValid = DecodeTimecode(inTimecode, inTimecodeSubframe, &hour, &minute, &second, &frame, &subframe);

	sprintf_s(Buffer,BufferSize,"%2d:%2d:%2d:%2d.%d",hour, minute, second, frame, subframe);
	for(unsigned int i=0; i<strlen(Buffer); i++)
		if(Buffer[i]==' ')
			Buffer[i]='0';

	return bValid;
}

void DecodeMarkerID(int sourceID, int* pOutEntityID, int* pOutMemberID)
{
    if (pOutEntityID)
        *pOutEntityID = sourceID >> 16;

    if (pOutMemberID)
        *pOutMemberID = sourceID & 0x0000ffff;
}

// *********************************************************************
//
//  Unpack Data:
//      Recieves pointer to bytes that represent a packet of data
//
//      There are lots of print statements that show what
//      data is being stored
//
//      Most memcpy functions will assign the data to a variable.
//      Use this variable at your descretion. 
//      Variables created for storing data do not exceed the 
//      scope of this function. 
//
// *********************************************************************
void Unpack(char* pData)
{
    // Checks for NatNet Version number. Used later in function. Packets may be different depending on NatNet version.
    int major = NatNetVersion[0];
    int minor = NatNetVersion[1];

    char *ptr = pData;

    printf("Begin Packet\n-------\n");

    // First 2 Bytes is message ID
    int MessageID = 0;
    memcpy(&MessageID, ptr, 2); ptr += 2;
    printf("Message ID : %d\n", MessageID);

    // Second 2 Bytes is the size of the packet
    int nBytes = 0;
    memcpy(&nBytes, ptr, 2); ptr += 2;
    printf("Byte count : %d\n", nBytes);
	
    if(MessageID == 7)      // FRAME OF MOCAP DATA packet
    {
        // Next 4 Bytes is the frame number
        int frameNumber = 0; memcpy(&frameNumber, ptr, 4); ptr += 4;
        printf("Frame # : %d\n", frameNumber);
    	
	    // Next 4 Bytes is the number of data sets (markersets, rigidbodies, etc)
        int nMarkerSets = 0; memcpy(&nMarkerSets, ptr, 4); ptr += 4;
        printf("Marker Set Count : %d\n", nMarkerSets);

        // Loop through number of marker sets and get name and data
        for (int i=0; i < nMarkerSets; i++)
        {    
            // Markerset name
            char szName[256];
            strcpy(szName, ptr);
            int nDataBytes = (int) strlen(szName) + 1;
            ptr += nDataBytes;
            printf("Model Name: %s\n", szName);

        	// marker data
            int nMarkers = 0; memcpy(&nMarkers, ptr, 4); ptr += 4;
            printf("Marker Count : %d\n", nMarkers);

            for(int j=0; j < nMarkers; j++)
            {
                float x = 0; memcpy(&x, ptr, 4); ptr += 4;
                float y = 0; memcpy(&y, ptr, 4); ptr += 4;
                float z = 0; memcpy(&z, ptr, 4); ptr += 4;
                printf("\tMarker %d : [x=%3.2f,y=%3.2f,z=%3.2f]\n",j,x,y,z);
            }
        }

	    // Loop through unlabeled markers
        int nOtherMarkers = 0; memcpy(&nOtherMarkers, ptr, 4); ptr += 4;
		// OtherMarker list is Deprecated
        printf("Unidentified Marker Count : %d\n", nOtherMarkers);
        for(int j=0; j < nOtherMarkers; j++)
        {
            float x = 0.0f; memcpy(&x, ptr, 4); ptr += 4;
            float y = 0.0f; memcpy(&y, ptr, 4); ptr += 4;
            float z = 0.0f; memcpy(&z, ptr, 4); ptr += 4;
            
			// Deprecated
			printf("\tMarker %d : pos = [%3.2f,%3.2f,%3.2f]\n",j,x,y,z);
        }
        
        // Loop through rigidbodies
        int nRigidBodies = 0;
        memcpy(&nRigidBodies, ptr, 4); ptr += 4;
        printf("Rigid Body Count : %d\n", nRigidBodies);
        for (int j=0; j < nRigidBodies; j++)
        {
            // Rigid body position and orientation 
            int ID = 0; memcpy(&ID, ptr, 4); ptr += 4;
            float x = 0.0f; memcpy(&x, ptr, 4); ptr += 4;
            float y = 0.0f; memcpy(&y, ptr, 4); ptr += 4;
            float z = 0.0f; memcpy(&z, ptr, 4); ptr += 4;
            float qx = 0; memcpy(&qx, ptr, 4); ptr += 4;
            float qy = 0; memcpy(&qy, ptr, 4); ptr += 4;
            float qz = 0; memcpy(&qz, ptr, 4); ptr += 4;
            float qw = 0; memcpy(&qw, ptr, 4); ptr += 4;
            printf("ID : %d\n", ID);
            printf("pos: [%3.2f,%3.2f,%3.2f]\n", x,y,z);
            printf("ori: [%3.2f,%3.2f,%3.2f,%3.2f]\n", qx,qy,qz,qw);

            // NatNet version 2.0 and later
            if(major >= 2)
            {
                // Mean marker error
                float fError = 0.0f; memcpy(&fError, ptr, 4); ptr += 4;
                printf("Mean marker error: %3.2f\n", fError);
            }

            // NatNet version 2.6 and later
            if( ((major == 2)&&(minor >= 6)) || (major > 2) || (major == 0) ) 
            {
                // params
                short params = 0; memcpy(&params, ptr, 2); ptr += 2;
                bool bTrackingValid = params & 0x01; // 0x01 : rigid body was successfully tracked in this frame
            }
           
        } // Go to next rigid body


        // Skeletons (NatNet version 2.1 and later)
        if( ((major == 2)&&(minor>0)) || (major>2))
        {
            int nSkeletons = 0;
            memcpy(&nSkeletons, ptr, 4); ptr += 4;
            printf("Skeleton Count : %d\n", nSkeletons);

            // Loop through skeletons
            for (int j=0; j < nSkeletons; j++)
            {
                // skeleton id
                int skeletonID = 0;
                memcpy(&skeletonID, ptr, 4); ptr += 4;

                // Number of rigid bodies (bones) in skeleton
                int nRigidBodies = 0;
                memcpy(&nRigidBodies, ptr, 4); ptr += 4;
                printf("Rigid Body Count : %d\n", nRigidBodies);

                // Loop through rigid bodies (bones) in skeleton
                for (int j=0; j < nRigidBodies; j++)
                {
                    // Rigid body position and orientation
                    int ID = 0; memcpy(&ID, ptr, 4); ptr += 4;
                    float x = 0.0f; memcpy(&x, ptr, 4); ptr += 4;
                    float y = 0.0f; memcpy(&y, ptr, 4); ptr += 4;
                    float z = 0.0f; memcpy(&z, ptr, 4); ptr += 4;
                    float qx = 0; memcpy(&qx, ptr, 4); ptr += 4;
                    float qy = 0; memcpy(&qy, ptr, 4); ptr += 4;
                    float qz = 0; memcpy(&qz, ptr, 4); ptr += 4;
                    float qw = 0; memcpy(&qw, ptr, 4); ptr += 4;
                    printf("ID : %d\n", ID);
                    printf("pos: [%3.2f,%3.2f,%3.2f]\n", x,y,z);
                    printf("ori: [%3.2f,%3.2f,%3.2f,%3.2f]\n", qx,qy,qz,qw);

                    // Mean marker error (NatNet version 2.0 and later)
                    if(major >= 2)
                    {
                        float fError = 0.0f; memcpy(&fError, ptr, 4); ptr += 4;
                        printf("Mean marker error: %3.2f\n", fError);
                    }

                    // Tracking flags (NatNet version 2.6 and later)
                    if( ((major == 2)&&(minor >= 6)) || (major > 2) || (major == 0) ) 
                    {
                        // params
                        short params = 0; memcpy(&params, ptr, 2); ptr += 2;
                        bool bTrackingValid = params & 0x01; // 0x01 : rigid body was successfully tracked in this frame
                    }

                } // next rigid body

            } // next skeleton
        }
        
        // labeled markers (NatNet version 2.3 and later)
        // labeled markers - this includes all markers: Active, Passive, and 'unlabeled' (markers with no asset but a PointCloud ID)
		if( ((major == 2)&&(minor>=3)) || (major>2))
		{
			int nLabeledMarkers = 0;
			memcpy(&nLabeledMarkers, ptr, 4); ptr += 4;
			printf("Labeled Marker Count : %d\n", nLabeledMarkers);

            // Loop through labeled markers
			for (int j=0; j < nLabeledMarkers; j++)
			{
				// id
                // Marker ID Scheme:
                // Active Markers:
                //   ID = ActiveID, correlates to RB ActiveLabels list
                // Passive Markers: 
                //   If Asset with Legacy Labels
                //      AssetID 	(Hi Word)
                //      MemberID	(Lo Word)
                //   Else
                //      PointCloud ID
				int ID = 0; memcpy(&ID, ptr, 4); ptr += 4;
                int modelID, markerID;
                DecodeMarkerID(ID, &modelID, &markerID);


				// x
				float x = 0.0f; memcpy(&x, ptr, 4); ptr += 4;
				// y
				float y = 0.0f; memcpy(&y, ptr, 4); ptr += 4;
				// z
				float z = 0.0f; memcpy(&z, ptr, 4); ptr += 4;
				// size
				float size = 0.0f; memcpy(&size, ptr, 4); ptr += 4;

                // NatNet version 2.6 and later
                if( ((major == 2)&&(minor >= 6)) || (major > 2) || (major == 0) ) 
                {
                    // marker params
                    short params = 0; memcpy(&params, ptr, 2); ptr += 2;
                    bool bOccluded = (params & 0x01) != 0;     // marker was not visible (occluded) in this frame
                    bool bPCSolved = (params & 0x02) != 0;     // position provided by point cloud solve
                    bool bModelSolved = (params & 0x04) != 0;  // position provided by model solve
                    if ((major >= 3) || (major == 0))
                    {
                        bool bHasModel = (params & 0x08) != 0;     // marker has an associated asset in the data stream
                        bool bUnlabeled = (params & 0x10) != 0;    // marker is 'unlabeled', but has a point cloud ID
                        bool bActiveMarker = (params & 0x20) != 0; // marker is an actively labeled LED marker
                    }

                }

                // NatNet version 3.0 and later
                float residual = 0.0f;
                if ((major >= 3) || (major == 0))
                {
                    // Marker residual
                    memcpy(&residual, ptr, 4); ptr += 4;
                }

				printf("ID  : [MarkerID: %d] [ModelID: %d]\n", markerID, modelID);
				printf("pos : [%3.2f,%3.2f,%3.2f]\n", x,y,z);
                printf("size: [%3.2f]\n", size);
                printf("err:  [%3.2f]\n", residual);
            }
		}

        // Force Plate data (NatNet version 2.9 and later)
        if (((major == 2) && (minor >= 9)) || (major > 2))
        {
            int nForcePlates;
            memcpy(&nForcePlates, ptr, 4); ptr += 4;
            for (int iForcePlate = 0; iForcePlate < nForcePlates; iForcePlate++)
            {
                // ID
                int ID = 0; memcpy(&ID, ptr, 4); ptr += 4;
                printf("Force Plate : %d\n", ID);

                // Channel Count
                int nChannels = 0; memcpy(&nChannels, ptr, 4); ptr += 4;

                // Channel Data
                for (int i = 0; i < nChannels; i++)
                {
                    printf(" Channel %d : ", i);
                    int nFrames = 0; memcpy(&nFrames, ptr, 4); ptr += 4;
                    for (int j = 0; j < nFrames; j++)
                    {
                        float val = 0.0f;  memcpy(&val, ptr, 4); ptr += 4;
                        printf("%3.2f   ", val);
                    }
                    printf("\n");
                }
            }
        }

        // Device data (NatNet version 3.0 and later)
        if (((major == 2) && (minor >= 11)) || (major > 2))
        {
            int nDevices;
            memcpy(&nDevices, ptr, 4); ptr += 4;
            for (int iDevice = 0; iDevice < nDevices; iDevice++)
            {
                // ID
                int ID = 0; memcpy(&ID, ptr, 4); ptr += 4;
                printf("Device : %d\n", ID);

                // Channel Count
                int nChannels = 0; memcpy(&nChannels, ptr, 4); ptr += 4;

                // Channel Data
                for (int i = 0; i < nChannels; i++)
                {
                    printf(" Channel %d : ", i);
                    int nFrames = 0; memcpy(&nFrames, ptr, 4); ptr += 4;
                    for (int j = 0; j < nFrames; j++)
                    {
                        float val = 0.0f;  memcpy(&val, ptr, 4); ptr += 4;
                        printf("%3.2f   ", val);
                    }
                    printf("\n");
                }
            }
        }
		
		// software latency (removed in version 3.0)
        if ( major < 3 )
        {
            float softwareLatency = 0.0f; memcpy(&softwareLatency, ptr, 4);	ptr += 4;
            printf("software latency : %3.3f\n", softwareLatency);
        }

		// timecode
		unsigned int timecode = 0; 	memcpy(&timecode, ptr, 4);	ptr += 4;
		unsigned int timecodeSub = 0; memcpy(&timecodeSub, ptr, 4); ptr += 4;
		char szTimecode[128] = "";
		TimecodeStringify(timecode, timecodeSub, szTimecode, 128);

        // timestamp
        double timestamp = 0.0f;

        // NatNet version 2.7 and later - increased from single to double precision
        if( ((major == 2)&&(minor>=7)) || (major>2))
        {
            memcpy(&timestamp, ptr, 8); ptr += 8;
        }
        else
        {
            float fTemp = 0.0f;
            memcpy(&fTemp, ptr, 4); ptr += 4;
            timestamp = (double)fTemp;
        }
        printf("Timestamp : %3.3f\n", timestamp);

        // high res timestamps (version 3.0 and later)
        if ( (major >= 3) || (major == 0) )
        {
            uint64_t cameraMidExposureTimestamp = 0;
            memcpy( &cameraMidExposureTimestamp, ptr, 8 ); ptr += 8;
            printf( "Mid-exposure timestamp : %" PRIu64"\n", cameraMidExposureTimestamp );

            uint64_t cameraDataReceivedTimestamp = 0;
            memcpy( &cameraDataReceivedTimestamp, ptr, 8 ); ptr += 8;
            printf( "Camera data received timestamp : %" PRIu64"\n", cameraDataReceivedTimestamp );

            uint64_t transmitTimestamp = 0;
            memcpy( &transmitTimestamp, ptr, 8 ); ptr += 8;
            printf( "Transmit timestamp : %" PRIu64"\n", transmitTimestamp );
        }

        // frame params
        short params = 0;  memcpy(&params, ptr, 2); ptr += 2;
        bool bIsRecording = (params & 0x01) != 0;                  // 0x01 Motive is recording
        bool bTrackedModelsChanged = (params & 0x02) != 0;         // 0x02 Actively tracked model list has changed


		// end of data tag
        int eod = 0; memcpy(&eod, ptr, 4); ptr += 4;
        printf("End Packet\n-------------\n");

    }
    else if(MessageID == 5) // Data Descriptions
    {
        // number of datasets
        int nDatasets = 0; memcpy(&nDatasets, ptr, 4); ptr += 4;
        printf("Dataset Count : %d\n", nDatasets);

        for(int i=0; i < nDatasets; i++)
        {
            printf("Dataset %d\n", i);

            int type = 0; memcpy(&type, ptr, 4); ptr += 4;
            printf("Type : %d\n", i, type);

            if(type == 0)   // markerset
            {
                // name
                char szName[256];
                strcpy(szName, ptr);
                int nDataBytes = (int) strlen(szName) + 1;
                ptr += nDataBytes;
                printf("Markerset Name: %s\n", szName);

        	    // marker data
                int nMarkers = 0; memcpy(&nMarkers, ptr, 4); ptr += 4;
                printf("Marker Count : %d\n", nMarkers);

                for(int j=0; j < nMarkers; j++)
                {
                    char szName[256];
                    strcpy(szName, ptr);
                    int nDataBytes = (int) strlen(szName) + 1;
                    ptr += nDataBytes;
                    printf("Marker Name: %s\n", szName);
                }
            }
            else if(type ==1)   // rigid body
            {
                if(major >= 2)
                {
                    // name
                    char szName[MAX_NAMELENGTH];
                    strcpy(szName, ptr);
                    ptr += strlen(ptr) + 1;
                    printf("Name: %s\n", szName);
                }

                int ID = 0; memcpy(&ID, ptr, 4); ptr +=4;
                printf("ID : %d\n", ID);
             
                int parentID = 0; memcpy(&parentID, ptr, 4); ptr +=4;
                printf("Parent ID : %d\n", parentID);
                
                float xoffset = 0; memcpy(&xoffset, ptr, 4); ptr +=4;
                printf("X Offset : %3.2f\n", xoffset);

                float yoffset = 0; memcpy(&yoffset, ptr, 4); ptr +=4;
                printf("Y Offset : %3.2f\n", yoffset);

                float zoffset = 0; memcpy(&zoffset, ptr, 4); ptr +=4;
                printf("Z Offset : %3.2f\n", zoffset);

                // Per-marker data (NatNet 3.0 and later)
                if ( major >= 3 )
                {
                    int nMarkers = 0; memcpy( &nMarkers, ptr, 4 ); ptr += 4;

                    // Marker positions
                    nBytes = nMarkers * 3 * sizeof( float );
                    float* markerPositions = (float*)malloc( nBytes );
                    memcpy( markerPositions, ptr, nBytes );
                    ptr += nBytes;

                    // Marker required active labels
                    nBytes = nMarkers * sizeof( int );
                    int* markerRequiredLabels = (int*)malloc( nBytes );
                    memcpy( markerRequiredLabels, ptr, nBytes );
                    ptr += nBytes;

                    for ( int markerIdx = 0; markerIdx < nMarkers; ++markerIdx )
                    {
                        float* markerPosition = markerPositions + markerIdx * 3;
                        const int markerRequiredLabel = markerRequiredLabels[markerIdx];

                        printf( "\tMarker #%d:\n", markerIdx );
                        printf( "\t\tPosition: %.2f, %.2f, %.2f\n", markerPosition[0], markerPosition[1], markerPosition[2] );

                        if ( markerRequiredLabel != 0 )
                        {
                            printf( "\t\tRequired active label: %d\n", markerRequiredLabel );
                        }
                    }

                    free( markerPositions );
                    free( markerRequiredLabels );
                }
            }
            else if(type ==2)   // skeleton
            {
                char szName[MAX_NAMELENGTH];
                strcpy(szName, ptr);
                ptr += strlen(ptr) + 1;
                printf("Name: %s\n", szName);

                int ID = 0; memcpy(&ID, ptr, 4); ptr +=4;
                printf("ID : %d\n", ID);

                int nRigidBodies = 0; memcpy(&nRigidBodies, ptr, 4); ptr +=4;
                printf("RigidBody (Bone) Count : %d\n", nRigidBodies);

                for(int i=0; i< nRigidBodies; i++)
                {
                    if(major >= 2)
                    {
                        // RB name
                        char szName[MAX_NAMELENGTH];
                        strcpy(szName, ptr);
                        ptr += strlen(ptr) + 1;
                        printf("Rigid Body Name: %s\n", szName);
                    }

                    int ID = 0; memcpy(&ID, ptr, 4); ptr +=4;
                    printf("RigidBody ID : %d\n", ID);

                    int parentID = 0; memcpy(&parentID, ptr, 4); ptr +=4;
                    printf("Parent ID : %d\n", parentID);

                    float xoffset = 0; memcpy(&xoffset, ptr, 4); ptr +=4;
                    printf("X Offset : %3.2f\n", xoffset);

                    float yoffset = 0; memcpy(&yoffset, ptr, 4); ptr +=4;
                    printf("Y Offset : %3.2f\n", yoffset);

                    float zoffset = 0; memcpy(&zoffset, ptr, 4); ptr +=4;
                    printf("Z Offset : %3.2f\n", zoffset);
                }
            }

        }   // next dataset

       printf("End Packet\n-------------\n");

    }
    else
    {
        printf("Unrecognized Packet Type.\n");
    }

}
