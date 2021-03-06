/****************************************************************
 gcf-to-fasta.C
 Copyright (C)2015 William H. Majoros (martiandna@gmail.com).
 This is OPEN SOURCE SOFTWARE governed by the Gnu General Public
 License (GPL) version 3, as described at www.opensource.org.
 ****************************************************************/
#include <iostream>
#include <fstream>
#include <stdio.h>
#include "BOOM/String.H"
#include "BOOM/CommandLine.H"
#include "BOOM/FastaReader.H"
#include "BOOM/FastaWriter.H"
#include "BOOM/Pipe.H"
#include "BOOM/VectorSorter.H"
#include "BOOM/Array1D.H"
#include "BOOM/ProteinTrans.H"
#include "BOOM/Regex.H"
#include "BOOM/Map.H"
#include "BOOM/TempFilename.H"
using namespace std;
using namespace BOOM;

const int PLOIDY=2;

// A Gentype represents the alleles of an individual at a single locus
struct Genotype {
  Array1D<int> alleles;
  Genotype(int ploidy) : alleles(ploidy) {}
};


struct Variant {
  String id, chr;
  Vector<String> alleles; // there can be several alternate alleles
  int pos, i;
  Variant(const String &id,const String &chr,int pos,int i)
    : id(id), chr(chr), pos(pos), i(i) {}
  void addAllele(const String &a) { alleles.push_back(a); }
  void printOn(ostream &os) const {
    os<<id<<":"<<chr<<":"<<pos;
    for(Vector<String>::const_iterator cur=alleles.begin(), end=alleles.end() ;
	cur!=end ; ++cur) os<<":"<<*cur;
  }
};
ostream &operator<<(ostream &os,const Variant &v) { v.printOn(os); return os; }


struct VariantComp : Comparator<Variant> {
  bool equal(Variant &a,Variant &b)
  { return a.chr==b.chr && a.pos==b.pos; }
  bool greater(Variant &a,Variant &b) 
  { return a.chr>b.chr || a.chr==b.chr && a.pos>b.pos; }
  bool less(Variant &a,Variant &b)    
  { return a.chr<b.chr || a.chr==b.chr && a.pos<b.pos; }
};



struct Region {
  String chr, id;
  int begin, end;
  String seq;
  char strand;
  Vector<Variant> variants;
  Region(const String &id,const String &chr,char strand,int begin,int end,
	 const String &seq) : id(id), chr(chr), begin(begin), end(end), 
			      strand(strand), seq(seq) {}
  bool contains(const Variant &v) const 
    { return v.chr==chr && v.pos>=begin && v.pos+v.alleles[0].length()<=end; }
  void loadSeq(const String &twoBitToFa,const String &genomeFile,
	       const String &tempFile);
  void clearSeq() { seq=""; }
  void printOn(ostream &os) const 
    { os<<chr<<":"<<begin<<"-"<<end<<":"<<strand; }
};
ostream &operator<<(ostream &os,const Region &r) { r.printOn(os); return os; }



bool operator<(const Variant &v,const Region &r)
{ return v.pos<r.begin; }
bool operator>(const Variant &v,const Region &r)
{ return v.pos>r.end; }



// This assumes no overlaps, so we need not compare ends
struct RegionComp : Comparator<Region*> {
  bool equal(Region *&a,Region *&b)
  { return a->chr==b->chr && a->begin==b->begin; }
  bool greater(Region *&a,Region *&b) 
  { return a->chr>b->chr || a->chr==b->chr && a->begin>b->begin; }
  bool less(Region *&a,Region *&b)    
  { return a->chr<b->chr || a->chr==b->chr && a->begin<b->begin; }
};



class Application {
public:
  Application();
  int main(int argc,char *argv[]);
protected:
  String twoBitToFa;
  String tempfile;
  Regex gzRegex;
  Map<String,Vector<Region*> > regionsByChr;
  Vector<Region*> regions;
  Vector<Variant> variants;
  FastaWriter writer;
  bool wantRef;
  String wantIndiv, wantChr, genomeFile;
  void convert(File &gcf,ostream &,const String genomeFile);
  void parseHeader(const String &line);
  void loadRegions(const String &regionsFilename,const String &genomeFilename,
		   const String &tempFile);
  void emit(const String &individualID,const Vector<Genotype> &loci,ostream &);
};


int main(int argc,char *argv[])
{
  try {
    Application app;
    return app.main(argc,argv);
  }
  catch(const char *p) { cerr << p << endl; }
  catch(const string &msg) { cerr << msg.c_str() << endl; }
  catch(const exception &e)
    {cerr << "STL exception caught in main:\n" << e.what() << endl;}
  catch(...) { cerr << "Unknown exception caught in main" << endl; }
  return -1;
}



Application::Application()
  : twoBitToFa("twoBitToFa"), gzRegex("gz$"), tempfile(TempFilename::get())
{
  // ctor
  randomize();
}



int Application::main(int argc,char *argv[])
{
  // Process command line
  CommandLine cmd(argc,argv,"rt:i:c:");
  if(cmd.numArgs()!=4)
    throw String("\ngcf-to-fasta [options] <in.gcf> <genome.2bit> <regions.bed> <out.fasta>\n\
     -t path : path to twoBitToFa\n\
     -r : emit reference sequence also\n\
     -i ID : only this sample (individual)\n\
     -c chr : only regions on this chromosome\n\
\n\
     NOTE: Run 'which twoBitToFa' to ensure it's in your path\n\
     NOTE: regions.bed is a BED6 file: chr begin end name score strand\n\
");
  const String &gcfFilename=cmd.arg(0);
  genomeFile=cmd.arg(1);
  const String &regionsFilename=cmd.arg(2);
  const String &fastaFilename=cmd.arg(3);
  if(cmd.option('t')) twoBitToFa=cmd.optParm('t');
  wantRef=cmd.option('r');
  if(cmd.option('i')) wantIndiv=cmd.optParm('i');
  if(cmd.option('c')) wantChr=cmd.optParm('c');

  // Load regions
  loadRegions(regionsFilename,genomeFile,fastaFilename);

  // Process GCF file
  File *gcf=gzRegex.search(gcfFilename) ? new GunzipPipe(gcfFilename)
    : new File(gcfFilename);
  ofstream os(fastaFilename.c_str());
  convert(*gcf,os,genomeFile);
  delete gcf;

  return 0;
}



void Application::convert(File &gcf,ostream &os,const String genomeFile)
{
  // Parse header
  String line=gcf.getline();
  line.trimWhitespace();
  parseHeader(line);
  const int numVariants=variants.size();

  // Emit reference
  if(wantRef) {
    for(Vector<Region*>::const_iterator cur=regions.begin(), end=regions.end() ;
	cur!=end ; ++cur) {
      const Region &region=**cur;
      String seq=region.seq;
      if(region.strand=='-') seq=ProteinTrans::reverseComplement(seq);
      for(int j=0 ; j<PLOIDY ; ++j) {
	String def=String(">reference_")+j+" /individual=reference"+
	  " /allele="+j+" /region="+region.id;
	writer.addToFasta(def,seq,os);
      }
    }
  }

  // Process each individual
  while(!gcf.eof()) {
    line=gcf.getline();
    line.trimWhitespace();
    if(line.isEmpty()) continue;
    Vector<String> &fields=*line.getFields();
    if(fields.size()!=numVariants+1) {
      cout<<fields.size()<<"\t"<<numVariants<<"\t"<<endl;
      if(fields.size()>0) cout<<fields[0]<<endl;
      throw "Wrong number of fields in GCF line";
    }
    String id=fields.front();
    if(!wantIndiv.isEmpty() && id!=wantIndiv) continue;
    fields.erase(fields.begin());
    Vector<Genotype> loci;
    for(Vector<String>::const_iterator cur=fields.begin(), end=fields.end() ;
	cur!=end ; ++cur) {
      const String &field=*cur;
      if(field.length()==1) {
	Genotype gt(1);
	if(field[0]=='.') gt.alleles[0]=0;
	else {
	  gt.alleles[0]=field[0]-'0'; gt.alleles[1]=-1;
	  if(gt.alleles[0]<0 || gt.alleles[0]>9) 
	    throw fields[0]+" : unknown allele indicator";
	}
	loci.push_back(gt);
      }
      else if(field.length()==3) {
	if(field[1]!='|') throw "VCF file is not phased";
	Genotype gt(2);
	if(field[0]=='.') gt.alleles[0]=0;
	else { 
	  gt.alleles[0]=field[0]-'0'; 
	  if(gt.alleles[0]<0 || gt.alleles[0]>9) 
	    throw fields[0]+" : unknown allele indicator";
	}
	if(field[2]=='.') gt.alleles[1]=0;
	else {
	  gt.alleles[1]=field[2]-'0';
	  if(gt.alleles[1]<0 || gt.alleles[1]>9) 
	    throw fields[2]+" : unknown allele indicator";
	}
	loci.push_back(gt);
      }
      else throw String("Cannot parse genotype: ")+field;
    }
    delete &fields;
    emit(id,loci,os);
    if(!wantIndiv.isEmpty()) break;
  }
}



void Application::parseHeader(const String &line)
{
  Vector<String> fields; line.getFields(fields);
  Map<String,int> prevPos;
  int i=0;
  for(Vector<String>::const_iterator cur=fields.begin(), end=fields.end()
	; cur!=end ; ++cur, ++i) {
    const String &field=*cur;
    Vector<String> fields; field.getFields(fields,":");
    if(fields.size()!=5) throw String("cannot parse GCF header: ")+field;
    const String &id=fields[0];
    const String &chr=fields[1];
    //if(!wantChr.isEmpty() && chr!=wantChr) continue;
    const int pos=fields[2];
    if(!prevPos.isDefined(chr)) prevPos[chr]=0;
    //if(pos==prevPos[chr]) continue;
    if(pos<prevPos[chr]) 
      throw String(pos)+"<="+prevPos[chr]+
	": input file is not sorted: use vcf-sort and re-convert to gcf";
    prevPos[chr]=pos;
    const String &ref=fields[3];
    const String &alt=fields[4];
    Variant variant(id,chr,pos,i);
    variant.addAllele(ref);
    Vector<String> altAlleles;
    alt.getFields(altAlleles,",");
    for(Vector<String>::iterator cur=altAlleles.begin(), end=altAlleles.end() ;
	cur!=end ; ++cur) {
      String allele=*cur;
      variant.addAllele(allele);
    }
    variants.push_back(variant);
  }

  // Now add to regions
  for(Vector<Variant>::iterator vcur=variants.begin(), 
	vend=variants.end() ; vcur!=vend ; ++vcur) {
    const Variant &v=*vcur;
    const int pos=v.pos;
    if(!regionsByChr.isDefined(v.chr)) continue;
    Vector<Region*> &rs=regionsByChr[v.chr];
    const int N=rs.size();
    for(int b=0, e=N ; b<e ; ) {
      const int mid=(b+e)/2;
      Region &midRegion=*rs[mid];
      if(pos<midRegion.begin) e=mid;
      else if(pos>=midRegion.end) b=mid+1;
      else { midRegion.variants.push_back(v); break; }
    }
  }
}



void Region::loadSeq(const String &twoBitToFa,const String &genomeFile,
		     const String &tempFile)
{
  // Invoke twoBitToFa to extract sequence from chrom file
  String cmd=twoBitToFa+" -seq="+chr+" -start="+begin+" -end="+end+
    +" "+genomeFile+" "+tempFile;
  system(cmd.c_str());
  String def;
  FastaReader::load(tempFile,def,seq);
}



void Application::loadRegions(const String &regionsFilename,const String &
			      genomeFilename,const String &tempFile)
{
  File reg(regionsFilename);
  while(!reg.eof()) {
    String line=reg.getline();
    line.trimWhitespace();
    if(line.isEmpty()) continue;
    Vector<String> fields; line.getFields(fields);
    if(fields.size()<6) throw "regions file requires 6 fields: chr begin end name score strand";
    const String chr=fields[0];
    if(!wantChr.isEmpty() && chr!=wantChr) continue;
    const int begin=fields[1].asInt(), end=fields[2].asInt();
    const String id=fields[3];
    char strand=fields[5][0];
    
    // Invoke twoBitToFa to extract sequence from chrom file
    String seq;
    if(wantIndiv.isEmpty()) {
      String cmd=twoBitToFa+" -seq="+chr+" -start="+begin+" -end="+end+
	+" "+genomeFilename+" "+tempFile;
      system(cmd.c_str());
      String def;
      FastaReader::load(tempFile,def,seq);
      //if(seq.contains(",")) throw tempFile+"contains a comma: "+cmd;
    }

    Region *r=new Region(id,chr,strand,begin,end,seq);
    regions.push_back(r);
    regionsByChr[chr].push_back(r);
  }
  unlink(tempFile.c_str());

  // Sort regions so we can do fast searches later
  RegionComp cmp;
  Set<String> keys; regionsByChr.getKeys(keys);
  for(Set<String>::const_iterator cur=keys.begin(),
	end=keys.end() ; cur!=end ; ++cur) {
    VectorSorter<Region*> sorter(regionsByChr[*cur],cmp);
    sorter.sortAscendInPlace();
  }
}



void Application::emit(const String &individualID,const Vector<Genotype> &loci,
		       ostream &os)
{
  const int numVariants=variants.size();
  for(Vector<Region*>::const_iterator cur=regions.begin(), end=regions.end() ;
      cur!=end ; ++cur) {
    Array1D<int> deltas(PLOIDY); deltas.setAllTo(0); // for indels
    const Region &region=**cur;
    //cout<<"region "<<region<<endl;
    if(!wantIndiv.isEmpty()) region.loadSeq(twoBitToFa,genomeFile,tempfile);
    unlink(tempfile.c_str());
    String seq[PLOIDY]; for(int i=0 ; i<PLOIDY ; ++i) seq[i]=region.seq;
    int prevPos=-1;
    for(Vector<Variant>::const_iterator cur=region.variants.begin(), end=
	  region.variants.end() ; cur!=end ; ++cur) {
      const Variant &variant=*cur;
      //cout<<variant<<endl;
      if(variant.pos==prevPos) continue;
      prevPos=variant.pos;
      const int localPos=variant.pos-region.begin;
      const Genotype &gt=loci[variant.i];
      for(int j=0 ; j<PLOIDY ; ++j) {
	const int allele=gt.alleles[j];
	if(allele) { // differs from reference
	  const String &refAllele=variant.alleles[0];
	  const String &altAllele=variant.alleles[allele];
	  int refLen=refAllele.length();
	  const int altLen=altAllele.length();
	  if(localPos+refLen>seq[j].length()) refLen=seq[j].length()-localPos;
	  const String refCheck=region.seq.substring(localPos,refLen);
	  const int refCheckLen=refCheck.length();
	  const String refAlleleSub=refAllele.substring(0,refCheckLen);
	  if(localPos-deltas[j]+refLen>seq[j].length())
	    throw String("Internal error: localPos=")+localPos+" deltas[j]="+
	      deltas[j]+" refLen="+refLen+" seq[j].length="+seq[j].length();
	  if(refCheck!=refAlleleSub)
	    throw String("reference mismatch: ")+refAlleleSub+" vs. "+
	      refCheck;
	  //cout<<"localPos="<<localPos<<" deltas[j]="<<deltas[j]<<" refLen="<<refLen<<" altAllele="<<altAllele<<" seq[j].length="<<seq[j].length()<<endl;
	  seq[j].replaceSubstring(localPos-deltas[j],refLen,altAllele);
	  deltas[j]+=refLen-altLen;
	}
      }
    } // end foreach variant
    for(int j=0 ; j<PLOIDY ; ++j) {
      String def=String(">")+individualID+"_"+j+" /individual="+individualID+
	" /allele="+j+" /region="+region.id;
      if(region.strand=='-') seq[j]=ProteinTrans::reverseComplement(seq[j]);
      writer.addToFasta(def,seq[j],os);
    }
    if(!wantIndiv.isEmpty()) region.clearSeq(); // save memory
  } // end foreach region
}



/*
void Application::emit(const String &individualID,const Vector<Genotype> &loci,
		       ostream &os)
{
  const int numVariants=variants.size();
  for(Vector<Region>::const_iterator cur=regions.begin(), end=regions.end() ;
      cur!=end ; ++cur) {
    Array1D<int> deltas(PLOIDY); deltas.setAllTo(0); // for indels
    const Region &region=*cur;
    String seq[PLOIDY]; for(int i=0 ; i<PLOIDY ; ++i) seq[i]=region.seq;
    for(int i=0 ; i<numVariants ; ++i) { // ### SLOW!!
      const Variant &variant=variants[i];
      const int localPos=variant.pos-region.begin;
      if(region.contains(variant)) {
	Genotype gt=loci[i];
	for(int j=0 ; j<PLOIDY ; ++j) {
	  const int allele=gt.alleles[j];
	  if(allele) { // differs from reference
	    const String &refAllele=variant.alleles[0];
	    const String &altAllele=variant.alleles[allele];
	    const int refLen=refAllele.length();
	    const int altLen=altAllele.length();
	    deltas[j]+=refLen-altLen;
	    if(region.seq.substring(localPos,refLen)!=refAllele)
	      throw String("reference mismatch: ")+refAllele+" vs. "+
		region.seq.substring(variant.pos,refLen);
	    seq[j].replaceSubstring(localPos-deltas[j],refLen,altAllele);
	  }
	}
      }
    } // end foreach variant
    for(int j=0 ; j<PLOIDY ; ++j) {
      String def=String(">")+individualID+"_"+j+" /individual="+individualID+
	" /allele="+j+" /region="+region.id;
      if(region.strand=='-') seq[j]=ProteinTrans::reverseComplement(seq[j]);
      writer.addToFasta(def,seq[j],os);
    }
  } // end foreach region
}
 */



