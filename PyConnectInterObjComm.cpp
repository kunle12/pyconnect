/*
 *  pyconnect_interobjcomm.cc
 *  
 *
 *  Created by Xun Wang on 26/09/06.
 *  Copyright 2006 Xun Wang. All rights reserved.
 *
 */
#include "def.h"
#include "PyConnectInterObjComm.h"

namespace pyconnect {
PyConnectInterObjComm * PyConnectInterObjComm::s_pPyConnectInterObjComm = NULL;

PyConnectInterObjComm::PyConnectInterObjComm() :
  pOSubject_( NULL ),
  pOObserver_( NULL ),
  pMP_( NULL )
{
}

void PyConnectInterObjComm::init( OSubject ** pOSubject, OObserver ** pOObserver, MessageProcessor * pMP )
{
  pOSubject_ = pOSubject;
  pOObserver_ = pOObserver;
  pMP_ = pMP;
  setMP( pMP );
  pMP_->addCommObject( this );
}

PyConnectInterObjComm * PyConnectInterObjComm::instance()
{
  if (!s_pPyConnectInterObjComm)
    s_pPyConnectInterObjComm = new PyConnectInterObjComm();
    
  return s_pPyConnectInterObjComm;
}

void PyConnectInterObjComm::dataReceived( const ONotifyEvent & event )
{
  unsigned char * data = (unsigned char *)(event.Data( 0 ));
  // analyse incoming packet.
  switch (verifyNegotiationMsg( data, strlen( (char*)data ) )) {
#ifdef PYTHON_SERVER
    case pyconnect::MODULE_DISCOVERY:
      break;
#else
    case pyconnect::MODULE_DECLARE:
    case pyconnect::PEER_SERVER_DISCOVERY:
    case pyconnect::PEER_SERVER_MSG:
      break;
#endif
    default:
    {
      if (pMP_) {
        struct sockaddr_in dummy;
        pMP_->processInput( data, strlen( (char*)data ), dummy );
      }
    }
  }
  pOObserver_[event.ObsIndex()]->AssertReady( event.SenderID() );
}

void PyConnectInterObjComm::dataSendReady( const OReadyEvent & event )
{
  // currently doing nothing apart from debugging message

  //INFO_MSG( "PyConnectInterObjComm::dataSendReady: receive assertion ready\n" );

  QueuedMessages::iterator qiter = qMesgs_.find( event.SenderID() );
  if (qiter != qMesgs_.end() && qiter->second) {
    struct MsgQ * qList = qiter->second;
    qiter->second = qList->next;
    pOSubject_[event.SbjIndex()]->SetData( event.SenderID(), qList->msg, qList->len );
    pOSubject_[event.SbjIndex()]->NotifyObserver( event.SenderID() );
    delete [] qList->msg;
    delete qList;
  }
}

void PyConnectInterObjComm::dataPacketSender( const unsigned char * data, int size, bool broadcast )
{
  for (ObserverConstIterator oiter = pOSubject_[sbjDataSender]->begin();
       oiter != pOSubject_[sbjDataSender]->end(); oiter++)
  {
    QueuedMessages::iterator qiter = qMesgs_.find( oiter->GetObserverID() );
    if (pOSubject_[sbjDataSender]->IsReady( *oiter )) {
      if (qiter == qMesgs_.end() || qiter->second == NULL) { // no previously queued messages
        pOSubject_[sbjDataSender]->SetData( *oiter, data, size );
        pOSubject_[sbjDataSender]->NotifyObserver( *oiter );
        continue;
      }
      struct MsgQ * qList = qiter->second;
      qiter->second = qList->next;
      pOSubject_[sbjDataSender]->SetData( *oiter, qList->msg, qList->len );
      pOSubject_[sbjDataSender]->NotifyObserver( *oiter );
      delete [] qList->msg;
      delete qList;
    }
    MsgQ * newMsg = new MsgQ;
    newMsg->len = size;
    newMsg->next = NULL;
    newMsg->msg = new unsigned char[size];
    memcpy( newMsg->msg, data, size );
    if (qiter == qMesgs_.end()) { // create a new
      qMesgs_[oiter->GetObserverID()] = newMsg;
      continue;
    }
    struct MsgQ * qList = qiter->second;
    if (qList == NULL) {
      qiter->second = newMsg;
      continue;
    }
    int qMsgCount = 1;
    while (qList->next != NULL) {
      qList = qList->next;
      qMsgCount++;
    }
    if (qMsgCount > 100) {
      WARNING_MSG( "PyConnectInterObjComm::dataPacketSender: "
                  "more than 100 msg queue for an observer. system is "
                  "not stable!!!\n" );
    }
    qList->next = newMsg;
  }
}

void PyConnectInterObjComm::fini()
{
  for (ObserverConstIterator oiter = pOSubject_[sbjDataSender]->begin();
       oiter != pOSubject_[sbjDataSender]->end(); oiter++)
  {
    QueuedMessages::const_iterator qiter = qMesgs_.find( oiter->GetObserverID() );
    if (qiter != qMesgs_.end() && qiter->second) {
      WARNING_MSG( "PyConnectInterObjComm::fini: cleanup a non-empty queue\n" );
      struct MsgQ * qList = qiter->second;
      while (qList) {
        struct MsgQ * tmpQ = qList;
        delete [] qList->msg;
        qList = qList->next;
        delete tmpQ;
      }
    }
  }
  qMesgs_.clear();
}
} // namespace pyconnect
