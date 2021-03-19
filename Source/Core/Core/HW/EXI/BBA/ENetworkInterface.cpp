// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "Common/Assert.h"
#include "Common/Logging/Log.h"
#include "Common/MsgHandler.h"
#include "Common/StringUtil.h"
#include "Core/HW/EXI/EXI_Device.h"
#include "Core/HW/EXI/EXI_DeviceEthernet.h"

#include "VideoCommon/OnScreenDisplay.h"


namespace ExpansionInterface
{
bool CEXIETHERNET::ENetworkInterface::Activate()
{
    if(IsActivated())
        return true;

    m_host = enet_host_create(nullptr, 1, (std::size_t)Channel::COUNT, 0, 0);
    if(!host_)
    {
      ERROR_LOG_FMT(SP1, "Couldn't open %interface_name% UDP socket, unable to initialize BBA");
      return false;
    }

    ENetAddress addr;
    if(!enet_address_set_host(&addr, m_dest_ip.c_str()))
    {
      ERROR_LOG_FMT(SP1, "Couldn't resolve %interface_name% hostname/IP, unable to initialize BBA");
      return false;
    }
    addr.port = dest_port_;

    m_peer = enet_host_connect(m_host, &addr, (std::size_t)Channel::COUNT, 0);
    if(!m_peer)
    {
      ERROR_LOG_FMT(SP1, "Couldn't connect to %interface_name% server, unable to initialize BBA");
      return false;
    }

    return RecvInit();
}

void CEXIETHERNET::ENetworkInterface::Deactivate()
{
  m_stop_service_thread.Set();
  m_service_thread.join();

  enet_peer_disconnect_now(m_peer, 0);
  enet_host_flush(m_host);
  enet_peer_reset(m_peer);
  enet_host_destroy(m_host);

  m_read_enabled.Clear();
}

bool CEXIETHERNET::ENetworkInterface::IsActivated()
{
    return (m_host != nullptr);
}

bool CEXIETHERNET::ENetworkInterface::SendFrame(const u8* frame, u32 size)
{
  if(!m_connected.IsSet())
  {
    // @todo OSD

    return true;
  }
  return Send(frame, size, Channel::ETH_FRAME);
}

bool CEXIETHERNET::ENetworkInterface::RecvInit()
{
    m_service_thread = std::thread(&ENetworkInterface::ServiceThread, this);
    return true;
}

void CEXIETHERNET::ENetworkInterface::RecvStart()
{
  m_read_enabled.Set();
}

void CEXIETHERNET::ENetworkInterface::RecvStop()
{
  m_read_enabled.Clear();
}

bool CEXIETHERNET::ENetworkInterface::Send(const u8* data, u32 size, Channel channel)
{
  ENetPacket* packet{enet_packet_create(data, size, ChannelFlags(channel))};
  if(!packet)
    return false;

  return (enet_peer_send(m_peer, (u8)channel, packet) == 0);
}

void CEXIETHERNET::ENetworkInterface::HandleEthFramePacket(const ENetPacket& packet)
{
  if(!m_read_enabled.IsSet() || packet.dataLength > BBA_RECV_SIZE)
    return;

  std::copy_n(packet.data, packet.dataLength, m_eth_ref->mRecvBuffer.get());
  m_eth_ref->mRecvBufferLength = packet.dataLength;
  m_eth_ref->RecvHandlePacket();
}

void CEXIETHERNET::ENetworkInterface::HandleControlPacket(const ENetPacket& packet)
{

}

bool CEXIETHERNET::ENetworkInterface::SendConnectMsg(const Common::MACAddress& mac_addr, const std::string& name)
{
  std::array<u8, 24> msg_buf;
  msg_buf[0] = (u8)ControlMessage::CONNECT;
  std::copy_n(mac_addr.begin(), mac_addr.size(), msg_buf.begin() + 1);
  msg_buf[mac_addr.size() + 1] = std::min(16, name.size());
  std::copy_n(name.begin(), std::min(16, name.size()), msg_buf.begin() + mac_addr.size() + 2);

  return Send(msg_buf.data(), msg_buf.size(), Channel::CONTROL);
}

bool CEXIETHERNET::ENetworkInterface::SendDisconnectMsg()
{
  auto msg{(u8)ControlMessage::DISCONNECT};
  return Send(&msg, sizeof(msg), Channel::CONTROL);
}

void CEXIETHERNET::ENetworkInterface::ServiceThread(ENetworkInterface* this_ptr)
{
  while(!this_ptr->m_stop_service_thread.IsSet())
  {
    ENetEvent event;
    while (enet_host_service(m_host, &event, 200))
    {
      switch (event.type)
      {
        case ENET_EVENT_TYPE_CONNECT:
          m_connected.Set();
          break;
        case ENET_EVENT_TYPE_DISCONNECT:
          m_connected.Clear();
          break;
        case ENET_EVENT_TYPE_RECEIVE:
          if((Channel)event.channelID == Channel::ETH_FRAME)
            this_ptr->HandleEthFramePacket(*event.packet);
          else
            this_ptr->HandleControlPacket(*event.packet);
          enet_packet_destroy(event.packet);
          break;
        default:
          break;
      }
    }
  }
}

}  // namespace ExpansionInterface
