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
limitations under the License. */

/*

PacketClient.cpp

Decodes NatNet packets directly.

Usage [optional]:

	PacketClient [ServerIP] [LocalIP]

	[ServerIP]			IP address of server ( defaults to local machine)
	[LocalIP]			IP address of client ( defaults to local machine)

*/

#include <stdio.h>
#include <inttypes.h>
#ifdef ORIGINAL_SDK
#include <tchar.h>
#include <conio.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma warning( disable : 4996 )
#else
#include <cstring>
#include <cstdlib>
#include <vector>
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

template <typename... Args>
int sprintf_s(char *buffer, size_t bufsz, const char *format, Args... args)
{
    return sprintf(buffer, format, args...);
}

#endif

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

#ifdef ORIGINAL_SDK
bool IPAddress_StringToAddr(char *szNameOrAddress, struct in_addr *Address);
#endif
void Unpack(char* pData);
#ifdef ORIGINAL_SDK
int GetLocalIPAddresses(unsigned long Addresses[], int nMax);
int SendCommand(char* szCOmmand);
#endif

// This should match the multicast address listed in Motive's streaming settings.
#define MULTICAST_ADDRESS		"239.255.42.99"    

// NatNet Command channel
#define PORT_COMMAND            1510

// NatNet Data channel
#define PORT_DATA  			    1511

#ifdef ORIGINAL_SDK
SOCKET CommandSocket;
SOCKET DataSocket;
in_addr ServerAddress;
sockaddr_in HostAddr;  
#endif

int NatNetVersion[4] = {0,0,0,0};
int ServerVersion[4] = {0,0,0,0};

#ifdef ORIGINAL_SDK
int gCommandResponse = 0;
int gCommandResponseSize = 0;
unsigned char gCommandResponseString[MAX_PATH];
int gCommandResponseCode = 0;

// command response listener thread
DWORD WINAPI CommandListenThread(void* dummy)
{
    int addr_len;
    int nDataBytesReceived;
    char str[256];
    sockaddr_in TheirAddress;
    sPacket PacketIn;
    addr_len = sizeof(struct sockaddr);

    while (1)
    {
        // blocking
        nDataBytesReceived = recvfrom( CommandSocket,(char *)&PacketIn, sizeof(sPacket),
            0, (struct sockaddr *)&TheirAddress, &addr_len);

        if((nDataBytesReceived == 0) || (nDataBytesReceived == SOCKET_ERROR) )
            continue;

        // debug - print message
        sprintf(str, "[Client] Received command from %d.%d.%d.%d: Command=%d, nDataBytes=%d",
            TheirAddress.sin_addr.S_un.S_un_b.s_b1, TheirAddress.sin_addr.S_un.S_un_b.s_b2,
            TheirAddress.sin_addr.S_un.S_un_b.s_b3, TheirAddress.sin_addr.S_un.S_un_b.s_b4,
            (int)PacketIn.iMessage, (int)PacketIn.nDataBytes);


        // handle command
        switch (PacketIn.iMessage)
        {
        case NAT_MODELDEF:
            Unpack((char*)&PacketIn);
            break;
        case NAT_FRAMEOFDATA:
            Unpack((char*)&PacketIn);
            break;
        case NAT_SERVERINFO:
            for(int i=0; i<4; i++)
            {
                NatNetVersion[i] = (int)PacketIn.Data.Sender.NatNetVersion[i];
                ServerVersion[i] = (int)PacketIn.Data.Sender.Version[i];
            }
            break;
        case NAT_RESPONSE:
            gCommandResponseSize = PacketIn.nDataBytes;
            if(gCommandResponseSize==4)
                memcpy(&gCommandResponse, &PacketIn.Data.lData[0], gCommandResponseSize);
            else
            {
                memcpy(&gCommandResponseString[0], &PacketIn.Data.cData[0], gCommandResponseSize);
                printf("Response : %s", gCommandResponseString);
                gCommandResponse = 0;   // ok
            }
            break;
        case NAT_UNRECOGNIZED_REQUEST:
            printf("[Client] received 'unrecognized request'\n");
            gCommandResponseSize = 0;
            gCommandResponse = 1;       // err
            break;
        case NAT_MESSAGESTRING:
            printf("[Client] Received message: %s\n", PacketIn.Data.szData);
            break;
        }
    }

    return 0;
}

// Data listener thread. Listens for incoming bytes from NatNet
DWORD WINAPI DataListenThread(void* dummy)
{
    char  szData[20000];
    int addr_len = sizeof(struct sockaddr);
    sockaddr_in TheirAddress;

    while (1)
    {
        // Block until we receive a datagram from the network (from anyone including ourselves)
        int nDataBytesReceived = recvfrom(DataSocket, szData, sizeof(szData), 0, (sockaddr *)&TheirAddress, &addr_len);
        // Once we have bytes recieved Unpack organizes all the data
        Unpack(szData);
    }

    return 0;
}

SOCKET CreateCommandSocket(unsigned long IP_Address, unsigned short uPort)
{
    struct sockaddr_in my_addr;     
    static unsigned long ivalue;
    static unsigned long bFlag;
    int nlengthofsztemp = 64;  
    SOCKET sockfd;

    // Create a blocking, datagram socket
    if ((sockfd=socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET)
    {
        return -1;
    }

    // bind socket
    memset(&my_addr, 0, sizeof(my_addr));
    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(uPort);
    my_addr.sin_addr.S_un.S_addr = IP_Address;
    if (bind(sockfd, (struct sockaddr *)&my_addr, sizeof(struct sockaddr)) == SOCKET_ERROR)
    {
        closesocket(sockfd);
        return -1;
    }

    // set to broadcast mode
    ivalue = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, (char *)&ivalue, sizeof(ivalue)) == SOCKET_ERROR)
    {
        closesocket(sockfd);
        return -1;
    }

    return sockfd;
}

int main(int argc, char* argv[])
{
    int retval;
    char szMyIPAddress[128] = "";
    char szServerIPAddress[128] = "";
    in_addr MyAddress, MultiCastAddress;
    WSADATA WsaData; 
    int optval = 0x100000;
    int optval_size = 4;

    if (WSAStartup(0x202, &WsaData) == SOCKET_ERROR)
    {
		printf("[PacketClient] WSAStartup failed (error: %d)\n", WSAGetLastError());
        WSACleanup();
        return 0;
    }

	// server address
	if(argc>1)
	{
		strcpy_s(szServerIPAddress, argv[1]);	// specified on command line
	    retval = IPAddress_StringToAddr(szServerIPAddress, &ServerAddress);
	}
	else
	{
        GetLocalIPAddresses((unsigned long *)&ServerAddress, 1);
        sprintf_s(szServerIPAddress, "%d.%d.%d.%d", ServerAddress.S_un.S_un_b.s_b1, ServerAddress.S_un.S_un_b.s_b2, ServerAddress.S_un.S_un_b.s_b3, ServerAddress.S_un.S_un_b.s_b4);
	}

    // client address
	if(argc>2)
	{
		strcpy_s(szMyIPAddress, argv[2]);	// specified on command line
	    retval = IPAddress_StringToAddr(szMyIPAddress, &MyAddress);
	}
	else
	{
        GetLocalIPAddresses((unsigned long *)&MyAddress, 1);
        sprintf_s(szMyIPAddress, "%d.%d.%d.%d", MyAddress.S_un.S_un_b.s_b1, MyAddress.S_un.S_un_b.s_b2, MyAddress.S_un.S_un_b.s_b3, MyAddress.S_un.S_un_b.s_b4);
    }
  	MultiCastAddress.S_un.S_addr = inet_addr(MULTICAST_ADDRESS);   
    printf("Client: %s\n", szMyIPAddress);
    printf("Server: %s\n", szServerIPAddress);
    printf("Multicast Group: %s\n", MULTICAST_ADDRESS);

    // create "Command" socket
    int port = 0;
    CommandSocket = CreateCommandSocket(MyAddress.S_un.S_addr,port);
    if(CommandSocket == -1)
    {
        // error
    }
    else
    {
        // [optional] set to non-blocking
        //u_long iMode=1;
        //ioctlsocket(CommandSocket,FIONBIO,&iMode); 
        // set buffer
        setsockopt(CommandSocket, SOL_SOCKET, SO_RCVBUF, (char *)&optval, 4);
        getsockopt(CommandSocket, SOL_SOCKET, SO_RCVBUF, (char *)&optval, &optval_size);
        if (optval != 0x100000)
        {
            // err - actual size...
        }
        // startup our "Command Listener" thread
        SECURITY_ATTRIBUTES security_attribs;
        security_attribs.nLength = sizeof(SECURITY_ATTRIBUTES);
        security_attribs.lpSecurityDescriptor = NULL;
        security_attribs.bInheritHandle = TRUE;
        DWORD CommandListenThread_ID;
        HANDLE CommandListenThread_Handle;
        CommandListenThread_Handle = CreateThread( &security_attribs, 0, CommandListenThread, NULL, 0, &CommandListenThread_ID);
    }

    // create a "Data" socket
    DataSocket = socket(AF_INET, SOCK_DGRAM, 0);

    // allow multiple clients on same machine to use address/port
    int value = 1;
    retval = setsockopt(DataSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&value, sizeof(value));
    if (retval == SOCKET_ERROR)
    {
        closesocket(DataSocket);
        return -1;
    }

    struct sockaddr_in MySocketAddr;
    memset(&MySocketAddr, 0, sizeof(MySocketAddr));
    MySocketAddr.sin_family = AF_INET;
    MySocketAddr.sin_port = htons(PORT_DATA);
    MySocketAddr.sin_addr = MyAddress; 
    if (bind(DataSocket, (struct sockaddr *)&MySocketAddr, sizeof(struct sockaddr)) == SOCKET_ERROR)
    {
		printf("[PacketClient] bind failed (error: %d)\n", WSAGetLastError());
        WSACleanup();
        return 0;
    }
    // join multicast group
    struct ip_mreq Mreq;
    Mreq.imr_multiaddr = MultiCastAddress;
    Mreq.imr_interface = MyAddress;
    retval = setsockopt(DataSocket, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&Mreq, sizeof(Mreq));
    if (retval == SOCKET_ERROR)
    {
        printf("[PacketClient] join failed (error: %d)\n", WSAGetLastError());
        WSACleanup();
        return -1;
    }
	// create a 1MB buffer
    setsockopt(DataSocket, SOL_SOCKET, SO_RCVBUF, (char *)&optval, 4);
    getsockopt(DataSocket, SOL_SOCKET, SO_RCVBUF, (char *)&optval, &optval_size);
    if (optval != 0x100000)
    {
        printf("[PacketClient] ReceiveBuffer size = %d", optval);
    }
    // startup our "Data Listener" thread
    SECURITY_ATTRIBUTES security_attribs;
    security_attribs.nLength = sizeof(SECURITY_ATTRIBUTES);
    security_attribs.lpSecurityDescriptor = NULL;
    security_attribs.bInheritHandle = TRUE;
    DWORD DataListenThread_ID;
    HANDLE DataListenThread_Handle;
    DataListenThread_Handle = CreateThread( &security_attribs, 0, DataListenThread, NULL, 0, &DataListenThread_ID);

    
    // server address for commands
    memset(&HostAddr, 0, sizeof(HostAddr));
    HostAddr.sin_family = AF_INET;        
    HostAddr.sin_port = htons(PORT_COMMAND); 
    HostAddr.sin_addr = ServerAddress;

    // send initial connect request
    sPacket PacketOut;
    PacketOut.iMessage = NAT_CONNECT;
    PacketOut.nDataBytes = 0;
    int nTries = 3;
    while (nTries--)
    {
        int iRet = sendto(CommandSocket, (char *)&PacketOut, 4 + PacketOut.nDataBytes, 0, (sockaddr *)&HostAddr, sizeof(HostAddr));
        if(iRet != SOCKET_ERROR)
            break;
    }


    printf("Packet Client started\n\n");
    printf("Commands:\ns\tsend data descriptions\nf\tsend frame of data\nt\tsend test request\nq\tquit\n\n");
    int c;
    char szRequest[512];
    bool bExit = false;
    nTries = 3;
    while (!bExit)
    {
        c =_getch();
        switch(c)
        {
        case 's':
            // send NAT_REQUEST_MODELDEF command to server (will respond on the "Command Listener" thread)
            PacketOut.iMessage = NAT_REQUEST_MODELDEF;
            PacketOut.nDataBytes = 0;
            nTries = 3;
            while (nTries--)
            {
                int iRet = sendto(CommandSocket, (char *)&PacketOut, 4 + PacketOut.nDataBytes, 0, (sockaddr *)&HostAddr, sizeof(HostAddr));
                if(iRet != SOCKET_ERROR)
                    break;
            }
            break;	
        case 'f':
            // send NAT_REQUEST_FRAMEOFDATA (will respond on the "Command Listener" thread)
            PacketOut.iMessage = NAT_REQUEST_FRAMEOFDATA;
            PacketOut.nDataBytes = 0;
            nTries = 3;
            while (nTries--)
            {
                int iRet = sendto(CommandSocket, (char *)&PacketOut, 4 + PacketOut.nDataBytes, 0, (sockaddr *)&HostAddr, sizeof(HostAddr));
                if(iRet != SOCKET_ERROR)
                    break;
            }
            break;	
        case 't':
            // send NAT_MESSAGESTRING (will respond on the "Command Listener" thread)
            strcpy(szRequest, "TestRequest");
            PacketOut.iMessage = NAT_REQUEST;
            PacketOut.nDataBytes = (int)strlen(szRequest) + 1;
            strcpy(PacketOut.Data.szData, szRequest);
            nTries = 3;
            while (nTries--)
            {
                int iRet = sendto(CommandSocket, (char *)&PacketOut, 4 + PacketOut.nDataBytes, 0, (sockaddr *)&HostAddr, sizeof(HostAddr));
                if(iRet != SOCKET_ERROR)
                    break;
            }
            break;	
        case 'w':
            {
                char szCommand[512];
                long testVal;
                int returnCode;
                
                testVal = -50;
                sprintf(szCommand, "SetPlaybackStartFrame,%d",testVal);
                returnCode = SendCommand(szCommand);

                testVal = 1500;
                sprintf(szCommand, "SetPlaybackStopFrame,%d",testVal);
                returnCode = SendCommand(szCommand);

                testVal = 0;
                sprintf(szCommand, "SetPlaybackLooping,%d",testVal);
                returnCode = SendCommand(szCommand);
                
                testVal = 100;
                sprintf(szCommand, "SetPlaybackCurrentFrame,%d",testVal);
                returnCode = SendCommand(szCommand);

            }
            break;
        case 'q':
            bExit = true;		
            break;	
        default:
            break;
        }
    }

    return 0;
}

// Send a command to Motive.  
int SendCommand(char* szCommand)
{
    // reset global result
    gCommandResponse = -1;

    // format command packet
    sPacket commandPacket;
    strcpy(commandPacket.Data.szData, szCommand);
    commandPacket.iMessage = NAT_REQUEST;
    commandPacket.nDataBytes = (int)strlen(commandPacket.Data.szData) + 1;

    // send command, and wait (a bit) for command response to set global response var in CommandListenThread
    int iRet = sendto(CommandSocket, (char *)&commandPacket, 4 + commandPacket.nDataBytes, 0, (sockaddr *)&HostAddr, sizeof(HostAddr));
    if(iRet == SOCKET_ERROR)
    {
        printf("Socket error sending command");
    }
    else
    {
        int waitTries = 5;
        while (waitTries--)
        {
            if(gCommandResponse != -1)
                break;
            Sleep(30);
        }

        if(gCommandResponse == -1)
        {
            printf("Command response not received (timeout)");
        }
        else if(gCommandResponse == 0)
        {
            printf("Command response received with success");
        }
        else if(gCommandResponse > 0)
        {
            printf("Command response received with errors");
        }
    }

    return gCommandResponse;
}

// Convert IP address string to address
bool IPAddress_StringToAddr(char *szNameOrAddress, struct in_addr *Address)
{
	int retVal;
	struct sockaddr_in saGNI;
	char hostName[256];
	char servInfo[256];
	u_short port;
	port = 0;

	// Set up sockaddr_in structure which is passed to the getnameinfo function
	saGNI.sin_family = AF_INET;
	saGNI.sin_addr.s_addr = inet_addr(szNameOrAddress);
	saGNI.sin_port = htons(port);

	// getnameinfo in WS2tcpip is protocol independent and resolves address to ANSI host name
	if ((retVal = getnameinfo((SOCKADDR *)&saGNI, sizeof(sockaddr), hostName, 256, servInfo, 256, NI_NUMERICSERV)) != 0)
	{
        // Returns error if getnameinfo failed
        printf("[PacketClient] GetHostByAddr failed. Error #: %ld\n", WSAGetLastError());
		return false;
	}

    Address->S_un.S_addr = saGNI.sin_addr.S_un.S_addr;
	
    return true;
}

// get ip addresses on local host
int GetLocalIPAddresses(unsigned long Addresses[], int nMax)
{
    unsigned long  NameLength = 128;
    char szMyName[1024];
    struct addrinfo aiHints;
	struct addrinfo *aiList = NULL;
    struct sockaddr_in addr;
    int retVal = 0;
    char* port = "0";
    
    if(GetComputerName(szMyName, &NameLength) != TRUE)
    {
        printf("[PacketClient] get computer name  failed. Error #: %ld\n", WSAGetLastError());
        return 0;       
    };

	memset(&aiHints, 0, sizeof(aiHints));
	aiHints.ai_family = AF_INET;
	aiHints.ai_socktype = SOCK_DGRAM;
	aiHints.ai_protocol = IPPROTO_UDP;

    // Take ANSI host name and translates it to an address
	if ((retVal = getaddrinfo(szMyName, port, &aiHints, &aiList)) != 0) 
	{
        printf("[PacketClient] getaddrinfo failed. Error #: %ld\n", WSAGetLastError());
        return 0;
	}

    memcpy(&addr, aiList->ai_addr, aiList->ai_addrlen);
    freeaddrinfo(aiList);
    Addresses[0] = addr.sin_addr.S_un.S_addr;

    return 1;
}
#else

void buildConnectPacket(std::vector<char> &buffer)
{
    sPacket packet;
    packet.iMessage = NAT_CONNECT;
    packet.nDataBytes = 0;
    buffer.resize(4);
    memcpy(buffer.data(), &packet, 4);
}

void UnpackCommand(char *pData)
{
    const sPacket *replyPacket = reinterpret_cast<const sPacket *>(pData);

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
        for (int i = 0; i < 4; i++)
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

#endif

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
            strcpy_s(szName, ptr);
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
        //printf("Unidentified Marker Count : %d\n", nOtherMarkers);
        for(int j=0; j < nOtherMarkers; j++)
        {
            float x = 0.0f; memcpy(&x, ptr, 4); ptr += 4;
            float y = 0.0f; memcpy(&y, ptr, 4); ptr += 4;
            float z = 0.0f; memcpy(&z, ptr, 4); ptr += 4;
            
			// Deprecated
			//printf("\tMarker %d : pos = [%3.2f,%3.2f,%3.2f]\n",j,x,y,z);
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
                strcpy_s(szName, ptr);
                int nDataBytes = (int) strlen(szName) + 1;
                ptr += nDataBytes;
                printf("Markerset Name: %s\n", szName);

        	    // marker data
                int nMarkers = 0; memcpy(&nMarkers, ptr, 4); ptr += 4;
                printf("Marker Count : %d\n", nMarkers);

                for(int j=0; j < nMarkers; j++)
                {
                    char szName[256];
                    strcpy_s(szName, ptr);
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
