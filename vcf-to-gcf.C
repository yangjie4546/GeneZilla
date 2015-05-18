/****************************************************************
 vcf-to-gcf.C
 Copyright (C)2015 William H. Majoros (martiandna@gmail.com).
 This is OPEN SOURCE SOFTWARE governed by the Gnu General Public
 License (GPL) version 3, as described at www.opensource.org.
 ****************************************************************/
#include <iostream>
#include "BOOM/String.H"
#include "BOOM/CommandLine.H"
#include "BOOM/File.H"
#include "BOOM/Vector.H"
#include "BOOM/Map.H"
using namespace std;
using namespace BOOM;

struct Region {
  int begin, end;
  Region(int b,int e) : begin(b), end(e) {}
  bool contains(int p) { return begin<=p && p<end; }
};

struct Variant {
  String chr;
  int pos;
  String ref, alt, id;
  Variant(const String &chr,int pos,const String &ref,
	  const String &alt,const String &id)
    : chr(chr), pos(pos), ref(ref), alt(alt), id(id) {}
  String asString() { return id+":"+chr+":"+pos+":"+ref+":"+alt; }
};

struct Individual {
  String id;
  Vector<int> variants[2]; // Only for diploid species!
};

class Application {
public:
  Application();
  int main(int argc,char *argv[]);
protected:
  Map<String,Vector<Region> > regions;
  Vector<Individual> individuals;
  Vector<Variant> variants;
  bool prependChr;
  bool SNPsOnly;
  void loadRegions(const String &filename);
  void convert(File &infile,File &outfile);
  void parseChromLine(const Vector<String> &);
  void parseVariant(const Vector<String> &);
  void parseGenotype(const String &,int gt[2]);
  bool keep(const String &chr,int pos);
  void output(File &outfile);
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
  catch(...)
    {cerr << "Unknown exception caught in main" << endl;}
  return -1;
}



Application::Application()
{
  // ctor
}



int Application::main(int argc,char *argv[])
{
  // Process command line
  CommandLine cmd(argc,argv,"cf:s");
  if(cmd.numArgs()!=2)
    throw String("\nvcf-to-gcf [options] <in.vcf> <out.gcf>\n\
   -f regions.bed : keep only variants in these regions\n\
        (coordinates are 0-based, end is not inclusive)\n\
   -c : prepend \"chr\" before chromosome names\n\
   -s : SNPs only\n\
");
  const String infile=cmd.arg(0);
  const String outfile=cmd.arg(1);
  bool wantFilter=cmd.option('f');
  prependChr=cmd.option('c');
  SNPsOnly=cmd.option('s');

  // Load regions to filter by
  if(wantFilter) loadRegions(cmd.optParm('f'));

  // Open files
  File vcf(infile), gcf(outfile,"w");

  // Perform conversion
  convert(vcf,gcf);

  return 0;
}



void Application::loadRegions(const String &filename)
{
  File file(filename);
  while(!file.eof()) {
    String line=file.getline();
    line.trimWhitespace();
    Vector<String> &fields=*line.getFields();
    if(fields.size()>0) {
      if(fields.size()<3) throw filename+" - can't parse bed file: "+line;
      const String &chr=fields[0];
      const int begin=fields[1].asInt();
      const int end=fields[2].asInt();
      if(!regions.isDefined(chr)) regions[chr]=Vector<Region>();
      regions[chr].push_back(Region(begin,end));
    }
    delete &fields;
  }
}



void Application::convert(File &infile,File &outfile)
{
  while(!infile.eof()) {
    String line=infile.getline();
    line.trimWhitespace();
    Vector<String> &fields=*line.getFields();
    if(fields.size()>0) {
      if(fields[0]=="#CHROM") parseChromLine(fields);
      else if(fields[0][0]!='#') parseVariant(fields);
    }
    delete &fields;
  }
  output(outfile);
}



void Application::parseChromLine(const Vector<String> &fields)
{
  // #CHROM  POS     ID      REF     ALT     QUAL    FILTER  INFO    FORMAT  200848168@1097100704
  // The FORMAT column may or may not be present

  const int numFields=fields.size();
  if(numFields<10 || fields[8]!="FORMAT") 
    throw "Error parsing #CHROM line in VCF file";
  int numIndiv=numFields-9;
  individuals.resize(numIndiv);
  for(int i=0 ; i<numIndiv ; ++i)
    individuals[i].id=fields[i+9];
}



void Application::parseVariant(const Vector<String> &fields)
{
  if(fields.size()<10 || fields[6]!="PASS" || fields[8]!="GT") return;
  const String chr=fields[0];
  if(prependChr) chr=String("chr")+chr;
  const int pos=fields[1].asInt()-1; // VCF files are 1-based
  if(!keep(chr,pos)) return;
  const String id=fields[2];
  if(id==".") id=chr+"@"+pos;
  const String ref=fields[3];
  const String alt=fields[4];
  if(SNPsOnly && (ref.size()!=1 || alt.size()!=1)) return;
  variants.push_back(Variant(chr,pos,ref,alt,id));
  const int numIndiv=fields.size()-9;
  int gt[2];
  for(int i=0 ; i<numIndiv ; ++i) {
    parseGenotype(fields[i+9],gt);
    Individual &indiv=individuals[i];
    indiv.variants[0].push_back(gt[0]);
    indiv.variants[1].push_back(gt[1]);
  }
}



void Application::parseGenotype(const String &s,int gt[2])
{
  if(s.length()!=3) throw String("can't parse genotype in vcf file: ")+s;
  if(s[1]!='|') throw "vcf file does not appear to be phased";
  gt[0]=s[0]-'0';
  gt[1]=s[2]-'0';
}



bool Application::keep(const String &chr,int pos)
{
  if(!regions.isDefined(chr)) return false;
  const Vector<Region> &regs=regions[chr];
  for(Vector<Region>::const_iterator cur=regs.begin(), end=regs.end() ;
      cur!=end ; ++cur) {
    if((*cur).contains(pos)) return true;
  }
  return false;
}



void Application::output(File &out)
{
  // Output list of variants
  const int numVariants=variants.size();
  for(int i=0 ; i<numVariants ; ++i) {
    const Variant &v=variants[i];
    out.print(v.asString());
    out.print(i+1<numVariants ? "\t" : "\n");
  }

  // Output individual genotypes
  for(Vector<Individual>::const_iterator cur=individuals.begin(), 
	end=individuals.end() ; cur!=end ; ++cur) {
    const Individual &indiv=*cur;
    out.print(indiv.id+"\t");
    const  Vector<int> &var0=indiv.variants[0], &var1=indiv.variants[1];
    const int numVariants=var0.size();
    for(int i=0 ; i<numVariants ; ++i) {
      out.print(String(var0[i]) + "|" + var1[i]);
      out.print(i+1<numVariants ? "\t" : "\n");
    }
  }
}

