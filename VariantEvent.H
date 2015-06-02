/****************************************************************
 VariantEvent.H
 Copyright (C)2015 William H. Majoros (martiandna@gmail.com).
 This is OPEN SOURCE SOFTWARE governed by the Gnu General Public
 License (GPL) version 3, as described at www.opensource.org.
 ****************************************************************/
#ifndef INCL_VariantEvent_H
#define INCL_VariantEvent_H
#include <iostream>
#include "BOOM/String.H"
using namespace std;
using namespace BOOM;


enum VariantSignalType {
  VAR_SIG_ATG,
  VAR_SIG_TAG,
  VAR_SIG_GT,
  VAR_SIG_AG,
  VAR_SIG_INDEL
};
VariantSignalType varSigTypeFromString(const String &);

enum VariantEventType {
  VAR_EVENT_GAIN,
  VAR_EVENT_LOSS,
  VAR_EVENT_INDEL
};
VariantEventType varEventTypeFromString(const String &);

class VariantEvent {
public:
  VariantEvent(VariantSignalType,VariantEventType,int pos);
  VariantSignalType getSignalType() const;
  VariantEventType getEventType() const;
  int getPosition() const;
protected:
  VariantSignalType varSignalType;
  VariantEventType varEventType;
  int pos;
};

#endif
