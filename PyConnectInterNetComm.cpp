#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <fstream>
#include <EndpointTypes.h>
#include <TCPEndpointMsg.h>
#include <UDPEndpointMsg.h>

#include "PyConnectInterNetComm.h"
#include "PyConnectCommon.h"
#include "entry.h"

namespace pyconnect {

PyConnectInterNetComm * PyConnectInterNetComm::s_pPyConnectInterNetComm = NULL;

void PyConnectInterNetComm::init( const OID & myOID, const antStackRef & ipstackRef,
             MessageProcessor * pMP, short port )
{
  oObjectOID_ = myOID;
  ipstackRef_ = ipstackRef;
  portInUse_ = port;
  pMP_ = pMP;
  setMP( pMP );
  pMP_->addCommObject( this );
  updateMPID();
  initCommNode();
}

PyConnectInterNetComm * PyConnectInterNetComm::instance()
{
  if (!s_pPyConnectInterNetComm)
    s_pPyConnectInterNetComm = new PyConnectInterNetComm();
  return s_pPyConnectInterNetComm;
}

PyConnectInterNetComm::PyConnectInterNetComm() :
  ObjectComm(),
  pMP_( NULL ),
  nextTCPId_( 1 ),
  portInUse_( PYCONNECT_NETCOMM_PORT )
{
}

void PyConnectInterNetComm::initCommNode()
{
  // initiate a UDP listening
  initUDPChannel();
#ifdef PYTHON_SERVER
  this->UDPReceive();
#endif
  createTCPListener();
}

bool PyConnectInterNetComm::initNetBuffer( NETConnection * conn, int bufferSize )
{
  // 
  // Allocate send buffer
  //
  antEnvCreateSharedBufferMsg sendBufferMsg( bufferSize );

  sendBufferMsg.Call( ipstackRef_, sizeof( sendBufferMsg ) );
  if (sendBufferMsg.error != ANT_SUCCESS) {
    ERROR_MSG( "%s : %s antError %d", "PyConnectInterNetComm::initNetBuffer()",
        "Can't allocate send buffer", sendBufferMsg.error );
    return false;
  }
  //
  // Allocate receive buffer
  //
  antEnvCreateSharedBufferMsg recvBufferMsg( bufferSize );

  recvBufferMsg.Call( ipstackRef_, sizeof( recvBufferMsg ) );
  if (recvBufferMsg.error != ANT_SUCCESS) {
    ERROR_MSG( "%s : %s antError %d", "PyConnectInterNetComm::initNetBuffer()",
        "Can't allocate receive buffer", recvBufferMsg.error );
    return false;
  }

  conn->sendBuffer = sendBufferMsg.buffer;
  conn->sendBuffer.Map();
  conn->sendData = (byte*)(conn->sendBuffer.GetAddress());
  conn->sendSize = bufferSize;

  conn->recvBuffer = recvBufferMsg.buffer;
  conn->recvBuffer.Map();
  conn->recvData = (byte*)(conn->recvBuffer.GetAddress());
  conn->recvSize = bufferSize;

  return true;
}

void PyConnectInterNetComm::destroyNetBuffer( NETConnection * conn )
{
  conn->sendBuffer.UnMap();
  antEnvDestroySharedBufferMsg destroySendBufferMsg( conn->sendBuffer );
  destroySendBufferMsg.Call( ipstackRef_, sizeof(destroySendBufferMsg) );

  // UnMap and Destroy RecvBuffer
  conn->recvBuffer.UnMap();
  antEnvDestroySharedBufferMsg destroyRecvBufferMsg( conn->recvBuffer );
  destroyRecvBufferMsg.Call( ipstackRef_,  sizeof( destroyRecvBufferMsg ) );
}

void PyConnectInterNetComm::createTCPListener()
{
  TCPChannel * chan = createTCPChannel();
  
  if (chan) {
    TCPEndpointListenMsg listenMsg( chan->endpoint,
                    IP_ADDR_ANY, portInUse_ );
    listenMsg.continuation = (void*)nextTCPId_;

    listenMsg.Send( ipstackRef_, oObjectOID_,
                   Extra_Entry[entryTCPListenCont], sizeof( TCPEndpointListenMsg ) );
    tcpChannels_[nextTCPId_++] = chan;
  }
}

void PyConnectInterNetComm::createTCPTalker( IPAddress & saddr, int port )
{
  TCPChannel * chan = createTCPChannel();
  
  if (chan) {
    char addr[20];
    saddr.GetAsString( addr );
    TCPEndpointConnectMsg connectMsg( chan->endpoint,
          IP_ADDR_ANY, IP_PORT_ANY, saddr, port );
    connectMsg.continuation = (void*)nextTCPId_;

    connectMsg.Send( ipstackRef_, oObjectOID_,
                    Extra_Entry[entryTCPConnectCont], sizeof( connectMsg ) );
    tcpChannels_[nextTCPId_++] = chan;
  }
}

TCPChannel * PyConnectInterNetComm::createTCPChannel()
{
  if (nextTCPId_ >= PYCONNECT_MAX_TCP_SESSION) {
    ERROR_MSG( "PyConnectInterNetComm::createTCPChannel: "
        "Unable to create new channel. Maximum channels reached!" );
    return NULL;
  }

  TCPChannel * chan = new TCPChannel();

  if (!initNetBuffer( chan, PYCONNECT_TCP_BUFFER_SIZE )) {
    ERROR_MSG( "%s : %s ",
        "PyConnectInterNetComm::createTCPChannel",
        "Can't create buffers" );
    delete chan;
    return NULL;
  }

  antEnvCreateEndpointMsg
    tcpCreateMsg( EndpointType_TCP, PYCONNECT_TCP_BUFFER_SIZE * 2 );

  tcpCreateMsg.Call( ipstackRef_, sizeof( tcpCreateMsg ) );
  if (tcpCreateMsg.error != ANT_SUCCESS) {
    ERROR_MSG( "%s : %s antError %d",
        "PyConnectInterNetComm::createTCPChannel",
        "Can't create endpoint",
        tcpCreateMsg.error );
    destroyNetBuffer( chan );
    delete chan;
    return NULL;
  }

  chan->endpoint = tcpCreateMsg.moduleRef;
  return chan;
}

void PyConnectInterNetComm::closeTCPChannel( int index )
{
  TCPChannels::iterator iter = tcpChannels_.find( index );
  if (iter != tcpChannels_.end() && iter->second) {
    TCPEndpointCloseMsg closeMsg( iter->second->endpoint );
    closeMsg.continuation = (void*)index;

    closeMsg.Send( ipstackRef_, oObjectOID_,
                  Extra_Entry[entryTCPCloseCont], sizeof( closeMsg ) );
    objCommChannelShutdown( index );
  }
}

void PyConnectInterNetComm::closeTCPChannel( TCPChannel * chan )
{
  if (chan) {
    TCPEndpointCloseMsg closeMsg( chan->endpoint );
    closeMsg.continuation = (void*)0; // don't care about index

    closeMsg.Call( ipstackRef_, sizeof( closeMsg ));
    destroyNetBuffer( chan );
    delete chan;
    chan = NULL;
  }
}

void PyConnectInterNetComm::TCPReceive( int index )
{
  TCPChannels::iterator iter = tcpChannels_.find( index );
  if (iter != tcpChannels_.end() && iter->second) {
    TCPEndpointReceiveMsg receiveMsg( iter->second->endpoint,
      iter->second->recvData, 1, PYCONNECT_TCP_BUFFER_SIZE );
    receiveMsg.continuation = (void*)index;

    receiveMsg.Send( ipstackRef_, oObjectOID_,
                    Extra_Entry[entryTCPReceiveCont], sizeof( receiveMsg ) );
  }
}

void PyConnectInterNetComm::TCPSend( const unsigned char * data, int size )
{
  int index = findOrAddCommChanByMsgID( data );

  if (index == INVALID_SOCKET) //probably not belong to current comm object
    return;

  TCPChannels::iterator iter = tcpChannels_.find( index );
  if (iter != tcpChannels_.end() && iter->second) {
    memcpy( (char *)iter->second->sendData, data, size );

    TCPEndpointSendMsg sendMsg( iter->second->endpoint,
          iter->second->sendData, size );
    sendMsg.continuation = (void*)index;
    sendMsg.Send( ipstackRef_, oObjectOID_,
                 Extra_Entry[entryTCPSendCont], sizeof( sendMsg ) );
  }
}

bool PyConnectInterNetComm::initUDPChannel()
{
  if (!initNetBuffer( &udpChannel_, PYCONNECT_UDP_BUFFER_SIZE )) {
    ERROR_MSG( "%s : %s antError", "PyConnectInterNetComm::initUDPChannel()",
        "Can't create buffers" );
    return false;
  }

  antEnvCreateEndpointMsg
    udpCreateMsg( EndpointType_UDP, PYCONNECT_UDP_BUFFER_SIZE * 2 );

  udpCreateMsg.Call( ipstackRef_, sizeof( udpCreateMsg ) );
  if (udpCreateMsg.error != ANT_SUCCESS) {
    ERROR_MSG( "%s : %s antError %d", "PyConnectInterNetComm::initUDPChannel()",
        "Can't create endpoint", udpCreateMsg.error );
    return false;
  }

  udpChannel_.endpoint = udpCreateMsg.moduleRef;

#ifdef PYTHON_SERVER
  // because OPEN-R does not fully support multiple binding
  // of same port (use either multicast or not). More than
  // one pythonised objects using network layer will have clashes.
  // Therefore, instead of allowing all objects lisstening on
  // possible incoming discovery messages. I only allow embedded
  // python server listens.
  UDPEndpointBindMsg bindMsg( udpChannel_.endpoint, IP_ADDR_ANY,
    PYCONNECT_NETCOMM_PORT );

  bindMsg.Call( ipstackRef_, sizeof( antEnvCreateEndpointMsg ) );
  if (bindMsg.error != UDP_SUCCESS) {
    ERROR_MSG( "%s : %s antError", "PyConnectInterNetComm::initUDPChannel()",
        "Can't bind to interface" );
    return false;
  }
#endif

  return true;
}

void PyConnectInterNetComm::UDPReceive()
{
  UDPEndpointReceiveMsg receiveMsg( udpChannel_.endpoint,
              udpChannel_.recvData,
              udpChannel_.recvSize );
  int index = 0;
  receiveMsg.continuation = (void *) index;

  receiveMsg.Send( ipstackRef_, oObjectOID_,
        Extra_Entry[entryUDPReceiveCont], sizeof( receiveMsg ) );
}

void PyConnectInterNetComm::UDPReceiveCont( ANTENVMSG msg )
{
  UDPEndpointReceiveMsg* receiveMsg
    = (UDPEndpointReceiveMsg*)antEnvMsg::Receive(msg);

  if (receiveMsg->error != UDP_SUCCESS) {
    ERROR_MSG( "%s : %s %d", "PyConnectInterNetComm::UDPReceiveCont()",
        "FAILED. receiveMsg->error", receiveMsg->error );
    return;
  }
  int index = (int)receiveMsg->continuation;

  int msgType = verifyNegotiationMsg( receiveMsg->buffer,
                                     receiveMsg->size );
  // analyse incoming packet.
  switch (msgType) {
#ifdef PYTHON_SERVER
    case pyconnect::MODULE_DISCOVERY:
      break;
    case pyconnect::PEER_SERVER_MSG:
    {
      if (pMP_) {
        struct sockaddr_in cAddr;
        cAddr.sin_family = AF_INET;
        cAddr.sin_addr.s_addr = receiveMsg->address.Address();
        cAddr.sin_port = receiveMsg->port;
        pMP_->processInput( receiveMsg->buffer, receiveMsg->size, cAddr );
      }
    }
      break;
    case pyconnect::MODULE_DECLARE:
    case pyconnect::PEER_SERVER_DISCOVERY:
#else
    case pyconnect::MODULE_DECLARE:
    case pyconnect::PEER_SERVER_DISCOVERY:
    case pyconnect::PEER_SERVER_MSG:
      break;
    case pyconnect::MODULE_DISCOVERY:
#endif
    {
      int port = 0;
      char * portStrPtr = (char *)&(receiveMsg->buffer[receiveMsg->size - sizeof( short ) - 1]);
      memcpy( (char *)&port, portStrPtr, sizeof( short ) );
      port = (port << 8 | port >> 8) & 0xffff;
      //DEBUG_MSG( "incoming port %d\n", port );
      createTCPTalker( receiveMsg->address, port );
      // delay process these message till we have created TCP connection
      // since any messages from now on will go through the TCP session
      // instead of the UDP.
      BufferedUDPMsgs::iterator bmIter = bufferedUDPMsgs_.find( receiveMsg->address );
      if (bmIter != bufferedUDPMsgs_.end()) { // should not happen
        char addr[20];
        receiveMsg->address.GetAsString( addr );
        if (bmIter->second)
          delete bmIter->second;

        WARNING_MSG( "PyConnectInterNetComm::UDPReceiveCont(): Another UDP "
          "connecting message from %s. Replace existing buffered message.\n",
          addr );
      }
      bufferedUDPMsgs_[receiveMsg->address] = new BufferedMessage( receiveMsg );
    }
      break;
    default:
      ERROR_MSG( "PyConnectInterNetComm::UDPReceiveCont: "
          "Unknown negotiation message %d\n", msgType );
      //return;
  }
  this->UDPReceive();
}

void PyConnectInterNetComm::UDPSend( const unsigned char * data, int size )
{
  if (!size) return;
  
  memcpy( (unsigned char *)udpChannel_.sendData, data, size );
  UDPEndpointSendMsg sendMsg( udpChannel_.endpoint,
                PYCONNECT_BROADCAST_IP,
                PYCONNECT_NETCOMM_PORT,
                udpChannel_.sendData,
                size );
  int index = 0;
  sendMsg.continuation = (void*)index;
  sendMsg.Send( ipstackRef_, oObjectOID_,
      Extra_Entry[entryUDPSendCont], sizeof( UDPEndpointSendMsg ) );
}

void PyConnectInterNetComm::UDPSendCont( ANTENVMSG msg )
{
  UDPEndpointSendMsg* sendMsg = (UDPEndpointSendMsg *)antEnvMsg::Receive( msg );

  if (sendMsg->error != UDP_SUCCESS) {
    ERROR_MSG( "%s : %s %d", "PyConnectInterNetComm::UDPSendCont()",
        "FAILED. sendMsg->error", sendMsg->error );
    return;
  }
}

void PyConnectInterNetComm::closeUDPChannel()
{
  UDPEndpointCloseMsg closeMsg(udpChannel_.endpoint);
  closeMsg.continuation = (void*)0;

  closeMsg.Call( ipstackRef_, sizeof(closeMsg) );
}

void PyConnectInterNetComm::UDPCloseCont( ANTENVMSG msg )
{
  // not doing much here.
  //UDPEndpointCloseMsg* closeMsg =
  //  (UDPEndpointCloseMsg*)antEnvMsg::Receive(msg);
}

void PyConnectInterNetComm::TCPListenCont( ANTENVMSG msg )
{
  TCPEndpointListenMsg* listenMsg
    = (TCPEndpointListenMsg*)antEnvMsg::Receive( msg );
  
  int index = (int)(listenMsg->continuation);
  if (listenMsg->error != TCP_SUCCESS) {
    ERROR_MSG( "%s : %s %d", "PyConnectInterNetComm::TCPListenCont",
        "FAILED. listenMsg->error", listenMsg->error );
    closeTCPChannel( index );
    return; // probably no point to create another listener.
  }
  TCPReceive( index );
  createTCPListener();
}

void PyConnectInterNetComm::TCPReceiveCont( ANTENVMSG msg )
{
  TCPEndpointReceiveMsg* receiveMsg
      = (TCPEndpointReceiveMsg*)antEnvMsg::Receive( msg );
  int index = (int)(receiveMsg->continuation);
  if (receiveMsg->error != TCP_SUCCESS) {
    ERROR_MSG( "%s : %s %d", "PyConnectInterNetComm::TCPReceiveCont()",
        "FAILED. receiveMsg->error", receiveMsg->error );
    closeTCPChannel( index );
    return;
  }

  setLastUsedCommChannel( index );

  TCPChannels::iterator iter = tcpChannels_.find( index );
  if (iter != tcpChannels_.end() && iter->second && pMP_) {
    struct sockaddr_in cAddr;
    pMP_->processInput( iter->second->recvData, receiveMsg->sizeMin, cAddr );
    TCPReceive( index );
  }
}

void PyConnectInterNetComm::TCPCloseCont( ANTENVMSG msg )
{
  TCPEndpointCloseMsg* closeMsg
    = (TCPEndpointCloseMsg*)antEnvMsg::Receive( msg );
  int index = (int)(closeMsg->continuation);

  TCPChannels::iterator iter = tcpChannels_.find( index );
  if (iter != tcpChannels_.end() && iter->second) {
    destroyNetBuffer( iter->second );
    delete iter->second;
    iter->second = NULL;
  }
}

void PyConnectInterNetComm::TCPSendCont( ANTENVMSG msg )
{
  TCPEndpointSendMsg* sendMsg =
    (TCPEndpointSendMsg*)antEnvMsg::Receive( msg );
  int index = (int)(sendMsg->continuation);

  if (sendMsg->error != TCP_SUCCESS) {
    ERROR_MSG( "%s : %s %d", "PyConnectInterNetComm::TCPSendCont()",
      "FAILED. sendMsg->error", sendMsg->error );
    closeTCPChannel( index );
    return;
  }
}

void PyConnectInterNetComm::TCPConnectCont( ANTENVMSG msg )
{
  TCPEndpointConnectMsg* connectMsg
    = (TCPEndpointConnectMsg*)antEnvMsg::Receive( msg );
  int index = (int)connectMsg->continuation;

  if (connectMsg->error != TCP_SUCCESS) {
    ERROR_MSG( "%s : %s %d", "PyConnectInterNetComm::TCPConnectCont()",
        "FAILED. connectMsg->error", connectMsg->error );
    closeTCPChannel( index );
    return;
  }
  // process earlier queued UDP connection messages.
  // set current taking channel since we going to send mesg immediately afterwards
  setLastUsedCommChannel( index );
  
  assert( !bufferedUDPMsgs_.empty() );
  BufferedUDPMsgs::iterator bmIter = 
    bufferedUDPMsgs_.find( connectMsg->fAddress );
  assert( bmIter!=bufferedUDPMsgs_.end() );
  BufferedMessage * bufMsg = bmIter->second;
  assert( bufMsg != NULL );

  bool goodData = true;
  if (pMP_) {
    struct sockaddr_in cAddr;
    cAddr.sin_addr.s_addr = connectMsg->fAddress.Address();
    cAddr.sin_port = 0;
    goodData = pMP_->processInput( (unsigned char*)bufMsg->data(), bufMsg->length(), cAddr );
  }
  delete bufMsg;
  bufferedUDPMsgs_.erase( bmIter );
  if (goodData)
    TCPReceive( index );
  else
    closeTCPChannel( index );
}

void PyConnectInterNetComm::dataPacketSender( const unsigned char * data, int size, bool broadcast )
{
  //TODO: consider to add commState_ back in for more detailed error handling.
//  if (commState_ == oFAIL)
//    return;
    
  if (broadcast) {
    // a hack job to pass on TCP port in use in declaration messages
    int msgType = verifyNegotiationMsg( data, size );
    if (msgType == pyconnect::MODULE_DISCOVERY ||
        msgType == pyconnect::MODULE_DECLARE)
    {
      unsigned char * modData = new unsigned char[size + sizeof( short )];
      memcpy( modData, data, size - 1 ); // exclude message end char
      unsigned char * modDataPtr = modData + size - 1;
      memcpy( modDataPtr, (char *)&portInUse_, sizeof( short ) );
      modDataPtr += sizeof( short );
      *modDataPtr = pyconnect::PYCONNECT_MSG_END;
      UDPSend( modData, size + sizeof( short ) );
      delete [] modData;
    }
    else {
      UDPSend( data, size );
    }
  }
  else {
    TCPSend( data, size );
  }
}

void PyConnectInterNetComm::updateMPID()
{
  int commAddr = 0;
  if (this->getIDFromIP( commAddr )) {
#ifdef PYTHON_SERVER
    INFO_MSG( "PythonServer: set server id to %d\n", commAddr );
#endif
    pMP_->updateMPID( commAddr );
  }
}

void PyConnectInterNetComm::fini()
{
  for (TCPChannels::iterator iter=tcpChannels_.begin();
       iter!=tcpChannels_.end(); iter++)
  {
    if (iter->second)
      closeTCPChannel( iter->second );
  }
  tcpChannels_.clear();
  resetObjCommChanMap();
  closeUDPChannel();
}

BufferedMessage::BufferedMessage( UDPEndpointReceiveMsg * mesg )
{
  msgSize_ = mesg->size - sizeof( short );
  data_ = new char[msgSize_];
  memcpy( data_, mesg->buffer, msgSize_ );
  data_[msgSize_-1] = pyconnect::PYCONNECT_MSG_END;
}

BufferedMessage::~BufferedMessage()
{
  if (data_) {
    delete [] data_;
    data_ = NULL;
  }
  msgSize_ = 0;
}

bool PyConnectInterNetComm::getIDFromIP( int & addr )
{
  // read in from configuration file
  std::ifstream infile;
  infile.open( IPCONFIG_FILE, std::ifstream::in );

  if (!infile.good()) {
    ERROR_MSG( "PyConnectInterNetComm::getMyIPAddress: Couldn't open %s.\n", IPCONFIG_FILE );
    return false;
  }

  bool goodAddr = false;
  
  std::string tmpStr;
	// if the file is readable
	while (infile.good()) {
    infile >> tmpStr;
    if (strstr( tmpStr.c_str(), IP_ADDRESS_TOKEN ) ) {
      char * lstDecimalPos = strrchr( tmpStr.c_str(), '.' );
      if (lstDecimalPos) {
        int myAddr = (int)strtol( lstDecimalPos + 1, (char **)NULL, 10 );
        if (myAddr == 0) {
          ERROR_MSG( "Invalid host id\n" );
          break;
        }
        else if (myAddr > 0xf) {
          WARNING_MSG( "host address %d is larger than %d, truncated to %d as ID\n",
                      myAddr, 0xf, myAddr & 0xf );
          myAddr &= 0xf;
        }
        addr = myAddr;
        goodAddr = true;
      }
      break;
    } 
	}
	infile.close();

  return goodAddr;
}
} // namespace pyconnect
