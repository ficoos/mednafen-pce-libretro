#include "mednafen/mednafen-types.h"
#include "mednafen/mednafen.h"
#include "mednafen/git.h"
#include "mednafen/general.h"
#include "mednafen/mednafen-driver.h"

#include <iostream>
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>

#ifdef __CELLOS_LV2__
#include <sys/timer.h>
#endif

#include "thread.h"

// Stubs

void MDFND_Sleep(unsigned int time)
{
#ifdef __CELLOS_LV2__
   sys_timer_usleep(time * 1000);
#else
   usleep(time * 1000);
#endif
}

void MDFND_DispMessage(unsigned char *str)
{
   std::cerr << str;
}

void MDFND_Message(const char *str)
{
   std::cerr << str;
}

void MDFND_MidSync(const EmulateSpecStruct *)
{}

void MDFND_PrintError(const char* err)
{
   std::cerr << err;
}

MDFN_Thread *MDFND_CreateThread(int (*fn)(void *), void *data)
{
   return (MDFN_Thread*)sthread_create((void (*)(void*))fn, data);
}

void MDFND_WaitThread(MDFN_Thread *thr, int *val)
{
   sthread_join((sthread_t*)thr);

   if (val)
   {
      *val = 0;
      std::cerr << "WaitThread relies on return value." << std::endl;
   }
}

void MDFND_KillThread(MDFN_Thread *thr)
{
   std::cerr << "Killing a thread is a BAD IDEA!" << std::endl;
}

MDFN_Mutex *MDFND_CreateMutex()
{
   return (MDFN_Mutex*)slock_new();
}

void MDFND_DestroyMutex(MDFN_Mutex *lock)
{
   slock_free((slock_t*)lock);
}

int MDFND_LockMutex(MDFN_Mutex *lock)
{
   slock_lock((slock_t*)lock);
   return 0;
}

int MDFND_UnlockMutex(MDFN_Mutex *lock)
{
   slock_unlock((slock_t*)lock);
   return 0;
}

int MDFND_SendData(const void*, uint32) { return 0; }
int MDFND_RecvData(void *, uint32) { return 0; }
void MDFND_NetplayText(const uint8*, bool) {}
void MDFND_NetworkClose() {}
