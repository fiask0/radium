/* link with : "" */
#include <stdlib.h>
#include <libgen.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <math.h>
#include <errno.h>
#include <time.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <assert.h>
#include <pthread.h> 
#include <sys/wait.h>
#include <list>

#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>

#include "faust/dsp/dsp.h"
#include "faust/gui/FUI.h"
#include "faust/misc.h"
#include "faust/dsp/llvm-dsp.h"

using namespace std;

// handle 32/64 bits int size issues
#ifdef __x86_64__

#define uint32	unsigned int
#define uint64	unsigned long int

#define int32	int
#define int64	long int

#else

#define uint32	unsigned int
#define uint64	unsigned long long int

#define int32	int
#define int64	long long int
#endif

// check 32/64 bits issues are correctly handled
#define CHECKINTSIZE \
	assert(sizeof(int32)==4);\
	assert(sizeof(int64)==8);

#define check_error(err) if (err) { printf("%s:%d, alsa error %d : %s\n", __FILE__, __LINE__, err, snd_strerror(err)); exit(1); }
#define check_error_msg(err,msg) if (err) { fprintf(stderr, "%s:%d, %s : %s(%d)\n", __FILE__, __LINE__, msg, snd_strerror(err), err); exit(1); }
#define display_error_msg(err,msg) if (err) { fprintf(stderr, "%s:%d, %s : %s(%d)\n", __FILE__, __LINE__, msg, snd_strerror(err), err); }

#define max(x,y) (((x)>(y)) ? (x) : (y))
#define min(x,y) (((x)<(y)) ? (x) : (y))

#define BENCHMARKMODE
#ifdef BENCHMARKMODE

/**
 * Returns the number of clock cycles elapsed since the last reset
 * of the processor
 */
static __inline__ uint64 rdtsc(void)
{
	union {
		uint32 i32[2];
		uint64 i64;
	} count;
	
	__asm__ __volatile__("rdtsc" : "=a" (count.i32[0]), "=d" (count.i32[1]));
     return count.i64;
}

#define KSKIP 20
#define KMESURE 100000

int mesure = 0;

// these values are used to determine the number of clocks in a second
uint64 firstRDTSC;
uint64 lastRDTSC;

// these tables contains the last KMESURE in clocks
uint64 starts[KMESURE];
uint64 stops [KMESURE];

#define STARTMESURE starts[mesure%KMESURE] = rdtsc();
#define STOPMESURE stops[mesure%KMESURE] = rdtsc(); mesure = mesure+1;

struct timeval tv1;
struct timeval tv2;

void openMesure()
{
	struct timezone tz;
	gettimeofday(&tv1, &tz);
	firstRDTSC = rdtsc();
}

void closeMesure()
{
	struct timezone tz;
	gettimeofday(&tv2, &tz);
	lastRDTSC = rdtsc();
}
	
/**
 * return the number of RDTSC clocks per seconds
 */
int64 rdtscpersec()
{
	// If the environment variable CLOCKSPERSEC is defined
	// we use it instead of our own measurement
	char* str = getenv("CLOCKSPERSEC");
    if (str) {
	    int64 cps = (int64) atoll(str);
        if (cps > 1000000000) {
		    return cps;
	    } else {
		    return (lastRDTSC-firstRDTSC) / (tv2.tv_sec - tv1.tv_sec) ;
	    }
    } else {
        return (lastRDTSC-firstRDTSC) / (tv2.tv_sec - tv1.tv_sec) ;
    }   
}
    
/**
 * Converts a duration, expressed in RDTSC clocks, into seconds
 */
double rdtsc2sec(uint64 clk)
{
	return double(clk) / double(rdtscpersec());
}

double rdtsc2sec(double clk)
{
	return clk / double(rdtscpersec());
}

/**
 * Converts RDTSC clocks into Megabytes/seconds according to the
 * number of frames processed during the period, the number of channels
 * and 4 bytes samples.
 */
double megapersec(int frames, int chans, uint64 clk)
{
	return double(frames) * double(chans) * 4 / double(1024 * 1024 * rdtsc2sec(clk));
}
    
/**
 * Compute the mean value of a vector of measures
 */
static uint64 meanValue( vector<uint64>::const_iterator a, vector<uint64>::const_iterator b)
{
	uint64 r = 0;
	unsigned int n = 0;
	while (a!=b) { r += *a++; n++; }
	return (n>0) ? r/n : 0;
}   

/**
 * Print the median value (in Megabytes/second) of KMESURE
 * throughputs measurements
 */
void printstats(const char* applname, const char* dspname, int bsize, int ichans, int ochans)
{
    assert(mesure > KMESURE);
    vector<uint64> V(KMESURE);

    for (int i = 0; i<KMESURE; i++) {
        V[i] = stops[i] - starts[i];
    }

    sort(V.begin(), V.end());
  
    // Mean of 10 best values (gives relatively stable results)
    uint64 meaval00 = meanValue(V.begin(), V.begin()+ 5);			
    uint64 meaval25 = meanValue(V.begin()+KMESURE/4 - 2, V.begin()+KMESURE/4 + 3);			
    uint64 meaval50 = meanValue(V.begin()+KMESURE/2 - 2, V.begin()+KMESURE/2 + 3);			
    uint64 meaval75 = meanValue(V.begin()+3*KMESURE/4 - 2, V.begin()+3*KMESURE/4 + 3);			
    uint64 meaval100 = meanValue(V.end() - 5, V.end());			
  
    //printing
    cout << applname << " " 
         << dspname
         << '\t' << megapersec(bsize, ichans+ochans, meaval00) 
         << '\t' << megapersec(bsize, ichans+ochans, meaval25) 
         << '\t' << megapersec(bsize, ichans+ochans, meaval50) 
         << '\t' << megapersec(bsize, ichans+ochans, meaval75) 
         << '\t' << megapersec(bsize, ichans+ochans, meaval100) 
         << endl;
}

#else

#define STARTMESURE
#define STOPMESURE

#endif

#include <stdlib.h>

bool running = true;

class measure_dsp : public dsp {

    private:
    
        dsp* fDSP;
        float* fInputs[256];
        float* fOutputs[256];
        int fBufferSize;

    public:
        measure_dsp(dsp* dsp, int fpb, int srate)
            :fDSP(dsp), fBufferSize(fpb)
        {
             for (int i = 0; i < fDSP->getNumInputs(); i++) {
                fInputs[i] = new float[fpb];
            }
            
            for (int i = 0; i < fDSP->getNumOutputs(); i++) {
                fOutputs[i] = new float[fpb];
            }
        }
        
        virtual ~measure_dsp() {
            
           // TODO : deallocate
        }

        int getNumInputs() 	{ return fDSP->getNumInputs(); }
        
        int getNumOutputs() { return fDSP->getNumOutputs(); }
        
        void buildUserInterface(UI* interface) 	{ fDSP->buildUserInterface(interface); }
        
        void init(int samplingRate) { fDSP->init(samplingRate); }
        
        void compute(int len, FAUSTFLOAT** inputs, FAUSTFLOAT** outputs) 
        {   
            STARTMESURE
            fDSP->compute(len, inputs, outputs);
            STOPMESURE
            running = mesure <= (KMESURE + KSKIP);
        }
        
        void compute_all() 
        {
            do {
                compute(fBufferSize, fInputs, fOutputs);
            } while (running);
        }

};

/******************************************************************************
*******************************************************************************

								MAIN PLAY THREAD

*******************************************************************************
*******************************************************************************/
		
// sopt : Scan Command Line string Arguments
const char* sopt(int argc, char *argv[], const char* longname, const char* shortname, const char* def) 
{
	for (int i=2; i<argc; i++) {
        if (strcmp(argv[i-1], shortname) == 0 || strcmp(argv[i-1], longname) == 0) {
            return argv[i];
        }
    }
	return def;
}
	
// fopt : Scan Command Line flag option (without argument), return true if the flag
bool fopt(int argc, char *argv[], const char* longname, const char* shortname) 
{
	for (int i=1; i<argc; i++) {
        if (strcmp(argv[i], shortname) == 0 || strcmp(argv[i], longname) == 0) {
            return true;
        }
    }
	return false;
}
	
//-------------------------------------------------------------------------
// 									MAIN
//-------------------------------------------------------------------------

int main(int argc, char *argv[])
{
	CHECKINTSIZE;
	
	// compute rcfilename to (re)store application state
    char name[256];
	char rcfilename[256];
	char* home = getenv("HOME");
    snprintf(name, 255, "%s", basename(argv[0]));
	snprintf(rcfilename, 255, "%s/.%src", home, basename(argv[0]));
    
	long srate = (long)lopt(argv, "--frequency", 44100);
    int	fpb = lopt(argv, "--buffer", 512);
    
    string error;
    llvm_dsp_factory* factory = createDSPFactory(argc - 1, (const char**)&argv[1], "", "", "", "", "", error, 5);
    assert(factory);
    llvm_dsp* dsp = createDSPInstance(factory);
    assert(dsp);
 	
    dsp->init(srate);
 	
    openMesure();
      
    measure_dsp measure(dsp, fpb, srate);
    measure.compute_all();

	closeMesure();

#ifdef BENCHMARKMODE
    printstats(argv[0], argv[1], fpb, dsp->getNumInputs(), dsp->getNumOutputs());
#endif       

  	return 0;
}
