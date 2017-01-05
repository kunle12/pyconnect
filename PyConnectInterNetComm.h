#ifndef pyconnect_netcomm_h_DEFINED
#define pyconnect_netcomm_h_DEFINED

#include <ant.h>
#include <IPAddress.h>
#include <UDPEndpointMsg.h>
#include <TCPEndpointMsg.h>
#include <map>
#include "PyConnectObjComm.h"

#define PYCONNECT_NETCOMM_PORT     37251
#define PYCONNECT_BROADCAST_IP     "255.255.255.255"
#define PYCONNECT_UDP_BUFFER_SIZE  2048
#define PYCONNECT_TCP_BUFFER_SIZE  2048
#define PYCONNECT_MAX_TCP_SESSION  50
#define IPCONFIG_FILE           "/MS/OPEN-R/SYSTEM/CONF/WLANCONF.TXT"
#define IP_ADDRESS_TOKEN        "ETHER_IP"
#define IP_NETMASK_TOKEN        "ETHER_NETMASK"

#define PYCONNECT_INTERNETCOMM_DECLARE  \
  void TCPConnectCont( ANTENVMSG msg ) \
    { return PyConnectInterNetComm::instance()->TCPConnectCont( msg ); }   \
  void TCPListenCont( ANTENVMSG msg ) \
    { return PyConnectInterNetComm::instance()->TCPListenCont( msg ); }   \
  void TCPSendCont( ANTENVMSG msg ) \
    { return PyConnectInterNetComm::instance()->TCPSendCont( msg ); }  \
  void TCPReceiveCont( ANTENVMSG msg ) \
    { return PyConnectInterNetComm::instance()->TCPReceiveCont( msg ); }  \
  void TCPCloseCont( ANTENVMSG msg ) \
    { return PyConnectInterNetComm::instance()->TCPCloseCont( msg ); }  \
  void UDPSendCont( ANTENVMSG msg ) \
    { return PyConnectInterNetComm::instance()->UDPSendCont( msg ); }  \
  void UDPReceiveCont( ANTENVMSG msg ) \
    { return PyConnectInterNetComm::instance()->UDPReceiveCont( msg ); }  \
  void UDPCloseCont( ANTENVMSG msg ) \
    { return PyConnectInterNetComm::instance()->UDPCloseCont( msg ); }  \

#define PYCONNECT_INTERNETCOMM_INIT  \
  PyConnectInterNetComm::instance()->init( this->myOID_, this->ipstackRef_, this->getMP() )

#define PYCONNECT_INTERNETCOMM_INIT_WITH_PORT( port )  \
  PyConnectInterNetComm::instance()->init( this->myOID_, this->ipstackRef_, this->getMP(), port )

#define PYCONNECT_INTERNETCOMM_FINI  \
  PyConnectInterNetComm::instance()->fini()

namespace pyconnect {

typedef enum {
  IPProtocol = 0,
  TCPProtocol,
  UDPProtocol
} ProtocolType;

class NETConnection {
public:
  antModuleRef     endpoint;
  // ConnectionState  state;

  // send buffer
  antSharedBuffer  sendBuffer;
  byte*            sendData;
  int              sendSize;
  // receive buffer
  antSharedBuffer  recvBuffer;
  byte*            recvData;
  int              recvSize;
  ProtocolType   protocol() { return protType_; }

protected:
  ProtocolType  protType_;
};

class TCPChannel : public NETConnection
{
public:
  TCPChannel() { protType_ = TCPProtocol; }
};

class UDPChannel : public NETConnection
{
public:
  UDPChannel() { protType_ = UDPProtocol; }
};

class BufferedMessage {
public:
  BufferedMessage( UDPEndpointReceiveMsg * mesg ); // ATM, just for UDP messages.
  ~BufferedMessage();

  char * data() { return data_; }
  int length() { return msgSize_; }
private:
  char * data_;
  int  msgSize_;
};

class PyConnectInterNetComm : public ObjectComm
{
public:
  static PyConnectInterNetComm * instance();
  void init( const OID & myOID, const antStackRef & ipstackRef, 
    MessageProcessor * pMP, short port = PYCONNECT_NETCOMM_PORT );
  void dataPacketSender( const unsigned char * data, int size, bool broadcast = false );
  void fini();

  void TCPConnectCont( ANTENVMSG msg );
  void TCPListenCont( ANTENVMSG msg );
  void TCPSendCont( ANTENVMSG msg );
  void TCPReceiveCont( ANTENVMSG msg );
  void TCPCloseCont( ANTENVMSG msg );
  void UDPSendCont( ANTENVMSG msg );
  void UDPReceiveCont( ANTENVMSG msg );
  void UDPCloseCont( ANTENVMSG msg );
  
private:
  typedef std::map<int, TCPChannel *>  TCPChannels;
  typedef std::map<IPAddress, BufferedMessage *> BufferedUDPMsgs;

  PyConnectInterNetComm();
  void initCommNode();
  TCPChannel * createTCPChannel();
  void createTCPListener();
  void createTCPTalker( IPAddress & saddr, int port );
  void TCPReceive( int index );  
  void TCPSend( const unsigned char * data, int size );
  void closeTCPChannel( int index );
  void closeTCPChannel( TCPChannel * chan );

  bool initUDPChannel();
  void UDPReceive();
  void UDPSend( const unsigned char * data, int size );
  void closeUDPChannel();
  
  bool initNetBuffer( NETConnection * conn, int bufferSize );
  void destroyNetBuffer( NETConnection * conn );

  void updateMPID();
  bool getIDFromIP( int & addr );
  static PyConnectInterNetComm *  s_pPyConnectInterNetComm;

  MessageProcessor * pMP_;
  antStackRef ipstackRef_;
  UDPChannel  udpChannel_;
  TCPChannels tcpChannels_;

  BufferedUDPMsgs  bufferedUDPMsgs_;
  
  int       nextTCPId_;
  OID       oObjectOID_;
  short     portInUse_;
};
} // namespace pyconnect

#endif  // pyconnect_netcomm_h_DEFINED
