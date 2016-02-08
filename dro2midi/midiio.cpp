// midiio.cpp written by Günter Nagler 1995 (gnagler@ihm.tu-graz.ac.at)
#include "midiio.hpp"
#include <assert.h>
#ifdef __MSDOS__
#include <mem.h>
#endif
#include <string.h>
#include <stdlib.h>

static const char* copyright = "midiio v1.4 (c) 1995 by Günter Nagler (" __DATE__ ")";

int compress = 1;

#define NOTREALISTIC_PAUSE 0x1000000UL

// common sysex events
unsigned char sysex_gmreset[] = { 0xF0, 0x05, 0x7E, 0x7F, 0x09, 0x01, 0xF7 };
unsigned char sysex_gsreset[] = { 0xF0, 0x0A, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7F, 0x00, 0x41, 0xF7 };
unsigned char sysex_gsexit[] =  { 0xF0, 0x0A, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7F, 0x7F, 0x42, 0xF7 };
unsigned char sysex_xgreset[] = { 0xF0, 0x08, 0x43, 0x10, 0x4C, 0x00, 0x00, 0x7E, 0x00, 0xF7 };

static int issysex(const unsigned char* sysex, const unsigned char* sysdata, int syslen)
{
  if (*sysex == 0xF0)
    sysex++;
  while (syslen > 0)
  {
    if (*sysex != *sysdata)
      return 0;
    syslen--;
    if (*sysex == 0xF7)
      break;
    sysex++;
    sysdata++;
  }
  return (syslen != 0) ? 0 : 1;
}

static int sysexlen(unsigned const char* sysex)
{
int len = 0;

  len = 0;
  while (*sysex != 0xF7)
  {
    sysex++;
    len++;
  }
  return len+1; // incl. F7
}

// class MidiRead

const char* MidiRead::copyright()
{
  return (const char*)::copyright;
}

MidiRead::MidiRead(const char* filename, FILE* f)
{
  midiname_ = filename;
  if (f)
  {
    f_ = f;
    shouldclose_ = 0;
  }
  else
  {
    shouldclose_ = 1;
    if (!filename)
      f_ = 0;
    else
      f_ = fopen(filename, READ_BINARY);
  }

  buflen_ = 0;
  bufpos_ = 0;
  curpos_ = 0;
  pos_ = 0;
  curchannel_ = NOCHANNEL;
  curtime_ = 0;
  options_ = 0;
  if (f_)
  {
    fseek(f_, 0L, SEEK_END);
    filesize_ = ftell(f_);
    fseek(f_, 0L, SEEK_SET);
  }
  else
    filesize_ = 0;

  version_ = tracks_ = clicks_ = trackno_ = 0;
  tracklen_ = 0;
}


MidiRead::~MidiRead()
{
  if (f_ && shouldclose_)
    fclose(f_);
}

int MidiRead::runhead()
{
  if (!f_)
  {
    error("file not open");
    return 0;
  }
  seek(0);
  if (getlong() != MThd)
  {
    error("missing midi header MThd");
    return 0;
  }

  if (getlong() == 6)
  {
    version_ = getword();
    tracks_ = getword();
    clicks_ = getword();
    head(version_, tracks_, clicks_);
  }
  else
  {
    error("illegal midi header");
    return 0;
  }
  return 1;
}

int MidiRead::run()
{
  pos_ = curpos_;
  if (!runhead())
    return 0;
  pos_ = curpos_;
  for (trackno_ = 1; trackno_ <= tracks_; trackno_++)
    if (!runtrack(trackno_))
      return 0;
  if (curpos_ >= filesize_)
    percent(percent_ = 100);
  endmidi();
  return 1;
}

int MidiRead::runevent(long trackend)
{
int midicode;

  pos_ = curpos_;

  unsigned char *c = need(1);
  if (!c || c[0] >= 0x80 || lastcode_ < 0)
    midicode = getbyte();
  else
    midicode = lastcode_;

  if (midicode < 0)
    return 0;

  switch(midicode)
  {
  case 0xf0: // sysex
    {
      int syslen = scansysevent(trackend - curpos_);
      if (!syslen)
      {
	error("end of sysex not found or sysex too large");
	return 0;
      }
      unsigned char* sysdata = get(syslen);
      if ((options_ & OPTION_NOSYSEVENTS) == 0)
      {
	if (issysex(sysex_gmreset, sysdata, syslen))
	  gmreset();
	else if (issysex(sysex_gsreset, sysdata, syslen))
	  gsreset();
	else if (issysex(sysex_gsexit, sysdata, syslen))
	  gsexit();
	else if (issysex(sysex_xgreset, sysdata, syslen))
	  xgreset();
	else
	  sysex(syslen, sysdata);
      }
      else if ((options_ & OPTION_NOREALTIMEEVENTS) == 0)
	sysex(syslen, sysdata);
      else
	event(0xf0, syslen, sysdata);
    }
    break;
  case 0xf2:
  {
    c = get(2); if (!c) return 0;
    if ((options_ & OPTION_NOREALTIMEEVENTS) == 0)
      songpos((unsigned(c[1]) << 7) + unsigned(c[0]));
    else
      event(0xf2, 2, c);
    break;
  }
  case 0xf3:
    c = get(1); if (!c) return 0;
    if ((options_ & OPTION_NOREALTIMEEVENTS) == 0)
      songselect(*c);
    else
      event(0xf3, 1, c);
    break;
  case 0xf6:
    if ((options_ & OPTION_NOREALTIMEEVENTS) == 0)
      tunerequest();
    else
      event(0xf6);
    break;
  case 0xf8:
    if ((options_ & OPTION_NOREALTIMEEVENTS) == 0)
      timingclock();
    else
      event(0xf8);
    break;
  case 0xfa:
    if ((options_ & OPTION_NOREALTIMEEVENTS) == 0)
      start();
    else
      event(0xfa);
    break;
  case 0xfb:
    if ((options_ & OPTION_NOREALTIMEEVENTS) == 0)
      cont();
    else
      event(0xfb);
    break;
  case 0xfc:
    if ((options_ & OPTION_NOREALTIMEEVENTS) == 0)
      stop();
    else
      event(0xfc);
    break;
  case 0xfe:
    if ((options_ & OPTION_NOREALTIMEEVENTS) == 0)
      activesense();
    else
      event(0xfe);
    break;
  case 0xff:
    {
    int c;
    unsigned long endpos;
    int len;

      c = getbyte();
      len = (int)getdelta();
      endpos = curpos_ + len;
      if (options_ & OPTION_NOREALTIMEEVENTS)
      {
	len += curdeltalen_ + 1;
	seek(curpos_ - curdeltalen_ - 1);
	event(0xff, len, get(len));
      }
      else if (options_ & OPTION_NOMETAEVENTS)
	meta(c, len, get(len));
      else
	switch(c)
	{
	case 0:
	  if (len == 2)
	    seqnumber(getword());
	  else
	    meta(c, len, get(len));
	  break;
	case meta_text:
	  text(c, len, "text", get(len)); break;
	case meta_copyright:
	  text(c, len, "copyright", get(len));  break;
	case meta_trackname:
	  text(c, len, "trackname", get(len)); break;
	case meta_instrument:
	  text(c, len, "instrument", get(len)); break;
	case meta_lyric:
	  text(c, len, "lyric", get(len)); break;
	case meta_marker:
	  text(c, len, "marker", get(len)); break;
	case meta_cuepoint:
	  text(c, len, "cuepoint", get(len)); break;
	case 8:
	case 9:
	case 10:
	case 11:
	case 12:
	case 13:
	case 14:
	case 15:
	  text(c, len, 0, get(len)); break;
	case 0x20:
	  if (len == 1)
	    prefixchannel(getbyte());
	  else
	    meta(c, len, get(len));
	  break;
	case 0x21:
	  if (len == 1)
	    prefixport(getbyte());
	  else
	    meta(c, len, get(len));
	  break;
	case 0x2F:
	  end();
	  break;
	case 0x51:
	  tempo(gettri());
	  break;
	case 0x54:
	  if (len == 5)
	  {
	  unsigned char *s = get(len);

	    smpteofs(s[0], s[1], s[2], s[3], s[4]);
	  }
	  else
	    meta(c, len, get(len));
	  break;
	case 0x58:
	  if (len == 4)
	  {
	  int nom, clicksperbeat, notes32perbeat, log2denom;

	    nom = getbyte();
	    log2denom = getbyte();

	    clicksperbeat = getbyte();
	    notes32perbeat = getbyte();
	    tact(nom, 1 << log2denom, clicksperbeat, notes32perbeat);
	  }
	  else
	    meta(c, len, get(len));
	  break;
	case 0x59:
	  if (len == 2)
	  {
	  signed char s[2];

	    s[0] = (signed char)getbyte(); // sf
	    s[1] = (signed char)getbyte(); // mi
	    if (s[0] >= -7 && s[0] <= +7 && s[1] >= 0 && s[1] <= 1)
	      key(s[0], s[1]);
	    else
	      meta(c, len, (unsigned char*)s);
	  }
	  else
	    meta(c, len, get(len));
	  break;
	default:
	  meta(c, len, get(len));
	  break;
	}
      seek(endpos);
    }
    break;
  default:
    {
      if (midicode < 0)
	return 0;
      if (midicode < 0x80)
      {
      char msg[30];

	sprintf(msg, "illegal midi command %02X", midicode);
	error(msg);
	return 0;
      }
      int channel = midicode & 0x0F;
      int cmd = midicode & 0xF0;

      switch(cmd)
      {
      case 0x80:
      case 0x90:
	{
	unsigned char* c;

	  lastcode_ = midicode;
	  c = get(2); if (!c) return 0;
	  if (options_ & OPTION_NONOTEEVENTS)
	    event(midicode, 2, c);
	  else
	  {
	    if (cmd == 0x80 || c[1] == 0)
	      noteoff(channel, c[0], c[1]);
	    else
	      noteon(channel, c[0], c[1]);
	  }
	}
	break;
      case 0xA0:
	{
	unsigned char* p = get(2); if (!p) return 0;

	  lastcode_ = midicode;

	  if (options_ & OPTION_NOPOLYEVENTS)
	    event(midicode, 2, p);
	  else
	  {
	    polyaftertouch(channel, p[0], p[1]);
	  }
	}
	break;
      case 0xB0:
	{
	unsigned char* p = get(2); if (!p) return 0;

	  lastcode_ = midicode;
	  if (options_ & OPTION_NOCONTROLEVENTS)
	    event(midicode, 2, p);
	  else if (options_ & OPTION_NOCONTROLS)
	    control(channel, p[0], p[1]);
	  else
	    switch(p[0])
	    {
	    case ctrl_highbank: highbank(channel, p[1]); break;
	    case ctrl_wheel: wheel(channel, p[1]); break;
	    case ctrl_breath: breath(channel, p[1]); break;
	    case ctrl_foot: foot(channel, p[1]); break;
	    case ctrl_portamentotime: portamentotime(channel, p[1]); break;
	    case ctrl_data: data(channel, p[1]); break;
	    case ctrl_volume: volume(channel, p[1]); break;
	    case ctrl_balance: balance(channel, p[1]); break;
	    case ctrl_expression: expression(channel, p[1]); break;
	    case ctrl_lowbank: lowbank(channel, p[1]); break;
	    case ctrl_hold: hold(channel, p[1]); break;
	    case ctrl_reverb: reverb(channel, p[1]); break;
	    case ctrl_chorus: chorus(channel, p[1]); break;
	    case ctrl_datainc: datainc(channel, p[1]); break;
	    case ctrl_datadec: datadec(channel, p[1]); break;
	    case ctrl_lowrpn: lowrpn(channel, p[1]); break;
	    case ctrl_highrpn:
	    case ctrl_resetctrlrs: resetctrlrs(channel, p[1]); break;
	    case ctrl_allnotesoff: allnotesoff(channel, p[1]); break;
	      {
	      unsigned char *c = need(8);

		if (c &&
		  c[0] == 0 && c[1] == midicode && c[2] == ctrl_lowrpn && c[3] == 0 &&
		  c[4] == 0 && c[5] == midicode && c[6] == ctrl_data)
		{
		  c = get(8);
		  pitchbendrange(channel, c[7]);
		}
		else
		  highrpn(channel, p[1]);
		break;
	      }
	    default:
	      control(channel, p[0], p[1]);
	      break;
	    }
	  break;
	}

      case 0xC0:
	{
	unsigned char* p = get(1); if (!p) return 0;

	  lastcode_ = midicode;
	  if (options_ & OPTION_NOPROGRAMEVENTS)
	    event(midicode, 1, p);
	  else
	    program(channel, p[0]);
	}
	break;
      case 0xD0:
	{
	unsigned char* p = get(1); if (!p) return 0;

	  lastcode_ = midicode;
	  if (options_ & OPTION_NOAFTERTOUCHEVENTS)
	    event(midicode, 1, p);
	  else
	    aftertouch(channel, p[0]);
	}
	break;
      case 0xE0:
	{
	unsigned char* p = get(2); if (!p) return 0;
	unsigned val = unsigned(p[0]) + (unsigned(p[1]) << 7);

	  lastcode_ = midicode;
	  if (options_ & OPTION_NOPITCHBENDEVENTS)
	    event(midicode, 2, p);
	  else
	    pitchbend(channel, val);
	}
	break;
      default:
	{
	char msg[30];

	  sprintf(msg, "unexpected command byte %02X", midicode);
	  error(msg);
	  return 0;
	}
      }
    }
    break;
  }
  return (int)(curpos_ - pos_);
}

int MidiRead::runtrack(int trackno)
{
unsigned long trackpos = curpos_, trackend;

  curtime_ = 0;
  lastcode_ = -1;
  pos_ = curpos_;
  if (!f_)
  {
    error("file not open");
    return 0;
  }

  if (getlong() != MTrk)
  {
    error("missing midi track MTrk");
    return 0;
  }
  tracklen_ = getlong();
  track(trackno, tracklen_, curchannel_ = scanchannel(tracklen_));
  trackpos = curpos_;
  trackend = trackpos + tracklen_;
  lastcode_ = -1;
  if ((options_ & OPTION_NOEVENTS) == 0)
  while (curpos_ < trackpos + tracklen_)
  {
    int newpercent = (int)((curpos_ * 100) / filesize_);
    if (newpercent != percent_)
      percent(percent_ = newpercent);

    unsigned long delta = getdelta();
    if ( delta >= NOTREALISTIC_PAUSE )
      warning("Unrealistic large pause found");
    time(delta);
    curtime_ += delta;
    if (runevent(trackend) <= 0)
      return 0;
  }
  seek(trackend);
  pos_ = curpos_;
  endtrack(trackno);
  return 1;
}

void MidiRead::setchannel(int channel)
{
  assert(channel >= -1 && channel <= 15);
  curchannel_ = channel;
}

int MidiRead::scansysevent(unsigned long maxlen)
{
int n = sizeof(buf_);
unsigned char* c, *p;
long savepos = curpos_;
int len;

  if (maxlen < n)
    n = (int)maxlen;
  c = need(n);
  if (!c)
  {
    n = buflen_;
    c = need(n);
    if (!c)
      return 0;
  }

  if (c[0] < 0x80)
  {
    // short sysex 0..127 bytes
    len = c[0];
    if (n >= len+1)
    {
      if (c[len] == 0xF7)
	return len+1;
    }
  }
  else if (n >= 2 && c[1] < 0x80)
  {
    // 128..16383 bytes
    int len = (int(c[0] & 0x7f) << 7) + c[1];
    if (n >= len + 2)
    {
      if (c[len+1] == 0xF7)
	return len+2;
    }
  }
  // sysex events without length information?
  p = (unsigned char*)memchr(c, 0xF7, n);
  if (p)
    return (int)(p - c + 1);
  seek(savepos);
  return 0;
}

int MidiRead::scanchannel(unsigned long maxlen)
{
int n = 512;
unsigned char* c;
int firstchannel = NOCHANNEL;
int channel, code;
long savepos = curpos_, endpos;
int lastcode = -1;

  if (maxlen < n)
    n = (int)maxlen;
  c = need(n);
  if (!c)
    return -1;

  endpos = curpos_ + n;
  while (curpos_ < endpos)
  {
    channel = NOCHANNEL;
    getdelta();
    c = need(1);
    if (!c || *c >= 0x80 || lastcode < 0)
      code = getbyte();
    else
      code = lastcode;
    switch(code & 0xF0)
    {
    case 0xF0:
      switch(code)
      {
      case 0xFF:
	getbyte();
	get((int)getdelta());
	break;
      case 0xf0: // sysex
	{
	  int len = scansysevent(endpos-curpos_);
	  if (!get(len))
	    goto endscan;
	}
	break;
      case 0xf2: get(2); break;
      case 0xf3: getbyte(); break;
      case 0xf6:
      case 0xf8:
      case 0xfa:
      case 0xfb:
      case 0xfc:
      case 0xfe:
	break;
      default:
	goto endscan;
      }
      break;
    case 0x80:
    case 0x90:
    case 0xA0:
    case 0xB0:
    case 0xE0:
      channel = code & 15;
      get(2);
      break;
    case 0xC0:
    case 0xD0:
      channel = code & 15;
      getbyte();
      break;
    default:
      goto endscan;
    }
    if (code < 0xf0)
      lastcode = code;
    if (channel >= 0)
    {
      if (firstchannel < 0)
	firstchannel = channel;
      else if (channel != firstchannel)
      {
	firstchannel = MULTICHANNEL;
	break;
      }
    }
  }
endscan:
  seek(savepos);
  return firstchannel;
}

int MidiRead::getbyte()
{
unsigned char* c = get(1);

  if (c)
    return *c;
  return -1;
}

unsigned MidiRead::getword()
{
unsigned char* c = get(2);
unsigned n = 0;

  if (c)
  {
    n = *c++;
    n = (n << 8) + *c++;
  }
  return n;
}

unsigned long MidiRead::gettri()
{
unsigned char* c = get(3);
unsigned long n = 0;

  if (c)
  {
    n = *c++;
    n = (n << 8) + *c++;
    n = (n << 8) + *c++;
  }
  return n;
}

unsigned long MidiRead::getlong()
{
unsigned char* c = get(4);
unsigned long n = 0;

  if (c)
  {
    n = *c++;
    n = (n << 8) + *c++;
    n = (n << 8) + *c++;
    n = (n << 8) + *c++;
  }
  return n;
}

unsigned long MidiRead::getdelta()
{
unsigned long n = 0;
int i = 0, c;

  curdeltalen_ = 0;
  for (i = 0; i < 4; i++)
  {
    c = getbyte();
    if (c < 0)
    {
      error("unexpected end of file");
      return 0;
    }
    curdeltalen_++;
    n = (n << 7) + (c & 0x7f);
    if ((c & 0x80) == 0)
      break;
  }
  return n;
}

unsigned char* MidiRead::need(int n)
{
  assert(n >= 0);
  if (n == 0)
    return 0;
  if (n > buflen_)
  {
    if (!f_)
      return 0;
    if (n > sizeof(buf_))
      return 0;
    if (n > sizeof(buf_) - bufpos_)
    {
      // move to beginning of buf
      memmove(buf_, buf_ + bufpos_, buflen_);
      bufpos_ = 0;
    }
    // add new data at end
    if (sizeof(buf_) - bufpos_ - buflen_ > 0)
    {
      fseek(f_, curpos_+buflen_, SEEK_SET);
      int l = fread(buf_+bufpos_+buflen_, 1, sizeof(buf_) - bufpos_ - buflen_, f_);
      if (l > 0)
	buflen_ += l;
      else if (l == 0)
	;
    }
  }
  if (n <= buflen_)
    return buf_ + bufpos_;
  return 0;
}

unsigned char* MidiRead::get(int n)
{
unsigned char* s;

  s = need(n);
  if (s)
  {
    buflen_ -= n;
    bufpos_ += n;
    curpos_ += n;
  }
  else if (n > sizeof(buf_))
   warning("midi event larger than internal bufsize ignored");
  else if (n > 0)
  {
    error("unexpected end of file");
    exit(1);
  }
  return s;
}

void MidiRead::seek(long pos)
{
  if (pos == curpos_ || pos < 0)
    return;
  if (pos >= curpos_ - bufpos_ && pos < curpos_ - bufpos_ + buflen_)
  {
  int n = (int)(pos - curpos_ + bufpos_);

    if (n < bufpos_)
    {
      buflen_ += bufpos_ - n;
      bufpos_ -= bufpos_ - n;
    }
    else
    {
      buflen_ -= n - bufpos_;
      bufpos_ += n - bufpos_;
    }
    curpos_ = pos;
  }
  else
  {
    fseek(f_, curpos_ = pos, SEEK_SET);
    bufpos_ = buflen_ = 0;
  }
}

unsigned long MidiRead::microsec(unsigned long units, unsigned long msperbeat)
{
  assert(clicks_ != 0);   // call runhead() or run() first!

  if (units > msperbeat)
    return (units / clicks_) * msperbeat;
  else
    return units * (msperbeat / clicks_);
}


long MidiRead::units(unsigned long microsec, unsigned long msperbeat)
{
  assert(clicks_ != 0);   // call runhead() or run() first!
  assert(msperbeat > 0);  // invalid tempo!
  int clicks = clicks_;
  while ((msperbeat & 1) == 0)
  {
    if ((clicks & 1) == 0)
      clicks >>= 1;
    else if ((microsec & 1) == 0)
      microsec >>= 1;
    else
      break;
    msperbeat >>= 1;
  }
  if (microsec >= 0x10000L)
    return (microsec / msperbeat) * clicks;
  else
    return (microsec * clicks) / msperbeat;
}


static const char* GMProg[128] =
{
  "Piano", "BritePiano", "HammerPiano", "HonkeyTonk", "NewTines", "DigiPiano", "Harpsicord", "Clav",
  "Celesta", "Glocken", "MusicBox", "Vibes", "Marimba", "Xylophon", "Tubular", "Santur",
  "FullOrgan", "PercOrgan", "BX-3Organ", "ChurchPipe", "Positive", "Musette", "Harmonica", "Tango",
  "ClassicGtr", "A.Guitar", "JazzGuitar", "CleanGtr", "MuteGuitar", "OverDrive", "DistGuitar", "RockMonics",
  "JazzBass", "DeepBass", "PickBass", "FretLess", "SlapBass1", "SlapBass2", "SynthBass1", "SynthBass2",
  "Violin", "Viola", "Cello", "ContraBass", "TremoloStr", "Pizzicato", "Harp", "Timpani",
  "Marcato", "SlowString", "AnalogPad", "StringPad", "Choir", "DooVoice", "Voices", "OrchHit",
  "Trumpet", "Trombone", "Tuba", "MutedTrumpet", "FrenchHorn", "Brass", "SynBrass1", "SynBrass2",
  "SopranoSax", "AltoSax", "TenorSax", "BariSax", "SweetOboe", "EnglishHorn", "BasoonOboe", "Clarinet",
  "Piccolo", "Flute", "Recorder", "PanFlute", "Bottle", "Shakuhachi","Whistle", "Ocarina",
  "SquareWave", "SawWave", "SynCalinope", "SynChiff", "Charang", "AirChorus", "Rezzo4ths", "Bass&Lead",
  "Fantasia", "WarmPad", "PolyPad", "GhostPad", "BowedGlas", "MetalPad", "HaloPad", "Sweep",
  "IceRain", "SoundTrack", "Crystal", "Atmosphere", "Brightness", "Goblin", "EchoDrop", "StarTheme",
  "Sitar", "Banjo", "Shamisen", "Koto", "Kalimba","Scotland","Fiddle", "Shanai",
  "MetalBell", "Agogo", "SteelDrums", "Woodblock", "Taiko", "Tom", "SynthTom", "RevCymbal",
  "FretNoise", "NoiseChiff", "Seashore", "Birds", "Telephone", "Helicopter", "Stadium!!", "GunShot"
};

const char* MidiRead::notename(unsigned char note)
{
static char name[5];
char* s = name;

  switch(note % 12)
  {
  case 0: *s++ = 'c'; break;
  case 1: *s++ = 'c'; *s++ = '#'; break;
  case 2: *s++ = 'd'; break;
  case 3: *s++ = 'd'; *s++ = '#'; break;
  case 4: *s++ = 'e'; break;
  case 5: *s++ = 'f'; break;
  case 6: *s++ = 'f'; *s++ = '#'; break;
  case 7: *s++ = 'g'; break;
  case 8: *s++ = 'g'; *s++ = '#'; break;
  case 9: *s++ = 'a'; break;
  case 10: *s++ = 'a'; *s++ = '#'; break;
  case 11: *s++ = 'b'; break; // former 'h': German language only
  }

  sprintf(s, "%d", (note / 12)-1);  // octave  (assuming Piano C4 is 60)
  return (const char*) name;
}


const char* MidiRead::progname(int n, int channel)
{
static char defname[10] = "";

  if (channel == 9) // drum programs
  {
    switch(n)
    {
    case 0:    return "Dr1";
    case 0x10: return "Dr2";
    case 0x19: return "Dr3";
    case 0x20: return "Dr4";
    case 0x28: return "Dr5";
    case 0x40: return "Dr6";
    case 0x18: return "Dr7";
    case 0x30: return "Dr8";
    }
  }
  else if (n >= 0 && n <= 127)
    return GMProg[n];
def:
  sprintf(defname, "%d", n);
  return (const char*)defname;
}

void MidiRead::head(unsigned version, unsigned tracks, unsigned clicksperquarter)
{
}

void MidiRead::track(int trackno, long length, int channel)
{
}

void MidiRead::endtrack(int trackno)
{
}

void MidiRead::event(int what, int len, unsigned char* data)
{
}

void MidiRead::seqnumber(unsigned int seqno)
{
}

void MidiRead::smpteofs(int hour, int min, int sec, int frame, int fracframe)
{
}

void MidiRead::key(int signature, int isminor)
{
}

void MidiRead::prefixchannel(unsigned char channel)
{
}

void MidiRead::prefixport(unsigned char port)
{
}

void MidiRead::text(int what, int len, const char* whattext, const unsigned char* txt)
{
}

void MidiRead::meta(int what, int len, const unsigned char* data)
{
}

void MidiRead::end()
{
}

void MidiRead::tact(int nom, int denom, int v1, int v2)
{
}

void MidiRead::tempo(unsigned long ticks)
{
}

void MidiRead::program(int channel, int program)
{
}

void MidiRead::control(int channel, int what, int value)
{
}

void MidiRead::highbank(int channel, int val)
{
  control(channel, ctrl_highbank, val);
}

void MidiRead::wheel(int channel, int val)
{
  control(channel, ctrl_wheel, val);
}

void MidiRead::breath(int channel, int val)
{
  control(channel, ctrl_breath, val);
}

void MidiRead::foot(int channel, int val)
{
  control(channel, ctrl_foot, val);
}

void MidiRead::portamentotime(int channel, int val)
{
  control(channel, ctrl_portamentotime, val);
}

void MidiRead::data(int channel, int val)
{
  control(channel, ctrl_data, val);
}

void MidiRead::volume(int channel, int val)
{
  control(channel, ctrl_volume, val);
}

void MidiRead::balance(int channel, int val)
{
  control(channel, ctrl_balance, val);
}

void MidiRead::expression(int channel, int val)
{
  control(channel, ctrl_expression, val);
}

void MidiRead::lowbank(int channel, int val)
{
  control(channel, ctrl_lowbank, val);
}

void MidiRead::hold(int channel, int val)
{
  control(channel, ctrl_hold, val);
}

void MidiRead::reverb(int channel, int val)
{
  control(channel, ctrl_reverb, val);
}

void MidiRead::chorus(int channel, int val)
{
  control(channel, ctrl_chorus, val);
}

void MidiRead::datainc(int channel, int val)
{
  control(channel, ctrl_datainc, val);
}

void MidiRead::datadec(int channel, int val)
{
  control(channel, ctrl_datadec, val);
}

void MidiRead::lowrpn(int channel, int val)
{
  control(channel, ctrl_lowrpn, val);
}

void MidiRead::highrpn(int channel, int val)
{
  control(channel, ctrl_highrpn, val);
}

void MidiRead::resetctrlrs(int channel, int val)
{
  control(channel, ctrl_resetctrlrs, val);
}

void MidiRead::allnotesoff(int channel, int val)
{
  control(channel, ctrl_allnotesoff, val);
}

void MidiRead::pitchbendrange(int channel, int halfnotes)
{
  highrpn(channel, 0);
  time(0);
  lowrpn(channel, 0);
  time(0);
  data(channel, halfnotes);
}

void MidiRead::noteon(int channel, int note, int vel)
{
}

void MidiRead::noteoff(int channel, int note, int vel)
{
}

void MidiRead::time(unsigned long ticks)
{
}

void MidiRead::pitchbend(int channel, int val)
{
}

void MidiRead::polyaftertouch(int channel, int note, int val)
{
}

void MidiRead::aftertouch(int channel, int val)
{
}

void MidiRead::songpos(unsigned pos)
{
}

void MidiRead::songselect(unsigned char song)
{
}

void MidiRead::tunerequest()
{
}

void MidiRead::timingclock()
{
}

void MidiRead::start()
{
}

void MidiRead::cont()
{
}

void MidiRead::stop()
{
}

void MidiRead::activesense()
{
}

void MidiRead::sysex(int syslen, const unsigned char* sysdata)
{
}

void MidiRead::gmreset()
{
}

void MidiRead::gsreset()
{
}

void MidiRead::gsexit()
{
}

void MidiRead::xgreset()
{
}

void MidiRead::endmidi()
{
}

void MidiRead::percent(int perc)
{
}

void MidiRead::error(const char* msg)
{
  fprintf(stderr, "error: %s\n", msg);
}

void MidiRead::warning(const char* msg)
{
  fprintf(stderr, "warning: %s\n", msg);
}

FILE* MidiRead::getf()
{
  return f_;
}

// class MidiWrite

const char* MidiWrite::copyright()
{
  return (const char*)::copyright;
}

MidiWrite::MidiWrite(const char* filename)
{
  midiname_ = filename;
  if (midiname_)
    f_ = fopen(midiname_, WRITE_BINARY);
  else
    f_ = 0;
  trackpos_ = -1;
  curpos_ = 0;
  trackchannel_ = -1;

  bufpos_ = 0;
  buflen_ = 0;
  filesize_ = 0;
  trackcount_ = 0;
  curtime_ = curdelta_ = 0;
  lastcode_ = -1;
  clicks_ = 0;
}

MidiWrite::~MidiWrite()
{
  if (trackcount_ > 0)
  {
    seek(10);
    putword(trackcount_);
  }
  if (trackpos_ > 0)
    endtrack();
  flush();
  if (f_)
    fclose(f_);
}

void MidiWrite::head(int version, int tracks, unsigned clicksperquarter)
{
  seek(0);
  putlong(MThd);
  putlong(6);
  putword(version);
  putword(tracks);  // unknown
  putword(clicks_ = clicksperquarter);
}

void MidiWrite::track()
{
  if (trackpos_ > 0)
    endtrack();
  endtrack_ = 0;
  lastcode_ = -1;
  curtime_ = curdelta_ = 0;
  seek(trackpos_ = filesize_);
  putlong(MTrk);
  putlong(0); // unknown yet
  trackcount_++;
}

void MidiWrite::endtrack()
{
  seek(filesize_);
  if (!endtrack_)
     end();
  if (trackpos_ <= 0)
    return;
  seek(trackpos_+4);
  putlong(filesize_ - trackpos_ - 8);
  trackpos_ = 0;
}

void MidiWrite::event(int what, int len, const unsigned char* data)
{
  puttime();
  putcode(what);
  put(len, data);
}

void MidiWrite::prefixchannel(unsigned char channel)
{
  meta(0x20, 1, &channel);
}

void MidiWrite::prefixport(unsigned char port)
{
  meta(0x21, 1, &port);
}

void MidiWrite::text(int what, int len, const unsigned char* txt)
{
  if (len < 0)
    len = strlen((char *)txt);
  meta(what, len, txt);
}

void MidiWrite::meta(int what, int len, const unsigned char* data)
{
  puttime();
  putcode(0xff);
  putbyte(what);
  putdelta(len);
  put(len, data);
}

void MidiWrite::end()
{
  if (endtrack_)  // don't need end of track event twice
    return;
  endtrack_ = 1;
  meta(0x2f, 0, 0);
}

void MidiWrite::tact(int nom, int denom, int v1, int v2)
{
int log2denom;

  switch(denom)
  {
  case 1:    log2denom = 0; break;
  case 2:    log2denom = 1; break;
  case 4:    log2denom = 2; break;
  case 8:    log2denom = 3; break;
  case 16:   log2denom = 4; break;
  case 32:   log2denom = 5; break;
  case 64:   log2denom = 6; break;
  case 128:  log2denom = 7; break;
  case 256:  log2denom = 8; break;
  default:
    log2denom = 2;
    assert((1 << log2denom) == denom);
  }
  puttime();
  putcode(0xff);
  putbyte(0x58);
  putbyte(4);
  putbyte(nom);
  putbyte(log2denom);
  putbyte(v1);
  putbyte(v2);
}

void MidiWrite::seqnumber(unsigned int seqno)
{
  puttime();
  putcode(0xff);
  putbyte(0x00);
  putbyte(2);
  putword(seqno);
}

void MidiWrite::smpteofs(int hour, int min, int sec, int frame, int fracframe)
{
  puttime();
  putcode(0xff);
  putbyte(0x54);
  putbyte(5);
  putbyte(hour);
  putbyte(min);
  putbyte(sec);
  putbyte(frame);
  putbyte(fracframe);
}

void MidiWrite::key(int signature, int isminor)
{
  puttime();
  putcode(0xff);
  putbyte(0x59);
  putbyte(2);
  putbyte((signed char)signature);
  putbyte((signed char)isminor);
}

void MidiWrite::tempo(unsigned long ticks)
{
  puttime();
  putcode(0xff);
  putbyte(0x51);
  putbyte(3);
  puttri(ticks);
}

void MidiWrite::program(int channel, int prg)
{
  assert(channel >= 0 && channel < 16);
  puttime();
  putcode(0xC0 + channel);
  putbyte(prg);
}

void MidiWrite::control(int channel, int what, int val)
{
  assert(channel >= 0 && channel < 16);
  puttime();
  putcode(0xB0 + channel);
  putbyte(what);
  putbyte(val);
}

void MidiWrite::highbank(int channel, int val)
{
  control(channel, ctrl_highbank, val);
}

void MidiWrite::wheel(int channel, int val)
{
  control(channel, ctrl_wheel, val);
}

void MidiWrite::breath(int channel, int val)
{
  control(channel, ctrl_breath, val);
}

void MidiWrite::foot(int channel, int val)
{
  control(channel, ctrl_foot, val);
}

void MidiWrite::portamentotime(int channel, int val)
{
  control(channel, ctrl_portamentotime, val);
}

void MidiWrite::data(int channel, int val)
{
  control(channel, ctrl_data, val);
}

void MidiWrite::volume(int channel, int val)
{
  control(channel, ctrl_volume, val);
}

void MidiWrite::balance(int channel, int val)
{
  control(channel, ctrl_balance, val);
}

void MidiWrite::expression(int channel, int val)
{
  control(channel, ctrl_expression, val);
}

void MidiWrite::lowbank(int channel, int val)
{
  control(channel, ctrl_lowbank, val);
}

void MidiWrite::hold(int channel, int val)
{
  control(channel, ctrl_hold, val);
}

void MidiWrite::reverb(int channel, int val)
{
  control(channel, ctrl_reverb, val);
}

void MidiWrite::chorus(int channel, int val)
{
  control(channel, ctrl_chorus, val);
}

void MidiWrite::datainc(int channel, int val)
{
  control(channel, ctrl_datainc, val);
}

void MidiWrite::datadec(int channel, int val)
{
  control(channel, ctrl_datadec, val);
}

void MidiWrite::lowrpn(int channel, int val)
{
  control(channel, ctrl_lowrpn, val);
}

void MidiWrite::highrpn(int channel, int val)
{
  control(channel, ctrl_highrpn, val);
}

void MidiWrite::resetctrlrs(int channel, int val)
{
  control(channel, ctrl_resetctrlrs, val);
}

void MidiWrite::allnotesoff(int channel, int val)
{
  control(channel, ctrl_allnotesoff, val);
}

void MidiWrite::noteon(int channel, int note, int vel)
{
  assert(channel >= 0 && channel < 16);
  puttime();
  putcode(0x90+channel);
  putbyte(note);
  putbyte(vel);
}

void MidiWrite::noteoff(int channel, int note, int vel)
{
  assert(channel >= 0 && channel < 16);
  puttime();
  if (vel != 0 || lastcode_ < 0 || (lastcode_ & 0xF0) != 0x90)
    putcode(0x80+channel);
  else
    putcode(0x90+channel);  // vel == 0!
  putbyte(note);
  putbyte(vel);
}

void MidiWrite::time(unsigned long ticks)
{
  if ( ticks >= NOTREALISTIC_PAUSE )
    warning("generating unrealistic large pause");
  curdelta_ += ticks;
  curtime_ += ticks;
}

void MidiWrite::cleardelta()
{
  curtime_ -= curdelta_;
  curdelta_ = 0;
}

void MidiWrite::pitchbend(int channel, int val)
{
  assert(channel >= 0 && channel < 16);
  puttime();
  putcode(0xE0 + channel);
  putbyte(val & 0x7F);
  putbyte((val >> 7) & 0x7F);
}

void MidiWrite::polyaftertouch(int channel, int note, int val)
{
  assert(channel >= 0 && channel < 16);
  puttime();
  putcode(0xA0 + channel);
  putbyte(note);
  putbyte(val);
}

void MidiWrite::aftertouch(int channel, int val)
{
  assert(channel >= 0 && channel < 16);
  puttime();
  putcode(0xD0 + channel);
  putbyte(val);
}

void MidiWrite::songpos(unsigned pos)
{
  puttime();
  putcode(0xF2);
  putbyte(pos & 0x7F);
  putbyte((pos >> 7) & 0x7F);
}

void MidiWrite::songselect(unsigned char song)
{
  puttime();
  putcode(0xF3);
  putbyte(song);
}

void MidiWrite::tunerequest()
{
  puttime();
  putcode(0xf6);
}

void MidiWrite::timingclock()
{
  puttime();
  putcode(0xf8);
}

void MidiWrite::start()
{
  puttime();
  putcode(0xfa);
}

void MidiWrite::cont()
{
  puttime();
  putcode(0xfb);
}

void MidiWrite::stop()
{
  puttime();
  putcode(0xfc);
}

void MidiWrite::activesense()
{
  puttime();
  putcode(0xfe);
}

void MidiWrite::sysex(int syslen, const unsigned char* sysdata)
{
  puttime();
  putcode(0xf0);
  if (*sysdata == 0xF0)
  {
    syslen--;
    sysdata++;
  }
  put(syslen, sysdata);
}

void MidiWrite::gmreset()
{
  sysex(sysexlen(sysex_gmreset), sysex_gmreset);
}

void MidiWrite::xgreset()
{
  sysex(sysexlen(sysex_xgreset), sysex_xgreset);
}

void MidiWrite::gsreset()
{
  sysex(sysexlen(sysex_gsreset), sysex_gsreset);
}

void MidiWrite::gsexit()
{
  sysex(sysexlen(sysex_gsexit), sysex_gsexit);
}

void MidiWrite::pitchbendrange(int channel, int halfnotes)
{
  highrpn(channel, 0);
  lowrpn(channel, 0);
  data(channel, halfnotes);
}

void MidiWrite::putbyte(unsigned char val)
{
  put(1, &val);
}

void MidiWrite::putcode(unsigned char code)
{
int put;

  assert(code >= 0x80);

  if (compress)
    put = !(code == lastcode_ && code <= 0x9f);
  else
    put = 1;
  if (put)
    putbyte(code);
  lastcode_ = code;
}

static unsigned char c[4];

void MidiWrite::putword(unsigned val)
{
  c[1] = (unsigned char)(val & 0xff); val >>= 8;
  c[0] = (unsigned char)(val & 0xff);
  put(2, c);
}

void MidiWrite::puttri(unsigned long val)
{
  c[2] = (unsigned char)(val & 0xff); val >>= 8;
  c[1] = (unsigned char)(val & 0xff); val >>= 8;
  c[0] = (unsigned char)(val & 0xff);
  put(3, c);
}

void MidiWrite::putlong(unsigned long val)
{
  c[3] = (unsigned char)(val & 0xff); val >>= 8;
  c[2] = (unsigned char)(val & 0xff); val >>= 8;
  c[1] = (unsigned char)(val & 0xff); val >>= 8;
  c[0] = (unsigned char)(val & 0xff);
  put(4, c);
}

void MidiWrite::putdelta(unsigned long val)
{
  int i = 0, j = 3;
  while (i < 4)
  {
    c[j] = val & 0x7F;
    if (j < 3)
      c[j] |= 0x80;
    val >>= 7;
    i++;
    if (!val)
      break;
    j--;
  }
  put(i, c+j);
}

void MidiWrite::puttime()
{
  putdelta(curdelta_);
  curdelta_ = 0;
}

void MidiWrite::flush()
{
  if (buflen_ > 0)
  {
    fseek(f_, curpos_ - bufpos_, SEEK_SET);
    if (fwrite(buf_, buflen_, 1, f_) != 1)
      error("write error (maybe disk full)");
    assert(ftell(f_) == curpos_ - bufpos_ + buflen_);
    bufpos_ = buflen_ = 0;
  }
}

void MidiWrite::put(int len, const unsigned char* c)
{
  if (len <= 0)
    return;
  if (c == 0 || len > sizeof(buf_))
    return;
  if (sizeof(buf_) - bufpos_ < len)
    flush();
  memcpy(buf_+bufpos_, c, len);
  bufpos_+=len;
  if (bufpos_ > buflen_)
    buflen_ = bufpos_;
  curpos_+= len;
  if (curpos_ > filesize_)
    filesize_ = curpos_;
}

void MidiWrite::seek(long pos)
{
  assert(pos >= 0 && pos <= filesize_);
  if (curpos_ == pos)
    return;
  if (pos >= curpos_-bufpos_ && pos <= curpos_-bufpos_+buflen_)
  {
    bufpos_ = (int)(pos - (curpos_-bufpos_));
    curpos_ = pos;
    return;
  }
  flush();
  curpos_ = pos;
}

FILE* MidiWrite::getf()
{
  return f_;
}

void MidiWrite::error(const char* msg)
{
  fprintf(stderr, "midi write error: %s\n", msg);
}

void MidiWrite::warning(const char* msg)
{
  fprintf(stderr, "midi write warning: %s\n", msg);
}

int MidiWrite::unitsperquarter()
{
  return clicks_;
}

MidiCopy::MidiCopy(const char* filename, FILE* f) : MidiRead(filename, f)
{
  dest_ = NULL;
  for (int c = 0; c < 16; c++)
    mapchannel_[c] = c;        // no change
}

void MidiCopy::head(unsigned version, unsigned tracks, unsigned clicksperquarter)
{
  if (dest_)
    dest_->head(version, 0, clicksperquarter);
}

void MidiCopy::track(int trackno, long length, int channel)
{
  if (dest_)
    dest_->track();
}

void MidiCopy::endtrack(int trackno)
{
  if (dest_)
    dest_->endtrack();
}

void MidiCopy::event(int what, int len, unsigned char* data)
{
  if (dest_)
    dest_->event(what, len, data);
}


void MidiCopy::seqnumber(unsigned int seqno)
{
  if (dest_)
    dest_->seqnumber(seqno);
}

void MidiCopy::smpteofs(int hour, int min, int sec, int frame, int fracframe)
{
  if (dest_)
    dest_->smpteofs(hour, min, sec, frame, fracframe);
}

void MidiCopy::key(int signature, int isminor)
{
  if (dest_)
    dest_->key(signature, isminor);
}

void MidiCopy::prefixchannel(unsigned char channel)
{
  if (dest_ && mapchannel_[channel] >= 0)
    dest_->prefixchannel(mapchannel_[channel]);
}

void MidiCopy::prefixport(unsigned char port)
{
  if (dest_)
    dest_->prefixport(port);
}

void MidiCopy::text(int what, int len, char* whattext, unsigned char* txt)
{
  if (dest_)
    dest_->text(what, len, txt);
}

void MidiCopy::meta(int what, int len, unsigned char* data)
{
  if (dest_)
    dest_->meta(what, len, data);
}

void MidiCopy::end()
{
  if (dest_)
    dest_->end();
}

void MidiCopy::tact(int nom, int denom, int v1, int v2)
{
  if (dest_)
    dest_->tact(nom, denom, v1,v2);
}

void MidiCopy::tempo(unsigned long ticks)
{
  if (dest_)
    dest_->tempo(ticks);
}

void MidiCopy::program(int channel, int program)
{
  if (dest_ && mapchannel_[channel] >= 0)
    dest_->program(mapchannel_[channel], program);
}

void MidiCopy::control(int channel, int what, int value)
{
  if (dest_ && mapchannel_[channel] >= 0)
    dest_->control(mapchannel_[channel], what, value);
}

void MidiCopy::highbank(int channel, int val)
{
  if (dest_ && mapchannel_[channel] >= 0)
    dest_->highbank(mapchannel_[channel], val);
}

void MidiCopy::wheel(int channel, int val)
{
  if (dest_ && mapchannel_[channel] >= 0)
    dest_->wheel(mapchannel_[channel], val);
}

void MidiCopy::breath(int channel, int val)
{
  if (dest_ && mapchannel_[channel] >= 0)
    dest_->wheel(mapchannel_[channel], val);
}

void MidiCopy::foot(int channel, int val)
{
  if (dest_ && mapchannel_[channel] >= 0)
    dest_->foot(mapchannel_[channel], val);
}

void MidiCopy::portamentotime(int channel, int val)
{
  if (dest_ && mapchannel_[channel] >= 0)
    dest_->portamentotime(mapchannel_[channel], val);
}

void MidiCopy::data(int channel, int val)
{
  if (dest_ && mapchannel_[channel] >= 0)
    dest_->data(mapchannel_[channel], val);
}

void MidiCopy::volume(int channel, int val)
{
  if (dest_ && mapchannel_[channel] >= 0)
    dest_->volume(mapchannel_[channel], val);
}

void MidiCopy::balance(int channel, int val)
{
  if (dest_ && mapchannel_[channel] >= 0)
    dest_->balance(mapchannel_[channel], val);
}

void MidiCopy::expression(int channel, int val)
{
  if (dest_ && mapchannel_[channel] >= 0)
    dest_->expression(mapchannel_[channel], val);
}

void MidiCopy::lowbank(int channel, int val)
{
  if (dest_ && mapchannel_[channel] >= 0)
    dest_->lowbank(mapchannel_[channel], val);
}

void MidiCopy::hold(int channel, int val)
{
  if (dest_ && mapchannel_[channel] >= 0)
    dest_->hold(mapchannel_[channel], val);
}

void MidiCopy::reverb(int channel, int val)
{
  if (dest_ && mapchannel_[channel] >= 0)
    dest_->reverb(mapchannel_[channel], val);
}

void MidiCopy::chorus(int channel, int val)
{
  if (dest_ && mapchannel_[channel] >= 0)
    dest_->chorus(mapchannel_[channel], val);
}

void MidiCopy::datainc(int channel, int val)
{
  if (dest_ && mapchannel_[channel] >= 0)
    dest_->datainc(mapchannel_[channel], val);
}

void MidiCopy::datadec(int channel, int val)
{
  if (dest_ && mapchannel_[channel] >= 0)
    dest_->datadec(mapchannel_[channel], val);
}

void MidiCopy::lowrpn(int channel, int val)
{
  if (dest_ && mapchannel_[channel] >= 0)
    dest_->lowrpn(mapchannel_[channel], val);
}

void MidiCopy::highrpn(int channel, int val)
{
  if (dest_ && mapchannel_[channel] >= 0)
    dest_->highrpn(mapchannel_[channel], val);
}

void MidiCopy::resetctrlrs(int channel, int val)
{
  if (dest_ && mapchannel_[channel] >= 0)
    dest_->resetctrlrs(mapchannel_[channel], val);
}

void MidiCopy::allnotesoff(int channel, int val)
{
  if (dest_ && mapchannel_[channel] >= 0)
    dest_->allnotesoff(mapchannel_[channel], val);
}


void MidiCopy::noteon(int channel, int note, int vel)
{
  if (dest_ && mapchannel_[channel] >= 0)
    dest_->noteon(mapchannel_[channel], note, vel);
}

void MidiCopy::noteoff(int channel, int note, int vel)
{
  if (dest_ && mapchannel_[channel] >= 0)
    dest_->noteoff(mapchannel_[channel], note, vel);
}

void MidiCopy::time(unsigned long ticks)
{
  if (dest_)
    dest_->time(ticks);
}

void MidiCopy::pitchbend(int channel, int val)
{
  if (dest_ && mapchannel_[channel] >= 0)
    dest_->pitchbend(mapchannel_[channel],val);
}

void MidiCopy::polyaftertouch(int channel, int note, int val)
{
  if (dest_ && mapchannel_[channel] >= 0)
    dest_->polyaftertouch(mapchannel_[channel], note,  val);
}

void MidiCopy::aftertouch(int channel, int val)
{
  if (dest_ && mapchannel_[channel] >= 0)
    dest_->aftertouch(mapchannel_[channel],  val);
}

void MidiCopy::songpos(unsigned pos)
{
  if (dest_)
    dest_->songpos(pos);
}

void MidiCopy::songselect(unsigned char song)
{
  if (dest_)
    dest_->songselect(song);
}

void MidiCopy::tunerequest()
{
  if (dest_)
    dest_->tunerequest();

}

void MidiCopy::timingclock()
{
  if (dest_)
     dest_->timingclock();
}

void MidiCopy::start()
{
  if (dest_)
     dest_->start();
}

void MidiCopy::cont()
{
  if (dest_)
     dest_->cont();
}

void MidiCopy::stop()
{
  if (dest_)
     dest_->stop();
}

void MidiCopy::activesense()
{
  if (dest_)
     dest_->activesense();
}

void MidiCopy::sysex(int syslen, unsigned char* sysdata)
{
  if (dest_)
     dest_->sysex(syslen, sysdata);
}

void MidiCopy::xgreset()
{
  if (dest_)
     dest_->xgreset();
}

void MidiCopy::gmreset()
{
  if (dest_)
     dest_->gmreset();
}

void MidiCopy::gsreset()
{
  if (dest_)
     dest_->gsreset();
}

void MidiCopy::gsexit()
{
  if (dest_)
     dest_->gsexit();
}

void MidiCopy::setoutput(MidiWrite* dest)
{
  assert(dest != NULL && dest->getf() != NULL); // need a valid MidiWrite instance
  dest_ = dest;
}

void MidiCopy::stopoutput()
{
  dest_ = NULL; // owner is responsible for deleting the MidiWrite object
}

MidiWrite* MidiCopy::getoutput()
{
  return dest_;
}

void MidiCopy::mapchannel(int channel, int newchannel)
{
  assert(channel >= 0 && channel < 16);
  assert(newchannel >= 0 && newchannel < 16);
  mapchannel_[channel] = newchannel;
}

void MidiCopy::ignorechannel(int channel)
{
  assert(channel >= 0 && channel < 16);
  mapchannel_[channel] = -1;
}
