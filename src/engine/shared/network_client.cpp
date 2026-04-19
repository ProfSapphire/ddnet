/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "network.h"

#include <base/dbg.h>
#include <base/mem.h>
#include <base/net.h>
#include <base/str.h>
#include <base/time.h>
#include <base/types.h>

#include <engine/shared/protocolglue.h>
#include <engine/shared/protocol7.h>

bool CNetClient::Open(NETADDR BindAddr)
{
	// open socket
	NETSOCKET Socket;
	Socket = net_udp_create(BindAddr);
	if(!Socket)
		return false;
	Close();
	// clean it
	*this = CNetClient{};

	// init
	m_Socket = Socket;
	m_pStun = new CStun(m_Socket);
	m_Connection.Init(m_Socket, false);
	m_TokenCache.Init(m_Socket);

	return true;
}

void CNetClient::Close()
{
	if(!m_Socket)
	{
		return;
	}
	if(m_pStun)
	{
		delete m_pStun;
		m_pStun = nullptr;
	}
	net_udp_close(m_Socket);
	m_Socket = nullptr;
}

void CNetClient::Disconnect(const char *pReason)
{
	m_Connection.Disconnect(pReason);
}

void CNetClient::Update()
{
	m_Connection.Update();
	if(m_Connection.State() == CNetConnection::EState::ERROR)
		Disconnect(m_Connection.ErrorString());
	m_pStun->Update();
	m_TokenCache.Update();
}

void CNetClient::Connect(const NETADDR *pAddr, int NumAddrs)
{
	m_Connection.Connect(pAddr, NumAddrs);
}

void CNetClient::Connect7(const NETADDR *pAddr, int NumAddrs)
{
	m_Connection.Connect7(pAddr, NumAddrs);
}

void CNetClient::ResetErrorString()
{
	m_Connection.ResetErrorString();
}

int CNetClient::Recv(CNetChunk *pChunk, SECURITY_TOKEN *pResponseToken, bool Sixup)
{
	while(true)
	{
		// Unpack next chunk from stored packet if available
		if(m_PacketChunkUnpacker.UnpackNextChunk(pChunk))
			return 1;

		// TODO: empty the recvinfo
		NETADDR Addr;
		unsigned char *pData;
		int Bytes = net_udp_recv(m_Socket, &Addr, &pData);

		// no more packets for now
		if(Bytes <= 0)
			break;

		if(m_pStun->OnPacket(Addr, pData, Bytes))
		{
			continue;
		}

		SECURITY_TOKEN Token;
		*pResponseToken = NET_SECURITY_TOKEN_UNKNOWN;
		if(CNetBase::UnpackPacket(pData, Bytes, &m_RecvBuffer, Sixup, &Token, pResponseToken) == 0)
		{
			if(Sixup)
			{
				Addr.type |= NETTYPE_TW7;
			}
			if(m_RecvBuffer.m_Flags & NET_PACKETFLAG_CONNLESS)
			{
				pChunk->m_Flags = NETSENDFLAG_CONNLESS;
				pChunk->m_ClientId = -1;
				pChunk->m_Address = Addr;
				pChunk->m_DataSize = m_RecvBuffer.m_DataSize;
				pChunk->m_pData = m_RecvBuffer.m_aChunkData;
				if(m_RecvBuffer.m_Flags & NET_PACKETFLAG_EXTENDED)
				{
					pChunk->m_Flags |= NETSENDFLAG_EXTENDED;
					mem_copy(pChunk->m_aExtraData, m_RecvBuffer.m_aExtraData, sizeof(pChunk->m_aExtraData));
				}
				return 1;
			}
			else
			{
				const bool Control = (m_RecvBuffer.m_Flags & NET_PACKETFLAG_CONTROL) != 0;
				if(Sixup &&
					Control &&
					m_RecvBuffer.m_DataSize >= 1 + (int)sizeof(SECURITY_TOKEN) &&
					m_RecvBuffer.m_aChunkData[0] == protocol7::NET_CTRLMSG_TOKEN)
				{
					m_TokenCache.AddToken(&Addr, *pResponseToken);
				}
				if(m_Connection.State() != CNetConnection::EState::OFFLINE &&
					m_Connection.State() != CNetConnection::EState::ERROR &&
					m_Connection.Feed(&m_RecvBuffer, &Addr, Token, *pResponseToken))
				{
					if(!Control &&
						m_RecvBuffer.m_DataSize > 0 &&
						m_RecvBuffer.m_NumChunks > 0)
					{
						m_PacketChunkUnpacker.FeedPacket(Addr, m_RecvBuffer, &m_Connection, 0);
					}
				}
			}
		}
	}
	return 0;
}

int CNetClient::Send(CNetChunk *pChunk)
{
	if(pChunk->m_DataSize >= NET_MAX_PAYLOAD)
	{
		dbg_msg("netclient", "chunk payload too big. %d. dropping chunk", pChunk->m_DataSize);
		return -1;
	}

	if(pChunk->m_Flags & NETSENDFLAG_CONNLESS)
	{
		// send connectionless packet
		if(pChunk->m_Address.type & NETTYPE_TW7)
		{
			m_TokenCache.SendPacketConnless(pChunk);
		}
		else
		{
			CNetBase::SendPacketConnless(m_Socket, &pChunk->m_Address, pChunk->m_pData, pChunk->m_DataSize,
				pChunk->m_Flags & NETSENDFLAG_EXTENDED, pChunk->m_aExtraData);
		}
	}
	else
	{
		int Flags = 0;
		dbg_assert(pChunk->m_ClientId == 0, "erroneous client id");

		if(pChunk->m_Flags & NETSENDFLAG_VITAL)
			Flags = NET_CHUNKFLAG_VITAL;

		m_Connection.QueueChunk(Flags, pChunk->m_DataSize, pChunk->m_pData);

		if(pChunk->m_Flags & NETSENDFLAG_FLUSH)
			m_Connection.Flush();
	}
	return 0;
}

bool CNetClient::SendChunkHeaderTruncationProbe(char *pError, int ErrorSize)
{
	if(ErrorSize > 0)
	{
		pError[0] = '\0';
	}

	if(m_Connection.State() != CNetConnection::EState::ONLINE)
	{
		str_copy(pError, "Not connected to a server.", ErrorSize);
		return false;
	}

	const bool HasInlineToken = !m_Connection.m_Sixup && m_Connection.SecurityToken() != NET_SECURITY_TOKEN_UNSUPPORTED;
	if((m_Connection.m_Sixup || HasInlineToken) && m_Connection.SecurityToken() == NET_SECURITY_TOKEN_UNKNOWN)
	{
		str_copy(pError, "Connection security token is still unknown.", ErrorSize);
		return false;
	}

	unsigned char aPacket[NET_MAX_PACKETSIZE];
	mem_zero(aPacket, sizeof(aPacket));

	const int PacketHeaderSize = m_Connection.m_Sixup ? NET_PACKETHEADERSIZE + (int)sizeof(SECURITY_TOKEN) : NET_PACKETHEADERSIZE;
	const int LogicalDataSize = m_Connection.m_Sixup ?
		NET_MAX_PACKETSIZE - PacketHeaderSize :
		NET_MAX_PAYLOAD - (HasInlineToken ? (int)sizeof(SECURITY_TOKEN) : 0);
	const int HeaderSplit = m_Connection.m_Sixup ? 6 : 4;
	const int ChunkHeaderSize = 2;

	// Serialize two valid chunks that exactly consume the available chunk payload,
	// but advertise three chunks in the packet header. The third header is missing,
	// so the receiver will try to unpack a header at the end of the logical packet.
	const int FirstChunkSize = NET_MAX_CHUNK_SIZE;
	const int SecondChunkSize = LogicalDataSize - (ChunkHeaderSize + FirstChunkSize) - ChunkHeaderSize;
	if(SecondChunkSize < 0 || SecondChunkSize > NET_MAX_CHUNK_SIZE)
	{
		str_copy(pError, "Could not build the truncation probe packet.", ErrorSize);
		return false;
	}

	unsigned char *pWrite = aPacket + PacketHeaderSize;
	CNetChunkHeader FirstHeader;
	FirstHeader.m_Flags = 0;
	FirstHeader.m_Size = FirstChunkSize;
	FirstHeader.m_Sequence = 0;
	pWrite = FirstHeader.Pack(pWrite, HeaderSplit);
	pWrite += FirstChunkSize;

	CNetChunkHeader SecondHeader;
	SecondHeader.m_Flags = 0;
	SecondHeader.m_Size = SecondChunkSize;
	SecondHeader.m_Sequence = 0;
	pWrite = SecondHeader.Pack(pWrite, HeaderSplit);
	pWrite += SecondChunkSize;

	int Flags = 0;
	if(m_Connection.m_Sixup)
	{
		Flags = PacketFlags_SixToSeven(Flags);
		WriteSecurityToken(aPacket + NET_PACKETHEADERSIZE, m_Connection.SecurityToken());
	}

	const int Ack = m_Connection.AckSequence();
	aPacket[0] = ((Flags << 2) & 0xfc) | ((Ack >> 8) & 0x3);
	aPacket[1] = Ack & 0xff;
	aPacket[2] = 3;

	if(HasInlineToken)
	{
		WriteSecurityToken(aPacket + PacketHeaderSize + LogicalDataSize, m_Connection.SecurityToken());
	}

	const int PacketSize = PacketHeaderSize + LogicalDataSize + (HasInlineToken ? (int)sizeof(SECURITY_TOKEN) : 0);
	net_udp_send(m_Socket, const_cast<NETADDR *>(m_Connection.PeerAddress()), aPacket, PacketSize);
	return true;
}

int CNetClient::State()
{
	if(m_Connection.State() == CNetConnection::EState::ONLINE)
		return NETSTATE_ONLINE;
	if(m_Connection.State() == CNetConnection::EState::OFFLINE)
		return NETSTATE_OFFLINE;
	return NETSTATE_CONNECTING;
}

int CNetClient::Flush()
{
	return m_Connection.Flush();
}

bool CNetClient::GotProblems(int64_t MaxLatency) const
{
	return time_get() - m_Connection.LastRecvTime() > MaxLatency;
}

const char *CNetClient::ErrorString() const
{
	return m_Connection.ErrorString();
}

void CNetClient::FeedStunServer(NETADDR StunServer)
{
	m_pStun->FeedStunServer(StunServer);
}

void CNetClient::RefreshStun()
{
	m_pStun->Refresh();
}

CONNECTIVITY CNetClient::GetConnectivity(int NetType, NETADDR *pGlobalAddr)
{
	return m_pStun->GetConnectivity(NetType, pGlobalAddr);
}
