#include "gba/GBA.h"
#include "gba/agbprint.h"
#include "gba/Flash.h"
#include "gba/Cheats.h"
#include "gba/RTC.h"
#include "gba/Sound.h"
#ifdef ENABLE_GB
#include "gb/gb.h"
#include "gb/gbGlobals.h"
#include "gb/gbCheats.h"
#include "gb/gbSound.h"
#endif
#include "common/SoundDriver.h"
#include "System.h"
#include "Util.h"
#include "gba/GBAcpu.h" // for fake
#include <emscripten/emscripten.h>
#include <time.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

static bool hadFrame = false;

class JSSoundDriver : public SoundDriver {
	virtual bool init(long sampleRate) {
    return true;
  }
	virtual void pause() {}
	virtual void reset() {}
	virtual void resume() {}
	virtual void write(u16 * finalWave, int length) {
    EM_ASM_INT(vsysWriteSound($0, $1), finalWave, length);
  }
};

void log(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
}
bool systemPauseOnFrame() {
  return true;
}
//void systemGbPrint(u8 *,int,int,int,int,int);
void systemScreenCapture(int) {}
void systemDrawScreen() {
}
bool systemReadJoypads() {
  return true;
}
u32 systemReadJoypad(int which) {
  return EM_ASM_INT(return vsysReadJoypad($0), which);
}
u32 systemGetClock() {
  return time(NULL);
}
static uint64_t frameCount;
struct tm *systemTime() {
  time_t time = frameCount / 60;
  return gmtime(&time);
}
void systemShowSpeed(int s) {
  printf("speed: %d\n", s);
}
void systemMessage(int id, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
}
void systemSetTitle(const char *) {}
SoundDriver *systemSoundInit() {
  return new JSSoundDriver;
}
void systemOnWriteDataToSoundBuffer(const u16 * finalWave, int length) {}
void systemOnSoundShutdown() {}
void systemScreenMessage(const char *) {}
void systemUpdateMotionSensor() {}
int systemGetSensorX() { return 0; }
int systemGetSensorY() { return 0; }
bool systemCanChangeSoundQuality() { return false; }
void system10Frames(int) {}
void systemFrame() {
  hadFrame = true;
}
void systemGbBorderOn() {}
u16 systemColorMap16[0x10000];
u32 systemColorMap32[0x10000];
u16 systemGbPalette[24];
int systemRedShift = 0 + 3;
int systemGreenShift = 8 + 3;
int systemBlueShift = 16 + 3;
int systemColorDepth = 32;
int systemDebug;
int systemVerbose;
int systemFrameSkip;
int systemSaveUpdateCounter;
int systemSpeed;
struct EmulatedSystem emulator;
int emulating;

extern "C" {

EMSCRIPTEN_KEEPALIVE
void vbam_js_init(char *szFile) {
  flashSetSize(0x10000);
  // XX gb palette
  systemSaveUpdateCounter = SYSTEM_SAVE_NOT_UPDATED;

  IMAGE_TYPE type = utilFindType(szFile);
  int srcWidth, srcHeight;

#ifdef ENABLE_GB
  if(type == IMAGE_GB) {
    srcWidth = 160;
    srcHeight = 144;
    pix = (u8 *)malloc(4 * (srcWidth + 1) * (srcHeight + 2));
    if (!gbLoadRom(szFile)) {
      fprintf(stderr, "gbLoadRom failed\n");
      return;
    }
    gbGetHardwareType();

    soundInit();
    gbSoundSetSampleRate(EM_ASM_INT(return vsysInitSound(), 0));

    // used for the handling of the gb Boot Rom
    if (gbHardware & 5)
      gbCPUInit(gbBiosFileName, useBios);

    emulator = GBSystem;
    int size = gbRomSize, patchnum;
    if(size != gbRomSize) {
      extern bool gbUpdateSizes();
      gbUpdateSizes();
      gbReset();
    }
    gbReset();
  } else
#endif
  if(type == IMAGE_GBA) {
    srcWidth = 240;
    srcHeight = 160;
    pix = (u8 *)malloc(4 * (srcWidth + 1) * (srcHeight + 2));
    int size = CPULoadRom(szFile);
    if (!size) {
      fprintf(stderr, "CPULoadRom failed\n");
      return;
    }

    doMirroring(mirroringEnable);

    emulator = GBASystem;

    soundInit();
    //soundSetEnable(2);
    int rate = EM_ASM_INT(return vsysInitSound(), 42);
    soundSetSampleRate(rate);
    rtcEnable(true);

    CPUInit("", false);
    CPUReset();
  } else {
    fprintf(stderr, "no image?\n");
    return;
  }

  utilUpdateSystemColorMaps();
  EM_ASM_INT(vsysInitGraphics($0, $1, $2), srcWidth + 1, srcHeight + 2, pix);

#if 0
  fprintf(stderr, "--> FAKE IN\n");
  for(int i = 0; i < 500000; i++) {
    thumbExecute(true);
    armExecute(true);
  }
  fprintf(stderr, "--> FAKE OUT\n");
#endif
}

EMSCRIPTEN_KEEPALIVE
void vbam_js_main() {
  hadFrame = false;
  while (!hadFrame)
    emulator.emuMain(emulator.emuCount);
  frameCount++;
}

EMSCRIPTEN_KEEPALIVE
int vbam_js_save_state(char *mem, int size) {
  gzFile gzFile = utilMemGzOpen(mem, size, "w");

  if(gzFile == NULL) {
    return -1;
  }

  bool res = CPUWriteState(gzFile);
  return utilGzMemTell(gzFile);
}

EMSCRIPTEN_KEEPALIVE
bool vbam_js_load_state(char *mem, int size) {
  bool ok = emulator.emuReadMemState(mem, size);
  frameCount = *(uint64_t *) (mem + size + 4);
  fprintf(stderr, "VBAM frameCount: %llu\n", frameCount);
  return ok;
}

} // extern
