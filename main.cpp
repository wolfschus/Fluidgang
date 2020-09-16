//============================================================================
// Name        : Fluidgang.cpp
// Author      : Wolfgang Schuster
// Version     : 0.03 16.09.2020
// Copyright   : Wolfgang Schuster
// Description : Fluidsynth MIDI for Linux
// License     : GNU General Public License v3.0
//============================================================================

#include <iostream>
#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>
#include <SDL/SDL_gfxPrimitives.h>
#include <SDL/SDL_mixer.h>
#include <SDL/SDL_image.h>
#include <SDL/SDL_rotozoom.h>
#include <string>
#include <vector>
#include <pthread.h>
#include "rtmidi/RtMidi.h"
#include <sqlite3.h>
#include <unistd.h>
#include <algorithm>
#include <sys/sysinfo.h>
#include <sys/time.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <fluidsynth.h>


#include "images/media-playback-start.xpm"
#include "images/help-info.xpm"
#include "images/system-shutdown.xpm"
#include "images/dialog-ok-apply.xpm"
#include "images/window-close.xpm"
#include "images/document-properties.xpm"
#include "images/go-up.xpm"
#include "images/go-down.xpm"

using namespace std;

char cstring[512];
int mode = 0;
int submode = 0;
int settingsmode = 0;
char aktprogram[16];
bool anzeige=true;

timeval start, stop;

struct cpuwerte{
	float idle;
	float usage;
};

cpuwerte oldcpu;
cpuwerte newcpu;
float cpuusage;
int anzahlcpu;
int cputimer = 0;
struct sysinfo memInfo;

// Fluidsynth
fluid_synth_t* fluid_synth;

RtMidiOut *midiout = new RtMidiOut();
RtMidiIn *midiin = new RtMidiIn();

SDL_Event CPUevent;

class ThreadCPUClass
{
public:
   void run()
   {
      pthread_t thread;

      pthread_create(&thread, NULL, entry, this);
   }

   void UpdateCPUTimer()
   {
      while(1)
	  {
    	  usleep(20000);
    	  SDL_PushEvent(&CPUevent);
    	  oldcpu.idle=newcpu.idle;
    	  oldcpu.usage=newcpu.usage;
    	  newcpu=get_cpuusage();
    	  cpuusage=((newcpu.usage - oldcpu.usage)/(newcpu.idle + newcpu.usage - oldcpu.idle - oldcpu.usage))*100;
    	  sysinfo (&memInfo);
	  }
   }

   cpuwerte get_cpuusage()
   {
		ifstream fileStat("/proc/stat");
		string line;

		cpuwerte cpuusage;

		while(getline(fileStat,line))
		{
			if(line.find("cpu ")==0)
			{
				istringstream ss(line);
				string token;

				vector<string> result;
				while(std::getline(ss, token, ' '))
				{
					result.push_back(token);
				}
				cpuusage.idle=atof(result[5].c_str()) + atof(result[6].c_str());
				cpuusage.usage=atof(result[2].c_str()) + atof(result[3].c_str()) + atof(result[4].c_str()) + atof(result[7].c_str()) + atof(result[8].c_str()) + atof(result[9].c_str()) + atof(result[10].c_str()) + atof(result[11].c_str());
			}
		}
		return cpuusage;

   }

private:
   static void * entry(void *ptr)
   {
      ThreadCPUClass *tcc = reinterpret_cast<ThreadCPUClass*>(ptr);
      tcc->UpdateCPUTimer();
      return 0;
   }
};

class WSButton
{

public:
	bool aktiv;
	SDL_Rect button_rect;
	SDL_Rect image_rect;
	SDL_Rect text_rect;
	SDL_Surface* button_image;
	string button_text;
	int button_width;
	int button_height;

	WSButton()
	{
		button_text="";
		button_image=NULL;
		button_width=2;
		button_height=2;
		aktiv = false;
		button_rect.x = 0+3;
		button_rect.y = 0+3;
		button_rect.w = 2*48-6;
		button_rect.h = 2*48-6;
	}

	WSButton(int posx, int posy, int width, int height, int scorex, int scorey, SDL_Surface* image, string text)
	{
		button_text=text;
		button_image=image;
		button_width=width;
		button_height=height;
		aktiv = false;
		button_rect.x = posx*scorex+3;
		button_rect.y = posy*scorey+3;
		button_rect.w = button_width*scorex-6;
		button_rect.h = button_height*scorey-6;
	}

	void show(SDL_Surface* screen, TTF_Font* font)
	{
		if(aktiv==true)
		{
			boxColor(screen, button_rect.x,button_rect.y,button_rect.x+button_rect.w,button_rect.y+button_rect.h,0x008F00FF);
		}
		else
		{
			boxColor(screen, button_rect.x,button_rect.y,button_rect.x+button_rect.w,button_rect.y+button_rect.h,0x8F8F8FFF);
		}

		if(button_image!=NULL)
		{
			image_rect.x = button_rect.x+2;
			image_rect.y = button_rect.y+1;
			SDL_BlitSurface(button_image, 0, screen, &image_rect);
		}
		if(button_text!="")
		{
			SDL_Surface* button_text_image;
			SDL_Color blackColor = {0, 0, 0};
			button_text_image = TTF_RenderText_Blended(font, button_text.c_str(), blackColor);
			text_rect.x = button_rect.x+button_rect.w/2-button_text_image->w/2;
			text_rect.y = button_rect.y+button_rect.h/2-button_text_image->h/2;
			SDL_BlitSurface(button_text_image, 0, screen, &text_rect);
//			SDL_FreeSurface(button_text_image);
		}
		return;
	}

	~WSButton()
	{
		return;
	}
};


void midiincallback( double deltatime, std::vector< unsigned char > *message, void *userData )
{
	unsigned int nBytes = message->size();

	for(unsigned int i=0;i<nBytes;i++)
	{
		cout << (int)message->at(i) << " ";
	}
	cout << endl;
	SDL_PushEvent(&CPUevent);

	if((int)message->at(0)>=144 and (int)message->at(0)<=159)
	{
		cout << "NoteOn " << (int)message->at(0)-144 << " " << (int)message->at(1) << " " << (int)message->at(2) << endl;
		fluid_synth_noteon(fluid_synth, (int)message->at(0)-144, (int)message->at(1), (int)message->at(2));
	}
	if((int)message->at(0)>=128 and (int)message->at(0)<=143)
	{
		cout << "NoteOff " << (int)message->at(0)-128 << " " << (int)message->at(1) << endl;
		fluid_synth_noteoff(fluid_synth, (int)message->at(0)-128, (int)message->at(1));
	}
	if((int)message->at(0)>=192 and (int)message->at(0)<=207)
	{
		cout << "ProgramChange " << (int)message->at(0)-192 << " " << (int)message->at(1) << endl;
		fluid_synth_program_change(fluid_synth, (int)message->at(0)-192, (int)message->at(1));
	}
	anzeige=true;								
	return;
}


bool CheckMouse(int mousex, int mousey, SDL_Rect Position)
{
	if( ( mousex > Position.x ) && ( mousex < Position.x + Position.w ) && ( mousey > Position.y ) && ( mousey < Position.y + Position.h ) )
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

int main(int argc, char* argv[])
{
	bool debug=false;
	bool fullscreen=false;
	
	// Argumentverarbeitung
	for (int i = 0; i < argc; ++i)
	{
		if(string(argv[i])=="--help")
		{
			cout << "Fluidgang" << endl;
			cout << "(c) 1987 - 2020 by Wolfgang Schuster" << endl;
			cout << "fluidgang --fullscreen = fullscreen" << endl;
			cout << "fluidgang --debug = debug" << endl;
			cout << "fluidgang --help = this screen" << endl;
			SDL_Quit();
			exit(0);
		}
		if(string(argv[i])=="--fullscreen")
		{
			fullscreen=true;
		}
		if(string(argv[i])=="--debug")
		{
			debug=true;
		}
	}

	ThreadCPUClass tcc;
	tcc.run();

	if(SDL_Init(SDL_INIT_VIDEO) == -1)
	{
		std::cerr << "Konnte SDL nicht initialisieren! Fehler: " << SDL_GetError() << std::endl;
		return -1;
	}
	SDL_Surface* screen;
	if(fullscreen==true)
	{
		screen = SDL_SetVideoMode(800, 480 , 32, SDL_DOUBLEBUF|SDL_FULLSCREEN);
	}
	else
	{
		screen = SDL_SetVideoMode(800, 480 , 32, SDL_DOUBLEBUF);
	}
	if(!screen)
	{
	    std::cerr << "Konnte SDL-Fenster nicht erzeugen! Fehler: " << SDL_GetError() << std::endl;
	    return -1;
	}
	int scorex = screen->w/36;
	int scorey = screen->h/21;

	if(TTF_Init() == -1)
	{
	    std::cerr << "Konnte SDL_ttf nicht initialisieren! Fehler: " << TTF_GetError() << std::endl;
	    return -1;
	}
	TTF_Font* fontbold = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 18);
	if(!fontbold)
	{
	    std::cerr << "Konnte Schriftart nicht laden! Fehler: " << TTF_GetError() << std::endl;
	    return -1;
	}
	TTF_Font* font = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 18);
	if(!font)
	{
	    std::cerr << "Konnte Schriftart nicht laden! Fehler: " << TTF_GetError() << std::endl;
	    return -1;
	}
	TTF_Font* fontsmall = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 16);
	if(!fontsmall)
	{
	    std::cerr << "Konnte Schriftart nicht laden! Fehler: " << TTF_GetError() << std::endl;
	    return -1;
	}
	TTF_Font* fontsmallbold = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 16);
	if(!fontsmallbold)
	{
	    std::cerr << "Konnte Schriftart nicht laden! Fehler: " << TTF_GetError() << std::endl;
	    return -1;
	}
	TTF_Font* fontextrasmall = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 10);
	if(!fontsmall)
	{
	    std::cerr << "Konnte Schriftart nicht laden! Fehler: " << TTF_GetError() << std::endl;
	    return -1;
	}
	SDL_WM_SetCaption("Fluidgang", "Fluidgang");

	bool run = true;

	// Fluidsynth
    fluid_settings_t* fluid_settings = new_fluid_settings();
    fluid_audio_driver_t* adriver;
	struct fluid_program {
    	unsigned int channel;
		unsigned int bank;
		unsigned int program;
	};
	fluid_program fptmp;
	vector<fluid_program> fluid_program_state;
    int sf2id;
    char* fluid_alsa_device;
    char* fluid_jack_id;
	unsigned int sfid = 0;
	unsigned int fsbank = 0;
	unsigned int fsprogram = 0;

    fluid_settings_setint(fluid_settings, "synth.polyphony", 128);
    fluid_synth = new_fluid_synth(fluid_settings);
    fluid_settings_setstr(fluid_settings, "audio.driver", "jack");
    fluid_settings_setstr(fluid_settings, "audio.jack.autoconnect", "yes");
    fluid_settings_setint(fluid_settings, "audio.period-size", 512);
    adriver = new_fluid_audio_driver(fluid_settings, fluid_synth);
    sf2id = fluid_synth_sfload(fluid_synth,"/usr/share/sounds/sf2/FluidR3_GM.sf2",true);
//    fluid_synth_program_change(fluid_synth, 0 , 0);
	fluid_synth_program_reset(fluid_synth);
    int fluid_nmid_chan = fluid_synth_count_midi_channels(fluid_synth);
//    fluid_settings_getstr(fluid_settings, "audio.alsa.device", &fluid_alsa_device);
//    fluid_settings_getstr(fluid_settings, "audio.jack.autoconnect", &fluid_jack_id);

// GM Instruments
	vector<char const *> gm_program_name;
	gm_program_name.push_back("Acoustic Piano");
	gm_program_name.push_back("Bright Piano");
	gm_program_name.push_back("Electric Grand Piano");
	gm_program_name.push_back("Honky-tonk Piano");
	gm_program_name.push_back("Electric Piano 1");
	gm_program_name.push_back("Electric Piano 2");
	gm_program_name.push_back("Harpsichord");
	gm_program_name.push_back("Clavi");
	gm_program_name.push_back("Celesta");
	gm_program_name.push_back("Glockenspiel");
	gm_program_name.push_back("Musical box");
	gm_program_name.push_back("Vibraphone");
	gm_program_name.push_back("Marimba");
	gm_program_name.push_back("Xylophone");
	gm_program_name.push_back("Tubular Bell");
	gm_program_name.push_back("Dulcimer");
	gm_program_name.push_back("Drawbar Organ");
	gm_program_name.push_back("Percussive Organ");
	gm_program_name.push_back("Rock Organ");
	gm_program_name.push_back("Church organ");
	gm_program_name.push_back("Reed organ");
	gm_program_name.push_back("Accordion");
	gm_program_name.push_back("Harmonica");
	gm_program_name.push_back("Tango Accordion");
	gm_program_name.push_back("Acoustic Guitar (nylon)");
	gm_program_name.push_back("Acoustic Guitar (steel)");
	gm_program_name.push_back("Electric Guitar (jazz)");
	gm_program_name.push_back("Electric Guitar (clean)");
	gm_program_name.push_back("Electric Guitar (muted)");
	gm_program_name.push_back("Overdriven Guitar");
	gm_program_name.push_back("Distortion Guitar");
	gm_program_name.push_back("Guitar harmonics");
	gm_program_name.push_back("Acoustic Bass");
	gm_program_name.push_back("Electric Bass (finger)");
	gm_program_name.push_back("Electric Bass (pick)");
	gm_program_name.push_back("Fretless Bass");
	gm_program_name.push_back("Slap Bass 1");
	gm_program_name.push_back("Slap Bass 2");
	gm_program_name.push_back("Synth Bass 1");
	gm_program_name.push_back("Synth Bass 2");
	gm_program_name.push_back("Violin");
	gm_program_name.push_back("Viola");
	gm_program_name.push_back("Cello");
	gm_program_name.push_back("Double bass");
	gm_program_name.push_back("Tremolo Strings");
	gm_program_name.push_back("Pizzicato Strings");
	gm_program_name.push_back("Orchestral Harp");
	gm_program_name.push_back("Timpani");
	gm_program_name.push_back("String Ensemble 1");
	gm_program_name.push_back("String Ensemble 2");
	gm_program_name.push_back("Synth Strings 1");
	gm_program_name.push_back("Synth Strings 2");
	gm_program_name.push_back("Voice Aahs");
	gm_program_name.push_back("Voice Oohs");
	gm_program_name.push_back("Synth Voice");
	gm_program_name.push_back("Orchestra Hit");
	gm_program_name.push_back("Trumpet");
	gm_program_name.push_back("Trombone");
	gm_program_name.push_back("Tuba");
	gm_program_name.push_back("Muted Trumpet");
	gm_program_name.push_back("French horn");
	gm_program_name.push_back("Brass Section");
	gm_program_name.push_back("Synth Brass 1");
	gm_program_name.push_back("Synth Brass 2");
	gm_program_name.push_back("Soprano Sax");
	gm_program_name.push_back("Alto Sax");
	gm_program_name.push_back("Tenor Sax");
	gm_program_name.push_back("Baritone Sax");
	gm_program_name.push_back("Oboe");
	gm_program_name.push_back("English Horn");
	gm_program_name.push_back("Bassoon");
	gm_program_name.push_back("Clarinet");
	gm_program_name.push_back("Piccolo");
	gm_program_name.push_back("Flute");
	gm_program_name.push_back("Recorder");
	gm_program_name.push_back("Pan Flute");
	gm_program_name.push_back("Blown Bottle");
	gm_program_name.push_back("Shakuhachi");
	gm_program_name.push_back("Whistle");
	gm_program_name.push_back("Ocarina");
	gm_program_name.push_back("Lead 1 (square)");
	gm_program_name.push_back("Lead 2 (sawtooth)");
	gm_program_name.push_back("Lead 3 (calliope)");
	gm_program_name.push_back("Lead 4 (chiff)");
	gm_program_name.push_back("Lead 5 (charang)");
	gm_program_name.push_back("Lead 6 (voice)");
	gm_program_name.push_back("Lead 7 (fifths)");
	gm_program_name.push_back("Lead 8 (bass + lead)");
	gm_program_name.push_back("Pad 1 (Fantasia)");
	gm_program_name.push_back("Pad 2 (warm)");
	gm_program_name.push_back("Pad 3 (polysynth)");
	gm_program_name.push_back("Pad 4 (choir)");
	gm_program_name.push_back("Pad 5 (bowed)");
	gm_program_name.push_back("Pad 6 (metallic)");
	gm_program_name.push_back("Pad 7 (halo)");
	gm_program_name.push_back("Pad 8 (sweep)");
	gm_program_name.push_back("FX 1 (rain)");
	gm_program_name.push_back("FX 2 (soundtrack)");
	gm_program_name.push_back("FX 3 (crystal)");
	gm_program_name.push_back("FX 4 (atmosphere)");
	gm_program_name.push_back("FX 5 (brightness)");
	gm_program_name.push_back("FX 6 (goblins)");
	gm_program_name.push_back("FX 7 (echoes)");
	gm_program_name.push_back("FX 8 (sci-fi)");
	gm_program_name.push_back("Sitar");
	gm_program_name.push_back("Banjo");
	gm_program_name.push_back("Shamisen");
	gm_program_name.push_back("Koto");
	gm_program_name.push_back("Kalimba");
	gm_program_name.push_back("Bagpipe");
	gm_program_name.push_back("Fiddle");
	gm_program_name.push_back("Shanai");
	gm_program_name.push_back("Tinkle Bell");
	gm_program_name.push_back("Agogo");
	gm_program_name.push_back("Steel Drums");
	gm_program_name.push_back("Woodblock");
	gm_program_name.push_back("Taiko Drum");
	gm_program_name.push_back("Melodic Tom");
	gm_program_name.push_back("Synth Drum");
	gm_program_name.push_back("Reverse Cymbal");
	gm_program_name.push_back("Guitar Fret Noise");
	gm_program_name.push_back("Breath Noise");
	gm_program_name.push_back("Seashore");
	gm_program_name.push_back("Bird Tweet");
	gm_program_name.push_back("Telephone Ring");
	gm_program_name.push_back("Helicopter");
	gm_program_name.push_back("Applause");
	gm_program_name.push_back("Gunshot");

// GM Drumkits
	char * gm_drum_kit[128];
	gm_drum_kit[0]="Standard Kit";
	gm_drum_kit[8]="Room Kit";
	gm_drum_kit[16]="Power Kit";
	gm_drum_kit[24]="Electronic Kit";
	gm_drum_kit[25]="TR-808 Kit";
	gm_drum_kit[32]="Jazz Kit";
	gm_drum_kit[40]="Brush Kit";
	gm_drum_kit[48]="Orchestra Kit";
	gm_drum_kit[56]="Sound FX Kit";
	gm_drum_kit[127]="Percussion";	

	
// GM Drums
	vector<char const *> gm_drum_name;
	gm_drum_name.push_back("Bass Drum 2");
	gm_drum_name.push_back("Bass Drum 1");
	gm_drum_name.push_back("Side Stick");
	gm_drum_name.push_back("Snare Drum 1");
	gm_drum_name.push_back("Hand Clap");
	gm_drum_name.push_back("Snare Drum 2");
	gm_drum_name.push_back("Low Tom 2");
	gm_drum_name.push_back("Closed Hi-hat");
	gm_drum_name.push_back("Low Tom 1");
	gm_drum_name.push_back("Pedal Hi-hat");
	gm_drum_name.push_back("Mid Tom 2");
	gm_drum_name.push_back("Open Hi-hat");
	gm_drum_name.push_back("Mid Tom 1");
	gm_drum_name.push_back("High Tom 2");
	gm_drum_name.push_back("Crash Cymbal 1");
	gm_drum_name.push_back("High Tom 1");
	gm_drum_name.push_back("Ride Cymbal 1");
	gm_drum_name.push_back("Chinese Cymbal");
	gm_drum_name.push_back("Ride Bell");
	gm_drum_name.push_back("Tambourine");
	gm_drum_name.push_back("Splash Cymbal");
	gm_drum_name.push_back("Cowbell");
	gm_drum_name.push_back("Crash Cymbal 2");
	gm_drum_name.push_back("Vibra Slap");
	gm_drum_name.push_back("Ride Cymbal 2");
	gm_drum_name.push_back("High Bongo");
	gm_drum_name.push_back("Low Bongo");
	gm_drum_name.push_back("Mute High Conga");
	gm_drum_name.push_back("Open High Conga");
	gm_drum_name.push_back("Low Conga");
	gm_drum_name.push_back("High Timbale");
	gm_drum_name.push_back("Low Timbale");
	gm_drum_name.push_back("High Agogo");
	gm_drum_name.push_back("Low Agogo");
	gm_drum_name.push_back("Cabasa");
	gm_drum_name.push_back("Maracas");
	gm_drum_name.push_back("Short Whistle");
	gm_drum_name.push_back("Long Whistle");
	gm_drum_name.push_back("Short Guiro");
	gm_drum_name.push_back("Long Guiro");
	gm_drum_name.push_back("Claves");
	gm_drum_name.push_back("High Wood Block");
	gm_drum_name.push_back("Low Wood Block");
	gm_drum_name.push_back("Mute Cuica");
	gm_drum_name.push_back("Open Cuica");
	gm_drum_name.push_back("Mute Triangle");
	gm_drum_name.push_back("Open Triangle");
	
	// [vor der Event-Schleife] In diesem Array merken wir uns, welche Tasten gerade gedrückt sind.
	bool keyPressed[SDLK_LAST];
	memset(keyPressed, 0, sizeof(keyPressed));
	SDL_EnableUNICODE(1);
	SDL_Rect textPosition;
	textPosition.x = 5;
	textPosition.y = 0;
	SDL_Surface* text = NULL;
	SDL_Color textColor = {225, 225, 225};
	SDL_Color blackColor = {0, 0, 0};

	SDL_Surface* info_image = IMG_ReadXPMFromArray(help_info_xpm);
	SDL_Surface* exit_image = IMG_ReadXPMFromArray(system_shutdown_xpm);
	SDL_Surface* ok_image = IMG_ReadXPMFromArray(dialog_ok_apply_xpm);
	SDL_Surface* cancel_image = IMG_ReadXPMFromArray(window_close_xpm);
	SDL_Surface* settings_image = IMG_ReadXPMFromArray(document_properties_xpm);
	SDL_Surface* start_image = IMG_ReadXPMFromArray(media_playback_start_xpm);
	SDL_Surface* up_image = IMG_ReadXPMFromArray(go_up_xpm);
	SDL_Surface* down_image = IMG_ReadXPMFromArray(go_down_xpm);

	char tmp[256];


	int selsetmididevice = 0;
	anzahlcpu=get_nprocs();
	sysinfo (&memInfo);

	SDL_Rect imagePosition;

	WSButton exit(15,19,2,2,scorex,scorey,exit_image,"");
	WSButton info(17,19,2,2,scorex,scorey,info_image,"");
	WSButton settings(19,19,2,2,scorex,scorey,settings_image,"");
	WSButton ok(16,19,2,2,scorex,scorey,ok_image,"");
	WSButton cancel(18,19,2,2,scorex,scorey,cancel_image,"");

	vector <WSButton> play;

	for(int i=0;i<16;i++)
	{
		WSButton tmp1(1,2+i,1,1,scorex,scorey,start_image,"");
		play.push_back(tmp1);
	}

	vector <WSButton> upbutton;

	for(int i=0;i<16;i++)
	{
		WSButton tmp1(2,2+i,1,1,scorex,scorey,up_image,"");
		upbutton.push_back(tmp1);
	}

	vector <WSButton> downbutton;

	for(int i=0;i<16;i++)
	{
		WSButton tmp1(3,2+i,1,1,scorex,scorey,down_image,"");
		downbutton.push_back(tmp1);
	}

	int mousex = 0;
	int mousey = 0;

	vector<unsigned char> message;
	vector<unsigned char> inmessage;
	vector<unsigned char> cc;
	message.push_back(0);
	message.push_back(0);
	message.push_back(0);

	vector<string> midioutname;
	size_t found;

	// Check available Midi Out ports.
	cout << "Midi Out" << endl;
	int onPorts = midiout->getPortCount();
	if ( onPorts == 0 )
	{
		cout << "No ports available!" << endl;
	}
	else
	{
		for(int i=0;i<onPorts;i++)
		{
			midioutname.push_back(midiout->getPortName(i));
			found = midiout->getPortName(i).find(":");
			cout << i << ": " << midiout->getPortName(i) << endl;
			cout << midiout->getPortName(i).substr(0,found) << endl;
		}
	}

	vector<string> midiinname;

	// Check available Midi In ports.
	cout << "Midi In" << endl;
	int inPorts = midiin->getPortCount();
	if ( inPorts == 0 )
	{
		cout << "No ports available!" << endl;
	}
	else
	{
		for(int i=0;i<inPorts;i++)
		{
			midiinname.push_back(midiin->getPortName(i));
			found = midiin->getPortName(i).find(":");
			
			cout << i << ": " << midiin->getPortName(i) << endl;
			cout << midiin->getPortName(i).substr(0,found) << endl;
		}
	}

	// MIDI IN Device
	if(0<inPorts)
	{
		midiin->openPort( 0 );
		midiin->setCallback( &midiincallback );
		// Don't ignore sysex, timing, or active sensing messages.
		midiin->ignoreTypes( false, false, false );
	}

	while(run)
	{
		if(anzeige==true)
		{
			SDL_FillRect(screen, NULL, 0x000000);
			boxColor(screen, 0,0,screen->w,1.5*scorey,0x00008FFF);
			SDL_FreeSurface(text);
			text = TTF_RenderText_Blended(fontbold, "*** Fluidgang ***", textColor);
			textPosition.x = screen->w/2-text->w/2;
			textPosition.y = 0.75*scorey-text->h/2;
			SDL_BlitSurface(text, 0, screen, &textPosition);

			if(mode==0)
			{

// CPU und RAM
				boxColor(screen, 0.2*scorex,0.25*scorey,0.4*scorex,1.25*scorey,0x2F2F2FFF);
				if(cpuusage>90)
				{
					boxColor(screen, 0.2*scorex,(0.25+(100-cpuusage)/100)*scorey,0.4*scorex,1.25*scorey,0xFF0000FF);
				}
				else if(cpuusage>80)
				{
					boxColor(screen, 0.2*scorex,(0.25+(100-cpuusage)/100)*scorey,0.4*scorex,1.25*scorey,0xFFFF00FF);
				}
				else
				{
					boxColor(screen, 0.2*scorex,(0.25+(100-cpuusage)/100)*scorey,0.4*scorex,1.25*scorey,0x00FF00FF);
				}

				boxColor(screen, 0.6*scorex,0.25*scorey,0.8*scorex,1.25*scorey,0x2F2F2FFF);
				if(float(memInfo.freeram+memInfo.bufferram+memInfo.sharedram)/float(memInfo.totalram)<0.1)
				{
					boxColor(screen, 0.6*scorex,(0.25+float(memInfo.freeram+memInfo.bufferram+memInfo.sharedram)/float(memInfo.totalram))*scorey,0.8*scorex,1.25*scorey,0xFF0000FF);
				}
				else if(float(memInfo.freeram+memInfo.bufferram+memInfo.sharedram)/float(memInfo.totalram)<0.2)
				{
					boxColor(screen, 0.6*scorex,(0.25+float(memInfo.freeram+memInfo.bufferram+memInfo.sharedram)/float(memInfo.totalram))*scorey,0.8*scorex,1.25*scorey,0xFFFF00FF);
				}
				else
				{
					boxColor(screen, 0.6*scorex,(0.25+float(memInfo.freeram+memInfo.bufferram+memInfo.sharedram)/float(memInfo.totalram))*scorey,0.8*scorex,1.25*scorey,0x00FF00FF);
				}

// Main Screen
				for(int i=0;i<16;i++)
				{
					SDL_FreeSurface(text);
					sprintf(tmp, "%d",i+1);
					text = TTF_RenderText_Blended(fontsmall, tmp, textColor);
					textPosition.x = 1*scorex-text->w-3;
					textPosition.y = (2.5+i)*scorey-text->h/2;
					SDL_BlitSurface(text, 0, screen, &textPosition);

					play[i].show(screen, fontsmall);
					upbutton[i].show(screen, fontsmall);
					downbutton[i].show(screen, fontsmall);
				}

				for(int i=0;i<16;i++)
			    {
					fluid_synth_get_program (fluid_synth, i, &sfid, &fsbank, &fsprogram);
					SDL_FreeSurface(text);
					sprintf(tmp, "%d",fsbank);
					text = TTF_RenderText_Blended(fontsmall, tmp, textColor);
					textPosition.x = 5*scorex-text->w/2;
					textPosition.y = (2.5+i)*scorey-text->h/2;
					SDL_BlitSurface(text, 0, screen, &textPosition);

					SDL_FreeSurface(text);
					sprintf(tmp, "%d",fsprogram);
					text = TTF_RenderText_Blended(fontsmall, tmp, textColor);
					textPosition.x = 7*scorex-text->w/2;
					textPosition.y = (2.5+i)*scorey-text->h/2;
					SDL_BlitSurface(text, 0, screen, &textPosition);

					SDL_FreeSurface(text);
					if(i==9)
					{
						sprintf(tmp, "%s",gm_drum_kit[fsprogram]);
					}
					else
					{
						sprintf(tmp, "%s",gm_program_name[fsprogram]);
					}
					text = TTF_RenderText_Blended(fontsmall, tmp, textColor);
					textPosition.x = 8*scorex+3;
					textPosition.y = (2.5+i)*scorey-text->h/2;
					SDL_BlitSurface(text, 0, screen, &textPosition);
			    }


// Exit Info

//				exit.show(screen, fontsmall);
//				info.show(screen, fontsmall);
//				settings.show(screen, fontsmall);

// Debug
				if(debug==true)
				{
				}
			}

			if(mode==1) // Info
			{
				SDL_FreeSurface(text);
				text = TTF_RenderText_Blended(fontbold, "(c) 1987-2020 by Wolfgang Schuster", textColor);
				textPosition.x = screen->w/2-text->w/2;
				textPosition.y = 2*scorey;
				SDL_BlitSurface(text, 0, screen, &textPosition);
				SDL_FreeSurface(text);
				text = TTF_RenderText_Blended(fontsmall, "GNU General Public License v3.0", textColor);
				textPosition.x = screen->w/2-text->w/2;
				textPosition.y = 3*scorey;
				SDL_BlitSurface(text, 0, screen, &textPosition);

				int i = 0;
				SDL_FreeSurface(text);
				text = TTF_RenderText_Blended(fontsmallbold, "MIDI OUT", textColor);
				textPosition.x = 2*scorex;
				textPosition.y = (5+i)*scorey-text->h/2;
				SDL_BlitSurface(text, 0, screen, &textPosition);
				i++;
				for(auto &mout: midioutname)
				{
					SDL_FreeSurface(text);
					sprintf(tmp, "%s",mout.c_str());
					text = TTF_RenderText_Blended(fontsmall, tmp, textColor);
					textPosition.x = 2*scorex;
					textPosition.y = (5+i)*scorey-text->h/2;
					SDL_BlitSurface(text, 0, screen, &textPosition);
					i++;
				}
				SDL_FreeSurface(text);
				text = TTF_RenderText_Blended(fontsmallbold, "MIDI IN", textColor);
				textPosition.x = 2*scorex;
				textPosition.y = (5+i)*scorey-text->h/2;
				SDL_BlitSurface(text, 0, screen, &textPosition);
				i++;
				for(auto &min: midiinname)
				{
					SDL_FreeSurface(text);
					sprintf(tmp, "%s",min.c_str());
					text = TTF_RenderText_Blended(fontsmall, tmp, textColor);
					textPosition.x = 2*scorex;
					textPosition.y = (5+i)*scorey-text->h/2;
					SDL_BlitSurface(text, 0, screen, &textPosition);
					i++;
				}
				
				ok.show(screen, fontsmall);
			}

			if(mode==2) // Exit
			{
				SDL_FreeSurface(text);
				text = TTF_RenderText_Blended(fontbold, "Really Exit ?", textColor);
				textPosition.x = screen->w/2-text->w/2;
				textPosition.y = 10*scorey;
				SDL_BlitSurface(text, 0, screen, &textPosition);

				ok.show(screen, fontsmall);
				cancel.show(screen, fontsmall);
			}

			if(mode==3) // Settings
			{
				SDL_FreeSurface(text);
				text = TTF_RenderText_Blended(fontbold, "Settings", textColor);
				textPosition.x = screen->w/2-text->w/2;
				textPosition.y = 2*scorey;
				SDL_BlitSurface(text, 0, screen, &textPosition);

				boxColor(screen, 0,3*scorey,screen->w,4*scorey,0x00008FFF);

				ok.show(screen, fontsmall);
				cancel.show(screen, fontsmall);
			}

			SDL_Flip(screen);
			anzeige=false;
		}

		// Wir holen uns so lange neue Ereignisse, bis es keine mehr gibt.
		SDL_Event event;
		if(SDL_WaitEvent(&event)!=0)
		{
			// Was ist passiert?
			switch(event.type)
			{
				case SDL_QUIT:
					run = false;
					break;
				case SDL_KEYDOWN:
					keyPressed[event.key.keysym.sym] = true;
					if(keyPressed[SDLK_ESCAPE])
					{
						run = false;        // Programm beenden.
					}
					anzeige=true;
					break;
				case SDL_MOUSEBUTTONDOWN:
			        if( event.button.button == SDL_BUTTON_LEFT )
			        {
			        	if(mode==0)
			        	{
							for(int i=0;i<16;i++)
							{
								if(CheckMouse(mousex, mousey, play[i].button_rect)==true)
								{
									fluid_synth_noteon(fluid_synth, i, 60, 100);
									fluid_synth_noteoff(fluid_synth, i, 60);
								}
								if(CheckMouse(mousex, mousey, upbutton[i].button_rect)==true)
								{
									fluid_synth_get_program (fluid_synth, i, &sfid, &fsbank, &fsprogram);
									if(i==9)
									{
										if(fsprogram==0)
											fluid_synth_program_change(fluid_synth, i,8);
										else if(fsprogram==8)
											fluid_synth_program_change(fluid_synth, i,16);
										else if(fsprogram==16)
											fluid_synth_program_change(fluid_synth, i,24);
										else if(fsprogram==24)
											fluid_synth_program_change(fluid_synth, i,25);
										else if(fsprogram==25)
											fluid_synth_program_change(fluid_synth, i,32);
										else if(fsprogram==32)
											fluid_synth_program_change(fluid_synth, i,40);
										else if(fsprogram==40)
											fluid_synth_program_change(fluid_synth, i,48);
										else if(fsprogram==48)
											fluid_synth_program_change(fluid_synth, i,56);
										else if(fsprogram==56)
											fluid_synth_program_change(fluid_synth, i,127);
									}
									else
									{
										if(fsprogram<127)
										{
											fluid_synth_program_change(fluid_synth, i,fsprogram+1);
										}
									}
									fluid_synth_get_program (fluid_synth, i, &sfid, &fsbank, &fsprogram);
								}
								if(CheckMouse(mousex, mousey, downbutton[i].button_rect)==true)
								{
									fluid_synth_get_program (fluid_synth, i, &sfid, &fsbank, &fsprogram);
									if(i==9)
									{
										if(fsprogram==8)
											fluid_synth_program_change(fluid_synth, i,0);
										else if(fsprogram==16)
											fluid_synth_program_change(fluid_synth, i,8);
										else if(fsprogram==24)
											fluid_synth_program_change(fluid_synth, i,16);
										else if(fsprogram==25)
											fluid_synth_program_change(fluid_synth, i,24);
										else if(fsprogram==32)
											fluid_synth_program_change(fluid_synth, i,25);
										else if(fsprogram==40)
											fluid_synth_program_change(fluid_synth, i,32);
										else if(fsprogram==48)
											fluid_synth_program_change(fluid_synth, i,40);
										else if(fsprogram==56)
											fluid_synth_program_change(fluid_synth, i,48);
										else if(fsprogram==127)
											fluid_synth_program_change(fluid_synth, i,56);
									}
									else
									{
										if(fsprogram>0)
										{
											fluid_synth_program_change(fluid_synth, i,fsprogram-1);
										}
									}
									fluid_synth_get_program (fluid_synth, i, &sfid, &fsbank, &fsprogram);
								}
							}
							if(CheckMouse(mousex, mousey, info.button_rect)==true)
							{
								mode=1;
							}
							else if(CheckMouse(mousex, mousey, exit.button_rect)==true)
							{
								mode=2;
							}
							else if(CheckMouse(mousex, mousey, settings.button_rect)==true)
							{
								mode=3;
							}
						}
			        	else if(mode==1)
			        	{
							if(CheckMouse(mousex, mousey, ok.button_rect)==true)
							{
								mode=0;
							}
			        	}
			        	else if(mode==2)
			        	{
							if(CheckMouse(mousex, mousey, ok.button_rect)==true)
							{
								run = false;        // Programm beenden.
							}
							if(CheckMouse(mousex, mousey, cancel.button_rect)==true)
							{
								mode=0;
							}
			        	}
			        	else if(mode==3)  // Settings
			        	{
							if(CheckMouse(mousex, mousey, ok.button_rect)==true)
							{
								mode=0;
							}
							if(CheckMouse(mousex, mousey, cancel.button_rect)==true)
							{
								mode=0;
							}
						}
					}
					anzeige=true;
					break;
				case SDL_MOUSEMOTION:
					mousex = event.button.x;
					mousey = event.button.y;
					anzeige=true;
					break;
				case SDL_MOUSEBUTTONUP:
			        if( event.button.button == SDL_BUTTON_LEFT )
			        {
					}
					anzeige=true;
					break;
			}
		}
	}
	
//cleanup:
	delete_fluid_audio_driver(adriver);
	delete_fluid_synth(fluid_synth);
	delete_fluid_settings(fluid_settings);
    
    SDL_Quit();
}
