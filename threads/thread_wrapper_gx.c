#include <ogcsys.h>
#include <gccore.h>
#define STACKSIZE 8*1024

#include "thread_wrapper_gx.h"
 
//imp
int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void*), void *arg)
{
	*thread = 0;
	return LWP_CreateThread(thread, start_routine, arg, 0, STACKSIZE, 64);
}
 
//int pthread_cancel(pthread_t thread);
 
int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
	return LWP_MutexInit(mutex, 0);
}

int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr)
{
   LWP_CondInit(cond);
}

int pthread_cond_signal(pthread_cond_t *cond)
{
   LWP_CondSignal(*(cond));
}

int pthread_cond_destroy(pthread_cond_t *cond)
{
   LWP_CondDestroy(*(cond));
}

int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
   LWP_CondWait(*(cond), *(mutex));
}
int pthread_mutex_destroy(pthread_mutex_t *mutex){ return LWP_MutexDestroy(*mutex);}
int pthread_join(pthread_t thread, void**retval) { return LWP_JoinThread(thread, NULL); }
 
int pthread_mutex_lock(pthread_mutex_t *mutex) { return LWP_MutexLock(*mutex); }
int pthread_mutex_trylock(pthread_mutex_t *mutex){ return LWP_MutexTryLock(*mutex);}
int pthread_mutex_unlock(pthread_mutex_t *mutex) { return LWP_MutexUnlock(*mutex); }
