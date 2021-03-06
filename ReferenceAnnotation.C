/****************************************************************
 ReferenceAnnotation.C
 Copyright (C)2015 William H. Majoros (martiandna@gmail.com).
 This is OPEN SOURCE SOFTWARE governed by the Gnu General Public
 License (GPL) version 3, as described at www.opensource.org.
 ****************************************************************/
#include <iostream>
#include "BOOM/GffReader.H"
#include "BOOM/VectorSorter.H"
#include "ReferenceAnnotation.H"
using namespace std;
using namespace BOOM;

ReferenceAnnotation::ReferenceAnnotation()
  : matrix(NULL), contentRegions(NULL)
{
  // ctor
}



ReferenceAnnotation::ReferenceAnnotation(const String &annoGFFfile,
					 const String &labelingFile,
					 const String &matrixFile,
					 Isochore &isochore,
					 const String &altSeqStr,
					 const Sequence &altSequence)
  : altSeq(altSequence), altSeqStr(altSeqStr)
{
  loadMatrix(matrixFile);
  loadLabeling(labelingFile);
  loadGFF(annoGFFfile,labeling.length());
  initSignals(isochore,altSeqStr,altSequence);
}



ReferenceAnnotation::~ReferenceAnnotation()
{
  delete matrix;
  delete contentRegions;
  // NOTE: don't delete signals here: they are handled by the garbage collector
}



void ReferenceAnnotation::loadMatrix(const String &filename)
{
  matrix=new LabelMatrix(filename);
  matrix->convertToLogs();
}



const LabelMatrix &ReferenceAnnotation::getMatrix() const
{
  return *matrix;
}



void ReferenceAnnotation::loadLabeling(const String &filename)
{
  labeling.load(filename);
}



const Labeling &ReferenceAnnotation::getLabeling() const
{
  return labeling;
}



const ContentRegions &ReferenceAnnotation::getRegions() const
{
  return *contentRegions;
}



void ReferenceAnnotation::loadGFF(const String &filename,const int seqLen)
{
  Vector<GffTranscript*> &transcripts=*GffReader::loadTranscripts(filename);
  if(transcripts.size()!=1) 
    throw "ReferenceAnnotation::loadGFF() : file must contain exactly one transcript";
  GffTranscript &transcript=*transcripts[0];
  contentRegions=new ContentRegions(transcript,seqLen);
  delete &transcripts;
}



void ReferenceAnnotation::initSignals(Isochore &isochore,const String &altSeqStr,
				      const Sequence &altSequence)
{
  Map<SignalType,SignalSensor*> &sensors=isochore.signalTypeToSensor;
  const Vector<ContentRegion> &regions=contentRegions->asVector();
  for(Vector<ContentRegion>::const_iterator cur=regions.begin(), end=
	regions.end() ; cur!=end ; ++cur) {
    const ContentRegion &region=*cur;
    ContentType contentType=region.getType();
    const Interval &interval=region.getInterval();
    SignalType rightSignalType=rightSignal(contentType);
    if(interval.getEnd()<altSeqStr.length())
      makeSignal(rightSignalType,interval.getEnd(),altSeqStr,altSequence,
		 *sensors[rightSignalType]);
  }
  sortSignals();
}



SignalPtr ReferenceAnnotation::getStartCodon() const
{
  for(Vector<Signal*>::const_iterator cur=signals.begin(), end=signals.end() ;
      cur!=end ; ++cur)
    if((*cur)->getSignalType()==ATG) return (*cur);
  return NULL;
  //throw "Start codon not found in ReferenceAnnotation::getStartCodon()";
}



void ReferenceAnnotation::sortSignals()
{
  SignalPosComparator signalCmp;
  VectorSorter<Signal*> sorter(signals,signalCmp);
  sorter.sortAscendInPlace();
}



void ReferenceAnnotation::makeSignal(SignalType signalType,int intervalBeginOrEnd,
				     const String &str,const Sequence &seq,
				     SignalSensor &sensor)
{
  int consensusPos, offset=sensor.getConsensusOffset();
  switch(signalType) {
  case ATG: consensusPos=intervalBeginOrEnd;   break;
  case TAG: consensusPos=intervalBeginOrEnd-3; break;
  case GT:  consensusPos=intervalBeginOrEnd;   break;
  case AG:  consensusPos=intervalBeginOrEnd-2; break;
  default: INTERNAL_ERROR;
  }
  if(sensor.consensusOccursAt(str,consensusPos))
    makeSignal(str,seq,consensusPos-offset,sensor);
  //else cerr<<"WARNING: "<<signalType<<" reference signal does not match"<<endl; // ### debugging
}



void ReferenceAnnotation::makeSignal(const String &str,const Sequence &seq,
				     int contextWindowPos,SignalSensor &sensor)
{
  const double score=sensor.getLogP(seq,str,contextWindowPos);
  Signal *signal=new Signal(contextWindowPos,score,sensor,sensor.getGC());
  signals.push_back(signal);
}



const Vector<Signal*> &ReferenceAnnotation::getSignals() const
{
  return signals;
}



int ReferenceAnnotation::getStartPosition() const
{
  const Vector<ContentRegion> &regions=contentRegions->asVector();
  for(Vector<ContentRegion>::const_iterator cur=regions.begin(), end=
	regions.end() ; cur!=end ; ++cur) {
    const ContentRegion &region=*cur;
    if(isCoding(region.getType())) return region.getInterval().getBegin();
  }
  throw "Can't find start position in ReferenceAnnotation::getStartPosition()";
}


