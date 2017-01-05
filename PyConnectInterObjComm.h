#ifndef pyconnect_interobjcomm_h_DEFINED
#define pyconnect_interobjcomm_h_DEFINED
#include <OPENR/OObject.h>
#include <OPENR/OSubject.h>
#include <OPENR/OObserver.h>

#include "PyConnectObjComm.h"
#include <map>

#define PYCONNECT_INTEROBJCOMM_DECLARE  \
  void dataSendReady( const OReadyEvent & event ) \
    { return PyConnectInterObjComm::instance()->dataSendReady( event ); }  \
  void dataReceived( const ONotifyEvent & event ) \
    { return PyConnectInterObjComm::instance()->dataReceived( event ); }  \
  void dummy1()

#define PYCONNECT_INTEROBJCOMM_INIT  \
  PyConnectInterObjComm::instance()->init( subject, observer, this->getMP() )

#define PYCONNECT_INTEROBJCOMM_FINI  \
  PyConnectInterObjComm::instance()->fini()

namespace pyconnect {

class PyConnectInterObjComm : public ObjectComm
{
public:
  void init( OSubject ** pOSubject, OObserver ** pOObserver, MessageProcessor * pMP );
  void dataPacketSender( const unsigned char * data, int size, bool broadcast = false );
  void dataReceived( const ONotifyEvent & event );
  void dataSendReady( const OReadyEvent & event );
  void fini();
  
  static PyConnectInterObjComm * instance();

private:
  struct MsgQ {
    unsigned char * msg;
    int len;
    struct MsgQ * next;
  } ;

  typedef std::map<ObserverID, MsgQ *> QueuedMessages;
  
  PyConnectInterObjComm();

  OSubject **  pOSubject_;
  OObserver ** pOObserver_;
  QueuedMessages qMesgs_;
  MessageProcessor * pMP_;
  static PyConnectInterObjComm * s_pPyConnectInterObjComm;
};
} // namespace pyconnect

#endif  // pyconnect_interobjcomm_h_DEFINED
