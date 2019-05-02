#include <cstdlib>
#include <cstring>
#include <iostream>
#include <boost/asio.hpp>

using boost::asio::ip::udp;

enum { max_length = 1024 };

const char* MULTICAST_ADDRESS = "239.255.42.99";
const int PORT_COMMAND = 1510;
const int PORT_DATA = 1511;

const int MAX_PACKETSIZE = 100000;  // max size of packet (actual packet size is dynamic)

// typedef struct
// {
//   uint16_t iMessage;                // message ID (e.g. NAT_FRAMEOFDATA)
//   uint16_t nDataBytes;              // Num bytes in payload
//   void* data;
// } sPacket;

#define MAX_NAMELENGTH              256

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

enum NatNetMessageID
{
  NAT_CONNECT              = 0,
  NAT_SERVERINFO           = 1,
  NAT_REQUEST              = 2,
  NAT_RESPONSE             = 3,
  NAT_REQUEST_MODELDEF     = 4,
  NAT_MODELDEF             = 5,
  NAT_REQUEST_FRAMEOFDATA  = 6,
  NAT_FRAMEOFDATA          = 7,
  NAT_MESSAGESTRING        = 8,
  NAT_UNRECOGNIZED_REQUEST = 100,
};

int main(int argc, char* argv[])
{
  try
  {
    if (argc != 2)
    {
      std::cerr << "Usage: blocking_udp_echo_client <host>\n";
      return 1;
    }

    boost::asio::io_service io_service;

    udp::socket s(io_service, udp::endpoint(udp::v4(), 0));

    udp::resolver resolver(io_service);
    udp::endpoint endpoint = *resolver.resolve({udp::v4(), argv[1], std::to_string(PORT_COMMAND)});

    sPacket packet;
    packet.iMessage = NAT_CONNECT;
    packet.nDataBytes = 0;
    s.send_to(boost::asio::buffer(&packet, 4), endpoint);

    std::vector<uint8_t> reply(MAX_PACKETSIZE);
    udp::endpoint sender_endpoint;
    size_t reply_length = s.receive_from(
        boost::asio::buffer(reply, MAX_PACKETSIZE), sender_endpoint);

    const sPacket* replyPacket = reinterpret_cast<const sPacket*>(reply.data());
    std::cout << replyPacket->iMessage << " " << replyPacket->nDataBytes << std::endl;



    int NatNetVersion[4] = {0,0,0,0};
    int ServerVersion[4] = {0,0,0,0};


    // handle command
        switch (replyPacket->iMessage)
        {
        // case NAT_MODELDEF:
        //     Unpack((char*)&PacketIn);
        //     break;
        // case NAT_FRAMEOFDATA:
        //     Unpack((char*)&PacketIn);
        //     break;
        case NAT_SERVERINFO:
            for(int i=0; i<4; i++)
            {
                NatNetVersion[i] = (int)replyPacket->Data.Sender.NatNetVersion[i];
                ServerVersion[i] = (int)replyPacket->Data.Sender.Version[i];
            }
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
        }

    std::cout << "NatNetVersion: " << NatNetVersion[0] << "." << NatNetVersion[1] << "." << NatNetVersion[2] << "." << NatNetVersion[3] << std::endl;
    std::cout << "ServerVersion: " << ServerVersion[0] << "." << ServerVersion[1] << "." << ServerVersion[2] << "." << ServerVersion[3] << std::endl;

  }
  catch (std::exception& e)
  {
    std::cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}