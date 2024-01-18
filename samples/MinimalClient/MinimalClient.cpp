/*********************************************************************
 * \page   MinimalClient.cpp
 * \file   MinimalClient.cpp
 * \brief  The *minimal* amount of code required to connect to Motive and get data.
 * For a more complete example with additional functionality, consult the
 * SampleClient.cpp example in the NatNet SDK
 *********************************************************************/

 /*
Copyright � 2012 NaturalPoint Inc.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License. 
*/

// using STL for cross platform sleep
#include <thread>

#ifndef ORIGINAL_SDK
#include <cstdio>
#include <cstring>

// non-standard/optional extension of C; define an unsafe version here
// to not change example code below
int strcpy_s(char *dest, size_t destsz, const char *src)
{
    strcpy(dest, src);
    return 0;
}

template <size_t size>
int strcpy_s(char (&dest)[size], const char *src)
{
    return strcpy_s(dest, size, src);
}
#endif

// NatNet SDK includes
#include "../../include/NatNetTypes.h"
#include "../../include/NatNetCAPI.h"
#include "../../include/NatNetClient.h"

void NATNET_CALLCONV DataHandler(sFrameOfMocapData* data, void* pUserData);    // receives data from the server
void PrintData(sFrameOfMocapData* data, NatNetClient* pClient);
void PrintDataDescriptions(sDataDescriptions* pDataDefs);

NatNetClient* g_pClient = nullptr;
sNatNetClientConnectParams g_connectParams;
sServerDescription g_serverDescription;
sDataDescriptions* g_pDataDefs = nullptr;

/**
 * \brief Minimal client example.
 * 
 * \param argc
 * \param argv
 * \return Returns NatNetTypes Error code.
 */
int main(int argc, char* argv[])
{
    ErrorCode ret = ErrorCode_OK;

    // Create a NatNet client
    g_pClient = new NatNetClient();

    // Set the Client's frame callback handler
    ret = g_pClient->SetFrameReceivedCallback(DataHandler, g_pClient);	

    // Specify client PC's IP address, Motive PC's IP address, and network connection type
    g_connectParams.localAddress = "127.0.0.1";
    g_connectParams.serverAddress = "127.0.0.1";
    g_connectParams.connectionType = ConnectionType_Multicast;

    // Connect to Motive
    ret = g_pClient->Connect(g_connectParams);
    if (ret != ErrorCode_OK)
    {
        // Connection failed
        printf("Unable to connect to server.  Error code: %d. Exiting.\n", ret);
        return 1;
    }
     
    // Get Motive server description
    memset(&g_serverDescription, 0, sizeof(g_serverDescription));
    ret = g_pClient->GetServerDescription(&g_serverDescription);
    if (ret != ErrorCode_OK || !g_serverDescription.HostPresent)
    {
        printf("Unable to get server description. Error Code:%d.  Exiting.\n", ret);
        return 1;
    }
    else
    {
        printf("Connected : %s (ver. %d.%d.%d.%d)\n", g_serverDescription.szHostApp, g_serverDescription.HostAppVersion[0],
            g_serverDescription.HostAppVersion[1], g_serverDescription.HostAppVersion[2], g_serverDescription.HostAppVersion[3]);
    }

    // Get current active asset list from Motive
    ret = g_pClient->GetDataDescriptionList(&g_pDataDefs);
    if (ret != ErrorCode_OK || g_pDataDefs == NULL)
    {
        printf("Error getting asset list.  Error Code:%d  Exiting.\n", ret);
        return 1;
    }
    else
    {
        PrintDataDescriptions(g_pDataDefs);
    }

    printf("\nClient is connected and listening for data...\n");
    
    // do something on the main app's thread...
    while (true)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Clean up
    if (g_pClient)
    {
        g_pClient->Disconnect();
        delete g_pClient;
    }
    
    if (g_pDataDefs)
    {
        NatNet_FreeDescriptions(g_pDataDefs);
        g_pDataDefs = NULL;
    }

    return ErrorCode_OK;
}

/**
 * DataHandler called by NatNet on a separate network processing
 * thread whenever a frame of mocap data is available.
 * So at 100 mocap fps, this function should be called ~ every 10ms.
 * \brief DataHandler called by NatNet
 * \param data Input Frame of Mocap data
 * \param pUserData
 * \return 
 */
void NATNET_CALLCONV DataHandler(sFrameOfMocapData* data, void* pUserData)
{
    NatNetClient* pClient = (NatNetClient*)pUserData;
    PrintData(data, pClient);

    return;
}

/**
 * \brief Print out the current Motive active assets descriptions.
 * 
 * \param pDataDefs
 */
void PrintDataDescriptions(sDataDescriptions* pDataDefs)
{
    printf("Retrieved %d Data Descriptions:\n", pDataDefs->nDataDescriptions);
    for (int i = 0; i < pDataDefs->nDataDescriptions; i++)
    {
        printf("---------------------------------\n");
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
 * \brief Print out a single frame of mocap data.
 * 
 * \param data
 * \param pClient
 */
void PrintData(sFrameOfMocapData* data, NatNetClient* pClient)
{
    printf("\n=====================  New Packet Arrived  =============================\n");
    printf("FrameID : %d\n", data->iFrame);
    printf("Timestamp : %3.2lf\n", data->fTimestamp);
    
    // Rigid Bodies
    printf("------------------------\n");
    printf("Rigid Bodies [ Count = %d ]\n", data->nRigidBodies);
    for (int i = 0; i < data->nRigidBodies; i++)
    {
        // params
        bool bTrackingValid = data->RigidBodies[i].params & 0x01;
        int streamingID = data->RigidBodies[i].ID;
        printf("[ID=%d  Error=%3.4f  Tracked=%d]\n", streamingID, data->RigidBodies[i].MeanError, bTrackingValid);
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
    printf("Skeletons [ Count = %d ]\n", data->nSkeletons);
    for (int i = 0; i < data->nSkeletons; i++)
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

    // Labeled markers - this includes all markers (Active, Passive, and 'unlabeled' (markers with no asset but a PointCloud ID)
    bool bUnlabeled;    // marker is 'unlabeled', but has a point cloud ID that matches Motive PointCloud ID (In Motive 3D View)
    bool bActiveMarker; // marker is an actively labeled LED marker
    printf("------------------------\n");
    printf("Markers [ Count = %d ]\n", data->nLabeledMarkers);
    for (int i = 0; i < data->nLabeledMarkers; i++)
    {
        bUnlabeled = ((data->LabeledMarkers[i].params & 0x10) != 0);
        bActiveMarker = ((data->LabeledMarkers[i].params & 0x20) != 0);
        sMarker marker = data->LabeledMarkers[i];
        int modelID, markerID;
        NatNet_DecodeID(marker.ID, &modelID, &markerID);
        char szMarkerType[512];
        if (bActiveMarker)
            strcpy_s(szMarkerType, "Active");
        else if (bUnlabeled)
            strcpy_s(szMarkerType, "Unlabeled");
        else
            strcpy_s(szMarkerType, "Labeled");
        printf("%s Marker [ModelID=%d, MarkerID=%d] [size=%3.2f] [pos=%3.2f,%3.2f,%3.2f]\n",
            szMarkerType, modelID, markerID, marker.size, marker.x, marker.y, marker.z);
    }

    // Force plates
    printf("------------------------\n");
    printf("Force Plates [ Count = %d ]\n", data->nForcePlates);
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
            for (int iSample = 0; iSample < data->ForcePlates[iPlate].ChannelData[iChannel].nFrames; iSample++)
                printf("%3.2f\t", data->ForcePlates[iPlate].ChannelData[iChannel].Values[iSample]);
            printf("\n");
        }
    }

    // Peripheral Devices (e.g. NIDAQ, Glove, EMG)
    printf("------------------------\n");
    printf("Devices [ Count = %d ]\n", data->nDevices);
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
            for (int iSample = 0; iSample < data->Devices[iDevice].ChannelData[iChannel].nFrames; iSample++)
                printf("%3.2f\t", data->Devices[iDevice].ChannelData[iChannel].Values[iSample]);
            printf("\n");
        }
    }
}
