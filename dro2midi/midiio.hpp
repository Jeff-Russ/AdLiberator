#ifndef __MIDIIO__
#define __MIDIIO__

#include <stdio.h>

#ifndef MIDI_BUFSIZE
#define MIDI_BUFSIZE  1024
#endif

// opening modes of a midi file
#define WRITE_BINARY  "wb"
#define READ_BINARY   "rb"

const unsigned long MThd = 0x4D546864ul;
const unsigned long MTrk = 0x4D54726Bul;

// different standard midi formats
#define VERSION_MULTICHANNEL   0
#define VERSION_SINGLECHANNEL  1
#define VERSION_MULTISONG      2

#define OPTION_NOCONTROLS   1    // no control details but general information
#define OPTION_NOEVENTS     2    // no track events at all
#define OPTION_NOMETAEVENTS 4    // no meta details but general information
#define OPTION_NOSYSEVENTS  8    // no sysex details but general information
#define OPTION_NONOTEEVENTS 16   // no note events (8x or 9x)
#define OPTION_NOPOLYEVENTS 32   // no poly aftertouch events (Ax)
#define OPTION_NOCONTROLEVENTS 64 // no control events at all (Bx)
#define OPTION_NOPROGRAMEVENTS 128 // no program change events (Cx)
#define OPTION_NOAFTERTOUCHEVENTS 256 // no aftertouch events (Dx)
#define OPTION_NOPITCHBENDEVENTS 512 // no pitchbend events (Ex)
#define OPTION_NOREALTIMEEVENTS  1024 // no realtime events (Fx)

// getchannel delivers a valid channel or:
#define NOCHANNEL     (-1)
#define MULTICHANNEL  (-2)
#define VALIDCHANNEL(ch)  ((ch) >= 0 && (ch) <= 15)
#define gm_drumchannel 9
#define SAYCHANNEL(ch)  ((ch) + 1) // 0..15 in midi format but spoken 1..16!

// for use of param what in function text()
#define meta_text       1
#define meta_copyright  2
#define meta_trackname  3
#define meta_instrument 4
#define meta_lyric	5
#define meta_marker	6
#define meta_cuepoint	7
#define meta_endtrack   0x2f

#define ctrl_highbank 0
#define ctrl_wheel 1
#define ctrl_breath 2
#define ctrl_foot 4
#define ctrl_portamentotime 5
#define ctrl_data 6
#define ctrl_volume 7
#define ctrl_balance 10
#define ctrl_expression 11
#define ctrl_lowbank 32
#define ctrl_hold 64
#define ctrl_reverb 91
#define ctrl_chorus 93
#define ctrl_datainc 96
#define ctrl_datadec 97
#define ctrl_lowrpn  100
#define ctrl_highrpn 101
#define ctrl_resetctrlrs 121
#define ctrl_allnotesoff 123

#define balance_left   0
#define balance_center 64
#define balance_right  127

#define volume_mute    0
#define volume_full    127

#define bpm(ticks)     (60000000.0 / (ticks))
#define ticks(beats)   ((unsigned long)(60000000.0 / (beats)))

#define tempo_60bpm    (1000000L)
#define tempo_120bpm   (500000L)
#define tempo_240bpm   (250000L)


class MidiRead
{
public:
  static const char* copyright();

  MidiRead(const char* filename, FILE* f = 0);
  virtual ~MidiRead();

  FILE* getf();

  int run();
  int runhead();
  int runtrack(int trackno);
  int runevent(long trackend);

  long getpos() { return pos_; }
  long geteventpos() { return pos_; }
  long getcurpos() { return curpos_; }
  unsigned long getcurtime() { return curtime_; } // in midi units

  virtual void head(unsigned version, unsigned tracks, unsigned clicksperquarter);
  virtual void track(int trackno, long length, int channel);
  virtual void endtrack(int trackno); // closing track

  virtual void time(unsigned long ticks); // delay between events

  virtual void event(int what, int len = 0, unsigned char* data = 0);

  // these are event categories:
    virtual void meta(int what, int len, const unsigned char* data);
    // these are special meta events:
      virtual void text(int what, int len, const char* whattext, const unsigned char* txt);
      virtual void end();  // end of track command
      virtual void prefixchannel(unsigned char channel);
      virtual void prefixport(unsigned char port);
      virtual void seqnumber(unsigned int seqno);
      virtual void smpteofs(int hour, int min, int sec, int frame, int fracframe);
      virtual void tact(int nom, int denom, int clicksperbeat, int notes32perbeat);
      virtual void tempo(unsigned long microsecperbeat);
      virtual void key(int signature, int isminor); // C=0, -7=7flats +7=7sharps
    virtual void program(int channel, int prg);
    virtual void control(int channel, int what, int val); // general controls
    // special controls:
      virtual void highbank(int channel, int val);
      virtual void wheel(int channel, int val);
      virtual void breath(int channel, int val);
      virtual void foot(int channel, int val);
      virtual void portamentotime(int channel, int val);
      virtual void data(int channel, int val);
      virtual void volume(int channel, int val);
      virtual void balance(int channel, int val);
      virtual void expression(int channel, int val);
      virtual void lowbank(int channel, int val);
      virtual void hold(int channel, int val);
      virtual void reverb(int channel, int val);
      virtual void chorus(int channel, int val);
      virtual void datainc(int channel, int val);
      virtual void datadec(int channel, int val);
      virtual void lowrpn(int channel, int val);
      virtual void highrpn(int channel, int val);
      virtual void resetctrlrs(int channel, int val);
      virtual void allnotesoff(int channel, int val);
      virtual void pitchbendrange(int channel, int val);
    virtual void noteon(int channel, int note, int vel);
    virtual void noteoff(int channel, int note, int vel);
    virtual void pitchbend(int channel, int val);
    virtual void polyaftertouch(int channel, int note, int val);
    virtual void aftertouch(int channel, int val);
    virtual void songpos(unsigned pos);
    virtual void songselect(unsigned char song);
    virtual void tunerequest();
    virtual void timingclock();
    virtual void start();
    virtual void cont();
    virtual void stop();
    virtual void activesense();
    virtual void sysex(int syslen, const unsigned char* sysdata);
    // these are special sysex events:
      virtual void xgreset();
      virtual void gmreset();
      virtual void gsreset();
      virtual void gsexit();

  virtual void endmidi(); // after last track
  virtual void error(const char* msg);
  virtual void warning(const char* msg);

  virtual void percent(int perc);

  int getchannel() { return curchannel_; }
  void setchannel(int channel);

  static const char* progname(int n, int channel);
  static const char* notename(unsigned char note);

  int options_;

  int version_, tracks_, clicks_, trackno_;

  void seek(long pos);
  int getbyte();
  unsigned getword();
  unsigned long gettri();
  unsigned long getlong();
  unsigned long getdelta();
  unsigned char* get(int len);
  unsigned char* need(int nbytes);

  unsigned long microsec(unsigned long units, unsigned long msperbeats);
  long units(unsigned long microsec, unsigned long msperbeats);

  // use scanchannel only at start of track!
  int scanchannel(unsigned long maxlen); // channel 0-15 or -1=no channel or -2=multichannels

  // use sysevent only directly after reading F0 or F7
  int scansysevent(unsigned long maxlen);

protected:
  const char *midiname_;
  FILE* f_;
  unsigned char shouldclose_; // 0=no, otherwise=yes
  long filesize_;
  unsigned char buf_[MIDI_BUFSIZE];
  int buflen_, bufpos_;
  int curchannel_;
  unsigned long curtime_;
  int percent_;
  int lastcode_;
  unsigned long tracklen_;

  long pos_, curpos_;
  unsigned char curdeltalen_; // number of bytes read by recent getdelta() call
};

class MidiWrite
{
public:
  static const char* copyright();

  MidiWrite(const char* filename);
  virtual ~MidiWrite();

  FILE* getf();

  long getcurpos() { return curpos_; }
  long getcurtime() { return curtime_; }
  void cleardelta();

  void head(int version = 1, int tracks = 0, unsigned clicksperquarter = 192);
  void track();
  void endtrack();

  void event(int what, int len, const unsigned char* data);

  void text(int what, int len, const unsigned char* txt);
  void meta(int what, int len, const unsigned char* data); // 0xff ....
  virtual void prefixchannel(unsigned char channel);
  virtual void prefixport(unsigned char port);
  virtual void seqnumber(unsigned int seqno);
  virtual void smpteofs(int hour, int min, int sec, int frame, int fracframe);
  virtual void key(int signature, int isminor);
  void tact(int nom, int denom, int v1, int v2);
  void tempo(unsigned long microsecperbeat);
  void end();
  void program(int channel, int prg);
  void control(int channel, int what, int val);
  // these are special controls
    void highbank(int channel, int val);
    void wheel(int channel, int val);
    void breath(int channel, int val);
    void foot(int channel, int val);
    void portamentotime(int channel, int val);
    void data(int channel, int val);
    void volume(int channel, int val);
    void balance(int channel, int val);
    void expression(int channel, int val);
    void lowbank(int channel, int val);
    void hold(int channel, int val);
    void reverb(int channel, int val);
    void chorus(int channel, int val);
    void datainc(int channel, int val);
    void datadec(int channel, int val);
    void lowrpn(int channel, int val);
    void highrpn(int channel, int val);
    void resetctrlrs(int channel, int val);
    void allnotesoff(int channel, int val);
    void pitchbendrange(int channel, int halfnotes);

  void noteon(int channel, int note, int vel);
  void noteoff(int channel, int note, int vel = 0);
  void time(unsigned long ticks);
  void pitchbend(int channel, int val);
  void polyaftertouch(int channel, int note, int val);
  void aftertouch(int channel, int val);
  void songpos(unsigned pos);
  void songselect(unsigned char song);
  void tunerequest();
  void timingclock();
  void start();
  void cont();
  void stop();
  void activesense();
  void sysex(int syslen, const unsigned char* sysdata);
  void xgreset();
  void gmreset();
  void gsreset();
  void gsexit();

  void putbyte(unsigned char val);
  void putcode(unsigned char code);
  void putword(unsigned val);
  void puttri(unsigned long val);
  void putlong(unsigned long val);
  void putdelta(unsigned long val);
  void puttime();
  void put(int len, const unsigned char* c);
  void seek(long pos);

  virtual void error(const char* msg);
  virtual void warning(const char* msg);

  int unitsperquarter();

protected:
  const char *midiname_;
  FILE* f_;
  long trackpos_, curpos_, filesize_;
  int trackchannel_, trackcount_, lastcode_, endtrack_;

  unsigned char buf_[MIDI_BUFSIZE];
  int bufpos_, buflen_;

  unsigned long curdelta_;
  unsigned long curtime_;

  int clicks_;

  void flush();
};


class MidiCopy : public MidiRead
{
public:
  MidiCopy(const char* filename, FILE* f = 0);

  void setoutput(MidiWrite* dest);
  void stopoutput();
  MidiWrite* getoutput();

  void mapchannel(int channel, int newchannel);
  void ignorechannel(int channel);

  virtual void head(unsigned version, unsigned tracks, unsigned clicksperquarter);
  virtual void track(int trackno, long length, int channel);
  virtual void endtrack(int trackno);

  virtual void time(unsigned long ticks);

  virtual void event(int what, int len = 0, unsigned char* data = 0);

  virtual void meta(int what, int len, unsigned char* data);
    virtual void text(int what, int len, char* whattext, unsigned char* txt);
    virtual void end();
    virtual void prefixchannel(unsigned char channel);
    virtual void prefixport(unsigned char port);
    virtual void seqnumber(unsigned int seqno);
    virtual void smpteofs(int hour, int min, int sec, int frame, int fracframe);
    virtual void tact(int nom, int denom, int clicksperbeat, int notes32perbeat);
    virtual void tempo(unsigned long microsecperbeat);
    virtual void key(int signature, int isminor); // C=0, -7=7flats +7=7sharps
  virtual void program(int channel, int prg);
  virtual void control(int channel, int what, int val);
    virtual void highbank(int channel, int val);
    virtual void wheel(int channel, int val);
    virtual void breath(int channel, int val);
    virtual void foot(int channel, int val);
    virtual void portamentotime(int channel, int val);
    virtual void data(int channel, int val);
    virtual void volume(int channel, int val);
    virtual void balance(int channel, int val);
    virtual void expression(int channel, int val);
    virtual void lowbank(int channel, int val);
    virtual void hold(int channel, int val);
    virtual void reverb(int channel, int val);
    virtual void chorus(int channel, int val);
    virtual void datainc(int channel, int val);
    virtual void datadec(int channel, int val);
    virtual void lowrpn(int channel, int val);
    virtual void highrpn(int channel, int val);
    virtual void resetctrlrs(int channel, int val);
    virtual void allnotesoff(int channel, int val);
    virtual void noteon(int channel, int note, int vel);
    virtual void noteoff(int channel, int note, int vel);
  virtual void pitchbend(int channel, int val);
  virtual void polyaftertouch(int channel, int note, int val);
  virtual void aftertouch(int channel, int val);
  virtual void songpos(unsigned pos);
  virtual void songselect(unsigned char song);
  virtual void tunerequest();
  virtual void timingclock();
  virtual void start();
  virtual void cont();
  virtual void stop();
  virtual void activesense();
  virtual void sysex(int syslen, unsigned char* sysdata);
    virtual void gmreset();
    virtual void gsreset();
    virtual void gsexit();
    virtual void xgreset();

protected:
  MidiWrite* dest_;
  int mapchannel_[16];  // channel 0-15 or events are ignored for invalid channel
};

#endif
