/*

             MSG - software sampler v1.09
   (c) copyright - 2001
     adrian ward - slub
    ade@slub.org - http://www.slub.org/


   "Computers're bringing about a situation that's like
    the invention of harmony. Sub-routines are like chords.
    No one would think of keeping a chord to himself. You'd
    give it to anyone who wanted it. You'd welcome
    alterations of it. Sub-routines are altered by a single
    punch. We're getting music made by man himself: not just
    one man."
                                           - John Cage, 1969


  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

  the complete license may be found here:
  http://www.gnu.org/copyleft/gpl.html

*/

#include <errno.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/soundcard.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sndfile.h>              // libsndfile is required

// Some constants
#define TRUE              1
#define FALSE             0

// Customizable parameters
#define MAXSAMPLES        400     // Max number of sample banks to be loaded
#define MAXCHANNELS       16      // Max number of sounds playing at once
#define MAXCMDBUFFER      32      // Max number of commands to buffer until flush (pointless if -nobuffer)

#define DEFHISS           0.0    // default % hiss you want to simulate
#define DEFHUM            0.0    // default % line hum you want to simulate
#define DEFHUMFREQ        50.0    // default line hum frequency (50hz for UK)
#define DEFCOMPRESSORGAIN 50.0    // default speed of compressor gain (higher =faster)

// Sound system constants
#define BUFFERSIZE        100        
#define FORMAT            AFMT_S16_LE
#define CHANNELS          1       // Stereo
#define SAMPLERATE        44100   // 44.1kHz

// stereo struct for easy handling of stereo sample pairs
typedef struct {
 float l;
 float r; 
 float mono; // optionally used
} stereo;

// envImplementation struct
typedef struct {
 float volume;
 float decay;
} envImplementation;

// ringModulatorImplementation struct
typedef struct {
 float frequency;
 float amplitude;
 int   ctr;
} ringModulatorImplementation;

// VCFimplementation struct
typedef struct {
 float cutoff;
 float res;
 float f;
 float k;
 float p;
 float scale;
 float r;
 float y1;
 float y2;
 float y3;
 float y4;
 float oldx;
 float oldy1;
 float oldy2;
 float oldy3;
 float x;
} VCFimplementation;

// synthImplementation struct
typedef struct {
 int   wave;             // Wavetype 0=S=Square 1=T=Triangle 2=Q=Square 3=R=Random
 float offset;           // Pulse width
 float amp;              // Pulse amplitude
 float decay;            // Waveform decay
 float targetOffset;
 float targetAmp;
 float targetDecay;
 float speed;            // Speed at which values change when make is recalled with new values
} synthImplementation;

// playbackChannel struct for playback channels (ie, a single channel 'head')
typedef struct {
 int                         active;
 int                         age;
 char*		             group; // Group name
 float                       position;
 float                       speed;
 float                       pan;
 int                         bank;
 float                       loopStart;
 float                       loopEnd;
 VCFimplementation           vcf;
 ringModulatorImplementation ringmodulator;
 envImplementation           env;
} playbackChannel;

// sampleBank struct for holding samples (ie, a single audio sound)
typedef struct {
 short  *samples;
 char   *name;
 int     size;
 int     loaded;
 int     isSynth;
 synthImplementation synth;
} sampleBank;

// Global switches
int       DEBUG=FALSE;
int       RECORDTODISK=FALSE;
int       SILENT=FALSE;
int       NOBUFFER=FALSE;

// Globals for audio system
int       devHandle;
pthread_t audioThreadHnd;
float     pitchSpeeds[3000];
float     hiss;
float     compressorgain;
float     hum;
float     humfreq;
float     sampleCtr;
char*     cmdBuffer[MAXCMDBUFFER];

SNDFILE  *renderFile;   // Record-to-disk functionality
SF_INFO   renderSFinfo;

// Globals for playback channels
int             channelPtr = 0;
playbackChannel channels[MAXCHANNELS];
int             maxChannels = 0;

// Globals for sample banks
int             sampleBankPtr = 0;
sampleBank      banks[MAXSAMPLES];

// Globals for compressor
float     compressor;


void quitHandler() {

 if (RECORDTODISK==TRUE) {
  if (!SILENT) printf("Closing output WAV...\n");
  sf_close (renderFile);
 }
 if (!SILENT) printf("Bye\n");
 exit(0);

}

void bomb(char *msg) {
 printf("FATAL ERROR: %s\n", msg);
 exit(-1);
}

int findNextWavNumber() {

 // Starts with 1, if a file called MSG-out-1.wav exists, adds
 // 1 until it finds a file that doesnt exist, returns that number

 int num = 0;
 char *filename;
 int result = -1;
 int myerr = 0;
 struct stat mystat;

 filename=malloc(1024);
 while (myerr != ENOENT) {
  num++;
  sprintf(filename,"MSG-out-%i.wav",num);
  result=stat(filename,&mystat);
  myerr=errno;
 }

 free(filename);

 return num;

}

void about() {

 if (!SILENT) { 
  printf("\n");
  printf("          MSG - software sampler v1.09\n");
  printf("(c) copyright - 2001\n");
  printf("  adrian ward - slub\n");
  printf(" ade@slub.org - http://www.slub.org/\n\n");
  printf("Distributed under the terms and conditions of the GNU General Public License\n\n");
 }

}

void loadSample(char *file,char *name) {

 SNDFILE *waveFile;
 SF_INFO waveFileInfo;
 int     memReq;
 int     i;
 int     loaded=-1;
 int     freeBank;

 freeBank=-1;
 for (i=0;i<MAXSAMPLES;i++) {
  if ((banks[i].loaded != TRUE) && (freeBank == -1)) {
   freeBank=i;
  }
 }

 if (freeBank == -1) {
  if (!SILENT) printf("There are no more sample banks available (reached the maximum of %i)\n",MAXSAMPLES);
 } else {

  sampleBankPtr=freeBank;

  for(i=0;i<sampleBankPtr;i++){
   if (strcmp(name,banks[i].name)==0) {
    loaded=i;
   }
  }

  if (loaded != -1) {

   if (!SILENT) printf("A sound called %s is already loaded. Ignoring.\n",name);

  } else {

   if (!SILENT) printf("Loading %s\n",file);

   if ((waveFile=sf_open_read(file, &waveFileInfo)) == NULL) {
    if (!SILENT) printf("Error: Couldnt open audio sample\n");
   } else {

    memReq=waveFileInfo.samples*waveFileInfo.channels*(waveFileInfo.pcmbitwidth/8);

    banks[sampleBankPtr].samples = malloc(memReq);
    if (sf_read_short(waveFile,banks[sampleBankPtr].samples,waveFileInfo.samples) != waveFileInfo.samples) {
     if (!SILENT) printf("WARNING: Didn't read entire sample file into memory!\n");
    }

    if (!SILENT) { 
     printf("Sample %s has the following structure:\n  samplerate=%i\n  samples=%i\n  channels=%i\n  pcmbitwidth=%i\n  format=%i\n  sections=%i\n",
             file,
             waveFileInfo.samplerate,
             waveFileInfo.samples,
             waveFileInfo.channels,
             waveFileInfo.pcmbitwidth,
             waveFileInfo.format,
             waveFileInfo.sections);
    }

    banks[sampleBankPtr].isSynth=0;
    banks[sampleBankPtr].size=waveFileInfo.samples;
    banks[sampleBankPtr].name=malloc(1024);
    banks[sampleBankPtr].loaded=TRUE;
    strcpy(banks[sampleBankPtr].name,name);

    sf_close(waveFile);

    if (!SILENT) printf("Bank %i loaded, named as %s, occupying %i bytes\n",sampleBankPtr,name,memReq);

   }

  } 

 }

}


float characterToFloat(char *codebit) {

 if (strncmp(codebit,"a",1)==0) { return 0.001; }
 if (strncmp(codebit,"b",1)==0) { return 0.04; }
 if (strncmp(codebit,"c",1)==0) { return 0.08; }
 if (strncmp(codebit,"d",1)==0) { return 0.12; }
 if (strncmp(codebit,"e",1)==0) { return 0.16; }
 if (strncmp(codebit,"f",1)==0) { return 0.20; }
 if (strncmp(codebit,"g",1)==0) { return 0.24; }
 if (strncmp(codebit,"h",1)==0) { return 0.28; }
 if (strncmp(codebit,"i",1)==0) { return 0.32; }
 if (strncmp(codebit,"j",1)==0) { return 0.36; }
 if (strncmp(codebit,"k",1)==0) { return 0.40; }
 if (strncmp(codebit,"l",1)==0) { return 0.44; }
 if (strncmp(codebit,"m",1)==0) { return 0.48; }
 if (strncmp(codebit,"n",1)==0) { return 0.52; }
 if (strncmp(codebit,"o",1)==0) { return 0.56; }
 if (strncmp(codebit,"p",1)==0) { return 0.60; }
 if (strncmp(codebit,"q",1)==0) { return 0.64; }
 if (strncmp(codebit,"r",1)==0) { return 0.68; }
 if (strncmp(codebit,"s",1)==0) { return 0.72; }
 if (strncmp(codebit,"t",1)==0) { return 0.76; }
 if (strncmp(codebit,"u",1)==0) { return 0.80; }
 if (strncmp(codebit,"v",1)==0) { return 0.84; }
 if (strncmp(codebit,"w",1)==0) { return 0.88; }
 if (strncmp(codebit,"x",1)==0) { return 0.92; }
 if (strncmp(codebit,"y",1)==0) { return 0.96; }
 if (strncmp(codebit,"z",1)==0) { return 0.999; }

 printf("characterToFloat: no matching result for '%s'\n",codebit);

 return 0.0;

}


void codeToSynthParams(int bankNum, char *code) {

 // Converts the four-letter synth code to the correct
 // parameters in the appropriate synthImplementation structure
 // Needs the bank number (so cannot be done on unmade synths)
 
 char *tempC;

 banks[bankNum].isSynth = 1;

 // wave type (s=0=sine t=1=triangle q=2=square r=3=random)
 banks[bankNum].synth.wave = 0;
 if (strncmp(code,"t",1)==0) { banks[bankNum].synth.wave = 1; }
 if (strncmp(code,"q",1)==0) { banks[bankNum].synth.wave = 2; }
 if (strncmp(code,"r",1)==0) { banks[bankNum].synth.wave = 3; }

 // pulse width
 tempC = code;

 tempC += 1;
 banks[bankNum].synth.targetOffset = characterToFloat(tempC);

 tempC += 1;
 banks[bankNum].synth.targetAmp    = characterToFloat(tempC);

 tempC += 1;
 banks[bankNum].synth.targetDecay  = characterToFloat(tempC);

}


void renderSynth(int bankNum) {

 float  tempSamples[400];
 float  tempSamples2[400];
 float  amp;
 float  decay;
 float  inc;
 float  d;
 float  da;
 int    i;
 short *smps;
 int    offset;

 // Fills the (supposedly) 336 sample buffer with a waveform, informed by
 // the synth params held in the synthImplementation struct.

 amp=1;
 decay=banks[bankNum].synth.decay * banks[bankNum].synth.decay;
 offset = (banks[bankNum].synth.offset * 335.0);
 inc    =  banks[bankNum].synth.amp;

 if (DEBUG) {
  printf("renderSynth:wave is %i\n",banks[bankNum].synth.wave);
  printf("renderSynth:offset is %i\n",offset);
  printf("renderSynth:amp is %f\n",inc);
  printf("renderSynth:decay is %f\n",decay);
 }

 if (banks[bankNum].synth.wave == 0) {
   // sine wave
   for (i=0; i<336; i++) {
    tempSamples[i] = sin( ((float) i) * (6.282 / 336) ) * amp;
    amp = amp * (1-decay);
   }
 }

 if (banks[bankNum].synth.wave == 1) {
   // triangle wave
   d  = 0;
   da = 0.0119047619;
   printf("renderSynth:d is %f,da is %f\n",d,da);
   for (i=0; i<336; i++) {
    tempSamples[i] = d;
    d = d + da;
    if (d >= amp) {
     d  = amp;
     da = 0-da;
    }
    if (d <= 0-amp) {
     d  = 0-amp;
     da = 0-da;
    }
    amp = amp * (1-decay);
   }
 }

 if (banks[bankNum].synth.wave == 2) {
  // square wave
  for (i=0; i<336; i++) {
   if (i< ((offset+168) % 336) ) {
    tempSamples[i] = 0-amp;
   } else {
    tempSamples[i] = amp;
   }
   amp = amp * (1-decay);
  }
 }

 if (banks[bankNum].synth.wave == 3) {
  // random
  for(i=0; i<336; i++) {
   tempSamples[i] = (((((float) rand()) / ((float) RAND_MAX) ) * 2)-1)*amp;
   amp = amp * (1-decay);
  }
 }

 // Apply pulse
 smps   =  banks[bankNum].samples;
 for (i=0; i<336; i++) {
  tempSamples2[i] = (tempSamples[i] * (1-inc)) - (tempSamples[offset] * inc);
  offset++;
  if (offset > 335) { offset = 0; }
 }

 // Normalise and copy to main buffer
 d=0;
 for (i=0; i<336; i++) {
  if (fabs(tempSamples2[i]) > d) {
   d = fabs(tempSamples2[i]);
  }
 }
 printf("renderSynth:d(maxpoint) is %f, multiplier should be %f\n",d,(32760/d));
 for (i=0; i<336; i++) {
  *smps = tempSamples2[i] * (32760/d);
  smps++;
 }

}


void makeSample(char *name,char *code) {

 int     memReq;
 int     i;
 int     loaded=-1;
 int     freeBank;
 
 freeBank=-1;
 for (i=0;i<MAXSAMPLES;i++) {
  if ((banks[i].loaded != TRUE) && (freeBank == -1)) {
   freeBank=i;
  }
 }
 
 if (freeBank == -1) {
  if (!SILENT) printf("There are no more sample banks available (reached the maximum of %i)\n",MAXSAMPLES);
 } else {
 
  sampleBankPtr=freeBank;
 
  for(i=0;i<sampleBankPtr;i++){
   if (strcmp(name,banks[i].name)==0) {
    loaded=i;   
   }
  }
 
  if (loaded != -1) {
 
   if (!SILENT) printf("Modifying existing synth %s, in bank %i.\n",name,loaded);

   codeToSynthParams(loaded,code);

   // XXX This shouldn't happen here, we should smoothly interpolate and rerender
   // the synth, but the speed param hasn't been done yet, so just copy the target
   // settings across
   banks[loaded].synth.offset = banks[loaded].synth.targetOffset;
   banks[loaded].synth.amp    = banks[loaded].synth.targetAmp;  
   banks[loaded].synth.decay  = banks[loaded].synth.targetDecay;
   
   // Render synth waveform   
   renderSynth(loaded);

  } else {
  
   memReq=672; // 336 samples, 16-bit mono
 
   banks[sampleBankPtr].samples = malloc(memReq);

   // Generate the sample now
   codeToSynthParams(sampleBankPtr,code);

   // As this is a new sound, we copy the target values to the actual values to start with
   banks[sampleBankPtr].synth.offset = banks[sampleBankPtr].synth.targetOffset;
   banks[sampleBankPtr].synth.amp    = banks[sampleBankPtr].synth.targetAmp;
   banks[sampleBankPtr].synth.decay  = banks[sampleBankPtr].synth.targetDecay;

   // Render synth waveform
   renderSynth(sampleBankPtr);

   banks[sampleBankPtr].isSynth=1;
   banks[sampleBankPtr].size=336;
   banks[sampleBankPtr].name=malloc(1024);
   banks[sampleBankPtr].loaded=TRUE;
   strcpy(banks[sampleBankPtr].name,name);
    
   if (!SILENT) printf("Bank %i loaded, named as %s, occupying %i bytes\n",sampleBankPtr,name,memReq);
    
  }
             
 }



}



void unloadSample(char *name) {

 int i;
 int found;

 found = -1;

 for (i=0; i<MAXSAMPLES; i++) {
  if ((banks[i].name != NULL) && (banks[i].loaded == TRUE)) {
   if (strcmp(banks[i].name,name) == 0) {
    found=i;
   }
  }
 }

 if (found == -1) {
  if (!SILENT) printf("A sound called %s could not be found.\n",name);
 } else {
  // Need to dealloc memory
  free(banks[found].name);
  free(banks[found].samples);
  banks[found].loaded=FALSE;
  banks[found].size=0;
 }

}


int findNewChannel() {

 // Search through all channels, pick one that is inactive

 int found = -1;
 int i;
 int oldestAge = 0;

 for (i=0; i<MAXCHANNELS; i++) {
  if ((channels[i].active == FALSE) && (found == -1)) {
   found=i;
  }
 } 

 if (found == -1) {
  // No free channels, so cull the oldest
  for (i=0; i<MAXCHANNELS; i++) {
   if (channels[i].age > oldestAge) {
    found=i;
    oldestAge=channels[i].age;
   }
  }
 }

 if (found == -1) {
  if (!SILENT) printf("WARNING: There was a problem allocating a channel. Using the first channel.\n");
  found=1;
 }

 return found;

}


float findSpeedForPitch (float pitch) {

 // Looks in the pitchSpeeds[] array and finds the right value
 // if negative, then use absolute index value and negative returned speed
 // if floating point, then get fraction between both parts

 float myPitch;
 int   myPitchInt;
 float myPitchFloat;
 float mySpeed;

 myPitch      = fabs(pitch);
 myPitchInt   = (int) myPitch;
 myPitchFloat = myPitch - ((float) myPitchInt);

 // ratio is linear, although I know this isn't correct
 // I think you would have to be superman to know the difference

 mySpeed      = (pitchSpeeds[myPitchInt  ] * (1 - myPitchFloat))
              + (pitchSpeeds[myPitchInt+1] * (    myPitchFloat));

 if (pitch < 0) {
  // Original pitch was negative, so our speed should be negative too
  mySpeed = 0 - mySpeed;
 }

 return mySpeed;

}

void triggerBank (char *name, float pitch, float pan, float res, float cutoff, float amp, float decay, float rmfreq, float rmamp,
                  float loopstart, float loopend, char *thegroupname) {

 int bankNum = -1;
 int i;

 for (i = 0; i < MAXSAMPLES; i++) {
  if ((banks[i].name != NULL) && (banks[i].loaded == TRUE)) {
   if (strcmp(banks[i].name,name) == 0) {
    bankNum = i;
   }
  }
 }

 if (bankNum == -1) {
  if (!SILENT) printf("ERROR: No sample called %s was previously loaded (use load)\n",name);
 } else {

  channelPtr = findNewChannel();

  channels[channelPtr].age        = 0;
  channels[channelPtr].position   = 0.0;
  channels[channelPtr].env.volume = amp;
  channels[channelPtr].env.decay  = decay;
  channels[channelPtr].pan        = pan;
  channels[channelPtr].bank       = bankNum;
  channels[channelPtr].speed      = findSpeedForPitch(pitch);
  sprintf(channels[channelPtr].group,"%s",thegroupname);

  if (channels[channelPtr].speed < 0) {
   // This is a backwards sound, it should start at the end
   channels[channelPtr].position  = banks[bankNum].size;
  }

  channels[channelPtr].loopStart  = loopstart;
  channels[channelPtr].loopEnd    = loopend;

  if (banks[bankNum].isSynth == 1) {
   channels[channelPtr].loopStart = 0;
   channels[channelPtr].loopEnd   = 336;
  }

  if (loopend > banks[bankNum].size) {
   channels[channelPtr].loopEnd   = banks[bankNum].size;
  }

  channels[channelPtr].vcf.res    = res;
  channels[channelPtr].vcf.cutoff = cutoff;

  channels[channelPtr].ringmodulator.frequency = rmfreq;
  channels[channelPtr].ringmodulator.amplitude = rmamp;
  channels[channelPtr].ringmodulator.ctr       = 0;

  // Init the VCFimplementation stuff
  // Muchos thanx to http://www.smartelectronix.com/musicdsp/filters.php
  // and Douglas Repetto for all this cool filter stuff!
  // y4 is the output!

  channels[channelPtr].vcf.f     = 2 * cutoff / SAMPLERATE;
  channels[channelPtr].vcf.k     = 3.6 * channels[channelPtr].vcf.f - 1.6 * channels[channelPtr].vcf.f * channels[channelPtr].vcf.f -1;
  channels[channelPtr].vcf.p     = (channels[channelPtr].vcf.k+1) * 0.5;
  channels[channelPtr].vcf.scale = exp((1-channels[channelPtr].vcf.p)*1.386249);
  channels[channelPtr].vcf.r     = res * channels[channelPtr].vcf.scale;
  channels[channelPtr].vcf.y1    = 0;
  channels[channelPtr].vcf.y2    = 0;
  channels[channelPtr].vcf.y3    = 0;
  channels[channelPtr].vcf.y4    = 0;
  channels[channelPtr].vcf.oldx  = 0;
  channels[channelPtr].vcf.oldy1 = 0;
  channels[channelPtr].vcf.oldy2 = 0;
  channels[channelPtr].vcf.oldy3 = 0;

  channels[channelPtr].active    = TRUE;

 }

}

float renderVCF(int ch,float input) {

 // http://www.smartelectronix.com/musicdsp/filters.php

 channels[ch].vcf.x  = input - channels[ch].vcf.r * channels[ch].vcf.y4;
 
 channels[ch].vcf.y1 = channels[ch].vcf.x  * channels[ch].vcf.p + channels[ch].vcf.oldx  * channels[ch].vcf.p - channels[ch].vcf.k * channels[ch].vcf.y1;
 channels[ch].vcf.y2 = channels[ch].vcf.y1 * channels[ch].vcf.p + channels[ch].vcf.oldy1 * channels[ch].vcf.p - channels[ch].vcf.k * channels[ch].vcf.y2;
 channels[ch].vcf.y3 = channels[ch].vcf.y2 * channels[ch].vcf.p + channels[ch].vcf.oldy2 * channels[ch].vcf.p - channels[ch].vcf.k * channels[ch].vcf.y3;
 channels[ch].vcf.y4 = channels[ch].vcf.y3 * channels[ch].vcf.p + channels[ch].vcf.oldy3 * channels[ch].vcf.p - channels[ch].vcf.k * channels[ch].vcf.y4;

 channels[ch].vcf.y4 = channels[ch].vcf.y4 - pow(channels[ch].vcf.y4,3) / 6;

 channels[ch].vcf.oldx  = channels[ch].vcf.x;
 channels[ch].vcf.oldy1 = channels[ch].vcf.y1;
 channels[ch].vcf.oldy2 = channels[ch].vcf.y2;
 channels[ch].vcf.oldy3 = channels[ch].vcf.y3;

 return channels[ch].vcf.y4;

}

float renderRingModulator(int ch,float input) {

 float t;
 float a;
 
 t=sin(channels[ch].ringmodulator.ctr / (SAMPLERATE / (channels[ch].ringmodulator.frequency * 6.282)));
 channels[ch].ringmodulator.ctr++;

 a=channels[ch].ringmodulator.amplitude;

 return (input * (1-a)) + ((t * input) * a);

}

stereo renderPlaybackChannels() {
 
 stereo  tempSample;
 short  *tempPtr;
 float   monoSample;
 int      i;
 int      c=0;

 sampleCtr++;  

 if (hum > 0) {
  monoSample   = sin(sampleCtr/  (SAMPLERATE / (humfreq*6.282)))*(hum/1000.0);
 } else {
  monoSample   = 0;
 }

 if (hiss > 0) {
  tempSample.l = monoSample+((((float)rand())/(((float)RAND_MAX)*1000.0)/100.0) * hiss);
  tempSample.r = monoSample+((((float)rand())/(((float)RAND_MAX)*1000.0)/100.0) * hiss);
 } else {
  tempSample.l = monoSample;
  tempSample.r = monoSample;
 }

 for (i = 0; i < MAXCHANNELS; i++) {

  if (channels[i].active == TRUE) {

   tempPtr=banks[channels[i].bank].samples;
   tempPtr+=(int) channels[i].position;

   monoSample = (((float) *tempPtr) / 32767) * channels[i].env.volume;
   if ((channels[i].vcf.res > 0) || (channels[i].vcf.cutoff < SAMPLERATE)) {
    monoSample = renderVCF(i,monoSample);
   }
   if (channels[i].ringmodulator.amplitude > 0) {
    monoSample = renderRingModulator(i,monoSample);
   }

   tempSample.l      += monoSample * (  channels[i].pan);
   tempSample.r      += monoSample * (1-channels[i].pan);

   c++;

   channels[i].age++;
   channels[i].position   += channels[i].speed;

   if ((channels[i].loopEnd > 0) && (channels[i].speed > 0)) {
    // this sample should loop, check if gone past loopend
    if (channels[i].position > channels[i].loopEnd) {
     channels[i].position -= (channels[i].loopEnd - channels[i].loopStart);
    }
   }

   if ((channels[i].loopEnd > 0) && (channels[i].speed < 0)) {
    // and also before the start (if going backwards)
    if (channels[i].position < channels[i].loopStart) {
     channels[i].position += (channels[i].loopEnd - channels[i].loopStart);
    }
   }

  }

  channels[i].env.volume*=((channels[i].env.decay - 1.0) / 1000) + 1.0;
  if (
      (channels[i].env.volume < 0.0001) || 
      (channels[i].position>banks[channels[i].bank].size) ||
      (channels[i].position<channels[i].loopStart)
     ) {
   channels[i].active = FALSE;
  }

 }

 compressor += (float) compressorgain / 100000;

 // Get the loudest of the two channels (for the compressor, which is mono)
 monoSample=tempSample.l;
 if (fabs(tempSample.r)>fabs(monoSample)) { monoSample=tempSample.r; }

 if (fabs(monoSample * compressor) > 1.0) {
  compressor = compressor / fabs(monoSample * compressor);
 }

 tempSample.l *= compressor;
 tempSample.r *= compressor;

 if (tempSample.l >  1.0) { tempSample.l =  1.0; }
 if (tempSample.l < -1.0) { tempSample.l = -1.0; }
 if (tempSample.r >  1.0) { tempSample.r =  1.0; }
 if (tempSample.r < -1.0) { tempSample.r = -1.0; }

 maxChannels = c;

 return tempSample;

}

void * audioThread(void *arg) {

 short* outPtr;
 short* outDataStartPtr;
 stereo tempSample;
 double tmp[2];
 int    i;
 char*  wavFileName;
 int    wavFileNumber;

 if (RECORDTODISK==TRUE) {

  wavFileNumber=findNextWavNumber();
 
  wavFileName=malloc(1024);
  sprintf(wavFileName,"MSG-out-%i.wav",wavFileNumber);

  memset(&renderSFinfo,0,sizeof(renderSFinfo)); // Blank sfinfo memory
 
  renderSFinfo.samplerate=SAMPLERATE;
  renderSFinfo.samples=9999999;
  renderSFinfo.pcmbitwidth=16;
  renderSFinfo.channels=2;
  renderSFinfo.format=(SF_FORMAT_WAV | SF_FORMAT_PCM);

  if (! (renderFile = sf_open_write(wavFileName,&renderSFinfo))) {
   if (!SILENT) printf("Couldn't create %s",wavFileName);
   RECORDTODISK=FALSE;
  } else {
   if (!SILENT) printf("Recording audio to disk as %s\n",wavFileName);
  }

 }

 outDataStartPtr = malloc(BUFFERSIZE*2*2L);
 while (1==1) {
  outPtr = outDataStartPtr;
  for (i=0; i<BUFFERSIZE; i++) {

   tempSample = renderPlaybackChannels();

   if (RECORDTODISK==TRUE) {
    tmp[0]  = (double) (tempSample.l * 32700);
    tmp[1]  = (double) (tempSample.r * 32700);
    sf_write_double(renderFile,&tmp[0],2,0);
   }

   *outPtr++ = tempSample.l * 32700.0; // L
   *outPtr++ = tempSample.r * 32700.0; // R

  }
  write(devHandle,outDataStartPtr,BUFFERSIZE*2*2);
 }

}

void init() {

/*

buffer sizes:

08    8        256
09    9        512
0a = 10 > 2^ = 1024
0b   11        2048
0c   12        4096   (default)
0d   13        8192   (new - try to avoid crackling problem)
0e   14       16384
0f   15
10   16

*/

  int setting    = 0x0003000B; // 3 fragments (???), 4kb buffer (8192=2 ^ 0x0D)
  int schannels  = CHANNELS;
  int format     = FORMAT;
  int sampleRate = SAMPLERATE;
  int i;

  hiss           = DEFHISS;
  humfreq        = DEFHUMFREQ;
  hum            = DEFHUM;
  compressorgain = DEFCOMPRESSORGAIN;

  // Init the pitchSpeeds
  pitchSpeeds[  0]=1.00000 / 32.0; // Equal temperament ratios from
  pitchSpeeds[  1]=1.05946 / 32.0; // http://www.phy.mtu.edu/~suits/scales.html
  pitchSpeeds[  2]=1.12246 / 32.0; // These are 1/32 of base octave
  pitchSpeeds[  3]=1.18921 / 32.0; // (32 = 2^5 = 5 octaves down)
  pitchSpeeds[  4]=1.25992 / 32.0;
  pitchSpeeds[  5]=1.33483 / 32.0;
  pitchSpeeds[  6]=1.41421 / 32.0;
  pitchSpeeds[  7]=1.49831 / 32.0;
  pitchSpeeds[  8]=1.58740 / 32.0;
  pitchSpeeds[  9]=1.68179 / 32.0;
  pitchSpeeds[ 10]=1.78180 / 32.0;
  pitchSpeeds[ 11]=1.88775 / 32.0;

  for (i=12; i<3000; i++) {
   pitchSpeeds[i]=pitchSpeeds[i-12]*2;
  }

  // Make sure all sample banks start off unloaded
  for (i=0; i<MAXSAMPLES; i++) {
   banks[i].loaded=FALSE;
  }

  // Make sure all playback heads start off silent
  for (i=0; i<MAXCHANNELS; i++) {
   channels[i].group=malloc(1024);
   channels[i].active=FALSE;
  }

  compressor = 1.0;

  about();

  if (!SILENT) printf("Opening /dev/dsp\n");
  if ((devHandle=open("/dev/dsp1",O_WRONLY)) == -1) { bomb("Problem opening /dev/dsp"); }

  if (!SILENT) printf("Configuring audio\n");
  if (ioctl(devHandle,SNDCTL_DSP_SETFRAGMENT,&setting   ) == -1) { bomb("Failed SNDCTL_DSP_SETFRAGMENT"); }
  if (ioctl(devHandle,SNDCTL_DSP_STEREO     ,&schannels ) == -1) { bomb("Failed SNDCTL_DSP_STEREO");      }
  if (ioctl(devHandle,SNDCTL_DSP_SETFMT     ,&format    ) == -1) { bomb("Failed SNDCTL_DSP_SETFMT");      }
  if (ioctl(devHandle,SNDCTL_DSP_SPEED      ,&sampleRate) == -1) { bomb("Failed SNDCTL_DSP_SPEED");       }
  
  if (!SILENT) printf("Creating audio thread\n");
  if (pthread_create(&audioThreadHnd, NULL, audioThread, NULL) != 0) { bomb("Couldn't create thread"); }
  if (pthread_detach(audioThreadHnd)                        != 0) { bomb("Couldn't detach thread"); }

}


void processCmd(char *cmd) {

 char *params[100];
 char *l;
 int   pPtr = 0;
 int   i;
 char  groupName[1024];
 float pitch;
 float pan;
 float res;
 float cutoff;
 float amp;
 float decay;
 float rmfreq;
 float rmamp;
 float loopstart;
 float loopend;

 int   pitchCh     = FALSE;
 int   panCh       = FALSE;
 int   resCh       = FALSE;
 int   cutoffCh    = FALSE;
 int   ampCh       = FALSE;
 int   decayCh     = FALSE;
 int   rmfreqCh    = FALSE;
 int   rmampCh     = FALSE;
 int   loopstartCh = FALSE;
 int   loopendCh   = FALSE;
 int   groupCh     = FALSE;
 
 if (DEBUG) { printf("processCmd:strtok\n"); }
 
 // Return first opcode
 l=strtok(cmd," ");
 
 // While l is a valid token, store it in params[] and get the next one
 while (l != NULL) {
  params[pPtr]=l;
  pPtr++;
  l=strtok(NULL," ");
 }

 if (pPtr > 0) {

  if (DEBUG) { printf("processCmd:initParams\n"); }

  sprintf(groupName,"default");
  pitch     = 60.0; // Default pitch is speed = 1
  pan       = 0.5;  // Default pan is 0.5 (middle)
  res       = 0.0;
  cutoff    = SAMPLERATE;
  amp       = 1.0;
  decay     = 1.0;
  rmfreq    = 1000;
  rmamp     = 0.0;
  loopstart = 0.0;
  loopend   = 0.0;

  if (DEBUG) { printf("processCmd:lookParams[%i]\n",pPtr); }

  // Look for optional parameters
  for (i=1; i<pPtr; i++) {
   if (strcmp(params[i],"pitch"    )==0) { pitch    =atof(params[i+1]); pitchCh     = TRUE; }
   if (strcmp(params[i],"pan"      )==0) { pan      =atof(params[i+1]); panCh       = TRUE; }
   if (strcmp(params[i],"res"      )==0) { res      =atof(params[i+1]); resCh       = TRUE; }
   if (strcmp(params[i],"cutoff"   )==0) { cutoff   =atof(params[i+1]); cutoffCh    = TRUE; }
   if (strcmp(params[i],"rmfreq"   )==0) { rmfreq   =atof(params[i+1]); rmfreqCh    = TRUE; }
   if (strcmp(params[i],"rmamp"    )==0) { rmamp    =atof(params[i+1]); rmampCh     = TRUE; }
   if (strcmp(params[i],"loopstart")==0) { loopstart=atof(params[i+1]); loopstartCh = TRUE; }
   if (strcmp(params[i],"loopend"  )==0) { loopend  =atof(params[i+1]); loopendCh   = TRUE; }
   if (strcmp(params[i],"vol"      )==0) { amp      =atof(params[i+1])/128; ampCh   = TRUE; }
   if (strcmp(params[i],"decay"    )==0) { decay    =atof(params[i+1])/100; decayCh = TRUE; }
   if (strcmp(params[i],"in"       )==0) { sprintf(groupName,"%s",params[i+1]); groupCh = TRUE; }
  }

  // Look for commands

  if (DEBUG) { printf("processCmd:about\n"); }
  if (strcmp(params[0],"about")==0) { about(); }

  if (DEBUG) { printf("processCmd:banks\n"); }
  if (strcmp(params[0],"banks")==0) {
   // List all loaded samples
   for (i=0; i<MAXSAMPLES; i++) {
    if (banks[i].loaded == TRUE) {
     if (banks[i].isSynth == 1) {
      if (!SILENT) printf("Bank %i = synth '%s' occupying %i bytes, type %i, pulse width %f, pulse amplitude %f, wave decay %f at speed %f\n",
               i,banks[i].name,banks[i].size,banks[i].synth.wave,banks[i].synth.offset,banks[i].synth.amp,banks[i].synth.decay,banks[i].synth.speed);
     } else {
      if (!SILENT) printf("Bank %i = '%s' occupying %i bytes\n",i,banks[i].name,banks[i].size);
     }
    }
   }
  }

  if (DEBUG) { printf("processCmd:sounds\n"); }
  if (strcmp(params[0],"sounds")==0) {
   // List all playing sounds
   for (i=0; i<MAXCHANNELS; i++) {
    if (channels[i].active == TRUE) {
     if (!SILENT) { 
      printf("Channel %i = '%s' (bank %i) in group '%s', pitch %f pan %f vol %f decay %f\n",
             i,
             banks[channels[i].bank].name,
             channels[i].bank,
             channels[i].group,
             channels[i].speed,
             channels[i].pan,
             channels[i].env.volume, 
             channels[i].env.decay
            );
     }
    }
   }
  }

  if (DEBUG) { printf("processCmd:panic\n"); }
  if (strcmp(params[0],"panic")==0) {
   if (groupCh == TRUE) {
    // A group was specified, silence only sounds in that group
    for (i=0; i<MAXCHANNELS; i++) {
     if (channels[i].active == TRUE) {
      if (strcmp(groupName,channels[i].group)==0) {
       channels[i].active = FALSE;
      }
     }
    }
   } else {
    // No specific group was given, silence all channels
    for (i=0; i<MAXCHANNELS; i++) {
     channels[i].active=FALSE;
    }
   }
  }

  if (DEBUG) { printf("processCmd:unload\n"); }
  if (strcmp(params[0],"unload")==0) {
   if (pPtr >= 2) { unloadSample(params[1]); }
   else           { printf("ERROR: Insufficient parameters: unload [name]\n"); }
  }

  if (DEBUG) { printf("processCmd:load\n"); }
  if (strcmp(params[0],"load" )==0) {
   if (pPtr >= 4) { loadSample(params[1],params[3]); }
   else           { printf("ERROR: Insufficient parameters: load [file] as [name]\n"); }
  }

  if (DEBUG) { printf("processCmd:make\n"); }
  if (strcmp(params[0],"make")==0) {
   if (pPtr >= 2) { makeSample(params[1],params[3]); }
   else           { printf("ERROR: Insufficient parameters: make [name] from [synthcode]\n"); }
  }

  if (DEBUG) { printf("processCmd:play\n"); }
  if (strcmp(params[0],"play" )==0) {
   if (pPtr >= 2) { triggerBank(params[1],pitch,pan,res,cutoff,amp,decay,rmfreq,rmamp,loopstart,loopend,groupName); }
   else           {
    if (!SILENT) { 
     printf("ERROR: Insufficient parameters: play [name]\n");
     printf("           Optional parameters: pitch [pitch 0..127]\n");
     printf("                                vol [vol 0..127]\n");
     printf("                                decay [decay percentage]\n");
     printf("                                pan [pan 0..1]\n");
     printf("                                res [resonance 0..1]\n");
     printf("                                cutoff [freq	(hz) 0..44100]\n");
     printf("                                rmfreq [ring modulator frequency (hz) 0..44100]\n");
     printf("                                rmamp [ring modular amplitude 0..1]\n");
     printf("                                loopstart [sample offset]\n");
     printf("                                loopend [sample offset]\n");
     printf("                                in [groupname]\n");
    }
   }
  }

  if (DEBUG) { printf("processCmd:change\n"); }
  if (strcmp(params[0],"change")==0) {
   // Look for all sounds with the .group matching params[1]
   // and for each param with a Ch value of TRUE, update those values
   if (pPtr >= 2) {
    for (i=0; i<MAXCHANNELS; i++) {
     if (channels[i].active == TRUE) {
      if (strcmp(params[1],channels[i].group)==0) {
       // This one matches
       if (pitchCh     == TRUE) { channels[i].speed      = findSpeedForPitch(pitch); }
       if (ampCh       == TRUE) { channels[i].env.volume = amp;    }
       if (decayCh     == TRUE) { channels[i].env.decay  = decay;  }
       if (panCh       == TRUE) { channels[i].pan        = pan;    }
       if (resCh       == TRUE) { channels[i].vcf.res    = res;    }
       if (cutoffCh    == TRUE) { channels[i].vcf.cutoff = cutoff; }
       if (rmfreqCh    == TRUE) { channels[i].ringmodulator.frequency = rmfreq; } 
       if (rmampCh     == TRUE) { channels[i].ringmodulator.amplitude = rmamp;  }
       if (loopstartCh == TRUE) { channels[i].loopStart = loopstart; }
       if (loopendCh   == TRUE) { channels[i].loopEnd   = loopend;   }

       // might need to reinit vcf stuff
       if ((resCh == TRUE) || (cutoffCh == TRUE)) {
        channels[i].vcf.f     = 2 * channels[i].vcf.cutoff / SAMPLERATE;
        channels[i].vcf.k     = 3.6 * channels[i].vcf.f - 1.6 * channels[i].vcf.f * channels[i].vcf.f -1;
        channels[i].vcf.p     = (channels[i].vcf.k+1) * 0.5;
        channels[i].vcf.scale = exp((1-channels[i].vcf.p)*1.386249);
        channels[i].vcf.r     = channels[i].vcf.res * channels[i].vcf.scale;
       }
      }
     }
    }
   } else {
    if (!SILENT) { 
     printf("ERROR: Insufficient parameters: change [groupname]\n");
     printf("           Optional parameters: pitch [pitch 0..127]\n");
     printf("                                vol [vol 0..127]\n");
     printf("                                decay [decay percentage]\n");
     printf("                                pan [pan 0..1]\n");
     printf("                                res [resonance 0..1]\n");
     printf("                                cutoff [freq (hz) 0..44100]\n");
     printf("                                rmfreq [ring modulator frequency (hz) 0..44100]\n");
     printf("                                rmamp [ring modular amplitude 0..1]\n");
     printf("                                loopstart [sample offset]\n");
     printf("                                loopend [sample offset]\n");
    }
   }
  } 

  if (DEBUG) { printf("processCmd:sleep\n"); }
  if (strcmp(params[0],"sleep")==0) {
   if (pPtr >= 2) { usleep(atof(params[1])*1000); }
   else           { printf("ERROR: Insufficient parameters: sleep [thousand-seconds]\n"); }
  }

  if (DEBUG) { printf("processCmd:hiss\n"); }
  if (strcmp(params[0],"hiss")==0) {
   if (pPtr >= 2) { hiss=atof(params[1]); }
   else           { printf("ERROR: Insufficient parameters: hiss [hiss 0..100%%]\n"); }
  }

  if (DEBUG) { printf("processCmd:hum\n"); }
  if (strcmp(params[0],"hum")==0) {
   if (pPtr >= 2) { hum=atof(params[1]); }
   else           { printf("ERROR: Insufficient parameters: hum [hum 0..100%%]\n"); }
  }

  if (DEBUG) { printf("processCmd:humfreq\n"); }
  if (strcmp(params[0],"humfreq")==0) {
   if (pPtr >= 2) { humfreq=atof(params[1]); }
   else           { printf("ERROR: Insufficient parameters: humfreq [hz]\n"); }
  }

  if (DEBUG) { printf("processCmd:compressor\n"); }
  if (strcmp(params[0],"compressor")==0) {
   if (pPtr >= 2) { compressorgain=atof(params[1]); }
   else           { printf("ERROR: Insufficient parameters: compressor [gainvalue 0..500]\n"); }
  }

 } else {

  if (DEBUG) { printf("processCmd:skipstrcmps[nullBuffer]\n"); }

 }

 if (DEBUG) { printf("processCmd:exitOkay\n"); }

}


void flushCmdBuffer() {

 // For every command in cmdBuffer, pass it to processCmd
 // and remove it from the cmdBuffer. This usually happens when
 // MSG receives a 'flush' command.

 int i;

 for (i=0; i<MAXCMDBUFFER; i++) {
  if (cmdBuffer[i] != NULL) {
   processCmd(cmdBuffer[i]);
   free(cmdBuffer[i]);
   cmdBuffer[i]=NULL;
  }
 }

}


void storeCmd(char* cmd) {

 // cmdBuffer[0..MAXCMDBUFFER]

 int i;
 int es=-1;

 for (i=0; i<MAXCMDBUFFER; i++) {
  if ((cmdBuffer[i] == NULL) && (es == -1)) {
   es = i;
  }
 }

 if (es == -1) {
  if (!SILENT) {
   printf("WARNING: Filled up command buffer. Try issuing 'flush' occasionally!\n");
   printf("         (command was ignored)\n");
  }
 } else {
  cmdBuffer[es]=malloc(1024);
  strncpy(cmdBuffer[es],cmd,1023);
 }

}


int main(int argc, char *argv[]) {

  char *tempString;
  FILE *input;
  char *newline;
  int   i;

  tempString=malloc(1024);

  if (argc > 1) {
   for (i=1; i<argc; i++) {
    if (strcmp(argv[i],"-record")==0)   { RECORDTODISK=TRUE; }
    if (strcmp(argv[i],"-silent")==0)   { SILENT=TRUE;       }
    if (strcmp(argv[i],"-nobuffer")==0) { NOBUFFER=TRUE;     }
    if (strcmp(argv[i],"-debug")==0)    { DEBUG=TRUE;        }
   }
  }

  // Install signal handlers
  signal (SIGHUP , quitHandler);
  signal (SIGTERM, quitHandler);
  signal (SIGABRT, quitHandler);
  signal (SIGKILL, quitHandler);
  signal (SIGSTOP, quitHandler);

  init();

  input=fdopen(0,"r");          // Open stdin
  setvbuf(input,NULL,_IONBF,0); // Turn off buffering

  while (1==1) {

   if (!SILENT) printf(">");
   tempString=fgets(tempString,1000,input);

   // Remove trailing newline, replace with ' ' - hence trailing spaces in following strcmp()s
   newline=strchr(tempString,10); // Look for 0x0A
   strncpy(newline," ",1);        // replace with 0x20

   if (strcmp(tempString,"flush ")==0) {
    if (!NOBUFFER) flushCmdBuffer();
    
   } else if (strcmp(tempString,"quit ")==0) {
    quitHandler();

   } else {
    if (NOBUFFER) {    
     processCmd(tempString);
    } else {
     storeCmd(tempString);
    }

   }

  }
  
  return 0;

}
