/****************************************************************
 project-annotation.C
 Copyright (C)2015 William H. Majoros (martiandna@gmail.com).
 This is OPEN SOURCE SOFTWARE governed by the Gnu General Public
 License (GPL) version 3, as described at www.opensource.org.
 ****************************************************************/
#include <iostream>
#include <fstream>
#include "BOOM/String.H"
#include "BOOM/CommandLine.H"
#include "BOOM/CigarString.H"
#include "BOOM/GffReader.H"
#include "BOOM/FastaReader.H"
#include "Labeling.H"
#include "ProjectionChecker.H"
using namespace std;
using namespace BOOM;


class Application
{
public:
  Application();
  int main(int argc,char *argv[]);
private:
  GffTranscript *loadGff(const String &filename);
  void parseNoncanonicals(const String &,Set<String> &);
  String loadSeq(const String &filename);
  void computeLabeling(GffTranscript &,Labeling &);
  void mapLabeling(Labeling &from,Labeling &to,const String &cigar);
  void mapTranscript(GffTranscript &,const String &cig,const String &outfile);
  void mapExon(GffExon &,CigarAlignment &);
};



int main(int argc,char *argv[])
  {
    try
      {
	Application app;
	return app.main(argc,argv);
      }
    catch(const char *p)
      {
	cerr << p << endl;
      }
    catch(const string &msg)
      {
	cerr << msg.c_str() << endl;
      }
    catch(const exception &e)
      {
	cerr << "STL exception caught in main:\n" << e.what() << endl;
      }
    catch(...)
      {
	cerr << "Unknown exception caught in main" << endl;
      }
    return -1;
  }



Application::Application()
  {
    // ctor
  }



int Application::main(int argc,char *argv[])
{
  // Process command line
  CommandLine cmd(argc,argv,"d:a:f");
  if(cmd.numArgs()!=6)
    throw String("\n\
project-annotation <ref.gff> <ref.fasta> <alt.fasta> <ref-alt.cigar> <out.vector> <out.gff>\n\
     -d donors = also allow these donors; comma-separated (capital letters)\n\
     -a acceptors = also allow these acceptors; comma-separated (capital)\n\
     -f = force reporting of problems when mapping fails\n\
\n");
  const String refGffFile=cmd.arg(0);
  const String refFasta=cmd.arg(1);
  const String altFasta=cmd.arg(2);
  const String cigarFile=cmd.arg(3);
  const String outfile=cmd.arg(4);
  const String outGff=cmd.arg(5);
  const bool forceFailureInfo=cmd.option('f');
  Set<String> nonCanonicalGTs, nonCanonicalAGs;
  if(cmd.option('d')) parseNoncanonicals(cmd.optParm('d'),nonCanonicalGTs);
  if(cmd.option('a')) parseNoncanonicals(cmd.optParm('a'),nonCanonicalAGs);
  
  // Read some data from files
  String refSeq=loadSeq(refFasta), altSeq=loadSeq(altFasta);
  int refSeqLen=refSeq.length(), altSeqLen=altSeq.length();
  GffTranscript *refTrans=loadGff(refGffFile);

  // Check that the reference gene is well-formed
  bool noStart, noStop, PTC, badSpliceSite;
  String msg;
  if(!ProjectionChecker::geneIsWellFormed(*refTrans,refSeq,
					  nonCanonicalGTs,nonCanonicalAGs,
					  noStart,noStop,PTC,badSpliceSite,
					  msg)) {
    cout<<"Error: reference gene is not well-formed -- aborting projection"<<endl;
    cout<<"Details:"<<endl;
    if(PTC) cout<<"\tReference gene has a pre-termination stop codon (PTC)"<<endl;
    cout<<msg<<endl;
    //if(noStart) cout<<"\tReference gene's CDS does not begin with a valid start codon"<<endl;
    //if(noStop) cout<<"\tLast codon in reference gene's CDS is not a stop codon"<<endl;
    //if(badSpliceSite) cout<<"\tReference gene has a nonconsensus splice site"<<endl;
    return -1;
  }
  else cout<<"reference gene is well-formed"<<endl; // ### DEBUGGING

  // Compute the reference labeling
  Labeling refLab(refSeqLen);
  computeLabeling(*refTrans,refLab);

  // Load CIGAR string
  String CIGAR;
  File cigar(cigarFile);
  CIGAR=cigar.getline();
  cigar.close();

  // Project the reference labeling over to the alternate sequence
  Labeling altLab(altSeqLen);
  mapLabeling(refLab,altLab,CIGAR);

  // Project the reference GFF over to an alternate GFF
  mapTranscript(*refTrans,CIGAR,outGff);

  // Generate labeling file
  ofstream os(outfile.c_str());
  os<<altLab;
  os.close();
  
  // Check the projection to see if the gene might be broken
  GffTranscript *altTrans=loadGff(outGff);
  ProjectionChecker checker(*refTrans,*altTrans,refSeq,altSeq,
			    altLab,nonCanonicalGTs,nonCanonicalAGs);
  bool spliceSitesOK=checker.checkSpliceSites(!forceFailureInfo);
  if(!spliceSitesOK) {
    cout<<"splice sites do not map perfectly -- please run CIA"<<endl;
    return -1;
  }

  // Otherwise, projection was successful
  cout<<"annotation successfully projected"<<endl;

  // Translate to proteins
  String refProtein, altProtein;
  checker.translate(*refTrans,*altTrans,refProtein,altProtein);
  //cout<<"TRANSLATIONS:\nALT="<<altProtein<<"\nREF="<<refProtein<<endl;
  
  // Check for start codon
  if(!checker.hasStartCodon(altProtein)) cout<<"No start codon"<<endl;

  // Check for frameshifts
  if(refProtein!=altProtein) cout<<"Proteins differ"<<endl;
  checker.checkFrameshifts(altLab,*altTrans,altSeq,false);

  // Check for stop codons
  int PTCpos;
  if(checker.hasPTC(altProtein,PTCpos)) {
    cout<<"Premature stop at AA position "<<PTCpos
	<<" in alt protein, length="<<altProtein.length()
	<<"\nalt="<<altProtein<<"\nref="<<refProtein<<endl;
    int EJCdistance;
    if(!checker.detectNMD(*altTrans,altSeq,false,EJCdistance))
      cout<<"Truncation predicted : PTC is "<<EJCdistance
	  <<"nt from last EJC"<<endl;
  }
  else if(!checker.hasStopCodon(altProtein)) {
    refTrans->extendFinalExonBy3(); altTrans->extendFinalExonBy3();
    checker.translate(*refTrans,*altTrans,refProtein,altProtein);
    if(!checker.hasStopCodon(altProtein))
      cout<<"Missing stop codon, probable loss of function:\nalt="<<altProtein
	  <<"\nref="<<refProtein<<endl; 
  }

  return 0;
}



void Application::computeLabeling(GffTranscript &transcript,
				  Labeling &refLab)
{
  int begin, end;
  transcript.getCDSbeginEnd(begin,end);
  char strand=transcript.getStrand();
  if(strand!='+') throw "only forward-strand features are currently supported";
  int numExons=transcript.getNumExons();
  refLab.asArray().setAllTo(LABEL_INTERGENIC);
  for(int i=begin ; i<end ; ++i) refLab[i]=LABEL_INTRON;
  int phase=0;
  for(int i=0 ; i<numExons ; ++i) {
    GffExon &exon=transcript.getIthExon(i);
    begin=exon.getBegin(); end=exon.getEnd();
    for(int j=begin ; j<end ; ++j) {
      refLab[j]=getExonLabel(phase);
      phase=(phase+1)%3;
    }
  }
  for(Vector<GffExon*>::iterator cur=transcript.getUTR(), end=
	transcript.getUTRend() ; cur!=end ; ++cur) {
    GffExon *UTR=*cur;
    refLab.setIntervalTo(Interval(UTR->getBegin(),UTR->getEnd()),LABEL_UTR);
  }
}



void Application::mapLabeling(Labeling &from,Labeling &to,const String &cig)
{
  CigarString cigar(cig);
  CigarAlignment &align=*cigar.getAlignment();
  to.asArray().setAllTo(LABEL_NONE);
  int L=align.length();
  for(int i=0 ; i<L ; ++i) {
    int j=align[i];
    if(j!=CIGAR_UNDEFINED) to[j]=from[i];
  }
  delete &align;
}



void Application::mapExon(GffExon &exon,CigarAlignment &align)
{
  int begin=exon.getBegin(), end=exon.getEnd();
  begin=align.mapApproximate(begin,DIR_RIGHT);
  end=align.mapApproximate(end-1,DIR_LEFT)+1;
  exon.setBegin(begin); exon.setEnd(end);
}



void Application::mapTranscript(GffTranscript &refTrans,const String &cig,
				 const String &outfile)
{
  GffTranscript transcript=refTrans;
  CigarString cigar(cig);
  CigarAlignment &align=*cigar.getAlignment();
  for(Vector<GffExon*>::iterator cur=transcript.getExons(), end=
	transcript.getExonsEnd() ; cur!=end ; ++cur)
    mapExon(**cur,align);
  for(Vector<GffExon*>::iterator cur=transcript.getUTR(), end=
	transcript.getUTRend() ; cur!=end ; ++cur)
    mapExon(**cur,align);
  delete &align;
  ofstream os(outfile.c_str());
  transcript.toGff(os);
}



String Application::loadSeq(const String &filename)
{
  FastaReader reader(filename);
  String def, seq;
  if(!reader.nextSequence(def,seq)) throw filename+" : cannot read file";
  return seq;
}



GffTranscript *Application::loadGff(const String &filename)
{
  GffReader reader(filename);
  Vector<GffTranscript*> *transcripts=reader.loadTranscripts();
  const int n=transcripts->size();
  if(n<1) throw filename+" contains no transcripts";
  GffTranscript *transcript=(*transcripts)[0];
  for(int i=1 ; i<n ; ++i) delete (*transcripts)[i];
  delete transcripts;
  return transcript;
}



void Application::parseNoncanonicals(const String &str,Set<String> &into)
{
  Vector<String> &fields=*str.getFields(",");
  for(Vector<String>::const_iterator cur=fields.begin(), end=fields.end() ;
      cur!=end ; ++cur)
    into.insert(*cur);
  delete &fields;
}


