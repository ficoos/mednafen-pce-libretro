#ifndef _THREAD_WRAP_GX_
#define _THREAD_WRAP_GX_

#include <ogcsys.h>
#include <gccore.h>
#include <ogc/cond.h>

#define STACKSIZE 8*1024

typedef lwp_t pthread_t;
typedef mutex_t pthread_mutex_t;
typedef void* pthread_mutexattr_t;
typedef int pthread_attr_t;
typedef cond_t pthread_cond_t;
typedef cond_t pthread_condattr_t;

static inline int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void*), void *arg)
{
   *thread = 0;
   return LWP_CreateThread(thread, start_routine, arg, 0, STACKSIZE, 64);
}

static inline int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
   return LWP_MutexInit(mutex, 0);
}

static inline int pthread_mutex_destroy(pthread_mutex_t *mutex){ return LWP_MutexDestroy(*mutex);}

static inline int pthread_mutex_lock(pthread_mutex_t *mutex) { return LWP_MutexLock(*mutex); }

static inline int pthread_mutex_unlock(pthread_mutex_t *mutex) { return LWP_MutexUnlock(*mutex); }

static inline int pthread_join(pthread_t thread, void**retval) { return LWP_JoinThread(thread, NULL); }

static inline int pthread_mutex_trylock(pthread_mutex_t *mutex){ return LWP_MutexTryLock(*mutex);}

static inline int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
   LWP_CondWait(*(cond), *(mutex));
}

static inline int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr)
{
   LWP_CondInit(cond);
}

static inline int pthread_cond_signal(pthread_cond_t *cond)
{
   LWP_CondSignal(*(cond));
}

static inline int pthread_cond_destroy(pthread_cond_t *cond)
{
   LWP_CondDestroy(*(cond));
}

#endif
